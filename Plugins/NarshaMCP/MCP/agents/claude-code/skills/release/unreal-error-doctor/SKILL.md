---
name: unreal-error-doctor
description: "Purpose: UE compilation error resolution. 4-step medical workflow (Diagnosis → Analysis → Preview → Apply) with multi-tool escalation for complex errors (symbol + config analysis). Handles 95%+ common errors AND complex cross-domain issues. Triggers: '빌드 에러', 'compile error', 'linker error', 'symbol not found', 'config mismatch', 'UPROPERTY error'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
argument-hint: "error message or 'Path A'/'Path B' | --self-test"
metadata:
  version: "3.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Unreal Error Doctor

**Priority**: Highest
**Goal**: Fix UE build errors in ≤5 tool calls (matching No-MCP optimal path)
**Issue**: #7112

---

## Purpose

Diagnoses and fixes Unreal Engine compilation errors using **two optimized paths**:

- **Path A (Read-first)**: Compile/build errors → Read(log) → Read(source) → Edit = **3-4 calls**
- **Path B (MCP)**: Asset/Blueprint errors → ue_fix_errors(smart) → Read → Edit = **3-5 calls**

---

## Trigger Phrases

### Korean

- "컴파일 에러", "빌드 에러", "빌드 실패"
- "링커 에러", "헤더 에러"
- "UPROPERTY 에러", "UCLASS 에러"
- "세미콜론", "generated.h"
- "에러 해결해줘", "빌드 고쳐줘"

### English

- "compile error", "build error", "build failed"
- "linker error", "header error"
- "UPROPERTY error", "UCLASS error"
- "semicolon", "generated.h"
- "fix build errors", "resolve errors"

---

## Anti-Pattern Rules (MANDATORY — READ FIRST)

> **CRITICAL**: These rules exist because Claude's default behavior wastes 2-4x tool calls.
> Violating ANY rule is a skill execution error. Real sessions show 6-16 calls when 3-4 suffice.

