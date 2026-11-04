// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUBatchCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/ScopeExit.h"  // Issue #266 Phase 2.5: RAII scope guard for flag cleanup

// Include all commandlet headers
#include "CUValidateDesignPatternsCommandlet.h"
#include "CUBlueprintMetadataCommandlet.h"
#include "CUUniversalAssetMetadataCommandlet.h"
#include "CUAIMetadataCommandlet.h"  // Issue #848
#include "CUMaterialMetadataCommandlet.h"  // Issue #366
#include "CUCommandlet.h"

#include "CULog.h"

UCUBatchCommandlet::UCUBatchCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] Constructor called"));
}

int32 UCUBatchCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] Main() function entered - PARAMS: %s"), *Params);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  NarshaMCP Batch Commandlet v4.9.0"));  // Issue #848
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Running multiple commandlets in a single process"));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));

	// Step 1: Parse output directory
	FString OutputDir = FCUCommandletHelpers::ParseOutputDirectory(Params);
	if (!FCUCommandletHelpers::EnsureOutputDirectory(OutputDir))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CUBatch] Failed to create output directory: %s"), *OutputDir);
		return 1;
	}

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] Output directory: %s"), *OutputDir);

	// Issue #266 Phase 2.6: Create commandlet_running.flag with PID + RAII scope guard
	FString FlagPath = FPaths::Combine(OutputDir, TEXT("commandlet_running.flag"));

	// Write PID to flag file for process verification (Phase 2.6: PID-based stale detection)
	uint32 CurrentPID = FPlatformProcess::GetCurrentProcessId();
	FString FlagContent = FString::Printf(TEXT("%u"), CurrentPID);
	FFileHelper::SaveStringToFile(FlagContent, *FlagPath);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] Created commandlet_running.flag with PID %u"), CurrentPID);

	// RAII: Cleanup flag on ALL exit paths (normal, early return, exception)
	ON_SCOPE_EXIT
	{
		if (FPaths::FileExists(FlagPath))
		{
			IFileManager::Get().Delete(*FlagPath);
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] ??Cleaned up commandlet_running.flag (scope exit)"));
		}
	};

	// Step 2: Parse task list
	TArray<FString> Tasks;
	FCUCommandletHelpers::ParseTasks(Params, Tasks);

	// If no tasks specified, use default (all tasks)
	if (Tasks.IsEmpty())
	{
		Tasks = GetDefaultTasks();
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] No tasks specified, running all %d tasks"), Tasks.Num());
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] Running %d specified tasks"), Tasks.Num());

		// Validate all tasks
		for (const FString& Task : Tasks)
		{
			if (!IsValidTask(Task))
			{
				UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CUBatch] Invalid task name: %s"), *Task);
				UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CUBatch] Valid tasks: ValidatePatterns, BlueprintMetadata, UniversalAsset, AIMetadata, MaterialMetadata, NarshaMCP"));
				return 1;
			}
		}
	}

	// Step 3: Initialize AssetRegistry ONCE (shared by all tasks)
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] Initializing AssetRegistry (this may take 30-50 seconds)..."));
	FDateTime InitStartTime = FDateTime::UtcNow();

	IAssetRegistry& AssetRegistry = FCUCommandletHelpers::InitializeAssetRegistry(true);

	float InitSeconds = (FDateTime::UtcNow() - InitStartTime).GetTotalSeconds();
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] AssetRegistry initialization complete! (%.1fs)"), InitSeconds);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));

	// Step 4: Execute each task sequentially
	int32 TotalErrors = 0;
	int32 CompletedTasks = 0;
	int32 FailedTasks = 0;

	FDateTime BatchStartTime = FDateTime::UtcNow();

	for (int32 i = 0; i < Tasks.Num(); ++i)
	{
		const FString& TaskName = Tasks[i];

		// Log progress
		FCUCommandletHelpers::LogProgress(TaskName, i + 1, Tasks.Num());

		FDateTime TaskStartTime = FDateTime::UtcNow();

		// Execute task
		int32 TaskErrors = ExecuteTask(TaskName, OutputDir, AssetRegistry);

		float TaskSeconds = (FDateTime::UtcNow() - TaskStartTime).GetTotalSeconds();

		// Update counters
		TotalErrors += TaskErrors;

		if (TaskErrors == 0)
		{
			CompletedTasks++;
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] ??%s completed successfully (%.1fs)"), *TaskName, TaskSeconds);
		}
		else
		{
			FailedTasks++;
			UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[CUBatch] ??%s completed with %d errors (%.1fs)"), *TaskName, TaskErrors, TaskSeconds);
		}

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	}

	// Step 4.5 (tier-merge) removed in Issue #14193; the low-priority blueprint-metadata
	// split (CULowPriorityBlueprintMetadataCommandlet producer + LowPriorityBlueprintMetadata
	// task) removed in Issue #14271 — it produced a file nothing read.
	// The Rust Binary Parser is PRIMARY (Issue #1727 Phase 60) and owns the canonical
	// blueprint_metadata.json[.zst] that every loader reads; nothing consumed the old
	// merged/low-priority commandlet output. blueprint_metadata_commandlet.json.gz remains
	// the high-priority task's incremental base — the incremental rebuild carries every
	// existing entry forward (CUBlueprintMetadataIncrementalHelpers.cpp) — and must NOT be
	// merged with a separate tier (would duplicate entries each run). ControlRig/ModularRig
	// are covered by the Rust parser via the ControlRigBlueprint asset class, so no
	// low-priority deferral is needed.

	// Step 5: Report final status
	float TotalSeconds = (FDateTime::UtcNow() - BatchStartTime).GetTotalSeconds();

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Batch Execution Complete!"));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Total time: %.1fs"), TotalSeconds);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Initialization: %.1fs (1× shared)"), InitSeconds);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Work time: %.1fs"), TotalSeconds - InitSeconds);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Tasks completed: %d / %d"), CompletedTasks, Tasks.Num());
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Tasks failed: %d"), FailedTasks);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Total errors: %d"), TotalErrors);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Output directory: %s"), *OutputDir);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));

	// Performance estimate
	if (Tasks.Num() >= 3)
	{
		float EstimatedIndividualTime = Tasks.Num() * 50.0f; // Each task: ~50s initialization
		float TimeSavings = EstimatedIndividualTime - TotalSeconds;
		float SpeedupRatio = EstimatedIndividualTime / TotalSeconds;

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("?�� Performance Comparison:"));
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("   Individual execution (estimated): %.0fs"), EstimatedIndividualTime);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("   Batch execution (actual): %.0fs"), TotalSeconds);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("   Time saved: %.0fs (%.0f%%)"), TimeSavings, (TimeSavings / EstimatedIndividualTime) * 100.0f);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("   Speedup: %.1fx faster"), SpeedupRatio);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	}

	// PROFILING: Log detailed profiling results
	FCUCommandletHelpers::LogProfileResults();

	// PROFILING: Save profile data as JSON
	FString ProfilePath = FPaths::Combine(OutputDir,
		FString::Printf(TEXT("batch_profile_%s.json"),
		*FDateTime::Now().ToString(TEXT("%Y-%m-%d-%H-%M-%S"))));

	TSharedPtr<FJsonObject> ProfileData = FCUCommandletHelpers::GetProfileResultsAsJson();

	// Add batch execution metadata
	ProfileData->SetNumberField(TEXT("batch_total_time_sec"), TotalSeconds);
	ProfileData->SetNumberField(TEXT("initialization_sec"), InitSeconds);
	ProfileData->SetNumberField(TEXT("work_time_sec"), TotalSeconds - InitSeconds);
	ProfileData->SetNumberField(TEXT("tasks_completed"), CompletedTasks);
	ProfileData->SetNumberField(TEXT("tasks_failed"), FailedTasks);

	if (FCUCommandletHelpers::SaveJsonToFile(ProfilePath, ProfileData))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("?�� Profile saved: %s"), *ProfilePath);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	}

	// Issue #266 Phase 2.5: Flag cleanup handled by ON_SCOPE_EXIT (removed duplicate code)
	// ON_SCOPE_EXIT will automatically delete commandlet_running.flag when function exits

	return TotalErrors;
}

