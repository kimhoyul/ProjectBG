# UE Automation Test Builder — Advanced

Gauntlet, CI/CD, headless 실행, 성능 테스트, 병렬 테스트 생성.

---

## Table of Contents

1. [Gauntlet Framework](#1-gauntlet-framework)
2. [CI/CD Headless Execution](#2-cicd-headless-execution)
3. [Performance Testing](#3-performance-testing-automatedperftesting)
4. [Test Module Separation](#4-test-module-separation)
5. [PythonAutomationTest Plugin](#5-pythonautomationtest-plugin)
6. [Agent Teams Parallel Generation](#6-agent-teams-parallel-test-generation)
7. [compile_project Operation](#7-compile_project-operation)
8. [Screenshot Regression Testing](#8-screenshot-regression-testing)
9. [FProperty Reflection Bypass](#9-fproperty-reflection-bypass)
10. [Dedicated Test Level Creation](#10-dedicated-test-level-creation)
11. [FPS Throttle & PIE Test Timing](#11-fps-throttle--pie-test-timing)
12. [GAS Test World Setup Pattern](#12-gas-test-world-setup-pattern)

---

## 1. Gauntlet Framework

대규모 자동화 테스트 프레임워크. `UGauntletTestController` 기반의 상태 머신 테스트.

### When to Use

- 게임 부팅 시퀀스 검증 (Smoke Test)
- 장시간 안정성 테스트 (Soak Test, 4-24시간)
- 성능 벤치마크 (FPS, 메모리, 프레임 타임)
- CI/CD 파이프라인 통합 (RunUAT.bat)

### Controller Architecture

```cpp
// Test Controller 상태 머신
UCLASS()
class UMyGameSmokeTest : public UGauntletTestController
{
    GENERATED_BODY()

protected:
    virtual void OnInit() override
    {
        // Phase 0: 초기화 + 타임아웃 설정
        SetMaxDuration(120.0f); // 2분
    }

    virtual void OnTick(float TimeDelta) override
    {
        switch (CurrentPhase)
        {
        case 0: // 메인 메뉴 대기
            if (IsMainMenuReady())
            {
                CurrentPhase = 1;
                StartNewGame();
            }
            break;

        case 1: // 게임 로딩 대기
            if (IsGameplayReady())
            {
                CurrentPhase = 2;
                BeginTests();
            }
            break;

        case 2: // 테스트 실행
            RunGameplayTests();
            if (AllTestsComplete())
            {
                EndTest(0); // 성공
            }
            break;
        }
    }

private:
    int32 CurrentPhase = 0;
};
```

### RunUAT.bat Execution

```bash
# Smoke Test
RunUAT.bat RunUnreal \
  -project=MyGame.uproject \
  -test=MyGameSmokeTest \
  -build=Editor \
  -platform=Win64 \
  -configuration=Development

# Soak Test (4시간)
RunUAT.bat RunUnreal \
  -project=MyGame.uproject \
  -test=SoakTestController \
  -build=Editor \
  -platform=Win64 \
  -timeout=14400

# 결과 리포트
RunUAT.bat RunUnreal \
  -project=MyGame.uproject \
  -test=MyGameSmokeTest \
  -ReportExportPath="Saved/Reports/" \
  -ReportType=html
```

### Build.cs

```csharp
PublicDependencyModuleNames.Add("Gauntlet");
```

> **Multi-Platform Orchestration**: 쿠킹된 빌드에서 멀티플랫폼 테스트 실행, 서버+N클라이언트 멀티플레이어 테스트, CSV 프로파일링 → HTML 리포트, CI/CD 파이프라인 통합이 필요하면 → [`ue-gauntlet-orchestration`](../ue-gauntlet-orchestration/SKILL.md) 스킬을 참조하세요.

---

## 2. CI/CD Headless Execution

Editor UI 없이 테스트를 실행하는 headless 모드. GitHub Actions, Jenkins 등 CI에 적합.

### Basic Headless Command

```bash
# NullRHI: 렌더링 없음 (GPU 불필요)
UnrealEditor-Cmd.exe MyGame.uproject \
  -ExecCmds="Automation RunTests MyGame;Quit" \
  -NullRHI \
  -Unattended \
  -NoSplash \
  -NoSound \
  -NOSPLASH \
  -NoPause
```

### With JSON Report Export

```bash
UnrealEditor-Cmd.exe MyGame.uproject \
  -ExecCmds="Automation RunTests MyGame;Quit" \
  -ReportExportPath="Saved/TestResults/" \
  -NullRHI -Unattended
```

출력: `Saved/TestResults/index.json` + 개별 테스트 결과 파일

### GitHub Actions Example

```yaml
name: UE Automation Tests
on:
  push:
    branches: [main]
  pull_request:

jobs:
  test:
    runs-on: [self-hosted, UE5]
    timeout-minutes: 30

    steps:
    - uses: actions/checkout@v4

    - name: Build Project
      run: |
        & "$env:UE_ROOT/Build/BatchFiles/RunUAT.bat" BuildCookRun `
          -project="$pwd/MyGame.uproject" `
          -platform=Win64 -clientconfig=Development `
          -build -cook -stage

    - name: Run Automation Tests
      run: |
        & "$env:UE_ROOT/Binaries/Win64/UnrealEditor-Cmd.exe" `
          "$pwd/MyGame.uproject" `
          -ExecCmds="Automation RunTests MyGame;Quit" `
          -ReportExportPath="TestResults/" `
          -NullRHI -Unattended -NoSplash

    - name: Parse Results
      run: |
        $results = Get-Content "TestResults/index.json" | ConvertFrom-Json
        Write-Host "Total: $($results.total), Passed: $($results.passed), Failed: $($results.failed)"
        if ($results.failed -gt 0) { exit 1 }

    - name: Upload Test Report
      if: always()
      uses: actions/upload-artifact@v4
      with:
        name: test-results
        path: TestResults/
```

### Filter-Based Execution (CI 단계별)

```bash
# PR Check (빠른 Smoke Test만)
-ExecCmds="Automation RunFilter Smoke;Quit"

# Nightly (전체 Product Test)
-ExecCmds="Automation RunFilter Product;Quit"

# Weekly (Performance + Stress)
-ExecCmds="Automation RunFilter Perf;Automation RunFilter Stress;Quit"
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All tests passed |
| 1 | Test failures |
| 2 | Crash / unhandled error |
| 3 | Timeout |

---

## 3. Performance Testing (AutomatedPerfTesting)

FPS, 프레임 타임, 메모리 벤치마크를 자동화.

### Material Performance Test

```ini
; Config/DefaultAutomatedPerfTesting.ini
[/Script/AutomatedPerfTesting.AutomatedMaterialPerfTestProjectSettings]
+MaterialsToTest=(FilePath="/Game/Materials/M_Character.M_Character")
+MaterialsToTest=(FilePath="/Game/Materials/M_Environment.M_Environment")
bCaptureScreenshots=True
WarmUpTime=5.0
SoakTime=30.0
CooldownTime=2.0
```

### Sequence Performance Test

```ini
[/Script/AutomatedPerfTesting.AutomatedSequencePerfTestProjectSettings]
+MapsAndSequencesToTest=(
    ComboName="BattleScene",
    Map="/Game/Maps/BattleArena",
    Sequence="/Game/Sequences/SEQ_BattleIntro",
    SequenceStartDelay=2.0)
```

### Custom Performance Benchmark

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FMyGame_AIPerf_Benchmark,
    "MyGame.Performance.AI.PathfindingBenchmark",
    EAutomationTestFlags::EditorContext |
    EAutomationTestFlags::PerfFilter)

bool FMyGame_AIPerf_Benchmark::RunTest(const FString& Parameters)
{
    FPerformanceMetrics Metrics;

    // 100회 반복 측정
    for (int32 i = 0; i < 100; ++i)
    {
        {
            FPerformanceMeasurement Measurement(Metrics);
            // 벤치마크 대상
            UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
            FPathFindingQuery Query(nullptr, *NavSys->GetDefaultNavDataInstance(),
                                    StartLocation, EndLocation);
            NavSys->FindPathSync(Query);
        }
    }

    double AverageMs = Metrics.TotalTimeMs / Metrics.Iterations;

    AddInfo(FString::Printf(TEXT("Pathfinding: Avg=%.2fms, Min=%.2fms, Max=%.2fms"),
                            AverageMs, Metrics.MinTimeMs, Metrics.MaxTimeMs));

    TestTrue(TEXT("Pathfinding should complete in <10ms"),
             AverageMs < 10.0);

    return true;
}
```

### Soak Test Configuration

```ini
[/Script/AutomatedPerfTesting.AutomatedSoakTest]
SoakDuration=14400       ; 4시간 (초)
SnapshotInterval=60      ; 1분마다 스냅샷
MemoryLeakThreshold=10   ; MB/hour 이상이면 경고
FPSDegradationThreshold=5 ; %/hour 이상이면 경고
```

---

## 4. Test Module Separation

테스트 코드를 별도 모듈로 분리하는 패턴.

### Pattern A: 같은 모듈 내 Tests 디렉토리

```text
Source/MyGame/
├── Characters/
│   ├── MyCharacter.h
│   └── MyCharacter.cpp
├── Tests/                    ← 테스트 파일
│   ├── MyCharacter_TakeDamage.spec.cpp
│   └── Inventory_System.spec.cpp
└── MyGame.Build.cs
```

장점: 모듈 내부 접근 가능 (private 멤버). 단점: 배포에 테스트 코드 포함.

### Pattern B: 별도 테스트 모듈

```text
Source/
├── MyGame/
│   ├── MyGame.Build.cs       ← 게임 코드
│   └── ...
└── MyGameTests/
    ├── MyGameTests.Build.cs  ← 테스트 전용 모듈
    ├── MyCharacter_TakeDamage.spec.cpp
    └── Inventory_System.spec.cpp
```

```csharp
// Source/MyGameTests/MyGameTests.Build.cs
public class MyGameTests : ModuleRules
{
    public MyGameTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Core", "CoreUObject", "Engine",
            "MyGame",  // 게임 모듈 참조
        });

        // Editor-only (배포에서 제외)
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.Add("UnrealEd");
            PrivateDependencyModuleNames.Add("FunctionalTesting");
        }
    }
}
```

장점: 배포에서 제외 가능. 단점: public API만 테스트 가능.

### Pattern C: Editor Module with Tests

```csharp
// Target.cs에서 조건부 포함
if (Target.Type == TargetType.Editor)
{
    ExtraModuleNames.Add("MyGameTests");
}
```

---

## 5. PythonAutomationTest Plugin

Python 스크립트로 Automation Test를 작성. 빠른 프로토타이핑에 적합.

### 활성화

```ini
; DefaultEngine.ini
[Plugins]
+EnabledPlugins=PythonScriptPlugin
+EnabledPlugins=PythonAutomationTest
```

### Python Test Example

```python
# Content/Python/Tests/test_damage_system.py
import unreal

@unreal.uclass()
class DamageSystemTest(unreal.AutomationTest):
    """Python-based damage system test"""

    @unreal.ufunction()
    def run_test(self):
        # Arrange
        world = unreal.EditorLevelLibrary.get_editor_world()
        character = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.MyCharacter, unreal.Vector(0, 0, 100))

        initial_hp = character.get_health()

        # Act
        character.take_damage(25.0)

        # Assert
        self.test_equal("HP reduced", character.get_health(), initial_hp - 25.0)
        self.test_true("Still alive", character.is_alive())

        # Cleanup
        character.destroy_actor()
        return True
```

### MCP Integration

```python
# MCP를 통한 Python 테스트 실행
ue_editor_automation(operation="execute_python", code="""
import unreal
# Python 테스트 실행
unreal.AutomationLibrary.run_automation_tests("Python")
""")
```

---

## 6. Agent Teams Parallel Test Generation

test-iterate 패턴을 활용한 병렬 테스트 생성.

### 시나리오

대규모 프로젝트에서 여러 시스템의 테스트를 동시에 생성.

### Workflow

```text
Leader Agent
├── Tester-1: Damage System → BDD Spec 생성
├── Tester-2: Inventory System → BDD Spec 생성
├── Tester-3: GAS Abilities → Complex + Latent 생성
├── Tester-4: Movement → Functional Test 생성
└── Tester-5: UI Widgets → Editor Utility 생성

결과 수집 → 통합 리포트 → 전체 빌드 & 실행
```

### Leader Script

```python
# Leader가 5개 영역을 병렬로 분배
systems = [
    {"name": "Damage", "class": "AMyCharacter", "focus": "TakeDamage"},
    {"name": "Inventory", "class": "UInventoryComponent", "focus": "AddItem,RemoveItem"},
    {"name": "GAS", "class": "UGA_FireBall", "focus": "Activate,Cooldown"},
    {"name": "Movement", "class": "AMyCharacter", "focus": "Jump,Move"},
    {"name": "UI", "class": "UHealthBarWidget", "focus": "SetHealth,Visibility"},
]

# 각 Tester에게 시스템 할당
for i, system in enumerate(systems):
    send_message(f"tester-{i+1}",
                 f"Generate {system['name']} tests for {system['class']}")
```

### Expected Results

| Metric | Sequential | Parallel (5 agents) | Improvement |
|--------|-----------|-------------------|-------------|
| Total Time | ~25min | ~7min | 3.5x faster |
| Tests Generated | 25-30 | 25-30 | Same quality |
| Build Failures | 2-3 | 2-3 | Same rate |

---

## 7. compile_project Operation

C++ 프로젝트 빌드를 MCP에서 트리거하는 operation.

### 3-Step Pipeline

```text
1. LiveCoding Trigger (기존 .cpp 수정만)
   ├─ 성공 → 완료 (10-30초)
   └─ 실패 or 새 .cpp → Step 2

2. Error Detection Polling (3초 간격, 최대 120초)
   ├─ 컴파일 에러 감지 → 에러 리포트
   └─ 타임아웃 → Step 3

3. UBT Fallback (새 .cpp 필수)
   ├─ 에디터 종료 (LiveCoding 잠금 해제)
   ├─ Build.bat 호출 (번들 DotNet 사용)
   └─ 에디터 재시작
```

### ⚠️ Critical: 새 .cpp는 LiveCoding 불가

새 파일 추가 시:
- LiveCoding은 기존 .obj의 증분 컴파일만 지원
- 새 TU(Translation Unit)는 UBT 풀빌드 필요
- **에디터 종료 → UBT → 에디터 재시작** 필수

### Step 1: LiveCoding (기존 파일 수정 시)

```python
ue_editor_assets(operation="compile_project")
# 또는 직접:
ue_editor_automation(operation="execute_python", code="""
import unreal, json
try:
    unreal.LiveCoding.enable_for_all_modules()
    unreal.LiveCoding.compile()
    print(json.dumps({"success": True, "method": "live_coding"}))
except Exception as e:
    print(json.dumps({"success": False, "error": str(e)}))
""")
```

### Step 3: UBT Fallback (Build.bat)

```bash
# Build.bat (번들 DotNet 포함 — 직접 UBT보다 안정)
"{ENGINE}/Build/BatchFiles/Build.bat" \
  {ProjectName}Editor Win64 Development \
  -Project="{PROJECT_DIR}/{ProjectName}.uproject" \
  -WaitMutex -FromMsBuild
```

**Build.bat vs 직접 UBT**: 직접 `UnrealBuildTool.exe` 호출 시 .NET 런타임 미설치 에러 발생 가능.
`Build.bat`은 번들 DotNet SDK를 사용하므로 더 안정적.

**Target 이름 규칙**: `{ProjectName}Editor` (예: `LyraEditor`, NOT `LyraGameEditor`)
→ `Source/{ProjectName}Editor.Target.cs` 파일명에서 확인

**LiveCoding 잠금**: 에디터 실행 중 UBT 빌드 시 "Unable to build while Live Coding is active" 에러.
→ 에디터 프로세스 종료 필수 (`taskkill //F //IM "UnrealEditor.exe"`)

### Timeout

| Method | Time | When |
|--------|------|------|
| LiveCoding | 10-30s | 기존 .cpp 수정 |
| UBT (증분) | 30-120s | 소규모 변경 |
| UBT (풀빌드) | 2-10min | 새 파일, 헤더 변경 |

---

## 8. Screenshot Regression Testing

시각적 변경을 자동 감지하는 스크린샷 비교 테스트.

### Workflow

```text
1. Baseline 생성 (최초 1회)
   ├── CaptureScreenshot("TestName")
   └── CreateBaseline("TestName")

2. 이후 테스트 실행
   ├── CaptureAndCompare("TestName", Config)
   ├── Result.bPassed?
   │   ├── Yes → PASS
   │   └── No → Diff 이미지 확인 → 의도된 변경이면 Baseline 업데이트
   └── 리포트 생성
```

### Configuration Recommendations

| 테스트 종류 | Algorithm | Tolerance | Notes |
|------------|-----------|-----------|-------|
| UI 정확성 | PixelDiff | 0.01 (1%) | 1픽셀도 다르면 실패 |
| VFX 검증 | Perceptual | 0.05 (5%) | 파티클 랜덤성 허용 |
| 레벨 검증 | SSIM | 0.03 (3%) | 구조적 유사도 |
| 색상 확인 | Histogram | 0.10 (10%) | 전체 색감 비교 |

### MCP Integration

```python
# 스크린샷 캡처 + 분석
ue_editor_automation(operation="capture_viewport_analyze",
    description="Compare health bar visual before and after damage")
```

---

## 9. FProperty Reflection Bypass

### 문제

일부 UPROPERTY는 `protected` + `EditDefaultsOnly`로 마킹되어
Python API, Remote Control HTTP, `OBJ SET` 콘솔 커맨드 모두 실패:
- Python `set_editor_property()` → "cannot be edited on instances"
- RC HTTP → "DisableEditOnInstance flag"
- `execute_python` 내에서 HTTP RC 호출 → 셀프 데드락

### 해결: C++ FProperty::ImportText_Direct

```cpp
#include "UObject/UnrealType.h"

// Protected UPROPERTY를 프로그래밍적으로 설정
FProperty* Prop = UMyClass::StaticClass()->FindPropertyByName(TEXT("PropertyName"));
if (Prop)
{
    const FString ValueStr = TEXT("value_or_asset_path");
    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetObject);
    Prop->ImportText_Direct(*ValueStr, ValuePtr, TargetObject, PPF_None);
}
```

### 실전 예: Lyra Experience 설정

```cpp
// WorldSettings의 DefaultGameplayExperience (TSoftClassPtr, protected+EditDefaultsOnly)
#include "GameModes/LyraWorldSettings.h"
#include "UObject/UnrealType.h"

ALyraWorldSettings* WS = Cast<ALyraWorldSettings>(EditorWorld->GetWorldSettings());
FProperty* Prop = ALyraWorldSettings::StaticClass()->FindPropertyByName(
    TEXT("DefaultGameplayExperience"));
if (Prop)
{
    const FString Path = TEXT("/ShooterCore/Experiences/B_ShooterGame_Elimination"
                              ".B_ShooterGame_Elimination_C");
    void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(WS);
    Prop->ImportText_Direct(*Path, ValuePtr, WS, PPF_None);
}
```

### When to Use

- Experience/GameMode 설정 (Lyra, LyraLike 프레임워크)
- Config 속성이 `EditDefaultsOnly` 마킹된 경우
- WorldSettings의 protected 속성 접근 시

---

## 10. Dedicated Test Level Creation

### When to Use

- 기존 맵 의존성이 복잡할 때 (Experience, 서브레벨 등)
- 깨끗한 환경에서 격리 테스트 필요 시
- Functional Test Actor 배치용 레벨

### Pattern: Programmatic Level Creation

MCP `execute_python` 또는 테스트 코드의 Latent Command에서:

1. `EditorLevelLibrary.new_level()` — 빈 레벨 생성
2. 바닥 메시 배치 (캐릭터가 떨어지지 않도록, 300m+ 권장)
3. PlayerStart 배치 (z=200 이상, 바닥 위)
4. 조명 (DirectionalLight + SkyLight + SkyAtmosphere)
5. 저장

```python
ue_editor_automation(operation="execute_python", code="""
import unreal
unreal.EditorLevelLibrary.new_level('/Game/Maps/L_AutomationTest')

# 바닥 300m x 300m
floor = unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.StaticMeshActor, unreal.Vector(0,0,0))
floor.static_mesh_component.set_static_mesh(
    unreal.EditorAssetLibrary.load_asset('/Engine/BasicShapes/Plane'))
floor.set_actor_scale3d(unreal.Vector(50,50,1))

# PlayerStart + 조명
unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.PlayerStart, unreal.Vector(0,0,200))
unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.DirectionalLight, unreal.Vector(0,0,500))
unreal.EditorLevelLibrary.spawn_actor_from_class(
    unreal.SkyLight, unreal.Vector(0,0,500))

unreal.EditorLevelLibrary.save_current_level()
""")
```

### Lyra 프로젝트 추가 단계

6. WorldSettings의 `DefaultGameplayExperience` 설정 (§9 FProperty 사용)
7. 테스트 코드에서 맵 로드 전 Experience 주입:
   ```text
   FEditorLoadMap → FSetWorldExperience → FStartPIECommand
   ```

---

## 11. FPS Throttle & PIE Test Timing

### 문제: FWaitForInteractiveFrameRate

UE Automation Framework는 PIE 시작 후 `FWaitForInteractiveFrameRate`로
프레임레이트가 10fps 이상이 될 때까지 대기함 (기본 600초 타임아웃).

에디터가 백그라운드일 때 CPU 쓰로틀로 3fps까지 떨어지면
테스트가 **600초간 멈춤**.

### 대응

| 방법 | 효과 | 비고 |
|------|------|------|
| 에디터 포그라운드 유지 | ✅ 가장 효과적 | 수동 |
| `bThrottleCPUWhenNotForeground=False` | ✅ 영구 | ini 수정 |
| `t.MaxFPS 0` 콘솔 커맨드 | ⚠️ 제한적 | 쓰로틀 자체는 미해제 |
| 600s 타임아웃 대기 | ⚠️ 마지막 수단 | 작동하지만 느림 |

### Config 설정

```ini
; {PROJECT}/Config/DefaultEditorPerProjectUserSettings.ini
[/Script/UnrealEd.EditorPerProjectUserSettings]
bThrottleCPUWhenNotForeground=False
```

### 영향

- PIE 시작 후 첫 번째 Latent Command까지 도달 시간에 영향
- 총 테스트 시간: 포그라운드 ~30초 vs 백그라운드 ~630초 (600s 대기 포함)
- CI/CD (headless)에서는 해당 없음 (-NullRHI 모드에서 쓰로틀 미발동)

---

## 12. GAS Test World Setup Pattern

> **E2E Lesson**: Lyra TakeDamage 33/33 PASS 과정에서 발견된 GAS 테스트 환경 구축 패턴

### 문제

GAS(Gameplay Ability System) 테스트에서 `CreateNewMap()`으로 만든 World를 사용하면 crash 발생:
- `PostGameplayEffectExecute` → `UGameplayMessageSubsystem::Get(GetWorld())` 호출
- `UGameplayMessageSubsystem`은 **UGameInstanceSubsystem** (NOT UWorldSubsystem)
- `World->GetGameInstance()` → null → `check(Router)` → **fatal crash**

### 해결 패턴

```cpp
// 1. Game-type World 생성 (CreateWorld가 InitWorld 자동 호출 — 중복 호출 금지)
TestWorld = UWorld::CreateWorld(EWorldType::Game, false, TEXT("TestWorld"));

// 2. WorldContext 등록 (GEngine에서 World 검색 가능하게)
FWorldContext& WC = GEngine->CreateNewWorldContext(EWorldType::Game);
WC.SetCurrentWorld(TestWorld);

// 3. UGameInstance 생성+초기화 (Subsystem 컬렉션 생성됨)
TestGameInstance = NewObject<UGameInstance>(GEngine);
WC.OwningGameInstance = TestGameInstance;
TestGameInstance->Init();
TestWorld->SetGameInstance(TestGameInstance);

// 4. ASC에 ULyraAbilitySystemComponent 사용 필수 (GetLyraASC가 Cast+check)
ASC = NewObject<ULyraAbilitySystemComponent>(TestActor);
ASC->RegisterComponent();
ASC->InitAbilityActorInfo(TestActor, TestActor);
```

### 주의사항

| 주의 | 설명 |
|------|------|
| `CreateWorld()` 후 `InitWorld()` 금지 | 내부적으로 이미 호출됨. 중복 시 `ensure(!bIsWorldInitialized)` 실패 |
| `ULyraAbilitySystemComponent` 필수 | 일반 `UAbilitySystemComponent` 사용 시 `GetLyraAbilitySystemComponent()` Cast+check crash |
| SelfDestruct 태그 | PreGE blocking + PostGE MinimumHealth **모두** 우회 — GodMode/UnlimitedHealth 무시 |
| AfterEach 정리 순서 | ASC→Actor→World→GameInstance 순서로 정리. GameInstance는 `Shutdown()` 호출 |

### AfterEach 패턴

```cpp
AfterEach([this]()
{
    HealthSet = nullptr;
    if (ASC) { ASC->DestroyComponent(); ASC = nullptr; }
    if (TestActor) { TestActor->Destroy(); TestActor = nullptr; }
    if (TestWorld) { GEngine->DestroyWorldContext(TestWorld); TestWorld->DestroyWorld(false); TestWorld = nullptr; }
    if (TestGameInstance) { TestGameInstance->Shutdown(); TestGameInstance = nullptr; }
});
```

---

## Related

- [SKILL.md](SKILL.md) — 6-Phase iterative cycle 정의
- [REFERENCE.md](REFERENCE.md) — UE 테스트 API 전체 레퍼런스
- [EXAMPLES.md](EXAMPLES.md) — 8개 완전한 워크스루
