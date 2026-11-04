---
name: project-map
description: "Purpose: Auto-generate progressive disclosure project structure docs (Level 0-3) to .claude/project/. Folder trees, module maps, class inventories for fast project comprehension. Change detection for incremental updates. Triggers: '프로젝트 구조', '프로젝트 맵', 'project map', 'project structure', 'folder tree', 'module map'."
argument-hint: "(no args = full L0-L2 scan), --level <0-3> (specific level), --module <name> (L3 for one module), --refresh (change-detect update), --dry-run (preview)"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  issue: "#7589"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Project Map — Progressive Disclosure Project Structure Docs

**Issue**: #7589
**Purpose**: Auto-generate progressive disclosure documentation about a UE project's structural layout (folders, modules, classes) so Claude Code can understand "where things are" without expensive per-session MCP exploration.

> **Reference**: Trigger phrases (KO+EN+negative routing), MCP tool examples, **L0-L3 Progressive Disclosure Level definitions (SSOT)**, token budget summary, error recovery, phase dependency graph, parallel execution batches, progress reporting format, edge cases, output sorting/determinism, relationship with `/ue-project-init`, practical notes (Issues #7612/#7613) — all in [REFERENCE.md](REFERENCE.md).

**Difference from other skills**:
- `/ue-project-init` → domain context (what systems/classes exist) → writes to `.claude/CLAUDE.md`
- `/project-onboarding` → ephemeral tool/skill recommendations → console only
- `/project-map` → structural layout (where things are) → writes to `.claude/project/`

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the project map workflow automatically**.
> Do NOT just display documentation — actually call the MCP tools and write the output files!

**Options**: `$ARGUMENTS`. Empty `$ARGUMENTS` → run the full L0-L2 workflow.

1. Parse `$ARGUMENTS` for flags:
   - `--level <0-3>` — Generate only the specified level
   - `--module <name>` — Generate Level 3 for a specific module
   - `--refresh` — Use change detection, regenerate only dirty levels/modules
   - `--dry-run` — Execute all phases but output to console instead of writing files
2. Execute Phase 0 (Pre-Check) immediately
3. Execute Phase 0.5 (Change Detection) if `--refresh`
4. Execute Phase 1 (Discovery Scan) → Level 0 + Level 1
5. Execute Phase 2 (Module Analysis) → Level 2
6. Execute Phase 3 (Class Inventory) only if `--level 3` or `--module` specified
7. Execute Phase 4 (Write + Meta)
8. Execute Phase 5 (Report)

> Output level definitions (what L0/L1/L2/L3 should contain): [REFERENCE.md § Progressive Disclosure Levels](REFERENCE.md#progressive-disclosure-levels).

---

## Workflow Phases

### Phase 0: Pre-Check

**MCP calls**: 2

```python
ue_check_health()
ue_cache_control(operation="get_stats")
```

**Logic**:
1. If health check fails → STOP with recovery guidance.
2. If PDB not loaded → WARN: "Symbol data incomplete. L0-L2 will use file-based scanning. L3 hierarchy tracing will be limited."
3. Extract `project_path` and `engine_version` from health response.
4. Check if `.claude/project/.map_meta.json` exists:
   - **Exists + `--refresh`** → Phase 0.5
   - **Exists + no flag** → display "Project map already exists ({date}). Use --refresh to update." and STOP unless user confirms full rescan.
   - **Does not exist** → Phase 1.

### Phase 0.5: Change Detection (--refresh only)

```bash
# 1. Read stored metadata
cat .claude/project/.map_meta.json → last_commit_sha, file_hashes

# 2. Git-based diff (primary)
git diff --name-only {last_commit_sha}..HEAD -- "*.h" "*.cpp" "*.Build.cs" "*.uproject" "*.uplugin"
```

**Classification rules**:

| Changed file | Dirty levels |
|-------------|-------------|
| `*.uproject`, `*.uplugin` | L0 + L1 (project structure changed) |
| `*.Build.cs` | L2 + L3 for affected module (deps changed) |
| `*.h`, `*.cpp` in module X | L3 for module X only (class inventory changed) |
| `Config/*` | L0 only (config list may change) |

**No changes**: report "Already up to date" and STOP.
**Fallback** (no git): hash compare → mtime compare → full rescan.

### Phase 1: Discovery Scan → Level 0 + Level 1

**Hybrid approach**: MCP for indexed data + filesystem for directory structure.

**Batch 1 — MCP indexed** (3 parallel):
```python
ue_glob(pattern="*.Build.cs", domain="source", limit=100)   # modules
ue_glob(pattern="*.h", domain="source", limit=100)           # header inventory
ue_glob(pattern="*", domain="assets", limit=50)              # asset overview
```

**Batch 2 — MCP directory + count** (4 parallel, Issues #7612/#7613):
```python
ue_glob(pattern="*", domain="source", entry_type="directory", depth=1)  # Source/ + Plugins/
ue_glob(pattern="*", domain="assets", entry_type="directory", depth=1)  # content folders
ue_glob(pattern="*.h", domain="source", mode="count")                    # exact .h count
ue_glob(pattern="*.cpp", domain="source", mode="count")                  # exact .cpp count
```

**Then** (1 call): per-module subdirectory listing
```python
ue_glob(pattern="*", domain="source", entry_type="directory", depth=2)
```

**Post-processing**:
1. Parse module names from Build.cs paths (`Source/LyraGame/LyraGame.Build.cs` → `LyraGame`).
2. **Plugin classification**: top-level `Plugins/` = regular plugins, `Plugins/GameFeatures/` = GameFeature plugins.
3. Build folder tree from `entry_type="directory"` results.
4. Count .h and .cpp from `mode="count"` (exact total per domain).
5. Classify project type: 0 .h → Blueprint-only · 1-50 → Small C++ · 50+ → Heavy C++.

**Output**: `PROJECT_MAP.md` (L0) and `FOLDER_TREE.md` (L1) per
[REFERENCE.md § Progressive Disclosure Levels](REFERENCE.md#progressive-disclosure-levels).

### Phase 2: Module Analysis → Level 2

**Per-module batch** (3 parallel, Issue #7612 `context_after`):
```python
# 2a. Build.cs dependencies — context_after extracts full multi-line arrays
ue_grep(query="PublicDependencyModuleNames", domain="source",
        path_pattern="*{Module}*", context_after=30, limit=1)
ue_grep(query="PrivateDependencyModuleNames", domain="source",
        path_pattern="*{Module}*", context_after=30, limit=1)

# 2b. Class declarations per module
ue_grep(query="UCLASS", domain="source", path_pattern="*{Module}*", limit=100)
```

**Post-processing**:
1. Parse dependency arrays from `context_after` output (extract string literals between quotes).
2. Map UCLASS declarations to modules by file path.
3. Extract parent class from UCLASS header lines (e.g., `class AMyChar : public ACharacter`).
4. Count headers per subdirectory from Phase 1 data.

**Token budget** (full enforcement table: [REFERENCE.md § Token Budget Summary](REFERENCE.md#token-budget-summary)):
- Module count >15 → detail top 15 by class count, list rest as one-liners.
- Max 5 deps per category per module.
- Class count >30 in a module → show top 10, list rest as names.

**Output**: `MODULE_MAP.md` (L2).

### Phase 3: Class Inventory → Level 3 (on demand)

**Trigger**: `--level 3` OR `--module {name}` OR small project (<5 modules → auto-generate top 3).

**MCP calls per module** (4-8):
```python
# 3a. Type declarations (filter to module by path)
ue_grep(query="UCLASS|USTRUCT|UENUM", domain="source", limit=50)

# 3b. Inheritance chains for top-10 classes
ue_analyze_symbols(operation="trace_hierarchy", class_name="{ClassName}", direction="up", max_depth=2)

# 3c. Function signatures
ue_analyze_symbols(operation="search_symbols", query="{ClassName}::*", limit=20)

# 3d. Property/function macro counts
ue_grep(query="UPROPERTY|UFUNCTION", domain="source", limit=100)

# 3e. Blueprint binding detection
ue_grep(query="BlueprintCallable|BlueprintReadWrite|BlueprintImplementableEvent",
        domain="source", limit=50)

# 3f. Replication detection
ue_grep(query="Replicated|DOREPLIFETIME", domain="source", limit=30)
```

**Large project guard**:
- >400 headers → L3 NEVER auto-generated for all modules.
- Without `--module`: top 3 modules by class count only.
- Each L3 file capped at 500 lines; classes beyond 30 listed as names only.

**PDB-not-loaded fallback**: skip 3b, regex-parse parent from UCLASS line, mark as "(hierarchy unavailable)".

**Output**: `CLASSES/{ModuleName}.md` (L3).

### Phase 4: Write + Meta Update

1. Create `.claude/project/` if not exists.
2. Write level files: `PROJECT_MAP.md`, `FOLDER_TREE.md`, `MODULE_MAP.md`, optionally `CLASSES/{Module}.md`.
3. Write `.claude/project/.map_meta.json`:
   ```json
   {
     "version": "1.0.0",
     "generated_at": "2026-03-29T10:00:00Z",
     "git_commit_sha": "abc123def456...",
     "engine_version": "5.4",
     "project_type": "Heavy C++",
     "modules": ["LyraGame", "LyraEditor", "LyraExampleContent"],
     "file_hashes": { "LyraGame.Build.cs": "sha256:...", ... },
     "level3_generated": ["LyraGame"],
     "token_estimates": { "level0": 180, "level1": 750, "level2": 1400, "level3_LyraGame": 3200 }
   }
   ```
4. Update `.claude/CLAUDE.md` with delimiter block (coexists with `<!-- BEGIN ue-project-init -->`):
   ```markdown
   <!-- BEGIN project-map v1.0 (auto-generated {YYYY-MM-DD}) -->
   <!-- Re-generate: /project-map --refresh -->

   ## Project Structure (Quick Map)
   - **Modules**: {N} ({Module1}, {Module2}, ...)
   - **Source files**: {N} .h, {M} .cpp
   - **Plugins**: {Plugin1}, {Plugin2}, ...
   - **Engine**: UE {version}

   > Full docs: `.claude/project/PROJECT_MAP.md` | `FOLDER_TREE.md` | `MODULE_MAP.md`
   <!-- END project-map -->
   ```

**`--dry-run` behavior**: skip all writes, output each level to console.

### Phase 5: Report

```
=== Project Map Generated ===

Output: .claude/project/
  - Level 0: PROJECT_MAP.md (~{N} tokens)
  - Level 1: FOLDER_TREE.md (~{N} tokens)
  - Level 2: MODULE_MAP.md (~{N} tokens, {M} modules)
  {- Level 3: CLASSES/{Module}.md (~{N} tokens, {M} classes)  # if generated}
  - Meta: .map_meta.json (git SHA: {short_sha})
  - CLAUDE.md: reference block updated

Total: ~{N} tokens across {M} files

To update after code changes: /project-map --refresh
To generate Level 3 for a module: /project-map --module {ModuleName}
To view a specific level: /project-map --level {N}
```

> Phase-by-phase progress reporting format: [REFERENCE.md § Progress Reporting](REFERENCE.md#progress-reporting).
> Phase dependency graph (failure handling per phase): [REFERENCE.md § Phase Dependency Graph](REFERENCE.md#phase-dependency-graph).

---

## See Also

- [REFERENCE.md](REFERENCE.md) — Trigger phrases (KO+EN+negative routing), MCP examples, **L0-L3 Progressive Disclosure Level definitions (SSOT)**, token budget summary, error recovery, phase dependency graph, parallel execution batches, progress reporting format, edge case handling, output sorting/determinism rules, `/ue-project-init` relationship table, practical notes (Issues #7612/#7613)
