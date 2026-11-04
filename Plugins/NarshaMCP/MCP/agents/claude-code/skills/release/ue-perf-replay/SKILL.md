---
name: ue-perf-replay
description: "Purpose: .replay-based performance testing using UE DemoRec + AutomatedPerfTesting. Record gameplay via DemoRec, replay with CSV/Insights profiling, analyze for regression detection. Triggers: '리플레이 성능 테스트', '리플레이 녹화 성능', 'DemoRec 성능', '.replay 테스트', 'replay perf test', 'replay performance', 'DemoRec perf', '.replay benchmark'."
compatibility: "Requires NarshaMCP MCP server (v0.9.0+). Editor required for Phase 1 (DemoRec in PIE). Cooked build required for Full mode (Gauntlet)."
argument-hint: "record | stop | analyze [--baseline <path>] | full [--replay <name>] | --quick | --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# /ue-perf-replay

`.replay` 파일 기반 성능 테스트 자동화. DemoRec으로 게임플레이를 녹화하고, 리플레이 재생 중 성능 메트릭을 수집하여 리그레션을 감지합니다.

**Issue**: #6944 (후속: #6799)

## When to Use

- PIE 게임플레이를 녹화하여 성능 기준선(baseline) 확립
- 리플레이 재생으로 반복 가능한 성능 벤치마크 실행
- 빌드 간 FPS/프레임 타임 리그레션 감지
- AutomatedPerfTesting 플러그인 기반 자동화 성능 테스트

## When NOT to Use

- 입력 기반 기능 테스트 생성 → `/ue-test-record`
- .utrace 파일 직접 분석 → `/ue-insights-profiler`
- Gauntlet smoke/soak 테스트 → `/ue-gauntlet-orchestration`
- 일반적인 성능 진단 ("왜 느려?") → `/ue-insights-profiler`

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Parse** `$ARGUMENTS` for command (`record`, `stop`, `analyze`, `full`) and flags (`--quick`, `--baseline`, `--replay`)
2. **Route** to the appropriate phase based on command
3. **Execute** MCP tools in sequence
4. **Generate** structured performance report

---

## Two Operating Modes

### Quick Mode (`--quick`, default)

PIE에서 DemoRec → DemoPlay + CsvProfile → Insights 분석. 쿠킹 빌드 불필요.
빠른 반복 개발에 적합.

```text
[PIE Running] → DemoRec → (User plays) → DemoStop → CsvProfile START → DemoPlay → CsvProfile STOP → Analyze
```

### Full Mode (`full`)

DemoRec → 쿠킹 빌드 → Gauntlet AutomatedReplayPerfTest → 전체 Insights 분석.
프로덕션 수준의 결과.

```text
DemoRec → BuildCookRun → Gauntlet(AutomatedReplayPerfTest) → .utrace + CSV → Full Analysis
```

---

## Execution Protocol

### PHASE 0: PARSE & VALIDATE

1. Parse `$ARGUMENTS`:
   - `record` → Phase 1 (녹화 시작)
   - `stop` → Phase 1 Step 4 (녹화 중지)
   - `analyze` → Phase 3 (기존 결과 분석)
   - `full` → Phase 1 → 2(Full) → 3 → 4 (전체 파이프라인)
   - `--quick` → Quick mode (기본값)
   - `--baseline <path>` → 리그레션 비교용 기준선 .utrace 경로
   - `--replay <name>` → 기존 .replay 파일 사용 (녹화 스킵)
   - `--self-test` → 스킬 구조 검증

2. IF `$ARGUMENTS` is empty: 사용자에게 모드 선택 요청

3. MCP 연결 확인:
   ```python
   ue_check_health()
   ```

4. `.replay` 파일 기존 검색 (--replay 지정 시 또는 analyze 모드):
   ```python
   ue_glob(pattern="Saved/Demos/**/*.replay")
   ```

---

### PHASE 1: RECORD (.replay 캡처)

> Editor PIE 실행 중이어야 합니다.

**Step 1**: `record` 명령 처리

```python
# 타임스탬프 기반 녹화 이름 생성
recording_name = "PerfTest_{YYYYMMDD_HHMMSS}"

# DemoRec 시작
ue_editor_automation(operation="execute_console_command", command=f"DemoRec {recording_name}")
```

사용자에게 안내:
```text
녹화가 시작되었습니다: {recording_name}
PIE에서 테스트 시나리오를 플레이하세요.
완료 후 /ue-perf-replay stop 을 실행하세요.
```

**Step 2**: `stop` 명령 처리

```python
# DemoRec 중지
ue_editor_automation(operation="execute_console_command", command="DemoStop")
```

