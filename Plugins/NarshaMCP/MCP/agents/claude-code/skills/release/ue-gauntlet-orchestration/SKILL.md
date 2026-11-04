---
name: ue-gauntlet-orchestration
model: opus
description: "Purpose: Orchestrate multi-platform smoke/soak/performance tests via UE Gauntlet RunUAT. Triggers: 'gauntlet', 'RunUAT test', 'soak test', 'smoke test 멀티플랫폼', '쿠킹 빌드 테스트', 'performance benchmark CI', '패키지 빌드 검증', 'gauntlet controller', '건틀릿'"
allowed-tools: [Bash, Read, Glob, Grep, Edit, Write, Task]
version: 2.3.0
argument-hint: ["smoke", "soak --duration 14400", "perf --platform Win64", "--ci-only", "--report path/to/junit.xml"]
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Gauntlet Orchestration

**Priority**: High
**Goal**: 쿠킹된 빌드에서 Smoke/Soak/Performance 테스트를 자동 설정 + 가이드 실행

---

## Architecture

Gauntlet은 **테스트 오케스트레이션 레이어**:

| Layer | Role | Automation Level |
|-------|------|-----------------|
| **Test Authoring** | C++ controller code | **STEP 2: Claude Write 자동** |
| **Build & Execute** | Cook + RunUAT | **STEP 3-4: 사용자 가이드** (30분+ 소요) |
| **Report** | JUnit XML 파싱 | **STEP 5: Claude Read 반자동** |
| **CI/CD** | GitHub Actions / Jenkins | **STEP 6: Claude Write 자동** |

```text
Gauntlet 2-Layer Architecture:

Layer 1 — C# TestNode (RunUAT, outside game process):
  RunUnreal.cs → -test=DefaultTest → UnrealTestNode.cs
    → Discovers build, launches game exe, monitors heartbeats
    → Injects -gauntlet=ControllerName into game command line (line 669)

Layer 2 — C++ UGauntletTestController (inside game process):
  GauntletModule.cpp:188 → FParse::Value("gauntlet=", ...)
    → LoadControllers() → NewObject<UGauntletTestController>(TestClass)
    → OnInit() → OnTick() → EndTest(0 or 1)
```

> **IMPORTANT**: `-test=` 파라미터는 C# TestNode 클래스명 (예: `DefaultTest`)이고,
> `-gauntlet=` 파라미터가 C++ UGauntletTestController 클래스명 (예: `MyGameSmokeTest`)입니다.
> 이 둘을 혼동하면 "Unable to find type" 에러가 발생합니다.

### MCP Tools Used

| Tool | Purpose |
|------|---------|
| `ue_build_pipeline` | Cook, package, and run commandlets (STEP 3 automation) |
| `ue_editor_automation` | Editor health check before test execution |

**Example MCP calls**:

```python
# Check Editor status before gauntlet run
ue_editor_automation(operation="get_editor_status")

# Cook and package for gauntlet (STEP 3)
ue_build_pipeline(operation="smart", cook=True, package=True, platform="Win64")

# Run commandlet for pre-cook validation
ue_build_pipeline(operation="smart", commandlet_name="CompileAllBlueprints")
```

### Prerequisites

| Requirement | Detail | Difficulty |
|-------------|--------|------------|
| **Cooked Build** | `BuildCookRun` packaging required | High (time) |
| **Gauntlet Plugin** | `.uproject` + `Build.cs` modification | Low (**auto**) |
| **UGauntletTestController** | C++ game-side controller class | Medium (**auto**) |

> **Note**: 직접 실행 방식(Option A)은 소스 빌드/Visual Studio 불필요.
> RunUAT 방식(Option B)은 소스 엔진의 C# AutomationTool이 필요.
> Editor 내에서만 테스트한다면 `ue-automation-test-builder` 스킬을 사용하세요.

---

## Activation

### Trigger Phrases

