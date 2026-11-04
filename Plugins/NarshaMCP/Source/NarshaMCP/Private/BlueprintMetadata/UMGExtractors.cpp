// Copyright 2026 Next Stage Inc. All rights reserved.

#if WITH_EDITOR
#include "BlueprintMetadata/UMGExtractors.h"
#include "BlueprintMetadata/BlueprintMetadataHelpers.h"
#include "BlueprintMetadata/BlueprintExtractors.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_CallFunction.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

#include "CULog.h"

namespace UMGExtractors
{
    void ExtractWidgetBlueprintMetadata(UWidgetBlueprint* WidgetBP, TSharedPtr<FJsonObject>& OutJson)
    {
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            return;
        }

        // Extract widget tree structure
        TArray<TSharedPtr<FJsonValue>> WidgetsArray;
        ExtractWidgetTree(WidgetBP, WidgetsArray);
        OutJson->SetArrayField(TEXT("widgets"), WidgetsArray);
        OutJson->SetNumberField(TEXT("widget_count"), WidgetsArray.Num());

        // Extract widget event bindings
        TArray<TSharedPtr<FJsonValue>> BindingsArray;
        ExtractWidgetEventBindings(WidgetBP, BindingsArray);
        OutJson->SetArrayField(TEXT("widget_event_bindings"), BindingsArray);
        OutJson->SetNumberField(TEXT("widget_event_binding_count"), BindingsArray.Num());

        // Extract delegate declarations (Issue #463 - WidgetBP support)
        TArray<TSharedPtr<FJsonValue>> DelegatesArray;
        BlueprintExtractors::ExtractDelegates(WidgetBP, DelegatesArray);
        OutJson->SetArrayField(TEXT("delegates"), DelegatesArray);

        // Extract delegate bindings (Issue #463 - WidgetBP support)
        TArray<TSharedPtr<FJsonValue>> DelegateBindingsArray;
        BlueprintExtractors::ExtractDelegateBindings(WidgetBP, DelegateBindingsArray);
        OutJson->SetArrayField(TEXT("delegate_bindings"), DelegateBindingsArray);

        // Extract delegate broadcasts (Issue #463 - WidgetBP support)
        TArray<TSharedPtr<FJsonValue>> BroadcastsArray;
        BlueprintExtractors::ExtractDelegateBroadcasts(WidgetBP, BroadcastsArray);
        OutJson->SetArrayField(TEXT("delegate_broadcasts"), BroadcastsArray);

