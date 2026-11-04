# /ue-insights-profiler — Reference Material

<!-- cspell:ignore callstacks fopen hotspots loadtime -->

> Companion to [SKILL.md](SKILL.md). Reference content (trigger phrases, analysis-mode
> classifiers, full output format templates, error recovery, evaluation criteria, and the
> 55-operation `ue_analyze_insights` catalog) lives here. SKILL.md keeps the imperative
> Steps 0-5 workflow.

---

## Auto-Trigger Phrases

### Korean
- "왜 게임이 느려?", "성능 분석해줘"
- "프로파일링 결과 보여줘", "utrace 분석해줘"
- "FPS 드랍 원인 분석", "병목 찾아줘"
- "CPU 바운드인지 GPU 바운드인지 확인"
- "핫 함수 찾아줘", "성능 회귀 감지"
- "프레임 타임 분석", "최적화 제안해줘"
- "Insights 트레이스 비교", "이전 빌드 대비 성능 비교"

### English
- "Why is my game slow?", "Profile my game"
- "Analyze this utrace file", "Show profiling results"
- "Find the bottleneck", "What's causing FPS drops?"
- "Is this CPU or GPU bound?", "Hot function analysis"
- "Detect performance regression", "Compare traces"
- "Frame time breakdown", "Optimization suggestions"
- "Insights trace analysis", "Performance comparison with baseline"

---

## Analysis Mode Classification

Step 1 of SKILL.md classifies the user's intent into one of these modes:

