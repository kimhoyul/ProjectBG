# ue-widget-authoring REFERENCE

Op recipes, rule catalogs, and mapping tables for the offline WBP authoring/validation round-trip.
SSOT pins: rule tables mirror the Rust doc-comment tables (smoke-tested by
`tests/unit/pure/skills/test_ue_widget_authoring_examples.py` — doc drift fails CI).

## §1 Op call shapes (offline legs)

```python
# Deterministic export — smart-router-EXCLUDED: explicit operation required
# (bare widget_name routes to get_structure and you silently get a summary).
ue_manage_widget(operation="export_dsl", widget_name="/Game/UI/WBP_HUD",
                 include_animations=False)  # authorable import; default true is review-only
# Determinism contract: same WBP -> byte-identical JSON. widgets[] order preserved
# (each entry carries export_index; parent_index = parent's export_index, #18017).

# DSL lint — no asset touched; dsl_content is the unique routing key.
ue_manage_widget(operation="validate_dsl", dsl_content="<json string>", strict=False)
# -> {is_valid, diagnostics[], error_count, warning_count, strict}
# strict=True escalates warnings into is_valid=False.

# Asset health — smart-router-EXCLUDED: explicit operation required.
ue_manage_widget(operation="validate_widget_blueprint",
                 widget_name="/Game/UI/WBP_HUD", strict=False)
# -> {is_valid, blueprint_name, widget_count, diagnostics[], error_count, warning_count}
```

## §2 validate_dsl rule catalog (13 rules)

GT-Source: `MCP/native/mcp_server/src/tools/widget_dsl_validate.rs` doc table (lines 12-25).

