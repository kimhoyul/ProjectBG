# Execution Flow Explorer - Advanced Features

Advanced usage, filtering, depth control, performance tuning, and comparisons.

---

## 🎛️ Depth Control

### Recommended Depths by Use Case

| Use Case | Recommended Depth | Rationale | Node Count |
|----------|-------------------|-----------|------------|
| **Quick Overview** | 1 | Direct callers/callees only | ~5-10 nodes |
| **Bug Tracing** | 2 | Covers most bug scenarios | ~10-20 nodes |
| **Impact Analysis** | 3 | Comprehensive impact | ~20-50 nodes |
| **Architecture Study** | 4+ | Full system understanding | 50+ nodes (use filters!) |

### Progressive Depth Exploration

**Workflow**:
```text
User: "Depth 1로 시작해줘"
Agent: [Shows 8 direct callers/callees]

User: "좀 더 깊게 볼래"
Agent: [Increases to depth=2, shows +12 new nodes]
       "Total: 20 nodes (8 from depth 1 + 12 from depth 2)"

User: "Depth 3까지"
Agent: ⚠️ Warning: Depth 3 may add 30+ nodes. Continue? (y/n)
User: "y"
Agent: [Shows depth=3 with 45 total nodes]
```

**Implementation Note**:
The skill tracks previous depth queries and only fetches incremental data when depth increases, leveraging session cache for performance.

**Performance Impact**:
- **Depth 1**: ~500ms (cached PDB)
- **Depth 2**: ~800ms (+60% time, +200% nodes)
- **Depth 3**: ~1200ms (+50% time, +150% nodes)
- **Depth 4+**: Exponential growth (use filters!)

### Smart Depth Recommendations

The skill automatically suggests appropriate depth based on query:

```text
Query: "TakeDamage 함수 어디서 호출하나?" (simple query)
→ Recommended depth: 1-2

Query: "TakeDamage 시그니처 바꾸면 영향 범위?" (impact analysis)
→ Recommended depth: 3 (comprehensive)

Query: "EventTick 전체 흐름 보고 싶어" (Tick event)
→ Warning: EventTick can result in 1000+ nodes. Consider depth=1 or filtering.
```

---

## 🔍 Filtering Strategies

### Filter Types

#### 1. Language Filter (C++ only, Blueprint only)

**Use Case**: Focus on specific domain

**Example**:
```text
User: "C++ 호출만 보여줘"

Agent: [Re-filters results to show only C++ callers/callees]
       Showing: 19/32 nodes (C++ only)
       Hidden: 13 Blueprint nodes
```

**Implementation**:
Filters are applied post-execution (no re-query needed):
```python
filtered_graph = {
    "nodes": [n for n in graph["nodes"] if n["type"] == "cpp"],
    "edges": [e for e in graph["edges"]
              if e["from"] in cpp_node_ids and e["to"] in cpp_node_ids]
}
```

#### 2. Direction Filter (Callers only, Callees only)

**Use Case**: Focus on upstream or downstream only

**Example**:
```text
User: "피호출자만 보여줘" (callees only)

Agent: [Shows only callee nodes]
       Target: TakeDamage
       ↓
       ApplyDamageModifiers
       ↓
       PlayHitAnimation
       (5 callers hidden)
```

#### 3. Hotspot Filter (Most connected nodes)

**Use Case**: Identify critical functions

**Example**:
```text
User: "핫스팟만 보여줘" (min_connections=3)

Agent: 🔥 Top 5 Hotspots:
       1. TakeDamage (11 connections)
       2. ApplyDamageModifiers (7 connections)
       3. ProcessDamage (6 connections)
       4. PlayHitAnimation (4 connections)
       5. CanAttack (3 connections)

       [Shows filtered graph with only these 5 nodes]
```

**Algorithm**:
```python
hotspots = sorted(
    graph["nodes"],
    key=lambda n: n.get("connection_count", 0),
    reverse=True
)[:top_n]
```

#### 4. Path Filter (File path patterns)

**Use Case**: Focus on specific modules

**Example**:
```text
User: "Combat 모듈만 보여줘"

Agent: [Filters nodes where file path contains "Combat"]
       Showing: 8/32 nodes (Combat module only)
       Paths matched: *Combat*, *Weapon*, *Damage*
```

