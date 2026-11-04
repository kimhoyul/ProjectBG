// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUGameplayEffectMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayEffect.h"
#endif
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayEffectTypes.h"
#endif
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayModMagnitudeCalculation.h"  // UE 5.6: Required for TSubclassOf<UGameplayModMagnitudeCalculation>
#endif
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "AttributeSet.h"
#endif
#include "GameplayTagContainer.h"
// Blueprint support (GameplayEffects are Blueprintable)
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "AssetRegistry/AssetData.h"
// UE 5.6: Component-based GameplayEffect API includes
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayEffectComponents/TargetTagRequirementsGameplayEffectComponent.h"
#endif
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayEffectComponents/AssetTagsGameplayEffectComponent.h"
#endif
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
#endif
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Compression.h"  // GZIP compression support

// Hash support for incremental build
#include "IO/IoHash.h"
#include "AssetRegistry/AssetRegistryState.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPGAS, Log, All);

UCUGameplayEffectMetadataCommandlet::UCUGameplayEffectMetadataCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

// Override CommandletBase::RunExtraction() - called by CommandletBase::Main()
int32 UCUGameplayEffectMetadataCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
	return RunExtractionStatic(OutputDir, Registry);
}

// Static method for batch commandlet (backward compatibility)
int32 UCUGameplayEffectMetadataCommandlet::RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	UE_LOG(LogNarshaMCPGAS, Display, TEXT("[GameplayEffectMetadata] Running extraction..."));

	// Find all Blueprint assets (GameplayEffects are Blueprintable, not DataAssets)
	// UGameplayEffect is UCLASS(Blueprintable), so GE_* assets are Blueprint instances
	FARFilter BlueprintFilter;
	BlueprintFilter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	BlueprintFilter.bRecursiveClasses = true;

	TArray<FAssetData> AllBlueprints;
	AssetRegistry.GetAssets(BlueprintFilter, AllBlueprints);

	// Filter Blueprints to find only GameplayEffect-derived ones
	TArray<FAssetData> GameplayEffectAssetList;
	for (const FAssetData& AssetData : AllBlueprints)
	{
		// Check if Blueprint's ParentClass is UGameplayEffect
		FString ParentClassPath;
		if (AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath))
		{
			// ParentClassPath format: "/Script/GameplayAbilities.GameplayEffect"
			if (ParentClassPath.Contains(TEXT("GameplayEffect")))
			{
				GameplayEffectAssetList.Add(AssetData);
			}
		}
	}

	UE_LOG(LogNarshaMCPGAS, Display, TEXT("[GameplayEffectMetadata] Found %d GameplayEffect Blueprint assets (from %d total Blueprints)"),
	       GameplayEffectAssetList.Num(), AllBlueprints.Num());

	// Root JSON object
	TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);
	TArray<TSharedPtr<FJsonValue>> GameplayEffectsArray;

	// Thread-safe counters
	int32 ProcessedCount = 0;
	int32 ErrorCount = 0;

	UE_LOG(LogNarshaMCPGAS, Display, TEXT("[GameplayEffectMetadata] Processing %d GameplayEffect assets..."), GameplayEffectAssetList.Num());

	FDateTime StartTime = FDateTime::UtcNow();
	int32 TotalCount = GameplayEffectAssetList.Num();
	int32 ProgressStep = FMath::Max(1, TotalCount / 10);  // 10% intervals

	// Sequential processing
	for (int32 Index = 0; Index < GameplayEffectAssetList.Num(); ++Index)
	{
		const FAssetData& AssetData = GameplayEffectAssetList[Index];

		// Progress reporting
		if ((Index + 1) % ProgressStep == 0 || Index == TotalCount - 1)
		{
			float Progress = (Index + 1) * 100.0f / TotalCount;
			UE_LOG(LogNarshaMCPGAS, Display, TEXT("[GameplayEffectMetadata] Progress: %d/%d (%.1f%%)"),
			       Index + 1, TotalCount, Progress);
		}

		// Load GameplayEffect asset
		UObject* Asset = AssetData.GetAsset();
		if (!Asset)
		{
			UE_LOG(LogNarshaMCPGAS, Warning, TEXT("[GameplayEffectMetadata] Failed to load asset: %s"), *AssetData.GetObjectPathString());
			ErrorCount++;
			continue;
		}

		// Blueprint assets need special handling - get CDO from GeneratedClass
		UGameplayEffect* GameplayEffect = nullptr;
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (Blueprint && Blueprint->GeneratedClass)
		{
			// Get Class Default Object (CDO) which is a UGameplayEffect instance
			GameplayEffect = Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject());
		}

		if (!GameplayEffect)
		{
			UE_LOG(LogNarshaMCPGAS, Warning, TEXT("[GameplayEffectMetadata] Asset is not a GameplayEffect Blueprint: %s"), *AssetData.GetObjectPathString());
			ErrorCount++;
			continue;
		}

		// Extract GameplayEffect metadata
		TSharedPtr<FJsonObject> AssetJson = ExtractGameplayEffectMetadata(GameplayEffect, AssetData, AssetRegistry);
		if (AssetJson.IsValid())
		{
			GameplayEffectsArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
			ProcessedCount++;
		}
		else
		{
			UE_LOG(LogNarshaMCPGAS, Warning, TEXT("[GameplayEffectMetadata] Failed to extract metadata: %s"), *AssetData.GetObjectPathString());
			ErrorCount++;
		}
	}

	FDateTime EndTime = FDateTime::UtcNow();
	FTimespan Duration = EndTime - StartTime;

	UE_LOG(LogNarshaMCPGAS, Display, TEXT("[GameplayEffectMetadata] Extraction complete: Processed %d, Errors %d, Time: %.2fs"),
	       ProcessedCount, ErrorCount, Duration.GetTotalSeconds());

	// Build final JSON object
	TSharedPtr<FJsonObject> FinalJson = BuildFinalJsonObject(GameplayEffectsArray, ProcessedCount, ErrorCount);

	// Write JSON to file with GZIP compression
	FString JsonPath = OutputDir / TEXT("gameplayeffect_metadata.json");
	bool bSuccess = SaveJsonWithMetadata(JsonPath, FinalJson);

	if (bSuccess)
	{
		UE_LOG(LogNarshaMCPGAS, Display, TEXT("[GameplayEffectMetadata] Successfully saved to: %s"), *JsonPath);
		return ErrorCount;  // Return error count (0 = success)
	}
	else
	{
		UE_LOG(LogNarshaMCPGAS, Error, TEXT("[GameplayEffectMetadata] Failed to save JSON file"));
		return 1;
	}
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	UE_LOG(LogNarshaMCPGAS, Error, TEXT("GameplayEffect metadata extraction requires the "
		"GameplayAbilities plugin, which this project has not enabled. "
		"Enable GameplayAbilities in your .uproject, then rebuild."));
	return 1;
