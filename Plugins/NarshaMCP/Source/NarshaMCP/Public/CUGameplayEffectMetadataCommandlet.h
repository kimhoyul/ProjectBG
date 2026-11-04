// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUGameplayEffectMetadataCommandlet.generated.h"

class UGameplayEffect;
struct FAssetData;
class FJsonObject;
class FJsonValue;
class IAssetRegistry;

/**
 * Commandlet for extracting GameplayEffect metadata (modifiers, duration, stacking)
 *
 * Issue #423 Phase 2B: GameplayEffect Modifiers - Complete GAS metadata extraction
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=GameplayEffectMetadata -OutputDir="Path/To/Output"
 *
 * Output:
 *   gameplayeffect_metadata.json.gz - GZIP compressed JSON file containing all GameplayEffect metadata
 *
 * Features:
 *   - Extracts FGameplayModifierInfo array (attribute, operation, magnitude)
 *   - Extracts duration policy (Instant, Infinite, HasDuration)
 *   - Extracts stacking configuration
 *   - Extracts GameplayTags (AssetTags, GrantedTags, RemovalTags)
 *   - 100% accurate (uses official Unreal GameplayAbilities APIs)
 *
 * Architecture (MaterialMetadata pattern reuse):
 *   - Same Commandlet → JSON → Parser → Policy Tool pattern
 *   - Lazy loading support (Issue #21)
 *   - GZIP compression (v4.6.0)
 *   - Batch commandlet support (v4.8.0)
 *   - Incremental build support (Issue #108)
 *
 * @since v4.21.0 (Issue #423 Phase 2B - GameplayEffect Modifiers)
 */
UCLASS()
class NARSHAMCP_API UCUGameplayEffectMetadataCommandlet : public UCUCommandletBase
{
	GENERATED_BODY()

public:
	UCUGameplayEffectMetadataCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
	/**
	 * Run extraction logic - Overrides CommandletBase::RunExtraction()
	 *
	 * Performs GameplayEffect metadata extraction work.
	 * Called automatically by CommandletBase::Main().
	 *
	 * @param OutputDir - Output directory for JSON file
	 * @param Registry - Initialized AssetRegistry instance
	 * @return 0 on success, non-zero on error
	 *
	 * @since Issue #289 v2 - CommandletBase framework
	 */
	virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override;

public:
	/**
	 * Static extraction method (for batch commandlet execution)
	 *
	 * Kept for backward compatibility with CUBatchCommandlet.
	 *
	 * @param OutputDir - Output directory for JSON file
	 * @param AssetRegistry - Initialized AssetRegistry instance (shared in batch mode)
	 * @return 0 on success, non-zero on error
	 *
	 * @since v4.21.0 (Issue #423 - Batch Commandlet Support)
	 */
	static int32 RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry);

