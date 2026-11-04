---
name: pcg-workflow
model: opus
description: "Purpose: Multi-tool PCG orchestration: Performance audit + Asset migration + Graph cloning + Landscape audit. 3-tool orchestration (ue_manage_pcg + ue_search_assets + ue_editor_*). Triggers: 'PCG 성능 최적화', 'PCG 에셋 일괄 교체', 'batch PCG audit', 'clone PCG graph'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
allowed-tools:
  - mcp__narshamcp__ue_manage_pcg
  - mcp__narshamcp__ue_search_assets
  - mcp__narshamcp__ue_editor_actors
  - mcp__narshamcp__ue_editor_assets
  - mcp__narshamcp__ue_editor_debug
  - mcp__narshamcp__ue_editor_automation
argument-hint: "PCG pattern or graph name, operation: audit/migrate/clone/landscape"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# PCG Workflow (Multi-Tool Orchestration)

**Status**: Dev
**Focus**: Multi-tool PCG workflows that **cannot be done with single MCP calls**

---

## Purpose

Multi-tool PCG workflows requiring N-step orchestration:

- **PCG Performance Optimization**: Search all graphs → estimate each → prioritize by cost
- **PCG Asset Migration**: Find references → batch replace → validate → regenerate
- **PCG Graph Cloning**: Copy structure → create graph → batch add nodes → connect
- **PCG Landscape Audit**: Full project scan → validate all → performance estimate → report

**NOT covered** (use `ue_manage_pcg` directly):
- Basic search, get_structure, trace_flow
- Single parameter modification
- Single node add/remove/connect

---

## Trigger Phrases

### Korean
- "PCG 성능 최적화"
- "PCG 에셋 일괄 교체"
- "전체 PCG 감사"
- "PCG 그래프 복제"
- "SM_Tree를 SM_Tree_V2로 모든 PCG에서 교체"
- "프로젝트 전체 PCG 성능 점검"

### English
- "optimize PCG performance"
- "batch replace PCG assets"
- "full PCG audit"
- "clone PCG graph"
- "replace SM_Tree with SM_Tree_V2 in all PCGs"
- "project-wide PCG performance check"

**Keywords**: `PCG performance`, `batch PCG`, `PCG audit`, `clone graph`, `asset migration`, `PCG 성능`, `일괄 교체`, `전체 감사`

### NOT triggers (use tool directly)
- "PCG 구조 보여줘" → `ue_manage_pcg(get_structure)`
- "노드 파라미터 바꿔줘" → `ue_manage_pcg(set_parameter)`
- "PCG 그래프 검색" → `ue_manage_pcg(search_graphs)`

---

**Target**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, ask the user which PCG graph(s) to target and which operation to perform (audit, migrate, clone, or landscape).

Parse `$ARGUMENTS` for PCG pattern/graph name and operation type.

---

## 3-Tool Orchestration Workflows

### Workflow 1: PCG Performance Optimization

**Goal**: Scan all PCG graphs, estimate performance, generate prioritized optimization report

```mermaid
graph LR
    A[Project] --> B[Stage 1: search_graphs *]
    B --> C[Stage 2: estimate_performance each]
    C --> D[Sort by cost]
    D --> E[Optimization Report]
```text

```python
def optimize_pcg_performance():
    """Multi-graph performance audit with prioritized recommendations"""

    # Stage 1: Find all PCG graphs
    graphs = ue_manage_pcg(operation="search_graphs", query="*")

    # Stage 2: Estimate performance for each
    results = []
    for graph in graphs.get("results", []):
        perf = ue_manage_pcg(
            operation="estimate_performance",
            graph_name=graph["name"]
        )
        structure = ue_manage_pcg(
            operation="get_structure",
            graph_name=graph["name"]
        )
        results.append({
            "graph": graph["name"],
            "node_count": structure.get("node_count", 0),
            "edge_count": structure.get("edge_count", 0),
            "estimated_cost": perf.get("estimated_time_ms", 0),
            "bottleneck_nodes": perf.get("bottleneck_nodes", [])
        })

    # Stage 3: Sort by cost (most expensive first)
    results.sort(key=lambda x: x["estimated_cost"], reverse=True)

    return {
        "total_graphs": len(results),
        "top_10_expensive": results[:10],
        "optimization_targets": [r for r in results if r["estimated_cost"] > 500],
        "summary": f"{len(results)} graphs analyzed, {len([r for r in results if r['estimated_cost'] > 500])} need optimization"
    }
```text

