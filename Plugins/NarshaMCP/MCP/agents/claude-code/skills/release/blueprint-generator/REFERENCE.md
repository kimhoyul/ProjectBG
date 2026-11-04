# Blueprint Generator - Reference

> Technical reference for Blueprint graph generation via T3D and ECA LISP paths.

## Node Type Registry

| Category | Count | Examples |
|----------|-------|---------|
| Manual (hand-coded) | 38 | Event, Branch, Sequence, ForLoop, PrintString, Delay, Cast |
| Curated (static) | 63 | Timeline, MakeArray, CreateWidget, AddToViewport, SetText |
| Dynamic (engine) | ~1500 | Conv_IntToString, KismetMathLibrary::*, GameplayStatics::* |
| K2Node (auto-discovered) | 94 | ConstructObjectFromClass, LoadAsset, InputKey, GetDataTableRow |

## Connection Format

```
[source_index, source_pin, target_index, target_pin]
```

| Connection Type | Source Pin | Target Pin | Example |
|----------------|-----------|------------|---------|
| Exec flow | `"then"` | `"execute"` | `[0, "then", 1, "execute"]` |
| Data flow | `"ReturnValue"` | `"InString"` | `[1, "ReturnValue", 2, "InString"]` |
| Delay completion | `"Completed"` | `"execute"` | `[1, "Completed", 2, "execute"]` |
| Widget reference | `"ReturnValue"` | `"self"` | `[1, "ReturnValue", 2, "self"]` |

## Pin Naming Conventions

| Node Type | Exec Out | Data Out | Notes |
|-----------|----------|----------|-------|
| Event | `then` | — | Entry point, exec only |
| Branch | `True`, `False` | — | Conditional exec split |
| Sequence | `then_0`, `then_1`, ... | — | Sequential exec pins |
| ForLoop | `LoopBody`, `Completed` | `Index` | Loop iteration |
| Delay | `Completed` | — | Async completion |
| Pure (Math) | — | `ReturnValue` | No exec pins |
| CallFunction | `then` | `ReturnValue` | Standard function |

## T3D Format Reference

T3D (Text-to-3D) is UE's text-based format for copy/paste of Blueprint nodes.

### Pipeline

```text
Graph JSON → ue_manage_blueprint(build_graph) → T3D text → ue_manage_blueprint(import_t3d) → Editor
```

### Key Fields in Graph JSON

| Field | Required | Type | Description |
|-------|----------|------|-------------|
| `type` | Yes | string | Node type name (must match registry) |
| `event` | For Event nodes | string | Event name (BeginPlay, Tick, etc.) |
| `params` | Optional | object | Node-specific parameters |
| `pos` | Optional | [x, y] | Editor position |
| `widget_class` | For CreateWidget | string | UMG widget Blueprint name |
| `target` | For CallFunction | string | Target object (Self, etc.) |
| `function` | For CallFunction | string | Function name |
| `enum` | For SwitchOnEnum | string | Enum path (/Script/...) |
| `struct` | For Break/MakeStruct | string | Struct name (Vector, etc.) |

## ECA LISP Syntax Reference (UE 5.8+)

ECA's LISP notation is a compact representation of Blueprint graphs.

### Format

```lisp
(EventName (Node1 params...) (Node2 params...))
```

### Examples

```lisp
;; Simple: BeginPlay → PrintString
(BeginPlay (PrintString "Hello World"))

;; Sequence: BeginPlay → Sequence → PrintString x2
(BeginPlay (Sequence (PrintString "A") (PrintString "B")))

;; Branch: BeginPlay → Branch → True/False
(BeginPlay (Branch condition (PrintString "True") (PrintString "False")))

;; Delay: BeginPlay → Delay(3s) → PrintString
(BeginPlay (Delay 3.0 (PrintString "After delay")))
```

### LISP vs T3D Comparison

| Aspect | ECA LISP (Path A) | T3D (Path B) |
|--------|-------------------|--------------|
| Format | S-expression | UE text format |
| Size | ~50 bytes/node | ~4000 bytes/node |
| Requires Editor | Yes (ECA bridge) | Import only |
| Offline generation | Yes (AI generates) | Yes (build_graph) |
| Engine version | 5.8+ only | All versions |

## Self-Test

`/blueprint-generator --self-test` validates:

1. **MCP connectivity**: `ue_check_health()` responds with status
2. **Reference retrieval**: `ue_manage_blueprint(build_graph_guide)` returns node catalog
3. **T3D generation**: `ue_manage_blueprint(build_graph)` produces valid T3D from sample JSON:
   ```json
   {"nodes": [{"type": "Event", "event": "BeginPlay", "pos": [0,0]}, {"type": "PrintString", "params": {"InString": "test"}, "pos": [300,0]}], "connections": [[0,"then",1,"execute"]]}
   ```
