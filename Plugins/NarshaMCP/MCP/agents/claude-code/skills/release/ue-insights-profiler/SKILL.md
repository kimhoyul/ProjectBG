---
name: ue-insights-profiler
description: "Purpose: Comprehensive UE Insights profiling analysis. Answers 'Why is my game slow?' by analyzing .utrace files, identifying hot functions, detecting performance regressions, and providing optimization suggestions. Orchestrates ue_analyze_insights for CPU/GPU bottleneck identification. Triggers: '성능 분석', '프로파일링', '왜 느려', 'FPS 분석', 'utrace 분석', 'performance profiling', 'bottleneck analysis', 'why is my game slow', 'CPU/GPU analysis', 'hot function', 'trace analysis'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "trace file path (.utrace), target function or category, comparison baseline trace"
allowed-tools:
  - ue_analyze_insights
  - ue_check_health
  - ue_glob
  - ue_analyze_config
  - ue_editor_automation
  - ue_analyze_symbols
metadata:
  version: "1.3.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Insights Profiler -- Performance Analysis Assistant

<!-- cspell:ignore RHIT beginplay tracefile -->

**Issue**: #6047
**Purpose**: Answer "Why is my game slow?" with systematic profiling from .utrace analysis to actionable optimization suggestions.

> **Reference**: Trigger phrases (KO+EN), `ANALYSIS_CLASSIFIERS` keyword dict, full output format templates (Trace-Free + Full Trace), error recovery, evaluation criteria, and the **55-operation `ue_analyze_insights` tool catalog (SSOT, 10 categories)** — all in [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the profiling workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

1. **Target**: `$ARGUMENTS`. If empty, ask the user for a `.utrace` file path or performance symptom.
2. **Detect analysis mode** from query (general profiling / regression / hot-function / optimization
   guidance) using [REFERENCE.md § Analysis Mode Classification](REFERENCE.md#analysis-mode-classification).
3. **Execute ue_analyze_insights** with appropriate operation (55 ops cataloged in
   [REFERENCE.md § Tool Reference](REFERENCE.md#tool-reference-ue_analyze_insights-55-operations)).
4. **Cross-reference** with symbol analysis if hot functions are identified.
5. **Generate performance report** using the appropriate output template
   ([REFERENCE.md § Output Format](REFERENCE.md#output-format)).

---

## Workflow

### Step 0: Trace Availability Check (Issue #6994)

Before any analysis, determine whether `.utrace` data is available. This gates the workflow into
**Trace Path** (full profiling, Steps 1-5) or **Trace-Free Fallback Path** (config + editor stats +
symbol analysis).

```python
ToolSearch("select:mcp__narshamcp__ue_check_health,mcp__narshamcp__ue_glob")

# 1. Verify MCP connectivity
health = ue_check_health()

# 2. If user provided a .utrace path in $ARGUMENTS, use it directly → Step 1.

# 3. Otherwise, auto-discover available traces:
traces = ue_glob(pattern="Saved/Profiling/**/*.utrace")
# Also check: ue_glob(pattern="Saved/Traces/**/*.utrace")
```

**Decision**:
- **Trace found** → proceed to **Step 1** (full trace analysis).
- **No trace found** → route to **Trace-Free Fallback Path** below.

---

### Trace-Free Fallback Path (Issue #6994)

When no `.utrace` file is available, provide useful performance analysis using alternative data sources.
Each fallback analysis is **independent** — partial results are still valuable if some tools fail (e.g., Editor offline).

#### Fallback A: CVar/Config Performance Analysis

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_config")

# GPU-related settings
gpu_config = ue_analyze_config(operation="search_config", query="r.Shadow")
screen_pct = ue_analyze_config(operation="search_config", query="r.ScreenPercentage")
scalability = ue_analyze_config(operation="search_config", query="sg.")

# CPU-related settings
tick_config = ue_analyze_config(operation="search_config", query="tick")
occlusion = ue_analyze_config(operation="search_config", query="r.AllowOcclusionQueries")

# Memory/Streaming settings
streaming = ue_analyze_config(operation="search_config", query="r.Streaming")
```

Flag non-default values that commonly cause performance issues (e.g., `r.Shadow.MaxResolution` too high, `r.ScreenPercentage` >100, tick dispatch disabled).

#### Fallback B: Editor Live Stats (if Editor is running)

```python
ToolSearch("select:mcp__narshamcp__ue_editor_automation")
fps_stats = ue_editor_automation(operation="execute_console_command", command="stat fps")
unit_stats = ue_editor_automation(operation="execute_console_command", command="stat unit")
# unit stats: Frame, Game, Draw, GPU, RHIT times
```

If Editor is offline, skip gracefully and report "Editor not running — launch Editor for real-time metrics."

#### Fallback C: Symbol-Based Complexity Analysis

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")

# Trace TickComponent callers (use find_callers — broad smart query times out)
tick_callers = ue_analyze_symbols(operation="find_callers", function_name="TickComponent", limit=10)
beginplay = ue_analyze_symbols(operation="find_callers", function_name="BeginPlay", limit=10)
tick_overrides = ue_analyze_symbols(operation="search_symbols", query="*::Tick", limit=10)
```

Flag classes with heavy Tick implementations, deep call chains from TickComponent, expensive BeginPlay init.

#### Fallback D: Owned Trace Capture (preferred) + Manual Guide

When the user wants full profiling and the Editor is connected, **do not stop at a manual guide**.
Drive the MCP-owned capture chain (Issue #20592 / #23848) so the returned path is authoritative:

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_insights")

# Prefer owned capture over raw Trace.Start / newest-file guesswork.
start = ue_analyze_insights(
    operation="capture_start",
    channels="cpu,gpu,frame,bookmark",  # or preset= / extra channels as needed
)
# Ask the user to reproduce the scenario (or wait the requested duration) while capturing.
status = ue_analyze_insights(operation="capture_status")
stop = ue_analyze_insights(operation="capture_stop")
# Use the handler-owned path only — never "newest .utrace in Saved/Profiling".
trace = stop.get("trace_path") or (stop.get("session") or {}).get("owned_trace_path")
if stop.get("success") and trace:
    # Continue into Step 1 with this exact path.
    result = ue_analyze_insights(operation="smart_analyze", trace_path=trace, top_n=20)
else:
    # Editor offline / flush incomplete: surface stop.error / stop.hint; fall back to manual guide.
    pass
```

Manual capture guide (Editor offline or user prefers UI):

- **In-Editor**: Tools → Trace → Start Trace (channels: cpu,gpu,frame,bookmark) → play scenario in PIE → stop. Saved to `Saved/Profiling/`.
- **CLI**: `UnrealInsights.exe <project>.uproject -trace=cpu,gpu,frame,bookmark -tracefile=MyTrace`
- **Standalone**: Launch `UnrealInsights.exe` (in `Engine/Binaries/Win64/`), connect to running Editor, record → reproduce → stop.

After a successful owned capture, continue to Step 1 with `trace_path` from `capture_stop`.
If capture is unavailable, finish Fallbacks A–C and emit the **Trace-Free Output Format**
([REFERENCE.md § Output Format](REFERENCE.md#output-format)), then stop.

---

### Step 1: Health Check & Initial Trace Analysis

> **Prerequisite**: Step 0 confirmed a `.utrace` file is available.

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_insights")
result = ue_analyze_insights(operation="smart_analyze", trace_path="<.utrace file path>", top_n=20)
```

Extract from **overview_v2** response (handler SSOT, Issue #72/#105/#23848) — only these top-level keys:
`routed_to` (=`overview_v2`), `schema_version`, `stats`, `hot_functions[]`, `frame_summary`
(`avg_frame_ms` / `median_frame_ms` / `p95_frame_ms` / `p99_frame_ms` / `total_frames` when present),
`gpu_summary` (null when GPU channel absent), `memory_summary`, `counters_summary`,
`top_edges[]`, `circular_dependencies`, `channels`, `hint`.

**Do not** read non-existent keys `frame_time_breakdown`, `cpu_vs_gpu`, or `thread_utilization`.
For CPU vs GPU balance, compare `frame_summary` vs `gpu_summary` (when non-null), or call
`get_thread_breakdown` / `diagnose_gpu_bottleneck` as a follow-up deep op.

**Memory channel (Mirror PR #128, Issue #23/#67)**: when the trace was recorded with
`-trace=memory` and the export dir holds Memory channel CSVs, the same response also
carries `memory_summary` — `total_allocated_bytes` / `peak_memory_bytes` /
`allocation_count` / `free_count` / `outstanding_allocations` / `top_pools[]` (top 10
by allocated bytes). Include it in the report when non-null (memory-bound symptoms:
high `outstanding_bytes` growth, one pool dominating `top_pools`). `Null` simply means
the Memory channel was not traced — do not treat it as an error.

### Step 2: Bottleneck Identification

Based on classifier match from Step 1, execute the appropriate deep analysis path.

**Path A — General Profiling**: reuse Step 1 results (do NOT re-call `smart_analyze`). Then:
```python
suggestions = ue_analyze_insights(
    operation="get_optimization_suggestions_trace",
    trace_path="<.utrace path>",
    top_n_functions=10,
    thread_filter="All",
    max_suggestions=3,
)
```
**GPU Note**: When `gpu_summary` is non-null and GPU time dominates (or `frame_summary`
p99 is high with GPU-heavy hot functions), additionally call
`ue_analyze_insights(operation="diagnose_gpu_bottleneck", trace_path="<.utrace path>", severity_threshold=0.5)`
to identify GPU bottleneck patterns separately.

**Path B — Regression Detection**:
```python
result = ue_analyze_insights(
    operation="compare_traces",
    trace_path="<current .utrace>",
    baseline_trace="<baseline .utrace>",
    threshold_percent=5.0,
    top_n=200,
)
# Returns: faster[], slower[], unchanged, new_functions[], removed_functions[], overall_change, metadata
```
Store baselines in `Saved/Profiling/Baselines/` and pass the baseline path explicitly.
`compare_traces` does not create or select a baseline automatically.

**Path C — Hot Function Deep Dive**:
```python
result = ue_analyze_insights(
    operation="find_callers",
    function_name="<expensive function>",
    trace_path="<.utrace path>",
)
# Returns: callers[], total
```

`find_callers` returns direct callers (one hop). For a bounded multi-level graph,
use `visualize` with `function_name` and `max_depth`.

**Path D — Optimization Suggestions**:
```python
result = ue_analyze_insights(
    operation="get_optimization_suggestions",
    trace_path="<.utrace path>",
    function_name="<hot function>",
    pattern="<tick|draw_calls|gc_pause|physics|animation|loading|network|ai|shader|async_task>",
    max_suggestions=3,
)
# Returns: operation, mode, function_name, pattern_detected, suggestions[], metadata
```

### Step 3: Cross-Reference with Symbol Analysis

When hot functions are identified, cross-reference with C++ symbol data for deeper context.

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")

for func in hot_functions[:5]:
    symbol = ue_analyze_symbols(operation="smart", function_name=func.name)
    # If function is in project code, check for known anti-patterns
    if symbol.is_project_code:
        impact = ue_analyze_symbols(operation="impact_analysis", function_name=func.name)
```

### Step 4: Config-Driven Performance Check (Optional)

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_config")
config = ue_analyze_config(operation="search_config", query="r.Shadow")
# Select CVars based on bottleneck type from Step 2:
# - CPU-bound: r.AllowOcclusionQueries, tick.* (tick.AllowAsyncTickDispatch, etc.)
# - GPU-bound: r.Shadow*, r.ScreenPercentage, sg.* (sg.ShadowQuality, etc.)
# - Memory:    r.Streaming.* (r.Streaming.PoolSize, r.Streaming.MaxEffectiveScreenSize, etc.)
```

### Step 5: Generate Report

Compile all findings into the **Full Trace Output Format**
([REFERENCE.md § Output Format](REFERENCE.md#output-format)).

---

## Verdict Citation Rules (Issue #96 F-006 / F-007)

These rules eliminate the inclusive↔exclusive verdict drift class. Pre-fix,
the same trace produced opposite conclusions ("GameThread-bound" vs "GPU-bound")
because summarizers chose different time bases per question phrasing, and
summary text was recomputed (yielding impossible values like "1318초" on a
43-second trace). The fix is **three layers**:

- **Layer 1 (Rust response fields)** — `get_thread_breakdown` exposes
  `verdict_source` / `verdict_value` / `verdict_warning` fields (computed by
  `compute_verdict_fields` in `analysis_ext.rs`) so verdicts are pre-extracted,
  not re-derived by the skill.
- **Layer 2 (Skill citation rules)** — this skill MUST cite those Layer 1
  fields verbatim per the four rules below (Verdict / Citation / Warning
  Surfacing).
- **Layer 3 (Post-hoc sanity check)** — every ms/sec number printed must be
  bounded by `trace_duration_seconds * 1000`; over-bound values are rejected.
  Caveat: `trace_duration_seconds` is currently absent from `get_thread_breakdown`
  responses — fall back to `summary.total_time_ms` as a conservative ceiling
  until the field is emitted (Issue #96 follow-up).

### Verdict Rule (Layer 2)

> The bottleneck verdict MUST be cited from the response's
> `verdict_source` / `verdict_value` fields **verbatim**. Do not recompute
> from `threads[]` or any per-thread sum. Do not re-pick a "different
> bottleneck" based on question phrasing.

`get_thread_breakdown` always sets:
- `verdict_source` = `"inclusive_time_ms"` (constant whenever data exists, `null` on empty)
- `verdict_value` = bottleneck thread's `inclusive_time_ms` (`null` on empty)

If a user asks "what's the bottleneck?", the answer is the response's
`summary.bottleneck_thread` with `verdict_value` ms — period.

### Citation Rule (Layer 2)

> All numbers in the summary MUST be cited by raw response field name and value,
> with NO unit conversion, NO recomputation, NO synthesis. Reference the field
> path (e.g. `frame_summary.avg_frame_ms`) so the reader can audit the source.

Example (correct):
> Bottleneck: `summary.bottleneck_thread = "GameThread"` with
> `bottleneck_analysis.time_ms = 16.0` (inclusive), holding
> `bottleneck_analysis.percentage = 57.1%` of total thread time.

Example (forbidden — recomputed):
> SceneRender took 1318 seconds  ← impossible on a 43s trace; recomputed from incomplete data.

### Warning Surfacing Rule (Layer 2)

> If the response's `verdict_warning` is non-null, the report MUST surface it
> verbatim to the user with the warning code as the prefix, before any
> bottleneck conclusion.

Possible `verdict_warning` codes (defined in `compute_verdict_fields`):
- `empty_thread_breakdown` — threads[] empty but hot_functions exist (label leak)
- `duration_overflow` — bottleneck time > total thread time (data corruption)
- `percentage_sum_invalid` — `|sum(thread.percentage) - 100| > 1.0`
- `uncertain` — bottleneck < 30% (no dominant thread; verdict low-confidence)

Example surfacing:
> ⚠️ `verdict_warning: "uncertain"` — bottleneck thread holds only X%
> of total time. Verdict confidence is LOW; consider widening capture window
> or excluding idle frames before concluding.

### Sanity Check Rule (Layer 3)

> Before printing ANY ms/sec figure, verify it does not exceed the trace's
> wall-clock duration. If `value_ms > trace_duration_seconds * 1000`,
> REJECT the value and print the raw response field path with a brief
> "exceeds trace duration — likely recomputation error" note.
>
> **Caveat**: `trace_duration_seconds` is currently absent from
> `get_thread_breakdown` responses (Issue #96 follow-up — emit not yet wired).
> Use `summary.total_time_ms` as a conservative fallback ceiling. Note that
> `summary.total_time_ms` is the SUM of per-thread inclusive time, so parallel
> multi-thread stacks can inflate it past wall-clock — sub-ms over-cites may
> escape, but orders-of-magnitude errors (the F-006/F-007 "1318초 on a 43s
> trace" class) are still caught.

The dispatcher's `total_time_ms` (insights.rs:1455-1458) bounds the response's
processing time; the trace itself has duration `trace_duration_seconds` per
`smart_analyze`. Numbers exceeding the latter are guaranteed-bogus.

---

## Tool Disambiguation

| Scenario | This Skill (ue-insights-profiler) | ue-debug (performance path) |
|----------|----------------------------------|---------------------------|
| "Why is my game slow?" | Full profiling workflow with trace analysis | Quick bottleneck check as part of general debugging |
| "Analyze this .utrace file" | Primary handler — deep trace analysis | Would not activate |
| "FPS drop cause" | Deep analysis with regression detection | Quick diagnosis, may route here |
| Trace comparison | `compare_traces` with delta analysis | Not supported |
| Optimization suggestions | `get_optimization_suggestions` with priorities | General performance tips only |

**Rule**: If the query is specifically about profiling, traces, or systematic performance analysis, use this skill. If performance is one symptom among others (e.g., "game crashes and is slow"), use `/ue-debug` which may route to this skill's tools.

---

## See Also


- [REFERENCE.md](REFERENCE.md) — Trigger phrases, `ANALYSIS_CLASSIFIERS` keyword dict, output format templates (Trace-Free + Full Trace), error recovery, evaluation criteria, **55-operation `ue_analyze_insights` SSOT catalog** (CALLGRAPH 8 / FRAME 6 / ADVANCED 10 / GPU 2 / LOADING 4 / BASELINE 5 / MEMORY 4 / OPTIMIZATION 3 / UTILITY 8 + query_channel + CAPTURE 4)

- [EXAMPLES.md](EXAMPLES.md) — Copy-ready regression comparison, caller analysis, and optimization workflows using the current handler contract
