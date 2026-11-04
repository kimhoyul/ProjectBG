// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CURemoteSequencerAnimMixerLibrary.h"
#include "CURemoteSequencerAnimMixerInternals.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ScopedTransaction.h"
#include "UObject/SavePackage.h"
#endif

// NARSHAMCP_ANIMMIXER_SUPPORTED (the UE 5.8 layer-model gate) and the shared
// JSON/loader helpers now live in CURemoteSequencerAnimMixerInternals.h so the
// Phase 5d bake translation unit can use the same definitions (Issue #27002).

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneAnimationMixerTrack.h"
#include "MovieSceneAnimationMixerLayer.h"
#include "MovieSceneLayerWeightDecoration.h"
// Issue #25921 (Phase 5b): mask decoration + mask section. The mask-section header
// includes UAF/BlendMask/UAFBlendMask.h, whose include path reaches us transitively
// because UAF is a PUBLIC dependency of MovieSceneAnimMixer (Build.cs). NarshaMCP
// still names no UAF symbol directly — the asset class is validated by name.
#include "MovieSceneAnimationMaskDecoration.h"
#include "MovieSceneAnimMixerMaskSection.h"
#include "MovieSceneAnimTransitionSectionBase.h"
#include "MovieSceneAnimCrossfadeTransitionSection.h"
#include "MovieSceneAnimInertialDeadBlendTransitionSection.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/Skeleton.h"
#include "AlphaBlend.h" // For EAlphaBlendOption (Issue #11753 Phase 2)
#include "HierarchyTable.h"
#include "HierarchyTableBlendProfile.h"
#include "MaskProfile/HierarchyTableTypeMask.h"
#include "MovieSceneAnimationMaskDecoration.h"
#include "SkeletonHierarchyTableType.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#endif
// Issue #16112 (Phase 3): SequencerScripting extension libraries (easing + conditions).
#if WITH_NARSHAMCP_SEQUENCERSCRIPTING
#include "ExtensionLibraries/MovieSceneSectionEasingExtensions.h"
#include "ExtensionLibraries/MovieSceneConditionExtensions.h"
#include "Conditions/MovieSceneCondition.h"
#endif
// Phase 5c (#25957) — Control Rig correction layer. All of these live in the
// optional RUNTIME ControlRig module; the
// editor-only UControlRigSequencerEditorLibrary is deliberately NOT used (its
// keying surface requires an open Sequencer — see docs + issue #25957).
#if WITH_NARSHAMCP_CONTROLRIG
#include "ControlRig.h"
#include "ControlRigObjectBinding.h"
#include "Rigs/FKControlRig.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Sequencer/MovieSceneControlRigParameterTrack.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#endif
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPAnimMixer, Log, All);

namespace
{
	// Shared with the Phase 5d bake TU -- definitions in
	// CURemoteSequencerAnimMixerInternals.h (Issue #27002).
	using NarshaAnimMixerInternal::SerializeJson;
	using NarshaAnimMixerInternal::MakeErrorJson;
	using NarshaAnimMixerInternal::MakeUnsupported;
	using NarshaAnimMixerInternal::MixedControlRigAvailabilityErrorCode;

	FString MakeOptionalPluginUnavailable(
		const TCHAR* PluginName, const TCHAR* ErrorCode, const TCHAR* Capability)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s requires the optional %s plugin"), Capability, PluginName));
		Obj->SetStringField(TEXT("error_code"), ErrorCode);
		Obj->SetStringField(TEXT("hint"), FString::Printf(
			TEXT("Enable %s in your .uproject, then rebuild."), PluginName));
		return SerializeJson(Obj);
	}

#if NARSHAMCP_ANIMMIXER_SUPPORTED
	using NarshaAnimMixerInternal::LoadLevelSequence;

	struct FAnimMixerTrackCandidate
	{
		UMovieSceneAnimationMixerTrack* Track = nullptr;
		FGuid BindingGuid;
		bool bBindingOwned = false;
	};

	FString CanonicalBindingGuid(const FGuid& BindingGuid)
	{
		return BindingGuid.ToString(EGuidFormats::DigitsWithHyphens);
	}

	void CollectBindingAnimMixerTracks(UMovieScene* MovieScene, TArray<FAnimMixerTrackCandidate>& OutCandidates)
	{
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (UMovieSceneAnimationMixerTrack* Mixer = Cast<UMovieSceneAnimationMixerTrack>(Track))
				{
					FAnimMixerTrackCandidate& Candidate = OutCandidates.AddDefaulted_GetRef();
					Candidate.Track = Mixer;
					Candidate.BindingGuid = Binding.GetObjectGuid();
					Candidate.bBindingOwned = true;
				}
			}
		}
	}

	FString JoinCandidateBindingGuids(const TArray<FAnimMixerTrackCandidate>& Candidates)
	{
		TArray<FString> BindingIds;
		BindingIds.Reserve(Candidates.Num());
		for (const FAnimMixerTrackCandidate& Candidate : Candidates)
		{
			if (Candidate.bBindingOwned)
			{
				BindingIds.Add(CanonicalBindingGuid(Candidate.BindingGuid));
			}
		}
		return FString::Join(BindingIds, TEXT(", "));
	}

	FString JoinCandidateTrackNames(const TArray<FAnimMixerTrackCandidate>& Candidates)
	{
		TArray<FString> TrackNames;
		TrackNames.Reserve(Candidates.Num());
		for (const FAnimMixerTrackCandidate& Candidate : Candidates)
		{
			if (Candidate.Track)
			{
				TrackNames.Add(Candidate.Track->GetFName().ToString());
			}
		}
		return FString::Join(TrackNames, TEXT(", "));
	}

	bool HasDuplicateBindingGuid(const TArray<FAnimMixerTrackCandidate>& Candidates)
	{
		for (int32 LeftIndex = 0; LeftIndex < Candidates.Num(); ++LeftIndex)
		{
			if (!Candidates[LeftIndex].bBindingOwned)
			{
				continue;
			}
			for (int32 RightIndex = LeftIndex + 1; RightIndex < Candidates.Num(); ++RightIndex)
			{
				if (Candidates[RightIndex].bBindingOwned &&
					Candidates[LeftIndex].BindingGuid == Candidates[RightIndex].BindingGuid)
				{
					return true;
				}
			}
		}
		return false;
	}

	UMovieSceneAnimationMixerTrack* ResolveAnimMixerTrack(UMovieScene* MovieScene, const FString& TrackId, FString& OutError)
	{
		if (!MovieScene)
		{
			OutError = TEXT("LevelSequence has no MovieScene");
			return nullptr;
		}

		// 1) Default selector: exactly one binding-owned AnimMixer track.
		// Empty is treated as omission for direct RC callers, matching Rust's
		// omitted/whitespace-only -> "auto" normalization. Issue #27558.
		if (TrackId.IsEmpty() || TrackId.Equals(TEXT("auto"), ESearchCase::IgnoreCase))
		{
			TArray<FAnimMixerTrackCandidate> Candidates;
			CollectBindingAnimMixerTracks(MovieScene, Candidates);
			if (Candidates.Num() == 1)
			{
				return Candidates[0].Track;
			}
			if (Candidates.IsEmpty())
			{
				OutError = TEXT("No binding-scoped AnimMixer track in this LevelSequence; create MovieSceneAnimationMixerTrack with binding_id or pass an explicit binding GUID");
				return nullptr;
			}
			if (HasDuplicateBindingGuid(Candidates))
			{
				OutError = FString::Printf(
					TEXT("AnimMixer track selection is ambiguous: auto found %d binding-scoped tracks [%s]; at least one binding owns multiple AnimMixer tracks [%s], so a binding GUID is not unique; pass a unique track FName as track_id"),
					Candidates.Num(),
					*JoinCandidateBindingGuids(Candidates),
					*JoinCandidateTrackNames(Candidates));
			}
			else
			{
				OutError = FString::Printf(
					TEXT("AnimMixer track selection is ambiguous: auto found %d binding-scoped tracks [%s]; pass one binding GUID as track_id"),
					Candidates.Num(),
					*JoinCandidateBindingGuids(Candidates));
			}
			return nullptr;
		}

		// 2) Explicit binding GUID.
		FGuid AsGuid;
		if (FGuid::Parse(TrackId, AsGuid))
		{
			if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(AsGuid))
			{
				TArray<FAnimMixerTrackCandidate> BindingMatches;
				for (UMovieSceneTrack* Track : Binding->GetTracks())
				{
					if (UMovieSceneAnimationMixerTrack* Mixer = Cast<UMovieSceneAnimationMixerTrack>(Track))
					{
						FAnimMixerTrackCandidate& Candidate = BindingMatches.AddDefaulted_GetRef();
						Candidate.Track = Mixer;
						Candidate.BindingGuid = AsGuid;
						Candidate.bBindingOwned = true;
					}
				}
				if (BindingMatches.Num() == 1)
				{
					return BindingMatches[0].Track;
				}
				if (BindingMatches.Num() > 1)
				{
					OutError = FString::Printf(
						TEXT("AnimMixer track selection is ambiguous: binding %s owns %d AnimMixer tracks [%s]; pass a unique track FName as track_id"),
						*CanonicalBindingGuid(AsGuid),
						BindingMatches.Num(),
						*JoinCandidateTrackNames(BindingMatches));
					return nullptr;
				}
			}
			OutError = FString::Printf(TEXT("No AnimMixer track on binding %s"), *TrackId);
			return nullptr;
		}

		// 3) Explicit legacy root track ("master" / "root"). Root ownership
		// has no bound-object target; success envelopes add an actionable warning.
		const bool bWantRoot =
			TrackId.Equals(TEXT("master"), ESearchCase::IgnoreCase) ||
			TrackId.Equals(TEXT("root"), ESearchCase::IgnoreCase);
		if (bWantRoot)
		{
			for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			{
				if (UMovieSceneAnimationMixerTrack* Mixer = Cast<UMovieSceneAnimationMixerTrack>(Track))
				{
					return Mixer;
				}
			}
			OutError = TEXT("No root AnimMixer track in this LevelSequence");
			return nullptr;
		}

		// 4) Track FName — require one match across root and bindings. The old
		// root-first return silently selected an unevaluable track on collisions.
		const FName AsName(*TrackId);
		TArray<FAnimMixerTrackCandidate> NameMatches;
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			if (Track && Track->GetFName() == AsName)
			{
				if (UMovieSceneAnimationMixerTrack* Mixer = Cast<UMovieSceneAnimationMixerTrack>(Track))
				{
					FAnimMixerTrackCandidate& Candidate = NameMatches.AddDefaulted_GetRef();
					Candidate.Track = Mixer;
				}
			}
		}
		// Explicit const access — UE 5.7 deprecated the non-const GetBindings() overload
		// ("Getting non-const access to GetBindings is no longer allowed"); the const
		// overload is the only one that remains supported across 5.7/5.8.
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (Track && Track->GetFName() == AsName)
				{
					if (UMovieSceneAnimationMixerTrack* Mixer = Cast<UMovieSceneAnimationMixerTrack>(Track))
					{
						FAnimMixerTrackCandidate& Candidate = NameMatches.AddDefaulted_GetRef();
						Candidate.Track = Mixer;
						Candidate.BindingGuid = Binding.GetObjectGuid();
						Candidate.bBindingOwned = true;
					}
				}
			}
		}
		if (NameMatches.Num() == 1)
		{
			return NameMatches[0].Track;
		}
		if (NameMatches.Num() > 1)
		{
			OutError = FString::Printf(
				TEXT("AnimMixer track name is ambiguous: %s matched %d tracks; rename or remove duplicate track names so one selector resolves exactly one track"),
				*TrackId,
				NameMatches.Num());
			return nullptr;
		}
		OutError = FString::Printf(TEXT("AnimMixer track not found: %s"), *TrackId);
		return nullptr;
	}

	void AppendAnimMixerTrackMetadata(
		const TSharedRef<FJsonObject>& Obj,
		UMovieScene* MovieScene,
		UMovieSceneAnimationMixerTrack* Track)
	{
		if (!MovieScene || !Track)
		{
			return;
		}

		for (UMovieSceneTrack* RootTrack : MovieScene->GetTracks())
		{
			if (RootTrack == Track)
			{
				Obj->SetStringField(TEXT("scope"), TEXT("root"));
				Obj->SetStringField(TEXT("track_id"), TEXT("master"));
				Obj->SetStringField(
					TEXT("warning"),
					TEXT("Root-scoped AnimMixer tracks have no bound-object animation target and may not evaluate; create MovieSceneAnimationMixerTrack with binding_id"));
				return;
			}
		}

		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			int32 BindingMixerCount = 0;
			for (UMovieSceneTrack* BindingTrack : Binding.GetTracks())
			{
				if (Cast<UMovieSceneAnimationMixerTrack>(BindingTrack))
				{
					++BindingMixerCount;
				}
			}

			for (UMovieSceneTrack* BindingTrack : Binding.GetTracks())
			{
				if (BindingTrack == Track)
				{
					const FString BindingId = CanonicalBindingGuid(Binding.GetObjectGuid());
					Obj->SetStringField(TEXT("scope"), TEXT("binding"));
					Obj->SetStringField(TEXT("binding_id"), BindingId);
					// A binding GUID is reusable only while that binding owns exactly
					// one AnimMixer track. When several mixers share the binding, the
					// resolver deliberately rejects the GUID as ambiguous, so return
					// the unique UObject FName accepted by its explicit-name path.
					Obj->SetStringField(
						TEXT("track_id"),
						BindingMixerCount == 1 ? BindingId : Track->GetFName().ToString());
					return;
				}
			}
		}
	}

	FString SerializeAnimMixerSuccess(
		const TSharedRef<FJsonObject>& Obj,
		ULevelSequence* Sequence,
		UMovieSceneAnimationMixerTrack* Track)
	{
		AppendAnimMixerTrackMetadata(Obj, Sequence ? Sequence->GetMovieScene() : nullptr, Track);
		return SerializeJson(Obj);
	}

	// Loads the LevelSequence and resolves the AnimMixer track in one step.
	// On failure, sets OutResultJson to the error envelope and returns nullptr.
	UMovieSceneAnimationMixerTrack* ResolveSequenceAndTrack(
		const FString& SequencePath, const FString& TrackId, ULevelSequence*& OutSequence, FString& OutResultJson)
	{
		OutSequence = LoadLevelSequence(SequencePath);
		if (!OutSequence)
		{
			OutResultJson = MakeErrorJson(FString::Printf(TEXT("Failed to load LevelSequence: %s"), *SequencePath));
			return nullptr;
		}
		UMovieScene* MovieScene = OutSequence->GetMovieScene();
		FString Err;
		UMovieSceneAnimationMixerTrack* Track = ResolveAnimMixerTrack(MovieScene, TrackId, Err);
		if (!Track)
		{
			OutResultJson = MakeErrorJson(Err);
			return nullptr;
		}
		return Track;
	}

	// ===== Phase 2 (#11753) helpers =====

	// Resolve a section by exact FName match against Section->GetFName(). Returns
	// nullptr on miss; populates OutError with a descriptive message. Phase 2's
	// transition_id handle is the section's FName as returned by add_transition
	// (e.g. "MovieSceneAnimCrossfadeTransitionSection_0").
	UMovieSceneSection* ResolveAnimMixerSection(
		UMovieSceneAnimationMixerTrack* Track, const FString& SectionId, FString& OutError)
	{
		if (!Track)
		{
			OutError = TEXT("Track is null");
			return nullptr;
		}
		if (SectionId.IsEmpty())
		{
			OutError = TEXT("SectionId is empty");
			return nullptr;
		}
		const FName AsName(*SectionId);
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (Section && Section->GetFName() == AsName)
			{
				return Section;
			}
		}
		OutError = FString::Printf(TEXT("Section not found on track: %s"), *SectionId);
		return nullptr;
	}

	// Map a short-name transition class string to the concrete UClass*.
	// Returns nullptr for unknown names; caller produces an error envelope.
	TSubclassOf<UMovieSceneAnimTransitionSectionBase> ResolveTransitionClass(const FString& ShortName)
	{
		if (ShortName.Equals(TEXT("crossfade"), ESearchCase::IgnoreCase))
		{
			return UMovieSceneAnimCrossfadeTransitionSection::StaticClass();
		}
		if (ShortName.Equals(TEXT("inertial_dead_blend"), ESearchCase::IgnoreCase))
		{
			return UMovieSceneAnimInertialDeadBlendTransitionSection::StaticClass();
		}
		return nullptr;
	}

	// Reverse lookup: concrete UClass* → short name (for response envelopes).
	FString TransitionClassToShortName(const UClass* Class)
	{
		if (!Class)
		{
			return TEXT("unknown");
		}
		if (Class == UMovieSceneAnimCrossfadeTransitionSection::StaticClass() ||
			Class->IsChildOf(UMovieSceneAnimCrossfadeTransitionSection::StaticClass()))
		{
			return TEXT("crossfade");
		}
		if (Class == UMovieSceneAnimInertialDeadBlendTransitionSection::StaticClass() ||
			Class->IsChildOf(UMovieSceneAnimInertialDeadBlendTransitionSection::StaticClass()))
		{
			return TEXT("inertial_dead_blend");
		}
		// Unknown subclass — fall back to the class's name.
		return Class->GetName();
	}

	// Parse a string like "HermiteCubic" into EAlphaBlendOption. Returns
	// EAlphaBlendOption::Custom + sets bOK=false on unknown name. The enum is
	// reflected, so we use UEnum::GetValueByNameString.
	EAlphaBlendOption ParseAlphaBlendOption(const FString& Name, bool& bOK)
	{
		const UEnum* EnumPtr = StaticEnum<EAlphaBlendOption>();
		if (!EnumPtr)
		{
			bOK = false;
			return EAlphaBlendOption::Linear;
		}
		const int64 Value = EnumPtr->GetValueByNameString(Name);
		if (Value == INDEX_NONE)
		{
			bOK = false;
			return EAlphaBlendOption::Linear;
		}
		bOK = true;
		return static_cast<EAlphaBlendOption>(Value);
	}

	// Convert EAlphaBlendOption to its enum name string for response envelopes.
	FString AlphaBlendOptionToString(EAlphaBlendOption Mode)
	{
		const UEnum* EnumPtr = StaticEnum<EAlphaBlendOption>();
		if (!EnumPtr)
		{
			return TEXT("Unknown");
		}
		return EnumPtr->GetNameStringByValue(static_cast<int64>(Mode));
	}

	// Parse the ParamsJson string into a JsonObject. Returns nullptr on parse
	// failure; populates OutError. Empty input is valid → empty object.
	TSharedPtr<FJsonObject> ParseParamsJson(const FString& ParamsJson, FString& OutError)
	{
		if (ParamsJson.IsEmpty())
		{
			return MakeShared<FJsonObject>();
		}
		TSharedPtr<FJsonObject> Parsed;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ParamsJson);
		if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to parse ParamsJson: %s"), *ParamsJson);
			return nullptr;
		}
		return Parsed;
	}
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerAddLayer(const FString& SequencePath, const FString& TrackId, const FString& LayerName)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] AddLayer seq=%s track=%s name=%s"), *SequencePath, *TrackId, *LayerName);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Add Layer")));
#endif

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	Track->Modify();
	UMovieSceneAnimationMixerLayer* Layer = Track->InsertLayer(Track->GetLayers().Num());
	if (!Layer)
	{
		return MakeErrorJson(TEXT("InsertLayer returned null"));
	}

	FString DisplayName;
