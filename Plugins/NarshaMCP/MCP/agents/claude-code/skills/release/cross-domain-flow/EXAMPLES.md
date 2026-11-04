# Cross-Domain Execution Flow - Examples

## Example 1: Attack Button Flow (MCP Live Test)

### Scenario
User wants to understand the complete execution flow when pressing the attack button.

### User Input
```text
"공격 버튼 누르면 어떤 일이 일어나?"
```

### Workflow Execution

**Stage 1: Input Layer (with Error Recovery)**
```python
ue_manage_gameplay(
    operation="trace_input_flow",
    params={"input_action": "IA_Attack"}
)
```
Result (Primary Failed):
```json
{
  "error": "InputAction 'IA_Attack' not found",
  "success": false
}
```

**Stage 1 Fallback**:
```python
ue_search_assets(
    asset_type="InputAction",
    query="*"
)
```
Result:
```json
{
  "assets": [
    {"name": "IA_Cancel", "path": "/Game/Input/IA_Cancel"},
    {"name": "IA_ChangeBladeAttack1", "path": "/Game/Input/IA_ChangeBladeAttack1"},
    {"name": "IA_Dash", "path": "/Game/Input/IA_Dash"}
  ],
  "total_found": 10
}
```

**Stage 2: GAS Layer (with Fallback)**
```python
ue_manage_gameplay(
    operation="search_tags",
    params={"query": "Ability.*", "limit": 10}
)
```
Result:
```json
{
  "tags": [
    {"tag": "Ability.Character.Attack", "source": "DefaultGameplayTags.ini"},
    {"tag": "Ability.Character.Dash", "source": "DefaultGameplayTags.ini"}
  ],
  "count": 10
}
```

**Stage 2 Continued: Find Ability**
```python
ue_search_assets(
    asset_type="GameplayAbility",
    query="*Attack*"
)
```
Result:
```json
{
  "assets": [
    {"name": "GA_BackDoor_Attack01", "path": "/Game/Core/EnemyCharacter/BackDoor/GA/"},
    {"name": "GA_BackDoor_Attack02_Back", "path": "/Game/Core/EnemyCharacter/BackDoor/GA/"}
  ],
  "total_found": 10
}
```

**Stage 3: Blueprint Layer**
```python
ue_trace_execution(
    operation="trace_ability_flow",
    params={
        "ability_name": "GA_BackDoor_Attack01",
        "include_animation": true,
        "include_cpp": true,
        "include_tags": true
    }
)
```
Result:
```json
{
  "flow": {
    "ability": {
      "name": "GA_BackDoor_Attack01",
      "parent_class": "GA_EnemyAttackBase",
      "montages": ["AM_BackDoor_Attack01"],
      "gameplay_tags": ["Cooldown.AI.Pattern.Attack01"]
    }
  },
  "success": true
}
```

**Stage 4: C++ Layer**
```python
ue_analyze_symbols(
    operation="get_methods",
    params={"class_name": "AAGameplayAbilityBase", "limit": 10}
)
```
Result:
```json
{
  "methods": [
    {"name": "ApplyCooldown", "full_name": "UAAGameplayAbilityBase::ApplyCooldown"},
    {"name": "ApplyCost", "full_name": "UAAGameplayAbilityBase::ApplyCost"},
    {"name": "CanActivateAbility", "full_name": "UAAGameplayAbilityBase::CanActivateAbility"}
  ],
  "methods_count": 10
}
```

**Stage 5: Animation Layer**
```python
ue_search_assets(
    asset_type="Animation",
    query="AM_BackDoor_Attack01"
)
```
Result:
```json
{
  "assets": [
    {
      "name": "AM_BackDoor_Attack01",
      "path": "/Game/Character/Monster/BackDoor/Animation/Attack/AM_BackDoor_Attack01",
      "type": "Animation"
    }
  ],
  "total_found": 1
}
```

### Complete Flow Report

