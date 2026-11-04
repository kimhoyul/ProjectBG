// Copyright 2026 Next Stage Inc. All rights reserved.

/**
 * Incremental Build Helper Functions for CUCommandlet
 *
 * Implements hash-based change detection for GameplayAbility tag extraction.
 * Provides 90%+ performance improvement for subsequent runs (10.5s ??1.5s).
 *
 * @since v4.8.0 Incremental Build implementation
 */

#include "CUCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "IO/IoHash.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/Compression.h"

#include "CULog.h"

// ============================================================================
// Load Existing JSON
// ============================================================================

TSharedPtr<FJsonObject> UCUCommandlet::LoadExistingJson(const FString& JsonPath)
{
    // Issue #14142: route through the canonical loader (corrupted-file recovery, GZIP footer
    // end-scan, and GZIP-bomb security layers all live in FCUCommandletHelpers::LoadJsonFromFile).
    // Probe plain JSON first to preserve this commandlet's original ordering, then the .gz fallback.
    const FString GzipPath = JsonPath + TEXT(".gz");

    TSharedPtr<FJsonObject> JsonObject;
    if (FPaths::FileExists(JsonPath))
    {
        JsonObject = FCUCommandletHelpers::LoadJsonFromFile(JsonPath);
    }
    if (!JsonObject.IsValid() && FPaths::FileExists(GzipPath))
    {
        JsonObject = FCUCommandletHelpers::LoadJsonFromFile(GzipPath);
    }

    if (!JsonObject.IsValid())
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] No existing JSON found (incremental disabled)"));
        return nullptr;
    }

    int32 TotalAbilities = 0;
    if (const TSharedPtr<FJsonObject>* MetadataObj; JsonObject->TryGetObjectField(TEXT("metadata"), MetadataObj))
    {
        TotalAbilities = (*MetadataObj)->GetIntegerField(TEXT("total_abilities"));
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Loaded existing JSON (%d abilities)"), TotalAbilities);

    return JsonObject;
}

// ============================================================================
// Find Changed GameplayAbility Assets
// ============================================================================

TArray<FAssetData> UCUCommandlet::FindChangedAbilities(
    const TArray<FAssetData>& AllAbilities,
    const TSharedPtr<FJsonObject>& ExistingJson,
    IAssetRegistry& AssetRegistry
)
{
    TArray<FAssetData> ChangedAbilities;

    // Build hash map: asset_path -> saved_hash
    TMap<FString, FString> CachedHashes;

    // ============================================================================
    // TODO(Issue #5657): Load cached_hashes object (optimized - same as BlueprintMetadata)
    // ============================================================================

    // Try to load from cached_hashes object first (v4.7.0+ format)
    const TSharedPtr<FJsonObject>* CachedHashesObject;
    if (ExistingJson->TryGetObjectField(TEXT("cached_hashes"), CachedHashesObject) && CachedHashesObject->IsValid())
    {
        // Fast path: Load from cached_hashes object (instant lookup)
        for (const auto& Pair : (*CachedHashesObject)->Values)
        {
            FString AssetPath = FString(Pair.Key);
            FString SavedHash = Pair.Value->AsString();
            CachedHashes.Add(AssetPath, SavedHash);
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Cached hashes loaded from cached_hashes object: %d"), CachedHashes.Num());
    }
    else
    {
        // Fallback: Load from abilities array (v4.6.0- format - backward compatible)
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] No cached_hashes object found, loading from arrays (legacy format)"));

        const TArray<TSharedPtr<FJsonValue>>* ExistingAbilitiesArray;
        if (!ExistingJson->TryGetArrayField(TEXT("abilities"), ExistingAbilitiesArray))
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] No 'abilities' array in JSON, full build"));
            return AllAbilities;
        }

        for (const TSharedPtr<FJsonValue>& Value : *ExistingAbilitiesArray)
        {
            const TSharedPtr<FJsonObject>* AbilityObject;
            if (Value->TryGetObject(AbilityObject) && AbilityObject->IsValid())
            {
                FString AssetPath = (*AbilityObject)->GetStringField(TEXT("asset_path"));
                FString SavedHash;
                if ((*AbilityObject)->TryGetStringField(TEXT("saved_hash"), SavedHash))
                {
                    CachedHashes.Add(AssetPath, SavedHash);
                }
            }
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Cached hashes loaded from arrays (legacy): %d"), CachedHashes.Num());
    }

    // Compare hashes for each ability
    int32 NewAssets = 0;
    int32 ChangedAssets = 0;
    int32 UnchangedAssets = 0;

    for (const FAssetData& Ability : AllAbilities)
    {
        FString AssetPath = Ability.GetObjectPathString();

        // Get current hash from AssetRegistry
        FString CurrentHash;
        TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(Ability.PackageName);
        if (PackageData.IsSet())
        {
            FIoHash SavedHash = PackageData->GetPackageSavedHash();
            CurrentHash = LexToString(SavedHash);
        }

        // Check if hash exists in cache
        FString* CachedHash = CachedHashes.Find(AssetPath);

        if (!CachedHash)
        {
            // New asset
            ChangedAbilities.Add(Ability);
            NewAssets++;
        }
        else if (CurrentHash != *CachedHash)
        {
            // Changed asset
            ChangedAbilities.Add(Ability);
            ChangedAssets++;
        }
        else
        {
            // Unchanged asset (skip)
            UnchangedAssets++;
        }
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Hash comparison complete:"));
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - New: %d"), NewAssets);
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Changed: %d"), ChangedAssets);
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  - Unchanged: %d (skipped)"), UnchangedAssets);

    return ChangedAbilities;
}

// ============================================================================
// Merge New Ability Data with Existing JSON
// ============================================================================

