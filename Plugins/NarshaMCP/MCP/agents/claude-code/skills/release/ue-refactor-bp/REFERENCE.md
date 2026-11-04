# Blueprint Refactoring - Reference

> Technical reference for analysis-driven Blueprint refactoring.

## MCP Tool Quick Reference

| Phase | Tool | Operation | Required Params | Notes |
|-------|------|-----------|-----------------|-------|
| 0a | `ue_grep` | — | `query`, `domain="assets"` | BP existence check |
| 0a-2 | `ue_read` | — | `identifier`, `domain="asset"` | Parent class, complexity preview |
| 0b | `ue_check_health` | — | — | Editor reachability detection |
| 1a | `ue_manage_blueprint` | `get_structure` | `blueprint_name` | Metadata: node count, path |
| 1a-2 | `ue_manage_blueprint` | `get_raw_nodes` | `blueprint_name` | Detailed nodes + connections |
| 1b | `ue_manage_blueprint` | `find_bp_references` | `blueprint_name` | `[E]` Editor required |
| 1c | `ue_trace_execution` | `trace_execution_flow` | `blueprint_name`, `max_depth` | Entry point flow |
| 2 | `ue_manage_blueprint` | `auto_layout` | `blueprint_name` | `[E]` Editor required (manual analysis-driven refactor) |
| 3a | `ue_fix_errors` | `smart` | `project_root` | Build verification |
| 3b | `ue_fix_errors` | `auto` | `errors` | One auto-fix attempt |

> **Path A (ECA LISP) — REMOVED post-Epic #11230**: rows `2A-1` (`ue_eca_proxy(call_tool, "blueprint_to_lisp")`) and `2A-3` (`lisp_to_blueprint`) were removed alongside the ECABridge stack in PR #11295. All refactoring now flows through Path B (manual analysis-driven). See [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md) for historical context.

## Phase Decision Matrix

| Editor Running? | Path | Phase 1b | Phase 2 | Phase 3 |
|-----------------|------|----------|---------|---------|
| Yes | B (full) | find_bp_references | Manual Guide + auto_layout | ue_fix_errors |
| No | B (minimal) | SKIP | Manual Guide | N/A |

## LISP Format Reference (Phase 2A)

ECA's LISP notation represents Blueprint graphs compactly.

### Syntax

```lisp
;; Event with sequential nodes
(EventName (Node1 params...) (Node2 params...))

;; Function definition
(Function "FunctionName" (Node1) (Node2))

;; Function with parameters
(Function "FunctionName" (Param1 Param2) (Node1 Param1) (Node2 Param2))

;; Branch
(Branch Condition (True (Node1)) (False (Node2)))

;; Delay
(Delay Duration (Node1))
```

### Common Refactoring Transforms

| Pattern | Before LISP | After LISP |
|---------|-------------|------------|
| Extract Function | `(Event (A) (B) (C))` | `(Event (CallFunction "DoABC"))` + `(Function "DoABC" (A) (B) (C))` |
| Remove Dead Code | `(Event (A)) (DeadEvent (B))` | `(Event (A))` |
| Consolidate Duplicates | `(A x) (A y) (A z)` | `(Function "DoA" (p) (A p))` + 3× `(CallFunction "DoA" ...)` |
| Replace Loop in Tick | `(Tick (ForEach arr (Process)))` | `(Timer 0.1 (ProcessBatch arr 10))` |

## Refactoring Pattern Catalog

| Issue Type | Detection Criteria | Severity | Recommendation |
|------------|-------------------|----------|----------------|
| **Complexity** | EventGraph >20 nodes | Medium | Split into named functions |
| **Duplication** | 2+ identical node sequences (≥3 nodes each) | Medium | Extract shared function |
| **Dead Code** | Event/Function with 0 callers | Low | Delete after confirmation |
| **Performance** | ForEach/LineTrace in Tick without throttle | High | Timer-based batching or rate limiting |
| **Deep Nesting** | Branch depth >3 levels | Medium | Extract conditions into functions |
| **Large Function** | Single function >15 nodes | Medium | Split by responsibility |

## Domain Routing Notes

| Query Pattern | DO Use | DON'T Use |
|---------------|--------|-----------|
| BP-to-BP references | `ue_manage_blueprint(find_bp_references)` | `ue_analyze_symbols(find_callers)` — matches C++ substrings |
| BP summary | `ue_read(domain="asset")` | `ue_read(identifier=...)` without domain — routes to C++ symbol |
| Node details offline | `ue_manage_blueprint(get_raw_nodes)` | `get_structure` alone — returns metadata only when offline |

## Self-Test

`/ue-refactor-bp --self-test` validates:

1. **MCP connectivity**: `ue_check_health()` responds with status
2. **Asset search**: `ue_grep(query="BP_*", domain="assets", limit=3)` returns Blueprint results
3. **Structure retrieval**: `ue_manage_blueprint(operation="get_structure", blueprint_name="<any_bp>")` returns node count
4. **Editor reachability**: `ue_check_health()` → check `editor_control` port status (Editor running for `find_bp_references` and `auto_layout`)

**Expected**: Steps 1-3 always pass if NarshaMCP is connected and project has Blueprints. Step 4 reports Editor reachability (the legacy ECA path was removed in Epic #11230 / PR #11295).

**Status**: v1.0.0 | Issue #7412