```python
ANALYSIS_CLASSIFIERS = {
    "general_profiling": {
        "keywords": ["느려", "slow", "성능 분석", "profile", "병목", "bottleneck",
                     "FPS", "프레임", "frame time", "왜 느려", "why slow",
                     "CPU bound", "GPU bound", "CPU 바운드", "GPU 바운드"],
        "operation": "smart_analyze",
        "description": "General performance profiling -- identify top bottlenecks"
    },
    "regression_detection": {
        "keywords": ["회귀", "regression", "비교", "compare", "이전 빌드", "baseline",
                     "slower than before", "이전보다 느려", "성능 저하", "degradation",
                     "delta", "diff"],
        "operation": "compare_traces",
        "description": "Compare two traces to detect performance regressions"
    },
    "hot_function_analysis": {
        "keywords": ["핫 함수", "hot function", "callers", "호출자", "콜 그래프",
                     "call graph", "누가 호출", "expensive function", "비용 높은 함수",
                     "inclusive time", "exclusive time"],
        "operation": "find_callers",
        "description": "Trace call graph for specific expensive functions"
    },
    "optimization_guidance": {
        "keywords": ["최적화", "optimization", "제안", "suggestions", "어떻게 고쳐",
                     "how to fix", "improve performance", "성능 개선", "줄이기"],
        "operation": "get_optimization_suggestions",
        "description": "Get actionable optimization recommendations"
    },

    # Issue #101 — channel-specific classifiers. These take PRECEDENCE over
    # the broader general_profiling/hot_function_analysis classifiers listed
    # earlier in this dict: when a query matches one of these specific channel
    # keywords (e.g. "초반 끊김", "패킷 드랍"), route to that channel even if a
    # broad keyword also matches, so specific intents don't get swallowed.
    # (Precedence is by intent, not dict position — match the most specific
    # channel first.) All nine route to `smart_analyze` whose response now
    # carries a `channels: {…}` sub-object — the matched category name is read
    # out of that sub-object for the channel-specific answer.
    # Extended by Issue #104 (tasks) + Issue #103 (callstacks).
    "asset_loading": {
        "keywords": ["에셋 로딩", "초반 끊김", "초반에 끊겨", "끊김", "스트리밍 stutter",
                     "로드 시간", "에셋 스트림", "패키지 로딩",
                     "asset load", "loading hitch", "streaming stutter",
                     "load time", "async loading"],
        "operation": "smart_analyze",
        "channel": "asset_loading",
        "description": "Channel: LoadTime — async asset loading events, slowest loads"
    },
    "log_pressure": {
        "keywords": ["로그 부하", "마커 폭주", "마커 분석", "북마크 분석",
                     "log spam", "marker flood", "log pressure", "bookmark analysis",
                     "trace markers"],
        "operation": "smart_analyze",
        "channel": "log_pressure",
        "description": "Channel: Markers — Bookmark + Log event frequency"
    },
    "network_activity": {
        "keywords": ["네트워크 분석", "패킷 드랍", "리플리케이션", "RPC 분석",
                     "네트워크 트래픽",
                     "network", "packet drop", "RPC", "replication", "net traffic"],
        "operation": "smart_analyze",
        "channel": "network_activity",
        "description": "Channel: Net — connections, objects, packet events"
    },
    "file_io": {
        "keywords": ["파일 IO", "디스크 읽기", "fopen 분석", "파일 핸들",
                     "file IO", "disk read", "fopen analysis", "file handle"],
        "operation": "smart_analyze",
        "channel": "file_io",
        "description": "Channel: File — PlatformFile events + handle lifecycle"
    },
    "strings_channel": {
        "keywords": ["FName 분석", "스트링 풀", "이름 중복", "이름 테이블", "정적 문자열",
                     "string definitions",
                     "FName analysis", "string pool", "static strings"],
        "operation": "smart_analyze",
        "channel": "strings",
        "description": "Channel: Strings — StaticString + FName definition tables"
    },
    "diagnostics_channel": {
        "keywords": ["진단 이벤트", "세션 정보", "warning 추적", "빌드 정보",
                     "diagnostics", "session info", "build info", "warning events"],
        "operation": "smart_analyze",
        "channel": "diagnostics",
        "description": "Channel: Diagnostics — FSessionInfo (platform, app, build)"
    },
    "modules_channel": {
        "keywords": ["모듈 로딩", "DLL 추적", "로드된 모듈", "모듈 목록",
                     "module load", "DLL trace", "loaded modules", "module list"],
        "operation": "smart_analyze",
        "channel": "modules",
        "description": "Channel: Modules — loaded DLL/dylib registry"
    },
    # Issue #104 — Tasks channel (TaskTrace; 11 active RouteEvents).
    # Lifecycle of UE's Task Graph (Created/Launched/Scheduled/Started/
    # Finished/Completed/SubsequentAdded/Waiting*). Surfaces
    # `channels.tasks.{total_tasks, total_waits, avg_task_latency_ms,
    # longest_waits, route_event_breakdown}`.
    "tasks_channel": {
        "keywords": ["태스크 분석", "Task Graph", "태스크 그래프", "태스크 의존성",
                     "왜 이 태스크가 대기", "task waiting", "task latency",
                     "TaskGraph dependency", "waiting tasks", "task scheduling"],
        "operation": "smart_analyze",
        "channel": "tasks",
        "description": "Channel: Tasks — FTasksProvider state (lifecycle + waiting top-N + latency)"
    },
    # Issue #103 — Callstacks channel (Memory.CallstackSpec*; 4 routes
    # incl. 3 compression variants). Backing data for allocation/memory
    # attribution: "어디서 alloc 됐는지" 추적. Surfaces
    # `channels.callstacks.{total_events, filled_slots, truncated_count,
    # avg_frame_depth, route_breakdown}`.
    "callstacks_channel": {
        "keywords": ["콜스택 분석", "callstack analysis", "어디서 alloc",
                     "alloc 위치", "메모리 어디서", "프레임 깊이",
                     "where allocated", "memory attribution", "allocation source",
                     "callstack frames", "stack trace analysis"],
        "operation": "smart_analyze",
        "channel": "callstacks",
        "description": "Channel: Callstacks — Memory.CallstackSpec* (slot count + truncation + frame depth + 3-algo breakdown)"
    }
}
```

---

## Output Format

### Trace-Free Output Format (Issue #6994)

Use this format when Step 0 routes to the Trace-Free Fallback Path:

```markdown
=== Performance Analysis (No Trace Available) ===

--- Environment ---
Project: [project name]
Trace File: NONE — using trace-free fallback analysis
Platform: [Win64 / Linux / etc.]

