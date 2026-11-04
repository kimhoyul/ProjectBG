// Copyright 2026 Next Stage Inc. All rights reserved.

/**
 * Incremental Build Helper Functions (Issue #108 Phase 1 Incremental)
 *
 * Implements hash-based change detection for Blueprint metadata extraction.
 * Provides 30x performance improvement for subsequent runs (91.4s → 2-3s).
 */

#include "CUBlueprintMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "IO/IoHash.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

#include "CULog.h"

// ============================================================================
// Load Existing JSON
// ============================================================================

TSharedPtr<FJsonObject> UCUBlueprintMetadataCommandlet::LoadExistingJson(const FString& JsonPath)
{
    // Issue #14142: route through the canonical loader. Corrupted-file recovery (delete + force
    // rebuild), the GZIP footer end-scan, and the GZIP-bomb security layers all live in
    // FCUCommandletHelpers::LoadJsonFromFile, so every incremental loader shares one hardened path.
    // Issue #992 Priority 12: probe GZIP first (always fresh, written every run); plain JSON may be
    // stale if a prior build was interrupted or only the .gz was written.
    const FString GzipPath = JsonPath + TEXT(".gz");

    TSharedPtr<FJsonObject> JsonObject;
    if (FPaths::FileExists(GzipPath))
    {
        JsonObject = FCUCommandletHelpers::LoadJsonFromFile(GzipPath);
    }
    else if (FPaths::FileExists(JsonPath))
    {
        JsonObject = FCUCommandletHelpers::LoadJsonFromFile(JsonPath);
    }

    if (!JsonObject.IsValid())
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] No existing JSON found (checked both .json and .json.gz), starting full build"));
        return nullptr;
    }

    // Issue #206: Safe field access to prevent crash on malformed JSON
    int32 TotalBlueprints = 0;
    if (JsonObject->HasField(TEXT("total_blueprints")))
    {
        TotalBlueprints = (int32)JsonObject->GetNumberField(TEXT("total_blueprints"));
    }
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Loaded existing JSON (%d blueprints)"), TotalBlueprints);

    return JsonObject;
}

// ============================================================================
// Find Changed Blueprints (Hash Comparison)
// ============================================================================

