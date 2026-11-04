# /ue-test-record Reference

## Raw Event Format

`stop_input_recording` 응답에서 반환되는 이벤트 배열 구조:

```json
{
  "events": [
    {"time_ms": 0, "input_type": "key_down", "key": "W"},
    {"time_ms": 50, "input_type": "mouse_move", "x": 512, "y": 384},
    {"time_ms": 2000, "input_type": "key_up", "key": "W"},
    {"time_ms": 2100, "input_type": "key_down", "key": "SpaceBar"},
    {"time_ms": 2200, "input_type": "key_up", "key": "SpaceBar"}
  ],
  "duration_ms": 2200,
  "event_count": 5
}
```

**이벤트 필드**:

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `input_type` | string | O | `key_down`, `key_up`, `mouse_down`, `mouse_up`, `mouse_move` |
| `key` | string | key_* 시 | 키 이름 (예: `W`, `SpaceBar`, `LeftMouseButton`) |
| `x`, `y` | int | mouse_* 시 | 마우스 좌표 |
| `shift`, `ctrl`, `alt` | bool | - | 모디파이어 키 상태 |
| `delay_ms` | int | - | 이전 이벤트로부터의 지연 시간 |
| `time_ms` | float | - | 녹화 시작 기준 절대 시간 |

**제한**: 최대 10,000 이벤트, 최대 300초 녹화

---

## Latent Command Templates (Faithful Mode)

### FInjectInputEvent — 입력 이벤트 주입

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
    FInjectInputEvent,
    FAutomationTestBase*, Test,
    FString, InputType,
    FString, Key)

bool FInjectInputEvent::Update()
{
    if (!GEngine || !GEngine->GameViewport) return true;

    FSlateApplication& Slate = FSlateApplication::Get();
    TSharedPtr<SWindow> Window = Slate.GetActiveTopLevelRegularWindow();
    if (!Window.IsValid()) return true;

    if (InputType == TEXT("key_down"))
    {
        FKeyEvent KeyEvent(FKey(*Key), FModifierKeysState(), 0, false, 0, 0);
        Slate.ProcessKeyDownEvent(KeyEvent);
    }
    else if (InputType == TEXT("key_up"))
    {
        FKeyEvent KeyEvent(FKey(*Key), FModifierKeysState(), 0, false, 0, 0);
        Slate.ProcessKeyUpEvent(KeyEvent);
    }
    else if (InputType == TEXT("mouse_down"))
    {
        FPointerEvent MouseEvent(0, FVector2D::ZeroVector, FVector2D::ZeroVector,
            EKeys::LeftMouseButton, EInputEvent::IE_Pressed, 0, FModifierKeysState());
        Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(), MouseEvent);
    }
    else if (InputType == TEXT("mouse_up"))
    {
        FPointerEvent MouseEvent(0, FVector2D::ZeroVector, FVector2D::ZeroVector,
            EKeys::LeftMouseButton, EInputEvent::IE_Released, 0, FModifierKeysState());
        Slate.ProcessMouseButtonUpEvent(MouseEvent);
    }
    return true; // One-shot
}
```

### FInjectMouseMove — 마우스 이동 주입

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
    FInjectMouseMove,
    FAutomationTestBase*, Test,
    float, X,
    float, Y)

bool FInjectMouseMove::Update()
{
    FSlateApplication& Slate = FSlateApplication::Get();
    FVector2D Pos(X, Y);
    FPointerEvent MouseEvent(0, Pos, Pos, TSet<FKey>(), FKey(), 0, FModifierKeysState());
    Slate.ProcessMouseMoveEvent(MouseEvent);
    return true;
}
```

### FWaitMs — 프레임 기반 대기

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FWaitMs,
    float, DelayMs)

bool FWaitMs::Update()
{
    // Use FEngineWaitLatentCommand for frame-accurate waiting
    // This command uses accumulated delta time
    static double Elapsed = 0.0;
    Elapsed += FApp::GetDeltaTime() * 1000.0;
    if (Elapsed >= DelayMs)
    {
        Elapsed = 0.0;
        return true;
    }
    return false;
}
```

---

## Checkpoint Assertion Templates

### FCheckpointPosition — 위치 검증

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FCheckpointPosition,
    FAutomationTestBase*, Test,
    FVector, ExpectedPosition)

bool FCheckpointPosition::Update()
{
    UWorld* World = GEngine->GetWorldContexts()[0].World();
    if (!World) { Test->AddError(TEXT("No valid world")); return true; }

    ACharacter* Character = UGameplayStatics::GetPlayerCharacter(World, 0);
    if (!Character) { Test->AddError(TEXT("No player character")); return true; }

    FVector ActualPos = Character->GetActorLocation();
    float Distance = FVector::Dist(ActualPos, ExpectedPosition);

    Test->TestTrue(
        FString::Printf(TEXT("Position checkpoint (dist=%.1f, threshold=100)"), Distance),
        Distance < 100.f
    );
    return true;
}
```

