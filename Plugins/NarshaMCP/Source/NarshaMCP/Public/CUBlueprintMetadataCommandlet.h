// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUBlueprintMetadataCommandlet.generated.h"

class UBlueprint;
class UAnimBlueprint;
class UEdGraphNode;
struct FAssetData;
class FJsonObject;
class FJsonValue;
class IAssetRegistry;

// StateTree forward declarations (Issue #27 Phase 1)
#if WITH_EDITOR
class UStateTree;
class UStateTreeEditorData;
class UStateTreeState;
#endif

// BehaviorTree forward declarations (Issue #27 Phase 2)
#if WITH_EDITOR
class UBehaviorTree;
class UBTNode;
class UBTCompositeNode;
class UBTTaskNode;
class UBTDecorator;
class UBTService;
#endif

// Enhanced Input forward declarations (Issue #20 Phase 7 - v3.4.0)
class UInputAction;
class UInputMappingContext;
class UK2Node_InputAction;

/**
 * Blueprint profiling data structure (Issue #213 - 2-Tier Loading)
 *
 * Stores profiling information for Blueprint loading and extraction performance.
 */
struct FCUBlueprintProfilingData
{
    FString BlueprintName;      // BP_Character
    FString AssetPath;          // /Game/Characters/BP_Character
    FString BlueprintType;      // Blueprint, AnimBlueprint, WidgetBlueprint
    FString ParentClass;        // ACharacter, UUserWidget, etc.
    int32 NodeCount;            // Number of nodes
    int32 FunctionCount;        // Number of functions
    int32 VariableCount;        // Number of variables
    double LoadTime;            // LoadObject() time (ms)
    double ExtractTime;         // Extract functions time (ms)
    double SerializeTime;       // JSON serialization time (ms)
    double TotalTime;           // Total processing time (ms)

    FCUBlueprintProfilingData()
        : NodeCount(0)
        , FunctionCount(0)
        , VariableCount(0)
        , LoadTime(0.0)
        , ExtractTime(0.0)
        , SerializeTime(0.0)
        , TotalTime(0.0)
    {
    }
};

/**
 * Commandlet for extracting Blueprint metadata (functions, calls, variables)
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=NarshaMCP.CUBlueprintMetadata -OutputDir="Path/To/Output"
 *
 * Output:
 *   blueprint_metadata.json - JSON file containing all Blueprint metadata
 *
 * Features:
 *   - Extracts all Blueprint functions with parameters and flags
 *   - Traces Blueprint -> C++ call relationships
 *   - Detects Blueprint -> Blueprint calls
 *   - Exports Blueprint variables with metadata
 *   - 100% accurate (uses official Unreal APIs)
 */
UCLASS()
class NARSHAMCP_API UCUBlueprintMetadataCommandlet : public UCUCommandletBase
{
    GENERATED_BODY()

public:
    UCUBlueprintMetadataCommandlet(const FObjectInitializer& ObjectInitializer);

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
    static int32 RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry);

    // ============================================================================
    // Asset Change Tracking Helpers (Issue #1132)
    // ============================================================================

    /**
     * Update timestamp file after successful extraction (Issue #1132)
     *
     * Writes current UTC timestamp to file in ISO8601 format.
     * Used by both full extraction and early exit paths.
     *
     * @param TimestampPath - Path to timestamp file (last_extraction.timestamp)
     * @return true if timestamp updated successfully, false otherwise
     */
    static bool UpdateTimestampFile(const FString& TimestampPath);