#if WITH_EDITORONLY_DATA
	if (!LayerName.IsEmpty())
	{
		Layer->Modify();
		Layer->SetDisplayName(FText::FromString(LayerName));
	}
	DisplayName = Layer->GetDisplayName().ToString();
#endif
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("layer_index"), Layer->GetLayerIndex());
	Obj->SetStringField(TEXT("layer_display_name"), DisplayName);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerAddLayer"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerAddAnimation(
	const FString& SequencePath, const FString& TrackId, int32 LayerIndex, const FString& AnimPath, int32 StartFrame)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] AddAnimation seq=%s track=%s layer=%d anim=%s start=%d"),
		*SequencePath, *TrackId, LayerIndex, *AnimPath, StartFrame);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Add Animation")));
#endif

	if (LayerIndex < 0)
	{
		return MakeErrorJson(TEXT("LayerIndex must be >= 0"));
	}

	UAnimSequenceBase* Anim = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
	if (!Anim)
	{
		const FSoftObjectPath SoftPath(AnimPath);
		Anim = Cast<UAnimSequenceBase>(SoftPath.TryLoad());
	}
	if (!Anim)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AnimPath));
	}

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	Track->Modify();
	UMovieSceneSection* Section = Track->AddNewAnimationOnRow(FFrameNumber(StartFrame), Anim, LayerIndex);
	if (!Section)
	{
		return MakeErrorJson(TEXT("AddNewAnimationOnRow returned null"));
	}
	Sequence->MarkPackageDirty();

	const FFrameNumber EndFrame = Section->HasEndFrame() ? Section->GetExclusiveEndFrame() : FFrameNumber(StartFrame);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("section_name"), Section->GetName());
	Obj->SetNumberField(TEXT("start_frame"), StartFrame);
	Obj->SetNumberField(TEXT("end_frame"), EndFrame.Value);
	Obj->SetNumberField(TEXT("layer_index"), Section->GetRowIndex());
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerAddAnimation"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerSetLayerWeight(
	const FString& SequencePath, const FString& TrackId, int32 LayerIndex, float Weight)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] SetLayerWeight seq=%s track=%s layer=%d weight=%f"),
		*SequencePath, *TrackId, LayerIndex, Weight);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Set Layer Weight")));
#endif

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	UMovieSceneAnimationMixerLayer* Layer = Track->GetLayer(LayerIndex);
	if (!Layer)
	{
		return MakeErrorJson(FString::Printf(TEXT("No layer at index %d (track has %d layers)"), LayerIndex, Track->GetLayers().Num()));
	}

	Layer->Modify();
	UMovieSceneLayerWeightDecoration* Decoration = Layer->GetOrCreateDecoration<UMovieSceneLayerWeightDecoration>();
	if (!Decoration)
	{
		return MakeErrorJson(TEXT("Failed to get/create Layer Weight decoration"));
	}
	Decoration->Modify();
	bool bWeightSectionCreated = false;
	if (Decoration->GetSections().IsEmpty())
	{
		UMovieSceneSection* WeightSection = Decoration->CreateNewSection();
		if (!WeightSection)
		{
			return MakeErrorJson(TEXT("Failed to create Layer Weight section"));
		}
		Decoration->AddSection(WeightSection);
		bWeightSectionCreated = true;
	}
	Decoration->WeightChannel.SetDefault(Weight);
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("layer_index"), LayerIndex);
	Obj->SetNumberField(TEXT("weight"), Weight);
	Obj->SetBoolField(TEXT("weight_section_created"), bWeightSectionCreated);
	Obj->SetNumberField(TEXT("weight_section_count"), Decoration->GetSections().Num());
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerSetLayerWeight"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerRemoveLayer(const FString& SequencePath, const FString& TrackId, int32 LayerIndex)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] RemoveLayer seq=%s track=%s layer=%d"), *SequencePath, *TrackId, LayerIndex);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Remove Layer")));
#endif

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	Track->Modify();

	// Remove the layer's child track (if it occupies the layer instead of sections).
	if (UMovieSceneAnimationMixerLayer* Layer = Track->GetLayer(LayerIndex))
	{
		if (UMovieSceneTrack* ChildTrack = Layer->GetChildTrack())
		{
			Track->RemoveChildTrack(ChildTrack);
		}
	}

	// Remove every section sitting on this row. Copy first — RemoveSection mutates GetAllSections().
	TArray<UMovieSceneSection*> SectionsOnRow;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		if (Section && Section->GetRowIndex() == LayerIndex)
		{
			SectionsOnRow.Add(Section);
		}
	}
	int32 RemovedSections = 0;
	for (UMovieSceneSection* Section : SectionsOnRow)
	{
		Track->RemoveSection(*Section);
		++RemovedSections;
	}
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("removed_layer_index"), LayerIndex);
	Obj->SetNumberField(TEXT("removed_section_count"), RemovedSections);
	Obj->SetNumberField(TEXT("remaining_layer_count"), Track->GetLayers().Num());
	Obj->SetStringField(TEXT("note"),
		TEXT("No engine RemoveLayer API; the layer slot may persist if empty and higher rows are not shifted."));
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerRemoveLayer"));
#endif
}

// =============================================================================
// Phase 2 (#11753) — Transition authoring
// =============================================================================

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerAddTransition(
	const FString& SequencePath, const FString& TrackId,
	const FString& FromSection, const FString& ToSection, const FString& TransitionClass)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] AddTransition seq=%s track=%s from=%s to=%s class=%s"),
		*SequencePath, *TrackId, *FromSection, *ToSection, *TransitionClass);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Add Transition")));
#endif

	TSubclassOf<UMovieSceneAnimTransitionSectionBase> ClassRef = ResolveTransitionClass(TransitionClass);
	if (!*ClassRef)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unknown transition_class: '%s' (expected 'crossfade' or 'inertial_dead_blend')"),
			*TransitionClass));
	}

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString FromErr, ToErr;
	UMovieSceneSection* FromSec = ResolveAnimMixerSection(Track, FromSection, FromErr);
	if (!FromSec)
	{
		return MakeErrorJson(FString::Printf(TEXT("from_section: %s"), *FromErr));
	}
	UMovieSceneSection* ToSec = ResolveAnimMixerSection(Track, ToSection, ToErr);
	if (!ToSec)
	{
		return MakeErrorJson(FString::Printf(TEXT("to_section: %s"), *ToErr));
	}
	if (FromSec == ToSec)
	{
		return MakeErrorJson(TEXT("from_section and to_section must differ"));
	}

	Track->Modify();
	UMovieSceneAnimTransitionSectionBase* Transition = Track->CreateTransitionSectionOfType(FromSec, ToSec, ClassRef);
	if (!Transition)
	{
		return MakeErrorJson(TEXT("CreateTransitionSectionOfType returned null (sections may not overlap)"));
	}
	Sequence->MarkPackageDirty();

	const FFrameNumber StartFrame = Transition->HasStartFrame() ? Transition->GetInclusiveStartFrame() : FFrameNumber(0);
	const FFrameNumber EndFrame   = Transition->HasEndFrame()   ? Transition->GetExclusiveEndFrame()   : FFrameNumber(0);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	// Use FName.ToString() so transition_id round-trips through ResolveAnimMixerSection
	// (which matches Section->GetFName() == AsName) without ambiguity.
	Obj->SetStringField(TEXT("transition_id"), Transition->GetFName().ToString());
	Obj->SetNumberField(TEXT("start_frame"), StartFrame.Value);
	Obj->SetNumberField(TEXT("end_frame"), EndFrame.Value);
	Obj->SetStringField(TEXT("transition_class"), TransitionClassToShortName(Transition->GetClass()));
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerAddTransition"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerAutoCreateTransitions(
	const FString& SequencePath, const FString& TrackId, const FString& SectionId)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] AutoCreateTransitions seq=%s track=%s section=%s"),
		*SequencePath, *TrackId, *SectionId);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Auto Create Transitions")));
#endif

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString SecErr;
	UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, SecErr);
	if (!Section)
	{
		return MakeErrorJson(SecErr);
	}

	// Snapshot existing transitions for this section, call the void engine method,
	// then diff to determine which transitions were newly created.
	const TArray<UMovieSceneAnimTransitionSectionBase*> Before = Track->FindTransitionsForSection(Section);
	TSet<UMovieSceneAnimTransitionSectionBase*> BeforeSet(Before);

	Track->Modify();
	Track->AutoCreateTransitionsForSection(Section);
	Sequence->MarkPackageDirty();

	const TArray<UMovieSceneAnimTransitionSectionBase*> After = Track->FindTransitionsForSection(Section);

	TArray<TSharedPtr<FJsonValue>> CreatedArray;
	for (UMovieSceneAnimTransitionSectionBase* Transition : After)
	{
		if (!Transition || BeforeSet.Contains(Transition))
		{
			continue;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("transition_id"), Transition->GetFName().ToString());
		Item->SetStringField(TEXT("transition_class"), TransitionClassToShortName(Transition->GetClass()));
		const FFrameNumber StartFrame = Transition->HasStartFrame() ? Transition->GetInclusiveStartFrame() : FFrameNumber(0);
		const FFrameNumber EndFrame   = Transition->HasEndFrame()   ? Transition->GetExclusiveEndFrame()   : FFrameNumber(0);
		Item->SetNumberField(TEXT("start_frame"), StartFrame.Value);
		Item->SetNumberField(TEXT("end_frame"), EndFrame.Value);
		CreatedArray.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetArrayField(TEXT("transitions_created"), CreatedArray);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerAutoCreateTransitions"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerSetTransitionBlend(
	const FString& SequencePath, const FString& TrackId,
	const FString& TransitionId, const FString& ParamsJson)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] SetTransitionBlend seq=%s track=%s transition=%s params=%s"),
		*SequencePath, *TrackId, *TransitionId, *ParamsJson);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Set Transition Blend")));
#endif

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString SecErr;
	UMovieSceneSection* Section = ResolveAnimMixerSection(Track, TransitionId, SecErr);
	if (!Section)
	{
		return MakeErrorJson(FString::Printf(TEXT("transition_id: %s"), *SecErr));
	}
	UMovieSceneAnimTransitionSectionBase* Transition = Cast<UMovieSceneAnimTransitionSectionBase>(Section);
	if (!Transition)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Section '%s' is not a transition section (class=%s)"),
			*TransitionId, *Section->GetClass()->GetName()));
	}

	FString ParseErr;
	TSharedPtr<FJsonObject> Params = ParseParamsJson(ParamsJson, ParseErr);
	if (!Params.IsValid())
	{
		return MakeErrorJson(ParseErr);
	}

	TSharedRef<FJsonObject> Applied = MakeShared<FJsonObject>();
	const FString ClassShortName = TransitionClassToShortName(Transition->GetClass());

	if (UMovieSceneAnimCrossfadeTransitionSection* Crossfade = Cast<UMovieSceneAnimCrossfadeTransitionSection>(Transition))
	{
		double Value = 0.0;
		if (!Params->TryGetNumberField(TEXT("value"), Value))
		{
			return MakeErrorJson(TEXT("Crossfade params require {\"value\": <float>} — does not apply other keys"));
		}
		Crossfade->Modify();
		Crossfade->BlendCurve.SetDefault(static_cast<float>(Value));
		Sequence->MarkPackageDirty();
		Applied->SetNumberField(TEXT("value"), Value);
	}
	else if (UMovieSceneAnimInertialDeadBlendTransitionSection* IDB = Cast<UMovieSceneAnimInertialDeadBlendTransitionSection>(Transition))
	{
		FString Mode;
		if (!Params->TryGetStringField(TEXT("mode"), Mode))
		{
			return MakeErrorJson(TEXT("InertialDeadBlend params require {\"mode\": \"<EAlphaBlendOption>\"} — does not apply other keys"));
		}
		bool bOK = false;
		const EAlphaBlendOption Parsed = ParseAlphaBlendOption(Mode, bOK);
		if (!bOK)
		{
			return MakeErrorJson(FString::Printf(
				TEXT("Unknown EAlphaBlendOption value: '%s' (expected Linear, Cubic, HermiteCubic, …)"),
				*Mode));
		}
		IDB->Modify();
		IDB->BlendMode = Parsed;
		Sequence->MarkPackageDirty();
		Applied->SetStringField(TEXT("mode"), AlphaBlendOptionToString(Parsed));
	}
	else
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unsupported transition class for set_transition_blend: %s"),
			*Transition->GetClass()->GetName()));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("transition_class"), ClassShortName);
	Obj->SetObjectField(TEXT("applied_params"), Applied);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerSetTransitionBlend"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerChangeTransitionType(
	const FString& SequencePath, const FString& TrackId,
	const FString& TransitionId, const FString& NewClass)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] ChangeTransitionType seq=%s track=%s transition=%s new=%s"),
		*SequencePath, *TrackId, *TransitionId, *NewClass);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Change Transition Type")));
#endif

	TSubclassOf<UMovieSceneAnimTransitionSectionBase> NewClassRef = ResolveTransitionClass(NewClass);
	if (!*NewClassRef)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Unknown new_class: '%s' (expected 'crossfade' or 'inertial_dead_blend')"),
			*NewClass));
	}

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString SecErr;
	UMovieSceneSection* Section = ResolveAnimMixerSection(Track, TransitionId, SecErr);
	if (!Section)
	{
		return MakeErrorJson(FString::Printf(TEXT("transition_id: %s"), *SecErr));
	}
	UMovieSceneAnimTransitionSectionBase* OldTransition = Cast<UMovieSceneAnimTransitionSectionBase>(Section);
	if (!OldTransition)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Section '%s' is not a transition section (class=%s)"),
			*TransitionId, *Section->GetClass()->GetName()));
	}

	// Same-type request is a no-op.
	if (OldTransition->GetClass() == *NewClassRef)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), true);
		Obj->SetStringField(TEXT("transition_id"), OldTransition->GetFName().ToString());
		Obj->SetStringField(TEXT("new_class"), TransitionClassToShortName(OldTransition->GetClass()));
		Obj->SetStringField(TEXT("note"), TEXT("transition was already of new_class; no change"));
		return SerializeAnimMixerSuccess(Obj, Sequence, Track);
	}

	// FromSection / ToSection are public UPROPERTY fields on
	// UMovieSceneAnimTransitionSectionBase (not BlueprintPure accessors). Direct
	// field access here avoids a dep on UMovieSceneAnimMixerScripting (which is
	// where the static GetFromSection/GetToSection helpers live).
	UMovieSceneSection* FromSec = OldTransition->FromSection;
	UMovieSceneSection* ToSec   = OldTransition->ToSection;
	if (!FromSec || !ToSec)
	{
		return MakeErrorJson(TEXT("Existing transition has null From or To section; cannot rebuild"));
	}

	// Capture identity in case CreateTransitionSectionOfType returns null after we
	// already removed the old section (data-loss avoidance — Issue #11753 review B-2).
	const FName OldName = OldTransition->GetFName();
	const UClass* OldClass = OldTransition->GetClass();

	Track->Modify();
	// Remove the old transition first — CreateTransitionSectionOfType refuses to
	// create a second transition over the same pair otherwise.
	Track->RemoveSection(*OldTransition);
	UMovieSceneAnimTransitionSectionBase* NewTransition = Track->CreateTransitionSectionOfType(FromSec, ToSec, NewClassRef);
	if (!NewTransition)
	{
		// Data-loss avoidance: try to rebuild the original transition class so the
		// user is not left with an empty slot. Surface data_loss in the envelope
		// either way so callers can detect partial state.
		UMovieSceneAnimTransitionSectionBase* Rebuilt = Track->CreateTransitionSectionOfType(
			FromSec, ToSec, const_cast<UClass*>(OldClass));
		const bool bDataLoss = (Rebuilt == nullptr);
		if (Rebuilt)
		{
			Sequence->MarkPackageDirty();
			UE_LOG(LogNarshaMCPAnimMixer, Warning,
				TEXT("[AnimMixer] ChangeTransitionType: new class rejected; rebuilt original transition (%s)"),
				*OldName.ToString());
		}
		else
		{
			UE_LOG(LogNarshaMCPAnimMixer, Error,
				TEXT("[AnimMixer] ChangeTransitionType: new class rejected AND original (%s, %s) failed to rebuild — transition LOST"),
				*OldName.ToString(), OldClass ? *OldClass->GetName() : TEXT("?"));
		}
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"),
			TEXT("CreateTransitionSectionOfType returned null for new_class after removing old transition"));
		Err->SetBoolField(TEXT("data_loss"), bDataLoss);
		Err->SetStringField(TEXT("recovered_class"), Rebuilt ? TransitionClassToShortName(Rebuilt->GetClass()) : TEXT(""));
		Err->SetStringField(TEXT("recovered_id"), Rebuilt ? Rebuilt->GetFName().ToString() : TEXT(""));
		return SerializeJson(Err);
	}
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("transition_id"), NewTransition->GetFName().ToString());
	Obj->SetStringField(TEXT("new_class"), TransitionClassToShortName(NewTransition->GetClass()));
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerChangeTransitionType"));
#endif
}

