// Copyright 2026 Next Stage Inc. All rights reserved.

#include "BlueprintMetadata/BlueprintMetadataHelpers.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"  // Issue #81: self-check entry-event nodes
#include "K2Node_ComponentBoundEvent.h"  // Issue #115: delegate handler name lives in CustomFunctionName, not EventReference
#include "Containers/Queue.h"           // Issue #115: TQueue for Case A BFS
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Blueprint.h"  // Issue #1339: For FBlueprintTags
#include "Misc/SecureHash.h"   // Issue #1339: For FMD5::HashAnsiString

// K2Node includes for EnrichNodeByType
#include "K2Node_AsyncAction.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_Composite.h"
#include "EdGraphSchema_K2.h"

#include "CULog.h"

// BehaviorTree includes
#if WITH_EDITOR
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"  // For Cast<UBTNode>
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#endif

namespace BlueprintMetadataHelpers
{
    // Issue #1339: Constant for generated class suffix (L-1: avoid magic string duplication)
    static const TCHAR* GENERATED_CLASS_SUFFIX = TEXT("_C");

    // Forward decl — FindOwningFunction and FindDownstreamConsumerOwner are
    // mutually recursive (Issue #115 Case A walk-down resolves a consumer's
    // owning_function by calling FindOwningFunction(Consumer)).
    FString FindOwningFunction(UEdGraphNode* Node);