### FCheckpointState — 상태 검증

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FCheckpointState,
    FAutomationTestBase*, Test,
    FString, StateName)

bool FCheckpointState::Update()
{
    UWorld* World = GEngine->GetWorldContexts()[0].World();
    ACharacter* Character = UGameplayStatics::GetPlayerCharacter(World, 0);
    if (!Character) { Test->AddError(TEXT("No player character")); return true; }

    if (StateName == TEXT("IsMoving"))
    {
        Test->TestTrue(TEXT("Character is moving"),
            Character->GetVelocity().Size() > 0.f);
    }
    else if (StateName == TEXT("IsFalling"))
    {
        Test->TestTrue(TEXT("Character is falling"),
            Character->GetCharacterMovement()->IsFalling());
    }
    else if (StateName == TEXT("IsGrounded"))
    {
        Test->TestTrue(TEXT("Character is grounded"),
            Character->GetCharacterMovement()->IsMovingOnGround());
    }
    return true;
}
```

---

## Semantic Pattern Detection (--semantic mode)

`ue-test-scenario`의 latent command 매핑을 재사용합니다.

### 패턴 감지 알고리즘

```text
For each event in recorded_events:
  1. key_down 이벤트 → 매칭되는 key_up 찾기
  2. delta = key_up.time_ms - key_down.time_ms
  3. 패턴 매칭:
     - W/A/S/D + delta > 500ms → Movement command
     - SpaceBar + delta < 200ms → Jump command
     - LeftMouseButton + delta < 300ms → Fire command
     - LeftShift sustained → Sprint
     - 인식 불가 → fallback to faithful (FInjectInputEvent)
```

### Semantic Command Templates (from ue-test-scenario)

```cpp
// Movement — keep ticking for duration
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FMoveForward, FAutomationTestBase*, Test, ACharacter*, Character)
bool FMoveForward::Update()
{
    if (!Character || !Character->IsValidLowLevel()) return true;
    Character->AddMovementInput(Character->GetActorForwardVector(), 1.0f);
    return false; // Keep ticking
}

// Jump — one-shot
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FPerformJump, ACharacter*, Character)
bool FPerformJump::Update()
{
    if (!Character) return true;
    Character->Jump();
    return true;
}

// Wait for landing
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FWaitForLanded, ACharacter*, Character)
bool FWaitForLanded::Update()
{
    if (!Character) return true;
    return Character->GetCharacterMovement()->IsMovingOnGround();
}

// Weapon fire (GAS/Lyra)
DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(
    FFireWeapon, FAutomationTestBase*, Test, ACharacter*, Character)
bool FFireWeapon::Update()
{
    if (!Character) return true;
    if (auto* ASC = Character->FindComponentByClass<UAbilitySystemComponent>())
    {
        FGameplayTag FireTag = FGameplayTag::RequestGameplayTag(
            FName("InputTag.Weapon.Fire"));
        ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(FireTag));
    }
    return true;
}

// Wait N seconds
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(
    FWaitSeconds, float, Duration)
bool FWaitSeconds::Update()
{
    static float Elapsed = 0.f;
    Elapsed += FApp::GetDeltaTime();
    if (Elapsed >= Duration) { Elapsed = 0.f; return true; }
    return false;
}
```

---

## Complete Test File Template (Faithful Mode)

```cpp
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "Tests/AutomationCommon.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

// ============================================================
// Latent Commands
// ============================================================

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FInjectInputEvent, ...)
// ... (see templates above)

DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(FInjectMouseMove, ...)
// ...

DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitMs, ...)
// ...

DEFINE_LATENT_AUTOMATION_COMMAND_TWO_PARAMETER(FCheckpointPosition, ...)
// ...

// ============================================================
// Test Class
// ============================================================

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    F{TestName},
    "{Module}.RecordedInput.{TestName}",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void F{TestName}::GetTests(
    TArray<FString>& OutBeautifiedNames,
    TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("Recorded Input Replay"));
    OutTestCommands.Add(TEXT(""));
}