--- Config Analysis ---
Performance-Related CVar Overrides Found: [N]

| CVar | Current Value | Default | Recommendation |
|------|--------------|---------|----------------|
| [r.Shadow.MaxResolution] | [4096] | [2048] | Reduce for GPU savings |
| ... | ... | ... | ... |

Non-Default Settings: [list of overrides that may impact performance]

--- Live Editor Stats (if Editor running) ---
FPS: [X.X] | Frame Time: [X.XX ms]
Game Thread: [X.XX ms] | Render Thread: [X.XX ms] | GPU: [X.XX ms]
Draw Calls: [N] | Triangles: [N]

(If Editor offline: "Editor not running — launch Editor for real-time metrics")

--- Code Complexity Hotspots ---
Tick Functions Found: [N]
| Class | Function | Callers | Potential Impact |
|-------|----------|---------|-----------------|
| [AMyActor] | [Tick] | [5 callers] | High — called every frame |
| ... | ... | ... | ... |

Heavy BeginPlay Patterns: [N functions with deep call chains]

--- How to Get Full Profiling ---
For complete bottleneck analysis with hot functions and regression detection,
capture a .utrace file:

1. In-Editor: Tools → Trace → Start Trace (channels: cpu,gpu,frame)
2. CLI: UnrealInsights.exe <project>.uproject -trace=cpu,gpu,frame,bookmark
3. Saved to: Saved/Profiling/

Then re-run: "Analyze <path-to-file>.utrace"
```

### Full Trace Output Format

Use this format when Step 0 confirms a `.utrace` file is available (Steps 1-5):

```markdown
=== Insights Performance Report ===

--- Environment ---
Project: [project name]
Trace File: [.utrace path or "live session"]
Trace Duration: [X seconds]
Platform: [Win64 / Linux / etc.]

--- Frame Time Summary ---
Average Frame Time: [X.XX ms] ([Y FPS])
P95 Frame Time:     [X.XX ms]
P99 Frame Time:     [X.XX ms]
Budget:             [16.67 ms (60 FPS) / 33.33 ms (30 FPS)]
Status:             [WITHIN BUDGET / OVER BUDGET by X.XX ms]

--- Bottleneck Classification ---
Primary Bottleneck: [CPU Game Thread / CPU Render Thread / GPU / Memory / I/O]
CPU vs GPU Split:   [CPU X.XX ms | GPU X.XX ms]
Limiting Factor:    [specific subsystem or function]

--- Top 10 Hot Functions ---
| # | Function | Inclusive (ms) | Exclusive (ms) | Calls | Thread |
|---|----------|---------------|----------------|-------|--------|
| 1 | [FuncName] | [X.XX] | [X.XX] | [N] | [GameThread] |
| 2 | ... | ... | ... | ... | ... |

--- Regression Analysis (if baseline provided) ---
Regressions (>0.5ms delta):
  [FuncA]: +X.XX ms (was Y.YY ms, now Z.ZZ ms) -- [possible cause]
  [FuncB]: +X.XX ms ...

Improvements:
  [FuncC]: -X.XX ms ...

--- Optimization Suggestions ---
Priority 1 (High Impact):
  [Suggestion with estimated savings and implementation steps]

Priority 2 (Medium Impact):
  [Suggestion ...]

Priority 3 (Low Hanging Fruit):
  [Suggestion ...]

--- CVar Recommendations ---
[CVar]: Current=[value] Recommended=[value] Impact=[description]

