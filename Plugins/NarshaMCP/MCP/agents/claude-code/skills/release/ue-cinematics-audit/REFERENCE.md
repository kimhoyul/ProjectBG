# UE Cinematics Audit Reference

Field provenance and deterministic aggregation rules for [SKILL.md](SKILL.md). Issue #19930, parent Epic #19915.

## 1. Operation and field provenance

| phase | tool / operation | fields consumed | source behavior |
|---|---|---|---|
| inventory | `ue_sequencer_structure(list_sequences)` | `success`, `sequences[].{name,path,file_path}`, `total_matching`, `truncated`, `limit`, `source` | `sequencer_discovery.rs`; current responses expose `name` + `file_path`; use a future mount `path` when present, otherwise the unique `name`; hard result cap 50 |
| baseline | `ue_sequencer_tracks(sequence_audit)` | `sequences_analyzed`, `warnings[].{sequence,issue,severity}`, `warning_count`, `summary`, `thresholds_used` | `sequencer_advanced.rs`; scans the whole `level_sequences` metadata category once |
| validate | `ue_sequencer_structure(validate_settings,use_editor=false)` | `success`, `method`, `valid`, `issues[].{severity,lint,message}`, `total_issues`, `settings` | `sequencer_search.rs`; binary-first; Rule-20 offline lints |
| bindings | `ue_sequencer_structure(list_bindings,use_editor=false)` | `success`, `method`, `bindings[].{name,type,possessed_class,spawned_actor_class,track_indices,parent_guid}`, `count` | `sequencer_ops.rs`; binary parser response |
| tracks | `ue_sequencer_tracks(list_tracks,use_editor=false)` | `success`, `method`, `tracks[].{name,type,section_indices}`, `count` | `sequencer_ops.rs`; binary parser response |
| keys | `ue_sequencer_keyframes(get_curve_data,track_id="all",use_editor=false)` | `success`, `method`, `track_id`, `curves[].{track,channels[].key_count}`, `total_channels`, `total_keys`, `truncated`, `offset`, `include_tangents`, `note` | bulk offline curves (`sequencer_curves.rs` json!); filter `curves[]` by track name for KEYLESS — do **not** treat top-level `total_keys` as per-track (and never a phantom top-level `key_count`; that is `get_keyframes`) |

All six operations are read entries in `schema_registry/op_read_write.rs`. `sequence_audit` ignores `sequence_path` and is intentionally project-wide.

`file_path` is provenance, not an input for the offline detail reads. The resolver accepts
`/Game/...`, plugin mount paths, or a unique bare asset name. Passing the absolute `.uasset`
path from current inventory responses fails closed; therefore use `path` when available and
fall back to `name`. A duplicate bare name must remain a coverage failure rather than being
resolved by guessing.

## 2. Native validation signals

`validate_settings(use_editor=false)` currently emits these `lint` values. Preserve the handler's severity and message; do not reinterpret them:

| lint | meaning |
|---|---|
| `low_display_rate` | display FPS below 24 |
| `low_tick_ratio` | fewer than 100 ticks per frame |
| `empty_playback_range` | playback end is not after start; native severity error |
| `binding_no_tracks` | referenced binding has no tracks |
| `orphan_binding` | binding is unreferenced or has a dangling parent GUID |
| `missing_subsequence` | sub-sequence/shot has no resolved asset |
| `non_integer_tick_ratio` | tick resolution is not an integer multiple of display rate |

The global `sequence_audit` adds metadata-level warnings such as `high_binding_count`, `zero_duration`, `very_long_sequence`, and `empty_sequence`. Use the response's `issue` value as the signal suffix; do not maintain a second exhaustive copy. Its native severity vocabulary is `high|medium|low|warning`, so normalize `high|medium|warning` to audit warning, `low` to audit info, and missing/unknown values to warning. These metadata heuristics never become hard errors.

## 3. Derived signals

| signal | severity | mechanically checkable condition |
|---|---|---|
| `BINDING_EMPTY` | warning | offline binding has empty/missing `track_indices`, unless native `binding_no_tracks`/`orphan_binding` already covers the same binding |
| `MISSING_CAMERA_CUT` | warning | no `MovieSceneCameraCutTrack` and either a binding name/class contains `camera` or `--require-camera-cut` is set |
| `MISSING_CAMERA_CUT` | info | no CameraCutTrack and no camera evidence; a utility/non-cinematic Level Sequence may legitimately have none |
| `KEYLESS_TRACK` | warning | eligible track has ≥1 `section_indices`; bulk `get_curve_data(all)` succeeds; ≥1 curve with `track.name ==` that track; every matching channel has numeric `key_count`; sum of those counts is zero |

Never derive a finding from an error envelope. A failed call changes coverage only.

### KEYLESS_TRACK (bulk get_curve_data, Issue #21965)

