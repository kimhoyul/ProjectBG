# Power Code Generation - Examples

> **2.0.0** — E2E Character Creation: BP + Input + GAS Integration + Final Verification (Issue #6232)

## Example 1: GAS Character System (Tier 1)

**User**: "GAS 캐릭터 시스템 만들어줘"

### Phase 1: Scenario Analysis

```text
Matched: Scenario 1 - GAS Character System
Classes to generate (4):
  1. UMyAttributeSet (UAttributeSet) - gas_attributeset_base
  2. UGA_MyBaseAbility (UGameplayAbility) - gas_ability_base, gas_ability_cost
  3. UMyHealthComponent (UActorComponent) - actor_component_base, damage_system
  4. AMyCharacter (ACharacter) - gas_setup, enhanced_input, damage_system
```

### Phase 2: Multi-Class Generation

| # | Class | Lines (h+cpp) | Key Features |
|---|-------|---------------|--------------|
| 1 | UMyAttributeSet | 95 (45+50) | ATTRIBUTE_ACCESSORS, DOREPLIFETIME, PreAttributeChange, PostGameplayEffectExecute |
| 2 | UGA_MyBaseAbility | 76 (36+40) | ActivateAbility, CommitAbility, Cost/Cooldown GE classes |
| 3 | UMyHealthComponent | 91 (44+47) | ApplyDamage, Heal, IsAlive, OnHealthChanged delegate |
| 4 | AMyCharacter | ~100 (50+50) | IAbilitySystemInterface, ASC, EnhancedInput, DamageSystem |

**Total: ~362 lines of production-ready code from 4 MCP calls**

### Phase 3: Validation Loop

```text
ue_fix_errors(mode="smart", error_message="... undeclared identifier ...")
→ "Add #include for 'USphereComponent'" (confidence: 0.9, PDB-verified)
```

### Output Summary

```text
Files Generated (8): 4 classes x (.h + .cpp)
Build.cs Dependencies: GameplayAbilities, GameplayTags, GameplayTasks, EnhancedInput
```

---

## Example 2: Weapon System (Tier 1)

**User**: "무기 시스템 만들어줘"

### Phase 2: Multi-Class Generation (Verified)

| # | Class | Lines (h+cpp) | Key Features |
|---|-------|---------------|--------------|
| 1 | AMyProjectile | 65 (33+32) | USphereComponent + UProjectileMovementComponent (CreateDefaultSubobject), OnHit callback, InitialLifeSpan, CollisionProfile |
| 2 | UMyWeaponComponent | ~120 (55+65) | BeginPlay/EndPlay + **weapon_fire_state** (Fire/Reload FSM, Ammo, Timer) |
| 3 | UGA_WeaponFire | ~70 (30+40) | **ability_projectile_spawn** (SpawnActor in ActivateAbility) |
| 4 | AMyWeapon | 41 (26+15) | IAbilitySystemInterface, ASC UPROPERTY, CreateDefaultSubobject |

**Total: 225 lines — 4/4 classes Production quality**

### AMyProjectile Generated Code (actual MCP output)

**Header highlights:**
```cpp
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
TObjectPtr<UProjectileMovementComponent> ProjectileMovement;

UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Collision")
TObjectPtr<USphereComponent> CollisionComponent;

UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Projectile")
float LifeSpan = 3.0f;

UFUNCTION()
void OnHit(UPrimitiveComponent* HitComp, AActor* OtherActor, ...);
```

**Constructor highlights:**
```cpp
CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
CollisionComponent->InitSphereRadius(15.0f);
CollisionComponent->SetCollisionProfileName(TEXT("Projectile"));
SetRootComponent(CollisionComponent);

ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(...);
ProjectileMovement->InitialSpeed = 3000.0f;
ProjectileMovement->MaxSpeed = 3000.0f;
ProjectileMovement->bRotationFollowsVelocity = true;

CollisionComponent->OnComponentHit.AddDynamic(this, &AMyProjectile::OnHit);
InitialLifeSpan = LifeSpan;
```

---

## Example 3: AI Enemy System (Tier 1)

**User**: "AI 적 시스템 만들어줘"

### Phase 2: Multi-Class Generation (Verified)

| # | Class | Lines (h+cpp) | Key Features |
|---|-------|---------------|--------------|
| 1 | UMyEnemyAttributeSet | 95 (45+50) | Health/MaxHealth/AttackPower attributes, full replication |
| 2 | UGA_EnemyAttack | ~70 (30+40) | **ability_melee** (LineTrace + GAS GE apply in ActivateAbility) |
| 3 | AMyAIController | ~130 (55+75) | IGenericTeamAgentInterface, Perception + **ai_patrol_chase** (Patrol/Chase FSM) |
| 4 | AMyEnemy | 89 (48+41) | IAbilitySystemInterface + ASC, DamageSystem (ApplyDamage, Heal, IsAlive) |

**Total: 309 lines — 4/4 classes Production quality**

### AMyAIController Generated Code (actual MCP output)

**Constructor highlights:**
```cpp
PerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(...);
UAISenseConfig_Sight* SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(...);
SightConfig->SightRadius = SightRadius;
SightConfig->LoseSightRadius = SightRadius + 500.0f;
SightConfig->DetectionByAffiliation.bDetectEnemies = true;
SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
SightConfig->DetectionByAffiliation.bDetectFriendlies = false;
PerceptionComponent->ConfigureSense(*SightConfig);
PerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());
PerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &AMyAIController::OnTargetPerceptionUpdated);
```

---

## Example 4: Multiplayer Character (Tier 1)

**User**: "멀티플레이어 캐릭터 시스템 만들어줘"

### Phase 2: Multi-Class Generation (Verified)

| # | Class | Lines (h+cpp) | Key Features |
|---|-------|---------------|--------------|
| 1 | AMyPlayerState | 57 (34+23) | Replicated Health/Score/TeamIndex, DOREPLIFETIME, OnRep callback |
| 2 | AMyPlayerController | 43 (23+20) | Enhanced Input setup |
| 3 | AMyGameMode | 27 (18+9) | Clean skeleton |
| 4 | AMyMPCharacter | 107 (59+48) | GAS (ASC + IAbilitySystemInterface), Replication, Sprint state machine |

**Total: 234 lines — 4/4 classes Production quality**

### AMyMPCharacter Generated Code (actual MCP output)

**Header highlights (3 features combined):**
```cpp
// GAS Setup
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

// Network Replication
UPROPERTY(ReplicatedUsing = OnRep_ReplicatedHealth, BlueprintReadOnly, Category = "Replication")
float ReplicatedHealth = 100.0f;

UPROPERTY(Replicated, BlueprintReadOnly, Category = "Replication")
float ReplicatedScore = 0.0f;

// Movement State Replication
UPROPERTY(Replicated, BlueprintReadOnly, Category = "Movement")
bool bIsSprinting = false;

UFUNCTION(Server, Reliable)
void ServerSetSprinting(bool bNewSprinting);
```

**Constructor highlights:**
```cpp
AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
bReplicates = true;
```

**Sprint implementation:**
```cpp
void AMyMPCharacter::ServerSetSprinting_Implementation(bool bNewSprinting)
{
    bIsSprinting = bNewSprinting;
    if (UCharacterMovementComponent* CMC = GetCharacterMovement())
    {
        CMC->MaxWalkSpeed = bIsSprinting ? 600.0f * SprintSpeedMultiplier : 600.0f;
    }
}
```

---

## Example 5: Validation Loop in Action

**User**: Generated code gives compilation error

### Workflow

```text
1. Generate code with ue_generate_code(operation="derive_class", ...)
2. Encounter error: "USphereComponent: undeclared identifier"
3. Fix with: ue_fix_errors(mode="smart", error_message="...")
```

### Actual Result (verified)

```json
{
  "errors": [{
    "category": "add_include",
    "file": "AMyProjectile.cpp",
    "line": 9,
    "type": "missing_header",
    "suggestion": {
      "action": "add_include",
      "code_snippet": "#include \"SphereComponent.h\"",
      "confidence": 0.9,
      "pdb_verified": true
    }
  }],
  "total_errors": 1,
  "total_time_ms": 55484
}
```

**Key**: `ue_fix_errors` works **fully offline** — no Editor required. PDB-verified include paths.

---

## Example 6: Custom Mix & Match

**User**: "캐릭터 + 인벤토리 조합으로 만들어줘. 어빌리티는 필요 없어"

### Scenario Composition

```text
No exact scenario match.
Composing custom scenario from:
  - Scenario 1 (Character) - exclude Ability, exclude HealthComponent
  - Scenario 9 (Item System) - include all

Custom generation plan:
  1. UMyAttributeSet (from Scenario 1)
  2. UMyItemData (from Scenario 9) - data_asset_base
  3. UMyInventoryComponent (from Scenario 9) - actor_component_base
  4. AMyCharacter (from Scenario 1, without gas_ability features)
```

---

## Example 7: Interactive Questionnaire (v1.5.0 — AI Enemy Boss)

**User**: "AI 적 시스템 만들어줘"

### Phase 1: Scenario Analysis

```text
Matched: Scenario 3 - AI Enemy System
```

### Phase 1.5: Parameter Collection (Interactive)

Claude asks targeted questions based on the matched scenario:

```text
Claude: "적 유형을 선택해주세요:"
  - 보스급 (HP=500, 시야=3000, 공격=50, 순찰=500)
  - 졸개급 (HP=50, 시야=1000, 공격=5, 순찰=2000)
  - 정찰급 (HP=100, 시야=4000, 공격=10, 순찰=3000)
  - 커스텀

User: "보스급으로, 체력은 800으로"
```

### Phase 2: Parameterized Generation

MCP calls use `feature_params` to inject custom values:

```python
# Class 3: AI Controller with parameterized perception + patrol
ue_generate_code(operation="derive_class", params={
    "base_class": "AAIController",
    "class_name": "AMyAIController",
    "output_dir": "Source/MyGame/AI",
    "features": [
        {"name": "perception", "sight_radius": 3000.0, "lose_sight_offset": 1000.0},
        "team_system",
        {"name": "ai_patrol_chase", "patrol_radius": 500.0, "chase_acceptance_radius": 150.0}
    ]
})
```

### Generated Code (with boss preset values)

**AMyAIController.h** (parameterized):
```cpp
// Perception — sight_radius=3000 (boss preset, not default 1500)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AI|Perception")
float SightRadius = 3000.0f;

// AI Patrol/Chase — patrol_radius=500 (boss stays close)
UPROPERTY(EditAnywhere, Category = "AI|Patrol")
float PatrolRadius = 500.0f;

UPROPERTY(EditAnywhere, Category = "AI|Chase")
float ChaseAcceptanceRadius = 150.0f;

UFUNCTION(BlueprintCallable, Category = "AI")
void SetTarget(AActor* NewTarget);
void ClearTarget();
void MoveToTarget();
void PatrolToRandomPoint();
```

**AMyAIController.cpp** (full game logic):
```cpp
void AMyAIController::SetTarget(AActor* NewTarget)
{
    CurrentTarget = NewTarget;
    if (CurrentTarget) { MoveToTarget(); }
}

void AMyAIController::PatrolToRandomPoint()
{
    if (APawn* Pawn = GetPawn()) {
        FNavLocation RandomPoint;
        if (UNavigationSystemV1* Nav = UNavigationSystemV1::GetCurrent(GetWorld())) {
            if (Nav->GetRandomReachablePointInRadius(
                    Pawn->GetActorLocation(), PatrolRadius, RandomPoint)) {
                MoveToLocation(RandomPoint.Location);
            }
        }
    }
}
```

---

## Example 8: Parameterized Shotgun (v1.5.0)

**User**: "샷건 무기 컴포넌트 만들어줘. 장탄수 8발, 발사속도 0.8초, 재장전 3.5초"

### MCP Call with feature_params

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "UActorComponent",
    "class_name": "UMyShotgunComponent",
    "output_dir": "Source/MyGame/Weapons",
    "features": [
        "actor_component_base",
        {"name": "weapon_fire_state", "fire_rate": 0.8, "max_ammo": 8, "reload_time": 3.5}
    ]
})
```

### Generated Code (custom values injected)

**UMyShotgunComponent.h**:
```cpp
UPROPERTY(EditDefaultsOnly, Category = "Weapon")
float FireRate = 0.8f;  // shotgun cadence (default would be 0.2f)