private:
    // ============================================================================
    // Issue #108 Phase 1: Blueprint extraction functions moved to BlueprintExtractors namespace
    // ============================================================================
    // Functions migrated (6 declarations + docstrings):
    //   - ExtractBlueprintMetadata()
    //   - ExtractFunctions()
    //   - ExtractCallRelationships()
    //   - ExtractVariables()
    //   - ExtractGraphNodes()
    //   - ExtractNodePins()
    // See: Source/NarshaMCP/Public/BlueprintMetadata/BlueprintExtractors.h (Lines 21-85)
    // ============================================================================



    // Note (Issue #2368): FindOwningFunction() removed (dead code)
    // Active implementation is in BlueprintMetadataHelpers::FindOwningFunction()
    // All callers use BlueprintMetadataHelpers::FindOwningFunction() via using directive

#if WITH_EDITOR
// ============================================================================
    // Issue #108 Phase 1: AI extraction functions moved to AIExtractors namespace
    // ============================================================================
    // Functions migrated (4 declarations + docstrings):
    //   - ExtractStateTreeMetadata()
    //   - ExtractStateTreeStateRecursive()
    //   - ExtractBehaviorTreeMetadata()
    //   - ExtractBTNodeRecursive()
    // See: Source/NarshaMCP/Public/BlueprintMetadata/AIExtractors.h (Lines 21-89)
    // ============================================================================

    // ============================================================================
    // Issue #108 Phase 1: Input extraction functions moved to InputExtractors namespace
    // ============================================================================
    // Functions migrated (2 declarations + docstrings):
    //   - ExtractInputActionMetadata()
    //   - ExtractInputMappingContextMetadata()
    // See: Source/NarshaMCP/Public/BlueprintMetadata/InputExtractors.h (Lines 16-36)
    // ============================================================================
