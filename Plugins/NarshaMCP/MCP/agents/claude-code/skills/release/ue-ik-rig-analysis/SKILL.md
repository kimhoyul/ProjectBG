---
name: ue-ik-rig-analysis
model: opus
description: "Purpose: IK Rig structure analysis and comparison. Answers 'Analyze IK Rig X' by inspecting solvers, goals, chains, and retargeter configurations. Supports A/B rig comparison and complexity audit. Triggers: 'IK Rig 분석', 'IK 리그 구조', 'IK 솔버 분석', 'Analyze IK Rig', 'IK Rig comparison', 'retargeter analysis'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools:
  - mcp__narshamcp__ue_manage_rigging
  - mcp__narshamcp__ue_search_assets
argument-hint: "IK Rig name (e.g., IK_Mannequin), operation: analyze/compare/audit, --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE IK Rig Analysis

> **Note**: Uses `ue_manage_rigging` with `rig_type='ik_rig'` (merged from ue_manage_ik_rig in Issue #6901)

**Issue**: #6047
**Purpose**: Analyze IK Rig structure, solvers, goals, chains, and retargeter configurations with A/B comparison support

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user which IK Rig to analyze.
2. **Detect operation mode** (single analysis, comparison, or audit) from context
3. **Execute MCP tools** via `ue_manage_rigging` with appropriate operations
4. **Generate structured report** using the output format below

---

## Auto-Trigger Phrases

### Korean
- "IK 리그 분석해줘", "IK Rig 구조 보여줘"
- "IK 솔버 분석", "IK 골 확인해줘"
- "IK 리그 비교해줘", "리타게터 분석"
- "IK 체인 구조", "IK 리그 감사"
- "IK 리그 복잡도 분석"

### English
- "Analyze IK Rig X", "Show IK Rig structure"
- "IK Rig solvers", "IK Rig goals"
- "Compare IK Rigs", "Retargeter analysis"
- "IK chain structure", "IK Rig audit"
- "IK Rig complexity analysis"

**Keywords**: `IK Rig`, `IK_`, `Retargeter`, `IK solver`, `IK goal`, `IK chain`, `IK 리그`, `솔버`, `리타게터`, `IK 체인`

---

## Mode Detection

```python
MODE_DETECTION = {
    "analyze":  ["analyze", "structure", "inspect", "분석", "구조", "확인"],
    "compare":  ["compare", "vs", "diff", "비교", "차이"],
    "audit":    ["audit", "batch", "all", "감사", "전체", "복잡도"],
}
# Default: "analyze" if no mode keyword detected
```

---

## Workflow

### Step 1: Discover IK Rigs

If the user provides a specific rig name, skip to Step 2. Otherwise, search for available rigs.

```python
ToolSearch("select:mcp__narshamcp__ue_manage_rigging")

# Smart mode: auto-routes based on params
ue_manage_rigging(operation="smart", rig_type="ik_rig", query="*")  # → search_rigs

# Or explicit search
ue_manage_rigging(operation="search_rigs", rig_type="ik_rig", query="*")
```

**Extract from response**: `results` entries carry `name`, `path`, `asset_sub_type`
(`IKRigDefinition` or `IKRetargeter`), and summary counts (`solver_count`, `goal_count`,
`retarget_chain_count`, `chain_mapping_count`). Retargeter entries also carry
`source_ik_rig`/`target_ik_rig`. Use `total_count`/`truncated` to detect capped results
(default `limit` is 50).

### Step 2: Get Rig Structure (Single Analysis)

For a named IK Rig, retrieve the full structure overview.

```python
# Smart mode: auto-detects get_structure from rig_name
ue_manage_rigging(operation="smart", rig_type="ik_rig", rig_name="IK_Mannequin")  # → get_structure

# Or explicit
ue_manage_rigging(operation="get_structure", rig_type="ik_rig", rig_name="IK_Mannequin")
```

**Extract from response**: `skeleton_ref`, `summary` counts (`solver_count`, `goal_count`,
`retarget_chain_count`), and the full `solvers`/`goals`/`retarget_chains` arrays.
For complexity scoring, use `analyze_rig` (Step 5) — `get_structure` does not compute it.

### Step 3: Deep-Dive - Solvers, Goals, Chains

Retrieve detailed breakdown of the three core IK Rig components.

```python
# Get solver configurations (FABRIK, LimbIK, CCDIK, etc.)
ue_manage_rigging(operation="get_solvers", rig_type="ik_rig", rig_name="IK_Mannequin")

# Get goal definitions (effectors, root bones, target bones)
ue_manage_rigging(operation="get_goals", rig_type="ik_rig", rig_name="IK_Mannequin")

# Get chain definitions (bone chains for IK solving)
ue_manage_rigging(operation="get_chains", rig_type="ik_rig", rig_name="IK_Mannequin")
```

**Extract from response** (offline-parsed fields):
- **Solvers** (`get_solvers`): `solver_name`, `solver_type` (UE class name, e.g. `IKRigFBIKSolver`), `root_bone`, `enabled`.
- **Solver settings** (`get_solver_settings`, #19954): deep read of iterations / per-bone stiffness (`rotation_stiffness`/`position_stiffness` in `bone_settings`). On some real assets values can still be empty or delta-omitted — treat sparse responses as a known parser caveat (#22976), not as “op missing”.
- **Goals** (`get_goals`): `goal_name`, `bone_name`. When the export includes them, position/rotation/space/alpha fields may appear (parser from #19955); null means export omitted the value, not that the op is unavailable.
- **Chains** (`retarget_chains[]`): `chain_name`, `start_bone`, `end_bone`, and `ik_goal_name` (empty string when the chain has no bound goal — Issue #19960). Per-chain bone count is still not exposed.

### Step 4: Retargeter Configuration (Optional)

If retargeting is relevant or the user asks about retargeters, inspect the retargeter asset.

> **Retargeter discovery**: Retargeters live in the same `ik_rig` category as IK Rig Definitions (`rig_type` accepts only `control_rig`/`ik_rig` — there is no `ik_retargeter` value). Search with `ue_manage_rigging(operation='search_rigs', rig_type='ik_rig', query='<rig_name_stem>')` using the IK Rig name stem (e.g., `Mannequin` from `IK_Mannequin`), then keep results whose `asset_sub_type == "IKRetargeter"`. Do not assume a hard-coded `RTG_` prefix.

```python
# Get retargeter configuration (source/target IK Rigs, chain mappings)
# Use the retargeter name discovered from Step 2 structure or search_rigs
ue_manage_rigging(operation="get_retargeter", rig_type="ik_rig", rig_name="<retargeter_name_from_discovery>")
```

**Extract from response** (Issue #19960 deepened offline parse):
- Source/target IK Rig references (`source_ik_rig` / `target_ik_rig`).
- `chain_mappings[]`: each mapping has `source_chain` / `target_chain`. When the export serialized non-default `FTargetChainSettings`, a nested `settings` object is present with `enable_fk`, `rotation_mode`, `rotation_alpha`, `translation_mode`, `translation_alpha`, `pole_vector_matching`, `enable_ik`, `blend_to_source`.
- `retarget_poses[]`: array of **objects** (not bare names) — each pose carries `pose_name`, `root_translation_offset`, `bone_rotation_offsets[]` (`bone_name` + `rotation`), and `bone_offset_count`. Poses exist only on IKRetargeter assets — IKRigDefinition has none (Issue #19947).

### Step 5: Complexity Audit (Audit Mode)

For batch analysis across all project IK Rigs.

```python
def audit_ik_rigs(pattern="*"):
    # Stage 1: Find all IK Rigs / Retargeters
    rigs = ue_manage_rigging(operation="search_rigs", rig_type="ik_rig", query=pattern)

    audit_results = []
    for rig in rigs.get("results", []):
        name = rig["name"]
        sub = rig.get("asset_sub_type") or ""
        # Prefer first-class audit ops when available:
        # - IKRigDefinition → validate + analyze_rig
        # - IKRetargeter → audit_retargeter
        if sub == "IKRetargeter":
            health = ue_manage_rigging(operation="audit_retargeter", rig_type="ik_rig", rig_name=name)
            audit_results.append({"name": name, "kind": "retargeter", "audit": health})
            continue
        health = ue_manage_rigging(operation="validate", rig_type="ik_rig", rig_name=name)
        resp = ue_manage_rigging(operation="analyze_rig", rig_type="ik_rig", rig_name=name)
        stats = resp.get("analysis", {})
        audit_results.append({
            "name": name,
            "kind": "definition",
            "validate": health,
            "solver_count": stats.get("solver_count", 0),
            "goal_count": stats.get("goal_count", 0),
            "chain_count": stats.get("retarget_chain_count", 0),
            "complexity": stats.get("complexity_level", "unknown"),
        })

    # Sort by total component count descending
    audit_results.sort(
        key=lambda x: x["solver_count"] + x["goal_count"] + x["chain_count"],
        reverse=True
    )
    return audit_results
```

### Step 6: A/B Rig Comparison (Compare Mode)

Compare two IK Rigs side-by-side.

```python
def compare_ik_rigs(rig_a, rig_b):
    # Single-call comparison
    comparison = ue_manage_rigging(operation="compare_rigs", rig_type="ik_rig", rig_a=rig_a, rig_b=rig_b)

    # Optionally get detailed structures for deeper analysis
    struct_a = ue_manage_rigging(operation="get_structure", rig_type="ik_rig", rig_name=rig_a)
    struct_b = ue_manage_rigging(operation="get_structure", rig_type="ik_rig", rig_name=rig_b)

    return {
        "comparison": comparison,
        "structure_a": struct_a,
        "structure_b": struct_b,
    }
```

---

## Output Format

Present results in this structured format:

```markdown
=== [Rig Name] -- IK Rig Analysis ===

--- Overview ---
[1-2 sentence summary: skeleton, purpose, complexity level]

--- Solvers ---
| # | Solver Name | Solver Type       | Root Bone | Enabled |
|---|-------------|-------------------|-----------|---------|
| 1 | FullBodyIK  | IKRigFBIKSolver   | pelvis    | true    |
| 2 | LeftLegIK   | IKRig_LimbSolver  | thigh_l   | true    |

--- Goals ---
| # | Goal Name   | Bone        |
|---|-------------|-------------|
| 1 | hand_r_Goal | hand_r      |
| 2 | foot_l_Goal | foot_l      |

--- Chains ---
| # | Chain Name | Start Bone | End Bone |
|---|-----------|------------|----------|
| 1 | RightArm  | clavicle_r | hand_r   |
| 2 | LeftLeg   | thigh_l    | foot_l   |

--- Retargeter (if applicable) ---
Source Rig: IK_Mannequin
Target Rig: IK_CustomCharacter
Chain Mappings: RightArm -> RightArm (enable_ik=true, blend_to_source=0.0), LeftLeg -> LeftLeg
Retarget Poses: Default Pose (N bone_rotation_offsets), TPose (...)

--- Complexity Summary ---
Solvers: 4 | Goals: 6 | Chains: 8 | Complexity: medium (from analyze_rig complexity_level)
```

### Comparison Output Format

```text
=== IK Rig Comparison: [Rig A] vs [Rig B] ===

                 | Rig A | Rig B | Diff (A-B)
Solvers          |   3   |   5   |  -2
Goals            |   4   |   6   |  -2
Chains           |   6   |   8   |  -2
Complexity Score |  48   |  76   |  -28

Solver Types:
  Shared: IKRigFBIKSolver
  Only in A: -
  Only in B: IKRig_LimbSolver

Chain Differences:
  Only in A: Spine
  Only in B: Tail, Spine_Extended
```

### Audit Output Format

```text
=== IK Rig Complexity Audit ===

Total: N rigs analyzed
High Complexity: X rigs
Medium: Y rigs
Low: Z rigs

Top Optimization Targets:
1. IK_FullBody (5 solvers, 12 goals, 10 chains) - high
2. IK_Mannequin (3 solvers, 6 goals, 8 chains) - medium
3. IK_Simple (1 solver, 2 goals, 2 chains) - low
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| IK Rig not found | Wrong name or asset not in project | Run `ue_manage_rigging(operation="search_rigs", rig_type="ik_rig", query="*")` to list all available rigs, then retry with correct name |
| Retargeter not found | No retargeter asset for this rig | Report "No retargeter configured for this IK Rig" and skip retargeter step; suggest creating one if needed |
| Metadata not cached | First query after cache clear | Run `ue_search_assets(asset_type="IKRig")` to populate metadata cache, then retry |
| MCP server not connected | Server process not running | Report: "Run `ue_check_health()` to verify MCP connection" |
| Comparison fails (missing rig) | One of the two rigs does not exist | Search for both rigs first with `search_rigs`, report which one is missing, suggest alternatives |
| rig_type ambiguity | Shared operation without rig_type | Add `rig_type='ik_rig'` explicitly |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** - Should activate:
1. "IK_Mannequin 분석해줘" -> Activate (single analysis)
2. "Analyze IK Rig structure" -> Activate (single analysis)
3. "IK_Hand vs IK_Foot 비교해줘" -> Activate (comparison)
4. "프로젝트 IK 리그 전체 감사" -> Activate (audit)
5. "IK 솔버랑 골 확인해줘" -> Activate (deep-dive)

**Negative (3)** - Should NOT activate:
1. "IK_Mannequin 검색해줘" -> Use `ue_manage_rigging(search_rigs)` directly
2. "Control Rig 분석해줘" -> Use `control-rig-workflow` skill (different domain)
3. "캐릭터 리타게팅 설정해줘" -> Use `ue_manage_rigging` directly for write operations

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Domain detection accuracy | >95% |
| MCP tool selection accuracy | 100% (hardcoded to ue_manage_rigging) |
| Response time (single rig) | <15s |
| Response completeness | All applicable output sections populated |
| Audit batch time | <2 min for 20 rigs |

---

---

## Self-Test

`/ue-ik-rig-analysis --self-test` validates this skill:

1. **Structure check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
2. **Frontmatter check**: Parse YAML for required fields (`name`, `description`, `allowed-tools`)
3. **Routing check**: Verify `routing.json` entry exists and trigger patterns match
4. **Tool availability**: Verify `ue_manage_rigging` is in `tools/list` via `ToolSearch`
5. **Example validity**: All `ue_manage_rigging` calls in EXAMPLES.md use `rig_type="ik_rig"`

**Expected**: All 5 checks PASS. Run `/ue-ik-rig-analysis --self-test` separately for full validation.

---

**Status**: Phase 1 MVP
**Related**: Issue #6047, #19961, #19960, #21942, #22977 (capability denial residual)
**Last Updated**: 2026-07-31
