---
name: gameplay-flow-doc
model: opus
description: "Generate the 8-file gameplay/*.md tree (OVERVIEW, INPUT_FLOWS, ABILITIES, EFFECTS, GAS_TAGS, BALANCE, VALIDATION, GAME_FLOW) for gameplay_project / open_world projects. Issue #8981 / Epic #8934. Triggers: '게임플레이 문서', 'gameplay flow doc', 'GAS docs', 'render gameplay'."
compatibility: "Requires NarshaMCP MCP server (v0.9.10+) and a UE 5.x gameplay project already scanned via /project-scan"
allowed-tools: [Bash, Read, Glob, Grep, Write]
argument-hint: "<project_root> [--enrich] [--docs-root <dir>]"
metadata:
  version: "0.1.0"
  author: "Next-Stage-Inc"
  epic: "#8934"
  issue: "#8981"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# gameplay-flow-doc

Phase-3 companion to `/per-module` and `/blueprint-linker`. Emits:

- `_data/gameplay.nps.json` — GAS system intermediate (schema: `gameplay.schema.json`)
- `gameplay/OVERVIEW.md` — summary + system mermaid
- `gameplay/INPUT_FLOWS.md` — IA / IMC roster
- `gameplay/ABILITIES.md` — GA roster
- `gameplay/EFFECTS.md` — GE roster
- `gameplay/GAS_TAGS.md` — full tag hierarchy tree
- `gameplay/BALANCE.md` — placeholder (needs `--enrich`)
- `gameplay/VALIDATION.md` — offline checks + enrichment-required items
- `gameplay/GAME_FLOW.md` — GameMode → Controller → Pawn chain placeholder

**Scope**: only runs for `project_type ∈ {gameplay_project, open_world}`.
Other types skip gameplay emission (asset_library / bp_showcase / small_sample
do not need GAS docs).

## Two modes

**Offline (default)** — filesystem-only scan. Works without Editor:
- Parses `Config/**/*GameplayTags*.ini` for the tag hierarchy (exact match with
  live Editor data, since tags live in plaintext ini).
- Classifies `Content/**/*.uasset` by filename prefix:
  `GA_*` → ability, `GE_*` → effect, `IA_*` → input action, `IMC_*` → mapping context.
- Populates summary counts + entity rosters; input flows / modifiers /
  balance / consistency stay as placeholders.

**Enriched (`--enrich`)** — Live MCP calls (many):
- `ue_manage_gameplay(build_input_flows)` → populates the 5-stage IA→Tag→GA→GE→Anim chains
- `ue_trace_execution(operation=trace_ability_flow, ability_name=<GA>)` per ability
- `ue_manage_gameplay(search_effects)` → duration / stacking / modifiers
- `ue_manage_gameplay(generate_balance_report)` → BALANCE.md
- `ue_manage_gameplay(validate_all_flows)` + `(check_tag_consistency)` → VALIDATION.md

## Usage

```bash
/gameplay-flow-doc <project_root>           # offline
/gameplay-flow-doc <project_root> --enrich  # live MCP enrichment
```

If `<project_root>` is omitted, use `$UECODEGEN_PROJECT_DIR` from `.env`.

## Parse arguments

1. Positional 1 (required): `project_root`
2. `--enrich` → enable the MCP enrichment pass (default: off)
3. `--docs-root <dir>` → output directory, default `<project_root>/Docs/Narsha`

## Workflow

### Phase 0 — Preflight

1. Verify `<docs_root>/_data/project_scan.nps.json` exists (run `/project-scan`
   first if not).
2. Verify `project_type ∈ {gameplay_project, open_world}` via `_index.json`.
   If not, bail with a note that gameplay docs don't apply.

### Phase 1 — Offline scan

Call `narshamcp::project_docs::scanner::scan` — the `ScanBundle` already
contains `gameplay: Value` (JSON) + all 8 `gameplay/*.md` entries inside
`md_tree`. Write to disk using the standard NARSHA:AUTO merge rule (preserve
anything after `<!-- NARSHA:AUTO:END -->`).

### Phase 2 — Enrichment (only with `--enrich`)

Call the MCP ops listed above and replace the placeholder sections. Prefer
`ue_batch_editor_operations` to batch per-ability queries (Lyra has ~87 GAs,
so a single batch saves ~15 seconds of round-trips).

### Phase 3 — Report

```
Project:      <name>
Abilities:    <N>
Effects:      <N>
Tags:         <N>
InputActions: <N>
IMCs:         <N>
Files:        8 (gameplay/*.md)
Enrichment:   <on|off>
```

## Acceptance criteria (Issue #8981)

| Project | Expected | Offline (this issue) | Enriched (later) |
|---|---|---|---|
| Lyra | ~87 GA, ~142 GE, ~312 tags, ~34 IA | counts covered | flow bindings + balance |
| Broken flow detection (~6/34) | — | placeholder | authoritative via `validate_all_flows` |
| Mermaid | required | OVERVIEW.md + GAME_FLOW.md emit diagrams | — |
| Tag hierarchy | required | GAS_TAGS.md renders full tree | — |

## See also

- Spec: `docs/epics/project-docs-system/GAMEPLAY_SECTION_SPEC.md`
- Schema: `docs/specs/project-docs-schema/gameplay.schema.json`
- Backend: `MCP/native/mcp_server/src/project_docs/gameplay_builder.rs` + `gameplay_renderer.rs`
- Sub-issue draft: `docs/epics/project-docs-system/sub-issues/08-gameplay-flow-doc.md`

## MCP Tool Usage

```python
# Generate gameplay flow documentation
ue_project_docs(operation="search", query="GameplayAbility")
ue_manage_gameplay(operation="smart", tag="Ability.Attack")
ue_manage_ai(operation="smart", asset_name="ST_EnemyAI")
```
