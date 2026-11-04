# Gauntlet Orchestration — Quick Reference

**Version**: 2.3.0

> RunUAT 파라미터, 빌트인 테스트, C++ API, exit codes, PerfReportTool 참조.

---

## 1. RunUAT RunUnreal Parameters

### Core Parameters

| Parameter | Required | Description | Example |
|-----------|----------|-------------|---------|
| `-project=` | ✅ | .uproject 절대 경로 | `-project=D:/MyGame/MyGame.uproject` |
| `-test=` | ✅ | C# TestNode 클래스명 (NOT C++ controller) | `-test=DefaultTest` |
| `-build=` | ❌ | 빌드 소스 (Editor, Local, Staged) | `-build=Local` |
| `-platform=` | ❌ | 타겟 플랫폼 (기본: Win64) | `-platform=PS5` |
| `-configuration=` | ❌ | 빌드 설정 (기본: Development) | `-configuration=Shipping` |

### Execution Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `-timeout=` | 300 | 테스트 타임아웃 (초) |
| `-ResumeOnCriticalFailure` | false | 치명적 오류 시 계속 실행 |
| `-unattended` | false | 무인 실행 모드 |
| `-nullrhi` | false | 렌더링 비활성화 (GPU 불필요) |
| `-nosplash` | false | 스플래시 화면 비활성화 |
| `-nosound` | false | 오디오 비활성화 |

### Report Parameters

| Parameter | Description |
|-----------|-------------|
| `-JUnitReportPath=` | JUnit XML 리포트 출력 경로 |
| `-ReportExportPath=` | HTML 리포트 출력 경로 |
| `-ReportType=` | 리포트 형식 (html, json, xml) |
| `-csvprofiling` | CSV 프로파일링 활성화 |
| `-csvFolder=` | CSV 출력 경로 |

### Multiplayer Parameters

| Parameter | Description |
|-----------|-------------|
| `-server` | 서버 인스턴스 실행 |
| `-numclients=` | 클라이언트 수 |
| `-serverport=` | 서버 포트 (기본: 7777) |
| `-clientport=` | 클라이언트 시작 포트 |
| `-usedsn` | Dedicated Server 모드 |

---

## 2. BuildCookRun Parameters

빌드 → 쿠킹 → 스테이징을 한 번에 수행하는 UAT 명령.

```bash
RunUAT.bat BuildCookRun ^
    -project=D:/MyGame/MyGame.uproject ^
    -platform=Win64 ^
    -clientconfig=Development ^
    -build -cook -stage -pak -archive ^
    -archivedirectory=D:/Builds ^
    -unattended -nullrhi
```

| Parameter | Description |
|-----------|-------------|
| `-build` | 바이너리 빌드 |
| `-cook` | 에셋 쿠킹 |
| `-stage` | 스테이징 디렉토리에 복사 |
| `-pak` | .pak 파일로 패킹 |
| `-archive` | 아카이브 디렉토리에 복사 |
| `-archivedirectory=` | 아카이브 출력 경로 |
| `-clientconfig=` | 클라이언트 설정 (Development/Shipping/Test) |
| `-serverconfig=` | 서버 설정 |
| `-compressed` | 패키지 압축 |
| `-iterativecooking` | 변경된 에셋만 재쿠킹 (개발용) |
| `-NoCompileEditor` | 에디터 바이너리 빌드 스킵 |

---

## 3. Built-in Tests

UE에서 기본 제공하는 Gauntlet 테스트 목록.

| Test Name | Class | Purpose |
|-----------|-------|---------|
| `UE.BootTest` | `UGauntletTestControllerBootTest` | 게임 부팅 검증 (엔진 초기화 확인) |
| `UE.ErrorTest` | `UGauntletTestControllerErrorTest` | 의도적 에러 발생 후 처리 검증 |
| `UE.EditorAutomation` | - | 에디터 자동화 테스트 실행 |
| `UE.TargetAutomation` | - | 타겟 플랫폼 자동화 테스트 실행 |

> **Note**: 대부분의 실제 테스트는 `UGauntletTestController`를 서브클래싱하여 프로젝트별로 작성합니다.

---

## 4. UGauntletTestController API

