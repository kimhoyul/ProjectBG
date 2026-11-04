# Advanced Patterns

**Doc Progressive Explorer** 고급 사용 패턴입니다.

---

## 1. 키워드 포커스 (Focus)

키워드와 관련된 섹션만 필터링하여 표시합니다.

### 기본 포커스

```bash
# 키워드로 포커스
focus "Attribute"

# 출력:
# 🔍 Focus: "Attribute"
> 8개 섹션에서 발견됨

## 🎯 높은 관련성
- **Attribute Set Setup** ●●● (85점)
  _기본 AttributeSet 생성 및 구성 방법..._
- **Attribute Modifiers** ●●○ (72점)
  _GameplayEffect를 통한 Attribute 수정..._

## 📋 중간 관련성
- **Health System** ●○○ (45점)
- **Damage Calculation** ●○○ (38점)

_+ 4개 낮은 관련성 섹션 (표시 생략)_

💡 상세 보기: `drill "Attribute Set Setup"`
💡 포커스 해제: `focus clear`
```

### 포커스 + 레벨 조합

```bash
# 포커스 상태에서 레벨 변경
focus "Cooldown"
level 3  # 포커스된 섹션들을 Level 3으로 표시

# 특정 섹션 드릴다운
drill "Cooldown GameplayEffect"
```

### 포커스 확장

```bash
# 관련 컨텍스트도 함께 표시
focus "Targeting" --expand

# 출력: 타겟팅 관련 섹션 + 연관된 Setup, Usage 섹션
```

### 포커스 해제

```bash
focus clear

# 출력: 🔓 포커스 해제됨 - 전체 문서 표시
```

---

## 2. 관련성 스코어링

### 스코어 계산 방식

```text
관련성 점수 (0-100):
- 제목 매칭: +50점
  - 정확 매칭 보너스: +10점
- 첫 문단 매칭: +30점
- 코드 블록 매칭: +20점
- 본문 빈도: 최대 +20점 (빈도수 × 2)
```

### 스코어별 표시

| 점수 범위 | 표시 | 의미 |
|-----------|------|------|
| ≥80점 | ●●● | 매우 높은 관련성 |
| 60-79점 | ●●○ | 높은 관련성 |
| 30-59점 | ●○○ | 중간 관련성 |
| <30점 | (생략) | 낮은 관련성 |

---

## 3. 섹션 매칭 알고리즘

드릴다운 시 섹션을 찾는 4단계 매칭:

### 매칭 우선순위

```python
# 1. 정확 매칭 (대소문자 무시)
drill "Quick Start"  # → "Quick Start" 섹션

# 2. 시작 매칭
drill "Quick"  # → "Quick Start" 섹션 (Quick으로 시작)

# 3. 포함 매칭
drill "Start"  # → "Quick Start" 섹션 (Start 포함)

# 4. 유사도 매칭 (Levenshtein distance ≤ 3)
drill "Quik Start"  # → "Quick Start" 섹션 (오타 허용)
```

### 다중 매칭 시

```bash
drill "Setup"

# 여러 섹션이 매칭될 때:
🔍 'Setup'와 매칭되는 섹션이 3개 있습니다:
   1. **Basic Setup** (Line 45)
   2. **Advanced Setup** (Line 120)
   3. **Editor Setup** (Line 200)

💡 정확한 섹션명을 입력하세요: `drill "Basic Setup"`
```

### 섹션 없음

```bash
drill "NonExistentSection"

# 출력:
❌ 'NonExistentSection' 섹션을 찾을 수 없습니다.

📑 사용 가능한 섹션:
   1. Quick Start
   2. Ability Activation
   3. Cooldown System
   ...

💡 섹션명을 다시 확인하세요.
```

---

## 4. 캐싱 시스템 활용

### 캐시 동작

```python
# 동일 문서 재탐색 시 캐시 활용
/doc-progressive-explorer docs/GAS/EXAMPLES.md  # 첫 호출: 파싱
/doc-progressive-explorer docs/GAS/EXAMPLES.md  # 두 번째: 캐시 히트

# 캐시 상태 확인
cache status

# 출력:
📦 Cache Status:
- Entries: 23/50
- Hit Rate: 78.5%
- Memory: 15.2 MB / 100 MB
- Oldest Entry: 4분 전
```

### 캐시 설정

| 설정 | 기본값 | 설명 |
|------|--------|------|
| `max_entries` | 50 | 최대 캐시 항목 수 |
| `ttl_seconds` | 300 | 항목 유효 시간 (5분) |
| `max_memory_mb` | 100 | 최대 메모리 사용량 |

### 캐시 무효화

