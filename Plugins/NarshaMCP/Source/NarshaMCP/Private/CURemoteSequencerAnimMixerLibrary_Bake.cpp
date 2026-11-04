// Copyright 2026 Next Stage Inc. All rights reserved.

// Issue #27002 (AnimMixer Phase 5d) -- bake the Sequencer evaluation of one
// binding into a NEW AnimSequence asset, so the Phase 5a-5c correction stack
// (mixer layers + BlendMask + additive Control Rig) can be handed to
// ue_manage_animation(create_montage) as an ordinary animation asset.
//
// WHY THE HEADLESS EXPORT, NOT UControlRigSequencerEditorLibrary
// --------------------------------------------------------------
// The issue originally specified ExportAnimSequenceFromSequencer, which needs an
// open Sequencer. That entry point is not merely restrictive, it is unsafe: with
// no sequence open, GetSequencerFromAsset() formats its own error message with
// LevelSequence->GetPathName() on a null LevelSequence and CRASHES the editor
// (ControlRigSequencerEditorLibrary.cpp:202-214, live-verified on 5.8.1). It never
// reaches its own IsValid() guard at :1170.
//
// USequencerToolsFunctionLibrary::ExportAnimSequence is the engine's documented
// no-open-Sequencer path ("use this when you want to export without an open Level
// Sequence in Sequencer"). It builds its own transient ALevelSequenceActor +
// ULevelSequencePlayer, evaluates, and destroys the actor (SequencerTools.cpp:388-467;
// the actor is RF_Transient per LevelSequencePlayer.cpp:66, so the level is never
// dirtied). Reading that was not taken as proof -- a live spike measured that an
// AnimMixer layer AND an additive Control Rig both land in the bake, and compose:
// with +60 deg keyed on upperarm_l, baked-minus-source was exactly +60.00 on that
// bone and 0.00 on the control bone, at every sampled frame. Evidence:
// docs/evidence/issues/27002/.

#include "CURemoteSequencerAnimMixerLibrary.h"
#include "CURemoteSequencerAnimMixerInternals.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_EDITOR && WITH_NARSHAMCP_SEQUENCERSCRIPTING
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimTypes.h"
#include "Animation/Skeleton.h"
#include "Editor.h"
#include "Exporters/AnimSeqExportOption.h"
#include "Factories/AnimSequenceFactory.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "MovieSceneAnimationMixerLayer.h"
#include "MovieSceneAnimationMixerTrack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "MovieSceneBindingProxy.h"
#include "PackageTools.h"
#include "SequencerTools.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPAnimMixerBake, Log, All);

using NarshaAnimMixerInternal::MakeErrorJson;
using NarshaAnimMixerInternal::MakeUnsupported;
using NarshaAnimMixerInternal::SerializeJson;

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_EDITOR && WITH_NARSHAMCP_SEQUENCERSCRIPTING
using NarshaAnimMixerInternal::LoadLevelSequence;

namespace
{
	/** Provenance schema. Bump only when the compared key set changes meaning. */
	const TCHAR* const kProvSchema = TEXT("1");
	const TCHAR* const kProvPrefix = TEXT("NarshaMCP.Bake.");

	/**
	 * Frame ceiling for one bake.
	 *
	 * MovieSceneToolHelpers::ExportToAnimSequence ticks the whole evaluation once
	 * per frame (MovieSceneToolHelpers.cpp:4315-4321), so cost is linear in frame
	 * count. Measured on 5.8.1 / Lyra: ~4 ms/frame (31 frames in 0.109-0.135 s,
	 * docs/evidence/issues/27002/). 3600 frames is 2 minutes of 30 fps animation
	 * and lands around 15 s -- comfortably inside the 30 s Remote Control leg, with
	 * room for a slower host. Callers who need more pass a narrower
	 * start_frame/end_frame rather than waiting on a transport timeout.
	 */
	constexpr int32 kMaxBakeFrames = 3600;

	/**
	 * Every key accepted inside export_options. Anything else is an error: a typo
	 * such as "warmup_frames" must not silently evaluate to "no warm-up".
	 */
	const TCHAR* const kKnownOptionKeys[] = {
		TEXT("start_frame"), TEXT("end_frame"), TEXT("export_transforms"),
		TEXT("export_morph_targets"), TEXT("export_attribute_curves"),
		TEXT("export_material_curves"), TEXT("record_in_world_space"),
		TEXT("warm_up_frames"), TEXT("delay_before_start"),
	};

	FString ProvKey(const TCHAR* Leaf)
	{
		return FString(kProvPrefix) + Leaf;
	}

	/**
	 * Clamp a JSON-supplied number to int32 before it reaches a frame count or a
	 * loop bound (Rule 27).
	 */
	int32 ClampToInt32(double Value)
	{
		const double Lo = static_cast<double>(TNumericLimits<int32>::Lowest());
		const double Hi = static_cast<double>(TNumericLimits<int32>::Max());
		return static_cast<int32>(FMath::Clamp(Value, Lo, Hi));
	}

	FString HashFileSha(const FString& Filename)
	{
		TArray<uint8> Bytes;
		if (Filename.IsEmpty() || !FFileHelper::LoadFileToArray(Bytes, *Filename))
		{
			return FString();
		}
		FSHA1 Sha;
		Sha.Update(Bytes.GetData(), Bytes.Num());
		Sha.Final();
		uint8 Digest[20];
		Sha.GetHash(Digest);
		FString Out;
		for (int32 Index = 0; Index < 20; ++Index)
		{
			Out += FString::Printf(TEXT("%02x"), Digest[Index]);
		}
		return Out;
	}

