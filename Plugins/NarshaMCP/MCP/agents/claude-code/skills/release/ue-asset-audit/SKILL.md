---
name: ue-asset-audit
model: opus
description: "Purpose: Mesh/texture project health rollup. Fans out offline validate_assets rules, estimates texture memory/format/size distribution, flags same-name collisions and unreferenced candidates, and emits a scored prioritized report. Triggers: '텍스처 예산 점검', '메시 상태 감사', '에셋 감사', 'asset audit', 'texture budget', 'mesh health report'. For single-asset refs use ue-asset-discovery; whole-project system health use ue-audit."
compatibility: "Requires NarshaMCP MCP server (v7.0.0+) with Unreal Engine 5.x project (offline metadata cache)"
argument-hint: "optional path prefix (default /Game), --top N (default 10), --type Texture|StaticMesh|SkeletalMesh|all, --no-refs"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "20648"
audience: end-user
---

# UE Asset Audit — Mesh / Texture Health Rollup

**Issue**: #20648 (Parent Epic #20632) · mesh/texture sibling of `/ue-data-audit` `/ue-physics-audit`
**Purpose**: Answer "이 프로젝트 메시·텍스처 건강한가?" in one skill: run offline `validate_assets` (#20647), summarize size/format/memory signals from cache fields, and surface orphans/same-name collisions — fully offline (Rule 20).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, Claude **MUST execute the audit automatically** via `ue_search_assets` — do NOT just print documentation.

1. **Target**: `$ARGUMENTS` (path prefix; empty = `/Game`). Flags: `--top N` (default 10), `--type Texture|StaticMesh|SkeletalMesh|all` (default all), `--no-refs`.
2. Run the 4-phase audit below.
3. Emit the structured mesh/texture health report.

---

## Auto-Trigger Phrases

Routing SSOT: `routing.json`.
- **KO**: 텍스처 예산 점검 · 메시 상태 감사 · 에셋 감사 · 텍스처 헬스 · 메시 헬스 · mesh/texture 감사 · 에셋 건강도 리포트
- **EN**: asset audit · texture budget · mesh health report · audit textures · audit meshes · mesh texture audit · asset health check

Single-asset "누가 참조?" → **`/ue-asset-discovery`**. Whole-project system audit → **`/ue-audit`**. Data tables → **`/ue-data-audit`**. Physics → **`/ue-physics-audit`**.

---

## Workflow

All ops are offline reads. Pass `operation=` verbatim.

```python
# Phase 1 — Offline mesh/texture rule validation (#20647)
# Do NOT wrap tool args in an empty params dict; call tool kwargs directly.
val = ue_search_assets(
    operation="validate_assets",
    asset_type="all",   # or Texture / StaticMesh / SkeletalMesh
    path="/Game",
    limit=500,
)
# -> is_valid (bool|null), indeterminate, reason, summary{error,warning,info},
#    findings[{rule_id,severity,message,asset_path,asset_type}], asset_count,
#    evaluated_rule_count, skipped_rule_count, checks_performed, checks_skipped,
#    source (inline_assets|metadata_cache|cache_cold)

# Phase 2 — Inventory + budget signals from cache search
tex = ue_search_assets(operation="search", asset_type="Texture", path="/Game", limit=200)
sm = ue_search_assets(operation="search", asset_type="StaticMesh", path="/Game", limit=200)
sk = ue_search_assets(operation="search", asset_type="SkeletalMesh", path="/Game", limit=200)
# When total_found > returned, re-call with higher limit — never silently truncate.
# Prefer fields already on search/meta rows when present:
#   size_x/size_y, compression_settings, srgb, never_stream, mip_count,
#   triangle_count, vertex_count, lod_count, material_slots, has_collision, nanite_enabled

# Phase 3 — Reference integrity (skip with --no-refs)
# For worst offenders + zero-ref candidates only (cap to avoid cold-graph thrash):
counts = [ue_search_assets(operation="get_ref_counts", asset_path=p) for p in sample_paths]
# incoming == 0 -> unreferenced candidate (info)

# Phase 4 — Aggregate
# - health = assets_with_error==0 / asset_count only when indeterminate=false
# - indeterminate=true or is_valid=null -> health N/A (never score skipped/defaulted metadata)
# - per-rule histogram from findings
# - top-N by severity then estimated texture max_dim / mesh triangle_count
# - same-name collisions: group by basename across tex/sm/sk inventories
```

**Degrade path**: if `validate_assets` returns Unknown operation (pre-#20647 binary), run Phases 2–3 only and mark validator N/A — never fake a pass. If `source=cache_cold`, report 0 assets and instruct cache warm / project connect.

---

For current binaries, the authoritative gate is `indeterminate`, not `source`
alone. Report health N/A for every `indeterminate=true`: warm/connect for
`cache_cold`, review the path/type filter for `no_matching_assets`, or regenerate
richer metadata for `no_evaluated_rules`.

## Report Template

```markdown
## Mesh/Texture Health Audit — {project}

**Health: {valid}/{total} clean ({pct}%)** · errors {E} · warnings {W} · info {I}
**source**: {metadata_cache|cache_cold|inline_assets} · validator: {v1|#20647|N/A}

| family | count | errors | warnings |
|---|---|---|---|
| Texture | … | … | … |
| StaticMesh | … | … | … |
| SkeletalMesh | … | … | … |

### 우선순위 (worst offenders)
| # | asset | type | errors | warnings | top rules |
|---|---|---|---|---|---|

### 룰별 집계
| rule_id | severity | count | assets |
|---|---|---|---|

### 예산 신호 (best-effort from cache fields)
- Texture max-dim top-N (size_x/size_y)
- StaticMesh triangle_count top-N
- NeverStream + large / non-PoT / Masks+sRGB flags (from findings)

### 참조 / 동명
- unreferenced candidates (incoming 0): …
- same-name collisions: …
```

---

## Error Recovery

| Error | Cause | Action |
|---|---|---|
| `validate_assets` Unknown operation | binary pre-#20647 | Tier-1 degrade: inventory+refs only; mark validator N/A |
| `indeterminate=true`, reason=`cache_cold` | metadata cache not warm | `ue_check_health`; ensure project path; re-index if needed |
| `indeterminate=true`, reason=`no_matching_assets` | path/type filter matched no assets | verify path and type; do not score health |
| `indeterminate=true`, reason=`no_evaluated_rules` | matched metadata is missing/defaulted | regenerate richer metadata; do not score skipped rules |
| search returns 0 | wrong path prefix / empty Content | try `/Game`, drop type filter; report empty honestly |
| `get_ref_counts` slow | cold reference graph | one retry; else omit Phase 3 with N/A |

---

## Composability

- `/ue-asset-discovery` — single-asset reference/dependency drilldown for worst offenders.
- `/ue-data-audit` — DataTable/CurveTable/ChooserTable domain (not mesh/texture).
- `/ue-physics-audit` — PhysicsAsset domain.
- `/ue-audit` — whole-project multi-domain health (this skill is mesh/texture only).

Full rule catalog + scoring detail → [REFERENCE.md](REFERENCE.md).