        // Delegate counts
        OutJson->SetNumberField(TEXT("delegate_count"), DelegatesArray.Num());
        OutJson->SetNumberField(TEXT("binding_count"), DelegateBindingsArray.Num());
        OutJson->SetNumberField(TEXT("broadcast_count"), BroadcastsArray.Num());
    }

    void ExtractWidgetTree(UWidgetBlueprint* WidgetBP, TArray<TSharedPtr<FJsonValue>>& OutWidgets)
    {
        if (!WidgetBP || !WidgetBP->WidgetTree)
        {
            return;
        }

        // Get all widgets using WidgetTree API
        TArray<UWidget*> AllWidgets;
        WidgetBP->WidgetTree->GetAllWidgets(AllWidgets);

        for (UWidget* Widget : AllWidgets)
        {
            if (!Widget)
            {
                continue;
            }

            TSharedPtr<FJsonObject> WidgetJson = MakeShareable(new FJsonObject);

            // Basic widget info
            WidgetJson->SetStringField(TEXT("widget_name"), Widget->GetName());
            WidgetJson->SetStringField(TEXT("widget_class"), Widget->GetClass()->GetName());

            // TODO(Issue #135 Phase 3): UE 5.6 API compatibility
            // GetDisplayLabel() and GetFieldNameFromClassByGuid APIs removed for UE 5.6 compatibility
            // Basic extraction sufficient for widget event propagation testing

            // Parent widget info
            UWidget* ParentWidget = Widget->GetParent();
            if (ParentWidget)
            {
                WidgetJson->SetStringField(TEXT("parent_widget_name"), ParentWidget->GetName());
                WidgetJson->SetStringField(TEXT("parent_widget_class"), ParentWidget->GetClass()->GetName());
            }
            else
            {
                WidgetJson->SetStringField(TEXT("parent_widget_name"), TEXT("RootWidget"));
            }

            // Check if this is a panel widget (can contain children)
            UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
            WidgetJson->SetBoolField(TEXT("is_panel"), PanelWidget != nullptr);

            if (PanelWidget)
            {
                WidgetJson->SetNumberField(TEXT("child_count"), PanelWidget->GetChildrenCount());
            }

            // Widget visibility
            WidgetJson->SetBoolField(TEXT("is_visible"), Widget->IsVisible());
            WidgetJson->SetBoolField(TEXT("is_enabled"), Widget->GetIsEnabled());

            OutWidgets.Add(MakeShareable(new FJsonValueObject(WidgetJson)));
        }
    }

    void ExtractWidgetEventBindings(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutBindings)
    {
        if (!Blueprint)
        {
            return;
        }

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
                // Check if this is a UK2Node_ComponentBoundEvent node
                UK2Node_ComponentBoundEvent* BoundEventNode = Cast<UK2Node_ComponentBoundEvent>(Node);
                if (!BoundEventNode)
                {
                    continue;
                }

                TSharedPtr<FJsonObject> BindingJson = MakeShareable(new FJsonObject);

                // Basic node info
                BindingJson->SetStringField(TEXT("node_id"), Node->GetName());
                BindingJson->SetStringField(TEXT("node_title"), BoundEventNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                BindingJson->SetStringField(TEXT("graph_name"), Graph->GetName());

                // Find owning function/event
                FString OwningFunction = BlueprintMetadataHelpers::FindOwningFunction(Node);
                BindingJson->SetStringField(TEXT("owning_function"), OwningFunction);

                // Extract widget component name (the widget that triggered the event)
                FName ComponentPropertyName = BoundEventNode->ComponentPropertyName;
                BindingJson->SetStringField(TEXT("widget_name"), ComponentPropertyName.ToString());

                // Extract delegate property name (the event being bound, e.g., "OnClicked")
                FName DelegatePropertyName = BoundEventNode->DelegatePropertyName;
                BindingJson->SetStringField(TEXT("event_name"), DelegatePropertyName.ToString());

                // Get delegate owner class (the widget class, e.g., "Button")
                if (BoundEventNode->DelegateOwnerClass)
                {
                    BindingJson->SetStringField(TEXT("widget_class"), BoundEventNode->DelegateOwnerClass->GetName());
                }

                // TODO(Issue #135 Phase 3): UE 5.6 API compatibility
                // UK2Node_ComponentBoundEvent::GetTargetFunction() doesn't exist in UE 5.6
                // Event function signature extraction removed for UE 5.6 compatibility
                // Basic node info sufficient for widget event propagation testing

                // Extract connected nodes (what this event calls)
                TArray<TSharedPtr<FJsonValue>> ConnectedNodesArray;

                // UK2Node_ComponentBoundEvent typically has:
                // - "execute" (input exec pin) - from the widget event
                // - "then" (output exec pin) - to the Blueprint logic

                for (UEdGraphPin* Pin : BoundEventNode->Pins)
                {
                    if (!Pin || Pin->LinkedTo.Num() == 0)
                    {
                        continue;
                    }

                    // We're interested in the output exec pin ("then")
                    if (Pin->PinName.ToString() == TEXT("then") && Pin->Direction == EGPD_Output)
                    {
                        for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
                        {
                            if (!LinkedPin || !LinkedPin->GetOwningNode())
                            {
                                continue;
                            }

                            TSharedPtr<FJsonObject> ConnectedJson = MakeShareable(new FJsonObject);

                            UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
                            ConnectedJson->SetStringField(TEXT("node_id"), LinkedNode->GetName());
                            ConnectedJson->SetStringField(TEXT("node_type"), LinkedNode->GetClass()->GetName());
                            ConnectedJson->SetStringField(TEXT("node_title"), LinkedNode->GetNodeTitle(ENodeTitleType::ListView).ToString());

                            // Try to get function name if this is a function call
                            if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(LinkedNode))
                            {
                                if (UFunction* TargetFunc = CallNode->GetTargetFunction())
                                {
                                    ConnectedJson->SetStringField(TEXT("function_name"), TargetFunc->GetName());
                                    ConnectedJson->SetStringField(TEXT("function_full_name"), TargetFunc->GetPathName());
                                }
                            }

                            ConnectedNodesArray.Add(MakeShareable(new FJsonValueObject(ConnectedJson)));
                        }
                    }
                }

                BindingJson->SetArrayField(TEXT("connected_nodes"), ConnectedNodesArray);
                BindingJson->SetNumberField(TEXT("connected_node_count"), ConnectedNodesArray.Num());

                OutBindings.Add(MakeShareable(new FJsonValueObject(BindingJson)));

                UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][UMG] Widget Event: %s.%s → %s"),
                       *ComponentPropertyName.ToString(), *DelegatePropertyName.ToString(), *OwningFunction);
            }
        }
    }
}

#endif // WITH_EDITOR
