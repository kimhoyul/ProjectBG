// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUModule.h"
#include "CUJsonlHelpers.h" // Issue #9122: AppendJsonlEntry, JsonEscape
#include "CULog.h"
#include "CUEditorStatusWriter.h"
#include "CULicenseTokenGenerator.h"
#include "CUPlatformPaths.h" // Issue #9230: cross-platform path helpers
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceBase.h"  // Issue #8564 Phase K-2: AnimNotify capture
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimData/AnimDataModel.h"       // Issue #9018: curve keyframe capture
#include "Materials/Material.h"                     // Issue #9021: UMaterial for asset filter
#include "Materials/MaterialInterface.h"            // Issue #9021: UMaterialInterface
#include "Animation/AnimData/AnimDataNotifications.h" // Issue #9018: EAnimDataModelNotifyType

// Issue #124 Phase 2: Blueprint metadata extraction
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Issue #1437: WebSocket Editor Subsystem (UEditorSubsystem migration)
#include "CUWebSocketEditorSubsystem.h"
#include "Editor.h"

// Issue #8564: Transaction logging
#include "Editor/TransBuffer.h"
#include "Misc/TransactionObjectEvent.h"
#include "GameFramework/Actor.h"
#include "EdGraph/EdGraphNode.h"  // Issue #8564 Phase K: Graph node deletion detection

// Forward declare BlueprintExtractors (Issue #124 Phase 2)
namespace BlueprintExtractors
{
	extern TSharedPtr<FJsonObject> ExtractBlueprintMetadata(UBlueprint* Blueprint, const FAssetData& AssetData);
}

#define LOCTEXT_NAMESPACE "FCUModule"

void FCUModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module

	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Plugin loading..."));

	// Get plugin directory using IPluginManager
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NarshaMCP"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("NarshaMCP: Plugin not found!"));
		return;
	}

	FString PluginDir = Plugin->GetBaseDir();
	FString ProjectDir = FPaths::ProjectDir();
	FString EngineDir = FPaths::EngineDir();

	// Cache paths for Delta JSON generation
	CachedPluginDir = PluginDir;
	CachedProjectDir = ProjectDir;
	CachedEngineDir = EngineDir;

	// Issue #1437: Start WebSocket subsystem for real-time communication
	// UEditorSubsystem migration from FCUWebSocketServer singleton
	// Only start in Editor mode, not in Commandlet mode
	if (GIsEditor && !IsRunningCommandlet() && GEditor)
	{
		UCUWebSocketEditorSubsystem* WSSubsystem = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
		// Issue #16334: StartAuto() scans for a free WS port (already auto-started in
		// the subsystem's Initialize(); this is the idempotent bIsRunning re-entry).
		if (WSSubsystem && WSSubsystem->StartAuto())
		{
			UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: WebSocket subsystem started on port %d (Issue #1437 / #16334)"), WSSubsystem->GetServerPort());
			UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Real-time events: asset_updated, compile_finished, etc."));
		}
		else
		{
			UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to start WebSocket subsystem"));
		}
	}

	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Plugin directory: %s"), *PluginDir);
	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Project directory: %s"), *ProjectDir);
	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Engine directory: %s"), *EngineDir);

	// Issue #118/#27966: only an interactive Editor owns editor_status.json.
	// Commandlets share the project path but are not Editor readiness endpoints.
	if (FCUEditorStatusWriter::ShouldManageEditorStatus(GIsEditor, IsRunningCommandlet()))
	{
		FCUEditorStatusWriter::WriteEditorStatus(ProjectDir);
	}

	// Issue #7948: Generate Fab license token for MCP server binary
	FCULicenseTokenGenerator::Generate(ProjectDir);

	// Run post-build initialization
	RunPostBuildInitialization(PluginDir, ProjectDir, EngineDir);

	// Run GameplayAbility Tag extraction
	RunGameplayAbilityTagExtraction(PluginDir, ProjectDir, EngineDir);

	// Run Blueprint Metadata extraction
	RunBlueprintMetadataExtraction(PluginDir, ProjectDir, EngineDir);

	// Phase 6-8 (Issue #29): Register OnAssetUpdated delegate for real-time updates
	// Check if real-time updates are enabled (default: true, disable via env var)
	FString DisableRealtime = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_DISABLE_REALTIME_UPDATE"));
	if (DisableRealtime.IsEmpty() || DisableRealtime != TEXT("1"))
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		AssetUpdatedHandle = AssetRegistry.OnAssetUpdated().AddRaw(this, &FCUModule::OnAssetUpdated);

		// Issue #8564 M2: Asset lifecycle tracking
		AssetAddedHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &FCUModule::OnAssetAdded);
		AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &FCUModule::OnAssetRemoved);
		AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FCUModule::OnAssetRenamed);

		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Real-time asset update enabled (Phase 6-8)"));
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Delta JSON files will be created on Ctrl+S"));
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: To disable: Set UECODEGEN_DISABLE_REALTIME_UPDATE=1"));
	}
	else
	{
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Real-time asset update disabled (UECODEGEN_DISABLE_REALTIME_UPDATE=1)"));
	}

	// Issue #8564 Phase J: Transaction logging (always-on, opt-out via UECODEGEN_TRANSACTION_LOG=0)
	FString DisableTransLog = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_TRANSACTION_LOG"));
	bool bTransLogDisabled = (DisableTransLog == TEXT("0"));

	if (!bTransLogDisabled)
	{
		if (GIsEditor && !IsRunningCommandlet() && GEditor && GEditor->Trans)
		{
			UTransBuffer* TransBuffer = Cast<UTransBuffer>(GEditor->Trans);
			if (TransBuffer)
			{
				TransactionStateHandle = TransBuffer->OnTransactionStateChanged().AddRaw(
					this, &FCUModule::OnTransactionStateChanged);
			}

			ObjectTransactedHandle = FCoreUObjectDelegates::OnObjectTransacted.AddRaw(
				this, &FCUModule::OnObjectTransacted);

			// Issue #8834 Part B: Capture property changes outside transactions
			// (e.g., Python set_editor_property() which bypasses FScopedTransaction)
			// Uses OnObjectPropertyChanged — fires on PreEditChange/PostEditChange pair
			ObjectPropertyChangedHandle = FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(
				this, &FCUModule::OnObjectPropertyChanged);

			UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: OnObjectPropertyChanged delegate registered for non-transaction capture"));

			TransactionLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("transactions.jsonl");

			// Ensure directory exists
			FString LogDir = FPaths::GetPath(TransactionLogPath);
			if (!FPaths::DirectoryExists(LogDir))
			{
				IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
				PlatformFile.CreateDirectoryTree(*LogDir);
			}

			UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Transaction logging enabled -> %s"), *TransactionLogPath);

			// Issue #8834 B1: Diagnostic mode — log all events pre-filter
			FString DiagEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_DIAGNOSTIC_MODE"));
			bDiagnosticMode = FParse::Param(FCommandLine::Get(), TEXT("DiagnosticMode"))
				|| (DiagEnv.Len() > 0 && DiagEnv != TEXT("0"));
			if (bDiagnosticMode)
			{
				DiagnosticLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("diagnostics.jsonl");
				UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Diagnostic mode ENABLED -> %s"), *DiagnosticLogPath);
			}

			// Issue #8834 B2: Level actor lifecycle delegates (outside-transaction capture)
			LevelActorAddedHandle = GEditor->OnLevelActorAdded().AddRaw(
				this, &FCUModule::OnLevelActorAdded);
			LevelActorDeletedHandle = GEditor->OnLevelActorDeleted().AddRaw(
				this, &FCUModule::OnLevelActorDeleted);
			ActorMovedHandle = GEditor->OnActorMoved().AddRaw(
				this, &FCUModule::OnActorMoved);

			UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Level actor lifecycle delegates registered (B2)"));

			// Issue #8564 L1: Blueprint compile tracking
			BlueprintCompiledHandle = GEditor->OnBlueprintCompiled().AddLambda([this]() {
				WriteEventEntry(TEXT("blueprint_compiled"), TEXT(""));
			});

			// Issue #8564 L2: PIE start/stop tracking
			BeginPIEHandle = FEditorDelegates::BeginPIE.AddLambda([this](bool bIsSimulating) {
				WriteEventEntry(TEXT("pie_started"), bIsSimulating ? TEXT("simulate") : TEXT("play"));
			});
			EndPIEHandle = FEditorDelegates::EndPIE.AddLambda([this](bool bIsSimulating) {
				WriteEventEntry(TEXT("pie_stopped"), bIsSimulating ? TEXT("simulate") : TEXT("play"));
			});

			// Issue #8564 L3: Map loading tracking
			MapOpenedHandle = FEditorDelegates::OnMapOpened.AddLambda([this](const FString& MapName, bool bAsTemplate) {
				WriteEventEntry(TEXT("map_opened"), MapName);
			});
		}
	}
}

void FCUModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// Issue #1437: WebSocket subsystem cleanup
	// UEditorSubsystem automatically calls Deinitialize() ??Stop()
	// No explicit Stop() needed, but log for consistency
	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: WebSocket subsystem will be cleaned up by UEditorSubsystem::Deinitialize()"));

	// Phase 6-8: Unregister OnAssetUpdated delegate
	if (AssetUpdatedHandle.IsValid())
	{
		FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");
		if (AssetRegistryModule)
		{
			IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
			AssetRegistry.OnAssetUpdated().Remove(AssetUpdatedHandle);
			UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Unregistered OnAssetUpdated delegate"));
		}
		AssetUpdatedHandle.Reset();
	}

	// Issue #8564 M2: Unregister asset lifecycle delegates
	{
		FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry");
		if (AssetRegistryModule)
		{
			IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
			if (AssetAddedHandle.IsValid()) { AssetRegistry.OnAssetAdded().Remove(AssetAddedHandle); AssetAddedHandle.Reset(); }
			if (AssetRemovedHandle.IsValid()) { AssetRegistry.OnAssetRemoved().Remove(AssetRemovedHandle); AssetRemovedHandle.Reset(); }
			if (AssetRenamedHandle.IsValid()) { AssetRegistry.OnAssetRenamed().Remove(AssetRenamedHandle); AssetRenamedHandle.Reset(); }
		}
	}

	// Issue #8564: Unregister transaction delegates
	if (TransactionStateHandle.IsValid())
	{
		if (GEditor && GEditor->Trans)
		{
			UTransBuffer* TransBuffer = Cast<UTransBuffer>(GEditor->Trans);
			if (TransBuffer)
			{
				TransBuffer->OnTransactionStateChanged().Remove(TransactionStateHandle);
			}
		}
		TransactionStateHandle.Reset();
	}
	if (ObjectTransactedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectTransacted.Remove(ObjectTransactedHandle);
		ObjectTransactedHandle.Reset();
	}
	if (ObjectPropertyChangedHandle.IsValid())
	{
		FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(ObjectPropertyChangedHandle);
		ObjectPropertyChangedHandle.Reset();
	}

	// Issue #8834 B2: Unregister level actor lifecycle delegates
	if (GEditor)
	{
		if (LevelActorAddedHandle.IsValid()) { GEditor->OnLevelActorAdded().Remove(LevelActorAddedHandle); LevelActorAddedHandle.Reset(); }
		if (LevelActorDeletedHandle.IsValid()) { GEditor->OnLevelActorDeleted().Remove(LevelActorDeletedHandle); LevelActorDeletedHandle.Reset(); }
		if (ActorMovedHandle.IsValid()) { GEditor->OnActorMoved().Remove(ActorMovedHandle); ActorMovedHandle.Reset(); }
	}

	// Issue #8564 L1-L4: Unregister editor event delegates
	if (GEditor)
	{
		if (BlueprintCompiledHandle.IsValid()) { GEditor->OnBlueprintCompiled().Remove(BlueprintCompiledHandle); BlueprintCompiledHandle.Reset(); }
	}
	if (BeginPIEHandle.IsValid()) { FEditorDelegates::BeginPIE.Remove(BeginPIEHandle); BeginPIEHandle.Reset(); }
	if (EndPIEHandle.IsValid()) { FEditorDelegates::EndPIE.Remove(EndPIEHandle); EndPIEHandle.Reset(); }
	if (MapOpenedHandle.IsValid()) { FEditorDelegates::OnMapOpened.Remove(MapOpenedHandle); MapOpenedHandle.Reset(); }

	// Issue #118/#27966: commandlet shutdown must never touch the Editor marker.
	if (FCUEditorStatusWriter::ShouldManageEditorStatus(GIsEditor, IsRunningCommandlet()))
	{
		FCUEditorStatusWriter::DeleteEditorStatus(CachedProjectDir);
	}

	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Plugin unloading..."));
}

void FCUModule::RunPostBuildInitialization(const FString& PluginDir, const FString& ProjectDir, const FString& EngineDir)
{
	// Issue #8908: Replaced Python initialize_plugin.py with narshamcp.exe setup subcommand.
	// Binary discovery matches NarshaMCP.Build.cs:FindNarshaMcpBinary pattern —
	// dev layout (MCP/bin/) first, then Fab layout (Source/ThirdParty/bin/).
	// Issue #9230: platform-aware suffix (.exe on Win64, "" on Mac/Linux).
	const FString NarshaExeName = NarshaMCP::Platform::GetNarshaMCPBinaryName();
	FString BinaryPath = FPaths::Combine(PluginDir, TEXT("MCP/bin"), NarshaExeName);
	if (!FPaths::FileExists(BinaryPath))
	{
		BinaryPath = FPaths::Combine(PluginDir, TEXT("Source/ThirdParty/bin"), NarshaExeName);
	}
	if (!FPaths::FileExists(BinaryPath))
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: %s not found in %s"), *NarshaExeName, *PluginDir);
		return;
	}

	// Build setup arguments — Rust setup covers all prior Python init steps plus more.
	FString Args = FString::Printf(
		TEXT("setup --project-path \"%s\" --plugin-dir \"%s\" --engine-path \"%s\" --skip-claude-registration"),
		*ProjectDir,
		*PluginDir,
		*EngineDir
	);

	UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Running setup: %s %s"), *BinaryPath, *Args);

	// Launch narshamcp.exe setup asynchronously (non-blocking)
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*BinaryPath,
		*Args,
		true,  // bLaunchDetached - process runs independently
		false, // bLaunchHidden
		false, // bLaunchReallyHidden
		nullptr, // OutProcessID
		0,     // PriorityModifier
		nullptr, // OptionalWorkingDirectory
		nullptr  // PipeWriteChild
	);

	if (ProcHandle.IsValid())
	{
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Setup started successfully (Issue #8908)"));
		// Don't wait for completion - let it run in background
		FPlatformProcess::CloseProc(ProcHandle);
	}
	else
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("NarshaMCP: Failed to start setup process"));
	}
}