### Interactive Filtering UI

```text
Agent: "32개 노드가 너무 많으시면 필터를 적용할 수 있습니다:"

Options:
  1. C++ only (19 nodes)
  2. Blueprint only (13 nodes)
  3. Direct calls only (depth=1, 8 nodes)
  4. Hotspots only (5+ connections, 6 nodes)
  5. Module filter (specify path pattern)
  6. Custom filter

User: [Selects option or types "cancel"]
```

---

## ⚡ Performance Tuning

### Caching Strategy (Issue #1714 - Session Cache)

**Session Cache Behavior**:
```python
# First call (MISS)
result = await execute_bidirectional_trace(
    function_name="TakeDamage",
    depth=2
)
# Time: 1.2s (ue_analyze_symbols: 500ms + ue_trace_execution: 800ms)
# Cache: Populated for both tools

# Second call (HIT - same function, same depth)
result = await execute_bidirectional_trace(
    function_name="TakeDamage",
    depth=2
)
# Time: <10ms (both tools cached)
# Cache: Retrieved from session

# Third call (PARTIAL HIT - increased depth)
result = await execute_bidirectional_trace(
    function_name="TakeDamage",
    depth=3  # Depth increased from 2
)
# Time: ~400ms (depth=2 data cached, only fetch depth=3)
# Cache: Incremental update
```

**Cache TTL**: 300 seconds (5 minutes) per session

**Cache Invalidation**:
Users can manually clear cache if needed:
```text
User: "캐시 지워줘"
Agent: [Uses ue_cache_control to clear session cache]
       "Session cache cleared. Next query will fetch fresh data."
```

### Parallel Execution Optimization

**Sequential (Baseline - SLOW)**:
```python
# NOT RECOMMENDED
caller_result = await ue_analyze_symbols(...)  # 500ms
callee_result = await ue_trace_execution(...)  # 800ms
# Total: 1300ms
```

**Parallel (Optimized - FAST)** ⭐:
```python
# RECOMMENDED
import asyncio
caller_result, callee_result = await asyncio.gather(
    ue_analyze_symbols(...),
    ue_trace_execution(...)
)
# Total: max(500ms, 800ms) = 800ms (38% faster!)
```

**Time Savings**:
- Bidirectional mode: **40% faster** (1.3s → 0.8s tool execution)
- Caller/Callee only: No benefit (single tool)

### Graph Truncation for Large Results

**Auto-Truncation**:
```python
if len(graph["nodes"]) > max_nodes:
    # Sort by connection count (most important nodes first)
    sorted_nodes = sorted(
        graph["nodes"],
        key=lambda n: n.get("connection_count", 0),
        reverse=True
    )

    # Keep target + top (max_nodes-1) nodes
    truncated = sorted_nodes[:max_nodes]

    logger.warning(f"Graph truncated: {len(graph['nodes'])} → {max_nodes} nodes")
```

**User Experience**:
```text
⚠️ Graph truncated: 127 → 50 nodes (77 hidden)
Showing: Top 50 most connected nodes
Hidden: 77 less connected nodes

Suggestions:
  - Reduce depth (current: 4 → recommended: 2)
  - Apply filters (C++ only, hotspots only)
  - Increase max_nodes (current: 50 → use 100 if needed)
```

### Performance Tips

1. **Start small, expand gradually**:
   - Begin with depth=1, increase to 2/3 only if needed
   - Default to bidirectional (users rarely need more than 2-3 levels)

2. **Use filters early**:
   - If analyzing C++ refactoring, filter Blueprint nodes immediately
   - If tracing module-specific code, apply path filters upfront

3. **Avoid EventTick at high depths**:
   - EventTick can result in 1000+ nodes at depth=3
   - Recommend depth=1 for Tick events, or filter to specific paths

4. **Leverage cache**:
   - Repeated queries are <10ms (session cache)
   - Depth increases reuse previous data (incremental fetch)

---

## 🆚 Comparison with Other Skills

### vs Blueprint Flow Tracer

