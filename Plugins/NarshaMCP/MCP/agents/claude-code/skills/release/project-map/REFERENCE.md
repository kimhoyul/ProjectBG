# /project-map — Reference Material

> Companion to [SKILL.md](SKILL.md). Reference content (trigger phrases, MCP tool examples,
> **L0-L3 Progressive Disclosure Level definitions** (SSOT), token budget, error recovery,
> phase dependency graph, parallel execution batches, progress reporting format, edge cases,
> output sorting, ue-project-init relationship, practical notes) lives here. SKILL.md keeps
> the imperative Phases 0-5 workflow.

---

## Auto-Trigger Phrases

### Korean
- "프로젝트 구조", "프로젝트 맵"
- "폴더 트리", "모듈 맵"
- "클래스 맵", "프로젝트 문서 생성"
- "프로젝트 구조 업데이트"

### English
- "project map", "project structure"
- "folder tree", "module map"
- "class map", "generate project docs"
- "update project structure"

### Negative (route elsewhere)
- "프로젝트 초기화", "컨텍스트 생성" → `/ue-project-init`
- "프로젝트 분석", "온보딩" → `/project-onboarding`
- "모듈 분석" → `/module-analyzer`
- "프로젝트 진단" → `/ue-audit`

---

## MCP Tool Usage Examples

```python
# Phase 0: Pre-check
ue_check_health()
ue_cache_control(operation="get_stats")

# Phase 1: Discovery (4 parallel calls)
ue_glob(pattern="*.Build.cs", domain="source", limit=100)   # modules
ue_glob(pattern="*", domain="source", limit=200)             # source tree + file counts
ue_glob(pattern="*", domain="assets", limit=50)              # content top-level folders
ue_glob(pattern="*.h", domain="source", limit=100)           # header inventory per module

# Phase 2: Module analysis
ue_grep(query="PublicDependencyModuleNames", domain="source")
ue_grep(query="PrivateDependencyModuleNames", domain="source")
ue_grep(query="UCLASS", domain="source")

# Phase 3: Class inventory (on-demand)
ue_analyze_symbols(operation="trace_hierarchy", params={"class_name": "AMyCharacter", "direction": "up"})
ue_analyze_symbols(operation="search_symbols", params={"query": "AMyCharacter::*", "limit": 20})
ue_grep(query="UPROPERTY|UFUNCTION", domain="source")
```

---

## Progressive Disclosure Levels

> **SSOT for downstream tools**. L0-L3 names and budget guards are consumed by other skills
> (`/ue-project-init`, `/project-docs-generate`, etc.) — preserve verbatim.

### Level 0: PROJECT_MAP.md (~20 lines, ~200 tokens)

Project at a glance. Always referenced from CLAUDE.md.

```markdown
# Project Map: {ProjectName}

- **Engine**: UE {version}
- **Type**: {Heavy C++ | Small C++ | Blueprint-only}
- **Modules** ({N}): {Module1}, {Module2}, ...
- **Plugins** ({N}): {Plugin1}, {Plugin2}, ...
- **GameFeatures** ({N}): {GF1}, {GF2}, ...  (if Plugins/GameFeatures/ exists)
- **Source**: {N} .h files, {M} .cpp files
- **Content**: {N} folders — {Folder1}, {Folder2}, ...

> Details: FOLDER_TREE.md (Level 1) | MODULE_MAP.md (Level 2)
> Per-module classes: /project-map --module {ModuleName}
```

### Level 1: FOLDER_TREE.md (~100 lines, ~800 tokens)

Source and content folder structure at depth 2.

```markdown
# Folder Tree: {ProjectName}

## Source/
{ModuleName}/
  Public/     ({N} headers)
  Private/    ({M} sources)
{ModuleName2}/
  Public/     ({N} headers)
  Private/    ({M} sources)

## Content/
Blueprints/   ({N} assets)
Materials/    ({N} assets)
Maps/         ({N} assets)
...

## Config/
DefaultEngine.ini
DefaultGame.ini
DefaultInput.ini
...

## Plugins/
{PluginName}/  (Source + Content)
...
```

**Budget guard**: Collapse folders with >20 children to `{name}/ ({N} items)`.

