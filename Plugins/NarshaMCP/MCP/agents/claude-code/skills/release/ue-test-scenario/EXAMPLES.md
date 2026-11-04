# /ue-test-scenario Examples

## Level 1: Single Action

**Input**: `"캐릭터가 10m 전진"`

**Generated Test** (`Source/LyraGame/Tests/LyraMoveForwardTest.cpp`):
```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "GameFramework/Character.h"

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FMoveForwardAndVerify,
    FAutomationTestBase*, Test,
    ACharacter*, Character)

bool FMoveForwardAndVerify::Update()
{
    if (!Character) return true;
    Character->AddMovementInput(Character->GetActorForwardVector(), 1.0f);
    return false; // Keep ticking until timeout
}

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FVerifyDisplacement,
    FAutomationTestBase*, Test,
    FVector, StartLocation)

bool FVerifyDisplacement::Update()
{
    float Dist = FVector::Dist(Character->GetActorLocation(), StartLocation);
    Test->TestTrue(TEXT("PASS: Displacement 1000 cm >= 1000 cm"),
                   Dist >= 1000.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FLyraMoveForwardTest,
    "LyraGame.Scenario.MoveForward10m",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLyraMoveForwardTest::RunTest(const FString& Parameters)
{
    // Setup: Spawn character in empty level
    // ADD_LATENT_AUTOMATION_COMMAND(FMoveForwardAndVerify(this, Character));
    // ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(3.0f));
    // ADD_LATENT_AUTOMATION_COMMAND(FVerifyDisplacement(this, StartLoc));
    return true;
}
```

---

## Level 2: Two Actions

**Input**: `"캐릭터가 10m 전진 후 점프"`

**Parsed Actions**:
```json
{
  "actions": [
    {"type": "move", "direction": "forward", "distance_cm": 1000},
    {"type": "jump", "wait_for_land": true}
  ],
  "assertions": [
    {"check": "displacement", "operator": ">=", "value": 1000},
    {"check": "is_falling", "value": true},
    {"check": "jump_height", "operator": ">=", "value": 50.0},
    {"check": "landed", "value": true}
  ]
}
```

**Expected Assertion Output** (#6797 JSON format):
```json
{
  "assertions": [
    {"name": "Displacement", "pass": true, "actual": 1002.0, "expected": ">= 1000"},
    {"name": "IsFalling detected", "pass": true, "detail": "IsFalling() was true"},
    {"name": "Jump height", "pass": true, "actual": 127.5, "expected": ">= 50.0"},
    {"name": "Landing", "pass": true, "detail": "Returned to ground"}
  ]
}
```

---

## Level 3: Three+ Action Chaining

**Input**: `"10m 전진 → 점프 → 착지 후 사격"`

**Parsed Actions**:
```json
{
  "actions": [
    {"type": "move", "distance_cm": 1000},
    {"type": "jump", "wait_for_land": true},
    {"type": "fire_weapon"}
  ]
}
```

**Command Sequence**:
```cpp
ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(3.0f));
ADD_LATENT_AUTOMATION_COMMAND(FVerifyDisplacement(this, StartLoc, 1000.f));
ADD_LATENT_AUTOMATION_COMMAND(FPerformJump(Character));
ADD_LATENT_AUTOMATION_COMMAND(FWaitForLanded(Character));
ADD_LATENT_AUTOMATION_COMMAND(FFireWeapon(this, Character));
ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(1.0f));
ADD_LATENT_AUTOMATION_COMMAND(FVerifyWeaponFired(this, Character));
```

---

## Level 4: Conditional Scenario (Unsupported)

**Input**: `"적이 5m 이내에 있으면 사격, 아니면 접근"`

**Response**:
```
조건부 시나리오는 현재 미지원입니다.

대안:
1. 두 개의 별도 시나리오로 분리:
   - "적이 5m 이내에 있을 때 사격"
   - "적이 5m 밖에 있을 때 접근"
2. FunctionalTest Actor 사용 (수동 구현 필요)
```
