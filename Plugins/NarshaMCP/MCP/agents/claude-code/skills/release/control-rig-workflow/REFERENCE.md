# Control Rig Workflow — Reference

**Version**: 1.0.0 | **3-Tool Orchestration**

---

## Tool Matrix

| Tool | Role | When Used |
|------|------|-----------|
| `ue_manage_rigging` | Rig structure, nodes, comparison | All workflows |
| `ue_search_assets` | Asset discovery by type | Audit workflow (find all rigs) |
| `ue_analyze_symbols` | C++ source lookup | C++ integration workflow |

All `ue_manage_rigging` calls use `rig_type="control_rig"`.

## Workflow Operations

### Workflow 1: Complexity Audit
```
search_rigs(query="*") → analyze_rig(each) → sort by complexity → report
```

| Step | Tool | Operation | Params |
|------|------|-----------|--------|
| 1 | `ue_manage_rigging` | `search_rigs` | `rig_type="control_rig"`, `query="*"` |
| 2 | `ue_manage_rigging` | `analyze_rig` | `rig_type="control_rig"`, `rig_name=<each>` |

### Workflow 2: A/B Comparison
```
compare_rigs(A, B) → get_structure(A) → get_structure(B) → diff report
```

| Step | Tool | Operation | Params |
|------|------|-----------|--------|
| 1 | `ue_manage_rigging` | `compare_rigs` | `rig_a=<A>`, `rig_b=<B>` |
| 2 | `ue_manage_rigging` | `get_structure` | `rig_name=<A>` (detail) |
| 3 | `ue_manage_rigging` | `get_structure` | `rig_name=<B>` (detail) |

### Workflow 3: C++ Integration
```
get_nodes(rig) → extract FRigUnit_* → search_symbols(each) → source map
```

| Step | Tool | Operation | Params |
|------|------|-----------|--------|
| 1 | `ue_manage_rigging` | `get_nodes` | `rig_type="control_rig"`, `rig_name=<name>` |
| 2 | `ue_analyze_symbols` | `search_symbols` | `query=<FRigUnit_Type>` |

## Disambiguation

| Query | This Skill | Other |
|-------|-----------|-------|
| "전체 리그 복잡도 감사" | Yes (batch audit) | — |
| "CR_A vs CR_B 비교" | Yes (A/B comparison) | — |
| "CR_X 구조 보여줘" | — | `ue_manage_rigging(get_structure)` directly |
| "IK Rig 분석" | — | `ue-ik-rig-analysis` skill |
| "리그에 노드 추가" | — | `ue_manage_rigging(add_node)` directly |