#endif
}

// ============================================================================
// GameplayEffect Extraction Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::ExtractGameplayEffectMetadata(
	UGameplayEffect* GameplayEffect,
	const FAssetData& AssetData,
	IAssetRegistry& AssetRegistry)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	if (!GameplayEffect)
	{
		return nullptr;
	}

	// Create basic asset JSON
	FString AssetName = AssetData.AssetName.ToString();
	TSharedPtr<FJsonObject> Json = CreateBasicAssetJson(AssetName, AssetData, AssetRegistry);

	// Extract modifiers (Day 2 implementation)
	TArray<TSharedPtr<FJsonValue>> Modifiers = ExtractModifiers(GameplayEffect);
	Json->SetArrayField(TEXT("modifiers"), Modifiers);

	// Extract duration config (Day 2 implementation)
	TSharedPtr<FJsonObject> DurationConfig = ExtractDurationConfig(GameplayEffect);
	Json->SetObjectField(TEXT("duration"), DurationConfig);

	// Extract stacking config (Day 2 implementation)
	TSharedPtr<FJsonObject> StackingConfig = ExtractStackingConfig(GameplayEffect);
	Json->SetObjectField(TEXT("stacking"), StackingConfig);

	// Extract GameplayTags (Day 2 implementation)
	TSharedPtr<FJsonObject> Tags = ExtractGameplayTags(GameplayEffect);
	Json->SetObjectField(TEXT("tags"), Tags);

	return Json;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return nullptr;