// =============================================================================
// Phase 3 (#16112) — Easing authoring
// =============================================================================

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerGetSectionEasing(
	const FString& SequencePath, const FString& TrackId, const FString& SectionId)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] GetSectionEasing seq=%s track=%s section=%s"),
		*SequencePath, *TrackId, *SectionId);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_SEQUENCERSCRIPTING
	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString Err;
	UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, Err);
	if (!Section)
	{
		return MakeErrorJson(Err);
	}

	const int32 EaseIn = UMovieSceneSectionEasingExtensions::GetEaseInDuration(Section);
	const int32 EaseOut = UMovieSceneSectionEasingExtensions::GetEaseOutDuration(Section);

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("section_name"), Section->GetName());
	Obj->SetNumberField(TEXT("ease_in_frames"), EaseIn);
	Obj->SetNumberField(TEXT("ease_out_frames"), EaseOut);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("SequencerScripting"),
		TEXT("sequencer_scripting_plugin_unavailable"), TEXT("Section easing authoring"));
	#else
	return MakeUnsupported(TEXT("AnimMixerGetSectionEasing"));
	#endif
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerSetSectionEasing(
	const FString& SequencePath, const FString& TrackId, const FString& SectionId,
	int32 EaseInFrames, int32 EaseOutFrames)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] SetSectionEasing seq=%s track=%s section=%s in=%d out=%d"),
		*SequencePath, *TrackId, *SectionId, EaseInFrames, EaseOutFrames);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_SEQUENCERSCRIPTING
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Set Section Easing")));
#endif

	// -1 is the "leave unchanged" sentinel; rejecting all-unchanged keeps the op honest.
	if (EaseInFrames < 0 && EaseOutFrames < 0)
	{
		return MakeErrorJson(TEXT("Provide ease_in_frames and/or ease_out_frames (>= 0); both were omitted"));
	}

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString Err;
	UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, Err);
	if (!Section)
	{
		return MakeErrorJson(Err);
	}

	Section->Modify();
	if (EaseInFrames >= 0)
	{
		UMovieSceneSectionEasingExtensions::SetEaseInDuration(Section, EaseInFrames);
	}
	if (EaseOutFrames >= 0)
	{
		UMovieSceneSectionEasingExtensions::SetEaseOutDuration(Section, EaseOutFrames);
	}
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("section_name"), Section->GetName());
	Obj->SetNumberField(TEXT("ease_in_frames"), UMovieSceneSectionEasingExtensions::GetEaseInDuration(Section));
	Obj->SetNumberField(TEXT("ease_out_frames"), UMovieSceneSectionEasingExtensions::GetEaseOutDuration(Section));
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("SequencerScripting"),
		TEXT("sequencer_scripting_plugin_unavailable"), TEXT("Section easing authoring"));
	#else
	return MakeUnsupported(TEXT("AnimMixerSetSectionEasing"));
	#endif
#endif
}

// =============================================================================
// Phase 3 (#16112) — Condition authoring
// =============================================================================

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_SEQUENCERSCRIPTING
namespace
{
	enum class EAnimMixerConditionLevel : uint8 { Invalid, Section, Track, Row };

	EAnimMixerConditionLevel ParseConditionLevel(const FString& Level)
	{
		if (Level.Equals(TEXT("section"), ESearchCase::IgnoreCase)) return EAnimMixerConditionLevel::Section;
		if (Level.Equals(TEXT("track"), ESearchCase::IgnoreCase)) return EAnimMixerConditionLevel::Track;
		if (Level.Equals(TEXT("row"), ESearchCase::IgnoreCase)) return EAnimMixerConditionLevel::Row;
		return EAnimMixerConditionLevel::Invalid;
	}

	// Read the condition for a level. For Section, resolves SectionId. Sets OutError on failure.
	UMovieSceneCondition* ReadConditionForLevel(
		UMovieSceneAnimationMixerTrack* Track, EAnimMixerConditionLevel Level,
		const FString& SectionId, int32 RowIndex, FString& OutError)
	{
		switch (Level)
		{
		case EAnimMixerConditionLevel::Section:
		{
			UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, OutError);
			return Section ? UMovieSceneConditionExtensions::GetSectionCondition(Section) : nullptr;
		}
		case EAnimMixerConditionLevel::Track:
			return UMovieSceneConditionExtensions::GetTrackCondition(Track);
		case EAnimMixerConditionLevel::Row:
			return UMovieSceneConditionExtensions::GetTrackRowCondition(Track, RowIndex);
		default:
			OutError = TEXT("invalid level");
			return nullptr;
		}
	}
}
#endif

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerGetCondition(
	const FString& SequencePath, const FString& TrackId,
	const FString& Level, const FString& SectionId, int32 RowIndex)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] GetCondition seq=%s track=%s level=%s section=%s row=%d"),
		*SequencePath, *TrackId, *Level, *SectionId, RowIndex);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_SEQUENCERSCRIPTING
	const EAnimMixerConditionLevel Lvl = ParseConditionLevel(Level);
	if (Lvl == EAnimMixerConditionLevel::Invalid)
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown level '%s' (expected 'section', 'track', or 'row')"), *Level));
	}
	if (Lvl == EAnimMixerConditionLevel::Row && RowIndex < 0)
	{
		return MakeErrorJson(TEXT("level='row' requires a non-negative 'row_index'"));
	}

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	FString Err;
	UMovieSceneCondition* Condition = ReadConditionForLevel(Track, Lvl, SectionId, RowIndex, Err);
	if (!Err.IsEmpty())
	{
		return MakeErrorJson(Err);
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("level"), Level.ToLower());
	Obj->SetBoolField(TEXT("has_condition"), Condition != nullptr);
	Obj->SetStringField(TEXT("condition_class"), Condition ? Condition->GetClass()->GetPathName() : FString());
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("SequencerScripting"),
		TEXT("sequencer_scripting_plugin_unavailable"), TEXT("Section condition authoring"));
	#else
	return MakeUnsupported(TEXT("AnimMixerGetCondition"));
	#endif
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerSetCondition(
	const FString& SequencePath, const FString& TrackId,
	const FString& Level, const FString& SectionId, int32 RowIndex,
	const FString& ConditionClass)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] SetCondition seq=%s track=%s level=%s section=%s row=%d class=%s"),
		*SequencePath, *TrackId, *Level, *SectionId, RowIndex, *ConditionClass);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_SEQUENCERSCRIPTING
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Set Condition")));
#endif

	const EAnimMixerConditionLevel Lvl = ParseConditionLevel(Level);
	if (Lvl == EAnimMixerConditionLevel::Invalid)
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown level '%s' (expected 'section', 'track', or 'row')"), *Level));
	}
	// Row-level conditions index into track-row metadata; a negative row index
	// would otherwise create orphaned metadata at a non-existent row (Issue #16112 review).
	if (Lvl == EAnimMixerConditionLevel::Row && RowIndex < 0)
	{
		return MakeErrorJson(TEXT("level='row' requires a non-negative 'row_index'"));
	}

	UClass* LoadedClass = StaticLoadClass(UMovieSceneCondition::StaticClass(), nullptr, *ConditionClass);
	if (!LoadedClass)
	{
		return MakeErrorJson(FString::Printf(
			TEXT("condition_class '%s' is not a loadable UMovieSceneCondition subclass"), *ConditionClass));
	}
	// StaticLoadClass only checks the subclass relationship — NewObject on an
	// abstract/deprecated UClass triggers an engine check() and crashes the editor
	// over Remote Control, so reject those here for a graceful envelope (Issue #16112 review).
	if (LoadedClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("condition_class '%s' is abstract/deprecated and cannot be instantiated"), *ConditionClass));
	}
	TSubclassOf<UMovieSceneCondition> CondClass = LoadedClass;

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	switch (Lvl)
	{
	case EAnimMixerConditionLevel::Section:
	{
		FString Err;
		UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, Err);
		if (!Section)
		{
			return MakeErrorJson(Err);
		}
		Section->Modify();
		UMovieSceneConditionExtensions::SetSectionCondition(Section, CondClass);
		break;
	}
	case EAnimMixerConditionLevel::Track:
		Track->Modify();
		UMovieSceneConditionExtensions::SetTrackCondition(Track, CondClass);
		break;
	case EAnimMixerConditionLevel::Row:
		Track->Modify();
		UMovieSceneConditionExtensions::SetTrackRowCondition(Track, RowIndex, CondClass);
		break;
	default:
		break;
	}
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("level"), Level.ToLower());
	Obj->SetStringField(TEXT("condition_class"), LoadedClass->GetPathName());
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("SequencerScripting"),
		TEXT("sequencer_scripting_plugin_unavailable"), TEXT("Section condition authoring"));
	#else
	return MakeUnsupported(TEXT("AnimMixerSetCondition"));
	#endif
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerClearCondition(
	const FString& SequencePath, const FString& TrackId,
	const FString& Level, const FString& SectionId, int32 RowIndex)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] ClearCondition seq=%s track=%s level=%s section=%s row=%d"),
		*SequencePath, *TrackId, *Level, *SectionId, RowIndex);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_SEQUENCERSCRIPTING
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Clear Condition")));
#endif

	const EAnimMixerConditionLevel Lvl = ParseConditionLevel(Level);
	if (Lvl == EAnimMixerConditionLevel::Invalid)
	{
		return MakeErrorJson(FString::Printf(TEXT("Unknown level '%s' (expected 'section', 'track', or 'row')"), *Level));
	}
	if (Lvl == EAnimMixerConditionLevel::Row && RowIndex < 0)
	{
		return MakeErrorJson(TEXT("level='row' requires a non-negative 'row_index'"));
	}

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	switch (Lvl)
	{
	case EAnimMixerConditionLevel::Section:
	{
		FString Err;
		UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, Err);
		if (!Section)
		{
			return MakeErrorJson(Err);
		}
		Section->Modify();
		UMovieSceneConditionExtensions::ClearSectionCondition(Section);
		break;
	}
	case EAnimMixerConditionLevel::Track:
		Track->Modify();
		UMovieSceneConditionExtensions::ClearTrackCondition(Track);
		break;
	case EAnimMixerConditionLevel::Row:
		Track->Modify();
		UMovieSceneConditionExtensions::ClearTrackRowCondition(Track, RowIndex);
		break;
	default:
		break;
	}
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("level"), Level.ToLower());
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("SequencerScripting"),
		TEXT("sequencer_scripting_plugin_unavailable"), TEXT("Section condition authoring"));
	#else
	return MakeUnsupported(TEXT("AnimMixerClearCondition"));
	#endif
#endif
}

