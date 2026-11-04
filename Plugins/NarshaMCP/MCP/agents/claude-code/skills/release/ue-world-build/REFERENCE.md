# ue-world-build -- Reference

Cold companion to [SKILL.md](SKILL.md) (Issue #20039, Epic #20023): op signatures,
availability probing, batching, ordering rationale, positioning, error recovery,
and eval cases. The hot execution workflow lives in SKILL.md.

## Skill Positioning

| Skill | Scope | Direction |
|-------|-------|-----------|
| `/ue-project-ops` | WP / DDC / SC **status** dashboard | Read-only |
| `/ue-build-automate` | C++ compile / cook / package / commandlet | Build system |
| **`/ue-world-build`** | **World-bake pipeline (nav + HLOD + grass + lighting)** | **Editor write orchestration** |

This skill is the **write** counterpart to the read-only `/ue-project-ops` WP
dashboard: it issues editor write commands that mutate baked world data (navmesh,
HLOD proxies, foliage instances, lightmaps).

## Stage -> op wiring

| Stage | Write op (build) | Verify op (offline read) | Availability |
|-------|------------------|--------------------------|--------------|
| 1. Navigation | nav build write op | `ue_manage_navmesh(operation="get_structure")` | PENDING #20033 |
| 2. HLOD | `ue_manage_project_ops(operation="wp_build_hlod", world_name=..., timeout=...)` | `ue_manage_project_ops(operation="wp_get_hlod_status")` | LANDED |
| 3. Grass/Foliage | foliage authoring write op | `ue_manage_foliage(operation="get_foliage_instances")` | PENDING #20032~#20036 |
| 4. Lighting | `ue_editor_automation(operation="execute_python", code="...build_light_maps(...)")` (build_lighting is a hidden, schema-unpromoted wrapper -- Issue #14349) | editor message log / `ue_editor_automation(operation="get_error_log")` | LANDED |

## Availability probing (do not trust the table blindly)

A pending stage may have landed since v1.0.0. Before emitting the PENDING
fallback, probe the live tool schema for the build op's enum value on the tool
that OWNS that stage -- a nav/foliage build op would appear on `ue_manage_navmesh`
/ `ue_manage_foliage` (or a new dedicated tool), NOT on `ue_manage_project_ops`.
Prefer a cheap per-tool `list_operations` probe (e.g.
`ue_manage_project_ops(operation="list_operations")` for the HLOD stage, and the
equivalent op-enum/schema check on the navmesh/foliage tool for those stages) so a
newly landed op is picked up automatically without a code change to this skill.
Only if the op is genuinely absent, fall back to the manual-editor step +
read-only baseline capture.

Rule 20 (Offline Binary First): the read-only verify ops (`ue_manage_navmesh`,
`ue_manage_foliage`, `wp_get_hlod_status`) all work with the Editor offline, so
Phase 0 baseline and Phase 5 verification run headless; only the Phase 1-4 bakes
require a live Editor.

## Why This Order (nav -> HLOD -> grass -> lighting)

- **Navigation first**: navmesh is generated from static collision geometry;
  build it before HLOD so the nav bake sees final placed geometry.
- **HLOD second**: HLOD proxies cluster/simplify static meshes; run after nav so
  proxy generation does not perturb the geometry nav just consumed.
- **Grass/foliage third**: procedural foliage is scattered onto the (now final)
  landscape/geometry; it is non-navigable and non-occluding, so it follows the
  structural bakes.
- **Lighting last**: lightmaps must bake after all static geometry, HLOD proxies,
  and foliage exist, else the bake is stale the moment an earlier stage changes a
  mesh. Lighting is always the terminal stage of a world bake.

## Batching (`ue_batch_editor_operations`, Issue #4313)

Batch only when **>= 3 landed** write commands run in one pass (69-79% faster):

```python
ue_batch_editor_operations(
    operations=[
        {"type": "send_command", "command": "<hlod_cmd>",     "params": {...}},
        {"type": "send_command", "command": "<lighting_cmd>", "params": {...}},
        # ...one entry per landed stage command, in pipeline order
    ],
    stop_on_failure=True,   # a failed structural bake must halt the pipeline
)
```

- Each entry is `{"type": "send_command"|"execute_python", "command"/"code": ..., "params": {...}}`.
- `stop_on_failure=True` so a failed nav/HLOD/grass bake does not bake lighting onto stale geometry.
- Never place a PENDING stage in the batch; emit its manual fallback instead.
- Array order IS execution order on the single WebSocket (nav -> hlod -> grass -> lighting).

## World Partition gating

`wp_build_hlod` is a World Partition feature. If Phase 0
`ue_manage_project_ops(operation="wp_get_status")` reports WP disabled (standard
level streaming), mark the HLOD stage **N/A** and continue -- nav, grass, and
lighting still apply to non-WP levels.

## MCP Call Budget

Target 6-12 calls per full run (fewer for a subset): Phase 0 health(1) +
wp_get_status(0-1); Phase 1 nav(1); Phase 2 wp_build_hlod(1, long-running --
pass `timeout`); Phase 3 foliage(1); Phase 4 execute_python -> build_light_maps(1);
Phase 5 verify inspectors(1-3, one per built stage).

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_check_health` editor offline | No live Editor | Report baseline-only; offer `/launch-editor`; do NOT attempt write ops |
| `wp_get_status` "not enabled" | Project is not World Partition | Mark HLOD N/A, continue nav/grass/lighting |
| `wp_build_hlod` timeout | Very large world | Re-run that stage alone with a higher `timeout`; report partial |
| Nav / grass write op absent | Dependency (#20033 / #20032~#20036) not landed | Manual-editor fallback + read-only baseline; mark PENDING (not FAILED) |
| A landed stage write op FAILS | Editor RC error, bad geometry | Halt pipeline (later stages depend on it), report FAILED, No-Retry |
| Batch partial failure | One command in batch failed | `stop_on_failure=True` already halted; report which command failed + skipped |

## Evaluation Criteria

**Positive (SHOULD trigger)**: "월드 빌드해줘" (full); "build the world" (full);
"내비랑 HLOD랑 라이팅 다시 빌드해줘" (nav+hlod+lighting subset); "bake the level"
(full); "run the world bake pipeline" (full).

**Negative (should NOT trigger)**: "월드 파티션 상태 확인" -> `/ue-project-ops`;
"HLOD 상태 보여줘" -> `/ue-project-ops --wp`; "프로젝트 쿡해줘" ->
`/ue-build-automate`; "네비게이션 구조 분석" -> `ue_manage_navmesh` read via
`/ue-explain`; "라이팅 시나리오 에셋 만들어" -> `/ue-create-system`.

## Related

- Read-only WP/DDC/SC status dashboard: `/ue-project-ops`
- C++ compile / cook / package: `/ue-build-automate`
- Navmesh inspection detail: `ue_manage_navmesh` (Issue #9398)
- Foliage/Mass Entity inspection detail: `ue_manage_foliage`
