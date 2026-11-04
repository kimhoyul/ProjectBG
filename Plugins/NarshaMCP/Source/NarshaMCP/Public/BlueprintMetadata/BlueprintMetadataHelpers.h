// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UEdGraph;
class UEdGraphNode;
class FJsonObject;
class FJsonValue;
struct FAssetData;
class UBTCompositeNode;

/**
 * Shared utility functions for Blueprint metadata extraction
 *
 * Issue #108 Phase 0: Extracted from CUBlueprintMetadataCommandlet.cpp (1938 lines → modular)
 */
namespace BlueprintMetadataHelpers
{
    /**
     * Find the owning function for a node (Issue #23 - v2.8.6+, Issue #81 - revised + Option D, Issue #115 - Cases A & B)
     *
     * Resolves which named function/event a node "belongs to" for documentation
     * and call-graph extraction. Resolution order:
     *
     *   1. Self-check entry-event nodes (Issue #81):
     *      - UK2Node_CustomEvent          -> CustomFunctionName     (e.g., "Detonate")
     *      - UK2Node_ComponentBoundEvent  -> DelegatePropertyName    (Issue #115 Case B,
     *                                                                 e.g., "OnDeathStarted";
     *                                                                 CustomFunctionName is the
     *                                                                 mangled bind name)
     *      - UK2Node_Event                -> EventReference name    (e.g., "ReceiveBeginPlay")
     *      Note: UK2Node_CustomEvent and UK2Node_ComponentBoundEvent both inherit
     *      from UK2Node_Event, so they MUST be checked BEFORE the generic Event
     *      branch — otherwise CustomEvents collapse to engine event name, and
     *      ComponentBoundEvents leak their delegate signature class name (e.g.
     *      "LyraHealth_DeathEvent__DelegateSignature") instead of the handler name.
     *
     *   2. Function graph (GT_Function): walk graph for UK2Node_FunctionEntry,
     *      return the function being defined (e.g., "UpdateCustomStencil").
     *
     *   3. Ubergraph body nodes (Issue #81 Option D): walk upstream along the
     *      first exec input pin chain until an entry-event is reached. Returns
     *      that event's name (CustomFunctionName or EventReference). A Visited
     *      set guards against cycles produced by macro/composite expansion.
     *      This is what makes a CallFunction downstream of "Event Detonate"
     *      report owning_function="Detonate" rather than collapsing to the
     *      legacy "first K2Node_Event" pattern, which silently mislabeled
     *      ~52% of EventGraph cells pre-fix.
     *
     *   4. Pure data nodes — downstream walk (Issue #115 Case A): nodes with no
     *      exec input pin (K2Node_VariableGet, K2Node_GetArrayItem, Random Float
     *      in Range, Make Transform, ...) dead-end the walk-up immediately. Walk
     *      DOWNSTREAM along data output links to find an impure consumer and
     *      adopt its owning_function (mutual recursion bounded by Visited set
     *      + deterministic PinId-sorted first-consumer selection).
     *
     *   5. Fallback: graph name. Reached when (a) an Ubergraph body node has
     *      no reachable entry-event AND no resolvable downstream consumer, or
     *      (b) a function graph has no FunctionEntry (malformed BP).
     *
     * @param Node - EdGraphNode to find owner for
     * @return Function/event name (entry-events, FunctionEntry resolution,
     *         upstream-event resolution, or downstream-consumer resolution),
     *         "Unknown" if Node is null, or graph name as last-resort fallback.
     *         Never returns an empty string.
     */
    FString FindOwningFunction(UEdGraphNode* Node);

    /**
     * Check if graph is relevant for extraction (v3.5.0 - Refactoring)
     *
     * Filters graphs to only process EventGraph and Function Graphs.
     * This eliminates 28 lines of duplicated code.
     *
     * @param Graph - EdGraph to check
     * @return true if graph should be processed, false to skip
     */
    bool IsRelevantGraphForExtraction(UEdGraph* Graph);

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
    TSharedPtr<FJsonObject> BuildFinalJsonObject(
        const TArray<TSharedPtr<FJsonValue>>& BlueprintsArray,
        const TArray<TSharedPtr<FJsonValue>>& AnimBPsArray,
        const TArray<TSharedPtr<FJsonValue>>& AnimAssetsArray,
        const TArray<TSharedPtr<FJsonValue>>& StateTreesArray,
        const TArray<TSharedPtr<FJsonValue>>& BehaviorTreesArray,
        const TArray<TSharedPtr<FJsonValue>>& InputActionsArray,
        const TArray<TSharedPtr<FJsonValue>>& InputMappingsArray,
        int32 ProcessedCount,
        int32 ErrorCount,
        int32 AnimBPProcessedCount,
        int32 AnimBPErrorCount,
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

    /**
     * Enrich node JSON with type-specific data (v3.5.0 - Phase 3)
     *
     * Detects node type and adds type-specific fields to JSON.
     * Handles 10 Priority 1 node types.
     *
     * @param Node - EdGraphNode to enrich
     * @param NodeJson - JSON object to populate
     * @return Node type string
     */
    FString EnrichNodeByType(UEdGraphNode* Node, TSharedPtr<FJsonObject>& NodeJson);

    // Note: ExtractCompositeNodeData() moved to AIExtractors namespace (Issue #108 Phase 0 - Bug Fix)

    /**
     * Create minimal Blueprint JSON entry without LoadObject (Issue #1339)
     *
     * Used by fast paths: Data-Only, High-Cost Skip, DirectChild
     * Extracts metadata from FAssetData tags only (no Blueprint loading)
     *
     * @param AssetData - Asset registry data containing tags
     * @param HashSuffix - Optional suffix for hash differentiation (e.g., "HIGH_COST_SKIP")
     *                     If empty, uses ParentClassName for hash
     * @param bHighCostSkipped - Set high_cost_skipped flag in JSON
     * @param PreExtractedParentClass - If already extracted, pass to avoid re-extraction (DirectChild case)
     * @param PreExtractedNativeParentClass - If already extracted, pass to avoid re-extraction
     * @return JSON object with minimal Blueprint metadata
     */
    TSharedPtr<FJsonObject> CreateMinimalBlueprintEntry(
        const FAssetData& AssetData,
        const FString& HashSuffix = TEXT(""),
        bool bHighCostSkipped = false,
        const FString& PreExtractedParentClass = TEXT(""),
        const FString& PreExtractedNativeParentClass = TEXT("")
    );
}