    // Issue #81 (Option D) + Issue #115 (Case B): Walk upstream along the first
    // exec input pin chain to find the entry-event that owns a body node
    // (CallFunction, VariableGet, operator, etc.). Returns the event's name,
    // or empty if the chain dead-ends.
    //
    // Why walk-up: on EventGraph, body nodes are downstream of exactly one
    // entry-event in typical Blueprint authoring. Control-flow forks
    // (Branch / Sequence / IfThenElse) themselves have a single exec input, so
    // following LinkedTo[0] still reaches the originating event through them.
    // Multi-event join nodes are rare and resolved deterministically via the
    // first link. Cycles (possible through macro expansions) are guarded by the
    // Visited set.
    //
    // Issue #115 Case B: UK2Node_ComponentBoundEvent IS-A UK2Node_Event but its
    // EventReference returns the delegate SIGNATURE class name (e.g.
    // "LyraHealth_DeathEvent__DelegateSignature") rather than the handler
    // function name (e.g. "OnDeathStarted"). The handler name lives in the
    // separate CustomFunctionName field — same storage UK2Node_CustomEvent uses.
    // So ComponentBoundEvent must be checked BEFORE the generic UK2Node_Event
    // branch, otherwise EventReference would shadow it.
    static FString FindUpstreamEventName(UEdGraphNode* Start)
    {
        TSet<UEdGraphNode*> Visited;
        UEdGraphNode* Current = Start;
        while (Current && !Visited.Contains(Current))
        {
            Visited.Add(Current);

            // Entry-event reached: return its function name. CustomEvent first
            // because UK2Node_CustomEvent IS-A UK2Node_Event.
            if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Current))
            {
                const FName Name = CustomEvent->CustomFunctionName;
                if (!Name.IsNone())
                {
                    return Name.ToString();
                }
            }
            // Issue #115 Case B: ComponentBoundEvent — handler name in DelegatePropertyName.
            // MUST precede the generic UK2Node_Event branch (IS-A relationship).
            // NOTE: CustomFunctionName here is the engine-mangled bind name
            // (e.g. "BndEvt__HealthComponent_K2Node_ComponentBoundEvent_0_LyraHealth_DeathEvent__DelegateSignature"),
            // populated by InitializeComponentBoundEventParams. The user-facing
            // handler name (e.g. "OnDeathStarted") lives in DelegatePropertyName
            // — confirmed against Engine/Source/Editor/BlueprintGraph/Private/K2Node_ComponentBoundEvent.cpp:103
            // (DelegatePropertyName = InDelegateProperty->GetFName()).
            if (UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Current))
            {
                const FName Name = BoundEvent->DelegatePropertyName;
                if (!Name.IsNone())
                {
                    return Name.ToString();
                }
            }
            if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Current))
            {
                const FName Name = EventNode->EventReference.GetMemberName();
                if (!Name.IsNone())
                {
                    return Name.ToString();
                }
            }

            // Step upstream along the first exec input link.
            UEdGraphNode* Next = nullptr;
            for (UEdGraphPin* Pin : Current->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Input &&
                    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
                    Pin->LinkedTo.Num() > 0)
                {
                    if (UEdGraphPin* Linked = Pin->LinkedTo[0])
                    {
                        Next = Linked->GetOwningNode();
                        break;
                    }
                }
            }
            Current = Next;
        }
        return FString();
    }

    // Issue #115 Case A: pure data nodes (K2Node_VariableGet, K2Node_GetArrayItem,
    // Random Float in Range, Make Transform, ...) have no exec input pin, so the
    // walk-up dead-ends immediately and the caller would fall through to the
    // graph-name fallback ("EventGraph"). Walk DOWNSTREAM along data output
    // links to find an impure consumer and adopt its owning_function.
    //
    // Multi-consumer policy: PinId-sorted output pins + PinId-sorted LinkedTo
    // entries for determinism. The first consumer whose owning_function
    // resolves to a non-graph-name, non-"Unknown" value wins (earlier consumers
    // that themselves dead-end to the graph-name fallback are skipped, not
    // "won by"). Cycles within a single BFS frame are guarded by Visited.
    // Mutual recursion with FindOwningFunction is bounded for the typical case
    // (acyclic data graph):
    //   - Impure consumer: walk-up succeeds, returns immediately (no re-entry).
    //   - Pure consumer: re-enters walk-down with its own Visited frame, but
    //     node count is finite per graph so the outer BFS still terminates.
    // Caveat: a pure-data cycle (e.g. macro-expansion artifact) could in theory
    // trigger stack growth across mutual recursion frames since each frame has
    // its own Visited set. UE's BPVM compiler typically rejects such cycles
    // before this code runs; if a hardened depth/global-visited guard becomes
    // necessary, see Issue #115 review notes (M-1).
    static FString FindDownstreamConsumerOwner(UEdGraphNode* Start)
    {
        if (!Start)
        {
            return FString();
        }

        UEdGraph* StartGraph = Start->GetGraph();
        const FString StartGraphName = StartGraph ? StartGraph->GetName() : FString();

        TSet<UEdGraphNode*> Visited;
        Visited.Add(Start);

        TQueue<UEdGraphNode*> Queue;
        Queue.Enqueue(Start);

        while (!Queue.IsEmpty())
        {
            UEdGraphNode* Current = nullptr;
            Queue.Dequeue(Current);
            if (!Current)
            {
                continue;
            }

            // Sort data output pins by PinId for deterministic traversal.
            TArray<UEdGraphPin*> OutPins;
            for (UEdGraphPin* Pin : Current->Pins)
            {
                if (Pin && Pin->Direction == EGPD_Output &&
                    Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
                {
                    OutPins.Add(Pin);
                }
            }
            OutPins.Sort([](const UEdGraphPin& A, const UEdGraphPin& B) {
                return A.PinId.ToString() < B.PinId.ToString();
            });

            for (UEdGraphPin* Pin : OutPins)
            {
                // Sort LinkedTo by PinId for deterministic first-consumer selection.
                TArray<UEdGraphPin*> LinkedSorted = Pin->LinkedTo;
                LinkedSorted.Sort([](const UEdGraphPin& A, const UEdGraphPin& B) {
                    return A.PinId.ToString() < B.PinId.ToString();
                });

                for (UEdGraphPin* Linked : LinkedSorted)
                {
                    if (!Linked)
                    {
                        continue;
                    }
                    UEdGraphNode* Consumer = Linked->GetOwningNode();
                    if (!Consumer || Visited.Contains(Consumer))
                    {
                        continue;
                    }
                    // Pre-recursion Visited.Add is load-bearing: prevents diamond
                    // patterns (Start -> {X,Y} -> Merge) from re-processing Merge
                    // twice (once via X's recursion, once via Y's outer enqueue).
                    Visited.Add(Consumer);

                    // Resolve consumer's owning_function via the full pipeline.
                    // Empty string / graph-name fallback both mean "not confidently
                    // attributed" — try the next consumer in deterministic order.
                    const FString Resolved = FindOwningFunction(Consumer);
                    if (!Resolved.IsEmpty() && Resolved != StartGraphName && Resolved != TEXT("Unknown"))
                    {
                        return Resolved;
                    }

                    // Inconclusive consumer: keep walking through it (e.g. chain
                    // of pure helpers feeding into another pure helper).
                    Queue.Enqueue(Consumer);
                }
            }
        }
        return FString();
    }

    FString FindOwningFunction(UEdGraphNode* Node)
    {
        if (!Node)
        {
            return TEXT("Unknown");
        }

        // Issue #81: Self-check for entry-event nodes.
        // K2Node_CustomEvent inherits from K2Node_Event (UE5 hierarchy),
        // so order matters: CustomEvent first, then generic Event fallback.
        // Without this, EventGraph entry-events were collapsed to the graph's
        // first K2Node_Event signature (e.g., all 6 events in B_Grenade.uasset
        // returned "ReceiveBeginPlay" regardless of their actual definition).
        if (UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node))
        {
            const FName CustomName = CustomEvent->CustomFunctionName;
            if (!CustomName.IsNone())
            {
                return CustomName.ToString();
            }
        }
        // Issue #115 Case B: ComponentBoundEvent (e.g. HealthComponent.OnDeathStarted
        // handler). IS-A UK2Node_Event but EventReference returns the delegate
        // signature class name (e.g. "LyraHealth_DeathEvent__DelegateSignature").
        // The user-facing handler name (e.g. "OnDeathStarted") lives in
        // DelegatePropertyName — confirmed against
        // Engine/Source/Editor/BlueprintGraph/Private/K2Node_ComponentBoundEvent.cpp:103
        // (DelegatePropertyName = InDelegateProperty->GetFName()).
        // CustomFunctionName here is the engine-mangled bind name (e.g.
        // "BndEvt__HealthComponent_K2Node_ComponentBoundEvent_0_..."), useful
        // for runtime delegate binding but NOT for owning_function attribution.
        // MUST precede the generic UK2Node_Event branch below.
        if (UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Node))
        {
            const FName Name = BoundEvent->DelegatePropertyName;
            if (!Name.IsNone())
            {
                return Name.ToString();
            }
        }
        if (UK2Node_Event* SelfEvent = Cast<UK2Node_Event>(Node))
        {
            const FName EventName = SelfEvent->EventReference.GetMemberName();
            if (!EventName.IsNone())
            {
                return EventName.ToString();
            }
        }

        UEdGraph* Graph = Node->GetGraph();
        if (!Graph)
        {
            return TEXT("Unknown");
        }

        // Search for FunctionEntry node in the same graph
        for (UEdGraphNode* GraphNode : Graph->Nodes)
        {
            if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(GraphNode))
            {
                // Get function name from FunctionReference
                FName FunctionName = EntryNode->FunctionReference.GetMemberName();
                if (FunctionName.IsValid() && !FunctionName.IsNone())
                {
                    return FunctionName.ToString();
                }

                // Fallback: Use generated class function name
                if (EntryNode->FindSignatureFunction())
                {
                    return EntryNode->FindSignatureFunction()->GetName();
                }
            }
        }

        // Issue #81 (Option D): No FunctionEntry in this graph — try to attribute
        // this body node to an upstream entry-event by walking its exec input
        // chain. Keeps owning_function semantically meaningful (e.g., a node
        // downstream of Event BeginPlay reports "ReceiveBeginPlay" rather than
        // "EventGraph"). Entry-events themselves are handled by the self-check
        // above and never reach here.
        const FString UpstreamEvent = FindUpstreamEventName(Node);
        if (!UpstreamEvent.IsEmpty())
        {
            return UpstreamEvent;
        }

        // Issue #115 Case A: pure data node (no exec input pin) — walk-up
        // dead-ended above. Walk DOWNSTREAM along data output links to find an
        // impure consumer and adopt its owning_function. Multi-consumer
        // resolution is deterministic (PinId-sorted first consumer wins).
        const FString DownstreamOwner = FindDownstreamConsumerOwner(Node);
        if (!DownstreamOwner.IsEmpty())
        {
            return DownstreamOwner;
        }

        // Final fallback: graph name. Reached only when the body node has no
        // exec chain back to any entry-event AND no resolvable downstream
        // consumer (disconnected nodes, delegate wiring stubs, etc.).
        return Graph->GetName();
    }

    bool IsRelevantGraphForExtraction(UEdGraph* Graph)
    {
        if (!Graph)
        {
            return false;
        }

        const UEdGraphSchema* Schema = Graph->GetSchema();
        if (!Schema)
        {
            return false;  // No schema = invalid graph
        }

        // Issue #227 Phase 3-B: Blacklist approach - exclude only specific graph types
        // Previous Whitelist approach missed "Gameplay Ability Graph" (GameplayAbility Blueprints)
        // New approach: Include all graph types except Macros and Animation graphs
        EGraphType GraphType = Schema->GetGraphType(Graph);

        // Exclude Macro and Animation graphs (not relevant for execution flow)
        if (GraphType == GT_Macro || GraphType == GT_Animation)
        {
            return false;
        }

        // Include everything else:
        // - GT_Function (function graphs)
        // - GT_Ubergraph (EventGraph, Gameplay Ability Graph, etc.)
        // - GT_StateMachine (state machines)
        // - Future graph types (automatic)
        return true;
    }

    TSharedPtr<FJsonObject> CreateBasicAssetJson(const FString& AssetName, const FAssetData& AssetData)
    {
        TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);

        Json->SetStringField(TEXT("name"), AssetName);
        Json->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        Json->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

        return Json;
    }

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
    )
    {
        TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

        // Metadata
        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue #561] Generating Blueprint Metadata v5.0.0 with Variable Default Values"));
        RootJson->SetNumberField(TEXT("version"), 5);  // v5 with variable default values (Issue #561)
        RootJson->SetStringField(TEXT("generator"), TEXT("NarshaMCP::BlueprintMetadataCommandlet v5.0.0 (Issue #561)"));
        RootJson->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToString());

        // Blueprint counts
        RootJson->SetNumberField(TEXT("total_blueprints"), ProcessedCount);
        RootJson->SetNumberField(TEXT("total_anim_blueprints"), AnimBPProcessedCount);
        RootJson->SetNumberField(TEXT("total_anim_assets"), AnimAssetProcessedCount);
        RootJson->SetNumberField(TEXT("processed"), ProcessedCount);
        RootJson->SetNumberField(TEXT("errors"), ErrorCount);
        RootJson->SetNumberField(TEXT("anim_bp_processed"), AnimBPProcessedCount);
        RootJson->SetNumberField(TEXT("anim_bp_errors"), AnimBPErrorCount);
        RootJson->SetNumberField(TEXT("anim_asset_processed"), AnimAssetProcessedCount);
        RootJson->SetNumberField(TEXT("anim_asset_errors"), AnimAssetErrorCount);

