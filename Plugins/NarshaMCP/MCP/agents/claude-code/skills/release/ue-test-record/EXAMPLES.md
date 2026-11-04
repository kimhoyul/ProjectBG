# /ue-test-record Examples

## Level 1: W Key Tap (3 Events)

### Input

```bash
/ue-test-record start
# (W키를 한 번 짧게 누름)
/ue-test-record stop
/ue-test-record export --test-name=FSimpleKeyTapTest --module=MyGame
```

### stop 결과

```text
녹화 완료:
- 이벤트 수: 3
- 녹화 시간: 350ms
- 내역: key_down 1개, key_up 1개, mouse_move 1개
```

Raw events:
```json
{
  "events": [
    {"time_ms": 0, "input_type": "key_down", "key": "W"},
    {"time_ms": 150, "input_type": "mouse_move", "x": 640, "y": 480},
    {"time_ms": 300, "input_type": "key_up", "key": "W"}
  ],
  "duration_ms": 350,
  "event_count": 3
}
```

### export 결과 (faithful)

```cpp
IMPLEMENT_COMPLEX_AUTOMATION_TEST(
    FSimpleKeyTapTest,
    "MyGame.RecordedInput.FSimpleKeyTapTest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

void FSimpleKeyTapTest::GetTests(
    TArray<FString>& OutBeautifiedNames,
    TArray<FString>& OutTestCommands) const
{
    OutBeautifiedNames.Add(TEXT("Simple Key Tap"));
    OutTestCommands.Add(TEXT(""));
}

bool FSimpleKeyTapTest::RunTest(const FString& Parameters)
{
    // Recorded: 3 events, 350ms
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_down"), TEXT("W")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(150.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectMouseMove(this, 640.f, 480.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(150.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_up"), TEXT("W")));

    return true;
}
```

---

## Level 2: Movement + Jump (10 Events)

### Input

```bash
/ue-test-record start
# (W키 2초 유지 후 SpaceBar 점프)
/ue-test-record stop
/ue-test-record export --test-name=FMoveJumpTest --module=LyraGame
```

### stop 결과

```text
녹화 완료:
- 이벤트 수: 10
- 녹화 시간: 3,200ms
- 내역: key_down 3개, key_up 3개, mouse_move 4개
```

Raw events:
```json
{
  "events": [
    {"time_ms": 0, "input_type": "key_down", "key": "W"},
    {"time_ms": 200, "input_type": "mouse_move", "x": 640, "y": 480},
    {"time_ms": 600, "input_type": "mouse_move", "x": 645, "y": 478},
    {"time_ms": 1200, "input_type": "mouse_move", "x": 650, "y": 475},
    {"time_ms": 1800, "input_type": "mouse_move", "x": 655, "y": 472},
    {"time_ms": 2000, "input_type": "key_up", "key": "W"},
    {"time_ms": 2100, "input_type": "key_down", "key": "SpaceBar"},
    {"time_ms": 2200, "input_type": "key_up", "key": "SpaceBar"},
    {"time_ms": 2800, "input_type": "key_down", "key": "W"},
    {"time_ms": 3200, "input_type": "key_up", "key": "W"}
  ],
  "duration_ms": 3200,
  "event_count": 10
}
```

### export 결과 (faithful — default)

```cpp
bool FMoveJumpTest::RunTest(const FString& Parameters)
{
    // Recorded: 10 events, 3200ms

    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_down"), TEXT("W")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(200.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectMouseMove(this, 640.f, 480.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(400.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectMouseMove(this, 645.f, 478.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(600.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectMouseMove(this, 650.f, 475.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(600.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectMouseMove(this, 655.f, 472.f));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(200.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_up"), TEXT("W")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(100.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_down"), TEXT("SpaceBar")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(100.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_up"), TEXT("SpaceBar")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(600.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_down"), TEXT("W")));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitMs(400.f));
    ADD_LATENT_AUTOMATION_COMMAND(FInjectInputEvent(this, TEXT("key_up"), TEXT("W")));

    return true;
}
```

### export 결과 (`--semantic`)