**Step 3**: .replay 파일 검증

```python
# 생성된 .replay 파일 확인
replays = ue_glob(pattern="Saved/Demos/**/*.replay")
# 가장 최근 파일이 방금 녹화한 것인지 확인
```

출력:
```text
녹화 완료: Saved/Demos/{recording_name}.replay ({size} MB, {duration}s)
```

**GOTO PHASE 2** (자동 진행 또는 사용자 요청 시)

---

### PHASE 2: EXECUTE (성능 프로파일링)

#### Quick Mode (기본값)

PIE에서 리플레이 재생 + CSV 프로파일링:

```python
# Step 1: CSV 프로파일링 시작
ue_editor_automation(operation="execute_console_command", command="CsvProfile START")

# Step 2: Insights trace 시작 (가능한 경우)
ue_editor_automation(operation="execute_console_command", command="Trace.Start cpu,gpu,frame,bookmark")

# Step 3: 리플레이 재생
ue_editor_automation(operation="execute_console_command", command=f"DemoPlay {recording_name}")

# Step 4: 사용자에게 안내
# "리플레이가 재생 중입니다. 완료되면 자동으로 프로파일링이 중지됩니다."
# 또는 수동 중지:
ue_editor_automation(operation="execute_console_command", command="CsvProfile STOP")
ue_editor_automation(operation="execute_console_command", command="Trace.Stop")
```

#### Full Mode

Gauntlet을 통한 AutomatedReplayPerfTest 실행:

```python
# Step 1: 프로젝트 빌드 (쿠킹)
ue_build_pipeline(operation="smart", cook=True, package=True)

# Step 2: Gauntlet 실행
ue_build_pipeline(
    operation="gauntlet_run",
    test_name="DefaultTest",
    extra_args=f"-gauntlet=AutomatedReplayPerfTest -csvprofiling -DemoPlay={recording_name}",
)
```

**GOTO PHASE 3**

---

### PHASE 3: ANALYZE (Insights + CSV 분석)

```python
# Step 1: 생성된 메트릭 파일 검색
utrace_files = ue_glob(pattern="Saved/Profiling/**/*.utrace")
csv_files = ue_glob(pattern="Saved/Profiling/CSV/**/*.csv")

# Step 2: .utrace 존재 시 Insights 분석
if utrace_files:
    insights_result = ue_analyze_insights(operation="smart_analyze", trace_path=utrace_files[-1], top_n=20)

# Step 3: baseline 비교 (--baseline 플래그 지정 시)
if baseline_path:
    regression_result = ue_analyze_insights(
        operation="compare_traces",
        current=utrace_files[-1],
        baseline=baseline_path,
        threshold_ms=0.5,
    )

# Step 4: CSV 메트릭 파싱 (Read 도구로 텍스트 파싱)
if csv_files:
    # CSV에서 핵심 메트릭 추출: FrameTime, FPS, GameThread, RenderThread, GPU
    Read(csv_files[-1])
```

**GOTO PHASE 4**

---

### PHASE 4: REPORT (성능 보고서 생성)

```markdown
=== Replay Performance Report ===

--- Recording Info ---
Replay:   {recording_name}.replay
Duration: {X} seconds
Mode:     Quick (PIE) | Full (Gauntlet)
Date:     {YYYY-MM-DD HH:MM:SS}

--- Frame Time Summary ---
Average Frame Time: X.XX ms (Y FPS)
P95 Frame Time:     X.XX ms
P99 Frame Time:     X.XX ms
Budget:             16.67 ms (60 FPS target)
Status:             WITHIN BUDGET | OVER BUDGET by X.XX ms

--- CSV Metrics (if available) ---
GameThread Avg:   X.XX ms
RenderThread Avg: X.XX ms
GPU Avg:          X.XX ms

--- Regression Detection (if baseline provided) ---
Baseline: {baseline_path}
Regressions Found: N
| Function | Before (ms) | After (ms) | Delta |
|----------|-------------|------------|-------|
| FuncA    | Y.YY        | Z.ZZ       | +X.XX |

Improvements Found: N
| Function | Before (ms) | After (ms) | Delta |
|----------|-------------|------------|-------|
| FuncB    | Y.YY        | Z.ZZ       | -X.XX |

--- Top 10 Hot Functions (if Insights available) ---
| # | Function | Inclusive (ms) | Exclusive (ms) | Calls | Thread |
|---|----------|----------------|----------------|-------|--------|

--- Optimization Suggestions ---
Priority 1: {suggestion}
Priority 2: {suggestion}

--- Next Steps ---
1. Save baseline: copy {current.utrace} → Saved/Profiling/Baselines/
2. Re-run after optimization: /ue-perf-replay analyze --baseline {saved_baseline}
3. For deeper analysis: /ue-insights-profiler {current.utrace}
```

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| "DemoRec command failed" | PIE not running | 사용자에게 PIE 시작 안내 |
| No .replay after DemoStop | 녹화 미시작 또는 경로 문제 | `Saved/Demos/` + `Saved/Replays/` 양쪽 확인 |
| No .utrace after replay | Quick mode에서 trace 미활성화 | CSV-only 분석으로 폴백, trace 캡처 가이드 제공 |
| CSV files empty | CsvProfile 미시작 | Phase 2에서 CsvProfile START 확인 |
| Gauntlet timeout | 리플레이 길이 초과 | `--timeout` 증가 안내 |
| AutomatedPerfTesting not found | 플러그인 미활성화 | .uproject 수정 가이드 제공 |
| Baseline file not found | 경로 오류 | `ue_glob("Saved/Profiling/Baselines/**/*.utrace")` 검색 |