TArray<FAssetData> UCUBlueprintMetadataCommandlet::FindChangedBlueprints(
    const TArray<FAssetData>& AllBlueprints,
    const TSharedPtr<FJsonObject>& ExistingJson,
    IAssetRegistry& AssetRegistry
)
{
    TArray<FAssetData> ChangedBlueprints;

    // Get existing blueprints array from JSON
    const TArray<TSharedPtr<FJsonValue>>* ExistingBPsArray;
    if (!ExistingJson->TryGetArrayField(TEXT("blueprints"), ExistingBPsArray))
    {
        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP] No 'blueprints' array in existing JSON, doing full build"));
        return AllBlueprints;  // No cache available, process all
    }

    // ============================================================================
    // Issue #108 Phase 1 Incremental: Load cached_hashes object (optimized)
    // ============================================================================
    TMap<FString, FString> CachedHashes;

    // Try to load from cached_hashes object first (v4.7.0+ format)
    const TSharedPtr<FJsonObject>* CachedHashesObject;
    if (ExistingJson->TryGetObjectField(TEXT("cached_hashes"), CachedHashesObject) && CachedHashesObject->IsValid())
    {
        // Load from cached_hashes object (much faster than iterating arrays)
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
        // Fallback: Load from blueprints/anim_blueprints arrays (v4.6.0- format)
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] No cached_hashes object found, loading from arrays (legacy format)"));

        for (const TSharedPtr<FJsonValue>& Value : *ExistingBPsArray)
        {
            const TSharedPtr<FJsonObject>* BPObject;
            if (Value->TryGetObject(BPObject) && BPObject->IsValid())
            {
                // Issue #206: Safe field access
                FString Path;
                if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
                {
                    continue;  // Skip malformed entry
                }

                FString SavedHash;
                if ((*BPObject)->TryGetStringField(TEXT("saved_hash"), SavedHash))
                {
                    CachedHashes.Add(Path, SavedHash);
                }
            }
        }

        // Also check AnimBlueprints
        const TArray<TSharedPtr<FJsonValue>>* ExistingAnimBPsArray;
        if (ExistingJson->TryGetArrayField(TEXT("anim_blueprints"), ExistingAnimBPsArray))
        {
            for (const TSharedPtr<FJsonValue>& Value : *ExistingAnimBPsArray)
            {
                const TSharedPtr<FJsonObject>* BPObject;
                if (Value->TryGetObject(BPObject) && BPObject->IsValid())
                {
                    // Issue #206: Safe field access
                    FString Path;
                    if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
                    {
                        continue;  // Skip malformed entry
                    }

                    FString SavedHash;
                    if ((*BPObject)->TryGetStringField(TEXT("saved_hash"), SavedHash))
                    {
                        CachedHashes.Add(Path, SavedHash);
                    }
                }
            }
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Cached hashes loaded from arrays (legacy): %d"), CachedHashes.Num());
    }

    // Compare hashes for each Blueprint
    int32 NewAssets = 0;
    int32 ChangedAssets = 0;
    int32 UnchangedAssets = 0;

    for (const FAssetData& AssetData : AllBlueprints)
    {
        FString AssetPath = AssetData.GetObjectPathString();

        // Get package data with saved hash
        TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);

        if (!PackageData.IsSet())
        {
            UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP] No package data for: %s (adding as new)"), *AssetPath);
            ChangedBlueprints.Add(AssetData);
            NewAssets++;
            continue;
        }

        // ROOT CAUSE #40 FIX: Use FMD5 hash for Data-Only BPs (matches save path)
        // Check if this is a Data-Only Blueprint
        FString IsDataOnlyStr;
        bool bIsDataOnly = AssetData.GetTagValue(FBlueprintTags::IsDataOnly, IsDataOnlyStr) &&
                          IsDataOnlyStr.Equals(TEXT("True"), ESearchCase::IgnoreCase);

        FString CurrentHashStr;
        if (bIsDataOnly)
        {
            // Data-Only BP: Generate FMD5 hash (same logic as save path)
            FString ParentClassName;
            AssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, ParentClassName);

            // Use path + parent class for hash (matches CUBlueprintMetadataCommandlet.cpp:440)
            FString ContentToHash = AssetData.GetObjectPathString() + TEXT("|") + ParentClassName;
            CurrentHashStr = FMD5::HashAnsiString(*ContentToHash);

            UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP][ROOT CAUSE #40] Generated FMD5 hash for Data-Only BP: %s (hash: %s)"),
                   *AssetPath, *CurrentHashStr);
        }
        else
        {
            // Regular BP: Use GetPackageSavedHash() as before
            FIoHash CurrentHash = PackageData->GetPackageSavedHash();
            CurrentHashStr = LexToString(CurrentHash);
        }

        // Check if cached hash exists
        FString* CachedHashPtr = CachedHashes.Find(AssetPath);

        if (!CachedHashPtr)
        {
            // New asset (not in previous JSON)
            UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP] New asset: %s"), *AssetPath);
            ChangedBlueprints.Add(AssetData);
            NewAssets++;
        }
        else if (CurrentHashStr != *CachedHashPtr)
        {
            // Hash changed
            UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP] Changed: %s (hash: %s -> %s)"),
                   *AssetPath, **CachedHashPtr, *CurrentHashStr);
            ChangedBlueprints.Add(AssetData);
            ChangedAssets++;
        }
        else
        {
            // Unchanged
            UnchangedAssets++;
        }
    }

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Change detection complete:"));
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   New: %d, Changed: %d, Unchanged: %d"),
           NewAssets, ChangedAssets, UnchangedAssets);
    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP]   Processing %d / %d blueprints (%.1f%% reduction)"),
           ChangedBlueprints.Num(), AllBlueprints.Num(),
           (1.0f - (float)ChangedBlueprints.Num() / AllBlueprints.Num()) * 100.0f);

    return ChangedBlueprints;
}

// ============================================================================
// Merge JSON Data
// ============================================================================

