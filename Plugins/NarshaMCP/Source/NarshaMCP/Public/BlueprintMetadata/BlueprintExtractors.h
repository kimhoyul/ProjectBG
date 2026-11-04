// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class FJsonObject;
class FJsonValue;
class IAssetRegistry;
struct FAssetData;

/**
 * Blueprint metadata extraction functions
 *
 * Issue #108 Phase 0: Extracted from CUBlueprintMetadataCommandlet.cpp (1938 lines → modular)
 */
namespace BlueprintExtractors
{
    /**
     * Extract metadata from a single Blueprint
     *
     * @param Blueprint - Blueprint to extract from
     * @param AssetData - Asset registry data for the Blueprint
     * @return JSON object containing Blueprint metadata, or nullptr on error
     */
    TSharedPtr<FJsonObject> ExtractBlueprintMetadata(UBlueprint* Blueprint, const FAssetData& AssetData);

    /**
     * Extract all functions from Blueprint
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutFunctions - Output array of function JSON objects
     */
    void ExtractFunctions(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutFunctions);

    /**
     * Extract call relationships from Blueprint graphs
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutCalls - Output array of call JSON objects
     */
    void ExtractCallRelationships(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutCalls);

    /**
     * Extract variables from Blueprint
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutVariables - Output array of variable JSON objects
     */
    void ExtractVariables(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutVariables);

    /**
     * Extract all graph nodes with pin connections (v2.8.3+)
     *
     * Extracts 10 Priority 1 node types for complete execution flow tracking:
     * - UK2Node_AsyncAction (Wait Gameplay Event, Delay, etc.)
     * - UK2Node_ExecutionSequence (Sequence node)
     * - UK2Node_IfThenElse (Branch node)
     * - UK2Node_VariableGet (Get variable)
     * - UK2Node_VariableSet (Set variable)
     * - UK2Node_CallFunction (Function call)
     * - UK2Node_Event (Event node)
     * - UK2Node_MacroInstance (Macro call)
     * - UK2Node_Composite (Collapsed graph)
     * - Generic UK2Node (fallback for other types)
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutNodes - Output array of node JSON objects with pin connections
     */
    void ExtractGraphNodes(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutNodes);

    /**
     * Extract all pins from a node with connection information
     *
     * For each pin, extracts:
     * - Pin name, category (exec/data), direction (input/output)
     * - Sub-type (e.g., GameplayEventData for struct pins)
     * - Linked connections (node_id + pin_name of connected nodes)
     *
     * @param Node - EdGraphNode to extract pins from
     * @param OutPins - Output array of pin JSON objects
     */
    void ExtractNodePins(UEdGraphNode* Node, TArray<TSharedPtr<FJsonValue>>& OutPins);

    /**
     * Extract all edges (pin-to-pin connections) from Blueprint graphs (Issue #1248)
     *
     * Iterates through all graph nodes and extracts edges by traversing output pins'
     * LinkedTo arrays. Each edge represents a connection from source pin to target pin.
     *
     * For each edge, extracts:
     * - Source node GUID
     * - Target node GUID
     * - Source pin ID
     * - Target pin ID
     *
     * This enables visualization of Blueprint execution flow and data flow connections.
     *
     * @param Blueprint - Blueprint to extract edges from
     * @param OutEdges - Output array of edge JSON objects
     */
    void ExtractGraphEdges(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutEdges);

    /**
     * Extract graph nodes AND edges in a single traversal (Issue #1327 Phase 3 - v5.x.x)
     *
     * Performance optimization: Combines ExtractGraphNodes() + ExtractGraphEdges()
     * into a single graph traversal, reducing processing time by ~50%.
     *
     * Before: 2 separate traversals over all graphs/nodes/pins
     * After: 1 traversal that extracts both nodes and edges simultaneously
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutNodes - Output array of node JSON objects
     * @param OutEdges - Output array of edge JSON objects
     */
    void ExtractGraphNodesAndEdges(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutNodes, TArray<TSharedPtr<FJsonValue>>& OutEdges);

    /**
     * Extract delegate declarations from Blueprint (Issue #107 Phase 1 - v4.7.0)
     *
     * Extracts all FMulticastDelegateProperty declarations from Blueprint's generated class,
     * including signature function parameters, return type, and flags.
     *
     * This achieves 100% coverage for delegate declarations via reflection API.
     *
     * Moved from BlueprintMetadataCommandlet to modular namespace (Issue #108 Phase 0).
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutDelegates - Output array of delegate JSON objects
     */
    void ExtractDelegates(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutDelegates);

    /**
     * Extract delegate binding relationships from Blueprint (Issue #107 Phase 2 - v4.7.0)
     *
     * Extracts all UK2Node_AddDelegate nodes from Blueprint graphs, tracking:
     * - Source delegate (which delegate property is being bound)
     * - Target event (which Blueprint event/function handles the delegate)
     * - Pin connections (delegate pin → event pin)
     * - Owning function context
     *
     * This achieves ~80% coverage for Blueprint delegate bindings.
     *
     * Moved from BlueprintMetadataCommandlet to modular namespace (Issue #108 Phase 0).
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutBindings - Output array of delegate binding JSON objects
     */
    void ExtractDelegateBindings(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutBindings);

