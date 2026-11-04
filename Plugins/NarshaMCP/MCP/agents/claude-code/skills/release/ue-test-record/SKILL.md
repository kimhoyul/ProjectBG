---
name: ue-test-record
description: "Purpose: Record PIE input and convert to Complex+Latent C++ automation test. 3-command workflow: start → stop → export. Triggers: '입력 녹화', 'record input', 'PIE 녹화', 'record test'."
compatibility: "Requires NarshaMCP MCP server (v0.9.0+). Editor required (PIE must be running for start/stop). Enhanced Input 프로젝트(Lyra 등)에서는 --semantic 모드 권장."
allowed-tools: [Bash, Read, Glob, Grep, Edit, Write]
argument-hint: "start | stop | export [--semantic] [--test-name=<Name>] [--module=<Module>] | --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# /ue-test-record

PIE에서 플레이한 입력을 녹화하여 재현 가능한 Complex+Latent C++ 자동화 테스트로 변환합니다.

## When to Use

- PIE에서 직접 플레이한 입력을 자동화 테스트로 기록
- 수동 QA 세션을 재현 가능한 자동 테스트로 전환
- 녹화 기반의 회귀 테스트 생성

## When NOT to Use

- 자연어로 시나리오 기술 → `/ue-test-scenario`
- 단일 함수 유닛 테스트 → `/ue-test-gen`
- 반복적 테스트 개선 → `/ue-automation-test-builder`
- 테스트가 아닌 기능 구현 → `/ue-scaffold`

---

## 3-Command Workflow

### Command: `start` (녹화 시작)

PIE에서 입력 녹화를 시작합니다.

```python
# Step 1: 녹화 시작
ue_editor_automation(
    operation="start_input_recording",
    max_duration_seconds=60
)
```

**출력**:
```text
녹화 시작됨 (최대 60초).
에디터에서 플레이한 후 `/ue-test-record stop`을 실행하세요.
```

**에러 처리**:
- PIE 미실행 → "PIE를 먼저 시작하세요 (Editor > Play 또는 Alt+P)"
- 이미 녹화 중 → "이미 녹화가 진행 중입니다. `stop` 후 다시 시작하세요."

### Command: `stop` (녹화 종료)

녹화를 종료하고 캡처된 입력 이벤트를 반환합니다.

```python
# Step 1: 녹화 종료
result = ue_editor_automation(operation="stop_input_recording")

# Step 2: 결과 파싱
# result.events = [{input_type, key, x, y, delay_ms, ...}, ...]
# result.duration_ms = 총 녹화 시간
# result.event_count = 이벤트 수
# result.pawn_start = {x, y, z, pitch, yaw, roll}  (Issue #7021)
# result.metadata = {avg_fps, resolution, player_class, engine_version}  (Issue #7022)
```

**출력 (정상)**:
```text
녹화 완료:
- 이벤트 수: 24
- 녹화 시간: 5,230ms
- 내역: key_down 8개, key_up 8개, mouse_move 6개, mouse_down 1개, mouse_up 1개

이벤트 데이터가 저장되었습니다.
`/ue-test-record export`로 테스트 파일을 생성하세요.
```

**에러 처리**:
- 녹화 안 됨 → "활성 녹화가 없습니다. `start`를 먼저 실행하세요."
- 0 이벤트 → "녹화된 입력 이벤트가 없습니다. 더 긴 시간 플레이 후 다시 시도하세요."
- 60초 초과 → "녹화가 60초 제한에 도달하여 자동 종료되었습니다."

**상태 보존**: `stop`의 전체 이벤트 JSON을 대화 컨텍스트에 보관합니다. `export`에서 참조합니다.

### Command: `export` (테스트 변환)

녹화된 이벤트를 Complex+Latent C++ 자동화 테스트로 변환합니다.

**파라미터**:
- `--test-name=<Name>`: 테스트 클래스명 (기본: `FRecordedInput_<timestamp>`)
- `--module=<Module>`: 모듈명 (기본: 프로젝트에서 자동 감지)
- `--semantic`: 의미 기반 변환 모드 (기본: 충실한 재현 모드)

#### 변환 모드

**Default — 충실한 재현 (Faithful Replay)**

각 입력 이벤트를 1:1로 `FInjectInputEvent` latent command + `FWaitMs(delay)` 명령으로 변환합니다.
타이밍과 입력 순서를 정확히 재현합니다.

```python
# Step 1: 이벤트 분석
# 이벤트 타입별 카운트, 총 시간, 키 시퀀스 분석

# Step 2: 테스트 골격 생성
ue_generate_code(
    operation="generate_test",
    test_target="RecordedInput",
    test_type="complex_latent",
    test_name="FRecordedInput_20260317",
    module_name="MyGame",
)

# Step 3: 골격에 녹화된 이벤트 기반 latent command 삽입
# 각 이벤트 → FInjectInputEvent(type, key) + FWaitMs(delay)
# 5초 간격으로 FCheckpointPosition assertion 삽입

# Step 4: 최종 파일 Write
Write("Source/MyGame/Tests/FRecordedInput_20260317.cpp", final_code)
```

**`--semantic` — 의미 기반 변환**

입력 패턴을 감지하여 의미 있는 latent command로 변환합니다.
`/ue-test-scenario`의 매핑 테이블을 재사용합니다.

