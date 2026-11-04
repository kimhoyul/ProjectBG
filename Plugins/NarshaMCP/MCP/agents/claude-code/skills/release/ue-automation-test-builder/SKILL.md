---
name: ue-automation-test-builder
model: opus
description: "Purpose: UE Automation Test creation. AI-guided 6-Phase iterative cycle (Analyze → Generate → Execute → Results → Evaluate → Repeat). Supports 5 test types (Simple, Complex+Latent, BDD Spec, FunctionalTest Actor, EditorUtility). Phase 3-4 MCP-automated, Phase 1/2/5/6 Claude AI-guided. Triggers: '자동화 테스트 만들어', 'automation test', '유닛 테스트', 'functional test', '오토메이션 테스트', 'create test', 'generate test'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
allowed-tools: [Bash, Read, Glob, Grep, Edit, Write, Task]
argument-hint: "target class/function or test description (supports --self-test)"
context: fork
agent: general-purpose
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Automation Test Builder

**Priority**: High
**Goal**: 사용자 게임 코드에 대한 UE Automation Test를 대화형으로 설계/생성/실행/평가하는 반복적 워크플로우
**Issue**: #4843

---

## Purpose

사용자의 **게임 프로젝트 코드**에 대한 UE Automation Test를 6-Phase 반복 사이클로 자동 생성합니다.

기존 NarshaMCP 테스트 스킬(test-iterate, e2e-test-runner)이 NarshaMCP 자체 코드를 테스트하는 것과 달리,
이 스킬은 **사용자의 게임 클래스/함수/시스템**에 대한 C++ Automation Test를 생성하고 실행합니다.

**5가지 테스트 타입 지원**:

| Type | UE Macro/Class | Best For | Editor Required |
|------|---------------|----------|----------------|
| Simple | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` | 순수 C++ unit test | No |
| Complex + Latent | `IMPLEMENT_COMPLEX_AUTOMATION_TEST` + `DEFINE_LATENT_AUTOMATION_COMMAND` | 멀티프레임 비동기 test | Depends |
| BDD Spec (권장) | `BEGIN_DEFINE_SPEC` / `END_DEFINE_SPEC` | Given/When/Then 조직화 | No |
| Functional Actor | `AFunctionalTest` subclass | 레벨 기반 test actor | Yes |
| Editor Utility | `UEditorUtilityTest` | Blueprint 호환 테스트 | Yes |

### Automation Scope

| Phase | 수행 주체 | 방식 |
|-------|----------|------|
| Phase 1 (분석) | **Claude AI** | MCP 도구로 코드 분석 후 사양 작성 |
| Phase 2 (생성) | **Claude AI** | C++ 테스트 코드 직접 작성 |
| Phase 3 (실행) | **MCP 자동** | `run_automation_tests` 트리거 → 폴링 → 파싱 |
| Phase 4 (결과) | **MCP 자동** | 구조화된 JSON 결과 (duration_ms, expected_tests) |
| Phase 5 (평가) | **Claude AI** | 5-criteria 채점 체크리스트 기반 판정 |
| Phase 6 (반복) | **Claude AI** | 점수 기반 반복/종료 의사결정 |

Phase 3-4는 MCP 도구가 완전 자동화합니다.
Phase 1/2/5/6은 Claude가 이 스킬의 가이드라인에 따라 대화형으로 수행합니다.

---

## Trigger Phrases

### Korean
- "자동화 테스트 만들어", "오토메이션 테스트"
- "테스트 코드 생성", "유닛 테스트 작성"
- "기능 테스트 만들어", "FunctionalTest 생성"
- "데미지 테스트 만들어", "캐릭터 HP 테스트"

### English
- "create automation test", "generate unit test"
- "write functional test", "test builder"
- "create BDD spec", "automation spec"

### Should NOT Activate
- "테스트 반복 50번" → test-iterate (반복 실행, 생성 아님)
- "MCP 도구 테스트" → test-iterate (NarshaMCP 도구 테스트)
- "E2E 테스트 실행" → e2e-test-runner (NarshaMCP E2E)

---

## 6-Phase Iterative Cycle

```text
┌──────────┐   ┌──────────┐   ┌──────────┐
│ Phase 1  │──→│ Phase 2  │──→│ Phase 3  │
│ 테스트   │   │ 테스트   │   │ 테스트   │
│ 분석     │   │ 생성     │   │ 실행     │
└──────────┘   └──────────┘   └──────────┘
     ↑                              │
     │                              ↓
