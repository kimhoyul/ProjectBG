---
name: ue-refactor-bp
description: "Purpose: Analysis-driven Blueprint refactoring. Phase 1: Analyze existing BP structure/callers/flow via NarshaMCP. Phase 2: Manual refactoring guide using NarshaMCP analysis output (developer applies changes via UE Editor). Phase 3: Build verification via ue_fix_errors. Not for analysis-only (use blueprint-flow) or new BP creation (use blueprint-generator). Triggers: 'BP 리팩터링', 'BP 개선', '블루프린트 정리', 'refactor Blueprint', 'clean up BP', 'optimize BP graph'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x project."
argument-hint: "Blueprint name to refactor (e.g., 'BP_PlayerCharacter', 'BP_EnemyAI') or --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "#7412"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Blueprint Refactoring Workflow

**Priority**: High
**Goal**: Analysis-driven Blueprint refactoring with NarshaMCP analysis + manual application guide
**Issue**: #7412

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation — actually run the MCP tools!

### Execution Requirements
1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user which Blueprint to refactor and what kind of improvement they want.
2. **Execute Phase 1 analysis** and present findings to user
3. **Wait for user confirmation** before proceeding to Phase 2
4. **Generate manual refactoring guide** based on Phase 1 analysis
5. **Verify result** with build check after user applies changes

---

## Auto-Trigger Phrases

### Korean
- "BP 리팩터링", "BP 리팩토링"
- "BP 개선", "블루프린트 정리"
- "블루프린트 개선", "BP 노드 정리"
- "BP 구조 개선", "BP 그래프 최적화"
- "BP 그래프 정리", "블루프린트 리팩터링"
- "BP 최적화", "블루프린트 구조 개선"

### English
- "refactor Blueprint", "refactor BP"
- "improve Blueprint", "clean up BP"
- "optimize BP graph", "simplify Blueprint"
- "restructure BP", "Blueprint cleanup"
- "Blueprint refactor", "optimize Blueprint"
- "BP graph optimization", "Blueprint restructuring"

**Keywords**: `BP refactor`, `Blueprint cleanup`, `BP 리팩터링`, `BP 개선`

---

## Skill Positioning

| Skill | Scope | Input → Output |
|-------|-------|----------------|
| `/blueprint-flow` | Analysis only | Existing BP → report (no modification) |
| `/blueprint-generator` | Create new | Description → new BP |
| **`/ue-refactor-bp`** | **Modify existing** | **Existing BP → improved BP** |
| `/ue-cpp-refactor` | C++ refactoring | C++ class → renamed/restructured C++ |
| `/bp-cpp-project-init` | Migration | BP → C++ class |

---

## Workflow

### Phase 0: Discovery (3 MCP calls)

```python
# 0a: Verify Blueprint exists
ue_grep(query="<blueprint_name>", domain="assets", limit=5)
# If not found → report error, suggest similar names via ue_search_assets

# 0a-2: Get BP summary (parent class, variables, functions preview)
ue_read(identifier="<blueprint_name>", domain="asset")
# IMPORTANT: domain="asset" is REQUIRED — without it, ue_read routes to C++ symbol
# lookup and returns "Symbol not found" for Blueprint names.
# Returns: parent class, variable count, function count, previews (~100 tokens)

# 0b: Editor detection (for Phase 1b find_bp_references gating)
ue_check_health()
# Read: editor_control port status → editor_running (used in Phase 1b)
```

### Phase 1: Analysis (3-4 MCP calls, all UE versions)

```python
# 1a: Get BP structure overview (metadata: node count, path)
ue_manage_blueprint(operation="get_structure", blueprint_name="<target_bp>")

# 1a-2: Get detailed node data for refactoring analysis (offline, no Editor)
ue_manage_blueprint(operation="get_raw_nodes", blueprint_name="<target_bp>")
# Returns: actual graph nodes, connections, entry points — needed for
# identifying complexity, duplication, and dead code patterns.
# NOTE: get_structure alone returns only metadata (node_count) when Editor is offline.

# 1b: Find BP references [E] (Editor required — conditional)
# Only call if Editor is running (detected via ue_check_health port status):
ue_manage_blueprint(operation="find_bp_references", blueprint_name="<target_bp>")
# IF Editor offline → skip, note "Reference analysis unavailable without Editor"
# NOTE: Do NOT use ue_analyze_symbols(find_callers) for BP names —
# it searches C++ function names and produces false positives from substring matching.

# 1c: Trace execution flow for key entry points (offline, no Editor)
ue_trace_execution(operation="trace_execution_flow",
    blueprint_name="<target_bp>", max_depth=10)
```

**Present Analysis to User** before proceeding:

```text
=== Blueprint Refactoring Analysis ===

Blueprint: <name>
Nodes: N | Functions: M | Events: K
Callers: X references across Y Blueprints

Identified Issues:
  1. [Complexity] EventGraph has 32 nodes — consider splitting into functions
  2. [Duplication] 3 similar Delay+PrintString chains found
  3. [Dead Code] CustomEvent "OnOldAbility" has 0 callers
  4. [Optimization] ForEachLoop on large array in Tick

Proceed with refactoring guide? [Y/n]
```

> **IMPORTANT**: Wait for user confirmation before Phase 2. The analysis may reveal that refactoring is unnecessary or that the user wants to adjust scope.

### Phase 2: Manual Refactoring Guide

No MCP calls — pure AI-generated guidance based on Phase 1 analysis results.
Developer applies the steps in the UE Editor; Phase 3 verifies the build.

