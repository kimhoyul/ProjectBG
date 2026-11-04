# /ue-architecture-lint — Reference Material

> Companion to [SKILL.md](SKILL.md). Reference content (full threshold config schema,
> ARCH-001~010 rule definitions, output templates, error recovery, standalone CLI usage,
> CI output-format routing) lives here. The hot-path SKILL.md keeps the imperative
> 5-Phase scan workflow.

---

## Auto-Trigger Phrases

### Korean
- "아키텍처 린트", "구조 검사", "아키텍처 검증"
- "코드 구조 분석", "설계 검사", "아키텍처 위반"
- "ARCH 규칙 검사", "구조 린트"

### English
- "architecture lint", "arch lint", "architecture check"
- "structure check", "design check", "architecture violations"
- "check ARCH rules", "structural analysis"

### Disambiguation
- `/ue-architecture-lint` = **Structural design rules** (ARCH-001~010: inheritance, components, module deps, replication)
- `/ue-perf-lint` = **Performance anti-patterns** (PERF-001~023: Tick allocations, replication perf)
- `/ue-pre-commit` = **Pre-commit impact analysis** ("what breaks if I commit this?")
- `/ue-audit` = **Project-wide health check** (server health, config, assets, code quality, message log)

---

## Threshold Configuration

### Project Config File: `.ue-arch-lint.json`

프로젝트 루트에 생성하면 기본값을 오버라이드합니다 (default 표는 SKILL.md Phase 0 참조):

```json
{
  "version": "1.0",
  "thresholds": {
    "ARCH-001": { "max_inheritance_depth": 4 },
    "ARCH-002": { "max_methods": 50, "max_lines": 2000 },
    "ARCH-006": { "max_public_deps": 24 }
  },
  "severity_overrides": {
    "ARCH-003": "info"
  },
  "disabled_rules": [],
  "exclude_paths": [
    "Source/ThirdParty/**"
  ],
  "exclude_classes": [
    "ALyraTaggedActor",
    "AModularCharacter",
    "AModularPawn"
  ]
}
```

**필드 설명:**

| Field | Type | Description |
|-------|------|-------------|
| `thresholds` | object | 규칙별 임계값 오버라이드 (미지정 시 기본값 사용) |
| `severity_overrides` | object | 규칙별 심각도 변경 (`error` / `warning` / `info`) |
| `disabled_rules` | array | 비활성화할 규칙 ID 목록 |
| `exclude_paths` | array | 스캔에서 제외할 경로 패턴 (glob) |
| `exclude_classes` | array | 스캔에서 제외할 클래스명 목록 |

**Config 미존재 시**: 모든 기본값이 적용됩니다. 파일이 없어도 정상 동작합니다.

### CLI Override

`--threshold` 인자로 일회성 오버라이드:

```bash
/ue-architecture-lint --scope all --threshold ARCH-006:max_public_deps=20
/ue-architecture-lint --scope staged --disable ARCH-003,ARCH-010
```

---

## Rule Reference (10 rules)

### Inheritance & Structure (3 rules)

| Rule | Pattern | Severity | Detection |
|------|---------|----------|-----------|
| **ARCH-001** | Inheritance depth > `max_inheritance_depth` (default: 3) | Warning | `ue_analyze_symbols(trace_hierarchy)` — count depth |
| **ARCH-002** | God Class: `max_methods`+ methods (default: 30) OR `max_lines`+ lines (default: 1500) | Warning | `ue_analyze_symbols(get_methods)` + file line count |
| **ARCH-003** | Actor subclass with < `min_components` components (default: 1) | Warning | `ue_grep("CreateDefaultSubobject")` — 0 hits in Actor .cpp |

### Module & Dependencies (3 rules)

| Rule | Pattern | Severity | Detection |
|------|---------|----------|-----------|
| **ARCH-004** | Circular module dependency | Error | Build.cs `PublicDependencyModuleNames` 파싱 → A↔B 양방향 참조 감지 |
| **ARCH-005** | Runtime module includes Editor-only headers | Error | `ue_grep` Build.cs type + include pattern analysis |
| **ARCH-006** | > `max_public_deps` PublicDependencyModuleNames (default: 10) | Warning | `ue_read` Build.cs — count dependencies |

### UE Patterns (4 rules)

