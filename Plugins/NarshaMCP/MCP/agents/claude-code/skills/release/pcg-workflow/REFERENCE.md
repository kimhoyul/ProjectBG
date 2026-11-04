# PCG Workflow - Reference

**Version**: 1.0.0 (Issue #4442)

---

## Tool: ue_manage_pcg

**Total Operations**: 32
**Smart Mode**: Yes

---

## Operations Overview

| Category | Operation | Description | Editor RC |
|----------|-----------|-------------|-----------|
| **Analysis** | smart | Auto-detect operation | No |
| | get_structure | Get graph layout (nodes, edges) | No |
| | search_graphs | Find PCG graph assets | No |
| | trace_flow | Data flow between nodes | No |
| | estimate_performance | Execution cost estimate | No |
| | find_assets | Find assets referenced by PCG | No |
| | find_nodes | Find nodes by type | No |
| | validate | Check graph validity | No |
| | get_hierarchy | Subgraph hierarchy | No |
| | find_pcg_by_asset | Find PCGs using specific asset | No |
| | find_in_level | Find PCG actors placed in levels | No |
| | get_landscape_pcg | Get PCGs associated with landscape | No |
| | find_pcg_usage | Find where a PCG is instantiated | No |
| **Modification** | set_parameter | Modify node parameter | No |
| | replace_asset | Replace mesh/asset reference | No |
| | add_node | Add new node | No |
| | remove_node | Remove existing node | No |
| | connect_nodes | Connect two nodes | No |
| | disconnect_nodes | Disconnect nodes | No |
| | create_graph | Create new PCG graph | No |
| | batch_add_nodes | Add multiple nodes at once | No |
| **Execution** | execute_graph | Run PCG generation | Yes |
| | regenerate | Re-run existing PCG | Yes |
| | get_spawn_result | Get PCG generation results | Yes |
| **Distribution** | set_density | Set spawn density | No |
| | set_bounds | Set volume bounds | No |
| | set_asset_weights | Set asset weight mappings | No |
| **Templates** | list_templates | List available templates | No |
| | apply_template | Apply template to graph | No |
| | save_as_template | Save graph as template | No |
| **Meta** | auto | Alias for smart | No |
| | list_operations | List all operations | No |

---

## Analysis Operations (13)

### smart

**Purpose**: Auto-detect operation from parameters

**Parameters**: Any combination of below parameters

---

### search_graphs

**Purpose**: Find PCG graph assets by name pattern

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| query | string | Yes | Search pattern (e.g., "*Forest*", "PCG_*") |
| limit | int | No | Max results (default: 20) |

**Returns**:
```json
{
  "results": [
    {"name": "PCG_Forest_Dense", "path": "/Game/PCG/PCG_Forest_Dense"}
  ],
  "count": 5
}
```

---

### get_structure

**Purpose**: Get complete graph structure (nodes, edges, layout)

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes* | Graph short name |
| graph_path | string | Yes* | Full asset path |

*One of graph_name or graph_path required

**Returns**:
```json
{
  "graph_name": "PCG_Forest",
  "node_count": 134,
  "edge_count": 76,
  "nodes": [
    {
      "id": "Node_0",
      "type": "PCGPointGenerator",
      "name": "LandscapeData",
      "position": {"x": 0, "y": 0}
    }
  ],
  "edges": [
    {
      "source": "Node_0",
      "target": "Node_1",
      "source_pin": "Out",
      "target_pin": "In"
    }
  ]
}
```

---

### trace_flow

**Purpose**: Trace data flow through graph

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| start_node | string | No | Starting node (default: first generator) |
| include_pins | bool | No | Include pin details (default: false) |

---

### estimate_performance

**Purpose**: Estimate PCG execution cost

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |

**Returns**:
```json
{
  "graph_name": "PCG_Forest",
  "estimated_time_ms": 2800,
  "node_count": 156,
  "bottleneck_nodes": ["MeshSampler_0", "DensityFilter_2"],
  "complexity_score": 7.5
}
```

---

### find_assets

**Purpose**: Find assets referenced by a PCG graph

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| asset_type | string | No | Filter by type (e.g., "StaticMesh") |

---

### find_nodes

**Purpose**: Find nodes by type within a graph

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| node_type | string | Yes | Node type (e.g., "Sampler", "Filter") |
| limit | int | No | Max results (default: 20) |

---

### validate

**Purpose**: Check graph validity (cycles, missing refs, errors)

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| check_cycles | bool | No | Check for circular dependencies (default: true) |
| check_assets | bool | No | Check asset references (default: false) |
| include_params | bool | No | Include parameter validation (default: false) |
| strict_mode | bool | No | Enable strict validation (default: false) |

**Returns**:
```json
{
  "is_valid": true,
  "warnings": ["Node_5 has unused output"],
  "errors": []
}
```

---

### get_hierarchy

**Purpose**: Get subgraph hierarchy tree

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| max_depth | int | No | Max depth (default: 10) |

---

### find_pcg_by_asset

**Purpose**: Find all PCG graphs referencing a specific asset

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| asset_path | string | Yes | Asset path (e.g., "/Game/Meshes/SM_Tree") |
| exact_match | bool | No | Exact match (default: true) |

---

### find_in_level

**Purpose**: Find PCG actors placed in levels

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | No | Filter by graph name |

---

### get_landscape_pcg

**Purpose**: Get PCG graphs associated with landscape

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | No | Filter by graph name |

---

### find_pcg_usage

**Purpose**: Find where a PCG graph is instantiated in levels

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |

---

## Modification Operations (8)

### set_parameter

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| node_id | string | Yes | Target node |
| parameter_name | string | Yes | Parameter name |
| parameter_value | any | Yes | New value |

---

### replace_asset

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| old_asset | string | Yes | Current asset path |
| new_asset | string | Yes | Replacement asset path |

---

### add_node

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| node_type | string | Yes | Node type to add |
| position | object | No | Node position {x, y} |

---

### remove_node

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| node_id | string | Yes | Node to remove |

---

### connect_nodes

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| source_node | string | Yes | Source node ID |
| target_node | string | Yes | Target node ID |
| source_pin | string | No | Source pin (default: "Out") |
| target_pin | string | No | Target pin (default: "In") |

---

### disconnect_nodes

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| source_node | string | Yes | Source node ID |
| target_node | string | Yes | Target node ID |
| port | string | No | Specific port name |

---

### create_graph

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | New graph name |
| output_path | string | Yes | Output directory |
| template | string | No | Base template name |

---

### batch_add_nodes

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| nodes | array | Yes | Array of node configs |
| connections | array | No | Node connection definitions |
| validate_before_apply | bool | No | Validate before applying batch |
| stop_on_first_error | bool | No | Stop on first error |

---

## Execution Operations (3) - Editor RC Required

### execute_graph

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| actor_path | string | Yes | PCG actor path in level |

---

### regenerate

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| actor_path | string | Yes | PCG actor path in level |

---

### get_spawn_result

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| actor_path | string | Yes | PCG actor path in level |

---

## Distribution Operations (3)

### set_density

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| density | number | Yes | Density value (0.0-1.0) |

---

### set_bounds

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| bounds | object | No | Structured bounds {extent, location} |
| min_x/max_x | number | No | Flat min/max X bounds |
| min_y/max_y | number | No | Flat min/max Y bounds |
| min_z/max_z | number | No | Flat min/max Z bounds |

---

### set_asset_weights

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Graph name |
| asset_weights | object | Yes | Asset weight mappings |

---

## Template Operations (3)

### list_templates

**Parameters**: None

---

### apply_template

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Target graph |
| template_name | string | Yes | Template to apply |

---

### save_as_template

**Parameters**:
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| graph_name | string | Yes | Source graph |
| template_name | string | Yes | Template name |
| output_path | string | No | Save location |

---

## Error Codes

| Code | Message | Solution |
|------|---------|----------|
| `GRAPH_NOT_FOUND` | PCG graph not found | Check name with search_graphs |
| `NODE_NOT_FOUND` | Node ID not found | Use get_structure to list nodes |
| `CYCLE_DETECTED` | Graph has circular dependency | Fix node connections |
| `EDITOR_RC_REQUIRED` | Editor not connected | Start Editor with RC |
| `ASSET_NOT_FOUND` | Referenced asset missing | Verify asset path |
| `INVALID_PARAMETER` | Parameter type mismatch | Check parameter type |

---

## Performance

| Operation | Average Time | Cache |
|-----------|-------------|-------|
| search_graphs | 50ms | Yes (TTL 300s) |
| get_structure | 50-200ms | Yes |
| trace_flow | 100-300ms | Yes |
| estimate_performance | 100-200ms | Yes |
| find_pcg_by_asset | 50-100ms | Yes |
| validate | 100-300ms | Yes |
| set_parameter | 50-100ms | No |
| execute_graph | 500ms-5s | No |
| batch_add_nodes | 200-500ms | No |

---

## Related Documentation

- [ue_manage_pcg Tool Doc](../../../docs/reference/tools/ue_manage_pcg.md)
- [CLAUDE.md - Tool Selection](../../../CLAUDE.md)

---

**Related**: [SKILL.md](SKILL.md) | [ADVANCED.md](ADVANCED.md) | [EXAMPLES.md](EXAMPLES.md)