**Example Output**:
```text
📊 PCG Performance Optimization Report

Total Graphs: 45
Optimization Targets (>500ms): 8

Top 5 Most Expensive:
| Graph | Nodes | Edges | Cost | Bottleneck |
|-------|-------|-------|------|------------|
| PCG_CityBlock_Large | 234 | 312 | 4.2s | MeshSampler (50K points) |
| PCG_Forest_Dense | 156 | 198 | 2.8s | DensityFilter (chain) |
| PCG_Landscape_Full | 189 | 245 | 2.1s | LandscapeData (4K res) |
| PCG_River_Valley | 98 | 134 | 1.5s | SplineSampler (1K segments) |
| PCG_Village_Props | 67 | 89 | 0.8s | ActorSpawner (200 actors) |

Recommendations:
1. PCG_CityBlock_Large: Partition into sub-volumes (4x speedup)
2. PCG_Forest_Dense: Add early density filter before sampler
3. PCG_Landscape_Full: Reduce landscape resolution to 2K
```markdown

---

### Workflow 2: PCG Asset Migration

**Goal**: Replace a mesh/asset across all PCG graphs that reference it

```mermaid
graph LR
    A[Old Asset] --> B[Stage 1: find_pcg_by_asset]
    B --> C[Stage 2: replace_asset each]
    C --> D[Stage 3: validate each]
    D --> E[Stage 4: regenerate optional]
```text

```python
def migrate_pcg_asset(old_asset, new_asset, regenerate=False):
    """Replace asset across all PCG graphs that reference it"""

    # Stage 1: Find all PCGs using old asset
    pcg_refs = ue_manage_pcg(
        operation="find_pcg_by_asset",
        asset_path=old_asset
    )

    # Stage 2: Replace in each graph
    replaced = []
    for ref in pcg_refs.get("graphs", []):
        result = ue_manage_pcg(
            operation="replace_asset",
            graph_name=ref["graph_name"],
            old_asset=old_asset,
            new_asset=new_asset
        )
        replaced.append({
            "graph": ref["graph_name"],
            "nodes_affected": result.get("nodes_replaced", 0),
            "status": "replaced"
        })

    # Stage 3: Validate each modified graph
    validated = []
    for item in replaced:
        validation = ue_manage_pcg(
            operation="validate",
            graph_name=item["graph"]
        )
        item["valid"] = validation.get("is_valid", False)
        item["warnings"] = validation.get("warnings", [])
        validated.append(item)

    # Stage 4: Regenerate (optional, Editor required)
    if regenerate:
        for item in validated:
            if item["valid"]:
                ue_manage_pcg(
                    operation="regenerate",
                    graph_name=item["graph"]
                )
                item["regenerated"] = True

    return {
        "old_asset": old_asset,
        "new_asset": new_asset,
        "graphs_affected": len(replaced),
        "details": validated,
        "summary": f"Replaced in {len(replaced)} graphs, {len([v for v in validated if v['valid']])} valid"
    }
```text

**Example Output**:
```yaml
📦 PCG Asset Migration: SM_Tree_Oak → SM_Tree_Oak_V2

Graphs Affected: 12
Nodes Replaced: 34

| Graph | Nodes | Valid | Regenerated |
|-------|-------|-------|-------------|
| PCG_Forest_Zone1 | 5 | ✅ | ✅ |
| PCG_Forest_Zone2 | 3 | ✅ | ✅ |
| PCG_Village_Trees | 8 | ✅ | ✅ |
| PCG_Park_Central | 4 | ⚠️ (1 warning) | ❌ |
| ... | ... | ... | ... |

Warnings:
- PCG_Park_Central: New mesh has different bounds (may affect density)
```markdown