| Feature | Execution Flow Explorer | Blueprint Flow Tracer |
|---------|-------------------------|----------------------|
| **Focus** | Function-centric (caller/callee) | Event-driven (input→animation) |
| **Directionality** | Bidirectional (caller + callee) | Forward only (input→output) |
| **C++ Support** | Full (PDB integration) | Limited (cross-domain only) |
| **Use Case** | Impact analysis, refactoring | Understanding feature flow |
| **Performance** | ~1.2s (parallel execution) | ~1.8s (workflow shortcuts) |
| **Visualization** | Unified graph (callers/target/callees) | Sequential flow (input→animation) |

**When to use which?**:
- **Execution Flow Explorer** for:
  - "What calls X?" (caller analysis)
  - "Impact of changing X" (impact analysis)
  - "Who depends on this function?" (dependency graph)

- **Blueprint Flow Tracer** for:
  - "How does attack button work?" (input→output)
  - "Input → Animation flow" (sequential trace)
  - "What happens when I press E key?" (event-driven)

### vs Unreal Error Doctor

| Feature | Execution Flow Explorer | Unreal Error Doctor |
|---------|-------------------------|---------------------|
| **Focus** | Call graph analysis | Error diagnosis & fixing |
| **Primary Tool** | ue_analyze_symbols + ue_trace_execution | ue_fix_errors |
| **Output** | Mermaid graph + text summary | Diff preview + auto-fix |
| **Use Case** | Understanding code structure | Fixing compilation errors |
| **Interaction** | 4-stage workflow | 4-step medical workflow |

**Complementary Usage**:
```text
User: "링커 에러: TakeDamage undefined reference"

Step 1: Use Unreal Error Doctor
  → Diagnosis: Missing module dependency
  → Fix: Add "GameplayAbilities" to Build.cs
  → Apply fix

Step 2: Use Execution Flow Explorer
  → Analyze: Who calls TakeDamage?
  → Result: Found GA_MeleeAttack (GAS module)
  → Insight: Need GAS module because ability system calls it

Conclusion: Error fixed + understood why dependency was needed
```

---

## 🔬 Advanced Analysis Features

### Circular Dependency Detection

**Feature**: Leverage existing Tarjan's SCC algorithm (graph_traversal.py)

**Example**:
```text
User: "ProcessDamage 추적해줘"

Agent: [Executes bidirectional trace with cycle detection]
       ⚠️ Warning: 2 cycles detected in execution flow

       Cycle 1: ProcessDamage → ApplyModifiers → GetDamageMultiplier → ProcessDamage
       Cycle 2: UpdateHealth → CheckDeath → RespawnPlayer → UpdateHealth

       Recommendation: Review cycle 1 (may cause infinite recursion)
```

**Implementation**:
```python
# Enable cycle detection
result = await ue_trace_execution(
    operation="trace_execution_flow",
    params={
        "start_node": "ProcessDamage",
        "max_depth": 5,
        "detect_cycles": True,
        "max_cycles": 10
    }
)

if result.get("cycles_detected", 0) > 0:
    logger.warning(f"⚠️ {result['cycles_detected']} cycles detected")
    for cycle in result["cycles"]:
        logger.info(f"Cycle: {cycle['description']}")
```

### Hotspot Analysis

**Feature**: Rank functions by connection count (caller + callee)

**Example**:
```text
🔥 Top 10 Hotspots (Connection Count):

1. ACharacter::TakeDamage (11 connections)
   - 5 callers (who calls this)
   - 6 callees (what this calls)
   - Risk: High (central function, many dependencies)

2. ApplyDamageModifiers (7 connections)
   - 3 callers
   - 4 callees
   - Risk: Medium

3. ProcessDamage (6 connections)
   - 4 callers
   - 2 callees
   - Risk: Medium

...

Recommendation:
  - High-risk hotspots (10+ connections) require careful testing when modifying
  - Consider refactoring hotspots to reduce coupling
```

### Cross-Domain Link Analysis

**Feature**: Identify C++ ↔ Blueprint boundaries

**Example**:
```text
🔗 Cross-Domain Links Analysis:

Total cross-domain links: 5

1. C++ → Blueprint:
   - APlayerController::ProcessDamage → BP_DamageIndicator::Show
   - ANS_AttackTrace::NotifyTick → BP_HitEffect::Spawn

2. Blueprint → C++:
   - BP_EnvironmentalHazard::OnOverlap → ACharacter::TakeDamage
   - BP_MeleeWeapon::OnHit → UDamageCalculator::Calculate
   - BP_AbilityTask::Activate → UAbilitySystemComponent::TryActivate

Performance Note:
  - Cross-domain calls have overhead (~0.1ms per call)
  - Consider batching if called frequently (e.g., in Tick)
```