bool F{TestName}::RunTest(const FString& Parameters)
{
    // --- Recorded Input Sequence ---
    // Generated from PIE recording on {Date}
    // Duration: {Duration}ms, Events: {EventCount}

    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_down"), TEXT("W")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(2000.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_up"), TEXT("W")));

    // --- Checkpoint: 2.0s ---
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector({X}, {Y}, {Z})));

    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_down"), TEXT("SpaceBar")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(100.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_up"), TEXT("SpaceBar")));

    // --- Checkpoint: 2.1s ---
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector({X2}, {Y2}, {Z2})));

    return true;
}
```

---

## Assertion Macros Quick Reference

| Macro | Usage | Example |
|-------|-------|---------|
| `TestTrue` | Boolean | `TestTrue(TEXT("Jumped"), Char->bWasJumping)` |
| `TestFalse` | Negation | `TestFalse(TEXT("Not grounded"), Char->IsGrounded())` |
| `TestEqual` | Exact match | `TestEqual(TEXT("HP"), Health, 100)` |
| `TestNearlyEqual` | Float tolerance | `TestNearlyEqual(TEXT("Dist"), Dist, 1000.f, 50.f)` |
| `TestNotNull` | Pointer check | `TestNotNull(TEXT("ASC"), ASC)` |

---

## Enhanced Input 호환 (Lyra/LCP 등)

### 문제

`FInjectInputEvent`는 `FSlateApplication::ProcessKeyDownEvent()`를 사용합니다.
Enhanced Input 프로젝트에서는 입력이 `UInputAction` → `UInputMappingContext` 경로로 처리되어,
Slate 레벨 입력 주입이 게임 로직까지 전달되지 않을 수 있습니다.

### 해결: `--semantic` 모드 사용

Semantic 모드의 latent command는 Character C++ API를 직접 호출하므로 입력 시스템과 무관합니다:

```cpp
// FMoveForward — Character->AddMovementInput() 직접 호출
// FPerformJump — Character->Jump() 직접 호출
// FFireWeapon — ASC->TryActivateAbilitiesByTag() 직접 호출
```

### Enhanced Input 직접 주입 (고급)

Enhanced Input을 통해 InputAction을 직접 트리거하려면:

```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_THREE_PARAMETER(
    FInjectInputAction,
    FAutomationTestBase*, Test,
    ACharacter*, Character,
    FName, ActionName)

bool FInjectInputAction::Update()
{
    if (!Character) return true;

    APlayerController* PC = Cast<APlayerController>(Character->GetController());
    if (!PC) { Test->AddError(TEXT("No PlayerController")); return true; }

    UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer());
    if (!Subsystem) { Test->AddError(TEXT("No EnhancedInput subsystem")); return true; }

    // Find the InputAction asset by name
    UInputAction* Action = FindObject<UInputAction>(
        ANY_PACKAGE, *ActionName.ToString());
    if (!Action)
    {
        Test->AddError(FString::Printf(TEXT("InputAction '%s' not found"), *ActionName.ToString()));
        return true;
    }

    // Inject the input value
    Subsystem->InjectInputForAction(Action, FInputActionValue(true),
        TArray<UInputModifier*>(), TArray<UInputTrigger*>());
    return true;
}
```

**사용 예**:
```cpp
// Enhanced Input: IA_Move InputAction 트리거
ADD_LATENT_AUTOMATION_COMMAND(FInjectInputAction(this, Character, FName("IA_Move")));
ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(2.0f));

