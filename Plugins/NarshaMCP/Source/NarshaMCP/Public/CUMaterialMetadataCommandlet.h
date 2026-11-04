// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUMaterialMetadataCommandlet.generated.h"

class UMaterial;
class UMaterialInstance;
class UMaterialInstanceConstant;
class UMaterialInstanceDynamic;
class UMaterialExpression;
class UTexture;
struct FAssetData;
class FJsonObject;
class FJsonValue;
class IAssetRegistry;

/**
 * Commandlet for extracting Material metadata (nodes, hierarchy, HLSL, performance)
 *
 * Issue #224: Material Policy Layer - Blueprint pattern reuse (100%)
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=NarshaMCP.CUMaterialMetadata -OutputDir="Path/To/Output"
 *
 * Output:
 *   material_metadata.json.gz - GZIP compressed JSON file containing all Material metadata
 *
 * Features:
 *   - Extracts MaterialExpression nodes with parameters and connections
 *   - Builds Material Instance hierarchy (Master → Instance tree)
 *   - Generates HLSL shader code
 *   - Analyzes performance (instruction count, texture samplers)
 *   - Provides optimization suggestions
 *   - 100% accurate (uses official Unreal Material APIs)
 *
 * Architecture (Blueprint pattern reuse):
 *   - Same Commandlet → JSON → Parser → Policy Tool pattern
 *   - Lazy loading support (Issue #21)
 *   - GZIP compression (v4.6.0)
 *   - Batch commandlet support (v4.8.0)
 *   - Incremental build support (Issue #108)
 *
 * @since v4.11.0 (Issue #224 - Material Policy Layer Phase 1)
 */
UCLASS()
class NARSHAMCP_API UCUMaterialMetadataCommandlet : public UCUCommandletBase
{
    GENERATED_BODY()

public:
    UCUMaterialMetadataCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
    /**
     * Run extraction logic - Overrides CommandletBase::RunExtraction()
     *
     * Performs Material metadata extraction work.
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
     * @since v4.11.0 (Issue #224 - Batch Commandlet Support)
     */
    static int32 RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry);