--- Next Steps ---
1. [Actionable next step]
2. [Actionable next step]
3. [Actionable next step]
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| No .utrace file found | User hasn't captured a trace yet | Auto-routes to **Trace-Free Fallback Path** (Step 0): config analysis + editor stats + symbol complexity + capture guide (Issue #6994) |
| smart_analyze returns empty | Trace file corrupted or incompatible version | Verify file with `ue_analyze_insights(operation="get_trace_info")`, suggest re-capture |
| compare_traces baseline missing | Baseline trace path invalid or deleted | Run single-trace analysis with `smart_analyze` instead; recommend saving baselines |
| find_callers function not found | Function name doesn't match trace symbols | Use `ue_analyze_symbols(search_symbols)` to find correct function name, retry |
| get_optimization_suggestions empty | Trace too short or no significant bottlenecks | Report "no significant bottlenecks detected"; suggest longer capture or stress test scenario |
| MCP server connection failed | Server not running or project path wrong | Run `ue_check_health()` to diagnose; verify `.mcp.json` configuration |
| Trace file too large (>1GB) | Extended profiling session | Suggest trimming trace range or using `smart_analyze` with `time_range` parameter |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (7)** - Should activate:
1. "왜 게임이 느려?" -> Activate (general_profiling)
2. "Analyze this utrace file at D:/Traces/session.utrace" -> Activate (general_profiling)
3. "이전 빌드 대비 성능 비교해줘" -> Activate (regression_detection)
4. "USkeletalMeshComponent::RefreshBoneTransforms 왜 이렇게 비싸?" -> Activate (hot_function_analysis)
5. "What optimization suggestions do you have?" -> Activate (optimization_guidance)
6. "Is this CPU bound or GPU bound?" -> Activate (general_profiling)
7. "왜 느려?" (no .utrace available) -> Activate (trace-free fallback via Step 0)

**Negative (4)** - Should NOT activate:
1. "빌드 에러 해결해줘" -> Use ue-debug skill (compile_error)
2. "ACharacter 어떻게 동작해?" -> Use ue-explain skill
3. "r.Shadow.MaxResolution 뭐야?" -> Use ue-cvar-explorer skill (single CVar query)
4. "캐릭터 만들어줘" -> Use ue-scaffold skill

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Bottleneck classification accuracy (CPU vs GPU) | >90% |
| Hot function identification (top 5 match) | >85% |
| Regression detection (>0.5ms delta) | >95% |
| Optimization suggestion relevance | >80% |

---

