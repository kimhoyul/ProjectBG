// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "GameplayTagContainer.h"

#include "CULog.h"

UCUCommandlet::UCUCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

// Override CommandletBase::RunExtraction()
int32 UCUCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
	return RunGASExtraction(OutputDir, Registry);
}

// Static method for batch commandlet
int32 UCUCommandlet::RunGASExtraction(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[NarshaMCP] Running GAS tag extraction..."));

	// PROFILING: Asset Search
	FCUCommandletHelpers::BeginProfileSection(TEXT("NarshaMCP.AssetSearch"));

	// Find all GameplayAbility assets
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[2/4] Searching for GameplayAbility assets..."));

	TArray<FAssetData> AbilityAssets;

	// Search for all Blueprint assets and check NativeParentClass
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      Searching for Blueprints inheriting from GameplayAbility..."));

	TArray<FAssetData> AllBlueprintAssets;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Engine"), TEXT("Blueprint")), AllBlueprintAssets, true);

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      Found %d total Blueprint assets"), AllBlueprintAssets.Num());

	int32 DebugCount = 0;
	for (const FAssetData& BlueprintAsset : AllBlueprintAssets)
	{
		// Check NativeParentClass tag (this is the C++ base class)
		FAssetDataTagMapSharedView::FFindTagResult NativeParentTag = BlueprintAsset.TagsAndValues.FindTag(FName("NativeParentClass"));
		if (NativeParentTag.IsSet())
		{
			FString NativeParentPath = NativeParentTag.GetValue();

			// Debug first 5 GA blueprints found
			if (NativeParentPath.Contains(TEXT("GameplayAbility")) && DebugCount < 5)
			{
				UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      DEBUG: Found GA Blueprint: %s"), *BlueprintAsset.AssetName.ToString());
				UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("             NativeParent: %s"), *NativeParentPath);
				DebugCount++;
			}

			if (NativeParentPath.Contains(TEXT("GameplayAbility")))
			{
				AbilityAssets.AddUnique(BlueprintAsset);
			}
		}
	}

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      Found %d GameplayAbility assets"), AbilityAssets.Num());

	// PROFILING: End AssetSearch
	FCUCommandletHelpers::EndProfileSection(TEXT("NarshaMCP.AssetSearch"));

	// ============================================================================
	// Incremental Build Phase 2: Hash-based change detection
	// ============================================================================
	// v5.1.0: Use metadata subdirectory
	FString OutputPath = FPaths::Combine(GetIntermediateDir(), TEXT("NarshaMCP"), TEXT("metadata"), TEXT("GameplayAbilityTags.json"));
	TSharedPtr<FJsonObject> ExistingJson = LoadExistingJson(OutputPath);
	bool bIncrementalMode = ExistingJson.IsValid();

	TArray<FAssetData> AssetsToProcess;
	if (bIncrementalMode)
	{
		// Incremental build: Only process changed abilities
		AssetsToProcess = FindChangedAbilities(AbilityAssets, ExistingJson, AssetRegistry);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[NarshaMCP] Incremental mode: Processing %d / %d changed abilities"),
		       AssetsToProcess.Num(), AbilityAssets.Num());
	}
	else
	{
		// Full build: Process all abilities
		AssetsToProcess = AbilityAssets;
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[NarshaMCP] Full build mode: Processing all %d abilities"),
		       AssetsToProcess.Num());
	}

	if (AssetsToProcess.Num() == 0 && AbilityAssets.Num() > 0)
	{
		// No changes detected, reuse existing JSON
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[NarshaMCP] No changes detected! Reusing existing JSON."));
		return 0;  // Success
	}

	if (AbilityAssets.Num() == 0)
	{
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT(""));
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("No GameplayAbility assets found in project."));
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("This is expected if:"));
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("  - Project doesn't use Gameplay Ability System"));
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("  - All abilities are defined in C++ (not Blueprint)"));
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT(""));
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Creating empty JSON output..."));
	}

	// PROFILING: BlueprintLoading + TagExtraction
	FCUCommandletHelpers::BeginProfileSection(TEXT("NarshaMCP.BlueprintLoading+TagExtraction"));

	// Extract tags from each ability
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[3/4] Extracting GameplayTags from abilities..."));

	// Store tuple: (AssetData, Tags, ClassName)
	TArray<TTuple<FAssetData, TArray<FString>, FString>> AbilityTagData;
	int32 SuccessCount = 0;
	int32 FailureCount = 0;

	// Process only changed/new abilities (Incremental mode optimization)
	for (const FAssetData& AssetData : AssetsToProcess)
	{
		TArray<FString> Tags;
		FString ClassName;

		if (ExtractAbilityTags(AssetData, Tags, ClassName))
		{
			AbilityTagData.Add(MakeTuple(AssetData, Tags, ClassName));
			SuccessCount++;

			if (Tags.Num() > 0)
			{
				UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      ??%s: %d tags"), *AssetData.AssetName.ToString(), Tags.Num());
			}
			else
			{
				UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      ??%s: No tags found"), *AssetData.AssetName.ToString());
			}
		}
		else
		{
			UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("      ??%s: Failed to load"), *AssetData.AssetName.ToString());
			FailureCount++;
		}
	}

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      Extraction Results:"));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("        Success: %d"), SuccessCount);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("        Failure: %d"), FailureCount);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));

	// PROFILING: End BlueprintLoading+TagExtraction
	FCUCommandletHelpers::EndProfileSection(TEXT("NarshaMCP.BlueprintLoading+TagExtraction"));

	// PROFILING: JsonSave
	FCUCommandletHelpers::BeginProfileSection(TEXT("NarshaMCP.JsonSave"));

	// Write JSON output
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[4/4] Writing JSON output..."));

	// ============================================================================
	// Incremental Build Phase 2: Merge with existing JSON
	// ============================================================================
	TSharedPtr<FJsonObject> FinalJson;

	if (bIncrementalMode && ExistingJson.IsValid())
	{
		// Incremental: Merge new data with existing JSON
		MergeJsonData(ExistingJson, AbilityTagData);
		FinalJson = ExistingJson;
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[NarshaMCP] Incremental mode: Merged %d new/changed abilities"),
		       AbilityTagData.Num());
	}
	else
	{
		// Full build: Create new JSON from scratch
		FinalJson = MakeShareable(new FJsonObject);

		TArray<TSharedPtr<FJsonValue>> AbilitiesArray;
		int32 TotalTags = 0;

		for (const auto& Tuple : AbilityTagData)
		{
			const FAssetData& AssetData = Tuple.Get<0>();
			const TArray<FString>& Tags = Tuple.Get<1>();
			const FString& ClassName = Tuple.Get<2>();

			TSharedPtr<FJsonObject> AbilityObject = MakeShareable(new FJsonObject);
			AbilityObject->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());
			AbilityObject->SetStringField(TEXT("class_name"), ClassName);
			AbilityObject->SetStringField(TEXT("source_file"), AssetData.GetFullName());

			// Tags array
			TArray<TSharedPtr<FJsonValue>> TagsArray;
			for (const FString& Tag : Tags)
			{
				TagsArray.Add(MakeShareable(new FJsonValueString(Tag)));
				TotalTags++;
			}
			AbilityObject->SetArrayField(TEXT("tags"), TagsArray);

			// Add saved_hash for incremental builds
			TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);
			if (PackageData.IsSet())
			{
				FIoHash SavedHash = PackageData->GetPackageSavedHash();
				AbilityObject->SetStringField(TEXT("saved_hash"), LexToString(SavedHash));
			}

			AbilitiesArray.Add(MakeShareable(new FJsonValueObject(AbilityObject)));
		}

		FinalJson->SetArrayField(TEXT("abilities"), AbilitiesArray);

		// ============================================================================
		// TODO(Issue #5657): Build cached_hashes object for fast loading (full build path)
		// ============================================================================
		TSharedPtr<FJsonObject> CachedHashesObject = MakeShareable(new FJsonObject);
		for (const auto& Tuple : AbilityTagData)
		{
			const FAssetData& AssetData = Tuple.Get<0>();
			FString AssetPath = AssetData.GetObjectPathString();

			TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);
			if (PackageData.IsSet())
			{
				FIoHash SavedHash = PackageData->GetPackageSavedHash();
				CachedHashesObject->SetStringField(AssetPath, LexToString(SavedHash));
			}
		}
		FinalJson->SetObjectField(TEXT("cached_hashes"), CachedHashesObject);

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[NarshaMCP] Built cached_hashes object: %d entries"), CachedHashesObject->Values.Num());

		// Metadata
		TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
		MetadataObject->SetStringField(TEXT("generated_at"), FDateTime::Now().ToString());
		MetadataObject->SetNumberField(TEXT("total_abilities"), AbilityTagData.Num());
		MetadataObject->SetNumberField(TEXT("total_tags"), TotalTags);
		MetadataObject->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString());
		FinalJson->SetObjectField(TEXT("metadata"), MetadataObject);
	}

	// Save using new SaveJsonWithMetadata (supports incremental mode)
	if (SaveJsonWithMetadata(OutputPath, FinalJson, bIncrementalMode))
	{
		// PROFILING: End JsonSave
		FCUCommandletHelpers::EndProfileSection(TEXT("NarshaMCP.JsonSave"));

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("      Output: %s"), *OutputPath);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  ??SUCCESS: GameplayAbility tags extracted"));
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
		return 0; // Success
	}
	else
	{
		// PROFILING: End JsonSave (even on failure)
		FCUCommandletHelpers::EndProfileSection(TEXT("NarshaMCP.JsonSave"));

		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT(""));
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("============================================================="));
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("  ??FAILURE: Failed to write JSON output"));
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("============================================================="));
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT(""));
		return 1; // Failure
	}
}

