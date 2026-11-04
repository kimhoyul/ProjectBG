// Copyright 2026 Next Stage Inc. All rights reserved.

#include "BlueprintMetadata/AnimationExtractors.h"
#include "BlueprintMetadata/BlueprintExtractors.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimStateMachineTypes.h"
// UAnimNotifyState reached this TU transitively through AnimNotify_GameplayCue.h;
// that include is now opt-in gated (Issue #28215), so depend on it explicitly.
#include "Animation/AnimNotifies/AnimNotifyState.h"
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "AnimNotify_GameplayCue.h"
#endif
#include "GameplayTagContainer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
// Issue #11472 Gap 1: UE 5.8 deprecated GetObjectsWithOuter(..., bool) in favor of EGetObjectsFlags.
#include "Misc/EngineVersionComparison.h"

namespace AnimationExtractors
{
    void ExtractAnimBlueprintMetadata(UAnimBlueprint* AnimBP, TSharedPtr<FJsonObject>& OutJson)
    {
        if (!AnimBP)
        {
            return;
        }

        // Extract AnimNotifies from all referenced animation sequences
        TArray<TSharedPtr<FJsonValue>> NotifiesArray;

        // Get all animation assets in the same package as this AnimBP
        UPackage* Package = AnimBP->GetOutermost();
        TArray<UObject*> ObjectsInPackage;
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
        GetObjectsWithOuter(Package, ObjectsInPackage, EGetObjectsFlags::None);
#else
        GetObjectsWithOuter(Package, ObjectsInPackage, false);
#endif

        for (UObject* Obj : ObjectsInPackage)
        {
            UAnimSequenceBase* Sequence = Cast<UAnimSequenceBase>(Obj);
            if (!Sequence)
            {
                continue;
            }

            // Extract all AnimNotifies from this sequence
            for (const FAnimNotifyEvent& NotifyEvent : Sequence->Notifies)
            {
                TSharedPtr<FJsonObject> NotifyJson = MakeShareable(new FJsonObject);

                // Basic notify info
                NotifyJson->SetStringField(FString(TEXT("notify_name")), NotifyEvent.NotifyName.ToString());
                NotifyJson->SetNumberField(FString(TEXT("trigger_time")), NotifyEvent.GetTriggerTime());
                NotifyJson->SetNumberField(FString(TEXT("duration")), NotifyEvent.GetDuration());
                NotifyJson->SetStringField(FString(TEXT("animation_sequence")), Sequence->GetName());

                // Extract Gameplay Tag using helper function
                FString GameplayTag, NotifyType, NotifyClass;
                ExtractGameplayTagFromNotify(NotifyEvent, GameplayTag, NotifyType, NotifyClass);

                NotifyJson->SetStringField(FString(TEXT("notify_type")), NotifyType);

                if (!NotifyClass.IsEmpty())
                {
                    NotifyJson->SetStringField(FString(TEXT("notify_class")), NotifyClass);
                }

                if (!GameplayTag.IsEmpty())
                {
                    NotifyJson->SetStringField(FString(TEXT("gameplay_tag")), GameplayTag);
                }

                NotifiesArray.Add(MakeShareable(new FJsonValueObject(NotifyJson)));
            }
        }

        OutJson->SetArrayField(FString(TEXT("anim_notifies")), NotifiesArray);
        OutJson->SetNumberField(FString(TEXT("anim_notifies_count")), NotifiesArray.Num());

        // Extract state machines (Issue #135 Phase 2)
        TArray<TSharedPtr<FJsonValue>> StateMachinesArray;
        ExtractStateMachines(AnimBP, StateMachinesArray);
        OutJson->SetArrayField(FString(TEXT("state_machines")), StateMachinesArray);
        OutJson->SetNumberField(FString(TEXT("state_machines_count")), StateMachinesArray.Num());

        // Extract delegate declarations (Issue #463 - AnimBP support)
        TArray<TSharedPtr<FJsonValue>> DelegatesArray;
        BlueprintExtractors::ExtractDelegates(AnimBP, DelegatesArray);
        OutJson->SetArrayField(FString(TEXT("delegates")), DelegatesArray);

        // Extract delegate bindings (Issue #463 - AnimBP support)
        TArray<TSharedPtr<FJsonValue>> BindingsArray;
        BlueprintExtractors::ExtractDelegateBindings(AnimBP, BindingsArray);
        OutJson->SetArrayField(FString(TEXT("delegate_bindings")), BindingsArray);

        // Extract delegate broadcasts (Issue #463 - AnimBP support)
        TArray<TSharedPtr<FJsonValue>> BroadcastsArray;
        BlueprintExtractors::ExtractDelegateBroadcasts(AnimBP, BroadcastsArray);
        OutJson->SetArrayField(FString(TEXT("delegate_broadcasts")), BroadcastsArray);

        // Delegate counts
        OutJson->SetNumberField(FString(TEXT("delegate_count")), DelegatesArray.Num());
        OutJson->SetNumberField(FString(TEXT("binding_count")), BindingsArray.Num());
        OutJson->SetNumberField(FString(TEXT("broadcast_count")), BroadcastsArray.Num());
    }

