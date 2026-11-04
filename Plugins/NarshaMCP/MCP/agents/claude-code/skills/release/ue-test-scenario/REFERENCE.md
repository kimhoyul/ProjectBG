# /ue-test-scenario Reference

## Latent Command Templates

### Basic Movement

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FMoveForward,
    FAutomationTestBase*, Test,
    ACharacter*, Character)

bool FMoveForward::Update()
{
    if (!Character || !Character->IsValidLowLevel()) return true;
    Character->AddMovementInput(Character->GetActorForwardVector(), 1.0f);
    return false; // Keep ticking
}
```

### Jump + Wait for Landing

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FPerformJump,
    ACharacter*, Character)

bool FPerformJump::Update()
{
    if (!Character) return true;
    Character->Jump();
    return true; // One-shot
}

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FWaitForLanded,
    ACharacter*, Character)

bool FWaitForLanded::Update()
{
    if (!Character) return true;
    return Character->GetCharacterMovement()->IsMovingOnGround();
}
```

### Wait N Seconds

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FWaitSeconds,
    float, Duration)

bool FWaitSeconds::Update()
{
    static float Elapsed = 0.f;
    Elapsed += FApp::GetDeltaTime();
    if (Elapsed >= Duration) { Elapsed = 0.f; return true; }
    return false;
}
```

### Weapon Fire (Lyra/GAS)

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FFireWeapon,
    FAutomationTestBase*, Test,
    ACharacter*, Character)

bool FFireWeapon::Update()
{
    if (!Character) return true;
    // Lyra: Simulate InputTag.Weapon.Fire via AbilitySystemComponent
    if (auto* ASC = Character->FindComponentByClass<UAbilitySystemComponent>())
    {
        FGameplayTag FireTag = FGameplayTag::RequestGameplayTag(FName("InputTag.Weapon.Fire"));
        ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(FireTag));
    }
    return true;
}
```

## Assertion Macros

| Macro | Usage | Example |
|-------|-------|---------|
| `TestTrue` | Boolean check | `TestTrue(TEXT("Jumped"), Char->bWasJumping)` |
| `TestFalse` | Negation | `TestFalse(TEXT("Not grounded"), Char->IsGrounded())` |
| `TestEqual` | Exact match | `TestEqual(TEXT("HP"), Health, 100)` |
| `TestNearlyEqual` | Float tolerance | `TestNearlyEqual(TEXT("Dist"), Dist, 1000.f, 50.f)` |
| `TestNotNull` | Pointer check | `TestNotNull(TEXT("ASC"), ASC)` |

## Test File Template

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
// Project-specific includes (auto-detected)

// --- Latent Commands ---
// (Generated per scenario)

// --- Test Class ---
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    F{TestName},
    "{Module}.{Category}.{TestName}",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::ProductFilter)

void F{TestName}::GetTests(TArray<FString>& OutBeautifiedNames,
                            TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("{ScenarioName}"));
    OutTestCommands.Add(TEXT(""));
}

bool F{TestName}::RunTest(const FString& Parameters)
{
    // Setup: Find/spawn character
    // Enqueue latent commands
    ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(2.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FVerifyDisplacement(this, Character, 1000.f));
    ADD_LATENT_AUTOMATION_COMMAND(FPerformJump(Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForLanded(Character));
    // ...
    return true;
}
```

## Project Pattern Detection

| Pattern | Detection Method | Impact |
|---------|-----------------|--------|
| Lyra GAS | `ue_grep("UAbilitySystemComponent")` | InputTag-based activation |
| Enhanced Input | `ue_grep("UInputAction")` | Action-based input |
| Test Helpers | `ue_glob("*TestHelper*")` | Reuse existing helpers |
| PIE World | `ue_grep("FindLyraPIEWorld")` | World access pattern |
| Vanilla UE | Default | Direct API calls |
