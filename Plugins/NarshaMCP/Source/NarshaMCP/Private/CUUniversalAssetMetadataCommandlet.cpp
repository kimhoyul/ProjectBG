// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUUniversalAssetMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Compression.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPUniversal, Log, All);

UCUUniversalAssetMetadataCommandlet::UCUUniversalAssetMetadataCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

// Override CommandletBase::RunExtraction()
int32 UCUUniversalAssetMetadataCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
    return RunUniversalExtraction(OutputDir, Registry);
}

// Static method for batch commandlet
int32 UCUUniversalAssetMetadataCommandlet::RunUniversalExtraction(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
    UE_LOG(LogNarshaMCPUniversal, Display, TEXT("[UniversalAsset] Running universal extraction..."));

    // Get ALL assets (empty filter = all types)
    FARFilter UniversalFilter;
    // Filter by package path (/Game = project assets)
    UniversalFilter.PackagePaths.Add("/Game");
    UniversalFilter.bRecursivePaths = true;

    TArray<FAssetData> AllAssets;
    AssetRegistry.GetAssets(UniversalFilter, AllAssets);

    UE_LOG(LogNarshaMCPUniversal, Display, TEXT("[NarshaMCP] Found %d total assets"), AllAssets.Num());

    // Build JSON array
    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    AssetsArray.Reserve(AllAssets.Num());

    int32 ProcessedCount = 0;
    int32 ProgressStep = FMath::Max(1, AllAssets.Num() / 10);  // 10% intervals

    // PROFILING: Asset metadata extraction
    FCUCommandletHelpers::BeginProfileSection(TEXT("UniversalAsset.MetadataExtraction"));

    FDateTime StartTime = FDateTime::UtcNow();

    for (int32 Index = 0; Index < AllAssets.Num(); ++Index)
    {
        const FAssetData& AssetData = AllAssets[Index];

        TSharedPtr<FJsonObject> AssetJson = ExtractAssetMetadata(AssetData);
        if (AssetJson.IsValid())
        {
            AssetsArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
            ProcessedCount++;
        }

        // Progress logging (10% intervals)
        if ((Index + 1) % ProgressStep == 0 || (Index + 1) == AllAssets.Num())
        {
            float Progress = (float)(Index + 1) / AllAssets.Num() * 100.0f;
            float ElapsedSeconds = (FDateTime::UtcNow() - StartTime).GetTotalSeconds();
            float EstimatedTotal = (Progress > 0) ? (ElapsedSeconds / Progress * 100.0f) : 0.0f;
            float Remaining = EstimatedTotal - ElapsedSeconds;

            UE_LOG(LogNarshaMCPUniversal, Display, TEXT("[NarshaMCP] Progress: %.1f%% (%d/%d) | Elapsed: %.1fs | Remaining: ~%.1fs"),
                   Progress, Index + 1, AllAssets.Num(), ElapsedSeconds, Remaining);
        }
    }

    FCUCommandletHelpers::EndProfileSection(TEXT("UniversalAsset.MetadataExtraction"));

    // PROFILING: JSON compression and save
    FCUCommandletHelpers::BeginProfileSection(TEXT("UniversalAsset.JsonCompressionSave"));

    // Build root JSON object
    TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

    // Metadata
    RootJson->SetNumberField(TEXT("version"), 1);
    RootJson->SetStringField(TEXT("generator"), TEXT("NarshaMCP::UniversalAssetMetadataCommandlet v4.6.0"));
    RootJson->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToString());

    // Stats
    RootJson->SetNumberField(TEXT("total_assets"), ProcessedCount);
    RootJson->SetNumberField(TEXT("processed"), ProcessedCount);
    RootJson->SetNumberField(TEXT("errors"), AllAssets.Num() - ProcessedCount);

    // Asset array
    RootJson->SetArrayField(TEXT("assets"), AssetsArray);

    // Save JSON with GZIP compression
    FString OutputPath = OutputDir / TEXT("universal_asset_metadata.json");

    if (SaveJsonWithCompression(OutputPath, RootJson))
    {
        FCUCommandletHelpers::EndProfileSection(TEXT("UniversalAsset.JsonCompressionSave"));

        UE_LOG(LogNarshaMCPUniversal, Display, TEXT("[NarshaMCP] Universal Asset metadata saved to: %s.gz"), *OutputPath);
        UE_LOG(LogNarshaMCPUniversal, Display, TEXT("[NarshaMCP] Processing complete: %d assets"), ProcessedCount);
        return 0; // Success
    }
    else
    {
        FCUCommandletHelpers::EndProfileSection(TEXT("UniversalAsset.JsonCompressionSave"));

        UE_LOG(LogNarshaMCPUniversal, Error, TEXT("[NarshaMCP] Failed to save JSON"));
        return 1;
    }
}

