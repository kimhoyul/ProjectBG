# Basic Usage Examples

**Doc Progressive Explorer** 기본 사용 예시입니다.

---

## 경로 안내

엔진 문서 경로는 환경에 따라 다릅니다:
- **개발 환경** (저장소): `docs/engine/ue5_7/...`
- **릴리즈 패키지** (플러그인): `MCP/docs/engine/ue5_7/...`

---

## 1. 문서 열기 및 레벨 탐색

### Level 0 - 요약 보기

```bash
# 기본 호출 (Level 0 자동 선택)
# 개발 환경:
/doc-progressive-explorer docs/engine/ue5_7/modules/Plugins/GameplayAbilities/EXAMPLES.md
# 릴리즈 패키지:
/doc-progressive-explorer MCP/docs/engine/ue5_7/modules/Plugins/GameplayAbilities/EXAMPLES.md

# 명시적 Level 0 지정
/doc-progressive-explorer docs/.../EXAMPLES.md --level 0
```

**출력 예시**:
```markdown
# GameplayAbilities EXAMPLES.md
> UE5.7 Gameplay Ability System 사용 예시 모음

## 📑 섹션 목록 (12개)
1. **Quick Start** - 기본 설정 및 초기화
2. **Ability Activation** - 어빌리티 활성화 패턴
3. **Cooldown System** - 쿨다운 구현
4. **Targeting** - 타겟팅 시스템
...

💡 다음 단계: `level 1` 또는 `drill "Quick Start"`
```

### Level 1 - 개요 보기

```bash
/doc-progressive-explorer docs/.../EXAMPLES.md --level 1
```

**출력 예시**:
```markdown
# GameplayAbilities EXAMPLES.md
> UE5.7 Gameplay Ability System 사용 예시 모음

## 📑 Quick Start
GAS(Gameplay Ability System)의 기본 설정 방법입니다.
AbilitySystemComponent 설정부터 첫 어빌리티 생성까지...

## 📑 Ability Activation
어빌리티를 활성화하는 여러 패턴을 소개합니다.
InputBinding, Tag-based, Event-based 방식...

## 📑 Cooldown System
어빌리티 쿨다운을 구현하는 두 가지 방법...
```

### Level 2 - 구조 보기

```bash
/doc-progressive-explorer docs/.../EXAMPLES.md --level 2
```

**출력 예시**:
```markdown
## 📑 Quick Start

### 핵심 포인트
- ✅ AbilitySystemComponent 필수
- ✅ GameplayAbility 클래스 상속
- ⚠️ GiveAbility() 호출 필요

### 코드 시그니처
```cpp
UCLASS()
class UMyGameplayAbility : public UGameplayAbility { ... }

void AMyCharacter::SetupAbilitySystem() { ... }
```
```text

### Level 3 - 상세 보기

```bash
/doc-progressive-explorer docs/.../EXAMPLES.md --level 3
```

전체 코드 블록과 상세 설명이 포함됩니다.

### Level 4 - 전체 문서

```bash
/doc-progressive-explorer docs/.../EXAMPLES.md --level 4
```

원본 문서 전체를 표시합니다.

---

## 2. 레벨 네비게이션 명령어

### 레벨 이동

```bash
# 한 단계 위로 (더 상세히)
level up

# 한 단계 아래로 (더 요약)
level down

# 특정 레벨로 직접 이동
level 2
level 3
```

### 네비게이션 상태 확인

```bash
# 현재 상태 표시
status

# 출력:
# 📍 현재 문서: GameplayAbilities/EXAMPLES.md
# 📊 현재 레벨: 2 (Structure)
# 🔍 포커스: 없음
# 📜 히스토리: level:0 → level:1 → level:2
```

---

## 3. 드릴다운 (Drill-Down)

특정 섹션을 더 깊게 탐색합니다.

### 기본 드릴다운

```bash
# 섹션명으로 드릴다운
drill "Cooldown System"

# 출력:
## 🔍 Cooldown System (Level 2 → Level 3)

### GameplayEffect 기반 쿨다운
```cpp
UCLASS()
class UMyCooldownEffect : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UMyCooldownEffect()
    {
        DurationPolicy = EGameplayEffectDurationType::HasDuration;
        DurationMagnitude = FScalableFloat(5.0f);
        // ...
    }
};
```

### 사용 방법
1. GameplayEffect 블루프린트 생성
2. Duration 설정 (HasDuration)
3. CooldownGameplayEffectClass 지정
...
```text

### 전체 레벨로 드릴다운

```bash
# 섹션을 Level 4 (전체)로 바로 보기
drill "Targeting" --full
```

### 드릴다운 후 복귀

```bash
# 이전 상태로 돌아가기
back

# 또는 전체 문서로 복귀
level 1
```

---

## 4. 자동 레벨 선택

쿼리 의도에 따라 최적의 레벨을 자동 선택합니다.

### 사용 방법

```bash
# 쿼리와 함께 호출 - 레벨 자동 선택
/doc-progressive-explorer docs/.../EXAMPLES.md "쿨다운 구현 방법"

# 출력:
# 🎯 쿼리 분석: "쿨다운 구현 방법"
# 📊 의도: specific (특정 주제)
# 📈 문서 복잡도: medium (65점)
# ➡️ 권장 레벨: 2 (Structure)
#
# [Level 2로 자동 표시됨, Cooldown 관련 섹션 하이라이트]
```

### 쿼리 의도 유형

| 의도 | 키워드 예시 | 권장 레벨 |
|------|-------------|-----------|
| **overview** | "요약", "구조", "목록" | 0-1 |
| **specific** | "어떻게", "자세히", 섹션명 | 2-3 |
| **code** | "코드", "예제", "구현" | 3 |
| **troubleshoot** | "에러", "문제", "해결" | 2-3 |

---

## 5. 실전 워크플로우 예시

### 새로운 시스템 학습

```bash
# 1. 전체 구조 파악 (Level 0)
/doc-progressive-explorer docs/Niagara/EXAMPLES.md

# 2. 관심 섹션 식별 후 드릴다운
drill "GPU Simulation"

# 3. 코드 상세 확인
level 3

# 4. 관련 섹션 이동
drill "Performance Optimization"
```

### 특정 문제 해결

```bash
# 1. 문제 키워드로 바로 검색
/doc-progressive-explorer docs/GAS/EXAMPLES.md "쿨다운이 적용 안됨"

# 2. 자동으로 관련 섹션과 적절한 레벨 표시
# 3. 필요시 상세 보기
level up
```

### 빠른 참조

```bash
# 바로 코드 레벨로 보기
/doc-progressive-explorer docs/PCG/EXAMPLES.md --level 3

# 특정 섹션 전체 보기
drill "Spawning Rules" --full
```

---

## 6. 출력 레벨별 예상 분량

| Level | 이름 | 예상 출력량 | 용도 |
|-------|------|-------------|------|
| 0 | Summary | ~10줄 | 빠른 개요 |
| 1 | Overview | ~50줄 | 섹션별 요약 |
| 2 | Structure | ~150줄 | 코드 시그니처 |
| 3 | Details | ~500줄 | 전체 코드+설명 |
| 4 | Complete | 전체 | 원본 문서 |

---

## 관련 문서

- [Advanced Patterns](advanced-patterns.md) - 고급 패턴 (포커스, 캐싱)
- [Multi-Document](multi-document.md) - 멀티 문서 탐색
- [API Reference](../reference/api.md) - 전체 API
- [Algorithms](../reference/algorithms.md) - 알고리즘 설명

---

**Version**: 1.9.0
**Last Updated**: 2026-01-25
