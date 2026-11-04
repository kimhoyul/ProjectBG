---
name: ue-sequencer-cinematics
model: opus
description: "Purpose: Sequencer cinematics workflow orchestration. Analyze, build, and debug Level Sequence cinematics using 4 sequencer sub-tools (107 ops). Structure analysis, track management, keyframe editing, camera cuts, and playback control. Triggers: 'cinematics', 'sequencer workflow', '시네마틱', '시퀀서 워크플로우', 'camera cut', 'level sequence', '레벨 시퀀스'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "sequence name or action (e.g., LS_IntroCinematic, 'add camera cut to LS_Boss')"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Sequencer Cinematics Workflow

**Purpose**: Orchestrate Level Sequence cinematics — from structure analysis to playback debugging

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what Level Sequence to analyze or what cinematic action to perform.
   > If `$ARGUMENTS` is a generic term like "cutscene" or "intro" (not a specific `LS_` asset name), use `ue_sequencer_structure(operation='search_sequences', params={'query': '$ARGUMENTS'})` to find matching Level Sequences. If no results, list all sequences with `ue_glob(pattern='LS_*', domain='assets')`.
2. **Detect intent** using the routing matrix below
3. **Execute MCP tools** across 4 sequencer sub-tools
4. **Generate structured report** with Mermaid timeline visualization

---

## Auto-Trigger Phrases

### Korean
- "시네마틱 분석", "시퀀서 워크플로우"
- "레벨 시퀀스 구조", "카메라 컷 추가"
- "시퀀서 키프레임", "시퀀서 재생"
- "시네마틱 만들어줘", "컷신 분석"

### English
- "Cinematics analysis", "Sequencer workflow"
- "Level sequence structure", "Add camera cut"
- "Sequencer keyframes", "Playback control"
- "Build a cinematic", "Cutscene analysis"

---

## Intent Auto-Routing Matrix

| Intent | Primary Tool | Key Operations |
|--------|-------------|----------------|
| Analyze structure | `ue_sequencer_structure` | `get_structure`, `list_bindings`, `get_shot_structure` |
| Find sequences | `ue_sequencer_structure` | `list_sequences`, `search_sequences` |
| Manage tracks | `ue_sequencer_tracks` | `list_tracks`, `add_track`, `compare_sequences` |
| Edit keyframes | `ue_sequencer_keyframes` | `get_keyframes`, `add_keyframe`, `get_curve_data`, `bake_keyframes` |
| Camera work | `ue_sequencer_playback` | `add_camera_cut`, `set_camera_binding`, `get_active_camera` |
| Playback control | `ue_sequencer_playback` | `play`, `stop`, `scrub`, `get_playback_status` (all write-style; `get_playback_status` opens the sequence in the Sequencer window) |
| Add actors | `ue_sequencer_playback` | `add_spawnable`, `add_property_track` |
| Animation Mixer layers (5.8+) | `ue_sequencer_tracks` | `anim_mixer_add_layer`, `anim_mixer_add_animation`, `anim_mixer_set_layer_weight`, `anim_mixer_remove_layer` |
| Bake mixer (+ Control Rig) to AnimSequence (5.8+) | `ue_sequencer_tracks` | `anim_mixer_bake_to_anim_sequence` |
| Animation Mixer transitions (5.8+) | `ue_sequencer_tracks` | `anim_mixer_add_transition`, `anim_mixer_auto_create_transitions`, `anim_mixer_set_transition_blend`, `anim_mixer_change_transition_type` |
| Audit quality | `ue_sequencer_tracks` | `sequence_audit` |

---

## Workflow

### Step 1: Discover Sequences

```python
ToolSearch("select:mcp__narshamcp__ue_sequencer_structure")

# List all Level Sequences in the project
sequences = ue_sequencer_structure(operation="list_sequences")

# Or search by name pattern
sequences = ue_sequencer_structure(operation="search_sequences", query="LS_Intro*")
```
### Step 2: Analyze Structure

