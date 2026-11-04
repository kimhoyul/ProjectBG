---
name: ue-physics-debug
model: opus
description: "Purpose: Physics debugging via ue_manage_physics + ue_analyze_config. Answers '이 PA에 어떤 body / constraint?', 'PM 마찰/탄성?', '어떤 collision profile이 정의?', 'simulated vs kinematic?'. Triggers: '물리 디버깅', 'physics debug', 'PhysicsAsset 분석', 'PhysicalMaterial 마찰', 'collision profile', 'ragdoll', 'constraint 검사'."
compatibility: "Requires NarshaMCP MCP server with Unreal Engine 5.x C++ project"
argument-hint: "asset name or pattern, optional --bone <bone_name>, --constraints, --material <PM_name>, --collision"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  issue: "9331"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Physics Debug

**Issue**: #9331 (Physics 도메인 커버리지 — Epic PhysicsToolsets 대응)
**Purpose**: PhysicsAsset / PhysicalMaterial / Collision profile 디버깅 워크플로우 자동화

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation — actually call the MCP tools!

### Execution Requirements

1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask: which PhysicsAsset (PA_*), PhysicalMaterial (PM_*), or collision setup?
2. **Detect intent** from arguments + natural language:
   - PA_* / "ragdoll" / "constraint" / "body" → `ue_manage_physics` PhysicsAsset ops
   - PM_* / "friction" / "마찰" / "탄성" → `ue_manage_physics` PhysicalMaterial ops
   - "collision profile" / "channel" / "ECC_*" → `ue_manage_physics(analyze_collision_profiles)` + `ue_analyze_config`
3. **Execute MCP tool calls** sequentially
4. **Generate structured report** with findings + suggested follow-ups

---

## Auto-Trigger Phrases

라우팅 SSOT: `routing.json`.
- **KO**: 물리 디버깅 · PhysicsAsset/PA_/래그돌 · constraint/조인트 limit · PhysicalMaterial 마찰/탄성/밀도 · collision profile/충돌 채널/ECC_ · chaos solver · simulated vs kinematic
- **EN**: physics debug · ragdoll · constraint/joint limits · friction/restitution/density · collision profile/channel/ECC_* · chaos settings · simulated vs kinematic

---

## Workflow

### Step 1: Intent classification

Parse `$ARGUMENTS` + most recent user query and select the routing branch:

| Signal | Branch | First op |
|---|---|---|
| Pattern starts with `PA_` or contains "ragdoll", "skeletal" | A. PhysicsAsset | `ue_manage_physics(operation="get_body_names", physics_asset=...)` |
| `--bone <name>` flag or message contains specific bone name | A.1 Body shape detail | `ue_manage_physics(get_body_shapes, physics_asset, bone_name)` |
| `--constraints` flag, "constraint", "joint", "limit" | A.2 Constraints | `ue_manage_physics(get_constraints, physics_asset)` |
| Pattern starts with `PM_` or contains "friction"/"restitution"/"density" | B. PhysicalMaterial | `ue_manage_physics(get_physical_material_properties, material_name=...)` |
| `--collision` flag, "channel", "profile", "ECC_" | C. Collision | `ue_manage_physics(analyze_collision_profiles)` + `ue_analyze_config(search_config, section="/Script/Engine.CollisionProfile")` |
| Wildcard / no specific name | D. Discovery | `ue_manage_physics(search_physics_assets, query="*")` then `search_physical_materials` |
| "chaos", "solver", "p.Chaos", "substep" | E. Chaos | `ue_manage_physics(get_chaos_settings)` (smart: `chaos=true`) |
| "set body shape", "add/set/remove constraint", "create PA from mesh" | F. Authoring (11 ops, explicit-only) | `ue_manage_physics(<authoring op>)` — `dry_run=true` 기본 (→ REFERENCE.md) |

### Step 2: Branch A — PhysicsAsset

For a target PhysicsAsset (e.g., `PA_Mannequin`):

```python
# 2A.1 Bodies summary (cheapest call)
ue_manage_physics(operation="get_body_names", physics_asset="PA_Mannequin")
# → bone_names: ["pelvis", "spine_01", "thigh_l", ...]

# 2A.2 Per-body shape detail (if user specified --bone or asked about specific bone)
ue_manage_physics(operation="get_body_shapes", physics_asset="PA_Mannequin", bone_name="pelvis")
# → data[*].shapes[*]: {shape_type, center, rotation, radius, length, extent_*}

# 2A.3 Per-body simulation mode + mass scale
ue_manage_physics(operation="get_body_properties", physics_asset="PA_Mannequin")
# → data[*]: {bone_name, physics_type: Default|Kinematic|Simulated, mass_scale}

# 2A.4 Constraints (if user mentioned constraint/joint/limit)
ue_manage_physics(operation="get_constraints", physics_asset="PA_Mannequin")
# → data[*]: {joint_name, bone1_name, bone2_name, swing1_motion, swing1_limit_degrees, ...}
```

### Step 3: Branch B — PhysicalMaterial

```python
# 3B.1 Direct property lookup
ue_manage_physics(operation="get_physical_material_properties", material_name="PM_Default")
# → friction, restitution, density, raise_mass_to_power, friction_combine_mode, restitution_combine_mode

# 3B.2 Search if name unclear
ue_manage_physics(operation="search_physical_materials", query="PM_Concrete*")
```