void UCUCommandlet::MergeJsonData(
    TSharedPtr<FJsonObject>& ExistingJson,
    const TArray<TTuple<FAssetData, TArray<FString>, FString>>& NewAbilityData
)
{
    // Get existing abilities array
    const TArray<TSharedPtr<FJsonValue>>* ExistingAbilitiesArray;
    if (!ExistingJson->TryGetArrayField(TEXT("abilities"), ExistingAbilitiesArray))
    {
        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] Cannot merge: no abilities array in existing JSON"));
        return;
    }

    // Build map: asset_path -> JsonObject
    TMap<FString, TSharedPtr<FJsonObject>> ExistingAbilitiesMap;

    for (const TSharedPtr<FJsonValue>& Value : *ExistingAbilitiesArray)
    {
        const TSharedPtr<FJsonObject>* AbilityObject;
        if (Value->TryGetObject(AbilityObject) && AbilityObject->IsValid())
        {
            FString AssetPath = (*AbilityObject)->GetStringField(TEXT("asset_path"));
            ExistingAbilitiesMap.Add(AssetPath, *AbilityObject);
        }
    }

    // Update or add new abilities
    for (const auto& Tuple : NewAbilityData)
    {
        const FAssetData& AssetData = Tuple.Get<0>();
        const TArray<FString>& Tags = Tuple.Get<1>();
        const FString& ClassName = Tuple.Get<2>();

        FString AssetPath = AssetData.GetObjectPathString();

        // Create new ability JSON
        TSharedPtr<FJsonObject> AbilityObject = MakeShareable(new FJsonObject);
        AbilityObject->SetStringField(TEXT("asset_path"), AssetPath);
        AbilityObject->SetStringField(TEXT("class_name"), ClassName);
        AbilityObject->SetStringField(TEXT("source_file"), AssetData.GetFullName());

        // Add tags array
        TArray<TSharedPtr<FJsonValue>> TagsArray;
        for (const FString& Tag : Tags)
        {
            TagsArray.Add(MakeShareable(new FJsonValueString(Tag)));
        }
        AbilityObject->SetArrayField(TEXT("tags"), TagsArray);

        // Add saved_hash
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
        TOptional<FAssetPackageData> PackageData = Registry.GetAssetPackageDataCopy(AssetData.PackageName);
        if (PackageData.IsSet())
        {
            FIoHash SavedHash = PackageData->GetPackageSavedHash();
            AbilityObject->SetStringField(TEXT("saved_hash"), LexToString(SavedHash));
        }

        // Update map
        ExistingAbilitiesMap.Add(AssetPath, AbilityObject);
    }

    // Rebuild abilities array
    TArray<TSharedPtr<FJsonValue>> MergedAbilitiesArray;
    for (const auto& Pair : ExistingAbilitiesMap)
    {
        MergedAbilitiesArray.Add(MakeShareable(new FJsonValueObject(Pair.Value)));
    }

    ExistingJson->SetArrayField(TEXT("abilities"), MergedAbilitiesArray);

    // ============================================================================
    // TODO(Issue #5657): Build cached_hashes object for fast loading (same as BlueprintMetadata)
    // ============================================================================
    TSharedPtr<FJsonObject> CachedHashesObject = MakeShareable(new FJsonObject);
    for (const auto& Pair : ExistingAbilitiesMap)
    {
        FString AssetPath = FString(Pair.Key);
        FString SavedHash;
        if (Pair.Value->TryGetStringField(TEXT("saved_hash"), SavedHash))
        {
            CachedHashesObject->SetStringField(AssetPath, SavedHash);
        }
    }
    ExistingJson->SetObjectField(TEXT("cached_hashes"), CachedHashesObject);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Built cached_hashes object: %d entries"), CachedHashesObject->Values.Num());

    // Update metadata
    TSharedPtr<FJsonObject> MetadataObject = MakeShareable(new FJsonObject);
    MetadataObject->SetStringField(TEXT("generated_at"), FDateTime::Now().ToString());
    MetadataObject->SetNumberField(TEXT("total_abilities"), MergedAbilitiesArray.Num());

    // Count total tags
    int32 TotalTags = 0;
    for (const TSharedPtr<FJsonValue>& Value : MergedAbilitiesArray)
    {
        const TSharedPtr<FJsonObject>* AbilityObj;
        if (Value->TryGetObject(AbilityObj))
        {
            const TArray<TSharedPtr<FJsonValue>>* TagsArray;
            if ((*AbilityObj)->TryGetArrayField(TEXT("tags"), TagsArray))
            {
                TotalTags += TagsArray->Num();
            }
        }
    }
    MetadataObject->SetNumberField(TEXT("total_tags"), TotalTags);
    MetadataObject->SetStringField(TEXT("unreal_version"), FEngineVersion::Current().ToString());

    ExistingJson->SetObjectField(TEXT("metadata"), MetadataObject);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Merge complete: %d abilities total"), MergedAbilitiesArray.Num());
}

// ============================================================================
// Save JSON with Metadata
// ============================================================================

bool UCUCommandlet::SaveJsonWithMetadata(
    const FString& OutputPath,
    const TSharedPtr<FJsonObject>& RootJson,
    bool bIncrementalMode
)
{
    if (!RootJson.IsValid())
    {
        UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[NarshaMCP] Invalid JSON object"));
        return false;
    }

    // Issue #14142: route through the canonical plain saver. The parent-directory ensure and the
    // atomic temp -> rename live in FCUCommandletHelpers::SaveJsonToFile. This commandlet writes
    // plain JSON, so it stays plain (no .gz) — behavior-preserving on-disk format.
    if (!FCUCommandletHelpers::SaveJsonToFile(OutputPath, RootJson))
    {
        return false;
    }

    if (bIncrementalMode)
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Incremental build complete!"));
    }

    return true;
}