**Status**: Phase 1 MVP + Trace-Free Fallback (Issue #6994)
**Related**: Issue #6047, Issue #6994

---

## Tool Reference: ue_analyze_insights (55 Operations)

- `query_channel` — row-level channel drill-down (Issue #20591). Optional
  `time_start` / `time_end` apply an inclusive filter in each row's native
  clock domain; rows without a temporal field are excluded while filtering.

> **Note**: The MCP schema for this tool is intentionally minimal (thin schema, Issue #7069).
> This section serves as the full operation reference (SSOT for `ue_analyze_insights`).

### Operations by Category

**CAPTURE** (4) — Issue #20592 (MCP-owned Editor Trace.* control; `trace_path` not required):
- `capture_start` — starts an **MCP-owned** session (`Trace.Start` under the hood) with channel preset/list + optional file. Params: `channels`/`preset`, `file`, `dry_run`, `project_root`. Returns `file_hint` / `session`.
- `capture_stop` — stops the **MCP-owned** session and returns the flushed `trace_path` (not an arbitrary newest file). Refuses unowned stop unless `force=true`. Params: `dry_run`, `force`, `project_root`
- `capture_status` — session + filesystem snapshot for the owned capture. Params: `dry_run`, `project_root`
- `insert_bookmark` — `Trace.Bookmark <name>` while capturing. Params: `name`/`bookmark`, `dry_run`

**CALLGRAPH** (8):
- `find_callers` — Find direct callers of a function. Params: `function_name`, `trace_path`
- `find_callees` — Find direct callees of a function. Params: `function_name`, `trace_path`
- `find_path` — Find one call path between two functions. Params: `source`, `target`, `trace_path`
- `find_paths` — Find multiple paths between functions. Params: `source`, `target`, `trace_path`, `max_paths`
- `analyze_hot_paths` — Identify hottest paths from an entry function. Params: `function_name`, `trace_path`, `top_n`
- `visualize` — Render a deterministic, syntax-sanitized call graph (hard cap: 500 nodes, with truncation comment). Params: `function_name`, `trace_path`, `max_depth`
- `get_caller_callee_matrix` — Return caller/callee edges. Params: `trace_path`, `top_n`
- `analyze_circular_dependencies` — Detect circular call patterns. Params: `trace_path`

**FRAME** (6):
- `analyze_frame` — Deep analysis of a specific frame. Params: `trace_path`, `frame_number`, `top_n`
- `get_frame_statistics` — Frame time statistics. Params: `trace_path`, `source` (cpu|game|rendering|all), `top_n`
- `calculate_frame_statistics` — Calculate function-filtered frame statistics. Params: `trace_path`, `function_name`, `top_n`
- `analyze_frame_statistics` — Alias of `calculate_frame_statistics` (Issue #20590). Params: same as `calculate_frame_statistics`
- `detect_frame_drops` — Detect hitches/frame drops. Contributor claims disclose `frame_breakdown`, `csv_multi_event` fixed-60fps bucketing, or `frame_boundary_only` (no ranked contributor). Params: `trace_path`, `hitch_threshold_ms`, `top_n`
- `analyze_hitch` — Rank hitch root-cause timers vs normal-frame baseline. Params: `trace_path`, `hitch_threshold_ms`, `top_n`, optional `start_frame`/`end_frame`

**ADVANCED** (10):
- `analyze_paths` — Analyze paths between two functions using caller-to-callee edge timing, with explicit fallback disclosure. Params: `source`, `target`, `trace_path`, `max_paths`
- `generate_flamegraph` — Generate a bounded flamegraph. Params: `function_name`, `trace_path`, `max_depth`
- `compare_traces` — Compare current and baseline traces over their common function population; `top_n` limits display only. Params: `trace_path`, `baseline_trace`, `threshold_percent`, `top_n`
- `get_thread_callgraph` — Thread-specific call graph. Params: `trace_path`, `thread_id`, `top_n`
- `validate_regression` — Apply an absolute relative-delta threshold heuristic (not a statistical test). Params: `trace_path`, `baseline_trace`, `significance_threshold`, `top_n`
- `link_to_source` — Link a function to source. Params: `function_name`, `trace_path`
- `find_critical_paths` — Find critical paths from an entry function. Params: `function_name`, `trace_path`, `optimization_target` (exclusive|inclusive), `top_n`
- `analyze_critical_path` — Alias of `find_critical_paths` (Issue #20590). Params: same as `find_critical_paths`
- `infer_thread_sync` — Infer thread synchronization. Params: `trace_path`, `thread_name`, `idle_threshold_ms`
- `diagnose_bottleneck` — Diagnose the primary bottleneck. Params: `trace_path`, optional `function_name`, `top_n`

**GPU** (2):
- `analyze_gpu_performance` — GPU performance analysis. Params: `trace_path`
- `diagnose_gpu_bottleneck` — GPU bottleneck diagnosis. Params: `trace_path`, `severity_threshold`

**LOADING** (4):
- `analyze_asset_loading` — Asset loading analysis. Params: `trace_path`, `include_sync`, `include_async`
- `detect_sync_loads` — Detect synchronous loads. Params: `trace_path`
- `analyze_streaming` — Streaming analysis. Params: `trace_path`, `streaming_type`
- `trace_load_chain` — Trace asset load chain. Params: `trace_path`

**MEMORY** (4 registered / 3 servable):
- `analyze_memory_allocations` — Memory allocation analysis. Params: `trace_path`
- `analyze_gc` — Garbage collection analysis. Params: `trace_path`. **⚠️ Not
  servable.** It reads `csv_export/GCEvents.csv`, which no producer in this
  toolchain writes, so no capture setting makes it return data (#20583). It is
  declared in `validate_trace`'s `quality.capabilities.unsupported[]` rather
  than under any channel capability (#24725) — do not recommend a recapture.
- `detect_memory_leaks` — Detect memory leaks. Params: `trace_path`, `threshold_kb`
- `get_memory_timeline` — Memory usage timeline. Params: `trace_path`, `bucket_size_ms`

**BASELINE** (5):
- `create_baseline` — Create performance baseline only from a provenance-bound CSV export; reports depth-aware exclusive time. Params: `trace_path`, `baseline_name`, `description`, `commit_sha`
- `compare_to_baseline` — Compare against baseline. Params: `trace_path`, `baseline_name`, `threshold_percent`
- `track_regression_history` — Track regression history. Params: `trace_path`, `baseline_name`
- `list_baselines` — List all baselines with stable `{version,total,baselines[]}` shape. Params: `trace_path`
- `delete_baseline` — Delete a baseline. Params: `baseline_name`

**OPTIMIZATION** (3):
- `get_optimization_suggestions` — Single-function suggestions. Detected patterns without a reviewed template appear in `unsupported_patterns`; estimate ranges are `reference_only`, not trace-measured. Params: `trace_path`, `function_name`, optional `pattern` (tick|draw_calls|gc_pause|physics|animation|loading|network|ai|shader|async_task), `max_suggestions`
- `get_optimization_suggestions_batch` — Batch suggestions for multiple functions. Params: `trace_path`, `functions` (array), `max_suggestions`
- `get_optimization_suggestions_trace` — Trace-wide optimization. Params: `trace_path`, `top_n_functions`, `thread_filter` (GameThread|RenderThread|All), `max_suggestions`

**UTILITY** (8):
- `smart` / `smart_analyze` — Auto-route by supplied target or summarize the trace. Params: `trace_path`, optional `function_name` or `source`+`target`, `top_n`
- `get_stats` — Basic trace statistics. Params: `trace_path`
- `get_hot_functions` — Top hot functions. Params: `trace_path`, `top_n`
- `get_thread_breakdown` — Thread time breakdown with `trace_duration_seconds` sourced from timestamped frame span; without frame timestamps the duration is `null` with an explicit unavailable source. Params: `trace_path`, `top_n`
- `get_trace_info` — Trace metadata and channel availability. Params: `trace_path`
- `validate_trace` — Trace metadata plus capture-quality guidance. Params: `trace_path`, optional `intent`
- `get_cpu_metadata` — Decode CPU timer metadata instances. Params: `trace_path`, `max_timers`, `max_instances`, `include_decoded_fields`, `timeout_sec`

### Common Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `trace_path` | string | **Required for analysis ops**. Path to .utrace file (not required for capture_*) |
| `operation` | string | Operation name (defaults to "smart") |
| `function_name` | string | Target function for callgraph operations |
| `source` / `target` | string | Source/target functions for path operations |
| `top_n` | integer | Number of top results to return |
| `max_depth` | integer | Traversal depth for `visualize`, `generate_flamegraph`, and `trace_load_chain` |
| `baseline_name` | string | Baseline identifier for comparison operations |
| `baseline_trace` | string | Path to baseline .utrace for comparison |
| `project_root` | string | Project root (auto-detected) |

### Methodology disclosure (Issue #20586)

Aggregate and name-based operations return root-level `is_estimated` and
`methodology` fields. Read these before presenting a result as measured:

- `measured`: the formula uses captured aggregate values only. The limitations
  can still note overlap or capture-length effects.
- `estimated`: numeric values such as percentiles are synthesized from an
  aggregate model rather than observed per-call samples.
- `heuristic`: classification depends on timer names, relative-delta thresholds,
  or inferred relationships and is not causal/statistical proof.
- `hybrid`: measured channel/edge evidence is combined with a disclosed
  fallback or name heuristic.

`methodology.data_sources`, `formula`, `limitations`, and `parameters` are the
binding explanation. Loading operations also return `loadtime_evidence` when
the native LoadTime channel is present; its names/counts corroborate activity,
but its `bytes_processed` is a relative emission-volume proxy and supplies no
duration. `detect_sync_loads` uses average call duration because aggregate
`HotFunction` data contains no true per-call maximum. `infer_thread_sync`
returns `sample_index`, never a synthetic event timestamp.

`analyze_paths` reports an estimated path-cost ranking proxy. It takes the
maximum caller-to-callee inclusive edge aggregate (or a disclosed callee
exclusive fallback) because nested inclusive edge totals overlap and cannot be
summed into an invocation duration. `compare_traces` aggregates duplicate timer
names before comparing the common population; `top_n` limits each returned
change page and never changes the overall totals. `validate_regression` uses the
same duplicate-name aggregation before applying its relative-delta heuristic.
