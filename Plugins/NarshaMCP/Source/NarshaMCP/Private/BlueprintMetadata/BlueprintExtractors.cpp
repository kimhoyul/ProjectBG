// Copyright 2026 Next Stage Inc. All rights reserved.

#include "BlueprintMetadata/BlueprintExtractors.h"
#include "BlueprintMetadata/BlueprintMetadataHelpers.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_AddDelegate.h"  // Issue #107 Phase 2: Delegate binding tracking
#include "K2Node_BaseAsyncTask.h"  // Issue #335: Async node detection
#include "K2Node_Variable.h"  // Issue #88: graph-referenced variable scan (covers VariableGet/Set/StructMemberGet/Set)
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"  // Issue #584 Phase 2A: Asset Registry for Data Asset discovery
#include "AssetRegistry/AssetRegistryModule.h"  // Issue #584 Phase 2A: Asset Registry module access
#include "EdGraphSchema_K2.h"
#include "GameplayTagContainer.h"  // Issue #561 Phase 1: FGameplayTag support for CDO extraction
#if WITH_NARSHAMCP_ENHANCEDINPUT
#include "InputAction.h"  // Issue #584 Phase 2A: UInputAction for Input Action binding extraction
#endif
#include "Engine/DataAsset.h"  // Issue #584 Phase 2A: UDataAsset base class

// Issue #581: Custom log category for Blueprint metadata extraction
#include "CULog.h"

namespace BlueprintExtractors
{
    using namespace BlueprintMetadataHelpers;

    // Issue #582: Property Type Coverage Tracking (v5.1.0)
    static FCriticalSection PropertyTypeStatsLock;  // Thread safety for TMap access
    static TMap<FString, int32> SupportedPropertyTypes;
    static TMap<FString, int32> UnsupportedPropertyTypes;

    // Issue #582: Named constant for Top N unsupported types
    // Issue #1405: UE5 best practices - constexpr adoption (v5.2.0)
    static constexpr int32 TOP_UNSUPPORTED_TYPES_COUNT = 5;
    static_assert(TOP_UNSUPPORTED_TYPES_COUNT == 5,
        "TOP_UNSUPPORTED_TYPES_COUNT must be compile-time constant with value 5");

    TSharedPtr<FJsonObject> ExtractBlueprintMetadata(UBlueprint* Blueprint, const FAssetData& AssetData)
    {
        if (!Blueprint || !Blueprint->GeneratedClass)
        {
            return nullptr;
        }

        TSharedPtr<FJsonObject> BPJson = MakeShareable(new FJsonObject);

        // Basic info
        BPJson->SetStringField(TEXT("name"), Blueprint->GetName());
        BPJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
        BPJson->SetStringField(TEXT("package"), AssetData.PackageName.ToString());
        BPJson->SetStringField(TEXT("class_name"), Blueprint->GeneratedClass->GetName());

        // Parent class
        UClass* ParentClass = Blueprint->GeneratedClass->GetSuperClass();
        if (ParentClass)
        {
            BPJson->SetStringField(TEXT("parent_class"), ParentClass->GetName());

            // Issue #213 Phase 3: Extract NativeParentClass (first native parent in hierarchy)
            UClass* NativeParent = ParentClass;
            while (NativeParent && NativeParent->IsChildOf(UBlueprintGeneratedClass::StaticClass()))
            {
                NativeParent = NativeParent->GetSuperClass();
            }

            if (NativeParent)
            {
                BPJson->SetStringField(TEXT("native_parent_class"), NativeParent->GetName());
            }
            else
            {
                // Fallback: parent_class is already native
                BPJson->SetStringField(TEXT("native_parent_class"), ParentClass->GetName());
            }
        }

        // Extract functions
        TArray<TSharedPtr<FJsonValue>> FunctionsArray;
        ExtractFunctions(Blueprint, FunctionsArray);
        BPJson->SetArrayField(TEXT("functions"), FunctionsArray);

        // Extract call relationships
        TArray<TSharedPtr<FJsonValue>> CallsArray;
        ExtractCallRelationships(Blueprint, CallsArray);
        BPJson->SetArrayField(TEXT("calls"), CallsArray);

        // Extract variables
        TArray<TSharedPtr<FJsonValue>> VariablesArray;
        ExtractVariables(Blueprint, VariablesArray);
        BPJson->SetArrayField(TEXT("variables"), VariablesArray);

        // Issue #1327 Phase 3: Extract nodes AND edges in single traversal (2x speedup)
        // Before: ExtractGraphNodes + ExtractGraphEdges = 2 full graph traversals
        // After: ExtractGraphNodesAndEdges = 1 traversal for both
        TArray<TSharedPtr<FJsonValue>> NodesArray;
        TArray<TSharedPtr<FJsonValue>> EdgesArray;
        ExtractGraphNodesAndEdges(Blueprint, NodesArray, EdgesArray);
        BPJson->SetArrayField(TEXT("nodes"), NodesArray);
        BPJson->SetArrayField(TEXT("edges"), EdgesArray);

        // Extract delegate declarations (Issue #107 Phase 1)
#if UE_CODEGEN_BUILD_DEBUG
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG] Before ExtractDelegates for %s"), *Blueprint->GetName());
#endif
        TArray<TSharedPtr<FJsonValue>> DelegatesArray;
        ExtractDelegates(Blueprint, DelegatesArray);
        BPJson->SetArrayField(TEXT("delegates"), DelegatesArray);
#if UE_CODEGEN_BUILD_DEBUG
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG] After SetArrayField delegates, count=%d"), DelegatesArray.Num());
#endif

        // Extract delegate bindings (Issue #107 Phase 2)
        TArray<TSharedPtr<FJsonValue>> BindingsArray;
        ExtractDelegateBindings(Blueprint, BindingsArray);
        BPJson->SetArrayField(TEXT("delegate_bindings"), BindingsArray);

        // Extract delegate broadcasts (Issue #107 Phase 3)
        TArray<TSharedPtr<FJsonValue>> BroadcastsArray;
        ExtractDelegateBroadcasts(Blueprint, BroadcastsArray);
        BPJson->SetArrayField(TEXT("delegate_broadcasts"), BroadcastsArray);

        // Add count fields for data integrity and performance (v2.8.6)
#if UE_CODEGEN_BUILD_DEBUG
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG] Before setting count fields for %s"), *Blueprint->GetName());
#endif
        BPJson->SetNumberField(TEXT("function_count"), FunctionsArray.Num());
        BPJson->SetNumberField(TEXT("call_count"), CallsArray.Num());
        BPJson->SetNumberField(TEXT("variable_count"), VariablesArray.Num());
        BPJson->SetNumberField(TEXT("node_count"), NodesArray.Num());
        BPJson->SetNumberField(TEXT("edge_count"), EdgesArray.Num());  // Issue #1248
        BPJson->SetNumberField(TEXT("delegate_count"), DelegatesArray.Num());
#if UE_CODEGEN_BUILD_DEBUG
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG] Set delegate_count=%d"), DelegatesArray.Num());
#endif
        BPJson->SetNumberField(TEXT("binding_count"), BindingsArray.Num());
        BPJson->SetNumberField(TEXT("broadcast_count"), BroadcastsArray.Num());