---

## 📤 Export Options

### Export to HTML Interactive Viewer

**Feature**: Generate interactive HTML with zoom/pan (leverages existing HTML exporter)

**Example**:
```text
User: "HTML로 내보내줘"

Agent: [Generates interactive HTML]
       ✅ Exported to: TakeDamage_bidirectional_flow.html
       Features:
         - Zoom/pan with mouse wheel
         - Click nodes to highlight connections
         - Search function names
         - Filter by type (C++/Blueprint)

       Open in browser: file:///.../TakeDamage_bidirectional_flow.html
```

**Implementation**:
```python
from MCP.tools.html_exporter import export_bidirectional_graph

html_path = export_bidirectional_graph(
    graph,
    output_path="TakeDamage_bidirectional_flow.html",
    interactive=True,
    style="unreal_engine"
)
```

### Export to JSON for External Tools

**Feature**: Export raw graph data for custom analysis

**Example**:
```text
User: "JSON으로 내보내줘"

Agent: [Exports graph structure]
       ✅ Exported to: TakeDamage_flow.json
       Format: {nodes: [...], edges: [...], metadata: {...}}

       Can be imported into:
         - Gephi (network analysis)
         - Cytoscape (visualization)
         - Custom Python scripts (NetworkX)
```

**Graph Format**:
```json
{
  "nodes": [
    {
      "id": "ACharacter::TakeDamage",
      "label": "TakeDamage",
      "type": "target",
      "depth": 0,
      "direction": "both",
      "connection_count": 11
    },
    {
      "id": "ProcessDamage",
      "type": "cpp",
      "depth": -1,
      "direction": "caller",
      "file": "Source/MyGame/PlayerController.cpp",
      "line": 156
    }
  ],
  "edges": [
    {
      "from": "ProcessDamage",
      "to": "ACharacter::TakeDamage",
      "type": "calls",
      "cross_domain": false
    }
  ],
  "metadata": {
    "target": "ACharacter::TakeDamage",
    "total_nodes": 12,
    "caller_nodes": 5,
    "callee_nodes": 6,
    "cross_domain_links": 3,
    "generation_time_ms": 1234
  }
}
```

---

## 🐛 Troubleshooting

### "Function not found in PDB index"

**Cause**: PDB not indexed or function name typo

**Solutions**:
```text
1. Check function name spelling (case-sensitive!)
   ✅ Correct: "ACharacter::TakeDamage"
   ❌ Wrong: "acharacter::takedamage", "TakeDamage" (missing class)

2. Use wildcard search to find similar names:
   User: "TakeDam으로 시작하는 함수 찾아줘"
   Agent: [Uses ue_analyze_symbols(search_symbols, query="TakeDam*")]
          Found: ACharacter::TakeDamage, AEnemy::TakeDamage

3. Verify PDB indexing status:
   from MCP.tools.policy_layer import ue_check_health
   health = await ue_check_health(input={"project_root": "D:/MyProject"})
   # Check status: health["pdb_index"]["status"] should be "ready"
   assert health["pdb_index"]["status"] == "ready", "PDB index not ready"

4. Re-index PDB if needed:
   Build project in Unreal Editor (generates fresh PDBs)
```

---

### "Graph too large (500+ nodes)"

**Cause**: Depth too high or function is a central hotspot

**Solutions**:
```mermaid
1. Reduce depth:
   Instead of depth=4, use depth=2
   result = await execute_bidirectional_trace(
       function_name="TakeDamage",
       depth=2  # Reduced from 4
   )

2. Apply language filter:
   result = await execute_bidirectional_trace(
       function_name="TakeDamage",
       depth=3,
       filters={"language": "cpp"}  # C++ only
   )

3. Focus on direct calls only:
   result = await execute_bidirectional_trace(
       function_name="TakeDamage",
       depth=1  # Direct callers/callees only
   )

4. Use truncation (keep top 50 by connections):
   graph = truncate_graph(large_graph, max_nodes=50)
   # Shows warning + provides filtering options
```

---

### "Blueprint metadata not found"

