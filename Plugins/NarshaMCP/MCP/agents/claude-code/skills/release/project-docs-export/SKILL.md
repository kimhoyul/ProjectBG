---
name: project-docs-export
model: opus
description: "Refresh AI-consumable exports (llms.txt, llms-full.txt, SCHEMA.md, per-page .md.txt/.md.json siblings) for the generated project docs tree. Issue #8992 / Epic #8934. Triggers: 'AI 익스포트', 'llms.txt', 'refresh exports', 'update llms', 'SCHEMA.md 생성', 'AI consumable export', 'docs export'."
compatibility: "Requires NarshaMCP MCP server (v0.9.12+) and a UE 5.x project already scanned via /project-docs-generate"
allowed-tools: [Bash, Read, Glob, Grep, Skill]
argument-hint: "[project_root] [--docs-root <dir>] [--dry-run]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8992"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# project-docs-export

Refreshes the AI-consumable side channel — the artifacts that let **any AI
client (with or without MCP)** consume a project's generated docs:

| Artifact | Purpose |
|---|---|
| `llms.txt` | Jeremy Howard's llms.txt standard — compact index of the project |
| `llms-full.txt` | Whole-project L0 dump (<200 KB target) — single-file wiki view |
| `SCHEMA.md` | Karpathy-style orientation page — conventions + how-to-query |
| `<page>.md.txt` | Plain-text sibling of every .md (markdown syntax stripped) |
| `<page>.md.json` | Structured metadata sibling (headings, entity_refs, citations) |

**Why a separate skill?** `/project-docs-generate` already emits these as
part of its normal writer flow. This skill is the fast path:

- You only want to refresh exports after hand-editing a few .md files
- CI wants to confirm exports are fresh without re-scanning the project
- You're shipping a fork that needs new branding in `SCHEMA.md`

It does **not** re-scan the project — it only re-renders exports from the
already-written `_data/*.nps.json` + `md_tree`.

## Usage

```bash
/project-docs-export                               # project_root = $UECODEGEN_PROJECT_DIR
/project-docs-export I:/Lyra/LyraStarterGame
/project-docs-export --docs-root Docs/Narsha
/project-docs-export --dry-run                     # list what would be written
```

## Parse arguments

1. Positional 1 (optional): `project_root`. If missing, read
   `UECODEGEN_PROJECT_DIR` from `.env`.
2. `--docs-root <dir>` → default `<project_root>/Docs/Narsha`
3. `--dry-run` → list paths only

## Workflow

### Phase 0 — Preflight

1. Verify `<docs_root>/_data/project_scan.nps.json` exists. If not, abort:
   ```
   Error: <docs_root> has no project_scan.nps.json — run /project-docs-generate first.
   ```
2. Check read access to `<docs_root>/_data/` and `<docs_root>/**/*.md`.

### Phase 1 — Rebuild bundle (offline, fast)

Invoke the backend:

```
narshamcp project-docs export --docs-root <docs_root> [--dry-run]
```

Under the hood this loads the existing `_data/*.nps.json` + walks `*.md`
under `<docs_root>`, constructs an ephemeral `ScanBundle`, runs
`project_docs::ai_exports::build_ai_exports`, and writes:

- `<docs_root>/llms.txt`
- `<docs_root>/llms-full.txt`
- `<docs_root>/SCHEMA.md`
- For every `<docs_root>/**/*.md`:
  - `<docs_root>/**/*.md.txt`
  - `<docs_root>/**/*.md.json`

### Phase 2 — Report

```
Exports refreshed.
  llms.txt        <bytes> B
  llms-full.txt   <bytes> B
  SCHEMA.md       <bytes> B
  siblings        <N> .md.txt + <N> .md.json
  elapsed         <ms> ms
```

Warn if:
- `llms.txt` > 10 KB (Jeremy Howard recommends compact)
- `llms-full.txt` > 200 KB (Lyra baseline; larger projects get their own target)

## Acceptance criteria (Issue #8992)

| Check | Target |
|---|---|
| `llms.txt` on Lyra | < 10 KB |
| `llms-full.txt` on Lyra | < 200 KB |
| `SCHEMA.md` at docs root | exists, contains Conventions + How to Query |
| Non-MCP AI (Cursor w/o MCP) can follow links from llms.txt | raw GitHub URLs resolve |
| Every `.md` has sibling `.md.txt` + `.md.json` | 1:1:1 ratio |
| `.md.json` includes `entities` array (filtered by doc_path) | non-empty when entity_refs covers the page |
| `llms.txt` standard compliance | passes https://llmstxt.org/ validator |

## How AI clients consume these

**With MCP** (Claude Code, Cursor-with-MCP, Windsurf, Gemini CLI):
```
ue_project_docs(operation="search", query="...")
ue_project_docs(operation="read", path="modules/Foo/MODULE.md", level=2)
```

**Without MCP** (raw GitHub, any AI with file-read):
1. Read `llms.txt` (<10 KB) — get the compact project map
2. Follow links to specific `.md` files
3. For a single-file overview, read `llms-full.txt` (<200 KB)
4. For symbol-level lookup, parse `_data/entity_refs.nps.json`

Both paths point to the **same source of truth**; the exports are a serving
convenience, not a duplicate.

## See also

- Backend: `MCP/native/mcp_server/src/project_docs/ai_exports.rs`
- Writer integration: `MCP/native/mcp_server/src/project_docs/writer.rs::write_ai_exports`
- Sibling skills: `/project-docs-generate`, `/project-docs-lint`
- User guide: [`docs/user-guides/PROJECT_DOCS_GUIDE.md`](../../../docs/user-guides/PROJECT_DOCS_GUIDE.md)
- Standards:
  - [llms.txt (Jeremy Howard)](https://llmstxt.org/)
  - [Karpathy LLM Wiki](https://gist.github.com/karpathy/442a6bf555914893e9891c11519de94f)
- Sub-issue draft: `docs/epics/project-docs-system/sub-issues/18-ai-consumable-exports.md`