```cpp
bool FMoveJumpTest::RunTest(const FString& Parameters)
{
    // Recorded: 10 events, 3200ms (semantic mode)

    UWorld* World = GEngine->GetWorldContexts()[0].World();
    ACharacter* Character = UGameplayStatics::GetPlayerCharacter(World, 0);
    TestNotNull(TEXT("Player character exists"), Character);

    // Action 1: Move forward 2 seconds
    ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(2.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector(200.f, 0.f, 0.f)));

    // Action 2: Jump
    ADD_LATENT_AUTOMATION_COMMAND(FPerformJump(Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForLanded(Character));
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointState(this, TEXT("IsGrounded")));

    // Action 3: Move forward 0.4 seconds
    ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(0.4f));
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector(240.f, 0.f, 0.f)));

    return true;
}
```

---

## Level 3: Combat Sequence (25+ Events)

### Input

```bash
/ue-test-record start
# (WASD 이동 + 마우스 에임 + 좌클릭 사격, 약 8초)
/ue-test-record stop
/ue-test-record export --test-name=FCombatSequenceTest --module=LyraGame --semantic
```

### stop 결과

```text
녹화 완료:
- 이벤트 수: 28
- 녹화 시간: 8,400ms
- 내역: key_down 6개, key_up 6개, mouse_move 12개, mouse_down 2개, mouse_up 2개
```

### export 결과 (--semantic)

```cpp
bool FCombatSequenceTest::RunTest(const FString& Parameters)
{
    // Recorded: 28 events, 8400ms (semantic mode)

    UWorld* World = GEngine->GetWorldContexts()[0].World();
    ACharacter* Character = UGameplayStatics::GetPlayerCharacter(World, 0);
    TestNotNull(TEXT("Player character exists"), Character);

    // Action 1: Move forward 3 seconds
    ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(3.0f));

    // --- Checkpoint: 3.0s ---
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector(300.f, 0.f, 0.f)));

    // Action 2: Strafe right 1.5 seconds
    ADD_LATENT_AUTOMATION_COMMAND(FMoveRight(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(1.5f));

    // Action 3: Fire weapon (2 shots)
    ADD_LATENT_AUTOMATION_COMMAND(FFireWeapon(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(0.5f));
    ADD_LATENT_AUTOMATION_COMMAND(FFireWeapon(this, Character));

    // --- Checkpoint: 5.0s ---
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector(300.f, 150.f, 0.f)));

    // Action 4: Move forward + jump
    ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(1.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FPerformJump(Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitForLanded(Character));

    // Action 5: Sprint forward
    ADD_LATENT_AUTOMATION_COMMAND(FStartSprint(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FMoveForward(this, Character));
    ADD_LATENT_AUTOMATION_COMMAND(FWaitSeconds(2.0f));
    ADD_LATENT_AUTOMATION_COMMAND(FStopSprint(this, Character));

    // --- Checkpoint: 8.4s ---
    ADD_LATENT_AUTOMATION_COMMAND(FCheckpointPosition(this, FVector(700.f, 150.f, 0.f)));

    return true;
}
```

---

## Level 4: Empty Recording (0 Events)

### Input

```bash
/ue-test-record start
# (즉시 stop — 아무 입력 없음)
/ue-test-record stop
```

### stop 결과

```text
녹화된 입력 이벤트가 없습니다 (0 events, 120ms).
더 긴 시간 동안 키보드/마우스 입력을 하며 플레이한 후 다시 시도하세요.

힌트:
- WASD로 캐릭터를 이동하세요
- SpaceBar로 점프하세요
- 마우스 좌클릭으로 사격하세요
```

### export 시도

```text
녹화된 이벤트가 없습니다. `/ue-test-record stop`을 먼저 실행하세요.
(이전 `stop`에서 0 이벤트가 반환되었습니다)
```

---

## Level 5: Non-Lyra Project — IMC Auto-Detection (LCP_B)

