# Blueprint Generator - Examples

> **1.1.0** — ECA LISP path added (Issue #7409)

## Example 1: Hello World (Path B — T3D)

**User**: "BeginPlay에서 Hello World 출력하는 BP 만들어"

### Phase 0: Path Selection

```text
ue_check_health() → engine_version: 5.7
→ generation always uses Path B (T3D) — NarshaMCP pipeline
  (ECA channel removed in #11230/#11231)
```

### Phase 1: Get Reference

```python
ue_manage_blueprint(operation="build_graph_guide")
# → Node catalog: 38 manual + 63 curated + ~1500 dynamic + 94 K2Node
# → Connection format: [source_index, source_pin, target_index, target_pin]
```

### Phase 2B: Generate Graph JSON

```json
{
  "nodes": [
    {"type": "Event", "event": "BeginPlay", "pos": [0, 0]},
    {"type": "PrintString", "params": {"InString": "Hello World!"}, "pos": [300, 0]}
  ],
  "connections": [[0, "then", 1, "execute"]]
}
```

### Phase 3B: Build T3D

```python
ue_manage_blueprint(operation="build_graph", params={
    "graph_json": "<above JSON>",
    "blueprint_path": "/Game/Blueprints/BP_HelloWorld"
})
# → T3D text (8151 bytes, 2 nodes)
```

### Phase 4B: Import to Editor

```python
ue_manage_blueprint(operation="import_t3d", params={
    "blueprint_path": "/Game/Blueprints/BP_HelloWorld",
    "t3d_text": "<T3D output from Phase 3B>"
})
# → Import: SUCCESS
```

### Phase 5: Compile & Verify

```python
ue_editor_assets(operation="compile_blueprints")
# → Compile: PASS (0 errors)

ue_manage_blueprint(operation="get_structure", params={
    "blueprint_name": "BP_HelloWorld"
})
# → Nodes: BeginPlay → PrintString("Hello World!")
```

### Output

```text
Blueprint Generation Report
───────────────────────────
Blueprint: /Game/Blueprints/BP_HelloWorld
Generation Path: T3D Import (Path B)
Nodes: 2
Connections: 1

[Import Result]
  T3D Size: 8151 bytes
  Import: SUCCESS
  Compile: PASS (0 errors)
```

---

## Example 2: UI Widget Setup (Path B — T3D)

**User**: "Create a BP that shows a HUD widget on BeginPlay"

### Phase 2B: Graph JSON

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

### Key Points

- `CreateWidget` needs `widget_class` field for the UMG widget Blueprint
- Data flow: `ReturnValue` → `self` connects widget reference to AddToViewport
- Exec flow: 3-node chain BeginPlay → CreateWidget → AddToViewport

### Output

```text
Blueprint Generation Report
───────────────────────────
Blueprint: /Game/Blueprints/BP_HUDSpawner
Generation Path: T3D Import (Path B)
Nodes: 3
Connections: 3
```

---

## Example 3: ECA LISP path — REMOVED post-Epic #11230

The previous "Path A — ECA LISP (UE 5.8+)" example has been removed because the
underlying integration (`ue_eca_proxy(call_tool, "lisp_to_blueprint", ...)`) was
deleted alongside the ECABridge stack in Epic #11230 / PR #11295. The ECABridge
plugin was confirmed not present in Epic's public UE 5.8 mirror, so the LISP
write-back channel is no longer reachable from any engine version.

**Use the T3D import pipeline instead** (the same flow as Example 1 and Example 2).
The skill's `SKILL.md` workflow now routes all generation through:

```text
build_graph_guide → build_graph (with `--apply=false` for preview) → import_t3d
```

For the historical LISP-based design rationale, see [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md).