**Cause**: BlueprintMetadata Commandlet not run

**Solutions**:
```python
1. Run Commandlet in Unreal Editor:
   Tools → Run Commandlet → BlueprintMetadata

2. Or use command line:
   UE5Editor-Cmd.exe "D:/MyProject/MyProject.uproject" \
     -run=NarshaMCP.CUBlueprintMetadata \
     -unattended -nopause

3. Verify metadata file exists:
   Check: D:/MyProject/Intermediate/NarshaMCP/metadata/blueprint_metadata.json.zst

4. Check health status:
   health = await ue_check_health(input={"project_root": "D:/MyProject"})
   # Check status: health["metadata"]["status"] should be "ready"
   assert health["metadata"]["status"] == "ready", "Metadata not ready"
```

---

### "Timeout: Analysis took > 10s"

**Cause**: Depth too high or large codebase

**Solutions**:
```text
1. Increase timeout:
   result = await execute_bidirectional_trace(
       function_name="TakeDamage",
       depth=3,
       timeout_seconds=30.0  # Increased from 10s
   )

2. Reduce depth:
   result = await execute_bidirectional_trace(
       function_name="TakeDamage",
       depth=2,  # Reduced from 3
       timeout_seconds=10.0
   )

3. Use caller-only mode (faster than bidirectional):
   result = await execute_callers_only(
       function_name="TakeDamage",
       depth=3
   )
   # Single tool = no parallel overhead
```

---

### "Circular dependency detected"

**Cause**: Recursive function calls (A→B→A)

**Solutions**:
```text
1. Enable cycle detection to see full cycle:
   result = await ue_trace_execution(
       operation="trace_execution_flow",
       params={
           "start_node": "ProcessDamage",
           "max_depth": 3,
           "detect_cycles": True,
           "max_cycles": 10
       }
   )

2. Review detected cycles:
   if result["cycles_detected"] > 0:
       logger.warning(f"⚠️ {result['cycles_detected']} cycles found:")
       for cycle in result["cycles"]:
           logger.warning(f"  {' → '.join(cycle['nodes'])}")

3. Refactor to remove harmful cycles:
   Example: Extract shared logic to break recursion
   A → B → C → A (BAD)
   A → Helper ← B ← C (GOOD)
```

---

## 📈 Performance Benchmarks

### Execution Time by Depth

| Depth | Nodes (avg) | Time (cold) | Time (cached) | Cache Hit Rate |
|-------|-------------|-------------|---------------|----------------|
| 1 | 5-10 | 500ms | <1ms | 95% |
| 2 | 10-20 | 800ms | <1ms | 90% |
| 3 | 20-50 | 1200ms | 200ms | 70% (partial) |
| 4 | 50-100 | 2000ms | 400ms | 50% (partial) |
| 5 | 100+ | 3500ms | 600ms | 30% (partial) |

**Recommendations**:
- **Depth 1-2**: Instant results (cached)
- **Depth 3**: Acceptable for comprehensive analysis
- **Depth 4+**: Use filters or accept longer wait

---

### Parallel vs Sequential Execution

| Mode | Tools | Sequential Time | Parallel Time | Savings |
|------|-------|----------------|---------------|---------|
| Bidirectional | 2 | 1300ms | 800ms | 38% |
| Callers Only | 1 | 500ms | 500ms | 0% (N/A) |
| Callees Only | 1 | 800ms | 800ms | 0% (N/A) |

**Insight**: Bidirectional mode benefits most from parallelization

---

## 🔗 Related Documentation

- **MCP Tools**:
  - [ue_analyze_symbols](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md#ue_analyze_symbols)
  - [ue_trace_execution](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md#ue_trace_execution)
  - [ue_manage_blueprint](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md#ue_manage_blueprint)

- **Other Skills**:
  - [blueprint-flow](../blueprint-flow/SKILL.md)
  - [unreal-error-doctor](../unreal-error-doctor/SKILL.md)

- **Guides**:
  - [Call Graph V2.8.0](../../../docs/technical/CALL_GRAPH_V2.8.0.md)
  - [Tool Selection Guide](../../../docs/user-guides/tools/TOOL_SELECTION_GUIDE.md)

---

**Status**: ✅ Advanced Features Documented (Issue #3033)
**Last Updated**: 2026-01-01