```python
# Get full structure (tracks, bindings, sub-sequences)
structure = ue_sequencer_structure(operation="get_structure", sequence_path="LS_IntroCinematic")

# Get hierarchical shot breakdown (for multi-shot cinematics)
shots = ue_sequencer_structure(operation="get_shot_structure", sequence_path="LS_IntroCinematic", max_depth=3)

# List actor bindings
bindings = ue_sequencer_structure(operation="list_bindings", sequence_path="LS_IntroCinematic")
```
### Step 3: Inspect Tracks & Keyframes

> **Parallelization**: Steps 2 (`list_tracks`) and 3 (`get_keyframes` for each track) can proceed in parallel for independent tracks. Use parallel MCP calls when analyzing multiple tracks simultaneously.

```python
ToolSearch("select:mcp__narshamcp__ue_sequencer_tracks")
ToolSearch("select:mcp__narshamcp__ue_sequencer_keyframes")

# List all tracks
tracks = ue_sequencer_tracks(operation="list_tracks", sequence_path="LS_IntroCinematic")

# Get keyframes for a specific track
keyframes = ue_sequencer_keyframes(
    operation="get_keyframes",
    sequence_path="LS_IntroCinematic",
    track_id="<track_id from list_tracks>",
)

# Get curve data with tangents
curves = ue_sequencer_keyframes(operation="get_curve_data", sequence_path="LS_IntroCinematic", track_id="<track_id>")

# Resample Float/Double curves every 4 display frames into linear keys
baked = ue_sequencer_keyframes(
    operation="bake_keyframes",
    sequence_path="LS_IntroCinematic",
    track_id="<track_id>",
    frame_interval=4,
    start_frame=0,
    end_frame=120,
)
```

If baking returns `partial_mutation:true`, stop and use Editor Undo for **Bake
Sequencer Keyframes** before retrying. The failure response also sets
`asset_dirty:true` and `undo_recommended:true`.

### Step 4: Camera & Playback (Editor Required)

> **Pre-check**: Before Editor-dependent operations, verify connectivity: `ue_check_health()`. If Editor is offline, skip Steps 4-5 (playback, quality audit) and note: "Editor offline — playback and quality audit skipped. Structural analysis complete."

```python
ToolSearch("select:mcp__narshamcp__ue_sequencer_playback")

# Add camera cut track
ue_sequencer_playback(
    operation="add_camera_cut",
    sequence_path="LS_IntroCinematic",
    start_frame=0,
    end_frame=120,
)

# Set active camera for a cut
ue_sequencer_playback(
    operation="set_camera_binding",
    sequence_path="LS_IntroCinematic",
    section_index=0,
    camera_binding_id="<binding_id from list_bindings>",
)

# Preview playback
ue_sequencer_playback(operation="play", sequence_path="LS_IntroCinematic")

# Get playback status (write-style side effect: opens the sequence in the Sequencer editor window)
status = ue_sequencer_playback(operation="get_playback_status", sequence_path="LS_IntroCinematic")
```
### Step 5: Quality Audit

```python
# Run sequence audit (checks naming, empty tracks, orphan bindings)
audit = ue_sequencer_tracks(operation="sequence_audit", sequence_path="LS_IntroCinematic")

# Get sequence structure for settings review (frame rate, resolution, etc.)
structure = ue_sequencer_structure(operation="get_structure", sequence_path="LS_IntroCinematic")
```
---

## Output Format