    void ExtractGameplayTagFromNotify(
        const FAnimNotifyEvent& NotifyEvent,
        FString& OutGameplayTag,
        FString& OutNotifyType,
        FString& OutNotifyClass
    )
    {
        OutGameplayTag = TEXT("");
        OutNotifyType = TEXT("");
        OutNotifyClass = TEXT("");

        if (NotifyEvent.Notify)
        {
            // Check if this is a GameplayCue notify
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
            UObject* GameplayCueNotify = Cast<UAnimNotify_GameplayCue>(NotifyEvent.Notify);
#else
            // Issue #28215 Phase A: GameplayAbilities is opt-in, so the notify type may not be
            // linked. The body below is already pure reflection, so gate on the resolved class
            // instead of the static type -- identical behaviour, no link required.
            // Deliberately NOT cached in a static: a first call before the module is
            // registered would pin nullptr for the process and silently misclassify every
            // later cue notify. FindObject on a registered native class is a hash lookup.
            UClass* const GameplayCueNotifyClass = FindObject<UClass>(
                nullptr, TEXT("/Script/GameplayAbilities.AnimNotify_GameplayCue"));
            UObject* GameplayCueNotify = (GameplayCueNotifyClass && NotifyEvent.Notify->IsA(GameplayCueNotifyClass))
                ? NotifyEvent.Notify
                : nullptr;
#endif
            if (GameplayCueNotify)
            {
                // Extract the Gameplay Tag using reflection
                UClass* NotifyClass = GameplayCueNotify->GetClass();
                FProperty* GameplayCueProp = NotifyClass->FindPropertyByName(TEXT("GameplayCue"));

                if (GameplayCueProp)
                {
                    // Get the property value
                    const void* PropValuePtr = GameplayCueProp->ContainerPtrToValuePtr<void>(GameplayCueNotify);

                    // FGameplayCueTag contains a FGameplayTag
                    FStructProperty* StructProp = CastField<FStructProperty>(GameplayCueProp);
                    if (StructProp && StructProp->Struct->GetName() == TEXT("GameplayCueTag"))
                    {
                        // GameplayCueTag has a GameplayTag member
                        FProperty* TagProp = StructProp->Struct->FindPropertyByName(TEXT("GameplayTag"));
                        if (TagProp)
                        {
                            const FGameplayTag* TagPtr = TagProp->ContainerPtrToValuePtr<FGameplayTag>(PropValuePtr);
                            if (TagPtr && TagPtr->IsValid())
                            {
                                OutGameplayTag = TagPtr->ToString();
                            }
                        }
                    }
                }

                OutNotifyType = TEXT("gameplay_cue");
            }
            else
            {
                // Generic AnimNotify
                OutNotifyType = TEXT("generic");
                OutNotifyClass = NotifyEvent.Notify->GetClass()->GetName();
            }
        }
        else if (NotifyEvent.NotifyStateClass)
        {
            // AnimNotifyState (duration-based)
            OutNotifyType = TEXT("notify_state");
            OutNotifyClass = NotifyEvent.NotifyStateClass->GetClass()->GetName();
        }
        else
        {
            // Notify event without a custom class
            OutNotifyType = TEXT("event");
        }
    }