void FCUModule::RunGameplayAbilityTagExtraction(const FString& PluginDir, const FString& ProjectDir, const FString& EngineDir)
{
	// Note: Commandlet execution requires Editor mode and cannot run automatically on plugin load
	// The Commandlet must be run manually via:
	//   UnrealEditor-Cmd.exe ProjectName -run=NarshaMCP
	//
	// This function creates a helper Python script that users can run to execute the Commandlet

	FString PythonScript = FPaths::Combine(PluginDir, TEXT("Content/Python/extract_ability_tags.py"));

	// Build UnrealEditor-Cmd path from EngineDir (Issue #9230: platform-aware).
	// Win64: Engine/Binaries/Win64/UnrealEditor-Cmd.exe
	// Mac:   Engine/Binaries/Mac/UnrealEditor-Cmd
	// Linux: Engine/Binaries/Linux/UnrealEditor-Cmd
	FString UECmdPath = FPaths::Combine(
		EngineDir,
		NarshaMCP::Platform::GetEditorBinariesSubdir(),
		NarshaMCP::Platform::GetEditorCmdBinaryName());

	// Create the Python helper script with auto-detected engine path
	FString ScriptContent = FString::Printf(TEXT(
		"#!/usr/bin/env python3\n"
		"\"\"\"\n"
		"GameplayAbility Tag Extraction Helper\n"
		"\n"
		"Executes the NarshaMCP Commandlet to extract GameplayTag data from abilities.\n"
		"Auto-generated by NarshaMCP plugin with detected engine path.\n"
		"\"\"\"\n"
		"import subprocess\n"
		"import sys\n"
		"from pathlib import Path\n"
		"\n"
		"def main():\n"
		"    project_dir = Path(r\"%s\")\n"
		"    project_file = project_dir / (project_dir.name + \".uproject\")\n"
		"    \n"
		"    if not project_file.exists():\n"
		"        print(f\"Error: Project file not found: {project_file}\")\n"
		"        return 1\n"
		"    \n"
		"    # Auto-detected UnrealEditor-Cmd path from FPaths::EngineDir() (platform-aware)\n"
		"    ue_cmd = r\"%s\"\n"
		"    \n"
		"    if not Path(ue_cmd).exists():\n"
		"        print(f\"Error: UnrealEditor-Cmd not found at: {ue_cmd}\")\n"
		"        print(\"Please verify your Unreal Engine installation\")\n"
		"        return 1\n"
		"    \n"
		"    # Execute Commandlet\n"
		"    cmd = [\n"
		"        ue_cmd,\n"
		"        str(project_file),\n"
		"        \"-run=NarshaMCP\"\n"
		"    ]\n"
		"    \n"
		"    print(f\"Executing: {' '.join(cmd)}\")\n"
		"    result = subprocess.run(cmd)\n"
		"    \n"
		"    return result.returncode\n"
		"\n"
		"if __name__ == \"__main__\":\n"
		"    sys.exit(main())\n"
	), *ProjectDir, *UECmdPath);

	// Write the helper script
	if (FFileHelper::SaveStringToFile(ScriptContent, *PythonScript))
	{
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Created GameplayAbility Tag extraction helper script"));
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Run manually: python \"%s\""), *PythonScript);
	}
	else
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to create extraction helper script"));
	}
}

/**
 * Phase 6-8 (Issue #29): OnAssetUpdated Delegate Handler
 *
 * Called when any asset is saved (Ctrl+S) in the editor.
 * Generates Delta JSON files for Blueprint and Animation assets.
 */
void FCUModule::OnAssetUpdated(const FAssetData& AssetData)
{
	// Filter for relevant asset types:
	// - Blueprints (UBlueprint)
	// - Animations (UAnimSequence) with AnimNotifies
	FTopLevelAssetPath BlueprintPath = UBlueprint::StaticClass()->GetClassPathName();
	FTopLevelAssetPath AnimSequencePath = UAnimSequence::StaticClass()->GetClassPathName();

	FTopLevelAssetPath AssetClassPath = AssetData.AssetClassPath;

	if (AssetClassPath == BlueprintPath || AssetClassPath == AnimSequencePath)
	{
		// Generate Delta JSON asynchronously (<5ms target)
		GenerateDeltaJSON(AssetData);
	}
}

/**
 * Phase 6-8 (Issue #29) + Issue #124 Phase 2: Generate Delta JSON
 *
 * Creates append-only Delta JSON file with complete Blueprint metadata.
 * Delta files are merged with base JSON by MCP tools.
 *
 * Performance target: <5ms (Issue #124 Phase 2)
 */
void FCUModule::GenerateDeltaJSON(const FAssetData& AssetData)
{
	double StartTime = FPlatformTime::Seconds();

	// Build Delta directory path: ProjectDir/Intermediate/NarshaMCP/Delta/
	FString DeltaDir = FPaths::Combine(CachedProjectDir, TEXT("Intermediate/NarshaMCP/Delta"));

	// Create directory if not exists
	if (!FPaths::DirectoryExists(DeltaDir))
	{
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		if (!PlatformFile.CreateDirectoryTree(*DeltaDir))
		{
			UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to create Delta directory: %s"), *DeltaDir);
			return;
		}
	}

	// Generate timestamp-based filename to avoid collisions
	FDateTime Now = FDateTime::Now();
	FString Timestamp = Now.ToString(TEXT("%Y%m%d_%H%M%S"));
	FString AssetName = AssetData.AssetName.ToString();
	FString DeltaFilename = FString::Printf(TEXT("delta_%s_%s.json"), *AssetName, *Timestamp);
	FString DeltaFilePath = FPaths::Combine(DeltaDir, DeltaFilename);

	// Issue #124 Phase 2: Extract complete Blueprint metadata
	FString DeltaJSON;

	// Issue #27272: never load from this callback. AssetRegistry OnAssetUpdated
	// fires for on-disk Lyra BPs during unattended boot; GetAsset() pulled in
	// ~971 async packages and NiagaraEditor AVd on nullptr pins
	// (NS_GunPad_Loading / NS_WallPortalEnter). Ctrl+S is already-loaded.
	UObject* Asset = AssetData.FastGetAsset(false);
	if (Asset == nullptr)
	{
		return;
	}
	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);

	if (Blueprint != nullptr)
	{
		// Blueprint asset (includes all derived types: AnimBlueprint, WidgetBlueprint, etc.) - extract full metadata
		// Use BlueprintExtractors for complete metadata
		TSharedPtr<FJsonObject> BlueprintData = BlueprintExtractors::ExtractBlueprintMetadata(Blueprint, AssetData);

		if (BlueprintData.IsValid())
		{
			// Add Delta metadata
			BlueprintData->SetStringField(TEXT("delta_timestamp"), Timestamp);
			BlueprintData->SetStringField(TEXT("delta_operation"), TEXT("update"));

			// Serialize to JSON string
			TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&DeltaJSON);
			FJsonSerializer::Serialize(BlueprintData.ToSharedRef(), JsonWriter);
		}
		else
		{
			UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to extract Blueprint metadata: %s"), *AssetName);
			return;
		}
	}
	else
	{
		// Non-Blueprint asset (Animation, etc.) - simple metadata
		DeltaJSON = FString::Printf(TEXT(
			"{\n"
			"  \"timestamp\": \"%s\",\n"
			"  \"asset_name\": \"%s\",\n"
			"  \"asset_path\": \"%s\",\n"
			"  \"asset_class\": \"%s\",\n"
			"  \"operation\": \"update\"\n"
			"}\n"
		),
			*Timestamp,
			*AssetName,
			*AssetData.GetObjectPathString(),
			*AssetData.AssetClassPath.ToString()
		);
	}

	// Phase 4 (Issue #96): Use atomic write instead of direct SaveStringToFile
	if (AtomicWriteFile(DeltaFilePath, DeltaJSON))
	{
		double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Generated Delta JSON: %s (%.2fms)"), *DeltaFilename, ElapsedMs);

		// Issue #124 Phase 2: Performance warning if >5ms
		if (ElapsedMs > 5.0)
		{
			UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Delta generation took %.2fms (target: <5ms)"), ElapsedMs);
		}

		// Issue #1437: Broadcast asset_updated event via WebSocket subsystem
		if (GEditor)
		{
			UCUWebSocketEditorSubsystem* WSSubsystem = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
			if (WSSubsystem && WSSubsystem->IsRunning())
			{
				// Build event data JSON
				FString EventData = FString::Printf(TEXT(
					"{"
					"\"asset_name\":\"%s\","
					"\"asset_path\":\"%s\","
					"\"asset_class\":\"%s\","
					"\"delta_file\":\"%s\""
					"}"
				),
					*AssetName,
					*AssetData.GetObjectPathString(),
					*AssetData.AssetClassPath.ToString(),
					*DeltaFilename
				);

				// Broadcast to all connected MCP clients
				WSSubsystem->BroadcastEvent(TEXT("asset_updated"), EventData);
			}
		}
	}
	else
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to write Delta JSON: %s"), *DeltaFilePath);
	}
}