4. **Generation path**: always the NarshaMCP T3D pipeline (`build_graph` → `import_t3d` → compile). (ECA channel removed in #11230/#11231; the `eca_bridge_available` health flag was dropped in #15171.)

**Expected**: All steps always pass via the T3D pipeline.

## MCP Tools Used

| Tool | Operation | When |
|------|-----------|------|
| `ue_check_health` | — | Phase 0: Editor reachability |
| `ue_manage_blueprint` | `build_graph_guide` | Phase 1: Reference |
| `ue_manage_blueprint` | `build_graph` | Phase 3: T3D generation |
| `ue_manage_blueprint` | `import_t3d` | Phase 4: Import |
| `ue_editor_assets` | `compile_blueprints` | Phase 5: Compile |
| `ue_manage_blueprint` | `get_structure` | Phase 5: Verify |

> Note: the Phase 3A `ue_eca_proxy(call_tool, "lisp_to_blueprint", ...)` row was removed alongside the ECA stack in Epic #11230 / PR #11295. All BP creation now flows through the T3D pipeline above.

## Quick Examples

### Example 1: Hello World

**User**: "BeginPlay에서 Hello World 출력하는 BP 만들어"

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "PrintString", "params": {"InString": "Hello World!"}, "pos": [300, 0]}
  ],
  "connections": [[0, "then", 1, "execute"]]
}
```

### Example 2: UI Widget Setup

**User**: "Create a BP that shows a HUD widget on BeginPlay"

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "CreateWidget", "widget_class": "WBP_HUD", "pos": [300, 0]},
    {"type": "AddToViewport", "params": {"ZOrder": "1"}, "pos": [600, 0]}
  ],
  "connections": [
    [0, "then", 1, "execute"],
    [1, "then", 2, "execute"],
    [1, "ReturnValue", 2, "self"]
  ]
}
```

### Example 3: Timed Action with Delay

**User**: "BeginPlay 3초 후에 폭발 이벤트 호출하는 BP"

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "Delay", "params": {"Duration": "3.0"}, "pos": [300, 0]},
    {"type": "CallFunction", "target": "Self", "function": "OnExplosion", "pos": [600, 0]}
  ],
  "connections": [
    [0, "then", 1, "execute"],
    [1, "Completed", 2, "execute"]
  ]
}
```

### Example 4: Math Pipeline

**User**: "Generate random float 0-100, clamp to 10-90, print result"

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "RandomFloatInRange", "params": {"Min": "0.0", "Max": "100.0"}, "pos": [0, 200]},
    {"type": "Clamp", "params": {"Min": "10.0", "Max": "90.0"}, "pos": [300, 200]},
    {"type": "PrintString", "pos": [600, 0]}
  ],
  "connections": [
    [0, "then", 3, "execute"],
    [1, "ReturnValue", 2, "Value"],
    [2, "ReturnValue", 3, "InString"]
  ]
}
```

### Example 5: Custom Function Graph

**User**: "Create a function that doubles a float value"

```json
{
  "nodes": [
    {"type": "FunctionEntry", "function_name": "DoubleValue", "params": [{"name": "Input", "type": "float"}], "pos": [0, 0]},
    {"type": "Multiply_FloatFloat", "params": {"B": "2.0"}, "pos": [300, 0]},
    {"type": "FunctionResult", "returns": [{"name": "Result", "type": "float"}], "pos": [600, 0]}
  ],
  "connections": [
    [0, "then", 2, "execute"],
    [0, "Input", 1, "A"],
    [1, "ReturnValue", 2, "Result"]
  ]
}
```

### Example 6: Dynamic Engine Node (auto-discovered)

**User**: "Convert integer to string and print"

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "Conv_IntToString", "pos": [300, 200]},
    {"type": "PrintString", "pos": [600, 0]}
  ],
  "connections": [
    [0, "then", 2, "execute"],
    [1, "ReturnValue", 2, "InString"]
  ]
}
```

> Any of the ~1500 engine library functions can be used by name as a node type.

## Common Failures

| Error | Cause | Fix |
|-------|-------|-----|
| "Unknown node type" | Misspelled type name | Check supported node list, use exact names |
| "requires 'enum' field" | SwitchOnEnum without enum path | Add `"enum": "/Script/..."` field |
| "requires 'struct' field" | BreakStruct/MakeStruct without struct | Add `"struct": "Vector"` field |
| Import fails silently | Editor not running or RC disabled | Start Editor, enable Remote Control |
| Compile errors after import | Missing connections or wrong pin names | Verify connections match pin names from reference |
| Pure nodes have exec pins | Using exec connection on pure node | Pure functions (math) have no execute/then pins |

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| "Unknown node type" | Misspelled type name in graph JSON | Check supported node list from `build_graph_guide`; use exact type names |
| Import fails silently | Editor not running or Remote Control disabled | Output T3D text for manual paste via `Edit > Paste Nodes` in Editor |
| Compile errors after import | Missing connections or wrong pin names | Run `build_graph_guide` to get pin name reference; verify connections match |
| Pure node has exec pins | Exec connection used on pure function (math nodes) | Pure functions have no execute/then pins; use data-only connections |

## Related Files

- **T3D Engine**: `MCP/native/mcp_server/src/tools/t3d_templates.rs`
- **Node Constructors**: `t3d_nodes.rs`, `t3d_nodes_advanced.rs`, `t3d_nodes_ui.rs`, `t3d_nodes_math.rs`
- **K2Node Registry**: `t3d_k2node_registry.rs`
- **Reference Data**: `t3d_reference.rs`
- **Integration Tests**: `tests/t3d_blueprint_patterns.rs`
- **Parent Issue**: [#4751](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/4751)

## MCP Tool Examples

```python
# Create Blueprint based on C++ class
ue_manage_blueprint(operation="smart", blueprint_name="BP_NewCharacter", parent_class="ACharacter")
```
