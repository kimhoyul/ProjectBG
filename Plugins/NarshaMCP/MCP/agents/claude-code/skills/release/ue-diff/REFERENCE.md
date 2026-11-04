# UE Diff — Reference

**Version**: 1.2.0
**Tool**: `ue_diff`
**Operations**: 23 (`auto` is an alias of `smart`)

## ue_diff Operations (Exact)

`ue_diff.operations`: smart, auto, replication_audit, diff_hierarchy, compare_configs, compare_config_chain, compare_assets, extract_asset, render_blueprintue, compare_revision, verify_identical, merge_preview, bisect_asset, diff_asset_revision, snapshot, compare_to_snapshot, compare_folders, diff_level, diff_datatable, compare_cooked_builds, export_changeset, compare_collections, open_visual_diff

---

## Smart Routing Priority

| Priority | Condition | Routes To |
|----------|-----------|-----------|
| 0 | `level_a` or `level_b` present | `diff_level` |
| 0a | `table_a` or `table_b` present | `diff_datatable` |
| 0b | `cook_a` or `cook_b` present | `compare_cooked_builds` |
| 0c | `collection_a` or `collection_b` present | `compare_collections` |
| 1 | `folder_a` or `folder_b` present | `compare_folders` |
| 2 | `asset_path` + `revision_good` present | `bisect_asset` |
| 3 | `asset_path` without `revision_good` | `diff_asset_revision` |
| 4 | `base_asset` or `base_revision` present | `merge_preview` |
| 5 | `revision_a` or `revision_b` present | `compare_revision` |
| 6 | `pairs` present | `verify_identical` |
| 7 | `scope` present | `snapshot` |
| 8 | `label` present without `scope` | `compare_to_snapshot` |
| 9 | `asset_a` or `asset_b` present | `compare_assets` |
| 10 | `config_files`, or `config_category` + `platform`, present | `compare_config_chain` |
| 11 | `config_a` or `config_b` present | `compare_configs` |
| 12 | `base_class` present | `diff_hierarchy` |
| 13 | `class_name` with replication hint | `replication_audit` |
| 14 | `class_name` without replication hint | `diff_hierarchy` |

`auto` follows this same routing table. `extract_asset` and
`render_blueprintue` are intentionally explicit and are never smart-routed.

---

## Parameters

### Common
| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `operation` | string | `smart` | Operation to execute; `auto` is an alias of `smart` |
| `limit` | integer | 50 | Max results per response |
| `project_root` | string | server project path | Consumed by `replication_audit`, `compare_configs`, `compare_config_chain`, `compare_revision`, revision-mode `merge_preview`, `bisect_asset`, `diff_asset_revision`, `snapshot`, `compare_to_snapshot`, `compare_folders`, and `diff_level` |

### replication_audit
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `class_name` | string | Yes | Target class (e.g., `ALyraPlayerState`) |

### diff_hierarchy
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `class_name` | string | Yes | Project class |
| `base_class` | string | No | Engine base class (auto-detected from PDB if omitted) |

### compare_configs
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `config_a` | string | Yes | First config file path or name |
| `config_b` | string | Yes | Second config file path or name |
| `section` | string | No | Filter to specific INI section |

### compare_config_chain
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `config_files` | string \| array | Conditional | Manual mode: config files in override order |
| `config_category` | string | Conditional | Auto mode: config category; requires `platform` |
| `platform` | string | Conditional | Auto mode platform |
| `engine_root` | string | No | Optional Unreal Engine root for engine-owned layers |
| `section` | string | No | Filter to specific INI section |

### compare_assets
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_a` | string | Yes | First asset — file path or asset name |
| `asset_b` | string | Yes | Second asset — file path or asset name |
| `asset_type` | string | No | Response-label override only. Native-class extraction still auto-detects the typed extractor. Schema labels: `blueprint, material, niagara, pcg, statetree, datatable, animation, generic` |

The same `asset_type` parameter filters inferred changed-asset classes in
`compare_revision` and overrides only the response label in both path and
revision modes of `merge_preview`.

### extract_asset
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset` | string | Yes | Single `.uasset` path or resolvable asset reference |

### render_blueprintue

