<!-- NarshaMCP:START -->
<!-- AUTO-MANAGED, single-sourced from MCP/native/mcp_server/src/assets/mcp-first-core.md (Issue #14023). Edit the SSOT, not this file. Drift blocked by test_claude_section_template_matches_ssot. Heavy UE Question-Answering workflow relocated to the on-demand /ue-explain skill. -->
## ⚠️ MCP-First Tool Selection (UE Queries)

When NarshaMCP tools are available, prefer them over built-in tools for UE-specific queries — built-in tools can't parse binary `.uasset`, query PDB symbols, or reach `Engine/Source/`.

**Decision priority (check in order):**
1. **Source exploration / search / find** → `ue_grep` → `ue_read` → `ue_glob` preferred (compact ~150 tokens, plugin + engine source included; pre-fetched, no ToolSearch needed).
2. **File editing** → built-in `Read` → `Edit` (MCP tools are read-only).
3. **Non-UE files** (`.json`, `.yaml`, `.toml`, `.md`, `CLAUDE.md`) → built-in tools.
4. **Skill-First** → matching `Skill()` / `/narshamcp:<skill>` for natural-language workflows.
5. **Binary assets / C++ symbols / configs** → MCP tools (`ue_analyze_symbols` is PDB-indexed; 62 operations over exact compiler symbols, Editor-off).

**ToolSearch rule:** always use the full `mcp__narshamcp__` prefix — `ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")` ✅ (short names return zero results silently).

**Common mistakes — NEVER do these:**

| Query | ❌ Wrong | ✅ Right |
|-------|---------|---------|
| UE source search | `Grep("Shadow", "Source/")` | `ue_grep("Shadow")` |
| Engine source search | `Grep("ACharacter")` | `ue_grep("ACharacter", scope="engine")` |
| Class hierarchy | `Grep("class.*APawn")` | `ue_analyze_symbols(trace_hierarchy)` |
| C++ class creation | manual `Write .h/.cpp` | `ue_generate_code(derive_class)` |
| Deep UE question | built-in Grep only | `/ue-explain` (engine_docs + ue_grep workflow) |

> For deep UE question-answering (engine_docs domain map + multi-step engine source workflow), use the **`/ue-explain`** skill — it loads the full procedure on demand.
<!-- NarshaMCP:END -->
