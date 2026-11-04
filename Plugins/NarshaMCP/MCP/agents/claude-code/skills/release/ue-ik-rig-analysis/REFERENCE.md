# UE IK Rig Analysis — Reference

**Version**: 1.0.0 | **Tool**: `ue_manage_rigging` with `rig_type="ik_rig"`

---

## Operations

All operations use `ue_manage_rigging` with `rig_type="ik_rig"`. Parameters are passed
flat (top-level tool args) — there is no `params={...}` wrapper. `asset_name` is accepted
as an alias for `rig_name`; rig lookup is case-insensitive with partial-match fallback.

| Operation | Purpose | Required Params | Optional Params |
|-----------|---------|----------------|-----------------|
| `search_rigs` | Find IK Rig Definitions + Retargeters by name pattern | — | `query` (glob, default `*`; deprecated alias `pattern`), `limit` (default 50) |
| `get_structure` | Full overview (Definition: skeleton + solvers/goals/chains; Retargeter: rig refs + mappings + poses) | `rig_name` | — |
| `get_solvers` | Solver list (name/type/root/enabled) | `rig_name` | — |
| `get_solver_settings` | Deep solver settings (iterations, bone stiffness, …) (#19954) | `rig_name` | `solver_index` / `solver_name` |
| `get_goals` | Goal list (name + bone; transform/alpha when export has values) | `rig_name` | — |
| `get_chains` | Retarget chain list (`ik_goal_name` when bound) | `rig_name` | — |
| `get_retargeter` | Retargeter mappings/poses (asset must be an `IKRetargeter`; Definitions get an error pointing to `get_structure`) | `rig_name` | — |
| `suggest_chain_mappings` | Offline auto-map style chain mapping planner (#21978) | `rig_name` | source/target hints |
| `audit_retargeter` | Offline 3-asset retargeter join audit (#21977) | `rig_name` (retargeter) | — |
| `find_rig_usage` | Reverse usage: AnimBP / retargeter / skeleton consumers (#21979) | `rig_name` | — |
| `validate` | Offline structural health (skeleton ref, goal bones, chain endpoints, bone existence) (#19951) | `rig_name` | — |
| `analyze_rig` | Complexity metrics (sub-type aware) | `rig_name` | — |
| `compare_rigs` | A/B comparison | `rig_a`, `rig_b` | — |
| `list_operations` | Static op catalog (offline vs editor) | — | — |
| `smart` | Auto-route by params | varies | `kind` (Solver/Goal/Chain/Retargeter) |

**Known caveats** (do not reintroduce hard “not available” denials for shipped ops):
- `get_solver_settings` may still return empty/partial settings on some real assets
  (delta-omitted `iterations`, ObjectProperty bone_settings, UE5.6+ SolverStack) — see #22976.
- Prefer `validate` + `audit_retargeter` over hand-rolled multi-call audits.

## Smart Mode Routing

`operation="smart"` detects the target op from params in this priority order
(asset-name prefixes like `RTG_` are NOT used for op selection — a bare
`rig_name="RTG_X"` routes to `get_structure`, which still returns the full
retargeter view):

| Priority | Params Provided | Routes To |
|----------|----------------|-----------|
| 1 | `rig_a` + `rig_b` | `compare_rigs` |
| 2 | `query` (or `pattern`) | `search_rigs` |
| 3 | `kind="Solver"` / `"Goal"` / `"Chain"` / `"Retargeter"` (with `rig_name`) | `get_solvers` / `get_goals` / `get_chains` / `get_retargeter` |
| 4 | `rig_name` (any name) | `get_structure` |
| 5 | (none) | `search_rigs` |

## Return Value Schemas

Sample values are illustrative; keys match the handler output.

### search_rigs
```json
{
  "success": true,
  "operation": "search_rigs",
  "query": "IK_*",
  "results": [{
    "name": "IK_Mannequin", "path": "/Game/...", "asset_sub_type": "IKRigDefinition",
    "solver_count": 1, "goal_count": 4, "retarget_chain_count": 28,
    "chain_mapping_count": 0, "type": "IKRig"
  }],
  "total_count": 3, "truncated": false, "total_available": 5,
  "aggregate": {
    "definition_count": 2, "retargeter_count": 1, "total_solvers": 2,
    "total_goals": 8, "total_retarget_chains": 40, "total_chain_mappings": 12
  }
}
```
`IKRetargeter` entries additionally carry `source_ik_rig` / `target_ik_rig`.

### get_structure (IKRigDefinition)
```json
{
  "success": true,
  "operation": "get_structure",
  "rig_name": "IK_Mannequin",
  "asset_sub_type": "IKRigDefinition",
  "summary": {"solver_count": 1, "goal_count": 4, "retarget_chain_count": 28, "chain_mapping_count": 0},
  "skeleton_ref": "SK_Mannequin",
  "solvers": [{"export_index": 3, "solver_name": "FullBodyIK", "solver_type": "IKRigFBIKSolver", "root_bone": "pelvis", "enabled": true}],
  "goals": [{"goal_name": "hand_r_Goal", "bone_name": "hand_r"}],
  "retarget_chains": [{"chain_name": "RightArm", "start_bone": "clavicle_r", "end_bone": "hand_r"}]
}
```
For `IKRetargeter` assets, `get_structure` instead returns `source_ik_rig`,
`target_ik_rig`, `chain_mappings` (`[{source_chain, target_chain}]`), and
`retarget_poses` as objects (`pose_name`, `root_translation_offset`, `bone_rotation_offsets[]`, `bone_offset_count`) — same shape as `get_retargeter` / SKILL.md (#19960).

### get_solvers
```json
{
  "success": true,
  "operation": "get_solvers",
  "rig_name": "IK_Mannequin",
  "solvers": [{"export_index": 3, "solver_name": "FullBodyIK", "solver_type": "IKRigFBIKSolver", "root_bone": "pelvis", "enabled": true}],
  "solver_count": 1
}
```
`get_goals` returns `goals` (`[{goal_name, bone_name}]`) + `goal_count`;
`get_chains` returns `retarget_chains` (`[{chain_name, start_bone, end_bone}]`) +
`retarget_chain_count`; `get_retargeter` returns `source_ik_rig`, `target_ik_rig`,
`chain_mappings` + `chain_mapping_count`, and `retarget_poses` (objects: `pose_name` / offsets, not bare names).

### analyze_rig (IKRigDefinition)
```json
{
  "success": true,
  "operation": "analyze_rig",
  "asset_sub_type": "IKRigDefinition",
  "analysis": {
    "solver_count": 1, "goal_count": 4, "retarget_chain_count": 28, "chain_mapping_count": 0,
    "complexity_score": 50, "complexity_level": "medium",
    "solver_types": ["IKRigFBIKSolver"], "unique_solver_type_count": 1
  },
  "skeleton_ref": "SK_Mannequin"
}
```
`complexity_score = solvers*10 + goals*3 + chains + mappings`; level: `>50` high,
`>20` medium, else low. For `IKRetargeter` assets the `analysis` object instead
holds mapping-health stats (`chain_mapping_count`, `mapped_chain_count`,
`unmapped_chain_count`, `retarget_pose_count`, `has_source_ik_rig`,
`has_target_ik_rig`) plus the rig refs/mappings/poses.

### compare_rigs
```json
{
  "success": true,
  "operation": "compare_rigs",
  "rig_a": {"name": "IK_A", "path": "/Game/...", "asset_sub_type": "IKRigDefinition"},
  "rig_b": {"name": "IK_B", "path": "/Game/...", "asset_sub_type": "IKRigDefinition"},
  "comparison": {
    "solvers": {"a": 3, "b": 5, "diff": -2},
    "goals": {"a": 4, "b": 6, "diff": -2},
    "retarget_chains": {"a": 6, "b": 8, "diff": -2},
    "chain_mappings": {"a": 0, "b": 0, "diff": 0},
    "complexity_score": {"a": 48, "b": 76, "diff": -28}
  },
  "retarget_chains": {"only_in_a": ["Spine"], "only_in_b": ["Tail"], "shared": ["RightArm"]},
  "goals": {"only_in_a": [], "only_in_b": [], "shared": ["hand_r_Goal"]},
  "solver_types": {"only_in_a": [], "only_in_b": ["IKRig_LimbSolver"], "shared": ["IKRigFBIKSolver"]}
}
```
`diff` values are `a - b`.

## Disambiguation

| Query | This Skill | Direct MCP | Why |
|-------|-----------|------------|-----|
| "IK_X 분석" | Yes | — | Multi-step analysis workflow |
| "IK Rig 검색" | — | `ue_manage_rigging(search_rigs)` | Single operation |
| "Control Rig 분석" | — | `control-rig-workflow` skill | Different rig type |
