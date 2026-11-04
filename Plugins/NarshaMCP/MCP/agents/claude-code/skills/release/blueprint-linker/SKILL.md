---
name: blueprint-linker
model: opus
description: "Populate BLUEPRINTS.md + ASSETS.md per subsystem with C++→BP derivation tables and per-subsystem asset inventories. Runs on top of /project-scan + /per-module output. Issue #8980 / Epic #8934. Triggers: 'BP 링크', 'blueprint linker', 'C++ to BP mapping', 'populate BLUEPRINTS'."
compatibility: "Requires NarshaMCP MCP server (v0.9.10+) and a UE 5.x project already scanned via /project-scan"
allowed-tools: [Bash, Read, Glob, Grep, Write]
argument-hint: "<project_root> [--enrich] [--docs-root <dir>]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8980"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# blueprint-linker

Phase-3 companion to `/per-module`. Emits:

- `_data/blueprint_links.nps.json` — `cpp_to_bp_map` + `bp_hierarchy`
  (schema: `blueprint_links.schema.json`)
- `modules/<M>/subsystems/<S>/BLUEPRINTS.md` — C++ base → derived BP table
  plus the BP asset roster
- `modules/<M>/subsystems/<S>/ASSETS.md` — full asset inventory with
  per-category counts and top 10 by name

## Two modes

**Offline (default)** — filename-prefix heuristic. Works without Editor:
- `BP_ShooterMannequin` → guessed base class stem `ShooterMannequin`;
  matched against every C++ class's stem (name minus U/A/F prefix)
  visible in `module_<name>.nps.json`.
- Fast, reproducible, produces enough structure to navigate the docs.

**Enriched (`--enrich`)** — Live MCP call:
- `ue_analyze_symbols(find_cpp_to_bp, class_name=<A/ULyraCharacter>)`
  to replace heuristic rows with authoritative mappings.
- `ue_manage_blueprint(find_bp_references, blueprint_name=<BP_X>)`
  to fill in `bp_hierarchy` parent/child chains.
- Requires a running NarshaMCP session attached to the project.

## Usage

```bash
/blueprint-linker <project_root>            # offline heuristic
/blueprint-linker <project_root> --enrich   # live MCP enrichment
```

## Parse arguments

1. Positional 1 (required): `project_root`
2. `--enrich` → enable the MCP enrichment pass (default: off)
3. `--docs-root <dir>` → output directory, default `<project_root>/Docs/Narsha`

## Workflow

### Phase 0 — Preflight

Verify `<docs_root>/_data/project_scan.nps.json` exists. If not, run
`/project-scan` first and bail.

### Phase 1 — Offline scan (Rust backend)

Call `narshamcp::project_docs::scanner::scan` — the `ScanBundle` already
contains `blueprint_links` (NPS JSON) + `md_tree` entries for every
subsystem's BLUEPRINTS.md and ASSETS.md. Merge into existing files using
the same NARSHA:AUTO:START/END preservation rule as `/per-module`.

### Phase 2 — Enrichment (only with `--enrich`)

For each C++ class named in `module_<name>.nps.json`:

1. Call `ue_analyze_symbols(find_cpp_to_bp, class_name=<name>)` via MCP.
2. Replace heuristic entries in `blueprint_links.nps.json.cpp_to_bp_map`
   with authoritative results.
3. For each Blueprint listed, call
   `ue_manage_blueprint(find_bp_references, blueprint_name=<bp>)`
   to derive `bp_hierarchy` parent/child relationships.
4. Re-render the affected `BLUEPRINTS.md` pages.

> Expect one `find_cpp_to_bp` call per C++ class — can be many LLM /
> MCP requests on a Lyra-sized project. Batch with `ue_batch_editor_operations`
> where possible.

### Phase 3 — Report

```
Project:                <name>
Blueprint assets:       <total>
Subsystems populated:   <N>
C++→BP mappings:        <N heuristic | N authoritative>
Output updated:
  _data/blueprint_links.nps.json
  modules/<M>/subsystems/<S>/BLUEPRINTS.md
  modules/<M>/subsystems/<S>/ASSETS.md
```

## Acceptance criteria (Issue #8980)

- Lyra `ALyraCharacter` → correct Blueprint derivation count
  (needs `--enrich` for authoritative result; offline uses heuristic)
- Each `BLUEPRINTS.md` is a populated markdown table (not empty)
- Each `ASSETS.md` lists per-category counts + top 10 assets

## See also

- Schema: `docs/specs/project-docs-schema/blueprint_links.schema.json`
- Backend: `MCP/native/mcp_server/src/project_docs/blueprint_links.rs`
- Parent skill: `.claude/skills/per-module/`
- Sub-issue draft: `docs/epics/project-docs-system/sub-issues/07-blueprint-linker.md`

## MCP Tool Usage

```python
# Search for Blueprint assets linked to C++ classes
ue_project_docs(operation="search", query="Blueprint derivation")
ue_search_assets(asset_type="blueprint", query="BP_*")
ue_manage_blueprint(operation="smart", blueprint_name="BP_Character")
```