bool UCUCommandlet::ExtractAbilityTags(
	const FAssetData& AssetData,
	TArray<FString>& OutTags,
	FString& OutClassName
)
{
	// ============================================================================
	// Incremental Build Phase 1: Optimized Blueprint loading
	// ============================================================================
	// LOAD_DisableCompileOnLoad: Skip Blueprint compilation (30-40% faster)
	// GameplayTag extraction only needs CDO, not compiled Blueprint
	// ============================================================================

	// Load the asset (Blueprint) with optimized flags
	UBlueprint* Blueprint = LoadObject<UBlueprint>(
		nullptr,
		*AssetData.GetObjectPathString(),
		nullptr,
		LOAD_DisableCompileOnLoad | LOAD_NoWarn  // Skip compilation
	);

	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return false;
	}

	// Get the CDO (Class Default Object)
	UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		return false;
	}

	OutClassName = Blueprint->GeneratedClass->GetName();

	// Use Reflection to find FGameplayTagContainer properties
	// Common property names in GameplayAbility:
	// - AbilityTags
	// - CancelAbilitiesWithTag
	// - BlockAbilitiesWithTag
	// - ActivationOwnedTags
	// - ActivationRequiredTags
	// - ActivationBlockedTags

	for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Check if it's a FGameplayTagContainer property
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct == TBaseStructure<FGameplayTagContainer>::Get())
			{
				// Get the property value from CDO
				const FGameplayTagContainer* TagContainer = StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);

				if (TagContainer && TagContainer->Num() > 0)
				{
					// Extract tag strings
					TArray<FGameplayTag> TagArray;
					TagContainer->GetGameplayTagArray(TagArray);

					for (const FGameplayTag& Tag : TagArray)
					{
						FString TagString = Tag.ToString();
						if (!TagString.IsEmpty())
						{
							// Format: PropertyName.TagName (e.g., "AbilityTags.Ability.Player.Attack")
							OutTags.AddUnique(FString::Printf(TEXT("%s.%s"),
								*Property->GetName(),
								*TagString
							));
						}
					}
				}
			}
		}
	}

	return true; // Success even if no tags found
}