| rule | severity |
|---|---|
| `unparseable-json` | error |
| `missing-version` | error |
| `unsupported-version` | error |
| `missing-blueprint-block` | error |
| `missing-required-field` | error |
| `unknown-widget-class` | warning (did-you-mean suggestions) |
| `dangling-named-slot-reference` | error |
| `dangling-parent-index` | error (`parent_index` matches no widget's `export_index`, or self-referential — #18017) |
| `duplicate-widget-name` | error |
| `invalid-slot-layout` | error (slot class or anchors/offsets/alignment/size-rule values cannot be converted to authoring structs) |
| `skipped-slot-property` | warning (unsupported slot key stripped so the rest of the widget can still import — #26263) |
| `unsupported-import-section` | error (`blueprint.animations` present and not `[]`, including non-array/alias keys, fails closed before planning or mutation — #27238. Author animations with `ue_manage_blueprint(operation="create_widget_animation")` instead; do not feed animations to import_dsl) |
| `unconsumed-dsl-section` | warning (non-empty `blueprint.<key>` section export_dsl emits but the import planner cannot author — #26388) |

## §3 validate_widget_blueprint rule catalog (4 rules)

GT-Source: `MCP/native/mcp_server/src/tools/widget_asset_validate.rs` doc table (lines 16-19).

| rule | severity |
|---|---|
| `invalid-parent-class` | error (empty parent, unresolved Blueprint parent, cycle, depth overflow, or a native `/Script/` parent that is unresolvable offline or not a `UUserWidget` — #27235) |
| `orphaned-widget` | warning (parent-less non-root widget — variable exists, never renders) |
| `dangling-named-slot` | error (`named_slot_content` names a widget absent from the tree) |
| `broken-property-binding` | error (binding-type-specific contract — see tool doc) |

`is_valid=false` ⇔ ≥1 error; `strict=true` also flips on warnings.

## §4 Phase 3a persistence contract (#16934 / #16777)

Fields live **inside `result`** (the raw RC payload), not the envelope top-level:
read `r["result"]["marked_dirty"]` / `["persisted"]` / `["persist_error"]`.

| op | persisted in-call? | note |
|---|---|---|
| `set_slot_property` | **true** | slot subobject transacted + saved in-call (#16777) |
| `add_widget_to_parent` | **true** | same in-call save path |
| `set_text` | false (mark-dirty) | later File>Save or caller-owned save persists |
| `set_image_brush` | false (mark-dirty) | 〃 |
| `set_widget_property` | false (mark-dirty) | 〃 (#19999 friendly aliases) |
| `remove_widget` | false (mark-dirty) | root widget cannot be removed |
| `create_widget_blueprint` | n/a (creates asset) | UMG-parent guard (#16522/#16570) |

**Bulk edits**: a synchronous SavePackage per call is heavy — pass `mark_dirty=False`
on every op **except the last** (a `mark_dirty=False` call is a full no-op mark/save-wise;
the final `mark_dirty=True` flushes everything).

## §5 DSL-edit → authoring-op mapping

| DSL-level change (diff on exported JSON) | apply with |
|---|---|
| `widgets[].properties.Text` changed (TextBlock) | `set_text` |
| `widgets[].properties.Brush` / image resource changed | `set_image_brush` |
| slot layout field changed (`anchors`/`offsets`/`padding`/`alignment`/`z_order`/`size_rule`/`fill_value`) | `set_slot_property` |
| other reflected widget property changed (`visibility`/`render_opacity`/`is_enabled`/`color`…) | `set_widget_property` |
| new entry in `widgets[]` under an existing panel | `add_widget_to_parent` (`insert_index`, `slot_properties`) |
| entry removed from `widgets[]` | `remove_widget` |
| whole new WBP | `create_widget_blueprint` (explicit `name` + `path`; UMG parents only) |

**`import_dsl` / `apply_dsl` landed in Issue #19998** (closes the half deferred under #9400
after ECABridge removal in Epic #11230). Prefer them for full-DSL apply:

```python
ue_manage_widget(operation="import_dsl", dsl_content="<json>", dry_run=True)   # plan only
ue_manage_widget(operation="apply_dsl", dsl_content="<json>", dry_run=False)   # execute
```

This §5 mapping remains the surgical fallback when you want a single Phase 3a op
instead of a full-DSL plan (`docs/reference/tools/ue_manage_widget.md`).

## §6 Live round-trip verification recipe (safety-hardened)

The procedure the skill (and its live test) follows for a **real-asset mutation**:

1. **Identity gate (fail-closed)**: full `ue_check_health` (no `compact`) → assert the
   connected editor's project is the intended one; also confirm the target WBP resolves
   there via `ue_search_assets`. Health **timeout/error counts as abort** — fallback
   identity probe: the editor's `editor_ports/<pid>.json` advertisement (#16695) or an
   RC project query; proceed only on a **positive** match (multi-editor incident
   history #16334/#16695).
2. **Baseline**: `export_dsl` → save JSON; capture the original value of the target field.
3. **Byte backup (no-VCS projects)**: copy the target `.uasset` + record mtime/hash to a
   **durable** location (e.g. `<project>/Saved/NarshaMCP_backup_<issue>/`), not a
   purge-eligible temp dir. Restore only with the editor closed.
4. **Mutate ONE persisted:true field**: prefer integer `z_order` via `set_slot_property`
   (CanvasPanelSlot children only — pre-check the baseline's slot layout; float fields
   invite phantom diffs). Assert `result.persisted == true` **and** the `.uasset`
   mtime/hash changed before trusting any re-export.
5. **Stale-cache disambiguation**: if the re-export looks unchanged despite
   `persisted:true` + changed mtime, run `ue_cache_control(operation="regenerate_metadata")`
   and re-export (export_dsl reads the consolidated metadata layer); deleting
   `parse_cache_consolidated.bin` is the second-tier escalation only. **Known pre-existing
   gap (not cache staleness)**: on Issue #20001's live run the offline re-export did **not**
   reflect a CanvasPanelSlot `z_order` change even after `regenerate_metadata` — a
   slot read-back nuance in the offline parser, independent of cache freshness. So for
   slot-property edits, treat `result.persisted == true` **+ the `.uasset` mtime/hash
   change** (step 4) as the authoritative persistence proof; do **not** keep escalating the
   cache ladder when only the slot value fails to appear in the re-export.
6. **Revert**: re-apply the original value with the same op → re-export diff must equal
   the baseline; `validate_widget_blueprint` must report no new errors. If the revert RC
   call itself fails: close the editor, restore the byte backup.

## §7 create_widget_animation (`ue_manage_blueprint`, #26388 / #27237)

This is **not** a Phase 3a `ue_manage_widget` op and **must not** appear in the §4 table
(`test_skill_op_names_exist_in_tool_doc` reads only `ue_manage_widget.md`). Animation
authoring is a parallel Editor path; `import_dsl` still fail-closes non-empty
`blueprint.animations` (#27238). Keep round-trip `export_dsl` on `include_animations=False`
(#27455).

SSOT: `MCP/native/mcp_server/src/tools/schema_registry/editor.rs` (`[create_widget_animation]`
descriptions) and `dispatcher_docs.rs` (`create_widget_animation` params/example).

Required: `blueprint_name` (aliases `asset_name`, `blueprint_path`), `animation_name`
(must not collide), `duration` (seconds, must be `> 0` — no zero-length default).
Optional: `frame_rate` (whole int32; fractional/out-of-int32 values are rejected;
runtime maps `<= 0` to `60`, preserves `1..1000`, and clamps `> 1000` to `1000`),
`mark_dirty` (default true), `tracks[]`.

Each track requires `target_widget` + `property_path`. `property_path` is fail-closed:
only `RenderOpacity` and `RenderTransform`. RenderTransform keyframes require `component`
in `Scale.X`, `Scale.Y`, `Scale.XY`, `Translation.X`, `Translation.Y`, `Translation.XY`.
Omission fails closed unless the track sets `legacy_default_scale_component=true`
(authors `Scale.XY` for #26388 compatibility). `component` is invalid on `RenderOpacity`.

```python
r = ue_manage_blueprint(
    operation="create_widget_animation",
    blueprint_name="WBP_MainMenu",
    animation_name="SlideIn",
    duration=1.0,
    tracks=[{
        "target_widget": "Img_Logo",
        "property_path": "RenderTransform",
        "keyframes": [
            {"time": 0.0, "value": -200.0, "component": "Translation.X"},
            {"time": 1.0, "value": 0.0, "component": "Translation.X"},
        ],
    }],
)

assert r["success"] and r["persisted"]
ue_cache_control(operation="regenerate_metadata")
readback = ue_manage_widget(
    operation="get_animation_tracks",
    widget_name="WBP_MainMenu",
    animation_name="SlideIn",
)
assert readback["success"] and readback["tracks_enriched"]
track = next(t for t in readback["tracks"]
             if t["target_widget"] == "Img_Logo"
             and t["property_path"] == "RenderTransform")
receipt = [(round(k["time_seconds"], 3), k["value"], k["component"])
           for k in track["keyframes"]]
assert receipt == [(0.0, -200.0, "Translation.X"),
                   (1.0, 0.0, "Translation.X")]
```

Read-back is a different vocabulary: `ue_manage_widget(get_animation_tracks)` emits
per-axis `component` labels (`Translation.X` / `Translation.Y` / `Scale.X` / `Scale.Y`)
or `null` when the 2D-transform mask is ambiguous. `*.XY` is authoring-only. Each
keyframe receipt also exposes `time`, `time_seconds`, `value`, `interp_mode`,
`tangent_mode`, `arrive_tangent`, `leave_tangent`, `tangent_weight_mode`,
`arrive_tangent_weight`, and `leave_tangent_weight`. An animation-free
`export_dsl(include_animations=False)` is never proof that the animation was authored.