#endif
}

TArray<TSharedPtr<FJsonValue>> UCUGameplayEffectMetadataCommandlet::ExtractModifiers(UGameplayEffect* GameplayEffect)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	TArray<TSharedPtr<FJsonValue>> Modifiers;

	if (!GameplayEffect)
	{
		return Modifiers;
	}

	// Access Modifiers array from GameplayEffect
	// UGameplayEffect has a public Modifiers array of type TArray<FGameplayModifierInfo>
	const TArray<FGameplayModifierInfo>& ModifiersArray = GameplayEffect->Modifiers;

	for (const FGameplayModifierInfo& ModifierInfo : ModifiersArray)
	{
		TSharedPtr<FJsonObject> ModifierJson = MakeShareable(new FJsonObject);

		// Serialize attribute (e.g., "Health.Current", "Damage.Base")
		FString AttributeName = SerializeAttribute(ModifierInfo.Attribute);
		ModifierJson->SetStringField(TEXT("attribute"), AttributeName);

		// Serialize modifier operation (Add, Multiply, Divide, Override)
		FString OperationName = SerializeModifierOp(static_cast<uint8>(ModifierInfo.ModifierOp));
		ModifierJson->SetStringField(TEXT("operation"), OperationName);

		// Serialize magnitude (ScalableFloat, AttributeBased, CustomCalculation, SetByCaller)
		TSharedPtr<FJsonObject> MagnitudeJson = SerializeMagnitude(ModifierInfo.ModifierMagnitude);
		ModifierJson->SetObjectField(TEXT("magnitude"), MagnitudeJson);

		Modifiers.Add(MakeShareable(new FJsonValueObject(ModifierJson)));
	}

	return Modifiers;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return TArray<TSharedPtr<FJsonValue>>();
#endif
}

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::ExtractDurationConfig(UGameplayEffect* GameplayEffect)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	TSharedPtr<FJsonObject> DurationConfig = MakeShareable(new FJsonObject);

	if (!GameplayEffect)
	{
		DurationConfig->SetStringField(TEXT("policy"), TEXT("Unknown"));
		return DurationConfig;
	}

	// Extract duration policy (Instant, Infinite, HasDuration)
	FString PolicyName;
	switch (GameplayEffect->DurationPolicy)
	{
	case EGameplayEffectDurationType::Instant:
		PolicyName = TEXT("Instant");
		break;
	case EGameplayEffectDurationType::Infinite:
		PolicyName = TEXT("Infinite");
		break;
	case EGameplayEffectDurationType::HasDuration:
		PolicyName = TEXT("HasDuration");
		break;
	default:
		PolicyName = TEXT("Unknown");
		break;
	}
	DurationConfig->SetStringField(TEXT("policy"), PolicyName);

	// Extract duration magnitude (for HasDuration policy)
	if (GameplayEffect->DurationPolicy == EGameplayEffectDurationType::HasDuration)
	{
		TSharedPtr<FJsonObject> MagnitudeJson = SerializeMagnitude(GameplayEffect->DurationMagnitude);
		DurationConfig->SetObjectField(TEXT("magnitude"), MagnitudeJson);
	}

	// Extract period (for periodic effects)
	if (GameplayEffect->Period.Value > 0.0f)
	{
		TSharedPtr<FJsonObject> PeriodJson = SerializeMagnitude(GameplayEffect->Period);
		DurationConfig->SetObjectField(TEXT("period"), PeriodJson);
	}

	return DurationConfig;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return nullptr;
