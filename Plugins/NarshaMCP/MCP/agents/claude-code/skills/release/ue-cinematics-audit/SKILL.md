---
name: ue-cinematics-audit
model: opus
description: "Purpose: Project-wide Level Sequence health audit. Aggregates offline Sequencer validation, bindings, tracks, and keyframe evidence into a scored, prioritized report with fail-closed coverage. Triggers: '시네마틱 감사', '시퀀서 건강도', '레벨 시퀀스 일괄 점검', 'cinematics audit', 'sequencer health report', 'audit all level sequences'. For editing camera cuts, tracks, or keyframes use ue-sequencer-cinematics."
compatibility: "Requires NarshaMCP MCP server with the Sequencer structure, tracks, and keyframes tools"
argument-hint: "optional sequence name/path filter, --top N (default 10), --require-camera-cut"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "19930"
audience: end-user
---

# UE Cinematics Audit

Audit all selected Level Sequences with existing read-only Sequencer operations. Run automatically when loaded; do not merely explain the workflow.

Read [REFERENCE.md](REFERENCE.md) before aggregating. It defines the exact response fields, signal rules, scoring, and coverage contract. Use [EXAMPLES.md](EXAMPLES.md) to keep healthy, partial-coverage, and routing behavior consistent.

## Inputs

- `$ARGUMENTS` without flags: optional case-insensitive sequence name/path pattern. Empty means the whole indexed project.
- `--top N`: number of findings to show, default 10. This limits display only, never collection.
- `--require-camera-cut`: promote missing camera-cut coverage from informational to warning even when no camera binding is detectable.

All operations are read-only. Pass `operation=` and `use_editor=false` explicitly; do not use smart routing or connect the Editor as a silent fallback.

## Workflow

### Phase 0 — Discover and establish coverage

```python
ToolSearch("select:mcp__narshamcp__ue_sequencer_structure")
ToolSearch("select:mcp__narshamcp__ue_sequencer_tracks")
ToolSearch("select:mcp__narshamcp__ue_sequencer_keyframes")

inventory = ue_sequencer_structure(
    operation="list_sequences",
    pattern="<optional name/path filter>",
)
```

- If `inventory.success != true`, stop and report `UNKNOWN`; there is no valid denominator.
- Use `inventory.sequences[].path` as `sequence_path` when present; otherwise use the
  unique `name`. Keep `file_path` as evidence only: the offline resolver does not accept
  an absolute `.uasset` filesystem path. If a name is ambiguous, record the failed reads
  as partial coverage instead of guessing a mount path.
- Record `len(sequences)`, `total_matching`, `truncated`, `limit`, and `source`.
- `truncated=true` means the audit is `PARTIAL`. Never label the project HEALTHY. Ask for narrower pattern shards and preserve each shard's coverage.
- Zero selected sequences is `N/A`, not 100/100.

### Phase 1 — Run the project-wide baseline once

```python
baseline = ue_sequencer_tracks(
    operation="sequence_audit",
    threshold_bindings=20,
)
```

`sequence_audit` already scans the project metadata category. Call it exactly once, not once per sequence. Index its `warnings[]` by sequence name for Phase 3.

- If `baseline.success != true`, record one failed required read and continue the
  per-sequence reads with `PARTIAL` coverage.
- If `baseline.sequences_analyzed < len(sequences)`, also treat the baseline as a
  coverage failure. A successful envelope with zero metadata rows is not proof that
  discovered sequences have no warnings. Preserve any returned warnings, but the final
  verdict remains `PARTIAL`.

### Phase 2 — Fan out offline reads

Process at most five sequences concurrently. For each selected `sequence_path`, issue these three independent reads in parallel:

```python
settings = ue_sequencer_structure(
    operation="validate_settings",
    sequence_path=sequence_path,
    use_editor=False,
)
bindings = ue_sequencer_structure(
    operation="list_bindings",
    sequence_path=sequence_path,
    use_editor=False,
)
tracks = ue_sequencer_tracks(
    operation="list_tracks",
    sequence_path=sequence_path,
    use_editor=False,
)
```

