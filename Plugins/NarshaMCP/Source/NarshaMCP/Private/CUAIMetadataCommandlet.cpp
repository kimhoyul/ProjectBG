// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUAIMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "BlueprintMetadata/AIExtractors.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

// StateTree includes
#if WITH_EDITOR && WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "StateTree.h"
#endif

// BehaviorTree includes
#if WITH_EDITOR
#include "BehaviorTree/BehaviorTree.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPAIMeta, Log, All);

UCUAIMetadataCommandlet::UCUAIMetadataCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// Override CommandletBase::RunExtraction()
int32 UCUAIMetadataCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
	return RunAIExtraction(OutputDir, Registry);
}

// Static method for batch commandlet
int32 UCUAIMetadataCommandlet::RunAIExtraction(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Running AI asset extraction (StateTree + BehaviorTree)..."));

#if WITH_EDITOR
	// ============================================================================
	// MAJOR #2: Input Validation
	// ============================================================================
	if (OutputDir.IsEmpty())
	{
		UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Output directory is empty"));
		return 1;
	}

	if (!FPaths::DirectoryExists(OutputDir))
	{
		UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Output directory does not exist: %s"), *OutputDir);
		return 1;
	}

	// CRITICAL #4: Performance Profiling - Total timer
	FDateTime TotalStartTime = FDateTime::UtcNow();

	// Phase 1: Find StateTree Assets
	#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
	FDateTime Phase1StartTime = FDateTime::UtcNow();
	FARFilter StateTreeFilter;
	StateTreeFilter.ClassPaths.Add(UStateTree::StaticClass()->GetClassPathName());
	StateTreeFilter.bRecursiveClasses = false;

	TArray<FAssetData> StateTreeAssetList;
	AssetRegistry.GetAssets(StateTreeFilter, StateTreeAssetList);

	float Phase1Time = (FDateTime::UtcNow() - Phase1StartTime).GetTotalSeconds();
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Found %d StateTree assets (%.3fs)"), StateTreeAssetList.Num(), Phase1Time);
	#else
	TArray<FAssetData> StateTreeAssetList;
	float Phase1Time = 0.0f;
	UE_LOG(LogNarshaMCPAIMeta, Display,
		TEXT("[AIMetadata] StateTree extraction unavailable: enable StateTree and PropertyBindingUtils"));
	#endif

	// Phase 2: Find BehaviorTree Assets
	FDateTime Phase2StartTime = FDateTime::UtcNow();
	FARFilter BehaviorTreeFilter;
	BehaviorTreeFilter.ClassPaths.Add(UBehaviorTree::StaticClass()->GetClassPathName());
	BehaviorTreeFilter.bRecursiveClasses = false;

	TArray<FAssetData> BehaviorTreeAssetList;
	AssetRegistry.GetAssets(BehaviorTreeFilter, BehaviorTreeAssetList);

	float Phase2Time = (FDateTime::UtcNow() - Phase2StartTime).GetTotalSeconds();
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Found %d BehaviorTree assets (%.3fs)"), BehaviorTreeAssetList.Num(), Phase2Time);

	// Phase 3: Extract StateTree Metadata (reuse AIExtractors)
	#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
	FDateTime Phase3StartTime = FDateTime::UtcNow();
	TSharedPtr<FJsonObject> StateTreesJson = MakeShareable(new FJsonObject);
	int32 StateTreeCount = 0;
	int32 FailedStateTreeCount = 0;  // CRITICAL #1: Track failures

	int32 TotalStateTrees = StateTreeAssetList.Num();
	for (int32 i = 0; i < TotalStateTrees; i++)
	{
		const FAssetData& AssetData = StateTreeAssetList[i];

		// MAJOR #8: Progress reporting (every 100 assets or every 10%)
		if (TotalStateTrees > 100 && i > 0 && (i % FMath::Max(TotalStateTrees / 10, 100) == 0))
		{
			UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Progress: %d / %d StateTrees (%.1f%%)"),
				i, TotalStateTrees, (float)i / TotalStateTrees * 100.0f);
		}

		// CRITICAL #1: Error handling for asset loading
		UStateTree* StateTree = Cast<UStateTree>(AssetData.GetAsset());
		if (!StateTree)
		{
			UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Failed to load StateTree asset: %s"),
				*AssetData.GetObjectPathString());
			FailedStateTreeCount++;
			continue;
		}

		// MAJOR #5: Validate asset name and handle duplicates
		FString AssetName = StateTree->GetName();
		if (AssetName.IsEmpty())
		{
			UE_LOG(LogNarshaMCPAIMeta, Warning, TEXT("[AIMetadata] ⚠️ StateTree has empty name, using object path: %s"),
				*AssetData.GetObjectPathString());
			AssetName = FPaths::GetBaseFilename(AssetData.GetObjectPathString());
		}

		// Check for duplicate names
		if (StateTreesJson->HasField(AssetName))
		{
			UE_LOG(LogNarshaMCPAIMeta, Warning, TEXT("[AIMetadata] ⚠️ Duplicate StateTree name detected: %s (appending index)"),
				*AssetName);
			int32 Index = 1;
			FString UniqueAssetName;
			do {
				UniqueAssetName = FString::Printf(TEXT("%s_%d"), *AssetName, Index++);
			} while (StateTreesJson->HasField(UniqueAssetName));
			AssetName = UniqueAssetName;
		}

		// CRITICAL #2: Validate AIExtractors return value
		TSharedPtr<FJsonObject> StateTreeMetadata = AIExtractors::ExtractStateTreeMetadata(StateTree, AssetData);
		if (StateTreeMetadata.IsValid())
		{
			StateTreesJson->SetObjectField(AssetName, StateTreeMetadata);
			StateTreeCount++;
		}
		else
		{
			UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Failed to extract StateTree metadata: %s"), *AssetName);
			FailedStateTreeCount++;
		}
	}

	float Phase3Time = (FDateTime::UtcNow() - Phase3StartTime).GetTotalSeconds();
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Extracted %d / %d StateTrees (%.3fs)"),
		StateTreeCount, StateTreeAssetList.Num(), Phase3Time);

	// MAJOR #4: Asset count mismatch detection
	if (StateTreeCount < StateTreeAssetList.Num())
	{
		int32 MissingCount = StateTreeAssetList.Num() - StateTreeCount;
		UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ⚠️ Failed to extract %d / %d StateTrees (%d failures)"),
			MissingCount, StateTreeAssetList.Num(), MissingCount);
	}
	#else
	TSharedPtr<FJsonObject> StateTreesJson = MakeShared<FJsonObject>();
	int32 StateTreeCount = 0;
	int32 FailedStateTreeCount = 0;
	float Phase3Time = 0.0f;
	StateTreesJson->SetStringField(TEXT("error_code"), TEXT("state_tree_plugin_unavailable"));
	StateTreesJson->SetStringField(TEXT("hint"),
		TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild."));
	#endif

	// Phase 4: Extract BehaviorTree Metadata (reuse AIExtractors)
	FDateTime Phase4StartTime = FDateTime::UtcNow();
	TSharedPtr<FJsonObject> BehaviorTreesJson = MakeShareable(new FJsonObject);
	int32 BehaviorTreeCount = 0;
	int32 FailedBehaviorTreeCount = 0;  // CRITICAL #1: Track failures

	int32 TotalBehaviorTrees = BehaviorTreeAssetList.Num();
	for (int32 i = 0; i < TotalBehaviorTrees; i++)
	{
		const FAssetData& AssetData = BehaviorTreeAssetList[i];

		// MAJOR #8: Progress reporting
		if (TotalBehaviorTrees > 100 && i > 0 && (i % FMath::Max(TotalBehaviorTrees / 10, 100) == 0))
		{
			UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Progress: %d / %d BehaviorTrees (%.1f%%)"),
				i, TotalBehaviorTrees, (float)i / TotalBehaviorTrees * 100.0f);
		}

		// CRITICAL #1: Error handling for asset loading
		UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(AssetData.GetAsset());
		if (!BehaviorTree)
		{
			UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Failed to load BehaviorTree asset: %s"),
				*AssetData.GetObjectPathString());
			FailedBehaviorTreeCount++;
			continue;
		}

		// MAJOR #5: Validate asset name and handle duplicates
		FString AssetName = BehaviorTree->GetName();
		if (AssetName.IsEmpty())
		{
			UE_LOG(LogNarshaMCPAIMeta, Warning, TEXT("[AIMetadata] ⚠️ BehaviorTree has empty name, using object path: %s"),
				*AssetData.GetObjectPathString());
			AssetName = FPaths::GetBaseFilename(AssetData.GetObjectPathString());
		}

		// Check for duplicate names
		if (BehaviorTreesJson->HasField(AssetName))
		{
			UE_LOG(LogNarshaMCPAIMeta, Warning, TEXT("[AIMetadata] ⚠️ Duplicate BehaviorTree name detected: %s (appending index)"),
				*AssetName);
			int32 Index = 1;
			FString UniqueAssetName;
			do {
				UniqueAssetName = FString::Printf(TEXT("%s_%d"), *AssetName, Index++);
			} while (BehaviorTreesJson->HasField(UniqueAssetName));
			AssetName = UniqueAssetName;
		}

		// CRITICAL #2: Validate AIExtractors return value
		TSharedPtr<FJsonObject> BehaviorTreeMetadata = AIExtractors::ExtractBehaviorTreeMetadata(BehaviorTree, AssetData);
		if (BehaviorTreeMetadata.IsValid())
		{
			BehaviorTreesJson->SetObjectField(AssetName, BehaviorTreeMetadata);
			BehaviorTreeCount++;
		}
		else
		{
			UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Failed to extract BehaviorTree metadata: %s"), *AssetName);
			FailedBehaviorTreeCount++;
		}
	}

	float Phase4Time = (FDateTime::UtcNow() - Phase4StartTime).GetTotalSeconds();
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Extracted %d / %d BehaviorTrees (%.3fs)"),
		BehaviorTreeCount, BehaviorTreeAssetList.Num(), Phase4Time);

	// MAJOR #4: Asset count mismatch detection
	if (BehaviorTreeCount < BehaviorTreeAssetList.Num())
	{
		int32 MissingCount = BehaviorTreeAssetList.Num() - BehaviorTreeCount;
		UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ⚠️ Failed to extract %d / %d BehaviorTrees (%d failures)"),
			MissingCount, BehaviorTreeAssetList.Num(), MissingCount);
	}

	// Phase 5: Build Root JSON Object
	FDateTime Phase5StartTime = FDateTime::UtcNow();
	TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

	// Metadata section
	TSharedPtr<FJsonObject> MetadataJson = MakeShareable(new FJsonObject);
	MetadataJson->SetStringField(TEXT("generator"), TEXT("AIMetadataCommandlet"));

	// CRITICAL #5: Use version constant instead of hardcoded value
	MetadataJson->SetStringField(TEXT("version"), AIMETADATA_COMMANDLET_VERSION);

	MetadataJson->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToIso8601());
	MetadataJson->SetNumberField(TEXT("total_statetrees"), StateTreeCount);
	MetadataJson->SetNumberField(TEXT("total_behaviortrees"), BehaviorTreeCount);

	RootJson->SetObjectField(TEXT("metadata"), MetadataJson);
	RootJson->SetObjectField(TEXT("statetrees"), StateTreesJson);
	RootJson->SetObjectField(TEXT("behaviortrees"), BehaviorTreesJson);

	float Phase5Time = (FDateTime::UtcNow() - Phase5StartTime).GetTotalSeconds();

	// Phase 6: Save JSON with Compression
	FDateTime Phase6StartTime = FDateTime::UtcNow();
	FString OutputPath = OutputDir / TEXT("ai_metadata.json");

	bool bSaveSuccess = FCUCommandletHelpers::SaveJsonWithCompression(OutputPath, RootJson);
	float Phase6Time = (FDateTime::UtcNow() - Phase6StartTime).GetTotalSeconds();

	// MINOR #1: Prevent division by zero in percentage calculations
	float TotalTime = FMath::Max((FDateTime::UtcNow() - TotalStartTime).GetTotalSeconds(), 0.001f);

	// CRITICAL #4: Performance breakdown
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT(""));
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] Performance Breakdown:"));
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Phase 1 (Find StateTree)       : %.3fs (%.1f%%)"), Phase1Time, Phase1Time / TotalTime * 100.0f);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Phase 2 (Find BehaviorTree)    : %.3fs (%.1f%%)"), Phase2Time, Phase2Time / TotalTime * 100.0f);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Phase 3 (Extract StateTree)    : %.3fs (%.1f%%)"), Phase3Time, Phase3Time / TotalTime * 100.0f);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Phase 4 (Extract BehaviorTree) : %.3fs (%.1f%%)"), Phase4Time, Phase4Time / TotalTime * 100.0f);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Phase 5 (Build JSON)           : %.3fs (%.1f%%)"), Phase5Time, Phase5Time / TotalTime * 100.0f);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Phase 6 (Save + Compress)      : %.3fs (%.1f%%)"), Phase6Time, Phase6Time / TotalTime * 100.0f);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("   Total                          : %.3fs"), TotalTime);
	UE_LOG(LogNarshaMCPAIMeta, Display, TEXT(""));

	if (bSaveSuccess)
	{
		UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] ✓ Successfully saved: %s.gz"), *OutputPath);
		UE_LOG(LogNarshaMCPAIMeta, Display, TEXT("[AIMetadata] ✓ Total AI assets: %d (StateTree: %d, BehaviorTree: %d)"),
			StateTreeCount + BehaviorTreeCount, StateTreeCount, BehaviorTreeCount);

		// CRITICAL #1: Report failures and return error code if any failures occurred
		if (FailedStateTreeCount > 0 || FailedBehaviorTreeCount > 0)
		{
			UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Failed to load/extract %d assets (StateTree: %d, BehaviorTree: %d)"),
				FailedStateTreeCount + FailedBehaviorTreeCount, FailedStateTreeCount, FailedBehaviorTreeCount);
			UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ⚠️ Extraction completed with errors"));
			return 1;  // Non-zero exit code on failures
		}

		return 0;
	}
	else
	{
		UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] ✗ Failed to save JSON: %s"), *OutputPath);
		return 1;
	}
#else
	// CRITICAL #3: Proper #if WITH_EDITOR guard for non-Editor builds
	UE_LOG(LogNarshaMCPAIMeta, Error, TEXT("[AIMetadata] Editor-only commandlet cannot run in non-editor builds"));
	return 1;
#endif
}
