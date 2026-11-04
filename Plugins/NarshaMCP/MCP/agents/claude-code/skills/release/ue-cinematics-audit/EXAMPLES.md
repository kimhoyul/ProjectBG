# UE Cinematics Audit Examples

These examples exercise the reporting and routing contracts from [SKILL.md](SKILL.md)
and [REFERENCE.md](REFERENCE.md). Counts are illustrative unless a section explicitly
identifies captured evidence.

## 1. Whole-project audit with complete coverage

User:

```text
/ue-cinematics-audit --top 5
```

Expected behavior:

1. Discover all Level Sequences once with `list_sequences`.
2. Run one project-wide `sequence_audit` baseline.
3. Fan out `validate_settings`, `list_bindings`, and `list_tracks` with
   `use_editor=false`, in batches of no more than five sequences.
4. Call `get_keyframes` only for section-bearing tracks in the exact allowlist.
5. Score the complete finding set, then display at most five priority findings.

Example report shape:

```markdown
## Cinematics Health Audit — whole project

**Coverage: complete — 23/23 reads, 5/5 sequences**
**Health: 80/100 — ATTENTION**

| sequence | score | errors | warnings | info | coverage |
|---|---:|---:|---:|---:|---|
| SEQ_LobbyScreen | 80 | 0 | 2 | 0 | complete |

### Priority findings (top 5)
| # | severity | signal | sequence / target | evidence | recommendation |
|---:|---|---|---|---|---|
| 1 | warning | BINDING_EMPTY | SEQ_LobbyScreen / LightRig | `track_indices=[]` | Remove the unused binding or attach an intended track. |
| 2 | warning | KEYLESS_TRACK | SEQ_LobbyScreen / FocusDistance | matching curve channel counts sum to 0 | Add keys or remove the empty channel track. |

### Coverage failures
| sequence | operation | error | next action |
|---|---|---|---|
```

The score is `100 - 10 - 10 = 80`. Informational findings do not reduce it.

## 2. Successful envelope with incomplete baseline

Assume inventory finds five sequences, but `sequence_audit` returns:

```json
{
  "success": true,
  "sequences_analyzed": 0,
  "warnings": []
}
```

This is not a clean baseline. Preserve the empty warnings list, record the analyzed-count
gap as a failed required read, continue the per-sequence offline reads, and emit:

```markdown
**Coverage: partial — 21/22 reads, 5/5 sequences**
**Health: 100/100 — PARTIAL**

### Coverage failures
| sequence | operation | error | next action |
|---|---|---|---|
| project | sequence_audit | analyzed 0 of 5 selected sequences | Rebuild/check metadata and rerun the audit. |
```

The numeric score may describe collected findings, but `PARTIAL` must remain the verdict.

## 3. Inventory truncation

User:

```text
/ue-cinematics-audit Lobby --top 20
```

If `list_sequences.truncated=true`, audit the returned rows, state the server limit and
`total_matching`, and keep the final verdict `PARTIAL`. Recommend narrower, non-overlapping
pattern shards. Do not claim that the returned subset represents the whole project.

## 4. Ambiguous bare asset names

Current inventory responses may include `name` and absolute `file_path` but no Unreal mount
`path`. If two discovered assets share the same `name`, do not pass either absolute
`.uasset` path to detail operations and do not guess a `/Game/...` mount. Record all
unresolved detail reads as coverage failures and suggest `/ue-asset-discovery` to establish
an accepted mount path.

## 5. Safe keylessness inference

These two tracks are not equivalent:

```json
[
  {
    "name": "CameraCutTrack",
    "type": "MovieSceneCameraCutTrack",
    "section_indices": [0]
  },
  {
    "name": "FocusDistance",
    "type": "MovieSceneFloatTrack",
    "section_indices": [1]
  }
]
```

Skip `get_keyframes` for `CameraCutTrack`. Query `FocusDistance`; emit `KEYLESS_TRACK` only
when its response has `success=true`, contains a curve whose `track.name` is
`FocusDistance`, and that curve's numeric channel `key_count` values sum to zero. Ignore
top-level `total_keys` (or a phantom `key_count`), which is not a per-track zero/non-zero signal on the offline
binary path. A failed response, missing matching curve, or malformed count affects coverage
and must not become a health finding.

## 6. Routing and composition

Prompts such as these route to this audit skill:

```text
Audit every level sequence and rank the worst cinematics problems.
프로젝트 시퀀서 건강도를 점수로 보여줘.
```

Editing prompts do not route here:

```text
Add a camera cut at frame 120.
이 시퀀스에 키프레임을 추가해줘.
```

Use `/ue-sequencer-cinematics` for those authoring operations. Use `/ue-audit` for broader
project systems health and `/ue-asset-discovery` for unresolved sequence paths.

## 7. Captured offline capability evidence

The verification fixture in `_verification/op_responses.json` was captured against a real
Lyra project with the Editor disconnected. It demonstrates successful envelopes for all
six required operations. Its keyframe response demonstrates why the top-level count is
unsafe: it reports 31 keys while the filtered requested-track curves sum to 9. It also demonstrates the
important partial-coverage case where `sequence_audit` returns success while analyzing
zero of five discovered sequences. Treat the fixture as capability evidence, not as a
permanent golden health result for Lyra.