This example shows using `export_recording` with a non-Lyra project. **No `action_map` or `input_actions_path` needed** — the server auto-detects from project IMC metadata (Issue #6938).

### How Auto-Detection Works

1. Server reads `input_mappings` metadata from the project's indexed InputMappingContexts
2. Extracts key→InputAction mappings (e.g., `LeftMouseButton` → `IA_NormalAttack`)
3. Derives `input_actions_path` from action asset paths (e.g., `/Game/Core/Input/InputAction`)
4. Detects Lyra mode from IMC names (`KBM`) or action names (`IA_Look_Mouse`)

**3-tier priority**: `user-supplied action_map` > `IMC auto-detect` > `hardcoded defaults`

### Scenario 1: 3x Combo Attack (auto-detected)

```python
ue_editor_automation(
    operation="export_recording",
    events=[
        {"input_type": "mouse_down", "key": "LeftMouseButton", "time_ms": 0},
        {"input_type": "mouse_up", "key": "LeftMouseButton", "time_ms": 80},
        {"input_type": "mouse_down", "key": "LeftMouseButton", "time_ms": 500},
        {"input_type": "mouse_up", "key": "LeftMouseButton", "time_ms": 580},
        {"input_type": "mouse_down", "key": "LeftMouseButton", "time_ms": 1000},
        {"input_type": "mouse_up", "key": "LeftMouseButton", "time_ms": 1080}
    ],
    mode="semantic",
    module_name="LastContact",
    map_path="/ShooterMaps/Maps/L_Expanse",
    test_name="LCComboAttackTest"
    # No action_map needed! Auto-detected from IMC metadata.
)
```

### export 결과

```text
patterns_detected: {press_button: 3}
actions_used: ["IA_NormalAttack"]
lyra_mode: false
auto_detection: {source: "imc_metadata", imc_count: 4, mappings_found: 12}
```

Generated code (key sections):
```cpp
// Non-Lyra: uses InputActionName = TEXT() instead of LoadObject
struct FPress_IA_NormalAttack : public FTestAction
{
    FPress_IA_NormalAttack()
    {
        InputActionName = TEXT("IA_NormalAttack");
        InputActionValue = FInputActionValue(true);
    }
};

// Test steps: 3x combo
.Then([this]() { Actions->IA_NormalAttack(); })  // Step 1
.Then([this]() { Actions->IA_NormalAttack(); })  // Step 2
.Then([this]() { Actions->IA_NormalAttack(); })  // Step 3
```

### Scenario 2: Movement + Combo + Dash (auto-detected)

```python
ue_editor_automation(
    operation="export_recording",
    events=[
        {"input_type": "key_down", "key": "W", "time_ms": 0},
        {"input_type": "mouse_move", "key": "None", "time_ms": 200, "x": 5, "y": 0},
        {"input_type": "key_up", "key": "W", "time_ms": 2000},
        {"input_type": "mouse_down", "key": "LeftMouseButton", "time_ms": 2200},
        {"input_type": "mouse_up", "key": "LeftMouseButton", "time_ms": 2280},
        {"input_type": "mouse_down", "key": "LeftMouseButton", "time_ms": 2700},
        {"input_type": "mouse_up", "key": "LeftMouseButton", "time_ms": 2780},
        {"input_type": "mouse_down", "key": "LeftMouseButton", "time_ms": 3200},
        {"input_type": "mouse_up", "key": "LeftMouseButton", "time_ms": 3280},
        {"input_type": "key_down", "key": "LeftShift", "time_ms": 3800},
        {"input_type": "key_up", "key": "LeftShift", "time_ms": 3900},
        {"input_type": "key_down", "key": "W", "time_ms": 4000},
        {"input_type": "key_up", "key": "W", "time_ms": 5500}
    ],
    mode="semantic",
    module_name="LastContact",
    map_path="/ShooterMaps/Maps/L_Expanse",
    test_name="LCMovementComboTest"
    # action_map auto-detected from IMC: LeftMouseButton→IA_NormalAttack, LeftShift→IA_Dash, etc.
)
```

### export 결과

```text
patterns_detected: {hold_axis: 2, press_button: 4, wait: 1}
actions_used: ["IA_Move", "IA_NormalAttack", "IA_Dash"]
latent_commands_generated: 7
```

Generated sequence:
```text
Step 1: HoldAxis(IA_Move, 2.0s)      — W key forward
Step 2: PressButton(IA_NormalAttack)  — combo 1
Step 3: PressButton(IA_NormalAttack)  — combo 2
Step 4: PressButton(IA_NormalAttack)  — combo 3
Step 5: Wait(0.5s)                    — gap
Step 6: PressButton(IA_Dash)          — dash
Step 7: HoldAxis(IA_Move, 1.5s)      — post-dash movement
```

### Build.cs dependency

```csharp
// Add to PrivateDependencyModuleNames in {Module}Editor.Build.cs
"CQTest",
"CQTestEnhancedInput",
"EnhancedInput",
"InputCore",
```

---

## Level 6: Data-Driven Mode (No Recompile — Issue #6938)

> `mode="datadriven"` outputs a JSON recipe instead of C++ code.
> A single C++ runner (`CUDataDrivenRecordedTest.cpp`) auto-discovers all JSON files
> in `Tests/Recordings/` and runs them. **No recompilation when adding new recordings.**

### Workflow

```text
[기존] 녹화 → .cpp 생성 → 빌드(7분) → 테스트 실행
[변경] 녹화 → .json 저장 → 테스트 실행 (빌드 불필요!)
```

### Export as JSON recipe

```python
ue_editor_automation(
    operation="export_recording",
    events=[...],  # from stop_input_recording
    mode="datadriven",
    module_name="LastContact",
    map_path="/Game/Maps/DefaultMap",
    test_name="LCComboAttackTest"
    # action_map auto-detected from IMC metadata (Issue #6938)
    # Override only if needed: action_map={"LeftMouseButton": "IA_NormalAttack"}
)
```

### export 결과

```json
{
  "success": true,
  "mode": "datadriven",
  "recipe": {
    "version": 1,
    "test_name": "LCComboAttackTest",
    "map_path": "/Game/Maps/DefaultMap",
    "actions": [
      { "type": "press", "action": "IA_NormalAttack" },
      { "type": "wait", "duration": 0.5 },
      { "type": "press", "action": "IA_NormalAttack" },
      { "type": "hold", "action": "IA_Move", "value": [0.0, 1.0], "duration": 2.0 }
    ]
  },
  "json_path": "Tests/Recordings/LCComboAttackTest.json",
  "auto_detection": {
    "source": "imc_metadata",
    "imc_count": 4,
    "mappings_found": 12,
    "input_actions_path_auto": "/Game/Core/Input/InputAction"
  }
}
```

### JSON 파일 저장 위치

```text
ProjectRoot/
├── Tests/
│   └── Recordings/
│       ├── LCComboAttackTest.json    ← 녹화 1
│       ├── LCMovementComboTest.json  ← 녹화 2
│       └── LCDodgeTest.json          ← 녹화 3 (빌드 없이 추가!)
└── Source/
    └── LastContactEditor/Tests/
        └── CUDataDrivenRecordedTest.cpp  ← 러너 (1회 컴파일)
```

### C++ 러너 (1회 설치)

`CUDataDrivenRecordedTest.cpp`를 프로젝트 `Source/{Module}Editor/Tests/`에 복사 후 1회 빌드.
이후 새 JSON 레시피를 `Tests/Recordings/`에 추가할 때마다 빌드 없이 Automation 탭에서 실행 가능.

**핵심 구현 패턴**:

1. **맵 로딩**: `AutomationOpenMap(Recipe.MapPath)` — World Partition 호환
   - `FEditorLoadMap` + `FStartPIECommand` 대신 사용 (UE 5.7)
   - `UEditorEngine::AutomationLoadMap` 델리게이트로 PIE 시작 + WP 초기화 처리
   - `FWaitForMapToLoadCommand` 자동 추가됨

2. **입력 재생**: `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction()`
   - `LoadObject<UInputAction>()` 으로 에셋 동적 로드 (e.g. `/Game/Input/Actions/IA_Attack.IA_Attack`)
   - Stateful `IAutomationLatentCommand` — 매 프레임 `Update()` 호출, 스텝별 타이밍 제어
   - Press: 1프레임 `InjectInputForAction(IA, true)` → 다음 스텝
   - Hold: duration 동안 매 프레임 `InjectInputForAction(IA, FVector2D)` 반복
   - Look: duration 동안 delta를 프레임 비율로 분산 inject
   - Wait: duration 경과까지 대기

3. **실행 흐름**:
   ```
   AutomationOpenMap → FWaitLatentCommand(2s) → FCUWaitForPawn(60s timeout)
   → FCUExecuteRecipeSteps(stateful, per-frame) → FCUEndPIE
   ```

---

## Edge Case: 60-Second Auto-Stop

### stop 결과

```text
녹화가 60초 제한에 도달하여 자동 종료되었습니다.
- 이벤트 수: 847
- 녹화 시간: 60,000ms
- 내역: key_down 124개, key_up 124개, mouse_move 580개, mouse_down 10개, mouse_up 9개

주의: 이벤트 수가 많아 export 시 큰 .cpp 파일이 생성됩니다.
--semantic 모드를 권장합니다.
```