After a successful `get_curve_data(track_id="all")` (one call per sequence; **no** per-track `get_keyframes` fan-out):

1. For each eligible key-bearing track (allowlist + non-empty `section_indices`), keep only bulk `curves[]` whose `track.name` equals that track `name`.
2. Require ≥1 matching curve and numeric `channels[].key_count` values.
3. `filtered_curve_key_count = sum(channels[].key_count)` across those curves.
4. Emit `KEYLESS_TRACK` only when that count is **zero**.
5. **Never** use sequence-wide top-level `total_keys` (or a phantom `key_count`) for per-track zero/non-zero — always filter `curves[]` by track name first.
6. Missing matching curves or malformed channel counts for an eligible track → coverage failure, not empty-track evidence.

Section-only tracks (camera-cut, sub-sequence, skeletal-animation, audio, event, attach, visibility, …) stay out of the eligible set.

### Key-bearing track allowlist

The offline curve extractor has meaningful zero-key semantics only for channel-backed track families. Match exact `tracks[].type` values:

- `MovieScene3DTransformTrack`
- `MovieSceneFloatTrack`
- `MovieSceneBoolTrack`
- `MovieSceneByteTrack`
- `MovieSceneIntegerTrack`
- `MovieSceneColorTrack`
- `MovieSceneFloatVectorTrack`
- `MovieSceneDoubleVectorTrack`

Do not infer keylessness for CameraCut, Sub, Shot, SkeletalAnimation, Audio, Event, Attach, Visibility, Spawn, ControlRig, or unknown track types. Those can be section/default driven or use specialized payloads even when the generic curve extractor returns zero keys.

## 4. Deduplication

Normalize every finding to:

```text
(sequence_path, signal_id, target, severity, message, source_operation)
```

- Native validation: `signal_id = VAL::<lint>`.
- Global baseline: `signal_id = AUD::<issue>`.
- Derived: stable IDs from section 3.
- Deduplicate on `(sequence_path, signal_id, target)` before counting.
- Prefer native validation over `BINDING_EMPTY` for the same binding because it has stronger GUID/reference context.

## 5. Scoring

Per sequence:

```text
score = max(0, 100 - 25 * error_count - 10 * warning_count)
```

Info findings do not subtract. Project score is the arithmetic mean of every selected sequence score; round to one decimal place only for display.

| complete numeric score | verdict |
|---:|---|
| 90–100 | `HEALTHY` |
| 70–89.9 | `ATTENTION` |
| below 70 | `UNHEALTHY` |

Coverage overrides the numeric verdict:

- zero selected sequences → score `N/A`, verdict `N/A`;
- inventory failure → no score, verdict `UNKNOWN`;
- inventory truncation or any failed required read → retain the observed score for context but verdict `PARTIAL`, never `HEALTHY`;
- a failed baseline `sequence_audit` is one failed required read. A nominally successful
  baseline with `sequences_analyzed < selected sequence count` is also a coverage failure:
  the metadata category may be empty or incomplete even though filesystem discovery found
  Level Sequences. Continue per-sequence collection because it can still produce useful evidence.

## 6. Coverage accounting

Required reads are:

```text
1 inventory + 1 global baseline
+ 4 per selected sequence (validate_settings + list_bindings + list_tracks + get_curve_data(all))
```

Count a normal per-sequence read successful only when its top-level `success` is `true`.
Count the global baseline successful only when `success == true` and
`sequences_analyzed >= selected sequence count`; this is a necessary lower-bound check,
not proof that the same names were covered because the current response does not return
the full audited-name set. Report:

- `successful_reads / required_reads`;
- `scanned_sequences / total_matching`;
- each failure's sequence, operation, `error`, and `hint`;
- inventory `source`, `limit`, and `truncated`.

`--top N` never changes required reads or scores. An input pattern intentionally narrows the denominator and must appear in the report title. If the 50-result inventory cap is hit, rerun with disjoint patterns; do not silently combine shards with overlapping sequence paths.

## 7. Recommendations

| signal family | recommendation |
|---|---|
| `VAL::empty_playback_range` | set a non-empty playback range before authoring/rendering |
| `VAL::orphan_binding` / `VAL::binding_no_tracks` / `BINDING_EMPTY` | inspect bindings and remove/rebind the empty reference |
| `VAL::missing_subsequence` | restore or replace the unresolved shot/sub-sequence asset |
| `MISSING_CAMERA_CUT` | add/verify a CameraCutTrack only when the sequence is intended to drive a camera |
| `KEYLESS_TRACK` | inspect the channel/default value; add keys or remove the unused track |
| `AUD::high_binding_count` | split or simplify the sequence after confirming the threshold is appropriate |
| `AUD::empty_sequence` / `AUD::zero_duration` | remove the placeholder or establish a valid playback/section range |

Recommendations are guidance, not new findings. Keep the source signal and concrete target visible.
