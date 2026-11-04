// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #9489 Phase 2 — AnimMontage authoring ops (8 UFUNCTIONs on
// UCURemoteLibrary) + Issue #16573 `AnimAM_CreateMontage` (asset creation).
// Declarations in `Public/CURemoteLibrary.h`. Shared helpers in
// `CUAnimAuthoring_Shared.h`. Sibling file `CUAnimAuthoring_BlendSpace.cpp`
// hosts the 7 BlendSpace handlers + `AnimBS_CreateBlendSpace`.

#include "CUAnimAuthoring_Shared.h"

#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequence.h"

// Issue #16573 — AnimMontage asset creation (UAnimMontageFactory is editor-only).
#if WITH_EDITOR
#include "Factories/AnimMontageFactory.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

using namespace NarshaAnimAuthoring;

DEFINE_LOG_CATEGORY_STATIC(LogNarshaAnimAM, Log, All);

FString UCURemoteLibrary::AnimAM_AddSection(
	const FString& AssetPath,
	const FString& Name,
	float StartTime)
{
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}
	const FName SectionName(*Name);
	for (const FCompositeSection& S : Montage->CompositeSections)
	{
		if (S.SectionName == SectionName)
		{
			return SerializeJsonResponse(MakeErrorObject(AssetPath,
				FString::Printf(TEXT("Section '%s' already exists"), *Name),
				TEXT("Use move_section to change StartTime, or remove_section first")));
		}
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: add_section on %s"), *AssetPath)));
	Montage->Modify();
#endif

	FCompositeSection NewSection;
	NewSection.SectionName = SectionName;
	NewSection.SetTime(StartTime);
	const int32 NewIndex = Montage->CompositeSections.Add(NewSection);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("section_index"), NewIndex);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_RemoveSection(
	const FString& AssetPath,
	const FString& Name)
{
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}
	const FName SectionName(*Name);
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		if (Montage->CompositeSections[i].SectionName == SectionName)
		{
			FoundIndex = i;
			break;
		}
	}
	if (FoundIndex == INDEX_NONE)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Section '%s' not found"), *Name)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: remove_section on %s"), *AssetPath)));
	Montage->Modify();
#endif

	Montage->CompositeSections.RemoveAt(FoundIndex);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("section_index"), FoundIndex);
	Result->SetBoolField(TEXT("removed"), true);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_SetSectionLink(
	const FString& AssetPath,
	const FString& Section,
	const FString& NextSection,
	bool bUseNextSection,
	const FString& LinkingRule)
{
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}
	const FName SectionName(*Section);
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		if (Montage->CompositeSections[i].SectionName == SectionName)
		{
			FoundIndex = i;
			break;
		}
	}
	if (FoundIndex == INDEX_NONE)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Section '%s' not found"), *Section)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: set_section_link on %s"), *AssetPath)));
	Montage->Modify();