// =============================================================================
// Phase 4 (#16259) — Introspection reads
// =============================================================================

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerGetLayers(
	const FString& SequencePath, const FString& TrackId)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] GetLayers seq=%s track=%s"),
		*SequencePath, *TrackId);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	// Per-row section counts in a single pass over the track's sections (cheaper than
	// re-scanning GetAllSections once per layer).
	TMap<int32, int32> RowToSectionCount;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		if (Section)
		{
			RowToSectionCount.FindOrAdd(Section->GetRowIndex())++;
		}
	}

	TArray<TSharedPtr<FJsonValue>> LayersArray;
	for (UMovieSceneAnimationMixerLayer* Layer : Track->GetLayers())
	{
		if (!Layer)
		{
			continue;
		}
		const int32 LayerIndex = Layer->GetLayerIndex();
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetNumberField(TEXT("layer_index"), LayerIndex);
		FString DisplayName;
#if WITH_EDITORONLY_DATA
		DisplayName = Layer->GetDisplayName().ToString();
#endif
		Item->SetStringField(TEXT("layer_display_name"), DisplayName);
		Item->SetNumberField(TEXT("section_count"), RowToSectionCount.FindRef(LayerIndex));
		LayersArray.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetArrayField(TEXT("layers"), LayersArray);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerGetLayers"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerGetSections(
	const FString& SequencePath, const FString& TrackId, int32 LayerIndex)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] GetSections seq=%s track=%s layer=%d"),
		*SequencePath, *TrackId, LayerIndex);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	TArray<TSharedPtr<FJsonValue>> SectionsArray;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		if (!Section)
		{
			continue;
		}
		const int32 RowIndex = Section->GetRowIndex();
		// LayerIndex < 0 → all rows; otherwise filter to the requested row.
		if (LayerIndex >= 0 && RowIndex != LayerIndex)
		{
			continue;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("section_name"), Section->GetName());
		Item->SetNumberField(TEXT("layer_index"), RowIndex);
		const FFrameNumber StartFrame = Section->HasStartFrame() ? Section->GetInclusiveStartFrame() : FFrameNumber(0);
		const FFrameNumber EndFrame   = Section->HasEndFrame()   ? Section->GetExclusiveEndFrame()   : FFrameNumber(0);
		Item->SetNumberField(TEXT("start_frame"), StartFrame.Value);
		Item->SetNumberField(TEXT("end_frame"), EndFrame.Value);
		// section_type is the section's UClass name — always safe (base UObject API).
		Item->SetStringField(TEXT("section_type"), Section->GetClass()->GetName());
		// anim_path enrichment (#19932, was deferred in #16259): an AnimMixer animation
		// section is a UMovieSceneSkeletalAnimationSection, whose UMovieSceneSection::
		// GetSourceObject() override returns Params.Animation (verified vs UE 5.8 engine
		// source: MovieSceneSkeletalAnimationSection.cpp `GetSourceObject() { return
		// Params.Animation; }`; AddNewAnimationOnRow sets `NewSection->Params.Animation`).
		// GetSourceObject() is a virtual on the already-included base UMovieSceneSection
		// (default nullptr), so no cast to the concrete section class — and no new module
		// dependency — is needed; transition / weight / mask sections inherit the base
		// nullptr, so anim_path is omitted for them. This completes the add_animation <->
		// get_sections round-trip (the section's AnimSequence path is now discoverable).
		if (UAnimSequenceBase* Anim = Cast<UAnimSequenceBase>(Section->GetSourceObject()))
		{
			Item->SetStringField(TEXT("anim_path"), Anim->GetPathName());
		}
		SectionsArray.Add(MakeShared<FJsonValueObject>(Item));
	}

	// Issue #25921 (Phase 5b): mask sections are owned by the layer's Mask decoration,
	// NOT the track ("not added to the mixer track" — MovieSceneAnimMixerMaskSection.cpp
	// constructor comment), so the GetAllSections() loop above cannot see them. Surface
	// them here, with the attached BlendMask's asset path as `blend_mask`, so
	// anim_mixer_set_blend_mask has a read-back. FindDecoration (non-creating) keeps
	// this a pure read.
	for (const TObjectPtr<UMovieSceneAnimationMixerLayer>& LayerPtr : Track->GetLayers())
	{
		UMovieSceneAnimationMixerLayer* Layer = LayerPtr;
		if (!Layer)
		{
			continue;
		}
		const int32 LayerRow = Layer->GetLayerIndex();
		if (LayerIndex >= 0 && LayerRow != LayerIndex)
		{
			continue;
		}
		UMovieSceneAnimationMaskDecoration* Decoration = Layer->FindDecoration<UMovieSceneAnimationMaskDecoration>();
		if (!Decoration)
		{
			continue;
		}
		for (const TObjectPtr<UMovieSceneSection>& MaskSectionPtr : Decoration->MaskSections)
		{
			UMovieSceneAnimMixerMaskSection* MaskSection = Cast<UMovieSceneAnimMixerMaskSection>(MaskSectionPtr);
			if (!MaskSection)
			{
				continue;
			}
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("section_name"), MaskSection->GetName());
			Item->SetNumberField(TEXT("layer_index"), LayerRow);
			const FFrameNumber MaskStart = MaskSection->HasStartFrame() ? MaskSection->GetInclusiveStartFrame() : FFrameNumber(0);
			const FFrameNumber MaskEnd   = MaskSection->HasEndFrame()   ? MaskSection->GetExclusiveEndFrame()   : FFrameNumber(0);
			Item->SetNumberField(TEXT("start_frame"), MaskStart.Value);
			Item->SetNumberField(TEXT("end_frame"), MaskEnd.Value);
			Item->SetStringField(TEXT("section_type"), MaskSection->GetClass()->GetName());
			const UObject* BlendMask = MaskSection->GetBlendMask().ResolveObjectPtr();
			Item->SetStringField(TEXT("blend_mask"), BlendMask ? BlendMask->GetPathName() : TEXT(""));
			SectionsArray.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetArrayField(TEXT("sections"), SectionsArray);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerGetSections"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerGetTransitions(
	const FString& SequencePath, const FString& TrackId, const FString& SectionId)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] GetTransitions seq=%s track=%s section=%s"),
		*SequencePath, *TrackId, *SectionId);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	// An empty SectionId enumerates every transition section on the track (cast filter
	// over GetAllSections); a non-empty SectionId returns only the transitions touching
	// that section (FindTransitionsForSection).
	TArray<UMovieSceneAnimTransitionSectionBase*> Transitions;
	if (SectionId.IsEmpty())
	{
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (UMovieSceneAnimTransitionSectionBase* Transition = Cast<UMovieSceneAnimTransitionSectionBase>(Section))
			{
				Transitions.Add(Transition);
			}
		}
	}
	else
	{
		FString Err;
		UMovieSceneSection* Section = ResolveAnimMixerSection(Track, SectionId, Err);
		if (!Section)
		{
			return MakeErrorJson(Err);
		}
		Transitions = Track->FindTransitionsForSection(Section);
	}

	TArray<TSharedPtr<FJsonValue>> TransitionsArray;
	for (UMovieSceneAnimTransitionSectionBase* Transition : Transitions)
	{
		if (!Transition)
		{
			continue;
		}
		TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("transition_id"), Transition->GetFName().ToString());
		Item->SetStringField(TEXT("transition_class"), TransitionClassToShortName(Transition->GetClass()));
		// FromSection / ToSection are public UPROPERTY pointers on the transition base.
		Item->SetStringField(TEXT("from_section"),
			Transition->FromSection ? Transition->FromSection->GetFName().ToString() : FString());
		Item->SetStringField(TEXT("to_section"),
			Transition->ToSection ? Transition->ToSection->GetFName().ToString() : FString());
		const FFrameNumber StartFrame = Transition->HasStartFrame() ? Transition->GetInclusiveStartFrame() : FFrameNumber(0);
		const FFrameNumber EndFrame   = Transition->HasEndFrame()   ? Transition->GetExclusiveEndFrame()   : FFrameNumber(0);
		Item->SetNumberField(TEXT("start_frame"), StartFrame.Value);
		Item->SetNumberField(TEXT("end_frame"), EndFrame.Value);
		TransitionsArray.Add(MakeShared<FJsonValueObject>(Item));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetArrayField(TEXT("transitions"), TransitionsArray);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerGetTransitions"));
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerCreateBlendMask(
	const FString& SkeletonPath, const FString& AssetPath, const FString& BoneWeightsJson)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] CreateBlendMask skeleton=%s asset=%s"),
		*SkeletonPath, *AssetPath);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	// Validate every input before creating a package or object. Error paths must not
	// leave a partial asset in memory or on disk (#25920 AC-5).
	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		const FSoftObjectPath SoftSkeletonPath(SkeletonPath);
		Skeleton = Cast<USkeleton>(SoftSkeletonPath.TryLoad());
	}
	if (!Skeleton)
	{
		return MakeErrorJson(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
	}

	TSharedPtr<FJsonObject> BoneWeightsObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BoneWeightsJson);
	if (!FJsonSerializer::Deserialize(Reader, BoneWeightsObject) || !BoneWeightsObject.IsValid())
	{
		return MakeErrorJson(TEXT("bone_weights must be a JSON object"));
	}
	if (BoneWeightsObject->Values.IsEmpty())
	{
		return MakeErrorJson(TEXT("bone_weights must contain at least one bone override"));
	}

	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
	TMap<int32, float> BoneWeightsByIndex;
	for (const auto& Pair : BoneWeightsObject->Values)
	{
		const FString BoneName = FString(Pair.Key);
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Number)
		{
			return MakeErrorJson(FString::Printf(TEXT("Weight for bone '%s' must be a number"), *BoneName));
		}

		const double Weight = Pair.Value->AsNumber();
		if (!FMath::IsFinite(Weight) || Weight < 0.0 || Weight > 1.0)
		{
			return MakeErrorJson(FString::Printf(TEXT("Weight for bone '%s' must be finite and in [0, 1]"), *BoneName));
		}

		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(FName(*BoneName));
		if (BoneIndex == INDEX_NONE)
		{
			return MakeErrorJson(FString::Printf(TEXT("Bone not found in skeleton: %s"), *BoneName));
		}
		BoneWeightsByIndex.Add(BoneIndex, static_cast<float>(Weight));
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	FText InvalidPackageReason;
	if (!PackageName.StartsWith(TEXT("/Game/")) ||
		!FPackageName::IsValidLongPackageName(PackageName, false, &InvalidPackageReason))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("asset_path must be a valid writable /Game package path: %s"),
			*InvalidPackageReason.ToString()));
	}

	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	if (AssetName.IsEmpty())
	{
		return MakeErrorJson(TEXT("asset_path must include an asset name"));
	}
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
	if (FPackageName::DoesPackageExist(PackageName) || FindObject<UObject>(nullptr, *ObjectPath))
	{
		return MakeErrorJson(FString::Printf(TEXT("Asset already exists: %s"), *ObjectPath));
	}

	// UE 5.9 renamed the class. Prefer the new name and fall back to the UE 5.8
	// name, keeping all UAF symbols out of this module's link surface.
	static const TCHAR* BlendMaskClassPaths[] = {
		TEXT("/Script/UAF.UAFSkeletonBlendMask"),
		TEXT("/Script/UAF.UAFBlendMask"),
	};
	UClass* BlendMaskClass = nullptr;
	FString ResolvedClassPath;
	for (const TCHAR* CandidatePath : BlendMaskClassPaths)
	{
		if (UClass* Candidate = StaticLoadClass(UObject::StaticClass(), nullptr, CandidatePath))
		{
			if (!Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
			{
				BlendMaskClass = Candidate;
				ResolvedClassPath = CandidatePath;
				break;
			}
		}
	}
	if (!BlendMaskClass)
	{
		return MakeErrorJson(TEXT("No supported UAF BlendMask class is available (tried UAFSkeletonBlendMask and UAFBlendMask)"));
	}

	FObjectPropertyBase* TableProperty = FindFProperty<FObjectPropertyBase>(BlendMaskClass, TEXT("Table"));
	if (!TableProperty || !TableProperty->PropertyClass ||
		!TableProperty->PropertyClass->IsChildOf(UHierarchyTable::StaticClass()))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Resolved BlendMask class has no compatible Table property: %s"), *ResolvedClassPath));
	}

	FString PackageFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
		PackageName, PackageFilename, FPackageName::GetAssetPackageExtension()))
	{
		return MakeErrorJson(FString::Printf(TEXT("Unable to map package path to a filename: %s"), *PackageName));
	}

	const FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "CreateAnimMixerBlendMask", "Create AnimMixer BlendMask"));
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return MakeErrorJson(FString::Printf(TEXT("Unable to create package: %s"), *PackageName));
	}
	Package->FullyLoad();

	UObject* BlendMask = NewObject<UObject>(
		Package, BlendMaskClass, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!BlendMask)
	{
		return MakeErrorJson(FString::Printf(TEXT("Unable to create BlendMask object: %s"), *ObjectPath));
	}

	UHierarchyTable* Table = NewObject<UHierarchyTable>(BlendMask, NAME_None, RF_Transactional);
	FInstancedStruct TableMetadata;
	TableMetadata.InitializeAs<FHierarchyTable_TableType_Skeleton>();
	TableMetadata.GetMutable<FHierarchyTable_TableType_Skeleton>().Skeleton = Skeleton;
	Table->Initialize(TableMetadata, FHierarchyTable_ElementType_Mask::StaticStruct());

	FInstancedStruct DefaultEntry = Table->CreateDefaultValue();
	FInstancedStruct DefaultTablePayload;
	DefaultTablePayload.InitializeAs<FHierarchyTable_TablePayloadType_Skeleton>();
	DefaultTablePayload.GetMutable<FHierarchyTable_TablePayloadType_Skeleton>().EntryType =
		ESkeletonHierarchyTable_TablePayloadEntryType::Bone;

	TArray<FHierarchyTableEntryData> EntriesToAdd;
	EntriesToAdd.Reserve(ReferenceSkeleton.GetNum());
	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
	{
		FHierarchyTableEntryData Entry;
		Entry.Parent = ReferenceSkeleton.GetParentIndex(BoneIndex);
		Entry.Identifier = ReferenceSkeleton.GetBoneName(BoneIndex);
		Entry.TablePayload = DefaultTablePayload;
		Entry.Payload = BoneIndex == 0 ? TOptional<FInstancedStruct>(DefaultEntry) : TOptional<FInstancedStruct>();
		Entry.OwnerTable = Table;
		EntriesToAdd.Add(MoveTemp(Entry));
	}
	Table->AddBulkEntries(EntriesToAdd);

	for (const TPair<int32, float>& Pair : BoneWeightsByIndex)
	{
		FHierarchyTableEntryData* Entry = Table->GetMutableTableEntry(Pair.Key);
		if (!Entry)
		{
			BlendMask->ClearFlags(RF_Public | RF_Standalone);
			BlendMask->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			BlendMask->MarkAsGarbage();
			Package->SetDirtyFlag(false);
			return MakeErrorJson(TEXT("Internal error while applying a bone override"));
		}
		Entry->Payload = Table->CreateDefaultValue();
		Entry->GetMutableValue<FHierarchyTable_ElementType_Mask>()->Value = Pair.Value;
	}
	Table->RegenerateEntriesGuid();
	TableProperty->SetObjectPropertyValue_InContainer(BlendMask, Table);

	TSharedRef<FJsonObject> EffectiveWeights = MakeShared<FJsonObject>();
	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
	{
		const FHierarchyTableEntryData* Entry = Table->GetTableEntry(BoneIndex);
		EffectiveWeights->SetNumberField(
			ReferenceSkeleton.GetBoneName(BoneIndex).ToString(),
			Entry->GetValue<FHierarchyTable_ElementType_Mask>()->Value);
	}

	Package->MarkPackageDirty();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), true);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, BlendMask, *PackageFilename, SaveArgs))
	{
		// Remove only the exact destination file. The source skeleton and every
		// pre-existing package were validated before mutation and are never touched.
		IFileManager::Get().Delete(*PackageFilename, false, true, true);
		BlendMask->ClearFlags(RF_Public | RF_Standalone);
		BlendMask->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
		BlendMask->MarkAsGarbage();
		Package->SetDirtyFlag(false);
		return MakeErrorJson(FString::Printf(TEXT("Failed to save BlendMask asset: %s"), *ObjectPath));
	}

	FAssetRegistryModule::AssetCreated(BlendMask);
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), ObjectPath);
	Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
	Result->SetStringField(TEXT("resolved_class"), ResolvedClassPath);
	Result->SetNumberField(TEXT("requested_override_count"), BoneWeightsByIndex.Num());
	Result->SetObjectField(TEXT("requested_overrides"), BoneWeightsObject.ToSharedRef());
	Result->SetObjectField(TEXT("effective_bone_weights"), EffectiveWeights);
	return SerializeJson(Result);
#else
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetBoolField(TEXT("editor_required"), true);
	Obj->SetStringField(TEXT("error"), TEXT("AnimMixerCreateBlendMask requires an editor build"));
	return SerializeJson(Obj);
#endif // WITH_EDITOR
#else
	return MakeUnsupported(TEXT("AnimMixerCreateBlendMask"));
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerSetBlendMask(
	const FString& SequencePath, const FString& TrackId, int32 LayerIndex, const FString& MaskAssetPath)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] SetBlendMask seq=%s track=%s layer=%d mask=%s"),
		*SequencePath, *TrackId, LayerIndex, *MaskAssetPath);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Set Blend Mask")));
#endif

	ULevelSequence* Sequence = nullptr;
	FString ResultJson;
	UMovieSceneAnimationMixerTrack* Track = ResolveSequenceAndTrack(SequencePath, TrackId, Sequence, ResultJson);
	if (!Track)
	{
		return ResultJson;
	}

	UMovieSceneAnimationMixerLayer* Layer = Track->GetLayer(LayerIndex);
	if (!Layer)
	{
		return MakeErrorJson(FString::Printf(TEXT("No layer at index %d (track has %d layers)"), LayerIndex, Track->GetLayers().Num()));
	}

	if (MaskAssetPath.IsEmpty())
	{
		return MakeErrorJson(TEXT("MaskAssetPath is empty"));
	}
	UObject* MaskObj = LoadObject<UObject>(nullptr, *MaskAssetPath);
	if (!MaskObj)
	{
		const FSoftObjectPath SoftPath(MaskAssetPath);
		MaskObj = SoftPath.TryLoad();
	}
	if (!MaskObj)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load BlendMask asset: %s"), *MaskAssetPath));
	}

	// The engine's SetBlendMask silently ignores anything that fails its internal
	// Cast<UUAFBlendMask> (MovieSceneAnimMixerMaskSection.cpp), so reject wrong-class
	// assets up front. Class identity is checked by NAME along the super chain —
	// NarshaMCP links no UAF symbol, and the dual list absorbs the 5.9 rename
	// (UAFBlendMask → UAFSkeletonBlendMask) the same way AnimMixerCreateBlendMask does.
	const FString ActualClassName = MaskObj->GetClass()->GetName();
	FString ResolvedMaskClass;
	for (const UClass* Klass = MaskObj->GetClass(); Klass; Klass = Klass->GetSuperClass())
	{
		const FString KlassName = Klass->GetName();
		if (KlassName == TEXT("UAFSkeletonBlendMask") || KlassName == TEXT("UAFBlendMask"))
		{
			ResolvedMaskClass = KlassName;
			break;
		}
	}
	if (ResolvedMaskClass.IsEmpty())
	{
		return MakeErrorJson(FString::Printf(
			TEXT("Asset is not a UAF BlendMask (expected UAFSkeletonBlendMask or UAFBlendMask, got %s): %s"),
			*ActualClassName, *MaskAssetPath));
	}

	Layer->Modify();
	UMovieSceneAnimationMaskDecoration* Decoration = Layer->GetOrCreateDecoration<UMovieSceneAnimationMaskDecoration>();
	if (!Decoration)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return MakeErrorJson(TEXT("Failed to get/create Mask decoration"));
	}

	// Reuse the layer's existing mask section when present; otherwise replay the
	// editor's own add-mask flow (AnimMixerDecorationMenuProviders.cpp:174-207):
	// CreateNewSection → SetRange BEFORE AddSection (masks don't support infinite
	// ranges, and AddSection trims overlaps against the range set here) → AddSection
	// (which wires OwningLayer + the layer's row index).
	//
	// The decoration supports multiple mask sections (SupportsMultipleSections), and
	// the editor's menu flow always ADDS one rather than reusing, so a layer authored
	// by hand can hold several. Retargeting the first is deterministic (MaskSections
	// order), but it leaves the others untouched — so report how many exist as
	// `mask_section_count`; a caller seeing > 1 knows this op only moved one of them.
	UMovieSceneAnimMixerMaskSection* MaskSection = nullptr;
	bool bCreatedSection = false;
	int32 ExistingMaskSectionCount = 0;
	for (const TObjectPtr<UMovieSceneSection>& Existing : Decoration->MaskSections)
	{
		if (UMovieSceneAnimMixerMaskSection* ExistingMask = Cast<UMovieSceneAnimMixerMaskSection>(Existing))
		{
			++ExistingMaskSectionCount;
			if (!MaskSection)
			{
				MaskSection = ExistingMask;
			}
		}
	}
	if (!MaskSection)
	{
		Decoration->Modify();
		UMovieSceneSection* NewSection = Decoration->CreateNewSection();
		MaskSection = Cast<UMovieSceneAnimMixerMaskSection>(NewSection);
		if (!MaskSection)
		{
#if WITH_EDITOR
			Transaction.Cancel();
#endif
			return MakeErrorJson(TEXT("Mask decoration did not create a UMovieSceneAnimMixerMaskSection"));
		}
		MaskSection->SetRange(Sequence->GetMovieScene()->GetPlaybackRange());
		Decoration->AddSection(MaskSection);
		bCreatedSection = true;
		ExistingMaskSectionCount = 1;
	}

	MaskSection->Modify();
	MaskSection->SetBlendMask(MaskObj);

	// SetBlendMask is a silent no-op on a failed internal cast; re-read to detect it.
	// GetBlendMask() is inline (TObjectKey — no UAF import symbol involved). On a
	// mismatch, cancel the transaction so no empty mask section / dirty flag is left.
	UObject* Applied = MaskSection->GetBlendMask().ResolveObjectPtr();
	if (Applied != MaskObj)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return MakeErrorJson(FString::Printf(
			TEXT("Engine SetBlendMask silently rejected the asset (class %s): %s"),
			*ActualClassName, *MaskAssetPath));
	}
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetNumberField(TEXT("layer_index"), LayerIndex);
	Obj->SetStringField(TEXT("mask_asset_path"), MaskObj->GetPathName());
	Obj->SetStringField(TEXT("section_name"), MaskSection->GetName());
	Obj->SetStringField(TEXT("resolved_mask_class"), ResolvedMaskClass);
	Obj->SetBoolField(TEXT("created_section"), bCreatedSection);
	Obj->SetNumberField(TEXT("mask_section_count"), ExistingMaskSectionCount);
	return SerializeAnimMixerSuccess(Obj, Sequence, Track);
#else
	return MakeUnsupported(TEXT("AnimMixerSetBlendMask"));
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerReloadEvaluateBlendMask(
	const FString& AssetPath, const FString& ExpectedOverridesJson)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] ReloadEvaluateBlendMask asset=%s"),
		*AssetPath);