int32 UCUBatchCommandlet::ExecuteTask(const FString& TaskName, const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
	// Dispatch to appropriate commandlet's static function
	// Note: These static functions will be implemented in Phase 3

	if (TaskName == TEXT("ValidatePatterns"))
	{
		return UCUValidateDesignPatternsCommandlet::RunValidation(OutputDir, AssetRegistry);
	}
	else if (TaskName == TEXT("BlueprintMetadata"))
	{
		return UCUBlueprintMetadataCommandlet::RunExtractionStatic(OutputDir, AssetRegistry);
	}
	else if (TaskName == TEXT("UniversalAsset"))
	{
		return UCUUniversalAssetMetadataCommandlet::RunUniversalExtraction(OutputDir, AssetRegistry);
	}
	else if (TaskName == TEXT("AIMetadata"))  // Issue #848
	{
		return UCUAIMetadataCommandlet::RunAIExtraction(OutputDir, AssetRegistry);
	}
	else if (TaskName == TEXT("MaterialMetadata"))  // Issue #366
	{
		return UCUMaterialMetadataCommandlet::RunExtractionStatic(OutputDir, AssetRegistry);
	}
	else if (TaskName == TEXT("NarshaMCP"))
	{
		return UCUCommandlet::RunGASExtraction(OutputDir, AssetRegistry);
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CUBatch] Unknown task: %s"), *TaskName);
		return 1;
	}
}