#endif

    // ============================================================================
    // Helper Functions (Refactoring v3.5.0)
    // ============================================================================

    // ============================================================================
    // Issue #108 Phase 1: Animation extraction functions moved to AnimationExtractors namespace
    // ============================================================================
    // Functions migrated (2 declarations):
    //   - ExtractAnimBlueprintMetadata()
    //   - ExtractGameplayTagFromNotify()
    // See: Source/NarshaMCP/Public/BlueprintMetadata/AnimationExtractors.h (Lines 21-47)
    // ============================================================================


    // Note (Issue #327): IsRelevantGraphForExtraction() removed (dead code, never called)
    // Active implementation is in BlueprintMetadataHelpers.cpp:85 (blacklist approach)
    // All actual callers use BlueprintMetadataHelpers::IsRelevantGraphForExtraction()

    /**
     * Create basic asset JSON object (v3.5.0 - Refactoring)
     *
     * Creates JSON object with common asset fields (name, path, package).
     *
     * @param AssetName - Asset name
     * @param AssetData - Asset registry data
     * @return JSON object with basic fields populated
     */
    TSharedPtr<FJsonObject> CreateBasicAssetJson(const FString& AssetName, const FAssetData& AssetData);

    // ============================================================================
    // JSON Building Helpers (Phase 2 - v3.5.0)
    // ============================================================================

    /**
     * Build final JSON output with all metadata (v3.5.0 - Phase 2)
     *
     * Creates root JSON object with all arrays and metadata fields.
     *
     * @param BlueprintsArray - Regular Blueprints
     * @param AnimBPsArray - Animation Blueprints
     * @param AnimAssetsArray - Animation assets
     * @param StateTreesArray - StateTrees
     * @param BehaviorTreesArray - BehaviorTrees
     * @param InputActionsArray - InputActions
     * @param InputMappingsArray - InputMappings
     * @param ProcessedCount - Regular BP count
     * @param ErrorCount - Regular BP errors
     * @param AnimBPProcessedCount - AnimBP count
     * @param AnimBPErrorCount - AnimBP errors
     * @param AnimAssetProcessedCount - Anim asset count
     * @param AnimAssetErrorCount - Anim asset errors
     * @param StateTreeProcessedCount - StateTree count
     * @param StateTreeErrorCount - StateTree errors
     * @param BTProcessedCount - BehaviorTree count
     * @param BTErrorCount - BehaviorTree errors
     * @param InputActionProcessedCount - InputAction count
     * @param InputActionErrorCount - InputAction errors
     * @param InputMappingProcessedCount - InputMapping count
     * @param InputMappingErrorCount - InputMapping errors
     * @return Root JSON object
     */
    static TSharedPtr<FJsonObject> BuildFinalJsonObject(
        const TArray<TSharedPtr<FJsonValue>>& BlueprintsArray,
        const TArray<TSharedPtr<FJsonValue>>& AnimBPsArray,
        const TArray<TSharedPtr<FJsonValue>>& WidgetBPsArray,  // Issue #135 Phase 3
        const TArray<TSharedPtr<FJsonValue>>& AnimAssetsArray,
        const TArray<TSharedPtr<FJsonValue>>& StateTreesArray,
        const TArray<TSharedPtr<FJsonValue>>& BehaviorTreesArray,
        const TArray<TSharedPtr<FJsonValue>>& InputActionsArray,
        const TArray<TSharedPtr<FJsonValue>>& InputMappingsArray,
        const TArray<TSharedPtr<FJsonValue>>& InputActionBindingsArray,  // Issue #584 Phase 2A
        int32 ProcessedCount,
        int32 ErrorCount,
        int32 AnimBPProcessedCount,
        int32 AnimBPErrorCount,
        int32 WidgetBPProcessedCount,  // Issue #135 Phase 3
        int32 WidgetBPErrorCount,  // Issue #135 Phase 3
        int32 AnimAssetProcessedCount,
        int32 AnimAssetErrorCount,
        int32 StateTreeProcessedCount,
        int32 StateTreeErrorCount,
        int32 BTProcessedCount,
        int32 BTErrorCount,
        int32 InputActionProcessedCount,
        int32 InputActionErrorCount,
        int32 InputMappingProcessedCount,
        int32 InputMappingErrorCount
    );

    // Note (Issue #2368): EnrichNodeByType() removed (dead code)
    // Active implementation is in BlueprintMetadataHelpers::EnrichNodeByType()
    // All callers use BlueprintMetadataHelpers::EnrichNodeByType() via using directive

    // ============================================================================
    // Profiling Helpers (Issue #212)
    // ============================================================================

    /**
     * Save Blueprint profiling results to CSV file (Issue #212)
     *
     * Generates CSV file with per-Blueprint timing data (Load, Extract, Serialize).
     * Helps identify performance bottlenecks in Blueprint loading.
     *
     * @param ProfilingData - Array of profiling data for each Blueprint
     * @param OutputDir - Output directory for CSV file
     */
    static void SaveProfilingResultsToCSV(
        const TArray<struct FCUBlueprintProfilingData>& ProfilingData,
        const FString& OutputDir
    );

    // ============================================================================
    // Incremental Build Helpers (Issue #108 Phase 1 Incremental)
    // ============================================================================

    /**
     * Load existing JSON metadata if available (Issue #108 Phase 1 Incremental)
     *
     * Loads previously generated JSON file for incremental build.
     *
     * @param JsonPath - Path to blueprint_metadata.json
     * @return Shared pointer to JSON object (null if file not found or invalid)
     */
    static TSharedPtr<FJsonObject> LoadExistingJson(const FString& JsonPath);

    /**
     * Find changed Blueprints using hash comparison (Issue #108 Phase 1 Incremental)
     *
     * Compares current SavedHash with cached hash from JSON.
     * Only returns Blueprints that have changed since last run.
     *
     * @param AllBlueprints - All Blueprint assets in project
     * @param ExistingJson - Previously generated JSON metadata
     * @param AssetRegistry - Asset registry for hash lookups
     * @return Array of Blueprints that have changed
     */
    static TArray<FAssetData> FindChangedBlueprints(
        const TArray<FAssetData>& AllBlueprints,
        const TSharedPtr<FJsonObject>& ExistingJson,
        IAssetRegistry& AssetRegistry
    );

    /**
     * Merge new Blueprint data with existing JSON (Issue #108 Phase 1 Incremental)
     *
     * Updates existing JSON with newly processed Blueprints.
     * Preserves unchanged Blueprints from existing JSON.
     *
     * @param ExistingJson - Previously generated JSON metadata
     * @param NewBlueprints - Newly processed Blueprint data
     * @param NewAnimBPs - Newly processed AnimBlueprint data
     * @param NewWidgetBPs - Newly processed WidgetBlueprint data (Issue #135 Phase 3)
     */
    static void MergeJsonData(
        TSharedPtr<FJsonObject>& ExistingJson,
        const TArray<TSharedPtr<FJsonValue>>& NewBlueprints,
        const TArray<TSharedPtr<FJsonValue>>& NewAnimBPs,
        const TArray<TSharedPtr<FJsonValue>>& NewWidgetBPs
    );

    /**
     * Save JSON with metadata for incremental build (Issue #108 Phase 1 Incremental)
     *
     * Saves JSON with generation timestamp and incremental mode flag.
     *
     * @param JsonPath - Output path for blueprint_metadata.json
     * @param JsonData - Complete JSON object to save
     * @param bIncrementalMode - Whether this was incremental build
     * @return true if saved successfully
     */
    static bool SaveJsonWithMetadata(
        const FString& JsonPath,
        const TSharedPtr<FJsonObject>& JsonData,
        bool bIncrementalMode
    );

    // ============================================================================
    // Multiprocess Helpers (Issue #135 - Multiprocess Support)
    // ============================================================================

    /**
     * Parse multiprocess command line parameters
     *
     * Extracts worker mode, worker ID, range indices, and worker count.
     *
     * @param Params - Command line parameters string
     * @param bWorkerMode - Output: true if in worker mode
     * @param WorkerID - Output: Worker ID (0-based)
     * @param StartIndex - Output: Start index for Blueprint processing
     * @param EndIndex - Output: End index for Blueprint processing
     * @param NumWorkers - Output: Total number of workers
     */
    void ParseMultiprocessParams(
        const FString& Params,
        bool& bWorkerMode,
        int32& WorkerID,
        int32& StartIndex,
        int32& EndIndex,
        int32& NumWorkers
    );

    /**
     * Run commandlet in worker mode
     *
     * Processes assigned Blueprint subset and writes partial JSON.
     *
     * @param Params - Command line parameters
     * @param BlueprintAssetList - Complete list of Blueprint assets
     * @param OutputDir - Output directory path
     * @param WorkerID - This worker's ID (0-based)
     * @param StartIndex - Start index in BlueprintAssetList
     * @param EndIndex - End index in BlueprintAssetList
     * @return 0 on success, non-zero on error
     */
    int32 RunWorkerMode(
        const FString& Params,
        const TArray<FAssetData>& BlueprintAssetList,
        const FString& OutputDir,
        int32 WorkerID,
        int32 StartIndex,
        int32 EndIndex
    );

    /**
     * Run commandlet in master mode
     *
     * Coordinates workers and merges partial results.
     *
     * @param Params - Command line parameters
     * @param BlueprintAssetList - Complete list of Blueprint assets
     * @param OutputDir - Output directory path
     * @param NumWorkers - Total number of workers to spawn
     * @return 0 on success, non-zero on error
     */
    int32 RunMasterMode(
        const FString& Params,
        const TArray<FAssetData>& BlueprintAssetList,
        const FString& OutputDir,
        int32 NumWorkers
    );

    /**
     * Merge partial JSON files from workers
     *
     * Combines worker outputs into final blueprint_metadata.json.
     *
     * @param OutputDir - Output directory path
     * @param NumWorkers - Number of worker partial files to merge
     * @return true if merged successfully
     */
    bool MergePartialJsonFiles(const FString& OutputDir, int32 NumWorkers);

    // ============================================================================
};