```bash
# 특정 문서 캐시 제거
cache invalidate docs/GAS/EXAMPLES.md

# 전체 캐시 클리어
cache clear

# 자동 무효화 조건:
# - 파일 수정 시간 변경
# - TTL 만료
# - 메모리 한도 초과 (LRU 정책)
```

---

## 5. 크로스레퍼런스 추천

### 관련 문서 추천

```bash
# 현재 문서 탐색 중 관련 문서 표시
/doc-progressive-explorer docs/Niagara/EXAMPLES.md

# 출력 하단:
## 🔗 관련 문서 추천

### 1. PCG_EXAMPLES.md 📦 🏷️
- **관련도**: 72%
- **이유**: 공통 클래스 참조 (UNiagaraDataInterface)
- **미리보기**: PCG와 Niagara를 결합하여 프로시저럴...

### 2. ChaosNiagara_EXAMPLES.md 🔗
- **관련도**: 68%
- **이유**: 명시적 링크
```

### 관계 유형

| 아이콘 | 관계 유형 | 가중치 |
|--------|-----------|--------|
| 🔗 | 명시적 링크 | 0.9 |
| 📦 | 클래스 참조 | 0.8 |
| 📥 | Import 체인 | 0.7 |
| 🔧 | API 공유 | 0.6 |
| 🏷️ | 키워드 유사 | 0.4 |

### 컨텍스트 기반 추천

```python
# 네비게이션 히스토리 기반 추천 부스팅
# - 이전에 방문한 모듈 관련 문서 우선
# - 현재 포커스 키워드와 매칭되는 문서 우선
```

---

## 6. TF-IDF 기반 키워드 분석

### 키워드 추출

```bash
# 문서의 핵심 키워드 확인
keywords

# 출력:
## 📊 핵심 키워드 (TF-IDF 기준)

| 키워드 | TF-IDF 점수 | 빈도 |
|--------|-------------|------|
| GameplayAbility | 0.85 | 47 |
| AbilitySystem | 0.72 | 38 |
| Cooldown | 0.68 | 25 |
| Targeting | 0.55 | 18 |
| AttributeSet | 0.52 | 15 |
```

### 키워드 기반 섹션 찾기

```bash
# 키워드로 가장 관련성 높은 섹션 찾기
find-by-keyword "GameplayEffect"

# 출력:
## 🔍 "GameplayEffect" 관련 섹션

1. **Cooldown System** (TF-IDF: 0.92)
2. **Attribute Modifiers** (TF-IDF: 0.85)
3. **Instant vs Duration Effects** (TF-IDF: 0.78)
```

---

## 7. 에러 처리 및 복구

### Graceful Degradation

```python
# 파싱 실패 시 부분 결과 제공
try:
    result = parse_document(path, level)
except ParseError as e:
    # 오류 발생 지점까지의 파싱 결과 반환
    return partial_result_with_warning(e)
```

### 복구 전략

| 상황 | 복구 전략 |
|------|-----------|
| 잘못된 Markdown | 원본 텍스트로 폴백 |
| 섹션 경계 불명확 | 보수적 분리 (여러 작은 섹션) |
| 인코딩 오류 | UTF-8 강제 + 오류 무시 |
| 파일 없음 | 명확한 에러 메시지 + 대안 제시 |

### 에러 메시지

```bash
# 파일 없음
❌ 파일을 찾을 수 없습니다: docs/Invalid/PATH.md

💡 확인사항:
   1. 경로가 올바른지 확인
   2. 파일이 존재하는지 확인
   3. 비슷한 파일: docs/GAS/EXAMPLES.md

# 파싱 경고
⚠️ 문서 파싱 중 경고가 발생했습니다:
   - Line 245: 닫히지 않은 코드 블록
   - 부분 결과가 표시됩니다.
```

---

## 8. 성능 최적화 팁

### 대용량 문서 처리

```bash
# Level 0-1로 먼저 구조 파악
/doc-progressive-explorer large-doc.md --level 0

# 필요한 섹션만 드릴다운
drill "Specific Section" --full
```

### 빈번한 접근 패턴

```bash
# 자주 사용하는 문서는 캐시 워밍
cache warm docs/GAS/EXAMPLES.md docs/Niagara/EXAMPLES.md

# 세션 시작 시 주요 문서 프리로드
```

### 메모리 관리

```bash
# 캐시 사용량 모니터링
cache status

# 필요시 오래된 항목 정리
cache trim --keep-recent 20
```

---

## 관련 문서

- [Basic Usage](basic-usage.md) - 기본 사용법
- [Multi-Document](multi-document.md) - 멀티 문서 탐색
- [Algorithms](../reference/algorithms.md) - 알고리즘 상세
- [Implementation](../reference/implementation.md) - 구현 세부사항

---

**Version**: 1.9.0
**Last Updated**: 2026-01-25