**Korean**: "건틀릿 테스트 설정해줘", "패키지 빌드에서 스모크 테스트", "멀티플랫폼 테스트 파이프라인", "쿠킹된 빌드 검증", "24시간 소크 테스트 설정", "성능 벤치마크 CI 워크플로우"

**English**: "Set up Gauntlet smoke test", "Configure soak test for packaged build", "Multi-platform performance benchmark pipeline", "Gauntlet test controller for multiplayer", "RunUAT test automation"

### Should NOT Activate (use other skills)

| Request | Use Instead | Why |
|---------|-------------|-----|
| "유닛 테스트 만들어줘" | `ue-automation-test-builder` | Gauntlet은 테스트 오케스트레이션, 작성 아님 |
| "에디터에서 테스트 실행" | `ue-automation-test-builder` Phase 3 | Gauntlet = 쿠킹 빌드 전용 |
| "CI 상태 확인" | `ci-monitor` | CI 모니터링은 별도 스킬 |

---

## EXECUTION PROTOCOL

**YOU MUST FOLLOW THESE STEPS. Do not skip steps. Do not just display documentation.**
**각 STEP의 도구 호출을 실제로 실행하시오. 마크다운 표시만 하지 마시오.**

### Automation Summary

| STEP | Phase | Automation | What Claude Does |
|------|-------|------------|-----------------|
| 0 | Parse | **Auto** | 인수 파싱, 모드 결정 |
| 1 | Gather | **Interactive** | AskUserQuestion으로 프로젝트 정보 수집 |
| 2 | Setup | **Auto (Edit/Write)** | .uproject Edit, Build.cs Edit, C++ Write, ini Write |
| 3 | Build | **Guide** | 사용자에게 BuildCookRun 명령 제시 |
| 4 | Execute | **Guide** | 사용자에게 RunUAT RunUnreal 명령 제시 |
| 5 | Report | **Semi-Auto (Read)** | JUnit XML Read + 파싱 + 결과 요약 |
| 6 | CI/CD | **Auto (Write)** | GitHub Actions YAML 또는 Jenkinsfile Write |

---

### STEP 0: PARSE ARGUMENTS

1. 인수에서 테스트 모드 결정:
   - `smoke` → `TEST_MODE = smoke`
   - `soak` → `TEST_MODE = soak`
   - `perf` → `TEST_MODE = perf`
   - `--ci-only` → `CI_ONLY = true` (STEP 1 후 STEP 6만 실행)
   - `--report <path>` → `REPORT_ONLY = true`, `REPORT_PATH = <path>` (STEP 5만 실행)
   - 인수 없음 → STEP 1에서 AskUserQuestion으로 결정

2. 추가 옵션 파싱:
   - `--duration <seconds>` → `DURATION = <seconds>` (기본: 14400)
   - `--platform <name>` → `PLATFORM = <name>` (기본: Win64)
   - `--multiplayer <N>` → `MULTIPLAYER = <N>` (기본: 0)
   - `--timeout <seconds>` → `TIMEOUT = <seconds>` (기본: 300, soak이면 14400)

3. IF `REPORT_ONLY == true` → **GOTO STEP 5**
4. OTHERWISE → **GOTO STEP 1**

---

### STEP 1: GATHER PROJECT INFO

AskUserQuestion으로 다음 정보를 수집합니다:

**필수 질문**:
1. `.uproject` 파일의 절대 경로는? (예: `D:/Projects/MyGame/MyGame.uproject`)
2. 게임 모듈명은? (예: `MyGame` — Build.cs의 모듈명)

**TEST_MODE 미정이면 추가 질문**:
3. 테스트 유형은? (Smoke / Soak / Performance)

**선택 질문** (기본값 있음):
4. 이미 작성된 `UGauntletTestController` 서브클래스가 있나요? (있으면 클래스명 입력, 없으면 자동 생성)
5. CI/CD 파이프라인도 생성할까요? (GitHub Actions / Jenkins / 불필요)
6. 멀티플레이어 테스트인가요? (서버+클라이언트 수)

**수집 후 변수 저장** — 아래 변수를 모두 결정하시오:

