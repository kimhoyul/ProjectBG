// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #14852 — AnimSequence sync-marker authoring. Two UFUNCTIONs on
// UCURemoteLibrary (declared in `Public/CURemoteLibrary.h`) wrapping the 7
// UAnimationBlueprintLibrary sync-marker functions, sharing the file-local
// helpers in `CUAnimAuthoring_Shared.h` with the #14795 notify handlers:
//
//   - AnimSeq_AddSyncMarker      → AddAnimationSyncMarker
//   - AnimSeq_RemoveSyncMarker   → RemoveAnimationSyncMarkersByName /
//                                  RemoveAnimationSyncMarkersByTrack /
//                                  RemoveAllAnimationSyncMarkers  (mode dispatch)
//
// (The read fns GetAnimationSyncMarkers / ...ForTrack / GetUniqueMarkerNames /
//  IsValidAnimationSyncMarkerName are served OFFLINE from the binary parser's
//  `anim_sync_markers[]` extraction — Rule 20 — by the Rust `get_sync_markers`
//  and `is_valid_sync_marker_name` ops, so no Editor round-trip is needed for
//  reads.)
//
// AuthoredSyncMarkers live on UAnimSequence (the BlueprintLibrary fns take a
// UAnimSequence*, not the AnimSequenceBase), so these ops are AnimSequence-only;
// the offline extractor still surfaces markers for any AnimSequenceBase. The add
// path mirrors AnimSeq_AddNotify: validate Time>=0 BEFORE the editor gate, load,
// transact + Modify, mutate via the engine library, re-find the new marker's
// serialized index, then MarkDirtyAndSave. Campaign #14709 E03 evidence.

#include "CUAnimAuthoring_Shared.h"

#include "AnimationBlueprintLibrary.h"
#include "Animation/AnimSequence.h"

using namespace NarshaAnimAuthoring;

namespace
{
	/** Load a UAnimSequence (AuthoredSyncMarkers authoring is UAnimSequence-typed). */
	static UAnimSequence* LoadAnimSequence(const FString& AssetPath)
	{
		return LoadObject<UAnimSequence>(nullptr, *AssetPath);
	}
}

FString UCURemoteLibrary::AnimSeq_AddSyncMarker(
	const FString& AssetPath,
	const FString& MarkerName,
	float Time,
	const FString& NotifyTrack)
{
	if (Time < 0.0f)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Time must be >= 0, got %.3f"), Time)));
	}
	if (MarkerName.IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			TEXT("MarkerName must be non-empty")));
	}
	if (NotifyTrack.IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			TEXT("NotifyTrack must be non-empty — sync markers attach to a notify track"),
			TEXT("Create a track first via add_notify_track, then pass its name as notify_track.")));
	}
	UAnimSequence* Seq = LoadAnimSequence(AssetPath);
	if (!Seq)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AssetPath),
			TEXT("Sync-marker authoring targets UAnimSequence (not AnimMontage/AnimComposite).")));
	}

	// The engine's AddAnimationSyncMarker gates on IsValidTimeInternal
	// (0 <= Time <= GetPlayLength()) and silently no-ops past it — validate the
	// upper bound here so the caller gets a precise error, not the misleading
	// post-add "track invalid?" fallback (ns-review C-1, Issue #14852).
	const float PlayLength = Seq->GetPlayLength();
	if (Time > PlayLength)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Time %.3f exceeds sequence play length %.3f"), Time, PlayLength),
			TEXT("Sync-marker time must be within [0, play_length].")));
	}

	const FName Marker(*MarkerName);
	const FName Track(*NotifyTrack);

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("NarshaMCP: add_sync_marker on %s"), *AssetPath)));
	Seq->Modify();
#endif

	UAnimationBlueprintLibrary::AddAnimationSyncMarker(Seq, Marker, Time, Track);

	// Re-find the just-added marker by (name, time) so the returned marker_index
	// matches the serialized order the offline reparse-verify reads back. A
	// missing match means the engine rejected the add (e.g. the notify track does
	// not exist) — surface that as success=false rather than a silent no-op.
	int32 MarkerIndex = INDEX_NONE;
	for (int32 i = 0; i < Seq->AuthoredSyncMarkers.Num(); ++i)
	{
		const FAnimSyncMarker& M = Seq->AuthoredSyncMarkers[i];
		if (M.MarkerName == Marker && FMath::IsNearlyEqual(M.Time, Time, 1e-4f))
		{
			MarkerIndex = i;
			break;
		}
	}
	if (MarkerIndex == INDEX_NONE)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Sync marker '%s' was not added — notify track '%s' not found on the sequence"),
				*MarkerName, *NotifyTrack),
			TEXT("Ensure the notify track exists (add_notify_track) before adding a sync marker.")));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("marker_index"), MarkerIndex);
	Result->SetStringField(TEXT("marker_name"), MarkerName);
	Result->SetNumberField(TEXT("time"), Time);
	Result->SetStringField(TEXT("track"), NotifyTrack);
	MarkDirtyAndSave(Seq, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimSeq_RemoveSyncMarker(
	const FString& AssetPath,
	const FString& MarkerName,
	const FString& NotifyTrack,
	const FString& Mode)
{
	// Mode dispatch: name (by MarkerName) | track (by NotifyTrack) | all.
	const FString ResolvedMode = Mode.IsEmpty() ? TEXT("name") : Mode.ToLower();
	const bool bModeName = ResolvedMode == TEXT("name");
	const bool bModeTrack = ResolvedMode == TEXT("track");
	const bool bModeAll = ResolvedMode == TEXT("all");
	if (!bModeName && !bModeTrack && !bModeAll)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Unknown remove mode '%s'"), *Mode),
			TEXT("mode must be one of: name | track | all")));
	}
	if (bModeName && MarkerName.IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			TEXT("MarkerName must be non-empty for mode=name")));
	}
	if (bModeTrack && NotifyTrack.IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			TEXT("NotifyTrack must be non-empty for mode=track")));
	}
	UAnimSequence* Seq = LoadAnimSequence(AssetPath);
	if (!Seq)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AssetPath)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("NarshaMCP: remove_sync_marker (%s) on %s"), *ResolvedMode, *AssetPath)));
	Seq->Modify();
#endif

	int32 RemovedCount = 0;
	if (bModeName)
	{
		RemovedCount = UAnimationBlueprintLibrary::RemoveAnimationSyncMarkersByName(Seq, FName(*MarkerName));
	}
	else if (bModeTrack)
	{
		RemovedCount = UAnimationBlueprintLibrary::RemoveAnimationSyncMarkersByTrack(Seq, FName(*NotifyTrack));
	}
	else // all — engine fn returns void, so snapshot the count first.
	{
		RemovedCount = Seq->AuthoredSyncMarkers.Num();
		UAnimationBlueprintLibrary::RemoveAllAnimationSyncMarkers(Seq);
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetStringField(TEXT("mode"), ResolvedMode);
	Result->SetNumberField(TEXT("removed_count"), RemovedCount);
	MarkDirtyAndSave(Seq, Result);
	return SerializeJsonResponse(Result);
}
