---
name: api-diff-analyzer
model: opus
description: "Purpose: UE API change analyzer for version migration. Generates breaking changes and migration guides. Triggers on 'API 변경점', 'migration guide', 'breaking changes'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools: [Bash, Read, Grep, Glob]
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# api-diff-analyzer

UE 버전 간 API 변경점 분석 전문가.

## Description

5.6 → 5.7 Breaking Changes, Deprecations를 추적하고 마이그레이션 가이드를 생성합니다.

## Triggers

- "API 변경점"
- "마이그레이션 가이드"
- "5.6 vs 5.7"
- "Breaking changes"
- "Deprecated API"
- "버전 비교"

## Inputs

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| module_name | string | Yes | 분석할 모듈 이름 (예: "Core", "Engine") |
| old_version | string | Yes | 이전 버전 (예: "5.6") |
| new_version | string | Yes | 새 버전 (예: "5.7") |
| old_engine_path | string | No | 이전 버전 엔진 경로 (환경변수 fallback) |
| new_engine_path | string | No | 새 버전 엔진 경로 (환경변수 fallback) |

## Outputs

| Output | Maturity | Description |
|--------|----------|-------------|
| API_CHANGES.md | beta | 버전 간 API 변경 문서 |
| migration_guide.md | beta | 마이그레이션 가이드 |
| breaking_changes.json | beta | Breaking changes 데이터 |

## Scripts

### diff_versions.py

두 버전 간 헤더 파일 diff를 수행합니다.

```bash
python "${CLAUDE_SKILL_DIR}/scripts/diff_versions.py" --module Core --old-path "C:/UE_5.6" --new-path "I:/UE_5.7"
```

**기능**:
- 헤더 파일 추가/삭제/수정 감지
- 함수 시그니처 변경 추적
- 클래스/구조체 변경 감지
- 라인 단위 diff 생성

### find_breaking.py

Breaking changes와 Deprecated API를 탐지합니다.

```bash
python "${CLAUDE_SKILL_DIR}/scripts/find_breaking.py" --module Core --old-path "C:/UE_5.6" --new-path "I:/UE_5.7"
```

**탐지 패턴**:
- `UE_DEPRECATED` 매크로
- `DEPRECATED_FORGAME` 매크로
- 함수 시그니처 변경 (파라미터 타입/순서)
- 클래스 상속 변경
- 삭제된 public API

## Workflow

```text
1. 환경변수에서 엔진 경로 로드
   - UECODEGEN_ENGINE_5_6_DIR
   - UECODEGEN_ENGINE_5_7_DIR

2. 모듈 경로 확인
   - Runtime: Engine/Source/Runtime/{module}
   - Editor: Engine/Source/Editor/{module}
   - Plugin: Engine/Plugins/{path}

3. diff_versions.py 실행
   - 헤더 파일 목록 비교
   - 변경된 파일 diff 생성
   - 변경 통계 수집

4. find_breaking.py 실행
   - DEPRECATED 매크로 탐지
   - 시그니처 변경 분석
   - Breaking changes 분류

5. API_CHANGES.md 생성
   - API_CHANGES_TEMPLATE.md 사용
   - 변경점 요약
   - 마이그레이션 가이드
```

## Output Example

```markdown
---
module: Core
old_version: "5.6"
new_version: "5.7"
breaking_changes: 3
deprecated_apis: 12
new_apis: 45
---

# Core - API Changes (5.6 → 5.7)

## Breaking Changes

### FName::ToString 시그니처 변경
- **이전**: `FString ToString() const`
- **현재**: `void ToString(FString& Out) const`
- **마이그레이션**: Out 파라미터 방식으로 변경 필요

## Deprecated APIs

### FPaths::GameDir
- **상태**: DEPRECATED (5.7에서 제거 예정)
- **대체**: `FPaths::ProjectDir()`
```

## ⚠️ Common Failures

