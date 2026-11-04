# Multi-Document Exploration

**Doc Progressive Explorer**의 멀티 문서 동시 탐색 기능입니다.

---

## 1. 여러 문서 동시 열기

### 기본 사용법

```bash
# 여러 문서 동시 열기
/doc-progressive-explorer docs/GAS/EXAMPLES.md docs/StateTree/EXAMPLES.md --mode parallel

# 출력:
## 📚 Multi-Document View (2 documents)

### 📄 GAS/EXAMPLES.md (Level 0)
> Gameplay Ability System 사용 예시

**섹션 (12개)**: Quick Start, Ability Activation, Cooldown...

### 📄 StateTree/EXAMPLES.md (Level 0)
> StateTree AI 시스템 사용 예시

**섹션 (8개)**: Basic Setup, State Transitions, Evaluators...

💡 특정 문서 선택: `select 1` 또는 `select "GAS"`
💡 비교 보기: `compare`
```

### 로딩 전략

| 전략 | 명령 | 용도 |
|------|------|------|
| **PARALLEL** | `--mode parallel` | 모든 문서 동시 로드 (기본) |
| **LAZY** | `--mode lazy` | 첫 문서만 로드, 나머지 필요 시 |
| **PRIORITY** | `--mode priority` | 캐시된 것 우선, 작은 파일 우선 |
| **CHUNKED** | `--mode chunked` | 청크 단위로 점진적 로드 |

---

## 2. 로딩 결과

### 로드 통계

```bash
# 로드 완료 시 통계 표시
📊 Load Statistics:
- Documents loaded: 5/5 (100%)
- Total time: 145ms
- Parallel efficiency: 3.2x
- Cache hits: 2

| Document | Load Time | Status |
|----------|-----------|--------|
| GAS/EXAMPLES.md | 35ms | ✅ |
| StateTree/EXAMPLES.md | 42ms | ✅ |
| Niagara/EXAMPLES.md | 38ms | ✅ (cached) |
| PCG/EXAMPLES.md | 15ms | ✅ (cached) |
| CommonUI/EXAMPLES.md | 45ms | ✅ |
```

### 실패 처리

```bash
# 일부 문서 로드 실패 시
⚠️ Partial Load (4/5 succeeded)

Failed:
- docs/Invalid/PATH.md: File not found

Successfully loaded documents are available.
💡 실패한 문서 무시하고 계속: `continue`
```

---

## 3. 문서 비교

### 구조 비교

```bash
# 두 문서의 구조 비교
compare

# 출력:
## 📊 Document Comparison

### GAS/EXAMPLES.md vs StateTree/EXAMPLES.md

**유사도**: 45%

| 메트릭 | 값 |
|--------|-----|
| 공통 섹션 | 3개 |
| GAS만 있는 섹션 | 9개 |
| StateTree만 있는 섹션 | 5개 |
| 내용 중복률 | 12% |
| 구조 유사도 | 38% |

**공통 키워드**: Setup, Activation, Debug, Profiling

**공통 섹션**:
- Quick Start (유사도: 65%)
- Debugging (유사도: 72%)
- Performance (유사도: 58%)
```

### 섹션별 Diff

```bash
# 특정 섹션의 차이 비교
diff "Quick Start"

# 출력:
## 📝 Section Diff: "Quick Start"

### GAS/EXAMPLES.md
```cpp
// AbilitySystemComponent 기반
void AMyCharacter::SetupASC()
{
    ASC = CreateDefaultSubobject<UAbilitySystemComponent>("ASC");
}
```

### StateTree/EXAMPLES.md
```cpp
// StateTreeComponent 기반
void AMyAI::SetupStateTree()
{
    StateTreeComp = CreateDefaultSubobject<UStateTreeComponent>("StateTree");
}
```

**차이점**: 컴포넌트 타입, 초기화 방식
```text

### 다중 문서 비교 매트릭스

```bash
# 3개 이상 문서의 상호 비교
compare-matrix

# 출력:
## 📊 Comparison Matrix (5 documents)

|                | GAS | StateTree | Niagara | PCG | CommonUI |
|----------------|-----|-----------|---------|-----|----------|
| **GAS**        | -   | 45%       | 32%     | 28% | 55%      |
| **StateTree**  | 45% | -         | 25%     | 35% | 40%      |
| **Niagara**    | 32% | 25%       | -       | 68% | 20%      |
| **PCG**        | 28% | 35%       | 68%     | -   | 15%      |
| **CommonUI**   | 55% | 40%       | 20%     | 15% | -        |

💡 높은 유사도: Niagara ↔ PCG (68%)
💡 낮은 유사도: PCG ↔ CommonUI (15%)
```

---

## 4. 통합 뷰

### 통합 목차

```bash
# 모든 문서의 통합 목차 생성
unified-toc

# 출력:
## 📑 Unified Table of Contents

### 🔧 Setup & Configuration
- [GAS] Quick Start
- [StateTree] Basic Setup
- [Niagara] System Setup
- [PCG] Graph Configuration

### ⚡ Core Features
- [GAS] Ability Activation
- [GAS] Cooldown System
- [StateTree] State Transitions
- [Niagara] Emitter Configuration

### 🐛 Debugging
- [GAS] Debugging Tips
- [StateTree] Debug Visualization
- [Niagara] Debug Drawing
```