#if NARSHAMCP_ANIMMIXER_SUPPORTED
#if WITH_EDITOR
	TSharedPtr<FJsonObject> ExpectedOverrides;
	const TSharedRef<TJsonReader<>> ExpectedReader =
		TJsonReaderFactory<>::Create(ExpectedOverridesJson);
	if (!FJsonSerializer::Deserialize(ExpectedReader, ExpectedOverrides) ||
		!ExpectedOverrides.IsValid() || ExpectedOverrides->Values.IsEmpty())
	{
		return MakeErrorJson(
			TEXT("expected_overrides must be a non-empty JSON object"),
			TEXT("invalid_expected_overrides"));
	}

	const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
	FText InvalidPackageReason;
	if (!PackageName.StartsWith(TEXT("/Game/")) ||
		!FPackageName::IsValidLongPackageName(PackageName, false, &InvalidPackageReason))
	{
		return MakeErrorJson(FString::Printf(
			TEXT("asset_path must be a valid /Game package path: %s"),
			*InvalidPackageReason.ToString()), TEXT("invalid_asset_path"));
	}
	const FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
	if (AssetName.IsEmpty())
	{
		return MakeErrorJson(TEXT("asset_path must include an asset name"), TEXT("invalid_asset_path"));
	}
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);

	auto LoadExactMask = [&ObjectPath]() -> UObject*
	{
		UObject* Found = FindObject<UObject>(nullptr, *ObjectPath);
		if (!Found)
		{
			Found = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		}
		return Found;
	};

	UObject* Mask = LoadExactMask();
	if (!Mask)
	{
		return MakeErrorJson(
			FString::Printf(TEXT("BlendMask not found at exact path: %s"), *ObjectPath),
			TEXT("asset_not_found"));
	}
	if (Mask->GetPathName() != ObjectPath)
	{
		return MakeErrorJson(
			FString::Printf(TEXT("Loaded object path %s does not match requested %s"),
				*Mask->GetPathName(), *ObjectPath),
			TEXT("identity_mismatch"));
	}

	static const TCHAR* BlendMaskClassPaths[] = {
		TEXT("/Script/UAF.UAFSkeletonBlendMask"),
		TEXT("/Script/UAF.UAFBlendMask"),
	};
	UClass* BlendMaskClass = nullptr;
	FString ResolvedClassPath;
	for (const TCHAR* CandidatePath : BlendMaskClassPaths)
	{
		if (UClass* Candidate = StaticLoadClass(UObject::StaticClass(), nullptr, CandidatePath))
		{
			if (!Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) &&
				Mask->IsA(Candidate))
			{
				BlendMaskClass = Candidate;
				ResolvedClassPath = CandidatePath;
				break;
			}
		}
	}
	if (!BlendMaskClass)
	{
		return MakeErrorJson(
			TEXT("Object at asset_path is not a supported UAF BlendMask class"),
			TEXT("invalid_class"));
	}

	UPackage* Package = Mask->GetOutermost();
	Mask = nullptr;
	FText UnloadError;
	if (!UPackageTools::UnloadPackages(
			TArray<UPackage*>{Package}, UnloadError, /*bUnloadDirtyPackages=*/true))
	{
		return MakeErrorJson(
			FString::Printf(TEXT("Failed to unload package: %s"), *UnloadError.ToString()),
			TEXT("unload_failed"));
	}
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);

	UObject* Reloaded = LoadExactMask();
	if (!Reloaded || Reloaded->GetPathName() != ObjectPath || !Reloaded->IsA(BlendMaskClass))
	{
		return MakeErrorJson(
			FString::Printf(TEXT("Reload did not restore exact object %s"), *ObjectPath),
			TEXT("identity_mismatch"));
	}

	FObjectPropertyBase* TableProperty =
		FindFProperty<FObjectPropertyBase>(Reloaded->GetClass(), TEXT("Table"));
	UHierarchyTable* Table = TableProperty
		? Cast<UHierarchyTable>(TableProperty->GetObjectPropertyValue_InContainer(Reloaded))
		: nullptr;
	if (!Table)
	{
		return MakeErrorJson(TEXT("Reloaded BlendMask has no HierarchyTable"), TEXT("table_missing"));
	}

	FObjectPropertyBase* SkeletonProperty =
		FindFProperty<FObjectPropertyBase>(Reloaded->GetClass(), TEXT("Skeleton"));
	USkeleton* Skeleton = SkeletonProperty
		? Cast<USkeleton>(SkeletonProperty->GetObjectPropertyValue_InContainer(Reloaded))
		: nullptr;
	if (!Skeleton)
	{
		return MakeErrorJson(TEXT("Reloaded BlendMask has no Skeleton"), TEXT("skeleton_missing"));
	}
	const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();

	TSharedRef<FJsonObject> EffectiveWeights = MakeShared<FJsonObject>();
	for (int32 BoneIndex = 0; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
	{
		const FHierarchyTableEntryData* Entry = Table->GetTableEntry(BoneIndex);
		if (!Entry)
		{
			return MakeErrorJson(TEXT("Reloaded table is missing a skeleton bone entry"), TEXT("table_incomplete"));
		}
		const FHierarchyTable_ElementType_Mask* MaskValue =
			Entry->GetValue<FHierarchyTable_ElementType_Mask>();
		if (!MaskValue)
		{
			return MakeErrorJson(TEXT("Reloaded table entry is not a mask element"), TEXT("table_incomplete"));
		}
		EffectiveWeights->SetNumberField(
			ReferenceSkeleton.GetBoneName(BoneIndex).ToString(), MaskValue->Value);
	}

	TSet<int32> ExpectedBoneIndices;
	TMap<int32, float> ExpectedByIndex;
	for (const auto& Pair : ExpectedOverrides->Values)
	{
		const FString BoneName = FString(Pair.Key);
		if (!Pair.Value.IsValid() || Pair.Value->Type != EJson::Number)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("Expected weight for bone '%s' must be a number"), *BoneName),
				TEXT("invalid_expected_overrides"));
		}
		const double Expected = Pair.Value->AsNumber();
		if (!FMath::IsFinite(Expected) || Expected < 0.0 || Expected > 1.0)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("Expected weight for bone '%s' must be finite and in [0, 1]"), *BoneName),
				TEXT("invalid_expected_overrides"));
		}
		const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(FName(*BoneName));
		if (BoneIndex == INDEX_NONE)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("Expected bone not in reloaded skeleton: %s"), *BoneName),
				TEXT("bone_not_found"));
		}
		const FHierarchyTableEntryData* Entry = Table->GetTableEntry(BoneIndex);
		const FHierarchyTable_ElementType_Mask* MaskValue =
			Entry ? Entry->GetValue<FHierarchyTable_ElementType_Mask>() : nullptr;
		if (!MaskValue || !FMath::IsNearlyEqual(MaskValue->Value, static_cast<float>(Expected)))
		{
			return MakeErrorJson(
				FString::Printf(TEXT("Override mismatch for bone '%s'"), *BoneName),
				TEXT("override_mismatch"));
		}
		ExpectedBoneIndices.Add(BoneIndex);
		ExpectedByIndex.Add(BoneIndex, static_cast<float>(Expected));
	}

	// Deepest expected bone that has a descendant which is NOT itself an override.
	// TMap iteration order must not pick another requested override as "inherited".
	int32 ParentOverrideIndex = INDEX_NONE;
	int32 DescendantIndex = INDEX_NONE;
	for (int32 ParentIndex = ReferenceSkeleton.GetNum() - 1; ParentIndex >= 0; --ParentIndex)
	{
		if (!ExpectedBoneIndices.Contains(ParentIndex))
		{
			continue;
		}
		for (int32 BoneIndex = ParentIndex + 1; BoneIndex < ReferenceSkeleton.GetNum(); ++BoneIndex)
		{
			if (ExpectedBoneIndices.Contains(BoneIndex))
			{
				continue;
			}
			int32 Ancestor = ReferenceSkeleton.GetParentIndex(BoneIndex);
			while (Ancestor != INDEX_NONE)
			{
				if (Ancestor == ParentIndex)
				{
					ParentOverrideIndex = ParentIndex;
					DescendantIndex = BoneIndex;
					break;
				}
				Ancestor = ReferenceSkeleton.GetParentIndex(Ancestor);
			}
			if (DescendantIndex != INDEX_NONE)
			{
				break;
			}
		}
		if (DescendantIndex != INDEX_NONE)
		{
			break;
		}
	}
	if (DescendantIndex == INDEX_NONE || ParentOverrideIndex == INDEX_NONE)
	{
		return MakeErrorJson(
			TEXT("No inherited descendant bone found for a requested override"),
			TEXT("descendant_missing"));
	}
	const FHierarchyTableEntryData* DescendantEntry = Table->GetTableEntry(DescendantIndex);
	const FHierarchyTable_ElementType_Mask* DescendantMask =
		DescendantEntry ? DescendantEntry->GetValue<FHierarchyTable_ElementType_Mask>() : nullptr;
	const float* ParentExpected = ExpectedByIndex.Find(ParentOverrideIndex);
	if (!DescendantMask || !ParentExpected
		|| !FMath::IsNearlyEqual(DescendantMask->Value, *ParentExpected))
	{
		return MakeErrorJson(
			TEXT("Inherited descendant weight does not match the parent override"),
			TEXT("descendant_mismatch"));
	}
	const double InheritedDescendantWeight = DescendantMask->Value;

	UMovieSceneAnimationMaskDecoration* Decoration =
		NewObject<UMovieSceneAnimationMaskDecoration>(GetTransientPackage());
	if (!Decoration)
	{
		return MakeErrorJson(
			TEXT("UMovieSceneAnimationMaskDecoration is unavailable"),
			TEXT("oracle_surface_unavailable"));
	}
	UMovieSceneSection* Section = Decoration->CreateNewSection();
	if (!Section)
	{
		return MakeErrorJson(
			TEXT("AnimMixer mask section could not be created"),
			TEXT("oracle_surface_unavailable"));
	}
	Decoration->AddSection(Section);
	FObjectPropertyBase* BlendMaskProperty =
		FindFProperty<FObjectPropertyBase>(Section->GetClass(), TEXT("BlendMask"));
	if (!BlendMaskProperty)
	{
		return MakeErrorJson(
			TEXT("Mask section has no BlendMask property"),
			TEXT("oracle_surface_unavailable"));
	}
	BlendMaskProperty->SetObjectPropertyValue_InContainer(Section, Reloaded);
	if (BlendMaskProperty->GetObjectPropertyValue_InContainer(Section) != Reloaded)
	{
		return MakeErrorJson(
			TEXT("Mask section did not retain the exact reloaded BlendMask"),
			TEXT("identity_mismatch"));
	}

	UScriptStruct* TaskStruct = FindObject<UScriptStruct>(
		nullptr, TEXT("/Script/UAFAnimGraph.AnimNextBlendKeyframePerBoneWithScaleTask"));
	if (!TaskStruct)
	{
		TaskStruct = LoadObject<UScriptStruct>(
			nullptr, TEXT("/Script/UAFAnimGraph.AnimNextBlendKeyframePerBoneWithScaleTask"));
	}
	FStructProperty* CachedProperty =
		FindFProperty<FStructProperty>(Reloaded->GetClass(), TEXT("CachedBlendProfileData"));
	const FBlendProfileStandaloneCachedData* CachedData =
		(CachedProperty && CachedProperty->Struct == FBlendProfileStandaloneCachedData::StaticStruct())
			? CachedProperty->ContainerPtrToValuePtr<FBlendProfileStandaloneCachedData>(Reloaded)
			: nullptr;
	if (!TaskStruct || !CachedData || CachedData->GetBoneBlendWeights().Num() != ReferenceSkeleton.GetNum()
		|| CachedData->GetSkeleton().Get() != Skeleton)
	{
		return MakeErrorJson(
			TEXT("Evaluation-task struct or reloaded cached blend profile is unavailable"),
			TEXT("oracle_surface_unavailable"));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), ObjectPath);
	Result->SetStringField(TEXT("skeleton_path"), Skeleton->GetPathName());
	Result->SetStringField(TEXT("resolved_class"), ResolvedClassPath);
	Result->SetBoolField(TEXT("reload"), true);
	Result->SetStringField(TEXT("oracle_module"), TEXT("NarshaMCP"));
	Result->SetNumberField(TEXT("requested_override_count"), ExpectedOverrides->Values.Num());
	Result->SetObjectField(TEXT("requested_overrides"), ExpectedOverrides.ToSharedRef());
	Result->SetObjectField(TEXT("effective_bone_weights"), EffectiveWeights);
	Result->SetNumberField(TEXT("inherited_descendant_weight"), InheritedDescendantWeight);
	Result->SetBoolField(TEXT("mask_section_assignment"), true);
	Result->SetBoolField(TEXT("evaluation_task_factory"), true);
	return SerializeJson(Result);
#else
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetBoolField(TEXT("editor_required"), true);
	Obj->SetStringField(TEXT("error"), TEXT("AnimMixerReloadEvaluateBlendMask requires an editor build"));
	Obj->SetStringField(TEXT("error_code"), TEXT("editor_required"));
	return SerializeJson(Obj);
#endif // WITH_EDITOR
#else
	return MakeUnsupported(TEXT("AnimMixerReloadEvaluateBlendMask"));
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED
}

// NOTE (Issue #16112): the Phase 3c outliner-expansion ops are intentionally NOT
// implemented here. The SequencerCore outliner/viewmodel scripting surface
// (USequencerOutlinerScriptingObject, USequencerViewModelStructExtensions) is
// reflection-only — UCLASS(MinimalAPI) with no *_API export on the methods, so it is
// Blueprint/Python-callable but NOT C++-linkable cross-module (verified vs UE 5.8
// engine source). Those ops are driven from Rust via Python Remote Execution instead.

// ===== Phase 5c (#25957) — Control Rig correction layer =====

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_CONTROLRIG
namespace
{
	/// One resolved Control Rig track on a binding: the track, its parameter
	/// section and the rig instance.
	struct FCUControlRigTrackInfo
	{
		UMovieSceneControlRigParameterTrack* Track = nullptr;
		UMovieSceneControlRigParameterSection* Section = nullptr;
		UControlRig* Rig = nullptr;
		UMovieSceneAnimationMixerTrack* ParentMixer = nullptr;
		int32 LayerIndex = INDEX_NONE;
	};

	/// Collect both top-level and AnimMixer-child Control Rig tracks on a binding.
	/// Child tracks remain owned by the object binding; their parent mixer/layer is
	/// recovered from the mixer's exported child-track map.
	void CollectControlRigTracks(
		UMovieScene* MovieScene, const FGuid& BindingGuid, TArray<FCUControlRigTrackInfo>& Out)
	{
		if (!MovieScene || !BindingGuid.IsValid())
		{
			return;
		}
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
		if (!Binding)
		{
			return;
		}
		TArray<UMovieSceneAnimationMixerTrack*> Mixers;
		for (UMovieSceneTrack* AnyTrack : Binding->GetTracks())
		{
			if (UMovieSceneAnimationMixerTrack* Mixer = Cast<UMovieSceneAnimationMixerTrack>(AnyTrack))
			{
				Mixers.Add(Mixer);
			}
		}
		for (UMovieSceneTrack* AnyTrack : Binding->GetTracks())
		{
			UMovieSceneControlRigParameterTrack* CRTrack = Cast<UMovieSceneControlRigParameterTrack>(AnyTrack);
			if (!CRTrack)
			{
				continue;
			}
			FCUControlRigTrackInfo Info;
			Info.Track = CRTrack;
			Info.Rig = CRTrack->GetControlRig();
			for (UMovieSceneAnimationMixerTrack* Mixer : Mixers)
			{
				if (Mixer && Mixer->IsChildTrack(CRTrack))
				{
					Info.ParentMixer = Mixer;
					Info.LayerIndex = Mixer->GetChildTrackRow(CRTrack);
					break;
				}
			}
			for (UMovieSceneSection* Section : CRTrack->GetAllSections())
			{
				if (UMovieSceneControlRigParameterSection* Param =
					Cast<UMovieSceneControlRigParameterSection>(Section))
				{
					Info.Section = Param;
					break;
				}
			}
			Out.Add(Info);
		}
	}

	/// Stable string for an ERigControlType. Mirrors the engine enum names so the
	/// AI-facing vocabulary matches Control Rig documentation.
	const TCHAR* RigControlTypeToString(ERigControlType Type)
	{
		switch (Type)
		{
		case ERigControlType::Bool:             return TEXT("Bool");
		case ERigControlType::Float:            return TEXT("Float");
		case ERigControlType::ScaleFloat:       return TEXT("ScaleFloat");
		case ERigControlType::Integer:          return TEXT("Integer");
		case ERigControlType::Vector2D:         return TEXT("Vector2D");
		case ERigControlType::Position:         return TEXT("Position");
		case ERigControlType::Scale:            return TEXT("Scale");
		case ERigControlType::Rotator:          return TEXT("Rotator");
		case ERigControlType::Transform:        return TEXT("Transform");
		case ERigControlType::TransformNoScale: return TEXT("TransformNoScale");
		case ERigControlType::EulerTransform:   return TEXT("EulerTransform");
		default:                                return TEXT("Unknown");
		}
	}

	/// Evaluate a float curve at a frame; returns false when the curve has no data.
	bool EvalCurve(const FMovieSceneFloatChannel& Curve, FFrameTime Time, float& Out)
	{
		return Curve.Evaluate(Time, Out);
	}