UPROPERTY(EditDefaultsOnly, Category = "Weapon")
int32 MaxAmmo = 8;  // 8-round magazine (default would be 30)

UPROPERTY(EditDefaultsOnly, Category = "Weapon")
float ReloadTime = 3.5f;  // slow reload (default would be 2.0f)

UPROPERTY(BlueprintReadOnly, Category = "Weapon")
int32 CurrentAmmo;

UPROPERTY(BlueprintReadOnly, Category = "Weapon")
bool bIsReloading = false;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFire);
UPROPERTY(BlueprintAssignable)
FOnFire OnFire;
```

**UMyShotgunComponent.cpp** (complete FSM):
```cpp
void UMyShotgunComponent::StartFire()
{
    if (CanFire()) {
        GetWorld()->GetTimerManager().SetTimer(
            FireTimerHandle, this, &UMyShotgunComponent::ExecuteFire,
            FireRate, true, 0.0f);
    }
}

void UMyShotgunComponent::ExecuteFire()
{
    if (!CanFire()) { StopFire(); return; }
    CurrentAmmo--;
    OnFire.Broadcast();
}

void UMyShotgunComponent::Reload()
{
    if (bIsReloading || CurrentAmmo == MaxAmmo) return;
    bIsReloading = true;
    GetWorld()->GetTimerManager().SetTimer(
        ReloadTimerHandle, [this]() {
            CurrentAmmo = MaxAmmo;
            bIsReloading = false;
        }, ReloadTime, false);
}
```

---

## Example 9: Inventory System with FItemSlot (v1.6.0)

**User**: "인벤토리 컴포넌트 만들어줘. 슬롯 40개로"

### MCP Call with inventory_slot

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "UActorComponent",
    "class_name": "UMyInventoryComponent",
    "output_dir": "Source/MyGame/Inventory",
    "features": [
        "actor_component_base",
        {"name": "inventory_slot", "max_slots": 40}
    ]
})
```

