---
name: world-section-doc
model: opus
description: "Generate the world/*.md tree (WORLD_PARTITION, DATA_LAYERS, HLOD, STREAMING, PCG_SYSTEMS, LANDSCAPE, WATER, ENVIRONMENT_ASSETS) for open_world projects. Issue #8982 / Epic #8934. Triggers: '월드 문서', 'world section doc', 'WP doc', 'render world'."
compatibility: "Requires NarshaMCP MCP server (v0.9.10+) and a UE 5.x open_world project already scanned via /project-scan"
allowed-tools: [Bash, Read, Glob, Grep, Write]
argument-hint: "<project_root> [--enrich] [--docs-root <dir>]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8982"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# world-section-doc

Phase-3 companion. Emits up to 8 `world/*.md` files plus
`_data/world_partition.nps.json` for `open_world`-classified projects.

**Offline Binary First** (CLAUDE.md Rule 20): 90%+ of the data comes from
plaintext filesystem signals. STREAMING.md is the only fully Editor-dependent
file; others populate partially offline and degrade gracefully.

| File | Offline source | Editor enrichment |
|---|---|---|
| WORLD_PARTITION.md | `Content/__ExternalActors__/**` tree + sizes | streaming source positions |
| DATA_LAYERS.md | `Content/**/DL_*.uasset` | runtime state, actor refs |
| HLOD.md | `Content/**/__HLOD__/**` + mtime | authoritative build state |
| STREAMING.md | template + `[REQUIRES_EDITOR]` badge | full population |
| PCG_SYSTEMS.md | `Content/PCG/**` + `PCG_*` prefix | node count + parameters |
| LANDSCAPE.md | `Content/Landscape/**` roster | materials, components |
| WATER.md | Water plugin markers + `Water_*` | surface area |
| ENVIRONMENT_ASSETS.md | `Content/Megascans/**` roster | texture resolutions |

## Scope condition

Runs only for `project_type == open_world`. Non-open_world types skip world
emission entirely (world: Value::Null in ScanBundle).

## Usage

```bash
/world-section-doc <project_root>           # offline
/world-section-doc <project_root> --enrich  # + Editor enrichment
```

## Workflow

### Phase 0 — Preflight

1. Verify `<docs_root>/_data/project_scan.nps.json` exists.
2. Verify `project_type == open_world`. If not, bail with a note.

### Phase 1 — Offline scan

Call `narshamcp::project_docs::scanner::scan`. The `ScanBundle` contains
`world: Value` + all `world/*.md` entries in `md_tree`. Write using
NARSHA:AUTO merge (preserve tail past END marker).

Conditional emission:
- `world/WORLD_PARTITION.md`, `HLOD.md`, `STREAMING.md` — always emitted
- `DATA_LAYERS.md` — only if `DL_*` assets exist
- `PCG_SYSTEMS.md` — only if PCG graphs found
- `LANDSCAPE.md` / `WATER.md` / `ENVIRONMENT_ASSETS.md` — only if signals present

### Phase 2 — Enrichment (only with `--enrich`)

- `ue_manage_project_ops(wp_get_status)` → WP activation / method
- `ue_manage_project_ops(wp_validate_cells)` → full cell dataset
- `ue_manage_project_ops(wp_list_data_layers)` → runtime state
- `ue_manage_project_ops(wp_get_hlod_status)` → authoritative build state
- `ue_manage_pcg(search_pcg + get_structure)` → node counts per graph
- Editor streaming sources for STREAMING.md

### Phase 3 — Report

```
Project:            <name>
Project type:       open_world
External actors:    <N>
DataLayers:         <N>
PCG graphs:         <N>
Landscape / Water / Megascans: <N> / <N> / <N>
Files emitted:      <N> (max 8)
Enrichment:         <on|off>
```

## Acceptance criteria (Issue #8982)

| Project | Expected | Offline | Enriched |
|---|---|---|---|
| ElectricDreams | 20,727 external actors, 7 DataLayers, 109 PCG | covered offline | — |
| STREAMING.md | [REQUIRES_EDITOR] badge | required | replaced by real data |

## See also

- Spec: `docs/epics/project-docs-system/WORLD_SECTION_SPEC.md`
- Schema: `docs/specs/project-docs-schema/world_partition.schema.json`
- Backend: `MCP/native/mcp_server/src/project_docs/world_builder.rs` + `world_renderer.rs`
- Sub-issue draft: `docs/epics/project-docs-system/sub-issues/09-world-section-doc.md`

## MCP Tool Usage

```python
# Generate world section documentation
ue_project_docs(operation="search", query="WorldPartition")
ue_manage_pcg(operation="smart", asset_name="PCG_Terrain")
```
