---
name: project-scan
model: opus
description: "Scan a UE project and produce the NPS intermediate files (_index.json, _data/project_scan.nps.json, _data/file_hashes.nps.json). Foundation of Epic #8934 Project Docs System. Triggers: '프로젝트 스캔', 'project scan', 'generate NPS', 'refresh project_scan.nps.json'."
compatibility: "Requires NarshaMCP MCP server (v0.9.10+) with Unreal Engine 5.x project"
allowed-tools: [Bash, Read, Glob, Grep, Write]
argument-hint: "<project_root> [--no-hash] [--docs-root <dir>] [--engine-version <ver>]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8977"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# project-scan

Produces the three NPS (Narsha Project Schema) intermediate documents that
downstream issues (#8979 Per-Module, #8984 Orchestrator, #8986 Delta Regen)
consume:

| File | Role |
|---|---|
| `<docs_root>/_index.json` | User-editable project metadata + overrides (project_type, subsystems_override, hysteresis_overrides) |
| `<docs_root>/_data/project_scan.nps.json` | Full scan: modules, subsystems, plugins, file counts, classification |
| `<docs_root>/_data/file_hashes.nps.json` | Normalized SHA-256 per source file, for delta regen |

**Backend**: `narshamcp::project_docs::scanner::scan` (Rust). The scanner runs
offline (CLAUDE.md Rule 20 — no Editor required). Subsystem detection applies
the [HYSTERESIS_SPEC.md] dead-zone rules so repeated scans don't flap around
file-count boundaries.

## Usage

```bash
/project-scan <project_root>
/project-scan <project_root> --no-hash              # skip per-file SHA-256
/project-scan <project_root> --docs-root Docs/Narsha  # custom output location
```

If `<project_root>` is omitted, use `$UECODEGEN_PROJECT_DIR` from `.env`.

## Parse arguments

1. Parse `$ARGUMENTS`:
   - Positional 1 (required): `project_root` — absolute path to the `.uproject` directory.
   - `--no-hash` → set `compute_hashes = false` (fast path; skips file_hashes.nps.json contents).
   - `--docs-root <dir>` → output directory, default `<project_root>/Docs/Narsha`.
   - `--engine-version <ver>` → override `engine_version` in the emitted metadata, default "unknown".

2. If `project_root` is missing, read `UECODEGEN_PROJECT_DIR` from `.env` via Bash.

## Workflow

### Phase 1 — Preflight

1. Verify `<project_root>/*.uproject` exists. If not, abort with a clear error.
2. Ensure `<docs_root>/_data/` and `<docs_root>/_meta/` exist (create if missing).

### Phase 2 — Load prior state (hysteresis)

Before scanning, read the previous state so hysteresis can keep stable
classifications:

1. If `<docs_root>/_index.json` exists → parse for `project_type`,
   `subsystems_override`, `hysteresis_overrides`.
2. If `<docs_root>/_meta/subsystem_detection.json` exists → parse for
   previous per-subsystem status map.

Missing files are fine — first-run scans start with no hysteresis input.

### Phase 3 — Scan

Invoke the Rust scanner. Until the orchestrator issue (#8984) wires this to an
MCP tool operation, the skill documents the invocation; implementers should
expose `narshamcp::project_docs::scanner::scan(project_root, &prior_state, &options)`
through an internal helper and call it from their integration path (CLI, test,
or future MCP op).

The scanner produces a `ScanBundle` with:

- `index`         → write to `<docs_root>/_index.json`
- `project_scan`  → write to `<docs_root>/_data/project_scan.nps.json`
- `file_hashes`   → write to `<docs_root>/_data/file_hashes.nps.json`
- `summary`       → for the skill's final report

### Phase 4 — Write outputs

Write the three JSON values to disk, pretty-printed with 2-space indent.

> **_index.json is user-editable**. If it already exists, **merge** rather
> than overwrite: preserve any keys the user has added (notes, TODOs). Only
> refresh `project_name`, `project_type`, `engine_version`, `confidence`,
> `reason` — leave `subsystems_override`, `hysteresis_overrides`, and any
> unknown keys untouched.

### Phase 5 — Report

Output a summary table:

```
Project:     <project_name>
Type:        <project_type> (confidence <0.xx>)
Reason:      <classifier reason>
Modules:     <count>
Subsystems:  <count independent>
Plugins:     <count> (first: <n>, third: <n>)
Hashed:      <count> files
Output:      <docs_root>
  _index.json
  _data/project_scan.nps.json
  _data/file_hashes.nps.json
```

## Acceptance criteria (Issue #8977)

Run against the six reference projects (see `docs/epics/project-docs-system/ANALYSIS_*.md`):

| Project | Expected type | Modules | Plugins (1st/3rd) |
|---|---|---|---|
| LyraStarterGame   | gameplay_project | 1 | 18 total |
| LCP_D             | gameplay_project | >1 | 78 (5/73) |
| ContentExamples   | bp_showcase      | 0 | 0 |
| ElectricDreamsEnv | open_world       | 1 | 3 |
| StackOBot         | small_sample     | 0 | 16 |
| MHCLightingPresets| asset_library    | 0 | 10 |

If any mismatch, inspect `summary.reason` — it explains which rule matched.

## See also

- Schema: [`docs/specs/project-docs-schema/`](../../../docs/specs/project-docs-schema/)
- Hysteresis rules: [`docs/epics/project-docs-system/HYSTERESIS_SPEC.md`](../../../docs/epics/project-docs-system/HYSTERESIS_SPEC.md)
- Sub-issue draft: [`docs/epics/project-docs-system/sub-issues/04-project-scan.md`](../../../docs/epics/project-docs-system/sub-issues/04-project-scan.md)