	/// Numeric-aware deep comparison of two JSON values.
	///
	/// The round-trip check MUST compare VALUES, not merely "something came back":
	/// the engine's parameter writers are void, and a mismatched input shape writes
	/// identity silently (live-verified #25957 — a rotator posted to an
	/// EulerTransform control produced keys whose yaw was 0 while a presence-only
	/// check reported success).
	bool JsonNearlyEqual(const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B, double Tolerance)
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return false;
		}
		if (A->Type != B->Type)
		{
			return false;
		}
		switch (A->Type)
		{
		case EJson::Number:
			return FMath::Abs(A->AsNumber() - B->AsNumber()) <= Tolerance;
		case EJson::Boolean:
			return A->AsBool() == B->AsBool();
		case EJson::String:
			return A->AsString() == B->AsString();
		case EJson::Array:
		{
			const TArray<TSharedPtr<FJsonValue>>& ArrA = A->AsArray();
			const TArray<TSharedPtr<FJsonValue>>& ArrB = B->AsArray();
			if (ArrA.Num() != ArrB.Num())
			{
				return false;
			}
			for (int32 i = 0; i < ArrA.Num(); ++i)
			{
				if (!JsonNearlyEqual(ArrA[i], ArrB[i], Tolerance))
				{
					return false;
				}
			}
			return true;
		}
		case EJson::Object:
		{
			const TSharedPtr<FJsonObject> ObjA = A->AsObject();
			const TSharedPtr<FJsonObject> ObjB = B->AsObject();
			if (!ObjA.IsValid() || !ObjB.IsValid() || ObjA->Values.Num() != ObjB->Values.Num())
			{
				return false;
			}
			for (const auto& Pair : ObjA->Values)
			{
				// TryGetField rather than Values.Find — FJsonObject's map uses
				// case-insensitive key funcs, so Find(FString) has no viable overload.
				const TSharedPtr<FJsonValue> Other = ObjB->TryGetField(FString(Pair.Key));
				if (!Other.IsValid() || !JsonNearlyEqual(Pair.Value, Other, Tolerance))
				{
					return false;
				}
			}
			return true;
		}
		default:
			return true; // Null == Null
		}
	}

	/// Read a control's current value off the section's parameter curves.
	/// Returns null when the control has no curve of its type yet (unkeyed).
	TSharedPtr<FJsonValue> ReadControlValue(
		UMovieSceneControlRigParameterSection* Section, FName ControlName,
		ERigControlType Type, FFrameTime Time, bool& bOutKeyed)
	{
		bOutKeyed = false;
		if (!Section)
		{
			return nullptr;
		}

		auto MakeNum = [](float V) { return MakeShared<FJsonValueNumber>(static_cast<double>(V)); };
		auto MakeArr = [](std::initializer_list<float> Vals)
			{
				TArray<TSharedPtr<FJsonValue>> Arr;
				for (float V : Vals)
				{
					Arr.Add(MakeShared<FJsonValueNumber>(static_cast<double>(V)));
				}
				return MakeShared<FJsonValueArray>(Arr);
			};

		switch (Type)
		{
		case ERigControlType::Float:
		case ERigControlType::ScaleFloat:
		{
			for (const FScalarParameterNameAndCurve& P : Section->GetScalarParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				bOutKeyed = P.ParameterCurve.GetNumKeys() > 0;
				float V = 0.0f;
				EvalCurve(P.ParameterCurve, Time, V);
				return MakeNum(V);
			}
			return nullptr;
		}
		case ERigControlType::Integer:
		{
			// Integer (and enum-backed Integer) consume integer/byte channels.
			// Reading the scalar channel we used to write is tautological: 3.0f
			// round-trips as 3.0 while Control Rig Get<int32>() type-puns it to
			// 1077936128 (Issue #27729). Walk integer then enum, matching
			// UMovieSceneControlRigParameterSection::ForEachParameter.
			for (const FIntegerParameterNameAndCurve& P : Section->GetIntegerParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				bOutKeyed = P.ParameterCurve.GetNumKeys() > 0;
				int32 V = 0;
				P.ParameterCurve.Evaluate(Time, V);
				return MakeShared<FJsonValueNumber>(static_cast<double>(V));
			}
			for (const FEnumParameterNameAndCurve& P : Section->GetEnumParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				bOutKeyed = P.ParameterCurve.GetNumKeys() > 0;
				uint8 V = 0;
				P.ParameterCurve.Evaluate(Time, V);
				return MakeShared<FJsonValueNumber>(static_cast<double>(V));
			}
			return nullptr;
		}
		case ERigControlType::Bool:
		{
			for (const FBoolParameterNameAndCurve& P : Section->GetBoolParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				bOutKeyed = P.ParameterCurve.GetNumKeys() > 0;
				bool V = false;
				P.ParameterCurve.Evaluate(Time, V);
				return MakeShared<FJsonValueBoolean>(V);
			}
			return nullptr;
		}
		case ERigControlType::Vector2D:
		{
			for (const FVector2DParameterNameAndCurves& P : Section->GetVector2DParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				bOutKeyed = P.XCurve.GetNumKeys() > 0 || P.YCurve.GetNumKeys() > 0;
				float X = 0.f, Y = 0.f;
				EvalCurve(P.XCurve, Time, X);
				EvalCurve(P.YCurve, Time, Y);
				return MakeArr({ X, Y });
			}
			return nullptr;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
		{
			for (const FVectorParameterNameAndCurves& P : Section->GetVectorParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				bOutKeyed = P.XCurve.GetNumKeys() > 0 || P.YCurve.GetNumKeys() > 0 || P.ZCurve.GetNumKeys() > 0;
				float X = 0.f, Y = 0.f, Z = 0.f;
				EvalCurve(P.XCurve, Time, X);
				EvalCurve(P.YCurve, Time, Y);
				EvalCurve(P.ZCurve, Time, Z);
				return MakeArr({ X, Y, Z });
			}
			return nullptr;
		}
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			for (const FTransformParameterNameAndCurves& P : Section->GetTransformParameterNamesAndCurves())
			{
				if (P.ParameterName != ControlName) { continue; }
				float T[3] = { 0.f, 0.f, 0.f }, Rot[3] = { 0.f, 0.f, 0.f }, S[3] = { 1.f, 1.f, 1.f };
				for (int32 i = 0; i < 3; ++i)
				{
					bOutKeyed |= P.Translation[i].GetNumKeys() > 0 || P.Rotation[i].GetNumKeys() > 0
						|| P.Scale[i].GetNumKeys() > 0;
					EvalCurve(P.Translation[i], Time, T[i]);
					EvalCurve(P.Rotation[i], Time, Rot[i]);
					EvalCurve(P.Scale[i], Time, S[i]);
				}
				TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetArrayField(TEXT("location"), {
					MakeShared<FJsonValueNumber>(T[0]), MakeShared<FJsonValueNumber>(T[1]),
					MakeShared<FJsonValueNumber>(T[2]) });
				TSharedRef<FJsonObject> RotObj = MakeShared<FJsonObject>();
				RotObj->SetNumberField(TEXT("roll"), Rot[0]);
				RotObj->SetNumberField(TEXT("pitch"), Rot[1]);
				RotObj->SetNumberField(TEXT("yaw"), Rot[2]);
				Obj->SetObjectField(TEXT("rotation"), RotObj);
				Obj->SetArrayField(TEXT("scale"), {
					MakeShared<FJsonValueNumber>(S[0]), MakeShared<FJsonValueNumber>(S[1]),
					MakeShared<FJsonValueNumber>(S[2]) });
				return MakeShared<FJsonValueObject>(Obj);
			}
			return nullptr;
		}
		default:
			return nullptr;
		}
	}
} // namespace
#endif // NARSHAMCP_ANIMMIXER_SUPPORTED

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerAddControlRigLayer(
	const FString& SequencePath, const FString& BindingId,
	const FString& ControlRigClassPath, bool bAdditive,
	const FString& TrackId, int32 LayerIndex)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log,
		TEXT("[AnimMixer] AddControlRigLayer seq=%s binding=%s class=%s additive=%d track=%s layer=%d"),
		*SequencePath, *BindingId, *ControlRigClassPath, bAdditive ? 1 : 0, *TrackId, LayerIndex);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_CONTROLRIG
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load LevelSequence: %s"), *SequencePath));
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return MakeErrorJson(TEXT("LevelSequence has no MovieScene"));
	}
	TArray<FString> ResolveTrail;
	auto AddResolveTrail = [&](const TSharedRef<FJsonObject>& Obj)
		{
			TArray<TSharedPtr<FJsonValue>> TrailArr;
			for (const FString& Entry : ResolveTrail)
			{
				TrailArr.Add(MakeShared<FJsonValueString>(Entry));
			}
			Obj->SetArrayField(TEXT("resolve_trail"), TrailArr);
		};
	auto BindingResolveError = [&](const FString& Message, const TCHAR* Code) -> FString
		{
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"), Message);
			Err->SetStringField(TEXT("error_code"), Code);
			AddResolveTrail(Err);
			return SerializeJson(Err);
		};

	// --- Resolve the binding that owns the skeletal mesh.
	FGuid BindingGuid;
	if (!BindingId.IsEmpty())
	{
		if (!FGuid::Parse(BindingId, BindingGuid))
		{
			return MakeErrorJson(FString::Printf(TEXT("BindingId is not a GUID: %s"), *BindingId),
				TEXT("binding_parse_failed"));
		}
		if (!MovieScene->FindBinding(BindingGuid))
		{
			ResolveTrail.Add(FString::Printf(
				TEXT("%s -> binding not present in MovieScene"), *BindingGuid.ToString()));
			return BindingResolveError(
				FString::Printf(TEXT("No binding %s in this LevelSequence"), *BindingId),
				TEXT("binding_not_found"));
		}
	}

	UObject* PlaybackContext = nullptr;
#if WITH_EDITOR
	if (GEditor)
	{
		PlaybackContext = GEditor->GetEditorWorldContext().World();
	}