```
=== Sequencer Cinematics: [Sequence Name] ===

--- Overview ---
Sequence: LS_IntroCinematic
Duration: 10.0s (300 frames @ 30fps)
Tracks: 12 | Bindings: 5 | Sub-sequences: 2

--- Shot Structure ---
LS_IntroCinematic
+-- Shot_01 (0-90f) — CineCamera_Wide
+-- Shot_02 (90-180f) — CineCamera_CloseUp
+-- Shot_03 (180-300f) — CineCamera_Dolly

--- Bindings ---
  1. BP_MainCharacter (Possessable)
  2. CineCamera_Wide (Spawnable)
  3. CineCamera_CloseUp (Spawnable)
  4. DirectionalLight (Possessable)
  5. SK_Environment (Possessable)

--- Tracks Summary ---
  Transform: 3 tracks (156 keyframes)
  Camera: 2 tracks (3 camera cuts)
  Animation: 2 tracks (4 sections)
  Event: 1 track (5 events)
  Material: 1 track (8 keyframes)

--- Audit ---
  Issues: 2
  - WARNING: Track "OldTrack" has 0 keyframes (empty)
  - WARNING: Binding "LightRig" has no tracks

--- Mermaid Timeline ---
gantt
    title LS_IntroCinematic
    dateFormat X
    axisFormat %s

    section Cameras
    CineCamera_Wide    :0, 90
    CineCamera_CloseUp :90, 180
    CineCamera_Dolly   :180, 300

    section Animation
    Walk Montage       :0, 120
    Idle Montage       :120, 300
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Sequence not found | Wrong name or not in project | `ue_sequencer_structure(list_sequences)` to find available sequences |
| Editor not connected | Playback/camera ops require live Editor | Skip Step 4, report offline limitation |
| Track ID invalid | Track removed or renamed | Re-query `list_tracks` to get current IDs |
| get_shot_structure max_depth | Capped at 5 for safety | Use default depth 3 for most projects |
| Binding state error | Actor not in level | Use `add_spawnable` to create spawnable actor |
| Empty audit result | No issues found | Report "Clean — no issues detected" |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** - Should activate:
1. "LS_IntroCinematic 구조 분석해줘" -> Activate (structure analysis)
2. "Add a camera cut to the boss cinematic" -> Activate (camera work)
3. "시퀀서 키프레임 확인" -> Activate (keyframe inspection)
4. "Compare LS_Intro and LS_Ending sequences" -> Activate (comparison)
5. "시네마틱 오디트" -> Activate (quality audit)

**Negative (3)** - Should NOT activate:
1. "ACharacter 어떻게 동작해?" -> Use ue-explain skill
2. "애니메이션 BP 분석" -> Use animation-workflow skill
3. "Niagara VFX 시퀀서에 추가" -> Use ue_manage_niagara directly

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Intent detection accuracy | >90% |
| Structure analysis completeness | >95% |
| Response time (single sequence) | <30s |
| Mermaid diagram validity | 100% |

---

**Status**: Phase 1 MVP
**Related**: Issue #6047 (MCP Skill Builder)

---

## Tool Reference: 4 Sequencer Sub-Tools (102 Operations Total)

> **Note**: MCP schemas for these tools are intentionally minimal (thin schema, Issue #7069).
> This section serves as the full operation reference.

### ue_sequencer_structure (25 ops) — Structure, bindings, discovery

| Operation | Description | Key Params |
|-----------|-------------|------------|
| `smart` | Auto-detect intent | `sequence_path` |
| `get_structure` | Get sequence structure | `sequence_path`, `include_tracks` |
| `get_sequence_info` | **Alias** → `get_structure` (#19915 live residual) | same as `get_structure` |
| `list_sequences` | List all sequences | `project_root` |
| `search_sequences` | Search by pattern | `pattern` |
| `list_bindings` [오프라인] | List actor bindings (binary-first) | `sequence_path` |
| `get_binding_state` [오프라인] | Get binding state (binary-first) | `sequence_path`, `binding_id` |
| `bind_actor` [E] | Bind actor to sequence | `sequence_path`, `actor_path` |
| `unbind_actor` [E] | Unbind actor | `sequence_path`, `binding_id` |
| `rebind_actor` [E] | Rebind to different actor | `sequence_path`, `binding_id`, `new_actor_path` |
| `validate_settings` [오프라인] | Validate sequence settings (playback range, empty bindings; offline binary parse by default) | `sequence_path`, `use_editor`? |
| `list_sub_sequences` [오프라인] | List sub-sequences (binary-first) | `sequence_path` |
| `add_sub_sequence` [E] | Add sub-sequence | `sequence_path`, `sub_sequence_path` |
| `remove_sub_sequence` [E] | Remove sub-sequence | `sequence_path`, `sub_sequence_path` |
| `get_shot_structure` [오프라인] | Get shot hierarchy (recursive binary tree) | `sequence_path`, `max_depth` |
| `list_operations` | List available ops | — |
| `introspect_api` | API introspection | — |
| `list_available_operations` | All operations list | — |
| `get_api_version_diff` | API version diff | `from_version`, `to_version` |
| `discover_track_types` | Available track types | — |
| `discover_section_types` | Available section types | `category` |

### ue_sequencer_tracks (47 ops) — Track, section, sequence management + AnimMixer layers, transitions, easing, conditions, outliner, introspection

| Operation | Description | Key Params |
|-----------|-------------|------------|
| `smart` | Auto-detect intent | `sequence_path` |
| `add_track` | Add track to sequence; `binding_id` is required for attach and AnimMixer tracks | `sequence_path`, `track_type`, `binding_id` |
| `remove_track` | Remove track | `sequence_path`, `track_id` |
| `list_tracks` [오프라인] | List all tracks (binary-first) | `sequence_path` |
| `modify_track` [E] | Modify track properties | `sequence_path`, `track_id`, `properties` |
| `duplicate_track` [E] | Duplicate a track | `sequence_path`, `track_id`, `new_name` |
| `reorder_tracks` [E] | Reorder tracks | `sequence_path`, `track_ids` (array) |
| `get_track_info` [오프라인] | Get track details (binary-first) | `sequence_path`, `track_id` |
| `list_sections` [오프라인] | List track sections (binary-first) | `sequence_path`, `track_id` |
| `add_section` | Add section to track | `sequence_path`, `track_id`, `start_frame`, `end_frame` |
| `resize_section` | Resize section | `sequence_path`, `track_id`, `section_index`, `start_frame`, `end_frame` |
| `get_section_properties` | Get section props | `sequence_path`, `track_id`, `section_index` |
| `create_sequence` | Create new sequence (auto-saves `.uasset` so offline binary `list_tracks`/`list_bindings` work immediately; #19915) | `asset_name`, `asset_path`, `display_rate` |
| `duplicate_sequence` | Duplicate sequence | `sequence_path`, `new_path`, `new_name` |
| `delete_sequence` [E] | Delete a LevelSequence asset (explicit-op only; never smart-routed) | `sequence_path` |
| `import_sequence` [E] | Import FBX animation onto an existing sequence's bindings | `sequence_path`, `input_path` |
| `merge_sequences` | Merge sequences | `source_sequences` (array), `target_sequence`, `merge_mode` |
| `compare_sequences` | Compare two sequences | `sequence_a`, `sequence_b` |
| `sequence_audit` | Audit sequence quality | `sequence_path`, `threshold_bindings` |
| `anim_mixer_add_layer` [E, 5.8+] | Append an AnimMixer layer; omission auto-selects one binding mixer | `sequence_path`, `track_id`? (binding GUID / explicit `master`), `layer_name`? |
| `anim_mixer_add_animation` [E, 5.8+] | Add AnimSequence to a layer/row | `sequence_path`, `track_id`, `layer_index`, `anim_path`, `start_frame`? |
| `anim_mixer_set_layer_weight` [E, 5.8+] | Set a layer's constant blend weight | `sequence_path`, `track_id`, `layer_index`, `weight` |
| `anim_mixer_remove_layer` [E, 5.8+] | Clear a layer (sections + child track) | `sequence_path`, `track_id`, `layer_index` |
| `anim_mixer_add_transition` [E, 5.8+] | Create a transition between two overlapping sections | `sequence_path`, `track_id`, `from_section`, `to_section`, `transition_class` (`crossfade`/`inertial_dead_blend`) |
| `anim_mixer_auto_create_transitions` [E, 5.8+] | Auto-create transitions for a section's row-neighbors | `sequence_path`, `track_id`, `section` |
| `anim_mixer_set_transition_blend` [E, 5.8+] | Type-aware blend config | `sequence_path`, `track_id`, `transition_id`, `blend_params_json` |
| `anim_mixer_change_transition_type` [E, 5.8+] | Swap transition concrete class | `sequence_path`, `track_id`, `transition_id`, `new_class` |
| `anim_mixer_get_section_easing` [E, 5.8+] | Read a section's effective ease-in/out durations (frames) | `sequence_path`, `track_id`, `section_name` |
| `anim_mixer_set_section_easing` [E, 5.8+] | Set manual ease-in/out durations; omit one side to leave it unchanged | `sequence_path`, `track_id`, `section_name`, `ease_in_frames`?, `ease_out_frames`? |
| `anim_mixer_get_condition` [E, 5.8+] | Read the condition on a section/track/row | `sequence_path`, `track_id`, `level`? (section/track/row), `section_name`?, `row_index`? |
| `anim_mixer_set_condition` [E, 5.8+] | Set a condition (UMovieSceneCondition subclass path) on a section/track/row | `sequence_path`, `track_id`, `condition_class`, `level`?, `section_name`?, `row_index`? |
| `anim_mixer_clear_condition` [E, 5.8+] | Remove the condition from a section/track/row | `sequence_path`, `track_id`, `level`?, `section_name`?, `row_index`? |
| `anim_mixer_set_node_expansion` [E] | Expand/collapse an outliner node (active Sequencer panel; no `sequence_path`) | `node_name` (or `node_path`), `expanded` |
| `anim_mixer_is_node_expanded` [E] | Query an outliner node's expansion state (active Sequencer panel) | `node_name` (or `node_path`) |
| `anim_mixer_get_layers` [E, 5.8+] | List layers on an AnimMixer track (index, name, section count) | `sequence_path`, `track_id` |
| `anim_mixer_get_sections` [E, 5.8+] | List sections on an AnimMixer track (FName, layer, frames, type) | `sequence_path`, `track_id`, `layer_index`? |
| `anim_mixer_get_transitions` [E, 5.8+] | List transitions on an AnimMixer track (FName, class, from/to section) | `sequence_path`, `track_id`, `section_name`? |
| `anim_mixer_create_blend_mask` [E, 5.8+] | Create and save a new UAF BlendMask without the editor UI; rejects existing destinations | `skeleton_path`, `asset_path`, `bone_weights` |
| `anim_mixer_reload_evaluate_blend_mask` [E, 5.8+] | Unload/reload an existing BlendMask at an exact path and verify weights, mask section, and evaluation cache | `asset_path`, `expected_overrides` |

#### Animation Mixer layer workflow (5.8+)

Authoring layers on a Sequencer Animation Mixer track (UE 5.8+; on 5.7 these ops
decline gracefully with `{"success": false, "error": "...requires UE 5.8+..."}`).
Create `MovieSceneAnimationMixerTrack` with `add_track` on the skeletal binding;
root mixers have no bound-object animation target. Omitted/empty `track_id` uses
`auto` and succeeds only when exactly one binding-owned mixer exists. Pass the
canonical binding GUID when multiple mixers exist. Explicit `"master"`/`"root"`
keeps legacy root access but returns `scope="root"` plus an evaluation warning.
`layer_index` is the layer's row index.

```python
ToolSearch("select:mcp__narshamcp__ue_sequencer_tracks")