### 키워드별 크로스 뷰

```bash
# 키워드로 모든 문서 필터링
focus-all "Performance"

# 출력:
## 🔍 Cross-Document Focus: "Performance"

### GAS/EXAMPLES.md (3 sections)
- **Performance Optimization** ●●● (92점)
- **Batching Abilities** ●●○ (65점)

### Niagara/EXAMPLES.md (2 sections)
- **GPU Performance** ●●● (88점)
- **LOD Settings** ●○○ (45점)

### PCG/EXAMPLES.md (2 sections)
- **Runtime Performance** ●●● (85점)
- **Async Generation** ●●○ (62점)
```

---

## 5. 문서 간 네비게이션

### 문서 선택

```bash
# 인덱스로 선택
select 1

# 이름으로 선택
select "GAS"

# 선택 후 일반 탐색 명령 사용
level 2
drill "Cooldown"
```

### 빠른 전환

```bash
# 다음 문서로
next

# 이전 문서로
prev

# 특정 문서로 점프
jump "StateTree"
```

### 동시 드릴다운

```bash
# 모든 문서에서 같은 섹션 드릴다운
drill-all "Quick Start"

# 출력: 각 문서의 "Quick Start" 섹션 나란히 표시
```

---

## 6. 세션 관리

### 문서 추가/제거

```bash
# 현재 세션에 문서 추가
add docs/EnhancedInput/EXAMPLES.md

# 문서 제거
remove "GAS"

# 모든 문서 닫기
close-all
```

### 세션 저장/복원

```bash
# 현재 세션 상태 저장
save-session "my-research"

# 세션 복원
load-session "my-research"

# 저장되는 정보:
# - 열린 문서 목록
# - 각 문서의 현재 레벨
# - 포커스 상태
# - 네비게이션 히스토리
```

---

## 7. 성능 고려사항

### 동시 로딩 최적화

```python
# 병렬 효율성 계산
parallel_efficiency = sequential_time / actual_time

# 예시:
# - 5개 문서, 각각 50ms
# - 순차: 250ms
# - 병렬: 80ms
# - 효율성: 3.1x
```

### 권장 설정

| 시나리오 | 권장 전략 | 이유 |
|----------|-----------|------|
| 2-3개 문서 | PARALLEL | 가장 빠름 |
| 5개 이상 | PRIORITY | 캐시 활용 |
| 대용량 문서 | CHUNKED | 메모리 절약 |
| 탐색적 분석 | LAZY | 필요한 것만 로드 |

### 메모리 관리

```bash
# 멀티 문서 메모리 사용량 확인
memory-status

# 출력:
📊 Memory Usage:
- Loaded documents: 5
- Total memory: 45 MB
- Per-document average: 9 MB
- Cache overhead: 8 MB

💡 메모리가 부족하면: `--mode chunked` 사용
```

---

## 8. 실전 워크플로우

### 시스템 간 비교 분석

```bash
# 1. 관련 시스템 문서 동시 열기
/doc-progressive-explorer docs/GAS/EXAMPLES.md docs/StateTree/EXAMPLES.md docs/MassEntity/EXAMPLES.md --mode parallel

# 2. 구조 비교
compare-matrix

# 3. 특정 주제로 필터링
focus-all "AI"

# 4. 상세 비교
select "StateTree"
drill "State Transitions"
```

### 학습 경로 구성

```bash
# 1. 기초 문서부터 순차 탐색
/doc-progressive-explorer docs/Core/EXAMPLES.md --mode lazy

# 2. 필요시 관련 문서 추가
add docs/Engine/EXAMPLES.md

# 3. 세션 저장
save-session "learning-path-day1"
```

### 문제 해결 조사

```bash
# 1. 관련 가능성 있는 문서 모두 열기
/doc-progressive-explorer docs/*/TROUBLESHOOT.md --mode priority

# 2. 에러 키워드로 전체 검색
focus-all "NullPointer"

# 3. 관련 섹션 비교
compare "Error Handling"
```

---

## 9. 멀티 문서 명령어 요약

| 명령어 | 설명 |
|--------|------|
| `select <n>` | 문서 선택 |
| `next` / `prev` | 문서 전환 |
| `compare` | 두 문서 비교 |
| `compare-matrix` | 전체 비교 매트릭스 |
| `diff "section"` | 섹션별 Diff |
| `unified-toc` | 통합 목차 |
| `focus-all "keyword"` | 전체 키워드 검색 |
| `drill-all "section"` | 전체 드릴다운 |
| `add <path>` | 문서 추가 |
| `remove <name>` | 문서 제거 |
| `save-session <name>` | 세션 저장 |
| `load-session <name>` | 세션 로드 |

---

## 관련 문서

- [Basic Usage](basic-usage.md) - 기본 사용법
- [Advanced Patterns](advanced-patterns.md) - 고급 패턴
- [Implementation](../reference/implementation.md) - 구현 세부사항
- [API Reference](../reference/api.md) - 전체 API

---

**Version**: 1.9.0
**Last Updated**: 2026-01-25