	FString PackageFileOnDisk(const FString& PackageName)
	{
		FString Filename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(
				PackageName, Filename, FPackageName::GetAssetPackageExtension()))
		{
			return FString();
		}
		return Filename;
	}

	/**
	 * Detach an in-memory asset from its path WITHOUT touching the file on disk.
	 *
	 * Renaming into the transient package frees the object path, which is all the
	 * subsequent CreateAsset needs. Deliberately no CollectGarbage: a full purge
	 * called from inside an RC/Python callback deadlocked the game thread against a
	 * queued async load (live-observed on Lyra -- the editor stopped answering after
	 * "Compacting FUObjectHashTables" / "FlushAsyncLoading"), and the rename alone
	 * already does the job.
	 */
	/**
	 * Move an existing asset out of the way so CreateAsset can take its path, keeping
	 * enough state to put it back.
	 *
	 * NOT marked garbage: if the bake then fails, the caller's previous asset has to be
	 * restorable. Leaving it detached-and-garbage poisons the path for the rest of the
	 * editor session -- the package object still exists so nothing reloads from disk, and
	 * the next retry sees an asset with no provenance and refuses it as foreign. That is
	 * an eventual-permanent failure, which is exactly what the retry contract forbids.
	 */
	void DetachAssetFromPath(UObject* Asset, UPackage* Package)
	{
		if (Asset)
		{
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
		}
		if (Package)
		{
			Package->SetDirtyFlag(false);
		}
	}

	/** Undo DetachAssetFromPath. Best-effort: reports whether the object is back. */
	bool RestoreDetachedAsset(UObject* Asset, UPackage* Package, const FName& OriginalName,
		bool bPackageWasDirty)
	{
		if (!Asset || !Package)
		{
			return false;
		}
		Asset->Rename(*OriginalName.ToString(), Package,
			REN_DontCreateRedirectors | REN_NonTransactional);
		Asset->SetFlags(RF_Public | RF_Standalone);
		// DetachAssetFromPath cleared this; a rolled-back bake must not make the editor
		// forget unrelated unsaved edits the package already had.
		Package->SetDirtyFlag(bPackageWasDirty);
		return Asset->GetOuter() == Package;
	}

	/**
	 * Detach a half-built asset AND remove the file, for the paths where this call is
	 * the one that created it. Mirrors AnimMixerCreateBlendMask's rollback. Callers pass
	 * an empty FileToDelete when the destination pre-existed -- see RollbackFileToDelete.
	 */
	void OrphanFailedAsset(UObject* Asset, UPackage* Package, const FString& FileToDelete)
	{
		if (!FileToDelete.IsEmpty())
		{
			IFileManager::Get().Delete(*FileToDelete, /*RequireExists=*/false,
				/*EvenReadOnly=*/true, /*Quiet=*/true);
		}
		if (Asset)
		{
			Asset->ClearFlags(RF_Public | RF_Standalone);
			Asset->Rename(nullptr, GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			Asset->MarkAsGarbage();
		}
		if (Package)
		{
			Package->SetDirtyFlag(false);
		}
	}

	/**
	 * One bake at a time on the game thread. SavePackage can pump Slate, so a
	 * second RC call could otherwise re-enter mid-save.
	 */
	bool GBakeInProgress = false;

	/**
	 * Resolve the skeleton the destination AnimSequence must be created with.
	 *
	 * This is not optional bookkeeping: UAnimSequence's data model builds an
	 * FKControlRig in PostInitProperties and fails hard without a skeleton
	 * ("Unable to initialize RigHierarchy due to missing USkeleton"), so the asset
	 * cannot even be created first and skeletoned later. Live-verified on 5.8.1.
	 *
	 * The engine overwrites the skeleton during export anyway
	 * (SequencerTools.cpp:427), so the value here only has to be right enough to
	 * construct the asset -- and resolving it from the same binding the export will
	 * use is the way to make those two agree.
	 */
	USkeleton* ResolveSkeletonForBinding(ULevelSequence* Sequence, UMovieScene* MovieScene,
		const FGuid& BindingGuid, UObject* PlaybackContext, TArray<FString>& OutTrail)
	{
		auto FromGuid = [&](const FGuid& Guid) -> USkeleton*
		{
			TArray<UObject*, TInlineAllocator<1>> Bound;
			// Issue #27373 migrated the sibling resolver to the surviving 4-arg overload;
			// this one follows so the bake path does not re-introduce the deprecation.
			// The SharedPlaybackState is optional and UE 5.8.1's base implementation
			// discards it (MovieSceneSequence.cpp:70-88 forwards nullptr from the
			// deprecated form and resolves through the locator-only
			// FMovieSceneBindingReferences::ResolveBinding either way), so nullptr is
			// behaviourally identical to the old call and merely silences C4996.
			const UE::UniversalObjectLocator::FResolveParams ResolveParams(PlaybackContext);
			Sequence->LocateBoundObjects(Guid, ResolveParams, nullptr, Bound);
			OutTrail.Add(FString::Printf(TEXT("%s -> %d object(s)"), *Guid.ToString(), Bound.Num()));
			for (UObject* Obj : Bound)
			{
				USkeletalMeshComponent* Comp = Cast<USkeletalMeshComponent>(Obj);
				if (!Comp)
				{
					if (AActor* AsActor = Cast<AActor>(Obj))
					{
						Comp = AsActor->FindComponentByClass<USkeletalMeshComponent>();
					}
				}
				if (Comp && Comp->GetSkeletalMeshAsset())
				{
					return Comp->GetSkeletalMeshAsset()->GetSkeleton();
				}
			}
			return nullptr;
		};

		if (USkeleton* Direct = FromGuid(BindingGuid))
		{
			return Direct;
		}
		// The caller may have named the ACTOR binding while the skeletal mesh lives on
		// a possessable child component binding.
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			const FMovieScenePossessable& Poss = MovieScene->GetPossessable(Index);
			if (Poss.GetParent() == BindingGuid)
			{
				if (USkeleton* Child = FromGuid(Poss.GetGuid()))
				{
					return Child;
				}
			}
		}
		return nullptr;
	}

	bool MixerHasContent(const UMovieSceneAnimationMixerTrack* Mixer)
	{
		if (!Mixer)
		{
			return false;
		}
		// InsertLayer (anim_mixer_add_layer) creates an empty row. A leftover
		// master mixer with the real animation must still trip
		// mixer_track_root_scoped -- layer count alone is not content.
		return Mixer->GetAllSections().Num() > 0;
	}

	UMovieSceneAnimationMixerTrack* FindMixerOnGuid(
		UMovieScene* MovieScene, const FGuid& Guid)
	{
		if (!MovieScene)
		{
			return nullptr;
		}
		if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(Guid))
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (UMovieSceneAnimationMixerTrack* Mixer =
						Cast<UMovieSceneAnimationMixerTrack>(Track))
				{
					if (MixerHasContent(Mixer))
					{
						return Mixer;
					}
				}
			}
		}
		return nullptr;
	}

	/** Binding-scoped mixer, including a possessable child that holds the skel mesh. */
	UMovieSceneAnimationMixerTrack* FindMixerOnBindingOrChildren(
		UMovieScene* MovieScene, const FGuid& BindingGuid)
	{
		if (UMovieSceneAnimationMixerTrack* Direct = FindMixerOnGuid(MovieScene, BindingGuid))
		{
			return Direct;
		}
		for (int32 Index = 0; Index < MovieScene->GetPossessableCount(); ++Index)
		{
			const FMovieScenePossessable& Poss = MovieScene->GetPossessable(Index);
			if (Poss.GetParent() == BindingGuid)
			{
				if (UMovieSceneAnimationMixerTrack* Child =
						FindMixerOnGuid(MovieScene, Poss.GetGuid()))
				{
					return Child;
				}
			}
		}
		return nullptr;
	}

	UMovieSceneAnimationMixerTrack* FindFirstRootMixerWithContent(UMovieScene* MovieScene)
	{
		if (!MovieScene)
		{
			return nullptr;
		}
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			if (UMovieSceneAnimationMixerTrack* Mixer =
					Cast<UMovieSceneAnimationMixerTrack>(Track))
			{
				if (MixerHasContent(Mixer))
				{
					return Mixer;
				}
			}
		}
		return nullptr;
	}

	constexpr double kStaticPoseLocEpsilonCm = 0.01;
	constexpr double kStaticPoseRotEpsilonDeg = 0.05;
	// Scale is measured alongside translation and rotation: a scale-only squash or
	// stretch is real motion, and ignoring it would let bake_static_pose reject a
	// genuine bake. 1e-4 is stricter than the 1e-3 first proposed for the raw-track
	// path, and stricter is the safe direction here -- a smaller epsilon is easier
	// to clear, so it yields fewer false static verdicts, not more.
	constexpr double kStaticPoseScaleEpsilon = 0.0001;

	struct FBakeMotion
	{
		int32 SampledTracks = 0;
		int32 MovingTracks = 0;
		double MaxLocDeltaCm = 0.0;
		double MaxRotDeltaDeg = 0.0;
	};

	/**
	 * Does the baked pose actually change across its own keys?
	 *
	 * Sampling goes through GetBoneTrackTransforms, NOT GetBoneTrackByName. That is
	 * not a style choice: in stock 5.8 a UAnimSequence's model is
	 * UAnimationSequencerDataModel (the AnimationData plugin ships EnabledByDefault),
	 * and there GetBoneTrackByName returns a shared default-constructed static
	 * FBoneAnimationTrack for EVERY name (AnimSequencerDataModel.cpp:357-361). Its
	 * InternalTrackData is empty, so a key-count filter over it samples zero tracks
	 * and any guard gated on "sampled > 0" silently never fires. Measured on the
	 * frozen-bake fixture: this accessor reports 164 sampled / 0 moving and the
	 * reject fires; the raw-track accessor reports 0 sampled and it does not.
	 *
	 * Every bone track is compared against its OWN first key, so a rig that simply
	 * rests away from the reference pose is not mistaken for motion. Tracks with
	 * fewer than two keys cannot express motion and are not counted as sampled.
	 * Thresholds are 0.01 cm / 0.05 deg / 1e-4 scale -- an exact-zero test would be
	 * wrong, because the live frozen bake measured a rotation delta of 2.41e-06 deg
	 * rather than bit-exact zero.
	 */
	FBakeMotion MeasureBakeMotion(const IAnimationDataModel* Model)
	{
		FBakeMotion Out;
		if (!Model)
		{
			return Out;
		}
		TArray<FName> TrackNames;
		Model->GetBoneTrackNames(TrackNames);
		for (const FName& TrackName : TrackNames)
		{
			// Declared INSIDE the loop on purpose. UAnimationSequencerDataModel's
			// GetBoneTrackTransforms APPENDS with no SetNum or Reset, so a hoisted
			// buffer would make each track compare against the PREVIOUS track's first
			// key. Demonstrated on a frozen 3-track model to flip sampled 3 / moving 0
			// (reject fires) to moving 2 (reject passes).
			TArray<FTransform> Keys;
			Model->GetBoneTrackTransforms(TrackName, Keys);
			if (Keys.Num() < 2)
			{
				continue;
			}
			++Out.SampledTracks;
			const FTransform& First = Keys[0];
			bool bTrackMoves = false;
			for (int32 Index = 1; Index < Keys.Num(); ++Index)
			{
				const FTransform& Key = Keys[Index];
				const double LocDelta =
					static_cast<double>(FVector::Dist(First.GetTranslation(), Key.GetTranslation()));
				const double RotDelta = FMath::RadiansToDegrees(
					static_cast<double>(First.GetRotation().AngularDistance(Key.GetRotation())));
				const double ScaleDelta =
					static_cast<double>(FVector::Dist(First.GetScale3D(), Key.GetScale3D()));
				Out.MaxLocDeltaCm = FMath::Max(Out.MaxLocDeltaCm, LocDelta);
				Out.MaxRotDeltaDeg = FMath::Max(Out.MaxRotDeltaDeg, RotDelta);
				if (LocDelta > kStaticPoseLocEpsilonCm
					|| RotDelta > kStaticPoseRotEpsilonDeg
					|| ScaleDelta > kStaticPoseScaleEpsilon)
				{
					bTrackMoves = true;
				}
			}
			if (bTrackMoves)
			{
				++Out.MovingTracks;
			}
		}
		return Out;
	}
}
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_EDITOR && WITH_NARSHAMCP_SEQUENCERSCRIPTING

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerBakeToAnimSequence(
	const FString& SequencePath, const FString& AssetPath, const FString& BindingId,
	const FString& SkeletonPath, const FString& ExportOptionsJson, bool bOverwrite,
	bool bAllowStaticPose)
{
#if !NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeUnsupported(TEXT("AnimMixerBakeToAnimSequence"));
#elif !WITH_EDITOR
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetBoolField(TEXT("editor_required"), true);
		Obj->SetStringField(TEXT("error"),
			TEXT("AnimMixerBakeToAnimSequence requires an editor build"));
		return SerializeJson(Obj);
	}
