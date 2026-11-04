// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "Dom/JsonObject.h"
#include "CUCommandlet.generated.h"

// Forward declarations
class IAssetRegistry;

/**
 * Commandlet to extract GameplayAbility tags for static call graph analysis
 *
 * This commandlet uses Unreal's Asset Registry and Reflection System to:
 * - Find all GameplayAbility assets in the project
 * - Extract GameplayTag properties from each ability
 * - Export tag mappings to JSON for MCP tool consumption
 *
 * Usage:
 *   UnrealEditor-Cmd.exe ProjectName -run=NarshaMCP
 *
 * Output:
 *   Intermediate/NarshaMCP/GameplayAbilityTags.json
 *
 * Author: NarshaMCP v2.8.4
 * Date: 2025-10-15
 */
UCLASS()
class NARSHAMCP_API UCUCommandlet : public UCUCommandletBase
{
	GENERATED_BODY()

public:
	UCUCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
	virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override;

public:
	/**
	 * Static extraction method (for batch commandlet execution)
	 *
	 * @param OutputDir - Output directory for JSON file
	 * @param AssetRegistry - Initialized AssetRegistry instance (shared in batch mode)
	 * @return 0 on success, non-zero on error
	 * @since v4.8.0 (Batch Commandlet Support)
	 */
	static int32 RunGASExtraction(const FString& OutputDir, IAssetRegistry& AssetRegistry);

private:
	/** Extract tags from a single GameplayAbility asset */
	static bool ExtractAbilityTags(
		const FAssetData& AssetData,
		TArray<FString>& OutTags,
		FString& OutClassName
	);

	// DEPRECATED: 	/** Write JSON output to file */
	// DEPRECATED: 	static bool WriteJsonOutput(
	// DEPRECATED: 		const FString& OutputPath,
	// DEPRECATED: 		const TArray<TTuple<FAssetData, TArray<FString>, FString>>& AbilityData
	// DEPRECATED: 	);

	static FString GetIntermediateDir();

	// ============================================================================
	// Incremental Build Support (Phase 2)
	// ============================================================================

	/** Load existing JSON file (supports both .json and .json.gz) */
	static TSharedPtr<FJsonObject> LoadExistingJson(const FString& JsonPath);

	/** Find changed GameplayAbility assets by comparing saved hashes */
	static TArray<FAssetData> FindChangedAbilities(
		const TArray<FAssetData>& AllAbilities,
		const TSharedPtr<FJsonObject>& ExistingJson,
		IAssetRegistry& AssetRegistry
	);

	/** Merge new ability data with existing JSON */
	static void MergeJsonData(
		TSharedPtr<FJsonObject>& ExistingJson,
		const TArray<TTuple<FAssetData, TArray<FString>, FString>>& NewAbilityData
	);

	/** Save JSON with metadata (includes saved_hash for incremental builds) */
	static bool SaveJsonWithMetadata(
		const FString& OutputPath,
		const TSharedPtr<FJsonObject>& RootJson,
		bool bIncrementalMode
	);
};