#endif

	FCompositeSection& S = Montage->CompositeSections[FoundIndex];
	// Issue #9489 C-2: distinguish "absent" (bUseNextSection=false → don't
	// touch existing) from "explicitly cleared" (bUseNextSection=true +
	// NextSection="" → set to NAME_None). Previously empty string always
	// cleared, which silently destroyed the link when callers updated only
	// LinkingRule. The Rust side now sends bUseNextSection only when the
	// caller actually passed `next_section` in args.
	if (bUseNextSection)
	{
		if (!NextSection.IsEmpty())
		{
			S.NextSectionName = FName(*NextSection);
		}
		else
		{
			S.NextSectionName = NAME_None;
		}
	}
	// EAnimLinkMethod::{Absolute, Relative, Proportional} per UE 5.8.
	// `LinkMethod` is `protected` on the FAnimLinkableElement base class — use
	// the public `ChangeLinkMethod` setter (AnimLinkableElement.h:94).
	if (LinkingRule == TEXT("Absolute")) { S.ChangeLinkMethod(EAnimLinkMethod::Absolute); }
	else if (LinkingRule == TEXT("Relative")) { S.ChangeLinkMethod(EAnimLinkMethod::Relative); }
	else if (LinkingRule == TEXT("Proportional")) { S.ChangeLinkMethod(EAnimLinkMethod::Proportional); }

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	TSharedRef<FJsonObject> Updated = MakeShared<FJsonObject>();
	Updated->SetStringField(TEXT("section"), Section);
	Updated->SetStringField(TEXT("next_section"), NextSection);
	Updated->SetStringField(TEXT("linking_rule"), LinkingRule);
	Result->SetObjectField(TEXT("updated"), Updated);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_MoveSection(
	const FString& AssetPath,
	const FString& Name,
	float NewStartTime)
{
	if (NewStartTime < 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("NewStartTime must be >= 0, got %.3f"), NewStartTime)));
	}
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}
	const FName SectionName(*Name);
	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < Montage->CompositeSections.Num(); ++i)
	{
		if (Montage->CompositeSections[i].SectionName == SectionName)
		{
			FoundIndex = i;
			break;
		}
	}
	if (FoundIndex == INDEX_NONE)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Section '%s' not found"), *Name)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: move_section on %s"), *AssetPath)));
	Montage->Modify();