```text
=== Manual Refactoring Guide ===

Blueprint: <name>
Based on Phase 1 analysis, here are the recommended refactoring steps:

Step 1: Extract Function "HandleDamageResponse"
  - Select nodes 5-12 in EventGraph (Branch → ApplyDamage → UpdateUI)
  - Right-click → Collapse to Function
  - Name: "HandleDamageResponse"
  - Move to new function graph

Step 2: Remove Dead Code
  - Delete CustomEvent "OnOldAbility" (0 callers confirmed in Phase 1)
  - Delete connected nodes (SetVisibility, PrintString)

Step 3: Optimize Tick
  - Replace ForEachLoop with Timer-based batch processing
  - Set Timer interval: 0.1s, process 10 items per tick

Step 4: Consolidate Duplicates
  - Create function "DelayedPrint(Message, Duration)"
  - Replace 3 duplicate Delay+PrintString chains with function calls

Estimated Impact:
  - Nodes reduced: ~47 → ~28
  - Functions added: +2
  - Maintainability: Improved (smaller EventGraph)
```

### Phase 3: Verification (after user applies the manual guide, 1-2 MCP calls)

```python
# 3a: Build verification
ue_fix_errors(operation="smart", project_root="<project_root>")

# If errors found:
# 3b: One auto-fix attempt
ue_fix_errors(operation="auto", errors="<detected_errors>")

# If errors persist → STOP. Report errors. Do NOT retry.
```

---

## MCP Call Budget

| Phase | Tool | Max Calls | Notes |
|-------|------|-----------|-------|
| Phase 0 | `ue_grep` | 1 | BP existence check |
| Phase 0 | `ue_read` | 1 | BP summary (parent class, complexity preview) |
| Phase 0 | `ue_check_health` | 1 | Editor status |
| Phase 1 | `ue_manage_blueprint` | 2-3 | get_structure + get_raw_nodes + find_bp_references `[E]` (conditional) |
| Phase 1 | `ue_trace_execution` | 1 | trace_execution_flow |
| Phase 3 | `ue_fix_errors` | 1-2 | smart + optional auto |
| **Total** | | **6-7** | **Manual guide (no Editor required)** |

---

## Output Format

```text
=== Blueprint Refactoring Report ===

Blueprint: <name>
Engine: UE <version>

--- Phase 1: Analysis ---
Nodes: N | Functions: M | Events: K
Callers: X references across Y Blueprints
Issues Found: Z (Complexity: a, Duplication: b, Dead Code: c, Performance: d)

--- Phase 2: Manual Refactoring Guide ---
Manual Steps: N actionable steps provided
Estimated Node Reduction: X → Y

--- Phase 3: Verification (after manual application) ---
Build: PASS | ERRORS (count)
Auto-fix: Applied N fixes

--- Next Steps ---
  - Open Blueprint in Editor to verify visual layout
  - Test gameplay to confirm behavior is unchanged
  - Review callers (X references) for compatibility
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| BP not found | Name typo or asset not indexed | `ue_search_assets(asset_type="blueprint")` to find correct name |
| `get_structure` returns empty | BP has no EventGraph or is data-only | Report as empty/data-only BP, suggest `/blueprint-generator` for adding logic |
| `blueprint_to_lisp` unavailable | ECA stack deleted in Epic #11230 / PR #11295 — `ue_eca_proxy` removed | Use Path B (manual analysis-driven refactor); future LISP path will go via MCPClientToolset |
| `lisp_to_blueprint` unavailable | Same — ECA stack deleted | Use Path B; show intended LISP for manual reference |
| `auto_layout` fails | Editor not running | Skip layout step, note in output |
| Build errors after refactoring | Broken connections in modified graph | `ue_fix_errors(auto)` one attempt, then stop and report |
| `find_bp_references` unavailable | Editor not running (operation is `[E]`) | Skip reference analysis, note in report — refactoring proceeds without caller info |
| `find_bp_references` returns 0 | BP is unused or references are in engine code | Note in analysis — refactoring is safe but BP may be orphaned |
| `get_raw_nodes` returns empty | BP has no graph nodes (data-only BP) | Report as data-only BP, suggest `/blueprint-generator` to add logic |

---

## Activation Test Cases

**Positive (5)** — Should activate `/ue-refactor-bp`:
1. "BP_EnemyAI 리팩터링해줘" → Phase 1 analysis + refactoring
2. "Clean up BP_PlayerCharacter Blueprint" → Phase 1 + appropriate path
3. "BP_InventoryWidget 노드 정리해줘" → triggers ue-refactor-bp
4. "Optimize BP_WeaponSystem graph" → triggers ue-refactor-bp
5. "블루프린트 개선 BP_HUD" → triggers ue-refactor-bp

**Negative (5)** — Should NOT activate `/ue-refactor-bp`:
1. "BP_Player 실행 흐름 분석" → `/blueprint-flow` (analysis only)
2. "BP 그래프 만들어줘" → `/blueprint-generator` (create new)
3. "AEnemy를 ABaseEnemy로 리네임" → `/ue-cpp-refactor` (C++ refactor)
4. "BP를 C++로 변환" → `/bp-cpp-project-init` (migration)
5. "BP_Player 뭐야?" → `/ue-explain` (explanation)

---

## Related Files

- **EXAMPLES.md** - Complete usage examples (Path A ECA, Path B Manual, Error Recovery)
- **REFERENCE.md** - Tool parameters, LISP format, refactoring patterns, self-test

## Prerequisites

- **Issues**: #7405, #7406, #7267 (BlueprintLisp — ECA path obsolete post-#11230, kept for historical reference)
- **Existing Skills**: `/blueprint-flow` (Phase 1 analysis pattern), `/blueprint-generator` (BP authoring pipeline; ECA LISP path superseded by T3D import in #11230)

**Status**: v1.0.0 | Issue #7412
**Last Updated**: 2026-03-28