```python
ue_diff(operation="render_blueprintue", asset="C:/absolute/path/BP_Player.uasset")
```

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset` | string | Yes | Absolute path to an existing `.uasset`; native Blueprint-family packages only. Project-external paths are allowed. |

This exact-file offline operation reads source bytes once and does not use the
asset resolver, metadata cache, or execution cache. It rejects `format`,
`project_root`, and `asset_type`. The HTML is byte-identical for the same source
bytes and provider version.

Success fields are `success`, `operation`, `asset_type`, `blueprint_name`,
`source_sha256`, `source_size_bytes`, `output_format="html"`, `node_count`,
`pin_count`, `edge_count`, `graph_count`, `graphs`, `provider_version`, and
`total_time_ms`. Legacy MCP clients additionally receive inline `html_content`.
MCP protocol 2025-06-18 clients receive `resource_link`, `ui_resource_uri`, and
`output_delivery="ui_resource"`; retrieve the exact HTML through
`resources/read`.

Failures return `success=false`, `operation`, `error_code`, `error`, and
`total_time_ms`. After bytes have been read, source identity is preserved.
Stable error codes: `invalid_asset_path`, `read_failed`, `parse_failed`,
`unsupported_asset_type`, `graph_data_unavailable`, `render_failed`, and
`internal_error`.

### compare_revision
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `revision_a` | string \| integer | Yes | Base revision (SVN number, Git hash, P4 changelist) |
| `revision_b` | string \| integer | No | Target revision (default: working copy / HEAD) |
| `vcs_type` | string | No | VCS type: `svn`, `git`, `p4` (auto-detected if omitted) |
| `filter` | string | No | Case-insensitive glob filter; also consumed by `compare_folders` and `snapshot` |

### verify_identical
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_a`, `asset_b` | string | Conditional | Single asset pair |
| `pairs` | array | Conditional | Batch of `[asset_a, asset_b]` pairs; overrides the single pair |
| `deep` | boolean | No | Include per-export hashing for byte-different files |

### merge_preview
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `base_asset`, `asset_a`, `asset_b` | string | Path mode | Base, ours, and theirs asset references |
| `base_revision`, `revision_a`, `revision_b` | string | Revision mode | Revisions for the path supplied in `asset_a` |
| `vcs_type` | string | No | `svn`, `git`, or `p4` |

### bisect_asset / diff_asset_revision
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | string | Yes | Working-tree/repository `.uasset`; `revision_good` selects `bisect_asset` in smart mode |
| `revision_good` | string | bisect_asset | Known-good Git revision |
| `revision_bad` | string | No | Known-bad Git revision (default: `HEAD`) |
| `field_path` | string | No | Semantic diff-path substring for bisection |
| `revision_a` | string \| integer | No | Revision for `diff_asset_revision` (default: `HEAD`) |

### snapshot / compare_to_snapshot
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `label` | string | Yes | Snapshot label under `Saved/NarshaMCP/diff_snapshots/` |
| `scope` | string | snapshot | Project-relative directory to baseline |
| `filter` | string | No | Snapshot-time glob filter; compare-to-snapshot reuses the stored filter |

### compare_folders
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `folder_a`, `folder_b` | string | Yes | Directory paths or project/content folder references |
| `filter` | string | No | Case-insensitive glob filter |
| `per_file_limit` | integer | No | Typed diff-entry cap per changed asset |

### diff_level
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `level_a`, `level_b` | string | Yes | `.umap` paths, mounted level paths, or bare names |
| `move_threshold` | number | No | Minimum position delta reported as moved (default: `1.0`) |

### diff_datatable
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `table_a`, `table_b` | string | Yes | DataTable/CurveTable paths, `/Game/...` mounts, or names (`asset_a`/`asset_b` aliases accepted) |





### open_visual_diff
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_a`, `asset_b` | string | Yes | Assets to open in editor Diff window |

Editor enrichment (Rule 20). Offline: structured hint to `compare_assets`. Explicit `operation=open_visual_diff` required.

### export_changeset
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_a`, `asset_b` | string | Conditional | Live pair — runs compare_assets then maps changes |
| `changes` | array | Conditional | Precomputed compare_assets changes[] (skips re-diff) |
| `direction` | string | No | `a_to_b` (default) or `b_to_a` |
| `target_asset` | string | No | Target label when using changes[] only |

Read-only machine-applicable JSON (`kind: narsha_diff_changeset`). Apply is write-tool responsibility (not git format-patch). Explicit `operation=export_changeset` required (not smart-routed from asset_a/asset_b).

### compare_collections

```
ue_diff(operation="compare_collections", collection_a="MyCol", collection_b="MyCol_v2")
# or full paths: Content/Collections/MyCol.collection
```

Offline UE `.collection` text set/folder/meta drift. Reports assets only-in-A/B, top-level `/Game/<Folder>` count deltas, and metadata key changes. Smart-routes on `collection_a`/`collection_b`.

### compare_cooked_builds
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `cook_a`, `cook_b` | string | Yes | Loose-cook output trees (Content or `Saved/Cooked/...`); `folder_a`/`folder_b` aliases accepted |
| `filter` | string | No | Case-insensitive glob filter |
| `platform` | string | No | Optional label echoed in the response |
| `limit` | integer | No | Cap for added/removed/changed lists (default shared with other ops) |

v1 mode is **loose cook only**: IoStore/pak containers (`.ucas`/`.utoc`/`.pak`/`.sig`) are counted as skipped and never hashed. Response includes `summary.deterministic` plus per-path SHA-256 for changed files.

---

## Response Schemas

