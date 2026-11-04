// Copyright 2026 Next Stage Inc. All rights reserved.

#include "BlueprintMetadata/AIExtractors.h"
#include "BlueprintMetadata/BlueprintMetadataHelpers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetData.h"

// StateTree includes
#if WITH_EDITOR && WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeEditorNode.h"
#include "StateTreeTypes.h"
#endif

// BehaviorTree includes
#if WITH_EDITOR
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#endif

#include "CULog.h"

namespace AIExtractors
{
    using namespace BlueprintMetadataHelpers;

#if WITH_EDITOR
#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
    // ============================================================================
    // StateTree Support (Issue #27 - v2.9.0+)
    // ============================================================================

    TSharedPtr<FJsonObject> ExtractStateTreeMetadata(UStateTree* StateTree, const FAssetData& AssetData)
    {
        if (!StateTree)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][StateTree] StateTree is null"));
            return nullptr;
        }

        // Access EditorData (only available in Editor builds)
        UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData);
        if (!EditorData)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][StateTree] EditorData is null for: %s"), *StateTree->GetName());
            return nullptr;
        }

        // Basic info (using helper to reduce duplication)
        TSharedPtr<FJsonObject> StateTreeJson = CreateBasicAssetJson(StateTree->GetName(), AssetData);

        // Schema info
        if (EditorData->Schema)
        {
            StateTreeJson->SetStringField(TEXT("schema"), EditorData->Schema->GetClass()->GetName());
        }

        // Extract states using VisitHierarchy (recommended approach)
        TArray<TSharedPtr<FJsonValue>> StatesArray;

        EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* ParentState) {
            TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject);

            // Basic state info
            StateJson->SetStringField(TEXT("name"), State.Name.ToString());
            StateJson->SetStringField(TEXT("id"), State.ID.ToString());
            StateJson->SetStringField(TEXT("parent_id"), ParentState ? ParentState->ID.ToString() : TEXT(""));

            // Log extracted state (for prototype validation)
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree] State: %s (ID: %s)"),
                   *State.Name.ToString(), *State.ID.ToString());

            // Extract Tasks (Day 2-3 expansion)
            TArray<TSharedPtr<FJsonValue>> TasksArray;
            for (const FStateTreeEditorNode& TaskNode : State.Tasks)
            {
                if (TaskNode.Node.IsValid())
                {
                    TSharedPtr<FJsonObject> TaskJson = MakeShareable(new FJsonObject);
                    TaskJson->SetStringField(TEXT("task_type"), TaskNode.Node.GetScriptStruct()->GetName());
                    TaskJson->SetStringField(TEXT("node_name"), TaskNode.Node.GetScriptStruct()->GetFName().ToString());

                    // Extract instance data if available
                    if (TaskNode.Instance.IsValid())
                    {
                        TaskJson->SetStringField(TEXT("instance_type"), TaskNode.Instance.GetScriptStruct()->GetName());
                    }

                    TasksArray.Add(MakeShareable(new FJsonValueObject(TaskJson)));

                    // Log extracted task
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree]   Task: %s"),
                           *TaskNode.Node.GetScriptStruct()->GetName());
                }
            }
            StateJson->SetArrayField(TEXT("tasks"), TasksArray);

            // Extract EnterConditions (Day 2-3 expansion)
            TArray<TSharedPtr<FJsonValue>> EnterConditionsArray;
            for (const FStateTreeEditorNode& CondNode : State.EnterConditions)
            {
                if (CondNode.Node.IsValid())
                {
                    TSharedPtr<FJsonObject> CondJson = MakeShareable(new FJsonObject);
                    CondJson->SetStringField(TEXT("condition_type"), CondNode.Node.GetScriptStruct()->GetName());

                    if (CondNode.Instance.IsValid())
                    {
                        CondJson->SetStringField(TEXT("instance_type"), CondNode.Instance.GetScriptStruct()->GetName());
                    }

                    EnterConditionsArray.Add(MakeShareable(new FJsonValueObject(CondJson)));

                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree]   EnterCondition: %s"),
                           *CondNode.Node.GetScriptStruct()->GetName());
                }
            }
            StateJson->SetArrayField(TEXT("enter_conditions"), EnterConditionsArray);

            // Extract Transitions (Day 2-3 expansion)
            TArray<TSharedPtr<FJsonValue>> TransitionsArray;
            for (const FStateTreeTransition& Transition : State.Transitions)
            {
                TSharedPtr<FJsonObject> TransJson = MakeShareable(new FJsonObject);

                // Transition trigger type (enum to string)
                FString TriggerType = StaticEnum<EStateTreeTransitionTrigger>()->GetNameStringByValue(
                    static_cast<int64>(Transition.Trigger));
                TransJson->SetStringField(TEXT("trigger"), TriggerType);

                // Target state ID
                if (Transition.State.ID.IsValid())
                {
                    TransJson->SetStringField(TEXT("target_state_id"), Transition.State.ID.ToString());
                    TransJson->SetStringField(TEXT("target_state_name"), Transition.State.Name.ToString());
                }

                // Transition priority
                TransJson->SetNumberField(TEXT("priority"), static_cast<int32>(Transition.Priority));

                // Transition conditions
                TArray<TSharedPtr<FJsonValue>> ConditionsArray;
                for (const FStateTreeEditorNode& CondNode : Transition.Conditions)
                {
                    if (CondNode.Node.IsValid())
                    {
                        TSharedPtr<FJsonObject> CondJson = MakeShareable(new FJsonObject);
                        CondJson->SetStringField(TEXT("condition_type"), CondNode.Node.GetScriptStruct()->GetName());

                        if (CondNode.Instance.IsValid())
                        {
                            CondJson->SetStringField(TEXT("instance_type"), CondNode.Instance.GetScriptStruct()->GetName());
                        }

                        ConditionsArray.Add(MakeShareable(new FJsonValueObject(CondJson)));
                    }
                }
                TransJson->SetArrayField(TEXT("conditions"), ConditionsArray);

                TransitionsArray.Add(MakeShareable(new FJsonValueObject(TransJson)));

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree]   Transition: %s → %s (Priority: %d)"),
                       *TriggerType, *Transition.State.Name.ToString(), static_cast<int32>(Transition.Priority));
            }
            StateJson->SetArrayField(TEXT("transitions"), TransitionsArray);

            StatesArray.Add(MakeShareable(new FJsonValueObject(StateJson)));

            return EStateTreeVisitor::Continue;
        });

        StateTreeJson->SetArrayField(TEXT("states"), StatesArray);
        StateTreeJson->SetNumberField(TEXT("state_count"), StatesArray.Num());

        // Extract Global Evaluators (Day 2-3 expansion)
        TArray<TSharedPtr<FJsonValue>> EvaluatorsArray;
        for (const FStateTreeEditorNode& EvalNode : EditorData->Evaluators)
        {
            if (EvalNode.Node.IsValid())
            {
                TSharedPtr<FJsonObject> EvalJson = MakeShareable(new FJsonObject);
                EvalJson->SetStringField(TEXT("evaluator_type"), EvalNode.Node.GetScriptStruct()->GetName());

                if (EvalNode.Instance.IsValid())
                {
                    EvalJson->SetStringField(TEXT("instance_type"), EvalNode.Instance.GetScriptStruct()->GetName());
                }

                EvaluatorsArray.Add(MakeShareable(new FJsonValueObject(EvalJson)));

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree] GlobalEvaluator: %s"),
                       *EvalNode.Node.GetScriptStruct()->GetName());
            }
        }
        StateTreeJson->SetArrayField(TEXT("global_evaluators"), EvaluatorsArray);
        StateTreeJson->SetNumberField(TEXT("global_evaluators_count"), EvaluatorsArray.Num());

        // Extract Global Tasks (Day 2-3 expansion)
        TArray<TSharedPtr<FJsonValue>> GlobalTasksArray;
        for (const FStateTreeEditorNode& TaskNode : EditorData->GlobalTasks)
        {
            if (TaskNode.Node.IsValid())
            {
                TSharedPtr<FJsonObject> TaskJson = MakeShareable(new FJsonObject);
                TaskJson->SetStringField(TEXT("task_type"), TaskNode.Node.GetScriptStruct()->GetName());

                if (TaskNode.Instance.IsValid())
                {
                    TaskJson->SetStringField(TEXT("instance_type"), TaskNode.Instance.GetScriptStruct()->GetName());
                }

                GlobalTasksArray.Add(MakeShareable(new FJsonValueObject(TaskJson)));

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree] GlobalTask: %s"),
                       *TaskNode.Node.GetScriptStruct()->GetName());
            }
        }
        StateTreeJson->SetArrayField(TEXT("global_tasks"), GlobalTasksArray);
        StateTreeJson->SetNumberField(TEXT("global_tasks_count"), GlobalTasksArray.Num());

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree] Successfully extracted from: %s"),
               *StateTree->GetName());
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][StateTree]   States: %d, Evaluators: %d, GlobalTasks: %d"),
               StatesArray.Num(), EvaluatorsArray.Num(), GlobalTasksArray.Num());

        return StateTreeJson;
    }

    void ExtractStateTreeStateRecursive(UStateTreeState* State, UStateTreeState* ParentState, TArray<TSharedPtr<FJsonValue>>& OutStates)
    {
        // Alternative manual recursion approach (not used in prototype)
        // Kept for reference - can be used if VisitHierarchy has issues

        if (!State)
        {
            return;
        }

        TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject);

        // Extract state data (same as VisitHierarchy approach)
        StateJson->SetStringField(TEXT("name"), State->Name.ToString());
        StateJson->SetStringField(TEXT("id"), State->ID.ToString());
        StateJson->SetStringField(TEXT("parent_id"), ParentState ? ParentState->ID.ToString() : TEXT(""));

        OutStates.Add(MakeShareable(new FJsonValueObject(StateJson)));

        // Recurse to children
        for (UStateTreeState* Child : State->Children)
        {
            ExtractStateTreeStateRecursive(Child, State, OutStates);
        }
    }

    // ============================================================================
    // BehaviorTree Support (Issue #27 Phase 2 - v2.9.0+)
    // ============================================================================