```text
================================================================================
🎯 COMPLETE EXECUTION FLOW: "GA_BackDoor_Attack01"
================================================================================

📍 FLOW VISUALIZATION

[Input]                    [GAS]                      [Blueprint]
IA_* (Enhanced Input) ────► Ability.Character.Attack ──► GA_BackDoor_Attack01
                                    │                         │
                                    ▼                         ▼
                           GameplayTag ◄────────────── Cooldown.AI.Pattern.Attack01
                                    │                         │
                                    ▼                         ▼
[Animation]                [C++]                       [Blueprint]
AM_BackDoor_Attack01 ◄──── PlayMontageAndWait ◄────── ActivateAbility
       │                          │
       ▼                          ▼
/Game/Character/         UAAGameplayAbilityBase
Monster/BackDoor/        ├─ ApplyCooldown
Animation/Attack/        ├─ ApplyCost
                         └─ CanActivateAbility

📍 KEY DATA
- Ability: GA_BackDoor_Attack01 (Parent: GA_EnemyAttackBase)
- Montage: AM_BackDoor_Attack01
- C++ Base: UAAGameplayAbilityBase (10+ methods)
- Tags: Cooldown.AI.Pattern.Attack01

📍 ERROR RECOVERY USED
- Stage 1: IA_Attack not found → Searched all InputActions
- Stage 2: Tag search → Found Ability via ue_search_assets

================================================================================
```

---

## Example 2: Dash Ability Flow

### Scenario
User wants to trace the dash ability from input to animation.

### User Input
```text
"Dash 버튼 누르면 전체 흐름 추적해줘"
```

### Workflow Execution

**Stage 1: Input Layer**
```python
ue_manage_gameplay(
    operation="trace_input_flow",
    params={"input_action": "IA_Dash"}
)
```

**Stage 2: GAS Layer**
```python
ue_manage_gameplay(
    operation="trace_abilities",
    params={"tag": "Ability.Character.Dash"}
)
```

**Stage 3: Blueprint Layer**
```python
ue_trace_execution(
    operation="trace_ability_flow",
    params={"ability_name": "GA_Dash"}
)
```

**Expected Output**:
```text
================================================================================
🎯 COMPLETE EXECUTION FLOW: "Dash"
================================================================================

[Input]                    [GAS]                      [Animation]
IA_Dash ──────────────────► Ability.Character.Dash ──► AM_Dash
  │                              │                         │
  ▼                              ▼                         ▼
Enhanced Input             GA_Dash Activation        Root Motion
  │                              │                         │
  ▼                              ▼                         ▼
InputTag.Dash              CommitAbilityCost         Movement Applied

================================================================================
```

---

## Example 3: Weapon Fire Flow

### Scenario
User wants to understand weapon firing mechanics.

### User Input
```text
"GA_Weapon_Fire 전체 실행 흐름 분석"
```

### Expected Stages
1. **Input**: IA_Fire → InputTag.Weapon.Fire
2. **GAS**: InputTag → GA_Weapon_Fire activation
3. **Blueprint**: Ability nodes → PlayMontage, SpawnProjectile
4. **C++**: UGameplayAbility::ActivateAbility, AProjectile::BeginPlay
5. **Animation**: AM_Fire_Rifle, AN_SpawnProjectile

---

## Error Recovery Examples

### Case 1: InputAction Not Found

**Problem**: `IA_Attack` doesn't exist in project
**Fallback**: `ue_search_assets(asset_type="InputAction")` to list available actions
**Result**: User can select from available InputActions

### Case 2: No Ability for Tag

**Problem**: `Ability.Character.Attack` tag exists but no Ability uses it
**Fallback**: `ue_manage_gameplay(operation="search_tags", query="Ability.*")`
**Result**: Find related abilities with similar tags

### Case 3: C++ Symbol Not Found

**Problem**: Blueprint-only ability, no C++ implementation
**Fallback**: Search for base class `UGameplayAbility` or project-specific base
**Result**: Show engine or project base class methods

### Case 4: Animation Not Found

**Problem**: Montage name doesn't match search pattern
**Fallback**: Broaden search with `*Attack*` pattern
**Result**: Find related animations

---

## Workflow Selection Guide

| Query Type | Starting Point | Key Stages | Best For |
|------------|----------------|------------|----------|
| "버튼 누르면?" | InputAction | 1→2→3→4→5 | Full input-to-animation |
| "Ability 흐름" | Ability name | 2→3→4→5 | GAS-focused analysis |
| "애니메이션 연결" | Animation name | 5→3→2 | Reverse trace |
| "C++ 구현 찾기" | Function name | 4→3 | Implementation lookup |

---

## Integration with Other Skills

**Combined with gas-development**:
```text
cross-domain-flow (quick overview) → gas-development (detailed GAS analysis)
```

**Combined with animation-workflow skill**:
```text
cross-domain-flow (find montage) → ue_manage_blueprint with AnimBP operations (detailed anim analysis)
```

**Combined with insights-profiler**:
```text
cross-domain-flow (identify hot path) → insights-profiler (performance analysis)
```

---

**Last Updated**: 2026-02-06
**Related**: Issue #4442
