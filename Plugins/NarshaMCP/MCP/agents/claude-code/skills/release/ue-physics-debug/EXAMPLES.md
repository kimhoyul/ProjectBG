# ue-physics-debug — Examples

## Example 1 — PhysicsAsset 일반 분석

**User**: `PA_Mannequin 분석해줘`

**Steps**:

1. `ue_manage_physics(operation="get_body_names", physics_asset="PA_Mannequin")`
   → `bone_names: ["pelvis", "spine_01", "spine_02", "head", "thigh_l", "thigh_r", "calf_l", "calf_r"]` (8 bodies)
2. `ue_manage_physics(operation="get_body_properties", physics_asset="PA_Mannequin")`
   → 8 entries; `pelvis` is `Default`, `head` is `Kinematic`, the rest `Default`.
3. `ue_manage_physics(operation="get_constraints", physics_asset="PA_Mannequin")`
   → 7 constraints; spine_01↔pelvis, spine_02↔spine_01, head↔spine_02, etc.

**Report**:

```markdown
## Physics 디버깅 결과 — PA_Mannequin

### Body / Constraint 요약
- **Bodies**: 8 (pelvis, spine_01-02, head, thigh_l/r, calf_l/r)
- **Constraints**: 7 (full skeletal chain pelvis→head + legs)
- **Simulation 모드**: Default 7, Kinematic 1 (head), Simulated 0

### 주목할 발견
- ✅ Constraint 7개로 8 bodies 연결 — 트리 구조 정상
- ⚠️ `head` body가 Kinematic — 의도된 설정인지 확인 (보통 ragdoll head는 Default)

### 다음 단계 제안
- shape 상세는: `ue_manage_physics(get_body_shapes, physics_asset="PA_Mannequin", bone_name="pelvis")`
- 변경 필요 시: `ue_manage_physics(set_body_shape, ...)` 등 authoring op (#16314) — 기본 `dry_run=true` 미리보기, 적용은 `dry_run=false` (live Editor 필요)
```

---

## Example 2 — 특정 본의 shape 검사

**User**: `PA_Mannequin pelvis body 어떤 shape이야?`

**Steps**:

1. `ue_manage_physics(operation="smart", physics_asset="PA_Mannequin", bone_name="pelvis")`
   → smart routes to `get_body_shapes`
2. → `data: [{bone_name: "pelvis", shapes: [{shape_type: "Sphere", radius: 12.0, center: [0,0,0], shape_name: ""}]}]`

**Report**:

```markdown
## PA_Mannequin / pelvis body — Shape 분석

| Type | Center (cm) | Size |
|---|---|---|
| Sphere | (0, 0, 0) | radius=12.0 |

✅ Single sphere — typical pelvis bullet collision.
```

---

## Example 3 — Constraint limit 검사

**User**: `PA_Mannequin constraint 한도 확인해줘`

**Steps**:

1. `ue_manage_physics(operation="smart", physics_asset="PA_Mannequin", constraints=true)`
   → smart routes to `get_constraints`

**Report**:

```markdown
## PA_Mannequin — Constraint Limits

| Joint | bone1↔bone2 | Swing1 | Swing2 | Twist |
|---|---|---|---|---|
| spine_01 | spine_01↔pelvis | Limited(30°) | Limited(30°) | Free(45°) |
| spine_02 | spine_02↔spine_01 | Limited(20°) | Limited(20°) | Free(30°) |
| head | head↔spine_02 | Limited(45°) | Limited(45°) | Limited(60°) |
| thigh_l | thigh_l↔pelvis | Limited(90°) | Limited(45°) | Limited(45°) |
| ... | ... | ... | ... | ... |

⚠️ `head` twist는 Limited 60° — 머리 회전이 제한적. 사람 목 가동 범위 (~80°)와 비교 시 보수적.
```

---

## Example 4 — PhysicalMaterial 속성

**User**: `PM_Concrete 마찰 얼마야?`

**Steps**:

1. `ue_manage_physics(operation="smart", material_name="PM_Concrete")`
   → smart routes to `get_physical_material_properties`

**Report**:

```markdown
## PhysicalMaterial — PM_Concrete

| Property | Value | UE Default | Note |
|---|---|---|---|
| Friction | 0.95 | 0.7 | 거친 표면 — 미끄러짐 거의 없음 |
| Restitution | 0.05 | 0.3 | 거의 안 튐 |
| Density | 2.4 | 1.0 | 약 2.4×기본 — 콘크리트 무게 |
| Friction Combine Mode | Max | Average | 두 표면 중 큰 마찰 |
| Restitution Combine Mode | Min | Average | 두 표면 중 작은 탄성 |
```

---

## Example 5 — 디스커버리 (target 미지정)

**User**: `이 프로젝트의 물리 에셋 어떤 거 있어?`

**Steps**:

1. `ue_manage_physics(operation="search_physics_assets", query="*")`
   → `total_found: 12`, `results: [...]`
2. `ue_manage_physics(operation="search_physical_materials", query="*")`
   → `total_found: 6`, `results: [...]`

**Report**:

```markdown
## 프로젝트 Physics 에셋 인벤토리

### PhysicsAssets (12)
| Name | Path |
|---|---|
| PA_Mannequin | /Game/Characters/Mannequin/PA_Mannequin |
| PA_Hero | /Game/Characters/Hero/PA_Hero |
| PA_Enemy_Standard | /Game/Enemies/Standard/PA_Enemy_Standard |
| ... | ... |

### PhysicalMaterials (6)
| Name | Friction | Restitution | Density |
|---|---|---|---|
| PM_Default | 0.7 | 0.3 | 1.0 |
| PM_Concrete | 0.95 | 0.05 | 2.4 |
| PM_Ice | 0.05 | 0.3 | 0.9 |
| PM_Wood | 0.6 | 0.4 | 0.7 |
| PM_Metal | 0.4 | 0.5 | 7.8 |
| PM_Rubber | 0.85 | 0.85 | 1.2 |

### 다음 단계
어떤 에셋을 자세히 볼까요? (예: `PA_Hero 분석`, `PM_Ice 마찰 확인`)
```

---

## Example 6 — Collision profile 분석

**User**: `ECC_GameTraceChannel1 어디 쓰여?`

**Steps**:

1. `ue_manage_physics(operation="analyze_collision_profiles")` → `channel_aliases{ECC_GameTraceChannel1→alias}` + `custom_channels[]` + profile `response_matrix` in one call (#18102 walker)
2. `ue_grep(query="ECC_GameTraceChannel1", domain="source")` → C++ usage list

**Report**:

````markdown
## Collision Channel — ECC_GameTraceChannel1

### Definition (Config/DefaultEngine.ini)
```ini
[/Script/Engine.CollisionProfile]
+DefaultChannelResponses=(Channel=ECC_GameTraceChannel1, Name="Weapon", DefaultResponse=ECR_Ignore, ...)
```

### 사용처 (5 hits)
- `Source/MyGame/Weapons/Projectile.cpp:42` — `ECC_GameTraceChannel1` 채널로 트레이스
- `Source/MyGame/Characters/HeroCharacter.cpp:128` — `SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block)`
- ...

### 의미
"Weapon" 채널 — 무기 시스템이 캐릭터 hit detection에 사용하는 trace channel.
````

---

## Example 7 — Edge case: 빈 PhysicsAsset

**User**: `PA_Empty 분석`

**Steps**:

1. `ue_manage_physics(operation="get_body_names", physics_asset="PA_Empty")`
   → `success: true, bone_names: [], note: "No bodies parsed..."`

**Report**:

```markdown
## PA_Empty — 빈 PhysicsAsset

⚠️ Bodies 0개. SkeletalBodySetups[]가 비어있거나, .uasset의 parsed_exports가 sparse입니다.

### 가능한 원인
1. PhysicsAsset이 막 생성되어 아직 body가 추가되지 않음
2. 인덱싱 미완 → `ue_check_health()` 확인
3. 에셋이 손상됨 → Editor에서 직접 열어 확인

### 다음 단계
`narshamcp launch-editor` → Editor에서 직접 검사 권장
```
