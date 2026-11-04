# BP-to-C++ Examples

## Example 1: Simple BP (< 10 nodes)

### User Request
```text
BP_HealthPickup을 C++로 변환해줘
```

### Phase 0: Discovery
```python
ue_glob(params={"pattern": "Source/**/*.Build.cs"})
# → Source/MyGame/MyGame.Build.cs ✅ (C++ project confirmed)

ue_manage_blueprint(operation="get_structure", params={
    "blueprint_name": "BP_HealthPickup"
})
# → 6 nodes, Parent: AActor → Simple path
```

### Phase 1: Structure
- **Events**: BeginPlay, OnComponentBeginOverlap
- **Variables**: HealAmount (float, default 25.0), bIsActive (bool)
- **Components**: SphereCollision, StaticMesh, RotatingMovement

### Phase 2: Flow
```text
BeginPlay → SetupCollision
OnOverlap → CheckIsActive → ApplyHeal → DestroyActor
```

### Phase 3-4: Mapping + Complete Code Generation

**Step 1**: `derive_class` generates skeleton (class + constructor).
**Step 2**: Claude enriches with ALL variables, components, and function bodies from Phase 1-3.

```cpp
// AHealthPickup.h — COMPLETE (not just skeleton)
UCLASS()
class MYGAME_API AHealthPickup : public AActor
{
    GENERATED_BODY()
public:
    AHealthPickup();
    virtual void BeginPlay() override;

    // Variables from Phase 1 (search_variables)
    UPROPERTY(EditAnywhere, Category = "Pickup")
    float HealAmount = 25.0f;

    UPROPERTY(VisibleAnywhere)
    bool bIsActive = true;

private:
    // Components from Phase 1
    UPROPERTY(VisibleAnywhere)
    USphereComponent* SphereCollision;

    UPROPERTY(VisibleAnywhere)
    UStaticMeshComponent* MeshComp;

    UPROPERTY(VisibleAnywhere)
    URotatingMovementComponent* RotatingMovement;

    // Function from Phase 1 (get_function_details)
    UFUNCTION()
    void OnOverlapBegin(UPrimitiveComponent* OverlappedComp,
        AActor* OtherActor, UPrimitiveComponent* OtherComp,
        int32 OtherBodyIndex, bool bFromSweep,
        const FHitResult& SweepResult);
};
```

```cpp
// AHealthPickup.cpp — COMPLETE function bodies from Phase 3 node mapping
#include "AHealthPickup.h"
#include "Components/SphereComponent.h"
#include "GameFramework/RotatingMovementComponent.h"

AHealthPickup::AHealthPickup()
{
    // Components → CreateDefaultSubobject (REFERENCE.md: Component Operations)
    SphereCollision = CreateDefaultSubobject<USphereComponent>(TEXT("SphereCollision"));
    RootComponent = SphereCollision;

    MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
    MeshComp->SetupAttachment(RootComponent);

    RotatingMovement = CreateDefaultSubobject<URotatingMovementComponent>(TEXT("RotatingMovement"));

    // ComponentBoundEvent → delegate binding (REFERENCE.md: Events)
    SphereCollision->OnComponentBeginOverlap.AddDynamic(this, &AHealthPickup::OnOverlapBegin);
}

void AHealthPickup::BeginPlay()
{
    Super::BeginPlay();
}

// Function body mapped from Phase 3 node-by-node:
// OnOverlap(8 nodes) → IsValid → Branch → ApplyDamage → Destroy
void AHealthPickup::OnOverlapBegin(UPrimitiveComponent* OverlappedComp,
    AActor* OtherActor, UPrimitiveComponent* OtherComp,
    int32 OtherBodyIndex, bool bFromSweep,
    const FHitResult& SweepResult)
{
    // IsValid macro → nullptr check (REFERENCE.md: Variables)
    if (!IsValid(OtherActor)) return;

    // Branch node → if (REFERENCE.md: Flow Control)
    if (bIsActive)
    {
        // ApplyDamage → UGameplayStatics (REFERENCE.md: Gameplay)
        UGameplayStatics::ApplyDamage(OtherActor, -HealAmount, nullptr, this, nullptr);

        // DestroyActor → Destroy() (REFERENCE.md: Actor Operations)
        Destroy();
    }
}
```
```

### Phase 5: Validation
```text
Functions Converted: 3/3 (100%)
Variables Migrated: 2/2 (100%)
Components: 3/3 (100%)
Build Check: PASS
Unsupported Nodes: 0
```

---

## Example 2: Medium BP (20 nodes) with unsupported nodes

### User Request
```text
convert BP_DoorInteraction to C++
```

### Phase 0: Discovery
```python
ue_manage_blueprint(operation="get_structure", params={
    "blueprint_name": "BP_DoorInteraction"
})
# → 22 nodes, Parent: AActor, has Timeline node → Medium path
```

### Phase 3: Mapping (partial — Timeline is TODO)
```cpp
void ADoorInteraction::Interact(AActor* Interactor)
{
    if (!bIsLocked)
    {
        bIsOpen = !bIsOpen;
        // TODO: Manual conversion required for Timeline node
        // Original: Timeline "DoorTimeline" controls door rotation
        // Suggested: Use FTimeline with FOnTimelineFloat delegate
        // DoorTimeline.PlayFromStart() / DoorTimeline.Reverse()
        PlayDoorSound();
    }
}
```

### Phase 5: Validation
```text
Functions Converted: 4/5 (80%)
  - BeginPlay ............... CONVERTED
  - Interact ................ CONVERTED (with TODO)
  - PlayDoorSound ........... CONVERTED
  - OnTimelineUpdate ........ TODO (Timeline callback)
  - OnTimelineFinished ...... TODO (Timeline callback)

Unsupported Nodes: 1 (Timeline)
  Action: FTimeline skeleton generated, curve setup manual
```

---

## Example 3: Dry-run preview

### User Request
```text
BP_EnemyAI C++로 변환해줘 --dry-run
```

### Output (Phase 3 only, no files generated)
```text
=== Dry Run: BP_EnemyAI → AEnemyAI ===

Parent Class: ACharacter
Estimated Complexity: Medium (28 nodes)
Recommended: Single class

Functions to convert (8):
  BeginPlay ............... LOW (override)
  Tick .................... LOW (override)
  PatrolToNextPoint ....... MEDIUM (AI MoveTo → TODO)
  DetectPlayer ............ MEDIUM (LineTrace + Branch)
  StartChasing ............ LOW (SetTimer + variable set)
  StopChasing ............. LOW (ClearTimer + variable set)
  TakeDamage .............. MEDIUM (ApplyDamage delegate)
  OnDeath ................. MEDIUM (PlayMontage + Delay → TODO)

Variables (6): Health, MaxHealth, PatrolPoints, DetectionRange, ChaseSpeed, bIsChasing
Components (3): CapsuleComponent, SkeletalMesh, PawnSensing

Unsupported nodes: 2
  - AI MoveTo (latent) → suggest UAIBlueprintHelperLibrary
  - Delay (latent) → suggest FTimerHandle

Proceed with conversion? [Y/n]
```

---

## Example 4: No Source/ directory

### User Request
```text
BP_Player를 C++로 변환해줘
```

### Phase 0: Discovery
```python
ue_glob(params={"pattern": "Source/**/*.Build.cs"})
# → 0 results — no C++ project structure!
```

### Output
```text
This is a Blueprint-only project (no Source/ directory found).

Before converting BP logic to C++, you need to add C++ project structure.
Run: /bp-cpp-project-init

After that, run this skill again:
/bp-to-cpp BP_Player
```
