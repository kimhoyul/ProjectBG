# GAS Development Workflow Examples

**Version**: 1.0.0 (Issue #1715 Phase 3)

---

## Example 1: Ability Execution Flow Analysis

**User Request**: "GA_FireBall의 실행 흐름 전체 분석해줘"

### Phase 1: Gameplay Analysis

```text
Tool: ue_manage_gameplay
Operation: trace_abilities
Query: GA_FireBall

Result:
├─ Ability: GA_FireBall
├─ Tags:
│   ├─ Granted: Ability.FireBall, Ability.Ranged
│   ├─ Blocked: Ability.Channeling
│   └─ Required: State.CanCast
├─ Cost:
│   ├─ Effect: GE_ManaCost
│   └─ Attribute: Mana (-30)
├─ Cooldown:
│   ├─ Effect: GE_FireBallCooldown
│   └─ Duration: 5.0s
└─ Applied Effects:
    └─ GE_FireBallDamage (Target)
```

### Phase 2: Execution Tracing

```text
Tool: ue_trace_execution
Operation: trace_ability_flow
Entry: GA_FireBall

Result:
├─ Activation Sequence:
│   ├─ CanActivateAbility()
│   │   ├─ CheckCost() → Mana >= 30
│   │   ├─ CheckCooldown() → Not on cooldown
│   │   └─ CheckTagRequirements() → State.CanCast present
│   ├─ CommitAbility()
│   │   ├─ ApplyCost() → GE_ManaCost
│   │   └─ ApplyCooldown() → GE_FireBallCooldown
│   └─ ActivateAbility()
│       ├─ PlayMontage(AM_Cast)
│       ├─ SpawnProjectile(BP_FireBall)
│       └─ WaitForMontageEnd()
├─ On Hit:
│   ├─ ApplyEffectToTarget(GE_FireBallDamage)
│   └─ ExecuteGameplayCue(GC_FireBall_Impact)
└─ EndAbility()
```

### Phase 3: Symbol Resolution

```text
Tool: ue_analyze_symbols
Operation: search_symbols
Query: UGA_FireBall

Result:
├─ Class Definition:
│   ├─ Header: Source/MyGame/Abilities/GA_FireBall.h
│   ├─ Source: Source/MyGame/Abilities/GA_FireBall.cpp
│   └─ Module: MyGame
├─ Base Class: UGameplayAbility
├─ Key Functions:
│   ├─ CanActivateAbility() - Override
│   ├─ ActivateAbility() - Override
│   └─ EndAbility() - Override
└─ Related Classes:
    ├─ ABP_FireBall (Projectile)
    └─ UGE_FireBallDamage (Effect)
```

### Unified Insight

```text
GA_FireBall Complete Analysis:
├─ Complexity: Medium (15 nodes, 3 effects)
├─ Cost: 30 Mana
├─ Cooldown: 5 seconds
├─ Damage Type: Fire, Ranged
├─ Key Integration Points:
│   ├─ Projectile spawning (requires BP_FireBall)
│   ├─ Montage dependency (AM_Cast)
│   └─ GAS effect chain (Cost → Cooldown → Damage)
└─ Development Notes:
    ├─ Override CanActivateAbility for custom checks
    └─ Consider target prediction for multiplayer
```

---

## Example 2: GameplayTag Hierarchy Analysis

**User Request**: "프로젝트의 Ability 관련 태그 구조 분석해줘"

### Phase 1: Tag Discovery

```text
Tool: ue_manage_gameplay
Operation: search_tags
Pattern: Ability.*

Result:
├─ Tag Hierarchy:
│   Ability
│   ├─ Ability.Attack
│   │   ├─ Ability.Attack.Light
│   │   ├─ Ability.Attack.Heavy
│   │   └─ Ability.Attack.Special
│   ├─ Ability.Defense
│   │   ├─ Ability.Defense.Block
│   │   └─ Ability.Defense.Dodge
│   ├─ Ability.Magic
│   │   ├─ Ability.Magic.Fire
│   │   ├─ Ability.Magic.Ice
│   │   └─ Ability.Magic.Lightning
│   └─ Ability.Movement
│       ├─ Ability.Movement.Dash
│       └─ Ability.Movement.Jump
├─ Total Tags: 14
└─ Usage Locations: 8 Ability classes
```

### Phase 2: Tag Usage Tracing

```text
Tool: ue_trace_execution
Operation: trace_ability_flow
Focus: Tag relationships

Result:
├─ Tag Blocking Patterns:
│   ├─ Ability.Attack.* blocks Ability.Defense.*
│   ├─ Ability.Magic.* requires State.CanCast
│   └─ Ability.Movement.Dash blocks itself (cooldown)
├─ Mutual Exclusions:
│   └─ Ability.Attack.Light ⊕ Ability.Attack.Heavy
└─ Common Combinations:
    ├─ Ability.Attack + State.Attacking
    └─ Ability.Defense + State.Defending
```

### Phase 3: Implementation Lookup

```text
Tool: ue_analyze_symbols
Operation: search_symbols
Query: GameplayTags config

Result:
├─ Tag Configuration:
│   ├─ Config/DefaultGameplayTags.ini
│   └─ Source/MyGame/MyGameTags.h (native tags)
├─ Native Tag Definitions:
│   └─ FMyGameTags::Ability_Attack (line 45)
└─ Usage Pattern:
    └─ AbilitySystemGlobals::Get().InitGlobalTags()
```

---

## Example 3: AttributeSet Development

**User Request**: "Health와 Mana Attribute 구현 구조 분석"

### Phase 1: Attribute Discovery

```text
Tool: ue_manage_gameplay
Operation: search_tags
Pattern: Attribute.*

Result:
├─ AttributeSets Found:
│   ├─ UMyCharacterAttributeSet
│   │   ├─ Health (base: 100, max: 200)
│   │   ├─ MaxHealth (base: 100)
│   │   ├─ Mana (base: 50, max: 100)
│   │   ├─ MaxMana (base: 50)
│   │   └─ Stamina (base: 100)
│   └─ UCombatAttributeSet
│       ├─ AttackPower (base: 10)
│       ├─ Defense (base: 5)
│       └─ CriticalChance (base: 0.05)
└─ Total Attributes: 8
```

### Phase 2: Attribute Modification Tracing

```text
Tool: ue_trace_execution
Operation: trace_ability_flow
Focus: Attribute modifications

Result:
├─ Health Modifiers:
│   ├─ GE_Damage → -Health (Instant)
│   ├─ GE_Heal → +Health (Instant)
│   ├─ GE_Regeneration → +Health (Duration: Infinite, Period: 1s)
│   └─ GE_Poison → -Health (Duration: 10s, Period: 0.5s)
├─ Mana Modifiers:
│   ├─ GE_ManaCost → -Mana (Instant, from abilities)
│   └─ GE_ManaRegen → +Mana (Duration: Infinite, Period: 2s)
└─ Clamping Rules:
    ├─ Health: [0, MaxHealth]
    └─ Mana: [0, MaxMana]
```

### Phase 3: Implementation Details

```text
Tool: ue_analyze_symbols
Operation: search_symbols
Query: UMyCharacterAttributeSet

Result:
├─ Class: UMyCharacterAttributeSet
│   ├─ Header: Source/MyGame/Attributes/MyCharacterAttributeSet.h
│   └─ Source: Source/MyGame/Attributes/MyCharacterAttributeSet.cpp
├─ Key Macros Used:
│   ├─ ATTRIBUTE_ACCESSORS(UMyCharacterAttributeSet, Health)
│   └─ GAMEPLAYATTRIBUTE_REPNOTIFY(UMyCharacterAttributeSet, Health)
├─ Replication:
│   ├─ GetLifetimeReplicatedProps() defined
│   └─ All attributes replicated via DOREPLIFETIME_CONDITION
└─ Pre/Post Handlers:
    ├─ PreAttributeChange() - Clamping
    └─ PostGameplayEffectExecute() - Death check
```

---

## Example 4: GameplayEffect Chain Analysis

**User Request**: "데미지 시스템의 GE 체인 전체 분석"

### Phase 1: Effect Discovery

```text
Tool: ue_manage_gameplay
Operation: trace_abilities
Focus: Damage effects

Result:
├─ Damage Effect Chain:
│   GE_ApplyDamage (Root)
│   ├─ Modifiers:
│   │   ├─ Health: -BaseDamage (Scalable Float)
│   │   └─ Source: AttackPower (Attribute Based)
│   ├─ Conditional Effects:
│   │   ├─ GE_ApplyCritical (if CriticalHit tag)
│   │   └─ GE_ApplyStagger (if damage > threshold)
│   └─ Gameplay Cues:
│       ├─ GC_Damage_Physical
│       └─ GC_Damage_Critical (conditional)
└─ Stacking: None (instant effect)
```

### Phase 2: Effect Execution Flow

```text
Tool: ue_trace_execution
Operation: trace_ability_flow
Entry: GE_ApplyDamage

Result:
├─ Execution Flow:
│   1. CalculateBaseDamage()
│   │   └─ Source.AttackPower - Target.Defense
│   2. ApplyDamageMultipliers()
│   │   ├─ ElementalWeakness (1.5x if weak)
│   │   └─ CriticalHit (2.0x if crit)
│   3. ExecuteModification()
│   │   └─ Target.Health -= FinalDamage
│   4. CheckConditionalEffects()
│   │   ├─ Damage > 50 → Apply GE_ApplyStagger
│   │   └─ IsCritical → Apply GC_Damage_Critical
│   └─ 5. TriggerGameplayCues()
├─ Execution Class:
│   └─ UDamageExecution (custom)
└─ Dependencies:
    ├─ Capture: AttackPower (Source), Defense (Target)
    └─ Required Tags: None
```

### Phase 3: Code Implementation

```text
Tool: ue_analyze_symbols
Operation: search_symbols
Query: UDamageExecution

Result:
├─ Class: UDamageExecution
│   ├─ Header: Source/MyGame/GAS/Executions/DamageExecution.h
│   └─ Source: Source/MyGame/GAS/Executions/DamageExecution.cpp
├─ Base: UGameplayEffectExecutionCalculation
├─ Captured Attributes:
│   ├─ AttackPowerDef (Source)
│   ├─ DefenseDef (Target)
│   └─ HealthDef (Target, for output)
└─ Key Functions:
    └─ Execute_Implementation()
        ├─ CaptureRelevantAttributes()
        ├─ CalculateFinalDamage()
        └─ OutExecutionOutput.AddOutputModifier()
```

### Unified Summary

```text
Damage System Analysis:
├─ Entry Point: GE_ApplyDamage
├─ Execution: UDamageExecution (custom calc)
├─ Formula: (AttackPower - Defense) × Multipliers
├─ Modifiers:
│   ├─ Elemental weakness (1.5x)
│   ├─ Critical hit (2.0x)
│   └─ Armor reduction (variable)
├─ Side Effects:
│   ├─ Stagger on high damage
│   └─ Death check on health <= 0
└─ Development Tips:
    ├─ Add damage types for elemental system
    ├─ Consider damage immunity tags
    └─ Implement damage number display via GC
```

---

## Workflow Benefits

| Metric | Manual Approach | GAS Workflow |
|--------|-----------------|--------------|
| Tool Calls | 6-9 separate | 3 orchestrated |
| Context Switching | High | None |
| Time | 10-15 min | 2-3 min |
| Insight Quality | Fragmented | Unified |

---

---

## Example 5: AttributeSet Macro Validation (Phase 3.5)

**User**: "UMyCharacterAttributeSet 매크로 검증해줘"
(or: auto-triggered when AttributeSet detected in Phase 1/3)

### Phase 3.5a: Cross-Check Replication Completeness

```python
ue_analyze_source(operation="cross_check", class_name="UMyCharacterAttributeSet")
```

**Result**:
```text
summary:
  declared_replicated: 5 (Health, MaxHealth, Mana, MaxMana, Stamina)
  registered_doreplifetime: 4 (Health, MaxHealth, Mana, MaxMana)
  missing_registration: 1 (Stamina)
  has_issues: true

matched: [Health, MaxHealth, Mana, MaxMana]
missing_registration: [Stamina]
  → Stamina is UPROPERTY(Replicated) but has no DOREPLIFETIME registration
```

### Phase 3.5b: ATTRIBUTE_ACCESSORS Completeness

```python
ue_analyze_source(operation="find_macro_invocations", macro_name="ATTRIBUTE_ACCESSORS")
```

**Result**:
```text
count: 5
results: [
  {class: "UMyCharacterAttributeSet", attribute: "Health"},
  {class: "UMyCharacterAttributeSet", attribute: "MaxHealth"},
  {class: "UMyCharacterAttributeSet", attribute: "Mana"},
  {class: "UMyCharacterAttributeSet", attribute: "MaxMana"},
  {class: "UMyCharacterAttributeSet", attribute: "Stamina"}
]
Cross-reference with Phase 1 attributes: All 5 present
```

### Phase 3.5c: GAMEPLAYATTRIBUTE_REPNOTIFY Check

```python
ue_analyze_source(operation="find_macro_invocations", macro_name="GAMEPLAYATTRIBUTE_REPNOTIFY")
```

**Result**:
```text
count: 4
results: [Health, MaxHealth, Mana, MaxMana]
Missing: Stamina (but Stamina has no OnRep → consistent with cross_check)
```

### Verdict

- Cross-check: 1 missing registration (Stamina) — add `DOREPLIFETIME(UMyCharacterAttributeSet, Stamina)` to `GetLifetimeReplicatedProps`
- ATTRIBUTE_ACCESSORS: All 5 attributes covered
- REPNOTIFY: Consistent with cross_check (4/4 OnRep functions have matching macro)

---

**Related**: [SKILL.md](SKILL.md) | [Issue #1715](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1715)