# 1) Create the mixer on the SkeletalMesh binding. Reuse the returned canonical binding_id.
created = ue_sequencer_tracks(operation="add_track",
                    sequence_path="/Game/Cine/LS_Shot.LS_Shot",
                    track_type="MovieSceneAnimationMixerTrack",
                    binding_id="Manny", track_name="Manny Mixer")
binding_guid = created["binding_id"]

# 2) Add a "Base" layer and animation. Explicit binding GUID is stable even if more mixers are added later.
ue_sequencer_tracks(operation="anim_mixer_add_layer",
                    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id=binding_guid, layer_name="Base")
ue_sequencer_tracks(operation="anim_mixer_add_animation",
                    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id=binding_guid,
                    layer_index=0, anim_path="/Game/Anims/A_Idle.A_Idle", start_frame=0)

# 3) Add an overlay layer at quarter weight.
ue_sequencer_tracks(operation="anim_mixer_add_layer",
                    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id=binding_guid, layer_name="Overlay")
ue_sequencer_tracks(operation="anim_mixer_set_layer_weight",
                    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id=binding_guid, layer_index=1, weight=0.25)

# 4) Remove the overlay layer (clears its sections; note: layer slot may persist empty, higher rows not shifted).
ue_sequencer_tracks(operation="anim_mixer_remove_layer",
                    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id=binding_guid, layer_index=1)