TSharedPtr<FJsonObject> UCUUniversalAssetMetadataCommandlet::ExtractAssetMetadata(const FAssetData& AssetData)
{
    TSharedPtr<FJsonObject> AssetJson = MakeShareable(new FJsonObject);

    // Basic info (lightweight - no asset loading!)
    AssetJson->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
    AssetJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());
    AssetJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
    AssetJson->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

    // Extract asset_type from class path (Issue #335 Critical Bug #3)
    // Example: "/Script/Engine.DataTable" → "DataTable"
    //          "/Script/Engine.Blueprint" → "Blueprint"
    FString ClassName = AssetData.AssetClassPath.GetAssetName().ToString();
    FString AssetType = ClassName;

    // Remove common UE prefixes (U, A, F, S, etc.)
    if (ClassName.Len() > 1)
    {
        TCHAR FirstChar = ClassName[0];
        if (FirstChar == 'U' || FirstChar == 'A' || FirstChar == 'F' ||
            FirstChar == 'S' || FirstChar == 'T' || FirstChar == 'I')
        {
            // Check if second char is uppercase (UDataTable → DataTable, not User → ser)
            if (FChar::IsUpper(ClassName[1]))
            {
                AssetType = ClassName.Mid(1);
            }
        }
    }

    AssetJson->SetStringField(TEXT("asset_type"), AssetType);

    // Extract tags (AssetRegistry metadata)
    TSharedPtr<FJsonObject> TagsJson = MakeShareable(new FJsonObject);

    for (const auto& TagPair : AssetData.TagsAndValues)
    {
        FString Value;
        Value = TagPair.Value.GetValue();

        // Only include non-empty tags
        if (!Value.IsEmpty())
        {
            TagsJson->SetStringField(TagPair.Key.ToString(), Value);
        }
    }

    // Only add tags object if not empty
    if (TagsJson->Values.Num() > 0)
    {
        AssetJson->SetObjectField(TEXT("tags"), TagsJson);
    }

    return AssetJson;
}

bool UCUUniversalAssetMetadataCommandlet::SaveJsonWithCompression(
    const FString& OutputPath,
    const TSharedPtr<FJsonObject>& JsonData)
{
    // Convert JSON to string
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    if (!FJsonSerializer::Serialize(JsonData.ToSharedRef(), Writer))
    {
        UE_LOG(LogNarshaMCPUniversal, Error, TEXT("[NarshaMCP] Failed to serialize JSON"));
        return false;
    }

    // Convert to UTF-8
    FTCHARToUTF8 UTF8String(*JsonString);
    TArray<uint8> UncompressedData;
    UncompressedData.Append((uint8*)UTF8String.Get(), UTF8String.Length());

    // Compress with GZIP (UE 5.6 API)
    // Calculate compressed buffer size
    int64 CompressedSizeRequired = 0;
    if (!FCompression::CompressMemoryBound(NAME_Gzip, CompressedSizeRequired, UncompressedData.Num()))
    {
        UE_LOG(LogNarshaMCPUniversal, Error, TEXT("[NarshaMCP] Failed to determine compression buffer size"));
        return false;
    }

    // Allocate buffer for compressed data
    TArray<uint8> CompressedData;
    CompressedData.SetNumUninitialized(CompressedSizeRequired);

    // Compress (UE 5.6 requires int64& for CompressedSize)
    int64 ActualCompressedSize = CompressedSizeRequired;
    if (!FCompression::CompressMemory(
        NAME_Gzip,                      // Compression format
        CompressedData.GetData(),       // Output buffer (void*)
        ActualCompressedSize,           // Compressed size (int64&)
        UncompressedData.GetData(),     // Input data (const void*)
        (int64)UncompressedData.Num())) // Input size (int64)
    {
        UE_LOG(LogNarshaMCPUniversal, Error, TEXT("[NarshaMCP] Failed to compress JSON"));
        return false;
    }

    // Resize to actual compressed size
    CompressedData.SetNum(ActualCompressedSize);

    // Save compressed data
    FString GzipPath = OutputPath + TEXT(".gz");
    if (!FFileHelper::SaveArrayToFile(CompressedData, *GzipPath))
    {
        UE_LOG(LogNarshaMCPUniversal, Error, TEXT("[NarshaMCP] Failed to save compressed file: %s"), *GzipPath);
        return false;
    }

    // Log compression stats
    float CompressionRatio = (float)CompressedData.Num() / UncompressedData.Num() * 100.0f;
    UE_LOG(LogNarshaMCPUniversal, Display, TEXT("[NarshaMCP] Compression: %d bytes -> %d bytes (%.1f%%)"),
           UncompressedData.Num(), CompressedData.Num(), CompressionRatio);

    return true;
}
