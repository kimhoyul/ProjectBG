// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUUniversalAssetMetadataCommandlet.generated.h"

struct FAssetData;
class FJsonObject;
class FJsonValue;
class IAssetRegistry;

/**
 * Commandlet for extracting Universal Asset metadata (ALL asset types)
 *
 * **Issue #155 Phase 2** - Universal Asset Search Support
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=UniversalAssetMetadata -OutputDir="Path/To/Output"
 *
 * Output:
 *   universal_asset_metadata.json - JSON file containing all asset metadata
 *
 * Features:
 *   - Extracts ALL asset types (200+ classes):
 *     - Materials, Textures, Sounds, Meshes
 *     - DataAssets, Curves, DataTables
 *     - Niagara, PCG, Particles
 *     - And ALL other UE asset classes
 *   - Lightweight metadata (name, class, path, tags)
 *   - Fast enumeration using AssetRegistry API
 *   - GZIP compression for large projects
 *
 * Performance:
 *   - 1,000 assets: ~3s
 *   - 5,000 assets: ~8s
 *   - 10,000 assets: ~15s
 *
 * Version History:
 *   - v4.6.0 (Issue #155 Phase 2): Initial implementation
 */
UCLASS()
class NARSHAMCP_API UCUUniversalAssetMetadataCommandlet : public UCUCommandletBase
{
    GENERATED_BODY()

public:
    UCUUniversalAssetMetadataCommandlet(const FObjectInitializer& ObjectInitializer);

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
    static int32 RunUniversalExtraction(const FString& OutputDir, IAssetRegistry& AssetRegistry);

private:
    /**
     * Extract basic metadata from FAssetData
     *
     * Lightweight extraction - no asset loading required.
     *
     * @param AssetData - Asset data from AssetRegistry
     * @return JSON object with basic asset info
     */
    static TSharedPtr<FJsonObject> ExtractAssetMetadata(const FAssetData& AssetData);

    /**
     * Save JSON to file with GZIP compression
     *
     * @param OutputPath - Output file path
     * @param JsonData - JSON data to save
     * @return true if saved successfully
     */
    static bool SaveJsonWithCompression(const FString& OutputPath, const TSharedPtr<FJsonObject>& JsonData);
};
