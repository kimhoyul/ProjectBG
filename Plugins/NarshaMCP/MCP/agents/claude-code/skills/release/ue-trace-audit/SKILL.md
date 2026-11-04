---
name: ue-trace-audit
description: "Purpose: offline execution-flow integrity audit for BP/GAS graphs. 2-stage rollup (validate op → audit skill, Issue #20377 / Epic #20369): ue_trace_execution validate acts as the integrity gate (orphan nodes, dangling exec pins, handler-less events, broken input→IA→ability chains, unresolved GameplayMessage channels), then get_stats supplies graph scale context, and this skill folds both into ONE scored broken-chain report. NOT single-path tracing (use ue-execution-trace / blueprint-flow). NOT .utrace runtime perf quality (use ue-perf-audit). NOT project-wide 6-point health (use ue-audit). Triggers: '실행 흐름 감사', '트레이스 무결성', 'broken chain', 'trace audit', 'execution graph audit', 'validate execution flow'."
compatibility: "Requires NarshaMCP MCP server with ue_trace_execution validate (Issue #20371) + get_stats; offline metadata/execution graph (no Editor required)"
argument-hint: "optional blueprint_name to scope validate; empty = whole-project execution graph"
allowed-tools:
  - ue_trace_execution
  - ue_check_health
metadata:
  version: "1.0.0"
  issue: "#20377"
  author: "Next-Stage-Inc"
audience: end-user
---

# UE Trace Audit — Execution-Graph Integrity Rollup

**Issue**: #20377 (Epic #20369; substrate validate op #20371)
**Purpose**: Answer "이 프로젝트의 실행 흐름 그래프는 무결한가?" with ONE scored report:
the materialized `ue_trace_execution(validate)` integrity gate first, then `get_stats`
scale context, with a ranked broken-chain / defect rollup.

> **Reference**: scoring rubric, report template, category glossary, and citation rules
> live in [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the audit workflow
> automatically**. Do NOT just display documentation — run the MCP tools.

1. **Target**: `$ARGUMENTS` — optional `blueprint_name` (BP_*/GA_* style) to scope validate.
   Empty → whole-project execution-graph audit.
2. Run **Step 0 → 1 → 2 → 3** below.
3. Every number in the report MUST be cited by raw response field name per
   [REFERENCE.md § Verdict & Citation Rules](REFERENCE.md#verdict--citation-rules).

**Call-count minimization** (MANDATORY): complete in **2-3 MCP calls** (Bash excluded).
Optimal 2 = `validate` + `get_stats`. Do NOT fan out per-blueprint
`trace_execution_flow` / `trace_ability` unless the user asks to deep-dive a single
finding.

---

## Workflow

### Step 0: Tool schema + optional health

```python
ToolSearch("select:mcp__narshamcp__ue_trace_execution")
# Optional (when daemon state is unknown):
# ue_check_health()
```

### Step 1: Integrity gate (`validate`)

```python
# Whole project (default):
v = ue_trace_execution(operation="validate", max_issues=200)
# Scoped:
# v = ue_trace_execution(operation="validate", blueprint_name="<from $ARGUMENTS>", max_issues=200)
# → success, issues[], checks_passed[], summary{errors,warnings,info,passed,
#   blueprints_scanned,nodes_scanned}, truncated, total_time_ms
```

**Gate honesty** (MCP Empty-Result Fallback):
- `success != true` → report failure and stop (do not invent a PASS).
- `truncated == true` → cap is hit; grade **WARN** at best and note that findings are incomplete.
- Empty `issues` with `summary.blueprints_scanned == 0` and `nodes_scanned == 0` →
  **N/A (graph empty / metadata not indexed)** — suggest
  `ue_cache_control(operation="regenerate_metadata")`, never PASS.

### Step 2: Scale context (`get_stats`)

```python
s = ue_trace_execution(operation="get_stats")
# → stats{nodes, edges, blueprints, tags}, total_time_ms
```

Use `stats` only as **context** (graph size), not as a health grade. A large graph with
zero integrity issues is still PASS; a tiny graph with errors is still FAIL.

### Step 3: Scored broken-chain report

Compose the report using
[REFERENCE.md § Scoring Rubric](REFERENCE.md#scoring-rubric-audit-score-0-100)
and [REFERENCE.md § Report Template](REFERENCE.md#report-template).

Bucket `issues[]` by `category` (five defect classes from #20371):

| Category | Severity typical | Meaning |
|----------|------------------|---------|
| `orphan_node` | error/warning | Unreachable / orphan graph node |
| `dangling_exec_pin` | error/warning | Exec input pin with no producer |
| `no_handler_event` | warning | Event with no handler |
| `broken_input_ability_chain` | warning | Input→IA→ability endpoint missing |
| `unresolved_channel` | info | GameplayMessage channel unresolved |

**Overall grade**: any `summary.errors > 0` → **FAIL** · else any `summary.warnings > 0`
or `truncated` → **WARN** · else non-empty graph with no issues → **PASS** · empty graph →
**N/A**.

Rank top offenders: error > warning > info. For each top finding quote
`category`, `severity`, `message`, and any of `blueprint` / `input_action` /
`ability_class` / `channel` present on the issue object.

End with Recommendations:
1. Fix FAIL categories first (orphan / dangling).
2. Repair `broken_input_ability_chain` bindings (ability class or InputAction).
3. Resolve `unresolved_channel` tags (or accept as hygiene).
4. If empty graph: regenerate metadata, then re-run this skill.
5. Deep single-path follow-up → `/ue-execution-trace` or `/blueprint-flow` on one BP.

---

## Tool Disambiguation

| Need | Skill / tool |
|------|----------------|
| Whole-graph integrity scorecard | **this skill** (`/ue-trace-audit`) |
| Single Input→Ability path | `/ue-execution-trace`, `/blueprint-flow` |
| GAS InputTag deep chain (5-stage) | `/cross-domain-flow` |
| `.utrace` capture quality / runtime FPS | `/ue-perf-audit` |
| Project 6-point health | `/ue-audit` |

Full table: [REFERENCE.md § Disambiguation](REFERENCE.md#disambiguation).

---

## See Also

- [REFERENCE.md](REFERENCE.md) — rubric, template, category glossary, citation rules
- `ue_trace_execution(validate)` — Issue #20371 offline integrity op
- `ue_trace_execution(get_stats)` — graph scale
- `/ue-execution-trace` — single-flow tracing
- `/ue-perf-audit` — `.utrace` quality rollup (different domain)