### Lifecycle Methods (Override)

| Method | When Called | Purpose |
|--------|-----------|---------|
| `OnInit()` | 테스트 시작 시 | 초기 설정 (맵 로드, 변수 초기화) |
| `OnTick(float DeltaTime)` | 매 프레임 | 테스트 로직 실행 (상태 머신) |
| `IsTestRunning(FString& OutReason)` | 매 프레임 | 테스트 진행 중 여부 반환 |

### Control Methods (Call)

| Method | Description |
|--------|-------------|
| `EndTest(int32 ExitCode = 0)` | 테스트 종료 (0=Pass, 1=Fail) |
| `SetMaxDuration(float Seconds)` | 최대 실행 시간 설정 |
| `GetTestDuration()` | 현재까지 경과 시간 |
| `GetTimeoutSeconds()` | 타임아웃 설정값 |
| `BeginTest()` | 테스트 시작 (내부 호출) |

### Exit Codes

```cpp
// EndTest(int32 ExitCode) — NOT an enum, just exit codes:
// 0 = Passed (test succeeded)
// 1 = Failed (test failed)
// Note: EGauntletTestResult does NOT exist in UE 5.7.
// Use UE_LOG before EndTest() to provide descriptive messages.
```

### Test States

| State | Description | Transitions To |
|-------|-------------|----------------|
| NotStarted | 시작 전 | Running |
| Running | 실행 중 | Passed, Failed, TimedOut |
| Passed | 성공 (Terminal) | - |
| Failed | 실패 (Terminal) | - |
| TimedOut | 타임아웃 (Terminal) | - |

---

## 5. Configuration (DefaultGauntlet.ini)

```ini
; Config/DefaultGauntlet.ini

[/Script/Gauntlet.GauntletTestController]
TimeoutSeconds=300.0
RetryCount=0
OutputDirectory=Saved/Gauntlet
bVerboseLogging=false

; Custom test controller config
[/Script/MyGame.MySmokeTestController]
BootTimeout=60.0
MainMenuTimeout=30.0

[/Script/MyGame.MyPerfBenchmarkController]
TestMapName=/Game/Maps/BenchmarkMap
WarmupDuration=10.0
BenchmarkDuration=60.0
MinAcceptableFPS=30.0
MaxAcceptableP99FrameTimeMs=33.3
```

---

## 6. Exit Codes

| Code | Meaning | CI Action |
|------|---------|-----------|
| 0 | 모든 테스트 통과 | ✅ Pipeline 성공 |
| 1 | 1개 이상 테스트 실패 | ❌ 빌드 실패 표시 |
| 2 | 크래시 발생 | 🚨 크래시 덤프 수집 |
| 3 | 타임아웃 | ⚠️ 타임아웃 값 검토 |

> **CI Integration**: Exit code 0만 성공으로 처리. `exit 1-3`은 모두 파이프라인 실패.

---

## 7. PerfReportTool

CSV 프로파일링 데이터를 HTML 리포트로 변환하는 도구.

### CSV Profiling 활성화

```bash
RunUAT.bat RunUnreal ^
    -project=MyGame.uproject ^
    -test=DefaultTest ^
    -gauntlet=PerfBenchmark ^
    -csvprofiling ^
    -csvFolder=D:/PerfData ^
    -csvCaptureFrames=3600
```

### HTML Report 생성

```bash
# Engine 내장 PerfReportTool
Engine/Binaries/DotNET/CsvTools/PerfReportTool.exe ^
    -csvDir=D:/PerfData ^
    -o=D:/Reports/PerfReport.html ^
    -reportType=html ^
    -title="MyGame Performance Report"
```

### CSV 출력 파일

| File | Content |
|------|---------|
| `*.csv` | 프레임별 메트릭 (FPS, GPU, CPU, Memory) |
| `*_Summary.csv` | 요약 통계 (Average, Min, Max, P99) |
| `*_Hitches.csv` | 히치 이벤트 목록 (33ms+ 프레임) |

### 주요 메트릭

| Metric | Column | Unit |
|--------|--------|------|
| Frame Time | `frametime` | ms |
| FPS | `fps` | frames/s |
| GPU Time | `gpuframetime` | ms |
| Game Thread | `gamethread` | ms |
| Render Thread | `renderthread` | ms |
| Used Physical Memory | `usedphysicalmemory` | MB |

