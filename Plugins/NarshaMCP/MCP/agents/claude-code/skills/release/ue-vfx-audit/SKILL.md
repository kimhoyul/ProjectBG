---
name: ue-vfx-audit
model: opus
description: "Purpose: Project-wide Niagara VFX health audit. Aggregates offline validate + performance_audit + promoted reads (execution_flow/module_details/asset_consumers/measured_cost) + optional compare_systems into a scored report. Triggers: 'VFX 감사', '나이아가라 감사', 'NS 일괄 점검', 'vfx audit', 'niagara health report'. For single-system explain use ue-explain; multi-domain use ue-audit."
compatibility: "Requires NarshaMCP with ue_manage_niagara (search_systems, validate, performance_audit, get_execution_flow, get_module_details, find_asset_consumers, measured_cost, compare_systems)"
argument-hint: "optional system name/path pattern, --top N (default 10), --compare-pairs M (default 0), --errors-only"
allowed-tools: [mcp__narshamcp__ue_manage_niagara]
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  issue: "23013"
audience: end-user
---

# UE VFX Audit — Niagara System Health Rollup

**Issue**: #19976 / **#23013** (Parent Epic #19965)
**Purpose**: Offline `ue_manage_niagara` rollup — inventory → validate + performance_audit fan-out → **promoted reads** on offenders (`get_execution_flow`, `get_module_details`, `find_asset_consumers`, optional `measured_cost`) → optional `compare_systems` → scored report.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, execute the audit automatically via `ue_manage_niagara`. Do **not** only print docs.

1. **Target**: `$ARGUMENTS` (name/path pattern; empty = project-wide). Flags: `--top N` (default 10), `--compare-pairs M` (default 0), `--errors-only`.
2. Run the phases below offline (`use_editor=false` when the op accepts it).
3. Emit the structured health report.

---

## Auto-Trigger Phrases

Routing SSOT: `routing.json`.
- **KO**: VFX 감사 · 나이아가라 감사 · NS 일괄 점검 · 나이아가라 건강도 · 파티클 시스템 audit
- **EN**: vfx audit · niagara audit · audit all niagara systems · niagara health report · particle system health

Disambiguation:
- Single system structure/parameters (`NS_X 설명해`, `how does NS_Fire work`) → **`/ue-explain`**
- Whole-project multi-domain (`프로젝트 감사`, `system health`) → **`/ue-audit`**
- Runtime GPU/CPU `.utrace` → **`/ue-perf-audit`** / **`/ue-insights-profiler`**

---

## Workflow

Always pass `operation=` explicitly (no smart-only reliance for audit ops).

### Phase 0 — Discover inventory

```python
ToolSearch("select:mcp__narshamcp__ue_manage_niagara")

inv = ue_manage_niagara(
    operation="search_systems",
    params={"query": "<pattern or *>", "limit": 200},
)
```

- If `success != true`, stop with `UNKNOWN` (no denominator).
- Collect system names from `results[]` / `systems[]` (prefer `name` then `asset_name`).
- If `total_count > len(results)` or `truncated == true`, re-call with a higher
  `limit` or mark coverage `PARTIAL`.
- Zero systems → `N/A` (not 100/100).

### Phase 1 — Fan-out per-system baseline (≤5 concurrent)

For each selected system:

```python
val = ue_manage_niagara(operation="validate", params={"system_name": name})
perf = ue_manage_niagara(
    operation="performance_audit",
    params={
        # performance_audit selects its per-system path by asset_name.
        "asset_name": name,
        # optional per-system budget overrides:
        # "budget_gpu_particles": ...,
        # "budget_sim_stage_iterations": ...,
        # "budget_sub_uv_frames": ...,
    },
)
```

Record per system:
- validate: `is_valid`, `summary.error`, `summary.warning`, `finding_count`,
  `findings` (broken refs, missing modules, empty emitters), `skipped`
- performance_audit: `overall_score` (if present), threshold breaches
  (particle/emitter/GPU/sim-stage/sub-UV budgets), **overdraw / fixed-bounds
  signals when present** on the single-system path

If `validate.skipped == true`, mark system `PARTIAL` (exclude from healthy
numerator even if `is_valid == true`).

Require `performance_audit.mode == "single_system"` and matching `asset_name`.
Global-mode/mismatched → PARTIAL, do not score from that envelope.

Failed op on one system → PARTIAL for that system; continue others.

### Phase 1b — Promoted reads on offenders (Issue #23013)

After baseline ranking, enrich **top offenders** (systems with validate errors,
hard perf fail, or worst `--top N` ranks). Cap concurrency ≤5.

```python
flow = ue_manage_niagara(operation="get_execution_flow", params={"asset_name": name})
mods = ue_manage_niagara(operation="get_module_details", params={"asset_name": name})
cons = ue_manage_niagara(operation="find_asset_consumers", params={"asset_name": name})
# optional measured cost (skip if Unknown operation / older MCP):
cost = ue_manage_niagara(operation="measured_cost", params={"asset_name": name})
```

Use results for report narrative only (which emitters/modules hurt, who consumes
the asset, measured ms/frame when present). **Do not** invent extra health %
terms unless a field already maps to a hard fail in REFERENCE.md.

### Phase 2 — Optional pairwise compare

When `--compare-pairs M` > 0, pick the top-M worst systems (by validate errors then worst perf score) and run adjacent pairs:

```python
cmp = ue_manage_niagara(
    operation="compare_systems",
    params={"system_a": a, "system_b": b},
)
```

Use diffs (emitter count, parameter delta, module sets) as context for the ranking narrative. Skip if M=0.

### Phase 3 — Aggregate + score

See [REFERENCE.md](REFERENCE.md) for formulas.

- **Health %** = systems with `validate.is_valid == true`,
  `validate.skipped != true`, and no hard perf fail / total scanned
- **Hard perf fail** = any explicit budget breach OR overall_score below floor when the op returns a numeric score (default floor 50 if score present)
- Rank worst offenders: `validate.summary.error` desc → `validate.summary.warning` desc → worst `overall_score`
- Apply `--top N` to display only; always report scanned/total
- `--errors-only`: ranking restricted to systems with validate errors or hard perf fails

### Phase 4 — Report

```markdown
## VFX Health Audit — {project} ({scanned}/{total} Niagara systems)

**Health: {healthy}/{scanned} ({pct}%)** · validate errors {E} · warnings {W} · perf fails {P}
**Coverage:** FULL | PARTIAL | N/A | UNKNOWN

### Worst offenders
| # | System | val_err | val_warn | perf_score | top signals |
|---|--------|---------|----------|------------|-------------|

### Signal histogram
| signal | severity | count | systems |
|--------|----------|-------|---------|

### Optional compares
| A | B | key deltas |
|---|---|------------|

### Next steps
- Drilldown: `/ue-explain NS_Name` or `ue_manage_niagara(get_structure|get_execution_flow)`
- Fix broken refs / budget breaches before authoring writes
```

---

## Error Recovery

| Error | Action |
|-------|--------|
| search returns 0 | `ue_check_health()`; confirm project cache has Niagara systems |
| validate/performance_audit fails one system | mark system PARTIAL; continue fan-out |
| compare_systems fails | omit pair; note in report |
| tool missing op | report MCP version gap; still emit inventory-only PARTIAL |

---

## Non-goals

- Editor live preview / activate-deactivate
- Authoring writes (`set_parameter`, `add_emitter`, …)
- Replacing `/ue-audit` multi-domain scorecard
