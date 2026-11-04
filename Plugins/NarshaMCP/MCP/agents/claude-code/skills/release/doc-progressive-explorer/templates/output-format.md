# Output Format Templates

**Doc Progressive Explorer** 출력 포맷 템플릿입니다.

---

## 1. Level별 출력 포맷

### Level 0 (Summary) - ~10줄

```markdown
# [문서 제목]
> [1줄 요약]

## 📑 섹션 목록 (N개)
1. **[섹션 1]** - [간략 설명]
2. **[섹션 2]** - [간략 설명]
3. **[섹션 3]** - [간략 설명]
...

💡 다음 단계: `level 1` 또는 `drill "[섹션명]"`
```

---

### Level 1 (Overview) - ~50줄

```markdown
# [문서 제목]
> [1줄 요약]

## 📑 [섹션 1]
[첫 문단 내용...]

## 📑 [섹션 2]
[첫 문단 내용...]

## 📑 [섹션 3]
[첫 문단 내용...]

💡 상세 보기: `level 2` 또는 `drill "[섹션명]"`
```

---

### Level 2 (Structure) - ~150줄

```markdown
## 📑 [섹션 1]
[첫 문단 내용...]

### 핵심 포인트
- ✅ [주요 항목 1]
- ✅ [주요 항목 2]
- ⚠️ [주의 사항]

### 코드 구조
```cpp
// 시그니처만 (구현 생략)
class UMyClass : public UObject
{
    UFUNCTION(BlueprintCallable)
    void MyFunction(FName Param);
    // ... (N more methods)
};
```

💡 전체 코드: `level 3` 또는 `drill "[섹션명]" --full`
```cpp

---

### Level 3 (Details) - ~500줄

```markdown
## 📑 [섹션 1]
[전체 내용...]

### 상세 설명
[설명 텍스트...]

### 전체 코드
```cpp
// 전체 구현 포함
class UMyClass : public UObject
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "MyCategory")
    void MyFunction(FName Param)
    {
        // 실제 구현 코드
        if (!Param.IsValid())
        {
            return;
        }
        // ...
    }
};
```

## 🔗 References
- [관련 문서 1](link1)
- [관련 문서 2](link2)

💡 원본 전체: `level 4`
```text

---

### Level 4 (Complete)

```markdown
[원본 문서 전체 - 수정 없이 그대로 출력]
```

---

## 2. 특수 출력 포맷

### Focus 결과

```markdown
## 🔍 Focus: "[키워드]"
> N개 섹션에서 발견됨

### 🎯 높은 관련성
- **[섹션 A]** ●●● (85점)
  _[미리보기 텍스트...]_
- **[섹션 B]** ●●○ (72점)
  _[미리보기 텍스트...]_

### 📋 중간 관련성
- **[섹션 C]** ●○○ (45점)
- **[섹션 D]** ●○○ (38점)

_+ N개 낮은 관련성 섹션 (표시 생략)_

💡 상세 보기: `drill "[섹션명]"`
💡 포커스 해제: `focus clear`
```

### 관련성 점수 표시

| 점수 범위 | 표시 | 의미 |
|-----------|------|------|
| ≥80점 | ●●● | 매우 높은 관련성 |
| 60-79점 | ●●○ | 높은 관련성 |
| 30-59점 | ●○○ | 중간 관련성 |
| <30점 | (생략) | 낮은 관련성 |

---

### Drill-Down 결과

```markdown
## 🔍 [섹션명] (Level N → Level N+1)

[섹션 상세 내용...]

### 하위 섹션
1. [하위 섹션 1]
2. [하위 섹션 2]

💡 더 상세히: `level up` 또는 `drill "[하위 섹션]"`
💡 이전으로: `back`
```

---

### 자동 레벨 선택 결과

```markdown
## 🎯 쿼리 분석: "[사용자 쿼리]"

| 분석 항목 | 결과 |
|-----------|------|
| 📊 의도 | specific (특정 주제) |
| 📈 문서 복잡도 | medium (65점) |
| ➡️ 권장 레벨 | 2 (Structure) |

---

[Level 2 출력 내용, 관련 섹션 하이라이트]
```

---

## 3. 네비게이션 힌트

### 레벨 이동

```markdown
💡 더 상세히: `level up` 또는 `level 3`
💡 더 요약: `level down` 또는 `level 1`
💡 특정 섹션: `drill "[섹션명]"`
```