### Generated Code (pre-class USTRUCT + full CRUD)

**UMyInventoryComponent.h** (FItemSlot before UCLASS):
```cpp
USTRUCT(BlueprintType)
struct FItemSlot
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UDataAsset> ItemData = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Count = 0;
    bool IsValid() const { return ItemData != nullptr && Count > 0; }
};

UCLASS()
class MYGAME_API UMyInventoryComponent : public UActorComponent
{
    // ...
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
    int32 MaxSlots = 40;  // parameterized (default would be 20)

    UPROPERTY(BlueprintReadOnly, Category = "Inventory")
    TArray<FItemSlot> Slots;

    UFUNCTION(BlueprintCallable, Category = "Inventory")
    bool AddItem(UDataAsset* Item, int32 Count = 1);

    DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnInventoryChanged, UDataAsset*, Item, int32, NewCount);
    UPROPERTY(BlueprintAssignable)
    FOnInventoryChanged OnInventoryChanged;
};
```

**UMyInventoryComponent.cpp** (complete CRUD):
```cpp
bool UMyInventoryComponent::AddItem(UDataAsset* Item, int32 Count)
{
    if (!Item || Count <= 0) return false;
    for (FItemSlot& Slot : Slots)
    {
        if (Slot.ItemData == Item)
        {
            Slot.Count += Count;
            OnInventoryChanged.Broadcast(Item, Slot.Count);
            return true;
        }
    }
    if (Slots.Num() < MaxSlots)
    {
        FItemSlot NewSlot;
        NewSlot.ItemData = Item;
        NewSlot.Count = Count;
        Slots.Add(NewSlot);
        OnInventoryChanged.Broadcast(Item, Count);
        return true;
    }
    return false;
}
```