/**
 * Phase 4 (Issue #96): Atomic File Write Helper
 *
 * Writes content to file atomically using temp file + rename pattern.
 * Prevents partial writes and corruption during crashes/interruptions.
 *
 * Pattern: temp_file ??write_content ??atomic_rename
 * Same as Python's: temp + os.replace()
 *
 * @param FilePath Target file path
 * @param Content String content to write
 * @return true if write succeeded, false otherwise
 */
bool FCUModule::AtomicWriteFile(const FString& FilePath, const FString& Content)
{
	// Get platform file interface
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Generate temp file path in same directory
	// Pattern: target_dir/target_name_tmp_RANDOM.json
	FString Directory = FPaths::GetPath(FilePath);
	FString Filename = FPaths::GetBaseFilename(FilePath);
	FString Extension = FPaths::GetExtension(FilePath);

	// Generate random suffix for temp file (avoid collisions)
	FGuid RandomGuid = FGuid::NewGuid();
	FString TempFilename = FString::Printf(TEXT("%s_tmp_%s.%s"),
		*Filename,
		*RandomGuid.ToString(EGuidFormats::Digits),
		*Extension
	);
	FString TempFilePath = FPaths::Combine(Directory, TempFilename);

	// Step 1: Write to temp file
	if (!FFileHelper::SaveStringToFile(Content, *TempFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to write temp file: %s"), *TempFilePath);
		return false;
	}

	// Step 2: Atomic rename temp ??target
	// IFileManager::Move() with bReplace=true is atomic on most platforms
	if (!PlatformFile.MoveFile(*FilePath, *TempFilePath))
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to atomic rename: %s ??%s"), *TempFilePath, *FilePath);

		// Cleanup temp file on failure
		PlatformFile.DeleteFile(*TempFilePath);
		return false;
	}

	// Success - temp file has been atomically renamed to target
	return true;
}

// ============================================================================
// Issue #8564 Phase J: Transaction logging callbacks
// ============================================================================

// Issue #9122: helpers moved to NarshaMCP namespace (declared in CUJsonlHelpers.h)
// so regression tests can exercise them directly. Implementations live here because
// these are CUModule-only helpers.

namespace NarshaMCP
{
	bool AppendJsonlEntry(const FString& Path, const FString& Entry, int64 MaxBytes, int64 MaxEntryBytes)
	{
		if (Path.IsEmpty())
		{
			return false;
		}
		const FTCHARToUTF8 EntryUtf8(*Entry);
		const FTCHARToUTF8 TerminatorUtf8(LINE_TERMINATOR);
		const int64 EntryBytes = static_cast<int64>(EntryUtf8.Length()) + TerminatorUtf8.Length();
		if (MaxEntryBytes <= 0 || EntryBytes > MaxEntryBytes)
		{
			UE_LOG(LogNarshaMCP, Warning,
				TEXT("NarshaMCP: JSONL entry rejected before append (%lld bytes, maximum %lld): %s"),
				EntryBytes, MaxEntryBytes, *Path);
			return false;
		}
		const int64 CurSize = IFileManager::Get().FileSize(*Path);
		if (CurSize > 0 && CurSize >= MaxBytes)
		{
			const FString Rotated = Path + TEXT(".1");
			IFileManager::Get().Delete(*Rotated, /*bRequireExists=*/false, /*bEvenReadOnly=*/true);
			IFileManager::Get().Move(*Rotated, *Path);
		}
		return FFileHelper::SaveStringToFile(
			Entry + LINE_TERMINATOR, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), FILEWRITE_Append);
	}

	FString JsonEscape(const FString& Input)
	{
		FString Result;
		Result.Reserve(Input.Len() + 16);
		for (TCHAR Ch : Input)
		{
			switch (Ch)
			{
			case TEXT('"'):  Result += TEXT("\\\""); break;
			case TEXT('\\'): Result += TEXT("\\\\"); break;
			case TEXT('\n'): Result += TEXT("\\n");  break;
			case TEXT('\r'): Result += TEXT("\\r");  break;
			case TEXT('\t'): Result += TEXT("\\t");  break;
			case TEXT('\b'): Result += TEXT("\\b");  break;
			case TEXT('\f'): Result += TEXT("\\f");  break;
			default:
				// Issue #9122: JSON spec requires all control characters (U+0000 to U+001F)
				// to be escaped. Prior code only handled \n\r\t which caused downstream
				// JSONL parsers to reject lines containing e.g. \u0001.
				if (Ch < 0x20)
				{
					Result += FString::Printf(TEXT("\\u%04x"), static_cast<int32>(Ch));
				}
				else
				{
					Result += Ch;
				}
				break;
			}
		}
		return Result;
	}
}

using NarshaMCP::AppendJsonlEntry;
using NarshaMCP::JsonEscape;

