---
name: per-module
model: opus
description: "Generate per-module project documentation (OVERVIEW, CLASSES, BLUEPRINTS per subsystem) from NPS intermediate data. Issue #8979 / Epic #8934. Triggers: '모듈 문서', 'per module docs', 'generate module docs', 'render project modules'."
compatibility: "Requires NarshaMCP MCP server (v0.9.10+) and a UE 5.x project already scanned via /project-scan"
allowed-tools: [Bash, Read, Glob, Grep, Write, Skill]
argument-hint: "<project_root> [--docs-root <dir>] [--enrich] [--engine-version <ver>]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8979"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# per-module

Converts the NPS intermediate data produced by `/project-scan` into the
per-project markdown tree (README / modules / subsystems / content /
overview, depending on project_type). Runs in two phases:

1. **Deterministic skeleton** (Rust `module_renderer`) — fast, reproducible,
   wraps every auto region with `<!-- NARSHA:AUTO:START/END -->` fences.
2. **LLM enrichment** (optional, `--enrich`) — orchestrates the three
   existing engine-docs skills against the scanned modules so the skeleton
   gains narrative content:
   - `module-analyzer` → richer `MODULE.md` / `CLASSES.md`
   - `example-extractor` → `EXAMPLES.md`
   - `troubleshoot-generator` → `TROUBLESHOOT.md`

## Usage

```bash
/per-module <project_root>
/per-module <project_root> --enrich               # adds LLM-generated docs
/per-module <project_root> --docs-root Docs/Narsha
```

If `<project_root>` is omitted, use `$UECODEGEN_PROJECT_DIR` from `.env`.

## Parse arguments

1. Positional 1 (required): `project_root`
2. `--docs-root <dir>` → output directory, default `<project_root>/Docs/Narsha`
3. `--enrich` → enable phase 2 (LLM enrichment). Skipped by default because
   it makes many LLM calls.
4. `--engine-version <ver>` → overrides `engine_version` in emitted metadata.

## Workflow

### Phase 0 — Preflight

Verify `/project-scan` has already produced `<docs_root>/_data/project_scan.nps.json`.
If it hasn't, run `/project-scan` first (bail out with instruction).

### Phase 1 — Deterministic skeleton

Invoke `narshamcp::project_docs::scanner::scan` (same as `/project-scan`) —
the returned `ScanBundle.md_tree` is a `Map<relative_path, content>` ready
to write. Each file:
- wraps auto-generated content in `<!-- NARSHA:AUTO:START/END -->`;
- is safe to overwrite **up to the END marker** — content after it is
  user-owned and must be preserved via a merge pass.

Per project_type, the emitted paths are:

| project_type | Emitted paths |
|---|---|
| `gameplay_project`, `open_world` | `README.md`, `modules/<M>/MODULE.md`, `modules/<M>/subsystems/<S>/{MODULE,CLASSES,BLUEPRINTS}.md` for each independent `<S>`; `_misc/` bucket only if there are merged classes |
| `bp_showcase` | `README.md`, `content/topics/OVERVIEW.md` (topic pages are user-added under `content/topics/*.md`) |
| `small_sample` | `overview.md`, `content/<Module>.md` per module (flat) |
| `asset_library` | `README.md` only |

Merge logic (for existing files with user edits):
1. Read existing file.
2. If it has `NARSHA:AUTO:END`, keep everything after that marker verbatim.
3. Write `<new_auto_region>` + `<preserved_tail>`.
4. If no marker is found, treat the whole file as auto and overwrite.

Write `_data/module_<name>.nps.json` for every module (already produced by
the scan bundle as `ScanBundle.module_npses`).

### Phase 2 — LLM enrichment (only with `--enrich`)

For each module in `ScanBundle.modules`:

1. Call `module-analyzer` skill with `module_path=<root>/<module.path>`
   — replaces the auto region of `modules/<M>/MODULE.md` with its output.
2. For each independent subsystem with ≥ 3 classes, pick the most-visited
   class (first in the NPS list) and call `example-extractor` to produce
   `modules/<M>/subsystems/<S>/EXAMPLES.md`.
3. Call `troubleshoot-generator` per module → `modules/<M>/TROUBLESHOOT.md`.

> **⚠️ Enrichment uses many LLM calls**. Default is off — run it only after
> the skeleton is verified.

### Phase 3 — Report

```
Project:      <name>
Project type: <type>
Skeleton:     <N md files emitted>
Modules:      <N>
Subsystems:   <N independent / <N misc>
Enrichment:   <on|off>
Output:       <docs_root>
```

## Acceptance criteria (Issue #8979)

Target file counts per project type (see `docs/epics/project-docs-system/ANALYSIS_*.md`):

| Project | Expected count |
|---|---|
| Lyra              | ~50 md (10 subsystems × 5 per module/subsystem) |
| ContentExamples   | 56 topic md (user-edited) |
| StackOBot         | `overview.md` + ~10 `content/*.md` |
| MHCLighting       | `README.md` only |

Skeleton tests (see `tests/project_docs_per_module.rs`) verify the layout
shape; enrichment tests are deferred until `--enrich` is live.

## See also

- Schema: `docs/specs/project-docs-schema/`
- Directory templates: `docs/specs/project-docs-schema/directory_templates.md`
- Parent skills: `.claude/skills/module-analyzer/`, `example-extractor/`, `troubleshoot-generator/`
- Sub-issue draft: `docs/epics/project-docs-system/sub-issues/06-per-module-gen.md`