### Level 2: MODULE_MAP.md (~200 lines, ~1600 tokens)

Per-module dependencies and class summary.

```markdown
# Module Map: {ProjectName}

## {ModuleName}
- **Path**: Source/{ModuleName}/
- **Public deps**: {Dep1}, {Dep2}, ...
- **Private deps**: {Dep3}, {Dep4}, ...
- **Headers**: {N} public, {M} private
- **Key classes**: {Class1} (→ {Parent}), {Class2} (→ {Parent}), ...

## {ModuleName2}
...
```

**Budget guard**: Max 15 modules detailed. Additional modules listed as one-liners: `{Name} — {N} headers`.
Max 5 dependencies shown per category; truncate with `... and {N} more`.

### Level 3: CLASSES/{ModuleName}.md (~500 lines, ~4000 tokens per file)

Per-class inventory for a specific module. Generated on demand only.

```markdown
# Classes: {ModuleName}

## {ClassName} → {ParentClass}
- **Type**: UCLASS / USTRUCT / UENUM
- **Blueprint**: {Callable|ReadWrite|Implementable|None}
- **Replicated**: {Yes ({N} props) | No}
- **Key functions** ({N} total):
  - {FuncName}({params}) → {return}
  - {FuncName2}({params}) → {return}
  - ...
- **Properties**: {N} UPROPERTY, {M} UFUNCTION

## {ClassName2} → {ParentClass}
...
```

**Budget guard**: Max 30 classes detailed per file. Additional classes listed as names only.
Max 5 functions per class. If >5: `... and {N} more functions`.

### Level 4: On-Demand (no file)

Individual class details via MCP tools (`ue_analyze_symbols`, `ue_read`).
This level is NOT documented — MCP provides real-time data.

---

## Token Budget Summary