#if UE_CODEGEN_BUILD_DEBUG
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG] After setting all counts"));
#endif

        // Add async node count (Issue #335 - v4.20.3)
        int32 AsyncNodeCount = CountAsyncNodes(Blueprint);
        BPJson->SetNumberField(TEXT("async_node_count"), AsyncNodeCount);
        BPJson->SetBoolField(TEXT("has_async"), AsyncNodeCount > 0);

        return BPJson;
    }

    void ExtractFunctions(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutFunctions)
    {
        if (!Blueprint->GeneratedClass)
        {
            return;
        }

        // Iterate through all functions in the generated class
        for (TFieldIterator<UFunction> FuncIt(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
        {
            UFunction* Function = *FuncIt;
            if (!Function)
            {
                continue;
            }

            TSharedPtr<FJsonObject> FuncJson = MakeShareable(new FJsonObject);

            FuncJson->SetStringField(TEXT("name"), Function->GetName());
            FuncJson->SetStringField(TEXT("full_name"), Function->GetPathName());

            // Function flags
            FuncJson->SetBoolField(TEXT("is_event"), Function->HasAnyFunctionFlags(FUNC_Event));
            FuncJson->SetBoolField(TEXT("is_blueprint_callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
            FuncJson->SetBoolField(TEXT("is_blueprint_pure"), Function->HasAnyFunctionFlags(FUNC_BlueprintPure));
            FuncJson->SetBoolField(TEXT("is_const"), Function->HasAnyFunctionFlags(FUNC_Const));
            FuncJson->SetBoolField(TEXT("is_static"), Function->HasAnyFunctionFlags(FUNC_Static));

            // Network RPC flags (Issue #135 Phase 1)
            FuncJson->SetBoolField(TEXT("is_net_server"), Function->HasAnyFunctionFlags(FUNC_NetServer));
            FuncJson->SetBoolField(TEXT("is_net_client"), Function->HasAnyFunctionFlags(FUNC_NetClient));
            FuncJson->SetBoolField(TEXT("is_net_multicast"), Function->HasAnyFunctionFlags(FUNC_NetMulticast));
            FuncJson->SetBoolField(TEXT("is_net_reliable"), Function->HasAnyFunctionFlags(FUNC_NetReliable));

            // Parameters
            TArray<TSharedPtr<FJsonValue>> ParamsArray;
            for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
            {
                FProperty* Property = *PropIt;
                if (Property->HasAnyPropertyFlags(CPF_Parm))
                {
                    TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
                    ParamJson->SetStringField(TEXT("name"), Property->GetName());
                    ParamJson->SetStringField(TEXT("type"), Property->GetCPPType());
                    ParamJson->SetBoolField(TEXT("is_return"), Property->HasAnyPropertyFlags(CPF_ReturnParm));
                    ParamJson->SetBoolField(TEXT("is_out"), Property->HasAnyPropertyFlags(CPF_OutParm));

                    ParamsArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
                }
            }
            FuncJson->SetArrayField(TEXT("parameters"), ParamsArray);

            OutFunctions.Add(MakeShareable(new FJsonValueObject(FuncJson)));
        }
    }

    void ExtractCallRelationships(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutCalls)
    {
        // Get all graphs
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);

        for (UEdGraph* Graph : Graphs)
        {
            // Use helper to filter graphs (v3.5.0 - Refactored)
            if (!IsRelevantGraphForExtraction(Graph))
            {
                continue;  // Skip this graph - saves ~60% processing time
            }

            // Iterate through all nodes in the graph
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }

                // Check if this is a function call node
                if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
                {
                    UFunction* TargetFunction = CallNode->GetTargetFunction();
                    if (TargetFunction)
                    {
                        TSharedPtr<FJsonObject> CallJson = MakeShareable(new FJsonObject);

                        // Issue #23: Use FindOwningFunction() instead of graph name
                        FString CallerFunctionName = FindOwningFunction(CallNode);
                        CallJson->SetStringField(TEXT("caller"), CallerFunctionName);
                        CallJson->SetStringField(TEXT("callee"), TargetFunction->GetName());
                        CallJson->SetStringField(TEXT("callee_full_name"), TargetFunction->GetPathName());

                        // Determine call type
                        UClass* OwnerClass = TargetFunction->GetOwnerClass();
                        if (OwnerClass)
                        {
                            FString CallType = TEXT("blueprint");
                            if (OwnerClass->IsNative())
                            {
                                CallType = TEXT("cpp");
                            }
                            CallJson->SetStringField(TEXT("call_type"), CallType);
                            CallJson->SetStringField(TEXT("owner_class"), OwnerClass->GetName());
                        }

                        OutCalls.Add(MakeShareable(new FJsonValueObject(CallJson)));
                    }
                }
                // Check for event nodes (Note: Events are not "calls", but we keep for compatibility)
                else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
                {
                    UFunction* EventFunction = EventNode->FindEventSignatureFunction();
                    if (EventFunction)
                    {
                        TSharedPtr<FJsonObject> CallJson = MakeShareable(new FJsonObject);

                        // For events, the "caller" is the event itself (entry point)
                        CallJson->SetStringField(TEXT("caller"), EventFunction->GetName());
                        CallJson->SetStringField(TEXT("callee"), EventFunction->GetName());
                        CallJson->SetStringField(TEXT("callee_full_name"), EventFunction->GetPathName());
                        CallJson->SetStringField(TEXT("call_type"), TEXT("event"));

                        OutCalls.Add(MakeShareable(new FJsonValueObject(CallJson)));
                    }
                }
            }
        }
    }

    // Issue #88: Derive a CPP-like type string from FEdGraphPinType for graph-referenced
    // variables (where we have no FProperty to call GetCPPType() on). Format mirrors the
    // Rust parser's K2Node-source view: lowercase pin category, optional sub-type in parens.
    static FString DerivePinTypeString(const FEdGraphPinType& PT)
    {
        const FString Category = PT.PinCategory.ToString();
        if (PT.PinSubCategoryObject.IsValid())
        {
            return FString::Printf(TEXT("%s(%s)"), *Category, *PT.PinSubCategoryObject->GetName());
        }
        if (!PT.PinSubCategory.IsNone())
        {
            return FString::Printf(TEXT("%s(%s)"), *Category, *PT.PinSubCategory.ToString());
        }
        return Category;
    }

    void ExtractVariables(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutVariables)
    {
        if (!Blueprint->GeneratedClass)
        {
            return;
        }

        // Issue #561 Phase 1: Get CDO (Class Default Object) for default value extraction
        UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
        bool bHasCDO = (CDO != nullptr);

        if (!bHasCDO)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue561] No CDO for Blueprint: %s"), *Blueprint->GetName());
        }

        // Issue #88: track names emitted by the reflection pass so the graph scan can dedup.
        TSet<FName> EmittedNames;

        // Iterate through all properties in the generated class
        for (TFieldIterator<FProperty> PropIt(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;
            if (!Property)
            {
                continue;
            }

            // Skip function parameters
            if (Property->GetOwner<UFunction>())
            {
                continue;
            }

            TSharedPtr<FJsonObject> VarJson = MakeShareable(new FJsonObject);

            VarJson->SetStringField(TEXT("name"), Property->GetName());
            VarJson->SetStringField(TEXT("type"), Property->GetCPPType());

            // Property flags
            VarJson->SetBoolField(TEXT("is_blueprint_visible"), Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
            VarJson->SetBoolField(TEXT("is_blueprint_readonly"), Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly));
            VarJson->SetBoolField(TEXT("is_replicated"), Property->HasAnyPropertyFlags(CPF_Net));
            VarJson->SetBoolField(TEXT("is_config"), Property->HasAnyPropertyFlags(CPF_Config));

            // Issue #561 Phase 1: Extract default value from CDO
            if (bHasCDO)
            {
                FString DefaultValue = ExtractPropertyDefaultValue(Property, CDO);
                if (!DefaultValue.IsEmpty())
                {
                    VarJson->SetStringField(TEXT("default_value"), DefaultValue);
                }
            }

            OutVariables.Add(MakeShareable(new FJsonValueObject(VarJson)));
            EmittedNames.Add(Property->GetFName());
        }

        // Issue #88: Scan Blueprint graphs for variables read/written by K2Node_Variable*
        // (covers VariableGet/Set and StructMemberGet/Set via shared base class). This
        // captures inherited-but-referenced variables that the reflection pass above misses
        // due to EFieldIteratorFlags::ExcludeSuper, matching the Rust parser's K2Node-source
        // union view (analysis_calls.rs::extract_variables_from_nodes).
        //
        // Boundary: UK2Node_Composite::BoundGraph (collapsed subgraphs) and macro
        // instance subgraphs are NOT in any of the four collections below. Variable
        // references living exclusively inside a collapsed graph are invisible to this
        // pass — follow-up issue if needed.
        TArray<UEdGraph*> ScanGraphs;
        ScanGraphs.Reserve(
            Blueprint->UbergraphPages.Num() +
            Blueprint->FunctionGraphs.Num() +
            Blueprint->MacroGraphs.Num() +
            Blueprint->IntermediateGeneratedGraphs.Num());
        ScanGraphs.Append(Blueprint->UbergraphPages);
        ScanGraphs.Append(Blueprint->FunctionGraphs);
        ScanGraphs.Append(Blueprint->MacroGraphs);
        ScanGraphs.Append(Blueprint->IntermediateGeneratedGraphs);

        for (UEdGraph* Graph : ScanGraphs)
        {
            if (!Graph)
            {
                continue;
            }

            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node);
                if (!VarNode)
                {
                    continue;
                }

                const FName VarName = VarNode->GetVarName();
                if (VarName.IsNone() || EmittedNames.Contains(VarName))
                {
                    continue;
                }

                // Issue #88: Prefer FProperty::GetCPPType() so graph_reference entries
                // share the same canonical UE type string as reflection-pass entries
                // (e.g. "USkeletalMeshComponent*", "FVector", "TSubclassOf<UFoo>").
                // ResolveMember walks the class hierarchy, so inherited members resolve
                // even though the reflection loop above uses ExcludeSuper.
                FString TypeString;
                if (FProperty* ResolvedProp =
                        VarNode->VariableReference.ResolveMember<FProperty>(Blueprint->GeneratedClass))
                {
                    TypeString = ResolvedProp->GetCPPType();
                }
                else
                {
                    // Fallback: variable was renamed/deleted from the class or lives in a
                    // non-class scope (function local) we can't resolve via SelfScope.
                    // Derive from value pin so the `type` field is never empty.
                    const FEdGraphPinType* ValuePinType = nullptr;
                    for (UEdGraphPin* Pin : VarNode->Pins)
                    {
                        if (!Pin || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
                        {
                            continue;
                        }
                        if (Pin->PinName == VarName)
                        {
                            ValuePinType = &Pin->PinType;
                            break;
                        }
                    }
                    TypeString = ValuePinType ? DerivePinTypeString(*ValuePinType) : FString();
                }

                TSharedPtr<FJsonObject> VarJson = MakeShareable(new FJsonObject);
                VarJson->SetStringField(TEXT("name"), VarName.ToString());
                VarJson->SetStringField(TEXT("type"), TypeString);
                VarJson->SetBoolField(TEXT("is_blueprint_visible"), true);
                VarJson->SetBoolField(TEXT("is_blueprint_readonly"), false);
                VarJson->SetBoolField(TEXT("is_replicated"), false);
                VarJson->SetBoolField(TEXT("is_config"), false);
                // Issue #88: tag function-local refs so consumers can distinguish them
                // from class-member refs. Matches analysis_calls.rs `is_local` semantics.
                VarJson->SetBoolField(TEXT("is_local"), VarNode->VariableReference.IsLocalScope());
                VarJson->SetStringField(TEXT("source"), TEXT("graph_reference"));

                OutVariables.Add(MakeShareable(new FJsonValueObject(VarJson)));
                EmittedNames.Add(VarName);
            }
        }
    }

    void ExtractGraphNodes(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutNodes)
    {
        // Issue #227 Phase 3-B: Debug actual Blueprint name and graph structure
        FString BPName = Blueprint->GetName();
        FString BPPath = Blueprint->GetPathName();

        // Debug for ALL Blueprints to see naming pattern (Verbose to reduce spam)
        UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP] ExtractGraphNodes called: %s"), *BPName);

        // Debug for GA_AttackPressed specifically (use broader match + Path check)
        if (BPPath.Contains(TEXT("GA_AttackPressed_Base")) || BPName.Contains(TEXT("AttackPressed")))
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found GA_AttackPressed_Base variant!"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Blueprint Name: %s"), *BPName);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Blueprint Path: %s"), *BPPath);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  UbergraphPages: %d"), Blueprint->UbergraphPages.Num());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  FunctionGraphs: %d"), Blueprint->FunctionGraphs.Num());
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Blueprint Type: %d"), (int32)Blueprint->BlueprintType);

            // List all UbergraphPages
            for (int32 i = 0; i < Blueprint->UbergraphPages.Num(); ++i)
            {
                if (Blueprint->UbergraphPages[i])
                {
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("    UbergraphPage[%d]: %s (%d nodes)"),
                        i, *Blueprint->UbergraphPages[i]->GetName(), Blueprint->UbergraphPages[i]->Nodes.Num());
                }
            }

            // List all FunctionGraphs
            for (int32 i = 0; i < Blueprint->FunctionGraphs.Num(); ++i)
            {
                if (Blueprint->FunctionGraphs[i])
                {
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("    FunctionGraph[%d]: %s (%d nodes)"),
                        i, *Blueprint->FunctionGraphs[i]->GetName(), Blueprint->FunctionGraphs[i]->Nodes.Num());
                }
            }
        }

        // Get all graphs
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);

        for (UEdGraph* Graph : Graphs)
        {
            // Use helper to filter graphs (v3.5.0 - Refactored)
            if (!IsRelevantGraphForExtraction(Graph))
            {
                continue;  // Skip Macro, Comment, Animation graphs
            }

            FString GraphName = Graph->GetName();

            // Iterate through all nodes
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }

                // Check if this is a K2Node (Blueprint node)
                UK2Node* K2Node = Cast<UK2Node>(Node);
                if (!K2Node)
                {
                    continue;  // Skip non-K2 nodes
                }

                TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);

                // Common fields for ALL node types
                NodeJson->SetStringField(TEXT("node_id"), Node->GetName());
                NodeJson->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());  // Issue #28: GUID for modification
                NodeJson->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
                NodeJson->SetStringField(TEXT("graph"), GraphName);
                NodeJson->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                NodeJson->SetStringField(TEXT("owning_function"), FindOwningFunction(Node));  // v2.8.6: Function context for all nodes

                // Use helper to enrich with type-specific data (v3.5.0 - Phase 3 Refactored)
                FString NodeType = EnrichNodeByType(Node, NodeJson);
                NodeJson->SetStringField(TEXT("node_type"), NodeType);

                // Extract all pins (works for ALL 114 node types!)
                TArray<TSharedPtr<FJsonValue>> PinsArray;
                ExtractNodePins(Node, PinsArray);
                NodeJson->SetArrayField(TEXT("pins"), PinsArray);

                OutNodes.Add(MakeShareable(new FJsonValueObject(NodeJson)));
            }
        }
    }

    void ExtractNodePins(UEdGraphNode* Node, TArray<TSharedPtr<FJsonValue>>& OutPins)
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            TSharedPtr<FJsonObject> PinJson = MakeShareable(new FJsonObject);

            PinJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
            PinJson->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());  // Issue #28: GUID for modification
            PinJson->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
            PinJson->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));

            // Issue #722 Phase 26: Extended Pin Metadata (B → A Strategy)
            // NEW: Always-present fields for 100% ground truth
            PinJson->SetStringField(TEXT("subcategory"), Pin->PinType.PinSubCategory.ToString());
            PinJson->SetBoolField(TEXT("is_array"), Pin->PinType.IsArray());  // UE 5.6: Use IsArray() instead of bIsArray
            PinJson->SetBoolField(TEXT("is_reference"), Pin->PinType.bIsReference);
            PinJson->SetStringField(TEXT("tooltip"), Pin->PinToolTip);  // PinToolTip is already FString

            // Pin type details
            if (Pin->PinType.PinSubCategoryObject.IsValid())
            {
                PinJson->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategoryObject->GetName());
            }

            // Issue #722 Phase 26: is_const (optional field, only if true)
            if (Pin->PinType.bIsConst)
            {
                PinJson->SetBoolField(TEXT("is_const"), true);
            }

            // Default value (for data pins)
            if (!Pin->DefaultValue.IsEmpty())
            {
                PinJson->SetStringField(TEXT("default_value"), Pin->DefaultValue);
            }

            // Extract Gameplay Tag from struct pins (Phase 4 - Issue #20)
            if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct &&
                Pin->PinType.PinSubCategoryObject.IsValid())
            {
                FString StructName = Pin->PinType.PinSubCategoryObject->GetName();

                // Debug log to see actual struct names
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Struct Pin Found - Name: %s, Default: %s"),
                       *StructName, *Pin->DefaultValue);

                // Check if this is a GameplayTag struct (support both with and without F prefix)
                if (StructName == TEXT("GameplayTag") || StructName == TEXT("GameplayTagContainer") ||
                    StructName == TEXT("FGameplayTag") || StructName == TEXT("FGameplayTagContainer"))
                {
                    // Try to extract tag value from DefaultValue string
                    // Format: "(TagName=\"Event.Character.Attack\")"
                    if (!Pin->DefaultValue.IsEmpty())
                    {
                        FString TagValue = Pin->DefaultValue;

                        // Parse TagName from the string
                        int32 TagNameStart = TagValue.Find(TEXT("TagName=\""));
                        if (TagNameStart != INDEX_NONE)
                        {
                            TagNameStart += 9; // Length of "TagName=\""
                            int32 TagNameEnd = TagValue.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, TagNameStart);

                            if (TagNameEnd != INDEX_NONE)
                            {
                                FString ExtractedTag = TagValue.Mid(TagNameStart, TagNameEnd - TagNameStart);
                                if (!ExtractedTag.IsEmpty())
                                {
                                    PinJson->SetStringField(TEXT("gameplay_tag"), ExtractedTag);
                                }
                            }
                        }
                    }

                    PinJson->SetStringField(TEXT("pin_struct_type"), StructName);
                }
            }

            // Linked connections - THE KEY TO EXECUTION FLOW RECONSTRUCTION!
            if (Pin->LinkedTo.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> LinkedArray;
                for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                {
                    if (LinkedPin && LinkedPin->GetOwningNode())
                    {
                        TSharedPtr<FJsonObject> LinkedJson = MakeShareable(new FJsonObject);
                        LinkedJson->SetStringField(TEXT("node_id"), LinkedPin->GetOwningNode()->GetName());
                        LinkedJson->SetStringField(TEXT("pin_name"), LinkedPin->PinName.ToString());

                        LinkedArray.Add(MakeShareable(new FJsonValueObject(LinkedJson)));
                    }
                }
                PinJson->SetArrayField(TEXT("linked_to"), LinkedArray);
            }

            OutPins.Add(MakeShareable(new FJsonValueObject(PinJson)));
        }
    }

    void ExtractGraphEdges(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutEdges)
    {
        if (!Blueprint)
        {
            return;
        }

        // Get all graphs
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);

        for (UEdGraph* Graph : Graphs)
        {
            // Use helper to filter graphs (same as ExtractGraphNodes)
            if (!IsRelevantGraphForExtraction(Graph))
            {
                continue;  // Skip Macro, Comment, Animation graphs
            }

            // Iterate through all nodes
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }

                // Check if this is a K2Node (Blueprint node)
                UK2Node* K2Node = Cast<UK2Node>(Node);
                if (!K2Node)
                {
                    continue;  // Skip non-K2 nodes
                }

                // Extract edges from all output pins
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin)
                    {
                        continue;
                    }

                    // Only process output pins (edges go from output to input)
                    if (Pin->Direction != EGPD_Output)
                    {
                        continue;
                    }

                    // Extract all connections from this output pin
                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin || !LinkedPin->GetOwningNode())
                        {
                            continue;
                        }

                        TSharedPtr<FJsonObject> EdgeJson = MakeShareable(new FJsonObject);

                        // Source node and pin (this node's output pin)
                        EdgeJson->SetStringField(TEXT("source_node"), Node->NodeGuid.ToString());
                        EdgeJson->SetStringField(TEXT("source_pin"), Pin->PinId.ToString());

                        // Target node and pin (connected node's input pin)
                        EdgeJson->SetStringField(TEXT("target_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
                        EdgeJson->SetStringField(TEXT("target_pin"), LinkedPin->PinId.ToString());

                        OutEdges.Add(MakeShareable(new FJsonValueObject(EdgeJson)));
                    }
                }
            }
        }
    }

    // ========================================================================
    // Issue #1327 Phase 3: Combined Node + Edge Extraction (2x speedup)
    // ========================================================================
    // Before: ExtractGraphNodes() + ExtractGraphEdges() = 2 full traversals
    // After: ExtractGraphNodesAndEdges() = 1 traversal for both
    // ========================================================================
    // Issue #1327 M-2: Named constant for edge estimation ratio
    // Based on profiling of 776 Blueprints, average edges per node is ~2
    constexpr int32 ESTIMATED_EDGES_PER_NODE = 2;

    void ExtractGraphNodesAndEdges(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutNodes, TArray<TSharedPtr<FJsonValue>>& OutEdges)
    {
        if (!Blueprint)
        {
            return;
        }

        // Get all graphs
        TArray<UEdGraph*> Graphs;
        Blueprint->GetAllGraphs(Graphs);

        // Issue #1327: Pre-allocate memory for better performance
        int32 EstimatedNodeCount = 0;
        for (UEdGraph* Graph : Graphs)
        {
            if (Graph && IsRelevantGraphForExtraction(Graph))
            {
                EstimatedNodeCount += Graph->Nodes.Num();
            }
        }
        OutNodes.Reserve(EstimatedNodeCount);
        OutEdges.Reserve(EstimatedNodeCount * ESTIMATED_EDGES_PER_NODE);

#if UE_CODEGEN_BUILD_DEBUG
        FString BPName = Blueprint->GetName();
        FString BPPath = Blueprint->GetPathName();
        UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP] ExtractGraphNodesAndEdges called: %s"), *BPName);

        // Debug for GA_AttackPressed specifically
        if (BPPath.Contains(TEXT("GA_AttackPressed_Base")) || BPName.Contains(TEXT("AttackPressed")))
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP] Found GA_AttackPressed_Base variant!"));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Blueprint Name: %s"), *BPName);
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Blueprint Path: %s"), *BPPath);
        }
