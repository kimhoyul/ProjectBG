---
name: ue-execution-trace
model: opus
description: "Purpose: Trace Blueprint/GAS execution flows using ue_trace_execution. Answers 'What happens when X fires?', 'Trace the ability flow', 'Follow input to action'. Supports BP flow, ability chains, input routing, gameplay tag propagation, and widget events. Triggers: '실행 추적', '실행 플로우', '어빌리티 플로우', '입력 추적', 'trace execution', 'execution flow', 'ability flow', 'trace from input', 'what happens when'."
compatibility: "Requires NarshaMCP MCP server with an Unreal Engine 5.x project; three live AnimNotify operations require a running Editor"
argument-hint: "target Blueprint, node, ability, input action, tag, message channel, widget, or animation"
metadata:
  version: "1.2.1"
  author: "Next-Stage-Inc"
audience: end-user
---

# UE Execution Trace

Automatically execute `ue_trace_execution` calls and return a concrete, step-by-step flow report. Do not only describe the workflow.

## Workflow

1. Identify the target and choose the narrowest concrete operation.
2. Use `list_blueprints`, `list_tags`, or `get_stats` if discovery is needed.
3. Execute the trace with flat tool arguments; never invent a nested `params` object.
   - For a broad Blueprint request, omit `end_node` and use the returned
     `path_selection="representative_farthest"` ordered `details`.
   - For a caller-named destination, provide `end_node`; this is a specific endpoint
     query and must not be conflated with the representative broad trace.
   - For an InputAction request, use `trace_from_input` with `input_action`. Report
     only the observed `mapping_contexts`/`mapped_keys`, then the input tag and
     resolved ability endpoint. `input_chain_complete:false` is an incomplete trace,
     not permission to infer an IMC, key, or runtime action.
4. Report exact endpoint identity, resolution status, branch points, and failures.
   Label an auto-selected path as **representative**, never exhaustive: sibling
   branches may exist outside the selected chain.
5. Use live AnimNotify writes only on a disposable duplicate and require `verified:true` readback.

## 16-operation catalog

| Operation | Access | Editor | Required |
|---|---:|---:|---|
| `smart` | write | conditional | none |
| `trace_execution_flow` | read | never | start node or Blueprint scope |
| `trace_gameplay_tag` | read | never | tag |
| `trace_gameplay_messages` | read | never | none |
| `trace_from_input` | read | never | key or input action |
| `trace_ability_flow` | read | never | ability name |
| `find_event_handlers` | read | never | start node |
| `trace_widget_events` | read | never | Blueprint name/path |
| `get_animnotify_list` | read | never | none |
| `add_animnotify` | write | always | montage + notify |
| `remove_animnotify` | write | always | montage + notify |
| `list_animnotify_live` | read | always | montage/animation identifier |
| `list_blueprints` | read | never | none |
| `list_tags` | read | never | none |
| `get_stats` | read | never | none |
| `validate` | read | never | none |

## Calls

```python
ue_trace_execution(operation="list_blueprints", limit=50)
ue_trace_execution(operation="get_stats")
ue_trace_execution(operation="trace_execution_flow", blueprint_name="<BP_Name>", max_depth=10)
ue_trace_execution(operation="trace_execution_flow", blueprint_name="<BP_Name>", start_node="<node_id>", end_node="<node_id>", max_depth=10)
ue_trace_execution(operation="find_event_handlers", blueprint_name="<BP_Name>", start_node="<node_id>", start_owning_function="<graph>")
ue_trace_execution(operation="trace_from_input", input_action="<IA_Name>")
ue_trace_execution(operation="trace_ability_flow", ability_name="<GA_Name>")
ue_trace_execution(operation="trace_gameplay_tag", tag="<Tag.Name>")
ue_trace_execution(operation="trace_gameplay_messages", message_channel="<Channel.Tag>")
ue_trace_execution(operation="trace_widget_events", blueprint_name="<WBP_Name>")
ue_trace_execution(operation="validate", max_issues=200)
```

## Fail closed

- On `AMBIGUOUS_NODE_ID`, choose a returned candidate and retry with its owning-function scope. If candidates still share that graph, also provide `start_export_index` or `end_export_index`. Do not merge candidates.
- On `NODE_NOT_FOUND`, verify the Blueprint and exact node identity. A scoped miss must not fall back globally.
- On `AMBIGUOUS_ABILITY_BINDING` or `ABILITY_BINDING_UNRESOLVED`, report `ability_resolution` and preserved flow steps. Do not guess an ability.
- On `INVALID_PARAMETER`, correct the value before retrying: `limit 1..500`, `max_depth 1..500`, `max_issues 1..5000`, `start_export_index`/`end_export_index 0..2147483647`, `time >= 0`.
- Treat an AnimNotify mutation without `verified:true` as failure.

## Output format

Include the target, chosen operation, scoped endpoint identity, ordered execution chain, ability resolution, event roots or branch points, relevant data-pin values, and any exact error code.

For `trace_from_input`, render the observed mapping context and physical key before
the InputAction, input tag, and resolved gameplay-ability class. Use `input_chain`
for that full physical-input chain while preserving `chain` as the requested-start
trace. State
`input_mapping_resolution` and `input_chain_complete` explicitly. If mapping evidence
is empty or the ability endpoint is unresolved/ambiguous, name that exact gap and do
not replace it with a guessed Character action.

For `details`, render `step`, `node_title`, `node_class`, and the transition pin names as a numbered list. If `end_node_auto_selected:true`, prefix the list with “representative path” and include `max_reachable_depth`; if `false` or absent, describe the caller-selected endpoint path. Distinguish a successful `path_found:false` query from a resolution failure. When the representative result contains only the start node, say that no downstream execution node is reachable instead of fabricating a chain.

## Error recovery

| Result | Meaning | Recovery |
|---|---|---|
| `AMBIGUOUS_NODE_ID` | Bare identity matches multiple scoped nodes | Retry with the returned owning function, then export index if still ambiguous. |
| `NODE_NOT_FOUND` | The node or Blueprint is absent in the selected scope | Run `list_blueprints`, verify the exact asset, and use returned candidates; do not guess. |
| `path_found:false` with a resolved explicit endpoint | Both endpoints exist but no bounded route connects them | Report the no-path result and retry only if the user supplies a different endpoint or depth. |
| Representative path has one detail | The start resolved but has no reachable downstream exec edge | Report the isolated entry point; do not present `max_reachable_depth` as a step chain. |
