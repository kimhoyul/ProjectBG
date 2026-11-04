# Code Generation Power — Built-in Scenarios

> Per-scenario enrichment templates and integration notes for the 17 built-in scenarios.
> Linked from SKILL.md `## 17 Built-in Scenarios` table. Open this file ONLY when
> executing a specific scenario; SKILL.md alone is sufficient for routing.

**Skill version**: 2.0.0+ (Phase 2.5 enrichment)
**Parent**: [SKILL.md](SKILL.md) | [REFERENCE.md](REFERENCE.md)

> Public-facing scenario catalog (separate concept, user-guide level):
> [docs/user-guides/tools/CODE_GENERATION_SCENARIOS.md](../../../docs/user-guides/tools/CODE_GENERATION_SCENARIOS.md).

---

## Scenario Index

| # | Tier | Name | Anchor |
|---|------|------|--------|
| 1 | Critical | GAS Character System | [#scenario-1-gas-character-system](#scenario-1-gas-character-system) |
| 2 | Critical | Weapon System | [#scenario-2-weapon-system](#scenario-2-weapon-system) |
| 3 | Critical | AI Enemy System | [#scenario-3-ai-enemy-system](#scenario-3-ai-enemy-system) |
| 4 | Critical | Multiplayer Character | [#scenario-4-multiplayer-character](#scenario-4-multiplayer-character) |
| 5 | High | Health Component | [#scenario-5-health-component](#scenario-5-health-component) |
| 6 | High | Gameplay Ability Set | [#scenario-6-gameplay-ability-set](#scenario-6-gameplay-ability-set) |
| 7 | High | Widget/HUD System | [#scenario-7-widgethud-system](#scenario-7-widgethud-system) |
| 8 | High | Projectile System | [#scenario-8-projectile-system](#scenario-8-projectile-system) |
| 9 | Medium | DataAsset Item System | [#scenario-9-dataasset-item-system](#scenario-9-dataasset-item-system) |
| 10 | Medium | Enhanced Input Setup | [#scenario-10-enhanced-input-setup](#scenario-10-enhanced-input-setup) |
| 11-17 | Medium | Additional Tier 3 scenarios | [#scenarios-11-17-tier-3-additional](#scenarios-11-17-tier-3-additional) |

---

## Scenario 1: GAS Character System

**Tier**: 1 (Critical) | **Verified**: 4/4 Production | **Total lines**: ~362
**Classes**: Character + AttributeSet + Ability + HealthComponent
**Phase 4 BP**: `BP_MyCharacter`
**Phase 5 Input**: `IA_Move`, `IA_Jump`, `IA_Look`
**Phase 6 GAS**: Tags + Effects

### Phase 2.5 Enrichment — AttributeSet

After MCP generates skeleton, Claude adds:

```cpp
// Header enrichment
#include "AbilitySystemComponent.h"
#include "Net/UnrealNetwork.h"

// Inside class body:
UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Health, Category="Attributes")
FGameplayAttributeData Health;
ATTRIBUTE_ACCESSORS(UMyAttributeSet, Health)

UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxHealth, Category="Attributes")
FGameplayAttributeData MaxHealth;
ATTRIBUTE_ACCESSORS(UMyAttributeSet, MaxHealth)

// Functions:
virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

UFUNCTION()
void OnRep_Health(const FGameplayAttributeData& OldHealth);
UFUNCTION()
void OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth);
```

### Phase 2.5 Enrichment — Character

```cpp
// Fix: Add IAbilitySystemInterface inheritance
class AMyCharacter : public ACharacter, public IAbilitySystemInterface

// Add missing UPROPERTY:
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="GAS")
TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

UPROPERTY()
TObjectPtr<UMyAttributeSet> AttributeSet;

// Constructor init:
AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
AttributeSet = CreateDefaultSubobject<UMyAttributeSet>(TEXT("AttributeSet"));
```

### Integration Notes
- IAbilitySystemInterface implementation: override `GetAbilitySystemComponent()` returning `AbilitySystemComponent`.
- Replication: `AbilitySystemComponent` and `AttributeSet` need `Replicated` UPROPERTY specifier.
- Client prediction: GAS prediction tags must be set up in `BeginPlay()`.

---

## Scenario 2: Weapon System

**Tier**: 1 (Critical) | **Verified**: 4/4 Production | **Total lines**: ~225
**Classes**: Weapon + WeaponComponent + FireAbility + Projectile
**Phase 4 BP**: `BP_MyWeapon`
**Phase 5 Input**: — (tied to character)
**Phase 6 GAS**: — (FireAbility uses gameplay tags but no Effect declaration here)

> Phase 2.5 enrichment templates: TBD — see canonical regression scenario in `tests/integration/code-generation-power/` (or future expansion).

---

## Scenario 3: AI Enemy System

**Tier**: 1 (Critical) | **Verified**: 4/4 Production | **Total lines**: ~309
**Classes**: Enemy + AIController + EnemyAbility + AttributeSet
**Phase 4 BP**: `BP_MyEnemy`
**Phase 5 Input**: —
**Phase 6 GAS**: — (Tags only on EnemyAbility)

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 4: Multiplayer Character

**Tier**: 1 (Critical) | **Verified**: 4/4 Production | **Total lines**: ~234
**Classes**: MPCharacter + PlayerState + Controller + GameMode
**Phase 4 BP**: `BP_MyMPCharacter`
**Phase 5 Input**: `IA_Move`, `IA_Jump`, `IA_Look`
**Phase 6 GAS**: —

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 5: Health Component

**Tier**: 2 (High) | **Verified**: 3/3 Production | **Total lines**: ~213
**Classes**: HealthComponent + AttributeSet + HealEffect
**Phase 4-5**: — | **Phase 6 GAS**: — (no tags/effects beyond HealEffect itself)

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 6: Gameplay Ability Set

**Tier**: 2 (High) | **Verified**: 3/3 Production | **Total lines**: ~156
**Classes**: BaseAbility + CostAbility + CooldownEffect
**Phase 4-5**: — | **Phase 6 GAS**: Tags + Effects

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 7: Widget/HUD System

**Tier**: 2 (High) | **Verified**: 3/3 Clean | **Total lines**: ~81
**Classes**: HUDWidget + HealthBar + HUD
**Phase 4-6**: —

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 8: Projectile System

**Tier**: 2 (High) | **Verified**: 3/3 Production | **Total lines**: ~168
**Classes**: Projectile + ProjectileMovement + FireAbility
**Phase 4-6**: —

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 9: DataAsset Item System

**Tier**: 3 (Medium) | **Verified**: 3/3 Production | **Total lines**: ~137
**Classes**: ItemData + InventoryComponent + PickupActor
**Phase 4-6**: —

> Phase 2.5 enrichment templates: TBD.

---

## Scenario 10: Enhanced Input Setup

**Tier**: 3 (Medium) | **Verified**: 3/3 Production | **Total lines**: ~123
**Classes**: InputConfig + InputComponent + Controller
**Phase 4**: — | **Phase 5 Input**: `IA_Move`, `IA_Jump`, `IA_Look` | **Phase 6**: —

> Phase 2.5 enrichment templates: TBD.

---

## Scenarios 11-17: Tier 3 Additional

Tier 3 (Medium) scenarios documented as the registry expands. Per-scenario detail
landed alongside the canonical regression test for each scenario. Phase 4-7 mapping
is "Varies" — consult `Scenario-Phase Mapping (Phase 4-7)` in SKILL.md and the
project test directory for current coverage.

| # | Name | Status |
|---|------|--------|
| 11 | TBD | Planned |
| 12 | TBD | Planned |
| 13 | TBD | Planned |
| 14 | TBD | Planned |
| 15 | TBD | Planned |
| 16 | TBD | Planned |
| 17 | Data-Driven Balancing | v2.5.0 addition |

---

## How to Add a New Scenario

1. Add a row to **SKILL.md** `## 17 Built-in Scenarios` table (Tier 1/2/3).
2. Update **SKILL.md** `### Scenario-Phase Mapping` with which Phases this scenario triggers.
3. Add a new H2 in this file: `## Scenario N: <Name>` with anchor `#scenario-N-kebab-cased-name`.
4. Document Tier, Verified count, total lines, classes, phase mappings.
5. Add Phase 2.5 enrichment templates (header includes, UPROPERTY, function bodies).
6. Add integration notes (interfaces, replication, prediction).
7. Update the [Scenario Index](#scenario-index) table at the top of this file.

---

**Issue**: Epic #10451 Tier 3 — modularization extracted from SKILL.md (#10512).
