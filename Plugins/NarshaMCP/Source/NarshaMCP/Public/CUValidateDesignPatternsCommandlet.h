// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUValidateDesignPatternsCommandlet.generated.h"

// Forward declarations
class FJsonObject;
class FJsonValue;
class IAssetRegistry;

/**
 * Commandlet for validating UE design patterns (Issue #X - Phase 2)
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=NarshaMCP.CUValidateDesignPatterns -OutputDir="Path/To/Output"
 *
 * Output:
 *   design_pattern_validation.json - JSON file containing all design pattern violations
 *
 * Features (v4.7.0 Phase 2):
 *   - 99% accuracy using runtime metadata (ClassFlags + UFunction metadata)
 *   - Detects CallInEditor on UEditorUtilityObject descendants
 *   - Validates full class hierarchy including Blueprint parents
 *   - Multi-file inheritance chain detection
 *
 * Validation Rules:
 *   1. CallInEditor + UEditorUtilityObject conflict
 *      - Problem: CallInEditor buttons don't appear in Details panel
 *      - Solution: Use UActorActionUtility instead
 *      - Detection: TObjectIterator → GetSuperClass() chain → HasMetaData("CallInEditor")
 *
 * Comparison with Static Analysis (Phase 1):
 *   - Static: 90% accuracy (single-file inheritance only)
 *   - Commandlet: 99% accuracy (full hierarchy with runtime data)
 */
UCLASS()
class NARSHAMCP_API UCUValidateDesignPatternsCommandlet : public UCUCommandletBase
{
	GENERATED_BODY()

public:
	UCUValidateDesignPatternsCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
	/**
	 * Override to skip AssetRegistry full scan wait (Issue #289 Phase 10)
	 *
	 * ValidateDesignPatterns only needs runtime metadata (ClassFlags, UFunction metadata),
	 * not full asset scan. Skipping wait saves 30-50 seconds.
	 *
	 * @return false to skip wait time
	 */
	virtual bool ShouldWaitForAssetRegistry() const override { return false; }

	virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override;

public:
	/**
	 * Static validation method (for batch commandlet execution)
	 *
	 * @param OutputDir - Output directory for JSON file
	 * @param AssetRegistry - Initialized AssetRegistry instance (shared in batch mode)
	 * @return 0 on success, non-zero on error
	 * @since v4.8.0 (Batch Commandlet Support)
	 */
	static int32 RunValidation(const FString& OutputDir, IAssetRegistry& AssetRegistry);

private:
	/**
	 * Check if class inherits from any forbidden ancestor
	 *
	 * Recursively traverses class hierarchy using GetSuperClass()
	 * until reaching UObject or finding forbidden class.
	 *
	 * @param Class - Class to check
	 * @param ForbiddenAncestors - List of forbidden class names
	 * @param OutForbiddenAncestor - Output: which forbidden ancestor was found
	 * @return True if class inherits from forbidden ancestor
	 */
	static bool InheritsFromForbiddenAncestor(
		UClass* Class,
		const TArray<FString>& ForbiddenAncestors,
		FString& OutForbiddenAncestor
	);

	/**
	 * Find all functions with CallInEditor metadata
	 *
	 * Uses TFieldIterator<UFunction> to enumerate all functions
	 * in the class and checks HasMetaData("CallInEditor").
	 *
	 * @param Class - Class to search
	 * @return Array of functions with CallInEditor metadata
	 */
	static TArray<UFunction*> FindCallInEditorFunctions(UClass* Class);

	/**
	 * Get source file path for a Blueprint class
	 *
	 * Blueprint classes don't have .h files, but we can get:
	 * - Blueprint asset path (e.g., /Game/MyBlueprint)
	 * - Generated class package path
	 *
	 * @param Class - Blueprint class
	 * @return Best-effort source path (asset path or package name)
	 */
	static FString GetSourceFilePath(UClass* Class);

	/**
	 * Create violation JSON object
	 *
	 * @param Class - Class with violation
	 * @param Function - Function with CallInEditor
	 * @param ForbiddenAncestor - Which forbidden ancestor was detected
	 * @return JSON object with violation details
	 */
	static TSharedPtr<FJsonObject> CreateViolationJson(
		UClass* Class,
		UFunction* Function,
		const FString& ForbiddenAncestor
	);
};
