---
name: ue-world-build
model: opus
description: "Purpose: One-stop World Build orchestration. Sequentially runs the world-bake pipeline -- navigation -> HLOD -> grass/foliage -> lighting -- batching editor write commands on a single WebSocket via ue_batch_editor_operations. Answers 'build my world', 'bake the level', 'rebuild nav and HLOD and lighting'. Triggers: '월드 빌드', '월드 빌드해줘', '레벨 베이크', '내비 HLOD 라이팅 다시 빌드', 'world build', 'build the world', 'bake the level', 'rebuild nav HLOD lighting'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with a live Unreal Engine 5.x Editor (write orchestration). Read-only preflight/verify work offline."
argument-hint: "(no args = full pipeline) | --nav --hlod --grass --lighting (stage subset) | --map <WorldName> | --dry-run"
metadata:
  version: "1.0.1"
  author: "Next-Stage-Inc"
  issue: "#20039"
  epic: "#20023"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE World Build -- nav + HLOD + grass + lighting Orchestration

**Issue**: #20039 (Part of Epic #20023, editor Phase 2)
**Purpose**: "월드 빌드해줘" one command to run the full world-bake pipeline
sequentially -- navigation, HLOD, grass/foliage, lighting -- instead of driving
four separate tools by hand. Detailed op wiring: [REFERENCE.md](REFERENCE.md).

## Auto-Execution Instructions

> **CRITICAL**: when loaded, **execute the workflow** -- do NOT just print docs.

1. **Scope** = `$ARGUMENTS`. Empty -> full pipeline (all 4 stages, in order).
   Stage flags (`--nav`/`--hlod`/`--grass`/`--lighting`) select a subset;
   `--dry-run` previews the plan + per-stage availability without writing.
2. **Preflight** (Phase 0): health gate + world detection + baseline read.
3. **Run each selected stage in strict order** nav -> HLOD -> grass -> lighting.
4. **Verify** each built stage with its offline read-only inspector (Phase 5).
5. **Report** the unified world-build summary with per-stage status + next steps.

## Auto-Trigger Phrases

- **ko**: "월드 빌드", "월드 빌드해줘", "레벨 베이크", "내비 HLOD 라이팅 다시 빌드", "오픈월드 베이크"
- **en**: "world build", "build the world", "bake the level", "rebuild nav HLOD and lighting", "run the world bake pipeline"
- **NOT**: "월드 파티션 상태"/"HLOD 상태" -> `/ue-project-ops` (read-only status); "프로젝트 쿡/패키징" -> `/ue-build-automate`; "네비게이션 구조 분석" -> `ue_manage_navmesh` read. Positioning + eval cases: [REFERENCE.md](REFERENCE.md).

## Stage Availability (honest capability map)