    /**
     * Extract delegate broadcast calls from Blueprint (Issue #107 Phase 3 - v4.7.0)
     *
     * Extracts all UK2Node_CallFunction nodes that call __DelegateSignature functions,
     * which represent Broadcast() calls on multicast delegates. Tracks:
     * - Delegate name being broadcast
     * - Call location (graph, owning function)
     * - Parameters passed to broadcast
     *
     * This achieves ~90% coverage for Blueprint delegate broadcasts.
     *
     * Moved from BlueprintMetadataCommandlet to modular namespace (Issue #108 Phase 0).
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutBroadcasts - Output array of delegate broadcast JSON objects
     */
    void ExtractDelegateBroadcasts(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutBroadcasts);

    /**
     * Count async nodes in Blueprint (Issue #335 - v4.20.3)
     *
     * Counts all asynchronous/latent nodes in Blueprint graphs using 3-tier detection:
     * 1. UK2Node_BaseAsyncTask inheritance (99% accuracy) - Delay, AsyncAction, etc.
     * 2. Latent metadata check (95% accuracy) - All Latent-flagged functions
     * 3. AbilityTask type check (90% accuracy, GAS-specific) - WaitGameplayEvent, etc.
     *
     * OR condition: Any node matching any method is counted as async.
     * Avoids false positives from string matching ("Wait" in non-async node names).
     *
     * @param Blueprint - Blueprint to count async nodes from
     * @return Number of async nodes found
     */
    int32 CountAsyncNodes(UBlueprint* Blueprint);

    /**
     * Extract property default value from CDO (Issue #561 Phase 1 - v5.0.0)
     *
     * Extracts default values from Blueprint Class Default Object (CDO) for variable properties.
     * Supports 5 property types:
     * 1. FObjectProperty (UAnimMontage*, UTexture2D*) → Asset path
     * 2. FSoftObjectProperty (FSoftObjectPtr) → Soft reference path
     * 3. FStructProperty (FGameplayTag, FVector, FRotator) → Structured string
     * 4. FNumericProperty (int32, float, bool) → Direct value
     * 5. FStrProperty/FNameProperty (FString, FName) → String value
     *
     * Returns empty string if:
     * - CDO is nullptr
     * - Property is unsupported type
     * - Default value is nullptr/empty
     *
     * @param Property - Property to extract default value from
     * @param CDO - Class Default Object containing the property value
     * @return String representation of default value, or empty string
     */
    FString ExtractPropertyDefaultValue(FProperty* Property, UObject* CDO);

    /**
     * Extract Input Action to Ability bindings from Data Assets (Issue #584 Phase 2A - v6.0.0)
     *
     * Detects 3 architectural patterns across different project structures:
     * 1. VariantExperience Pattern (Last Contact): VEEnhancedInputBindConfig + VEAbilitySet
     *    - InputIdByInputAction: TMap<UInputAction*, int32>
     *    - GrantedGameplayAbilities: TArray<FVEAbilitySet_GameplayAbility> with InputId
     *    - Cross-reference by InputId to create IA → GA mapping
     * 2. Lyra Pattern (Epic Games): ULyraInputConfig
     *    - Direct IA → GA mappings with GameplayTag-based activation
     * 3. Generic Pattern: Any TMap<UInputAction*, X> in Data Assets
     *    - Heuristic detection for common naming conventions
     *
     * Coverage: 60-70% of projects (Data Asset-based architectures)
     * Confidence: 0.9-1.0 (high - static Data Asset properties)
     *
     * @param AssetRegistry - Asset Registry for finding Data Assets
     * @param OutBindings - Output array of binding JSON objects with fields:
     *                      - input_action, input_action_path
     *                      - ability_class, ability_path
     *                      - input_id (optional, for InputId-based patterns)
     *                      - detection_method: "data_asset_static"
     *                      - confidence: 0.9-1.0
     *                      - data_asset_path: Source Data Asset
     */
    void ExtractInputActionBindings(IAssetRegistry& AssetRegistry, TArray<TSharedPtr<FJsonValue>>& OutBindings);

    /**
     * Extract Input Action to Ability bindings from Blueprint graphs (Issue #584 Phase 2B - v6.0.0)
     *
     * Traces execution flow from Enhanced Input event nodes to ActivateAbility calls:
     * 1. Find UK2Node_InputAction and UK2Node_EnhancedInputAction event nodes
     * 2. Trace execution flow through connected nodes (reusing Issue #107 infrastructure)
     * 3. Identify UK2Node_CallFunction nodes calling ActivateAbility/ActivateAbilityFromClass
     * 4. Extract target Ability class from function parameters
     *
     * Coverage: +10-15% of projects (Blueprint-defined bindings)
     * Confidence: 0.7-0.8 (medium-high - execution flow traced)
     *
     * @param Blueprints - Array of Blueprints to analyze
     * @param OutBindings - Output array of binding JSON objects with fields:
     *                      - input_action, input_action_path
     *                      - ability_class, ability_path
     *                      - detection_method: "blueprint_node_trace"
     *                      - confidence: 0.7-0.8
     *                      - blueprint_path: Source Blueprint
     *                      - graph_name: Graph containing the binding
     */
    void ExtractInputActionBindingsFromBlueprints(
        const TArray<UBlueprint*>& Blueprints,
        TArray<TSharedPtr<FJsonValue>>& OutBindings
    );

