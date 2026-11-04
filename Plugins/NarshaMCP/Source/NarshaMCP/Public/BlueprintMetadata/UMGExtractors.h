// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UWidgetBlueprint;
class FJsonObject;
class FJsonValue;

/**
 * UMG (Unreal Motion Graphics) Widget extraction functions
 *
 * Issue #135 Phase 3: Extract UMG widget event bindings and propagation chains
 */
namespace UMGExtractors
{
    /**
     * Extract UMG widget metadata from Widget Blueprint (Issue #135 Phase 3)
     *
     * Extracts widget tree structure, event bindings, and propagation chains.
     * Includes UK2Node_ComponentBoundEvent nodes for widget events.
     *
     * @param WidgetBP - Widget Blueprint to extract from
     * @param OutJson - Output JSON object to populate with UMG data
     */
    void ExtractWidgetBlueprintMetadata(UWidgetBlueprint* WidgetBP, TSharedPtr<FJsonObject>& OutJson);

    /**
     * Extract widget tree from Widget Blueprint (Issue #135 Phase 3)
     *
     * Traverses widget hierarchy and extracts all widgets with their properties.
     *
     * @param WidgetBP - Widget Blueprint to extract from
     * @param OutWidgets - Output array to populate with widget data
     */
    void ExtractWidgetTree(UWidgetBlueprint* WidgetBP, TArray<TSharedPtr<FJsonValue>>& OutWidgets);

    /**
     * Extract widget event bindings from Blueprint graphs (Issue #135 Phase 3)
     *
     * Finds UK2Node_ComponentBoundEvent nodes that bind widget events to Blueprint functions.
     * Examples: Button.OnClicked → BP_HandleButtonClick
     *
     * @param Blueprint - Blueprint to extract from
     * @param OutBindings - Output array to populate with event binding data
     */
    void ExtractWidgetEventBindings(UBlueprint* Blueprint, TArray<TSharedPtr<FJsonValue>>& OutBindings);
}
