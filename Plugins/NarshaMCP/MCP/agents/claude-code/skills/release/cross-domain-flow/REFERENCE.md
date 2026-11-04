# Cross-Domain Execution Flow Reference

**Version**: 1.1.0
**Parent**: [SKILL.md](SKILL.md)

---

## Tool Reference

### Stage 1: ue_manage_gameplay (trace_input_flow)

Traces InputAction → InputTag flow.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"trace_input_flow"` |
| `params.input_action` | str | Yes | InputAction name (e.g., `"IA_Attack"`) |

**Returns**:

```python
{
    "success": bool,
    "input_action": str,
    "input_tag": str | None,      # Mapped InputTag
    "bound_abilities": list,       # Abilities triggered by this input
    "error": str                   # Only if success=False
}
```

**Error Recovery**:
- If InputAction not found → Fallback to `ue_search_assets(asset_type="InputAction")`

---

### Stage 1 Fallback: ue_search_assets (InputAction)

Lists available InputActions when primary fails.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_type` | str | Yes | `"InputAction"` |
| `query` | str | No | Search pattern (default: `"*"`) |

**Returns**:

```python
{
    "assets": [
        {"name": str, "path": str}
    ],
    "total_found": int
}
```

---

### Stage 2: ue_manage_gameplay (search_tags / trace_abilities)

Searches GameplayTags or traces Ability activation.

**Operation: search_tags**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"search_tags"` |
| `params.query` | str | Yes | Tag pattern (e.g., `"Ability.*"`) |
| `params.limit` | int | No | Max results (default: 10) |

**Returns**:

```python
{
    "tags": [
        {"tag": str, "source": str}
    ],
    "count": int
}
```

**Operation: trace_abilities**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"trace_abilities"` |
| `params.tag` | str | Yes | Ability tag to trace |

**Returns**:

```python
{
    "abilities": [
        {"name": str, "path": str, "tags": list}
    ],
    "total_found": int
}
```

---

### Stage 2 Fallback: ue_search_assets (GameplayAbility)

Lists available Abilities when tag search fails.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_type` | str | Yes | `"GameplayAbility"` |
| `query` | str | No | Search pattern (e.g., `"*Attack*"`) |

**Returns**:

```python
{
    "assets": [
        {"name": str, "path": str}
    ],
    "total_found": int
}
```

---

### Stage 3: ue_trace_execution (trace_ability_flow)

Traces Ability internal execution flow.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"trace_ability_flow"` |
| `params.ability_name` | str | Yes | Ability class name |
| `params.include_animation` | bool | No | Include montage references |
| `params.include_cpp` | bool | No | Include C++ call targets |
| `params.include_tags` | bool | No | Include GameplayTag usage |

**Returns**:

```python
{
    "flow": {
        "ability": {
            "name": str,
            "parent_class": str,
            "montages": list,
            "gameplay_tags": list
        }
    },
    "success": bool
}
```

---

### Stage 4: ue_analyze_symbols (get_methods / find_callers)

Resolves C++ implementations.

**Operation: get_methods**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"get_methods"` |
| `params.class_name` | str | Yes | C++ class name |
| `params.limit` | int | No | Max methods (default: 10) |

**Returns**:

```python
{
    "class_name": str,
    "methods": [
        {"name": str, "full_name": str}
    ],
    "methods_count": int
}
```

**Operation: find_callers**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"find_callers"` |
| `params.function_name` | str | Yes | Function to find callers for |

**Returns**:

```python
{
    "callers": [
        {"name": str, "file": str}
    ]
}
```

---

### Stage 5: ue_search_assets (Animation)

Searches for Animation assets (Montage, Sequence).

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_type` | str | Yes | `"Animation"` |
| `query` | str | Yes | Search pattern (e.g., `"AM_BackDoor*"`) |

**Returns**:

```python
{
    "assets": [
        {"name": str, "path": str, "type": str}
    ],
    "total_found": int
}
```

---

## Type Definitions

### CrossDomainFlowResult

```python
class CrossDomainFlowResult(TypedDict):
    input_layer: InputLayerInfo
    gas_layer: GASLayerInfo
    blueprint_layer: BlueprintLayerInfo
    cpp_layer: CppLayerInfo
    animation_layer: AnimationLayerInfo
```

### InputLayerInfo

```python
class InputLayerInfo(TypedDict):
    input_action: str
    input_tag: Optional[str]
    available_actions: List[str]  # If primary not found
```

### GASLayerInfo

```python
class GASLayerInfo(TypedDict):
    ability_name: str
    ability_tags: List[str]
    effects: List[str]
```

### BlueprintLayerInfo

```python
class BlueprintLayerInfo(TypedDict):
    ability_class: str
    parent_class: str
    montages: List[str]
    nodes: List[str]
```

### CppLayerInfo

```python
class CppLayerInfo(TypedDict):
    class_name: str
    methods: List[MethodInfo]
    source_files: List[str]
```

### AnimationLayerInfo

```python
class AnimationLayerInfo(TypedDict):
    montages: List[MontageInfo]
    notifies: List[str]
```

---

## Error Code Reference

| Stage | Error | Cause | Recovery |
|-------|-------|-------|----------|
| 1 | `InputAction not found` | IA_* doesn't exist | Use `ue_search_assets(InputAction)` |
| 2 | `No abilities for tag` | Tag exists but no Ability | Use `ue_search_assets(GameplayAbility)` |
| 3 | `Ability not found` | Blueprint doesn't exist | Search with broader pattern |
| 4 | `Symbol not found` | C++ class not in PDB | Check Engine PDB indexing |
| 5 | `Animation not found` | Montage path incorrect | Search with `*` pattern |

---

## Workflow Stages Summary

| Stage | Tool | Operation | Offline | Required |
|-------|------|-----------|---------|----------|
| 1 | `ue_manage_gameplay` | trace_input_flow | ✅ | ✅ |
| 1F | `ue_search_assets` | InputAction | ✅ | Fallback |
| 2 | `ue_manage_gameplay` | search_tags | ✅ | ✅ |
| 2F | `ue_search_assets` | GameplayAbility | ✅ | Fallback |
| 3 | `ue_trace_execution` | trace_ability_flow | ✅ | ✅ |
| 4 | `ue_analyze_symbols` | get_methods | ✅ | ✅ |
| 5 | `ue_search_assets` | Animation | ✅ | ✅ |

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios with MCP results |

---

**Tracking**: Issue #4442 - Skills & SubAgents Improvement
**Last Updated**: 2026-02-06