```text
# Core
PROJECT_PATH = .uproject 절대 경로
MODULE_NAME  = 게임 모듈명
TEST_MODE    = smoke | soak | perf

# Derived
PROJECT_DIR       = dirname(PROJECT_PATH)
UPROJECT_FILENAME = basename(PROJECT_PATH)
HAS_EXISTING_CONTROLLER = true | false
CONTROLLER_CLASS  = smoke→U{MODULE_NAME}SmokeTest, soak→...SoakTest, perf→...PerfBenchmark
CONTROLLER_CLASS_NAME = CONTROLLER_CLASS에서 U prefix 제거

# Platform & CI
CI_TARGET = github | jenkins | none
MULTIPLAYER = 0 | N
PLATFORM = Win64 | PS5 | XSX | Linux
PLATFORM_DIR = Win64→"Windows", PS5→"PS5", XSX→"XboxSeriesX", Linux→"Linux"
TIMEOUT = 300 (soak: 14400)
TIMEOUT_MINUTES = ceil(TIMEOUT/60)+30
DURATION = soak전용 (14400)
REPORT_PATH = {PROJECT_DIR}/TestResults/
EXTRA_PARAMS = perf→"-csvprofiling", soak→"-ResumeOnCriticalFailure"
```

**조건 분기**:
- IF `CI_ONLY == true` → **GOTO STEP 6**
- OTHERWISE → **GOTO STEP 2**

---

### STEP 2: SETUP (Automated — Edit/Write)

**이 스텝은 실제 파일을 생성/수정합니다.**

#### STEP 2.1: Enable Gauntlet Plugin

1. Read 도구로 `{PROJECT_PATH}` 파일 읽기
2. `"Gauntlet"` 문자열이 이미 있는지 확인
3. IF 이미 있으면 — 스킵
4. IF 없으면:
   - **Case A**: `"Plugins"` 배열이 이미 존재하면 — 마지막 항목 뒤에 `{"Name": "Gauntlet", "Enabled": true}` 추가
   - **Case B**: `"Plugins"` 배열이 없으면 — 마지막 `}` 앞에 Plugins 블록 추가

#### STEP 2.2: Add Module Dependency

1. Glob 도구로 `{PROJECT_DIR}/Source/{MODULE_NAME}/{MODULE_NAME}.Build.cs` 찾기
   - 없으면 `{PROJECT_DIR}/Source/{MODULE_NAME}/*.Build.cs` 시도
   - 여전히 없으면 `{PROJECT_DIR}/Source/**/*.Build.cs` 후 `{MODULE_NAME}` 포함된 파일 선택
2. Read 도구로 해당 Build.cs 읽기
3. `"Gauntlet"` 의존성이 이미 있으면 스킵
4. 없으면 `PublicDependencyModuleNames` 또는 `PrivateDependencyModuleNames`에 `"Gauntlet"` 추가

#### STEP 2.3: Create Test Controller (Skip if HAS_EXISTING_CONTROLLER)

IF `HAS_EXISTING_CONTROLLER == true` → GOTO STEP 2.4

`CONTROLLER_CLASS` 결정:
- smoke → `U{MODULE_NAME}SmokeTest`
- soak → `U{MODULE_NAME}SoakTest`
- perf → `U{MODULE_NAME}PerfBenchmark`

`CONTROLLER_CLASS_NAME` = `CONTROLLER_CLASS`에서 앞의 `U` 제거

**파일 위치**: `{PROJECT_DIR}/Source/{MODULE_NAME}/Tests/`

##### smoke 모드 (primary template):

**.h 파일** — Write 도구로 `{CONTROLLER_CLASS_NAME}.h` 생성:
```cpp
#pragma once
#include "CoreMinimal.h"
#include "GauntletTestController.h"
#include "{CONTROLLER_CLASS_NAME}.generated.h"

UCLASS()
class {MODULE_NAME}_API {CONTROLLER_CLASS} : public UGauntletTestController
{
    GENERATED_BODY()

public:
    virtual void OnInit() override;
    virtual void OnTick(float TimeDelta) override;

private:
    bool IsMainMenuReady() const;
    double TestStartTime = 0.0;
    int32 CurrentPhase = 0;
};
```

