---
name: animation-workflow
model: opus
description: "Purpose: Offline-first UE animation via ue_manage_animation. Inspect montages/blendspaces/notifies offline; reparse-verify authoring; runtime play_montage. Triggers: AnimBP, montage, notify."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+). Offline reads need no Editor; authoring/play require Editor + Remote Control."
allowed-tools:
  - Bash
  - mcp__narshamcp__ue_manage_animation
  - mcp__narshamcp__ue_search_assets
  - mcp__narshamcp__ue_manage_blueprint
  - mcp__narshamcp__ue_analyze_symbols
  - mcp__narshamcp__ue_editor_automation
  - mcp__narshamcp__ue_editor_actors
  - mcp__narshamcp__ue_editor_assets
  - mcp__narshamcp__ue_editor_debug
metadata:
  version: "2.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  origin_issue: 21940
  expertise_anchor:
    source: docs/engine/ue5_7/modules/Runtime/Engine/CLASSES.md
    sections:
      - USkeletalMeshComponent
      - ACharacter
      - APawn
    inject_when: trigger_match
    max_chars: 6000
    note: "Sections chosen from animation-adjacent classes that exist as H2 headings in the source."
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Animation Workflow Skill

**Status**: Production (offline-first rewrite — Issue #21940)
**Primary tool**: `ue_manage_animation` (33 ops — see `docs/reference/tools/ue_manage_animation.md`)

---

## Purpose

Drive UE animation work **offline-first** through `ue_manage_animation`:

| Layer | Path | Editor |
|-------|------|--------|
| **Read** (12 ops + smart/list_operations) | Binary-parsed montage / blendspace / sequence / sync-marker metadata | **No** |
| **Author** (15+ ops) | Editor RC mutations with **reparse-verify** (default `verify: true`) | **Yes** |
| **AnimBP graph** | `ue_manage_blueprint` list_animgraphs / get_animgraph | **No** |
| **Runtime play** | `ue_editor_automation` only when user asks to *play* in PIE | **Yes** |

Do **not** use `execute_python` for notify lists, montage structure, or blendspace inspection.
Those are first-class offline ops on `ue_manage_animation`.

---

## Hybrid Mode Support

| Mode | Tools | Editor | Use case |
|------|-------|--------|----------|
| **Offline (default)** | `ue_manage_animation` + `ue_search_assets` + `ue_manage_blueprint` + `ue_analyze_symbols` | No | Inspect montages, sequences, blendspaces, AnimGraph, C++ |
| **Author** | `ue_manage_animation` write ops (`add_notify`, `add_section`, …) | Yes | Safe mutations with reparse-verify |
| **Runtime** | `ue_editor_automation` | Yes | Live montage play / PIE control only |

**Editor Required**: **Partial** — offline surface is complete for read; authoring and play need the Editor.

---

## Trigger Phrases

### Korean
- "AnimBP 분석", "애님BP 구조", "애니메이션 블루프린트"
- "몽타주 구조", "몽타주 재생", "몽타주 정지"
- "Anim Notify 추가", "노티파이 목록", "노티파이 관리"
- "블렌드스페이스 분석", "BlendSpace 구조"
- "AnimGraph 분석", "애님그래프", "애니메이션 검색"

### English
- "analyze AnimBP", "AnimBP structure", "animation blueprint"
- "montage structure", "play montage", "list montage notifies"
- "add anim notify", "blendspace analysis"
- "AnimGraph analysis", "search animations", "offline montage"

---

## Auto-Execution Instructions

> When this skill loads, **execute** the workflow — do not only dump docs.

1. Parse target asset name/path from `$ARGUMENTS` / user text (`AM_*`, `AS_*`, `BS_*`, `ABP_*`).
2. Prefer **offline** `ue_manage_animation` for reads.
3. Use authoring ops only when the user asks to mutate; keep `verify: true`.
4. Use `execute_python` **only** for runtime play or when no offline/author op exists.

```python
ToolSearch("select:mcp__narshamcp__ue_manage_animation")
```

---

## Quick Workflows

### 1. Discover animation assets (offline)

```python
ue_manage_animation(operation="smart", query="AM_Attack")  # prefix-routes AM_/BS_/AS_
ue_manage_animation(operation="search_montages", query="AM_*", limit=20)
ue_manage_animation(operation="search_blendspaces", query="BS_*")
ue_manage_animation(operation="search_animsequences", query="AS_*")
# Fallback broad search
ue_search_assets(asset_type="animation", query="AM_*", limit=20)
```

### 2. Montage structure + notifies (offline — NOT execute_python)

```python
ue_manage_animation(operation="get_montage_structure", asset_path="/Game/Anim/AM_Attack")
# sections, slot tracks, BlendIn/BlendOut

ue_manage_animation(operation="get_montage_notifies", asset_path="/Game/Anim/AM_Attack")
# notify + notify-state entries by track (+ track_index_source)

ue_manage_animation(operation="get_sync_markers", asset_path="/Game/Anim/AS_Walk")
```

### 3. BlendSpace / sequence inspect (offline)

```python
ue_manage_animation(operation="get_blendspace_structure", asset_path="/Game/Anim/BS_IdleRun")
ue_manage_animation(operation="get_blendspace_parameters", asset_path="/Game/Anim/BS_IdleRun")
ue_manage_animation(operation="get_animsequence_structure", asset_path="/Game/Anim/AS_Walk")
```

### 4. AnimBP / AnimGraph (offline via blueprint tool)

```python
ue_manage_blueprint(operation="list_animgraphs", pattern="ABP_*")
ue_manage_blueprint(operation="get_animgraph", blueprint_name="ABP_Player", detail_level="standard")
```

### 5. Authoring with reparse-verify (Editor required)

```python
# Prefer verified write ops over raw Editor Python mutation
ue_manage_animation(
    operation="add_notify",
    asset_path="/Game/Anim/AM_Attack",
    track_name="Default",
    notify_name="AnimNotify_Hit",
    trigger_time=0.25,
    verify=True,  # default true — binary re-parse cross-check
)
ue_manage_animation(operation="add_section", asset_path="/Game/Anim/AM_Attack", section_name="Combo2", start_time=1.0)
ue_manage_animation(operation="add_sample", asset_path="/Game/Anim/BS_IdleRun", animation="/Game/Anim/AS_Run", axis_values=[1.0, 0.0])
```

On Editor OFF, authoring returns `editor_required: true` plus a `phase_1_alternative` read op — fall back to offline inspect and report clearly.

### 6. Play montage (runtime only — Editor + PIE)

Reserve `ue_editor_automation(execute_python)` for **playback** and other non-inspect actions. Do not use it to list notifies or dump montage structure.

### 7. C++ AnimInstance lookup (offline)

```python
ue_analyze_symbols(operation="search_symbols", query="*AnimInstance*", limit=10)
ue_analyze_symbols(operation="get_hierarchy", class_name="ULCAnimInstance", direction="up")
```

---

## Operation Map (ue_manage_animation)

Authoritative list: `list_operations` / `animation::ALLOWED_OPS`. Summary:

| Category | Ops (examples) | Editor |
|----------|----------------|--------|
| BlendSpace read | `search_blendspaces`, `get_blendspace_structure`, `get_blendspace_parameters` | No |
| Montage read | `search_montages`, `get_montage_structure`, `get_montage_notifies` | No |
| Sequence read | `search_animsequences`, `get_animsequence_structure` | No |
| Sync markers | `get_sync_markers`, `is_valid_sync_marker_name` | No |
| Meta | `smart`, `list_operations` | No |
| BlendSpace author | `add_sample`, `remove_sample`, `move_sample`, `set_sample_animation`, `set_axis_range`, `set_interpolation`, `set_parameter` | Yes + verify |
| Montage author | `add_section`, `remove_section`, `set_section_link`, `move_section`, `add_notify`, `add_notify_state`, `remove_notify`, `set_blend_in_out` | Yes + verify |

Full field tables: [ue_manage_animation.md](../../../docs/reference/tools/ue_manage_animation.md).

---

## Supported Asset Types

| Type | Prefix hint | Tool surface |
|------|-------------|--------------|
| AnimMontage | `AM_` | `ue_manage_animation` montage ops |
| AnimSequence | `AS_` | sequence + sync-marker ops |
| BlendSpace / AimOffset | `BS_` / `AO_` | blendspace ops |
| AnimBlueprint | `ABP_` | `ue_manage_blueprint` animgraph ops |

---

## Common Failures

| Error | Cause | Fix |
|-------|-------|-----|
| Asset not found | Wrong path | `search_*` / `ue_search_assets` then retry |
| `editor_required: true` | Authoring without Editor | Start Editor + RC, or stay on offline reads |
| `verify_failed` / reparse mismatch | Write did not land on disk | Inspect `reparse_verify.mismatches`; do not claim success |
| Skeleton mismatch on play | Montage vs mesh skeleton | Offline-inspect both; fix assignment before play |
| AnimBP not found | Wrong blueprint name | `list_animgraphs` / search |

---



### Runtime play helper (Editor + PIE)

```python
def play_montage(character_name: str, montage_path: str, play_rate: float = 1.0):
    """Play an AnimMontage on a level actor via Editor Python (runtime only)."""
    # Prefer offline inspect first: ue_manage_animation(get_montage_structure/get_montage_notifies)
    ue_editor_automation(operation="execute_python", code=f"""
import unreal
actors = unreal.EditorLevelLibrary.get_all_level_actors()
char = next((a for a in actors if a.get_actor_label() == '{character_name}'), None)
mesh = char.get_component_by_class(unreal.SkeletalMeshComponent)
anim = mesh.get_anim_instance()
mont = unreal.load_asset('{montage_path}')
print(anim.montage_play(mont, {play_rate}))
""")
```

## Related Skills

| Skill | Purpose |
|-------|---------|
| [ue-ik-rig-analysis](../ue-ik-rig-analysis/SKILL.md) | IK Rig / retargeter offline analysis |
| [control-rig-workflow](../control-rig-workflow/SKILL.md) | Control Rig audit |
| [blueprint-flow](../blueprint-flow/SKILL.md) | Non-AnimBP graph flow |

**Tool doc**: [ue_manage_animation.md](../../../docs/reference/tools/ue_manage_animation.md)

---

## Performance / Capability

| Metric | Value |
|--------|-------|
| Offline read path | Default for inspect |
| Editor Required | Partial (author + play only) |
| Authoring safety | reparse-verify default on |
| Primary tool mentions | `ue_manage_animation` required in this skill |

**Last Updated**: 2026-07-25 (Issue #21940 offline-first rewrite)
