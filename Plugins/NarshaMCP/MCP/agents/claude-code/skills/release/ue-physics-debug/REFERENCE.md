# ue-physics-debug — Reference

## Tool surface

`ue_manage_physics` — 26 enum entries / 25 distinct ops (`auto`=`smart` 별칭): 13 read (smart/auto 라우터 + 9 offline binary-parse reads + `validate_physics_asset` + `compute_mass_properties`) + 2 live-PIE runtime ops + 11 live-Editor authoring ops. Issues #9331 (read) / #9996 (chaos) / #18102 (collision) / #18108 (validate) / #18112 (mass) / #18114·#18115 (runtime) / #16314·#18110·#18111·#18113 (authoring):

| Op | Required args | Optional args | Returns |
|---|---|---|---|
| `smart` | one of `physics_asset` / `material_name` / `query` / `pattern` | `bone_name`, `constraints`, `collision_profile` | Auto-routes |
| `search_physics_assets` | — | `query` (default `"*"`), `limit` | `results[]: {name,path,asset_type,class_name}`, `total_found` |
| `get_body_names` | `physics_asset` | — | `bone_names: [string]` |
| `get_body_shapes` | `physics_asset` | `bone_name` (filter), `limit`, `offset` | `data[]: {bone_name, shapes[]}` |
| `get_body_properties` | `physics_asset` | `bone_name` (filter), `limit`, `offset` | `data[]: {bone_name, physics_type, mass_scale}` |
| `get_constraints` | `physics_asset` | `bone_name` (filter), `limit`, `offset` | `data[]: {joint_name, bone1_name, bone2_name, swing1_motion, swing1_limit_degrees, swing2_*, twist_*}` |
| `search_physical_materials` | — | `query`, `limit` | `results[]`, `total_found` |
| `get_physical_material_properties` | `material_name` | — | `friction, restitution, density, raise_mass_to_power, friction_combine_mode, restitution_combine_mode, destructible_damage_threshold` |
| `analyze_collision_profiles` | — | — | `profiles[]`, `deprecated_profiles[]`, `custom_channels[]`, `channel_aliases{}`, `response_matrix{}` (sparse), `profile_redirects[]`, `channel_redirects[]` (12-layer INI cascade walker, #18102; requires engine path) |
| `get_chaos_settings` | — | `chaos`/`chaos_solver` (smart 진입), `overridden_only`, `summary` | `p.Chaos.*` CVars + `[/Script/ChaosSolverEngine.ChaosSolverSettings]` INI cascade (#9996) |
| `validate_physics_asset` | `physics_asset` | — | `{valid, error_count, warning_count, findings[], summary{bodies, constraints, rules_checked:6, constraints_source}}` — offline PA/ragdoll 구조 검증, 6 deterministic rules, explicit-only (#18108; 규칙 상세는 [ue-physics-audit/REFERENCE.md](../ue-physics-audit/REFERENCE.md)) |
| `compute_mass_properties` | `physics_asset` | `density` (g/cm³, 기본 1.0; `material_name` 우선 override) | per-body & total mass / COM / inertia tensor (shape geometry + density), offline, explicit-only (#18112) |
| `runtime_collision_query` ‡ | `query_type` (`line_trace`/`sweep`/`overlap`) + `start`/`end` (line_trace/sweep) 또는 `location` (overlap) | `shape`, `radius`, `box_extent`, `trace_channel`, `multi`, `trace_complex`, `actors_to_ignore`, `object_types` | live PIE sweep/overlap/line-trace hits (Editor RC WS Python; #18114) |
| `get_runtime_body_state` ‡ | `actor` | `component`, `bone_name` | live body transform / linear+angular velocity / sleep state / runtime mass+COM (solver 결과, asset 설정 아님; #18115) |
| `create_from_mesh` † | `mesh` | `assign_to_mesh`, `dry_run` | created `<mesh>_PhysicsAsset` path |
| `set_body_shape` † | `physics_asset`, `bone`, `shape` (sphere/capsule/box) | `shape_name`, `radius`, `length`, `box_extent`, `center`, `rotation`, `dry_run` | planned/applied body-shape 변경 |
| `add_constraint` † | `physics_asset`, `bone_a`, `bone_b` | `dry_run` | planned/applied new constraint |
| `set_constraint` † | `physics_asset`, `bone_a`, `bone_b` | `swing1_limit`, `swing2_limit`, `twist_limit`, `swing1/2_motion`, `twist_motion`, `dry_run` | planned/applied constraint limits |
| `remove_constraint` † | `physics_asset`, `bone_a`, `bone_b` | `dry_run` | planned/applied constraint 제거 |
| `set_body_physics_mode` † | `physics_asset`, `bone`, `mode` (`Default`/`Kinematic`/`Simulated`) | `dry_run`, `skip_bone_validation` | planned/applied body physics-mode 변경 (#18110) |
| `set_body_mass_scale` † | `physics_asset`, `bone`, `mass_scale` (finite, >0) | `dry_run` | planned/applied body mass-scale 변경 (#18110) |
| `add_body` † | `physics_asset`, `bone` (body-less bone) | `dry_run`, `skip_bone_validation` | planned/applied new body (#18111) |
| `remove_body` † | `physics_asset`, `bone` (body 보유 bone) | `dry_run` | planned/applied body 제거 + 참조 constraint cascade (#18111) |
| `remove_shape` † | `physics_asset`, `bone`, `shape_name` (non-empty) | `dry_run` | planned/applied named primitive 제거 (#18111) |
| `set_physical_material_properties` † | `material_name` + `friction`/`restitution`/`density` 중 1+ | `dry_run` | planned/applied PhysicalMaterial write (UE reflection, not toolset; #18113) |

† authoring op — live Editor RC, explicit-only (smart 라우팅 미도달), `dry_run=true` 기본. ‡ runtime op — live PIE query, explicit-only, Editor-off/PIE-off는 구조화 응답으로 graceful degrade. 상세: § Read / Runtime / Authoring ops.

## Smart routing priority (first match wins)

디스패처 SSOT: `manage_physics.rs` `dispatch_ops!` 매치 테이블 (L111-155) + smart 우선순위 로직 `tool_manage_physics_smart` (L177).

1. `material_name` (concrete) + `query`/`pattern` → `search_physical_materials`
2. `material_name` (concrete, alone) → `get_physical_material_properties`
3. `material_name == "*"` → `search_physical_materials` (wildcard)
4. `collision_profile=true` or `analyze_collision=true` → `analyze_collision_profiles`
5. `chaos=true` or `chaos_solver=true` → `get_chaos_settings`
6. `physics_asset == "*"` → `search_physics_assets` (wildcard)
7. `physics_asset` (concrete) + `bone_name` → `get_body_shapes`
8. `physics_asset` (concrete) + `constraints=true` → `get_constraints`
9. `physics_asset` (concrete, alone) → `get_body_names`
10. `query`/`pattern` alone → `search_physics_assets`
11. None → error with hint

## Data shape examples

### Body

```json
{
  "bone_name": "spine_01",
  "physics_type": "Kinematic",
  "mass_scale": 0.5,
  "shapes": [
    {
      "shape_type": "Capsule",
      "shape_name": "spine_capsule",
      "center": [0.0, 0.0, 8.0],
      "rotation": [0.0, 0.0, 0.0],
      "radius": 8.0,
      "length": 16.0,
      "extent_x": 0.0,
      "extent_y": 0.0,
      "extent_z": 0.0
    }
  ]
}
```

`shape_type` ∈ `Sphere | Capsule | Box | Convex | TaperedCapsule | LevelSet | Other`. Box uses `extent_x/y/z`; Sphere uses `radius` only; Capsule uses `radius` + `length`.

### Constraint

```json
{
  "joint_name": "spine_pelvis",
  "bone1_name": "spine_01",
  "bone2_name": "pelvis",
  "swing1_motion": "Limited",
  "swing1_limit_degrees": 30.0,
  "swing2_motion": "Limited",
  "swing2_limit_degrees": 30.0,
  "twist_motion": "Free",
  "twist_limit_degrees": 45.0
}
```

`*_motion` ∈ `Free | Limited | Locked`. `bone1` is the child bone; `bone2` is the parent.

### PhysicalMaterial

```json
{
  "friction": 0.95,
  "restitution": 0.05,
  "density": 2.4,
  "raise_mass_to_power": 0.75,
  "friction_combine_mode": "Max",
  "restitution_combine_mode": "Min"
}
```

Combine modes ∈ `Average | Min | Multiply | Max`.

## Read / Runtime / Authoring ops

**Read ops (offline, 13)**: PhysicsAsset / PhysicalMaterial / collision 조회는 Editor 없이 동작 (Rule 20) — Epic `UPhysicsAssetToolset` 5 reads 미러 + 6 NarshaMCP-exclusive (PhysicalMaterial × 2 바이너리 파싱, CollisionProfile 12-layer INI cascade walker #18102, Chaos solver walker #9996, `validate_physics_asset` 구조 검증 #18108, `compute_mass_properties` 오프라인 질량 계산 #18112). `validate_physics_asset` / `compute_mass_properties`는 explicit-only (smart 미도달).

**Runtime ops (live PIE, 2)**: `runtime_collision_query` (sweep/overlap/line-trace, #18114) / `get_runtime_body_state` (live body transform·속도·sleep·mass·COM = solver 결과, asset 설정 아님, #18115) — Editor RC WebSocket Python, read-only, explicit-only (smart 미도달). Editor-off / PIE-off는 각각 구별되는 구조화 응답으로 graceful degrade.

**Authoring ops (구현 완료, 11, #16314/#18110/#18111/#18113)**: PhysicsAsset — `create_from_mesh` / `set_body_shape` / `set_body_physics_mode` / `set_body_mass_scale` / `add_body` / `remove_body` / `remove_shape` / `add_constraint` / `set_constraint` / `remove_constraint` (Epic PhysicsToolsets 기반). PhysicalMaterial — `set_physical_material_properties` (UE reflection write, toolset 불필요, #18113). live Editor RC 필요, explicit-only (smart 라우팅으로 도달 불가). 기본 `dry_run=true` (검증 + 계획만, mutation 없음); `dry_run=false`면 op 실행 + asset 저장 + binary read-back 검증 (#17304). Body / shape 추가·제거(`add_body` / `remove_body` / `remove_shape`)는 #18111에서 구현 완료 (이전 "미구현" 서술 제거).

## Engine source mapping

| NarshaMCP field | Engine source |
|---|---|
| `bodies[].bone_name` | `UBodySetupCore::BoneName` |
| `bodies[].physics_type` | `UBodySetupCore::PhysicsType` (`EPhysicsType`) |
| `bodies[].mass_scale` | `UBodySetup::MassScale` |
| `bodies[].shapes[]` | `UBodySetup::AggGeom` (`FKAggregateGeom`) |
| `constraints[]` | `UPhysicsConstraintTemplate::DefaultInstance` (`FConstraintInstance`) |
| `material[].friction` | `UPhysicalMaterial::Friction` |
| `material[].restitution` | `UPhysicalMaterial::Restitution` |
| `material[].density` | `UPhysicalMaterial::Density` |

## Composability cheatsheet

| You want | Tool chain |
|---|---|
| "Who uses PA_Mannequin?" | `/ue-asset-discovery PA_Mannequin --refs` |
| "이 PA의 모든 정보" | `ue_manage_physics(smart, physics_asset=...)` × 4 (body_names, body_properties, get_constraints, then per-bone shapes) |
| "Compare two PhysicsAssets" | `get_body_names` × 2, diff manually |
| "Find PA referencing PM_Concrete" | `ue_grep("PM_Concrete")` then filter to PA `.uasset` paths |
| "Chaos solver settings" | `ue_manage_physics(get_chaos_settings)` (#9996); config 원본 대조는 `ue_analyze_config(search_config, query="p.Chaos")` |

## Skill triggers (routing.json)

Positive: `물리 디버깅`, `physics debug`, `PhysicsAsset 분석`, `PA_*`, `PM_*`, `constraint 검사`, `joint limit`, `friction 마찰`, `restitution 탄성`, `density 밀도`, `collision profile`, `ECC_GameTraceChannel`, `ragdoll`, `kinematic vs simulated`.

Negative (do NOT trigger): `Niagara physics binding` (use `ue_manage_niagara`), `vehicle physics setup` (out of scope), `cloth simulation` (use `ue_manage_animation`), `chaos destruction` (현재 미지원 — 별도 op 없음).