### compare_assets
```json
{
  "operation": "compare_assets",
  "success": true,
  "asset_type": "NiagaraSystem",
  "asset_a": { "path": "...", "name_count": 245, "import_count": 32, "export_count": 18 },
  "asset_b": { "path": "...", "name_count": 251, "import_count": 34, "export_count": 19 },
  "summary": {
    "total_changes": 42,
    "typed_changes": 30,
    "generic_changes": 12,
    "added": 15,
    "removed": 10,
    "modified": 17,
    "diff_method": "typed+generic",
    "truncated": false
  },
  "changes": [
    { "path": "typed.emitters[0].name", "type": "modified", "old": "Sparks", "new": "Flames" },
    { "path": "names.ExplosionForce", "type": "added", "new": "ExplosionForce" }
  ],
  "total_time_ms": 45.2
}
```

### compare_revision
```json
{
  "operation": "compare_revision",
  "success": true,
  "vcs_type": "svn",
  "revision_a": "5948",
  "revision_b": "5950",
  "summary": {
    "total_files_changed": 15,
    "files_processed": 15,
    "uasset_files": 3,
    "text_files": 8,
    "config_files": 2,
    "skipped_files": 2,
    "truncated": false
  },
  "files": [
    {
      "path": "Content/Characters/BP_Hero.uasset",
      "status": "modified",
      "file_type": "uasset",
      "diff_method": "typed+generic",
      "summary": { "total_changes": 12, "typed_changes": 8, "generic_changes": 4 }
    },
    {
      "path": "Source/MyGame/MyCharacter.cpp",
      "status": "modified",
      "file_type": "source",
      "diff_method": "text",
      "summary": { "lines_added": 45, "lines_removed": 12 }
    }
  ],
  "total_time_ms": 2345.6
}
```

---

## Typed Diff — 80 Known Asset Types

`compare_assets` performs **typed** (semantic-field) extraction for **80 known asset types** (see the module doc of [analysis_diff_assets_typed.rs](../../../MCP/native/mcp_server/src/tools/analysis_diff_assets_typed.rs): "for 80 known asset types"; grown `9 core → 35 → 80` across #8726/#8738/#8743). The table below lists only the **major families** as a representative sample — it is NOT the full set. Anything not covered by a dedicated extractor (or `AssetType::Unknown`) falls back to **generic diff** (names, imports, exports comparison).

| Family (representative) | Example AssetType Variants | Semantic Fields |
|------|-------------------|-----------------|
| Blueprint | Blueprint, BlueprintGeneratedClass, AnimBlueprint, AnimationBlueprint, WidgetBlueprint | nodes, variables, functions, interfaces, connections |
| Material | Material, MaterialInstance, MaterialFunction, MaterialParameterCollection | parameters, connections, nodes |
| Niagara / VFX | NiagaraSystem, NiagaraEmitter, ParticleSystem | emitters, parameters, renderers |
| Data | DataTable, CurveTable, DataAsset, PrimaryDataAsset | rows, properties |
| AI | StateTree, BehaviorTree, EnvironmentQuery, BlackboardData, ChooserTable | tasks, conditions, transitions |
| Gameplay / Input | GameplayEffect, GameplayAbility, InputAction, InputMappingContext | modifiers, tags, cost, cooldown, bindings |
| Animation | AnimSequence, AnimMontage, BlendSpace, LevelSequence, Skeleton | tracks, notifies, blend params |
| Mesh / Physics | StaticMesh, SkeletalMesh, PhysicsAsset, PhysicalMaterial | LODs, sockets, bodies |

Many more families are covered (PCG, Rigging/ControlRig, Texture/Sound/MetaSound/Media, Landscape/Foliage, Groom, IKRig, UserDefinedEnum/Struct, …). See the `diff_typed` dispatch for the authoritative list.

---

## Error Messages

Exact messages emitted by the handlers (cross-checked in Issue #20718 — do not paraphrase):

| Error (exact) | Cause | Recovery |
|-------|-------|----------|
| `Could not find asset_a: '{path}'` / `Could not find asset_b: '{path}'` | Path/name did not resolve to a file | Check path, use `ue_glob` to find |
| `Failed to parse asset_a '{path}': {err}` / `Failed to parse asset_b '{path}': {err}` | Corrupt or unsupported format | Verify file is valid `.uasset` |
| `compare_assets requires 'asset_a'` / `compare_assets requires 'asset_b'` | Missing parameter | Provide both `asset_a` and `asset_b` |
| `Could not detect VCS in '{path}'. No .svn, .git, or .p4config found.` | No `.svn`/`.git`/`.p4config` | Provide `vcs_type` explicitly, or pass `project_root` at a working tree |
| `Failed to get changed files from {Vcs} in '{path}': {err}` | SVN/Git/P4 CLI error (`{err}` = e.g. `git diff failed: …`) | Check VCS installation; confirm the dir is a working tree |
| `Header file for class '{name}' not found in Source/` | Class `.h` not found | Check class name spelling (the handler already tries with/without the `A`/`U`/`F` prefix) |
| `Cannot auto-detect base class for '{name}'. PDB data may not be loaded.` | PDB not loaded | Provide `base_class` explicitly |