**.cpp 파일** — Write 도구로 `{CONTROLLER_CLASS_NAME}.cpp` 생성:
```cpp
#include "{CONTROLLER_CLASS_NAME}.h"
#include "Engine/World.h"

void {CONTROLLER_CLASS}::OnInit()
{
    TestStartTime = FPlatformTime::Seconds();
    CurrentPhase = 1;
}

void {CONTROLLER_CLASS}::OnTick(float TimeDelta)
{
    const double Elapsed = FPlatformTime::Seconds() - TestStartTime;

    switch (CurrentPhase)
    {
    case 1: // Wait for game world
        if (GetWorld() && GetWorld()->HasBegunPlay())
        {
            UE_LOG(LogTemp, Log, TEXT("Boot complete (%.1fs)"), Elapsed);
            CurrentPhase = 2;
        }
        else if (Elapsed > 60.0)
        {
            UE_LOG(LogTemp, Error, TEXT("Boot timeout: world not ready after 60s"));
            EndTest(1);
        }
        break;

    case 2: // Wait for main menu
        if (IsMainMenuReady())
        {
            UE_LOG(LogTemp, Log, TEXT("Main menu ready (%.1fs)"), Elapsed);
            UE_LOG(LogTemp, Log, TEXT("Smoke test passed"));
            EndTest(0);
        }
        else if (Elapsed > 90.0)
        {
            UE_LOG(LogTemp, Error, TEXT("Main menu timeout: not ready after 90s"));
            EndTest(1);
        }
        break;
    }
}

bool {CONTROLLER_CLASS}::IsMainMenuReady() const
{
    // TODO: Replace with your project's main menu detection logic
    return GetWorld() && GetWorld()->HasBegunPlay();
}
```

##### soak / perf 모드:

See [EXAMPLES.md](EXAMPLES.md) for soak and perf C++ templates, and multiplayer additions.

표시: "C++ 컨트롤러 파일 생성 완료: `{PROJECT_DIR}/Source/{MODULE_NAME}/Tests/{CONTROLLER_CLASS_NAME}.h`, `.cpp`"

#### STEP 2.4: Create Config File (Optional)

Write 도구로 `{PROJECT_DIR}/Config/DefaultGauntlet.ini` 생성:

```ini
; Generated by Gauntlet Orchestration Skill v2.1
[/Script/{MODULE_NAME}.{CONTROLLER_CLASS_NAME}]
```

설정 내용은 TEST_MODE에 따라:
- smoke: `BootTimeout=60.0` + `MainMenuTimeout=30.0`
- soak: `MaxAllowedMemory=4294967296` + `MinAcceptableFPS=20.0`
- perf: `WarmupDuration=3.0` + `ProfilingDuration=30.0`

표시: "Setup 완료. 생성/수정된 파일 목록:"
- `.uproject` (수정 또는 스킵)
- `Build.cs` (수정 또는 스킵)
- `Tests/{CONTROLLER_CLASS_NAME}.h` (생성 또는 스킵)
- `Tests/{CONTROLLER_CLASS_NAME}.cpp` (생성 또는 스킵)
- `Config/DefaultGauntlet.ini` (생성)

**GOTO STEP 3**

---

### STEP 3: BUILD (Guide — 사용자 실행)

**이 스텝은 자동화할 수 없습니다** — 쿠킹은 30분+ 소요되며 UE Source Build가 필요합니다.

사용자에게 다음 명령을 제시합니다:

```text
다음 명령으로 프로젝트를 쿠킹하세요:

RunUAT.bat BuildCookRun ^
    -project="{PROJECT_PATH}" ^
    -platform={PLATFORM} ^
    -clientconfig=Development ^
    -build -cook -stage -pak -archive ^
    -archivedirectory="{PROJECT_DIR}/Builds"

예상 소요 시간: 30분-2시간 (프로젝트 크기에 따라)
빌드 완료 후 알려주시면 STEP 4로 진행합니다.
```