**Key**: `inventory_slot` injects FItemSlot USTRUCT **before** UCLASS() — a unique pre-class injection pattern.

---

## Example 10: GAS Character E2E with Editor (v2.0.0 — 7-Phase)

**User**: "GAS 캐릭터 시스템 만들어줘" (Editor running)

### Phase 1-3: Same as Example 1

Phase 1 matches Scenario 1, Phase 2 generates 4 classes, Phase 3 validates.

### Phase 3.5: Editor Detection

```text
ue_check_health() → capabilities.editor_control = true
→ Editor available, proceeding with Phase 4-5
```

### Phase 4: Blueprint Subclass Creation

```python
ue_manage_blueprint(operation="create_blueprint",
    blueprint_name="BP_MyCharacter",
    parent_class="AMyCharacter")
# → Created: /Game/Characters/BP_MyCharacter

ue_manage_blueprint(operation="add_variable",
    blueprint_name="BP_MyCharacter",
    variable_name="DefaultAbilities",
    variable_type="Array<GameplayAbility>")
```

### Phase 5: Input System Setup

```python
ue_manage_gameplay(operation="create_input_action",
    action_name="IA_Move", value_type="Axis2D")
# → {"success": true, "asset_path": "/Game/Input/Actions/IA_Move.IA_Move", "saved_to_disk": true}

ue_manage_gameplay(operation="create_input_action",
    action_name="IA_Jump", value_type="Digital")
# → {"success": true, "asset_path": "/Game/Input/Actions/IA_Jump.IA_Jump", "saved_to_disk": true}

ue_manage_gameplay(operation="create_input_action",
    action_name="IA_Look", value_type="Axis2D")
# → {"success": true, "asset_path": "/Game/Input/Actions/IA_Look.IA_Look", "saved_to_disk": true}

ue_manage_gameplay(operation="create_input_mapping",
    input_action="IA_Jump", key="SpaceBar", mapping_context="IMC_DefaultCharacter")
# → {"success": true, "mapping_context_path": "/Game/.../IMC_DefaultCharacter.IMC_DefaultCharacter", "saved_to_disk": true}
```

