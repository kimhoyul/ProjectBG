# PCG Workflow - Examples

**Version**: 1.0.0 (Issue #4442)

---

## Example 1: Full Project PCG Audit

**Scenario**: "프로젝트 전체 PCG 성능 점검해줘"

### User Query
```text
"전체 PCG 그래프 성능 감사하고 최적화 우선순위 보고서 만들어줘"
```

### Workflow Execution

**Step 1: Search All Graphs**
```python
ue_manage_pcg(
    operation="search_graphs",
    query="*"
)
```

**Output**:
```text
Found 45 PCG graphs:
1. /Game/PCG/PCG_Forest_Dense
2. /Game/PCG/PCG_CityBlock_Large
3. /Game/PCG/PCG_River_Valley
...
```

**Step 2: Estimate Performance (each)**
```python
for graph in results:
    ue_manage_pcg(
        operation="estimate_performance",
        graph_name=graph["name"]
    )
```

**Step 3: Validate (each)**
```python
for graph in results:
    ue_manage_pcg(
        operation="validate",
        graph_name=graph["name"]
    )
```

**Final Output**:
```text
📊 PCG Project Audit Report

Summary:
├── Total Graphs: 45
├── Healthy: 38 (84%)
├── Warnings: 5 (11%)
├── Errors: 2 (4%)
└── Total Est. Cost: 15.2s

Top 5 Optimization Targets:
| # | Graph | Cost | Nodes | Issue |
|---|-------|------|-------|-------|
| 1 | PCG_CityBlock_Large | 4.2s | 234 | 50K point sampler |
| 2 | PCG_Forest_Dense | 2.8s | 156 | Filter chain |
| 3 | PCG_Landscape_Full | 2.1s | 189 | High-res landscape |
| 4 | PCG_River_Valley | 1.5s | 98 | 1K spline segments |
| 5 | PCG_Village_Props | 0.8s | 67 | 200 actor spawns |

Errors:
❌ PCG_OldTest: Cycle at Node_5 → Node_12
❌ PCG_Deprecated: Missing SM_Rock_Old
```

---

## Example 2: Asset Batch Replacement

**Scenario**: "SM_Tree_Oak을 SM_Tree_Oak_V2로 모든 PCG에서 교체"

### User Query
```text
"SM_Tree_Oak 메시를 SM_Tree_Oak_V2로 전체 PCG에서 일괄 교체해줘"
```

### Workflow Execution

**Step 1: Find PCGs Using Old Asset**
```python
ue_manage_pcg(
    operation="find_pcg_by_asset",
    asset_path="/Game/Meshes/Trees/SM_Tree_Oak"
)
```

**Output**:
```text
Found 12 PCG graphs referencing SM_Tree_Oak:
├── PCG_Forest_Zone1 (5 nodes)
├── PCG_Forest_Zone2 (3 nodes)
├── PCG_Village_Trees (8 nodes)
├── PCG_Park_Central (4 nodes)
└── ... 8 more
```

**Step 2: Replace in Each**
```python
for graph in affected_graphs:
    ue_manage_pcg(
        operation="replace_asset",
        graph_name=graph["name"],
        old_asset="/Game/Meshes/Trees/SM_Tree_Oak",
        new_asset="/Game/Meshes/Trees/SM_Tree_Oak_V2"
    )
```

**Step 3: Validate**
```python
for graph in affected_graphs:
    ue_manage_pcg(
        operation="validate",
        graph_name=graph["name"]
    )
```

**Final Output**:
```text
📦 Asset Migration Complete: SM_Tree_Oak → SM_Tree_Oak_V2

Graphs Modified: 12
Total Nodes Replaced: 34

| Graph | Nodes | Status |
|-------|-------|--------|
| PCG_Forest_Zone1 | 5 | ✅ Valid |
| PCG_Forest_Zone2 | 3 | ✅ Valid |
| PCG_Village_Trees | 8 | ✅ Valid |
| PCG_Park_Central | 4 | ⚠️ Bounds mismatch |

⚠️ PCG_Park_Central: New mesh bounds differ, density may shift
```

---

## Example 3: PCG Graph Structure Analysis

**Scenario**: "PCG_BattleField 구조 분석해줘"

### User Query
```text
"PCG_BattleField 그래프 구조 보여주고 데이터 흐름 추적해줘"
```

### Workflow Execution

**Step 1: Get Structure**
```python
ue_manage_pcg(
    operation="get_structure",
    graph_name="PCG_BattleField"
)
```

**Output**:
```text
📊 PCG_BattleField Structure:

Nodes: 134 | Edges: 76

Node Categories:
├── Generators: 3 (LandscapeData, VolumeData, SplineData)
├── Filters: 8 (DensityFilter ×4, BoundsFilter ×2, DistanceFilter ×2)
├── Modifiers: 5 (TransformPoints ×3, SetAttribute ×2)
├── Spawners: 4 (StaticMeshSpawner ×3, ActorSpawner ×1)
└── Subgraphs: 2 (Tree_Pipeline, Rock_Pipeline)
```

**Step 2: Trace Data Flow**
```python
ue_manage_pcg(
    operation="trace_flow",
    graph_name="PCG_BattleField"
)
```

**Output**:
```text
🔄 Data Flow:

LandscapeData
├─→ DensityFilter(slope<30°) ─→ TransformPoints(random) ─→ StaticMeshSpawner(trees)
├─→ DensityFilter(altitude>500) ─→ StaticMeshSpawner(rocks)
├─→ BoundsFilter(safe_zone) ─→ ActorSpawner(spawn_points)
└─→ Subgraph:Road_Pipeline ─→ StaticMeshSpawner(barriers)

VolumeData(city_area)
└─→ BoundsFilter ─→ DistanceFilter(min=100) ─→ ActorSpawner(buildings)
```

---

## Example 4: Graph Cloning with Modifications

**Scenario**: "PCG_Forest를 복제하고 밀도 조절"

### User Query
```text
"PCG_Forest를 복제해서 PCG_Forest_Sparse로 만들고 밀도 50%로 줄여줘"
```

### Workflow Execution

**Step 1: Get Source Structure**
```python
ue_manage_pcg(
    operation="get_structure",
    graph_name="PCG_Forest"
)
```

**Step 2: Create New Graph**
```python
ue_manage_pcg(
    operation="create_graph",
    graph_name="PCG_Forest_Sparse",
    output_path="/Game/PCG/Variants/"
)
```

**Step 3: Copy Nodes**
```python
ue_manage_pcg(
    operation="batch_add_nodes",
    graph_name="PCG_Forest_Sparse",
    nodes=[...copied_from_source...]
)
```

**Step 4: Recreate Connections**
```python
for edge in source_edges:
    ue_manage_pcg(
        operation="connect_nodes",
        graph_name="PCG_Forest_Sparse",
        source_node=edge["source"],
        target_node=edge["target"]
    )
```

**Step 5: Adjust Density**
```python
ue_manage_pcg(
    operation="set_density",
    graph_name="PCG_Forest_Sparse",
    density=0.5
)
```

**Final Output**:
```text
✅ Graph Cloned Successfully

Source: PCG_Forest
Clone: PCG_Forest_Sparse
Location: /Game/PCG/Variants/PCG_Forest_Sparse

Copied: 156 nodes, 198 edges
Density: 1.0 → 0.5 (50% reduction)

Expected spawn count: ~5,000 (was ~10,000)
```

---

## Example 5: Find PCG Usage in Levels

**Scenario**: "이 PCG가 어디서 사용되는지 확인"

### User Query
```text
"PCG_Forest_Dense가 어떤 레벨에서 사용되는지 찾아줘"
```

### Workflow Execution

```python
ue_manage_pcg(
    operation="find_pcg_usage",
    graph_name="PCG_Forest_Dense"
)
```

**Output**:
```text
📍 PCG_Forest_Dense Usage in Levels

Found 7 instances in 3 levels:

Level: MainWorld
├── PCG_Forest_Actor_1 (Volume: 500x500x100)
├── PCG_Forest_Actor_2 (Volume: 300x300x80)
└── PCG_Forest_Actor_3 (Volume: 800x200x100)

Level: SubLevel_East
├── PCG_Forest_Actor_4 (Volume: 400x400x120)
└── PCG_Forest_Actor_5 (Volume: 600x600x100)

Level: SubLevel_West
├── PCG_Forest_Actor_6 (Volume: 350x350x90)
└── PCG_Forest_Actor_7 (Volume: 500x300x100)
```

---

## Example 6: Template Management

**Scenario**: "PCG 템플릿으로 새 그래프 빠르게 만들기"

### User Query
```text
"사용 가능한 PCG 템플릿 보여주고 Forest_Dense 템플릿으로 새 그래프 만들어줘"
```

### Workflow Execution

**Step 1: List Templates**
```python
ue_manage_pcg(operation="list_templates")
```

**Output**:
```text
📚 Available PCG Templates:

| Template | Nodes | Description |
|----------|-------|-------------|
| Forest_Dense | 12 | Dense tree forest with undergrowth |
| Forest_Sparse | 8 | Scattered trees on hillside |
| City_Grid | 15 | Grid-based building placement |
| Landscape_Rocks | 6 | Terrain-following rock scatter |
| Road_Barrier | 4 | Spline-based road barriers |
```

**Step 2: Apply Template**
```python
ue_manage_pcg(
    operation="apply_template",
    graph_name="PCG_MyNewForest",
    template_name="Forest_Dense"
)
```

**Output**:
```text
✅ Template Applied

Graph: PCG_MyNewForest
Template: Forest_Dense
Nodes Created: 12
Edges Created: 14

Ready for customization:
- Set density with set_density
- Adjust bounds with set_bounds
- Change meshes with replace_asset
```

---

## Example 7: Performance-Focused Optimization

**Scenario**: "PCG가 너무 느려서 최적화 필요"

### User Query
```text
"PCG_CityBlock_Large 실행이 4초 넘게 걸려. 최적화 방법 찾아줘"
```

### Workflow Execution

**Step 1: Estimate Performance**
```python
ue_manage_pcg(
    operation="estimate_performance",
    graph_name="PCG_CityBlock_Large"
)
```

**Output**:
```text
⏱️ Performance Estimate:
- Total: 4.2s
- Bottleneck: MeshSampler_0 (50K points, 2.8s)
- Secondary: DensityFilter_3 (chain of 4 filters)
```

**Step 2: Get Structure for Bottleneck Analysis**
```python
ue_manage_pcg(
    operation="get_structure",
    graph_name="PCG_CityBlock_Large"
)
```

**Step 3: Trace Flow Through Bottleneck**
```python
ue_manage_pcg(
    operation="trace_flow",
    graph_name="PCG_CityBlock_Large",
    start_node="MeshSampler_0"
)
```

**Final Output**:
```text
🔧 Optimization Recommendations for PCG_CityBlock_Large

Current: 4.2s (234 nodes, 312 edges)
Target: <1.0s

Issue 1: MeshSampler_0 processes 50K points (2.8s)
→ Fix: Add DensityFilter BEFORE sampler to reduce input to 10K points
→ Expected: 2.8s → 0.5s

Issue 2: Filter chain (4 sequential filters)
→ Fix: Merge DensityFilter_1 + DensityFilter_2 into single combined filter
→ Expected: 0.8s → 0.3s

Issue 3: No LOD consideration
→ Fix: Add distance-based density scaling
→ Expected: Further 30% reduction at runtime

Estimated result: 4.2s → 0.8s (81% improvement)
```

---

## Activation Tests

### Should Trigger PCG Workflow

- "전체 PCG 성능 감사" ✅ (batch audit)
- "SM_Tree를 모든 PCG에서 교체" ✅ (asset migration)
- "PCG 그래프 복제" ✅ (graph cloning)
- "프로젝트 PCG 건강도 점검" ✅ (landscape audit)
- "batch PCG performance check" ✅
- "clone PCG graph with modifications" ✅

### Should NOT Trigger (use tool directly)

- "PCG 구조 보여줘" ❌ → `ue_manage_pcg(get_structure)`
- "노드 파라미터 바꿔줘" ❌ → `ue_manage_pcg(set_parameter)`
- "PCG 그래프 검색" ❌ → `ue_manage_pcg(search_graphs)`
- "PCG 그래프 실행" ❌ → `ue_manage_pcg(execute_graph)`

---

**Related**: [SKILL.md](SKILL.md) | [ADVANCED.md](ADVANCED.md) | [REFERENCE.md](REFERENCE.md)
