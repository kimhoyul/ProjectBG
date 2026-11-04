# UE VFX Audit — Reference

Companion to [SKILL.md](SKILL.md). Issue #19976 / **#23013** / Epic #19965.

## Ops used (`ue_manage_niagara`)

| op | role | when |
|----|------|------|
| `search_systems` | inventory / denominator | always |
| `validate` | structural + reference integrity gate | every selected system |
| `performance_audit` | particle/emitter/GPU/sim-stage/sub-UV (+ overdraw/fixed-bounds when present) | every selected system (`mode=single_system`) |
| `get_execution_flow` | emitter graph / stage structure signals | **offenders only** (#23013) |
| `get_module_details` | module stack detail for offenders | **offenders only** |
| `find_asset_consumers` | impact radius (who references this NS) | **offenders only** |
| `measured_cost` | measured/estimated ms-frame when available | **optional** offenders; skip if missing |
| `compare_systems` | optional pairwise delta context | `--compare-pairs M` |

All offline. Pass `operation=` explicitly.

## Scoring (field contracts)

Align names with live envelopes (do **not** invent alternate keys):

1. **Per-system healthy** iff **all**:
   - `validate.success == true`
   - `validate.is_valid == true` (or equivalent zero-error envelope)
   - `validate.skipped != true`
   - `performance_audit` single-system success envelope with **no hard perf fail**
2. **Hard perf fail** when any of:
   - explicit budget-breach flags / findings from particle, emitter, GPU, sim-stage, sub-UV budgets
   - overdraw or GPU fixed-bounds hard-fail fields when the op returns them on single-system path
   - `overall_score` present and numeric **`< 50`**
   - `performance_audit.success == false` with a budget-class error
3. **Project health %** = `healthy_count / scanned_count * 100` (integer math ok; display one decimal only if needed).
4. **Coverage**:
   - `FULL` — inventory complete and every selected system got both validate + performance_audit success envelopes
   - `PARTIAL` — truncated inventory, any per-system op failure/skipped validate, or global-mode perf envelope
   - `N/A` — zero systems
   - `UNKNOWN` — inventory call failed

Never claim HEALTHY under PARTIAL coverage. Promoted reads (1b) never flip FULL→PARTIAL solely because `measured_cost` is absent on older MCP.

## Ranking

Sort systems by:
1. `validate.summary.error` (or `error_count` alias) desc
2. `validate.summary.warning` desc
3. worst `overall_score` (missing score sorts after scored fails, before pure-clean)
4. name asc

Display top `--top N` (default 10). Totals always use full scanned set.

## Promoted-read narrative (#23013)

| op | narrative use |
|----|----------------|
| `get_execution_flow` | stage/emitter graph anomalies (excessive stages, odd loops) |
| `get_module_details` | which modules drive cost/errors |
| `find_asset_consumers` | levels/BPs/assets impacted if the NS is fixed |
| `measured_cost` | pair static scores with ms/frame when available |

## compare_systems

Optional. Adjacent pairs among top offenders. Narrative deltas only — do not double-count into health % unless a delta proves a validate/perf defect already counted.

## Interaction with other skills

| Need | Skill |
|------|-------|
| Single NS structure/params | `/ue-explain` |
| Whole-project multi-domain | `/ue-audit` |
| Runtime trace | `/ue-perf-audit` |
| This rollup | `/ue-vfx-audit` |

## Failure modes

- Empty Niagara cache → inventory 0 → N/A
- One corrupt `.uasset` → that system PARTIAL, others proceed
- Missing promoted op on older MCP → omit that enrichment; state the gap
- Missing `validate` → PARTIAL inventory + performance_audit only