```

> No `blend_mode` param — layer-level blending in 5.8 is a decoration (Mask), not an enum; future-phase territory.
> No `duration` on `anim_mixer_add_animation` — the section length comes from the AnimSequence.
> Discover these via `ue_sequencer_structure(operation="list_operations", category="anim_mixer")`.

#### Animation Mixer transition workflow (5.8+, Phase 2 / Issue #11753)

Author **transitions** between overlapping animation sections on the same AnimMixer
layer. Two transition classes ship in 5.8: `crossfade` (curve-blended) and
`inertial_dead_blend` (velocity-aware, EAlphaBlendOption mode). Blend params are
**type-aware** — `{"value": <float>}` for Crossfade, `{"mode": "<EAlphaBlendOption>"}`
for InertialDeadBlend.

```python
# Assumes two overlapping sections already exist on layer 0 (from add_animation calls above).
# 1) Create a crossfade between them.
add_resp = ue_sequencer_tracks(
    operation="anim_mixer_add_transition",
    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id="<bindingGuid>",
    from_section="MovieSceneSkeletalAnimationSection_0",
    to_section="MovieSceneSkeletalAnimationSection_1",
    transition_class="crossfade")
tid = add_resp["transition_id"]  # e.g. "MovieSceneAnimCrossfadeTransitionSection_0"

