---
name: blueprint-generator
model: opus
description: "Purpose: create new Blueprint graphs from natural language via the T3D import pipeline (build_graph_guide → build_graph → import_t3d → compile). Not for analyzing existing BPs (use blueprint-flow). Triggers: 'BP 생성', 'Blueprint 만들어', 'create Blueprint', 'generate Blueprint', 'BP 그래프 생성'."
argument-hint: "natural language description of Blueprint graph (e.g., 'BeginPlay에서 PrintString', 'Branch with Delay') or --self-test"
compatibility: "Requires NarshaMCP MCP server (v0.7.0+). Editor required for T3D import; offline T3D text generation available without Editor."
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "Proprietary"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Blueprint Generator

**Priority**: High
**Goal**: Generate Blueprint graphs from natural language via the T3D import pipeline (build_graph_guide → build_graph → import_t3d → enhance) — ECA LISP path removed in Epic #11230 / PR #11295

---

## Purpose

Converts natural language descriptions into working Blueprint graphs:

1. **Reference Retrieval** - Get format spec and node catalog
2. **Graph JSON Generation** - AI builds graph JSON from user request
3. **T3D Generation** - Convert JSON to T3D text (offline, no Editor needed)
4. **T3D Import** - Import into Editor via Remote Control
5. **Compile & Verify** - Compile and check result

**Key Advantage**:
- **Manual**: Open Editor, drag nodes, connect pins, set properties (~5-15 min)
- **Blueprint Generator**: Describe intent, auto-generate graph (~30 sec)

---

### Should NOT Activate — Disambiguation

- "BP_Player 노드 흐름 분석해줘" → ❌ Use blueprint-flow (analyzing existing BP, not creating)
- "복잡한 BP 로직을 C++로 직접 구현" → ❌ Use bp-to-cpp or code-generation (C++ code, not BP graph)
- "Blueprint 구조 설명해줘" → ❌ Use ue-explain (concept explanation, not generation)
- "BP 변환 검증해줘" → ❌ Use bp-to-cpp-validator (migration validation, not creation)

---

## Trigger Phrases

### Korean

- "BP 생성", "블루프린트 만들어"
- "BP 그래프 생성", "블루프린트 그래프 만들어줘"
- "이런 로직 블루프린트로 만들어", "블루프린트 노드 추가해줘"
- "BeginPlay에서 PrintString 하는 BP 만들어"

### English

- "create Blueprint", "generate Blueprint"
- "make a Blueprint graph", "build BP graph"
- "add Blueprint nodes for...", "generate Blueprint logic for..."
- "create a BP that prints hello world on BeginPlay"

---

## Workflow Overview

### 5-Phase Generation Process (T3D import pipeline)

```text
Phase 1: Get Reference
| Tool: ue_manage_blueprint(operation="build_graph_guide")
| Output: Node catalog, format spec, connection rules
v
Phase 2: Generate Graph JSON
| AI generates graph JSON from user request + reference
| Validates against node catalog
v
Phase 3: Build T3D (offline)
| Tool: ue_manage_blueprint(operation="build_graph",
|       params={graph_json, blueprint_path})
| Output: T3D text
v
Phase 4: Import to Editor
| Tool: ue_manage_blueprint(operation="import_t3d",
|       params={blueprint_path, t3d_text})
| Output: Import confirmation
v
Phase 5: Compile & Verify
| Tool: ue_editor_assets(operation="compile_blueprints")
| Tool: ue_manage_blueprint(operation="get_structure",
|       params={blueprint_name})
| Output: Verified graph structure
```

### Auto-Layout post-hook (Issue #17059 P4)

