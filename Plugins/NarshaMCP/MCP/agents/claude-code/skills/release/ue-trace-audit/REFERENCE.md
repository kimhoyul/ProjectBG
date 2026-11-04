# UE Trace Audit — Reference

Issue #20377 · Epic #20369 · substrate validate op #20371

## Triggers & Disambiguation

### Auto-trigger phrases

**KO**: 실행 흐름 감사, 트레이스 무결성, 실행그래프 감사, broken chain 검사, 입력→어빌리티 체인 감사,
트레이스 감사, 실행 흐름 건강도, 그래프 무결성 검사.

**EN**: trace audit, execution graph audit, execution flow audit, broken chain audit,
validate execution flow, integrity audit for trace, BP/GAS graph health, audit execution graph.

### Disambiguation

| Query intent | Route |
|--------------|-------|
| Whole-graph integrity / broken chains scorecard | **ue-trace-audit** (this skill) |
| "What happens when I press Attack?" single path | ue-execution-trace / blueprint-flow |
| InputTag → Ability → Anim 5-stage GAS chain | cross-domain-flow |
| `.utrace` capture quality / hitch / FPS | ue-perf-audit |
| Project health (config+assets+code) | ue-audit |
| Comprehension eval of a trace JSON | bp-flow-comprehension-eval |

Negative: do **not** activate for utrace/Insights profiling, static Tick lint, or daemon memory.

---

## Category glossary (validate issues)

From `ue_trace_execution(operation="validate")` Issue #20371:

1. **orphan_node** — node unreachable from graph roots / orphaned.
2. **dangling_exec_pin** — exec input pin without a producer edge.
3. **no_handler_event** — event node with no handler path.
4. **broken_input_ability_chain** — `input_ability_bindings` entry whose ability class or
   InputAction is missing from metadata (only when those categories are non-empty).
5. **unresolved_channel** — GameplayMessage pub/sub site whose channel string did not resolve
   to a tag (reuses `trace_gameplay_messages` unresolved bucket).

`checks_passed` may include: `no_orphan_nodes`, `no_dangling_exec_pins`,
`no_handler_missing_events`, `input_ability_chains_resolved`, `message_channels_resolved`.
A check is only listed when that class was actually run and found zero defects.

Issue #23816: validate also returns structured `checks[]` rows with
`status` ∈ {`passed`,`failed`,`not_run`} and optional `reason`. `complete=false`
when any required check is `not_run` (e.g. GameplayMessage corpus load failed,
or no bindings for a chain check that could not run). Lyra tag-based
`InputAbilityBinding` rows with empty `ability_class` + non-empty `input_tag`
must still resolve to a GameplayAbility endpoint or emit
`broken_input_ability_chain`.

---

## Scoring rubric (audit score 0–100)

Weights (renormalize when a dimension is N/A):

| Dimension | Weight | Source |
|-----------|--------|--------|
| Integrity (errors) | 40 | `summary.errors` — 0 → 40 pts; any error → 0 |
| Chain/warn hygiene | 30 | `summary.warnings` — 0 → 30; 1–3 → 20; 4–10 → 10; >10 → 0 |
| Info / channel hygiene | 15 | `summary.info` — 0 → 15; 1–5 → 10; >5 → 5 |
| Coverage confidence | 15 | `truncated==false` and `nodes_scanned>0` and `complete!=false` → 15; truncated or `complete==false` / any `checks[].status==not_run` → 5; empty graph → N/A |

**Overall letter grade** (skill primary output):

| Grade | Condition |
|-------|-----------|
| **PASS** | errors=0, warnings=0, graph non-empty, not truncated, **and** `complete!=false` with **no** required check `not_run` |
| **WARN** | errors=0 and (warnings>0 or truncated or info-heavy or `complete==false` / any `not_run`) |
| **FAIL** | errors>0 |
| **N/A** | blueprints_scanned=0 and nodes_scanned=0 (or validate failed) |

Numeric score is optional secondary; letter grade is authoritative for the scorecard header.
Never award PASS/100 when a required check is `not_run` or `complete` is false
(Issue #23816) — missing work is not zero defects.

---

## Verdict & citation rules

- Cite every count as `` `summary.errors` `` / `` `summary.warnings` `` / `` `stats.nodes` `` style
  field paths from the raw MCP JSON (not paraphrased).
- Never invent issue rows. If `issues` is empty, say so.
- If `truncated` is true, print: "Finding list truncated at max_issues; counts may under-report."
- Empty graph: "MCP data: graph empty — regenerate metadata" (not "project is healthy").

---

## Report template

```markdown
# Execution-Flow Integrity Audit

**Grade**: PASS | WARN | FAIL | N/A
**Score**: <0-100 or N/A>
**Scope**: whole-project | blueprint=<name>

## 1. Scale (`get_stats`)
| Metric | Value |
|--------|-------|
| nodes | `stats.nodes` |
| edges | `stats.edges` |
| blueprints | `stats.blueprints` |
| tags | `stats.tags` |

## 2. Integrity summary (`validate`)
| Field | Value |
|-------|-------|
| errors | `summary.errors` |
| warnings | `summary.warnings` |
| info | `summary.info` |
| checks_passed | `checks_passed` |
| blueprints_scanned | `summary.blueprints_scanned` |
| nodes_scanned | `summary.nodes_scanned` |
| truncated | `truncated` |

## 3. Findings by category
| Category | Count | Top severity |
|----------|-------|--------------|
| orphan_node | n | … |
| dangling_exec_pin | n | … |
| no_handler_event | n | … |
| broken_input_ability_chain | n | … |
| unresolved_channel | n | … |

## 4. Top offenders (≤10)
1. [severity] category — message (blueprint/action/ability/channel if present)
…

## 5. Recommendations
- …
```

---

## Error recovery

| Symptom | Action |
|---------|--------|
| Unknown tool / schema miss | ToolSearch full name `mcp__narshamcp__ue_trace_execution` once |
| `success: false` | Print error; stop (no PASS) |
| Empty graph | Suggest `ue_cache_control(regenerate_metadata)`; grade N/A |
| truncated true | Re-run with higher `max_issues` (≤5000 clamp) if user wants full list |
| validate missing op | Build is pre-#20371; report substrate missing |

---

## Evaluation criteria (skill quality)

1. Runs validate + get_stats (not docs-only).
2. Grades FAIL when `summary.errors > 0`.
3. Does not PASS empty graph.
4. Disambiguates vs ue-perf-audit (.utrace) and ue-execution-trace (single path).
5. ≤3 MCP tool calls for the default whole-project audit.