Forward-declared orchestrator (Epic #20023). **Three stages have landed write
paths; grass/foliage still has no BUILD op** (authoring ops only). The skill runs
what is landed and **degrades gracefully** for what is not -- a pending stage is
`PENDING` with manual-editor fallback, never a hard fail.

| Stage | Write op today | Status | Verify op (offline) |
|-------|----------------|--------|---------------------|
| 1. Navigation | `ue_manage_navmesh(rebuild)` | **LANDED** (#20033) | `ue_manage_navmesh(get_structure)` |
| 2. HLOD | `ue_manage_project_ops(wp_build_hlod)` | **LANDED** | `ue_manage_project_ops(wp_get_hlod_status)` |
| 3. Grass/Foliage | *(no build op — authoring only: scatter/erase/set_*)* | **PENDING** (no #20032–#20036 grass-map BUILD op) | `ue_manage_foliage(get_foliage_instances)` |
| 4. Lighting | `ue_editor_automation(execute_python -> build_light_maps)` | **LANDED** | message log (`get_error_log`) |

> **Re-check availability at run time** (probe the tool schema / `list_operations`).
> Grass stays PENDING until a real grass-map / foliage **build** op lands — do not
> treat authoring ops as a build. Order: nav → HLOD → grass → lighting.
> [REFERENCE.md](REFERENCE.md).

## Workflow

### Phase 0: Preflight
```python
health = ue_check_health(compact=true)   # write stages need editor: online
# offline -> only read-only baseline/verify run; offer /launch-editor, do NOT bake.
ue_manage_project_ops(operation="wp_get_status", project_root="<project_path>")
# WP disabled -> HLOD stage is N/A (WP feature); note + continue, do NOT fail.
```
Resolve `world_name` from `--map`, else the currently open level.

### Phase 1: Navigation
```python
# Issue #20033 LANDED: rebuild is Editor-required and returns task_id for get_task_status.
ue_manage_navmesh(operation="rebuild", project_root="<project_path>")
# Poll background task if task_id is returned, then verify offline:
ue_manage_navmesh(operation="get_structure", project_root="<project_path>")
```
Fallback only if rebuild is unavailable offline / Editor down: `Build menu -> Build Paths`;
still verify with `ue_manage_navmesh(get_structure)`.

### Phase 2: HLOD (World Partition)
```python
ue_manage_project_ops(operation="wp_build_hlod", project_root="<project_path>",
                      world_name="<world_name>", timeout=<auto_or_user>)
```
Skip with an N/A note if Phase 0 found WP disabled.

### Phase 3: Grass / Foliage
```python
# No grass-map BUILD op exists (Issue #22947): foliage/landscape ops are authoring
# (scatter/erase/set_type_params/set_grass_type), not bake. Baseline read only:
ue_manage_foliage(operation="get_foliage_instances", project_root="<project_path>")
```
PENDING fallback (manual): `Foliage mode -> reapply procedural foliage`, or
`Build menu -> Build Grass Maps`; verify with `ue_manage_foliage(get_foliage_instances)`.

### Phase 4: Lighting (terminal stage)
```python
# The dedicated build_lighting op is a hidden wrapper (extended 120s timeout,
# not yet promoted to the public schema -- Issue #14349). Same underlying
# ULevelEditorSubsystem::BuildLightMaps() call via the schema-visible
# execute_python op:
ue_editor_automation(
    operation="execute_python", project_root="<project_path>",
    code="import unreal,json; ss=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem); "
         "r=ss.build_light_maps(unreal.LightingBuildQuality.QUALITY_PRODUCTION) if ss else False; "
         "print(json.dumps({'success': bool(r)}))",
    timeout_seconds=120,
)
```

### Batching (Issue #4313)
When **>= 3 landed** write commands run in one pass, batch them on one WebSocket
(69-79% faster). Use `send_command`/`execute_python` entries in pipeline order,
`stop_on_failure=True` (a failed structural bake must halt before lighting).
Never batch a PENDING stage. Full snippet: [REFERENCE.md](REFERENCE.md).

### No-Retry
If a stage write op fails on the FIRST call, do NOT retry with tweaked params:
report the stage `FAILED` with the tool error, **halt the pipeline** (later stages
depend on it), let the user decide.

### Phase 5: Verify + Report
Run each built stage's offline inspector -- `ue_manage_navmesh(get_structure)`,
`ue_manage_project_ops(wp_get_hlod_status)`, `ue_manage_foliage(get_foliage_instances)`,
lighting via editor message log -- and fold results into the report.

## Output Format

```text
=== World Build Report ===
World: <world_name>   Project: <project_name>   Editor: online | offline
Mode: full pipeline | subset (<stages>)

--- Pipeline (nav -> HLOD -> grass -> lighting) ---
  1. Navigation ...... OK       (ue_manage_navmesh rebuild, task polled)
  2. HLOD ............ OK       (wp_build_hlod, 42.3s; status: up-to-date)
  3. Grass/Foliage ... PENDING  (no grass-map BUILD op -- manual: Build Grass Maps)
  4. Lighting ........ OK       (build_lighting, 118.7s)

--- Verification (offline read-back) ---
  Nav: <N> RecastNavMesh / baked data: yes|no   HLOD: up-to-date|stale|missing
  Foliage: <N> instances / <M> types           Lighting: completed | log errors

--- Batching ---  <k> landed cmds on 1 WebSocket  (or "0 -- <3 landed, ran solo")
--- Next Steps ---
  1. Grass/foliage needs a real BUILD op (authoring ops are not a substitute)
  2. Re-run after any static-geometry edit (lighting goes stale)
```
Editor offline -> `=== World Build Report (PREFLIGHT ONLY) ===`: read-only baseline
only; start the Editor and re-run to bake. Error-recovery table: [REFERENCE.md](REFERENCE.md).

## References
- Epic #20023; nav rebuild #20033 (LANDED); grass build still open (Issue #22947)
- Landed ops: `ue_manage_navmesh(rebuild)`, `ue_manage_project_ops(wp_build_hlod)`,
  `ue_editor_automation(execute_python -> build_light_maps)`, `ue_batch_editor_operations`
- Read-only counterpart dashboard: `/ue-project-ops` -- Op wiring: [REFERENCE.md](REFERENCE.md)