#if WITH_EDITOR
        // StateTree/BehaviorTree/Input counts
        RootJson->SetNumberField(TEXT("total_statetrees"), StateTreeProcessedCount);
        RootJson->SetNumberField(TEXT("statetree_processed"), StateTreeProcessedCount);
        RootJson->SetNumberField(TEXT("statetree_errors"), StateTreeErrorCount);
        RootJson->SetNumberField(TEXT("total_behaviortrees"), BTProcessedCount);
        RootJson->SetNumberField(TEXT("behaviortree_processed"), BTProcessedCount);
        RootJson->SetNumberField(TEXT("behaviortree_errors"), BTErrorCount);
        RootJson->SetNumberField(TEXT("total_inputactions"), InputActionProcessedCount);
        RootJson->SetNumberField(TEXT("inputaction_processed"), InputActionProcessedCount);
        RootJson->SetNumberField(TEXT("inputaction_errors"), InputActionErrorCount);
        RootJson->SetNumberField(TEXT("total_inputmappings"), InputMappingProcessedCount);
        RootJson->SetNumberField(TEXT("inputmapping_processed"), InputMappingProcessedCount);
        RootJson->SetNumberField(TEXT("inputmapping_errors"), InputMappingErrorCount);
#endif

        // Arrays
        RootJson->SetArrayField(TEXT("blueprints"), BlueprintsArray);
        RootJson->SetArrayField(TEXT("anim_blueprints"), AnimBPsArray);
        RootJson->SetArrayField(TEXT("anim_assets"), AnimAssetsArray);