┌──────────┐   ┌──────────┐   ┌──────────┐
│ Phase 6  │←──│ Phase 5  │←──│ Phase 4  │
│ 반복     │   │ 테스트   │   │ 테스트   │
│ 판정     │   │ 평가     │   │ 결과     │
└──────────┘   └──────────┘   └──────────┘

종료 조건: 평가 점수 >= 4.0/5.0 또는 max_iterations 또는 stagnation
```

---

## Phase 1: 테스트 분석 (Deep Analysis & Specification)

**Goal**: 사용자의 자연어 요청을 구체적인 테스트 사양으로 변환

### Step 1.1 — 대상 코드 분석

MCP 도구로 대상 클래스/함수의 구조를 파악합니다:

```python
ue_analyze_symbols(operation="search_symbols", query="<TargetClass>")
ue_analyze_symbols(operation="find_callers", symbol="<TargetFunction>")
ue_search_assets(asset_type="blueprint", query="BP_<TargetClass>")
```

### Step 1.1b — 프로젝트 테스트 패턴 발견 (Issue #6796, updated Issue #9334)

기존 테스트 파일과 Build.cs를 분석하여 프로젝트별 패턴을 감지합니다.
**이 단계는 자동 실행되며, 결과는 Phase 2 코드 생성에 자동 반영됩니다.**

**v9334 업데이트**: 기존 Automation Test 인벤토리는 `ue_manage_automation_test(list_tests)` 단일 호출로 대체됨 (이전: `ue_glob` + 2회 `ue_grep` + `ue_read`). GAS/Enhanced Input 감지는 여전히 `ue_grep`/`ue_read`로 처리 (이 부분은 Automation Test 외 프로젝트 패턴).

```python
# 1. 기존 Automation Test 인벤토리 (Issue #9334 — 통합됨)
ue_manage_automation_test(operation="list_tests", limit=10)
# → FAutomationTestBase 후손 중 10개 샘플(패턴 감지 충분) + total_found 전체 수
# → limit=10 유지 권장: 이름 규약과 네이밍 스타일 유추에 10개면 족함
# → 전체 인벤토리가 필요하면 limit=100 또는 offset 페이징 사용
# → 기존 ue_glob Source/**/Tests/*.h + 매크로 grep 2회 + ue_read 헬퍼 대체
# → Lyra에서 측정된 토큰 감소: Phase 1.1b 61.4%, 통합 35.6% (AC #3 충족)
#
# 추가로 기존 테스트의 구조/상속이 필요하면:
#   ue_manage_automation_test(operation="get_test_metadata", test_name="<예시>")

# 2. GAS/어빌리티 시스템 사용 감지 (Automation Test 외 프로젝트 패턴)
ue_grep(query="AbilityInputTagPressed|ProcessAbilityInput|GameplayTag", scope="source")
# → GAS 입력 시뮬레이션 패턴 필요 여부

# 3. 발견된 헬퍼 내용 읽기
ue_read(identifier="<발견된 헬퍼 헤더>")
# → 재사용 가능한 함수 목록 추출 (FindLyraPIEWorld, FLyraSetWorldExperience 등)

# 4. Build.cs 모듈 의존성 확인
ue_grep(query="GameplayAbilities|EnhancedInput|FunctionalTesting", scope="source", path_pattern="Build.cs")
# → 필요한 모듈이 이미 포함되어 있는지 확인
```

**결과 요약** (Phase 2에 전달):
```text
Project Pattern Profile:
  - GAS 사용: Yes/No
  - Enhanced Input: Yes/No
  - 기존 테스트 헬퍼: <헤더 경로> (<함수 목록>)
  - 테스트 패턴: Complex+Latent / BDD Spec / Simple
  - 공통 InputTag: [InputTag.Jump, InputTag.Weapon.Fire, ...]
  - 필요 모듈: [GameplayAbilities, FunctionalTesting, ...]