#endif

        for (UEdGraph* Graph : Graphs)
        {
            // Use helper to filter graphs (v3.5.0 - Refactored)
            if (!IsRelevantGraphForExtraction(Graph))
            {
                continue;  // Skip Macro, Comment, Animation graphs
            }

            FString GraphName = Graph->GetName();

            // Iterate through all nodes - SINGLE TRAVERSAL for both nodes and edges
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node)
                {
                    continue;
                }

                // Check if this is a K2Node (Blueprint node)
                UK2Node* K2Node = Cast<UK2Node>(Node);
                if (!K2Node)
                {
                    continue;  // Skip non-K2 nodes
                }

                // ===== EXTRACT NODE =====
                TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);

                // Common fields for ALL node types
                FString NodeGuidStr = Node->NodeGuid.ToString();
                NodeJson->SetStringField(TEXT("node_id"), Node->GetName());
                NodeJson->SetStringField(TEXT("node_guid"), NodeGuidStr);
                NodeJson->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
                NodeJson->SetStringField(TEXT("graph"), GraphName);
                NodeJson->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                NodeJson->SetStringField(TEXT("owning_function"), FindOwningFunction(Node));

                // Use helper to enrich with type-specific data (v3.5.0 - Phase 3 Refactored)
                FString NodeType = EnrichNodeByType(Node, NodeJson);
                NodeJson->SetStringField(TEXT("node_type"), NodeType);

                // Extract all pins (works for ALL 114 node types!)
                TArray<TSharedPtr<FJsonValue>> PinsArray;
                ExtractNodePins(Node, PinsArray);
                NodeJson->SetArrayField(TEXT("pins"), PinsArray);

                OutNodes.Add(MakeShareable(new FJsonValueObject(NodeJson)));

                // ===== EXTRACT EDGES (from output pins only) =====
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin || Pin->Direction != EGPD_Output)
                    {
                        continue;  // Only process output pins
                    }

                    FString PinIdStr = Pin->PinId.ToString();

                    for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                    {
                        if (!LinkedPin || !LinkedPin->GetOwningNode())
                        {
                            continue;
                        }

                        TSharedPtr<FJsonObject> EdgeJson = MakeShareable(new FJsonObject);

                        // Source node and pin (this node's output pin)
                        EdgeJson->SetStringField(TEXT("source_node"), NodeGuidStr);
                        EdgeJson->SetStringField(TEXT("source_pin"), PinIdStr);

                        // Target node and pin (connected node's input pin)
                        EdgeJson->SetStringField(TEXT("target_node"), LinkedPin->GetOwningNode()->NodeGuid.ToString());
                        EdgeJson->SetStringField(TEXT("target_pin"), LinkedPin->PinId.ToString());

                        OutEdges.Add(MakeShareable(new FJsonValueObject(EdgeJson)));
                    }
                }
            }
        }
    }

    // ========================================================================
    // Delegate Tracking Functions (Issue #107 Phase 1-3, moved from Commandlet in Issue #108 Phase 0)
    // ========================================================================

    void ExtractDelegates(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutDelegates)
    {
        if (!Blueprint->GeneratedClass)
        {
            return;
        }

        // Iterate through all FMulticastDelegateProperty in the generated class (Issue #107 Phase 1)
        for (TFieldIterator<FMulticastDelegateProperty> DelegateIt(Blueprint->GeneratedClass, EFieldIteratorFlags::ExcludeSuper); DelegateIt; ++DelegateIt)
        {
            FMulticastDelegateProperty* DelegateProperty = *DelegateIt;
            if (!DelegateProperty)
            {
                continue;
            }

            TSharedPtr<FJsonObject> DelegateJson = MakeShareable(new FJsonObject);

            // Basic delegate info
            DelegateJson->SetStringField(TEXT("delegate_name"), DelegateProperty->GetName());
            DelegateJson->SetStringField(TEXT("delegate_type"), DelegateProperty->GetCPPType(nullptr, 0));

            // Property flags (BlueprintAssignable, BlueprintCallable, etc.)
            DelegateJson->SetBoolField(TEXT("is_blueprint_assignable"), DelegateProperty->HasAnyPropertyFlags(CPF_BlueprintAssignable));
            DelegateJson->SetBoolField(TEXT("is_blueprint_callable"), DelegateProperty->HasAnyPropertyFlags(CPF_BlueprintCallable));
            DelegateJson->SetBoolField(TEXT("is_replicated"), DelegateProperty->HasAnyPropertyFlags(CPF_Net));

            // Extract signature function (the function that defines delegate parameters and return type)
            UFunction* SignatureFunction = DelegateProperty->SignatureFunction;
            if (SignatureFunction)
            {
                DelegateJson->SetStringField(TEXT("signature_function"), SignatureFunction->GetName());
                DelegateJson->SetStringField(TEXT("signature_full_name"), SignatureFunction->GetPathName());

                // Extract return type
                FProperty* ReturnProperty = SignatureFunction->GetReturnProperty();
                if (ReturnProperty)
                {
                    DelegateJson->SetStringField(TEXT("return_type"), ReturnProperty->GetCPPType());
                }
                else
                {
                    DelegateJson->SetStringField(TEXT("return_type"), TEXT("void"));
                }

                // Extract parameters
                TArray<TSharedPtr<FJsonValue>> ParametersArray;
                for (TFieldIterator<FProperty> ParamIt(SignatureFunction); ParamIt; ++ParamIt)
                {
                    FProperty* ParamProperty = *ParamIt;

                    // Skip return parameter (already handled above)
                    if (ParamProperty->HasAnyPropertyFlags(CPF_ReturnParm))
                    {
                        continue;
                    }

                    // Only include actual parameters
                    if (ParamProperty->HasAnyPropertyFlags(CPF_Parm))
                    {
                        TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);

                        ParamJson->SetStringField(TEXT("name"), ParamProperty->GetName());
                        ParamJson->SetStringField(TEXT("type"), ParamProperty->GetCPPType());
                        ParamJson->SetBoolField(TEXT("is_out"), ParamProperty->HasAnyPropertyFlags(CPF_OutParm) && !ParamProperty->HasAnyPropertyFlags(CPF_ConstParm));
                        ParamJson->SetBoolField(TEXT("is_reference"), ParamProperty->HasAnyPropertyFlags(CPF_ReferenceParm));
                        ParamJson->SetBoolField(TEXT("is_const"), ParamProperty->HasAnyPropertyFlags(CPF_ConstParm));

                        ParametersArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
                    }
                }
                DelegateJson->SetArrayField(TEXT("parameters"), ParametersArray);
                DelegateJson->SetNumberField(TEXT("parameter_count"), ParametersArray.Num());

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Delegate] Extracted: %s (Signature: %s, Params: %d)"),
                       *DelegateProperty->GetName(), *SignatureFunction->GetName(), ParametersArray.Num());
            }
            else
            {
                UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Delegate] No signature function for: %s"), *DelegateProperty->GetName());
            }

            OutDelegates.Add(MakeShareable(new FJsonValueObject(DelegateJson)));
        }
    }

    void ExtractDelegateBindings(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutBindings)
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG-Bindings] ExtractDelegateBindings for %s"), *Blueprint->GetName());
        // Get all graphs from Blueprint (Issue #107 Phase 2)
        TArray<UEdGraph*> AllGraphs;
        Blueprint->GetAllGraphs(AllGraphs);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DEBUG-Bindings] Found %d graphs"), AllGraphs.Num());

        for (UEdGraph* Graph : AllGraphs)
        {
            if (!Graph || !BlueprintMetadataHelpers::IsRelevantGraphForExtraction(Graph))
            {
                continue;
            }

            // Iterate through all nodes in the graph
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                // DEBUG: Log all node types to find delegate-related nodes (Issue #463)
                FString NodeType = Node->GetClass()->GetName();
                if (NodeType.Contains(TEXT("Delegate")) || NodeType.Contains(TEXT("Bind")) || NodeType.Contains(TEXT("Event")))
                {
                    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][DELEGATE-NODE] %s in graph '%s' of BP '%s'"),
                           *NodeType, *Graph->GetName(), *Blueprint->GetName());
                }

                // Check if this is a UK2Node_AddDelegate node
                UK2Node_AddDelegate* AddDelegateNode = Cast<UK2Node_AddDelegate>(Node);
                if (!AddDelegateNode)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> BindingJson = MakeShareable(new FJsonObject);

                // Basic node info
                BindingJson->SetStringField(TEXT("node_id"), Node->GetName());
                BindingJson->SetStringField(TEXT("node_title"), AddDelegateNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                BindingJson->SetStringField(TEXT("graph_name"), Graph->GetName());

                // Find owning function/event
                FString OwningFunction = BlueprintMetadataHelpers::FindOwningFunction(Node);
                BindingJson->SetStringField(TEXT("owning_function"), OwningFunction);

                // Extract delegate property name (the delegate being bound to)
                // UK2Node_AddDelegate has a DelegateReference public member (UE 5.6 API)
                FString DelegateName = AddDelegateNode->GetPropertyName().ToString();
                BindingJson->SetStringField(TEXT("delegate_name"), DelegateName);

                // Extract pin connections
                TArray<TSharedPtr<FJsonValue>> ConnectionsArray;

                // UK2Node_AddDelegate typically has these pins:
                // - "execute" (input exec pin)
                // - "then" (output exec pin)
                // - "Delegate" (input delegate pin) - this connects to the event/function to bind
                // - Self pin (the object that owns the delegate)

                for (UEdGraphPin* Pin : AddDelegateNode->Pins)
                {
                    if (!Pin || Pin->LinkedTo.Num() == 0)
                    {
                        continue;
                    }

                    // We're particularly interested in the delegate input pin
                    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate && Pin->Direction == EGPD_Input)
                    {
                        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (!LinkedPin || !LinkedPin->GetOwningNode())
                            {
                                continue;
                            }

                            TSharedPtr<FJsonObject> ConnectionJson = MakeShareable(new FJsonObject);

                            ConnectionJson->SetStringField(TEXT("source_pin"), Pin->PinName.ToString());
                            ConnectionJson->SetStringField(TEXT("source_category"), Pin->PinType.PinCategory.ToString());

                            UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                            ConnectionJson->SetStringField(TEXT("target_node_id"), LinkedNode->GetName());
                            ConnectionJson->SetStringField(TEXT("target_node_type"), LinkedNode->GetClass()->GetName());
                            ConnectionJson->SetStringField(TEXT("target_pin"), LinkedPin->PinName.ToString());

                            // Try to get the target function/event name
                            FString TargetFunctionName = TEXT("");
                            if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(LinkedNode))
                            {
                                if (UFunction* EventFunc = EventNode->FindEventSignatureFunction())
                                {
                                    TargetFunctionName = EventFunc->GetName();
                                }
                            }
                            else if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(LinkedNode))
                            {
                                if (UFunction* TargetFunc = CallNode->GetTargetFunction())
                                {
                                    TargetFunctionName = TargetFunc->GetName();
                                }
                            }

                            if (!TargetFunctionName.IsEmpty())
                            {
                                ConnectionJson->SetStringField(TEXT("target_function_name"), TargetFunctionName);
                            }

                            ConnectionsArray.Add(MakeShareable(new FJsonValueObject(ConnectionJson)));

                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DelegateBinding] %s -> %s (Node: %d)"),
                                   *DelegateName, *TargetFunctionName, LinkedNode->GetUniqueID());
                        }
                    }
                }

                BindingJson->SetArrayField(TEXT("connections"), ConnectionsArray);
                BindingJson->SetNumberField(TEXT("connection_count"), ConnectionsArray.Num());

                OutBindings.Add(MakeShareable(new FJsonValueObject(BindingJson)));
            }
        }
    }

    void ExtractDelegateBroadcasts(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutBroadcasts)
    {
        // Get all graphs from Blueprint (Issue #107 Phase 3)
        TArray<UEdGraph*> AllGraphs;
        Blueprint->GetAllGraphs(AllGraphs);

        for (UEdGraph* Graph : AllGraphs)
        {
            if (!Graph || !BlueprintMetadataHelpers::IsRelevantGraphForExtraction(Graph))
            {
                continue;
            }

            // Iterate through all nodes in the graph
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                // Check if this is a UK2Node_CallFunction node
                UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
                if (!CallNode)
                {
                    continue;
                }

                // Get the target function
                UFunction* TargetFunction = CallNode->GetTargetFunction();
                if (!TargetFunction)
                {
                    continue;
                }

                FString FunctionName = TargetFunction->GetName();

                // Check if this is a __DelegateSignature function (Broadcast call)
                // Delegate broadcast functions always end with __DelegateSignature
                if (!FunctionName.EndsWith(TEXT("__DelegateSignature")))
                {
                    continue;
                }

                TSharedPtr<FJsonObject> BroadcastJson = MakeShareable(new FJsonObject);

                // Basic node info
                BroadcastJson->SetStringField(TEXT("node_id"), Node->GetName());
                BroadcastJson->SetStringField(TEXT("node_title"), CallNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                BroadcastJson->SetStringField(TEXT("graph_name"), Graph->GetName());

                // Find owning function/event
                FString OwningFunction = BlueprintMetadataHelpers::FindOwningFunction(Node);
                BroadcastJson->SetStringField(TEXT("owning_function"), OwningFunction);

                // Extract delegate name (remove __DelegateSignature suffix)
                FString DelegateName = FunctionName;
                DelegateName.RemoveFromEnd(TEXT("__DelegateSignature"));
                BroadcastJson->SetStringField(TEXT("delegate_name"), DelegateName);
                BroadcastJson->SetStringField(TEXT("signature_function"), FunctionName);
                BroadcastJson->SetStringField(TEXT("signature_full_name"), TargetFunction->GetPathName());

                // Extract broadcast parameters (from input pins)
                TArray<TSharedPtr<FJsonValue>> ParametersArray;

                for (UEdGraphPin* Pin : CallNode->Pins)
                {
                    if (!Pin)
                    {
                        continue;
                    }

                    // Skip execution pins and output pins
                    if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->Direction == EGPD_Output)
                    {
                        continue;
                    }

                    // Skip "self" pin (the object that owns the delegate)
                    if (Pin->PinName.ToString().Equals(TEXT("self"), ESearchCase::IgnoreCase))
                    {
                        continue;
                    }

                    TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);

                    ParamJson->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    ParamJson->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());

                    // Get default value if pin is not connected
                    if (Pin->LinkedTo.Num() == 0)
                    {
                        FString DefaultValue = Pin->DefaultValue;
                        if (DefaultValue.IsEmpty())
                        {
                            DefaultValue = Pin->DefaultTextValue.ToString();
                        }
                        if (!DefaultValue.IsEmpty())
                        {
                            ParamJson->SetStringField(TEXT("default_value"), DefaultValue);
                        }
                    }
                    else
                    {
                        // Track if parameter comes from a connection
                        ParamJson->SetBoolField(TEXT("is_connected"), true);

                        // Get source of the connection
                        if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0])
                        {
                            UEdGraphNode* SourceNode = Pin->LinkedTo[0]->GetOwningNode();
                            if (SourceNode)
                            {
                                ParamJson->SetStringField(TEXT("source_node_id"), SourceNode->GetName());
                                ParamJson->SetStringField(TEXT("source_node_type"), SourceNode->GetClass()->GetName());
                            }
                        }
                    }

                    ParametersArray.Add(MakeShareable(new FJsonValueObject(ParamJson)));
                }

                BroadcastJson->SetArrayField(TEXT("parameters"), ParametersArray);
                BroadcastJson->SetNumberField(TEXT("parameter_count"), ParametersArray.Num());

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][DelegateBroadcast] %s.Broadcast() in %s (Node: %d, Params: %d)"),
                       *DelegateName, *OwningFunction, Node->GetUniqueID(), ParametersArray.Num());

                OutBroadcasts.Add(MakeShareable(new FJsonValueObject(BroadcastJson)));
            }
        }
    }

    int32 CountAsyncNodes(UBlueprint* Blueprint)
    {
        if (!Blueprint)
        {
            return 0;
        }

        int32 AsyncCount = 0;

        // Get all graphs from Blueprint
        TArray<UEdGraph*> AllGraphs;
        Blueprint->GetAllGraphs(AllGraphs);

        for (UEdGraph* Graph : AllGraphs)
        {
            if (!Graph || !BlueprintMetadataHelpers::IsRelevantGraphForExtraction(Graph))
            {
                continue;
            }

            // Iterate through all nodes in the graph
            for (UEdGraphNode* Node : Graph->Nodes)
            {
                UK2Node* K2Node = Cast<UK2Node>(Node);
                if (!K2Node)
                {
                    continue;
                }

                // ✅ Method 1: UK2Node_BaseAsyncTask inheritance (99% accuracy, 70% coverage)
                // Covers: Delay, AsyncAction, WaitGameplayEvent, etc.
                if (K2Node->IsA<UK2Node_BaseAsyncTask>())
                {
                    AsyncCount++;
                    UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP][AsyncNode] Found via BaseAsyncTask: %s"), *K2Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
                    continue;  // Skip other checks to avoid double counting
                }

                // ✅ Method 2: Latent metadata check (95% accuracy, 80% coverage)
                // Covers: All Latent-flagged functions
                UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(K2Node);
                if (FuncNode)
                {
                    UFunction* Func = FuncNode->GetTargetFunction();
                    if (Func && Func->HasMetaData(TEXT("Latent")))
                    {
                        AsyncCount++;
                        UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP][AsyncNode] Found via Latent meta: %s"), *Func->GetName());
                        continue;  // Skip other checks
                    }

                    // ✅ Method 3: AbilityTask/GameplayTask type check (90% accuracy, GAS-specific)
                    // Covers: WaitForGameplayEvent, PlayMontageAndWait, etc.
                    if (Func)
                    {
                        FProperty* ReturnProp = Func->GetReturnProperty();
                        if (ReturnProp)
                        {
                            FString ReturnType = ReturnProp->GetCPPType();
                            if (ReturnType.Contains(TEXT("AbilityTask")) ||
                                ReturnType.Contains(TEXT("UGameplayTask")))
                            {
                                AsyncCount++;
                                UE_LOG(LogNarshaMCPMetadata, Verbose, TEXT("[NarshaMCP][AsyncNode] Found via AbilityTask return: %s (Type: %s)"),
                                       *Func->GetName(), *ReturnType);
                                continue;
                            }
                        }
                    }
                }
            }
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][CountAsyncNodes] Blueprint: %s, Async Nodes: %d"),
               *Blueprint->GetName(), AsyncCount);

        return AsyncCount;
    }

    // Issue #1020 M-1: TMap-based struct handler registry for O(1) lookup
    using FStructHandler = TFunction<FString(FStructProperty*, UObject*)>;
    static TMap<FString, FStructHandler> StructHandlers;
    static bool bStructHandlersInitialized = false;  // Issue #1088: Moved to namespace level for test access

    /**
     * Initialize the TMap-based struct handler registry for O(1) property extraction.
     *
     * Thread Safety: Uses double-checked locking with PropertyTypeStatsLock for multi-threaded initialization.
     * Performance: One-time cost ~10ms for 178 handlers, amortized over 9,982+ properties.
     *
     * Handlers Registered:
     * - 14 Value Extraction types (FVector, FRotator, FTransform, FFloatRange, etc.)
     * - 164 Type-Only types (plugin-specific structs)
     *
     * @note Called automatically from ExtractPropertyDefaultValue() on first use
     * @see ExtractPropertyDefaultValue() for usage context
     */
    static void InitializeStructHandlers()
    {
        // Fast path: Already initialized (no lock needed)
        // Issue #1088: Use namespace-level variable for test access
        if (bStructHandlersInitialized)
        {
            return;
        }

        // Thread-safe initialization with double-checked locking (Issue #1020 M-1 Code Review)
        FScopeLock Lock(&PropertyTypeStatsLock);

        // Re-check after acquiring lock (another thread may have initialized while we waited)
        if (bStructHandlersInitialized)
        {
            return;
        }

        // Helper for Type-Only handlers (returns {Type=TypeName})
        auto MakeTypeOnlyHandler = [](const FString& TypeName) -> FStructHandler
        {
            return [TypeName](FStructProperty* StructProp, UObject* CDO) -> FString
            {
                return FString::Printf(TEXT("{Type=%s}"), *TypeName);
            };
        };

        // ========================================
        // Value Extraction Handlers (14 types)
        // ========================================

        // GameplayTag (2 variations)
        auto GameplayTagHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FGameplayTag* Tag = StructProp->ContainerPtrToValuePtr<FGameplayTag>(CDO);
            if (Tag && Tag->IsValid())
            {
                return Tag->ToString();
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("GameplayTag"), GameplayTagHandler);
        StructHandlers.Add(TEXT("FGameplayTag"), GameplayTagHandler);

        // Vector (2 variations)
        auto VectorHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FVector* Vec = StructProp->ContainerPtrToValuePtr<FVector>(CDO);
            if (Vec)
            {
                return FString::Printf(TEXT("(X=%.2f,Y=%.2f,Z=%.2f)"), Vec->X, Vec->Y, Vec->Z);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Vector"), VectorHandler);
        StructHandlers.Add(TEXT("FVector"), VectorHandler);

        // Rotator (2 variations)
        auto RotatorHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FRotator* Rot = StructProp->ContainerPtrToValuePtr<FRotator>(CDO);
            if (Rot)
            {
                return FString::Printf(TEXT("(Pitch=%.2f,Yaw=%.2f,Roll=%.2f)"), Rot->Pitch, Rot->Yaw, Rot->Roll);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Rotator"), RotatorHandler);
        StructHandlers.Add(TEXT("FRotator"), RotatorHandler);

        // Transform (2 variations)
        auto TransformHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FTransform* Trans = StructProp->ContainerPtrToValuePtr<FTransform>(CDO);
            if (Trans)
            {
                FVector Loc = Trans->GetLocation();
                FRotator Rot = Trans->Rotator();
                FVector Scale = Trans->GetScale3D();
                return FString::Printf(TEXT("(Location=(X=%.2f,Y=%.2f,Z=%.2f),Rotation=(Pitch=%.2f,Yaw=%.2f,Roll=%.2f),Scale=(X=%.2f,Y=%.2f,Z=%.2f))"),
                    Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll, Scale.X, Scale.Y, Scale.Z);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Transform"), TransformHandler);
        StructHandlers.Add(TEXT("FTransform"), TransformHandler);

        // LinearColor (2 variations)
        auto LinearColorHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FLinearColor* Color = StructProp->ContainerPtrToValuePtr<FLinearColor>(CDO);
            if (Color)
            {
                return FString::Printf(TEXT("(R=%.3f,G=%.3f,B=%.3f,A=%.3f)"), Color->R, Color->G, Color->B, Color->A);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("LinearColor"), LinearColorHandler);
        StructHandlers.Add(TEXT("FLinearColor"), LinearColorHandler);

        // Color (2 variations)
        auto ColorHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FColor* Color = StructProp->ContainerPtrToValuePtr<FColor>(CDO);
            if (Color)
            {
                return FString::Printf(TEXT("(R=%d,G=%d,B=%d,A=%d)"), Color->R, Color->G, Color->B, Color->A);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Color"), ColorHandler);
        StructHandlers.Add(TEXT("FColor"), ColorHandler);

        // Vector2D (2 variations)
        auto Vector2DHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FVector2D* Vec2D = StructProp->ContainerPtrToValuePtr<FVector2D>(CDO);
            if (Vec2D)
            {
                return FString::Printf(TEXT("(X=%.2f,Y=%.2f)"), Vec2D->X, Vec2D->Y);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Vector2D"), Vector2DHandler);
        StructHandlers.Add(TEXT("FVector2D"), Vector2DHandler);

        // GameplayTagContainer (2 variations)
        auto GameplayTagContainerHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FGameplayTagContainer* TagContainer = StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO);
            if (TagContainer)
            {
                TArray<FGameplayTag> Tags;
                TagContainer->GetGameplayTagArray(Tags);

                TArray<FString> TagStrings;
                for (const FGameplayTag& Tag : Tags)
                {
                    TagStrings.Add(Tag.ToString());
                }

                if (TagStrings.Num() > 0)
                    return FString::Printf(TEXT("[%s]"), *FString::Join(TagStrings, TEXT(", ")));
                else
                    return TEXT("[]");
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("GameplayTagContainer"), GameplayTagContainerHandler);
        StructHandlers.Add(TEXT("FGameplayTagContainer"), GameplayTagContainerHandler);

        // Guid (2 variations) - Special: IsValid check
        auto GuidHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FGuid* GuidPtr = StructProp->ContainerPtrToValuePtr<FGuid>(CDO);
            if (GuidPtr && GuidPtr->IsValid())
            {
                return GuidPtr->ToString();
            }
            else
            {
                return TEXT("Invalid");
            }
        };
        StructHandlers.Add(TEXT("Guid"), GuidHandler);
        StructHandlers.Add(TEXT("FGuid"), GuidHandler);

        // IntPoint (2 variations)
        auto IntPointHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FIntPoint* IntPt = StructProp->ContainerPtrToValuePtr<FIntPoint>(CDO);
            if (IntPt)
            {
                return FString::Printf(TEXT("(X=%d,Y=%d)"), IntPt->X, IntPt->Y);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("IntPoint"), IntPointHandler);
        StructHandlers.Add(TEXT("FIntPoint"), IntPointHandler);

        // Vector4 (2 variations)
        auto Vector4Handler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FVector4* Vec4 = StructProp->ContainerPtrToValuePtr<FVector4>(CDO);
            if (Vec4)
            {
                return FString::Printf(TEXT("(X=%.2f,Y=%.2f,Z=%.2f,W=%.2f)"), Vec4->X, Vec4->Y, Vec4->Z, Vec4->W);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Vector4"), Vector4Handler);
        StructHandlers.Add(TEXT("FVector4"), Vector4Handler);

        // IntVector (2 variations) - Phase 9 Step 2A
        // Note: nullptr check only, no IsValid member
        // Reason: FIntVector is always valid if ptr is non-null
        auto IntVectorHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FIntVector* IntVec = StructProp->ContainerPtrToValuePtr<FIntVector>(CDO);
            if (IntVec)
            {
                return FString::Printf(TEXT("(X=%d,Y=%d,Z=%d)"), IntVec->X, IntVec->Y, IntVec->Z);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("IntVector"), IntVectorHandler);
        StructHandlers.Add(TEXT("FIntVector"), IntVectorHandler);

        // Box (2 variations) - Phase 9 Step 2A
        // Note: Combined nullptr AND IsValid check for FBox
        // Reason: FBox has IsValid member to check validity state
        auto BoxHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FBox* BoxPtr = StructProp->ContainerPtrToValuePtr<FBox>(CDO);
            if (BoxPtr && BoxPtr->IsValid)
            {
                return FString::Printf(TEXT("(Min=(%.2f,%.2f,%.2f),Max=(%.2f,%.2f,%.2f))"),
                    BoxPtr->Min.X, BoxPtr->Min.Y, BoxPtr->Min.Z,
                    BoxPtr->Max.X, BoxPtr->Max.Y, BoxPtr->Max.Z);
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("Box"), BoxHandler);
        StructHandlers.Add(TEXT("FBox"), BoxHandler);

        // FloatRange (2 variations) - Phase 9 Step 2A (C-2 Fix)
        // Note: nullptr check only, then HasBounds() checks
        // Reason: FFloatRange validity determined by HasLowerBound/HasUpperBound
        auto FloatRangeHandler = [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            FFloatRange* RangePtr = StructProp->ContainerPtrToValuePtr<FFloatRange>(CDO);
            if (RangePtr)
            {
                // Check bounds before accessing values (Issue #1020 CR-2)
                if (RangePtr->HasLowerBound() && RangePtr->HasUpperBound())
                {
                    return FString::Printf(TEXT("(Min=%.2f,Max=%.2f)"),
                        RangePtr->GetLowerBoundValue(), RangePtr->GetUpperBoundValue());
                }
                else if (RangePtr->HasLowerBound())
                {
                    return FString::Printf(TEXT("(Min=%.2f,Max=Unbounded)"),
                        RangePtr->GetLowerBoundValue());
                }
                else if (RangePtr->HasUpperBound())
                {
                    return FString::Printf(TEXT("(Min=Unbounded,Max=%.2f)"),
                        RangePtr->GetUpperBoundValue());
                }
                else
                {
                    return TEXT("(Unbounded)");
                }
            }
            return TEXT("");
        };
        StructHandlers.Add(TEXT("FloatRange"), FloatRangeHandler);
        StructHandlers.Add(TEXT("FFloatRange"), FloatRangeHandler);

        // ========================================
        // Type-Only Handlers (75 types)
        // ========================================

        // State Machine runtime types (10 types)
        TArray<FString> StateMachineTypes = {
            TEXT("SMGraphProperty_Runtime"), TEXT("FSMGraphProperty_Runtime"),
            TEXT("PointerToUberGraphFrame"), TEXT("FPointerToUberGraphFrame"),
            TEXT("SMTransition"), TEXT("FSMTransition"),
            TEXT("SMState"), TEXT("FSMState"),
            TEXT("SMStateMachine"), TEXT("FSMStateMachine")
        };
        for (const FString& TypeName : StateMachineTypes)
        {
            StructHandlers.Add(TypeName, MakeTypeOnlyHandler(TypeName));
        }

        // Top 5 additional struct types (Phase 6) (10 types counting variations)
        TArray<FString> Phase6Types = {
            TEXT("RigUnit_HierarchyAddControlTransform_Settings"), TEXT("FRigUnit_HierarchyAddControlTransform_Settings"),
            TEXT("RigElementKey"), TEXT("FRigElementKey"),
            TEXT("ModuleSettings"), TEXT("FModuleSettings"),
            TEXT("StateTreeDelegateDispatcher"), TEXT("FStateTreeDelegateDispatcher")
        };
        for (const FString& TypeName : Phase6Types)
        {
            StructHandlers.Add(TypeName, MakeTypeOnlyHandler(TypeName));
        }

        // Empty struct name special case
        StructHandlers.Add(TEXT(""), [](FStructProperty* StructProp, UObject* CDO) -> FString
        {
            return TEXT("{Type=(unnamed)}");
        });

        // Top 3 additional struct types (Phase 7) (6 types)
        TArray<FString> Phase7Types = {
            TEXT("LiveLinkSubjectName"), TEXT("FLiveLinkSubjectName"),
            TEXT("ControlRigSpline"), TEXT("FControlRigSpline"),
            TEXT("RFTransformOffset"), TEXT("FRFTransformOffset")
        };
        for (const FString& TypeName : Phase7Types)
        {
            StructHandlers.Add(TypeName, MakeTypeOnlyHandler(TypeName));
        }

        // Timer and GAS handle types (Phase 8) (6 types)
        TArray<FString> HandleTypes = {
            TEXT("TimerHandle"), TEXT("FTimerHandle"),
            TEXT("ActiveGameplayEffectHandle"), TEXT("FActiveGameplayEffectHandle"),
            TEXT("GameplayAbilitySpecHandle"), TEXT("FGameplayAbilitySpecHandle")
        };
        for (const FString& TypeName : HandleTypes)
        {
            StructHandlers.Add(TypeName, MakeTypeOnlyHandler(TypeName));
        }

        // Phase 9 Step 1: Plugin/Project-specific struct types (110 types = 55 × 2)
        TArray<FString> Phase9Step1Types = {
            TEXT("FilterOptionPerAxis"), TEXT("FFilterOptionPerAxis"),
            TEXT("AnimPresetParams_LoopAnim"), TEXT("FAnimPresetParams_LoopAnim"),
            TEXT("AnimPresetParams_StartAnim"), TEXT("FAnimPresetParams_StartAnim"),
            TEXT("AnimPresetParams_StopAnim"), TEXT("FAnimPresetParams_StopAnim"),
            TEXT("AnimPresetParams_IdleAnim"), TEXT("FAnimPresetParams_IdleAnim"),
            TEXT("ProcMeshData"), TEXT("FProcMeshData"),
            TEXT("Landmass_EdgeFalloffSettings"), TEXT("FLandmass_EdgeFalloffSettings"),
            TEXT("Landmass_EdgeFalloffNoiseSettings"), TEXT("FLandmass_EdgeFalloffNoiseSettings"),
            TEXT("LandmassBrush_ElevationMask_Settings"), TEXT("FLandmassBrush_ElevationMask_Settings"),
            TEXT("LandmassBrush_SlopeMask_Settings"), TEXT("FLandmassBrush_SlopeMask_Settings"),
            TEXT("Landmass_EmptyStruct"), TEXT("FLandmass_EmptyStruct"),
            TEXT("Landmass_Erosion_Keyframe"), TEXT("FLandmass_Erosion_Keyframe"),
            TEXT("Landmass_NoiseSettings"), TEXT("FLandmass_NoiseSettings"),
            TEXT("Landmass_NoisePass_WeightMapSettings"), TEXT("FLandmass_NoisePass_WeightMapSettings"),
            TEXT("Landmass_Quadtree_Data"), TEXT("FLandmass_Quadtree_Data"),
            TEXT("LandscapeBrushParameters"), TEXT("FLandscapeBrushParameters"),
            TEXT("LandscapeTextureData"), TEXT("FLandscapeTextureData"),
            TEXT("ErosionInterface"), TEXT("FErosionInterface"),
            TEXT("BiomeDefinition"), TEXT("FBiomeDefinition"),
            TEXT("BiomeGenerator"), TEXT("FBiomeGenerator"),
            TEXT("WaterBrushCache"), TEXT("FWaterBrushCache"),
            TEXT("CausticsWaveSpectrumSettings"), TEXT("FCausticsWaveSpectrumSettings"),
            TEXT("FluidForceImpulse"), TEXT("FFluidForceImpulse"),
            TEXT("FluidForceDynamic"), TEXT("FFluidForceDynamic"),
            TEXT("GerstnerWave"), TEXT("FGerstnerWave"),
            TEXT("StateTreeDelegateListener"), TEXT("FStateTreeDelegateListener"),
            TEXT("StateTreeBlueprintPropertyRef"), TEXT("FStateTreeBlueprintPropertyRef"),
            TEXT("StateTreeReference"), TEXT("FStateTreeReference"),
            TEXT("PCGDataCollection"), TEXT("FPCGDataCollection"),
            TEXT("PCGPoint"), TEXT("FPCGPoint"),
            TEXT("MovieSceneSequencePlaybackParams"), TEXT("FMovieSceneSequencePlaybackParams"),
            TEXT("MovieSceneSequencePlaybackSettings"), TEXT("FMovieSceneSequencePlaybackSettings"),
            TEXT("DataTableRowHandle"), TEXT("FDataTableRowHandle"),
            TEXT("GameplayEventData"), TEXT("FGameplayEventData"),
            TEXT("GameplayTagChangedEventWrapperSpecHandle"), TEXT("FGameplayTagChangedEventWrapperSpecHandle"),
            TEXT("GameplayTagQuery"), TEXT("FGameplayTagQuery"),
            TEXT("SmartObjectClaimHandle"), TEXT("FSmartObjectClaimHandle"),
            TEXT("RuntimeFloatCurve"), TEXT("FRuntimeFloatCurve"),
            TEXT("TransformFilter"), TEXT("FTransformFilter"),
            TEXT("LCSpawnSettings_Standard"), TEXT("FLCSpawnSettings_Standard"),
            TEXT("Struct_FogQualityVars"), TEXT("FStruct_FogQualityVars"),
            TEXT("LCCoreweakQTESettings"), TEXT("FLCCoreweakQTESettings"),
            TEXT("LCWeaponChangeContextData"), TEXT("FLCWeaponChangeContextData"),
            TEXT("LCWeaponDissolveContextData"), TEXT("FLCWeaponDissolveContextData"),
            TEXT("LCActionCameraDataStruct"), TEXT("FLCActionCameraDataStruct"),
            TEXT("LCSyncMontageSettings"), TEXT("FLCSyncMontageSettings"),
            TEXT("LCHurtIgnoreActionData"), TEXT("FLCHurtIgnoreActionData"),
            TEXT("LDDialogueParameters"), TEXT("FLDDialogueParameters"),
            TEXT("ONInstancedSpawnStateTreeReference"), TEXT("FONInstancedSpawnStateTreeReference"),
            TEXT("RFRandomVectorOffset"), TEXT("FRFRandomVectorOffset"),
            TEXT("RFRandomTransformOffset"), TEXT("FRFRandomTransformOffset"),
            TEXT("RFPDAWithOffset"), TEXT("FRFPDAWithOffset"),
            TEXT("RootModuleSettings"), TEXT("FRootModuleSettings"),
            TEXT("PDQueueHandle"), TEXT("FPDQueueHandle"),
            TEXT("CameraAssetReference"), TEXT("FCameraAssetReference"),
            TEXT("EVTakeQuestParameters"), TEXT("FEVTakeQuestParameters"),
            TEXT("CharacterInputState"), TEXT("FCharacterInputState"),
            TEXT("S_CharacterPropertiesForCamera"), TEXT("FS_CharacterPropertiesForCamera"),
            TEXT("S_GridMaterialParams"), TEXT("FS_GridMaterialParams"),
            TEXT("S_TraversalCheckResult"), TEXT("FS_TraversalCheckResult"),
            TEXT("HFAttractRotationParam"), TEXT("FHFAttractRotationParam"),
            TEXT("HFDynamicData_WorldSlomo"), TEXT("FHFDynamicData_WorldSlomo"),
            TEXT("CTWorldSlomoStruct"), TEXT("FCTWorldSlomoStruct")
        };
        for (const FString& TypeName : Phase9Step1Types)
        {
            StructHandlers.Add(TypeName, MakeTypeOnlyHandler(TypeName));
        }

        // Phase 9 Step 2B: Runtime/Complex types (8 types)
        TArray<FString> RuntimeComplexTypes = {
            TEXT("RandomStream"), TEXT("FRandomStream"),
            TEXT("GameplayEffectContextHandle"), TEXT("FGameplayEffectContextHandle"),
            TEXT("GameplayEffectSpecHandle"), TEXT("FGameplayEffectSpecHandle"),
            TEXT("SlateFontInfo"), TEXT("FSlateFontInfo")
        };
        for (const FString& TypeName : RuntimeComplexTypes)
        {
            StructHandlers.Add(TypeName, MakeTypeOnlyHandler(TypeName));
        }

        // Architecture: Static assertion to prevent regression (PR #1087)
        // Total handlers: 60 (Phase 1-8) + 110 (Phase 9 Step 1) + 8 (Phase 9 Step 2B) + 15 (additional) = 193
        const int32 ExpectedHandlerCount = 193;
        const int32 ActualHandlerCount = StructHandlers.Num();

        checkf(
            ActualHandlerCount == ExpectedHandlerCount,
            TEXT("TMap handler count mismatch! Expected: %d, Actual: %d. "
                 "If you added/removed Phase 9 types, update ExpectedHandlerCount in InitializeStructHandlers()."),
            ExpectedHandlerCount,
            ActualHandlerCount
        );

        bStructHandlersInitialized = true;  // Issue #1088: Use namespace-level variable
    }

    // Issue #561 Phase 1: Extract property default value from CDO (v5.0.0)
    FString ExtractPropertyDefaultValue(FProperty* Property, UObject* CDO)
    {
        if (!Property || !CDO)
        {
            return TEXT("");
        }

        // Issue #582: Track property type for coverage statistics (optimized)
        FString PropertyClassName = Property->GetClass()->GetName();
        FString ResultValue;
        bool bIsSupported = false;

        // 1. FObjectProperty (UAnimMontage*, UTexture2D*, etc.)
        if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Property))
        {
            UObject* DefaultValue = ObjProp->GetObjectPropertyValue_InContainer(CDO);
            if (DefaultValue)
            {
                // Return asset path (e.g., "/Game/Animations/AM_Attack.AM_Attack")
                ResultValue = DefaultValue->GetPathName();
            }
            else
            {
                // nullptr is a valid default value (Issue #1020 Phase 3 - Priority 1)
                ResultValue = TEXT("None");
            }
            bIsSupported = true;  // Always supported (both nullptr and valid object)
        }
        // 2. FSoftObjectProperty (FSoftObjectPtr)
        else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
        {
            FSoftObjectPtr SoftObjPtr = SoftObjProp->GetPropertyValue_InContainer(CDO);
            if (SoftObjPtr.IsValid())
            {
                ResultValue = SoftObjPtr.ToString();
                bIsSupported = true;
            }
            else
            {
                // Invalid or null soft object reference - still supported (Issue #1020 Phase 7 - Priority 20)
                ResultValue = TEXT("None");
                bIsSupported = true;
            }
        }
        // 3. FStructProperty (FGameplayTag, FVector, FRotator, etc.)
        else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
        {
            if (StructProp->Struct)
            {
                FString StructName = StructProp->Struct->GetName();

                // Issue #1020 M-1: Initialize TMap-based struct handlers once
                // Safety: Ensure handlers are initialized before lookup (PR #1087)
                // Issue #1088: Use namespace-level variable for test access
                if (!bStructHandlersInitialized)
                {
                    InitializeStructHandlers();
                }

                // O(1) lookup for registered struct types (replaces 328-line if-else chain)
                if (FStructHandler* Handler = StructHandlers.Find(StructName))
                {
                    ResultValue = (*Handler)(StructProp, CDO);
                    if (!ResultValue.IsEmpty())  // Handler succeeded
                    {
                        bIsSupported = true;
                    }
                }
                else
                {
                    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
                    Property->ExportTextItem_Direct(ResultValue, ValuePtr, nullptr, nullptr, PPF_None);
                    bIsSupported = false;  // Fallback path = unsupported

                    // Issue #1020 Phase 4: Track specific struct type name for unsupported structs
                    PropertyClassName = FString::Printf(TEXT("StructProperty:%s"), *StructName);
                }
            }
        }
        // 4. FNumericProperty (int32, float, bool)
        else if (FNumericProperty* NumProp = CastField<FNumericProperty>(Property))
        {
            void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
            Property->ExportTextItem_Direct(ResultValue, ValuePtr, nullptr, nullptr, PPF_None);
            bIsSupported = true;
        }
        // 5. FStrProperty (FString, FName)
        else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
        {
            FString* StrValue = StrProp->ContainerPtrToValuePtr<FString>(CDO);
            if (StrValue)
            {
                ResultValue = *StrValue;
                bIsSupported = true;
            }
        }
        else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
        {
            FName* NameValue = NameProp->ContainerPtrToValuePtr<FName>(CDO);
            if (NameValue)
            {
                ResultValue = NameValue->ToString();
                bIsSupported = true;
            }
        }
        // 6. FBoolProperty (Issue #1020 Phase 3 - Priority 3)
        else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
        {
            bool BoolValue = BoolProp->GetPropertyValue_InContainer(CDO);
            ResultValue = BoolValue ? TEXT("true") : TEXT("false");
            bIsSupported = true;
        }
        // 7. FEnumProperty (Issue #1020 Phase 3 - Priority 6)
        else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
        {
            void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
            if (ValuePtr)
            {
                UEnum* EnumClass = EnumProp->GetEnum();
                if (EnumClass)
                {
                    int64 EnumValue = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
                    ResultValue = EnumClass->GetNameStringByValue(EnumValue);
                    bIsSupported = true;
                }
            }
        }
        // 8. FTextProperty (Issue #1020 Phase 3 - Priority 7)
        else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
        {
            FText* TextValue = TextProp->ContainerPtrToValuePtr<FText>(CDO);
            if (TextValue)
            {
                ResultValue = TextValue->ToString();
                bIsSupported = true;
            }
        }
        // 9. FArrayProperty (Issue #1020 Phase 3 - Priority 4)
        else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
        {
            FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(CDO));
            int32 ArraySize = ArrayHelper.Num();

            // Return array size and first few elements
            TArray<FString> ElementStrings;
            int32 MaxElements = FMath::Min(ArraySize, 5);  // Show first 5 elements

            for (int32 i = 0; i < MaxElements; ++i)
            {
                void* ElementPtr = ArrayHelper.GetRawPtr(i);
                FString ElementValue;
                ArrayProp->Inner->ExportTextItem_Direct(ElementValue, ElementPtr, nullptr, nullptr, PPF_None);
                ElementStrings.Add(ElementValue);
            }

            if (ArraySize > MaxElements)
            {
                ResultValue = FString::Printf(TEXT("[%d elements: %s, ...]"), ArraySize, *FString::Join(ElementStrings, TEXT(", ")));
            }
            else if (ArraySize > 0)
            {
                ResultValue = FString::Printf(TEXT("[%s]"), *FString::Join(ElementStrings, TEXT(", ")));
            }
            else
            {
                ResultValue = TEXT("[]");
            }
            bIsSupported = true;
        }
        // 10. FMapProperty (Issue #1020 Phase 3 - Priority 5)
        else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
        {
            FScriptMapHelper MapHelper(MapProp, MapProp->ContainerPtrToValuePtr<void>(CDO));
            int32 MapSize = MapHelper.Num();

            // Return map size and first few key-value pairs
            TArray<FString> PairStrings;
            int32 MaxPairs = FMath::Min(MapSize, 3);  // Show first 3 pairs
            int32 PairCount = 0;

            for (int32 i = 0; i < MapHelper.GetMaxIndex() && PairCount < MaxPairs; ++i)
            {
                if (MapHelper.IsValidIndex(i))
                {
                    void* KeyPtr = MapHelper.GetKeyPtr(i);
                    void* ValuePtr = MapHelper.GetValuePtr(i);

                    FString KeyStr, ValueStr;
                    MapProp->KeyProp->ExportTextItem_Direct(KeyStr, KeyPtr, nullptr, nullptr, PPF_None);
                    MapProp->ValueProp->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);

                    PairStrings.Add(FString::Printf(TEXT("%s: %s"), *KeyStr, *ValueStr));
                    PairCount++;
                }
            }

            if (MapSize > MaxPairs)
            {
                ResultValue = FString::Printf(TEXT("{%d pairs: %s, ...}"), MapSize, *FString::Join(PairStrings, TEXT(", ")));
            }
            else if (MapSize > 0)
            {
                ResultValue = FString::Printf(TEXT("{%s}"), *FString::Join(PairStrings, TEXT(", ")));
            }
            else
            {
                ResultValue = TEXT("{}");
            }
            bIsSupported = true;
        }
        // 11. FSetProperty (Issue #1020 Phase 9 Step 2C - Priority 30)
        else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
        {
            FScriptSetHelper SetHelper(SetProp, SetProp->ContainerPtrToValuePtr<void>(CDO));
            int32 SetSize = SetHelper.Num();

            TArray<FString> ElementStrings;
            int32 MaxElements = FMath::Min(SetSize, 5);

            for (int32 i = 0; i < SetHelper.GetMaxIndex() && ElementStrings.Num() < MaxElements; ++i)
            {
                if (SetHelper.IsValidIndex(i))
                {
                    void* ElementPtr = SetHelper.GetElementPtr(i);
                    FString ElementValue;
                    SetProp->ElementProp->ExportTextItem_Direct(ElementValue, ElementPtr, nullptr, nullptr, PPF_None);
                    ElementStrings.Add(ElementValue);
                }
            }

            if (SetSize > MaxElements)
            {
                ResultValue = FString::Printf(TEXT("Set of %d: {%s, ...}"), SetSize, *FString::Join(ElementStrings, TEXT(", ")));
            }
            else if (SetSize > 0)
            {
                ResultValue = FString::Printf(TEXT("Set: {%s}"), *FString::Join(ElementStrings, TEXT(", ")));
            }
            else
            {
                ResultValue = TEXT("Set: {}");
            }
            bIsSupported = true;
        }
        // Unknown property type (unsupported)
        else
        {
            bIsSupported = false;
        }

        // Issue #582: Thread-safe statistics tracking (single TMap access)
        {
            FScopeLock Lock(&PropertyTypeStatsLock);
            if (bIsSupported)
            {
                SupportedPropertyTypes.FindOrAdd(PropertyClassName)++;
            }
            else
            {
                UnsupportedPropertyTypes.FindOrAdd(PropertyClassName)++;
            }
        }

        return ResultValue;
    }

    // Issue #584 Phase 2A: Extract Input Action to Ability bindings from Data Assets (v6.0.0)
    void ExtractInputActionBindings(IAssetRegistry& AssetRegistry, TArray<TSharedPtr<FJsonValue>>& OutBindings)
    {
#if !WITH_NARSHAMCP_ENHANCEDINPUT
        TSharedPtr<FJsonObject> Unavailable = MakeShared<FJsonObject>();
        Unavailable->SetBoolField(TEXT("success"), false);
        Unavailable->SetStringField(TEXT("error_code"), TEXT("enhanced_input_plugin_unavailable"));
        Unavailable->SetStringField(TEXT("hint"), TEXT("Enable EnhancedInput in your .uproject, then rebuild."));
        OutBindings.Add(MakeShared<FJsonValueObject>(Unavailable));
        return;
#else
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Starting Input Action binding extraction..."));

        // Step 1: Find all Data Asset classes that might contain input bindings
        // We search for UPrimaryDataAsset and UDataAsset subclasses
        FARFilter DataAssetFilter;
        DataAssetFilter.ClassPaths.Add(UPrimaryDataAsset::StaticClass()->GetClassPathName());
        DataAssetFilter.ClassPaths.Add(UDataAsset::StaticClass()->GetClassPathName());
        DataAssetFilter.bRecursiveClasses = true;

        TArray<FAssetData> DataAssets;
        AssetRegistry.GetAssets(DataAssetFilter, DataAssets);

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Found %d Data Assets to scan"), DataAssets.Num());

        // Step 2: Pattern detection - VariantExperience architecture
        // Look for:
        // 1. VEEnhancedInputBindConfig (InputAction → InputId mapping)
        // 2. VEAbilitySet (InputId → Ability mapping)
        TMap<int32, FString> InputIdToInputActionMap;  // InputId → IA path
        TMap<int32, FString> InputIdToAbilityMap;      // InputId → GA path
        FString InputConfigAssetPath;
        FString AbilitySetAssetPath;

        for (const FAssetData& AssetData : DataAssets)
        {
            FString AssetClassName = AssetData.AssetClassPath.ToString();
            FString AssetName = AssetData.AssetName.ToString();

            // Pattern 1: VEEnhancedInputBindConfig (IA → InputId)
            if (AssetClassName.Contains(TEXT("VEEnhancedInputBindConfig")) ||
                AssetName.Contains(TEXT("InputBindConfig")) ||
                AssetName.Contains(TEXT("InputConfig")))
            {
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Found potential InputBindConfig: %s"), *AssetData.GetObjectPathString());

                // Load the asset to inspect properties
                UObject* LoadedAsset = AssetData.GetAsset();
                if (!LoadedAsset)
                {
                    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584] Failed to load asset: %s"), *AssetData.GetObjectPathString());
                    continue;
                }

                // Use reflection to find TMap<UInputAction*, int32> property
                UClass* AssetClass = LoadedAsset->GetClass();
                for (TFieldIterator<FProperty> PropIt(AssetClass); PropIt; ++PropIt)
                {
                    FProperty* Property = *PropIt;
                    FMapProperty* MapProp = CastField<FMapProperty>(Property);

                    if (!MapProp)
                    {
                        continue;
                    }

                    // Check if this is TMap<UInputAction*, int32>
                    FObjectProperty* KeyProp = CastField<FObjectProperty>(MapProp->KeyProp);
                    FIntProperty* ValueProp = CastField<FIntProperty>(MapProp->ValueProp);

                    if (KeyProp && ValueProp)
                    {
                        // Check if key type is UInputAction or subclass
                        UClass* KeyClass = KeyProp->PropertyClass;
                        if (KeyClass && KeyClass->IsChildOf(UInputAction::StaticClass()))
                        {
                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Found InputAction→InputId map: %s.%s"),
                                   *AssetName, *Property->GetName());

                            // Extract the map contents
                            FScriptMapHelper MapHelper(MapProp, MapProp->ContainerPtrToValuePtr<void>(LoadedAsset));
                            int32 NumElements = MapHelper.Num();

                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Map contains %d entries"), NumElements);

                            for (int32 i = 0; i < NumElements; ++i)
                            {
                                if (!MapHelper.IsValidIndex(i))
                                {
                                    continue;
                                }

                                uint8* PairPtr = MapHelper.GetPairPtr(i);
                                UInputAction* InputAction = *reinterpret_cast<UInputAction**>(PairPtr);
                                int32 InputId = *reinterpret_cast<int32*>(PairPtr + MapProp->MapLayout.ValueOffset);

                                if (InputAction)
                                {
                                    FString InputActionPath = InputAction->GetPathName();
                                    InputIdToInputActionMap.Add(InputId, InputActionPath);

                                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584]   InputId=%d → IA=%s"),
                                           InputId, *InputAction->GetName());
                                }
                            }

                            InputConfigAssetPath = AssetData.GetObjectPathString();
                        }
                    }
                }
            }

            // Pattern 2: VEAbilitySet (InputId → Ability)
            if (AssetClassName.Contains(TEXT("VEAbilitySet")) ||
                AssetClassName.Contains(TEXT("AbilitySet")) ||
                AssetName.Contains(TEXT("AbilitySet")))
            {
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Found potential AbilitySet: %s"), *AssetData.GetObjectPathString());

                UObject* LoadedAsset = AssetData.GetAsset();
                if (!LoadedAsset)
                {
                    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584] Failed to load asset: %s"), *AssetData.GetObjectPathString());
                    continue;
                }

                // Look for TArray<FVEAbilitySet_GameplayAbility> or similar
                UClass* AssetClass = LoadedAsset->GetClass();
                for (TFieldIterator<FProperty> PropIt(AssetClass); PropIt; ++PropIt)
                {
                    FProperty* Property = *PropIt;
                    FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);

                    if (!ArrayProp)
                    {
                        continue;
                    }

                    // Check if inner type is a struct containing ability info
                    FStructProperty* InnerStructProp = CastField<FStructProperty>(ArrayProp->Inner);
                    if (InnerStructProp && InnerStructProp->Struct)
                    {
                        FString StructName = InnerStructProp->Struct->GetName();

                        // Look for structs with "Ability" in name (e.g., FVEAbilitySet_GameplayAbility)
                        if (StructName.Contains(TEXT("Ability")) || StructName.Contains(TEXT("Granted")))
                        {
                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Found ability array: %s.%s (Struct: %s)"),
                                   *AssetName, *Property->GetName(), *StructName);

                            // Extract array contents
                            FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(LoadedAsset));
                            int32 NumElements = ArrayHelper.Num();

                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Array contains %d entries"), NumElements);

                            // Find InputId and Ability fields in the struct
                            FProperty* InputIdField = nullptr;
                            FProperty* AbilityClassField = nullptr;

                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG] Struct fields in %s:"), *StructName);
                            for (TFieldIterator<FProperty> StructFieldIt(InnerStructProp->Struct); StructFieldIt; ++StructFieldIt)
                            {
                                FProperty* StructField = *StructFieldIt;
                                FString FieldName = StructField->GetName();
                                FString FieldType = StructField->GetClass()->GetName();

                                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG]   Field: %s (Type: %s)"), *FieldName, *FieldType);

                                if (FieldName.Contains(TEXT("InputId")) || FieldName == TEXT("InputID"))
                                {
                                    InputIdField = StructField;
                                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG]   ✓ Found InputId field"));
                                }
                                else if (FieldName == TEXT("Ability") ||
                                         (FieldName.Contains(TEXT("Ability")) &&
                                          (StructField->IsA<FClassProperty>() || StructField->IsA<FSoftClassProperty>())))
                                {
                                    AbilityClassField = StructField;
                                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG]   ✓ Found Ability field: %s"), *FieldName);
                                }
                            }

                            if (!InputIdField)
                            {
                                UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG] ❌ InputId field NOT found in struct"));
                            }
                            if (!AbilityClassField)
                            {
                                UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG] ❌ Ability field NOT found in struct"));
                            }

                            // Extract InputId → Ability mappings
                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG] Extracting from %d array elements..."), NumElements);
                            for (int32 i = 0; i < NumElements; ++i)
                            {
                                uint8* ElementPtr = ArrayHelper.GetRawPtr(i);

                                int32 InputId = 0;
                                FString AbilityClassPath;

                                // Extract InputId
                                if (InputIdField)
                                {
                                    if (FIntProperty* IntProp = CastField<FIntProperty>(InputIdField))
                                    {
                                        InputId = IntProp->GetPropertyValue_InContainer(ElementPtr);
                                        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] Extracted InputId=%d"), i, InputId);
                                    }
                                    else
                                    {
                                        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] InputIdField is not FIntProperty (Type: %s)"), i, *InputIdField->GetClass()->GetName());
                                    }
                                }
                                else
                                {
                                    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] InputIdField is nullptr"), i);
                                }

                                // Extract Ability class
                                if (AbilityClassField)
                                {
                                    if (FClassProperty* ClassProp = CastField<FClassProperty>(AbilityClassField))
                                    {
                                        UClass* AbilityClass = Cast<UClass>(ClassProp->GetObjectPropertyValue_InContainer(ElementPtr));
                                        if (AbilityClass)
                                        {
                                            AbilityClassPath = AbilityClass->GetPathName();
                                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] Extracted Ability (FClassProperty)=%s"), i, *AbilityClassPath);
                                        }
                                        else
                                        {
                                            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] Ability class is nullptr"), i);
                                        }
                                    }
                                    else if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(AbilityClassField))
                                    {
                                        FSoftObjectPtr SoftClass = SoftClassProp->GetPropertyValue_InContainer(ElementPtr);
                                        if (SoftClass.IsValid())
                                        {
                                            AbilityClassPath = SoftClass.ToString();
                                            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] Extracted Ability (FSoftClassProperty)=%s"), i, *AbilityClassPath);
                                        }
                                        else
                                        {
                                            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] Soft class is invalid"), i);
                                        }
                                    }
                                    else
                                    {
                                        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] AbilityClassField is not FClassProperty or FSoftClassProperty (Type: %s)"), i, *AbilityClassField->GetClass()->GetName());
                                    }
                                }
                                else
                                {
                                    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] AbilityClassField is nullptr"), i);
                                }

                                if (InputId > 0 && !AbilityClassPath.IsEmpty())
                                {
                                    InputIdToAbilityMap.Add(InputId, AbilityClassPath);

                                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584]   InputId=%d → Ability=%s"),
                                           InputId, *AbilityClassPath);
                                }
                                else
                                {
                                    UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584][DEBUG]   [%d] Skipped: InputId=%d, AbilityPath='%s'"), i, InputId, *AbilityClassPath);
                                }
                            }

                            AbilitySetAssetPath = AssetData.GetObjectPathString();
                        }
                    }
                }
            }
        }

        // Step 3: Cross-reference InputId to create IA → GA bindings
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Cross-referencing InputIds..."));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584]   InputActions: %d, Abilities: %d"),
               InputIdToInputActionMap.Num(), InputIdToAbilityMap.Num());

        int32 BindingsCreated = 0;

        for (const auto& Pair : InputIdToInputActionMap)
        {
            int32 InputId = Pair.Key;
            FString InputActionPath = Pair.Value;

            // Find matching ability by InputId
            if (InputIdToAbilityMap.Contains(InputId))
            {
                FString AbilityClassPath = InputIdToAbilityMap[InputId];

                // Create binding JSON
                TSharedPtr<FJsonObject> BindingJson = MakeShareable(new FJsonObject);

                // Extract asset names from paths
                FString InputActionName = FPaths::GetBaseFilename(InputActionPath);
                FString AbilityClassName = FPaths::GetBaseFilename(AbilityClassPath);

                BindingJson->SetStringField(TEXT("input_action"), InputActionName);
                BindingJson->SetStringField(TEXT("input_action_path"), InputActionPath);
                BindingJson->SetStringField(TEXT("ability_class"), AbilityClassName);
                BindingJson->SetStringField(TEXT("ability_path"), AbilityClassPath);
                BindingJson->SetNumberField(TEXT("input_id"), InputId);
                BindingJson->SetStringField(TEXT("source"), TEXT("data_asset"));
                BindingJson->SetStringField(TEXT("detection_method"), TEXT("data_asset_static"));
                BindingJson->SetNumberField(TEXT("confidence"), 0.95);  // High confidence for static data
                BindingJson->SetStringField(TEXT("data_asset_path"), InputConfigAssetPath);
                BindingJson->SetStringField(TEXT("ability_set_path"), AbilitySetAssetPath);

                OutBindings.Add(MakeShareable(new FJsonValueObject(BindingJson)));
                BindingsCreated++;

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Created binding: %s → %s (InputId=%d)"),
                       *InputActionName, *AbilityClassName, InputId);
            }
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584] Extraction complete: %d bindings created"), BindingsCreated);
#endif
    }

    // ============================================================================
    // Issue #584 Phase 2B: Blueprint Node Tracing (v6.0.0)
    // ============================================================================

    /**
     * Helper: Check if node is an Input Action event node
     */
    bool IsInputActionEventNode(UK2Node* Node)
    {
        if (!Node)
        {
            return false;
        }

        // Check node class name for Input Action patterns
        FString NodeClassName = Node->GetClass()->GetName();
        FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

        // Pattern 1: UK2Node_InputAction (UE4 legacy)
        if (NodeClassName.Contains(TEXT("InputAction")))
        {
            return true;
        }

        // Pattern 2: UK2Node_EnhancedInputAction (UE5)
        if (NodeClassName.Contains(TEXT("EnhancedInputAction")))
        {
            return true;
        }

        // Pattern 3: Custom event nodes with Input Action reference
        if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
        {
            // Check if event has InputAction property
            for (UEdGraphPin* Pin : EventNode->Pins)
            {
                if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
                {
                    if (Pin->PinType.PinSubCategoryObject.IsValid())
                    {
                        UClass* ObjectClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
#if WITH_NARSHAMCP_ENHANCEDINPUT
                        if (ObjectClass && ObjectClass->IsChildOf(UInputAction::StaticClass()))
                        {
                            return true;
                        }
#endif
                    }
                }
            }
        }

        return false;
    }

    /**
     * Helper: Extract Input Action from event node
     */
    FString ExtractInputActionFromNode(UK2Node* Node)
    {
        if (!Node)
        {
            return FString();
        }

        // Method 1: Check node properties for InputAction reference
        FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();

        // Try to extract InputAction name from node title
        // Example: "InputAction IA_Jump" → "IA_Jump"
        if (NodeTitle.Contains(TEXT("InputAction")))
        {
            int32 LastSpaceIndex;
            if (NodeTitle.FindLastChar(' ', LastSpaceIndex))
            {
                FString InputActionName = NodeTitle.RightChop(LastSpaceIndex + 1);
                return InputActionName;
            }
        }

        // Method 2: Check pins for InputAction object reference
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
            {
#if WITH_NARSHAMCP_ENHANCEDINPUT
                if (Pin->DefaultObject && Pin->DefaultObject->IsA(UInputAction::StaticClass()))
                {
                    return Pin->DefaultObject->GetName();
                }
#endif
            }
        }

        return FString();
    }

    /**
     * Helper: Check if node is ActivateAbility function call
     */
    bool IsActivateAbilityCall(UK2Node_CallFunction* FunctionNode)
    {
        if (!FunctionNode || !FunctionNode->FunctionReference.GetMemberName().IsValid())
        {
            return false;
        }

        FName FunctionName = FunctionNode->FunctionReference.GetMemberName();
        FString FunctionNameStr = FunctionName.ToString();

        // Pattern 1: UAbilitySystemComponent::ActivateAbility
        if (FunctionNameStr.Contains(TEXT("ActivateAbility")))
        {
            return true;
        }

        // Pattern 2: UAbilitySystemComponent::TryActivateAbility
        if (FunctionNameStr.Contains(TEXT("TryActivateAbility")))
        {
            return true;
        }

        // Pattern 3: UAbilitySystemComponent::ActivateAbilityFromClass
        if (FunctionNameStr.Contains(TEXT("ActivateAbilityFromClass")))
        {
            return true;
        }

        return false;
    }

    /**
     * Helper: Extract Ability class from ActivateAbility call node
     */
    FString ExtractAbilityClassFromCall(UK2Node_CallFunction* FunctionNode)
    {
        if (!FunctionNode)
        {
            return FString();
        }

        // Find "AbilityClass" or "Ability" parameter pin
        for (UEdGraphPin* Pin : FunctionNode->Pins)
        {
            if (!Pin)
            {
                continue;
            }

            FString PinName = Pin->PinName.ToString();

            // Pattern 1: AbilityClass parameter (ActivateAbilityFromClass)
            if (PinName.Contains(TEXT("AbilityClass")) || PinName.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
            {
                // Check for connected pin (variable reference)
                if (Pin->LinkedTo.Num() > 0)
                {
                    UEdGraphPin* SourcePin = Pin->LinkedTo[0];
                    if (SourcePin && SourcePin->GetOwningNode())
                    {
                        UK2Node* SourceNode = Cast<UK2Node>(SourcePin->GetOwningNode());
                        if (SourceNode)
                        {
                            // Try to extract class from variable get node
                            FString NodeTitle = SourceNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
                            // This is a best-effort extraction, may not always work
                            return NodeTitle;
                        }
                    }
                }

                // Check for default value (direct class reference)
                if (Pin->DefaultObject)
                {
                    UClass* AbilityClass = Cast<UClass>(Pin->DefaultObject);
                    if (AbilityClass)
                    {
                        return AbilityClass->GetName();
                    }
                }

                // Check for default value as text
                if (!Pin->DefaultValue.IsEmpty())
                {
                    return Pin->DefaultValue;
                }
            }

            // Pattern 2: Ability parameter (handle reference)
            if (PinName.Contains(TEXT("Ability")) && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
            {
                if (Pin->DefaultObject)
                {
                    UClass* AbilityClass = Cast<UClass>(Pin->DefaultObject);
                    if (AbilityClass)
                    {
                        return AbilityClass->GetName();
                    }
                }
            }
        }

        return FString();
    }

    /**
     * Helper: Trace execution flow from start node to find ActivateAbility calls
     * Reuses Issue #107 infrastructure (delegate tracking) for execution flow tracing
     */
    void TraceExecutionToActivateAbility(
        UK2Node* StartNode,
        TSet<UK2Node*>& VisitedNodes,
        TArray<UK2Node_CallFunction*>& OutActivateAbilityCalls,
        int32 MaxDepth = 10
    )
    {
        if (!StartNode || MaxDepth <= 0)
        {
            return;
        }

        // Avoid infinite loops
        if (VisitedNodes.Contains(StartNode))
        {
            return;
        }
        VisitedNodes.Add(StartNode);

        // Check if this node is an ActivateAbility call
        if (UK2Node_CallFunction* FunctionNode = Cast<UK2Node_CallFunction>(StartNode))
        {
            if (IsActivateAbilityCall(FunctionNode))
            {
                OutActivateAbilityCalls.Add(FunctionNode);
                return;  // Found a binding, stop tracing this path
            }
        }

        // Follow execution pins (Then, Exec, etc.)
        for (UEdGraphPin* Pin : StartNode->Pins)
        {
            if (!Pin || Pin->Direction != EGPD_Output)
            {
                continue;
            }

            // Only follow execution pins
            if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
            {
                continue;
            }

            // Trace to connected nodes
            for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
            {
                if (LinkedPin && LinkedPin->GetOwningNode())
                {
                    UK2Node* NextNode = Cast<UK2Node>(LinkedPin->GetOwningNode());
                    if (NextNode)
                    {
                        TraceExecutionToActivateAbility(NextNode, VisitedNodes, OutActivateAbilityCalls, MaxDepth - 1);
                    }
                }
            }
        }
    }

    /**
     * Extract Input Action to Ability bindings from Blueprint graphs (Issue #584 Phase 2B)
     */
    void ExtractInputActionBindingsFromBlueprints(
        const TArray<UBlueprint*>& Blueprints,
        TArray<TSharedPtr<FJsonValue>>& OutBindings
    )
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2B] Starting Blueprint node tracing for Input Action bindings..."));

        int32 BindingsCreated = 0;

        for (UBlueprint* Blueprint : Blueprints)
        {
            if (!Blueprint)
            {
                continue;
            }

            FString BlueprintName = Blueprint->GetName();
            FString BlueprintPath = Blueprint->GetPathName();

            // Iterate through all graphs in the Blueprint
            TArray<UEdGraph*> AllGraphs;
            Blueprint->GetAllGraphs(AllGraphs);

            for (UEdGraph* Graph : AllGraphs)
            {
                if (!Graph)
                {
                    continue;
                }

                FString GraphName = Graph->GetName();

                // Find Input Action event nodes
                for (UEdGraphNode* Node : Graph->Nodes)
                {
                    UK2Node* K2Node = Cast<UK2Node>(Node);
                    if (!K2Node)
                    {
                        continue;
                    }

                    // Check if this is an Input Action event node
                    if (!IsInputActionEventNode(K2Node))
                    {
                        continue;
                    }

                    // Extract Input Action name from node
                    FString InputActionName = ExtractInputActionFromNode(K2Node);
                    if (InputActionName.IsEmpty())
                    {
                        UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584-Phase2B] Could not extract InputAction name from node in %s::%s"),
                               *BlueprintName, *GraphName);
                        continue;
                    }

                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2B] Found InputAction event: %s in %s::%s"),
                           *InputActionName, *BlueprintName, *GraphName);

                    // Trace execution flow to find ActivateAbility calls
                    TSet<UK2Node*> VisitedNodes;
                    TArray<UK2Node_CallFunction*> ActivateAbilityCalls;
                    TraceExecutionToActivateAbility(K2Node, VisitedNodes, ActivateAbilityCalls);

                    // Create bindings for each ActivateAbility call found
                    for (UK2Node_CallFunction* ActivateCall : ActivateAbilityCalls)
                    {
                        FString AbilityClassName = ExtractAbilityClassFromCall(ActivateCall);
                        if (AbilityClassName.IsEmpty())
                        {
                            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584-Phase2B] Could not extract Ability class from ActivateAbility call"));
                            continue;
                        }

                        // Remove common prefixes/suffixes for cleaner names
                        AbilityClassName.RemoveFromStart(TEXT("Default__"));
                        AbilityClassName.RemoveFromEnd(TEXT("_C"));

                        // Create binding JSON object
                        TSharedPtr<FJsonObject> BindingJson = MakeShareable(new FJsonObject);
                        BindingJson->SetStringField(TEXT("input_action"), InputActionName);
                        BindingJson->SetStringField(TEXT("input_action_path"), FString::Printf(TEXT("/Game/.../Input/%s"), *InputActionName));
                        BindingJson->SetStringField(TEXT("ability_class"), AbilityClassName);
                        BindingJson->SetStringField(TEXT("ability_path"), FString::Printf(TEXT("/Game/.../Abilities/%s"), *AbilityClassName));
                        BindingJson->SetStringField(TEXT("source"), TEXT("blueprint"));
                        BindingJson->SetStringField(TEXT("detection_method"), TEXT("blueprint_node_trace"));
                        BindingJson->SetNumberField(TEXT("confidence"), 0.75);  // Medium-high confidence for Blueprint tracing
                        BindingJson->SetStringField(TEXT("blueprint_path"), BlueprintPath);
                        BindingJson->SetStringField(TEXT("graph_name"), GraphName);

                        OutBindings.Add(MakeShareable(new FJsonValueObject(BindingJson)));
                        BindingsCreated++;

                        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2B] Created binding: %s → %s (Blueprint: %s)"),
                               *InputActionName, *AbilityClassName, *BlueprintName);
                    }
                }
            }
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2B] Blueprint node tracing complete: %d bindings created"), BindingsCreated);
    }

    // ============================================================================
    // Issue #584 Phase 2F: Validation + Confidence Scoring (v6.0.0)
    // ============================================================================

    void ValidateInputActionBindings(
        IAssetRegistry& AssetRegistry,
        TArray<TSharedPtr<FJsonValue>>& InOutBindings,
        TSharedPtr<FJsonObject>& OutValidationStats
    )
    {
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F] Starting binding validation..."));

        // Initialize statistics
        int32 TotalBindings = InOutBindings.Num();
        int32 ValidatedBindings = 0;
        int32 FailedBindings = 0;
        int32 DuplicateBindings = 0;

        // Step 1: Asset Verification
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F] Step 1: Asset verification..."));

        for (int32 i = InOutBindings.Num() - 1; i >= 0; --i)
        {
            TSharedPtr<FJsonValue> BindingValue = InOutBindings[i];
            TSharedPtr<FJsonObject> Binding = BindingValue->AsObject();

            if (!Binding.IsValid())
            {
                continue;
            }

            FString InputActionPath = Binding->GetStringField(TEXT("input_action_path"));
            FString AbilityPath = Binding->GetStringField(TEXT("ability_path"));

            bool bInputActionExists = false;
            bool bAbilityExists = false;

            // Check InputAction existence (skip if placeholder path)
            if (!InputActionPath.Contains(TEXT("...")))
            {
                // Remove "_C" suffix from Blueprint generated class paths
                FString CleanInputActionPath = InputActionPath;
                if (CleanInputActionPath.EndsWith(TEXT("_C")))
                {
                    CleanInputActionPath = CleanInputActionPath.LeftChop(2);
                }
                FAssetData InputActionAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(CleanInputActionPath));
                bInputActionExists = InputActionAsset.IsValid();
            }
            else
            {
                // Placeholder path from Phase 2B - mark as unknown
                bInputActionExists = true;  // Don't fail on placeholder paths
            }

            // Check Ability existence (skip if placeholder path)
            if (!AbilityPath.Contains(TEXT("...")))
            {
                // Remove "_C" suffix from Blueprint generated class paths
                FString CleanAbilityPath = AbilityPath;
                if (CleanAbilityPath.EndsWith(TEXT("_C")))
                {
                    CleanAbilityPath = CleanAbilityPath.LeftChop(2);
                }
                FAssetData AbilityAsset = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(CleanAbilityPath));
                bAbilityExists = AbilityAsset.IsValid();
            }
            else
            {
                // Placeholder path from Phase 2B - mark as unknown
                bAbilityExists = true;  // Don't fail on placeholder paths
            }

            // Mark validation result
            if (bInputActionExists && bAbilityExists)
            {
                Binding->SetBoolField(TEXT("validation_passed"), true);
                ValidatedBindings++;
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   ✓ Validated: %s → %s"),
                       *Binding->GetStringField(TEXT("input_action")),
                       *Binding->GetStringField(TEXT("ability_class")));
            }
            else
            {
                Binding->SetBoolField(TEXT("validation_passed"), false);
                Binding->SetBoolField(TEXT("validation_failed"), true);
                FailedBindings++;
                UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][Issue584-Phase2F]   ✗ Failed: %s → %s (IA exists: %s, Ability exists: %s)"),
                       *Binding->GetStringField(TEXT("input_action")),
                       *Binding->GetStringField(TEXT("ability_class")),
                       bInputActionExists ? TEXT("Yes") : TEXT("No"),
                       bAbilityExists ? TEXT("Yes") : TEXT("No"));
            }
        }

        // Step 2: Deduplication
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F] Step 2: Deduplication..."));

        // Group bindings by (InputAction, Ability) pair
        TMap<FString, TArray<TSharedPtr<FJsonValue>>> BindingGroups;

        for (TSharedPtr<FJsonValue> BindingValue : InOutBindings)
        {
            TSharedPtr<FJsonObject> Binding = BindingValue->AsObject();
            if (!Binding.IsValid())
            {
                continue;
            }

            FString InputActionPath = Binding->GetStringField(TEXT("input_action_path"));
            FString AbilityPath = Binding->GetStringField(TEXT("ability_path"));
            FString GroupKey = FString::Printf(TEXT("%s|%s"), *InputActionPath, *AbilityPath);

            BindingGroups.FindOrAdd(GroupKey).Add(BindingValue);
        }

        // Process duplicates: Keep highest confidence binding
        TArray<TSharedPtr<FJsonValue>> DeduplicatedBindings;

        for (const auto& Pair : BindingGroups)
        {
            const TArray<TSharedPtr<FJsonValue>>& Group = Pair.Value;

            if (Group.Num() == 1)
            {
                // No duplicates
                DeduplicatedBindings.Add(Group[0]);
            }
            else
            {
                // Multiple sources found same binding
                DuplicateBindings += (Group.Num() - 1);

                // Find highest confidence binding
                TSharedPtr<FJsonValue> BestBinding = Group[0];
                double HighestConfidence = Group[0]->AsObject()->GetNumberField(TEXT("confidence"));

                TArray<TSharedPtr<FJsonValue>> DuplicateSources;

                for (int32 i = 0; i < Group.Num(); ++i)
                {
                    TSharedPtr<FJsonObject> Binding = Group[i]->AsObject();
                    double Confidence = Binding->GetNumberField(TEXT("confidence"));

                    if (Confidence > HighestConfidence)
                    {
                        HighestConfidence = Confidence;
                        BestBinding = Group[i];
                    }

                    // Track all detection methods
                    DuplicateSources.Add(MakeShareable(new FJsonValueString(Binding->GetStringField(TEXT("detection_method")))));
                }

                // Add duplicate_sources array to best binding
                BestBinding->AsObject()->SetArrayField(TEXT("duplicate_sources"), DuplicateSources);
                DeduplicatedBindings.Add(BestBinding);

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   Deduplicated: %s → %s (kept %.2f confidence, removed %d duplicates)"),
                       *BestBinding->AsObject()->GetStringField(TEXT("input_action")),
                       *BestBinding->AsObject()->GetStringField(TEXT("ability_class")),
                       HighestConfidence,
                       Group.Num() - 1);
            }
        }

        // Replace original array with deduplicated array
        InOutBindings = DeduplicatedBindings;

        // Step 3: Generate statistics
        int32 FinalBindings = InOutBindings.Num();

        OutValidationStats = MakeShareable(new FJsonObject);
        OutValidationStats->SetNumberField(TEXT("total_bindings"), TotalBindings);
        OutValidationStats->SetNumberField(TEXT("validated_bindings"), ValidatedBindings);
        OutValidationStats->SetNumberField(TEXT("failed_bindings"), FailedBindings);
        OutValidationStats->SetNumberField(TEXT("duplicate_bindings"), DuplicateBindings);
        OutValidationStats->SetNumberField(TEXT("final_bindings"), FinalBindings);

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F] Validation complete:"));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   Total: %d"), TotalBindings);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   Validated: %d"), ValidatedBindings);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   Failed: %d"), FailedBindings);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   Duplicates removed: %d"), DuplicateBindings);
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][Issue584-Phase2F]   Final: %d"), FinalBindings);
    }

    // Issue #582: Log Property Type Coverage Statistics (v5.1.0)
    void LogPropertyTypeCoverage()
    {
        int32 TotalSupported = 0;
        int32 TotalUnsupported = 0;

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("============================================================"));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  Property Type Coverage Statistics (Issue #582)"));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("============================================================"));

        // Thread-safe: Copy TMaps for processing
        TMap<FString, int32> SupportedCopy;
        TMap<FString, int32> UnsupportedCopy;
        {
            FScopeLock Lock(&PropertyTypeStatsLock);
            SupportedCopy = SupportedPropertyTypes;
            UnsupportedCopy = UnsupportedPropertyTypes;
        }

        // Log supported property types
        if (SupportedCopy.Num() > 0)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("Supported Property Types:"));

            // Sort by occurrence count (descending)
            TArray<TPair<FString, int32>> SupportedArray;
            for (const auto& Entry : SupportedCopy)
            {
                SupportedArray.Add(TPair<FString, int32>(Entry.Key, Entry.Value));
                TotalSupported += Entry.Value;
            }
            SupportedArray.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) {
                return A.Value > B.Value;
            });

            for (const auto& Entry : SupportedArray)
            {
                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  %s: %d occurrences"), *Entry.Key, Entry.Value);
            }
        }
        else
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("Supported Property Types: None"));
        }

        // Log unsupported property types
        if (UnsupportedCopy.Num() > 0)
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("Unsupported Property Types (using fallback):"));

            // Sort by occurrence count (descending)
            TArray<TPair<FString, int32>> UnsupportedArray;
            for (const auto& Entry : UnsupportedCopy)
            {
                UnsupportedArray.Add(TPair<FString, int32>(Entry.Key, Entry.Value));
                TotalUnsupported += Entry.Value;
            }
            UnsupportedArray.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) {
                return A.Value > B.Value;
            });

            // Log top N unsupported types with warning emoji
            int32 RankIndex = 0;
            for (const auto& Entry : UnsupportedArray)
            {
                RankIndex++;
                if (RankIndex <= TOP_UNSUPPORTED_TYPES_COUNT)
                {
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  %s: %d occurrences ⚠️ TOP %d (Phase 3 candidate)"),
                           *Entry.Key, Entry.Value, RankIndex);
                }
                else
                {
                    UE_LOG(LogNarshaMCPMetadata, Display, TEXT("  %s: %d occurrences"), *Entry.Key, Entry.Value);
                }
            }
        }
        else
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("Unsupported Property Types: None (100%% coverage!)"));
        }

        // Calculate and log coverage percentage
        // Formula: Coverage = (Supported / Total) * 100
        // Where Total = Supported + Unsupported
        // Example: 9,108 supported / 9,626 total = 94.6%
        int32 TotalProperties = TotalSupported + TotalUnsupported;
        if (TotalProperties > 0)
        {
            float Coverage = (float)TotalSupported / TotalProperties * 100.0f;
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("Coverage: %.1f%% (%d/%d properties)"),
                   Coverage, TotalSupported, TotalProperties);
        }
        else
        {
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("Coverage: N/A (no properties processed)"));
        }

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("============================================================"));
        UE_LOG(LogNarshaMCPMetadata, Display, TEXT(""));
    }

    // Issue #582: Reset Property Type Statistics (v5.1.0)
    void ResetPropertyTypeStats()
    {
        FScopeLock Lock(&PropertyTypeStatsLock);
        SupportedPropertyTypes.Reset();
        UnsupportedPropertyTypes.Reset();
    }

#if WITH_AUTOMATION_TESTS
    // Issue #1088: Test accessor implementations for TMap unit tests (v5.2.0)

    int32 GetStructHandlerCount()
    {
        // Ensure handlers are initialized before checking count
        if (!bStructHandlersInitialized)
        {
            InitializeStructHandlers();
        }

        FScopeLock Lock(&PropertyTypeStatsLock);
        return StructHandlers.Num();
    }

    bool HasStructHandler(const FString& StructName)
    {
        // Ensure handlers are initialized before checking
        if (!bStructHandlersInitialized)
        {
            InitializeStructHandlers();
        }

        FScopeLock Lock(&PropertyTypeStatsLock);
        return StructHandlers.Contains(StructName);
    }

    void TestInitializeStructHandlers()
    {
        InitializeStructHandlers();
    }

    void TestResetStructHandlers()
    {
        FScopeLock Lock(&PropertyTypeStatsLock);
        StructHandlers.Reset();
        bStructHandlersInitialized = false;
    }
#endif // WITH_AUTOMATION_TESTS
}