#if WITH_EDITOR
        RootJson->SetArrayField(TEXT("statetrees"), StateTreesArray);
        RootJson->SetArrayField(TEXT("behaviortrees"), BehaviorTreesArray);
        RootJson->SetArrayField(TEXT("input_actions"), InputActionsArray);
        RootJson->SetArrayField(TEXT("input_mappings"), InputMappingsArray);
#endif

        return RootJson;
    }

    FString EnrichNodeByType(UEdGraphNode* Node, TSharedPtr<FJsonObject>& NodeJson)
    {
        FString NodeType = TEXT("k2node");  // Default

        // Type-specific enrichment (only for Priority 1 nodes)
        if (Cast<UK2Node_AsyncAction>(Node))
        {
            NodeType = TEXT("async_action");
        }
        else if (UK2Node_ExecutionSequence* SeqNode = Cast<UK2Node_ExecutionSequence>(Node))
        {
            NodeType = TEXT("sequence");

            // Count Then pins
            int32 ThenPinCount = 0;
            for (UEdGraphPin* Pin : SeqNode->Pins)
            {
                if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                {
                    ThenPinCount++;
                }
            }
            NodeJson->SetNumberField(TEXT("then_pin_count"), ThenPinCount);
        }
        else if (Cast<UK2Node_IfThenElse>(Node))
        {
            NodeType = TEXT("branch");
        }
        else if (UK2Node_VariableGet* VarGetNode = Cast<UK2Node_VariableGet>(Node))
        {
            NodeType = TEXT("variable_get");
            NodeJson->SetStringField(TEXT("variable_name"), VarGetNode->GetVarName().ToString());
        }
        else if (UK2Node_VariableSet* VarSetNode = Cast<UK2Node_VariableSet>(Node))
        {
            NodeType = TEXT("variable_set");
            NodeJson->SetStringField(TEXT("variable_name"), VarSetNode->GetVarName().ToString());
        }
        else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
        {
            NodeType = TEXT("call_function");

            if (UFunction* TargetFunc = CallNode->GetTargetFunction())
            {
                NodeJson->SetStringField(TEXT("target_function"), TargetFunc->GetName());
                NodeJson->SetStringField(TEXT("target_function_full_name"), TargetFunc->GetPathName());
            }
            // Issue #135 Phase 4: Reflection-based Calls Extraction
            // Detect dynamic function calls (e.g., CallFunctionByName, ProcessEvent)
            else if (CallNode->FunctionReference.GetMemberName() != NAME_None)
            {
                FString FunctionName = CallNode->FunctionReference.GetMemberName().ToString();

                NodeJson->SetBoolField(TEXT("is_reflection_call"), true);
                NodeJson->SetStringField(TEXT("reflection_function_name"), FunctionName);

                // Check if this is a known UE reflection API
                TArray<FString> KnownReflectionAPIs = {
                    TEXT("CallFunctionByName"),
                    TEXT("ExecuteUbergraph"),
                    TEXT("ProcessEvent"),
                    TEXT("CallUFunction")
                };

                if (KnownReflectionAPIs.Contains(FunctionName))
                {
                    NodeJson->SetStringField(TEXT("reflection_api_type"), FunctionName);
                }
            }
        }
        else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
        {
            NodeType = TEXT("event");

            if (UFunction* EventFunc = EventNode->FindEventSignatureFunction())
            {
                NodeJson->SetStringField(TEXT("event_function"), EventFunc->GetName());
            }
        }
        else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
        {
            NodeType = TEXT("macro");

            if (MacroNode && MacroNode->GetMacroGraph())
            {
                NodeJson->SetStringField(TEXT("macro_name"), MacroNode->GetMacroGraph()->GetName());
            }
        }
        else if (Cast<UK2Node_Composite>(Node))
        {
            NodeType = TEXT("composite");
        }

        return NodeType;
    }

