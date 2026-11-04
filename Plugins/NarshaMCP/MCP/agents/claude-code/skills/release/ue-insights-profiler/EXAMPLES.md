# UE Insights Profiler Examples

Copy-ready workflows for the current `ue_analyze_insights` handler contract. Replace placeholder
trace paths and function names before execution.

---

## Example 1: Compare a Trace with an Explicit Baseline

**User**: "Compare this capture with last week's baseline and show regressions over 5%."

```python
result = ue_analyze_insights(
    operation="compare_traces",
    trace_path="C:/Traces/current.utrace",
    baseline_trace="C:/Traces/baseline.utrace",
    threshold_percent=5.0,
    top_n=200,
)
```

Read `faster[]`, `slower[]`, `unchanged`, `new_functions[]`, `removed_functions[]`,
`overall_change`, and `metadata` from the response. `compare_traces` requires
`baseline_trace`; it does not create or select a baseline automatically.

---

## Example 2: Inspect Direct Callers of a Hot Function

**User**: "What directly calls `UInventoryComponent::RefreshInventory` in this trace?"

```python
result = ue_analyze_insights(
    operation="find_callers",
    trace_path="C:/Traces/inventory-hitch.utrace",
    function_name="UInventoryComponent::RefreshInventory",
)
```

Read `callers[]` and `total` from the response. `find_callers` is a one-hop query. For a
bounded multi-level graph, call `visualize` with `function_name` and `max_depth`.

---

## Example 3: Generate Focused Optimization Suggestions

**User**: "Suggest fixes for this expensive Tick function."

```python
result = ue_analyze_insights(
    operation="get_optimization_suggestions",
    trace_path="C:/Traces/game-thread.utrace",
    function_name="AEnemyManager::Tick",
    pattern="tick",
    max_suggestions=3,
)
```

Read `operation`, `mode`, `function_name`, `pattern_detected`, `suggestions[]`, and `metadata`.
For trace-wide suggestions use `get_optimization_suggestions_trace`; for a GPU-bound capture use
`diagnose_gpu_bottleneck` rather than nesting `params` or `focus` under `smart_analyze`.