### 포커스 관련

```markdown
💡 키워드 검색: `focus "[키워드]"`
💡 포커스 해제: `focus clear`
💡 관련 섹션만: `focus "[키워드]" --expand`
```

### 히스토리 관련

```markdown
💡 이전 상태로: `back`
💡 탐색 기록: `history`
💡 초기 상태로: `level reset`
```

---

## 4. 상태 표시 포맷

### 현재 상태 (status)

```markdown
## 📍 현재 상태

| 항목 | 값 |
|------|-----|
| 📄 문서 | [문서명] |
| 📊 레벨 | 2 (Structure) |
| 🔍 포커스 | "[키워드]" |
| 📜 히스토리 | level:0 → level:1 → drill:"Setup" |

💡 사용 가능한 명령: `level`, `drill`, `focus`, `back`
```

---

### 탐색 히스토리 (history)

```markdown
## 📜 탐색 히스토리

| # | 시간 | 명령 | 결과 |
|---|------|------|------|
| 1 | 10:01 | 문서 열기 | Level 0 로드 |
| 2 | 10:02 | `level 1` | Level 1 표시 |
| 3 | 10:03 | `drill "Setup"` | Setup 섹션 드릴다운 |
| 4 | 10:05 | `focus "config"` | 3개 섹션 필터링 |

💡 특정 상태로: `back 2` (2번 상태로 복원)
```

---

## 5. 에러 메시지 포맷

### 파싱 오류

```markdown
## 📄 문서 파싱 오류

문서 형식에 문제가 있습니다.
> [상세 에러 메시지]

💡 **해결 방법**: Markdown 문법을 확인하세요.
```

### 섹션 없음

```markdown
## 🔍 섹션을 찾을 수 없음

'[섹션명]' 섹션이 문서에 없습니다.

📑 사용 가능한 섹션:
1. Quick Start
2. Ability Activation
3. Cooldown System
...

💡 **해결 방법**: 섹션명을 다시 확인하세요.
```

### 네비게이션 오류

```markdown
## 🧭 네비게이션 오류

이동할 수 없는 상태입니다.
> [상세 에러 메시지]

💡 **해결 방법**: `history`로 현재 상태를 확인하세요.
```

### 파일 없음

```markdown
## ❌ 파일을 찾을 수 없음

파일을 찾을 수 없습니다: [경로]

💡 확인사항:
1. 경로가 올바른지 확인
2. 파일이 존재하는지 확인
3. 비슷한 파일: [추천 경로]
```

---

## 6. 멀티 문서 출력 포맷

### 동시 로드 결과

```markdown
## 📚 Multi-Document View (N documents)

### 📄 [문서 1] (Level 0)
> [1줄 요약]
**섹션 (N개)**: [섹션 목록...]

### 📄 [문서 2] (Level 0)
> [1줄 요약]
**섹션 (N개)**: [섹션 목록...]

💡 문서 선택: `select 1` 또는 `select "[문서명]"`
💡 비교 보기: `compare`
```

### 비교 결과

```markdown
## 📊 Document Comparison

### [문서 1] vs [문서 2]

**유사도**: 45%

| 메트릭 | 값 |
|--------|-----|
| 공통 섹션 | 3개 |
| [문서 1]만 있는 섹션 | 9개 |
| [문서 2]만 있는 섹션 | 5개 |
| 내용 중복률 | 12% |

**공통 키워드**: Setup, Activation, Debug, Profiling

**공통 섹션**:
- Quick Start (유사도: 65%)
- Debugging (유사도: 72%)
- Performance (유사도: 58%)
```

---

## 7. 예상 출력량 가이드

| Level | 이름 | 예상 출력량 | 용도 |
|-------|------|-------------|------|
| 0 | Summary | ~10줄 | 빠른 개요 |
| 1 | Overview | ~50줄 | 섹션별 요약 |
| 2 | Structure | ~150줄 | 코드 시그니처 |
| 3 | Details | ~500줄 | 전체 코드+설명 |
| 4 | Complete | 전체 | 원본 문서 |

---

## 관련 문서

- [Basic Usage](../examples/basic-usage.md) - 기본 사용 예시
- [Advanced Patterns](../examples/advanced-patterns.md) - 고급 패턴
- [API Reference](../reference/api.md) - 전체 API

---

**Version**: 1.9.0
**Last Updated**: 2026-01-25