| 입력 패턴 | 감지 조건 | Semantic Command |
|-----------|----------|------------------|
| W키 500ms 이상 유지 | key_down W ... key_up W, delta >500ms | `FMoveForward(duration)` |
| S키 500ms 이상 유지 | key_down S ... key_up S, delta >500ms | `FMoveBackward(duration)` |
| SpaceBar 짧은 탭 | key_down + key_up, delta <200ms | `FPerformJump` + `FWaitForLanded` |
| 좌클릭 짧은 탭 | mouse_down + mouse_up, delta <300ms | `FFireWeapon` (GAS 감지 시) |
| 마우스 연속 이동 | consecutive mouse_move | `FMouseLook(dx, dy)` |
| Shift 유지 | key_down LeftShift sustained | `FStartSprint` / `FStopSprint` |

인식 불가한 패턴은 faithful 모드로 fallback 합니다.

**출력 (정상)**:
```text
테스트 파일 생성 완료:
- 파일: Source/MyGame/Tests/FRecordedInput_20260317.cpp
- 모드: faithful (충실한 재현)
- Latent Commands: 48개
- Checkpoint Assertions: 3개 (5초 간격)
- 테스트 이름: MyGame.RecordedInput.FRecordedInput_20260317

빌드: ue_fix_errors(operation="build_and_fix") 로 컴파일 확인
실행: ue_editor_automation(operation="run_automation_tests", test_name="MyGame.RecordedInput.FRecordedInput_20260317")
```

**에러 처리**:
- 이벤트 없음 → "녹화된 이벤트가 없습니다. `/ue-test-record stop`을 먼저 실행하세요."
- 모듈 감지 실패 → `--module` 파라미터 요청

---

## Checkpoint Assertions

테스트의 재현성 검증을 위해 자동으로 position checkpoint를 삽입합니다.

- **Faithful 모드**: 매 5초 간격으로 `FCheckpointPosition` 삽입
- **Semantic 모드**: 각 semantic action 완료 후 삽입

Checkpoint는 녹화 시점의 플레이어 위치를 기대값으로 저장하고, 재생 시 ±100 UU 이내인지 검증합니다.

---

## Error Handling

| 상황 | 처리 |
|------|------|
| PIE 미실행 상태에서 `start` | 에러: "PIE를 먼저 시작하세요" |
| 녹화 안 된 상태에서 `stop` | 에러: "활성 녹화가 없습니다" |
| `stop` 전에 `export` | 에러: "녹화를 먼저 종료하세요" |
| 0 이벤트 녹화 | 경고 + "더 긴 시간 플레이 후 재시도" 안내 |
| 60초 초과 | 자동 종료 + 경고 |
| 에디터 크래시 | "녹화가 불완전할 수 있습니다. `stop`으로 부분 데이터 확인" |
| 10,000 이벤트 초과 | 자동 트리밍 + 경고 |

---

## Enhanced Input 호환성

### 제한사항

Faithful 모드의 `FInjectInputEvent`는 `FSlateApplication::ProcessKeyDownEvent()`를 사용합니다.
**Enhanced Input** (UInputAction 기반) 프로젝트에서는 Slate 레벨 입력이 Enhanced Input 시스템으로 전달되지 않을 수 있습니다.

| 프로젝트 타입 | Faithful 모드 | Semantic 모드 |
|---------------|--------------|--------------|
| 기본 Input (Legacy) | O 정상 동작 | O 정상 동작 |
| Enhanced Input (Lyra 등) | X 동작 안 함 | O 정상 동작 |

### 대안

**Enhanced Input 프로젝트에서는 `--semantic` 모드를 사용하세요.**

`--semantic` 모드의 latent command (FMoveForward, FPerformJump 등)는 `Character->AddMovementInput()`, `Character->Jump()` 등 C++ API를 직접 호출하므로 Enhanced Input 시스템과 무관하게 동작합니다.

```bash
# Enhanced Input 프로젝트 (Lyra 등)
/ue-test-record export --semantic --test-name=FMyTest --module=LyraGame

# Legacy Input 프로젝트
/ue-test-record export --test-name=FMyTest --module=MyGame
```

**Faithful 모드 대안**: `replay_input_recording` MCP operation을 직접 사용하면 Editor-side에서 입력을 처리하므로 Enhanced Input과의 호환성이 더 높을 수 있습니다.

```python
# 녹화된 이벤트를 Editor에서 직접 재생
ue_editor_automation(
    operation="replay_input_recording",
    events=[...],  # stop에서 받은 이벤트 배열
    speed=1.0
)
```

---

## Self-Test Mode

`/ue-test-record --self-test` 실행 시 스킬 자체를 검증합니다:

1. **Structure check**: SKILL.md, REFERENCE.md, EXAMPLES.md 존재 확인
2. **Frontmatter check**: `name`, `description`, `allowed-tools` 필드 파싱
3. **Routing check**: routing.json에 `ue-test-record` 항목 존재 + trigger patterns 매칭
4. **MCP tool check**: `start_input_recording`, `stop_input_recording`, `replay_input_recording`이 `ue_editor_automation` schema에 포함 확인

**Output**: PASS / FAIL (per-check results). 전체 3-Command 워크플로우는 실행하지 않음.

---

## Dependencies

- **Issue #6378**: PIE record/replay 인프라 (`editor_control_pie.rs`)
- **Issue #6798**: `/ue-test-scenario` latent command 매핑 (`--semantic` 모드에서 재사용)
- **Issue #6797**: `run_automation_tests` 결과 assertion JSON 구조화
- **Issue #6796**: 프로젝트 패턴 감지 (테스트 헬퍼 include 판단)