| Level | File | Max Lines | Max Tokens | Enforcement |
|-------|------|-----------|------------|-------------|
| 0 | PROJECT_MAP.md | 20 | 200 | Truncate at 15 modules |
| 1 | FOLDER_TREE.md | 100 | 800 | Depth-2 tree; collapse >20-child folders |
| 2 | MODULE_MAP.md | 200 | 1600 | Max 15 modules detailed; max 5 deps shown |
| 3 | CLASSES/*.md | 500/file | 4000/file | Max 30 classes; max 5 functions/class |
| ref | CLAUDE.md block | 15 | 120 | Summary + file references only |

**Total for L0-L2**: ~2600 tokens (what Claude reads at session start)
**Estimated per-session savings**: 1,500-3,000 tokens (40-70% reduction in project-understanding overhead)

---

## Error Recovery

| Error | Trigger | Recovery |
|-------|---------|----------|
| MCP server not connected | `ue_check_health` fails | STOP — report connection error with recovery guidance |
| PDB not loaded | `symbols_cached == 0` | WARN — proceed with file-based scanning only. L3 hierarchy tracing degraded |
| No Source/ directory | `ue_glob("*.Build.cs")` returns 0 | Classify as Blueprint-only project. Skip L2 module analysis. L0+L1 still generated from Content/ |
| Git not available | `git diff` fails | Fall back to hash-based change detection. If hashes also fail, force full rescan |
| Module count > 30 | Discovery finds many modules | Cap at 15 detailed modules in L2. Show rest as one-liners |
| Single .h file very large | Class inventory exceeds budget | Cap at 30 classes per L3 file. List remainder as names only |
| `.claude/CLAUDE.md` doesn't exist | No prior ue-project-init run | Create the file with just the project-map delimiter block |
| Delimiter block already exists | Re-run without --refresh | Replace existing block content. Preserve content outside delimiters |

---

## Phase Dependency Graph

```
Phase 0 (Pre-Check)
  ├─ Healthy → Phase 0.5 (if --refresh) OR Phase 1
  └─ Unhealthy → STOP (all downstream blocked)

Phase 0.5 (Change Detection, --refresh only)
  ├─ Changes found → Phase 1 (scoped to dirty levels only)
  ├─ No changes → STOP ("Already up to date")
  └─ Git unavailable → hash fallback → full rescan if hashes also fail

Phase 1 (Discovery) — requires Phase 0 success
  ├─ Modules ≥ 1 → Phase 2
  ├─ Modules = 0 (Blueprint-only) → Write L0+L1 from Content, skip Phase 2 → Phase 4
  └─ MCP timeout → Fallback to filesystem-only discovery, retry

Phase 2 (Module Analysis) — requires Phase 1 modules
  ├─ Success → Phase 3 decision
  └─ Grep timeout → Write L2 with module names only (no deps), continue

Phase 3 (Class Inventory) — conditional
  ├─ --level 3 OR --module specified → Generate L3
  ├─ Small project (< 5 modules) → Auto-generate top 3
  └─ Default (no flag) → Skip L3

Phase 4 (Write) — always runs after Phase 1+
Phase 5 (Report) — always runs after Phase 4
```

---

## Parallel Execution Batches

### Phase 1: Two parallel batches

**Batch 1 — MCP indexed** (3 calls, parallel):
1. `ue_glob(pattern="*.Build.cs", domain="source", limit=100)` → module names
2. `ue_glob(pattern="*.h", domain="source", limit=100)` → header paths for MCP-based counts
3. `ue_glob(pattern="*", domain="assets", limit=50)` → asset overview

**Batch 2 — MCP directory + count** (4 calls, parallel — Issue #7612/#7613):
1. `ue_glob(pattern="*", domain="source", entry_type="directory", depth=1)` → Source/ + Plugins/ top-level dirs (filter for plugin/GameFeature classification)
2. `ue_glob(pattern="*", domain="assets", entry_type="directory", depth=1)` → content folder list
3. `ue_glob(pattern="*.h", domain="source", mode="count")` → exact header count
4. `ue_glob(pattern="*.cpp", domain="source", mode="count")` → exact source count

**After Batch 1+2**: Per-module subdirectory scan (1 MCP call):
- `ue_glob(pattern="*", domain="source", entry_type="directory", depth=2)` → all subdirectories (filter per module)

**Note**: `*.Target.cs` files still require filesystem `ls` — MCP source glob doesn't index .Target.cs.

### Phase 2: MCP-only batch (Issue #7612 context_after)

**Batch 1 — MCP** (per module, 3 calls parallel):
1. `ue_grep(query="PublicDependencyModuleNames", domain="source", path_pattern="*{Module}*", context_after=30, limit=1)` → public deps array
2. `ue_grep(query="PrivateDependencyModuleNames", domain="source", path_pattern="*{Module}*", context_after=30, limit=1)` → private deps array
3. `ue_grep(query="UCLASS", domain="source", path_pattern="*{Module}*", limit=100)` → class declarations

---

## Progress Reporting

Print after each phase during execution:

```
=== Project Map Generation ===
[Phase 0] Pre-Check ✓ project: {name}, UE {version}, {N} symbols
[Phase 1] Discovery ✓ {N} modules, {M} .h, {K} .cpp, {P} plugins (+{G} GameFeatures)
[Phase 2] Module Analysis ✓ {N} modules with deps, {M} UCLASS declarations
[Phase 3] Class Inventory ⏭ skipped (use --module to generate)
[Phase 4] Write ✓ 3 files to .claude/project/
[Phase 5] Done — ~{N} tokens total
```

---

## Edge Case Handling

| Condition | Detection | Recovery |
|-----------|-----------|----------|
| `Source/` doesn't exist | `ls Source/` fails | Blueprint-only project. Skip Phase 2. L0+L1 from Content/ only |
| `Content/` doesn't exist | `ls Content/` fails | Empty project. Write L0 with 0 assets, skip Content section in L1 |
| `Plugins/` doesn't exist | `ls Plugins/` fails | 0 plugins. Continue normally |
| Permission denied on `ls` | Command exits non-zero | Use `find {dir} -maxdepth 1 -type d 2>/dev/null` instead |
| `--module {name}` invalid | Module not in Phase 1 results | Error: "Module '{name}' not found. Available: {list}" → STOP |
| `--level` out of range | Not in [0,1,2,3] | Error: "Invalid level. Use 0-3" → STOP |
| `.map_meta.json` > 7 days | Timestamp check | Warn: "Project map is {N} days old. Consider --refresh" |
| `git_commit_sha` not in repo | `git cat-file -t {sha}` fails | Force full rescan (shallow clone or history rewritten) |
| `project_root` moved | `.map_meta.json` path differs from health check | Force full rescan, update meta |

---

## Output Sorting & Determinism

Consistent output across re-runs on the same project state:

| Element | Sort Order | Rule |
|---------|-----------|------|
| Modules in L0/L2 | By directory path | `Source/LyraEditor` before `Source/LyraGame` |
| Plugins in L0/L1 | Alphabetical | `AsyncMixin` before `CommonGame` |
| GameFeatures in L0/L1 | Alphabetical | `ShooterCore` before `TopDownArena` |
| Subdirectories in L1 | Alphabetical | `AbilitySystem/` before `Animation/` |
| Content folders in L1 | Alphabetical | `Audio/` before `Characters/` |
| Classes in L3 | By section (Core→System), then alphabetical within section | Core framework classes first |
| Dependencies in L2 | As declared in Build.cs | Preserves dependency importance order |

**Determinism guarantee**: Same project state → identical output. Token estimates may vary ±5% due to MCP cache timing, but file content is deterministic from filesystem + MCP data.

---

## Relationship with `/ue-project-init`

| Aspect | ue-project-init | project-map |
|--------|-----------------|-------------|
| **Purpose** | Domain context (what the project does) | Structural layout (where things are) |
| **Output** | `.claude/CLAUDE.md` inline (~2500 tokens) | `.claude/project/` files + ~120 token CLAUDE.md ref |
| **Content** | Active systems, key classes, GAS tags, naming conventions | Folder trees, module deps, class inventories |
| **Delimiter** | `<!-- BEGIN ue-project-init -->` | `<!-- BEGIN project-map -->` |
| **When to use** | First: understand what systems the project uses | Then: understand where code lives |
| **Update** | `--refresh` | `--refresh` (with git-based change detection) |

**Recommended workflow**: Run `/ue-project-init` first (domain context), then `/project-map` (structural layout). The project-map skill detects if ue-project-init has already run and reuses engine version from the existing block to avoid redundant MCP calls.

---

## Practical Notes (updated 2026-03-29, Issue #7612/#7613)

**MCP-primary approach** — Issues #7612 and #7613 resolved all previous MCP-only limitations:

1. **`ue_grep(context_after=30)`** — extracts full multi-line arrays from Build.cs (previously required `grep -A 30`)
2. **`ue_glob(entry_type="directory", depth=N)`** — lists directories directly (previously required `ls`, `find -type d`)
3. **`ue_glob(mode="count")`** — exact file counts without overflow (previously required `find | wc -l`)
4. **`ue_glob(mode="count", group_by="directory")`** — per-directory file counts in one call

**Remaining filesystem-only** (1 case):
- **Target files**: `ls Source/*.Target.cs` — MCP doesn't index .Target.cs files

**MCP tools for all other queries**:
- `ue_glob(pattern="*.Build.cs", domain="source")` → module names (accurate)
- `ue_glob(pattern="*", entry_type="directory", depth=1)` → Source/ + Plugins/ top-level dirs
- `ue_glob(pattern="*.h", mode="count", group_by="directory")` → per-dir header counts
- `ue_grep("PublicDependencyModuleNames", context_after=30, path_pattern="*Module*")` → full dep arrays
- `ue_grep("UCLASS", domain="source", path_pattern="*ModuleName*")` → per-module class declarations
- `ue_analyze_symbols(trace_hierarchy)` → class inheritance (PDB, authoritative)
- `ue_check_health()` → engine version, project path

**UCLASS note**: `ue_grep("UCLASS", domain="source")` includes plugin classes (marked `[source:plugin]`). Filter by `path_pattern` for per-module accuracy.

**Lyra benchmark** (2 modules, 242 .h, 2840 assets):
- Phase 0: 2 MCP calls, ~20ms
- Phase 1: 4 parallel MCP calls, ~1.7s (asset glob dominates)
- Phase 2: 4 parallel MCP calls, ~2.6s (grep dominates)
- **Total: 10 MCP calls, ~4.3s**
- Output: L0 (12 lines), L1 (86 lines), L2 (43 lines) = **141 lines, ~1,380 tokens**