IF `MULTIPLAYER > 0`:
```text
멀티플레이어용으로 서버 빌드도 필요합니다:

RunUAT.bat BuildCookRun ^
    -project="{PROJECT_PATH}" ^
    -platform={PLATFORM} ^
    -serverconfig=Development ^
    -build -cook -stage -pak -server
```

**사용자가 빌드 완료를 알리면 → GOTO STEP 4**

---

### STEP 4: EXECUTE (Guide — 사용자 실행)

**이 스텝은 사용자 환경에서 직접 실행해야 합니다.**

두 가지 실행 방법을 제시합니다. **Option A (직접 실행)을 우선 권장**합니다.

#### Option A: 직접 실행 (권장 — 소스 빌드 불필요)

쿠킹된 게임 실행 파일을 `-gauntlet=` 파라미터로 직접 실행합니다.

```text
"{PROJECT_DIR}/Builds/{PLATFORM_DIR}/{UPROJECT_NAME}/Binaries/{PLATFORM}/
{GAME_TARGET}.exe" ^
    -gauntlet={CONTROLLER_CLASS_NAME} ^
    -unattended -nosplash -nosound ^
    -windowed -ResX=640 -ResY=480 ^
    -log
```

> **{GAME_TARGET} 찾기**: `Builds/{PLATFORM_DIR}/{UPROJECT_NAME}/Binaries/{PLATFORM}/` 폴더에서
> `*Game.exe` 또는 `*Client.exe` 파일을 찾으세요.

##### 모드별 추가 옵션:
- **perf**: `-csvprofiling -csvFolder="{PROJECT_DIR}/PerfData"`
- **soak**: 추가 옵션 없음 (C++ SetMaxDuration이 종료 제어)
- **CI headless**: `-nullrhi`

#### Option B: RunUAT RunUnreal (소스 엔진 C# 빌드 필요)

RunUAT의 C# TestNode를 통해 실행합니다. 상세 명령과 주의사항은 [REFERENCE.md](REFERENCE.md) §1, §9를 참조하세요.