#elif !WITH_NARSHAMCP_SEQUENCERSCRIPTING
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"),
			TEXT("AnimMixerBakeToAnimSequence requires the optional SequencerScripting plugin"));
		Obj->SetStringField(TEXT("error_code"), TEXT("sequencer_scripting_plugin_unavailable"));
		Obj->SetStringField(TEXT("hint"),
			TEXT("Enable SequencerScripting in your .uproject, then rebuild."));
		return SerializeJson(Obj);
	}
#else
	UE_LOG(LogNarshaMCPAnimMixerBake, Log,
		TEXT("[AnimMixer] BakeToAnimSequence seq=%s asset=%s binding=%s overwrite=%d allow_static=%d"),
		*SequencePath, *AssetPath, *BindingId, bOverwrite ? 1 : 0, bAllowStaticPose ? 1 : 0);

	if (GBakeInProgress)
	{
		return MakeErrorJson(
			TEXT("Another AnimMixerBakeToAnimSequence call is still running in this editor"),
			TEXT("bake_in_progress"));
	}
	GBakeInProgress = true;
	ON_SCOPE_EXIT { GBakeInProgress = false; };

	// --- sequence + movie scene
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return MakeErrorJson(
			FString::Printf(TEXT("Failed to load LevelSequence: %s"), *SequencePath));
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return MakeErrorJson(TEXT("LevelSequence has no MovieScene"));
	}

	// --- binding. Deliberately required: the ops that build the correction stack
	// (anim_mixer_add_control_rig_layer, anim_mixer_get_layers) already return the
	// GUID, and pre-resolving it here would reject spawnable bindings that the
	// engine's own player resolves fine (SequencerTools.cpp:422 resolves through
	// the player, not the editor world).
	if (BindingId.IsEmpty())
	{
		return MakeErrorJson(
			TEXT("binding_id is required; take it from anim_mixer_add_control_rig_layer "
				 "or anim_mixer_get_layers"),
			TEXT("binding_required"));
	}
	FGuid BindingGuid;
	if (!FGuid::Parse(BindingId, BindingGuid))
	{
		return MakeErrorJson(
			FString::Printf(TEXT("binding_id is not a GUID: %s"), *BindingId),
			TEXT("binding_parse_failed"));
	}
	if (!MovieScene->FindBinding(BindingGuid))
	{
		return MakeErrorJson(
			FString::Printf(TEXT("No binding %s in this LevelSequence"), *BindingId),
			TEXT("binding_not_found"));
	}

	// Root-scope mixer tracks do not evaluate against a binding. Export still
	// returns true and writes a rest-pose AnimSequence. Explicit master/root
	// remains a compatibility path even though omitted add_layer/add_animation
	// selectors now resolve the unique binding-owned mixer through "auto".
	// Refuse before any asset work. allow_static_pose does not unlock this; the
	// mixer is on the wrong object.
	if (UMovieSceneAnimationMixerTrack* RootMixer =
			FindFirstRootMixerWithContent(MovieScene))
	{
		if (!FindMixerOnBindingOrChildren(MovieScene, BindingGuid))
		{
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"),
				TEXT("AnimMixer track is on the sequence root (master), so it will not "
					 "evaluate against this binding; the bake would be a rest pose"));
			Err->SetStringField(TEXT("error_code"), TEXT("mixer_track_root_scoped"));
			Err->SetStringField(TEXT("root_track_name"), RootMixer->GetFName().ToString());
			Err->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
			Err->SetStringField(TEXT("hint"),
				TEXT("Create MovieSceneAnimationMixerTrack with binding_id, then omit "
					 "track_id (auto) or pass that binding ID to the anim_mixer authoring ops"));
			return SerializeJson(Err);
		}
	}

	// --- destination path
	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	FText InvalidReason;
	if (!PackageName.StartsWith(TEXT("/Game/")) ||
		!FPackageName::IsValidLongPackageName(PackageName, false, &InvalidReason))
	{
		return MakeErrorJson(
			FString::Printf(TEXT("asset_path must be a valid /Game package path: %s"),
				*InvalidReason.ToString()),
			TEXT("invalid_asset_path"));
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	if (AssetName.IsEmpty())
	{
		return MakeErrorJson(TEXT("asset_path must include an asset name"),
			TEXT("invalid_asset_path"));
	}
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
	const FString PackageFilename = PackageFileOnDisk(PackageName);
	if (PackageFilename.IsEmpty())
	{
		return MakeErrorJson(
			FString::Printf(TEXT("Cannot map %s to a filename"), *PackageName),
			TEXT("invalid_asset_path"));
	}

	// --- export options
	// Held by a strong pointer, not a bare NewObject: this object has no outer and no
	// referencer, so any GC between here and the export could collect it. The overwrite
	// path used to run a full-purge CollectGarbage right in that gap (removed — it
	// deadlocked the game thread); the holder stays because the exposure is structural,
	// not specific to that one call.
	TStrongObjectPtr<UAnimSeqExportOption> OptionsHolder(NewObject<UAnimSeqExportOption>());
	UAnimSeqExportOption* Options = OptionsHolder.Get();
	Options->bExportTransforms = true;
	Options->bExportMorphTargets = false;
	Options->bExportAttributeCurves = false;
	Options->bExportMaterialCurves = false;
	// The engine's own transaction would wrap a bake we already roll back by hand.
	Options->bTransactRecording = false;

	bool bCustomRange = false;
	int32 StartFrame = 0;
	int32 EndFrame = 0;
	if (!ExportOptionsJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> Opts;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ExportOptionsJson);
		if (!FJsonSerializer::Deserialize(Reader, Opts) || !Opts.IsValid())
		{
			return MakeErrorJson(TEXT("export_options is not a JSON object"),
				TEXT("invalid_export_options"));
		}
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Opts->Values)
		{
			const FString OptionKey = FString(Pair.Key);
			bool bKnown = false;
			for (const TCHAR* Known : kKnownOptionKeys)
			{
				bKnown = bKnown || OptionKey.Equals(Known, ESearchCase::IgnoreCase);
			}
			if (!bKnown)
			{
				return MakeErrorJson(
					FString::Printf(TEXT("Unknown export_options key: %s"), *OptionKey),
					TEXT("unknown_export_option"));
			}
		}

		double Num = 0.0;
		bool bFlag = false;
		if (Opts->TryGetBoolField(TEXT("export_transforms"), bFlag)) { Options->bExportTransforms = bFlag; }
		if (Opts->TryGetBoolField(TEXT("export_morph_targets"), bFlag)) { Options->bExportMorphTargets = bFlag; }
		if (Opts->TryGetBoolField(TEXT("export_attribute_curves"), bFlag)) { Options->bExportAttributeCurves = bFlag; }
		if (Opts->TryGetBoolField(TEXT("export_material_curves"), bFlag)) { Options->bExportMaterialCurves = bFlag; }
		if (Opts->TryGetBoolField(TEXT("record_in_world_space"), bFlag)) { Options->bRecordInWorldSpace = bFlag; }
		if (Opts->TryGetNumberField(TEXT("warm_up_frames"), Num)) { Options->WarmUpFrames = FFrameNumber(ClampToInt32(Num)); }
		if (Opts->TryGetNumberField(TEXT("delay_before_start"), Num)) { Options->DelayBeforeStart = FFrameNumber(ClampToInt32(Num)); }

		const bool bHasStart = Opts->TryGetNumberField(TEXT("start_frame"), Num);
		if (bHasStart) { StartFrame = ClampToInt32(Num); }
		const bool bHasEnd = Opts->TryGetNumberField(TEXT("end_frame"), Num);
		if (bHasEnd) { EndFrame = ClampToInt32(Num); }
		if (bHasStart != bHasEnd)
		{
			return MakeErrorJson(
				TEXT("start_frame and end_frame must be given together"),
				TEXT("invalid_frame_range"));
		}
		bCustomRange = bHasStart;
		if (bCustomRange && EndFrame <= StartFrame)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("end_frame (%d) must be greater than start_frame (%d)"),
					EndFrame, StartFrame),
				TEXT("invalid_frame_range"));
		}
	}

	// Resolve the effective span so the cap is checked before any work, and so the
	// response can echo what was actually baked.
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	if (!bCustomRange)
	{
		// Same bound-access pattern the Phase 5c code uses, rather than the
		// UE::MovieScene::Discrete* helpers, so this TU needs no extra include.
		const FFrameRate Tick = MovieScene->GetTickResolution();
		const TRange<FFrameNumber> Playback = MovieScene->GetPlaybackRange();
		const FFrameNumber LowerTick =
			Playback.HasLowerBound() ? Playback.GetLowerBoundValue() : FFrameNumber(0);
		const FFrameNumber UpperTick =
			Playback.HasUpperBound() ? Playback.GetUpperBoundValue() : LowerTick;
		StartFrame = FFrameRate::TransformTime(FFrameTime(LowerTick), Tick, DisplayRate)
			.FloorToFrame().Value;
		EndFrame = FFrameRate::TransformTime(FFrameTime(UpperTick), Tick, DisplayRate)
			.CeilToFrame().Value;
	}
	else
	{
		Options->bUseCustomTimeRange = true;
		Options->CustomStartFrame = FFrameNumber(StartFrame);
		Options->CustomEndFrame = FFrameNumber(EndFrame);
		// CustomDisplayRate defaults to 30 fps (AnimSeqExportOption.h:110) and the
		// custom range is interpreted IN it (MovieSceneToolHelpers.cpp:4241-4245).
		// Leaving it at the default silently bakes the wrong span on a 24 or 60 fps
		// sequence, so it is pinned to the sequence's own display rate.
		Options->CustomDisplayRate = DisplayRate;
	}
	const int32 FrameCount = FMath::Max(0, EndFrame - StartFrame);
	if (FrameCount > kMaxBakeFrames)
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Bake span is %d frames; the per-call ceiling is %d"),
				FrameCount, kMaxBakeFrames));
		Err->SetStringField(TEXT("error_code"), TEXT("bake_range_too_long"));
		Err->SetNumberField(TEXT("frame_count"), FrameCount);
		Err->SetNumberField(TEXT("max_frames"), kMaxBakeFrames);
		Err->SetStringField(TEXT("hint"),
			TEXT("Pass export_options.start_frame and export_options.end_frame to bake a narrower span"));
		return SerializeJson(Err);
	}

	// --- provenance of this request, used for both the overwrite decision and the
	// idempotent retry after a client-side timeout.
	TMap<FString, FString> Prov;
	Prov.Add(ProvKey(TEXT("Schema")), kProvSchema);
	Prov.Add(ProvKey(TEXT("SourceSequence")), Sequence->GetPathName());
	Prov.Add(ProvKey(TEXT("BindingGuid")), BindingGuid.ToString());
	Prov.Add(ProvKey(TEXT("StartFrame")), FString::FromInt(StartFrame));
	Prov.Add(ProvKey(TEXT("EndFrame")), FString::FromInt(EndFrame));
	Prov.Add(ProvKey(TEXT("DisplayRate")), DisplayRate.ToPrettyText().ToString());
	// The pose check is part of what a bake MEANS, so the flag that waives it has to
	// be part of its identity. Without this the already_baked return above the export
	// hands a later default-guard call the earlier waived asset with success:true and
	// the guard never runs -- one opt-in would silence it permanently for this
	// (asset_path, params) tuple, which overwrite=true could not even force past
	// because the idempotent return precedes the overwrite branch. As a provenance
	// key it becomes a bake_params_mismatch with a provenance_diff instead. The
	// root-scope reject above is unaffected either way; this covers the other static
	// paths, e.g. a curve-only bake with export_transforms off.
	Prov.Add(ProvKey(TEXT("AllowStaticPose")), bAllowStaticPose ? TEXT("1") : TEXT("0"));

	// Same argument, applied to the rest of the request. Every one of these changes what
	// the engine WRITES -- export_transforms reaches FAnimationRecorder::bRecordTransforms
	// and record_in_world_space reaches bRecordLocalToWorld -- so two bakes that differ in
	// any of them are two different bakes. While they were absent from the identity, the
	// already_baked return above handed a caller who flipped record_in_world_space the
	// EARLIER asset with success:true, and bake_params_mismatch (which exists for exactly
	// that) was unreachable for 8 of the 11 request inputs -- only start_frame/end_frame
	// (StartFrame/EndFrame above) ever reached it.
	//
	// SkeletonPath is recorded as REQUESTED rather than as resolved: it is what the caller
	// controls, and a retry that repeats the same request repeats the same string, so the
	// timeout-recovery path is unaffected. Spelling the same skeleton differently between
	// two calls costs a bake_params_mismatch -- the conservative direction, since the
	// alternative is returning an asset built against a skeleton the caller did not name.
	Prov.Add(ProvKey(TEXT("ExportTransforms")), Options->bExportTransforms ? TEXT("1") : TEXT("0"));
	Prov.Add(ProvKey(TEXT("ExportMorphTargets")), Options->bExportMorphTargets ? TEXT("1") : TEXT("0"));
	Prov.Add(ProvKey(TEXT("ExportAttributeCurves")), Options->bExportAttributeCurves ? TEXT("1") : TEXT("0"));
	Prov.Add(ProvKey(TEXT("ExportMaterialCurves")), Options->bExportMaterialCurves ? TEXT("1") : TEXT("0"));
	Prov.Add(ProvKey(TEXT("RecordInWorldSpace")), Options->bRecordInWorldSpace ? TEXT("1") : TEXT("0"));
	Prov.Add(ProvKey(TEXT("WarmUpFrames")), FString::FromInt(Options->WarmUpFrames.Value));
	Prov.Add(ProvKey(TEXT("DelayBeforeStart")), FString::FromInt(Options->DelayBeforeStart.Value));
	Prov.Add(ProvKey(TEXT("SkeletonPath")), SkeletonPath);

	UPackage* SequencePackage = Sequence->GetOutermost();
	const bool bSourceDirty = SequencePackage && SequencePackage->IsDirty();
	Prov.Add(ProvKey(TEXT("SourceSequenceDirty")), bSourceDirty ? TEXT("1") : TEXT("0"));

	const FString SequenceFile =
		SequencePackage ? PackageFileOnDisk(SequencePackage->GetName()) : FString();
	const FString SequenceHashBefore = HashFileSha(SequenceFile);
	Prov.Add(ProvKey(TEXT("SourceSequenceHash")), SequenceHashBefore);

	// --- existing asset: reject, or return the identical prior bake unchanged.
	const bool bExistedOnDisk = FPackageName::DoesPackageExist(PackageName);
	bool bDetachExistingBeforeCreate = false;
	UPackage* ExistingPkg = nullptr;
	UObject* Existing = FindObject<UObject>(nullptr, *ObjectPath);
	if (!Existing && bExistedOnDisk)
	{
		Existing = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	}
	if (Existing)
	{
		ExistingPkg = Existing->GetOutermost();
		bool bSameProvenance = (ExistingPkg != nullptr);
		bool bHasProvenance = false;
		bool bPriorFromDirtySource = false;
		TSharedRef<FJsonObject> Diff = MakeShared<FJsonObject>();
		if (ExistingPkg)
		{
			FMetaData& Meta = ExistingPkg->GetMetaData();
			const FString* Schema = Meta.FindValue(Existing, *ProvKey(TEXT("Schema")));
			bHasProvenance = (Schema != nullptr) && (*Schema == kProvSchema);
			// SourceSequenceDirty records what the SOURCE looked like, not something the
			// caller asked for, so it is judged separately from request identity. Comparing
			// it as a parameter made bake_source_dirty_unverifiable unreachable for every
			// prior bake taken from a saved sequence: stored "0" vs. a now-dirty "1" cleared
			// bSameProvenance, so the caller who changed NOTHING was told "different
			// parameters" and pointed at a destructive overwrite instead of save-and-retry.
			// SourceSequenceHash stays in the comparison on purpose -- a source saved with
			// real edits IS a different input, and the stale bake must not be returned.
			//
			// The stored value is still READ, just not as a parameter: a bake taken from an
			// unsaved sequence recorded a hash of the file on DISK, which is not what it
			// baked, so it can never be proven identical to anything and must stay out of
			// the idempotent return -- exactly as when it was compared. Only the direction
			// changes: it now reports as unverifiable rather than as "different parameters".
			const FString DirtyKey = ProvKey(TEXT("SourceSequenceDirty"));
			const FString* StoredDirty = Meta.FindValue(Existing, *DirtyKey);
			bPriorFromDirtySource = (StoredDirty != nullptr) && (*StoredDirty == TEXT("1"));
			for (const TPair<FString, FString>& Want : Prov)
			{
				if (Want.Key == DirtyKey)
				{
					continue;
				}
				const FString* Got = Meta.FindValue(Existing, *Want.Key);
				const FString GotStr = Got ? *Got : FString();
				if (GotStr != Want.Value)
				{
					bSameProvenance = false;
					TSharedRef<FJsonObject> One = MakeShared<FJsonObject>();
					One->SetStringField(TEXT("stored"), GotStr);
					One->SetStringField(TEXT("requested"), Want.Value);
					Diff->SetObjectField(Want.Key, One);
				}
			}
		}

		// A dirty source is deliberately NOT eligible for the idempotent return even when
		// every provenance key matches: the stored SourceSequenceHash describes the file on
		// disk, which is not what an unsaved sequence would bake. That case is reported on
		// its own below rather than as a parameter mismatch -- the diff would be empty.
		if (bHasProvenance && bSameProvenance && !bSourceDirty && !bPriorFromDirtySource)
		{
			// A prior run of this exact request already produced this asset. This is
			// the recovery path for a client that timed out while the editor kept
			// going -- the retry must not be a permanent failure.
			UAnimSequence* Prior = Cast<UAnimSequence>(Existing);
			TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetBoolField(TEXT("success"), true);
			Obj->SetBoolField(TEXT("already_baked"), true);
			Obj->SetBoolField(TEXT("recovered_from_prior_run"), true);
			Obj->SetStringField(TEXT("asset_path"), ObjectPath);
			Obj->SetStringField(TEXT("sequence_path"), Sequence->GetPathName());
			Obj->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
			Obj->SetNumberField(TEXT("start_frame"), StartFrame);
			Obj->SetNumberField(TEXT("end_frame"), EndFrame);
			Obj->SetNumberField(TEXT("frame_count"), FrameCount);
			Obj->SetBoolField(TEXT("created_link"), false);
			Obj->SetNumberField(TEXT("play_length_seconds"), Prior ? Prior->GetPlayLength() : 0.0);
			Obj->SetNumberField(TEXT("sampled_key_count"),
				Prior ? Prior->GetNumberOfSampledKeys() : 0);
			Obj->SetBoolField(TEXT("montage_ready"),
				Prior != nullptr && Prior->GetSkeleton() != nullptr && Prior->GetPlayLength() > 0.0f);
			Obj->SetStringField(TEXT("note"),
				TEXT("Existing asset carries identical bake provenance; returned unchanged"));
			return SerializeJson(Obj);
		}

		// Anything we cannot prove we authored is refused outright -- overwrite=true
		// does NOT unlock it. The op must never destroy a user's asset.
		if (!bHasProvenance)
		{
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"),
				FString::Printf(
					TEXT("An asset already exists at %s and was not created by this op"),
					*ObjectPath));
			Err->SetStringField(TEXT("error_code"), TEXT("asset_exists_foreign"));
			Err->SetStringField(TEXT("existing_asset_class"), Existing->GetClass()->GetName());
			Err->SetStringField(TEXT("hint"), TEXT("Choose a different asset_path"));
			return SerializeJson(Err);
		}
		if (!bOverwrite)
		{
			// Distinguish "you asked for something different" from "I cannot prove it is the
			// same". The second covers BOTH unprovable directions: the source has unsaved
			// edits now, or the prior bake was itself taken from an unsaved source (its
			// recorded hash describes the file on disk, not what it baked). Reporting either
			// as a mismatch tells a caller who changed nothing that their parameters differ,
			// and points them at a destructive overwrite instead of save-and-retry.
			const bool bUnverifiable = bSameProvenance && (bSourceDirty || bPriorFromDirtySource);
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"), bUnverifiable
				? FString::Printf(
					TEXT("A bake exists at %s with matching parameters, but %s, so it cannot be "
						 "proven identical"), *ObjectPath,
					bSourceDirty ? TEXT("the source sequence has unsaved edits")
						: TEXT("that bake was taken from an unsaved source"))
				: FString::Printf(
					TEXT("A bake already exists at %s with different parameters"), *ObjectPath));
			Err->SetStringField(TEXT("error_code"), bUnverifiable
				? TEXT("bake_source_dirty_unverifiable") : TEXT("bake_params_mismatch"));
			Err->SetObjectField(TEXT("provenance_diff"), Diff);
			Err->SetStringField(TEXT("hint"), bUnverifiable
				? (bSourceDirty
					? TEXT("Save the LevelSequence and retry, or pass overwrite=true")
					: TEXT("Pass overwrite=true to re-bake from the current source"))
				: TEXT("Pass overwrite=true to replace it, or choose a different asset_path"));
			return SerializeJson(Err);
		}

		// Overwrite of our own prior bake. The detach itself is DEFERRED to just before
		// CreateAsset: renaming the old object into the transient package and marking it
		// garbage destroys its in-memory identity, and everything between here and the
		// create can still fail (no editor world, unresolvable skeleton). Failing after an
		// early detach would leave the caller's previous asset unfindable at its own path
		// for the rest of the session even though we changed nothing on disk.
		bDetachExistingBeforeCreate = true;
	}

	// --- editor world (the engine spawns its temporary player into it)
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return MakeErrorJson(TEXT("No editor world available to evaluate the sequence"),
			TEXT("no_playback_context"));
	}

	// --- create the destination asset: the engine writes INTO a UAnimSequence, it
	// does not create one (SequencerTools.cpp:427 only calls SetSkeleton).
	// The destination asset cannot be created without a skeleton (see
	// ResolveSkeletonForBinding). Prefer an explicit skeleton_path, otherwise take it
	// from the same binding the export will evaluate.
	USkeleton* TargetSkeleton = nullptr;
	FString SkeletonSource;
	TArray<FString> SkeletonTrail;
	if (!SkeletonPath.IsEmpty())
	{
		TargetSkeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!TargetSkeleton)
		{
			TargetSkeleton = Cast<USkeleton>(FSoftObjectPath(SkeletonPath).TryLoad());
		}
		if (!TargetSkeleton)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("Failed to load skeleton_path: %s"), *SkeletonPath),
				TEXT("skeleton_not_found"));
		}
		SkeletonSource = TEXT("skeleton_path");
	}
	else
	{
		TargetSkeleton =
			ResolveSkeletonForBinding(Sequence, MovieScene, BindingGuid, World, SkeletonTrail);
		SkeletonSource = TEXT("binding");
	}
	if (!TargetSkeleton)
	{
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"),
			FString::Printf(
				TEXT("Could not resolve a skeleton from binding %s; pass skeleton_path explicitly"),
				*BindingId));
		Err->SetStringField(TEXT("error_code"), TEXT("skeleton_unresolved"));
		TArray<TSharedPtr<FJsonValue>> TrailArr;
		for (const FString& Line : SkeletonTrail)
		{
			TrailArr.Add(MakeShared<FJsonValueString>(Line));
		}
		Err->SetArrayField(TEXT("resolve_trail"), TrailArr);
		Err->SetStringField(TEXT("hint"),
			TEXT("A spawnable binding may not resolve at author time; pass skeleton_path"));
		return SerializeJson(Err);
	}

	// Same reasoning as OptionsHolder, though this one is created after the purge.
	TStrongObjectPtr<UAnimSequenceFactory> FactoryHolder(NewObject<UAnimSequenceFactory>());
	UAnimSequenceFactory* Factory = FactoryHolder.Get();
	// Without this the created asset has no skeleton and its data model fails to
	// build an FKControlRig, which aborts asset creation outright (live-verified).
	Factory->TargetSkeleton = TargetSkeleton;
	IAssetTools& AssetTools =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	// Deferred from the overwrite branch: by here the world and the skeleton have both
	// resolved, so CreateAsset is the only remaining way to fail and the window in which
	// the caller's previous asset has no valid in-memory identity is as small as it gets.
	// Strong-held for the same reason OptionsHolder and FactoryHolder above are: this
	// object lives across CreateAsset, the export and SavePackage, and DetachAssetFromPath
	// just stripped RF_Public|RF_Standalone and moved it into the transient package -- it
	// is MORE exposed to a GC in that window than a fresh NewObject, not less.
	TStrongObjectPtr<UObject> DetachedPriorHolder;
	// Pinned for the same reason, one Outer hop up: the detach renames Existing out of
	// this package, severing the edge that kept the package itself reachable.
	TStrongObjectPtr<UPackage> ExistingPkgHolder;
	FName DetachedPriorName = NAME_None;
	bool bDetachedPriorPackageWasDirty = false;
	if (bDetachExistingBeforeCreate && Existing)
	{
		DetachedPriorHolder.Reset(Existing);
		ExistingPkgHolder.Reset(ExistingPkg);
		DetachedPriorName = Existing->GetFName();
		bDetachedPriorPackageWasDirty = ExistingPkg && ExistingPkg->IsDirty();
		DetachAssetFromPath(Existing, ExistingPkg);
		Existing = nullptr;
	}

	UObject* CreatedAsset = AssetTools.CreateAsset(
		AssetName, FPackageName::GetLongPackagePath(PackageName),
		UAnimSequence::StaticClass(), Factory);
	UAnimSequence* Baked = Cast<UAnimSequence>(CreatedAsset);
	if (!Baked)
	{
		if (DetachedPriorHolder.IsValid()
			&& !RestoreDetachedAsset(DetachedPriorHolder.Get(), ExistingPkgHolder.Get(),
				DetachedPriorName, bDetachedPriorPackageWasDirty))
		{
			UE_LOG(LogNarshaMCPAnimMixerBake, Warning,
				TEXT("[AnimMixer] Rollback could not restore the previous asset at %s; "
					 "a retry may see it as foreign"), *ObjectPath);
		}
		return MakeErrorJson(
			FString::Printf(TEXT("Failed to create AnimSequence at %s"), *ObjectPath),
			TEXT("asset_create_failed"));
	}
	UPackage* Package = Baked->GetOutermost();

	// Rollback must never delete a file this call did not create. On the overwrite path
	// the destination still holds the caller's PREVIOUS, valid bake until SavePackage
	// replaces it -- CreateAsset only builds the object in memory. Deleting it on an
	// export/empty/save failure would destroy that asset and put nothing back, which is
	// exactly the window the detach step above refuses to open.
	const FString RollbackFileToDelete = bExistedOnDisk ? FString() : PackageFilename;

	// The engine restores this CVar to a hardcoded 0 rather than the value it saved
	// (MovieSceneToolHelpers.cpp:4345-4347), so a user who had it at 1 would silently
	// lose it. Save and put it back ourselves, then report the re-read.
	IConsoleVariable* LiveLinkCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("Sequencer.AlwaysSendInterpolatedLiveLink"));
	const int32 CVarBefore = LiveLinkCVar ? LiveLinkCVar->GetInt() : 0;
	ON_SCOPE_EXIT
	{
		if (LiveLinkCVar && LiveLinkCVar->GetInt() != CVarBefore)
		{
			LiveLinkCVar->Set(CVarBefore, ECVF_SetByConsole);
		}
	};

	// --- the bake. bCreateLink stays false and is not exposed: LinkAnimSequence
	// calls Sequence->Modify() on the SOURCE LevelSequence (SequencerTools.cpp:503-531),
	// which would break the "source assets are untouched" contract.
	const FMovieSceneBindingProxy Binding(BindingGuid, Sequence);
	const double BakeStart = FPlatformTime::Seconds();
	const bool bExported = USequencerToolsFunctionLibrary::ExportAnimSequence(
		World, Sequence, Baked, Options, Binding, /*bCreateLink=*/false);
	const double BakeSeconds = FPlatformTime::Seconds() - BakeStart;

	if (!bExported)
	{
		OrphanFailedAsset(Baked, Package, RollbackFileToDelete);
		if (DetachedPriorHolder.IsValid()
			&& !RestoreDetachedAsset(DetachedPriorHolder.Get(), ExistingPkgHolder.Get(),
				DetachedPriorName, bDetachedPriorPackageWasDirty))
		{
			UE_LOG(LogNarshaMCPAnimMixerBake, Warning,
				TEXT("[AnimMixer] Rollback could not restore the previous asset at %s; "
					 "a retry may see it as foreign"), *ObjectPath);
		}
		return MakeErrorJson(
			TEXT("Engine export returned false; the binding may not resolve to a skeletal mesh"),
			TEXT("export_failed"));
	}

	// A silently empty bake is the failure mode this op has to refuse: the engine
	// reports success for an evaluation that produced nothing (the Phase 5c
	// inert-Control-Rig lesson). Judge the result, not the return value.
	const int32 KeysInMemory = Baked->GetNumberOfSampledKeys();
	if (KeysInMemory < 2 || Baked->GetPlayLength() <= 0.0f)
	{
		const float EmptyLength = Baked->GetPlayLength();
		OrphanFailedAsset(Baked, Package, RollbackFileToDelete);
		if (DetachedPriorHolder.IsValid()
			&& !RestoreDetachedAsset(DetachedPriorHolder.Get(), ExistingPkgHolder.Get(),
				DetachedPriorName, bDetachedPriorPackageWasDirty))
		{
			UE_LOG(LogNarshaMCPAnimMixerBake, Warning,
				TEXT("[AnimMixer] Rollback could not restore the previous asset at %s; "
					 "a retry may see it as foreign"), *ObjectPath);
		}
		return MakeErrorJson(
			FString::Printf(
				TEXT("Bake produced %d sampled key(s) over %.4f s -- nothing was evaluated"),
				KeysInMemory, EmptyLength),
			TEXT("bake_empty"));
	}

	// Rest-pose bake: keys and length are present, but no bone moves. The
	// root-scope guard above is the named cause; this is the result-side net
	// for any other silent-unpose path. HasBeenPopulated() is true for a flat
	// curve, so we compare sampled transforms (0.01 cm / 0.05 deg).
	const FBakeMotion Motion = MeasureBakeMotion(Baked->GetDataModel());
	//
	// SampledTracks == 0 is NOT quietly accepted. A track is skipped only when it
	// carries fewer than two keys, which for UAnimDataModel cannot happen once the
	// aggregate key count is >= 2 -- but UAnimSequence does not use that model in
	// stock 5.8. Under UAnimationSequencerDataModel the aggregate key count comes
	// from the MovieScene range while per-track transforms come from FK control
	// channels, so the two axes are decoupled and "aggregate says there is data,
	// per-track scan found none" is reachable rather than impossible. Report it
	// instead of inferring health from key counts -- the exact trap this guard
	// exists to close.
	const bool bNoSampledTracks = (Motion.SampledTracks == 0 && KeysInMemory >= 2);
	if (!bAllowStaticPose
		&& (bNoSampledTracks || (Motion.SampledTracks > 0 && Motion.MovingTracks == 0)))
	{
		OrphanFailedAsset(Baked, Package, RollbackFileToDelete);
		if (DetachedPriorHolder.IsValid()
			&& !RestoreDetachedAsset(DetachedPriorHolder.Get(), ExistingPkgHolder.Get(),
				DetachedPriorName, bDetachedPriorPackageWasDirty))
		{
			UE_LOG(LogNarshaMCPAnimMixerBake, Warning,
				TEXT("[AnimMixer] Rollback could not restore the previous asset at %s; "
					 "a retry may see it as foreign"), *ObjectPath);
		}
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), bNoSampledTracks
			? FString::Printf(
				TEXT("Bake reported %d key(s) but no bone track carried enough keys to "
					 "check for motion, so the result could not be verified as animating."),
				KeysInMemory)
			: FString::Printf(
				TEXT("Bake produced %d bone track(s) with no pose change across samples "
					 "(max %.4f cm / %.4f deg) -- likely a rest pose"),
				Motion.SampledTracks, Motion.MaxLocDeltaCm, Motion.MaxRotDeltaDeg));
		Err->SetStringField(TEXT("error_code"), TEXT("bake_static_pose"));
		Err->SetNumberField(TEXT("sampled_bone_track_count"), Motion.SampledTracks);
		Err->SetNumberField(TEXT("moving_bone_track_count"), Motion.MovingTracks);
		Err->SetNumberField(TEXT("max_bone_translation_delta_cm"), Motion.MaxLocDeltaCm);
		Err->SetNumberField(TEXT("max_bone_rotation_delta_deg"), Motion.MaxRotDeltaDeg);
		Err->SetStringField(TEXT("hint"),
			TEXT("If the source is a genuine idle/single pose, pass allow_static_pose=true. "
				 "If you authored via anim_mixer_add_layer without track_id, the mixer is "
				 "on master and will not evaluate -- pass track_id=<binding_id>"));
		return SerializeJson(Err);
	}

	// --- stamp provenance BEFORE saving so it is serialized with the package.
	{
		FMetaData& Meta = Package->GetMetaData();
		for (const TPair<FString, FString>& Pair : Prov)
		{
			Meta.SetValue(Baked, *Pair.Key, *Pair.Value);
		}
	}

	Package->MarkPackageDirty();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Baked, *PackageFilename, SaveArgs))
	{
		OrphanFailedAsset(Baked, Package, RollbackFileToDelete);
		if (DetachedPriorHolder.IsValid()
			&& !RestoreDetachedAsset(DetachedPriorHolder.Get(), ExistingPkgHolder.Get(),
				DetachedPriorName, bDetachedPriorPackageWasDirty))
		{
			UE_LOG(LogNarshaMCPAnimMixerBake, Warning,
				TEXT("[AnimMixer] Rollback could not restore the previous asset at %s; "
					 "a retry may see it as foreign"), *ObjectPath);
		}
		return MakeErrorJson(
			FString::Printf(TEXT("Failed to save baked AnimSequence: %s"), *ObjectPath),
			TEXT("save_failed"));
	}
	FAssetRegistryModule::AssetCreated(Baked);

	// The new asset is on disk now, so the detached predecessor is genuinely superseded
	// and can go. Until this line it was kept restorable on purpose.
	if (DetachedPriorHolder.IsValid())
	{
		DetachedPriorHolder->MarkAsGarbage();
		DetachedPriorHolder.Reset();
		ExistingPkgHolder.Reset();
	}

	// --- everything reported below is read back off the RELOADED asset, so success
	// describes what is on disk rather than what is in memory.
	FText UnloadError;
	const bool bUnloaded = UPackageTools::UnloadPackages(
		TArray<UPackage*>{ Package }, UnloadError, /*bUnloadDirtyPackages=*/true);
	Baked = nullptr;
	Package = nullptr;
	if (bUnloaded)
	{
		// This purge is gated on a SUCCESSFUL UnloadPackages, which flushes async loading
		// first -- and it is what makes the reload below a real disk read rather than a
		// cache hit, so the verification block is only honest with it. Measured clean
		// across every live bake. The overwrite path had an ungated purge with an async
		// load still queued and deadlocked the game thread; that one is gone.
		// Assert the precondition here rather than trusting UnloadPackages to have flushed:
		// the purge that was removed from the overwrite path deadlocked against a queued
		// async load, and ExportAnimSequence spins up its own transient player that can
		// queue streaming work. Cheap when nothing is pending.
		FlushAsyncLoading();
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);
	}

	// Every reject from here on runs AFTER SavePackage, so refusing without rolling back
	// leaves the refused asset on disk carrying full NarshaMCP.Bake.* provenance. The next
	// identical request would then take the idempotent branch and answer success:true /
	// montage_ready:true for an asset this op just declared unusable -- and the documented
	// timeout-recovery path is exactly a byte-identical retry, so that is the likely call,
	// not an exotic one. Reachable today via export_options={"export_transforms":false}:
	// the recorder drops every bone track but still leaves frames, so bake_empty passes,
	// the reload finds zero bone tracks, and bone_tracks_empty fires post-save.
	// Takes the RELOADED object: Baked/Package were nulled at the unload above and the
	// detached predecessor was already released as genuinely superseded, so the pre-save
	// rollback pair has nothing left to act on here.
	//
	// The file is deleted UNCONDITIONALLY, unlike the pre-save path. RollbackFileToDelete
	// is empty when the destination pre-existed, which is right before the save (the file
	// on disk is still the caller's) and wrong after it (SavePackage has replaced it with
	// this refused bake). Leaving it is what let a byte-identical retry hit the idempotent
	// branch and report success:true / montage_ready:true for an asset just refused.
	const auto RollbackSavedBake = [&](UObject* SavedObj)
	{
		UPackage* SavedPkg = SavedObj ? SavedObj->GetOutermost() : nullptr;
		OrphanFailedAsset(SavedObj, SavedPkg, PackageFilename);
	};

	UObject* ReloadedObj = FindObject<UObject>(nullptr, *ObjectPath);
	if (!ReloadedObj)
	{
		ReloadedObj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	}
	UAnimSequence* Reloaded = Cast<UAnimSequence>(ReloadedObj);
	if (!Reloaded || Reloaded->GetPathName() != ObjectPath)
	{
		RollbackSavedBake(ReloadedObj);
		return MakeErrorJson(
			FString::Printf(TEXT("Baked asset did not reload as a UAnimSequence at %s"), *ObjectPath),
			TEXT("identity_mismatch"));
	}
	USkeleton* BakedSkeleton = Reloaded->GetSkeleton();
	if (!BakedSkeleton)
	{
		RollbackSavedBake(Reloaded);
		return MakeErrorJson(
			TEXT("Baked AnimSequence has no skeleton; create_montage would reject it"),
			TEXT("skeleton_missing"));
	}
	const IAnimationDataModel* Model = Reloaded->GetDataModel();
	const int32 BoneTracks = Model ? Model->GetNumBoneTracks() : 0;
	if (BoneTracks <= 0)
	{
		RollbackSavedBake(Reloaded);
		return MakeErrorJson(TEXT("Baked AnimSequence has no bone tracks"),
			TEXT("bone_tracks_empty"));
	}

	// Provenance must survive the round trip, or the retry/overwrite logic is blind.
	TSharedRef<FJsonObject> ProvOut = MakeShared<FJsonObject>();
	{
		FMetaData& Meta = Reloaded->GetOutermost()->GetMetaData();
		for (const TPair<FString, FString>& Pair : Prov)
		{
			const FString* Got = Meta.FindValue(Reloaded, *Pair.Key);
			if (!Got || *Got != Pair.Value)
			{
				RollbackSavedBake(Reloaded);
				return MakeErrorJson(
					FString::Printf(TEXT("Bake provenance did not survive save/reload (key %s)"),
						*Pair.Key),
					TEXT("provenance_write_failed"));
			}
			ProvOut->SetStringField(Pair.Key, *Got);
		}
	}

	const FString SequenceHashAfter = HashFileSha(SequenceFile);
	const bool bSourceUnchanged = (SequenceHashBefore == SequenceHashAfter);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("asset_path"), ObjectPath);
	Obj->SetStringField(TEXT("sequence_path"), Sequence->GetPathName());
	Obj->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
	Obj->SetStringField(TEXT("skeleton_path"), BakedSkeleton->GetPathName());
	Obj->SetStringField(TEXT("skeleton_source"), SkeletonSource);
	Obj->SetBoolField(TEXT("skeleton_matches_resolved"),
		BakedSkeleton == TargetSkeleton);
	Obj->SetNumberField(TEXT("start_frame"), StartFrame);
	Obj->SetNumberField(TEXT("end_frame"), EndFrame);
	Obj->SetNumberField(TEXT("frame_count"), FrameCount);
	Obj->SetStringField(TEXT("display_rate"), DisplayRate.ToPrettyText().ToString());
	Obj->SetBoolField(TEXT("custom_time_range"), bCustomRange);
	Obj->SetNumberField(TEXT("play_length_seconds"), Reloaded->GetPlayLength());
	Obj->SetNumberField(TEXT("sampled_key_count"), Reloaded->GetNumberOfSampledKeys());
	Obj->SetNumberField(TEXT("sampled_key_count_in_memory"), KeysInMemory);
	Obj->SetNumberField(TEXT("bone_track_count"), BoneTracks);
	Obj->SetNumberField(TEXT("sampled_bone_track_count"), Motion.SampledTracks);
	Obj->SetNumberField(TEXT("moving_bone_track_count"), Motion.MovingTracks);
	Obj->SetBoolField(TEXT("allow_static_pose"), bAllowStaticPose);
	Obj->SetNumberField(TEXT("max_bone_translation_delta_cm"), Motion.MaxLocDeltaCm);
	Obj->SetNumberField(TEXT("max_bone_rotation_delta_deg"), Motion.MaxRotDeltaDeg);
	Obj->SetNumberField(TEXT("model_frame_count"), Model ? Model->GetNumberOfFrames() : 0);
	// The single boolean create_montage cares about: a skeleton, real length, real tracks.
	// Re-derived, not asserted: the skeleton and bone-track checks above already returned
	// on failure, but play length is only measured here.
	Obj->SetBoolField(TEXT("montage_ready"), Reloaded->GetPlayLength() > 0.0f);
	Obj->SetBoolField(TEXT("created_link"), false);
	Obj->SetBoolField(TEXT("overwrote_existing"), bOverwrite && bExistedOnDisk);
	Obj->SetBoolField(TEXT("already_baked"), false);
	// Only true when the package was actually unloaded first. If UnloadPackages failed the
	// FindObject below re-finds the same in-memory object we just wrote, so the "verified
	// from disk" claim would be false -- unloaded_for_verify and warnings[] say so too, but
	// this field must not assert something it did not do.
	Obj->SetBoolField(TEXT("reloaded_verified"), bUnloaded);
	Obj->SetBoolField(TEXT("unloaded_for_verify"), bUnloaded);
	Obj->SetBoolField(TEXT("source_sequence_unchanged"), bSourceUnchanged);
	Obj->SetStringField(TEXT("source_sequence_hash"), SequenceHashAfter);
	Obj->SetNumberField(TEXT("bake_seconds"), BakeSeconds);
	Obj->SetObjectField(TEXT("provenance"), ProvOut);

	// No Sequencer toolkit is opened, closed, or restored on this path, so there is
	// no editor state to put back -- these fields report that as an observed fact.
	TSharedRef<FJsonObject> Restored = MakeShared<FJsonObject>();
	Restored->SetBoolField(TEXT("sequencer_opened"), false);
	Restored->SetBoolField(TEXT("restore_performed"), false);
	Restored->SetNumberField(TEXT("cvar_always_send_interpolated_livelink_before"), CVarBefore);
	Restored->SetNumberField(TEXT("cvar_always_send_interpolated_livelink_after"),
		LiveLinkCVar ? LiveLinkCVar->GetInt() : 0);
	Obj->SetObjectField(TEXT("restored_state"), Restored);

	TArray<TSharedPtr<FJsonValue>> Warnings;
	if (!bSourceUnchanged)
	{
		Warnings.Add(MakeShared<FJsonValueString>(
			TEXT("source LevelSequence .uasset changed during the bake")));
	}
	if (!bUnloaded)
	{
		Warnings.Add(MakeShared<FJsonValueString>(FString::Printf(
			TEXT("package could not be unloaded for verification: %s"),
			*UnloadError.ToString())));
	}
	Obj->SetArrayField(TEXT("warnings"), Warnings);

	UE_LOG(LogNarshaMCPAnimMixerBake, Log,
		TEXT("[AnimMixer] Baked %s (%d frames, %d keys) in %.3f s"),
		*ObjectPath, FrameCount, Reloaded->GetNumberOfSampledKeys(), BakeSeconds);
	return SerializeJson(Obj);
#endif
}