| Rule | Pattern | Severity | Detection |
|------|---------|----------|-----------|
| **ARCH-007** | UPROPERTY(Replicated) without DOREPLIFETIME registration | Error | `ue_diff(replication_audit)` + `ue_analyze_source(cross_check)` |
| **ARCH-008** | Direct Health/Damage manipulation in GAS project (bypassing GameplayEffects) | Warning | `ue_grep` with configurable `health_patterns` + GAS module presence check |
| **ARCH-009** | Tick abuse (heavy operations in Tick) | Warning | `ue_grep("::Tick(")` + Tick 본문 내 NewObject/SpawnActor/LoadObject/GetAllActorsOfClass 패턴 매칭 |
| **ARCH-010** | Member variables/functions without UPROPERTY/UFUNCTION that should be BP-exposed | Info | `ue_analyze_source(extract_pattern)` + `ue_grep` for raw declarations |
| **ARCH-011** | Unguarded `FSlateApplication::Get()` calls (cook commandlet crash risk per Issue #10756 RCA / PR #10793) | **Error** (graduated from Warning in #10984; v2 brace-depth scan + 0 production violations enabled the promotion) | Line-based regex + brace-depth-aware enclosing-if guard-stack (v2, Issue #10892) + 10-line lookback fallback for v1-compat boundary cases + function-scope early-return tracking (`if (IsRunningCommandlet()) return;` or `if (!FSlateApplication::IsInitialized()) return;`) |

#### ARCH-011 detail (Issue #10856)

**What it catches**: Direct `FSlateApplication::Get().XXX()` calls that lack any of the following guards:
- Same-line `IsInitialized()` test: `if (FSlateApplication::IsInitialized()) FSlateApplication::Get()...`
- Enclosing `if (FSlateApplication::IsInitialized()) { ... }` block of arbitrary length (v2 brace-depth-aware reverse-scan, Issue #10892). Compound conditions like `if (Cond && FSlateApplication::IsInitialized())` also accepted. Negated form `if (!FSlateApplication::IsInitialized())` is NOT pushed to the guard-stack — its body is the unsafe branch — but the v1-compat 10-line lookback still suppresses violations at common patterns like `if (!Init) {} else { Get(); }` and `if (!Init) { return; }` (multi-line). See "v2 changelog" below.
- Function-scope single-line early-return guard: `if (IsRunningCommandlet()) return;` or `if (!FSlateApplication::IsInitialized()) return;` (must precede the call within the same function body)
- Per-line `// NOLINT` suppression comment

**Severity graduation** (Issue #10984): default severity promoted from `Severity.WARNING` to `Severity.ERROR` after a production scan resolved all 16 pre-existing violations across 3 files (test exclusion via `Tests/**` exclude_paths in `.ue-arch-lint.json` for 4 sites + `if (IsRunningCommandlet()) return;` function-scope guards for 11 + 1 sites). Operators can still override per-config via `severity_overrides`.

**v2 changelog** (Issue #10892, PR #10934):
- **Long function body false-positive class — RESOLVED.** Brace-depth-aware
  guard-stack replaces the v1 fixed-window `LOOKBACK_LINES = 10` for the
  primary detection path. The stack walks back to function start (cleared on
  function boundary) — guards 49–94+ lines from the call are now correctly
  recognized as enclosing.
- **PR #10868 audit re-run on CUWebSocketEditorSubsystem.cpp**: 16 → **11**
  flags. Decomposition: v1 had 11 TPs + 4 long-distance FPs (lines 2244,
  2304, 4833, 4841) + 1 incidental FP at line 7015 (else-of-negated-if,
  resolved by Stage 2's lookback fallback). v2 result: 11 TPs preserved,
  4 long-distance FPs resolved by Stage 1, 1 incidental FP suppressed by
  Stage 2 — net 16 = 11 + 4 + 1 → 11 + 0 + 0 = 11. FPR: 25% → 0% on the
  audit fixture set.
- **New regression fixture**: `S63_FSlateGuardLongFunc.cpp` covers the
  50-line distance case (1 call site, 50+ inert padding lines between the
  enclosing `if (Init)` and `Get()`). Asserts 0 violations under v2; would
  yield 1 under v1.
- **2-stage detection** retained for backward compatibility: Stage 1 is the
  brace-stack; Stage 2 is a short 10-line lookback for `is_init_guard_re`,
  consulted only when Stage 1 says "no enclosing guard". Stage 2 catches
  edge classes the brace-stack legitimately can't (negated-then-else,
  multi-line `if (!Init) { return; }`) so v2 introduces zero new
  false-positives on the audit fixture set while still resolving the four
  long-distance v1 FPs via Stage 1.

**Remaining limitations** (out of scope for v2, deferred to v3):
- Multi-line early-return (`if (...) {\n  return;\n}`) is NOT recognized — use the single-line form or `// NOLINT`. Tracked by `pytest.mark.xfail` test [`test_v1_limitation_multiline_early_return_block`](../../../scripts/quality/tests/test_arch_011.py).
- `ShouldCreateSubsystem(IsRunningCommandlet → false)` cross-function analysis NOT done — calls in subsystem methods may be cook-safe via instance-not-created path; flag-and-NOLINT for v2. Tracked by `pytest.mark.xfail` test [`test_v1_limitation_shouldcreatesubsystem_guard`](../../../scripts/quality/tests/test_arch_011.py).
- Editor-only path auto-whitelist (e.g., `Source/.../Slate/` widget internals) NOT done — flag-and-NOLINT.
- Multi-line `if (\n  IsInitialized()\n) {` form where the opening `{` is more than 10 lines below the bare `if` line — neither Stage 1 (which requires `{` on the next meaningful line) nor Stage 2 (10-line lookback) catches it. Rare in practice; no fixture added.

**NOLINT suppression** (Phase 3 #2 tightening): the per-line suppression regex
`r"//\s*NOLINT(?:\s*\(\s*ARCH-011\s*\)|(?!\s*\())"` accepts `// NOLINT` standalone or
`// NOLINT(ARCH-011)` exactly, but rejects `// NOLINT(ARCH-009)` and incidental
substring mentions like the literal string `"NOLINT"` inside log text. Other ARCH
rules in `ue_arch_lint.py` still use the looser `if "NOLINT" in line:` substring
check — tightening them is out of scope for this PR (would need a per-rule audit).

**Function-boundary detection**: `^[A-Za-z_][\w\s\*&<>,]*\s+\w+::\w+\s*\(` regex catches `RetType Class::Method(` openers AND brace-depth tracking (mirrors `check_arch_009` Tick-body tracking) clears the function-scope `commandlet_guarded_function` flag when `{...}` depth returns to 0. The combined approach handles templated/multi-line signatures (the regex alone would miss `void Class<T>::Method()` and similar) — guard flags do NOT leak across function boundaries.

**Guard detection (v2, Issue #10892)** — two-stage:
- **Stage 1: brace-depth-aware guard-stack.** A `guard_open_depths: list[int]` tracks the brace-depths at which positive `if (FSlateApplication::IsInitialized()) {` blocks opened. Push on the `{` line (same-line `if (...) {` or multi-line `if (...)\n{`); pop when `brace_depth` drops below the recorded target. Cleared at function boundaries (`func_start_re` match or `brace_depth` returning to 0 after entering a body) so guards never leak across functions. The negated form `if (!IsInitialized())` is excluded from pushes — its body is the unsafe branch.
- **Stage 2: 10-line lookback fallback.** Consulted only when Stage 1 says "no enclosing guard". Scans the prior 10 lines for `is_init_guard_re` — preserves v1 boundary behavior at `if (!Init) { ... } else { Get(); }` (else body is incidentally safe) and multi-line `if (!Init) { return; } Get();` patterns. The `LOOKBACK_LINES = 10` constant was chosen to accommodate guard blocks padded by explanatory comments (v1 N=5 produced false-positives on real plugin code with multi-line `// Issue #...` rationale comments inside the guard block).

**Reference**: PR #10793 commit `c20729303`, `docs/development/RELEASE_REGRESSION_GUARDS.md` § "Direct `FSlateApplication::Get()` calls".

### Suppression

- `// NOLINT(ARCH-XXX)` on the same line suppresses a specific rule
- `// NOLINT` suppresses all ARCH rules for that line

---

## MCP Tools Used

| Tool | Phase | Purpose |
|------|-------|---------|
| `ue_analyze_symbols` | 2A | Hierarchy depth (ARCH-001), method count (ARCH-002), change detection |
| `ue_analyze_source` | 2B, 2C | Module dependencies (ARCH-004), replication cross-check (ARCH-007), UPROPERTY extraction (ARCH-010) |
| `ue_diff` | 2C | Replication audit (ARCH-007) |
| `ue_grep` | 1, 2A-C | File discovery, component check (ARCH-003), Build.cs analysis (ARCH-005/006), GAS/Health patterns (ARCH-008) |
| `ue_read` | 2B | Build.cs detailed reading (ARCH-005/006), file line counting (ARCH-002) |
| `/ue-perf-lint` skill | 2C | Tick abuse detection reuse (ARCH-009) |

---

## MCP Tool Examples

```python
# Inheritance depth check (ARCH-001)
ue_analyze_symbols(operation="trace_hierarchy", class_name="AMyCharacter", direction="up")

# Replication audit (ARCH-007)
ue_diff(operation="replication_audit", class_name="AMyCharacter")

# Module dependency map (ARCH-004)
ue_analyze_source(operation="map_dependencies")

# GAS bypass detection (ARCH-008)
ue_grep(params={"query": "Health -= ", "domain": "source"})
```

---

## Limitations

1. **Static analysis only** — cannot detect runtime-only architecture violations
2. **ARCH-003 heuristic** — checks CreateDefaultSubobject calls; manual component creation patterns may be missed
3. **ARCH-008 GAS bypass** — pattern-based; custom health wrapper functions may not be detected
4. **ARCH-010 BP exposure** — cannot determine intent; flags as Info severity only
5. **Module-level rules** (ARCH-004/005/006) require Build.cs files to exist in project

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_analyze_symbols` returns empty | PDB not indexed | Skip ARCH-001/002, use `ue_grep` fallback |
| `ue_analyze_source(map_dependencies)` fails | No Build.cs in scope | Skip ARCH-004, report "module analysis unavailable" |
| `ue_diff(replication_audit)` returns empty | No replicated properties | Skip ARCH-007, report "no replication found" |
| GAS module not found | Non-GAS project | Skip ARCH-008 entirely |
| `/ue-perf-lint` unavailable | Skill not loaded | Fallback: `ue_grep("::Tick(")` + manual pattern check for ARCH-009 |
| Too many classes (>50) | Large project | Limit to --scope staged or top 50 by file size |

---

## Output Format

### Markdown Report (default)

```markdown
## Architecture Lint Report

**Scanned**: N classes (M files) | **Scope**: staged/all/module:X
**Violations**: V total | **Errors**: E | **Warnings**: W | **Info**: I

### Errors (E items)

| # | File:Line | Rule | Description | Suggestion |
|---|-----------|------|-------------|------------|
| 1 | AMyChar.h:15 | ARCH-007 | UPROPERTY(Replicated) Health without DOREPLIFETIME | Add DOREPLIFETIME(AMyChar, Health) to GetLifetimeReplicatedProps |

### Warnings (W items)

| # | File:Line | Rule | Description | Suggestion |
|---|-----------|------|-------------|------------|
| 1 | AMyChar.h:1 | ARCH-002 | God Class: 45 methods | Extract to components (Movement, Combat, Inventory) |

### Info (I items)

| # | File:Line | Rule | Description | Suggestion |
|---|-----------|------|-------------|------------|
| 1 | AMyChar.h:30 | ARCH-010 | float Speed not UPROPERTY | Add UPROPERTY(EditAnywhere) if BP access needed |

### Suppressed (S items)
- AMyChar.cpp:89: ARCH-008 direct Health (NOLINT)

### Summary
- Architecture Score: X/10 (10 = no violations)
- Top recommendation: [most impactful fix]
```

### Compact Console Output

```text
================================================================
 Architecture Lint Report
================================================================
 Scanned:    N classes (M files)
 Scope:      staged / all / module:Combat
 Violations: V total (E error, W warning, I info)

 [ERROR]   AMyChar.h:15      ARCH-007  Replicated without DOREPLIFETIME
 [ERROR]   Combat.Build.cs:1 ARCH-004  Circular dep: Combat ↔ AI
 [WARNING] AMyChar.h:1       ARCH-002  God Class: 45 methods
 [WARNING] AEnemy.h:1        ARCH-001  Inheritance depth: 4
 [INFO]    AMyChar.h:30      ARCH-010  float Speed not UPROPERTY

 Suppressed: S items (NOLINT)
 Score: 6/10
 Top fix: Split AMyChar into components (ARCH-002)
================================================================
```

---

## Standalone Script (Claude 없이 실행)

MCP 서버나 Claude 없이 Python 스크립트로 직접 실행:

```bash
# 1. 초기 설정 — 프로젝트 분석 후 .ue-arch-lint.json 자동 생성
python scripts/quality/ue_arch_lint.py --init --project "C:/MyProject"

# 2. 전체 스캔
python scripts/quality/ue_arch_lint.py --project "C:/MyProject"

# 3. 커밋 전 변경분만
python scripts/quality/ue_arch_lint.py --scope staged --project "C:/MyProject"

# 4. 특정 모듈만
python scripts/quality/ue_arch_lint.py --scope module:Combat --project "C:/MyProject"

# 5. CI용 JSON 출력
python scripts/quality/ue_arch_lint.py --json --project "C:/MyProject"

# 6. 특정 룰 비활성화
python scripts/quality/ue_arch_lint.py --disable ARCH-003,ARCH-010
```

**`--init` 동작**: Build.cs 분석 후 현재 최대 의존성 수 + 여유분 2를 `ARCH-006` 기본값으로 설정. GAS 미사용 프로젝트면 `ARCH-008` 자동 비활성화.

**Exit code**: 0 = 에러 없음 (경고 허용), 1 = 에러 발견, 2 = 스크립트 오류

**Script path**: `scripts/quality/ue_arch_lint.py`

> **Release Package Note**: 현재 릴리즈 패키지(`package_release.py`)에는 이 스크립트가 미포함.
> `copy_scripts()`의 `skill_scripts` 리스트에 등록 필요 — 별도 이슈로 추적 예정.

---

## Related Skills

- [`/ue-perf-lint`](../ue-perf-lint/SKILL.md) — Performance anti-patterns (PERF-001~023). ARCH-009 reuses its results.
- [`/ue-pre-commit`](../ue-pre-commit/SKILL.md) — Pre-commit safety analysis (complementary: run architecture-lint before pre-commit)
- [`/ue-audit`](../ue-audit/SKILL.md) — Project-wide health audit (broader scope)
- [`/ue-diff`](../ue-diff/SKILL.md) — Cross-check and mismatch detection (ARCH-007 reuses replication_audit)

---

## Output Format Routing (Issue #8413 Phase 4)

> **Additive feature** — when `$ARGUMENTS` does NOT contain `--output-format`
> AND `CI` env var is not set to `true`, behavior is **identical** to the
> pre-Phase-4 Markdown report above (AC #8). Integration only triggers
> when CI/automation explicitly opts in OR the `CI=true` convention is detected.

### When to activate

Parse `$ARGUMENTS` for both flags AND inspect environment:
- `--output-format json|sarif|junit` (explicit opt-in; case-sensitive value)
- `--output <path>` (optional — defaults to stdout if absent)
- **CI auto-detect**: if `$CI` env equals `true` (set by GitHub Actions / GitLab CI / CircleCI / Travis), default to `--output-format json` even when `--output-format` is absent. Explicit `--output-format` always wins.

If neither `--output-format` nor `CI=true` is present, ignore this entire
section and produce the Markdown report defined above.

### When activated (CI / automation mode)

Skip the human-readable Markdown report. Instead:

1. **Collect findings during the ARCH scan** as an in-memory list of
   `AnalysisResult` objects, one per rule hit:
   ```json
   {
     "tool": "ue-architecture-lint",
     "rule_id": "ARCH-002",
     "severity": "warning",
     "file_path": "Source/MyGame/MyManager.cpp",
     "line": 1,
     "column": null,
     "message": "God Class detected (1024 lines, 47 methods)",
     "suggestion": "Split into focused subsystems (UManagerCoordinator + per-domain managers)"
   }
   ```
   `severity` MUST be one of `error` / `warning` / `info` (lowercase). Map
   the rule's table severity (Critical/Warning/Info) → `error`/`warning`/`info`.
   `column` and `suggestion` are optional (omit or `null`).

2. **Write to temp file** as a bare JSON array (CLI accepts envelope or array):
   ```bash
   TMPFILE="${TMPDIR:-/tmp}/architecture-lint-findings-$$.json"
   ```

3. **Shell out to `narshamcp analyze`** for format conversion:
   ```bash
   narshamcp analyze \
     --source ue-architecture-lint \
     --input "$TMPFILE" \
     --format <fmt-from-args-or-json-when-CI=true> \
     --output <path-from-args-or-omit-for-stdout>
   ```

4. **Report only the file path** (or stdout indicator) — no Markdown summary.

### Argument forms supported

- `--output-format json` (space-separated) ✅
- `--output-format sarif` (space-separated) ✅
- `--output-format junit` (space-separated) ✅
- `--output /tmp/out.sarif` (space-separated) ✅
- `--output-format=json` (equals form) ❌ — emit error: `Use --output-format json (space-separated, not equals)`

### Examples

```bash
# Local dev: Markdown (no env, no flag)
/ue-architecture-lint

# Local dev: explicit SARIF for GitHub Code Scanning upload
/ue-architecture-lint --output-format sarif --output /tmp/arch.sarif

# CI runner: JSON auto-default (CI=true detected)
CI=true /ue-architecture-lint
```

### Backward compatibility (AC #8)

The ARCH scan workflow above is **unchanged**. The only addition is a
post-scan branch that, when activated, serializes findings as JSON and
shells to the CLI instead of formatting Markdown. Trigger phrases,
argument parsing, and Markdown report generation remain byte-identical
to the pre-Phase-4 baseline.