**Verified**: MCP live test on LCP_D_571 (UE 5.7) — `create_input_action` 18ms, `create_input_mapping` 334ms.

### Phase 6: GAS Integration Check

```text
GAS Integration:
  ✅ gas_setup feature included in Phase 2
  Manual steps required:
  - GameplayTags to create:
    • Ability.Attack
    • Ability.Jump
    • Status.Dead
    • Status.Stunned
  - GameplayEffects to create:
    • GE_Damage (Instant, -Health)
    • GE_Heal (Instant, +Health)
  - AbilitySet DataAsset:
    • DA_DefaultAbilities (grant GA_MyBaseAbility on possess)
```

### Phase 7: Final Verification

```text
ue_check_health() → status: healthy
Generated artifacts:
  C++: 8 files (4 classes × .h + .cpp)
  BP:  BP_MyCharacter (parent: AMyCharacter)
  Input: IA_Move, IA_Jump, IA_Look, IMC_Default

Next steps:
  [ ] Build project in Editor
  [ ] Create GameplayTags in Project Settings
  [ ] Create GE_Damage, GE_Heal GameplayEffect DataAssets
  [ ] Create DA_DefaultAbilities AbilitySet
  [ ] Test character spawn in level
```

---

## Example 11: GAS Character E2E Offline (v2.0.0 — 7-Phase, No Editor)

**User**: "GAS 캐릭터 시스템 만들어줘" (Editor NOT running)

### Phase 1-3: Same as Example 1

Phase 1 matches Scenario 1, Phase 2 generates 4 classes, Phase 3 validates.

### Phase 3.5: Editor Detection

```text
ue_check_health() → capabilities.editor_control = false
→ Editor not available, Phase 4-5 will output manual guidance
```

### Phase 4: Blueprint Subclass (Manual Guidance)

```text
⚠️ Editor not running. Create BP_MyCharacter manually:
  1. Open Content Browser → Right-click → Blueprint Class
  2. Select Parent Class: AMyCharacter
  3. Name: BP_MyCharacter
  4. Save to: /Game/Characters/
  5. Add variable: DefaultAbilities (Array<GameplayAbility>)
```

### Phase 5: Input Setup (Manual Guidance)

```text
⚠️ Editor not running. Create InputActions manually:
  1. Content Browser → Misc → Input Action:
     • IA_Move (Value Type: Axis2D)
     • IA_Jump (Value Type: Digital)
     • IA_Look (Value Type: Axis2D)
  2. Content Browser → Misc → Input Mapping Context:
     • IMC_Default
  3. Open IMC_Default → Add Mappings:
     • IA_Move → W/A/S/D (with Swizzle/Negate modifiers)
     • IA_Jump → Space Bar
     • IA_Look → Mouse XY2D
```

### Phase 6-7: Same as Example 10

GAS integration check and final verification run identically (no Editor needed).

---

## Anti-Patterns

| Don't | Do Instead |
|-------|------------|
| `ue_generate_code` 4번 각각 수동 호출 | "GAS 캐릭터 시스템" 한 번에 요청 |
| 생성 후 에러 수동 수정 | Validation loop 자동 수정 대기 |
| Build.cs 의존성 추측 | 출력의 Build.cs 목록 그대로 복사 |
| 단일 클래스로 시스템 구현 시도 | 시나리오 기반 멀티 클래스 분리 |
| Lyra 프로젝트에서 vanilla organism 사용 | `lyra_character` 등 Lyra organism 활용 |

## Version History

| Version | Lines per Scenario | Production Quality |
|---------|-------------------|-------------------|
| v1.1.0 | ~134 (estimates) | 11/17 features |
| v1.2.0 | ~319 (GAS Character) | 17/17 features |
| v1.3.0 | 225-362 (verified) | **4/4 classes Production** per scenario |
| v1.4.0 | 81-362 (all 10 verified) | **19 features, 34 classes, 2,008 lines** |
| v1.5.0 | 81-400+ (parameterized) | **23 features, 34 classes, parameterized values + game logic** |
| v1.6.0 | 81-400+ (26 features) | **26 features, 37 classes, +AoE/Inventory/Save, CurrentAmmo bug fix** |
| v2.0.0 | 81-400+ (50 features) | **50 features, 7-Phase E2E: +BP Creation, Input Setup, GAS Integration, Final Verification** |
