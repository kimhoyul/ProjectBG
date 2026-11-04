# Control Rig Workflow — Examples

## Example 1: Batch Complexity Audit

**User**: "프로젝트 전체 리그 복잡도 감사해줘"

**Workflow**:
```python
# Stage 1: Find all Control Rigs
rigs = ue_manage_rigging(operation="search_rigs", rig_type="control_rig", query="CR_*")

# Stage 2: Analyze each
for rig in rigs["results"]:
    ue_manage_rigging(operation="analyze_rig", rig_type="control_rig", rig_name=rig["name"])
```

**Output**:
```text
Control Rig Complexity Audit

Total: 3 rigs analyzed
High Complexity: 1 rig (>50 nodes)
Medium: 1 rig
Low: 1 rig

Top Optimization Targets:
1. CR_Mannequin_Body (1264 nodes, 2148 links) - HIGH
2. CR_Mannequin_FootPlant (282 nodes, 376 links) - MEDIUM
3. CR_Mannequin_Procedural (178 nodes, 214 links) - LOW
```

---

## Example 2: A/B Rig Comparison

**User**: "CR_Mannequin_Body vs CR_Mannequin_FootPlant 비교"

**Workflow**:
```python
# Single-call comparison
ue_manage_rigging(operation="compare_rigs", rig_type="control_rig",
                  rig_a="CR_Mannequin_Body", rig_b="CR_Mannequin_FootPlant")
```

**Output**:
```text
Control Rig Comparison: CR_Mannequin_Body vs CR_Mannequin_FootPlant

              | Body    | FootPlant | Diff
Nodes         |  1264   |    282    | -982
Links         |  2148   |    376    | -1772
Unit Types    |    27   |     14    |  -13

Unit Types:
  Shared: SetBoneTransform, GetBoneTransform, BeginExecution (14)
  Only in Body: AimBoneMath, TwoBoneIKSimple, SendEvent (13)
```

---

## Example 3: C++ Integration Analysis

**User**: "CR_Mannequin_Body의 커스텀 노드 C++ 코드 찾아줘"

**Workflow**:
```python
# Stage 1: Get rig nodes
nodes = ue_manage_rigging(operation="get_nodes", rig_type="control_rig",
                          rig_name="CR_Mannequin_Body")

# Stage 2: Search C++ symbols for each FRigUnit type
for unit_type in unique_unit_types:
    ue_analyze_symbols(operation="search_symbols", query=unit_type)
```

**Output**:
```text
Rig + C++ Integration: CR_Mannequin_Body

Unit Types Found: 27
C++ Sources Located: 25/27

Source Mapping:
  FRigUnit_SetBoneTransform → ControlRig/Private/Units/Hierarchy/RigUnit_SetBoneTransform.cpp
  FRigUnit_TwoBoneIKSimplePerItem → ControlRig/Private/Units/Highlevel/RigUnit_TwoBoneIK.cpp
  FRigUnit_AimBoneMath → ControlRig/Private/Units/Highlevel/RigUnit_AimBone.cpp

Missing C++ (project-specific):
  FRigUnit_CustomFootContact
  FRigUnit_ProceduralOffset
```
