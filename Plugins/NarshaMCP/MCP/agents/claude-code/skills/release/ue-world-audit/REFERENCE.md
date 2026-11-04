# UE World Audit — Reference

Companion to [SKILL.md](SKILL.md). Signal semantics, scoring, and the exact read-op fields each signal is derived from. Issue #20030 (Epic #20023).

---

## Signal table (Tier 1, read-derived heuristics)

All three tools are Phase 1 **read-only** (#9398); until the W7 validators land (#20029) the audit derives health signals from fields the reads already return. Every signal below names its source op + field so a fired signal is mechanically checkable.

| id | domain | severity | condition (source op → fields) |
|---|---|---|---|
| LW1 | landscape | warning | non-proxy ALandscape with `referenced_layer_infos == []` (`get_structure`) — no paint layers wired; terrain renders on default/unpainted material |
| LW2 | landscape | warning | streaming proxies exist (`"StreamingProxy" in search_landscapes results[].actor_class`, or `get_structure.is_streaming_proxy == true`) but **no** non-proxy ALandscape parent in the scan — orphaned streaming proxies. `search_landscapes` does **not** return `is_streaming_proxy` (Issue #22946) |
| LI1 | landscape | info | `referenced_grass_types != []` but `components_with_grass_data == 0` (`get_structure`) — grass types referenced, no built grass data in component tails (rebuild pending or intentional) |
| LI2 | landscape | info | `edit_layers_present: false` (`get_edit_layers`) — presence-only bool (#16560); informational, never scored |
| NE1 | navmesh | error / warning | `nav_modifier_volume_count + nav_link_proxy_count > 0` while `recast_navmesh_count == 0` (`get_structure`) — modifiers/links with no RecastNavMesh actor. **Mitigations** (Issue #22946 + handler hints): (a) runtime generation `mode == "Dynamic"` → severity `warning` (may materialize at load — verify in PIE); (b) `navmesh_built_data_files > 0` (int count of baked BuiltData) → severity `warning` or skip. Else `error` |
| NW1 | navmesh | warning | `recast_navmesh_count > 0` but `navmesh_built_data_files == 0` (`get_structure`, **integer count** — not a path array; paths are in `navmesh_built_data_sample`) — RecastNavMesh actor exists, no built tile data (Issue #22946 fixed the never-true `== []` check) |
| NI1 | navmesh | info | `mode_explicit: false` (`get_runtime_generation`) while `total_nav_actors > 0` (`get_structure`) — project relies on the engine-default runtime generation mode without declaring it in `DefaultEngine.ini` |
| FW1 | foliage | warning | IFA actors present (`get_foliage_instances` `count > 0`, `data[]` non-empty) but `get_foliage_types` `total_found == 0` — instances placed, no resolvable UFoliageType assets |
| FI1 | foliage | info | density ranking: per-IFA `data[].total_built_instances`, per-type `foliage_type_summary[].{ifa_count, total_ifa_size_bytes, levels[]}` — context for budget review (thresholds land with `validate_budget`, #20029) |
| FI2 | foliage | info | `mass_plugin_enabled: true` with 0 MassEntityConfigAsset (`get_mass_entity_configs`) — plugin enabled but unused |
| CI1 | cross | info | world has navmesh somewhere (`navmesh_present: true`) but a landscape-bearing level shows 0 nav actors in `per_level[]` — possible unnavigable terrain; info-level because many terrains are legitimately non-navigable |

Reporting rule (not a signal): any truncation signal MUST be surfaced in the report header — never silently truncate a capped scan. Per op: `scan_capped: true` exists on navmesh `get_structure` and foliage `get_mass_entity_configs`; the paginated listing ops (landscape `search_landscapes`/`get_layers`, navmesh volume/link lists, foliage instances/types) signal truncation via `total_found > count`.

---

## Scoring

- **Domain sub-score** = `max(0, 100 - 25*errors - 10*warnings)`; info signals never subtract.
- **Presence gate**: a domain with zero presence (landscape: 0 actors; navmesh: 0 nav actors AND no built data; foliage: no IFA actors — `get_foliage_instances` `data[]` empty) scores **N/A** and drops out of the weighted mean (remaining weights renormalized). Absence of a feature is not a defect.
- **World health** = `0.40*landscape + 0.35*navmesh + 0.25*foliage` over non-N/A domains.
- **Verdict bands**: `>= 90` HEALTHY · `70-89` ATTENTION · `< 70` UNHEALTHY.
- **Priority list**: fired signals sorted (error > warning > info), then by domain weight; top `--top N` (default 10). `--domain` restricts collection AND scoring to one domain (report shows the other domains as "skipped by flag", not N/A).

---

## Field provenance (what each phase reads)

| phase | op | key fields consumed |
|---|---|---|
| 0 | `ue_manage_project_ops(level_list_levels)` | level inventory → rollup denominator |
| 0 | `ue_manage_project_ops(wp_get_status)` | World Partition on/off (streaming header) |
| 1 | `ue_manage_landscape(search_landscapes)` | `results[].{name, actor_class, level_name, file_path}`, `total_found`, `count` — **no** `is_streaming_proxy` here (Issue #22946); proxy filter uses `actor_class` containing `StreamingProxy` |
| 1 | `ue_manage_landscape(get_structure)` | `is_streaming_proxy`, `referenced_layer_infos`, `referenced_grass_types`, `referenced_foliage_types`, `grid`, `components_scanned`, `components_with_grass_data` |
| 1 | `ue_manage_landscape(get_edit_layers)` | `edit_layers_present` |
| 2 | `ue_manage_navmesh(get_structure)` | `navmesh_present`, `recast_navmesh_count`, `nav_modifier_volume_count`, `nav_link_proxy_count`, `total_nav_actors`, `per_level[]`, `navmesh_built_data_files` (**int** count), `navmesh_built_data_sample` (path sample array), `scan_capped` |
| 2 | `ue_manage_navmesh(get_modifier_volumes / get_link_proxies)` | actor lists for the per-level rollup + report detail |
| 2 | `ue_manage_navmesh(get_runtime_generation)` | `mode`, `mode_explicit`, `supports_runtime_generation`, `source_file` |
| 3 | `ue_manage_foliage(get_foliage_instances)` | `count`, `total_found`, `data[]` (per-IFA `total_built_instances`, `instance_counts_per_component[]`), `foliage_type_summary[]` (per-type `ifa_count`, `total_ifa_size_bytes`, `levels[]`) |
| 3 | `ue_manage_foliage(get_foliage_types)` | `total_found`, `data[].{name, path, class_name, asset_type}` |
| 3 | `ue_manage_foliage(get_mass_entity_configs)` | `mass_plugin_enabled`, `count` |

Landscape fan-out cost: `get_structure` is called once per `search_landscapes` hit. Typical worlds have single-digit landscape actors; if `total_found` exceeds ~50, page with `limit`/`offset` and report the scanned/total ratio in the header.

---

## Tier 2 (W7 #20029) upgrade contract

When `ue_manage_landscape(validate)` + `ue_manage_foliage(validate_budget)` land:

1. **Probe**: attempt the op once; `Unknown operation` → stay Tier 1 (this document's signal table).
2. **Tier 2 collection**: fan the validators out per landscape / per foliage budget target; validator `findings[]` replace LW1/LI1/FI1 as the scored source (read-derived signals remain as context rows, unscored).
3. **Tier 2 scoring**: domain sub-score switches to `valid/total` (physics-audit convention, see [ue-physics-audit/REFERENCE.md](../ue-physics-audit/REFERENCE.md)); NE1/NW1 stay (navmesh has no W7 validator).

This keeps one skill surface across both tiers — the report format does not change, only the signal source column.

---

## Composition

- Worst-offender drill-down: `/ue-level-analysis <level>` for actor-level inventory behind a fired signal.
- Config drill-down: `/ue-cvar-explorer` for `NavigationSystemV1` runtime generation and scalability context (NI1).
- Domain twin: `/ue-physics-audit` — same audit shape (collect → fan out → aggregate → prioritized report) over PhysicsAssets.
