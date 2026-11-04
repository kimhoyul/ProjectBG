// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CommandletFramework/CUCommandletBase.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/IAssetRegistry.h"

#include "CULog.h"

UCUCommandletBase::UCUCommandletBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UCUCommandletBase::Main(const FString& Params)
{
	// Get commandlet name for logging and profiling
	FString CommandletName = GetClass()->GetName();
	CommandletName.RemoveFromEnd(TEXT("_C")); // Remove Blueprint suffix if present

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[%s] Starting..."), *CommandletName);

	// Parse parameters using CommandletHelpers
	FString OutputDir = FCUCommandletHelpers::ParseOutputDirectory(Params);
	if (!FCUCommandletHelpers::EnsureOutputDirectory(OutputDir))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[%s] Failed to create output directory: %s"), *CommandletName, *OutputDir);
		return 1;
	}

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[%s] Output directory: %s"), *CommandletName, *OutputDir);

	// Initialize AssetRegistry using CommandletHelpers
	// Issue #289 Phase 10: Allow child classes to opt-out of wait time (e.g., ValidatePatterns)
	bool bWaitForCompletion = ShouldWaitForAssetRegistry();
	FCUCommandletHelpers::BeginProfileSection(CommandletName + TEXT(".AssetRegistryInit"));
	IAssetRegistry& AssetRegistry = FCUCommandletHelpers::InitializeAssetRegistry(bWaitForCompletion);
	double InitTime = FCUCommandletHelpers::EndProfileSection(CommandletName + TEXT(".AssetRegistryInit"));

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[%s] AssetRegistry initialized (%.1fs)"), *CommandletName, InitTime);

	// Profile the extraction work
	FCUCommandletHelpers::BeginProfileSection(CommandletName + TEXT(".Extraction"));
	int32 Result = RunExtraction(OutputDir, AssetRegistry);
	double ExtractionTime = FCUCommandletHelpers::EndProfileSection(CommandletName + TEXT(".Extraction"));

	// Log results
	if (Result == 0)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[%s] ✓ Completed successfully (%.1fs)"), *CommandletName, ExtractionTime);
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[%s] ✗ Completed with errors: %d (%.1fs)"), *CommandletName, Result, ExtractionTime);
	}

	// Log profiling summary
	FCUCommandletHelpers::LogProfileResults();

	return Result;
}

int32 UCUCommandletBase::RunExtraction(const FString& OutputDir, class IAssetRegistry& Registry)
{
	UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("RunExtraction not implemented! Override this in child class."));
	return -1;
}
