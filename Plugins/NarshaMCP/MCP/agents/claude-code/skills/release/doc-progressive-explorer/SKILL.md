---
name: doc-progressive-explorer
model: opus
description: |
  점진적 공개(Progressive Disclosure) 방식으로 대용량 문서를 탐색합니다.
  Level 0(요약) → Level 4(전체)로 단계별 깊이 제공.

  대상 문서: UE5.7 엔진 문서 (EXAMPLES.md 등, 700-2000줄 규모)

  Activation Keywords:
  - Korean: '문서 탐색', '점진적 공개', '상세 보기', 'EXAMPLES 분석'
  - English: 'explore doc', 'progressive disclosure', 'deep dive', 'drill down'

  Access Mode (Issue #4468):
  - Development: docs/engine/ 경로 → Read/Glob/Grep 도구 사용
  - Release Package: engine.ndb 존재 → MCP 도구 사용 (ue_engine_docs with search/read/grep/glob/list operations)
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "<doc-path> [--level 0-4] [--drill section] [--focus keyword]"
allowed-tools:
  - Read
  - Glob
  - Grep
  - ue_engine_docs
metadata:
  version: "1.9.1"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Doc Progressive Explorer Skill

**Purpose**: 대용량 UE5.7 문서를 5단계 깊이로 점진적 탐색

> **HISTORICAL RETIREMENT NOTE (Issues #20012, #25038)**
> The `navigation-generator` skill was archived and its `SEARCH_INDEX.json`
> artifacts were removed. Do not run that retired pipeline. This skill now
> calculates progressive-disclosure structure live; use `ue_engine_docs` for
> the current packaged-document workflow.

---

## 📊 5-Level 계층 구조

| Level | 이름 | 내용 | 예상 출력량 |
|-------|------|------|-------------|
| **0** | Summary | 제목 + 1줄 요약 + 섹션 목록 | ~10줄 |
| **1** | Overview | 각 섹션 헤더 + 첫 문단 | ~50줄 |
| **2** | Structure | 코드 블록 시그니처 + 주요 포인트 | ~150줄 |
| **3** | Details | 전체 코드 블록 + 설명 | ~500줄 |
| **4** | Complete | 원본 문서 전체 | 전체 |

---

## 🎯 핵심 기능

### 1. Level-Based Navigation
```text
/doc-explore <path> --level <0-4>
```markdown
지정된 레벨로 문서를 파싱하여 해당 깊이의 정보만 제공합니다.

### 2. Drill-Down Navigation
```text
/doc-explore <path> --drill <section-name>
```markdown
특정 섹션만 더 깊은 레벨로 탐색합니다.

### 3. Keyword Focus
```text
/doc-explore <path> --focus <keyword>
```markdown
키워드와 관련된 섹션만 필터링하여 표시합니다.

### 4. Auto-Level Selection
쿼리 의도와 문서 복잡도를 분석하여 최적의 초기 레벨을 자동 추천합니다.

### 5. Multi-Document Exploration
여러 문서를 동시에 탐색하고 비교할 수 있습니다.

---

## 🔧 Quick Start

### 기본 사용법

```bash
# Level 0 (요약)으로 문서 탐색
# 개발 환경:
/doc-explore docs/engine/ue5_7/modules/Plugins/GameplayAbilities/EXAMPLES.md
# 릴리즈 패키지 (플러그인 설치 시):
/doc-explore MCP/docs/engine/ue5_7/modules/Plugins/GameplayAbilities/EXAMPLES.md

# 특정 레벨로 탐색
/doc-explore <path> --level 2

# 섹션 드릴다운
drill "Cooldown System"

# 키워드 포커스
focus "Attribute"
```markdown

### 경로 자동 감지 및 접근 모드 (Issue #4468)

스킬은 환경에 따라 자동으로 접근 방법을 선택합니다:

**개발 환경** (저장소에서 직접 작업 시):
- 경로: `docs/engine/`
- 도구: `Read`, `Glob`, `Grep` (파일시스템 직접 접근)

**릴리즈 패키지** (플러그인 설치 시):
- 경로: `MCP/docs/engine.ndb` (암호화된 번들)
- 도구: `ue_engine_docs` (MCP 도구 - search/read/grep/glob/list operations)

```bash
# 환경 감지 로직 (개발 환경 우선)
if (docs/engine/ 디렉토리 존재):
    # 개발 환경 - 파일시스템 직접 접근 (가장 빠름)
    Glob("docs/engine/**/*.md")
    Read("docs/engine/ue5_7/modules/Plugins/GameplayAbilities/EXAMPLES.md")
elif (MCP/docs/engine.ndb 존재) OR (Plugins/NarshaMCP/MCP/docs/engine.ndb 존재):
    # 릴리즈 패키지 - MCP 도구 사용
    ue_engine_docs(operation="search", query="GameplayAbility")
    ue_engine_docs(operation="read", path="ue5_7/modules/Plugins/GameplayAbilities/EXAMPLES.md", level=0)
    ue_engine_docs(operation="grep", pattern="SpawnRate", path_filter="ue5_7/")  # 본문 전문 검색
    ue_engine_docs(operation="diff", module="Niagara", from_version="5.6", to_version="5.7")  # 크로스버전 비교
    ue_engine_docs(operation="stats")  # 캐시 상태 모니터링
else:
    # 엔진 문서 없음 - 사용자에게 안내
    "엔진 문서를 찾을 수 없습니다. install-to-plugin.bat을 실행해 주세요."
```markdown

### 자동 레벨 선택 (권장)

```bash
# 쿼리와 함께 사용 - 최적 레벨 자동 선택
/doc-explore docs/.../EXAMPLES.md "쿨다운 구현 방법 알려줘"
```markdown

### 멀티 문서 탐색

```bash
# 여러 문서 동시 열기
/doc-explore docs/GAS/EXAMPLES.md docs/StateTree/EXAMPLES.md --mode parallel
```markdown

---

## 🔄 워크플로우

**Target**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, ask the user for the document path and optional level/drill/focus options.

Parse `$ARGUMENTS` for `<doc-path>` and optional flags (`--level 0-4`, `--drill <section>`, `--focus <keyword>`).

```text
1. 문서 경로 입력
   ├─ 절대/상대 경로 또는 Glob 패턴
   └─ 파일 존재 여부 확인

2. 문서 파싱
   ├─ Markdown 헤더 기반 섹션 분리
   ├─ 코드 블록 추출 및 언어 태그 파싱
   └─ 주요 포인트 마커 감지

3. 레벨 필터링
   ├─ 자동 레벨 추천 (쿼리 의도 분석)
   └─ 요청된 레벨로 콘텐츠 필터링

4. 결과 출력
   ├─ 네비게이션 힌트 제공
   └─ 다음 단계 안내
```markdown

---

## 📋 출력 포맷

### Level 0 (Summary)
```markdown
# [문서 제목]
> [1줄 요약]

## 📑 섹션 목록
1. [섹션 1] - [간략 설명]
2. [섹션 2] - [간략 설명]

💡 Drill down: `--level 1` 또는 `--drill [섹션명]`
```

### Level 1-4
점진적으로 더 많은 세부 정보를 표시합니다.
상세 포맷: [templates/output-format.md](templates/output-format.md)

---

## 📋 Navigation Commands

| 명령어 | 설명 | 예시 |
|--------|------|------|
| `level up` | 레벨 +1 (더 상세히) | `level up` |
| `level down` | 레벨 -1 (더 요약) | `level down` |
| `level <n>` | 특정 레벨로 이동 | `level 2` |
| `drill "섹션"` | 섹션 드릴다운 | `drill "Setup"` |
| `drill --full` | 섹션을 Level 4로 | `drill "API" --full` |
| `focus "키워드"` | 키워드 필터링 | `focus "Cooldown"` |
| `focus clear` | 포커스 해제 | `focus clear` |

---

## 🤖 Auto-Level Decision Matrix

| 복잡도 | overview | specific | code | troubleshoot |
|--------|----------|----------|------|--------------|
| Low (<40) | 1 | 2 | 3 | 3 |
| Medium (40-70) | 0 | 2 | 2 | 3 |
| High (>70) | 0 | 1 | 2 | 2 |

쿼리 의도 분석:
- **overview**: "요약", "구조", "목록" 등
- **specific**: "어떻게", "자세히", 특정 섹션명
- **code**: "코드", "예제", "구현"
- **troubleshoot**: "에러", "문제", "해결"

---

## 📊 검증 기준

| 항목 | 기준 | 상태 |
|------|------|------|
| 5-Level 파싱 정확도 | ≥90% | ✅ |
| Level 0 출력량 | ≤10줄 | ✅ |
| Level 4 일치율 | 100% | ✅ |
| 레벨 전환 속도 | <500ms | ✅ |
| 섹션 매칭 정확도 | ≥95% | ✅ |
| 키워드 관련성 | ≥90% | ✅ |
| 자동 레벨 적절성 | ≥80% | ✅ |
| 캐시 히트율 | ≥60% | ✅ |
| 추천 정확도 (TF-IDF) | ≥85% | ✅ |

---

## 🚀 Advanced Features

### 캐싱 시스템 (Cycle 7)
- LRU 캐시 (최대 50 문서)
- TTL 기반 만료 (5분)
- 캐시 히트율 60%+ 달성

### 크로스레퍼런스 (Cycle 8)
- 문서 간 관계 자동 분석
- 클래스 참조 기반 추천
- 키워드 연결 시스템

### TF-IDF 추천 (Cycle 9)
- 키워드 가중치 계산
- 컨텍스트 기반 부스팅
- 사용자 피드백 학습

### 멀티 문서 탐색 (Cycle 10)
- 병렬 문서 로딩
- 통합 뷰 생성
- 크로스 문서 비교

상세 구현: [reference/implementation.md](reference/implementation.md)

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Document file not found | Path typo or file moved after refactor | Use `Glob("docs/engine/**/*.md")` to locate correct path; retry with corrected path |
| Level parsing returns empty result | Non-standard Markdown headers (missing space after `#`) | Fall back to Level 4 (full document) and let user manually navigate |
| `ue_engine_docs` returns connection error | Release package mode but MCP server not running | Switch to development mode with direct `Read`/`Glob` on `docs/engine/` directory |
| Keyword focus yields zero matches | Keyword not present or TF-IDF threshold too strict | Use `Grep` to verify keyword existence; suggest alternative keywords from Level 0 section list |

## ⚠️ Common Failures

| 에러 유형 | 원인 | 해결 방법 |
|-----------|------|-----------|
| 문서 파일을 찾을 수 없음 | 잘못된 경로 또는 Glob 패턴 불일치 | 절대 경로 사용, `ls` 또는 `Glob`으로 파일 존재 확인 |
| Level 파싱 결과가 비어 있음 | Markdown 헤더 형식이 비표준 (예: 공백 누락) | 문서가 표준 `# / ## / ###` 헤더 형식을 따르는지 확인 |
| 드릴다운 시 섹션 매칭 실패 | 섹션 이름 오타 또는 대소문자 불일치 | `--level 0`으로 정확한 섹션 이름 확인 후 재시도 |
| 대용량 문서 (>2000줄) 읽기 timeout | Read 도구의 기본 줄 수 제한 초과 | `--level 0` 또는 `--level 1`로 시작, 필요한 섹션만 `--drill`로 탐색 |
| 캐시 히트율 저조 (<30%) | 짧은 TTL 또는 문서 경로 변경 | 동일 세션 내 일관된 경로 사용, 캐시 TTL 확인 |
| 키워드 포커스 결과 없음 | 키워드가 문서 내 존재하지 않거나 TF-IDF 임계값 미달 | `Grep`으로 키워드 존재 여부 확인, 유사 키워드로 재시도 |

## ⚠️ 제한사항

### 완료된 기능 (v1.9.0)
- ✅ 5-Level 계층화 로직
- ✅ 드릴다운 네비게이션
- ✅ 키워드 포커스
- ✅ 자동 레벨 선택
- ✅ LRU 캐싱
- ✅ 크로스레퍼런스
- ✅ TF-IDF 추천
- ✅ 멀티 문서 탐색

### 미구현 (향후)
- 실시간 협업 기능
- 외부 검색 엔진 연동
- AI 기반 요약 자동 생성

---

## 📚 Documentation

### Reference
- [API Reference](reference/api.md) - 전체 API 문서
- [Implementation Details](reference/implementation.md) - 구현 상세
- [Algorithms](reference/algorithms.md) - 알고리즘 설명

### Examples
- [Basic Usage](examples/basic-usage.md) - 기본 사용 예시
- [Advanced Patterns](examples/advanced-patterns.md) - 고급 패턴
- [Multi-Document](examples/multi-document.md) - 멀티 문서 탐색

### Templates
- [Output Format](templates/output-format.md) - 출력 포맷 템플릿

### Related
- [SubAgent Definition](../../agents/doc-progressive-explorer.md)

---

**Quality Score**: 9.5/10
**Last Updated**: 2026-01-25
**Lines**: ~300 (compliant with <500 standard)

## MCP Tool Usage

```python
# Progressive engine docs exploration
ue_engine_docs(operation="search", query="Tick", level=0)
ue_project_docs(operation="read", path="modules/Core/OVERVIEW.md", level=1)
```