void FCUModule::OnTransactionStateChanged(
	const FTransactionContext& Context,
	ETransactionStateEventType State)
{
	// Issue #8564 L4: Track undo/redo state changes
	if (State == ETransactionStateEventType::UndoRedoFinalized)
	{
		FString Title = Context.Title.ToString();
		Title = JsonEscape(Title);
		WriteEventEntry(TEXT("undo_redo"), Title);
		return;
	}

	// Issue #8834 Part B: Track transaction state for duplicate prevention
	if (State == ETransactionStateEventType::TransactionStarted)
	{
		// Issue #9122: reset stale context from the previous transaction so any
		// OnObjectTransacted that fires outside a finalize cycle sees empty
		// context rather than data from an unrelated prior transaction.
		CurrentTransactionContext.Reset();
		++TransactionDepth;
		return;
	}
	if (State == ETransactionStateEventType::TransactionCanceled)
	{
		CurrentTransactionContext.Reset();
		TransactionDepth = FMath::Max(0, TransactionDepth - 1);
		return;
	}

	// Only record finalized transactions (EditorTransaction.cpp:1320)
	if (State != ETransactionStateEventType::TransactionFinalized)
	{
		return;
	}
	TransactionDepth = FMath::Max(0, TransactionDepth - 1);

	FString Title = Context.Title.ToString();
	Title = JsonEscape(Title);

	FString Primary = Context.PrimaryObject ? Context.PrimaryObject->GetPathName() : TEXT("null");

	// Phase J-1: Capture spawn info for Place Actor transactions
	FString SpawnStr;
	if (Context.PrimaryObject)
	{
		AActor* Actor = Cast<AActor>(Context.PrimaryObject);
		if (Actor && Title.StartsWith(TEXT("Place Actor")))
		{
			FVector Loc = Actor->GetActorLocation();
			FRotator Rot = Actor->GetActorRotation();
			FString Label = Actor->GetActorLabel();
			Label = JsonEscape(Label);

			FVector Scale = Actor->GetActorScale3D();
			SpawnStr = FString::Printf(
				TEXT(",\"spawn\":{\"class\":\"%s\",\"location\":\"X=%.1f Y=%.1f Z=%.1f\",\"rotation\":\"P=%.1f Y=%.1f R=%.1f\",\"scale\":\"X=%.1f Y=%.1f Z=%.1f\",\"label\":\"%s\"}"),
				*Actor->GetClass()->GetPathName(),
				Loc.X, Loc.Y, Loc.Z,
				Rot.Pitch, Rot.Yaw, Rot.Roll,
				Scale.X, Scale.Y, Scale.Z,
				*Label
			);
		}
	}

	// Issue #8564 Phase K: Store context for OnObjectTransacted to include in object_changed entries
	CurrentTransactionContext = Context.Context;

	// Issue #8834 Phase 5: Auto-detect Material context from API-initiated transactions
	// FScopedTransaction from SetMaterialParameter/CreateMaterialInstance has empty Context.Context,
	// but title starts with "Set Material Parameter:" or "Create Material Instance:"
	if (CurrentTransactionContext.IsEmpty()
		&& (Title.StartsWith(TEXT("Set Material Parameter:")) || Title.StartsWith(TEXT("Create Material Instance:"))))
	{
		CurrentTransactionContext = TEXT("MaterialEditor");
	}

	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"title\":\"%s\",\"context\":\"%s\",\"primary\":\"%s\"%s}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Title,
		*Context.Context,
		*Primary,
		*SpawnStr
	);

	AppendJsonlEntry(TransactionLogPath, Entry);

	// WebSocket broadcast (optional, same pattern as CUModule.cpp OnAssetUpdated)
	if (GEditor)
	{
		UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
		if (WS && WS->IsRunning())
		{
			WS->BroadcastEvent(TEXT("transaction_finalized"), Entry);
		}
	}
}

// Shared blacklist of noisy transient properties to skip during transaction/property change capture.
// These are internal UE lifecycle properties that generate noise without meaningful replay value.
static const TSet<FName>& GetNoisyTransientProps()
{
	static const TSet<FName> Props = {
		FName(TEXT("bActorIsBeingDestroyed")),
		FName(TEXT("bHasBegunPlay")),
		FName(TEXT("bHasRegisteredAllComponents")),
		FName(TEXT("bActorInitialized")),
		FName(TEXT("bRoutePostActorCreated")),
		FName(TEXT("bExchangedRoles")),
		FName(TEXT("CreationTime")),
		FName(TEXT("LastRenderTime")),
		FName(TEXT("LastRenderTimeOnScreen")),
		FName(TEXT("bComponentToWorldUpdated")),
		FName(TEXT("bShouldBeAttached")),
		FName(TEXT("AttachChildren")),
	};
	return Props;
}