#endif
	if (!PlaybackContext)
	{
		return MakeErrorJson(TEXT("No editor world available to resolve the binding"),
			TEXT("no_playback_context"));
	}

	USkeletalMeshComponent* SkelMeshComp = nullptr;
	TArray<FString> Candidates;
	FString ResolutionPath;
	auto ResolveSkeletalFromBinding =
		[&](const FGuid& Guid, FString& OutResolutionPath) -> USkeletalMeshComponent*
		{
			TArray<UObject*, TInlineAllocator<1>> Bound;
			// The (Guid, UObject*, ...) overload is deprecated since 5.4 and the
			// (Guid, FResolveParams, ...) one since 5.5; only the form below survives.
			// Its SharedPlaybackState is optional (TSharedPtr, not TSharedRef) and the
			// 5.8.1 base implementation discards it — MovieSceneSequence.cpp:70-88 has
			// the deprecated form forward with nullptr, then resolves through the
			// locator-only FMovieSceneBindingReferences::ResolveBinding either way — so
			// passing nullptr is identical to the old call and merely silences C4996.
			// Resolving spawnables / custom bindings is a different API entirely
			// (MovieSceneHelpers::GetBoundObjects) and additionally needs a playback
			// state carrying an FMovieSceneSpawnRegister, which a transient one lacks.
			UE::UniversalObjectLocator::FResolveParams ResolveParams(PlaybackContext);
			Sequence->LocateBoundObjects(Guid, ResolveParams, nullptr, Bound);
			ResolveTrail.Add(FString::Printf(TEXT("%s -> %d object(s)"), *Guid.ToString(), Bound.Num()));
			for (UObject* Obj : Bound)
			{
				ResolveTrail.Add(FString::Printf(TEXT("  obj=%s class=%s"),
					Obj ? *Obj->GetName() : TEXT("null"),
					Obj ? *Obj->GetClass()->GetName() : TEXT("null")));
				if (USkeletalMeshComponent* AsComp = Cast<USkeletalMeshComponent>(Obj))
				{
					OutResolutionPath = TEXT("direct_component");
					return AsComp;
				}
				if (AActor* AsActor = Cast<AActor>(Obj))
				{
					if (USkeletalMeshComponent* Found = AsActor->FindComponentByClass<USkeletalMeshComponent>())
					{
						OutResolutionPath = TEXT("actor_component");
						return Found;
					}
					ResolveTrail.Add(TEXT("  (actor has no USkeletalMeshComponent)"));
				}
			}
			return nullptr;
		};

	if (BindingGuid.IsValid())
	{
		SkelMeshComp = ResolveSkeletalFromBinding(BindingGuid, ResolutionPath);
		if (!SkelMeshComp)
		{
			// The requested binding may be the ACTOR binding whose skeletal mesh lives
			// on a child component binding - walk possessable children before failing.
			for (int32 Idx = 0; Idx < MovieScene->GetPossessableCount() && !SkelMeshComp; ++Idx)
			{
				const FMovieScenePossessable& Poss = MovieScene->GetPossessable(Idx);
				if (Poss.GetParent() == BindingGuid)
				{
					FString ChildResolutionPath;
					SkelMeshComp = ResolveSkeletalFromBinding(Poss.GetGuid(), ChildResolutionPath);
					if (SkelMeshComp)
					{
						BindingGuid = Poss.GetGuid();
						ResolutionPath = TEXT("child_component_fallback");
					}
				}
			}
		}
		if (!SkelMeshComp)
		{
			return BindingResolveError(
				FString::Printf(TEXT("Binding %s does not resolve to a SkeletalMeshComponent"), *BindingId),
				TEXT("binding_not_skeletal"));
		}
	}
	else
	{
		const UMovieScene* ConstMS = MovieScene;  // const overload only (5.7 deprecation)
		for (const FMovieSceneBinding& Binding : ConstMS->GetBindings())
		{
			FString CandidateResolutionPath;
			if (USkeletalMeshComponent* Found = ResolveSkeletalFromBinding(
					Binding.GetObjectGuid(), CandidateResolutionPath))
			{
				Candidates.Add(Binding.GetObjectGuid().ToString());
				if (!SkelMeshComp)
				{
					SkelMeshComp = Found;
					BindingGuid = Binding.GetObjectGuid();
				}
			}
		}
		if (Candidates.Num() != 1)
		{
			if (Candidates.IsEmpty())
			{
				ResolveTrail.Add(TEXT("candidate scan resolved 0 skeletal bindings"));
			}
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"),
				FString::Printf(TEXT("BindingId omitted and %d skeletal bindings found; pass one explicitly"),
					Candidates.Num()));
			Err->SetStringField(TEXT("error_code"), TEXT("binding_ambiguous"));
			TArray<TSharedPtr<FJsonValue>> CandArr;
			for (const FString& C : Candidates)
			{
				CandArr.Add(MakeShared<FJsonValueString>(C));
			}
			Err->SetArrayField(TEXT("candidates"), CandArr);
			AddResolveTrail(Err);
			return SerializeJson(Err);
		}
		ResolutionPath = TEXT("auto_single_binding");
	}

	// --- Resolve optional mixer-child placement. INDEX_NONE preserves the shipped
	// top-level contract; non-negative indices opt into UE 5.8's MixedControlRig
	// evaluation bridge.
	UMovieSceneAnimationMixerTrack* TargetMixer = nullptr;
	UMovieSceneAnimationMixerLayer* TargetLayer = nullptr;
	if (LayerIndex < INDEX_NONE)
	{
		return MakeErrorJson(TEXT("LayerIndex must be -1 (top-level) or >= 0"),
			TEXT("invalid_layer_index"));
	}
	if (LayerIndex >= 0)
	{
		// The child track itself is authored by MovieSceneAnimMixer, but it only
		// contributes evaluation tasks when the optional MixedControlRig bridge is
		// present and loaded.  Refuse before touching the sequence rather than
		// returning success for a structurally valid but inert correction layer.
		const FName MixedControlRigModule(TEXT("MovieSceneMixedControlRig"));
		const TSharedPtr<IPlugin> MixedControlRigPlugin =
			IPluginManager::Get().FindPlugin(TEXT("MovieSceneMixedControlRig"));
		const bool bPluginEnabled = MixedControlRigPlugin.IsValid()
			&& MixedControlRigPlugin->IsEnabled();
		const bool bModuleExists =
			FModuleManager::Get().ModuleExists(TEXT("MovieSceneMixedControlRig"));
		const bool bModuleLoaded = bPluginEnabled && bModuleExists
			&& (FModuleManager::Get().IsModuleLoaded(MixedControlRigModule)
				|| FModuleManager::Get().LoadModule(MixedControlRigModule) != nullptr);
		const TCHAR* AvailabilityErrorCode = MixedControlRigAvailabilityErrorCode(
			MixedControlRigPlugin.IsValid(), bPluginEnabled, bModuleExists, bModuleLoaded);
		if (AvailabilityErrorCode)
		{
			return MakeErrorJson(
				TEXT("Mixer-child Control Rig placement requires the enabled MovieSceneMixedControlRig plugin"),
				AvailabilityErrorCode);
		}

		FString MixerError;
		TargetMixer = ResolveAnimMixerTrack(MovieScene, TrackId, MixerError);
		if (!TargetMixer)
		{
			return MakeErrorJson(MixerError, TEXT("mixer_track_not_found"));
		}

		FGuid MixerBindingGuid;
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			for (UMovieSceneTrack* BindingTrack : Binding.GetTracks())
			{
				if (BindingTrack == TargetMixer)
				{
					MixerBindingGuid = Binding.GetObjectGuid();
					break;
				}
			}
			if (MixerBindingGuid.IsValid())
			{
				break;
			}
		}
		if (!MixerBindingGuid.IsValid())
		{
			return MakeErrorJson(
				TEXT("Mixer-child Control Rig placement requires a binding-owned AnimMixer; root mixers do not evaluate a skeletal target"),
				TEXT("mixer_track_root_scoped"));
		}
		if (MixerBindingGuid != BindingGuid)
		{
			return MakeErrorJson(
				FString::Printf(
					TEXT("Selected AnimMixer belongs to binding %s, but Control Rig resolved to %s"),
					*MixerBindingGuid.ToString(), *BindingGuid.ToString()),
				TEXT("mixer_binding_mismatch"));
		}

		TargetLayer = TargetMixer->GetLayer(LayerIndex);
		if (!TargetLayer)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("No AnimMixer layer at index %d (track has %d layers)"),
					LayerIndex, TargetMixer->GetLayers().Num()),
				TEXT("mixer_layer_not_found"));
		}
	}

	// --- Resolve the Control Rig class (default: FKControlRig).
	UClass* RigClass = UFKControlRig::StaticClass();
	if (!ControlRigClassPath.IsEmpty())
	{
		UClass* Loaded = LoadClass<UControlRig>(nullptr, *ControlRigClassPath);
		if (!Loaded)
		{
			// Blueprint-generated rigs resolve via the _C suffix.
			Loaded = LoadClass<UControlRig>(nullptr, *(ControlRigClassPath + TEXT("_C")));
		}
		if (!Loaded)
		{
			return MakeErrorJson(
				FString::Printf(TEXT("Failed to load Control Rig class: %s"), *ControlRigClassPath),
				TEXT("rig_class_load_failed"));
		}
		RigClass = Loaded;
	}

	// --- Idempotent reuse: a track already driving this rig class is not duplicated.
	//
	// The reuse key is (placement, rig class, additive mode) — NOT the class alone. Additive and
	// Replace are opposite contracts: an additive rig layers a delta on top of the
	// existing motion, a Replace rig overwrites the pose outright. Reusing a Replace
	// rig for an bAdditive=true request would return success:true while the caller's
	// later keys DESTROY the base animation instead of correcting it — the exact
	// failure this op exists to prevent, reported as a success.
	//
	// A mismatch is refused rather than silently flipped: the existing rig's keys were
	// authored under its current mode, so switching it re-interprets every one of them.
	// The caller is told which mode is already there and can reuse it, remove it, or
	// ask for a distinct rig class.
	{
		TArray<FCUControlRigTrackInfo> Existing;
		CollectControlRigTracks(MovieScene, BindingGuid, Existing);
		for (const FCUControlRigTrackInfo& Info : Existing)
		{
			const bool bPlacementMatches = LayerIndex >= 0
				? Info.ParentMixer == TargetMixer && Info.LayerIndex == LayerIndex
				: Info.ParentMixer == nullptr;
			if (!bPlacementMatches)
			{
				continue;
			}
			if (!Info.Rig || Info.Rig->GetClass() != RigClass)
			{
				continue;
			}
			if (Info.Rig->IsAdditive() != bAdditive)
			{
				return MakeErrorJson(
					FString::Printf(
						TEXT("Binding already has a %s track whose additive mode is %s, but %s was requested. ")
						TEXT("Reuse it as-is, remove it first, or request a different control_rig_class_path."),
						*RigClass->GetName(),
						Info.Rig->IsAdditive() ? TEXT("true") : TEXT("false"),
						bAdditive ? TEXT("true") : TEXT("false")),
					TEXT("additive_mode_mismatch"));
			}
			TArray<FRigControlElement*> ExistingControls;
			Info.Rig->GetControlsInOrder(ExistingControls);
			TSharedRef<FJsonObject> ReuseObj = MakeShared<FJsonObject>();
			ReuseObj->SetBoolField(TEXT("success"), true);
			ReuseObj->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
			ReuseObj->SetStringField(TEXT("control_rig_class"), RigClass->GetName());
			ReuseObj->SetStringField(TEXT("control_rig_id"), Info.Rig->GetPathName());
			ReuseObj->SetStringField(TEXT("track_name"), Info.Track->GetName());
			ReuseObj->SetBoolField(TEXT("additive"), Info.Rig->IsAdditive());
			ReuseObj->SetNumberField(TEXT("control_count"), ExistingControls.Num());
			ReuseObj->SetBoolField(TEXT("reused_track"), true);
			ReuseObj->SetStringField(TEXT("placement"),
				LayerIndex >= 0 ? TEXT("mixer_child") : TEXT("top_level"));
			ReuseObj->SetNumberField(TEXT("layer_index"), LayerIndex);
			ReuseObj->SetStringField(TEXT("resolved_component"), SkelMeshComp->GetPathName());
			ReuseObj->SetStringField(TEXT("resolution_path"), ResolutionPath);
			AddResolveTrail(ReuseObj);
			return TargetMixer
				? SerializeAnimMixerSuccess(ReuseObj, Sequence, TargetMixer)
				: SerializeJson(ReuseObj);
		}
	}

	if (TargetLayer && !TargetLayer->IsEmpty())
	{
		return MakeErrorJson(
			FString::Printf(
				TEXT("AnimMixer layer %d is occupied (pose_sections=%d, child_track=%s); refusing destructive replacement"),
				LayerIndex, TargetLayer->GetNumSections(),
				TargetLayer->GetChildTrack() ? *TargetLayer->GetChildTrack()->GetName() : TEXT("none")),
			TEXT("mixer_layer_occupied"));
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Add Control Rig Layer")));
#endif

	Sequence->Modify();
	MovieScene->Modify();
	if (TargetMixer)
	{
		TargetMixer->Modify();
		TargetLayer->Modify();
	}

	UMovieSceneTrack* NewTrack = TargetMixer
		? TargetMixer->AddChildTrack(
			BindingGuid, UMovieSceneControlRigParameterTrack::StaticClass(), LayerIndex)
		: MovieScene->AddTrack(UMovieSceneControlRigParameterTrack::StaticClass(), BindingGuid);
	UMovieSceneControlRigParameterTrack* CRTrack = Cast<UMovieSceneControlRigParameterTrack>(NewTrack);

	// Any failure from here on leaves a rig-less Control Rig track, which makes the
	// whole LevelSequence fail to open - so every early-out must roll back.
	auto Rollback = [&](const FString& Message, const TCHAR* Code, UControlRig* OrphanRig) -> FString
		{
			if (OrphanRig)
			{
				OrphanRig->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
			}
			if (NewTrack)
			{
				if (TargetMixer && TargetMixer->IsChildTrack(NewTrack))
				{
					TargetMixer->RemoveChildTrack(NewTrack);
				}
				else
				{
					MovieScene->RemoveTrack(*NewTrack);
				}
			}
#if WITH_EDITOR
			Transaction.Cancel();
#endif
			return MakeErrorJson(Message, Code);
		};

	if (!CRTrack)
	{
		return Rollback(TEXT("Track creation did not return a MovieSceneControlRigParameterTrack"),
			TEXT("track_create_failed"), nullptr);
	}

	// --- Mirror the engine's rig setup order (ControlRigParameterTrackEditor.cpp:2298-2440),
	// with the one addition the engine's scripted path omits: BindToObject BEFORE
	// Initialize, without which UFKControlRig builds zero controls.
	FString ObjectName = RigClass->GetName();
	ObjectName.RemoveFromEnd(TEXT("_C"));

	UControlRig* Rig = NewObject<UControlRig>(CRTrack, RigClass, FName(*ObjectName), RF_Transactional);
	if (!Rig)
	{
		return Rollback(TEXT("Failed to construct the Control Rig instance"),
			TEXT("rig_construct_failed"), nullptr);
	}
	Rig->Modify();

	if (UFKControlRig* FKRig = Cast<UFKControlRig>(Rig))
	{
		FKRig->SetApplyMode(bAdditive ? EControlRigFKRigExecuteMode::Additive
			: EControlRigFKRigExecuteMode::Replace);
	}
	else
	{
		Rig->SetIsAdditive(bAdditive);
	}

	TSharedRef<FControlRigObjectBinding> ObjectBinding = MakeShared<FControlRigObjectBinding>();
	Rig->SetObjectBinding(ObjectBinding);
	ObjectBinding->BindToObject(SkelMeshComp);
	Rig->Initialize();
	Rig->Evaluate_AnyThread();

	// --- Assert the rig actually produced controls; a silent 0-control rig would
	// satisfy every other check while making the feature useless.
	TArray<FRigControlElement*> Controls;
	Rig->GetControlsInOrder(Controls);
	if (Controls.Num() == 0)
	{
		return Rollback(
			FString::Printf(TEXT("Control Rig %s initialized with 0 controls (binding did not resolve to a usable skeleton)"),
				*ObjectName),
			TEXT("rig_no_controls"), Rig);
	}

	CRTrack->Modify();
	const FFrameNumber StartFrame = MovieScene->GetPlaybackRange().HasLowerBound()
		? MovieScene->GetPlaybackRange().GetLowerBoundValue()
		: FFrameNumber(0);
	UMovieSceneSection* NewSection = CRTrack->CreateControlRigSection(StartFrame, Rig, /*bInOwnsControlRig=*/true);
	if (!NewSection)
	{
		return Rollback(TEXT("CreateControlRigSection returned null"),
			TEXT("section_create_failed"), Rig);
	}
	NewSection->Modify();
	CRTrack->SetTrackName(FName(*ObjectName));
#if WITH_EDITOR
	CRTrack->SetDisplayName(FText::FromString(
		bAdditive ? ObjectName + TEXT(" (Layered)") : ObjectName));
#endif

	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("binding_id"), BindingGuid.ToString());
	Obj->SetStringField(TEXT("control_rig_class"), RigClass->GetName());
	Obj->SetStringField(TEXT("control_rig_id"), Rig->GetPathName());
	Obj->SetStringField(TEXT("track_name"), CRTrack->GetName());
	Obj->SetBoolField(TEXT("additive"), Rig->IsAdditive());
	Obj->SetNumberField(TEXT("control_count"), Controls.Num());
	Obj->SetBoolField(TEXT("reused_track"), false);
	Obj->SetStringField(TEXT("placement"),
		TargetMixer ? TEXT("mixer_child") : TEXT("top_level"));
	Obj->SetNumberField(TEXT("layer_index"), LayerIndex);
	Obj->SetStringField(TEXT("resolved_component"), SkelMeshComp->GetPathName());
	Obj->SetStringField(TEXT("resolution_path"), ResolutionPath);
	AddResolveTrail(Obj);
	return TargetMixer
		? SerializeAnimMixerSuccess(Obj, Sequence, TargetMixer)
		: SerializeJson(Obj);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("ControlRig"),
		TEXT("control_rig_plugin_unavailable"), TEXT("Control Rig correction layers"));
	#else
	return MakeUnsupported(TEXT("AnimMixerAddControlRigLayer"));
	#endif
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerGetControls(
	const FString& SequencePath, const FString& BindingId, int32 Frame)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] GetControls seq=%s binding=%s frame=%d"),
		*SequencePath, *BindingId, Frame);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_CONTROLRIG
	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load LevelSequence: %s"), *SequencePath));
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return MakeErrorJson(TEXT("LevelSequence has no MovieScene"));
	}

	// Resolve the binding: explicit GUID, or the single binding that owns a
	// Control Rig track when omitted.
	FGuid BindingGuid;
	if (!BindingId.IsEmpty())
	{
		if (!FGuid::Parse(BindingId, BindingGuid))
		{
			return MakeErrorJson(FString::Printf(TEXT("BindingId is not a GUID: %s"), *BindingId),
				TEXT("binding_parse_failed"));
		}
	}

	TArray<FCUControlRigTrackInfo> Rigs;
	if (BindingGuid.IsValid())
	{
		CollectControlRigTracks(MovieScene, BindingGuid, Rigs);
	}
	else
	{
		TArray<FString> Owners;
		const UMovieScene* ConstMS = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMS->GetBindings())
		{
			TArray<FCUControlRigTrackInfo> Found;
			CollectControlRigTracks(MovieScene, Binding.GetObjectGuid(), Found);
			if (Found.Num() > 0)
			{
				Owners.Add(Binding.GetObjectGuid().ToString());
				if (Rigs.Num() == 0)
				{
					Rigs = Found;
					BindingGuid = Binding.GetObjectGuid();
				}
			}
		}
		if (Owners.Num() > 1)
		{
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"),
				FString::Printf(TEXT("BindingId omitted and %d bindings own a Control Rig track; pass one explicitly"),
					Owners.Num()));
			Err->SetStringField(TEXT("error_code"), TEXT("binding_ambiguous"));
			TArray<TSharedPtr<FJsonValue>> CandArr;
			for (const FString& C : Owners)
			{
				CandArr.Add(MakeShared<FJsonValueString>(C));
			}
			Err->SetArrayField(TEXT("candidates"), CandArr);
			return SerializeJson(Err);
		}
	}
	if (Rigs.Num() == 0)
	{
		return MakeErrorJson(TEXT("No Control Rig track on this binding"),
			TEXT("no_control_rig_track"));
	}

	// Frames arrive in display rate (what the caller sees in Sequencer); the curves
	// are stored in tick resolution.
	const FFrameRate TickRes = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const FFrameTime SampleTime = FFrameRate::TransformTime(FFrameTime(FFrameNumber(Frame)), DisplayRate, TickRes);

	TArray<TSharedPtr<FJsonValue>> RigArr;
	for (const FCUControlRigTrackInfo& Child : Rigs)
	{
		TSharedRef<FJsonObject> RigObj = MakeShared<FJsonObject>();
		RigObj->SetStringField(TEXT("track_name"), Child.Track ? Child.Track->GetName() : TEXT(""));
		RigObj->SetStringField(TEXT("control_rig_id"), Child.Rig ? Child.Rig->GetPathName() : TEXT(""));
		RigObj->SetStringField(TEXT("control_rig_class"),
			Child.Rig ? Child.Rig->GetClass()->GetName() : TEXT(""));
		RigObj->SetBoolField(TEXT("additive"), Child.Rig ? Child.Rig->IsAdditive() : false);
		RigObj->SetStringField(TEXT("placement"), Child.ParentMixer ? TEXT("mixer_child") : TEXT("top_level"));
		RigObj->SetNumberField(TEXT("layer_index"), Child.ParentMixer ? Child.LayerIndex : INDEX_NONE);
		if (Child.ParentMixer)
		{
			AppendAnimMixerTrackMetadata(RigObj, MovieScene, Child.ParentMixer);
		}
		else
		{
			RigObj->SetStringField(TEXT("scope"), TEXT("binding"));
			RigObj->SetStringField(TEXT("binding_id"), CanonicalBindingGuid(BindingGuid));
			RigObj->SetStringField(TEXT("track_id"),
				Child.Track ? Child.Track->GetFName().ToString() : TEXT(""));
		}

		TArray<TSharedPtr<FJsonValue>> ControlArr;
		if (Child.Rig)
		{
			TArray<FRigControlElement*> Controls;
			Child.Rig->GetControlsInOrder(Controls);
			for (FRigControlElement* Control : Controls)
			{
				if (!Control)
				{
					continue;
				}
				const FName ControlName = Control->GetFName();
				const ERigControlType Type = Control->Settings.ControlType;
				TSharedRef<FJsonObject> CtrlObj = MakeShared<FJsonObject>();
				CtrlObj->SetStringField(TEXT("name"), ControlName.ToString());
				CtrlObj->SetStringField(TEXT("type"), RigControlTypeToString(Type));
				bool bKeyed = false;
				TSharedPtr<FJsonValue> Value =
					ReadControlValue(Child.Section, ControlName, Type, SampleTime, bKeyed);
				CtrlObj->SetBoolField(TEXT("keyed"), bKeyed);
				if (Value.IsValid())
				{
					CtrlObj->SetField(TEXT("value"), Value);
				}
				ControlArr.Add(MakeShared<FJsonValueObject>(CtrlObj));
			}
		}
		RigObj->SetNumberField(TEXT("control_count"), ControlArr.Num());
		RigObj->SetArrayField(TEXT("controls"), ControlArr);
		RigArr.Add(MakeShared<FJsonValueObject>(RigObj));
	}

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("binding_id"), CanonicalBindingGuid(BindingGuid));
	Obj->SetNumberField(TEXT("frame"), Frame);
	Obj->SetArrayField(TEXT("control_rigs"), RigArr);
	return SerializeJson(Obj);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("ControlRig"),
		TEXT("control_rig_plugin_unavailable"), TEXT("Control Rig inspection"));
	#else
	return MakeUnsupported(TEXT("AnimMixerGetControls"));
	#endif
#endif
}