---

## 8. Platform Device Interfaces

Gauntlet이 각 플랫폼에서 디바이스를 관리하는 방식.

| Platform | Device Interface | Build Config | Notes |
|----------|-----------------|--------------|-------|
| Win64 | `WindowsTargetDevice` | Development/Shipping | 기본 플랫폼, 설정 불필요 |
| Linux | `LinuxTargetDevice` | Development/Shipping | CI 서버 환경 |
| PS5 | `PS5TargetDevice` | Test/Shipping | DevKit 연결 필요, NDA |
| XSX | `XboxTargetDevice` | Test/Shipping | DevKit 연결 필요, NDA |
| Switch | `SwitchTargetDevice` | Shipping | DevKit 연결 필요, NDA |
| iOS | `IOSTargetDevice` | Shipping | 프로비저닝 프로파일 필요 |
| Android | `AndroidTargetDevice` | Shipping | ADB 연결 필요 |

> **Note**: 콘솔 플랫폼 (PS5, XSX, Switch)은 NDA 하에서만 사용 가능하며 소스 엔진 빌드가 필요합니다.

---

## 9. Command Line Quick Reference

```bash
# === Option A: 직접 실행 (권장 — 소스 빌드 불필요) ===

# Smoke Test (직접 실행)
D:/Builds/Windows/MyGame/Binaries/Win64/MyGame.exe -gauntlet=MySmokeTest -unattended -nosplash -nosound -windowed -ResX=640 -ResY=480 -log

# Soak Test (장시간 안정성)
D:/Builds/Windows/MyGame/Binaries/Win64/MyGame.exe -gauntlet=MySoakTest -unattended -nosplash -nosound -log

# Performance Benchmark (CSV 프로파일링 포함)
D:/Builds/Windows/MyGame/Binaries/Win64/MyGame.exe -gauntlet=MyPerfBenchmark -csvprofiling -csvFolder=D:/PerfData -unattended -nosplash -nosound -log

# CI Headless (GPU 없는 환경)
D:/Builds/Windows/MyGame/Binaries/Win64/MyGame.exe -gauntlet=MySmokeTest -nullrhi -unattended -nosplash -nosound -log

# === Option B: RunUAT RunUnreal (소스 엔진 C# 빌드 필요) ===
# Note: -test= 는 C# TestNode 클래스명 (DefaultTest), -gauntlet= 는 C++ 컨트롤러명

# Smoke Test (RunUAT)
RunUAT.bat RunUnreal -project=MyGame.uproject -test=DefaultTest -gauntlet=MySmokeTest -build=Local -platform=Win64 -timeout=120

# Multiplayer (1 서버 + 4 클라이언트 — RunUAT만 가능)
RunUAT.bat RunUnreal -project=MyGame.uproject -test=DefaultTest -gauntlet=MyMultiplayerTest -server -numclients=4 -serverport=7777 -platform=Win64 -timeout=300

# === Build (both options require this first) ===
RunUAT.bat BuildCookRun -project=MyGame.uproject -platform=Win64 -clientconfig=Development -build -cook -stage -pak -archive -archivedirectory=D:/Builds
```

---

## Related Documents

- [Engine Gauntlet CLASSES.md](../../../docs/engine/ue5_7/modules/Plugins/Gauntlet/CLASSES.md) — 3 classes reference
- [Engine Gauntlet EXAMPLES.md](../../../docs/engine/ue5_7/modules/Plugins/Gauntlet/EXAMPLES.md) — C++ 예제 코드
- [Engine Gauntlet TROUBLESHOOT.md](../../../docs/engine/ue5_7/modules/Plugins/Gauntlet/TROUBLESHOOT.md) — 트러블슈팅
- [ADVANCED.md §1](../ue-automation-test-builder/ADVANCED.md) — Gauntlet 기본 가이드
- [ADVANCED.md §2](../ue-automation-test-builder/ADVANCED.md) — CI/CD headless 실행

---

*Version: 1.0.0 | Issue #4911 | Last Updated: 2026-02-18*