---

### Workflow 3: PCG Graph Cloning

**Goal**: Deep copy a PCG graph with optional modifications

```mermaid
graph LR
    A[Source Graph] --> B[Stage 1: get_structure]
    B --> C[Stage 2: create_graph]
    C --> D[Stage 3: batch_add_nodes]
    D --> E[Stage 4: connect_nodes each]
    E --> F[Stage 5: set_parameter each]
```text

```python
def clone_pcg_graph(source_graph, new_name, output_path, parameter_overrides=None):
    """Deep copy PCG graph with optional parameter modifications"""

    # Stage 1: Get source structure
    structure = ue_manage_pcg(
        operation="get_structure",
        graph_name=source_graph
    )

    # Stage 2: Create empty graph
    new_graph = ue_manage_pcg(
        operation="create_graph",
        graph_name=new_name,
        output_path=output_path
    )

    # Stage 3: Batch add nodes
    nodes = [{"type": n["type"], "position": n.get("position", {})}
             for n in structure.get("nodes", [])]
    ue_manage_pcg(
        operation="batch_add_nodes",
        graph_name=new_name,
        nodes=nodes
    )

    # Stage 4: Recreate connections
    for edge in structure.get("edges", []):
        ue_manage_pcg(
            operation="connect_nodes",
            graph_name=new_name,
            source_node=edge["source"],
            target_node=edge["target"],
            source_pin=edge.get("source_pin", "Out"),
            target_pin=edge.get("target_pin", "In"),
        )

    # Stage 5: Apply parameter overrides
    if parameter_overrides:
        for node_id, params in parameter_overrides.items():
            for param_name, param_value in params.items():
                ue_manage_pcg(
                    operation="set_parameter",
                    graph_name=new_name,
                    node_id=node_id,
                    parameter_name=param_name,
                    parameter_value=param_value,
                )

    return {
        "source": source_graph,
        "clone": new_name,
        "nodes_copied": len(structure.get("nodes", [])),
        "edges_copied": len(structure.get("edges", [])),
        "parameters_overridden": len(parameter_overrides or {}),
        "status": "success"
    }
```markdown

---

### Workflow 4: PCG Landscape Audit

**Goal**: Full project PCG audit with validation and performance assessment

```mermaid
graph TD
    A[Project] --> B[Stage 1: search_graphs *]
    B --> C[Stage 2: validate each]
    C --> D[Stage 3: estimate_performance each]
    D --> E[Stage 4: find_in_level]
    E --> F[Comprehensive Report]
```text

```python
def audit_pcg_landscape():
    """Complete project PCG audit: structure + validation + performance"""

    # Stage 1: Find all PCG graphs
    all_graphs = ue_manage_pcg(operation="search_graphs", query="*")

    audit = []
    for graph in all_graphs.get("results", []):
        entry = {"name": graph["name"]}

        # Stage 2: Validate
        validation = ue_manage_pcg(operation="validate", graph_name=graph["name"])
        entry["valid"] = validation.get("is_valid", False)
        entry["warnings"] = len(validation.get("warnings", []))
        entry["errors"] = len(validation.get("errors", []))

        # Stage 3: Performance estimate
        perf = ue_manage_pcg(operation="estimate_performance", graph_name=graph["name"])
        entry["estimated_cost_ms"] = perf.get("estimated_time_ms", 0)
        entry["node_count"] = perf.get("node_count", 0)

        # Stage 4: Level usage
        usage = ue_manage_pcg(operation="find_pcg_usage", graph_name=graph["name"])
        entry["level_instances"] = len(usage.get("instances", []))

        audit.append(entry)

    # Categorize results
    healthy = [a for a in audit if a["valid"] and a["warnings"] == 0]
    warnings = [a for a in audit if a["warnings"] > 0]
    errors = [a for a in audit if not a["valid"]]

    return {
        "total_graphs": len(audit),
        "healthy": len(healthy),
        "with_warnings": len(warnings),
        "with_errors": len(errors),
        "total_estimated_cost_ms": sum(a["estimated_cost_ms"] for a in audit),
        "top_10_expensive": sorted(audit, key=lambda x: x["estimated_cost_ms"], reverse=True)[:10],
        "error_graphs": errors,
        "details": audit
    }
```text