// Note: ExtractCompositeNodeData() moved to AIExtractors.cpp (Issue #108 Phase 0 - Bug Fix)
// This avoids circular dependency issues (Helpers → AIExtractors → Helpers)

    TSharedPtr<FJsonObject> CreateMinimalBlueprintEntry(
        const FAssetData& AssetData,
        const FString& HashSuffix,
        bool bHighCostSkipped,
        const FString& PreExtractedParentClass,
        const FString& PreExtractedNativeParentClass
    )
    {
        TSharedPtr<FJsonObject> BPJson = MakeShareable(new FJsonObject);

        // ========== Basic Fields ==========
        FString AssetName = AssetData.AssetName.ToString();
        FString ObjectPath = AssetData.GetObjectPathString();  // Cache for reuse in hash generation
        BPJson->SetStringField(TEXT("name"), AssetName);
        BPJson->SetStringField(TEXT("path"), ObjectPath);
        BPJson->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

        // ========== Class Name Extraction ==========
        FString GeneratedClassPath;
        if (AssetData.GetTagValue(FBlueprintTags::GeneratedClassPath, GeneratedClassPath))
        {
            // GeneratedClassPath format: "Class'/Game/BP_Name.BP_Name_C'"
            FString GeneratedClassName;
            if (GeneratedClassPath.Split(TEXT("."), nullptr, &GeneratedClassName,
                                          ESearchCase::IgnoreCase, ESearchDir::FromEnd))
            {
                GeneratedClassName.RemoveFromEnd(TEXT("'"));
                BPJson->SetStringField(TEXT("class_name"), GeneratedClassName);
            }
            else
            {
                BPJson->SetStringField(TEXT("class_name"), AssetName + GENERATED_CLASS_SUFFIX);
            }
        }
        else
        {
            BPJson->SetStringField(TEXT("class_name"), AssetName + GENERATED_CLASS_SUFFIX);
        }

        // ========== Parent Class Extraction ==========
        FString ParentClassName;
        if (!PreExtractedParentClass.IsEmpty())
        {
            // Use pre-extracted value (DirectChild case)
            ParentClassName = PreExtractedParentClass;
        }
        else
        {
            FString ParentClassPath;
            if (AssetData.GetTagValue(FBlueprintTags::ParentClassPath, ParentClassPath))
            {
                if (ParentClassPath.Split(TEXT("."), nullptr, &ParentClassName,
                                           ESearchCase::IgnoreCase, ESearchDir::FromEnd))
                {
                    ParentClassName.RemoveFromEnd(TEXT("'"));
                }
                else
                {
                    ParentClassPath.RemoveFromEnd(TEXT("'"));
                    ParentClassName = ParentClassPath;
                }
            }
            else
            {
                ParentClassName = TEXT("Unknown");
            }
        }
        BPJson->SetStringField(TEXT("parent_class"), ParentClassName);

        // ========== Native Parent Class Extraction ==========
        FString NativeParentClassName;
        if (!PreExtractedNativeParentClass.IsEmpty())
        {
            // Use pre-extracted value (DirectChild case)
            NativeParentClassName = PreExtractedNativeParentClass;
        }
        else
        {
            FString NativeParentClassPath;
            if (AssetData.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParentClassPath))
            {
                if (NativeParentClassPath.Split(TEXT("."), nullptr, &NativeParentClassName,
                                                ESearchCase::IgnoreCase, ESearchDir::FromEnd))
                {
                    NativeParentClassName.RemoveFromEnd(TEXT("'"));
                }
                else
                {
                    // M-2: Intentional fallback - NativeParentClassPath has no '.' separator
                    // This is rare but valid (e.g., native classes without package paths)
                    // Fall back to ParentClassName which is already validated
                    NativeParentClassName = ParentClassName;
                }
            }
            else
            {
                // M-2: Intentional fallback - NativeParentClassPath tag not present
                // Common for BPs that directly inherit from native classes
                NativeParentClassName = ParentClassName;
            }
        }
        BPJson->SetStringField(TEXT("native_parent_class"), NativeParentClassName);

        // ========== Minimal Metadata (zeros) ==========
        BPJson->SetNumberField(TEXT("node_count"), 0);
        BPJson->SetNumberField(TEXT("function_count"), 0);
        BPJson->SetNumberField(TEXT("variable_count"), 0);
        BPJson->SetArrayField(TEXT("nodes"), TArray<TSharedPtr<FJsonValue>>());
        BPJson->SetArrayField(TEXT("functions"), TArray<TSharedPtr<FJsonValue>>());
        BPJson->SetArrayField(TEXT("variables"), TArray<TSharedPtr<FJsonValue>>());
        BPJson->SetArrayField(TEXT("calls"), TArray<TSharedPtr<FJsonValue>>());

        // ========== Hash Generation ==========
        // Issue #1339: Fix DirectChild path missing hash bug
        // L-3: Defensive check - ObjectPath should never be empty for valid FAssetData
        FString ContentToHash = ObjectPath.IsEmpty() ? AssetName : ObjectPath;
        if (!HashSuffix.IsEmpty())
        {
            ContentToHash += TEXT("|") + HashSuffix;
        }
        else
        {
            ContentToHash += TEXT("|") + ParentClassName;
        }
        BPJson->SetStringField(TEXT("saved_hash"), FMD5::HashAnsiString(*ContentToHash));

        // ========== High-Cost Marker ==========
        if (bHighCostSkipped)
        {
            BPJson->SetBoolField(TEXT("high_cost_skipped"), true);
        }

        return BPJson;
    }
}