void UCUBlueprintMetadataCommandlet::MergeJsonData(
    TSharedPtr<FJsonObject>& ExistingJson,
    const TArray<TSharedPtr<FJsonValue>>& NewBlueprints,
    const TArray<TSharedPtr<FJsonValue>>& NewAnimBPs,
    const TArray<TSharedPtr<FJsonValue>>& NewWidgetBPs
)
{
    // Get existing arrays
    TArray<TSharedPtr<FJsonValue>> ExistingBPs;
    TArray<TSharedPtr<FJsonValue>> ExistingAnimBPs;
    TArray<TSharedPtr<FJsonValue>> ExistingWidgetBPs;  // Issue #135 Phase 3

    const TArray<TSharedPtr<FJsonValue>>* ExistingBPsArray;
    if (ExistingJson->TryGetArrayField(TEXT("blueprints"), ExistingBPsArray))
    {
        ExistingBPs = *ExistingBPsArray;
    }

    const TArray<TSharedPtr<FJsonValue>>* ExistingAnimBPsArray;
    if (ExistingJson->TryGetArrayField(TEXT("anim_blueprints"), ExistingAnimBPsArray))
    {
        ExistingAnimBPs = *ExistingAnimBPsArray;
    }

    const TArray<TSharedPtr<FJsonValue>>* ExistingWidgetBPsArray;  // Issue #135 Phase 3
    if (ExistingJson->TryGetArrayField(TEXT("widget_blueprints"), ExistingWidgetBPsArray))
    {
        ExistingWidgetBPs = *ExistingWidgetBPsArray;
    }

    // Build map of new data: AssetPath -> JsonValue
    TMap<FString, TSharedPtr<FJsonValue>> NewBPMap;
    for (const TSharedPtr<FJsonValue>& Value : NewBlueprints)
    {
        const TSharedPtr<FJsonObject>* BPObject;
        if (Value->TryGetObject(BPObject) && BPObject->IsValid())
        {
            // Issue #206: Safe field access
            FString Path;
            if ((*BPObject)->TryGetStringField(TEXT("path"), Path))
            {
                NewBPMap.Add(Path, Value);
            }
        }
    }

    TMap<FString, TSharedPtr<FJsonValue>> NewAnimBPMap;
    for (const TSharedPtr<FJsonValue>& Value : NewAnimBPs)
    {
        const TSharedPtr<FJsonObject>* BPObject;
        if (Value->TryGetObject(BPObject) && BPObject->IsValid())
        {
            // Issue #206: Safe field access
            FString Path;
            if ((*BPObject)->TryGetStringField(TEXT("path"), Path))
            {
                NewAnimBPMap.Add(Path, Value);
            }
        }
    }

    // Issue #135 Phase 3: Build map for WidgetBlueprints
    TMap<FString, TSharedPtr<FJsonValue>> NewWidgetBPMap;
    for (const TSharedPtr<FJsonValue>& Value : NewWidgetBPs)
    {
        const TSharedPtr<FJsonObject>* BPObject;
        if (Value->TryGetObject(BPObject) && BPObject->IsValid())
        {
            // Issue #206: Safe field access
            FString Path;
            if ((*BPObject)->TryGetStringField(TEXT("path"), Path))
            {
                NewWidgetBPMap.Add(Path, Value);
            }
        }
    }

    // Merge regular Blueprints
    TArray<TSharedPtr<FJsonValue>> MergedBPs;
    for (const TSharedPtr<FJsonValue>& ExistingValue : ExistingBPs)
    {
        const TSharedPtr<FJsonObject>* BPObject;
        if (ExistingValue->TryGetObject(BPObject) && BPObject->IsValid())
        {
            // Issue #206: Safe field access
            FString Path;
            if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
            {
                continue;  // Skip malformed entry
            }

            // Use new data if available, otherwise keep existing
            if (NewBPMap.Contains(Path))
            {
                MergedBPs.Add(NewBPMap[Path]);
                NewBPMap.Remove(Path);  // Mark as merged
            }
            else
            {
                MergedBPs.Add(ExistingValue);
            }
        }
    }

    // Add remaining new Blueprints (not in existing JSON)
    for (const auto& Pair : NewBPMap)
    {
        MergedBPs.Add(Pair.Value);
    }

    // Merge AnimBlueprints
    TArray<TSharedPtr<FJsonValue>> MergedAnimBPs;
    for (const TSharedPtr<FJsonValue>& ExistingValue : ExistingAnimBPs)
    {
        const TSharedPtr<FJsonObject>* BPObject;
        if (ExistingValue->TryGetObject(BPObject) && BPObject->IsValid())
        {
            // Issue #206: Safe field access
            FString Path;
            if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
            {
                continue;  // Skip malformed entry
            }

            if (NewAnimBPMap.Contains(Path))
            {
                MergedAnimBPs.Add(NewAnimBPMap[Path]);
                NewAnimBPMap.Remove(Path);
            }
            else
            {
                MergedAnimBPs.Add(ExistingValue);
            }
        }
    }

    for (const auto& Pair : NewAnimBPMap)
    {
        MergedAnimBPs.Add(Pair.Value);
    }

    // Issue #135 Phase 3: Merge WidgetBlueprints
    TArray<TSharedPtr<FJsonValue>> MergedWidgetBPs;
    for (const TSharedPtr<FJsonValue>& ExistingValue : ExistingWidgetBPs)
    {
        const TSharedPtr<FJsonObject>* BPObject;
        if (ExistingValue->TryGetObject(BPObject) && BPObject->IsValid())
        {
            // Issue #206: Safe field access
            FString Path;
            if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
            {
                continue;  // Skip malformed entry
            }

            if (NewWidgetBPMap.Contains(Path))
            {
                MergedWidgetBPs.Add(NewWidgetBPMap[Path]);
                NewWidgetBPMap.Remove(Path);
            }
            else
            {
                MergedWidgetBPs.Add(ExistingValue);
            }
        }
    }

    for (const auto& Pair : NewWidgetBPMap)
    {
        MergedWidgetBPs.Add(Pair.Value);
    }

    // Update arrays in JSON
    ExistingJson->SetArrayField(TEXT("blueprints"), MergedBPs);
    ExistingJson->SetArrayField(TEXT("anim_blueprints"), MergedAnimBPs);
    ExistingJson->SetArrayField(TEXT("widget_blueprints"), MergedWidgetBPs);  // Issue #135 Phase 3

    // Update counts
    ExistingJson->SetNumberField(TEXT("total_blueprints"), MergedBPs.Num());
    ExistingJson->SetNumberField(TEXT("total_anim_blueprints"), MergedAnimBPs.Num());
    ExistingJson->SetNumberField(TEXT("total_widgetbps"), MergedWidgetBPs.Num());  // Issue #135 Phase 3 - Fixed key name

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Merged JSON: %d blueprints, %d anim_blueprints, %d widget_blueprints"),
           MergedBPs.Num(), MergedAnimBPs.Num(), MergedWidgetBPs.Num());
}