| 에러 유형 | 원인 | 해결 방법 |
|-----------|------|-----------|
| 엔진 경로를 찾을 수 없음 | `UECODEGEN_ENGINE_5_6_DIR` / `5_7_DIR` 환경변수 미설정 | `--old-path`, `--new-path` 인자로 명시적 경로 제공 |
| `${CLAUDE_SKILL_DIR}/scripts/diff_versions.py` 실행 시 빈 결과 | 모듈 경로 불일치 (Runtime/Editor/Plugin 구분 오류) | 모듈이 `Engine/Source/Runtime/`, `Editor/`, `Plugins/` 중 어디에 있는지 확인 |
| 대규모 모듈 diff timeout (>5분) | 수천 개의 헤더 파일 비교 (예: Engine 모듈) | 특정 서브디렉토리로 범위 축소, `--include` 필터 적용 |
| `UE_DEPRECATED` 매크로 미탐지 | 새로운 deprecation 매크로 패턴 미지원 | `${CLAUDE_SKILL_DIR}/scripts/find_breaking.py`에 새 패턴 추가, `DEPRECATED_FORGAME` 등 확인 |
| `API_CHANGES.md` 생성 실패 | 출력 디렉토리 쓰기 권한 없음 | 출력 경로 권한 확인, `docs/engine/` 디렉토리 존재 여부 검증 |
| 함수 시그니처 변경 false positive | 주석이나 문자열 내 함수명 매칭 | diff 결과를 수동 검토, 코드 블록만 필터링하도록 패턴 개선 |

## Dependencies

- module-analyzer (모듈 구조 정보)
- Python difflib (텍스트 diff)

## Output Format

```
API Diff Report: <module> (<old_version> → <new_version>)
──────────────────────────────────────────────────────────
Breaking Changes: <count>
Deprecated APIs: <count>
New APIs: <count>

[Breaking Changes]
  1. <function/class name> - <change description>
     Before: <old signature>
     After:  <new signature>
     Migration: <action required>

[Deprecated APIs]
  1. <API name> - Replacement: <new API>

[New APIs]
  1. <API name> - <description>

Output Files:
  - docs/engine/API_CHANGES_<module>.md
  - breaking_changes.json
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Engine path not found | `UECODEGEN_ENGINE_5_X_DIR` env var not set | Provide explicit `--old-path` / `--new-path` arguments |
| Empty diff results | Module path mismatch (Runtime/Editor/Plugin confusion) | Verify module location under `Engine/Source/Runtime/`, `Editor/`, or `Plugins/` |
| Diff timeout (>5 min) | Large module with thousands of headers | Narrow scope with `--include` filter or target specific subdirectory |
| `UE_DEPRECATED` macro not detected | New deprecation macro pattern not supported | Manually add new pattern to `${CLAUDE_SKILL_DIR}/scripts/find_breaking.py` detection rules |

## Related Issues

- Issue #2836 (Engine Documentation System)
## MCP Tool Examples

```python
# Search for API changes between versions
ue_grep(query="DEPRECATED", domain="source")
# Read API header for comparison
ue_read(identifier="UGameplayAbility")
# Analyze symbol changes
ue_analyze_symbols(operation="smart", query="UGameplayAbility")
# Module-scoped PDB-vs-PDB symbol-surface diff between two engine installs.
# Diffs only the PDB-stem intersection; one-sided stems -> missing_sources
# (never fake mass deletions). method="complete"|"partial", changes[] typed,
# summary keeps full pre-cap counts. Symbol surface, not owned-API (#5670).
ue_analyze_symbols(
    operation="symbol_diff",
    module="Bridge",
    old_engine_path="C:/Program Files/Epic Games/UE_5.7",
    new_engine_path="C:/Program Files/Epic Games/UE_5.8",
    limit=100,
)
```

Treat `method:"partial"`, non-empty `missing_sources`, `truncated:true`, or
`completeness.records_exact:false` as partial evidence. Preserve reported API
changes, but never turn an empty partial diff into “no API change”; disclose the
missing side/records and retry with complete PDB inputs.