#endif

	Montage->CompositeSections[FoundIndex].SetTime(NewStartTime);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("section_index"), FoundIndex);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_AddNotify(
	const FString& AssetPath,
	const FString& Track,
	float TriggerTime,
	const FString& NotifyClass)
{
	if (TriggerTime < 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("TriggerTime must be >= 0, got %.3f"), TriggerTime)));
	}
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}
	// Issue #14796 (campaign #14706 D09b): reject a trigger_time past the end of
	// the montage. Without this an out-of-bounds notify is silently appended
	// (the D09 "silent corrupt" risk) — the notify never fires and the asset is
	// quietly wrong. Mirrors the negative-trigger_time guard above; the upper
	// bound needs the loaded montage so it lives here (write side) rather than
	// the Rust pre-gate, which has no play length. Checked before track
	// resolution so the bound message wins over a Track-not-found.
	// NOTE: this intentionally REJECTS rather than clamps. The AnimSequence
	// sibling (AddAnimNotify on UAnimSequenceBase) clamps an out-of-range time
	// into [0, length]; D09 deliberately surfaces the out-of-bounds input as a
	// graded error instead, so a caller bug is caught, not silently absorbed.
	const float PlayLength = Montage->GetPlayLength();
	if (TriggerTime > PlayLength)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("trigger_time %.3f exceeds montage length %.3f"), TriggerTime, PlayLength)));
	}
	UClass* NotifyCls = TryLoadNotifyClass(NotifyClass);
	if (!NotifyCls)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load notify class: %s"), *NotifyClass),
			TEXT("Pass a fully-qualified class path (e.g. /Game/Notifies/AN_Footstep.AN_Footstep_C)")));
	}
	// Type validation (Issue #9489 commit 2.7 — discovered by live test 2026-05-12:
	// AddNotifyState with a UAnimNotify subclass CRASHED the Editor in NewObject).
	// Strict check before allocating: must be UAnimNotify subclass, not state.
	if (!NotifyCls->IsChildOf(UAnimNotify::StaticClass()) ||
	    NotifyCls->IsChildOf(UAnimNotifyState::StaticClass()))
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("NotifyClass '%s' is not a UAnimNotify subclass"), *NotifyClass),
			TEXT("Use add_notify_state for UAnimNotifyState subclasses (ANS_*); add_notify accepts UAnimNotify subclasses (AN_*) only.")));
	}

	// Issue #23577: accept exact TrackName + synthetic "Track N" + numeric index.
	const int32 TrackIndex = ResolveMontageNotifyTrackIndex(Montage, Track);
	if (TrackIndex == INDEX_NONE)
	{
		// Issue #14776: list the live available track names instead of the old
		// circular "Inspect tracks[] via get_montage_notifies" hint.
		return SerializeJsonResponse(MakeTrackNotFoundError(AssetPath, Track, Montage));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: add_notify on %s"), *AssetPath)));
	Montage->Modify();
#endif

	FAnimNotifyEvent NewNotify;
	NewNotify.SetTime(TriggerTime);
	NewNotify.TrackIndex = TrackIndex;
	NewNotify.NotifyName = NotifyCls->GetFName();
	NewNotify.Notify = NewObject<UAnimNotify>(Montage, NotifyCls);
	const int32 NewIndex = Montage->Notifies.Add(NewNotify);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("notify_index"), NewIndex);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_AddNotifyState(
	const FString& AssetPath,
	const FString& Track,
	float TriggerTime,
	float Duration,
	const FString& NotifyClass)
{
	if (TriggerTime < 0.0f || Duration <= 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("TriggerTime must be >= 0 and Duration > 0; got trigger=%.3f duration=%.3f"),
				TriggerTime, Duration)));
	}
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}
	UClass* NotifyCls = TryLoadNotifyClass(NotifyClass);
	if (!NotifyCls)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load notify state class: %s"), *NotifyClass)));
	}
	// Type validation (Issue #9489 commit 2.7 — live test 2026-05-12 crashed
	// UE Editor when AddNotifyState was called with a UAnimNotify subclass:
	// NewObject<UAnimNotifyState>(Montage, NotifyCls) with NotifyCls actually
	// being a UAnimNotify subclass triggers a CastChecked failure inside the
	// engine's serialization. Strict check before allocating.
	if (!NotifyCls->IsChildOf(UAnimNotifyState::StaticClass()))
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("NotifyClass '%s' is not a UAnimNotifyState subclass"), *NotifyClass),
			TEXT("add_notify_state accepts UAnimNotifyState subclasses (ANS_*); for one-shot UAnimNotify (AN_*) use add_notify instead.")));
	}

	// Issue #23577: accept exact TrackName + synthetic "Track N" + numeric index.
	const int32 TrackIndex = ResolveMontageNotifyTrackIndex(Montage, Track);
	if (TrackIndex == INDEX_NONE)
	{
		// Issue #14776: surface the live available track names.
		return SerializeJsonResponse(MakeTrackNotFoundError(AssetPath, Track, Montage));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: add_notify_state on %s"), *AssetPath)));
	Montage->Modify();
#endif

	FAnimNotifyEvent NewNotify;
	NewNotify.SetTime(TriggerTime);
	NewNotify.SetDuration(Duration);
	NewNotify.TrackIndex = TrackIndex;
	NewNotify.NotifyName = NotifyCls->GetFName();
	NewNotify.NotifyStateClass = NewObject<UAnimNotifyState>(Montage, NotifyCls);
	const int32 NewIndex = Montage->Notifies.Add(NewNotify);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("notify_index"), NewIndex);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_RemoveNotify(
	const FString& AssetPath,
	int32 Index,
	const FString& Track,
	float TriggerTime,
	bool bUseIndex)
{
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}

	int32 ResolvedIndex = INDEX_NONE;
	if (bUseIndex)
	{
		if (Index < 0 || Index >= Montage->Notifies.Num())
		{
			return SerializeJsonResponse(MakeErrorObject(AssetPath,
				FString::Printf(TEXT("Notify index %d out of range (size=%d)"),
					Index, Montage->Notifies.Num())));
		}
		ResolvedIndex = Index;
	}
	else
	{
		// Issue #23577: accept exact TrackName + synthetic "Track N" + numeric index.
		const int32 TrackIdx = ResolveMontageNotifyTrackIndex(Montage, Track);
		if (TrackIdx == INDEX_NONE)
		{
			// Issue #14776: surface the live available track names.
			return SerializeJsonResponse(MakeTrackNotFoundError(AssetPath, Track, Montage));
		}
		TArray<int32> Matches;
		for (int32 i = 0; i < Montage->Notifies.Num(); ++i)
		{
			if (Montage->Notifies[i].TrackIndex == TrackIdx
				&& FMath::IsNearlyEqual(Montage->Notifies[i].GetTime(), TriggerTime, 1e-4f))
			{
				Matches.Add(i);
			}
		}
		if (Matches.Num() == 0)
		{
			return SerializeJsonResponse(MakeErrorObject(AssetPath,
				FString::Printf(TEXT("No notify matches (track='%s', trigger_time=%.3f)"), *Track, TriggerTime)));
		}
		if (Matches.Num() > 1)
		{
			TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetBoolField(TEXT("success"), false);
			Result->SetStringField(TEXT("asset_path"), AssetPath);
			Result->SetStringField(TEXT("error"),
				FString::Printf(TEXT("Multiple notifies match (track='%s', trigger_time=%.3f) — pass `index` to disambiguate"), *Track, TriggerTime));
			TArray<TSharedPtr<FJsonValue>> MatchArr;
			for (int32 m : Matches)
			{
				MatchArr.Add(MakeShared<FJsonValueNumber>(m));
			}
			Result->SetArrayField(TEXT("matches"), MatchArr);
			return SerializeJsonResponse(Result);
		}
		ResolvedIndex = Matches[0];
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: remove_notify on %s"), *AssetPath)));
	Montage->Modify();
#endif

	Montage->Notifies.RemoveAt(ResolvedIndex);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("notify_index"), ResolvedIndex);
	Result->SetBoolField(TEXT("removed"), true);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimAM_SetBlendInOut(
	const FString& AssetPath,
	float BlendIn,
	float BlendOut,
	const FString& Option)
{
	if (BlendIn < 0.0f || BlendOut < 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("BlendIn and BlendOut must be >= 0; got in=%.3f out=%.3f"), BlendIn, BlendOut)));
	}
	UAnimMontage* Montage = LoadMontage(AssetPath);
	if (!Montage)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimMontage: %s"), *AssetPath)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: set_blend_in_out on %s"), *AssetPath)));
	Montage->Modify();