void FCUModule::OnObjectTransacted(
	UObject* Object,
	const FTransactionObjectEvent& Event)
{
	// Issue #8834 B1: Diagnostic mode — log ALL events before any filtering
	if (bDiagnosticMode && Object)
	{
		WriteDiagnosticEntry(Object, Event, TEXT("pre_filter"));
	}

	if (Event.GetEventType() != ETransactionObjectEventType::Finalized)
	{
		return;
	}

	// Issue #8564 Phase K: Graph node deletion detection
	// UEdGraphNode (Blueprint, Material, PCG, etc.) deletion emits a dedicated event
	// with graph path, node GUID, and class for accurate replay script generation.
	if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object))
	{
		if (Event.HasPendingKillChange())
		{
			FString GraphPath = GraphNode->GetGraph()
				? GraphNode->GetGraph()->GetPathName() : TEXT("");
			FString NodeClass = GraphNode->GetClass()->GetName();
			FString NodeGuid = GraphNode->NodeGuid.ToString();

			FString Entry = FString::Printf(
				TEXT("{\"ts\":%lld,\"event\":\"graph_node_deleted\",\"object\":\"%s\",\"class\":\"%s\","
					"\"path\":\"%s\",\"context\":\"%s\","
					"\"values\":{\"node_guid\":\"%s\",\"node_class\":\"%s\",\"graph\":\"%s\"}}"),
				FDateTime::UtcNow().ToUnixTimestamp(),
				*Object->GetName(),
				*NodeClass,
				*Object->GetPathName(),
				*CurrentTransactionContext,
				*NodeGuid,
				*NodeClass,
				*JsonEscape(GraphPath)
			);

			AppendJsonlEntry(TransactionLogPath, Entry);

			if (GEditor)
			{
				UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
				if (WS && WS->IsRunning())
				{
					WS->BroadcastEvent(TEXT("graph_node_deleted"), Entry);
				}
			}
			return; // Deletion fully handled — no need to process property changes
		}
		// Node property changes (e.g., pin value edit) fall through to normal handling below
	}

	// Issue #8833 Phase 3: MetaSound transaction capture.
	//
	// `FMetasoundFrontendDocument` uses custom serialization (FArchive), so a
	// MetaSound mutation triggered through `UMetaSoundBuilderBase` never produces
	// reflected `ChangedProperties` — `Event.HasPropertyChanges()` returns false.
	// However, `CURemoteLibrary::AddMetaSoundNode/RemoveMetaSoundNode/...` wraps
	// each call in `FScopedTransaction` + `Asset->Modify()`, which DOES emit a
	// finalized transaction object event for the asset. We detect MetaSound
	// assets by class name (avoids hard-coupling CUModule to MetasoundEngine
	// module headers) and emit a dedicated `metasound_transaction` event so the
	// replay-coverage benchmark can tag the domain explicitly.
	{
		const FString ClassName = Object->GetClass()->GetName();
		const bool bIsMetaSoundAsset =
			ClassName.Equals(TEXT("MetaSoundPatch")) ||
			ClassName.Equals(TEXT("MetaSoundSource")) ||
			ClassName.Equals(TEXT("MetaSoundBuilderBase")) ||
			ClassName.Equals(TEXT("MetaSoundPatchBuilder")) ||
			ClassName.Equals(TEXT("MetaSoundSourceBuilder"));
		if (bIsMetaSoundAsset)
		{
			FString Entry = FString::Printf(
				TEXT("{\"ts\":%lld,\"event\":\"metasound_transaction\",\"object\":\"%s\",\"class\":\"%s\","
					 "\"path\":\"%s\",\"context\":\"%s\","
					 "\"values\":{\"has_property_changes\":%s,\"has_non_property_changes\":%s}}"),
				FDateTime::UtcNow().ToUnixTimestamp(),
				*Object->GetName(),
				*ClassName,
				*Object->GetPathName(),
				*CurrentTransactionContext,
				Event.HasPropertyChanges() ? TEXT("true") : TEXT("false"),
				Event.HasNonPropertyChanges() ? TEXT("true") : TEXT("false")
			);
			AppendJsonlEntry(TransactionLogPath, Entry);
			if (GEditor)
			{
				UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
				if (WS && WS->IsRunning())
				{
					WS->BroadcastEvent(TEXT("metasound_transaction"), Entry);
				}
			}
			// Fall through — if reflected property changes are also present
			// (rare for MetaSound but possible for thin wrappers like author/name),
			// the standard property-change path below still captures them.
		}
	}

	// Issue #8564 + Issue #8834 Cycle 20 (B3): Capture non-property changes
	// Changed: Always log non-property changes, even if property changes also exist.
	// Previous bug: rename + property edit in same transaction = rename lost.
	if (Event.HasNonPropertyChanges())
	{
		FString ChangeType;
		if (Event.HasPendingKillChange()) ChangeType = TEXT("deleted");
		else if (Event.HasNameChange()) ChangeType = TEXT("renamed");
		else if (Event.HasOuterChange()) ChangeType = TEXT("reparented");
		else if (Event.HasExternalPackageChange()) ChangeType = TEXT("package_moved");
		else ChangeType = TEXT("non_property");

		FString Entry = FString::Printf(
			TEXT("{\"ts\":%lld,\"event\":\"%s\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\",\"context\":\"%s\"}"),
			FDateTime::UtcNow().ToUnixTimestamp(),
			*ChangeType,
			*Object->GetName(),
			*Object->GetClass()->GetName(),
			*Object->GetPathName(),
			*CurrentTransactionContext
		);

		AppendJsonlEntry(TransactionLogPath, Entry);

		if (GEditor)
		{
			UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
			if (WS && WS->IsRunning())
			{
				WS->BroadcastEvent(*ChangeType, Entry);
			}
		}

		// Issue #8834 B3: Do NOT return if property changes also exist — fall through
		if (!Event.HasPropertyChanges())
		{
			return;
		}
	}

	if (!Event.HasPropertyChanges())
	{
		return;
	}

	// Phase J-1: Build "values" map with ExportText serialization
	const TArray<FName>& ChangedProps = Event.GetChangedProperties();
	UClass* Class = Object->GetClass();

	FString ValuesStr;
	for (const FName& PropName : ChangedProps)
	{
		FProperty* Prop = Class->FindPropertyByName(PropName);
		if (!Prop)
		{
			continue;
		}

		// Issue #8564 L7 + Issue #8834 Phase 16: Transient property handling
		// Changed from blanket skip to blacklist approach:
		// - Most transient properties ARE useful for replay (bHidden, bActorEnableCollision, etc.)
		// - Only skip transient properties that are truly internal/noise
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
		{
			// Blacklist: skip only known-noisy transient properties
			if (GetNoisyTransientProps().Contains(PropName))
			{
				continue;
			}
			// Fall through: capture all other transient properties
		}

		FString ValueStr;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
		Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Object, PPF_None);
		ValueStr = JsonEscape(ValueStr);

		if (!ValuesStr.IsEmpty())
		{
			ValuesStr += TEXT(",");
		}
		ValuesStr += FString::Printf(TEXT("\"%s\":\"%s\""), *PropName.ToString(), *ValueStr);
	}

	// Field names match Rust TransactionEntry: "object", "class", "path", "context", "values"
	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"object_changed\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\",\"context\":\"%s\",\"values\":{%s}}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Object->GetName(),
		*Object->GetClass()->GetName(),
		*Object->GetPathName(),
		*CurrentTransactionContext,
		*ValuesStr
	);

	AppendJsonlEntry(TransactionLogPath, Entry);

	// Issue #8564 J-8: Broadcast object_transacted for auto-snapshot
	if (GEditor)
	{
		UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
		if (WS && WS->IsRunning())
		{
			WS->BroadcastEvent(TEXT("object_transacted"), Entry);
		}
	}

	// Issue #8564 Phase K-2: Granular AnimNotify capture
	// When Notifies array changes on an AnimSequenceBase, write individual notify details
	if (UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(Object))
	{
		if (ChangedProps.Contains(FName(TEXT("Notifies"))))
		{
			FString NotifiesStr;
			for (int32 i = 0; i < AnimAsset->Notifies.Num(); i++)
			{
				const FAnimNotifyEvent& N = AnimAsset->Notifies[i];
				if (!NotifiesStr.IsEmpty())
				{
					NotifiesStr += TEXT(",");
				}
				FString ClassName;
				if (N.Notify)
				{
					ClassName = N.Notify->GetClass()->GetName();
				}
				else if (N.NotifyStateClass)
				{
					ClassName = N.NotifyStateClass->GetClass()->GetName();
				}
				NotifiesStr += FString::Printf(
					TEXT("{\"i\":%d,\"name\":\"%s\",\"class\":\"%s\",\"time\":%.4f,\"dur\":%.4f}"),
					i, *N.NotifyName.ToString(), *ClassName,
					N.GetTriggerTime(), N.GetDuration());
			}

			FString NotifyEntry = FString::Printf(
				TEXT("{\"ts\":%lld,\"event\":\"anim_notifies_changed\",\"object\":\"%s\","
					"\"class\":\"%s\",\"path\":\"%s\",\"context\":\"%s\","
					"\"values\":{\"notifies\":[%s],\"count\":%d}}"),
				FDateTime::UtcNow().ToUnixTimestamp(),
				*Object->GetName(),
				*Object->GetClass()->GetName(),
				*Object->GetPathName(),
				*CurrentTransactionContext,
				*NotifiesStr,
				AnimAsset->Notifies.Num());

			AppendJsonlEntry(TransactionLogPath, NotifyEntry);
		}
	}
}