// ============================================================================
// DEPRECATED: Legacy function - replaced by SaveJsonWithMetadata (Incremental Build Phase 2)
// This function is kept for reference only
// ============================================================================
/*
bool UCUCommandlet::WriteJsonOutput(
	const FString& OutputPath,
	const TArray<TTuple<FAssetData, TArray<FString>, FString>>& AbilityData
)
{
	// Create output directory if it doesn't exist
	FString OutputDir = FPaths::GetPath(OutputPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*OutputDir))
	{
		if (!PlatformFile.CreateDirectoryTree(*OutputDir))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to create directory: %s"), *OutputDir);
			return false;
		}
	}

	// Build JSON structure
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// Abilities array
	TArray<TSharedPtr<FJsonValue>> AbilitiesArray;

	int32 TotalTags = 0;

	for (const auto& Tuple : AbilityData)
	{
		const FAssetData& AssetData = Tuple.Get<0>();
		const TArray<FString>& Tags = Tuple.Get<1>();
		const FString& ClassName = Tuple.Get<2>();

		TSharedPtr<FJsonObject> AbilityObject = MakeShareable(new FJsonObject);

		// Asset path (e.g., "/Game/Abilities/GA_Attack.GA_Attack")
		AbilityObject->SetStringField(TEXT("asset_path"), AssetData.GetObjectPathString());

		// Class name (e.g., "GA_Attack_C" for Blueprint, "UGA_Attack" for C++)
		// Use runtime class name from GeneratedClass->GetName(), not AssetName
		AbilityObject->SetStringField(TEXT("class_name"), ClassName);

		// Asset file path (e.g., "Content/Abilities/GA_Attack.uasset")
		AbilityObject->SetStringField(TEXT("source_file"), AssetData.GetFullName());

		// Tags array
		TArray<TSharedPtr<FJsonValue>> TagsArray;
		for (const FString& Tag : Tags)
		{
			TagsArray.Add(MakeShareable(new FJsonValueString(Tag)));
			TotalTags++;
		}
		AbilityObject->SetArrayField(TEXT("tags"), TagsArray);

		AbilitiesArray.Add(MakeShareable(new FJsonValueObject(AbilityObject)));
	}

	RootObject->SetArrayField(TEXT("abilities"), AbilitiesArray);

	// Metadata object
	TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
	MetadataObject->SetStringField(TEXT("generated_at"), FDateTime::Now().ToString());
	MetadataObject->SetNumberField(TEXT("total_abilities"), AbilityData.Num());
	MetadataObject->SetNumberField(TEXT("total_tags"), TotalTags);
	MetadataObject->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString());

	RootObject->SetObjectField(TEXT("metadata"), MetadataObject);

	// Serialize to string
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);

	if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to serialize JSON"));
		return false;
	}

	// Write to file
	if (!FFileHelper::SaveStringToFile(JsonString, *OutputPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to write file: %s"), *OutputPath);
		return false;
	}

	return true;
}
*/

FString UCUCommandlet::GetIntermediateDir()
{
	// Use project's Intermediate directory
	return FPaths::ProjectIntermediateDir();
}