    void ExtractStateMachines(UAnimBlueprint* AnimBP, TArray<TSharedPtr<FJsonValue>>& OutStateMachines)
    {
        if (!AnimBP)
        {
            return;
        }

        UAnimBlueprintGeneratedClass* AnimClass = AnimBP->GetAnimBlueprintGeneratedClass();
        if (!AnimClass)
        {
            return;
        }

        // Iterate through BakedStateMachines (Issue #135 Phase 2)
        for (const FBakedAnimationStateMachine& StateMachine : AnimClass->BakedStateMachines)
        {
            TSharedPtr<FJsonObject> MachineJson = MakeShareable(new FJsonObject);

            // Basic machine info
            MachineJson->SetStringField(FString(TEXT("machine_name")), StateMachine.MachineName.ToString());
            MachineJson->SetNumberField(FString(TEXT("initial_state")), StateMachine.InitialState);

            // Extract states
            TArray<TSharedPtr<FJsonValue>> StatesArray;
            for (const FBakedAnimationState& State : StateMachine.States)
            {
                TSharedPtr<FJsonObject> StateJson = MakeShareable(new FJsonObject);
                StateJson->SetStringField(FString(TEXT("state_name")), State.StateName.ToString());
                StateJson->SetNumberField(FString(TEXT("state_root_node_index")), State.StateRootNodeIndex);
                StateJson->SetBoolField(FString(TEXT("is_conduit")), State.bIsAConduit);

                // Extract transitions from this state
                TArray<TSharedPtr<FJsonValue>> TransitionsArray;
                for (const FBakedStateExitTransition& Transition : State.Transitions)
                {
                    TSharedPtr<FJsonObject> TransitionJson = MakeShareable(new FJsonObject);

                    // ============================================================
                    // Legacy fields (backward compatibility for existing tools)
                    // ============================================================

                    // 1. next_state: Resolve from StateMachine.Transitions using TransitionIndex
                    //    TransitionIndex is "The index into the machine table of transitions" (UE 5.6 source)
                    if (Transition.TransitionIndex >= 0 && Transition.TransitionIndex < StateMachine.Transitions.Num())
                    {
                        const FAnimationTransitionBetweenStates& GlobalTransition = StateMachine.Transitions[Transition.TransitionIndex];
                        TransitionJson->SetNumberField(FString(TEXT("next_state")), GlobalTransition.NextState);
                    }
                    else
                    {
                        // Fallback: TransitionIndex is INDEX_NONE or out of range
                        // INDEX_NONE typically means "no valid transition" - use transition_index value as-is for debugging
                        // Note: Downstream tools should check transition_index field to detect invalid transitions
                        TransitionJson->SetNumberField(FString(TEXT("next_state")), Transition.TransitionIndex);
                    }

                    // 2. can_trigger_delegates: CanTakeDelegateIndex != INDEX_NONE means delegates can be triggered
                    bool bCanTriggerDelegates = (Transition.CanTakeDelegateIndex != INDEX_NONE);
                    TransitionJson->SetBoolField(FString(TEXT("can_trigger_delegates")), bCanTriggerDelegates);

                    // 3. automatic_rule: Map from bAutomaticRemainingTimeRule (closest semantic match)
                    TransitionJson->SetBoolField(FString(TEXT("automatic_rule")), Transition.bAutomaticRemainingTimeRule);

                    // ============================================================
                    // UE 5.6 new fields (for future migration)
                    // ============================================================

                    TransitionJson->SetNumberField(FString(TEXT("transition_index")), Transition.TransitionIndex);
                    TransitionJson->SetNumberField(FString(TEXT("can_take_delegate_index")), Transition.CanTakeDelegateIndex);
                    TransitionJson->SetNumberField(FString(TEXT("custom_result_node_index")), Transition.CustomResultNodeIndex);
                    TransitionJson->SetBoolField(FString(TEXT("desired_transition_return_value")), Transition.bDesiredTransitionReturnValue);
                    TransitionJson->SetBoolField(FString(TEXT("automatic_remaining_time_rule")), Transition.bAutomaticRemainingTimeRule);
                    TransitionJson->SetNumberField(FString(TEXT("automatic_rule_trigger_time")), Transition.AutomaticRuleTriggerTime);

                    // Optional: Sync group name (if not None)
                    if (!Transition.SyncGroupNameToRequireValidMarkersRule.IsNone())
                    {
                        TransitionJson->SetStringField(FString(TEXT("sync_group_name")),
                            Transition.SyncGroupNameToRequireValidMarkersRule.ToString());
                    }

                    // Optional: Pose evaluator links (if not empty)
                    if (Transition.PoseEvaluatorLinks.Num() > 0)
                    {
                        TArray<TSharedPtr<FJsonValue>> LinksArray;
                        for (int32 Link : Transition.PoseEvaluatorLinks)
                        {
                            LinksArray.Add(MakeShareable(new FJsonValueNumber(Link)));
                        }
                        TransitionJson->SetArrayField(FString(TEXT("pose_evaluator_links")), LinksArray);
                    }

                    TransitionsArray.Add(MakeShareable(new FJsonValueObject(TransitionJson)));
                }
                StateJson->SetArrayField(FString(TEXT("transitions")), TransitionsArray);
                StateJson->SetNumberField(FString(TEXT("transition_count")), TransitionsArray.Num());

                StatesArray.Add(MakeShareable(new FJsonValueObject(StateJson)));
            }
            MachineJson->SetArrayField(FString(TEXT("states")), StatesArray);
            MachineJson->SetNumberField(FString(TEXT("state_count")), StatesArray.Num());

            // Extract overall transitions
            TArray<TSharedPtr<FJsonValue>> GlobalTransitionsArray;
            for (const FAnimationTransitionBetweenStates& Transition : StateMachine.Transitions)
            {
                TSharedPtr<FJsonObject> TransitionJson = MakeShareable(new FJsonObject);
                // UE 5.6 API: StartState → PreviousState, EndState → NextState
                TransitionJson->SetNumberField(FString(TEXT("start_state")), Transition.PreviousState);
                TransitionJson->SetNumberField(FString(TEXT("end_state")), Transition.NextState);
                // Note: PriorityOrder removed in UE 5.6

                GlobalTransitionsArray.Add(MakeShareable(new FJsonValueObject(TransitionJson)));
            }
            MachineJson->SetArrayField(FString(TEXT("transitions")), GlobalTransitionsArray);
            MachineJson->SetNumberField(FString(TEXT("transition_count")), GlobalTransitionsArray.Num());

            OutStateMachines.Add(MakeShareable(new FJsonValueObject(MachineJson)));
        }
    }

}  // namespace AnimationExtractors
