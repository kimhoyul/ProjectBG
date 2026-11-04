# UE IK Rig Analysis — Examples

## Example 1: Single IK Rig Analysis

**User**: "IK_Mannequin 분석해줘"

**Workflow**:
```python
# Step 1: Get structure
ue_manage_rigging(operation="get_structure", rig_type="ik_rig", rig_name="IK_Mannequin")

# Step 2: Deep-dive into components
ue_manage_rigging(operation="get_solvers", rig_type="ik_rig", rig_name="IK_Mannequin")
ue_manage_rigging(operation="get_goals", rig_type="ik_rig", rig_name="IK_Mannequin")
ue_manage_rigging(operation="get_chains", rig_type="ik_rig", rig_name="IK_Mannequin")
```

**Output**:
```text
=== IK_Mannequin -- IK Rig Analysis ===

--- Overview ---
Full-body IK rig for UE5 Mannequin skeleton with 3 solvers, 6 goals, 8 chains.

--- Solvers ---
| # | Solver Name | Solver Type      | Root Bone | Enabled |
|---|-------------|------------------|-----------|---------|
| 1 | FullBodyIK  | IKRigFBIKSolver  | pelvis    | true    |
| 2 | LeftLegIK   | IKRig_LimbSolver | thigh_l   | true    |
| 3 | RightLegIK  | IKRig_LimbSolver | thigh_r   | true    |

--- Complexity Summary ---
Solvers: 3 | Goals: 6 | Chains: 8 | Complexity: medium
```

---

## Example 2: IK Rig Comparison

**User**: "IK_Mannequin vs IK_MetaHuman 비교해줘"

**Workflow**:
```python
# Single-call comparison
ue_manage_rigging(operation="compare_rigs", rig_type="ik_rig",
                  rig_a="IK_Mannequin", rig_b="IK_MetaHuman")
```

**Output**:
```text
=== IK Rig Comparison: IK_Mannequin vs IK_MetaHuman ===

            | IK_Mannequin | IK_MetaHuman | Diff (A-B)
Solvers     |     3        |     5        |  -2
Goals       |     6        |     10       |  -4
Chains      |     8        |     12       |  -4

Solver Types:
  Shared: IKRigFBIKSolver
  Only in MetaHuman: IKRig_LimbSolver, IKRig_PoleSolver
```

---

## Example 3: Project-Wide IK Rig Audit

**User**: "프로젝트 IK 리그 전체 감사해줘"

**Workflow**:
```python
# Step 1: Find all IK Rigs
rigs = ue_manage_rigging(operation="search_rigs", rig_type="ik_rig", query="*")

# Step 2: Analyze each
for rig in rigs["results"]:
    ue_manage_rigging(operation="analyze_rig", rig_type="ik_rig", rig_name=rig["name"])
```

**Output**:
```text
=== IK Rig Complexity Audit ===

Total: 5 rigs analyzed
High Complexity: 1 rig
Medium: 3 rigs
Low: 1 rig

Top Optimization Targets:
1. IK_FullBody (5 solvers, 12 goals, 10 chains) - high
2. IK_Mannequin (3 solvers, 6 goals, 8 chains) - medium
3. IK_Simple (1 solver, 2 goals, 2 chains) - low
```