**Example Output**:
```text
📋 PCG Landscape Audit Report

Summary:
├── Total Graphs: 45
├── Healthy: 38 (84%)
├── Warnings: 5 (11%)
├── Errors: 2 (4%)
└── Total Est. Cost: 15.2s

Health Distribution:
✅ Healthy: 38
⚠️ Warnings: 5
❌ Errors: 2

Error Graphs:
1. PCG_OldTest → Cycle detected between Node_5 and Node_12
2. PCG_Deprecated → Missing asset reference: SM_Rock_Old

Top 5 Expensive:
1. PCG_CityBlock_Large: 4.2s (234 nodes, 3 instances)
2. PCG_Forest_Dense: 2.8s (156 nodes, 7 instances)
3. PCG_Landscape_Full: 2.1s (189 nodes, 1 instance)
```markdown

---

## Evaluation Criteria

### Quantitative Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Batch audit time | **<3 min** for 50 graphs | search + validate + estimate |
| Asset migration | **<5 min** for 20 graphs | find + replace + validate |
| Graph cloning | **<2 min** | structure + create + batch_add + connect |
| Landscape audit | **<5 min** for 50 graphs | Full 4-stage audit |

### Before/After Comparison

| Scenario | Without Skill | With Skill |
|----------|--------------|------------|
| Full PCG performance audit | ~20 min (graph-by-graph) | **<3 min** (batch) |
| Asset batch replacement | ~30 min (find+replace+validate) | **<5 min** (automated) |
| Graph clone + customize | ~15 min (manual recreation) | **<2 min** (automated) |

---

## Error Handling

```python
def handle_workflow_error(error):
    if "graph not found" in error:
        return ue_manage_pcg(operation="search_graphs", query="*")
    elif "editor not connected" in error:
        return "Offline analysis only. Regeneration requires Editor with Remote Control."
    elif "cycle detected" in error:
        return "Graph has circular dependency. Use validate to identify cycle nodes."
    elif "asset not found" in error:
        return ue_search_assets(asset_type="staticmesh", query="*")
```markdown

---

## Related Files

- **ADVANCED.md** - Complex workflow patterns
- **EXAMPLES.md** - Complete scenarios with outputs
- **REFERENCE.md** - All 32 operation specifications

---

## Output Format

```text
=== PCG Workflow: {workflow_name} ===
Mode: {audit|migrate|clone|landscape}
Date: {YYYY-MM-DD}

Graphs Processed: {count}
Stages Completed: {N}/{total}
Duration: {seconds}s

Results:
  Healthy: {count}
  Warnings: {count}
  Errors: {count}

Top Issues:
  1. {graph_name}: {issue_description}
  2. {graph_name}: {issue_description}

Summary: {one-line summary}
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `graph not found` during batch operation | PCG graph asset deleted or renamed after initial search | Re-run `search_graphs` to refresh the graph list; verify asset paths in Content Browser |
| `editor not connected` on regenerate step | Editor Remote Control plugin not running or WebSocket disconnected | Start Editor with Remote Control enabled; offline analysis (search, validate, estimate) still works without Editor |
| `cycle detected` in validate step | Circular node dependency in PCG graph | Use `ue_manage_pcg(validate)` to identify the cycle nodes; break the cycle by removing one edge |
| `asset not found` during migration | Old asset path is invalid or asset was already migrated | Use `ue_search_assets` to find the correct asset path; check if migration was partially applied |

---

**Status**: Dev (Multi-tool PCG workflows only)
**Related**: Issue #4442
## MCP Tool Examples

```python
# List actors affected by PCG graph
ue_editor_actors(operation="smart", class_name="APCGVolume")
```
