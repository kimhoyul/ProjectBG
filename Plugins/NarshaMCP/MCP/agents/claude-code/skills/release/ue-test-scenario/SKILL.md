---
name: ue-test-scenario
model: opus
description: "Purpose: Convert natural language game scenarios to Complex+Latent UE automation tests. Multi-phase: parse scenario -> design structure -> generate code -> build -> execute. Triggers: '테스트 시나리오', 'test scenario', '시나리오 테스트', '시나리오 검증'."
compatibility: "Requires NarshaMCP MCP server (v0.9.0+). Editor required for Phase 4 (build+execute)."
allowed-tools: [Bash, Read, Glob, Grep, Edit, Write]
argument-hint: "자연어 시나리오 (예: '캐릭터가 10m 전진 후 점프')"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# /ue-test-scenario

자연어 게임 시나리오를 UE Complex+Latent 자동화 테스트로 변환합니다.

## When to Use

- "캐릭터가 10m 전진 후 점프" → 자동화 테스트 생성
- "적에게 사격 후 HP 감소 확인" → assertion 포함 테스트
- 게임플레이 시나리오를 자동 검증 가능한 코드로

## When NOT to Use

- 단일 함수 유닛 테스트 → `/ue-test-gen`
- 기존 테스트 반복 개선 → `/ue-automation-test-builder`
- 테스트가 아닌 기능 구현 → `/ue-scaffold`

---

## 5-Phase Workflow

### Phase 0: Discovery (프로젝트 패턴 감지)

프로젝트의 테스트 인프라를 탐색합니다.

```python
# 1. 기존 테스트 헬퍼 검색
ue_grep(query="TestHelpers|DEFINE_LATENT_AUTOMATION_COMMAND", path_pattern="Source/")

# 2. GAS/Enhanced Input 사용 여부
ue_grep(query="UAbilitySystemComponent|UInputAction", path_pattern="Source/")

# 3. 기존 자동화 테스트 패턴
ue_glob(pattern="Source/**/Tests/*.cpp")
```

**출력**: `project_context` — 감지된 패턴, 헬퍼 파일, GAS 여부, 테스트 타입 선호

### Phase 1: Parse (시나리오 → 액션 리스트 분해)

자연어 시나리오를 구조화된 액션 리스트로 분해합니다.

**입력**: `"캐릭터가 10m 전진 후 점프 → 착지 후 사격"`

**출력**:
```json
{
  "actions": [
    {"type": "move", "direction": "forward", "distance_cm": 1000},
    {"type": "jump", "wait_for_land": true},
    {"type": "fire_weapon"}
  ],
  "assertions": [
    {"after_action": 0, "check": "displacement", "operator": ">=", "value": 1000, "unit": "cm"},
    {"after_action": 1, "check": "is_falling", "value": true},
    {"after_action": 1, "check": "landed", "value": true},
    {"after_action": 2, "check": "weapon_fired", "value": true}
  ]
}
```

**액션 매핑 테이블**:

| 자연어 패턴 | Action Type | Latent Command | 자동 Assertion |
|-------------|-------------|----------------|---------------|
| "N m 전진", "move forward N" | `move` | `FMoveForward(N*100)` | 거리 >= N*100 cm |
| "점프", "jump" | `jump` | `FPerformJump` + `FWaitForLanded` | IsFalling() + 착지 |
| "사격", "fire", "shoot" | `fire_weapon` | `FFireWeapon` | 발사 이벤트 확인 |
| "대기 N초", "wait N seconds" | `wait` | `FWaitSeconds(N)` | — |
| "HP 확인", "check health" | `check_health` | — | `GetHealth() == expected` |
| "재장전", "reload" | `reload` | `FReloadWeapon` | 재장전 완료 |
| "달리기", "sprint" | `sprint` | `FStartSprint` | 속도 > 기본속도 |

**Default values for bare actions** (when user omits parameters):

| Action | Default Parameter |
|--------|------------------|
| move/walk | Distance: 200cm, Direction: Forward |
| run/sprint | Distance: 500cm, Direction: Forward |
| jump | Use default JumpZVelocity |
| wait | Duration: 1.0s |

### Phase 2: Design (테스트 구조 설계)

액션 리스트를 C++ 테스트 구조로 매핑합니다.

1. **테스트 타입 결정**: 2+ 액션 → `complex_latent`, 1 액션 → `simple`
2. **Latent Command 매핑**: 각 액션 → `DEFINE_LATENT_AUTOMATION_COMMAND` 블록
3. **Assertion 매핑**: 각 검증 → `TestTrue`/`TestEqual`/`TestNearlyEqual`
4. **헬퍼 재사용**: Phase 0에서 감지된 프로젝트 헬퍼 include

**설계 출력**:
```
TestClass: FLyraMoveJumpFireTest
TestType: complex_latent
Commands: [FMoveForward, FWaitForLanded, FPerformJump, FWaitForLanded, FFireWeapon]
Assertions: 4
Helpers: [LyraTestHelpers.h]
Module: LyraStarterGame
```

### Phase 3: Generate (코드 생성)

`ue_generate_code`를 사용하여 테스트 코드를 생성합니다.

```python
# Step 1: 기본 테스트 골격 생성
ue_generate_code(
    operation="generate_test",
    test_target="ALyraCharacter",
    test_type="complex_latent",
    test_name="FLyraMoveJumpFireTest",
    module_name="LyraGame",
)

# Step 2: 생성된 코드에 시나리오별 Latent Command 삽입
# Edit tool로 DEFINE_LATENT_AUTOMATION_COMMAND 블록 추가
# assertion 코드 삽입

# Step 2.5: Character acquisition
# Player tests: UGameplayStatics::GetPlayerPawn(GetWorld(), 0)
# NPC tests: GetWorld()->SpawnActor<ACharacter>(CharacterClass)
#   where CharacterClass is discovered from Phase 1 project_context

# Step 3: Write 최종 파일
Write("Source/LyraGame/Tests/LyraMoveJumpFireTest.cpp", generated_code)
```

### Phase 4: Build & Execute (빌드 + 실행)

```python
# Step 1: 빌드
ue_fix_errors(operation="build_and_fix", project_root="...")

# Step 2: 테스트 실행
ue_editor_automation(
    operation="run_automation_tests",
    test_name="LyraGame.LyraMoveJumpFireTest",
    timeout_seconds=120
)

# Step 3: 결과 검증 (#6797 assertion JSON 활용)
# response.assertions 배열로 개별 assertion 통과 확인
```

**실패 시**: Phase 3으로 돌아가 코드 수정 (최대 3회)

---

## Error Handling

| 상황 | 처리 |
|------|------|
| 인식 불가 액션 | 사용자에게 매핑 확인 요청 |
| 조건부 시나리오 | "조건부 시나리오 미지원" 안내 (Level 4) |
| 빌드 실패 | `ue_fix_errors`로 자동 수정 시도 |
| 테스트 실패 | assertion 분석 후 코드 수정 재시도 |
| 프로젝트 헬퍼 없음 | 기본 UE API로 fallback |

---

## Dependencies

- **#6797** (assertion JSON 구조화): Phase 4에서 `run_automation_tests` 결과의 개별 assertion 검증
- **#6796** (패턴 학습): Phase 0에서 프로젝트 패턴 자동 감지