// ============================================================================
// Issue #8564 L1-L4: Generic event entry writer
void FCUModule::WriteEventEntry(const FString& EventType, const FString& Detail)
{
	FString DetailEscaped = JsonEscape(Detail);
	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"%s\",\"object\":\"%s\",\"class\":\"EditorEvent\",\"path\":\"\"}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*EventType,
		*DetailEscaped
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

// Issue #8834 B1: Diagnostic entry writer — captures ALL events before filtering
// Output: Intermediate/NarshaMCP/diagnostics.jsonl
void FCUModule::WriteDiagnosticEntry(UObject* Object, const FTransactionObjectEvent& Event, const FString& Phase)
{
	if (DiagnosticLogPath.IsEmpty() || !Object) return;

	// Collect ALL changed properties with their skip reasons
	FString PropsJson;
	const TArray<FName>& ChangedProps = Event.GetChangedProperties();
	UClass* Class = Object->GetClass();

	for (const FName& PropName : ChangedProps)
	{
		FProperty* Prop = Class ? Class->FindPropertyByName(PropName) : nullptr;
		FString Reason = TEXT("normal");
		FString Value;

		if (!Prop)
		{
			Reason = TEXT("property_not_found");
		}
		else
		{
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient))
			{
				Reason = GetNoisyTransientProps().Contains(PropName) ? TEXT("transient_blacklisted") : TEXT("transient_captured");
			}
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
			if (ValuePtr)
			{
				Prop->ExportText_Direct(Value, ValuePtr, ValuePtr, Object, PPF_None);
				Value = JsonEscape(Value);
			}
		}

		if (!PropsJson.IsEmpty()) PropsJson += TEXT(",");
		PropsJson += FString::Printf(TEXT("{\"name\":\"%s\",\"reason\":\"%s\",\"value\":\"%s\"}"),
			*PropName.ToString(), *Reason, *Value);
	}

	FString EventTypeStr;
	switch (Event.GetEventType())
	{
		case ETransactionObjectEventType::Finalized: EventTypeStr = TEXT("Finalized"); break;
		case ETransactionObjectEventType::Snapshot: EventTypeStr = TEXT("Snapshot"); break;
		default: EventTypeStr = FString::Printf(TEXT("Type_%d"), (int32)Event.GetEventType()); break;
	}

	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"phase\":\"%s\",\"event_type\":\"%s\",\"object\":\"%s\",\"class\":\"%s\","
			"\"path\":\"%s\",\"has_property_changes\":%s,\"has_non_property_changes\":%s,"
			"\"has_pending_kill\":%s,\"properties\":[%s]}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Phase, *EventTypeStr,
		*Object->GetName(), *Object->GetClass()->GetName(),
		*Object->GetPathName(),
		Event.HasPropertyChanges() ? TEXT("true") : TEXT("false"),
		Event.HasNonPropertyChanges() ? TEXT("true") : TEXT("false"),
		Event.HasPendingKillChange() ? TEXT("true") : TEXT("false"),
		*PropsJson
	);

	AppendJsonlEntry(DiagnosticLogPath, Entry);
}

// Issue #8564 M2: Asset lifecycle callbacks
// ============================================================================