#endif
}

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::ExtractStackingConfig(UGameplayEffect* GameplayEffect)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	TSharedPtr<FJsonObject> StackingConfig = MakeShareable(new FJsonObject);

	if (!GameplayEffect)
	{
		StackingConfig->SetStringField(TEXT("type"), TEXT("None"));
		return StackingConfig;
	}

	// Extract stacking type (None, AggregateBySource, AggregateByTarget)
	FString StackingTypeName;
	// UE 5.7+: StackingType deprecated but GetStackingType() not exported (linker error)
	// Using PRAGMA to suppress deprecation warning while maintaining functionality
	// PRAGMA preferred over #if conditionals when replacement API unavailable (linker/export issues)
	// TEST: Verify both UE 5.6 StackingType access and UE 5.7+ PRAGMA suppression work without warnings/errors
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	switch (GameplayEffect->StackingType)
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
	case EGameplayEffectStackingType::None:
		StackingTypeName = TEXT("None");
		break;
	case EGameplayEffectStackingType::AggregateBySource:
		StackingTypeName = TEXT("AggregateBySource");
		break;
	case EGameplayEffectStackingType::AggregateByTarget:
		StackingTypeName = TEXT("AggregateByTarget");
		break;
	default:
		StackingTypeName = TEXT("Unknown");
		break;
	}
	StackingConfig->SetStringField(TEXT("type"), StackingTypeName);

	// Extract stack limit
	StackingConfig->SetNumberField(TEXT("limit"), GameplayEffect->StackLimitCount);

	// Extract stack duration refresh policy
	FString DurationRefreshPolicy;
	switch (GameplayEffect->StackDurationRefreshPolicy)
	{
	case EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication:
		DurationRefreshPolicy = TEXT("RefreshOnSuccessfulApplication");
		break;
	case EGameplayEffectStackingDurationPolicy::NeverRefresh:
		DurationRefreshPolicy = TEXT("NeverRefresh");
		break;
	default:
		DurationRefreshPolicy = TEXT("Unknown");
		break;
	}
	StackingConfig->SetStringField(TEXT("duration_refresh_policy"), DurationRefreshPolicy);

	// Extract stack period reset policy
	FString PeriodResetPolicy;
	switch (GameplayEffect->StackPeriodResetPolicy)
	{
	case EGameplayEffectStackingPeriodPolicy::ResetOnSuccessfulApplication:
		PeriodResetPolicy = TEXT("ResetOnSuccessfulApplication");
		break;
	case EGameplayEffectStackingPeriodPolicy::NeverReset:
		PeriodResetPolicy = TEXT("NeverReset");
		break;
	default:
		PeriodResetPolicy = TEXT("Unknown");
		break;
	}
	StackingConfig->SetStringField(TEXT("period_reset_policy"), PeriodResetPolicy);

	return StackingConfig;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return nullptr;
