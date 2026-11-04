// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #14795 — AnimSequence notify authoring + notify-track creation +
// notify payload property setter. Three UFUNCTIONs on UCURemoteLibrary,
// declared in `Public/CURemoteLibrary.h`, sharing the file-local helpers in
// `CUAnimAuthoring_Shared.h` with the Phase 2 montage / blendspace handlers.
//
// All three target UAnimSequenceBase so the ops cover BOTH AnimSequence and
// AnimMontage (Notifies + AnimNotifyTracks live on the base class). The Rust
// dispatcher routes `add_notify` to AnimAM_AddNotify for AM_* montages
// (strict track) and to AnimSeq_AddNotify otherwise. Empirically validated in
// campaign #14709 (E04 notify-event / E05 notify-track) — refutes the ACL
// rationale of WONTFIX #11155 / #11189.

#include "CUAnimAuthoring_Shared.h"

#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "UObject/UnrealType.h"

using namespace NarshaAnimAuthoring;

namespace
{
	/** Load a UAnimSequenceBase (AnimSequence, AnimMontage, AnimComposite). */
	static UAnimSequenceBase* LoadAnimSequenceBase(const FString& AssetPath)
	{
		return LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	}
}

FString UCURemoteLibrary::AnimSeq_AddNotify(
	const FString& AssetPath,
	float TriggerTime,
	const FString& NotifyClass,
	const FString& Track,
	float Duration)
{
	if (TriggerTime < 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("TriggerTime must be >= 0, got %.3f"), TriggerTime)));
	}
	UAnimSequenceBase* Seq = LoadAnimSequenceBase(AssetPath);
	if (!Seq)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequenceBase: %s"), *AssetPath)));
	}
	UClass* NotifyCls = TryLoadNotifyClass(NotifyClass);
	if (!NotifyCls)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load notify class: %s"), *NotifyClass),
			TEXT("Pass a fully-qualified class path (e.g. /Game/Notifies/AN_Footstep.AN_Footstep_C)")));
	}

	const bool bIsState = NotifyCls->IsChildOf(UAnimNotifyState::StaticClass());
	const bool bIsPoint = NotifyCls->IsChildOf(UAnimNotify::StaticClass()) && !bIsState;
	if (!bIsState && !bIsPoint)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("NotifyClass '%s' is neither a UAnimNotify nor a UAnimNotifyState subclass"), *NotifyClass)));
	}
	if (bIsState && Duration <= 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Duration must be > 0 for AnimNotifyState '%s'; got %.3f"), *NotifyClass, Duration),
			TEXT("Pass duration>0 for ANS_* classes; omit it (or pass 0) for one-shot AN_* notifies.")));
	}

	// Resolve target track index. Empty Track → default lane 0 (valid even with
	// zero tracks). A named track must already exist (create it first via
	// Anim_AddNotifyTrack) — we never silently create the lane here.
	int32 TrackIndex = 0;
	if (!Track.IsEmpty())
	{
		const FName TrackName(*Track);
		TrackIndex = INDEX_NONE;
		for (int32 i = 0; i < Seq->AnimNotifyTracks.Num(); ++i)
		{
			if (Seq->AnimNotifyTracks[i].TrackName == TrackName)
			{
				TrackIndex = i;
				break;
			}
		}
		if (TrackIndex == INDEX_NONE)
		{
			return SerializeJsonResponse(MakeErrorObject(AssetPath,
				FString::Printf(TEXT("Notify track '%s' not found"), *Track),
				TEXT("Create the track first via add_notify_track, or omit `track` to use the default lane.")));
		}
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: add_notify on %s"), *AssetPath)));
	Seq->Modify();
#endif

	FAnimNotifyEvent NewNotify;
	NewNotify.SetTime(TriggerTime);
	NewNotify.TriggerTimeOffset = 0.0f;
	NewNotify.TrackIndex = TrackIndex;
	NewNotify.NotifyName = NotifyCls->GetFName();

	// Keep the created instance object so we can re-find the notify by identity
	// after RefreshCacheData() (which may re-sort Notifies by time).
	UAnimNotify* CreatedNotify = nullptr;
	UAnimNotifyState* CreatedState = nullptr;
	if (bIsState)
	{
		CreatedState = NewObject<UAnimNotifyState>(Seq, NotifyCls);
		NewNotify.NotifyStateClass = CreatedState;
		NewNotify.SetDuration(Duration);
	}
	else
	{
		CreatedNotify = NewObject<UAnimNotify>(Seq, NotifyCls);
		NewNotify.Notify = CreatedNotify;
	}

	Seq->Notifies.Add(NewNotify);

#if WITH_EDITOR
	Seq->RefreshCacheData();
#endif

	// RefreshCacheData may re-sort Notifies by time. Re-find the element by the
	// instance-object pointer we created so the returned notify_index matches
	// the serialized order the offline reparse-verify reads back.
	int32 FinalIndex = INDEX_NONE;
	for (int32 i = 0; i < Seq->Notifies.Num(); ++i)
	{
		const FAnimNotifyEvent& N = Seq->Notifies[i];
		if ((CreatedNotify && N.Notify == CreatedNotify) ||
		    (CreatedState && N.NotifyStateClass == CreatedState))
		{
			FinalIndex = i;
			break;
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("notify_index"), FinalIndex);
	Result->SetNumberField(TEXT("track_index"), TrackIndex);
	Result->SetStringField(TEXT("notify_kind"), bIsState ? TEXT("notify_state") : TEXT("notify"));
	MarkDirtyAndSave(Seq, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::Anim_AddNotifyTrack(
	const FString& AssetPath,
	const FString& TrackName)
{
	if (TrackName.IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			TEXT("TrackName must be non-empty")));
	}
	UAnimSequenceBase* Seq = LoadAnimSequenceBase(AssetPath);
	if (!Seq)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequenceBase: %s"), *AssetPath)));
	}

	const FName NewName(*TrackName);
	for (int32 i = 0; i < Seq->AnimNotifyTracks.Num(); ++i)
	{
		if (Seq->AnimNotifyTracks[i].TrackName == NewName)
		{
			return SerializeJsonResponse(MakeErrorObject(AssetPath,
				FString::Printf(TEXT("Notify track '%s' already exists at index %d"), *TrackName, i),
				TEXT("Track names must be unique; use the existing track or pick a new name.")));
		}
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: add_notify_track on %s"), *AssetPath)));
	Seq->Modify();
#endif

	FAnimNotifyTrack NewTrack;
	NewTrack.TrackName = NewName;
	NewTrack.TrackColor = FLinearColor::White;
	const int32 NewIndex = Seq->AnimNotifyTracks.Add(NewTrack);

#if WITH_EDITOR
	Seq->RefreshCacheData();
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("track_index"), NewIndex);
	Result->SetStringField(TEXT("track_name"), TrackName);
	MarkDirtyAndSave(Seq, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::Anim_SetNotifyProperty(
	const FString& AssetPath,
	int32 NotifyIndex,
	const FString& PropertyName,
	const FString& Value)
{
	if (PropertyName.IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			TEXT("PropertyName must be non-empty")));
	}
	UAnimSequenceBase* Seq = LoadAnimSequenceBase(AssetPath);
	if (!Seq)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequenceBase: %s"), *AssetPath)));
	}
	if (NotifyIndex < 0 || NotifyIndex >= Seq->Notifies.Num())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Notify index %d out of range (count=%d)"), NotifyIndex, Seq->Notifies.Num())));
	}

	FAnimNotifyEvent& N = Seq->Notifies[NotifyIndex];
	UObject* Target = nullptr;
	if (N.Notify)
	{
		Target = N.Notify;
	}
	else if (N.NotifyStateClass)
	{
		Target = N.NotifyStateClass;
	}
	if (!Target)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Notify at index %d has no instance object (skeleton / named-only notify)"), NotifyIndex),
			TEXT("Payload properties can only be set on UAnimNotify / UAnimNotifyState instances (AN_*/ANS_*).")));
	}

	FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Target->GetClass()->GetName()),
			TEXT("Inspect the notify class to find a valid UPROPERTY name.")));
	}