```

> **No helpers found?** → 테스트 헬퍼가 없는 프로젝트 (LCP 등 첫 테스트 작성 시):
>
> 1. **Build.cs 기반 프로젝트 유형 감지**는 헬퍼 없이도 동작 (GAS/Enhanced Input 감지)
> 2. **테스트 헬퍼 자동 생성 제안**: GAS 감지 시 → `{ProjectName}TestHelpers.h` 스캐폴딩
>    - `FindPIEWorld()` 헬퍼
>    - GAS 프로젝트면 `FSetWorldExperience` (Experience 설정)
>    - 공유 상태 구조체 템플릿
>    - 기본 latent command (캐릭터 준비 대기, 이동, 점프)
> 3. 사용자에게 "테스트 헬퍼를 생성할까요?" 확인 후 Phase 2에서 생성
> 4. 이후 테스트는 생성된 헬퍼를 자동 재사용
>
> **핵심**: 첫 테스트 작성 시 헬퍼 인프라까지 같이 만들어주므로, 두 번째 테스트부터는 재사용 가능.

### Step 1.2 — 사용자 질문 (Clarification)

다음 질문으로 테스트 범위를 구체화합니다:

1. **테스트 타입**: Simple / BDD Spec(권장) / Complex / Functional / EditorUtility?
2. **테스트 범위**: 단일 함수 vs 클래스 전체 vs 시스템 통합?
3. **Edge Cases**: 어떤 경계 조건을 테스트할지?
4. **의존성**: GAS/Enhanced Input 등 서브시스템 사용 여부? *(Step 1.1b에서 자동 감지된 결과 제시)*
5. **환경**: 특정 레벨/액터/에셋 필요 여부?

### Step 1.3 — 테스트 사양 생성

분석 결과를 구조화된 사양(test_name, test_type, target_class, assertions, edge_cases 등)으로 정리합니다.
**사양을 사용자에게 보여주고 확인받은 후** Phase 2로 진행합니다.

---

## Phase 2: 테스트 생성 (Code Generation & Environment Setup)

**Goal**: 테스트 사양에서 실행 가능한 C++ 테스트 코드 생성

### Step 2.1 — 테스트 타입 선택 & 코드 생성

```text
단순 값 검증?           → Simple (IMPLEMENT_SIMPLE_AUTOMATION_TEST)
멀티프레임/비동기?      → Complex + Latent Commands
여러 관련 테스트?       → BDD Spec (BEGIN_DEFINE_SPEC) ← 권장
레벨에서 액터 테스트?   → Functional (AFunctionalTest)
BP에서도 테스트?        → EditorUtility (UEditorUtilityTest)
```

전체 코드 템플릿과 패턴은 [REFERENCE.md](REFERENCE.md)를 참조하세요.

### Step 2.1b — 프로젝트 패턴 적용 (Issue #6796)

Step 1.1b에서 감지된 프로젝트 패턴을 코드 생성에 반영합니다:

```text
IF 기존 테스트 헬퍼 발견 (예: LyraTestHelpers.h):
  → #include "<헬퍼 헤더>" 추가
  → 헬퍼 함수 재사용 (FindLyraPIEWorld, FLyraSetWorldExperience 등)
  → 중복 코드 생성 방지

IF GAS 사용 감지:
  → AbilityInputTagPressed/Released 패턴 사용 (Character::Jump() 대신)
  → ASC 초기화 패턴 포함
  → ProcessAbilityInput() 매 틱 호출

IF Complex+Latent 패턴 감지:
  → DEFINE_LATENT_AUTOMATION_COMMAND 사용
  → 공유 상태 구조체 (FTestState) 패턴
  → 타임아웃 + 진행 로깅 포함

IF 프로젝트 패턴 미감지 (새 프로젝트):
  → generic 템플릿 사용 (기존 동작 유지)
