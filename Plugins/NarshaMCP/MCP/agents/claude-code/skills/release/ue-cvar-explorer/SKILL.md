---
name: ue-cvar-explorer
model: opus
description: "Purpose: Comprehensive CVar/config exploration with engine metadata (ECVF flags, scalability group mapping, change impact prediction). Searches, explains, and traces CVar relationships. Triggers: 'CVar', 'config search', '설정 검색', '그래픽 설정', '렌더링 옵션', '퀄리티 설정', 'scalability', 'quality settings'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "CVar name or search pattern (e.g., r.Shadow.MaxResolution, sg.ShadowQuality, r.Shadow*)"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE CVar Explorer

**Issue**: #5295, #6352
**Purpose**: Deep CVar exploration with ECVF flags, scalability group mapping, config relationships, and change impact prediction.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Flow

1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what CVar or config setting to explore.
2. **Determine query type** and execute appropriate MCP call
3. **Format structured report** from the response

---

### Should NOT Activate — Disambiguation

Route broad impact questions to `ue-impact`/`ue-explain`, broad setting sweeps to `ue_grep`, and file-edit requests to built-in editing. Examples: [REFERENCE.md § Disambiguation](REFERENCE.md#disambiguation).

---

## Auto-Trigger Phrases

CVar/config exploration triggers (KO: "CVar 찾아줘", "설정 검색", "그래픽/렌더링/퀄리티 설정", "스케일러빌리티 그룹"; EN: "Explore CVar X", "config search", "graphics/quality settings", "scalability group mapping"). Full KO/EN lists: [REFERENCE.md § Auto-Trigger Phrases](REFERENCE.md#auto-trigger-phrases).

---

## Step 0: Discovery Pre-Exploration (Issue #6352)

Lightweight pre-verify before `explore_cvar`/`search_config` (~27x token savings on typos). **ALWAYS** run for specific CVar queries; **skip** for natural-language searches ("shadow quality") → go directly to `search_config`.

```python
# 0a. Verify existence. If domains_unavailable shows "configs", fall back to
#     ue_analyze_config(operation="search_config", query="<cvar_name>").
ue_grep(query="<cvar_name>", limit=10)            # matches across configs/source (~50 tokens)
# 0b. If "CVar registry not loaded", skip to ue_analyze_config directly.
ue_read(identifier="<cvar_name>")                 # cvar_type, default, help_text, flags (~100 tokens)
```

Pre-verify ONLY — typo → suggest similar from grep; never terminate here (grep/read omit ECVF flags, scalability mapping, change-impact). Downstream vars: `step0_target_exists`, `step0_read_detail` (`ue_read` resp), `step0_similar_cvars` (typo "Did you mean X?").

**Decision Logic**:

```text
ue_read has help_text      → confirmed; STILL call explore_cvar (canonical ECVF/scalability/impact, no bypass)
Found but no help_text      → proceed to explore_cvar for full detail
CVar registry not loaded    → skip Step 0, fall back to ue_analyze_config directly
Not found, similar exist     → "Not found. Did you mean: <similar>?"
Not found, nothing similar   → "CVar not found in project or engine."
```

> **MANDATORY (Issue #14700)**: For any **specific CVar or `sg.*` query**, `ue_analyze_config(operation="explore_cvar", ...)` is **REQUIRED** — Step 0 `ue_grep`/`ue_read` is a pre-existence check only, not the terminal answer. Full bypass permitted ONLY when the CVar is confirmed **absent** (typo path → suggest similar names).

---

## Workflow

> Step 0 (above) is the discovery gate (Issue #6289 dup): `ue_grep(domain="configs")` → `ue_read` confirm existence (~95% token savings vs `search_config`). Decision: found → Step 2 `explore_cvar`; multiple → user picks; not found → suggest similar. `ue_read` pre-verifies only — still call `explore_cvar` for a specific CVar.

### Step 1: Detect Query Type

| Input Pattern | Query Type | MCP Operation |
|--------------|-----------|---------------|
| Specific CVar name (e.g., `r.Shadow.MaxResolution`) | Single CVar exploration | `explore_cvar` |
| Scalability group (e.g., `sg.ShadowQuality`) | Group exploration | `explore_cvar` (shows all controlled CVars) |
| Wildcard pattern (e.g., `r.Shadow*`) | Search first | `search_config` then `explore_cvar` on top results |
| Natural language (e.g., "shadow quality") | Keyword search + metadata enrichment | `search_config`, then `explore_cvar` on 1–3 representative results |

### Step 2: Execute MCP Tools

**For specific CVar or scalability group:**
```python
result = ue_analyze_config(
    operation="explore_cvar",
    option="r.Shadow.MaxResolution",
    explore=true
)
```text

**For pattern or natural-language search:**
```python
# Step A: Search
search = ue_analyze_config(operation="search_config", query="r.Shadow*")

# Step B: Explore 1–3 representative results so flags/scalability are evidence-backed
for cvar in search.results[:3]:
    detail = ue_analyze_config(
        operation="explore_cvar",
        option=cvar.key,
        explore=true
    )
```markdown

### Step 2.5: CVar Name Normalization

Always try prefix variants before giving up. Rules, lookup table, and `GUITextureLODBias` example: [REFERENCE.md § CVar Name Normalization](REFERENCE.md#cvar-name-normalization--prefix-variant-table).

### Step 2.6: Fallback Chain (when `engine_source` is null)

When `explore_cvar` returns data but `engine_source: null`, enrich via fallback chain: `ue_analyze_symbols(search_symbols, symbol_type="variable")` → `ue_analyze_symbols(find_callers)` → `ue_engine_docs(grep)`. Report findings honestly; do NOT fabricate behavior from the name — state "Engine source not found; based on config + symbol analysis." Full chain + per-step interpretation: [REFERENCE.md § Fallback Chain (when engine_source is null)](REFERENCE.md#fallback-chain-when-engine_source-is-null).

### Step 2.7: Multi-CVar Handling

Explore each requested CVar independently, then report separate sections and an optional comparison. Details: [REFERENCE.md § Multi-CVar Handling](REFERENCE.md#multi-cvar-handling).

### Step 2.8: Engine Docs Cross-Check (conditional)

Cross-check engine docs only when `engine_source` is null or the name is cross-system-ambiguous; otherwise continue to Step 3. Commands and scope rules: [REFERENCE.md § Engine Docs Cross-Check](REFERENCE.md#engine-docs-cross-check-conditional).

### Step 3: Format Report

For search/group results, include this exact evidence table:

`CVar | Value | Source | ECVF flags | Scalability | Performance impact`

Every column is mandatory. Use `ECVF flags: unknown` and `unknown` for unavailable fields; never infer metadata from search hits. Full report template: [REFERENCE.md § Output Format](REFERENCE.md#output-format).

---

## ⚠️ Final Response Contract (MUST — eval-gated)

Before answering, render these evidence-backed fields for a specific CVar (and the corresponding
columns or representative top entries for a search/group request):

1. **Name and value** — canonical name plus current/project/default value, clearly distinguishing
   them; use `unknown` when the tool does not return one.
2. **Source** — config file/section, engine declaration, or explicit registry/tool provenance.
3. **ECVF flags** — list the returned flags, or say `ECVF flags: unknown` when unavailable.
4. **Scalability** — name the controlling `sg.*` group and quality-level values, or explicitly say
   `Not controlled by a scalability preset`; use `unknown` when coverage is incomplete.
5. **Performance impact** — explain the CPU/GPU/memory/rendering consequence supported by the tool
   result, then give a safe recommendation and note preset override behavior.

If no CVar matches, report `CVar not found` with the searched scope and suggested close matches.
Never infer behavior, ECVF flags, scalability membership, or performance impact from the name
alone. Missing metadata stays `unknown`, not silently omitted.

---

## Error Recovery

On failure, fall back per error: not in registry → show config occurrences only; no sg.* mapping → "Not controlled by scalability preset"; `explore_cvar` unavailable → `explain_config` + `find_overrides`; no config files → check project_root. Full table: [REFERENCE.md § Error Recovery](REFERENCE.md#error-recovery).

---

## Examples

Worked walkthroughs (specific CVar, scalability group, pattern search, impact analysis) — e.g. "r.Shadow.MaxResolution 뭐야?" → `ue_analyze_config(operation="explore_cvar", option="r.Shadow.MaxResolution", explore=true)`. All 4: [REFERENCE.md § Examples](REFERENCE.md#examples).