### Step 4: Branch C — Collision profiles

`ue_manage_physics(analyze_collision_profiles)` walks the 12-layer INI cascade (#18102); one call returns all profile/channel/alias/matrix/redirect fields (detail: REFERENCE.md):

```python
# 4C.1 Full collision profile analysis in ONE call
ue_manage_physics(operation="analyze_collision_profiles")

# 4C.2 If user asked "what uses ECC_GameTraceChannel1", grep C++ usage
ue_grep(query="ECC_GameTraceChannel1", domain="source")
```

### Step 5: Branch D — Discovery (no specific name supplied)

```python
ue_manage_physics(operation="smart", query="*")  # search_physics_assets wildcard
ue_manage_physics(operation="search_physical_materials", query="*")
```

Then ask the user which asset to drill into.

---

## Structured Report Template

When all calls complete, emit a report in this shape (Korean default; switch to English when the user's query was English):

```markdown
## Physics 디버깅 결과 — {target_name}

### Body / Constraint 요약
- **Bodies**: N개 (e.g., pelvis, spine_01, thigh_l)
- **Constraints**: M개 (예: spine_01↔pelvis, thigh_l↔pelvis)
- **Simulation 모드 분포**: Default {x}, Kinematic {y}, Simulated {z}

### 주목할 발견 (있을 때만)
- ⚠️ `spine_01` body에 shape이 없음 (collision 없음)
- ⚠️ `thigh_l` body가 Kinematic + mass_scale=0.1 → 의도된 설정인지 확인
- ⚠️ swing1 limit이 180°에 가까운 constraint가 N개 (사실상 unlimited)

### 다음 단계 제안
- Body shape 변경: `ue_manage_physics(set_body_shape, ...)` — `dry_run=true` 기본(미리보기); 적용은 `dry_run=false` (live Editor 필요, #16314)
- Constraint 한도 변경: `ue_manage_physics(set_constraint, ...)` — 동일 (dry_run 기본, explicit-only)
- Body/shape 추가·제거: 구현 완료 (#18111, `add_body`/`remove_body`/`remove_shape`)
- Editor 직접 열기: `narshamcp launch-editor` 또는 `ue_editor_automation(focus_asset)` (다른 skill)
```

For PhysicalMaterial:

```markdown
## PhysicalMaterial — {material_name}

| Property | Value | UE Default | Note |
|---|---|---|---|
| Friction | 0.95 | 0.7 | 콘크리트 같은 거친 표면 |
| Restitution | 0.05 | 0.3 | 거의 안 튐 |
| Density | 2.4 | 1.0 | (kg/cc 단위, 기본보다 무거움) |
| Friction Combine | Max | Average | 두 표면 중 큰 값 사용 |
```

---

## Output Format Examples

### Example 1 — User: "PA_Mannequin 분석해줘"

1. `ue_manage_physics(get_body_names, physics_asset="PA_Mannequin")`
2. `ue_manage_physics(get_body_properties, physics_asset="PA_Mannequin")`
3. `ue_manage_physics(get_constraints, physics_asset="PA_Mannequin")`
4. Emit Body/Constraint summary report.

### Example 2 — User: "PM_Concrete 마찰 얼마야?"

1. `ue_manage_physics(get_physical_material_properties, material_name="PM_Concrete")`
2. Emit single-row table.

### Example 3 — User: "ECC_GameTraceChannel1 어디 쓰여?"

1. `ue_analyze_config(search_config, section="/Script/Engine.CollisionProfile")`
2. `ue_grep(query="ECC_GameTraceChannel1", domain="source")`
3. Emit profile definition + C++ usage list.

### Example 4 — User: "물리 디버깅 시작" (no target)

1. `ue_manage_physics(search_physics_assets, query="*")`
2. `ue_manage_physics(search_physical_materials, query="*")`
3. Ask the user which asset to inspect.

---

## Error Recovery

| Error | Cause | Action |
|---|---|---|
| `PhysicsAsset 'X' not found in metadata cache` | Indexing not complete or asset missing | Run `ue_check_health()`; suggest `search_physics_assets("*")` |
| Empty `bone_names` array | Asset's `parsed_exports` is sparse (Rule 20 path) | Report explicitly; suggest opening in Editor for live verification |
| `analyze_collision_profiles` returns `success:false` engine-path error | `UECODEGEN_ENGINE_DIR` not set/invalid (#18102 walker reads the engine cascade) | Set `UECODEGEN_ENGINE_DIR`, restart MCP; fallback `ue_analyze_config(search_config)` |
| User asks for authoring (set/add/remove) | Authoring 11종 노출됨 (#16314/#18110/#18111/#18113) — live Editor RC, explicit-only | 직접 호출, `dry_run=true` 기본(미리보기)→`dry_run=false` 적용(read-back #17304). 목록: REFERENCE.md § Read / Runtime / Authoring ops |

---

## Composability

This skill chains naturally with:

- `/ue-asset-discovery` — "What references PA_Mannequin?"
- `/ue-cvar-explorer` — `p.Chaos.*` CVar lookup (Chaos solver 설정 조회 자체는 이 스킬의 `get_chaos_settings`, #9996)
- `/ue-debug` — generic UE debugging with symptom routing
- `/ue-validate` — pre-submit asset validation including PhysicsAsset
