---
name: ue-engine-content
model: sonnet
context: fork
agent: general-purpose
description: "Purpose: On-demand engine PDB/metadata loading. Triggers: '엔진 포함', '엔진 메타데이터', 'include engine', 'engine content', 'load engine', '엔진 로드'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "(no args = guided workflow), runtime (runtime PDBs only), plugins (engine plugin PDBs: MetaSound, Niagara, PCG, etc.), full (runtime + plugins + editor PDBs), --self-test (validate skill structure)"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Engine Content — On-Demand Engine Metadata Loading

**Issue**: #5773
**Purpose**: Guide users through loading engine PDBs and metadata on-demand, since `include_engine_metadata` defaults to `false` (Issue #5547) for faster cold start.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually call the MCP tools!

**Loading scope**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, run the full guided workflow (Steps 1-5).

---

## Auto-Trigger Phrases

### Korean
- "엔진 포함", "엔진 메타데이터 포함"
- "엔진 PDB 로드", "엔진 심볼 로드"
- "엔진 콘텐츠 로드", "엔진 로드"

### English
- "Include engine", "Engine content"
- "Load engine PDBs", "Engine metadata"
- "Load engine symbols", "Engine pdb"

---

## Workflow

### Step 1: Check Current Status

Call `ue_cache_control(operation="get_stats")`.

**Extract from response** (field names may vary by version — look for these concepts):
- Are PDB symbols loaded? (e.g., `stats.loaded`)
- Total symbols currently indexed (e.g., `stats.symbols_cached`)
- Is metadata loaded/in progress? (e.g., `metadata_status`, `generation_in_progress`)
- 4-phase indexing status (e.g., `four_phase_complete`, `four_phase_running`)
- Engine PDB loading status (check response for engine-related fields)

**Report current state** before proceeding:

```
## Current Engine Content Status

| Component | Status | Count |
|-----------|--------|-------|
| Project PDBs | loaded/not loaded | N PDBs |
| Engine Runtime PDBs | loaded/not loaded | ~100 PDBs |
| Engine Plugin PDBs | loaded/not loaded | ~1,598 PDBs |
| Engine Editor PDBs | loaded/not loaded | ~400 PDBs |
| Metadata (engine) | included/excluded | N nodes |
```
If engine runtime PDBs are already loaded, skip to Step 2.5 or Step 3.
If metadata already includes engine content, report "Already loaded" and stop.

### Step 2: Load Engine Runtime PDBs

Call `ue_cache_control(operation="load_engine_runtime")`.

This loads ~100 gameplay/runtime engine PDBs from `Engine/Binaries/Win64/` (GAS, EnhancedInput, etc., excluding editor-only modules).

**Expected**: Success with symbol count increase. Typically adds 50K-200K symbols.

**Time estimate**: 30-60 seconds depending on disk speed.

**Progress Polling**: This operation may take 1-5 minutes. After initiating, poll `ue_cache_control(operation='get_stats')` every 30 seconds to report progress to the user (e.g., "Loading PDBs: 45/162 complete...").

### Step 2.5: Load Engine Plugin PDBs (Issue #6896)

**Cumulative Memory Warning**: Steps 2 + 2.5 combined may require ~12 GB additional RSS. Verify available system memory before proceeding. If < 16 GB free, warn user and suggest loading only runtime PDBs (skip Step 2.5).

Call `ue_cache_control(operation="load_engine_plugins")`.

This loads ~1,598 PDBs from `Engine/Plugins/**/Binaries/Win64/` — plugin modules like **MetaSound, Niagara, PCG, SignalProcessing, WaveTable** that live in separate directories from the main engine binaries.

**Why this is separate from Step 2**: Engine Runtime PDBs (Step 2) cover modules in `Engine/Binaries/Win64/` (core gameplay modules compiled into the main editor binary). Engine Plugin PDBs are in `Engine/Plugins/*/Binaries/Win64/` (separate DLLs loaded as plugins). Without this step, classes like `UMetaSoundGeneratorHandle`, `FDelay` (SignalProcessing), and many Niagara/PCG types are invisible to symbol search.

**Expected**: Significant symbol count increase. Adds 100K-500K+ symbols depending on engine version.

**Time estimate**: 1-3 minutes (many small PDBs to parse).

**Progress Polling**: This operation may take 1-5 minutes. After initiating, poll `ue_cache_control(operation='get_stats')` every 30 seconds to report progress to the user (e.g., "Loading plugin PDBs: 800/1598 complete...").

### Step 3: Regenerate Metadata with Engine Content

Call `ue_cache_control(operation="regenerate_metadata", params={"include_engine": true})`.

This regenerates UAsset metadata from scratch, including engine content paths.

**Expected**: Success. Takes ~60-180 seconds for large projects.

**Warning**: This operation can take several minutes. Inform the user:
> "Regenerating metadata with engine content included. This may take 1-3 minutes..."

**Progress Polling**: After initiating, poll `ue_cache_control(operation='get_stats')` every 30 seconds to report progress to the user (e.g., "Regenerating metadata: 60% complete...").

### Step 4: (Optional) Load Engine Editor PDBs

**Ask the user before proceeding** — editor PDBs add ~400 more PDBs and significant memory (~12 GB RSS).

Only load if the user explicitly needs editor symbols (e.g., analyzing UnrealEd, Kismet, PropertyEditor modules).

If confirmed, call `ue_cache_control(operation="load_engine_editor")`.

**Time estimate**: 2-5 minutes.

### Step 5: Final Status Report

Call `ue_cache_control(operation="get_stats")` again.

## Output Format

Generate a before/after comparison:

```
## Engine Content Loading Complete

| Component | Before | After |
|-----------|--------|-------|
| PDBs loaded | N | M |
| Symbols indexed | N | M |
| Metadata nodes | N | M |
| Engine included | No | Yes |

Engine content is now available for symbol search, hierarchy tracing, and cross-reference queries.
```

---

## Argument Handling

| Argument | Behavior |
|----------|----------|
| (none) | Full guided workflow (Steps 1-5) |
| `runtime` | Steps 1-2 only (load runtime PDBs, skip metadata regeneration) |
| `plugins` | Steps 1-2.5 only (load engine plugin PDBs: MetaSound, Niagara, PCG, etc.) |
| `full` | Steps 1-4 (runtime + plugins + editor PDBs + metadata, no user confirmation for Step 4). Requires ~12 GB free RAM for editor PDBs. |

---

## Judgment Criteria

| Check | PASS | WARN | FAIL |
|-------|------|------|------|
| Engine path | Valid engine path configured | — | Engine path not found |
| Runtime PDBs | Loaded, symbols > 0 | Already loaded (skip) | Load failed |
| Metadata | Regenerated with engine=true | In progress | Error during regeneration |
| Editor PDBs | Loaded (if requested) | Skipped (user declined) | Load failed |

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| "Engine path not found" | `UECODEGEN_ENGINE_DIR` not set | Run `ue_cache_control(operation="validate_paths")` and report |
| "Indexing in progress" | 4-phase indexing still running | Wait and retry, or show progress |
| "Out of memory" | Editor PDBs too large | Skip editor PDBs, use runtime only |
| "Timeout" | Large project metadata | Inform user to wait, check `get_stats` for progress |
| "Symbols already loaded" | Runtime PDBs already present | Skip Step 2, proceed to Step 3 |
| Partial load failure | Step failed midway | Use `ue_cache_control(operation='unload_modules', params={'module_names': ['engine_runtime']})` to unload partially-loaded data, preventing inconsistent index state |

---

## Self-Test (`--self-test`)

When invoked with `--self-test`, validate this skill's structure without executing the workflow:

1. **Files exist**: `SKILL.md`, `REFERENCE.md`, `EXAMPLES.md` in `.claude/skills/ue-engine-content/`
2. **Frontmatter**: `name`, `description`, `argument-hint`, `metadata.version` present
3. **Routing**: `.claude/skills/routing.json` has entry with `"skill": "ue-engine-content"`
4. **Trigger patterns**: routing.json `trigger_patterns` keywords appear in SKILL.md Auto-Trigger section
5. **Example invocations**: All `/ue-engine-content` calls in EXAMPLES.md use valid argument values

Output: PASS/FAIL per check. Does not call any MCP tools.

---

## Related

- `/session-health-check` — General MCP health verification
- `ue_cache_control` — Underlying cache management tool
- Issue #5547 — `include_engine_metadata` default changed to `false`
- [REFERENCE.md](REFERENCE.md) — MCP tool mapping, tier details, environment variables
- [EXAMPLES.md](EXAMPLES.md) — 5 complete workflow examples
## MCP Tool Examples

```python
# Control cache when loading engine content
ue_cache_control(operation="load_engine_runtime")
```
