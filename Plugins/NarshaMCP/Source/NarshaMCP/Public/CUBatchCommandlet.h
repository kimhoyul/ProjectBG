// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUBatchCommandlet.generated.h"

// Forward declarations
class IAssetRegistry;

/**
 * Batch commandlet for running multiple NarshaMCP commandlets in a single process
 *
 * Purpose: Eliminate initialization overhead by running all commandlets sequentially
 * in a single UE Editor instance with shared AssetRegistry.
 *
 * Performance Improvement:
 * - Before (individual): 250 seconds (5× 50s initialization)
 * - After (batch): 100 seconds (1× 50s initialization + work)
 * - Savings: 60% time reduction, 80% memory reduction
 *
 * Usage:
 *   # Run all commandlets
 *   UnrealEditor-Cmd.exe Project.uproject -run=NarshaMCP.CUBatch
 *
 *   # Run specific commandlets
 *   UnrealEditor-Cmd.exe Project.uproject -run=NarshaMCP.CUBatch -Tasks=ValidatePatterns,BlueprintMetadata
 *
 *   # Custom output directory
 *   UnrealEditor-Cmd.exe Project.uproject -run=NarshaMCP.CUBatch -OutputDir="D:/Custom/Output"
 *
 * Supported Tasks (subset; full default order via GetDefaultTasks):
 *   - ValidatePatterns: Design pattern validation (CallInEditor conflicts)
 *   - BlueprintMetadata: Blueprint metadata (all Blueprints, incl. ControlRig/ModularRig)
 *   - UniversalAsset: All asset metadata extraction
 *   - PCGMetadata: PCG Graph metadata extraction
 *   - NarshaMCP: GameplayAbility Tag extraction
 *
 * Backward Compatibility:
 *   Individual commandlets still work as before:
 *   - ValidateDesignPatterns
 *   - BlueprintMetadata
 *   - UniversalAssetMetadata
 *   - PCGMetadata
 *   - NarshaMCP
 *
 * @since v4.8.0 (Batch Commandlet Implementation)
 */
UCLASS()
class NARSHAMCP_API UCUBatchCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUBatchCommandlet(const FObjectInitializer& ObjectInitializer);

	/**
	 * Main entry point for the batch commandlet
	 *
	 * Workflow:
	 *   1. Parse output directory and task list from parameters
	 *   2. Initialize AssetRegistry once (shared by all tasks)
	 *   3. Run each task sequentially
	 *   4. Report final status
	 *
	 * @param Params - Command line parameters
	 *                 Supported: OutputDir=path, Tasks=task1,task2,...
	 * @return 0 on success, non-zero on error (accumulated error count)
	 */
	virtual int32 Main(const FString& Params) override;

private:
	/**
	 * Execute a single commandlet task
	 *
	 * Dispatches to the appropriate commandlet's static RunXXX() function.
	 *
	 * @param TaskName - Task identifier (e.g., "ValidatePatterns")
	 * @param OutputDir - Output directory for JSON files
	 * @param AssetRegistry - Initialized AssetRegistry instance (shared)
	 * @return Error count from task (0 = success)
	 */
	int32 ExecuteTask(const FString& TaskName, const FString& OutputDir, IAssetRegistry& AssetRegistry);

	/**
	 * Get default task list
	 *
	 * Returns the default tasks in recommended execution order.
	 * Order: Fastest to slowest.
	 *
	 * @return Array of default task names
	 */
	TArray<FString> GetDefaultTasks() const;

	/**
	 * Validate task name
	 *
	 * Checks if task name is valid and supported.
	 *
	 * @param TaskName - Task identifier to validate
	 * @return True if task is valid, false otherwise
	 */
	bool IsValidTask(const FString& TaskName) const;
};