// Enhanced Input: IA_Jump InputAction 트리거
ADD_LATENT_AUTOMATION_COMMAND(FInjectInputAction(this, Character, FName("IA_Jump")));
```

### Faithful 모드 대안: replay_input_recording

녹화된 이벤트를 Editor-side에서 재생하면 Enhanced Input과의 호환성이 더 높을 수 있습니다:

```python
# MCP operation으로 Editor에서 직접 재생
ue_editor_automation(
    operation="replay_input_recording",
    events=[...],  # stop에서 받은 이벤트 배열
    speed=1.0
)
```

---

## MCP Tool Reference

### ue_editor_automation

| Operation | Parameters | Response |
|-----------|------------|----------|
| `start_input_recording` | `max_duration_seconds` (1-300, default 60) | `{success, max_duration_seconds}` |
| `stop_input_recording` | (none) | `{events[], duration_ms, event_count}` |
| `replay_input_recording` | `events[]`, `speed` (0.1-10.0) | `{success, event_count, speed}` |
| `export_recording` | `events[]`, `mode`, `module_name`, `map_path`, `test_name`, `action_map` | `{cpp_code, cpp_path, actions_used, patterns_detected, lyra_mode}` |

#### export_recording Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `events` | array | (required) | Raw input events from `stop_input_recording` or manually constructed |
| `mode` | string | `"semantic"` | `"semantic"` (CQTest C++ code) or `"datadriven"` (JSON recipe — no recompile) |
| `module_name` | string | `"MyGame"` | UE module name for test path (e.g., `"LyraGame"`, `"LastContact"`) |
| `map_path` | string | `"/Game/Maps/DefaultMap"` | Map content path for FMapTestSpawner |
| `test_name` | string | `"RecordedPlayTest"` | Test class name (generates `FRecorded{test_name}Test`) |
| `action_map` | object | (auto-detected) | Key→InputAction name overrides (e.g., `{"LeftMouseButton": "IA_NormalAttack"}`) |
| `pawn_start` | object | (from stop) | Pawn position `{x,y,z,pitch,yaw,roll}` for replay teleport (Issue #7021) |
| `camera_start` | object | (alias) | Alias for `pawn_start` |
| `metadata` | object | (from stop) | Recording metadata `{avg_fps, resolution, player_class, engine_version}` (Issue #7022) |
| `playback_speed` | number | `1.0` | Recipe playback speed multiplier (Issue #7022) |

#### Datadriven JSON Recipe Schema (Issue #7021, #7022)

```json
{
  "version": 1,
  "test_name": "RecordedPlayTest",
  "module_name": "LyraGame",
  "map_path": "/Game/Maps/TestMap",
  "input_actions_path": "/Game/Input/Actions",
  "lyra_mode": true,
  "actions": [
    {"type": "hold", "action": "IA_Move", "value": [0,1], "duration": 2.0},
    {"type": "press", "action": "IA_Jump"},
    {"type": "wait", "duration": 0.5},
    {"type": "look", "action": "IA_Look_Mouse", "delta_x": 45.0, "delta_y": -10.0, "duration": 0.3}
  ],
  "camera_start": {
    "x": 100.0, "y": 200.0, "z": 300.0,
    "pitch": -10.0, "yaw": 45.0, "roll": 0.0
  },
  "metadata": {
    "avg_fps": 60.0,
    "resolution": "1920x1080",
    "player_class": "BP_Hero_C",
    "engine_version": "5.7.0-29121000+++UE5+Release-5.7"
  },
  "playback_speed": 1.0
}
```

**Optional fields**: `camera_start`, `metadata`, `playback_speed` are only present when captured/specified. Old recipes without these fields remain fully compatible (backward compat).

**camera_start**: When present in a recipe, the generated CQTest C++ code includes a `.Then()` step that teleports the pawn via `SetActorLocationAndRotation()` + `SetControlRotation()` before replay. During WebSocket replay (`replay_input_recording`), the pawn is teleported before events are scheduled.

#### action_map Override

Default key→InputAction mappings are auto-detected based on project type (Lyra vs non-Lyra). Use `action_map` to override specific keys when your project uses different InputAction names.

**Default mappings (non-Lyra)**:

| Key | Default InputAction |
|-----|-------------------|
| W | IA_Move (0,1) |
| S | IA_Move (0,-1) |
| A | IA_Move (-1,0) |
| D | IA_Move (1,0) |
| SpaceBar | IA_Jump |
| LeftMouseButton | IA_Weapon_Fire |
| RightMouseButton | IA_Weapon_ADS |
| LeftShift | IA_Sprint |
| E | IA_Interact |
| MouseMove | IA_Look_Mouse |

**Override example** (LCP_B project):
```json
{
  "LeftMouseButton": "IA_NormalAttack",
  "LeftShift": "IA_Dash",
  "E": "IA_Interaction"
}
```

> **Note**: User-supplied `action_map` entries are treated as `Bool` type (PressButton). Default Axis2D mappings (WASD→IA_Move, MouseMove→IA_Look) are preserved unless explicitly overridden.

#### Lyra vs Non-Lyra Codegen

| Aspect | Lyra | Non-Lyra |
|--------|------|----------|
| Detection | `module_name` starts with "Lyra" or `map_path` contains "Lyra" | Everything else |
| InputAction lookup | `LoadObject<UInputAction>("/Game/Input/Actions/IA_X.IA_X")` | `InputActionName = TEXT("IA_X")` (FindInputAction) |
| StartWhen | Checks `ULyraExperienceManagerComponent::IsExperienceLoaded` + EnhancedInput bindings | Checks `FindFirstPlayerPawn()` only |
| Test path | `Source/LyraGame/Tests/` | `Source/{module_name}/Tests/` |

### ue_generate_code

| Operation | Key Parameters | Response |
|-----------|---------------|----------|
| `generate_test` | `test_target`, `test_type="complex_latent"`, `test_name`, `module_name` | `{cpp_code, cpp_path, assertions_generated}` |
