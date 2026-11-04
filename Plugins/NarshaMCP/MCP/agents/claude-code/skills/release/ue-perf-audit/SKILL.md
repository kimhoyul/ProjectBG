---
name: ue-perf-audit
description: "Purpose: trace-quality + runtime-performance audit of a .utrace capture. 2-stage convergence (offline validate op → rollup audit skill, Issue #20596): ue_analyze_insights validate_trace acts as the quality gate, then smart_analyze + detect_frame_drops + the 9-channel rollup fold into ONE scored audit report (trace quality 0-100 + runtime health verdict + recapture guidance). NOT static project audit (use performance-health-check), NOT project health diagnostics (use ue-audit), NOT deep single-bottleneck cause hunting (use ue-insights-profiler). Triggers: '성능 감사', '트레이스 품질', '캡처 품질', 'perf audit', 'trace quality audit', 'validate trace', 'runtime performance audit'."
compatibility: "Requires NarshaMCP MCP server with the Issue #20596 materialized validate_trace op; a .utrace capture (or a live Editor for the capture chain)"
argument-hint: ".utrace path (optional — auto-discovers Saved/Profiling), analysis intent (cpu|gpu|frames|hitch|memory|loading|network|file|tasks|counters|log)"
allowed-tools:
  - ue_analyze_insights
  - ue_glob
  - ue_check_health
  - ue_editor_automation
metadata:
  version: "1.0.0"
  issue: "#20596"
  author: "Next-Stage-Inc"
audience: end-user
---

# UE Performance Audit — Trace Quality + Runtime Performance Rollup

**Issue**: #20596
**Purpose**: Answer "이 캡처는 믿을 수 있고, 런타임 성능은 건강한가?" with ONE scored report:
the materialized `validate_trace` quality gate first, then a runtime rollup
(`smart_analyze` + `detect_frame_drops` + 9-channel summary), with recapture
guidance for everything the trace cannot answer.

> **Reference**: scoring rubric, report template, capture chain, citation rules,
> and the full `quality` response schema live in [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the audit workflow
> automatically**. Do NOT just display documentation — run the MCP tools.

1. **Target**: `$ARGUMENTS` — a `.utrace` path and/or an analysis intent word.
2. Run **Step 0 → 1 → 2 → 3 → 4** below.
3. Every number in the report MUST be cited by raw response field name per
   [REFERENCE.md § Verdict & Citation Rules](REFERENCE.md#verdict--citation-rules).

---

## Workflow

### Step 0: Trace Discovery

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_insights")
# $ARGUMENTS path given → use it directly (preferred).
# Otherwise list candidates — but do NOT silently assume "newest by mtime"
# when an MCP-owned capture just finished; prefer capture_stop.trace_path.
traces = ue_glob(pattern="Saved/Profiling/**/*.utrace")   # also try Saved/Traces/**
```

- **Explicit path / user-selected path** → Step 1.
- **No path + live Editor + user asked to capture** → run the
  [owned Capture Chain](REFERENCE.md#capture-chain-step-c--캡처검증분석리포트)
  (`capture_start` → scenario → `capture_status` → `capture_stop` → use
  `stop.trace_path`) then Step 1.
- **No path at all** → print the capture guide from REFERENCE.md and stop.

### Step 1: Trace Quality Gate (`validate_trace`)

```python
quality = ue_analyze_insights(
    operation="validate_trace",
    trace_path="<.utrace path>",
    intent="<intent word if the user stated one — e.g. gpu, hitch, memory>",
)
# → response.quality: verdict / score / channels / capture / clock /
#   frame_boundaries / session / intent / capabilities / warnings / notes
```

**Gate**: if `quality.verdict == "unusable"` → STOP after reporting Section 1 only,
with `quality.warnings` verbatim + the `channels.missing[].recapture_hint` flags.
Do not run Steps 2-3 on an unusable trace — the numbers would be noise.

### Step 2: Runtime Rollup (`smart_analyze`)

```python
overview = ue_analyze_insights(operation="smart_analyze", trace_path="<path>", top_n=20)
# → stats / hot_functions / frame_summary / gpu_summary / memory_summary /
#   counters_summary / channels (9-channel rollup)
```

### Step 3: Frame Health (`detect_frame_drops`)

**UNSCORED (never invent a 0→100 Frame Health score)** when any of:

1. `quality.frame_boundaries.present == false`, **or**
2. `quality.frame_boundaries.frame_health_scorable == false` (Issue #23856:
   GPU-only traces can set `present=true` via `gpu_frame_count` while CPU
   `frame_events` are empty — hitch ops still fail), **or**
3. the `detect_frame_drops` response reports `success:false` / non-null
   `error_kind` (e.g. `no_frame_data`) — always check producer success
   **before** reading `summary.*` zeros.

```python
if not quality.get("frame_boundaries", {}).get("frame_health_scorable",
        quality.get("frame_boundaries", {}).get("present", False)):
    frame_health = "UNSCORED"  # do not call, or ignore zeros
else:
    drops = ue_analyze_insights(operation="detect_frame_drops", trace_path="<path>",
                                hitch_threshold_ms=33.33, top_n=10)
    if drops.get("success") is False or drops.get("error_kind"):
        frame_health = "UNSCORED"  # NoFrameData / empty producer — NOT 100
    else:
        # score from summary.drop_rate_percent / worst_drop_ms per REFERENCE.md
        ...
# → frame_drops[] + summary{drop_count, drop_rate_percent, worst_drop_ms, ...}
```

### Step 4: Scored Report

Compose the report using [REFERENCE.md § Scoring Rubric](REFERENCE.md#scoring-rubric-audit-score-0-100)
(Trace Quality 40% / Frame Health 30% / Runtime Balance 30%, UNSCORED weights
renormalized) and [REFERENCE.md § Report Template](REFERENCE.md#report-template).
End with Recommendations: `quality.capabilities.unavailable[]` recapture hints,
`quality.capabilities.unsupported[]` permanently-unavailable ops cited by `op` +
`blocking_artifact` + `reason` (Issue #24725 — never offer a recapture for
these; no capture setting enables them), the worst hitch offender (suggest a
/ue-insights-profiler deep dive on `top_contributor`), and the
unsatisfied-intent `-trace=` flag when present.

**Session / compatibility**: cite `quality.session.compatibility` (`ok` |
`unknown` | `invalid` | `incompatible`). Never treat a non-empty
`build_version` string as automatically `ok` — Issue #23856 classifies against
the supported engine range and surfaces `compatibility_detail`.

---

## Tool Disambiguation

Breadth-first scored snapshot of ONE capture. Depth-first cause hunting →
/ue-insights-profiler · static project audit → /performance-health-check · project
health diagnostics → /ue-audit · daemon memory → /mem-report. Full table:
[REFERENCE.md § Disambiguation](REFERENCE.md#disambiguation-full-table).

---

## See Also

- [REFERENCE.md](REFERENCE.md) — rubric, template, capture chain, citation rules,
  `quality` schema, intent alias list
- `ue_analyze_insights(validate_trace)` — Issue #20596 materialized quality op
- [/ue-insights-profiler REFERENCE.md](../ue-insights-profiler/REFERENCE.md) — 55-op
  `ue_analyze_insights` catalog + output templates