Node placement is **automatic** — `build_graph` runs `auto_layout` on the generated graph by
default (readable left-to-right for K2/AnimGraph; 2D hub-centred for AnimStateMachine graphs —
`LayoutStrategy` is auto-detected from node classes). **Do not hand-place nodes.** If you edit
the graph afterwards via `add_node` / `connect_nodes`, finish with a single
`ue_manage_blueprint(operation="auto_layout")` call so the whole graph is re-tidied (per-`add_node`
re-layout is intentionally off to avoid reflow churn — Issue #17059 §P4).

---

## Tool Integration

### Primary Tools Used

| Tool | Operation | Purpose | Editor? |
|------|-----------|---------|---------|
| `ue_manage_blueprint` | `build_graph_guide` | Get node catalog + format spec | No |
| `ue_manage_blueprint` | `build_graph` | Convert JSON → T3D text | No |
| `ue_manage_blueprint` | `import_t3d` | Import T3D into Blueprint | Yes |
| `ue_editor_*` | `compile_blueprints` | Compile after import | Yes |
| `ue_manage_blueprint` | `get_structure` | Verify result | No |

### Offline vs Editor Operations

- **Phases 1-3**: Fully offline (no Editor needed). T3D generation works anywhere.
- **Phases 4-5**: Require Editor with Remote Control enabled.
- If Editor is not available, output T3D text for manual paste via `Edit > Paste Nodes`.

---

## Graph JSON Format

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "PrintString", "params": {"InString": "Hello!"}, "pos": [300, 0]}
  ],
  "connections": [
    [0, "then", 1, "execute"]
  ]
}
```

### Connection Format

`[source_index, source_pin, target_index, target_pin]`

- Exec flow: `"then"` -> `"execute"`
- Data flow: `"ReturnValue"` -> `"InString"`

### Supported Node Types (38 manual + 63 curated + ~1500 dynamic + 94 K2Node)

**Events**: Event, CustomEvent
**Flow Control**: Branch, Sequence, DoOnce, Gate, FlipFlop, MultiGate
**Loops**: ForLoop, WhileLoop, ForEachLoop, ForEachLoopWithBreak, DoN
**Functions**: CallFunction, PrintString, SetActorLocation, Delay, SpawnActorFromClass
**Variables**: VariableGet, VariableSet
**Switch**: SwitchOnInt, SwitchOnString, SwitchOnEnum
**Timer**: SetTimer
**Struct**: BreakStruct, MakeStruct
**Timeline**: Timeline
**Array**: MakeArray, GetArrayItem, ArrayLength
**Math (Pure)**: Lerp, Add, Multiply, VectorLength, Normalize, RandomFloatInRange, Clamp, MapRangeUnclamped, MakeVector
**UI Widgets**: CreateWidget, AddToViewport, RemoveFromParent, SetText, SetVisibility, PlayAnimation
**Cast**: Cast
**Custom Functions**: FunctionEntry, FunctionResult, InterfaceCall, MacroInstance
**Input**: InputAction, EnhancedInputAction
**Other**: AddComponent, Self, GetClassDefaults
**Dynamic Engine**: ~1500 auto-discovered functions (KismetMathLibrary, GameplayStatics, etc.)
**K2Node Auto-Discovered**: ~94 K2Node classes from engine .cpp (ConstructObjectFromClass, LoadAsset, InputKey, EnumEquality, GetDataTableRow, ForEachElementInEnum, etc.)

---

## Quick Examples

6 worked walkthroughs (Hello World, UI Widget, Delay, Math pipeline, Custom Function, Dynamic Engine node) with full graph JSON → [REFERENCE.md § Quick Examples](REFERENCE.md#quick-examples). Any of the ~1500 engine library functions is usable by name as a node type.

---

## Common Failures & Error Recovery

Error tables (Unknown node type, missing enum/struct field, silent import, compile errors, pure-node exec pins) → [REFERENCE.md § Common Failures](REFERENCE.md#common-failures) and [REFERENCE.md § Error Recovery](REFERENCE.md#error-recovery). Related source files (t3d_templates.rs, t3d_nodes*.rs, K2Node registry, parent issue #4751) → [REFERENCE.md § Related Files](REFERENCE.md#related-files).

---

## Output Format

```
Blueprint Generation Report
───────────────────────────
Blueprint: <path>
Nodes: <count>
Connections: <count>

[Generated Graph]
  Nodes:
    0: <type> (<event/function>)
    1: <type> (params: <key=value>)
    ...
  Connections:
    [0] then → [1] execute
    ...

[Import Result]
  T3D Size: <bytes>
  Import: SUCCESS | FAILED (Editor required)
  Compile: PASS | ERRORS (<count>)

Next Steps:
  - Open Blueprint in Editor to verify
  - If Editor offline: paste T3D via Edit > Paste Nodes
```

**Status**: Phase 30 — Full Implementation (38 manual + 63 static + ~1500 dynamic + 94 K2Node nodes)
**Last Updated**: 2026-03-27

## MCP Tool Examples

```python
# Create Blueprint based on C++ class
ue_manage_blueprint(operation="smart", blueprint_name="BP_NewCharacter", parent_class="ACharacter")
```

More walkthrough JSON → [REFERENCE.md § Quick Examples](REFERENCE.md#quick-examples).
