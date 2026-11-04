// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class FJsonValue;
struct FAssetData;

#if WITH_EDITOR
class UStateTree;
class UStateTreeState;
class UBehaviorTree;
class UBTNode;
class UBTCompositeNode;
#endif

/**
 * StateTree and BehaviorTree extraction functions
 *
 * Issue #108 Phase 0: Extracted from CUBlueprintMetadataCommandlet.cpp (1938 lines → modular)
 */
namespace AIExtractors
{
#if WITH_EDITOR
    /**
     * Extract StateTree metadata (Issue #27 - v2.9.0+)
     *
     * Extracts complete StateTree structure including:
     * - State hierarchy with parent/child relationships
     * - Tasks, Evaluators, and Conditions
     * - State transitions with triggers and conditions
     * - Linked states and parameters
     *
     * @param StateTree - StateTree asset to extract from
     * @param AssetData - Asset registry data for the StateTree
     * @return JSON object containing StateTree metadata, or nullptr on error
     */
    TSharedPtr<FJsonObject> ExtractStateTreeMetadata(UStateTree* StateTree, const FAssetData& AssetData);

    /**
     * Extract StateTree state recursively
     *
     * @param State - StateTree state to extract
     * @param ParentState - Parent state (nullptr for root states)
     * @param OutStates - Output array of state JSON objects
     */
    void ExtractStateTreeStateRecursive(UStateTreeState* State, UStateTreeState* ParentState, TArray<TSharedPtr<FJsonValue>>& OutStates);

    /**
     * Extract BehaviorTree metadata (Issue #27 Phase 2 - v2.9.0+)
     *
     * Extracts complete BehaviorTree structure including:
     * - Node hierarchy (Composites, Tasks)
     * - Decorators (conditions) per node
     * - Services (background tasks)
     * - Blackboard asset reference
     *
     * @param BehaviorTree - BehaviorTree asset to extract from
     * @param AssetData - Asset registry data for the BehaviorTree
     * @return JSON object containing BehaviorTree metadata, or nullptr on error
     */
    TSharedPtr<FJsonObject> ExtractBehaviorTreeMetadata(UBehaviorTree* BehaviorTree, const FAssetData& AssetData);

    /**
     * Extract BehaviorTree node recursively
     *
     * @param Node - BT node to extract
     * @param Parent - Parent composite node (nullptr for root)
     * @param OutNodes - Output array of node JSON objects
     * @param NodeIndex - Current child index in parent
     */
    void ExtractBTNodeRecursive(UBTNode* Node, UBTCompositeNode* Parent, TArray<TSharedPtr<FJsonValue>>& OutNodes, int32 NodeIndex);

    /**
     * Extract composite node specific data (Issue #108 Phase 0 - Bug Fix)
     *
     * Moved from BlueprintMetadataHelpers to avoid circular dependency.
     * Extracts services and recursively processes children.
     *
     * @param Composite - Composite node to extract
     * @param NodeJson - JSON object to populate
     * @param OutNodes - Output array of node JSON objects
     */
    void ExtractCompositeNodeData(
        UBTCompositeNode* Composite,
        TSharedPtr<FJsonObject>& NodeJson,
        TArray<TSharedPtr<FJsonValue>>& OutNodes
    );
#endif
}