private:
    // ============================================================================
    // Material Extraction Functions
    // ============================================================================

    /**
     * Extract metadata from a single Material asset
     *
     * Extracts nodes, connections, parameters, and hierarchy information.
     *
     * @param Material - Material asset to extract
     * @param AssetData - Asset registry data
     * @param AssetRegistry - Asset registry for hierarchy lookups
     * @return JSON object with Material metadata
     */
    static TSharedPtr<FJsonObject> ExtractMaterialMetadata(
        UMaterial* Material,
        const FAssetData& AssetData,
        IAssetRegistry& AssetRegistry
    );

    /**
     * Extract metadata from a Material Instance asset
     *
     * Extracts parent reference and parameter overrides.
     *
     * @param MaterialInstance - Material Instance asset to extract
     * @param AssetData - Asset registry data
     * @param AssetRegistry - Asset registry for hierarchy lookups
     * @return JSON object with Material Instance metadata
     */
    static TSharedPtr<FJsonObject> ExtractMaterialInstanceMetadata(
        UMaterialInstance* MaterialInstance,
        const FAssetData& AssetData,
        IAssetRegistry& AssetRegistry
    );

    /**
     * Extract MaterialExpression nodes from a Material
     *
     * Iterates Material->Expressions array and extracts:
     * - Node GUID (persistent identifier)
     * - Node type (e.g., MaterialExpressionTextureSample)
     * - Node parameters (e.g., Texture reference, SamplerType)
     * - Node position (EditorX, EditorY)
     * - Output pins with types
     * - Input connections (FExpressionInput)
     *
     * @param Material - Material asset
     * @return JSON array of MaterialExpression nodes
     */
    static TArray<TSharedPtr<FJsonValue>> ExtractMaterialNodes(UMaterial* Material);

    /**
     * Extract pin connections from MaterialExpression nodes
     *
     * Traces FExpressionInput connections between nodes.
     * Maps source node GUID + output pin → target node GUID + input pin.
     *
     * @param Material - Material asset
     * @return JSON array of connections
     */
    static TArray<TSharedPtr<FJsonValue>> ExtractMaterialConnections(UMaterial* Material);

    /**
     * Extract Material inputs (BaseColor, Roughness, etc.)
     *
     * Extracts which nodes are connected to Material output pins.
     *
     * @param Material - Material asset
     * @return JSON array of Material input connections
     */
    static TArray<TSharedPtr<FJsonValue>> ExtractMaterialInputs(UMaterial* Material);

    /**
     * Build Material Instance hierarchy tree
     *
     * Finds all Material Instances that reference this Material as parent.
     * Recursively builds child hierarchy.
     *
     * @param MaterialPath - Material asset path
     * @param AssetRegistry - Asset registry for instance lookups
     * @return JSON array of child Material Instance paths
     */
    static TArray<TSharedPtr<FJsonValue>> BuildMaterialInstanceHierarchy(
        const FString& MaterialPath,
        IAssetRegistry& AssetRegistry
    );

    /**
     * Extract Material Instance parameter overrides
     *
     * Extracts scalar, vector, and texture parameter overrides.
     *
     * @param MaterialInstance - Material Instance asset
     * @return JSON object with parameter overrides
     */
    static TSharedPtr<FJsonObject> ExtractMaterialInstanceParameters(UMaterialInstance* MaterialInstance);

    // ============================================================================
    // HLSL & Performance Analysis Functions
    // ============================================================================

    /**
     * Extract generated HLSL shader code from Material
     *
     * Uses Material->GetMaterialResource()->GetMaterialShaderCode()
     * to extract generated HLSL code.
     *
     * Note: Requires Material to be compiled. Returns empty string if not compiled.
     *
     * @param Material - Material asset
     * @return Generated HLSL code string
     */
    static FString ExtractHLSLCode(UMaterial* Material);

    /**
     * Analyze Material performance
     *
     * Extracts:
     * - Instruction count (from shader compilation)
     * - Texture sampler count (count MaterialExpressionTextureSample nodes)
     * - Complexity rating (Low/Medium/High)
     * - Estimated GPU cost
     *
     * @param Material - Material asset
     * @return JSON object with performance metrics
     */
    static TSharedPtr<FJsonObject> AnalyzeMaterialPerformance(UMaterial* Material);

    /**
     * Extract compiler-accurate shader statistics (Issue #446)
     *
     * Extracts real instruction counts from FMaterialShaderMap using
     * the same method as Material Editor Stats Panel.
     *
     * Falls back to heuristic estimation if Material is not compiled.
     *
     * @param Material - Material asset
     * @param FeatureLevel - Target platform feature level (SM5, SM6, etc.)
     * @return JSON object with compiler stats, or nullptr if unavailable
     *
     * @since v4.32.0 (Issue #446 - Compiler-accurate Material performance)
     */
    static TSharedPtr<FJsonObject> ExtractCompilerStats(UMaterial* Material, ERHIFeatureLevel::Type FeatureLevel);

    /**
     * Generate optimization suggestions for Material
     *
     * Analyzes Material nodes and provides optimization suggestions:
     * - Texture channel merging opportunities
     * - Math operation simplifications (e.g., Power(x,2) → x*x)
     * - Mobile compatibility warnings
     *
     * @param Material - Material asset
     * @return JSON array of optimization suggestions
     */
    static TArray<TSharedPtr<FJsonValue>> GenerateOptimizationSuggestions(UMaterial* Material);

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
     * @param AssetRegistry - Asset registry for SavedHash lookup (Issue #224 Hotfix)
     * @return JSON object with basic fields populated
     */
    static TSharedPtr<FJsonObject> CreateBasicAssetJson(
        const FString& AssetName,
        const FAssetData& AssetData,
        IAssetRegistry& AssetRegistry);

    /**
     * Get Material Expression node GUID
     *
     * MaterialExpressions don't have persistent GUIDs by default.
     * We generate a deterministic GUID based on:
     * - Node type
     * - Position (EditorX, EditorY)
     * - Parameter values (if any)
     *
     * This ensures same GUID across multiple runs for same node.
     *
     * @param Expression - Material Expression node
     * @param Material - Parent Material asset
     * @return Generated GUID string
     */
    static FString GetMaterialExpressionGUID(UMaterialExpression* Expression, UMaterial* Material);

    /**
     * Get MaterialExpression type name
     *
     * Returns human-readable type name (e.g., "MaterialExpressionTextureSample").
     *
     * @param Expression - Material Expression node
     * @return Type name string
     */
    static FString GetMaterialExpressionTypeName(UMaterialExpression* Expression);

    /**
     * Extract MaterialExpression parameters
     *
     * Extracts type-specific parameters based on Expression type.
     * E.g., for MaterialExpressionTextureSample:
     * - Texture: Referenced texture asset
     * - SamplerType: Color/LinearColor/Grayscale/etc.
     *
     * @param Expression - Material Expression node
     * @return JSON object with parameters
     */
    static TSharedPtr<FJsonObject> ExtractMaterialExpressionParameters(UMaterialExpression* Expression);

    /**
     * Count texture samplers in Material
     *
     * Counts MaterialExpressionTextureSample nodes.
     *
     * @param Material - Material asset
     * @return Texture sampler count
     */
    static int32 CountTextureSamplers(UMaterial* Material);

    // ============================================================================
    // JSON Building Functions
    // ============================================================================

    /**
     * Build final JSON output with all metadata
     *
     * Creates root JSON object with arrays and metadata fields.
     *
     * @param MaterialsArray - Regular Materials
     * @param MaterialInstancesArray - Material Instances
     * @param ProcessedCount - Material count
     * @param ErrorCount - Material errors
     * @param InstanceProcessedCount - Instance count
     * @param InstanceErrorCount - Instance errors
     * @return Root JSON object
     */
    static TSharedPtr<FJsonObject> BuildFinalJsonObject(
        const TArray<TSharedPtr<FJsonValue>>& MaterialsArray,
        const TArray<TSharedPtr<FJsonValue>>& MaterialInstancesArray,
        int32 ProcessedCount,
        int32 ErrorCount,
        int32 InstanceProcessedCount,
        int32 InstanceErrorCount
    );

    // ============================================================================
    // Incremental Build Helpers (Issue #108 Phase 1 Incremental pattern)
    // ============================================================================

    /**
     * Load existing JSON metadata if available
     *
     * Loads previously generated JSON file for incremental build.
     *
     * @param JsonPath - Path to material_metadata.json
     * @return Shared pointer to JSON object (null if file not found or invalid)
     */
    static TSharedPtr<FJsonObject> LoadExistingJson(const FString& JsonPath);

    /**
     * Find changed Materials using hash comparison
     *
     * Compares current SavedHash with cached hash from JSON.
     * Only returns Materials that have changed since last run.
     *
     * @param AllMaterials - All Material assets in project
     * @param ExistingJson - Previously generated JSON metadata
     * @param AssetRegistry - Asset registry for hash lookups
     * @return Array of Materials that have changed
     */
    static TArray<FAssetData> FindChangedMaterials(
        const TArray<FAssetData>& AllMaterials,
        const TSharedPtr<FJsonObject>& ExistingJson,
        IAssetRegistry& AssetRegistry
    );

    /**
     * Merge new Material data with existing JSON
     *
     * Updates existing JSON with newly processed Materials.
     * Preserves unchanged Materials from existing JSON.
     *
     * @param ExistingJson - Previously generated JSON metadata
     * @param NewMaterials - Newly processed Material data
     * @param NewInstances - Newly processed Material Instance data
     */
    static void MergeJsonData(
        TSharedPtr<FJsonObject>& ExistingJson,
        const TArray<TSharedPtr<FJsonValue>>& NewMaterials,
        const TArray<TSharedPtr<FJsonValue>>& NewInstances
    );

    /**
     * Save JSON with metadata for incremental build
     *
     * Saves JSON with generation timestamp and incremental mode flag.
     * Uses GZIP compression (v4.6.0).
     *
     * @param JsonPath - Output path for material_metadata.json
     * @param JsonData - Complete JSON object to save
     * @param bIncrementalMode - Whether this was incremental build
     * @return true if saved successfully
     */
    static bool SaveJsonWithMetadata(
        const FString& JsonPath,
        const TSharedPtr<FJsonObject>& JsonData,
        bool bIncrementalMode
    );
};