For every successful `tracks` result, select only tracks whose `type` is in the key-bearing allowlist in REFERENCE.md and whose `section_indices` is non-empty (eligibility filter for Phase 3 `KEYLESS_TRACK`). **Do not fan out per-track keyframe reads.**

Issue #21965: fetch all track curves in **one offline call** instead of M× `get_keyframes`:

```python
curves = ue_sequencer_keyframes(
    operation="get_curve_data",
    sequence_path=sequence_path,
    track_id="all",
    use_editor=False,
)
```

Bulk curves: one offline `get_curve_data(all)` per sequence (REFERENCE § KEYLESS_TRACK). No per-track `get_keyframes`. Section-only tracks stay ineligible for `KEYLESS_TRACK`. Failed bulk read = sequence coverage miss, not keyless proof.

### Phase 3 — Normalize findings

For each sequence:

1. Preserve every `validate_settings.issues[]` severity and `lint` as `VAL::<lint>`.
2. Add matching global `sequence_audit.warnings[]` as `AUD::<issue>`; normalize `high`, `medium`, and `warning` to warning, `low` to info, and an absent/unknown severity to warning.
3. Derive `BINDING_EMPTY` when a binding's `track_indices` is empty, unless `binding_no_tracks` or `orphan_binding` already describes that binding.
4. Derive `MISSING_CAMERA_CUT` when no track has `type == "MovieSceneCameraCutTrack"`:
   - warning when a binding name/class contains `camera`, or `--require-camera-cut` is set;
   - info otherwise.
5. After bulk `get_curve_data(all)` succeeds, derive `KEYLESS_TRACK` per eligible
   track using the track-name-filtered key-count rules in REFERENCE § KEYLESS_TRACK
   (never sequence-wide top-level `total_keys` / phantom `key_count`).
6. Deduplicate by `(sequence_path, signal_id, target)` before scoring.

Apply the scoring and coverage rules from REFERENCE.md. Failed required reads do not create health findings, but they force the overall verdict to `PARTIAL` and appear in the failures table.

### Phase 4 — Output Format

Sort findings by severity (`error`, `warning`, `info`), then sequence path and signal ID. Display only the first `--top N`, while totals and scoring use the complete collected set.

```markdown
## Cinematics Health Audit — {project/filter}

**Coverage: {complete|partial} — {successful}/{required} reads, {scanned}/{total_matching} sequences**
**Health: {score|N/A}/100 — {HEALTHY|ATTENTION|UNHEALTHY|PARTIAL|N/A}**

| sequence | score | errors | warnings | info | coverage |
|---|---:|---:|---:|---:|---|

### Priority findings (top N)
| # | severity | signal | sequence / target | evidence | recommendation |
|---:|---|---|---|---|---|

### Coverage failures
| sequence | operation | error | next action |
|---|---|---|---|
```

Always include coverage failures and truncation even when there are no health findings.

## Error Recovery

| Condition | Action |
|---|---|
| `list_sequences` fails | Stop with `UNKNOWN`; check project metadata/index health. |
| `list_sequences.truncated=true` | Report `PARTIAL`; rerun with narrower pattern shards. |
| `sequence_audit` succeeds but covers fewer rows than selected sequences | Preserve returned warnings, record the baseline coverage gap, verdict `PARTIAL`. |
| One sequence fails binary parsing | Continue other sequences, record all three failed/missing reads, verdict `PARTIAL`. |
| `get_curve_data(all)` fails / malformed per-track curves | Coverage miss for affected tracks; no `KEYLESS_TRACK` (REFERENCE § KEYLESS_TRACK). |
| Editor is disconnected | Continue unchanged. Every required operation is explicitly offline. |
| No sequences match | Report `N/A` with the filter and inventory source. |

## Composition

- Use `/ue-sequencer-cinematics` to repair a finding or author tracks, keys, playback, and camera cuts.
- Use `/ue-audit` for whole-project systems health; this skill covers only Level Sequences.
- Use `/ue-asset-discovery` when the metadata index is missing or a sequence path cannot be resolved.