#else
    TSharedPtr<FJsonObject> ExtractStateTreeMetadata(UStateTree* StateTree, const FAssetData& AssetData)
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error_code"), TEXT("state_tree_plugin_unavailable"));
        Result->SetStringField(TEXT("error"), TEXT("StateTree metadata extraction requires the optional StateTree and PropertyBindingUtils plugins"));
        Result->SetStringField(TEXT("hint"), TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild."));
        return Result;
    }

    void ExtractStateTreeStateRecursive(UStateTreeState* State, UStateTreeState* ParentState, TArray<TSharedPtr<FJsonValue>>& OutStates)
    {
    }
#endif

    // ============================================================================
    // BehaviorTree Support (Issue #27 Phase 2 - v2.9.0+)
    // ============================================================================

    TSharedPtr<FJsonObject> ExtractBehaviorTreeMetadata(UBehaviorTree* BehaviorTree, const FAssetData& AssetData)
    {
        if (!BehaviorTree)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][BehaviorTree] BehaviorTree is null"));
            return nullptr;
        }

        // Basic info (using helper to reduce duplication)
        TSharedPtr<FJsonObject> BTJson = CreateBasicAssetJson(BehaviorTree->GetName(), AssetData);

        // Blackboard info
        if (BehaviorTree->BlackboardAsset)
        {
            BTJson->SetStringField(TEXT("blackboard"), BehaviorTree->BlackboardAsset->GetName());
            BTJson->SetStringField(TEXT("blackboard_path"), BehaviorTree->BlackboardAsset->GetPathName());
        }

        // Root node info
        if (BehaviorTree->RootNode)
        {
            BTJson->SetStringField(TEXT("root_node_class"), BehaviorTree->RootNode->GetClass()->GetName());
        }

        // Extract tree structure
        TArray<TSharedPtr<FJsonValue>> NodesArray;
        if (BehaviorTree->RootNode)
        {
            ExtractBTNodeRecursive(BehaviorTree->RootNode, nullptr, NodesArray, 0);
        }

        BTJson->SetArrayField(TEXT("nodes"), NodesArray);
        BTJson->SetNumberField(TEXT("node_count"), NodesArray.Num());

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree] Successfully extracted from: %s"),
               *BehaviorTree->GetName());
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree]   Nodes: %d, Blackboard: %s"),
               NodesArray.Num(), BehaviorTree->BlackboardAsset ? *BehaviorTree->BlackboardAsset->GetName() : TEXT("None"));

        return BTJson;
    }

    void ExtractCompositeNodeData(
        UBTCompositeNode* Composite,
        TSharedPtr<FJsonObject>& NodeJson,
        TArray<TSharedPtr<FJsonValue>>& OutNodes
    )
    {
        // Extract composite-specific data
        int32 ChildCount = Composite->GetChildrenNum();
        NodeJson->SetNumberField(TEXT("child_count"), ChildCount);

        // Extract services attached to this composite
        TArray<TSharedPtr<FJsonValue>> ServicesArray;
        for (UBTService* Service : Composite->Services)
        {
            if (Service)
            {
                TSharedPtr<FJsonObject> ServiceJson = MakeShareable(new FJsonObject);
                ServiceJson->SetStringField(TEXT("service_name"), Service->GetNodeName());
                ServiceJson->SetStringField(TEXT("service_class"), Service->GetClass()->GetName());

                ServicesArray.Add(MakeShareable(new FJsonValueObject(ServiceJson)));

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree]   Service: %s (on %s)"),
                       *Service->GetNodeName(), *Composite->GetNodeName());
            }
        }
        if (ServicesArray.Num() > 0)
        {
            NodeJson->SetArrayField(TEXT("services"), ServicesArray);
        }

        // Recurse through children (CRITICAL: Restored recursive call - Issue #108 Phase 0 Bug Fix)
        for (int32 i = 0; i < ChildCount; i++)
        {
            FBTCompositeChild& ChildInfo = Composite->Children[i];
            // Bug Fix: Check both ChildComposite and ChildTask (UE 5.6 compatibility)
            UBTNode* ChildNode = ChildInfo.ChildComposite.Get()
                ? Cast<UBTNode>(ChildInfo.ChildComposite.Get())
                : Cast<UBTNode>(ChildInfo.ChildTask.Get());

            if (ChildNode)
            {
                // Extract decorators for this child
                TArray<TSharedPtr<FJsonValue>> DecoratorsArray;
                for (UBTDecorator* Decorator : ChildInfo.Decorators)
                {
                    if (Decorator)
                    {
                        TSharedPtr<FJsonObject> DecoratorJson = MakeShareable(new FJsonObject);
                        DecoratorJson->SetStringField(TEXT("decorator_name"), Decorator->GetNodeName());
                        DecoratorJson->SetStringField(TEXT("decorator_class"), Decorator->GetClass()->GetName());

                        DecoratorsArray.Add(MakeShareable(new FJsonValueObject(DecoratorJson)));

                        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree]   Decorator: %s (on child %d)"),
                               *Decorator->GetNodeName(), i);
                    }
                }

                // CRITICAL: Recursive call to extract child nodes (Bug Fix - Issue #108 Phase 0)
                ExtractBTNodeRecursive(ChildNode, Composite, OutNodes, i);
            }
        }
    }

    void ExtractBTNodeRecursive(UBTNode* Node, UBTCompositeNode* Parent, TArray<TSharedPtr<FJsonValue>>& OutNodes, int32 NodeIndex)
    {
        if (!Node)
        {
            return;
        }

        TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);

        // Common node info
        NodeJson->SetStringField(TEXT("node_name"), Node->GetNodeName());
        NodeJson->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
        NodeJson->SetNumberField(TEXT("node_index"), NodeIndex);

        if (Parent)
        {
            NodeJson->SetStringField(TEXT("parent_node"), Parent->GetNodeName());
            NodeJson->SetNumberField(TEXT("depth"), Parent->GetTreeDepth() + 1);
        }
        else
        {
            NodeJson->SetNumberField(TEXT("depth"), 0);
        }

        // Determine node type
        FString NodeType = TEXT("unknown");
        if (UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node))
        {
            NodeType = TEXT("composite");

            // Extract composite-specific data with recursion (v4.7.0 - Bug Fix)
            ExtractCompositeNodeData(Composite, NodeJson, OutNodes);
        }
        else if (UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
        {
            NodeType = TEXT("task");

            // Extract task-specific data
            // Note: IsTaskExecutionBlocking() is not available in all UE versions
            // We can check bNotifyTick or other properties if needed

            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree] Task: %s (depth: %.0f)"),
                   *Task->GetNodeName(), NodeJson->GetNumberField(TEXT("depth")));
        }
        else if (UBTDecorator* Decorator = Cast<UBTDecorator>(Node))
        {
            NodeType = TEXT("decorator");

            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree] Decorator: %s"),
                   *Decorator->GetNodeName());
        }
        else if (UBTService* Service = Cast<UBTService>(Node))
        {
            NodeType = TEXT("service");

            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][BehaviorTree] Service: %s"),
                   *Service->GetNodeName());
        }

        NodeJson->SetStringField(TEXT("node_type"), NodeType);

        OutNodes.Add(MakeShareable(new FJsonValueObject(NodeJson)));
    }
#endif // WITH_EDITOR
}
