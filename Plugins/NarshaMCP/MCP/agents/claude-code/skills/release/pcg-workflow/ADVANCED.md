# PCG Workflow - Advanced Guide

**Version**: 1.0.0 (Issue #4442)

---

## Smart Mode Deep Dive

### Auto-Routing Rules

Smart mode detects the intended operation from parameters:

```python
# Pattern-based detection
if "query" in params: → search_graphs
elif "graph_name" and "node_type" in params: → find_nodes
elif "graph_name" and "start_node" in params: → trace_flow
elif "graph_name" and "parameter_name" in params: → set_parameter
elif "graph_name" and "old_asset" in params: → replace_asset
elif "asset_path" in params: → find_pcg_by_asset
elif "graph_name" in params: → get_structure  # default for single graph
```

### Smart Mode Examples

```python
# Claude interprets "PCG_* 검색" → search_graphs
ue_manage_pcg(operation="smart", query="PCG_Forest*")

# Claude interprets "구조 분석" → get_structure
ue_manage_pcg(operation="smart", graph_name="PCG_BattleField")

# Claude interprets "이 메시 사용하는 PCG" → find_pcg_by_asset
ue_manage_pcg(operation="smart", asset_path="/Game/Meshes/SM_Tree")
```

---

## PCG Node Type Deep Dive

### Complete Node Type Mapping

| UE Class | Category | Description |
|----------|----------|-------------|
| `PCGPointGenerator` | Generator | Random point cloud |
| `PCGLandscapeDataNode` | Generator | Points from landscape |
| `PCGVolumeDataNode` | Generator | Points from volume |
| `PCGSplineDataNode` | Generator | Points along spline |
| `PCGDensityFilter` | Filter | Filter by density value |
| `PCGBoundsFilter` | Filter | Filter by volume bounds |
| `PCGDistanceFilter` | Filter | Filter by min/max distance |
| `PCGSlopeFilter` | Filter | Filter by surface slope |
| `PCGMeshSampler` | Sampler | Sample mesh surface |
| `PCGSplineSampler` | Sampler | Sample spline path |
| `PCGSurfaceSampler` | Sampler | Sample any surface |
| `PCGTransformPoints` | Modifier | Translate/rotate/scale |
| `PCGSetAttribute` | Modifier | Set point attribute |
| `PCGCopyPoints` | Modifier | Duplicate points |
| `PCGStaticMeshSpawner` | Spawner | Place static meshes |
| `PCGActorSpawner` | Spawner | Place actors |
| `PCGSubgraph` | Flow | Reusable sub-pipeline |
| `PCGLoop` | Flow | Iterate over data |
| `PCGBranch` | Flow | Conditional execution |

### Node Type Selection Guide

```text
Q: Scatter meshes on landscape?
→ LandscapeData → DensityFilter → TransformPoints → StaticMeshSpawner

Q: Place objects along a road?
→ SplineData → SplineSampler → TransformPoints → StaticMeshSpawner

Q: Fill a room with furniture?
→ VolumeData → DistanceFilter → StaticMeshSpawner

Q: Place NPCs at valid locations?
→ LandscapeData → SlopeFilter → BoundsFilter → ActorSpawner
```

---

## Multi-Graph Orchestration Patterns

### Pattern 1: Cascading PCG Pipeline

Execute multiple PCG graphs in sequence where output of one feeds into another.

```python
def cascading_pcg_pipeline(graphs_in_order):
    """Execute PCGs in sequence, each building on previous results"""

    for i, graph in enumerate(graphs_in_order):
        # Stage 1: Validate before execution
        validation = ue_manage_pcg(
            operation="validate",
            graph_name=graph
        )
        if not validation["is_valid"]:
            print(f"⚠️ {graph} has errors, skipping")
            continue

        # Stage 2: Execute
        ue_manage_pcg(
            operation="execute_graph",
            actor_path=f"/Game/Maps/Main.{graph}_Actor"
        )

        # Stage 3: Verify spawn results
        result = ue_manage_pcg(
            operation="get_spawn_result",
            actor_path=f"/Game/Maps/Main.{graph}_Actor"
        )
        print(f"✅ {graph}: {result.get('spawn_count', 0)} objects spawned")
```

### Pattern 2: Variant Generation

Create multiple variants of a PCG graph with different parameters.

```python
def generate_pcg_variants(source_graph, variants):
    """Create variants: Forest_Dense, Forest_Sparse, Forest_Autumn"""

    for variant_name, overrides in variants.items():
        # Clone
        structure = ue_manage_pcg(
            operation="get_structure",
            graph_name=source_graph
        )

        ue_manage_pcg(
            operation="create_graph",
            graph_name=variant_name,
            output_path="/Game/PCG/Variants/"
        )

        # Copy structure
        ue_manage_pcg(
            operation="batch_add_nodes",
            graph_name=variant_name,
            nodes=structure["nodes"]
        )

        # Apply variant-specific overrides
        for node_id, params in overrides.items():
            for key, value in params.items():
                ue_manage_pcg(
                    operation="set_parameter",
                    graph_name=variant_name,
                    node_id=node_id,
                    parameter_name=key,
                    parameter_value=value
                )

# Usage
generate_pcg_variants("PCG_Forest", {
    "PCG_Forest_Dense": {"DensityFilter_0": {"density": 0.9}},
    "PCG_Forest_Sparse": {"DensityFilter_0": {"density": 0.3}},
    "PCG_Forest_Autumn": {"SetAttribute_0": {"material": "/Game/M_Autumn"}}
})
```

---

## WebSocket Batch Operations Integration

For PCG workflows that modify multiple graphs, use `ue_batch_editor_operations` to batch Editor operations.

```python
# Instead of 6 individual regenerate calls:
ue_batch_editor_operations(operations=[
    {"type": "send_command", "command": "regenerate_pcg",
     "params": {"actor": "/Game/Maps/Main.PCG_Forest_1"}},
    {"type": "send_command", "command": "regenerate_pcg",
     "params": {"actor": "/Game/Maps/Main.PCG_Forest_2"}},
    {"type": "send_command", "command": "regenerate_pcg",
     "params": {"actor": "/Game/Maps/Main.PCG_Forest_3"}},
    {"type": "send_command", "command": "regenerate_pcg",
     "params": {"actor": "/Game/Maps/Main.PCG_City_1"}},
    {"type": "send_command", "command": "regenerate_pcg",
     "params": {"actor": "/Game/Maps/Main.PCG_City_2"}},
    {"type": "send_command", "command": "regenerate_pcg",
     "params": {"actor": "/Game/Maps/Main.PCG_City_3"}}
])
# Before: ~24s (6 × 4s individual connections)
# After: ~7.4s (single connection, 69% improvement)
```

---

## Cross-Tool Integration

### PCG + Asset Search

```python
# Find all static meshes used by PCG graphs
pcg_graphs = ue_manage_pcg(operation="search_graphs", query="*")

for graph in pcg_graphs["results"]:
    assets = ue_manage_pcg(
        operation="find_assets",
        graph_name=graph["name"],
        asset_type="StaticMesh"
    )
    # Cross-reference with project assets
    for asset in assets.get("assets", []):
        details = ue_search_assets(
            asset_type="staticmesh",
            query=asset["name"]
        )
```

### PCG + Editor Control

```python
# Visualize PCG bounds in editor
structure = ue_manage_pcg(
    operation="get_structure",
    graph_name="PCG_Forest"
)

# Draw debug bounds for each PCG volume
for node in structure["nodes"]:
    if node["type"] == "VolumeData":
        ue_editor_automation(
            operation="draw_debug",
            shape="box",
            location=node.get("position", {"x": 0, "y": 0, "z": 0}),
            extent={"x": 500, "y": 500, "z": 100},
            color={"r": 0, "g": 255, "b": 0},
            duration=30.0
        )
```

---

## Performance Optimization Strategies

### 1. Filter Early, Spawn Late

```text
WRONG:  Generator → Sampler(50K) → Filter → Spawner
RIGHT:  Generator → Filter → Sampler(5K) → Spawner
                     ↑ Reduce points BEFORE expensive operations
```

### 2. Partition Large Volumes

```text
WRONG:  Single VolumeData(1000x1000x100) → 100K points
RIGHT:  4x VolumeData(500x500x100) → 25K each, parallel-friendly
```

### 3. Use Subgraphs for Reuse

```text
WRONG:  Copy same 10-node pipeline in 5 graphs
RIGHT:  Create Subgraph, reference it 5 times
         → Maintain one copy, update propagates
```

### 4. LOD-Aware Density

```python
# Set different densities based on distance from player
ue_manage_pcg(operation="set_density", graph_name="PCG_Near", density=1.0)
ue_manage_pcg(operation="set_density", graph_name="PCG_Mid", density=0.5)
ue_manage_pcg(operation="set_density", graph_name="PCG_Far", density=0.1)
```

---

## Troubleshooting

### Graph Not Generating Output

```python
# 1. Validate graph
validation = ue_manage_pcg(operation="validate", graph_name="PCG_Test")
# Check: is_valid, errors, warnings

# 2. Check data flow
flow = ue_manage_pcg(operation="trace_flow", graph_name="PCG_Test")
# Look for disconnected nodes or dead-end paths

# 3. Verify bounds
structure = ue_manage_pcg(operation="get_structure", graph_name="PCG_Test")
# Check: Are bounds properly set? Is volume too small?
```

### Performance Degradation

```python
# 1. Estimate current performance
perf = ue_manage_pcg(operation="estimate_performance", graph_name="PCG_Slow")
# Check: bottleneck_nodes, complexity_score

# 2. Identify expensive nodes
structure = ue_manage_pcg(operation="get_structure", graph_name="PCG_Slow")
# Look for: High node_count, many edges, large samplers

# 3. Find optimization targets
nodes = ue_manage_pcg(
    operation="find_nodes",
    graph_name="PCG_Slow",
    node_type="Sampler"
)
# Check: Are samplers processing too many points?
```

### Asset Reference Issues

```python
# Find all missing asset references
validation = ue_manage_pcg(operation="validate", graph_name="PCG_Broken")

for error in validation.get("errors", []):
    if "asset not found" in error.lower():
        # Search for replacement
        ue_search_assets(
            asset_type="staticmesh",
            query="*" + error.split(":")[-1].strip() + "*"
        )
```

---

## Common Pitfalls

| Pitfall | Problem | Solution |
|---------|---------|----------|
| Infinite loops | Loop node without exit condition | Always set max_iterations |
| Memory overflow | Too many points in pipeline | Add density filters early |
| Z-fighting | Objects spawning at same height | Add random Z offset in TransformPoints |
| Overlapping | Objects too close together | Use DistanceFilter after spawner |
| Missing LOD | All PCGs at full density | Use distance-based density scaling |
| Slow iteration | Regenerating entire level | Work on isolated sub-volumes |

---

**Related**: [SKILL.md](SKILL.md) | [EXAMPLES.md](EXAMPLES.md) | [REFERENCE.md](REFERENCE.md)
