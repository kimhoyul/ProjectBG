# ue-data-audit REFERENCE — rule catalog, scoring, table read/write recipes

Cold companion to [SKILL.md](SKILL.md) (Issue #20623). Loaded on demand.

---

## 1. validate_table rule catalog (Issue #20617)

`ue_search_assets(operation="validate_table", asset_path=... | asset_paths=[...], check_refs=true, project_root=...)` — offline, zero new parsing (reuses the `ue_diff` typed extractors). Response: per-asset `findings[]` of `{severity, rule_id, location, message}` + severity counts + `is_valid`.

| Family | rule_id | severity | what it catches |
|--------|---------|----------|-----------------|
| DataTable | `DT000_parse_error` | error | row-schema parse failed |
| DataTable | `DT001_duplicate_row_name` | error | two rows share a name |
| DataTable | `DT002_empty_row_name` | error | a row has a blank name |
| DataTable | `DT003_failed_row` | error | a partial-parse marker row |
| DataTable | `DT004_dangling_soft_ref` | error | a cell soft ref -> deleted `/Game/` asset |
| DataTable | `DT005_empty_soft_ref` | info | a cell soft ref is empty/unset |
| DataTable | `DT006_type_mismatch` | warning | a cell value contradicts its column's declared type |
| DataTable | `DT007_incomplete_extraction` | error | extractor could not prove a complete row payload |
| CurveTable | `CT000_parse_error` | error | curve binary parse errored |
| CurveTable | `CT001_unsorted_keys` | error | curve keys not ascending by time |
| CurveTable | `CT002_duplicate_key_time` | warning | two keys at the same time |
| CurveTable | `CT003_empty_curve` | warning | a curve has no keys |
| CurveTable | `CT004_partial_parse` | error | header declared more rows than parsed |
| CurveTable | `CT005_incomplete_extraction` | error | extractor bailed to a minimal phase |
| ChooserTable | `CHT001_unreachable_row` | warning | row shadowed by an identical earlier enabled row |
| ChooserTable | `CHT002_dangling_result_ref` | error | result soft ref -> deleted `/Game/` asset |
| ChooserTable | `CHT003_empty_result_ref` | info | result soft ref is empty/unset |

**Probe-gating**: the op ships with #20617. On an older deployed binary the call returns *Unknown operation* — degrade to Tier 1 (enumeration + references + stats), mark the health score N/A, and say so in the report.

## 2. Scoring detail

- Tier 2 (validator available): `health = valid_tables / total_tables`; per-rule histogram; worst offenders by `(errors desc, warnings desc)`.
- Tier 1: no per-table verdicts exist — report family counts, orphan candidates (incoming refs 0), most-referenced tables, and any Phase 1 anomalies (`total_found > count` truncation, 0-asset families). Never synthesize a score.
- A family with **zero presence** (e.g. no ChooserTable at all) is excluded from ranking — absence is not unhealthy.

## 3. Enumeration — `domain="Data"`

`ue_search_assets(operation="search", domain="Data")` expands to exactly these asset types (SSOT: `MCP/native/mcp_server/src/tools/infrastructure/search_assets_domain.rs`):

`DataAsset`, `DataTable`, `CompositeDataTable`, `CurveTable`, `CompositeCurveTable`, `ChooserTable`, `UserDefinedEnum`, `UserDefinedStruct`

The audit validates the table families (plain + Composite* + Chooser); DataAsset/UserDefined* appear as context rows (counts only). Filter one family with `asset_type="DataTable"` etc.
(Issue #23871: Composite* included in Data domain SSOT.)

## 4. Table READ recipes

Typed extraction is fail-closed for genuinely unversioned tables whose
RowStruct schema is missing or rejected before the first row can be decoded.
Those responses expose `parse_errors[]` and `schema_recovery`; `allow_partial=true`
is diagnostic only and never invents missing row values. The verified Lyra
fixture `DT_UniversalActions` is different: it is an UE 5.2 legacy tagged table
and decodes completely after the nested HasPropertyGuid alignment fix (#24509).

```python
# Typed extraction (offline; Lyra 5.8: 8 rows, 7 columns, complete)
ue_diff(operation="extract_asset", asset="/Game/UI/DT_UniversalActions")
# -> typed: {row_struct: "CommonInputActionDataBase", row_count: 8,
#            column_count: 7, failed_row_count: 0,
#            extraction_status: "complete", extraction_reason: "ok", rows: [...]}

# Fast reference counts (Phase 3 workhorse)
ue_search_assets(operation="get_ref_counts", asset_path="/Game/UI/DT_UniversalActions")

# Who uses this table? (drill-down on a worst offender)
ue_search_assets(operation="find_references", asset_path="/Game/UI/DT_UniversalActions")

# What does this table point at? (outgoing danglings beyond DT004's cell scan)
ue_search_assets(operation="find_dependencies", asset_path="/Game/UI/DT_UniversalActions")
```

## 5. Table WRITE — `rows.` / `curves.` grammar (ue_modify_asset)

First workflow documentation of the custom-serialize write surface (#8396 / #10458 / #10459). Full tool contract: [docs/reference/tools/ue_modify_asset.md](../../../docs/reference/tools/ue_modify_asset.md).

### Grammar (SSOT: `MCP/native/mcp_server/src/tools/modify_asset_path_validators.rs`)

```text
row path   ::= "exports[" N "].rows." RowName "." ColumnName
               # RowName and ColumnName: single segments — no "." and no "[" in either
curve path ::= "exports[" N "].curves." CurveName "." Field
               # CurveName: single segment; Field MAY be dotted (keys.0.time) but never "[N]"
```

### Reliability boundary (what actually writes)

- **Reliable cells**: fixed-width `Byte`/`Int`/`Int64`/`UInt32`/`UInt64`/`Float`/`Double`/`Object`, plus the shipped NameProperty, StrProperty, and GameplayTag slice (#20624). Width-preserving or handler-verified splice; `dry_run=true` by default; apply writes a `.bak` and renames atomically.
- **Rejected by design** (HIGH-severity / handler rejection): TextProperty and general nested struct cells (`rows.X.Y.Z`), Array/Map/Set columns, row insertion/deletion (#8396-B changes `NumRows`).
- The engine dispatches on the raw path via `serialize_handlers::handler_for`; `exports[N].properties.RowStruct` is a normal Tagged Property — NOT the custom tail.

### ⚠️ Export-index trap (measured on Lyra 5.8)

`exports[N]` must be the **table's own export** — the handler claims the path only when export `N`'s class is DataTable/CurveTable. On real UE 5.x editor assets the table export is **not always 0**: on Lyra 5.8 `DT_UniversalActions` it is `exports[1]`; with `exports[0]` the handler doesn't claim and the call falls through to `error_kind: "PathResolve"` (`expected '.properties.<Name>'...`). That error on a `rows.` path = **wrong export index**, not wrong grammar — probe indices with `dry_run=true` (free) or check `extract_asset` first.

### Verified examples (dry-run first, always)

Runtime-verified call (Lyra 5.8, `DT_UniversalActions`, `NavBarPriority` is an `IntProperty` → fixed-width Reliable; captured in `docs/technical/performance/_artifacts/20623/`):

```json
{
  "asset_path": "/Game/UI/DT_UniversalActions",
  "property_path": "exports[1].rows.DefaultForward.NavBarPriority",
  "new_value": 1
}
```

→ `{"success": true, "would_succeed": true, "splice_warnings": [], "changes": [{"path": "exports[1].rows.DefaultForward.NavBarPriority", ...}], "method": "write_pipeline.dry_run"}`

Grammar-form examples (schema-verified against the Rust validators; CurveTable form has no live Lyra fixture — Lyra 5.8 ships zero CurveTables):

```json
{
  "asset_path": "/Game/Data/DT_Weapons",
  "property_path": "exports[1].rows.Pistol.Damage",
  "new_value": {"type": "float", "value": 25.0}
}
```

```json
{
  "asset_path": "/Game/Data/CT_Scaling",
  "mutations": [
    {"property_path": "exports[1].curves.DamageMul.keys.0.time", "new_value": {"type": "float", "value": 0.5}},
    {"property_path": "exports[1].curves.DamageMul.keys.0.value", "new_value": {"type": "float", "value": 1.25}},
    {"property_path": "exports[1].rows.Rifle.MagazineSize", "new_value": 30}
  ]
}
```

Apply = same call + `"dry_run": false`. Read the dry-run `changes[]` (`old_value` → `new_value`) and `splice_warnings[]` before applying; a `CustomHandled` HIGH warning means the handler recognized but refused the mutation — do NOT force it with `allow_splice_zone=true` (that silently drops it into `dropped_mutations`).

### Rejected forms (grammar errors — copy-paste traps)

| path | why rejected |
|---|---|
| `exports[0].rows.Pistol` | missing `.ColumnName` — two segments required |
| `exports[0].rows.Pistol.Stats.Damage` | nested struct column — row/column must be single segments |
| `exports[0].rows..Damage` | empty row name |
| `exports[0].curves.HP.keys[0].time` | `[N]` index syntax — use dot form `keys.0.time` |
| `exports[0].curves..default_value` | empty curve name |

These examples are source-backed contract-tested: the Rust test `modify_asset_path_validators::tests::ue_data_audit_reference_examples_use_live_rust_grammar` passes every fenced `rows.`/`curves.` path through the production `validate_property_path` function and proves every rejected-form row fails. `tests/unit/pure/skills/test_ue_data_audit_examples.py` separately cross-checks the `domain="Data"` and rule catalogs, so no Python grammar mirror can drift from production.

## 6. Row-level reverse references — `find_row_references` (Issue #20620 / #24713)

Asset-level `find_references` / `get_ref_counts` answer "who points at this table".
They **cannot** answer "which **row** of which table points at this icon / tag / row-struct".
That is what `ue_search_assets(operation="find_row_references")` does — offline query
over the cached `data_tables` metadata (no per-call `.uasset` re-parse).

| `target_kind` | question | example `target` |
|---|---|---|
| `asset` (auto if `/` in target) | which item rows soft-ref this asset? | `/Game/UI/T_Icon_Sword` |
| `tag` (auto if `.` in target) | which cells / tag-shaped row names carry this GameplayTag? | `Damage.Fire` |
| `row_struct` (auto bare id, or force with `target_kind`) | which tables use this RowStruct? | `FWeaponRow` |

```python
# Completeness drill-down after Phase 3 flags a worst offender or orphan icon path
hits = ue_search_assets(
    operation="find_row_references",
    target="/Game/UI/T_Icon_Sword",  # or target_kind="asset"
    limit=1000,
)
# -> success, target, resolved_target_kind, tables_scanned, tables_with_rows,
#    count, hits[{table, table_path, row_name, column, value, match_kind}, ...],
#    truncated?, degraded? (cold cache — run regenerate_metadata)
```

**When to call (audit flow)**
1. Phase 2 `DT004_dangling_soft_ref` / `CHT002_dangling_result_ref` — confirm other live rows still pointing at the same soft path before bulk edit.
2. Phase 3 high-incoming tables — pick a consumer soft-path from a top row and list every row that embeds it.
3. Schema renames — `target_kind="row_struct"` lists tables still on the old `F*Row` before CoreRedirects.
4. GameplayTag renames — `target="Old.Tag.Name"` lists cells still carrying the old tag.

**Not a substitute for** `get_ref_counts` / `find_references` (asset-level blast radius) or `validate_table` (structural health). Use them first; use `find_row_references` for row-level completeness.