#endif
}

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::ExtractGameplayTags(UGameplayEffect* GameplayEffect)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	TSharedPtr<FJsonObject> Tags = MakeShareable(new FJsonObject);

	if (!GameplayEffect)
	{
		TArray<TSharedPtr<FJsonValue>> EmptyArray;
		Tags->SetArrayField(TEXT("asset_tags"), EmptyArray);
		Tags->SetArrayField(TEXT("granted_tags"), EmptyArray);
		Tags->SetArrayField(TEXT("blocked_ability_tags"), EmptyArray);
		return Tags;
	}

	// UE 5.6: GetAssetTags() public API (replaces InheritableGameplayEffectTags)
	TArray<TSharedPtr<FJsonValue>> AssetTags = SerializeTagContainer(GameplayEffect->GetAssetTags());
	Tags->SetArrayField(TEXT("asset_tags"), AssetTags);

	// UE 5.6: GetGrantedTags() public API (replaces InheritableOwnedTagsContainer)
	TArray<TSharedPtr<FJsonValue>> GrantedTags = SerializeTagContainer(GameplayEffect->GetGrantedTags());
	Tags->SetArrayField(TEXT("granted_tags"), GrantedTags);

	// UE 5.6: GetBlockedAbilityTags() public API
	TArray<TSharedPtr<FJsonValue>> BlockedAbilityTags = SerializeTagContainer(GameplayEffect->GetBlockedAbilityTags());
	Tags->SetArrayField(TEXT("blocked_ability_tags"), BlockedAbilityTags);

	// UE 5.6: Component-based Tag Requirements API
	// FindComponent<T>() searches for UTargetTagRequirementsGameplayEffectComponent
	const UTargetTagRequirementsGameplayEffectComponent* TagReqComp =
		GameplayEffect->FindComponent<UTargetTagRequirementsGameplayEffectComponent>();

	if (TagReqComp)
	{
		// Extract OngoingTagRequirements (tags required for effect to remain active)
		if (TagReqComp->OngoingTagRequirements.RequireTags.Num() > 0 ||
			TagReqComp->OngoingTagRequirements.IgnoreTags.Num() > 0)
		{
			TSharedPtr<FJsonObject> OngoingReqs = MakeShareable(new FJsonObject);
			OngoingReqs->SetArrayField(TEXT("require"), SerializeTagContainer(TagReqComp->OngoingTagRequirements.RequireTags));
			OngoingReqs->SetArrayField(TEXT("ignore"), SerializeTagContainer(TagReqComp->OngoingTagRequirements.IgnoreTags));
			Tags->SetObjectField(TEXT("ongoing_requirements"), OngoingReqs);
		}

		// Extract ApplicationTagRequirements (tags required for effect to be applied)
		if (TagReqComp->ApplicationTagRequirements.RequireTags.Num() > 0 ||
			TagReqComp->ApplicationTagRequirements.IgnoreTags.Num() > 0)
		{
			TSharedPtr<FJsonObject> ApplicationReqs = MakeShareable(new FJsonObject);
			ApplicationReqs->SetArrayField(TEXT("require"), SerializeTagContainer(TagReqComp->ApplicationTagRequirements.RequireTags));
			ApplicationReqs->SetArrayField(TEXT("ignore"), SerializeTagContainer(TagReqComp->ApplicationTagRequirements.IgnoreTags));
			Tags->SetObjectField(TEXT("application_requirements"), ApplicationReqs);
		}

		// Extract RemovalTagRequirements (UE 5.6 new component field)
		if (TagReqComp->RemovalTagRequirements.RequireTags.Num() > 0 ||
			TagReqComp->RemovalTagRequirements.IgnoreTags.Num() > 0)
		{
			TSharedPtr<FJsonObject> RemovalReqs = MakeShareable(new FJsonObject);
			RemovalReqs->SetArrayField(TEXT("require"), SerializeTagContainer(TagReqComp->RemovalTagRequirements.RequireTags));
			RemovalReqs->SetArrayField(TEXT("ignore"), SerializeTagContainer(TagReqComp->RemovalTagRequirements.IgnoreTags));
			Tags->SetObjectField(TEXT("removal_requirements"), RemovalReqs);
		}
	}

	return Tags;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return nullptr;
#endif
}

// ============================================================================
// Modifier Serialization Functions (Day 2)
// ============================================================================

FString UCUGameplayEffectMetadataCommandlet::SerializeAttribute(const FGameplayAttribute& Attribute)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	if (!Attribute.IsValid())
	{
		return TEXT("Invalid.Attribute");
	}

	// Get attribute name from FGameplayAttribute
	// Format: "AttributeSetClass.AttributeName" → "AttributeName" or keep full path
	FString AttributeName = Attribute.GetName();

	// Clean up attribute name (remove "AttributeSet" prefix if present)
	// Example: "UHealthAttributeSet::Health" → "Health.Current"
	// Or: "Damage" → "Damage"

	if (AttributeName.IsEmpty())
	{
		return TEXT("Unknown.Attribute");
	}

	// Try to get a cleaner name by extracting the property name
	// UE's FGameplayAttribute stores the UProperty, we can get its name
	if (Attribute.GetUProperty())
	{
		FString PropertyName = Attribute.GetUProperty()->GetName();

		// Get the owner class name (AttributeSet name)
		UClass* OwnerClass = Attribute.GetUProperty()->GetOwnerClass();
		if (OwnerClass)
		{
			FString ClassName = OwnerClass->GetName();

			// Remove "AttributeSet" suffix if present
			ClassName.RemoveFromEnd(TEXT("AttributeSet"));

			// Format: "ClassName.PropertyName" (e.g., "Health.Current", "Damage.Base")
			return FString::Printf(TEXT("%s.%s"), *ClassName, *PropertyName);
		}

		return PropertyName;
	}

	return AttributeName;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return FString();
