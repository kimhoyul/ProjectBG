---
name: project-docs-generate
model: opus
description: "Top-level skill that generates the full Narsha project docs tree (scan → per-module → blueprint-linker → gameplay → world → citation → lint) in one command. Epic #8934 / Issue #8989. Triggers: '프로젝트 문서 생성', 'generate project docs', 'project docs generate', '나샤 문서', 'narsha docs', 'refresh project docs', '프로젝트 문서 갱신', 'docs generate'."
compatibility: "Requires NarshaMCP MCP server (v0.9.10+) and a UE 5.x project with a `*.uproject` file"
allowed-tools: [Bash, Read, Glob, Grep, Write, Skill, Task]
argument-hint: "[project_root] [--type <type>] [--reclassify] [--rename-hint old=new] [--version-tag <ver>] [--delta-only] [--docs-root <dir>] [--enrich] [--strict-lint] [--enable-pdf-conversion] [--enable-url-fetch]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8989"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# project-docs-generate

One-command entry point for Epic #8934. Produces the complete
`<project>/Docs/Narsha/` tree by chaining every Phase 3–5 component the
project has accumulated:

```
project-scan → per-module → blueprint-linker → gameplay-flow-doc
             → world-section-doc → citation validation → project-docs-lint
```

All heavy work lives in the NarshaMCP Rust backend
(`MCP/native/mcp_server/src/project_docs/`), surfaced via the
`ue_project_docs` MCP tool. The skill layers:

- **concurrency guard** (`.narsha-lock`)
- **ignore patterns** (`.narsha-docignore`)
- **flag plumbing** (`--type`, `--reclassify`, `--rename-hint`, `--version-tag`, `--delta-only`)
- **agent dispatch** → `project-doc-orchestrator` handles the heavy chain,
  calling `ue_project_docs(operation="generate"|"search"|"read"|...)` as needed

## When to invoke

- "프로젝트 문서 생성해줘"
- "Generate Narsha docs for this project"
- After a large refactor or when `Docs/Narsha/` is out of date
- In CI before release (see [`ci-template.md`](../../../docs/epics/project-docs-system/sub-issues/14-ci-template.md))

## Usage

```bash
/project-docs-generate                              # project_root = $UECODEGEN_PROJECT_DIR
/project-docs-generate I:/Lyra/LyraStarterGame      # explicit project
/project-docs-generate --delta-only                 # regen only changed subsystems
/project-docs-generate --reclassify                 # ignore cached project_type
/project-docs-generate --type open_world            # force project_type
/project-docs-generate --rename-hint "OldName=NewName" # one-shot module rename
/project-docs-generate --version-tag 5.7            # stamp engine_version
/project-docs-generate --enrich                     # include LLM enrichment (many calls)
/project-docs-generate --strict-lint                # exit non-zero on lint errors (CI)
/project-docs-generate --enable-pdf-conversion      # Issue #9826 Phase B: convert PDFs/MDs/images under Docs/{Design,Concepts,References}/ → normalized MD under <docs_root>/references/converted/ (requires narshamcp built with --features pdf-extraction)
/project-docs-generate --enable-url-fetch           # PR 5 of #9826 (stub): fetch URLs from Docs/References/urls.txt (requires --features url-fetch)
```

### Issue #9826 Phase B flags (default OFF — Rule 20)

`--enable-pdf-conversion` and `--enable-url-fetch` forward to the `ue_project_docs(operation="scan")`
MCP tool as `enable_reference_conversion` and `enable_url_fetch` booleans respectively. The daemon
must be built with the matching Cargo features (`pdf-extraction` and/or `url-fetch`); otherwise the
per-entry conversion outcome surfaces as `FeatureDisabled` and the lint report flags it. Phase A
inventory remains byte-stable when both flags are off.

Once `Docs/Narsha/` is generated the `ue_project_docs` MCP tool services lookups:

```python
ue_project_docs(operation="read", path="modules/LyraGame/MODULE.md", level=2)
ue_project_docs(operation="list", path="modules/")
ue_project_docs(operation="grep", pattern="GAS ability", path="gameplay/")
```

## Parse arguments

1. **Positional 1** (optional): `project_root`. If missing, read `UECODEGEN_PROJECT_DIR` from `.env`.
2. `--type <type>` → override project_type (`gameplay_project` | `open_world` | `bp_showcase` | `small_sample` | `asset_library`). Writes to `_index.json.project_type` before scan.
3. `--reclassify` → delete `_index.json.project_type` / `_meta/subsystem_detection.json` before scan so classification runs fresh.
4. `--rename-hint "old=new"` → write to `_index.json.rename_hints` (one-shot; cleared after a successful run).
5. `--version-tag <ver>` → passes through to the scanner (`engine_version` in emitted metadata).
6. `--delta-only` → set `WriteOptions.delta_mode = true`. Skips rewriting files whose subsystems were not touched.
7. `--docs-root <dir>` → default `<project_root>/Docs/Narsha`.
8. `--enrich` → chain per-module / blueprint-linker / gameplay-flow-doc / world-section-doc in their `--enrich` variants (LLM calls).
9. `--strict-lint` → lint step runs with `--strict`; fail fast on errors.

## Workflow

### Phase 0 — Preflight