TArray<FString> UCUBatchCommandlet::GetDefaultTasks() const
{
	// All 6 tasks in recommended execution order (Issue #848: AIMetadata, Issue #366: MaterialMetadata)
	// Order: Fastest to slowest (to see results sooner)
	TArray<FString> Tasks = {
		TEXT("NarshaMCP"),                      // ~25s
		TEXT("AIMetadata"),                     // ~30s (Issue #848: StateTree + BehaviorTree)
		TEXT("MaterialMetadata"),               // ~35s (Issue #366)
		TEXT("ValidatePatterns"),               // ~40s
		TEXT("UniversalAsset"),                 // ~50s
		TEXT("BlueprintMetadata")               // ~75s (all Blueprints incl. ControlRig/ModularRig)
	};

	// Issue #722 Phase 97: Skip Blueprint tasks if Binary Parser handled them
	// When UECODEGEN_SKIP_BLUEPRINT=1, Binary Parser already generated blueprint_metadata.json.gz
	// This allows Batch Commandlet to run other tasks (Material, AI, etc.) without duplicating Blueprint work
	FString SkipBlueprint = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_SKIP_BLUEPRINT"));
	if (SkipBlueprint == TEXT("1"))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] UECODEGEN_SKIP_BLUEPRINT=1 - Skipping Blueprint task (Binary Parser active)"));
		Tasks.Remove(TEXT("BlueprintMetadata"));
	}

	// Issue #1593: Skip Material tasks if Binary Parser handled them
	// When UECODEGEN_SKIP_MATERIAL=1, Binary Parser already generated material_metadata.json.gz
	FString SkipMaterial = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_SKIP_MATERIAL"));
	if (SkipMaterial == TEXT("1"))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CUBatch] UECODEGEN_SKIP_MATERIAL=1 ??Skipping Material tasks (Binary Parser active)"));
		Tasks.Remove(TEXT("MaterialMetadata"));
	}

	return Tasks;
}

bool UCUBatchCommandlet::IsValidTask(const FString& TaskName) const
{
	TArray<FString> ValidTasks = {
		TEXT("ValidatePatterns"),
		TEXT("BlueprintMetadata"),
		TEXT("UniversalAsset"),
		TEXT("AIMetadata"),  // Issue #848
		TEXT("MaterialMetadata"),  // Issue #366
		TEXT("NarshaMCP")
	};

	return ValidTasks.Contains(TaskName);
}