#endif
}

FString UCUGameplayEffectMetadataCommandlet::SerializeModifierOp(uint8 ModifierOp)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	// Convert EGameplayModOp::Type enum to string
	// UE 5.6: Enum values changed for calculation order clarity
	switch (static_cast<EGameplayModOp::Type>(ModifierOp))
	{
	case EGameplayModOp::AddBase:              // 0 (UE 5.6: renamed from Additive)
		return TEXT("Add (Base)");
	case EGameplayModOp::MultiplyAdditive:     // 1 (UE 5.6: renamed from Multiplicative)
		return TEXT("Multiply (Additive)");
	case EGameplayModOp::DivideAdditive:       // 2 (UE 5.6: renamed from Division)
		return TEXT("Divide (Additive)");
	case EGameplayModOp::Override:             // 3 (unchanged)
		return TEXT("Override");
	case EGameplayModOp::MultiplyCompound:     // 4 (UE 5.6: new - compound multiplication)
		return TEXT("Multiply (Compound)");
	case EGameplayModOp::AddFinal:             // 5 (UE 5.6: new - final additive after all multiplies)
		return TEXT("Add (Final)");
	default:
		return TEXT("Unknown");
	}
#else
	// Issue #28215 Phase A: the signature is uint8 but the body switches on
	// EGameplayModOp, which lives in the opt-in module.
	return FString();
#endif
}

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::SerializeMagnitude(const FGameplayEffectModifierMagnitude& Magnitude)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	TSharedPtr<FJsonObject> MagnitudeJson = MakeShareable(new FJsonObject);

	// UE 5.6: MagnitudeCalculationType is protected - use public getter
	EGameplayEffectMagnitudeCalculation CalcType = Magnitude.GetMagnitudeCalculationType();

	switch (CalcType)
	{
	case EGameplayEffectMagnitudeCalculation::ScalableFloat:
		{
			MagnitudeJson->SetStringField(TEXT("type"), TEXT("ScalableFloat"));

			// UE 5.6: GetStaticMagnitudeIfPossible() API for ScalableFloat value extraction
			float OutValue = 0.0f;
			if (Magnitude.GetStaticMagnitudeIfPossible(1.0f, OutValue))
			{
				// Successfully extracted static value
				MagnitudeJson->SetNumberField(TEXT("value"), OutValue);
				MagnitudeJson->SetStringField(TEXT("detail"), FString::Printf(TEXT("Value: %.2f"), OutValue));
			}
			else
			{
				// Level-dependent or curve-based value
				MagnitudeJson->SetStringField(TEXT("detail"), TEXT("ScalableFloat (level-dependent)"));
			}
			break;
		}

	case EGameplayEffectMagnitudeCalculation::AttributeBased:
		{
			MagnitudeJson->SetStringField(TEXT("type"), TEXT("AttributeBased"));
			// UE 5.6: AttributeBasedMagnitude is still protected, no public API available
			// MVP: Type-only extraction, detailed attribute info requires future enhancement
			MagnitudeJson->SetStringField(TEXT("detail"), TEXT("Attribute-based calculation"));
			break;
		}

	case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:
		{
			MagnitudeJson->SetStringField(TEXT("type"), TEXT("CustomCalculation"));

			// UE 5.6: GetCustomMagnitudeCalculationClass() API for custom class extraction
			TSubclassOf<UGameplayModMagnitudeCalculation> CalcClass = Magnitude.GetCustomMagnitudeCalculationClass();
			if (CalcClass)
			{
				FString ClassName = CalcClass->GetName();
				MagnitudeJson->SetStringField(TEXT("class"), ClassName);
				MagnitudeJson->SetStringField(TEXT("detail"), FString::Printf(TEXT("Custom: %s"), *ClassName));
			}
			else
			{
				MagnitudeJson->SetStringField(TEXT("detail"), TEXT("Custom calculation (class not set)"));
			}
			break;
		}

	case EGameplayEffectMagnitudeCalculation::SetByCaller:
		{
			MagnitudeJson->SetStringField(TEXT("type"), TEXT("SetByCaller"));

			// UE 5.6: GetSetByCallerDataNameIfPossible() API for DataName extraction
			FName DataName;
			if (Magnitude.GetSetByCallerDataNameIfPossible(DataName))
			{
				MagnitudeJson->SetStringField(TEXT("data_name"), DataName.ToString());
				MagnitudeJson->SetStringField(TEXT("detail"), FString::Printf(TEXT("SetByCaller: %s"), *DataName.ToString()));
			}
			else
			{
				MagnitudeJson->SetStringField(TEXT("detail"), TEXT("SetByCaller (runtime value)"));
			}
			break;
		}

	default:
		MagnitudeJson->SetStringField(TEXT("type"), TEXT("Unknown"));
		MagnitudeJson->SetStringField(TEXT("detail"), TEXT("Unknown magnitude type"));
		break;
	}

	return MagnitudeJson;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return nullptr;