| # | Rule | Why |
|---|------|-----|
| 1 | **Max 1 `ue_fix_errors` call** — use `smart` mode only. Do NOT sequentially try crash_analysis, runtime_analysis, get_compiler_results, broken_asset_refs. | Each mode returns partial data; smart returns all. Sequential trying wastes 3-4 calls. |
| 2 | **No `ue_check_health` calls** — server health is already confirmed when skill loads. | Wastes 1 call, provides zero diagnostic value (Issue #7110). |
| 3 | **No `ToolSearch` calls** — this skill is already loaded, all tool schemas are available. | ToolSearch is for discovering unknown tools. Wastes 1 call. |
| 4 | **Read-first for text errors** — compile/build errors exist as text. Use Read(log) + Read(source) + Edit. Do NOT call ue_fix_errors. | Build errors are text. MCP adds indirection without value. |
| 5 | **No `Glob` for log discovery** — build log paths are known constants (see Step 0). Read directly. | Glob wastes 1 call to find a file at a known path. |
| 6 | **Smart failure = 1 fallback max** — if `smart` returns nothing, try ONE explicit mode (e.g., `scan_build_log`). Then stop. | Prevents the 4-mode sequential pattern seen in real sessions. |

**Call Budget**:
- Path A (compile/build): **≤ 4 calls** (Read + Read + Edit + optional Bash)
- Path B (asset/BP): **≤ 5 calls** (ue_fix_errors + Read + Edit + optional Build + optional Verify)
- **If you exceed 6 calls, STOP and reassess your approach.**

---

## Workflow Overview

### Step 0: Error Type Classification & Path Selection

> **CRITICAL**: Determine error type FIRST, then follow the correct path.
> This is the single most important decision — it determines 3-call vs 5-call execution.

**Classification Logic** (check in order):

| Signal | Error Type | Path |
|--------|-----------|------|
| User mentions: compile error, build error, linker error, missing semicolon, undeclared identifier, C2xxx/C3xxx/LNKxxxx | **Compile/Build (text)** | **Path A** |
| User pastes error message with `file:line` reference | **Compile/Build (text)** | **Path A** |
| User mentions: Blueprint error, asset error, BP crash, widget error | **Asset/Blueprint (binary)** | **Path B** |
| User mentions: runtime crash, assertion, ensure | **Runtime (mixed)** | **Path B** |
| Ambiguous | Default to **Path A** | Try Read-first, fall back to Path B if no build log found |

---

### Path A: Read-First (Compile/Build Errors) — Target: 3-4 calls

> **MANDATORY**: Path A uses ONLY built-in tools: `Read`, `Edit`, `Bash`.
> **Do NOT call ANY MCP tool** — no `ue_fix_errors`, no `ue_analyze_symbols`, no `ue_grep`, no `ue_check_health`.
> Compile errors are plain text. MCP adds indirection and extra calls with zero benefit.

```text
ALLOWED tools in Path A: Read, Edit, Bash, Grep, Glob
FORBIDDEN in Path A: ue_fix_errors, ue_analyze_symbols, ue_grep, ue_read, ue_check_health, ToolSearch
```

**Workflow** (follow this EXACT sequence — YOUR VERY FIRST TOOL CALL must be one of these):

**YOUR FIRST CALL** — Run this exact Bash command to get the build log:
```bash
Bash("cat \"$LOCALAPPDATA/UnrealBuildTool/Log.txt\" 2>/dev/null | tail -80")
```
This is a SINGLE Bash call. Do NOT use Glob, find, ls, or Read first. Do NOT search for log files. The UBT log is ALWAYS at this exact path on Windows.

**After reading the log output**:
- If it contains `error C` or `error LNK` with `Result: Failed` → parse `file(line)` references → Read(source) → Edit = **3 calls**
- If it shows `Result: Succeeded` or errors from a different project → the log is stale. Run the build:
  ```bash
  Bash("PROJ=$(ls *.uproject | head -1) && PROJNAME=${PROJ%.uproject} && ENGINE_VER=$(grep -oP '\"EngineAssociation\":\\s*\"\\K[^\"]+' \"$PROJ\") && \"C:/Program Files/Epic Games/UE_${ENGINE_VER}/Engine/Build/BatchFiles/Build.bat\" ${PROJNAME}Editor Win64 Development -Project=\"$(pwd)/$PROJ\" 2>&1 | tail -50")
  ```
  Parse errors from output → Read(source) → Edit = **4 calls**

```text
Summary:
1. Bash(cat UBT log)             ← ALWAYS first. Never Glob/find.
2. Read(source file from error)  ← Parse file:line from log
3. Edit(source file)             ← Apply fix
4. [Optional] Bash(build)        ← Verify OR run build if log was stale
= 3-4 calls total
```

**When user pastes the error directly**: Skip log read. Parse `file:line` from pasted text. Start at Read(source). = **2-3 calls**.

**When user provides the source file path**: Skip log read. Read the file directly. = **2 calls**.

**Fallback**: If build also succeeds (no errors) → tell the user "빌드 성공 — 에러를 재현할 수 없습니다." Do NOT switch to Path B for compile errors.

---

### Path B: MCP Path (Asset/Blueprint/Runtime Errors) — Target: 3-5 calls

> For binary asset errors or runtime crashes that need MCP diagnostics.

```python
# Call 1: One smart call captures all diagnostics
ue_fix_errors(operation="smart")
# Call 2: Read source file pointed to by MCP
# Call 3: Edit source file to apply fix
# Call 4 (optional): Build to verify
ue_build_pipeline(operation="build_editor")
# Call 5 (optional): Verify resolution
ue_fix_errors(operation="scan_build_log")
```

```text
1. ue_fix_errors(smart)   ← ONE call only, captures all diagnostics
2. Read(source file)      ← If MCP points to a source file needing edit
3. Edit(source file)      ← Apply fix
4. [Optional] Build       ← ue_build_pipeline(build_editor) if needed
5. [Optional] Verify      ← ue_fix_errors(scan_build_log) if needed
= 3-5 calls total
```

**RULE**: `ue_fix_errors(operation="smart")` is called **exactly ONCE**. Do NOT call it again with different modes.

After the MCP smart call, proceed to the 6-Step Cycle below with Step 1 pre-populated from the result.

---

### 6-Step Complete Cycle (Path B Only)

> This cycle applies when Step 0 routes to Path B (Asset/Blueprint/Runtime errors).
> For compile/build errors (Path A), use Read-first above and skip this cycle entirely.

```text
Step 1: Diagnosis (진단)
↓ Use the ue_fix_errors(smart) result from Path B above
↓ Do NOT call ue_fix_errors again — reuse the Path B result
↓ CAPTURE: Store initial error count as errors_before

Step 2: Analysis (분석)
↓ Explain root cause + Epic best practices
↓ Show prevention strategies

Step 3: Preview (미리보기)
↓ Generate unified diff
↓ Show safety info (.backup)
↓ Filter false positives (engine code, generated code)

Step 4: Apply (적용)
↓ Execute fix with backup

Step 5: Build (빌드)
↓ Smart routing based on error type + Editor state:
↓   BP error + Editor ON  → ue_editor_automation(compile_blueprints)
↓   C++ error (any state) → ue_build_pipeline(build_editor)
↓   Fallback              → ue_fix_errors(build_and_fix)

Step 6: Verify (검증)
↓ Check post-build error state:
↓   Editor ON  → ue_fix_errors(get_compiler_results)
↓   Editor OFF → ue_fix_errors(scan_build_log)
↓ Compare errors_after vs errors_before
↓ If errors remain + iteration < 3 → loop to Step 1
```

---

## Step 5: Build — Post-Fix Rebuild

After applying the fix, trigger a project rebuild to validate effectiveness.

### Build Smart Routing

| Condition | Tool | Notes |
|-----------|------|-------|
| BP-only error + Editor running | `ue_editor_automation(operation="compile_blueprints")` | Fastest (~5s), no UBT spawn |
| C++ error (Editor ON or OFF) | `ue_build_pipeline(operation="build_editor")` | Full UBT compile, offline-capable (default 600s) |
| Unknown/mixed errors | `ue_fix_errors(operation="build_and_fix")` | Builds + captures + parses in one call (default 300s, dynamic timeout) |

### Timeout Handling

- Default timeout: 600s (10 min) for `build_editor`, 300s (5 min) for `build_and_fix`
- Both tools support dynamic timeout: user `timeout` param is respected at dispatcher level (Issue #6231)
- If timeout exceeded: report partial result, suggest manual build
- Large projects (Lyra etc.): set `timeout: 900` or higher (max 3700s ≈ 1h)

---

## Step 6: Verify — Error Resolution Check

After build completes, verify the original error is resolved.

### Verify Routing

| Condition | Tool | Speed |
|-----------|------|-------|
| Editor running | `ue_fix_errors(operation="get_compiler_results")` | <1s (real-time via RC API) |
| Editor not running | `ue_fix_errors(operation="scan_build_log")` | <500ms (parses Saved/Logs/) |

### Decision After Verification

| Condition | Action |
|-----------|--------|
| `errors_after == 0` | Report **SUCCESS** — all errors resolved |
| `errors_after < errors_before` | Report **PARTIAL** — show reduction, loop to Step 1 |
| `errors_after >= errors_before` | Report **FAILED** — suggest .backup restore, escalate |
| Loop iteration >= 3 | **STOP** — prevent infinite loops, report cumulative progress |

### Loop-Back Rules

- Max 3 iterations per session
- Each iteration feeds remaining errors into Step 1
- Report cumulative progress: "Fixed 3/5 errors across 2 iterations"
- If VERIFY shows more errors than before → regression detected → restore .backup

---

## Quick Examples

### Path A — Read-First (Compile Error, 3 Calls)

**User**: "CanCauseDamage 선언 없다는 빌드 에러"

**Error Doctor**:

```text
Step 0: Compile error (undeclared identifier) → Path A (Read-first)

Call 1: Read(Source/LyraGame/Teams/LyraTeamSubsystem.h)
  → No CanCauseDamage declaration found

Call 2: Read(Source/LyraGame/Teams/LyraTeamSubsystem.cpp)
  → Line 128: bool ALyraTeamSubsystem::CanCauseDamage(...) defined
  → Missing: declaration in .h file

Call 3: Edit(Source/LyraGame/Teams/LyraTeamSubsystem.h)
  → Added: bool CanCauseDamage(const UObject* Instigator, ...) const;

Resolution: COMPLETE — 3 calls
```

### Path B — MCP (Blueprint Error, 3 Calls)

**User**: "BP_Player Blueprint compile failed"

**Error Doctor**:

```text
Step 0: Blueprint error → Path B (MCP)

Call 1: ue_fix_errors(operation="smart")
  → BP_Player pin type mismatch at EventBeginPlay node

Call 2: Read(Source/MyGame/Characters/MyPlayerCharacter.h)
  → Verify C++ return type matches BP expectation

Call 3: Edit(Source/MyGame/Characters/MyPlayerCharacter.h)
  → Fixed return type mismatch

Resolution: COMPLETE — 3 calls
NOTE: Did NOT call crash_analysis, runtime_analysis, or ue_check_health.
```

---

## Common Failures

| Error | Cause | Fix |
|-------|-------|-----|
| `ue_fix_errors` project_root missing | `.env`에 `UECODEGEN_PROJECT_DIR` 누락 | `.env` 설정 후 MCP 서버 재시작 |
| False positive on engine code | Engine/Source 경로 오인 | 파일 경로가 프로젝트 `Source/` 하위인지 확인 |
| `.backup` overwritten | 동일 파일 연속 수정 | `git checkout -- <file>` 사용 |
| Build log not found | 빌드 실행 전이거나 경로 틀림 | Path B로 폴백, 또는 수동 빌드 후 재시도 |
| Smart mode wrong routing | 모호한 에러 메시지 | ONE explicit mode fallback, then stop |
| Build timeout (>5min) | 대형 프로젝트 | `timeout` 파라미터 증가 또는 수동 빌드 |
| VERIFY shows more errors | 수정이 새 에러 유발 (회귀) | `.backup` 복원 후 수동 검토 |
| Loop exceeds 3 iterations | 연쇄/상호의존 에러 | 자동 중단, 누적 보고, 수동 검토 |
| Editor RC connection failed | BP 컴파일 시 Editor 미실행 | `ue_build_pipeline(build_editor)`로 폴백 |

---

## Self-Test

`/unreal-error-doctor --self-test` runs validation:

1. **Path A check**: Verify UBT log path exists (`$LOCALAPPDATA/UnrealBuildTool/Log.txt`)
2. **Path B check**: Verify `ue_fix_errors` tool is available via MCP
3. **Anti-pattern check**: Confirm skill body contains all 6 mandatory rules
4. **Version check**: SKILL.md version matches ADVANCED.md version

Output: PASS / FAIL with per-check results.

---

## Related Files

For detailed information, see:

- **REFERENCE.md** - Complete workflow details, error patterns, tool routing
- **EXAMPLES.md** - Complete examples for both Path A and Path B
- **ADVANCED.md** - Tool integration, success criteria, version history

---

## Output Format

### Path A Output

```text
=== Error Doctor Report (Read-First) ===

Step 0: {error_type} → Path A
Call 1: Read({log_or_header})
Call 2: Read({source_file})
Call 3: Edit({file}:{line})

Resolution: COMPLETE
Total Calls: {N} (target: 3-4)
```

### Path B Output

```text
=== Error Doctor Report [Iteration N/M] ===

Diagnosis: {error_pattern}
File: {file_path}:{line}
Confidence: {pct}%
Errors Before: {count}

Analysis:
  {root_cause_explanation}
  Epic Best Practice: {practice_note}

Preview:
  {unified_diff}

Fix Applied: YES
Backup: {file}.backup

Build: {tool_used}
Build Result: SUCCESS|FAILED ({duration})

Verify: PASSED|PARTIAL|FAILED
Errors: {before} → {after}
Resolution: COMPLETE|PARTIAL ({fixed}/{total} resolved)|FAILED

=== Cycle Summary (if multi-iteration) ===
Iterations: {N}
Errors Resolved: {fixed}/{total}
Time Elapsed: {duration}
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `ue_fix_errors` returns no suggestions | Error pattern not in database | Try ONE explicit mode (e.g., `scan_build_log`), then fall back to Read-first |
| False positive on Engine code | Engine/Source path misidentified | Verify file is under project `Source/`, not engine paths |
| `.backup` overwritten by subsequent fix | Multiple fix cycles on same file | Use `git checkout -- <file>` to restore from VCS |
| `ue_build_pipeline` timeout | Large project >5min build | Increase timeout param; suggest manual build |
| `ue_editor_automation` RC failed | Editor not running for BP compile | Fall back to `ue_build_pipeline(build_editor)` |
| VERIFY shows error regression | Fix introduced new errors | Restore `.backup`, report regression, escalate |
| Build log not found at known paths | No recent build, or custom log location | Fall back to Path B (MCP) |

**Last Updated**: 2026-03-21
## MCP Tool Examples

```python
# Check editor status during error diagnosis
ue_editor_automation(operation="get_editor_status")
```
