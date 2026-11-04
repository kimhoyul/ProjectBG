---
name: control-rig-workflow
model: opus
description: "Purpose: Multi-tool Control Rig orchestration: Rig complexity audit + A/B comparison + C++ integration analysis. 3-tool orchestration (ue_manage_rigging + ue_search_assets + ue_analyze_symbols). Triggers: '리그 성능 감사', 'compare control rigs', 'rig C++ integration'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools:
  - mcp__narshamcp__ue_manage_rigging
  - mcp__narshamcp__ue_search_assets
  - mcp__narshamcp__ue_analyze_symbols
argument-hint: "Control Rig pattern for batch analysis, operation: audit/compare/integrate, --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Control Rig Workflow (Multi-Tool Orchestration)

> **Note**: Uses `ue_manage_rigging` with `rig_type='control_rig'` (merged from ue_manage_control_rig in Issue #6901)

**Status**: Beta
**Focus**: Rig batch analysis + A/B comparison + C++ source correlation (NOT single-tool operations)

---

## Purpose

Multi-tool Control Rig workflows that **cannot be done with single MCP calls**:

- **Rig Complexity Audit**: Search all rigs -> Analyze each -> Prioritize by complexity
- **Rig Comparison**: Compare two rigs side-by-side (nodes, hierarchy, unit types)
- **Rig + C++ Integration**: Get rig nodes -> Find FRigUnit_* C++ implementations

**Single-call capability reference** (use `ue_manage_rigging` directly):

- Shared/meta: `smart`, `search_rigs`, `get_structure`, `analyze_rig`, `compare_rigs`, `validate`, `find_rig_usage`, `list_operations`.
- Control Rig reads: `get_hierarchy`, `get_nodes`, `get_connections`, `get_controls`, `get_variables`, `get_pins`, `find_bone_writers`, `trace_rig_graph`, `export_graph`.
- Graph/variable writes: `add_node`, `remove_node`, `add_link`, `remove_link`, `modify_node`, `recompile`, `add_variable`, `remove_variable`, `set_variable_default`, `rename_variable`.
- Control/hierarchy authoring: `add_control`, `remove_control`, `configure_control`, `add_bone`, `add_null`, `remove_element`, `create_control_rig`.

Parameter contract:

- Offline/read calls select an indexed asset with `rig_name`; Editor writes select a package with `rig_path`.
- `get_pins` accepts optional `bone`; `find_bone_writers` requires `bone`.
- `trace_rig_graph` requires `node_name` and accepts `direction=up|down|both`, `max_depth`.
- `export_graph` accepts `graph_format=mermaid|dot`, `collapse`, and `max_nodes` (`format` remains a deprecated compatibility alias).
- Variable defaults accept scalar string/number/boolean values. Hierarchy writes use explicit element names/types and support `verify=true`.

```python
health = ue_manage_rigging(operation="validate", rig_type="control_rig", rig_name="CR_Mannequin")
pins = ue_manage_rigging(operation="get_pins", rig_type="control_rig", rig_name="CR_Mannequin", bone="foot_l")
trace = ue_manage_rigging(operation="trace_rig_graph", rig_type="control_rig", rig_name="CR_Mannequin", node_name="BeginExecution_0", direction="down", max_depth=16)
graph = ue_manage_rigging(operation="export_graph", rig_type="control_rig", rig_name="CR_Mannequin", graph_format="mermaid", collapse=True, max_nodes=100)
```

---

## Trigger Phrases

### Korean
- "리그 성능 감사"
- "컨트롤 리그 비교"
- "리그 C++ 연동 분석"
- "배치 리그 분석"
- "전체 리그 복잡도 감사"

### English
- "rig performance audit"
- "compare control rigs"
- "rig C++ integration"
- "batch rig analysis"
- "audit all control rigs"

**Keywords**: `rig audit`, `rig comparison`, `compare rigs`, `C++ integration`, `FRigUnit`, `batch rig`, `리그 감사`, `리그 비교`

---

**Target**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, ask the user which Control Rig(s) to analyze and which operation to perform (audit, compare, or C++ integration).

Parse `$ARGUMENTS` for rig pattern/names and operation type.

---

## 3-Tool Orchestration Workflows

### Workflow 1: Rig Complexity Audit (Batch)

**Goal**: Analyze all project rigs and prioritize optimization targets by complexity

```mermaid
graph LR
    A[search_rigs *] --> B[Stage 1: List All Rigs]
    B --> C[Stage 2: analyze_rig each]
    C --> D[Sort by Complexity]
    D --> E[Top N Optimization Targets]
```text

```python
def audit_rig_complexity(pattern="*"):
    """Batch audit all Control Rigs, sorted by complexity"""

    # Stage 1: Find all Control Rigs
    rigs = ue_manage_rigging(operation="search_rigs", rig_type="control_rig", query=pattern)

    audit_results = []
    for rig in rigs.get("results", []):
        # Stage 2: Analyze each rig (READ op -> rig_name, NOT rig_path;
        # rig_path is only accepted by CR write ops)
        resp = ue_manage_rigging(
            operation="analyze_rig", rig_type="control_rig", rig_name=rig["name"]
        )

        # Complexity metrics are nested under "analysis"; the unit-type strings
        # come back as a top-level "unit_types" array (control_rig.rs analyze_rig).
        stats = resp.get("analysis", {})
        audit_results.append({
            "name": rig["name"],
            "path": resp.get("path", ""),
            "node_count": stats.get("total_nodes", 0),
            "link_count": stats.get("total_links", 0),
            "hierarchy_count": stats.get("hierarchy_elements", 0),
            "complexity": stats.get("complexity_rating", "Unknown"),
            "unit_types": resp.get("unit_types", []),
        })

    # Sort by node_count descending (complexity proxy)
    audit_results.sort(key=lambda x: x["node_count"], reverse=True)

    return {
        "total_rigs": len(audit_results),
        "high_complexity": [r for r in audit_results if r["complexity"] == "High"],
        "top_10": audit_results[:10],
        "summary": f"{len(audit_results)} rigs analyzed, "
                   f"{len([r for r in audit_results if r['complexity'] == 'High'])} high-complexity"
    }
```text

**Example Output**:
```text
Control Rig Complexity Audit

Total: 42 rigs analyzed
High Complexity: 8 rigs (>50 nodes)
Medium: 22 rigs (10-50 nodes)
Low: 12 rigs (<10 nodes)

Top Optimization Targets:
1. CR_Mannequin_PostProcess (127 nodes, 89 links) - HIGH
2. CR_IK_FullBody (95 nodes, 72 links) - HIGH
3. CR_Aim_Offset (63 nodes, 41 links) - HIGH
```markdown

---

### Workflow 2: Rig Comparison (A/B)

**Goal**: Compare two rigs side-by-side for migration, review, or debugging

```mermaid
graph LR
    A[Rig A + Rig B] --> B[Stage 1: compare_rigs]
    B --> C[Node/Link/Hierarchy Diff]
    C --> D[Unit Type Differences]
    D --> E[Migration Guide]
```text

```python
def compare_rigs_workflow(rig_a, rig_b):
    """Compare two Control Rigs with detailed diff"""

    # Stage 1: Use built-in compare
    comparison = ue_manage_rigging(
        operation="compare_rigs", rig_type="control_rig", rig_a=rig_a, rig_b=rig_b
    )

    # Stage 2: Get detailed structure for each (READ op -> rig_name, NOT rig_path)
    struct_a = ue_manage_rigging(operation="get_structure", rig_type="control_rig", rig_name=rig_a)
    struct_b = ue_manage_rigging(operation="get_structure", rig_type="control_rig", rig_name=rig_b)

    return {
        "comparison": comparison,
        "structure_a": struct_a,
        "structure_b": struct_b,
        "migration_notes": generate_migration_notes(comparison)
    }

def generate_migration_notes(comparison):
    """Generate migration guide from comparison diff"""
    notes = []
    unit_diff = comparison.get("unit_types", {})

    for unit in unit_diff.get("only_in_a", []):
        notes.append(f"REMOVE: {unit} (exists in A but not B)")
    for unit in unit_diff.get("only_in_b", []):
        notes.append(f"ADD: {unit} (exists in B but not A)")

    return notes
```text

**Example Output**:
```text
Control Rig Comparison: CR_IK_Hand vs CR_IK_Foot

            | CR_IK_Hand | CR_IK_Foot | Diff
Nodes       |     35     |     28     |  -7
Links       |     22     |     18     |  -4
Hierarchy   |     15     |     12     |  -3
Complexity  |   Medium   |   Medium   |  =

Unit Types:
  Shared: SetBoneTransform, GetBoneTransform, FABRIK
  Only in Hand: FingerIK, SpreadCorrection
  Only in Foot: FloorContact, SlopeAdaptation
```markdown

---

### Workflow 3: Rig + C++ Integration Analysis

**Goal**: Find C++ source code for custom RigUnit nodes used in a rig

```mermaid
graph LR
    A[Control Rig] --> B[Stage 1: get_nodes]
    B --> C[Extract FRigUnit_ types]
    C --> D[Stage 2: ue_analyze_symbols]
    D --> E[C++ Source Locations]
    E --> F[Implementation Details]
```text

```python
def analyze_rig_cpp_integration(rig_name):
    """Find C++ implementations for rig's custom nodes"""

    # Stage 1: Get all nodes with types (READ op -> rig_name, NOT rig_path)
    nodes = ue_manage_rigging(operation="get_nodes", rig_type="control_rig", rig_name=rig_name)

    # Extract unique unit types. Each node carries its RigUnit struct name under
    # "rig_unit_type" (get_nodes response), NOT "unit_type".
    unit_types = set()
    for node in nodes.get("nodes", []):
        unit_type = node.get("rig_unit_type", "")
        if unit_type.startswith("FRigUnit_"):
            unit_types.add(unit_type)

    # Stage 2: Search C++ symbols for each unit type. The optional type filter is
    # the singular string param `symbol_type` (enum: class|struct|...) - there is
    # no plural `symbol_types` array. FRigUnit_* are USTRUCTs, so filter on struct.
    cpp_sources = []
    for unit_type in sorted(unit_types):
        symbols = ue_analyze_symbols(
            operation="search_symbols",
            query=unit_type,
            symbol_type="struct"
        )
        if symbols.get("results"):
            cpp_sources.append({
                "unit_type": unit_type,
                "source_file": symbols["results"][0].get("file", ""),
                "line": symbols["results"][0].get("line", 0),
                "module": symbols["results"][0].get("module", ""),
            })

    return {
        "rig_name": rig_name,
        "total_unit_types": len(unit_types),
        "cpp_found": len(cpp_sources),
        "cpp_sources": cpp_sources,
        "missing_cpp": [u for u in unit_types if u not in [s["unit_type"] for s in cpp_sources]]
    }
```text

**Example Output**:
```text
Rig + C++ Integration: CR_Mannequin_PostProcess

Unit Types Found: 15
C++ Sources Located: 13/15

Source Mapping:
  FRigUnit_SetBoneTransform -> ControlRig/Private/Units/Hierarchy/RigUnit_SetBoneTransform.cpp:45
  FRigUnit_FABRIK          -> ControlRig/Private/Units/Highlevel/RigUnit_FABRIK.cpp:23
  FRigUnit_AimBone         -> ControlRig/Private/Units/Highlevel/RigUnit_AimBone.cpp:31

Missing C++ (custom project units):
  FRigUnit_CustomFootPlant
  FRigUnit_WeaponIK
```markdown

---

## Evaluation Criteria

### Quantitative Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Batch audit time | **<2 min** | From pattern to sorted results |
| Comparison accuracy | **>95%** | Node/link/type diff correct |
| C++ source match rate | **>80%** | Found source for engine units |
| Multi-tool completion | **>85%** | 3-stage workflow completes |

### Activation Test Cases

**Positive (5)** - Should activate:
1. "프로젝트 리그 전체 성능 감사해줘" -> Activate (batch audit)
2. "CR_IK_Hand vs CR_IK_Foot 비교" -> Activate (comparison)
3. "이 리그의 커스텀 노드 C++ 코드 찾아줘" -> Activate (C++ integration)
4. "rig performance audit" -> Activate (batch audit)
5. "compare these two control rigs" -> Activate (comparison)

**Negative (3)** - Should NOT activate (use tool directly):
1. "CR_Mannequin 구조 보여줘" -> Use `ue_manage_rigging(get_structure)` directly
2. "리그에 노드 추가해줘" -> Use `ue_manage_rigging(add_node)` directly
3. "리그 목록 검색" -> Use `ue_manage_rigging(search_rigs)` directly

### Before/After Comparison

| Scenario | Without Skill | With Skill |
|----------|--------------|------------|
| Audit 42 rigs | ~15 min (manual) | **<2 min** (batch) |
| Compare two rigs | ~5 min (2 calls + manual diff) | **<30 sec** (single workflow) |
| Find C++ for 15 node types | ~10 min (manual search) | **<1 min** (automated) |

---

## Error Handling

```python
def handle_workflow_error(error):
    if "metadata not found" in error:
        return "Run ue_search_assets first to populate metadata cache."
    elif "rig not found" in error:
        return ue_manage_rigging(operation="search_rigs", rig_type="control_rig", query="*")
    elif "symbol not found" in error:
        return "C++ source not indexed. Ensure PDB is loaded via ue_check_health."
```markdown

---

## Output Format

```text
Control Rig Workflow Report
───────────────────────────
Workflow: Audit | Compare | C++ Integration
Target: <rig pattern or rig names>

[Audit Results] (if audit)
  Total Rigs: <N>
  High Complexity: <N> (>50 nodes)
  Top Targets:
    1. <rig> (<N> nodes, <N> links) - HIGH
    2. <rig> (<N> nodes, <N> links) - MEDIUM

[Comparison] (if compare)
  | Metric    | Rig A | Rig B | Diff |
  |-----------|-------|-------|------|
  | Nodes     | <N>   | <N>   | <+/->|
  | Links     | <N>   | <N>   | <+/->|
  Unit Types: <shared>, Only A: <list>, Only B: <list>

[C++ Integration] (if integrate)
  Unit Types: <N> found, <N>/<N> C++ sources located
  Missing C++: <list of custom units>
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Metadata not found for rig | Metadata cache not populated | Run `ue_search_assets` first to populate metadata cache, then retry |
| Rig not found | Incorrect rig path or name | Use `ue_manage_rigging(operation="search_rigs", rig_type="control_rig", query="*")` to list all available rigs |
| C++ symbol not found for FRigUnit | Project PDB not indexed or engine PDB missing | Run `ue_check_health()` to verify PDB status; engine units require engine PDB loading |
| rig_type ambiguity | Shared operation without rig_type | Add `rig_type='control_rig'` explicitly |

---

## Self-Test

`/control-rig-workflow --self-test` validates this skill:

1. **Structure check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
2. **Frontmatter check**: Parse YAML for required fields (`name`, `description`, `allowed-tools`)
3. **Routing check**: Verify `routing.json` entry exists and trigger patterns match
4. **Tool availability**: Verify `ue_manage_rigging` is in `tools/list` via `ToolSearch`
5. **Example validity**: All `ue_manage_rigging` calls in EXAMPLES.md use `rig_type="control_rig"`

**Expected**: All 5 checks PASS. Run `/control-rig-workflow --self-test` separately for full validation.

---

**Status**: Beta
**Related**: Issue #4801 Phase 5B; #19945 (example realignment - READ ops use `rig_name` not `rig_path`; `analyze_rig` metrics nested under `analysis.*`; nodes carry `rig_unit_type`; symbol filter is singular `symbol_type`)