핵심: `-test=DefaultTest` (C# TestNode) + `-gauntlet={CONTROLLER_CLASS_NAME}` (C++ 컨트롤러)

**Exit Codes** (두 Option 모두 동일):

| Code | Meaning | Action |
|------|---------|--------|
| 0 | 모든 테스트 통과 | STEP 5에서 리포트 확인 |
| 1 | 테스트 실패 | STEP 5에서 실패 분석 |
| 2 | 크래시 | 로그 확인 필요 |
| 3 | 타임아웃 | SetMaxDuration 또는 -timeout 값 증가 |

**참고**: `-gauntlet=` 파라미터에는 `U` prefix 없이 클래스명을 사용합니다.

**사용자가 실행 완료를 알리면 → GOTO STEP 5**

---

### STEP 5: REPORT ANALYSIS (Semi-Automated — Read/Parse)

> **Option A vs B 차이**: Option A는 JUnit XML을 생성하지 않습니다.
> 결과는 게임 로그 + exit code로 확인. Option B만 JUnit XML 생성.

**5-A: Option A** — Glob으로 `{PROJECT_DIR}/Saved/Logs/*.log` 검색 후 Grep으로 `LogGauntlet|TEST COMPLETE|EXIT CODE` 파싱. 실패 시 `Error|Fatal|Ensure` 검색.

**5-B: Option B** — Glob으로 `{REPORT_PATH}/**/*.xml` 검색, Read로 JUnit XML 파싱. tests/failures/errors/time 추출.

**5-C: CSV 프로파일링** (perf 모드) — Glob으로 `{PROJECT_DIR}/PerfData/*Summary*.csv` 검색, Read로 Average FPS, P99 Frame Time, Memory Usage 추출.

**조건 분기**:
- IF `REPORT_ONLY == true` → **DONE**
- IF `CI_TARGET != none` → **GOTO STEP 6**
- OTHERWISE → **DONE**

---

### STEP 6: CI/CD SETUP (Automated — Write)

CI/CD 파이프라인 파일을 생성합니다. 전체 템플릿은 [EXAMPLES.md](EXAMPLES.md) §4-5를 참조하세요.

- **GitHub Actions** (`CI_TARGET == github`): Write 도구로 `{PROJECT_DIR}/.github/workflows/gauntlet-tests.yml` 생성. self-hosted UE5 Windows 러너, Build&Cook → Test → Collect Logs → Upload Artifacts.
- **Jenkins** (`CI_TARGET == jenkins`): Write 도구로 `{PROJECT_DIR}/Jenkinsfile.gauntlet` 생성. Cook → Test 스테이지, 로그 아카이빙.

표시: "CI/CD 파일 생성 완료: {파일 경로}"

**DONE**

---

## Anti-Patterns

| Do NOT | Do Instead | Why |
|--------|------------|-----|
| Use `-test=MyControllerName` | Use `-gauntlet=MyControllerName` | `-test=` is for C# TestNode classes, `-gauntlet=` is for C++ controllers |
| Run Gauntlet from Editor UI | Use game exe with `-gauntlet=` | Gauntlet runs in cooked builds, not Editor |
| Skip cooked build | Run `BuildCookRun` first | Gauntlet requires packaged builds |
| Set infinite timeout | Use `SetMaxDuration()` + `-timeout=` | Prevents hung tests in CI |
| Ignore exit codes | Check return code (0/1/2/3) | CI needs proper pass/fail signal |
| Hardcode paths | Use `-project=` and `-build=` params | Portability across machines |
| Test without `-NullRHI` in CI | Add `-NullRHI -Unattended` | CI runners may not have GPU |

---

## Related Skills

| Skill | Relation | When to Use |
|-------|----------|-------------|
| `ue-automation-test-builder` | **Prerequisite** | Write C++ tests first, then orchestrate with Gauntlet |
| `ci-monitor` | Complementary | Monitor CI pipeline after Gauntlet tests run |
| `code-generation` | Helper | Generate additional C++ code |
| `insights-profiler` | Follow-up | Analyze PerfReportTool output in detail |
| `unreal-error-doctor` | Follow-up | Auto-fix build/test errors |

---

## Engine Documentation Reference

- **Classes**: `docs/engine/ue5_7/modules/Plugins/Gauntlet/CLASSES.md`
- **Examples**: `docs/engine/ue5_7/modules/Plugins/Gauntlet/EXAMPLES.md`
- **Troubleshoot**: `docs/engine/ue5_7/modules/Plugins/Gauntlet/TROUBLESHOOT.md`
- **Quick Reference**: [REFERENCE.md](REFERENCE.md) — RunUAT params, API, exit codes

**Detailed reference**: [REFERENCE.md](REFERENCE.md) (RunUAT parameters, API reference, exit codes, Option B execution details)

**Code templates & CI/CD examples**: [EXAMPLES.md](EXAMPLES.md) (soak/perf C++ templates, multiplayer, GitHub Actions, Jenkinsfile)

## Output Format

```text
=== Gauntlet Orchestration Report ===

Mode: smoke
Controller: UMyGameSmokeTest
Platform: Win64

STEP 0: Arguments parsed (smoke, Win64)
STEP 1: Project info gathered (MyGame.uproject)
STEP 2: Setup complete
  - .uproject: Gauntlet plugin added
  - Build.cs: Gauntlet dependency added
  - Tests/MyGameSmokeTest.h/.cpp: created
  - Config/DefaultGauntlet.ini: created
STEP 3: Build command provided to user
STEP 4: Execution command provided to user
STEP 5: Test Results
  Exit Code: 0 (PASS)
  Duration: 45s
  Controllers: MyGameSmokeTest
STEP 6: CI/CD skipped (CI_TARGET=none)
```

## Error Recovery

See [EXAMPLES.md](EXAMPLES.md#error-recovery) for the full error recovery table.
