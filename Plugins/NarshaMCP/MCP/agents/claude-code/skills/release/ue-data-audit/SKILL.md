---
name: ue-data-audit
model: opus
description: "Purpose: Data-domain table health rollup. Enumerates DataTable / CurveTable / ChooserTable assets, fans out the offline validate_table rules, adds reference-integrity context, and emits a scored, prioritized data health report. Also the workflow home of the ue_modify_asset rows./curves. write syntax (REFERENCE.md). Triggers: '데이터 테이블 감사', '테이블 건강도 리포트', '데이터 에셋 점검', 'data table audit', 'audit data tables', 'table health report'. For C++ API reference (UAssetManager/FindRow) use data-assets-tables."
compatibility: "Requires NarshaMCP MCP server (v0.9.13+) with Unreal Engine 5.x C++ project"
argument-hint: "optional name pattern (default all tables), --top N (default 10), --family DataTable|CurveTable|ChooserTable, --no-refs"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  issue: "20623"
audience: end-user  # Issue #14105: ships in end-user bundle (offline read/validate surface)
---

# UE Data Audit — DataTable / CurveTable / ChooserTable Health Rollup

**Issue**: #20623 (D16) · Data-domain sibling of `/ue-physics-audit` `/ue-world-audit` `/ue-config-audit` · C++ API twin: `data-assets-tables`
**Purpose**: Answer "이 프로젝트의 테이블 데이터 건강한가?" in one skill: enumerate the table families, fan out the offline `validate_table` rules (#20617), add reference-integrity context, and emit a scored, prioritized report — fully offline (binary metadata cache, no Editor, Rule 20). Table read/write recipes incl. the `rows.`/`curves.` write grammar live in [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, Claude **MUST execute the audit automatically** via `ue_search_assets` — do NOT just print documentation.

1. **Target**: `$ARGUMENTS` (a name substring filter; empty = all tables). Flags: `--top N` (default 10), `--family DataTable|CurveTable|ChooserTable` (default all three), `--no-refs` (skip Phase 3).
2. Run the 4-phase audit below.
3. Emit the structured data health report.

---

## Auto-Trigger Phrases

Routing SSOT: `routing.json`.
- **KO**: 데이터 테이블 감사 · 테이블 건강도 리포트 · 데이터 에셋 점검 · 테이블 일괄 검증 · 데이터 헬스 체크
- **EN**: data table audit · audit data tables · table health report · validate all data tables · data health check

C++ API questions (`UAssetManager`, `FindRow`, `AssetBundles` meta) → **`data-assets-tables`**; whole-project system health ("프로젝트 감사") → **`/ue-audit`** (negative_patterns route there).

---

## Workflow

All audit ops are read-only. Pass `operation=` verbatim (deterministic op selection keeps runs comparable).

```python
# Phase 1 — Enumerate table-family assets (offline metadata cache)
res = ue_search_assets(operation="search", domain="Data", limit=200)
# domain="Data" -> DataAsset, DataTable, CompositeDataTable, CurveTable,
#                  CompositeCurveTable, ChooserTable,
#                  UserDefinedEnum, UserDefinedStruct (context rows only)
# res["total_found"] > res["returned"] -> re-call with higher limit (never silently truncate)
tables = [r for r in res["results"]
          if r["type"] in ("DataTable", "CompositeDataTable", "CurveTable",
                           "CompositeCurveTable", "ChooserTable")]

# Phase 2 — Batched structural validation (probe-gated; the validate_table op
# ships with #20617). On a post-#20617 binary, call it on ue_search_assets with
# asset_paths=[t["path"] for t in tables] (check_refs defaults true) ->
#   per-asset findings {severity, rule_id, location, message} + severity counts
#   + is_valid; 17-rule catalog DT000-DT007 / CT000-CT005 / CHT001-CHT003 -> REFERENCE.md.
# Treat every assets[] entry with success=false as invalid. In particular,
# error_code="ASSET_PATH_UNRESOLVED" increments the separate resolution-failure
# histogram bucket and remains in the health-score denominator; never drop it
# merely because finding_count=0.
# "Unknown operation" -> this binary is Tier 1 (pre-#20617):
#   skip Phase 2, score from Phases 1+3 only, report "validator unavailable".
# (Call shape + literal example: REFERENCE.md section 1 — kept there until
#  #20617 merges so the SKILL.md op-contract gate tracks the registry SSOT.)

# Phase 3 — Reference integrity + row-reference completeness (skip with --no-refs)
counts = [ue_search_assets(operation="get_ref_counts", asset_path=t["path"])
          for t in tables]
# incoming == 0 -> orphan candidate (info); highest incoming = blast-radius context
# worst offenders only: find_dependencies for outgoing danglings beyond DT004's cell scan
# Phase 3b — Row-level reverse refs (completeness; Issue #20620 / #24713)
# When a Phase 3 orphan/worst-offender needs *which row* points at an asset/tag,
# or which tables use a RowStruct, call find_row_references (offline scan of the
# data_tables metadata cache — finer than asset-level find_references).
#
# asset soft-ref cell:
#   ue_search_assets(operation="find_row_references",
#                    target="/Game/UI/T_Icon_Sword")
# GameplayTag cell / tag-shaped row name:
#   ue_search_assets(operation="find_row_references",
#                    target="Damage.Fire")
# RowStruct back-index (which tables use FWeaponRow):
#   ue_search_assets(operation="find_row_references",
#                    target="FWeaponRow", target_kind="row_struct")
# Optional: target_kind="auto|asset|tag|row_struct" (default auto),
#           limit=1000, project_root=...
# Hits: {table, table_path, row_name?, column?, value?, match_kind}
# + tables_scanned / tables_with_rows. Cold cache -> degraded=true with
# regenerate_metadata hint (not silent zero). See REFERENCE.md §6.
# A row-reference result is authoritative only when success=true,
# degraded is absent/false, and truncated=false. If truncated=true, increase
# limit and retry. Record tables_scanned and scan_scope; never turn a degraded
# or truncated zero-count into "no row references".
# Phase 4 — Aggregate: rule histogram, health score, worst-offender ranking, family stats
#   priority = sort tables by (errors desc, warnings desc); take --top N
```

**Aggregation** (rule/score detail → REFERENCE.md):
- **Per-rule histogram** across the 17-rule catalog (DT000-DT007, CT000-CT005, CHT001-CHT003; incomplete/partial extraction is fail-closed error severity).
- **Row-reference completeness** = authoritative only when every `find_row_references` response is non-degraded and non-truncated; otherwise report the incomplete targets and do not claim a zero-reference result.
- **Resolution-failure histogram** = count/list every `assets[].error_code == "ASSET_PATH_UNRESOLVED"`; these are invalid tables with zero rule findings, not successful omissions.
- **Health score** = `valid_tables / total_tables` (Tier 2). Tier 1 (no validator): report enumeration + orphan/ref stats only and mark the score N/A — never fake a pass.
- **Priority ranking** = tables sorted by `errors` desc then `warnings` desc, top N.
- Report scanned/total explicitly — **never silently truncate** a capped scan.

---

## Report Template

```markdown
## Data Health Audit — {project} ({scanned}/{total} tables)

**Health: {valid}/{total} valid ({pct}%)** · errors {E} · warnings {W} · validator: {v1|N/A (pre-#20617)}
**Resolution failures: {R}** · unresolved paths: {...}
| family | count | errors | warnings | orphans |
|---|---|---|---|---|
| DataTable | 12 | 1 | 3 | 2 |

### 우선순위 (worst offenders)
| # | table | errors | warnings | 주요 룰 |
|---|---|---|---|---|
| 1 | DT_Foo | 2 | 1 | DT001_duplicate_row_name, DT004_dangling_soft_ref |

### 룰별 집계
| rule_id | severity | 건수 | 영향 테이블 수 |
|---|---|---|---|

### 참조 컨텍스트
- orphan tables (incoming 0): ...  · most-referenced: ...
- row-reference scan: {complete|incomplete}; tables_scanned: {...}; truncated/degraded targets: {...}
```

---

## Error Recovery

| Error | Cause | Action |
|---|---|---|
| Phase 1 returns 0 assets | 인덱싱 미완 / 프로젝트 미연결 | `ue_check_health()` 확인; 캐시 empty면 프로젝트 로드 상태 점검 |
| `validate_table` → Unknown operation | 배포 바이너리가 #20617 이전 | Tier 1 degrade: Phase 2 skip, 리포트에 "validator unavailable" 명시 |
| 한 테이블 validate가 `error` finding (DT000/CT000) | 손상/비정형 에셋 | 해당 테이블을 error로 집계하고 계속 (다른 테이블 진행) |
| `get_ref_counts` slow/timeout | 첫 호출이 참조 그래프 cold-build (Lyra 실측: 30-60s; 60s timeout 가능) | 1회 재시도(그래프 warm 후 fast); 계속 실패면 Phase 3만 생략(orphan 열 N/A), Phase 2 집계는 진행 |
| `find_row_references` has `degraded:true` | DataTable row cache absent/trimmed | Regenerate metadata, retry, and mark row-reference completeness false until a non-degraded result is returned |
| `find_row_references` has `truncated:true` | `limit` smaller than the hit set | Increase `limit` and retry; never report the partial count as complete |
| `rows.` write가 `PathResolve` | `exports[N]` 이 테이블 export가 아님 (Lyra 실측: 테이블은 `exports[1]`) | REFERENCE.md Export-index trap 참조; dry-run으로 인덱스 프로브 |

---

## Table Read / Write (pointer)

Read: `ue_diff(operation="extract_asset")` — typed rows/curves JSON. Write: `ue_modify_asset` with `exports[N].rows.<Row>.<Column>` / `exports[N].curves.<Curve>.<Field>` paths — **grammar, reliability boundary, and copy-paste-safe examples in [REFERENCE.md](REFERENCE.md)**. The examples are executed against the real Rust path validator by `modify_asset_path_validators::tests::ue_data_audit_reference_examples_use_live_rust_grammar`; Python tests only guard the remaining source catalogs. Always dry-run first (`dry_run=true` is the default).

```python
# Lyra 5.8 typed read: row_count=8, column_count=7,
# extraction_status="complete", extraction_reason="ok".
table = ue_diff(operation="extract_asset", asset="/Game/UI/DT_UniversalActions")

# Write one fixed-width primitive cell (runtime-verified on Lyra 5.8; the table
# export is NOT always exports[0] — probe with dry_run, see REFERENCE.md §5)
ue_modify_asset(
    asset_path="/Game/UI/DT_UniversalActions",
    property_path="exports[1].rows.DefaultForward.NavBarPriority",
    new_value=1,
)  # dry_run defaults to true: returns would_succeed + splice_warnings, writes nothing
```

---

## Composability & Role Split

- **`data-assets-tables`** — static C++ API reference (UAssetManager/DataTable C++ 사용법). Use it for "어떻게 C++로 읽나"; use **this** skill for MCP 워크플로우 (audit / read / write). Mutual When-NOT-to-use links.
- `/ue-diff` — 감사가 지목한 테이블의 revision/snapshot 비교 드릴다운.
- `/ue-impact` — 특정 테이블을 뭐가 참조하는지 심층 분석.
- `/ue-audit` — 프로젝트 전체 시스템 건강; 본 스킬은 Data 도메인 전용.