    /**
     * Validate and deduplicate Input Action to Ability bindings (Issue #584 Phase 2F - v6.0.0)
     *
     * Performs post-extraction validation and cleanup:
     * 1. Asset Verification: Verify InputAction and Ability assets exist in Asset Registry
     * 2. Deduplication: Remove duplicate bindings (InputAction + Ability pair, prefer higher confidence)
     * 3. Statistics: Track validated_count, failed_count, duplicate_count
     *
     * Validation criteria:
     * - InputAction asset must exist at input_action_path
     * - Ability asset must exist at ability_path
     * - Bindings failing verification are marked with "validation_failed": true
     *
     * Deduplication strategy:
     * - Group by (input_action_path, ability_path) pair
     * - If multiple sources find same binding:
     *   → Keep highest confidence (data_asset_static: 0.95 > blueprint_node_trace: 0.75)
     *   → Add "duplicate_sources" array listing all detection methods
     *
     * @param AssetRegistry - Asset Registry for asset existence checks
     * @param InOutBindings - Input/output array of binding JSON objects (modified in-place)
     * @param OutValidationStats - Output JSON object with validation statistics:
     *                            - total_bindings: Initial count before validation
     *                            - validated_bindings: Passed asset verification
     *                            - failed_bindings: Failed asset verification
     *                            - duplicate_bindings: Removed duplicates
     *                            - final_bindings: After validation + deduplication
     */
    void ValidateInputActionBindings(
        IAssetRegistry& AssetRegistry,
        TArray<TSharedPtr<FJsonValue>>& InOutBindings,
        TSharedPtr<FJsonObject>& OutValidationStats
    );

    /**
     * Log Property Type Coverage Statistics (Issue #582 - v5.1.0)
     *
     * Logs statistics about supported vs unsupported property types encountered during
     * default value extraction. Helps prioritize which property types to add in Phase 3.
     *
     * Coverage Formula:
     *   Coverage = (Supported / Total) * 100
     *   Where Total = Supported + Unsupported
     *   Example: 9,108 supported / 9,626 total = 94.6%
     *
     * Outputs:
     * - Supported property types with occurrence counts (FObjectProperty, FSoftObjectProperty, etc.)
     * - Unsupported property types with occurrence counts (FArrayProperty, FMapProperty, etc.)
     * - Coverage percentage (supported / total)
     * - Top 5 unsupported types for Phase 3 prioritization
     *
     * Called at end of commandlet execution when -Profile flag is enabled.
     */
    void LogPropertyTypeCoverage();

    /**
     * Reset Property Type Statistics (Issue #582 - v5.1.0)
     *
     * Resets property type tracking maps to prevent double-counting when commandlet
     * is called multiple times (e.g., in Batch Commandlet mode).
     *
     * Should be called at start of each commandlet run.
     */
    void ResetPropertyTypeStats();

#if WITH_AUTOMATION_TESTS
    /**
     * Test accessor: Get TMap handler count (Issue #1088 - v5.2.0)
     *
     * Returns the number of registered struct handlers in the TMap after initialization.
     * Used by unit tests to verify M-1 TMap refactoring correctness.
     *
     * @return Number of registered struct handlers (expected: 193)
     */
    int32 GetStructHandlerCount();

    /**
     * Test accessor: Check if struct handler exists (Issue #1088 - v5.2.0)
     *
     * Checks if a specific struct type name has a registered handler in the TMap.
     * Used by unit tests to verify Phase 9 Step 1 type coverage.
     *
     * @param StructName - Struct type name to check (e.g., "FilterOptionPerAxis")
     * @return True if handler is registered, false otherwise
     */
    bool HasStructHandler(const FString& StructName);

    /**
     * Test accessor: Trigger struct handler initialization (Issue #1088 - v5.2.0)
     *
     * Calls InitializeStructHandlers() which uses double-checked locking.
     * If already initialized, returns immediately (idempotent).
     * Use TestResetStructHandlers() first if you need to force re-initialization.
     *
     * WARNING: Test-only function. Do not call in production code.
     */
    void TestInitializeStructHandlers();

    /**
     * Test accessor: Reset struct handlers (Issue #1088 - v5.2.0)
     *
     * Clears all struct handlers and resets initialization flag.
     * Used by unit tests to reset state between tests.
     *
     * WARNING: Test-only function. Do not call in production code.
     */
    void TestResetStructHandlers();
#endif // WITH_AUTOMATION_TESTS
}