// ============================================================================
// Save JSON with Metadata
// ============================================================================

bool UCUBlueprintMetadataCommandlet::SaveJsonWithMetadata(
    const FString& JsonPath,
    const TSharedPtr<FJsonObject>& JsonData,
    bool bIncrementalMode
)
{
    // ============================================================================
    // Issue #108 Phase 1 Incremental: Build cached_hashes map for next run
    // ============================================================================
    TSharedPtr<FJsonObject> CachedHashesObject = MakeShareable(new FJsonObject);

    // Extract hashes from blueprints array
    const TArray<TSharedPtr<FJsonValue>>* BlueprintsArray;
    if (JsonData->TryGetArrayField(TEXT("blueprints"), BlueprintsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *BlueprintsArray)
        {
            const TSharedPtr<FJsonObject>* BPObject;
            if (Value->TryGetObject(BPObject) && BPObject->IsValid())
            {
                // Issue #206: Safe field access
                FString Path;
                if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
                {
                    continue;  // Skip malformed entry
                }

                FString SavedHash;
                if ((*BPObject)->TryGetStringField(TEXT("saved_hash"), SavedHash))
                {
                    CachedHashesObject->SetStringField(Path, SavedHash);
                }
            }
        }
    }

    // Extract hashes from anim_blueprints array
    const TArray<TSharedPtr<FJsonValue>>* AnimBPsArray;
    if (JsonData->TryGetArrayField(TEXT("anim_blueprints"), AnimBPsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *AnimBPsArray)
        {
            const TSharedPtr<FJsonObject>* BPObject;
            if (Value->TryGetObject(BPObject) && BPObject->IsValid())
            {
                // Issue #206: Safe field access
                FString Path;
                if (!(*BPObject)->TryGetStringField(TEXT("path"), Path))
                {
                    continue;  // Skip malformed entry
                }

                FString SavedHash;
                if ((*BPObject)->TryGetStringField(TEXT("saved_hash"), SavedHash))
                {
                    CachedHashesObject->SetStringField(Path, SavedHash);
                }
            }
        }
    }

    // Add cached_hashes object to JSON
    JsonData->SetObjectField(TEXT("cached_hashes"), CachedHashesObject);

    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Built cached_hashes: %d entries"),
           CachedHashesObject->Values.Num());

    // Add metadata (Issue #108 Phase 1 Incremental)
    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue #561] Incremental: Generating Blueprint Metadata v5.0.0 with Variable Default Values"));
    JsonData->SetStringField(TEXT("build_mode"), bIncrementalMode ? TEXT("incremental") : TEXT("full"));
    JsonData->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToString());
    JsonData->SetStringField(TEXT("generator"), TEXT("NarshaMCP::BlueprintMetadataCommandlet v5.0.0 (Issue #561)"));
    JsonData->SetNumberField(TEXT("version"), 5);  // v5 with variable default values (Issue #561)

    // Serialize to string
    FString JsonString;
    TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);

    if (!FJsonSerializer::Serialize(JsonData.ToSharedRef(), JsonWriter))
    {
        UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[NarshaMCP] Failed to serialize JSON"));
        return false;
    }

    // ============================================================================
    // GZIP Compression (Issue #108: Preserve original GZIP functionality)
    // ============================================================================
    bool bSkipGzip = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_SKIP_GZIP")) == TEXT("1");
    bool bSavePlainJson = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_SAVE_PLAIN_JSON")) == TEXT("1");

    bool bCompressionSuccess = false;

    if (!bSkipGzip)
    {
        // Issue #14142: route the compressed write through the canonical saver. The atomic
        // temp -> rename and the UE 5.6 FCompression API are centralized in
        // FCUCommandletHelpers::SaveJsonWithCompression; output stays {JsonPath}.gz. The
        // cached_hashes / metadata build above and the plain-JSON fallback below are
        // commandlet-specific and intentionally remain here.
        bCompressionSuccess = FCUCommandletHelpers::SaveJsonWithCompression(JsonPath, JsonData);
    }
    else
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] GZIP compression skipped (UECODEGEN_SKIP_GZIP=1)"));
    }

    // Save plain JSON only if requested or if GZIP failed
    if (bSavePlainJson || !bCompressionSuccess)
    {
        // ============================================================================
        // Issue #108 Multi-Terminal Safety: Atomic write for plain JSON (temp → rename)
        // ============================================================================
        FString TempJsonPath = JsonPath + TEXT(".tmp");

        // Step 1: Write to temporary file
        if (FFileHelper::SaveStringToFile(JsonString, *TempJsonPath))
        {
            // Step 2: Atomic rename (replaces existing file atomically)
            IFileManager& FileManager = IFileManager::Get();
            if (FileManager.Move(*JsonPath, *TempJsonPath, true, true))  // Replace existing, Even read-only
            {
                if (bSavePlainJson)
                {
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Saved plain JSON (atomic write, UECODEGEN_SAVE_PLAIN_JSON=1): %s"), *JsonPath);
                }
                else
                {
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Saved plain JSON (atomic write, GZIP fallback): %s"), *JsonPath);
                }
                return true;
            }
            else
            {
                UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[NarshaMCP] Atomic rename failed for plain JSON, deleting temp file: %s"), *TempJsonPath);
                FileManager.Delete(*TempJsonPath);  // Cleanup temp file
                return false;
            }
        }
        else
        {
            UE_LOG(LogNarshaMCPMetadata, Error, TEXT("[NarshaMCP] Failed to save temp JSON file: %s"), *TempJsonPath);
            return false;
        }
    }

    return bCompressionSuccess;
}