#if WITH_EDITOR
	// Transact both the sub-object (whose property changes) AND the owning
	// sequence so Ctrl+Z restores cleanly and the package is marked dirty.
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(
		TEXT("NarshaMCP: set_notify_property %s.%s"), *Target->GetName(), *PropertyName)));
	Target->Modify();
	Seq->Modify();
#endif

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
	FString PreviousValue;
	Property->ExportTextItem_Direct(PreviousValue, ValuePtr, nullptr, Target, PPF_None);

	const TCHAR* ImportResult = Property->ImportText_Direct(*Value, ValuePtr, Target, PPF_None);
	if (!ImportResult)
	{
		TSharedRef<FJsonObject> Err = MakeErrorObject(AssetPath,
			FString::Printf(TEXT("ImportText failed for '%s' on property '%s' (type: %s)"),
				*Value, *PropertyName, *Property->GetCPPType()));
		Err->SetStringField(TEXT("property_type"), Property->GetCPPType());
		Err->SetStringField(TEXT("previous_value"), PreviousValue);
		return SerializeJsonResponse(Err);
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("notify_index"), NotifyIndex);
	Result->SetStringField(TEXT("property"), PropertyName);
	Result->SetStringField(TEXT("previous_value"), PreviousValue);
	Result->SetStringField(TEXT("new_value"), Value);
	Result->SetStringField(TEXT("property_type"), Property->GetCPPType());
	MarkDirtyAndSave(Seq, Result);
	return SerializeJsonResponse(Result);
}
