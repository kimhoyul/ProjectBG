# UE Level Analysis Reference

**Version**: 1.0.0
**Parent**: [SKILL.md](SKILL.md)
**Issue**: #8564 (skill) · #20028 (REFERENCE + evals + example fix) · #22948 (schema↔handler param parity + StaticMeshActor example)

All operations are dispatched through the single policy tool
`ue_manage_project_ops`. The schema exposes **exactly one** routing parameter —
`operation` (a string enum) — plus a set of per-operation parameters. There is
**no `operation_type` parameter**; passing one is silently dropped by the schema
(see [Common Pitfalls](#common-pitfalls)).

SSOT for the parameter surface:
[`MCP/native/mcp_server/src/tools/schema_registry/project_ops.rs`](../../../MCP/native/mcp_server/src/tools/schema_registry/project_ops.rs).
Level dispatch:
[`project_ops_level.rs`](../../../MCP/native/mcp_server/src/tools/project_ops_level.rs).
World Partition dispatch:
[`project_ops_wp.rs`](../../../MCP/native/mcp_server/src/tools/project_ops_wp.rs).

---

## Routing

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `operation` | str (enum) | No | `"smart"` | Concrete op name (e.g. `level_scan`, `wp_get_status`) or `"smart"` / `"auto"` to auto-route. Prefer the concrete name when you know it. |
| `project_root` | str | No | auto-detected | Project root path. |

> **Use a concrete `operation` value when you know the op you want.** `"smart"`
> is a convenience router for ambiguous natural-language intent; a
> documentation/example call should name the operation directly so the routing
> is unambiguous and reproducible.

---

## Level Content Operations

### `level_scan`

Triggers an OFPA (One File Per Actor) scan and returns aggregate counts. Cache
the result; downstream FIND/DIAGNOSE ops reuse the built index.

**Parameters**: none.

**Returns**:

```python
{
    "success": bool,            # True if scan succeeded
    "operation": "level_scan",
    "total_actors": int,        # OFPA actors discovered
    "total_levels": int,        # Levels containing actors
    "total_bp_classes": int,    # Distinct Blueprint classes placed
    "category_counts": dict,    # Actor category -> count
    "scan_stats": {
        "total_ofpa_files": int,
        "parse_ok": int,
        "parse_fail": int,
        "tag_found": int,
        "tag_missing": int,
        "scan_time_ms": float
    },
    "total_time_ms": float
}
```

**Example**:

```python
ue_manage_project_ops(operation="level_scan")
```

---

### `level_list_levels`

Lists levels sorted by actor count (top-N summary).

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `limit` | int | No | 20 | Max levels returned. |

```python
ue_manage_project_ops(operation="level_list_levels", limit=20)
```

---

### `level_find_actors`

Filters actors by class, category, level, Blueprint, and/or Data Layer.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `class_name` | str | No | Actor class filter (case-insensitive substring; no `A`/`U` prefix). Alias: `actor_class`. |
| `category` | str | No | Actor category exact match (e.g. `StaticMesh`, `Light`, `Blueprint`, `PCG`). |
| `level_name` | str | No | Restrict to one level. |
| `blueprint` | str | No | Blueprint class filter. |
| `data_layer` | str | No | Data Layer name filter (substring). |
| `include` | list[str] \| str | No | Optional row projection (`position`, `file_path`, `tags`, `runtime_grid`). Omitted = thin row (Issue #21937). |
| `limit` | int | No | Max results. |

```python
# class_name is a substring match without A/U reflection prefixes (Issue #22948).
ue_manage_project_ops(operation="level_find_actors", class_name="StaticMeshActor", limit=50)
ue_manage_project_ops(operation="level_find_actors", category="Light", data_layer="Lighting", limit=20)
ue_manage_project_ops(operation="level_find_actors", class_name="StaticMeshActor", include=["position", "file_path"], limit=10)
```

---

### `level_get_summary`

Detailed breakdown of a single level.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `level_name` | str | Yes | Level to summarize. |

```python
ue_manage_project_ops(operation="level_get_summary", level_name="LV_Main")
```

---

### `level_bp_usage` / `level_cross_reference`

Where is a Blueprint placed? (`level_bp_usage`) and bidirectional BP↔Level query
(`level_cross_reference`).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `blueprint` | str | Yes (usage) | Blueprint filter. |
| `tag` | str | No (cross_reference) | GameplayTag filter. |

```python
ue_manage_project_ops(operation="level_bp_usage", blueprint="BP_Enemy")
```

---

### `level_find_by_tag` / `level_find_by_asset`

Find actors by GameplayTag or by referenced asset.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `tag` | str | Yes (`level_find_by_tag`) | GameplayTag to search. |
| `asset` | str | Yes (`level_find_by_asset`) | Asset path/name substring in actor asset references (Issue #22948). |
| `level_name` | str | No | Restrict to one level. |
| `limit` | int | No | Max results. |

```python
ue_manage_project_ops(operation="level_find_by_tag", tag="Gameplay.Objective")
ue_manage_project_ops(operation="level_find_by_asset", asset="/Game/Meshes/SM_Rock", limit=50)
```

---

### `level_get_actor_detail`

Full PlacedActorInfo (transform, bounds, tags, data layers) for matching OFPA actors.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `file_path` | str | One of | OFPA `.uasset` path substring. |
| `class_name` | str | One of | Actor class filter (substring). Alias: `actor_class`. |
| `level_name` | str | No | Level filter. |
| `limit` | int | No | Max detail rows (default 10). |

```python
ue_manage_project_ops(operation="level_get_actor_detail", class_name="StaticMeshActor", limit=5)
```

---

### `level_diagnose` / `level_spatial_analysis` / `level_get_graph`

Health rules + suggestions (`level_diagnose`), cell density + clustering
(`level_spatial_analysis`), and Mermaid composition (`level_get_graph`).

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `rules` | str | No | Comma-separated diagnose rule names (omit = all). |
| `max_actors` | int | No | Density warning threshold (default 5000). |
| `max_file_kb` | int | No | Large-file warning threshold in KB (default 100). |
| `max_bp_spread` | int | No | Max levels a BP may appear in (default 20). |
| `max_distance` | number | No | Position outlier distance in UU (default 500000). |
| `max_depth` | int | No | `level_get_graph` max depth (default 3). |
| `level_name` | str | No | Graph root / filter where applicable. |

```python
ue_manage_project_ops(operation="level_diagnose")
ue_manage_project_ops(operation="level_diagnose", rules="density_warning,bp_hotspots", max_actors=3000)
ue_manage_project_ops(operation="level_spatial_analysis")
ue_manage_project_ops(operation="level_get_graph", max_depth=2)
```

---

## World Partition Status (`wp_get_status`)

Level analysis on a World Partition project usually starts by confirming WP is
active. This is the operation the SKILL.md MCP-usage example uses.

**Parameters**: none required. (`world_name` / `map_name` are accepted
schema-wide for `wp_*` ops but are unused by the status path, which scans the
`Content/` dir and `DefaultEngine.ini` directly.)

**Returns**:

```python
{
    "success": True,
    "operation": "wp_get_status",
    "wp_enabled": bool,             # __ExternalActors__ or __ExternalObjects__ present
    "indicators": {
        "external_actors": bool,
        "external_objects": bool
    },
    "config": [ {"key": str, "value": str}, ... ],  # WorldPartitionSettings ini
    "map_count": int,               # .umap files under Content/Maps
    "project_root": str,
    "total_time_ms": float
}
```

**Example** (the corrected form used in SKILL.md):

```python
ue_manage_project_ops(operation="wp_get_status")
```

---

## Common Pitfalls

### ❌ `operation_type` is not a real parameter (Issue #20028)

```python
# WRONG — operation_type is not in the schema. It is silently dropped, and
# operation="smart" then auto-routes (often to the wrong op).
# example-smoke: ignore
ue_manage_project_ops(operation="smart", operation_type="wp_get_status")

# RIGHT — pass the concrete operation value directly.
ue_manage_project_ops(operation="wp_get_status")
```

The schema (`schema_registry/project_ops.rs`) defines only `operation` as the
router. Any `operation_type=...` you pass is dropped before dispatch, so the
call falls back to whatever `operation` you gave (here `"smart"`), and the
intended `wp_get_status` never runs.

### ❌ Non-WP projects

`wp_get_status` reports `wp_enabled: false` when neither `__ExternalActors__`
nor `__ExternalObjects__` exists. The whole skill requires a World Partition
project (see `compatibility` in SKILL.md).

---

## See Also

- [SKILL.md](SKILL.md) — workflow + argument routing.
- `ue_manage_project_ops` schema — the full operation surface across four
  domains (World Partition / DDC / Source Control / Level Content). (The exact
  op count is the schema enum in `project_ops.rs`, not a fixed number quoted
  here — the tool's own docstring count has drifted from the enum before.)
