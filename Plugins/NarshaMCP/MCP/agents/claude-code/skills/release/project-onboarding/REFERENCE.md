# Project Onboarding — Reference

Version 1.1.0 | 4-phase workflow | 13 asset types | 4 MCP tools

---

## Table of Contents

- [Phase 1: Server & Environment Check](#phase-1-server--environment-check)
- [Phase 2: System Detection](#phase-2-system-detection)
- [Phase 3: C++ Architecture Sample](#phase-3-c-architecture-sample)
- [Phase 4: Recommendation Engine](#phase-4-recommendation-engine)
- [Asset Type Reference](#asset-type-reference)
- [Flags](#flags)
- [Error Recovery](#error-recovery)
- [Self-Test](#self-test)

---

## Phase 1: Server & Environment Check

### Tool Calls

```python
ue_check_health()
ue_cache_control(operation="get_stats")
```

### Key Response Fields

| Field | Source | Expected |
|-------|--------|----------|
| `status` | `ue_check_health` | `"healthy"` |
| `version` | `ue_check_health` | Server version string |
| `project_path` | `ue_check_health` | Absolute path to `.uproject` |
| `path_validation.status` | `ue_check_health` | `"valid"` |
| `symbol_index.loaded` | `ue_cache_control` | `true` |
| `symbol_index.symbol_count` | `ue_cache_control` | Integer (0 = no PDB) |
| `metadata.status` | `ue_cache_control` | `"loaded"` |
| `four_phase.complete` | `ue_cache_control` | `true` when indexing done |
| `four_phase.running` | `ue_cache_control` | `false` when idle |

### Decision Matrix

| Condition | Action |
|-----------|--------|
| `status != "healthy"` | STOP — report connection error |
| `path_validation.status != "valid"` | WARN — proceed with fallback `project_path` |
| `four_phase.running == true` | WARN — mark incomplete counts as `?` |

---

## Phase 2: System Detection

### Tool Call (per system)

```python
ue_search_assets(asset_type="<AssetType>", query="*")
```

All 13 calls are independent — execute in parallel.

### Interpretation

| Condition | Mark | Meaning |
|-----------|------|---------|
| `count > 0` | `[✅]` | System detected |
| `count == 0` AND indexing complete | `[❌]` | System not used |
| `count == 0` AND indexing incomplete | `[❓]` | Unknown |
| Search call fails | `N/A` | Continue with remaining types |

---

## Phase 3: C++ Architecture Sample

### Tool Call

```python
ue_analyze_symbols(operation="search_symbols", params={"query": "A*", "limit": 20})
```

### Complexity Classification

| Actor Count | Classification | Recommendation Bias |
|-------------|---------------|---------------------|
| 0 | Blueprint-only | BP tools, `/bp-cpp-project-init` |
| 1-10 | Small C++ | Basic C++ + BP workflow |
| 11+ | Heavy C++ | Advanced: hierarchy tracing, refactoring, test gen |

---

## Phase 4: Recommendation Engine

### System-to-Tool Mapping

| Detected System | Skills | MCP Tools |
|-----------------|--------|-----------|
| GAS (ability/effect) | `/gas-development`, `/ue-execution-trace` | `ue_manage_gameplay(smart)` |
| PCG | `/pcg-workflow` | `ue_manage_pcg(smart)` |
| Niagara | — | `ue_manage_niagara(smart)` |
| Materials | `/material-analysis` | `ue_manage_material(smart)` |
| AI (StateTree/BT) | `/game-design-intelligence` | `ue_manage_ai(smart)` |
| Blueprints | `/blueprint-flow` | `ue_manage_blueprint(smart)` |
| Enhanced Input | `/ue-execution-trace` | `ue_manage_gameplay(smart, tag="InputTag.*")` |
| Level Sequences | `/ue-sequencer-cinematics` | `ue_sequencer_structure(smart)` |
| Control Rig | `/control-rig-workflow` | `ue_manage_rigging(smart, rig_type="control_rig")` |
| IK Rig | `/ue-ik-rig-analysis` | `ue_manage_rigging(smart, rig_type="ik_rig")` |
| Animation | `/animation-workflow` | `ue_search_assets(asset_type="animation")` |
| Always | `/ue-debug`, `/ue-audit`, `/ue-pre-commit` | `ue_fix_errors(smart)` |

### Output Priority

1. **Priority 1** — Tools for detected systems
2. **Priority 2** — Always-useful tools (debug, audit, pre-commit)
3. **Priority 3** — C++ tools (if heavy C++ detected)

### "Try It Now" Commands

Generate 3 copy-pasteable MCP tool calls based on the top detected systems.

---

## Asset Type Reference

| # | System | `asset_type` Value | Prefix |
|---|--------|-------------------|--------|
| 1 | GAS (Abilities) | `gameplayability` | `GA_` |
| 2 | GAS (Effects) | `gameplayeffect` | `GE_` |
| 3 | PCG | `pcg` | `PCG_` |
| 4 | Enhanced Input | `inputaction` | `IA_` |
| 5 | Niagara | `NiagaraSystem` | `NS_` |
| 6 | Materials | `material` | `M_`, `MI_` |
| 7 | Blueprints | `blueprint` | `BP_` |
| 8 | Animation | `animation` | `AM_`, `ABP_` |
| 9 | AI (StateTree) | `statetree` | `ST_` |
| 10 | AI (BehaviorTree) | `behaviortree` | `BT_` |
| 11 | Cinematics | `levelsequence` | `LS_` |
| 12 | Control Rig | `controlrig` | `CR_` |
| 13 | IK Rig | `ikrig` | `IK_` |

---

## Flags

| Flag | Effect |
|------|--------|
| (no args) | Full onboarding — all 4 phases |
| `--quick` | Phase 1 + 2 only — skip C++ architecture sampling |
| `--self-test` | Validate skill structure — PASS/FAIL (no MCP calls) |

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `ue_check_health` fails | MCP server not connected | Stop — check `.mcp.json` path and restart |
| `path_validation` invalid | Wrong project path | Warn — proceed with fallback path |
| All searches return 0 | Empty project or metadata not loaded | Check indexing status; suggest re-run after completion |
| `ue_analyze_symbols` empty | No PDB or no C++ classes | Report BP-only; skip Phase 3 |
| Individual search fails | Unsupported type or server error | Mark N/A; continue remaining |
| Timeout (>100K assets) | Large project | Suggest `--quick` mode |

---

## Self-Test

`/project-onboarding --self-test` validates skill integrity without executing the full workflow:

| # | Check | What |
|---|-------|------|
| 1 | Structure | SKILL.md, REFERENCE.md, EXAMPLES.md exist |
| 2 | Frontmatter | Required fields: `name`, `description`, `argument-hint`, `metadata.version` |
| 3 | Routing | `routing.json` entry exists with matching trigger patterns |
| 4 | Asset types | All 13 asset types in SKILL.md Phase 2 table are valid `ue_search_assets` types |
| 5 | Recommendation table | Every detected system maps to at least one skill or MCP tool |

**Output**: PASS / FAIL with per-check results.