---

## Evaluation Test Cases

| # | Prompt | Expected | Reason |
|---|--------|----------|--------|
| 1 | "리플레이 성능 테스트 해줘" | Activate | 핵심 한국어 트리거 |
| 2 | "DemoRec으로 벤치마크 돌려줘" | Activate | DemoRec + 벤치마크 |
| 3 | "replay perf test on combat level" | Activate | 영어 트리거 |
| 4 | ".replay 파일로 성능 분석" | Activate | .replay + 성능 |
| 5 | "Run replay performance regression test" | Activate | 영어 리그레션 |
| 6 | "입력 녹화해서 테스트 만들어줘" | NOT activate | → ue-test-record |
| 7 | "왜 게임이 느려?" | NOT activate | → ue-insights-profiler |
| 8 | "건틀릿 테스트 설정해줘" | NOT activate | → ue-gauntlet-orchestration |
| 9 | "시네마틱 리플레이" | NOT activate | → ue-sequencer-cinematics |
| 10 | "utrace 분석해줘" | NOT activate | → ue-insights-profiler |

---

## Related Skills

| Skill | Relationship | Boundary |
|-------|-------------|----------|
| `/ue-test-record` | 상호보완 | 기능 테스트 (입력 재현 + assertion) vs 성능 테스트 (FPS + 리그레션) |
| `/ue-insights-profiler` | 후속 분석 | 이 스킬이 생성한 .utrace의 심층 분석 |
| `/ue-gauntlet-orchestration` | Full mode 기반 | Gauntlet 파이프라인 활용 (Full mode) |
| `/ue-build-automate` | 전제 조건 | Full mode 쿠킹 빌드 필요 시 |

---

## Technical Reference

### DemoRec Console Commands

| Command | Description |
|---------|-------------|
| `DemoRec <Name>` | 리플레이 녹화 시작. 파일: `Saved/Demos/<Name>.replay` |
| `DemoStop` | 현재 녹화 중지 |
| `DemoPlay <Name>` | 리플레이 재생 |

### CSV Profiling Commands

| Command | Description |
|---------|-------------|
| `CsvProfile START` | CSV 성능 프로파일링 시작 |
| `CsvProfile STOP` | 프로파일링 중지. 파일: `Saved/Profiling/CSV/` |

### Insights Trace Commands

| Command | Description |
|---------|-------------|
| `Trace.Start cpu,gpu,frame,bookmark` | Insights trace 시작 |
| `Trace.Stop` | trace 중지. 파일: `Saved/Profiling/*.utrace` |

### AutomatedPerfTesting Plugin

- **Plugin**: `Engine/Plugins/Performance/AutomatedPerfTesting`
- **Controller**: `UAutomatedReplayPerfTest` (inherits `UAutomatedPerfTestControllerBase`)
- **Settings**: `UAutomatedReplayPerfTestProjectSettings`
  - `ReplaysToTest`: array of `FFilePath` — 테스트할 .replay 파일 목록
  - `CSVOutputMode`: `EAutomatedPerfTestCSVOutputMode` (Separate / Granular)
- **Profiling**: `SetupProfiling()` → Insights trace + CSV, `MarkProfilingStart/End()` → 구간 측정

### ReplaySubsystem API (프로그래밍 방식)

```cpp
UReplaySubsystem* RS = GameInstance->GetSubsystem<UReplaySubsystem>();
RS->RecordReplay("MyRecording", "Friendly Name", Options, nullptr);
// ... 플레이 ...
RS->StopReplay();
```
