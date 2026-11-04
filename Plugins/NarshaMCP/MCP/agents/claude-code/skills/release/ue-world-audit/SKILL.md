---
name: ue-world-audit
model: opus
description: "Purpose: World-domain comprehensive health rollup. Aggregates landscape + navmesh + foliage reads plus level/streaming context into one scored, prioritized world health report - fully offline. Triggers: '월드 감사', '월드 건강도 리포트', '월드 상태 점검', 'world audit', 'world health report', 'is this world healthy'. For per-level actor drill-down use ue-level-analysis."
compatibility: "Requires NarshaMCP MCP server (v0.9.13+) with Unreal Engine 5.x C++ project"
argument-hint: "--top N (default 10), --domain landscape|navmesh|foliage"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "20030"
audience: end-user  # Issue #14105: ships in end-user bundle (worldbuilding read tools are end-user surface)
---

# UE World Audit — Landscape + NavMesh + Foliage Health Rollup

**Issue**: #20030 (Parent Epic #20023) · world-domain companion to `/ue-physics-audit` · drill-down sibling `/ue-level-analysis`
**Purpose**: Answer "이 월드 건강한가?" in one skill: roll up the three worldbuilding read tools (`ue_manage_landscape` / `ue_manage_navmesh` / `ue_manage_foliage`) plus level/streaming context into a scored, prioritized world health report — fully offline (`LevelContentIndex` + asset metadata cache, no Editor, Rule 20).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, Claude **MUST execute the audit automatically** via the worldbuilding MCP tools — do NOT just print documentation.

1. **Target**: `$ARGUMENTS` is optional context only (Phase 1 always uses `query="*"`). Flags: `--top N` (default 10), `--domain landscape|navmesh|foliage`.
2. Run the 5-phase audit below.
3. Emit the structured world health report.

---

## Auto-Trigger Phrases

Routing SSOT: `routing.json`.
- **KO**: 월드 감사 · 월드 건강도 리포트 · 월드 상태 점검 · 랜드스케이프 내비 폴리지 일괄 점검 · 월드 헬스 체크
- **EN**: world audit · world health report · is this world healthy · audit the world · landscape navmesh foliage audit

Per-level actor inventory ("레벨 분석", "what actors in L_X") → **`/ue-level-analysis`**; whole-project system health ("프로젝트 감사") → **`/ue-audit`** (negative_patterns route there).

---

## Workflow

All ops are read-only. Pass `operation=` verbatim (never rely on smart routing for an audit — deterministic op selection keeps runs comparable).

```python
# Phase 0 — Level/streaming context (denominator for the per-level rollup)
levels = ue_manage_project_ops(operation="level_list_levels")
wp     = ue_manage_project_ops(operation="wp_get_status")   # World Partition on/off; graceful skip on error

# Phase 1 — Landscape (skip when --domain excludes it)
found = ue_manage_landscape(operation="search_landscapes", query="*", limit=50)
# total_found > count -> re-page (never silently truncate). Field contracts: REFERENCE.md (#22946).
structures, non_proxy = [], []
for r in found["results"]:
    cls = r.get("actor_class") or r.get("name") or ""
    is_proxy = "StreamingProxy" in cls  # search has no is_streaming_proxy key
    st = ue_manage_landscape(operation="get_structure", landscape_name=r["name"])
    if "is_streaming_proxy" in st:
        is_proxy = bool(st["is_streaming_proxy"])
    structures.append(st)
    if not is_proxy:
        non_proxy.append(r)
edit_layers = [ue_manage_landscape(operation="get_edit_layers", landscape_name=r["name"])
               for r in non_proxy]

# Phase 2 — NavMesh
nav   = ue_manage_navmesh(operation="get_structure")
# navmesh_built_data_files: int count (NOT path list); sample paths: navmesh_built_data_sample
vols  = ue_manage_navmesh(operation="get_modifier_volumes")
links = ue_manage_navmesh(operation="get_link_proxies")
rt    = ue_manage_navmesh(operation="get_runtime_generation")

# Phase 3 — Foliage
ifa   = ue_manage_foliage(operation="get_foliage_instances")
types = ue_manage_foliage(operation="get_foliage_types")
mass  = ue_manage_foliage(operation="get_mass_entity_configs")

# Phase 4 — Aggregate (signals/scores → REFERENCE.md)
```

**Aggregation** (full tables → REFERENCE.md):
- Signals: LW1/LW2/LI1/LI2 · NE1/NW1/NI1 · FW1/FI1/FI2 · CI1.
- Domain score = 100 − 25·errors − 10·warnings (floor 0); zero presence → N/A (weights renormalize).
- World health = 0.40 landscape + 0.35 navmesh + 0.25 foliage → ≥90 HEALTHY / 70–89 ATTENTION / <70 UNHEALTHY.
- Priority list = fired signals by severity, top `--top N`. Always report `scan_capped` / total_found.

---

## Report Template

```markdown
## World Health Audit — {project} ({levels_scanned} levels, WP: {on|off})

**World health: {score}/100 — {HEALTHY|ATTENTION|UNHEALTHY}**
| domain | score | errors | warnings | presence |
|---|---|---|---|---|
| landscape | 85 | 0 | 1+1 info | 3 actors (2 proxies) |
| navmesh | 100 | 0 | 0 | 1 RecastNavMesh, mode=Static |
| foliage | N/A | - | - | no IFA |

### 우선순위 권고 (top N)
| # | severity | signal | 대상 | 권고 |
|---|---|---|---|---|
| 1 | warning | LW1 no layer infos | Landscape_0 | LandscapeLayerInfo 연결 확인 |

### 레벨별 롤업
| level | landscape | nav actors | foliage instances |
|---|---|---|---|
```

---

## Error Recovery

| Error | Cause | Action |
|---|---|---|
| `search_landscapes` / `get_structure` returns 0 or empty | level_content 스캔 미완 / 프로젝트 미연결 | `ue_check_health()` 확인; 캐시 empty면 프로젝트 로드 상태 점검 |
| `get_runtime_generation` config-path error | `Config/DefaultEngine.ini` 미발견 | mode를 "unknown (config unread)"로 보고하고 NI1은 skip; 액터 집계는 진행 |
| `wp_get_status` / `level_list_levels` error | project_ops 경로 문제 | Phase 0 컨텍스트만 생략(스트리밍 열 N/A), 3-도메인 집계는 진행 |
| 한 도메인 툴 전체 실패 | 도구 미배포/버전 구식 | 해당 도메인 sub-score를 N/A로 두고 리포트에 명시(다른 도메인은 계속) |

---

## Two-Tier Upgrade (W7 — Tier 2 landed in #20029)

Probe `ue_manage_landscape(validate)` + `ue_manage_foliage(validate_budget)` — `Unknown operation` → stay Tier 1. When present, domain scores use `valid/total`; keep read signals as unscored context. Cold contracts (foliage single-call, WP name ambiguity + `file_path` de-dupe, never fold `checks_not_determinable` into valid) → **REFERENCE.md**.

---

## Composability

- `/ue-level-analysis` — 감사가 지목한 레벨의 액터 단위 드릴다운.
- `/ue-physics-audit` — 물리 도메인 동형 감사 (per-PA validator fan-out).
- `/ue-audit` — 프로젝트 전체 시스템 건강(서버/설정/에셋) 감사; 본 스킬은 월드 도메인 전용.
- `/ue-cvar-explorer` — NI1이 지목한 NavigationSystemV1 런타임 생성 모드 등 설정 드릴다운.
