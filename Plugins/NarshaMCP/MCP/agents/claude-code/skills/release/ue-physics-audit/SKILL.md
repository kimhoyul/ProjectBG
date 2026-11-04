---
name: ue-physics-audit
model: opus
description: "Purpose: Project-wide PhysicsAsset health audit. Fans out validate_physics_asset across every PhysicsAsset, adds chaos/collision context, and emits a scored, prioritized health report. Triggers: '물리 감사', 'PhysicsAsset 일괄 검사', '물리 건강도 리포트', 'physics audit', 'audit all physics assets', 'physics health report'. For single-asset drilldown use ue-physics-debug."
compatibility: "Requires NarshaMCP MCP server (v0.9.13+) with Unreal Engine 5.x C++ project"
argument-hint: "optional pattern (default all), --top N (default 10), --errors-only"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "18109"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Physics Audit — Project-Wide PhysicsAsset Health

**Issue**: #18109 (Parent Epic #18099) · batch companion to `/ue-physics-debug`
**Purpose**: Fan out `validate_physics_asset` over **all** PhysicsAssets, add chaos/collision project context, and produce a scored, prioritized health report — fully offline (`.uasset` metadata cache, no Editor).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, Claude **MUST execute the audit automatically** via `ue_manage_physics` — do NOT just print documentation.

1. **Target**: `$ARGUMENTS` (a name pattern; empty = whole project). Flags: `--top N` (default 10), `--errors-only`.
2. Run the 4-phase audit below.
3. Emit the structured health report.

---

## Auto-Trigger Phrases

Routing SSOT: `routing.json`.
- **KO**: 물리 감사 · PhysicsAsset 일괄/전체 검사 · 전 PA 검증 · 물리 건강도 리포트 · 래그돌 일괄 검증
- **EN**: physics audit · audit/validate all PhysicsAssets · physics health report · batch physics validation

Single-asset drilldown ("PA_X 분석", "물리 디버깅") → **`/ue-physics-debug`** (negative_patterns route there).

---

## Workflow

`validate_physics_asset` is **explicit-only** (never reached via smart routing), so always pass `operation=` verbatim.

```python
# Phase 1 — Collect every PhysicsAsset (offline metadata cache)
res = ue_manage_physics(operation="search_physics_assets", query="*", limit=200)
pas = [r["name"] for r in res["results"]]
# res["total_found"] > res["count"] → re-call with a higher `limit` (no offset paging)

# Phase 2 — Fan out the structural validator over each PA
reports = []
for name in pas:
    reports.append(ue_manage_physics(operation="validate_physics_asset", physics_asset=name))
    # each → {valid, error_count, warning_count, findings[], summary{bodies,constraints,rules_checked:6,...}}

# Phase 3 — Project-level context (one call each)
chaos = ue_manage_physics(operation="get_chaos_settings")
collision = ue_manage_physics(operation="analyze_collision_profiles")

# Phase 4 — Aggregate: per-rule histogram, health score, worst-offender ranking
#   priority = sort PAs by (error_count desc, warning_count desc); take --top N
```

**Aggregation** (detail + rule/score tables → REFERENCE.md):
- **Per-rule histogram** across the 6 rules (3 error: `shapeless_body`, `zero_mass_body`, `dangling_bone_constraint`; 3 warning: `all_default_bodies`, `effectively_unlimited_limit`, `sim_kinematic_mismatch`).
- **Health score** = `valid_PA / total_PA`; also total errors/warnings and #PAs with errors.
- **Priority ranking** = PAs sorted by `error_count` desc then `warning_count` desc, top N.
- Report the scanned/total count explicitly — **never silently truncate** a capped scan.

---

## Report Template

```markdown
## Physics Health Audit — {project} ({scanned}/{total} PA)

**Health: {valid}/{total} valid ({pct}%)** · errors {E} · warnings {W}

### 우선순위 (worst offenders)
| # | PhysicsAsset | errors | warnings | 주요 룰 |
|---|---|---|---|---|
| 1 | PA_Foo | 3 | 2 | shapeless_body, dangling_bone_constraint |

### 룰별 집계
| rule | severity | 건수 | 영향 PA 수 |
|---|---|---|---|

### 프로젝트 컨텍스트
- Chaos: MaxPhysicsDeltaTime / solver iterations (from get_chaos_settings)
- Collision: N profiles / channels (from analyze_collision_profiles)
```

---

## Error Recovery

| Error | Cause | Action |
|---|---|---|
| `search_physics_assets` returns 0 | 인덱싱 미완 / 프로젝트 미연결 | `ue_check_health()` 확인; 캐시 empty면 프로젝트 로드 상태 점검 |
| A PA validate returns `success:false` | asset 이름 해석 실패 | 해당 PA를 skip하고 리포트에 명시(다른 PA는 계속) |
| `analyze_collision_profiles` engine-path error | `UECODEGEN_ENGINE_DIR` 미설정 | 설정 후 재시도; chaos/collision 컨텍스트만 생략하고 PA 집계는 진행 |

---

## Composability

- `/ue-physics-debug` — audit가 지목한 worst-offender PA 단건 드릴다운.
- `/ue-validate` — pre-submit 자산 검증(PhysicsAsset 포함).
- `/ue-impact` — 특정 PA를 뭐가 참조하는지.