private:
	// ============================================================================
	// GameplayEffect Extraction Functions
	// ============================================================================

	/**
	 * Extract metadata from a single GameplayEffect asset
	 *
	 * Extracts modifiers, duration, stacking, and tags information.
	 *
	 * @param GameplayEffect - GameplayEffect asset to extract
	 * @param AssetData - Asset registry data
	 * @param AssetRegistry - Asset registry for hash lookups
	 * @return JSON object with GameplayEffect metadata
	 */
	static TSharedPtr<FJsonObject> ExtractGameplayEffectMetadata(
		UGameplayEffect* GameplayEffect,
		const FAssetData& AssetData,
		IAssetRegistry& AssetRegistry
	);

	/**
	 * Extract FGameplayModifierInfo array from GameplayEffect
	 *
	 * Iterates GameplayEffect->Modifiers array and extracts:
	 * - Attribute (e.g., "Health.Current", "Damage.Base")
	 * - ModifierOp (Add, Multiply, Divide, Override)
	 * - ModifierMagnitude (ScalableFloat, AttributeBased, CustomCalculation, SetByCaller)
	 *
	 * @param GameplayEffect - GameplayEffect asset
	 * @return JSON array of modifiers
	 */
	static TArray<TSharedPtr<FJsonValue>> ExtractModifiers(UGameplayEffect* GameplayEffect);

	/**
	 * Extract duration configuration from GameplayEffect
	 *
	 * Extracts:
	 * - DurationPolicy (Instant, Infinite, HasDuration)
	 * - DurationMagnitude (for HasDuration policy)
	 * - Period (for periodic effects)
	 *
	 * @param GameplayEffect - GameplayEffect asset
	 * @return JSON object with duration configuration
	 */
	static TSharedPtr<FJsonObject> ExtractDurationConfig(UGameplayEffect* GameplayEffect);

	/**
	 * Extract stacking configuration from GameplayEffect
	 *
	 * Extracts:
	 * - StackingType (None, AggregateBySource, AggregateByTarget)
	 * - StackLimitCount
	 * - StackDurationRefreshPolicy
	 * - StackPeriodResetPolicy
	 *
	 * @param GameplayEffect - GameplayEffect asset
	 * @return JSON object with stacking configuration
	 */
	static TSharedPtr<FJsonObject> ExtractStackingConfig(UGameplayEffect* GameplayEffect);

	/**
	 * Extract GameplayTag containers from GameplayEffect
	 *
	 * Extracts:
	 * - AssetTags (tags identifying this effect)
	 * - GrantedTags (tags granted to target while effect is active)
	 * - RemovalTags (tags that remove this effect when applied)
	 * - OngoingTagRequirements (tags required for effect to remain active)
	 * - ApplicationTagRequirements (tags required for effect to be applied)
	 *
	 * @param GameplayEffect - GameplayEffect asset
	 * @return JSON object with tag containers
	 */
	static TSharedPtr<FJsonObject> ExtractGameplayTags(UGameplayEffect* GameplayEffect);

	// ============================================================================
	// Modifier Serialization Functions
	// ============================================================================

	/**
	 * Serialize FGameplayAttribute to JSON
	 *
	 * Converts attribute to string format: "AttributeSet.AttributeName"
	 * Example: "UHealthAttributeSet.Health" → "Health.Current"
	 *
	 * @param Attribute - GameplayAttribute to serialize
	 * @return Attribute name string
	 */
	static FString SerializeAttribute(const struct FGameplayAttribute& Attribute);

	/**
	 * Serialize EGameplayModOp::Type to string
	 *
	 * Converts enum to human-readable string:
	 * - EGameplayModOp::Additive → "Add"
	 * - EGameplayModOp::Multiplicative → "Multiply"
	 * - EGameplayModOp::Division → "Divide"
	 * - EGameplayModOp::Override → "Override"
	 *
	 * @param ModifierOp - Modifier operation enum
	 * @return Operation name string
	 */
	static FString SerializeModifierOp(uint8 ModifierOp);

	/**
	 * Serialize FGameplayEffectModifierMagnitude to JSON
	 *
	 * Handles multiple magnitude types:
	 * - ScalableFloat: { "type": "ScalableFloat", "value": 50.0, "curve": null }
	 * - AttributeBased: { "type": "AttributeBased", "attribute": "Health.Max", "coefficient": 0.5 }
	 * - CustomCalculation: { "type": "CustomCalculation", "class": "UDamageCalculation" }
	 * - SetByCaller: { "type": "SetByCaller", "tag": "Data.Damage" }
	 *
	 * MVP: Only ScalableFloat fully supported, others marked as "complex"
	 *
	 * @param Magnitude - Modifier magnitude struct
	 * @return JSON object with magnitude data
	 */
	static TSharedPtr<FJsonObject> SerializeMagnitude(const struct FGameplayEffectModifierMagnitude& Magnitude);

	/**
	 * Serialize FGameplayTagContainer to JSON array
	 *
	 * Converts tag container to array of tag strings.
	 * Example: ["Ability.Attack", "State.Active"]
	 *
	 * @param TagContainer - GameplayTag container
	 * @return JSON array of tag strings
	 */
	static TArray<TSharedPtr<FJsonValue>> SerializeTagContainer(const struct FGameplayTagContainer& TagContainer);

	// ============================================================================
	// Helper Functions
	// ============================================================================

	/**
	 * Create basic asset JSON object
	 *
	 * Creates JSON object with common asset fields (name, path, class, saved_hash).
	 *
	 * @param AssetName - Asset name
	 * @param AssetData - Asset registry data
	 * @param AssetRegistry - Asset registry for SavedHash lookup
	 * @return JSON object with basic fields populated
	 */
	static TSharedPtr<FJsonObject> CreateBasicAssetJson(
		const FString& AssetName,
		const FAssetData& AssetData,
		IAssetRegistry& AssetRegistry
	);

	// ============================================================================
	// JSON Building Functions
	// ============================================================================

	/**
	 * Build final JSON output with all metadata
	 *
	 * Creates root JSON object with arrays and metadata fields.
	 *
	 * @param GameplayEffectsArray - Extracted GameplayEffect data
	 * @param ProcessedCount - GameplayEffect count
	 * @param ErrorCount - Extraction errors
	 * @return Root JSON object
	 */
	static TSharedPtr<FJsonObject> BuildFinalJsonObject(
		const TArray<TSharedPtr<FJsonValue>>& GameplayEffectsArray,
		int32 ProcessedCount,
		int32 ErrorCount
	);

	/**
	 * Save JSON with GZIP compression
	 *
	 * Saves JSON to file with GZIP compression (v4.6.0 pattern).
	 * Uses UE 5.6-compatible FCompression API.
	 *
	 * @param JsonPath - Output path for gameplayeffect_metadata.json
	 * @param JsonData - Complete JSON object to save
	 * @return true if saved successfully
	 */
	static bool SaveJsonWithMetadata(
		const FString& JsonPath,
		const TSharedPtr<FJsonObject>& JsonData
	);
};
