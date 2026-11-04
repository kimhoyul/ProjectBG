// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CURemoteSequencerAnimMixerLibrary.generated.h"

/**
 * NarshaMCP Sequencer Animation Mixer Remote Control Library (Issues #11398, #11753, #16112, #16259, #25920, #25921).
 *
 * Phase 1 (#11398): layer-authoring ops (AddLayer, AddAnimation, SetLayerWeight, RemoveLayer).
 * Phase 2 (#11753): transition authoring ops (AddTransition, AutoCreateTransitions,
 *                   SetTransitionBlend, ChangeTransitionType).
 * Phase 3 (#16112): easing + condition reads/writes (GetSectionEasing/SetSectionEasing,
 *                   Get/Set/ClearCondition). Outliner-expansion ops are Rust/Python-RE only.
 * Phase 4 (#16259): introspection reads (GetLayers, GetSections, GetTransitions).
 * Phase 5a (#25920): off-UI UAF BlendMask asset creation (CreateBlendMask).
 * Phase 5b (#25921): BlendMask attachment to a mixer layer (SetBlendMask).
 *
 * Version note: both layer and transition models on UMovieSceneAnimationMixerTrack
 * were introduced in UE 5.8. On 5.7 the MovieSceneAnimMixer plugin ships a layer-less
 * track, so all functions here compile but decline gracefully (returning
 * {"success":false,"error":"... requires UE 5.8+"}). There is intentionally no
 * engine-version router on the Rust side — the only version branch is the #if block
 * in the .cpp.
 *
 * Each function returns a JSON string of the shape {"success":bool, ...}.
 * Invoked via Remote Control:
 *   PUT http://localhost:30010/remote/object/call
 *   {
 *     "objectPath": "/Script/NarshaMCP.Default__CURemoteSequencerAnimMixerLibrary",
 *     "functionName": "AnimMixerAddLayer",
 *     "parameters": { "SequencePath": "/Game/Cine/LS_Shot.LS_Shot", "TrackId": "auto", "LayerName": "Base" }
 *   }
 *
 * TrackId resolution (Issue #27558): "auto" / "" → the unique binding-owned
 * AnimMixer track (zero or multiple candidates fail closed); a binding GUID →
 * the AnimMixer track on that object binding; explicit "master" / "root" →
 * the first root AnimMixer track with an evaluation warning; otherwise a track
 * FName that must resolve uniquely across binding and root tracks.
 *
 * Successful RC-backed operations return the canonical selection metadata:
 * scope, track_id, binding_id for binding scope, and warning for root scope.
 * Binding-owned tracks keep the engine's default FMovieSceneMixedAnimationTarget
 * (Automatic); NarshaMCP does not construct FMovieSceneAnimInstanceTarget.
 *
 * SectionId resolution (Phase 2): a section's FName (exact match against
 * Section->GetFName()), e.g. "MovieSceneSkeletalAnimationSection_0" or
 * "MovieSceneAnimCrossfadeTransitionSection_0".
 *
 * TransitionClass short names (Phase 2):
 *   "crossfade"            → UMovieSceneAnimCrossfadeTransitionSection
 *   "inertial_dead_blend"  → UMovieSceneAnimInertialDeadBlendTransitionSection
 *
 * LayerIndex is the layer's row index ("layer index is implicitly the row index").
 */