#endif
}

TArray<TSharedPtr<FJsonValue>> UCUGameplayEffectMetadataCommandlet::SerializeTagContainer(const FGameplayTagContainer& TagContainer)
{
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
	TArray<TSharedPtr<FJsonValue>> TagArray;

	// Iterate through all tags in the container
	for (const FGameplayTag& Tag : TagContainer)
	{
		if (Tag.IsValid())
		{
			// Convert tag to string format (e.g., "Ability.Attack.Heavy")
			FString TagString = Tag.ToString();
			TagArray.Add(MakeShareable(new FJsonValueString(TagString)));
		}
	}

	return TagArray;
#else
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The class is NARSHAMCP_API so
	// every declared member needs a definition; compile the body out and stub instead.
	return TArray<TSharedPtr<FJsonValue>>();
#endif
}

// ============================================================================
// Helper Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::CreateBasicAssetJson(
	const FString& AssetName,
	const FAssetData& AssetData,
	IAssetRegistry& AssetRegistry)
{
	TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);

	Json->SetStringField(TEXT("name"), AssetName);
	Json->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
	Json->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
	Json->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());

	// Get SavedHash for incremental build support (UE 5.6 API - matches MaterialMetadataCommandlet pattern)
	TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);
	if (PackageData.IsSet())
	{
		FIoHash SavedHash = PackageData->GetPackageSavedHash();
		FString SavedHashStr = LexToString(SavedHash);
		Json->SetStringField(TEXT("saved_hash"), SavedHashStr);
	}

	return Json;
}

// ============================================================================
// JSON Building Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUGameplayEffectMetadataCommandlet::BuildFinalJsonObject(
	const TArray<TSharedPtr<FJsonValue>>& GameplayEffectsArray,
	int32 ProcessedCount,
	int32 ErrorCount)
{
	TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

	// Metadata
	RootJson->SetNumberField(TEXT("version"), 1);
	RootJson->SetStringField(TEXT("generator"), TEXT("NarshaMCP::GameplayEffectMetadataCommandlet v4.21.0"));
	RootJson->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToString(TEXT("%Y.%m.%d-%H.%M.%S")));

	// Stats
	RootJson->SetNumberField(TEXT("total_gameplayeffects"), ProcessedCount + ErrorCount);
	RootJson->SetNumberField(TEXT("processed"), ProcessedCount);
	RootJson->SetNumberField(TEXT("errors"), ErrorCount);

	// GameplayEffects array
	RootJson->SetArrayField(TEXT("gameplayeffects"), GameplayEffectsArray);

	return RootJson;
}

bool UCUGameplayEffectMetadataCommandlet::SaveJsonWithMetadata(
	const FString& JsonPath,
	const TSharedPtr<FJsonObject>& JsonData)
{
	// Issue #14142: route through the canonical compressed saver (atomic temp -> rename + the UE 5.6
	// FCompression API centralized in FCUCommandletHelpers::SaveJsonWithCompression), replacing this
	// commandlet's local #if ladder. It builds a fresh tree every run (no incremental load/merge);
	// output stays {JsonPath}.gz - byte-identical format.
	return FCUCommandletHelpers::SaveJsonWithCompression(JsonPath, JsonData);
}