FString UCURemoteSequencerAnimMixerLibrary::AnimMixerSetControlKeys(
	const FString& SequencePath, const FString& BindingId,
	const FString& ControlName, const FString& KeysJson)
{
	UE_LOG(LogNarshaMCPAnimMixer, Log, TEXT("[AnimMixer] SetControlKeys seq=%s binding=%s control=%s"),
		*SequencePath, *BindingId, *ControlName);

#if NARSHAMCP_ANIMMIXER_SUPPORTED && WITH_NARSHAMCP_CONTROLRIG
	if (ControlName.IsEmpty())
	{
		return MakeErrorJson(TEXT("ControlName is required"), TEXT("missing_control_name"));
	}

	ULevelSequence* Sequence = LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return MakeErrorJson(FString::Printf(TEXT("Failed to load LevelSequence: %s"), *SequencePath));
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return MakeErrorJson(TEXT("LevelSequence has no MovieScene"));
	}

	// Resolve the binding: explicit GUID, or the single binding that owns a
	// Control Rig track when omitted.
	FGuid BindingGuid;
	if (!BindingId.IsEmpty())
	{
		if (!FGuid::Parse(BindingId, BindingGuid))
		{
			return MakeErrorJson(FString::Printf(TEXT("BindingId is not a GUID: %s"), *BindingId),
				TEXT("binding_parse_failed"));
		}
	}

	TArray<FCUControlRigTrackInfo> Rigs;
	if (BindingGuid.IsValid())
	{
		CollectControlRigTracks(MovieScene, BindingGuid, Rigs);
	}
	else
	{
		TArray<FString> Owners;
		const UMovieScene* ConstMS = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMS->GetBindings())
		{
			TArray<FCUControlRigTrackInfo> Found;
			CollectControlRigTracks(MovieScene, Binding.GetObjectGuid(), Found);
			if (Found.Num() > 0)
			{
				Owners.Add(Binding.GetObjectGuid().ToString());
				if (Rigs.Num() == 0)
				{
					Rigs = Found;
					BindingGuid = Binding.GetObjectGuid();
				}
			}
		}
		if (Owners.Num() > 1)
		{
			TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
			Err->SetBoolField(TEXT("success"), false);
			Err->SetStringField(TEXT("error"),
				FString::Printf(TEXT("BindingId omitted and %d bindings own a Control Rig track; pass one explicitly"),
					Owners.Num()));
			Err->SetStringField(TEXT("error_code"), TEXT("binding_ambiguous"));
			TArray<TSharedPtr<FJsonValue>> CandArr;
			for (const FString& C : Owners)
			{
				CandArr.Add(MakeShared<FJsonValueString>(C));
			}
			Err->SetArrayField(TEXT("candidates"), CandArr);
			return SerializeJson(Err);
		}
	}
	if (Rigs.Num() == 0)
	{
		return MakeErrorJson(TEXT("No Control Rig track on this binding"),
			TEXT("no_control_rig_track"));
	}

	// Resolve the control through the RIG, never by trusting the caller's string —
	// Add*ParameterKey happily creates an orphan parameter for an unknown name.
	const FName ControlFName(*ControlName);
	TArray<const FCUControlRigTrackInfo*> MatchingRigs;
	for (const FCUControlRigTrackInfo& Child : Rigs)
	{
		if (!Child.Rig)
		{
			continue;
		}
		if (Child.Rig->FindControl(ControlFName))
		{
			MatchingRigs.Add(&Child);
		}
	}
	if (MatchingRigs.Num() > 1)
	{
		TArray<TSharedPtr<FJsonValue>> Candidates;
		for (const FCUControlRigTrackInfo* Match : MatchingRigs)
		{
			TSharedRef<FJsonObject> Candidate = MakeShared<FJsonObject>();
			Candidate->SetStringField(TEXT("control_rig_id"), Match->Rig->GetPathName());
			Candidate->SetStringField(TEXT("track_name"),
				Match->Track ? Match->Track->GetName() : TEXT(""));
			Candidate->SetStringField(TEXT("placement"),
				Match->ParentMixer ? TEXT("mixer_child") : TEXT("top_level"));
			Candidate->SetNumberField(TEXT("layer_index"), Match->LayerIndex);
			Candidates.Add(MakeShared<FJsonValueObject>(Candidate));
		}
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Control '%s' exists on %d Control Rig tracks; target a unique rig before keying"),
			*ControlName, MatchingRigs.Num()));
		Err->SetStringField(TEXT("error_code"), TEXT("control_rig_ambiguous"));
		Err->SetArrayField(TEXT("candidates"), Candidates);
		return SerializeJson(Err);
	}
	const FCUControlRigTrackInfo* Target = MatchingRigs.Num() == 1 ? MatchingRigs[0] : nullptr;
	FRigControlElement* Control = Target ? Target->Rig->FindControl(ControlFName) : nullptr;
	if (!Target || !Control)
	{
		// Offer near-miss candidates so the caller can correct without guessing.
		TArray<TSharedPtr<FJsonValue>> Candidates;
		const FString Needle = ControlName.ToLower();
		for (const FCUControlRigTrackInfo& Child : Rigs)
		{
			if (!Child.Rig)
			{
				continue;
			}
			TArray<FRigControlElement*> All;
			Child.Rig->GetControlsInOrder(All);
			for (FRigControlElement* C : All)
			{
				if (!C)
				{
					continue;
				}
				const FString Name = C->GetFName().ToString();
				if (Name.ToLower().Contains(Needle) && Candidates.Num() < 20)
				{
					Candidates.Add(MakeShared<FJsonValueString>(Name));
				}
			}
		}
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Control '%s' not found on the Control Rig"), *ControlName));
		Err->SetStringField(TEXT("error_code"), TEXT("control_not_found"));
		Err->SetArrayField(TEXT("candidates"), Candidates);
		return SerializeJson(Err);
	}
	if (!Target->Section)
	{
		return MakeErrorJson(TEXT("Control Rig track has no parameter section"),
			TEXT("no_parameter_section"));
	}

	// --- Parse the key array.
	TArray<TSharedPtr<FJsonValue>> KeyArray;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(KeysJson);
		if (!FJsonSerializer::Deserialize(Reader, KeyArray))
		{
			return MakeErrorJson(TEXT("KeysJson must be a JSON array of {frame, value}"),
				TEXT("keys_json_parse_failed"));
		}
	}
	if (KeyArray.Num() == 0)
	{
		return MakeErrorJson(TEXT("KeysJson is empty"), TEXT("keys_json_empty"));
	}

	const ERigControlType Type = Control->Settings.ControlType;
	const FFrameRate TickRes = MovieScene->GetTickResolution();
	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();

	// Collect+validate everything BEFORE mutating, so a bad entry never leaves a
	// half-written curve behind.
	struct FParsedKey
	{
		int32 DisplayFrame = 0;
		FFrameNumber TickFrame;
		double Scalar = 0.0;
		int32 Integer = 0;
		bool Bool = false;
		double Vec[3] = { 0.0, 0.0, 0.0 };
		FTransform Xform;
	};
	TArray<FParsedKey> Parsed;
	Parsed.Reserve(KeyArray.Num());

	auto ReadVec = [](const TArray<TSharedPtr<FJsonValue>>* Arr, int32 Count, double* Out) -> bool
		{
			if (!Arr || Arr->Num() < Count)
			{
				return false;
			}
			for (int32 i = 0; i < Count; ++i)
			{
				double D = 0.0;
				if (!(*Arr)[i]->TryGetNumber(D))
				{
					return false;
				}
				Out[i] = D;
			}
			return true;
		};

	for (int32 Idx = 0; Idx < KeyArray.Num(); ++Idx)
	{
		const TSharedPtr<FJsonObject>* KeyObj = nullptr;
		if (!KeyArray[Idx]->TryGetObject(KeyObj) || !KeyObj)
		{
			return MakeErrorJson(FString::Printf(TEXT("keys[%d] is not an object"), Idx),
				TEXT("key_not_object"));
		}
		double FrameNum = 0.0;
		if (!(*KeyObj)->TryGetNumberField(TEXT("frame"), FrameNum))
		{
			return MakeErrorJson(FString::Printf(TEXT("keys[%d] missing 'frame'"), Idx),
				TEXT("key_missing_frame"));
		}
		// Rule 27: clamp an externally supplied number before it reaches int32.
		const double ClampedFrame = FMath::Clamp(FrameNum,
			static_cast<double>(MIN_int32), static_cast<double>(MAX_int32));
		FParsedKey Key;
		Key.DisplayFrame = static_cast<int32>(ClampedFrame);
		Key.TickFrame = FFrameRate::TransformTime(
			FFrameTime(FFrameNumber(Key.DisplayFrame)), DisplayRate, TickRes).RoundToFrame();

		const TSharedPtr<FJsonValue> ValueField = (*KeyObj)->TryGetField(TEXT("value"));
		if (!ValueField.IsValid())
		{
			return MakeErrorJson(FString::Printf(TEXT("keys[%d] missing 'value'"), Idx),
				TEXT("key_missing_value"));
		}

		auto TypeError = [&](const TCHAR* Expected)
			{
				return MakeErrorJson(
					FString::Printf(TEXT("keys[%d].value must be %s for control type %s"),
						Idx, Expected, RigControlTypeToString(Type)),
					TEXT("key_value_type_mismatch"));
			};

		switch (Type)
		{
		case ERigControlType::Float:
		case ERigControlType::ScaleFloat:
			if (!ValueField->TryGetNumber(Key.Scalar)) { return TypeError(TEXT("a number")); }
			break;
		case ERigControlType::Integer:
		{
			double Raw = 0.0;
			if (!ValueField->TryGetNumber(Raw) || !FMath::IsFinite(Raw))
			{
				return TypeError(TEXT("an integer"));
			}
			if (Raw < static_cast<double>(MIN_int32) || Raw > static_cast<double>(MAX_int32))
			{
				return MakeErrorJson(
					FString::Printf(TEXT("keys[%d].value %g is outside int32"), Idx, Raw),
					TEXT("key_value_out_of_range"));
			}
			const double Rounded = FMath::RoundToDouble(Raw);
			if (FMath::Abs(Raw - Rounded) > 1.0e-6)
			{
				return TypeError(TEXT("an integer"));
			}
			Key.Integer = static_cast<int32>(Rounded);
			if (Control->Settings.ControlEnum && (Key.Integer < 0 || Key.Integer > 255))
			{
				return MakeErrorJson(
					FString::Printf(TEXT("keys[%d].value %d is outside uint8 for enum-backed Integer"),
						Idx, Key.Integer),
					TEXT("key_value_out_of_range"));
			}
			break;
		}
		case ERigControlType::Bool:
			if (!ValueField->TryGetBool(Key.Bool)) { return TypeError(TEXT("a boolean")); }
			break;
		case ERigControlType::Vector2D:
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (!ValueField->TryGetArray(Arr) || !ReadVec(Arr, 2, Key.Vec)) { return TypeError(TEXT("[x, y]")); }
			break;
		}
		case ERigControlType::Position:
		case ERigControlType::Scale:
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (!ValueField->TryGetArray(Arr) || !ReadVec(Arr, 3, Key.Vec)) { return TypeError(TEXT("[x, y, z]")); }
				break;
			}
		case ERigControlType::Rotator:
		{
			const TSharedPtr<FJsonObject>* RotObj = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (ValueField->TryGetObject(RotObj) && RotObj)
			{
				(*RotObj)->TryGetNumberField(TEXT("roll"), Key.Vec[0]);
				(*RotObj)->TryGetNumberField(TEXT("pitch"), Key.Vec[1]);
				(*RotObj)->TryGetNumberField(TEXT("yaw"), Key.Vec[2]);
			}
			else if (ValueField->TryGetArray(Arr))
			{
				if (!ReadVec(Arr, 3, Key.Vec)) { return TypeError(TEXT("{roll,pitch,yaw} or [r,p,y]")); }
			}
			else
			{
				return TypeError(TEXT("{roll,pitch,yaw} or [r,p,y]"));
			}
			break;
		}
		case ERigControlType::Transform:
		case ERigControlType::TransformNoScale:
		case ERigControlType::EulerTransform:
		{
			const TSharedPtr<FJsonObject>* XObj = nullptr;
			if (!ValueField->TryGetObject(XObj) || !XObj)
			{
				return TypeError(TEXT("{location:[x,y,z], rotation:{roll,pitch,yaw}, scale:[x,y,z]} (or a bare {roll,pitch,yaw} for rotation-only)"));
			}
			double Loc[3] = { 0, 0, 0 }, Rot[3] = { 0, 0, 0 }, Scl[3] = { 1, 1, 1 };
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			const TSharedPtr<FJsonObject>* RotObj = nullptr;
			const bool bHasRotationField = (*XObj)->TryGetObjectField(TEXT("rotation"), RotObj) && RotObj;
			const bool bBareRotator =
				!bHasRotationField &&
				((*XObj)->HasField(TEXT("roll")) || (*XObj)->HasField(TEXT("pitch")) || (*XObj)->HasField(TEXT("yaw")));
			if (!bHasRotationField && !bBareRotator
				&& !(*XObj)->HasField(TEXT("location")) && !(*XObj)->HasField(TEXT("scale")))
			{
				// Neither shape — refuse rather than silently writing identity.
				return TypeError(TEXT("{location, rotation, scale} (or a bare {roll,pitch,yaw})"));
			}
			if ((*XObj)->TryGetArrayField(TEXT("location"), Arr)) { ReadVec(Arr, 3, Loc); }
			if ((*XObj)->TryGetArrayField(TEXT("scale"), Arr)) { ReadVec(Arr, 3, Scl); }
			if (bBareRotator)
			{
				// Rotation-only convenience: animators reason in rotations, and this
				// shape is unambiguous because a transform value always nests its
				// rotation under "rotation".
				(*XObj)->TryGetNumberField(TEXT("roll"), Rot[0]);
				(*XObj)->TryGetNumberField(TEXT("pitch"), Rot[1]);
				(*XObj)->TryGetNumberField(TEXT("yaw"), Rot[2]);
			}
			else if (bHasRotationField)
			{
				(*RotObj)->TryGetNumberField(TEXT("roll"), Rot[0]);
				(*RotObj)->TryGetNumberField(TEXT("pitch"), Rot[1]);
				(*RotObj)->TryGetNumberField(TEXT("yaw"), Rot[2]);
			}
			Key.Xform = FTransform(
				FRotator(Rot[1], Rot[2], Rot[0]),
				FVector(Loc[0], Loc[1], Loc[2]),
				FVector(Scl[0], Scl[1], Scl[2]));
			break;
		}
		default:
			return MakeErrorJson(
				FString::Printf(TEXT("Control type %s is not supported for keying"),
					RigControlTypeToString(Type)),
				TEXT("unsupported_control_type"));
		}
		Parsed.Add(Key);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AnimMixer Set Control Keys")));
#endif
	Sequence->Modify();
	Target->Section->Modify();

	int32 Written = 0;
	for (const FParsedKey& Key : Parsed)
	{
		switch (Type)
		{
		case ERigControlType::Float:
		case ERigControlType::ScaleFloat:
			Target->Section->AddScalarParameterKey(ControlFName, Key.TickFrame, static_cast<float>(Key.Scalar));
			break;
		case ERigControlType::Integer:
			if (Control->Settings.ControlEnum)
			{
				Target->Section->AddEnumParameterKey(
					ControlFName, Key.TickFrame, static_cast<uint8>(Key.Integer));
			}
			else
			{
				Target->Section->AddIntegerParameterKey(ControlFName, Key.TickFrame, Key.Integer);
			}
			break;
		case ERigControlType::Bool:
			Target->Section->AddBoolParameterKey(ControlFName, Key.TickFrame, Key.Bool);
			break;
		case ERigControlType::Vector2D:
			Target->Section->AddVector2DParameterKey(ControlFName, Key.TickFrame,
				FVector2D(Key.Vec[0], Key.Vec[1]));
			break;
		case ERigControlType::Position:
		case ERigControlType::Scale:
		case ERigControlType::Rotator:
			Target->Section->AddVectorParameterKey(ControlFName, Key.TickFrame,
				FVector(Key.Vec[0], Key.Vec[1], Key.Vec[2]));
			break;
		default:
			Target->Section->AddTransformParameterKey(ControlFName, Key.TickFrame, Key.Xform);
			break;
		}
		++Written;
	}

	// --- Round-trip: read every written key back off the channel Control Rig
	// consumes (Issue #27729: Integer must not re-read the scalar channel we
	// used to write — that comparison is tautological). The engine's parameter
	// writers are void, so this is the only proof the write landed.
	// Build the EXPECTED value in the same shape ReadControlValue produces, so the
	// comparison is value-for-value rather than "did anything come back".
	auto BuildExpected = [&](const FParsedKey& Key) -> TSharedPtr<FJsonValue>
		{
			auto Num = [](double V) { return MakeShared<FJsonValueNumber>(V); };
			switch (Type)
			{
			case ERigControlType::Float:
			case ERigControlType::ScaleFloat:
				return Num(static_cast<float>(Key.Scalar));
			case ERigControlType::Integer:
				return Num(static_cast<double>(Key.Integer));
			case ERigControlType::Bool:
				return MakeShared<FJsonValueBoolean>(Key.Bool);
			case ERigControlType::Vector2D:
			{
				TArray<TSharedPtr<FJsonValue>> Arr{ Num(Key.Vec[0]), Num(Key.Vec[1]) };
				return MakeShared<FJsonValueArray>(Arr);
			}
			case ERigControlType::Position:
			case ERigControlType::Scale:
			case ERigControlType::Rotator:
			{
				TArray<TSharedPtr<FJsonValue>> Arr{ Num(Key.Vec[0]), Num(Key.Vec[1]), Num(Key.Vec[2]) };
				return MakeShared<FJsonValueArray>(Arr);
			}
			default:
			{
				const FVector Loc = Key.Xform.GetLocation();
				const FRotator Rot = Key.Xform.Rotator();
				const FVector Scl = Key.Xform.GetScale3D();
				TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetArrayField(TEXT("location"), { Num(Loc.X), Num(Loc.Y), Num(Loc.Z) });
				TSharedRef<FJsonObject> RotObj = MakeShared<FJsonObject>();
				RotObj->SetNumberField(TEXT("roll"), Rot.Roll);
				RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
				RotObj->SetNumberField(TEXT("yaw"), Rot.Yaw);
				Obj->SetObjectField(TEXT("rotation"), RotObj);
				Obj->SetArrayField(TEXT("scale"), { Num(Scl.X), Num(Scl.Y), Num(Scl.Z) });
				return MakeShared<FJsonValueObject>(Obj);
			}
			}
		};

	// Curves store float32 and rotations round-trip through quaternions, so the
	// tolerance is loose enough for representation error and tight enough that a
	// dropped value (identity written instead of the request) still fails.
	constexpr double ReadbackTolerance = 0.05;

	TArray<TSharedPtr<FJsonValue>> ReadbackArr;
	bool bMatch = true;
	for (const FParsedKey& Key : Parsed)
	{
		bool bKeyed = false;
		TSharedPtr<FJsonValue> Got = ReadControlValue(
			Target->Section, ControlFName, Type, FFrameTime(Key.TickFrame), bKeyed);
		const TSharedPtr<FJsonValue> Expected = BuildExpected(Key);
		const bool bEntryOk = bKeyed && Got.IsValid()
			&& JsonNearlyEqual(Got, Expected, ReadbackTolerance);
		bMatch &= bEntryOk;

		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("frame"), Key.DisplayFrame);
		Entry->SetBoolField(TEXT("keyed"), bKeyed);
		Entry->SetBoolField(TEXT("match"), bEntryOk);
		if (Got.IsValid())
		{
			Entry->SetField(TEXT("value"), Got);
		}
		if (!bEntryOk && Expected.IsValid())
		{
			Entry->SetField(TEXT("expected"), Expected);
		}
		ReadbackArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	if (!bMatch)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		TSharedRef<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetBoolField(TEXT("success"), false);
		Err->SetStringField(TEXT("error"),
			TEXT("Keys did not read back from the parameter curve; the write was rolled back"));
		Err->SetStringField(TEXT("error_code"), TEXT("readback_failed"));
		Err->SetArrayField(TEXT("readback"), ReadbackArr);
		return SerializeJson(Err);
	}

	Target->Section->MarkAsChanged();
	Sequence->MarkPackageDirty();

	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	Obj->SetStringField(TEXT("control_name"), ControlName);
	Obj->SetStringField(TEXT("control_type"), RigControlTypeToString(Type));
	Obj->SetStringField(TEXT("track_name"), Target->Track ? Target->Track->GetName() : TEXT(""));
	Obj->SetNumberField(TEXT("keys_written"), Written);
	Obj->SetArrayField(TEXT("readback"), ReadbackArr);
	Obj->SetBoolField(TEXT("readback_match"), bMatch);
	return SerializeJson(Obj);
#else
	#if NARSHAMCP_ANIMMIXER_SUPPORTED
	return MakeOptionalPluginUnavailable(TEXT("ControlRig"),
		TEXT("control_rig_plugin_unavailable"), TEXT("Control Rig key authoring"));
	#else
	return MakeUnsupported(TEXT("AnimMixerSetControlKeys"));
	#endif
#endif
}

// Keep NARSHAMCP_ANIMMIXER_SUPPORTED defined for the rest of the translation
// unit. UBT unity builds can include CURemoteSequencerAnimMixerLibrary_Bake.cpp
// after this file, and its pragma-once internals header cannot define the macro
// a second time (Issue #27822).