UCLASS()
class NARSHAMCP_API UCURemoteSequencerAnimMixerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Append a new empty layer at the end of the AnimMixer track.
	 *
	 * @param SequencePath  LevelSequence asset path (with or without the .Object suffix).
	 * @param TrackId       "auto"/"" for the unique binding track, binding GUID,
	 *                      explicit "master"/"root", or a unique track name.
	 * @param LayerName     Optional editor display name for the new layer ("" = leave default).
	 * @return JSON: {"success":bool, "layer_index":int, "layer_display_name":str}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerAddLayer(const FString& SequencePath, const FString& TrackId, const FString& LayerName);

	/**
	 * Add a skeletal animation to a layer (row) at StartFrame. The section length is
	 * derived from the AnimSequence (there is no duration parameter).
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param LayerIndex    Target layer / row index.
	 * @param AnimPath      UAnimSequenceBase asset path.
	 * @param StartFrame    Section start frame (tick resolution).
	 * @return JSON: {"success":bool, "section_name":str, "start_frame":int, "end_frame":int}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerAddAnimation(const FString& SequencePath, const FString& TrackId, int32 LayerIndex, const FString& AnimPath, int32 StartFrame);

	/**
	 * Set a layer's constant blend weight. Adds a "Layer Weight" decoration to the
	 * layer if one is not already present and ensures that decoration owns its single
	 * hosting section before writing the weight channel's default value. This also
	 * repairs decoration-only assets saved by older versions. Keyframed weight curves
	 * are out of scope for Phase 1.
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param LayerIndex    Target layer / row index.
	 * @param Weight        Constant layer weight (typically 0.0–1.0).
	 * @return JSON: {"success":bool, "layer_index":int, "weight":float,
	 *                "weight_section_created":bool, "weight_section_count":int}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerSetLayerWeight(const FString& SequencePath, const FString& TrackId, int32 LayerIndex, float Weight);

	/**
	 * Clear the layer at LayerIndex: removes every section on that row plus the
	 * layer's child track (if any). Note: the engine exposes no RemoveLayer — the
	 * track's Layers array is private and InsertLayer has no inverse — so the layer
	 * slot may persist (empty) and higher rows are not shifted down.
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param LayerIndex    Target layer / row index.
	 * @return JSON: {"success":bool, "removed_layer_index":int, "remaining_layer_count":int, "note":str}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerRemoveLayer(const FString& SequencePath, const FString& TrackId, int32 LayerIndex);

	// ===== Phase 2 (#11753) — Transition authoring =====

	/**
	 * Create a transition section between two adjacent animation sections on the
	 * same layer. Wraps UMovieSceneAnimationMixerTrack::CreateTransitionSectionOfType
	 * (runtime-only, not BlueprintCallable on the track itself).
	 *
	 * @param SequencePath     LevelSequence asset path.
	 * @param TrackId          See AnimMixerAddLayer.
	 * @param FromSection      Source section's FName (Section->GetFName().ToString()).
	 * @param ToSection        Target section's FName.
	 * @param TransitionClass  "crossfade" or "inertial_dead_blend".
	 * @return JSON: {"success":bool, "transition_id":str, "start_frame":int, "end_frame":int, "transition_class":str}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerAddTransition(
		const FString& SequencePath, const FString& TrackId,
		const FString& FromSection, const FString& ToSection, const FString& TransitionClass);

	/**
	 * Auto-create transitions for every neighboring section on the same row as the
	 * given Section. Engine call returns void, so we snapshot
	 * FindTransitionsForSection before + after and diff to populate the list.
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param SectionId     Section's FName whose neighbors get transitions.
	 * @return JSON: {"success":bool, "transitions_created":[ {"transition_id":str, "transition_class":str, "start_frame":int, "end_frame":int} ... ]}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerAutoCreateTransitions(
		const FString& SequencePath, const FString& TrackId, const FString& SectionId);

	/**
	 * Type-aware blend configuration for a transition section.
	 *
	 * Crossfade params:           {"value": <float>}            → BlendCurve default
	 * InertialDeadBlend params:   {"mode": "<EAlphaBlendOption>"}  → BlendMode
	 *
	 * Rejects params that don't match the transition's concrete class.
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param TransitionId  Transition section's FName.
	 * @param ParamsJson    JSON object with type-specific blend params.
	 * @return JSON: {"success":bool, "transition_class":str, "applied_params":obj}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerSetTransitionBlend(
		const FString& SequencePath, const FString& TrackId,
		const FString& TransitionId, const FString& ParamsJson);

	/**
	 * Change a transition section's concrete class. Implemented inline: creates a
	 * new transition section of NewClass on the same From/To pair, removes the old
	 * section. Blend data is NOT preserved across types (engine API
	 * UMovieSceneAnimMixerTransitionExtensions::ChangeTransitionType claims best-
	 * effort preservation; we make no such claim — callers should re-issue
	 * AnimMixerSetTransitionBlend after a type change).
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param TransitionId  Existing transition section's FName.
	 * @param NewClass      "crossfade" or "inertial_dead_blend".
	 * @return JSON: {"success":bool, "transition_id":str, "new_class":str}
	 *           transition_id is the NEW section's FName (may differ from the input).
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerChangeTransitionType(
		const FString& SequencePath, const FString& TrackId,
		const FString& TransitionId, const FString& NewClass);

	// ===== Phase 3 (#16112) — Easing authoring =====

	/**
	 * Read a section's effective ease-in / ease-out durations (in frames), honoring
	 * any manual override. Wraps UMovieSceneSectionEasingExtensions::GetEaseIn/OutDuration.
	 *
	 * @param SequencePath  LevelSequence asset path.
	 * @param TrackId       See AnimMixerAddLayer.
	 * @param SectionId     Section's FName (e.g. an animation or transition section).
	 * @return JSON: {"success":bool, "ease_in_frames":int, "ease_out_frames":int}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerGetSectionEasing(
		const FString& SequencePath, const FString& TrackId, const FString& SectionId);

	/**
	 * Set a section's manual ease-in / ease-out durations (in frames), enabling the
	 * manual override. Pass -1 for either parameter to leave it unchanged. Wraps
	 * UMovieSceneSectionEasingExtensions::SetEaseIn/OutDuration.
	 *
	 * @param SequencePath   LevelSequence asset path.
	 * @param TrackId        See AnimMixerAddLayer.
	 * @param SectionId      Section's FName.
	 * @param EaseInFrames   New ease-in duration in frames, or -1 to leave unchanged.
	 * @param EaseOutFrames  New ease-out duration in frames, or -1 to leave unchanged.
	 * @return JSON: {"success":bool, "ease_in_frames":int, "ease_out_frames":int}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerSetSectionEasing(
		const FString& SequencePath, const FString& TrackId, const FString& SectionId,
		int32 EaseInFrames, int32 EaseOutFrames);

	// ===== Phase 3 (#16112) — Condition authoring =====
	//
	// Level: "section" (needs SectionId), "track" (the resolved AnimMixer track),
	// or "row" (the track + RowIndex). Conditions are UMovieSceneCondition subclasses;
	// SetCondition takes a class object path and instantiates a new condition.

	/**
	 * Read the condition on a section / track / track-row.
	 * @return JSON: {"success":bool, "level":str, "has_condition":bool, "condition_class":str}
	 *   condition_class is "" when no condition is set.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerGetCondition(
		const FString& SequencePath, const FString& TrackId,
		const FString& Level, const FString& SectionId, int32 RowIndex);

	/**
	 * Set a condition on a section / track / track-row by instantiating ConditionClass
	 * (a UMovieSceneCondition subclass object path, e.g.
	 * "/Script/MovieScene.MovieSceneGroupCondition").
	 * @return JSON: {"success":bool, "level":str, "condition_class":str}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerSetCondition(
		const FString& SequencePath, const FString& TrackId,
		const FString& Level, const FString& SectionId, int32 RowIndex,
		const FString& ConditionClass);

	/**
	 * Remove the condition from a section / track / track-row.
	 * @return JSON: {"success":bool, "level":str}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerClearCondition(
		const FString& SequencePath, const FString& TrackId,
		const FString& Level, const FString& SectionId, int32 RowIndex);

	// ===== Phase 5a (#25920) — BlendMask asset creation =====

	/**
	 * Create and save a UAF skeleton BlendMask without opening an editor UI.
	 * BoneWeightsJson is a non-empty object mapping skeleton bone names to weights
	 * in [0, 1]. Unspecified bones inherit the closest ancestor's value.
	 *
	 * The concrete UAF class is resolved by object path at runtime so NarshaMCP does
	 * not link UAF/UAFEditor and remains source-compatible with the UE 5.9 rename.
	 * UE 5.8 uses UAFBlendMask; the newer UAFSkeletonBlendMask name is tried first.
	 *
	 * @param SkeletonPath     Skeleton asset path (with or without the .Object suffix).
	 * @param AssetPath        New /Game package/object path. Existing assets are rejected.
	 * @param BoneWeightsJson  JSON object, for example {"root":0.0,"spine_01":1.0}.
	 * @return JSON: {"success":bool, "asset_path":str, "resolved_class":str,
	 *                "requested_override_count":int, "effective_bone_weights":obj}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerCreateBlendMask(
		const FString& SkeletonPath, const FString& AssetPath, const FString& BoneWeightsJson);

	// ===== Phase 5b (#25921) — BlendMask attachment =====

	/**
	 * Attach an existing UAF BlendMask asset to a mixer layer via the layer's Mask
	 * decoration, mirroring the editor's own add-mask flow: GetOrCreateDecoration →
	 * CreateNewSection → SetRange(closed playback range; masks don't support infinite
	 * ranges) → AddSection (which wires OwningLayer + row index) → SetBlendMask. If
	 * the layer already has a mask section it is re-targeted instead of duplicated.
	 * The decoration supports multiple mask sections, so when a hand-authored layer
	 * holds several, the FIRST is retargeted and the rest are left alone —
	 * mask_section_count reports the total so the caller can tell.
	 *
	 * The engine's SetBlendMask(UObject*) silently ignores a non-BlendMask object
	 * (Cast<UUAFBlendMask> failure), so this wrapper (a) pre-validates the asset's
	 * class name against UAFSkeletonBlendMask/UAFBlendMask — name-based, NarshaMCP
	 * does not link UAF, surviving the 5.9 rename — and (b) re-reads GetBlendMask()
	 * after the call; a mismatch cancels the transaction and returns an explicit error.
	 *
	 * @param SequencePath   LevelSequence asset path.
	 * @param TrackId        See AnimMixerAddLayer.
	 * @param LayerIndex     Target layer / row index.
	 * @param MaskAssetPath  Existing UAF BlendMask asset path.
	 * @return JSON: {"success":bool, "layer_index":int, "mask_asset_path":str,
	 *                "section_name":str, "resolved_mask_class":str, "created_section":bool,
	 *                "mask_section_count":int}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerSetBlendMask(
		const FString& SequencePath, const FString& TrackId, int32 LayerIndex, const FString& MaskAssetPath);

	// ===== Issue #26583 — exact-path BlendMask reload/evaluate oracle =====

	/**
	 * Unload/reload an existing BlendMask at the exact AssetPath and verify
	 * table weights, skeleton identity, mask-section assignment, and the
	 * evaluation-task UStruct / cached blend profile. Does not create or write
	 * an asset. UAF is not linked; class and properties are resolved by path.
	 *
	 * @param AssetPath              Existing /Game package/object path.
	 * @param ExpectedOverridesJson  JSON object of bone name -> [0, 1] weights.
	 * @return JSON: success, asset_path, resolved_class, reload, oracle_module,
	 *         requested_overrides, effective_bone_weights,
	 *         inherited_descendant_weight, mask_section_assignment,
	 *         evaluation_task_factory, error_code.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerReloadEvaluateBlendMask(
		const FString& AssetPath, const FString& ExpectedOverridesJson);

	// NOTE (Issue #16112): Phase 3c outliner-expansion ops are driven from Rust via
	// Python Remote Execution, NOT this C++ RC library — the SequencerCore outliner
	// scripting surface is reflection-only (MinimalAPI, not C++-linkable).

	// ===== Phase 4 (#16259) — Introspection reads =====
	//
	// Pure reads (no transaction) that surface the layer / section / transition FNames
	// the Phase 2/3 set-ops require on a pre-existing AnimMixer asset. Mirrors the
	// Phase 3 easing/condition read pattern (resolve track → iterate → JSON array).

	/**
	 * List the layers on the AnimMixer track.
	 * @return JSON: {"success":bool, "layers":[ {"layer_index":int, "layer_display_name":str, "section_count":int} ... ]}
	 *   layer_display_name is "" outside editor builds (WITH_EDITORONLY_DATA gates GetDisplayName).
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerGetLayers(const FString& SequencePath, const FString& TrackId);

	/**
	 * List the sections on the AnimMixer track. LayerIndex < 0 returns every layer/row;
	 * LayerIndex >= 0 filters to that row. section_type is the section's UClass name.
	 * anim_path (#19932) is the section's AnimSequence object path — present only for
	 * animation sections (GetSourceObject() -> Params.Animation); omitted for transitions.
	 * @return JSON: {"success":bool, "sections":[ {"section_name":str, "layer_index":int, "start_frame":int, "end_frame":int, "section_type":str, "anim_path"?:str} ... ]}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerGetSections(const FString& SequencePath, const FString& TrackId, int32 LayerIndex);

	/**
	 * List the transitions on the AnimMixer track. An empty SectionId returns every
	 * transition section on the track; a non-empty SectionId returns only the
	 * transitions touching that section (FindTransitionsForSection).
	 * @return JSON: {"success":bool, "transitions":[ {"transition_id":str, "transition_class":str, "from_section":str, "to_section":str, "start_frame":int, "end_frame":int} ... ]}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerGetTransitions(const FString& SequencePath, const FString& TrackId, const FString& SectionId);

	// ===== Phase 5c (#25957) — Control Rig correction layer =====
	//
	// LIVE-VERIFIED DESIGN NOTE (UE 5.8.0 install, 2026-08-19):
	// The issue originally specified a Control Rig track nested inside an AnimMixer
	// layer (`AddChildTrackToLayer`). UE 5.8 ships that evaluation bridge in the
	// separate experimental MovieSceneMixedControlRig plugin. Its
	// UMovieSceneMixedControlRigSystem produces FAnimNextControlRigTask entries for
	// mixer-child Control Rig tracks; NarshaMCP enables it as an optional dependency
	// so UE 5.7 can ignore the absent descriptor dependency and load safely. The
	// AnimMixer API itself still declines gracefully on 5.7 via the version gate.

	/**
	 * Add a Layered (additive) Control Rig correction track to a binding.
	 *
	 * Guards live-verified against UE 5.8.0:
	 *  - `UFKControlRig::Initialize` only creates controls when the rig's object
	 *    binding already resolves to a USkeletalMeshComponent/USkeleton
	 *    (`FKControlRig.cpp:292-318`), so BindToObject runs BEFORE Initialize and
	 *    control_count is asserted non-zero afterwards. The engine's own scripted
	 *    entry point skips this and yields a rig with zero controls.
	 *  - Any failure after the track exists rolls the transaction back and reparents
	 *    the orphan rig to the transient package: a Control Rig track with no rig
	 *    makes the whole LevelSequence fail to open.
	 *
	 * @param SequencePath        LevelSequence asset path.
	 * @param BindingId           Binding GUID owning the skeletal mesh. Empty = auto-resolve
	 *                            the single skeletal binding; 0 or 2+ candidates is an error.
	 * @param ControlRigClassPath Control Rig class/blueprint path. Empty = UFKControlRig.
	 * @param bAdditive           True (default intent) = delta correction on top of the base pose.
	 * @param TrackId             AnimMixer selector used when LayerIndex >= 0. Empty/auto selects
	 *                            the unique binding-owned mixer.
	 * @param LayerIndex          Existing empty mixer layer for child placement. -1 preserves the
	 *                            legacy top-level Control Rig path.
	 * @return JSON: {"success":bool, "binding_id":str, "control_rig_class":str,
	 *                "control_rig_id":str, "track_name":str, "additive":bool,
	 *                "control_count":int, "reused_track":bool, "placement":str,
	 *                "layer_index":int,
	 *                "resolved_component":str, "resolution_path":str,
	 *                "resolve_trail":[str]}. binding_not_found,
	 *                binding_not_skeletal, binding_ambiguous, and
	 *                mixed_control_rig_unavailable return error_code; binding
	 *                resolution failures also include the non-empty resolve_trail.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer",
		meta = (CPP_Default_TrackId = "", CPP_Default_LayerIndex = "-1"))
	static FString AnimMixerAddControlRigLayer(
		const FString& SequencePath, const FString& BindingId,
		const FString& ControlRigClassPath, bool bAdditive,
		const FString& TrackId = TEXT(""), int32 LayerIndex = INDEX_NONE);

	/**
	 * List the Control Rig controls on a binding's Control Rig track(s) — the lookup
	 * surface an AI uses to turn "the left shoulder" into a control name.
	 *
	 * Values are read straight off the section's parameter curves, NOT via
	 * UControlRigSequencerEditorLibrary's getters: those call GetSequencerFromAsset(),
	 * which null-derefs and CRASHES the editor when no LevelSequence is open
	 * (5.8.0 live-verified, `ControlRigSequencerEditorLibrary.cpp:203-214`).
	 *
	 * @param Frame Frame to sample values at (display-rate frame number).
	 * @return JSON: {"success":bool, "frame":int, "control_rigs":[
	 *                 {"control_rig_id":str, "control_rig_class":str, "additive":bool,
	 *                  "control_count":int,
	 *                  "controls":[{"name":str, "type":str, "keyed":bool, "value":<typed>}]}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerGetControls(
		const FString& SequencePath, const FString& BindingId, int32 Frame);

	/**
	 * Insert keys on ONE Control Rig control by writing the section's parameter
	 * curves directly (UMovieSceneParameterSection::Add*ParameterKey).
	 *
	 * Live-verified constraints this enforces (UE 5.8.0):
	 *  - Add*ParameterKey accepts ANY name and silently creates an orphan parameter
	 *    with zero channels when the name is not a real control, so the control is
	 *    resolved through the rig (FindControl) BEFORE any write.
	 *  - The engine's SetLocalControlRig* setters do NOT write keys outside an open
	 *    Sequencer (they only broadcast ControlModified, whose only listener is the
	 *    editor-side track editor) — hence the direct-curve path.
	 *  - Every write is read back off the same curve and compared NUMERICALLY; a
	 *    presence-only check reported success for keys whose value was identity.
	 *
	 * @param KeysJson JSON array: [{"frame":int,"value":<typed>}, ...]. `value` shape
	 *                 follows the control type — number for Float/ScaleFloat/Integer,
	 *                 bool for Bool, [x,y] for Vector2D, [x,y,z] for Position/Scale,
	 *                 {"roll","pitch","yaw"} for Rotator, and either
	 *                 {"location":[..],"rotation":{..},"scale":[..]} or a bare
	 *                 {"roll","pitch","yaw"} (rotation-only) for the transform family.
	 * @return JSON: {"success":bool, "control_name":str, "control_type":str,
	 *                "keys_written":int, "readback":[{"frame":int,"keyed":bool,
	 *                "match":bool,"value":<typed>}], "readback_match":bool}.
	 *                Multiple matching rigs fail with control_rig_ambiguous and
	 *                candidate placement metadata rather than selecting one.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerSetControlKeys(
		const FString& SequencePath, const FString& BindingId,
		const FString& ControlName, const FString& KeysJson);

	// ---- Phase 5d (#27002): bake the evaluated result to a new AnimSequence ----
	//
	// LIVE-VERIFIED DESIGN NOTE (UE 5.8.1 install, 2026-08-22):
	// The issue specified UControlRigSequencerEditorLibrary::ExportAnimSequenceFromSequencer,
	// which requires an open Sequencer. That entry point CRASHES the editor when no
	// sequence is open -- GetSequencerFromAsset() dereferences a null LevelSequence
	// while formatting its own error string (ControlRigSequencerEditorLibrary.cpp:202-214),
	// so it never reaches its own guard at :1170. The same crash is already documented
	// for the CR getters above.
	//
	// This op uses the engine's documented headless path instead,
	// USequencerToolsFunctionLibrary::ExportAnimSequence, which builds its own
	// transient ALevelSequenceActor + ULevelSequencePlayer and never touches the
	// Sequencer toolkit. A live spike confirmed the AnimMixer layer AND the additive
	// Control Rig both reach the bake and compose (baked-minus-source was exactly the
	// injected +60 deg on the keyed bone and 0.00 on a control bone, every sampled
	// frame). Evidence: docs/evidence/issues/27002/.
	//
	// Consequences worth knowing before you edit this:
	//  - NO Sequencer is opened, closed, or restored, so outliner expansion, selection
	//    and curve-editor state are untouched. Nothing to save and put back.
	//  - bCreateLink is deliberately NOT exposed and hardcoded false: LinkAnimSequence
	//    calls Sequence->Modify() on the SOURCE LevelSequence (SequencerTools.cpp:503-531).
	//  - A custom frame range is interpreted in UAnimSeqExportOption::CustomDisplayRate,
	//    which defaults to 30 fps (AnimSeqExportOption.h:110). This op pins it to the
	//    sequence's own display rate, or a 24/60 fps sequence would silently bake the
	//    wrong span.
	//  - A root-scope ("master") AnimMixer track does not evaluate against a binding.
	//    UMovieSceneAnimMixerSystem::ResolveAnimationTarget has no bound object, the
	//    mixer entity never enters ECS, and ExportAnimSequence still returns true with
	//    a rest-pose asset (measured: 0 moving bones / 0 cm). This op refuses that
	//    combination (mixer_track_root_scoped) and also refuses a fully static bake
	//    unless allow_static_pose is set (bake_static_pose). HasBeenPopulated() cannot
	//    tell the two apart.
	//
	/**
	 * Bake one binding's evaluated animation into a NEW AnimSequence asset.
	 *
	 * @param SequencePath      LevelSequence asset path.
	 * @param AssetPath         Destination /Game path for the new AnimSequence.
	 * @param BindingId         REQUIRED binding GUID. anim_mixer_add_control_rig_layer and
	 *                          anim_mixer_get_layers both return it; requiring it also avoids
	 *                          pre-rejecting spawnable bindings that the engine's own player
	 *                          resolves fine.
	 * @param SkeletonPath       Optional. The destination AnimSequence CANNOT be created
	 *                          without a skeleton -- its data model builds an FKControlRig
	 *                          in PostInitProperties and fails hard without one. Omit it and
	 *                          the skeleton is resolved from the binding's skeletal mesh
	 *                          (including a possessable child component); pass it explicitly
	 *                          when the binding does not resolve at author time, e.g. a
	 *                          spawnable. Either way the engine overwrites it during export,
	 *                          and the response reports both the source and whether the
	 *                          reloaded asset agrees.
	 * @param ExportOptionsJson Optional JSON object. Accepted keys: start_frame, end_frame
	 *                          (must be given together), export_transforms,
	 *                          export_morph_targets, export_attribute_curves,
	 *                          export_material_curves, record_in_world_space,
	 *                          warm_up_frames, delay_before_start. An unknown key is an
	 *                          error, never a silent no-op.
	 * @param bOverwrite        Replace an existing asset THIS op previously baked. An asset
	 *                          without our provenance is refused even when true.
	 * @param bAllowStaticPose  Permit a bake whose sampled bone tracks do not move
	 *                          (legitimate idle / single-pose sources). Default false.
	 *                          Does NOT unlock a root-scope mixer — that is always refused.
	 * @return JSON: {"success":bool, "asset_path":str, "skeleton_path":str,
	 *                "start_frame":int, "end_frame":int, "frame_count":int,
	 *                "display_rate":str, "play_length_seconds":float,
	 *                "sampled_key_count":int, "bone_track_count":int,
	 *                "montage_ready":bool, "created_link":false,
	 *                "source_sequence_unchanged":bool, "already_baked":bool,
	 *                "provenance":{...}, "restored_state":{...}, "warnings":[str]}
	 *               All numeric/identity fields are read back off the RELOADED asset.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Sequencer")
	static FString AnimMixerBakeToAnimSequence(
		const FString& SequencePath, const FString& AssetPath, const FString& BindingId,
		const FString& SkeletonPath, const FString& ExportOptionsJson, bool bOverwrite,
		bool bAllowStaticPose);
};