# 2) Tune the crossfade's blend curve default to 0.7.
ue_sequencer_tracks(
    operation="anim_mixer_set_transition_blend",
    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id="<bindingGuid>",
    transition_id=tid, blend_params_json='{"value":0.7}')

# 3) Swap to InertialDeadBlend (blend data is NOT preserved — re-apply below).
swap_resp = ue_sequencer_tracks(
    operation="anim_mixer_change_transition_type",
    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id="<bindingGuid>",
    transition_id=tid, new_class="inertial_dead_blend")
new_tid = swap_resp["transition_id"]

# 4) Set the InertialDeadBlend's BlendMode.
ue_sequencer_tracks(
    operation="anim_mixer_set_transition_blend",
    sequence_path="/Game/Cine/LS_Shot.LS_Shot", track_id="<bindingGuid>",
    transition_id=new_tid, blend_params_json='{"mode":"HermiteCubic"}')
```

> `transition_id` is the transition section's `FName` as returned by `add_transition`.
> `auto_create_transitions` snapshots `FindTransitionsForSection` before/after and diffs to populate `transitions_created[]` (the engine method returns `void`).
> `set_transition_blend` rejects type-mismatched params (e.g. `{"value":…}` on InertialDeadBlend) with a descriptive error.

### ue_sequencer_keyframes (18 ops) — Keyframe and curve operations

| Operation | Description | Key Params |
|-----------|-------------|------------|
| `smart` | Auto-detect intent | `sequence_path` |
| `add_keyframe` | Add keyframe | `sequence_path`, `track_id`, `frame_number`, `value` |
| `remove_keyframe` | Remove keyframe | `sequence_path`, `track_id`, `frame_number` |
| `modify_keyframe` | Modify keyframe | `sequence_path`, `track_id`, `frame_number`, `new_value`, `new_time` |
| `copy_keyframes` | Copy keyframes between tracks | `sequence_path`, `source_track_id`, `target_track_id`, `time_offset` |
| `bake_keyframes` | Resample Float/Double curves to linear keys | `sequence_path`, `track_id`, `frame_interval`, `start_frame?`, `end_frame?` |
| `get_keyframes` [오프라인] | Get keyframes for track (binary-first) | `sequence_path`, `track_id` |
| `list_keyframes` | **Alias** → `get_keyframes` (#19915 live residual) | same as `get_keyframes` |
| `get_curve_data` [오프라인] | Get curve data (binary Tier 3) | `sequence_path`, `track_id`, `include_tangents` |
| `clear_keyframes` [E] | Clear all keyframes | `sequence_path`, `track_id` |
| `get_all_keyframes_for_binding` [오프라인] | All keyframes for binding (binary-first) | `sequence_path`, `binding_id` |
| `set_playback_range` [E] | Set playback range | `sequence_path`, `start_frame`, `end_frame` |
| `export_sequence` [오프라인*] | Export sequence data (json/csv: offline binary serialize of real channel/key data — `frame,track,channel,value,tangent`; fbx: SequencerTools / Editor; usd: returns USDImporter guidance error) | `sequence_path`, `export_format` (json/csv/fbx/usd), `output_path` |
| `add_marker` [E] | Add marker | `sequence_path`, `frame_number`, `label` |
| `list_markers` [오프라인] | List all markers (binary-first) | `sequence_path` |

### ue_sequencer_playback (26 ops) — Playback, camera, spawnables, events

| Operation | Description | Key Params |
|-----------|-------------|------------|
| `smart` | Auto-detect intent | `sequence_path` |
| `play` | Play sequence | `sequence_path` |
| `stop` | Stop playback | `sequence_path` |
| `pause` | Pause playback | `sequence_path` |
| `scrub` | Scrub to frame | `sequence_path`, `frame_position` |
| `get_playback_status` | Get playback state (write-style: opens the sequence in the Sequencer window) | `sequence_path` |
| `set_play_mode` | Set play mode | `sequence_path`, `play_mode` (loop/once/ping_pong) |
| `set_playback_rate` | Set playback speed | `sequence_path`, `rate` |
| `add_camera_cut` | Add camera cut track | `sequence_path`, `start_frame`, `end_frame` |
| `set_camera_binding` | Set camera binding | `sequence_path`, `camera_binding_id` |
| `get_active_camera` [오프라인] | Get active camera at frame (binary-first) | `sequence_path`, `frame_number` |
| `add_spawnable` [E] | Add spawnable actor | `sequence_path`, `blueprint_class` or `actor_class` |
| `convert_to_spawnable` [E] | Convert possessable to spawnable | `sequence_path`, `actor_path` |
| `add_property_track` [E] | Add property track | `sequence_path`, `binding_id`, `property_path` |
| `list_animatable_properties` [E] | List animatable props | `sequence_path`, `binding_id` |
| `set_display_rate` [E] | Set display rate | `sequence_path`, `fps` |
| `set_tick_resolution` [E] | Set tick resolution | `sequence_path`, `tick_resolution` |
| `add_event_key` [E] | Add event keyframe | `sequence_path`, `track_id`, `frame_number`, `event_name` |
| `list_event_endpoints` [오프라인] | List event endpoints (binary-first) | `sequence_path` |
| `bind_event` [E] | Bind event to function | `sequence_path`, `event_name`, `function_name` |
| `add_material_parameter_track` | Add material param track | `sequence_path`, `collection_path`, `parameter_name` |
| `set_material_keyframe` | Set material keyframe | `sequence_path`, `track_id`, `frame_number` |

### Common Parameters (All 4 Tools)

| Parameter | Type | Description |
|-----------|------|-------------|
| `sequence_path` | string | LevelSequence asset path or name (e.g. `/Game/Cinematics/LS_Intro`) |
| `operation` | string | Operation name |
| `track_id` | string | Track identifier |
| `binding_id` | string | Actor binding identifier |
| `frame_number` | integer | Frame number |
| `project_root` | string | Project root (auto-detected) |

`[E]` = Requires Editor (Remote Control API)
`[오프라인]` = Works without Editor (binary parser / discovery meta). SSOT: `OFFLINE_OPERATIONS` in `sequencer_common.rs` (Issue #21938).
`[오프라인*]` = Partial offline (e.g. `export_sequence` json/csv offline; fbx still Editor).

## Full offline/editor op inventory (Issue #22997)

Total unique ops across the 4 sub-tools: **107** (structure 25 + tracks 47 + keyframes 18 + playback 26 raw; unique union 107 after `smart`/catalog de-dup; #19915 / #27002).

Ops previously omitted from this skill body (still in MCP schema):
- `audit_deep`
- `check_asset_references`
- `find_sequences_using_asset`
- `retime_sequence`
- `set_section_properties`
- `set_track_interpolation`
- `apply_shot_plan`
- `join_camera_rig`

Use `list_operations` / `list_available_operations` for the live catalog.
These catalog ops are accepted on **all four** sub-tools (`structure` /
`tracks` / `keyframes` / `playback`) as well as the parent dispatcher
(#19915 residual SSOT).

**Caller aliases** (same handler; do not double-count toward 102 unique ops):
- `get_sequence_info` → `get_structure`
- `list_keyframes` → `get_keyframes`