#endif

	Montage->BlendIn.SetBlendTime(BlendIn);
	Montage->BlendOut.SetBlendTime(BlendOut);

	if (!Option.IsEmpty())
	{
		if (Option == TEXT("Linear")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::Linear); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::Linear); }
		else if (Option == TEXT("Cubic")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::Cubic); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::Cubic); }
		else if (Option == TEXT("HermiteCubic")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::HermiteCubic); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::HermiteCubic); }
		else if (Option == TEXT("Sinusoidal")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::Sinusoidal); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::Sinusoidal); }
		else if (Option == TEXT("QuadraticInOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::QuadraticInOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::QuadraticInOut); }
		else if (Option == TEXT("CubicInOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::CubicInOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::CubicInOut); }
		else if (Option == TEXT("QuarticInOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::QuarticInOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::QuarticInOut); }
		else if (Option == TEXT("QuinticInOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::QuinticInOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::QuinticInOut); }
		else if (Option == TEXT("CircularIn")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::CircularIn); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::CircularIn); }
		else if (Option == TEXT("CircularOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::CircularOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::CircularOut); }
		else if (Option == TEXT("CircularInOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::CircularInOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::CircularInOut); }
		else if (Option == TEXT("ExpIn")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::ExpIn); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::ExpIn); }
		else if (Option == TEXT("ExpOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::ExpOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::ExpOut); }
		else if (Option == TEXT("ExpInOut")) { Montage->BlendIn.SetBlendOption(EAlphaBlendOption::ExpInOut); Montage->BlendOut.SetBlendOption(EAlphaBlendOption::ExpInOut); }
		// Unknown options are silently ignored so future UE enum additions
		// don't break this op.
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	TSharedRef<FJsonObject> Updated = MakeShared<FJsonObject>();
	Updated->SetNumberField(TEXT("blend_in"), BlendIn);
	Updated->SetNumberField(TEXT("blend_out"), BlendOut);
	Updated->SetStringField(TEXT("option"), Option);
	Result->SetObjectField(TEXT("updated"), Updated);
	MarkDirtyAndSave(Montage, Result);
	return SerializeJsonResponse(Result);
}

// ---------------------------------------------------------------------------
// Issue #16573 — create_montage: AnimSequence → AnimMontage.
//
// Mirrors the editor's "Create AnimMontage" asset action: a single
// UAnimMontageFactory::FactoryCreateNew call with SourceAnimation set embeds
// the sequence into the auto-generated DefaultSlot and a "Default" section,
// and auto-derives TargetSkeleton from the sequence. We deliberately set ONLY
// SourceAnimation — the factory hard-asserts
// `check(TargetSkeleton==NULL || TargetSkeleton==SourceSkeleton)`, so setting a
// separate skeleton risks crashing the Editor.
// ---------------------------------------------------------------------------
FString UCURemoteLibrary::AnimAM_CreateMontage(
	const FString& SourceAnimSequencePath,
	const FString& DestinationPath)
{
#if WITH_EDITOR
	UAnimSequence* SourceSeq = Cast<UAnimSequence>(
		StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *SourceAnimSequencePath));
	if (!SourceSeq)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("Failed to load source AnimSequence: %s"), *SourceAnimSequencePath),
			TEXT("source_anim_path must be a /Game/... AnimSequence path (e.g. /Game/Anim/A_Idle.A_Idle)")));
	}

	// Normalize destination → long package name (drop any `.ObjectName` suffix).
	FString PackageName = DestinationPath.Contains(TEXT("."))
		? FPackageName::ObjectPathToPackageName(DestinationPath)
		: DestinationPath;
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("Invalid destination package name: %s"), *PackageName),
			TEXT("destination_path must be a long package name under /Game (e.g. /Game/Anim/AM_Reload)")));
	}
	if (FPackageName::DoesPackageExist(PackageName))
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("An asset already exists at %s"), *PackageName),
			TEXT("Choose a destination_path that does not yet exist, or delete the existing asset first")));
	}
	const FString AssetName = FPackageName::GetShortName(PackageName);
	// RC UFUNCTIONs are directly invokable, bypassing the Rust param gate (cf.
	// #15755), so reject a trailing-slash/empty leaf here — otherwise
	// FactoryCreateNew(FName("")) would auto-name a garbage asset.
	if (AssetName.TrimStartAndEnd().IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("Could not derive an asset name from %s"), *PackageName),
			TEXT("destination_path must end in a non-empty asset name (e.g. /Game/Anim/AM_Reload)")));
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("CreatePackage failed for %s"), *PackageName)));
	}
	// Issue #1699: FullyLoad a freshly-created package before SavePackage to avoid
	// the 'partially loaded' save crash (the sibling creation paths
	// HandleCreateAsset / CUCameraRigAuthor do this; the shared MarkDirtyAndSave
	// skips it because the mutation ops operate on already-loaded assets).
	Package->FullyLoad();

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->SourceAnimation = SourceSeq;  // TargetSkeleton auto-derived; do NOT set.

	UObject* NewAsset = Factory->FactoryCreateNew(
		UAnimMontage::StaticClass(), Package, FName(*AssetName),
		RF_Public | RF_Standalone, nullptr, GWarn);
	if (!NewAsset)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			TEXT("UAnimMontageFactory::FactoryCreateNew returned nullptr"),
			TEXT("The source AnimSequence may lack a valid Skeleton")));
	}

	FAssetRegistryModule::AssetCreated(NewAsset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), PackageName);
	Result->SetStringField(TEXT("source_anim"), SourceAnimSequencePath);
	Result->SetStringField(TEXT("slot"), TEXT("DefaultSlot"));
	Result->SetStringField(TEXT("section"), TEXT("Default"));
	MarkDirtyAndSave(NewAsset, Result);
	return SerializeJsonResponse(Result);
#else
	return SerializeJsonResponse(MakeErrorObject(
		DestinationPath,
		TEXT("create_montage requires a WITH_EDITOR build (UAnimMontageFactory is editor-only)")));
#endif
}