```

### Step 2.2 — 코드 생성 방법

- **Method A: 직접 코드 생성** (기본) — Claude가 C++ 테스트 코드를 직접 작성
- **Method B: ue_generate_code 활용** (AFunctionalTest only) — `derive_class` operation 사용

### Step 2.3 — 환경 구성 (Editor 필요 시)

Functional Test의 경우 테스트 레벨과 액터를 배치합니다.
전용 테스트 레벨 생성이 필요한 경우 [ADVANCED.md](ADVANCED.md)를 참조하세요.

**주의**: Lyra 등 Experience 기반 프레임워크에서는 WorldSettings에 Experience 자산을 설정해야 합니다.
→ [ADVANCED.md §9: FProperty Reflection Bypass](ADVANCED.md#9-fproperty-reflection-bypass) 참조

### Step 2.4 — 사용자 리뷰

생성된 코드를 **사용자에게 보여주고 확인받습니다** (코드 리뷰 기회 제공, 수정 요청 반영).

---

## Phase 3: 테스트 실행 (Build & Run)

**Goal**: 생성된 테스트를 빌드하고 실행

### Step 3.1 — 빌드 방법 선택

| 상황 | 방법 | 이유 |
|------|------|------|
| **새 .cpp 파일 생성** (Phase 2 첫 실행) | **UBT 풀빌드** (에디터 종료 필요) | LiveCoding은 새 파일 미감지 |
| **기존 .cpp 수정** (반복 개선) | **LiveCoding** (`compile_project`) | 10-30초, 에디터 유지 |
| **Editor 오프라인** | **Build.bat** 직접 실행 | WS 연결 불가 |

> **핵심**: Phase 6에서 Phase 2로 돌아가 코드를 **수정**할 때는 LiveCoding 사용.
> 최초 테스트 파일 **생성** 시에만 UBT 풀빌드가 필요합니다.

### Step 3.2 — 테스트 실행

```python
# Method A (권장): run_automation_tests — 실행+대기+파싱 원스텝
ue_editor_automation(operation="run_automation_tests",
    test_name="MyGame.Characters.TakeDamage",
    timeout_seconds=120)

# Method B: Console Command (수동 폴링 필요)
ue_editor_automation(operation="execute_console_command",
    command="Automation RunTests MyGame.Characters.TakeDamage")
```

**테스트 목록 확인**:
```python
ue_editor_automation(operation="list_automation_tests", filter="MyGame")
```

Method A는 자동으로 폴링+파싱하므로 별도 대기 불필요.
Method B는 비동기이므로 `get_error_log`로 수동 확인 필요.

---

## Phase 4: 테스트 결과 (Result Parsing & Report)

**Goal**: 테스트 실행 결과를 구조화된 리포트로 변환

**v9334 업데이트**: 과거 실행 결과 파싱 + 실패 분류는 native 도구 `ue_manage_automation_test`로 대체 (이전: `get_error_log` regex 파싱, ~120 토큰).

```python
# 1. 최근 실행 결과 (Saved/Automation/*.json + Saved/Logs/*.log fallback)
ue_manage_automation_test(
    operation="get_recent_results",
    days=7,               # 최근 N일 (기본 7, 최대 90)
    limit=20,
    only_failures=False   # True면 실패만 필터
)
# → {tests: [{name, status, failure_kind, message, file_source, timestamp_epoch}]}

