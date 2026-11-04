// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UAnimBlueprint;
class FJsonObject;
struct FAnimNotifyEvent;

/**
 * Animation Blueprint and AnimNotify extraction functions
 *
 * Issue #108 Phase 0: Extracted from CUBlueprintMetadataCommandlet.cpp (1938 lines → modular)
 */
namespace AnimationExtractors
{
    /**
     * Extract AnimNotify metadata from Animation Blueprint (v2.8.4+)
     *
     * Extracts animation notify events from all AnimSequences in the AnimBP's package,
     * including Gameplay Tags from UAnimNotify_GameplayCue notifies.
     *
     * @param AnimBP - Animation Blueprint to extract from
     * @param OutJson - Output JSON object to populate with AnimNotify data
     */
    void ExtractAnimBlueprintMetadata(UAnimBlueprint* AnimBP, TSharedPtr<FJsonObject>& OutJson);

    /**
     * Extract GameplayTag from AnimNotify event (v3.5.0 - Refactoring)
     *
     * Extracts Gameplay Tag from UAnimNotify_GameplayCue using reflection.
     * This helper eliminates 58 lines of duplicated code.
     *
     * @param NotifyEvent - AnimNotify event to extract from
     * @param OutGameplayTag - Output GameplayTag string (empty if not found)
     * @param OutNotifyType - Output notify type string ("gameplay_cue", "generic", "notify_state", "event")
     * @param OutNotifyClass - Output notify class name (for generic notifies)
     */
    void ExtractGameplayTagFromNotify(
        const FAnimNotifyEvent& NotifyEvent,
        FString& OutGameplayTag,
        FString& OutNotifyType,
        FString& OutNotifyClass
    );

    /**
     * Extract state machines from Animation Blueprint (Issue #135 Phase 2)
     *
     * Extracts compiled state machine structure from AnimBlueprintGeneratedClass.
     * Includes states, transitions, and conduit nodes.
     *
     * @param AnimBP - Animation Blueprint to extract from
     * @param OutStateMachines - Output array to populate with state machine data
     */
    void ExtractStateMachines(UAnimBlueprint* AnimBP, TArray<TSharedPtr<FJsonValue>>& OutStateMachines);
}