void FCUModule::OnAssetAdded(const FAssetData& AssetData)
{
	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"asset_created\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\"}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*AssetData.AssetName.ToString(),
		*AssetData.AssetClassPath.ToString(),
		*AssetData.GetObjectPathString()
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

void FCUModule::OnAssetRemoved(const FAssetData& AssetData)
{
	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"asset_deleted\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\"}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*AssetData.AssetName.ToString(),
		*AssetData.AssetClassPath.ToString(),
		*AssetData.GetObjectPathString()
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

void FCUModule::OnAssetRenamed(const FAssetData& AssetData, const FString& OldPath)
{
	FString EscapedOld = JsonEscape(OldPath);
	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"asset_renamed\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\",\"values\":{\"old_path\":\"%s\"}}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*AssetData.AssetName.ToString(),
		*AssetData.AssetClassPath.ToString(),
		*AssetData.GetObjectPathString(),
		*EscapedOld
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

// Issue #8834 B2: Level actor lifecycle handlers
// ============================================================================

void FCUModule::OnLevelActorAdded(AActor* Actor)
{
	if (!Actor || TransactionLogPath.IsEmpty()) return;
	// Skip if inside transaction — OnObjectTransacted handles spawn
	if (TransactionDepth > 0) return;

	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"actor_added\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\","
			"\"values\":{\"RelativeLocation\":\"%s\"}}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Actor->GetName(),
		*Actor->GetClass()->GetName(),
		*Actor->GetPathName(),
		*JsonEscape(Actor->GetActorLocation().ToString())
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

void FCUModule::OnLevelActorDeleted(AActor* Actor)
{
	if (!Actor || TransactionLogPath.IsEmpty()) return;
	// Skip if inside transaction — OnObjectTransacted handles deletion
	if (TransactionDepth > 0) return;

	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"actor_deleted_level\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\"}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Actor->GetName(),
		*Actor->GetClass()->GetName(),
		*Actor->GetPathName()
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

void FCUModule::OnActorMoved(AActor* Actor)
{
	if (!Actor || TransactionLogPath.IsEmpty()) return;
	// Skip if inside transaction — OnObjectTransacted handles property changes
	if (TransactionDepth > 0) return;

	FVector Loc = Actor->GetActorLocation();
	FRotator Rot = Actor->GetActorRotation();

	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"actor_moved\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\","
			"\"values\":{\"RelativeLocation\":\"X=%f Y=%f Z=%f\",\"RelativeRotation\":\"P=%f Y=%f R=%f\"}}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Actor->GetName(),
		*Actor->GetClass()->GetName(),
		*Actor->GetPathName(),
		Loc.X, Loc.Y, Loc.Z,
		Rot.Pitch, Rot.Yaw, Rot.Roll
	);

	AppendJsonlEntry(TransactionLogPath, Entry);
}

// Issue #8834 Part B: Capture property changes that bypass FScopedTransaction.
//
// KNOWN LIMITATION (confirmed 2026-04-15, 15-test empirical audit):
// UE Python's set_editor_property() does NOT trigger FCoreUObjectDelegates::OnObjectPropertyChanged.
// Python calls PreEditChange/PostEditChange but the delegate broadcast is skipped in the
// PythonScriptPlugin bindings. This means this handler NEVER fires from Python API calls.
//
// This handler IS useful for:
// - C++ code that calls PostEditChangeProperty() without FScopedTransaction
// - Custom editor tools or third-party plugins that modify properties directly
// - Editor UI interactions that somehow bypass the transaction system
//
// For Python coverage, all replay scripts auto-wrap in ScopedEditorTransaction + modify().
void FCUModule::OnObjectPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent)
{
	// Skip if inside a transaction for Actors — OnObjectTransacted will handle it
	// But allow non-Actor assets through even inside transactions (Issue #9021)
	// because OnObjectTransacted.GetChangedProperties() is empty for these types.
	if (TransactionDepth > 0 && Object)
	{
		bool bIsActorOrComponent = Object->IsA(AActor::StaticClass()) ||
			(Object->GetOuter() && Object->GetOuter()->IsA(AActor::StaticClass()));
		if (bIsActorOrComponent)
		{
			return; // Actors handled by OnObjectTransacted
		}
		// Non-Actor assets fall through — capture here since OnObjectTransacted misses them
	}

	// Skip if no property changed
	if (PropertyChangedEvent.Property == nullptr)
	{
		return;
	}

	// Skip transient objects and CDOs
	if (!Object || Object->HasAnyFlags(RF_Transient) || Object->HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	// Capture actors, components, AND replayable asset types (Issue #9021)
	bool bIsActor = Object->IsA(AActor::StaticClass());
	bool bIsActorComponent = Object->GetOuter() && Object->GetOuter()->IsA(AActor::StaticClass());
	// Issue #9021: Whitelist additional asset types for transaction replay capture
	bool bIsReplayableAsset = false;
	if (!bIsActor && !bIsActorComponent)
	{
		FString ClassName = Object->GetClass()->GetName();
		bIsReplayableAsset =
			Object->IsA(UMaterial::StaticClass()) ||
			Object->IsA(UMaterialInterface::StaticClass()) ||
			Object->IsA(UAnimSequenceBase::StaticClass()) ||
			ClassName.Contains(TEXT("MaterialExpression")) ||
			ClassName.Contains(TEXT("PCG")) ||
			ClassName.Contains(TEXT("StateTree")) ||
			ClassName.Contains(TEXT("BehaviorTree")) ||
			ClassName.Contains(TEXT("NiagaraSystem")) ||
			ClassName.Contains(TEXT("NiagaraEmitter")) ||
			ClassName.Contains(TEXT("MovieScene")) ||
			ClassName.Contains(TEXT("ControlRig"));
	}
	if (!bIsActor && !bIsActorComponent && !bIsReplayableAsset)
	{
		return;
	}

	// Also skip if TransactionLogPath is not set
	if (TransactionLogPath.IsEmpty())
	{
		return;
	}

	FString PropName = PropertyChangedEvent.Property->GetName();

	// Apply same noisy transient blacklist as OnObjectTransacted
	if (GetNoisyTransientProps().Contains(FName(*PropName)))
	{
		return;
	}

	// Issue #9113 / #9131: PropertyChangedEvent.Property may not belong to Object's class.
	// Two distinct crash patterns share the same ContainerPtrToValuePtr assertion
	// (UnrealType.h:761, check((UObject*)ContainerPtr)->IsA(GetOwner<UClass>()))):
	//
	//   #9113: Property.Owner is UScriptStruct (nested struct leaf — e.g.
	//          FEditorScalarParameterValue::ParameterValue inside
	//          UMaterialEditorInstanceConstant::ScalarParameterValues). GetOwner<UClass>()
	//          returns nullptr.
	//
	//   #9131: Property.Owner is a DIFFERENT UClass than Object. UE fires
	//          OnObjectPropertyChanged on the real MI (MaterialInstanceConstant) but
	//          with Property belonging to MaterialEditorInstanceConstant (Editor proxy
	//          forwarding PostEditChangeProperty via its parent chain). GetOwner<UClass>()
	//          is valid but Object->IsA() returns false.
	//
	// Both are safely skipped — OnObjectTransacted + RC tools (SetMaterialParameter etc.)
	// cover these cases through FScopedTransaction, so the fallback delegate loses
	// negligible coverage while gaining crash safety.
	UClass* PropOwner = PropertyChangedEvent.Property->GetOwner<UClass>();
	if (!PropOwner || !Object->IsA(PropOwner))
	{
		return;
	}

	// Get the value via ExportText
	FString ValueStr;
	const void* ValuePtr = PropertyChangedEvent.Property->ContainerPtrToValuePtr<void>(Object);
	if (ValuePtr)
	{
		PropertyChangedEvent.Property->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Object, PPF_None);
		ValueStr = JsonEscape(ValueStr);
	}

	FString Entry = FString::Printf(
		TEXT("{\"ts\":%lld,\"event\":\"object_changed\",\"object\":\"%s\",\"class\":\"%s\",\"path\":\"%s\",\"context\":\"\",\"values\":{\"%s\":\"%s\"}}"),
		FDateTime::UtcNow().ToUnixTimestamp(),
		*Object->GetName(),
		*Object->GetClass()->GetName(),
		*Object->GetPathName(),
		*PropName,
		*ValueStr
	);

	AppendJsonlEntry(TransactionLogPath, Entry);

	// Broadcast via WebSocket
	if (GEditor)
	{
		UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
		if (WS && WS->IsRunning())
		{
			WS->BroadcastEvent(TEXT("object_changed"), Entry);
		}
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FCUModule, NarshaMCP)

void FCUModule::RunBlueprintMetadataExtraction(const FString& PluginDir, const FString& ProjectDir, const FString& EngineDir)
{
	// Note: Commandlet execution requires Editor mode and cannot run automatically on plugin load
	// The Commandlet must be run manually via:
	//   UnrealEditor-Cmd.exe ProjectName -run=NarshaMCP.CUBlueprintMetadata
	//
	// This function creates a helper Python script that users can run to execute the Commandlet

	FString PythonScript = FPaths::Combine(PluginDir, TEXT("Content/Python/extract_blueprint_metadata.py"));

	// Build UnrealEditor-Cmd path from EngineDir (Issue #9230: platform-aware).
	FString UECmdPath = FPaths::Combine(
		EngineDir,
		NarshaMCP::Platform::GetEditorBinariesSubdir(),
		NarshaMCP::Platform::GetEditorCmdBinaryName());

	// Create the Python helper script with auto-detected engine path
	FString ScriptContent = FString::Printf(TEXT(
		"#!/usr/bin/env python3\n"
		"\"\"\"\n"
		"Blueprint Metadata Extraction Helper\n"
		"\n"
		"Executes the BlueprintMetadata Commandlet to extract Blueprint call graph data.\n"
		"Auto-generated by NarshaMCP plugin with detected engine path.\n"
		"\"\"\"\n"
		"import subprocess\n"
		"import sys\n"
		"from pathlib import Path\n"
		"\n"
		"def main():\n"
		"    project_dir = Path(r\"%s\")\n"
		"    project_file = project_dir / (project_dir.name + \".uproject\")\n"
		"    \n"
		"    if not project_file.exists():\n"
		"        print(f\"Error: Project file not found: {project_file}\")\n"
		"        return 1\n"
		"    \n"
		"    # Auto-detected UnrealEditor-Cmd path from FPaths::EngineDir() (platform-aware)\n"
		"    ue_cmd = r\"%s\"\n"
		"    \n"
		"    if not Path(ue_cmd).exists():\n"
		"        print(f\"Error: UnrealEditor-Cmd not found at: {ue_cmd}\")\n"
		"        print(\"Please verify your Unreal Engine installation\")\n"
		"        return 1\n"
		"    \n"
		"    # Execute Commandlet (Issue #183: Startup optimization flags)\n"
		"    cmd = [\n"
		"        ue_cmd,\n"
		"        str(project_file),\n"
		"        \"-run=NarshaMCP.CUBlueprintMetadata\",\n"
		"        \"-stdout\",\n"
		"        \"-unattended\",\n"
		"        \"-nopause\",\n"
		"        \"-NoShaderCompile\",        # Disable shader compilation (saves ~16s, -18.7%%)\n"
		"        \"-nosplash\",              # Skip splash screen (saves ~1-2s)\n"
		"        \"-noloadstartuppackages\",  # Skip startup content loading (saves ~3-4s)\n"
		"        \"-nologtimes\",             # Skip log timestamp formatting (saves ~0.5s)\n"
		"        \"-Multiprocess\"             # Enable multiprocessing (parallel asset loading)\n"
		"    ]\n"
		"    \n"
		"    print(f\"Executing: {' '.join(cmd)}\")\n"
		"    result = subprocess.run(cmd)\n"
		"    \n"
		"    return result.returncode\n"
		"\n"
		"if __name__ == \"__main__\":\n"
		"    sys.exit(main())\n"
	), *ProjectDir, *UECmdPath);

	// Write the helper script
	if (FFileHelper::SaveStringToFile(ScriptContent, *PythonScript))
	{
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Created Blueprint Metadata extraction helper script"));
		UE_LOG(LogNarshaMCP, Log, TEXT("NarshaMCP: Run manually: python \"%s\""), *PythonScript);
	}
	else
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("NarshaMCP: Failed to create Blueprint Metadata extraction helper script"));
	}
}