# 2. 실패 분류 (compile/runtime/assertion/timeout)
ue_manage_automation_test(
    operation="categorize_failures",
    days=7,
    group_by="kind"       # kind | test | module
)
# → {groups: {assertion: 8, runtime: 3, timeout: 1}, total_failures: 12}
```

**경로별 사용 가이드**:

- **Method A (`run_automation_tests`)**: 반환 payload에 `tests_run, passed, failed, results` 이미 포함 → Phase 4 추가 파싱 불필요
- **Method B/C (비동기 실행)**: `ue_manage_automation_test(get_recent_results)`로 `Saved/Automation/` JSON을 파싱. Editor 꺼진 상태 + 헤드리스 CI에서도 동작
- **레거시 로그 파싱 (fallback)**: JSON 결과 파일이 없으면 위 도구가 자동으로 `Saved/Logs/*.log`의 `LogAutomationController` 카테고리를 스캔

**주의**: `LogAutomationTest`가 아닌 `LogAutomationController`입니다. (도구가 이 구분을 자동 처리하지만, 직접 grep 시 참고)

시각적 검증이 필요한 Functional Test의 경우 `capture_viewport_analyze` 사용 가능.

---

## Phase 5: 테스트 평가 (Test Quality Assessment)

**Goal**: 생성된 테스트의 품질을 객관적으로 평가하고 개선 방향 제시

### 5-Criteria Weighted Scoring

| Criterion | Weight | Scale | Description |
|-----------|--------|-------|-------------|
| **Test Coverage** | 30% | 1-5 | 대상 함수/분기의 커버리지. edge case 포함 여부 |
| **Assertion Quality** | 25% | 1-5 | assertion 다양성, 의미 있는 검증 |
| **Test Isolation** | 20% | 1-5 | 외부 의존성 최소화, 적절한 setup/teardown |
| **Readability** | 15% | 1-5 | 테스트 이름 명확성, Arrange-Act-Assert 패턴 |
| **Robustness** | 10% | 1-5 | flaky test 방지, 타이밍/환경 독립성 |

### Score Interpretation

- **4.0+ / 5.0 → PASS**: 테스트 품질 충분. 완료 또는 다음 테스트 대상으로 진행
- **3.0 - 3.9 → IMPROVE**: 구체적 개선 방향 제시 후 Phase 2로 돌아가 테스트 개선
- **< 3.0 → REWRITE**: 근본적 문제. Phase 1로 돌아가 재분석

### 채점 템플릿

| Criterion | Score | Evidence |
|-----------|-------|----------|
| **Coverage** (30%) | _/5 | 테스트된 분기/함수: _개, edge case: _개 |
| **Assertion** (25%) | _/5 | assertion 종류: _, 의미 있는 검증: Y/N |
| **Isolation** (20%) | _/5 | BeforeEach/AfterEach: Y/N, 외부 의존성: _개 |
| **Readability** (15%) | _/5 | AAA 패턴: Y/N, 테스트명 명확: Y/N |
| **Robustness** (10%) | _/5 | flaky 위험: Y/N, float 비교 안전: Y/N |
| **Total** | **_/5.0** | `= Σ(score × weight)` |

**중요**: 점수가 아닌 **Evidence** 열이 핵심. 사용자가 왜 그 점수인지 이해할 수 있어야 합니다.

### Duration-Based Analysis

`run_automation_tests` 결과에 `duration_ms`가 포함되면 느린 테스트를 식별:
- **> 10초**: 불필요한 대기/로딩 확인
- **> 30초**: 테스트 분할 또는 비동기 처리 검토

---

## Phase 6: 반복 판정 (Iteration Decision)

**Goal**: 평가 결과에 따라 반복 또는 종료 결정

### Exit Conditions (순서대로 확인)

1. **Score >= 4.0?** → PASS. Final Report 생성 후 종료
2. **반복 5회 이상?** → MAX. 최고 점수 iteration의 코드를 최종본으로 채택
3. **직전 2회 점수 동일?** → STAGNATION. 현재 코드 채택 + 수동 개선 안내
4. **전체 PASS + Score >= 3.5?** → ALL_PASS. 모든 테스트 통과이므로 충분
5. **그 외** → Phase 2로 돌아가 개선. 가장 낮은 항목부터 집중 개선

### 개선 적용 (Phase 2로 돌아갈 때)

- **Coverage 낮음** → edge case 추가
- **Assertion 낮음** → assertion 다양화 (TestTrue + TestEqual + TestNull 등)
- **Isolation 낮음** → BeforeEach/AfterEach 추가, 글로벌 의존성 제거
- **Readability 낮음** → 테스트명 개선, AAA 패턴 적용
- **Robustness 낮음** → float 안전 비교, 타이밍 독립 처리

개선 후 Phase 3 (실행)으로 직행합니다 (Phase 1 재분석 불필요).

---

## Offline Mode

Editor가 연결되지 않은 경우에도 Phase 1 (분석) + Phase 2 (코드 생성)은 동작합니다:

- Phase 1: `ue_analyze_symbols`는 PDB 인덱스 기반으로 Editor 없이 동작
- Phase 2: C++ 코드 생성은 로컬 파일 작성이므로 Editor 불필요
- Phase 3-4: Editor 필요 — 사용자에게 수동 실행 안내 제공

---

## Anti-Patterns

| Do NOT | Do Instead | Why |
|--------|------------|-----|
| execute_python으로 C++ assertion 실행 | C++ 테스트 생성 후 Automation RunTests | Python에서 UE TestTrue/TestEqual 사용 불가 |
| 한 번에 전체 6-Phase 자동 실행 | 사용자 확인 후 단계별 진행 | Phase 1 사양 검토, Phase 2 코드 리뷰 기회 |
| Editor 필수 가정 | Offline mode 지원 | Phase 1+2는 Editor 없이 동작 |
| 점수 없이 무한 반복 | 매 반복 5-criteria scoring | 객관적 품질 추적 필수 |
| float 정확 동치 비교 | FMath::IsNearlyEqual 사용 | 부동소수점 오차로 flaky test |
| 새 .cpp에 LiveCoding 사용 | 에디터 닫고 UBT 빌드 | 새 파일 미감지, 심볼 미등록 |
| FPS 쓰로틀 무시 (Method B/C) | `run_automation_tests` 사용 (자동 처리) | `FWaitForInteractiveFrameRate` 600s 대기 |

---

**Detailed reference**: [REFERENCE.md](REFERENCE.md)
**Usage examples**: [EXAMPLES.md](EXAMPLES.md)
**Advanced topics**: [ADVANCED.md](ADVANCED.md)

## Output Format

```text
=== UE Automation Test Builder -- Final Report ===

Test Suite: MyGame.Characters.TakeDamage
Iterations: 3
Final Score: 4.2/5.0 (PASS)
Score History: [2.8, 3.6, 4.2]

Tests Generated: 6
  PASSED  ReducesHP
  PASSED  ZeroDamage_HPUnchanged
  PASSED  NegativeDamage_Clamped
  PASSED  ExactKill_HPZero
  PASSED  Overkill_HPClamped
  PASSED  DamageWithArmor_Reduced

Files Created:
  Source/MyGame/Tests/MyCharacter_TakeDamage.spec.cpp

Improvements Applied:
  Iter 1->2: Added 3 edge cases, fixed float comparison
  Iter 2->3: Added cleanup, improved test names
```

## Self-Test Mode

`/ue-automation-test-builder --self-test` runs a focused validation of this skill:

1. **Structure check**: SKILL.md, REFERENCE.md, EXAMPLES.md, ADVANCED.md, PROMPT.md 존재 확인
2. **Frontmatter check**: `name`, `description`, `allowed-tools`, `argument-hint` 필드 유효성
3. **Routing check**: `routing.json`에 `ue-automation-test-builder` 엔트리 존재 + trigger patterns 매칭
4. **Pattern library check**: REFERENCE.md에 "Lyra/GAS Test Patterns" 섹션 존재
5. **Phase completeness**: SKILL.md에 Phase 1-6 모두 정의 + Step 1.1b (패턴 발견) 존재

**Output**: PASS / FAIL with per-check results. 전체 6-Phase 워크플로우는 실행하지 않음.

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `run_automation_tests` returns 0 tests found | Test class not registered or binary not rebuilt after adding new .cpp | Close Editor, run UBT full build, then relaunch |
| `FWaitForInteractiveFrameRate` 600s timeout | Editor throttles CPU when not in foreground (Method B/C only) | Use `run_automation_tests` (Method A) which auto-sets BenchmarkMode |
| `compile_project` LiveCoding fails silently | Header changes or new UCLASS require full UBT rebuild | Kill Editor process, run `Build.bat`, then relaunch |
## MCP Tool Examples

```python
# Generate test code for automation testing
ue_generate_code(operation="smart", test_target="ACharacter::TakeDamage")
```