1. Resolve `<project_root>`. If not a directory or missing `*.uproject`, abort with actionable error.
2. Ensure `<docs_root>` exists; create `_data/`, `_meta/` subdirs.
3. **Lock check** — if `<docs_root>/.narsha-lock` exists and its PID is still alive, abort:
   ```
   Error: Another project-docs-generate run is in progress
     Lock file: <docs_root>/.narsha-lock
     Held by PID: <pid> (started <ts>)
   If the PID is stale, delete the lock file and retry.
   ```
   Otherwise write our own lock:
   ```json
   { "pid": <current_pid>, "started_at": "<iso8601>", "skill": "project-docs-generate" }
   ```
4. **Ignore patterns** — if `<docs_root>/.narsha-docignore` exists, parse it (gitignore-style). Pass patterns through to the scanner via `ScanOptions.ignore_patterns` so matching files are skipped during walk.

### Phase 1 — Classification overrides

If `--type` / `--reclassify` / `--rename-hint` were given, mutate `_index.json` **before** invoking the scanner so hysteresis picks up the override:

- `--type <t>` → set `"project_type": "<t>"`, `"override": "manual"`.
- `--reclassify` → delete `"project_type"`, `"override"`, and clear `_meta/subsystem_detection.json`.
- `--rename-hint "A=B"` → set `"rename_hints": { "A": "B" }`.

### Phase 2 — Orchestrate

Dispatch to the **`project-doc-orchestrator`** agent with the resolved flags. That agent owns the whole chain:

```
Agent(subagent_type="project-doc-orchestrator",
      prompt="""
Run the full project-docs generation chain:
  project_root:  <path>
  docs_root:     <path>
  engine_version: <ver|unknown>
  delta_only:    <bool>
  enrich:        <bool>
Return the contents of _meta/generation_report.md when done.
""")
```

Why an agent: the chain makes many tool calls (scanner, writer, optional enrichers, citation, lint), and isolating them in a sub-agent keeps this skill's context lean.

**Fallback path** (agent unavailable / CI headless): run the phases inline via `Skill`:

```bash
/project-scan <root> --docs-root <docs> [--engine-version <v>]
/per-module <root> --docs-root <docs> [--enrich]
/blueprint-linker <root> --docs-root <docs> [--enrich]
# gameplay + world are emitted by per-module's scanner invocation; enrichment only if --enrich
[--enrich] /gameplay-flow-doc <root> --docs-root <docs>
[--enrich] /world-section-doc <root> --docs-root <docs>
/project-docs-lint <root> --docs-root <docs> [--strict]
```

### Phase 3 — Post-run

1. **Clear one-shot hints** — if `--rename-hint` was applied, remove `rename_hints` from `_index.json` (keeping the rest untouched).
2. **Release lock** — delete `<docs_root>/.narsha-lock`.
3. **Report** — display `<docs_root>/_meta/generation_report.md` + a one-line summary:
   ```
   ✓ project-docs-generate complete (<elapsed>)
     Type:     <type>
     Modules:  N  Subsystems: N independent / M misc
     Files:    <N> JSON, <N> MD written (<N> merged, <N> delta-skipped)
     Citations: <valid>/<total> (<gt_coverage>%)
     Lint:     <errors>E <warnings>W  ✓|✗
   ```
4. If `--strict-lint` and lint errors > 0 → exit non-zero so CI fails.

## Lock file format

`<docs_root>/.narsha-lock`:
```json
{
  "pid": 12345,
  "started_at": "2026-04-17T10:30:00Z",
  "skill": "project-docs-generate"
}
```

Checked via `ps -p <pid>` (Linux/macOS) or `tasklist /FI "PID eq <pid>"` (Windows). Stale locks must be removed manually — we don't auto-delete to avoid racing with a live parallel run.

## `.narsha-docignore` format

Same syntax as `.gitignore`. Applied to **source paths** during the scan (never to generated Docs/ files). Example:

```gitignore
# Skip experimental module
Source/Experimental/

# Skip a plugin we don't want documented
Plugins/LegacyStuff/

# Skip generated headers (scanner already skips *.generated.h)
```

The scanner respects these in addition to its built-in filters (`Saved/`, `Intermediate/`, `Binaries/`, `.generated.h`).

## Acceptance criteria (Issue #8989)

| Project | Scenario | Expected |
|---|---|---|
| Lyra | baseline (`/project-docs-generate I:/Lyra/LyraStarterGame`) | success, ~100 md files |
| LCP_D | full pass | success, gameplay + per-module sections emitted |
| ContentExamples | auto-detected `bp_showcase` | emits `content/topics/OVERVIEW.md` only |
| ElectricDreams | auto-detected `open_world` | emits `world/*.md` sections |
| `--delta-only` | after trivial edit | skips unchanged subsystems, rewrites only touched ones |
| `.narsha-lock` | run while another is running | second run aborts with lock error |
| `--strict-lint` with lint error | CI mode | exits non-zero |

## See also

- Orchestrator agent: `.claude/agents/project-doc-orchestrator.md`
- Sub-skills: `/project-scan`, `/per-module`, `/blueprint-linker`, `/gameplay-flow-doc`, `/world-section-doc`, `/project-docs-lint`
- Backend: `MCP/native/mcp_server/src/project_docs/{scanner,writer,delta,lint}.rs`
- CI template: `docs/epics/project-docs-system/sub-issues/14-ci-template.md` (Issue #8990)
- Sub-issue draft: `docs/epics/project-docs-system/sub-issues/13-generate-skill.md`
