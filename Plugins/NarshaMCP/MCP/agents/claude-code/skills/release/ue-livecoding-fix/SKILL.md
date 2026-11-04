---
name: ue-livecoding-fix
model: opus
description: "Purpose: Auto-diagnose and retry Live Coding (Hot Reload) failures. 5-step workflow (Status Check -> Error Extract -> Diagnose -> Fix Apply -> Retry Compile). Orchestrates ue_editor_automation + ue_fix_errors for seamless compile-fix-retry loop. UE 5.8 LiveCodingToolset awareness — coexists with Epic's raw CompileLiveCoding() MCP tool. Triggers: 'live coding 실패', 'hot reload 에러', '핫 리로드', 'live coding fix', '라이브 코딩', 'hot reload failed', 'live coding error', 'patch failed'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
argument-hint: "error description or empty for auto-detect"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Live Coding Fix -- Auto-Diagnose & Retry

**Issue**: #5297
**Purpose**: Automatically diagnose Live Coding/Hot Reload failures and orchestrate fix-retry loop

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Error context**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, auto-detect errors from the Editor output log.
2. **Check status** of Live Coding subsystem
3. **Extract errors** from Editor output log
4. **Diagnose** errors with PDB-verified fix suggestions
5. **Verify** hot reload safety of changed files
6. **Guide user** to apply fixes, then **retry** compilation

---

## Auto-Trigger Phrases

### Korean
- "라이브 코딩 실패", "라이브 코딩 에러"
- "핫 리로드 실패", "핫 리로드 에러"
- "Live Coding 안돼", "패치 실패"
- "컴파일 핫 리로드 안돼"

### English
- "Live Coding failed", "Live Coding error"
- "Hot reload failed", "Hot reload error"
- "Patch failed", "Live coding fix"
- "Hot reload not working"

---

## Workflow

### Step 0: Discovery Pre-Exploration (Issue #6289)

> Before checking Live Coding status, use discovery tools to locate error context in source files.
> If `$ARGUMENTS` is empty, proceed without pre-filtering. If it contains a class/file name (e.g., 'AMyCharacter'), use it to scope error search in Step 2.

```python
# 0a. If error description mentions a specific entity
ue_grep(query="<error_entity>", domain="source", limit=5)
# Returns: source files containing the entity (~50 tokens)

# 0b. Get entity detail for better diagnosis
ue_read(identifier="<error_entity>")
# Returns: class structure, file location (~100 tokens)
```text

**Step 0 Decision Logic**:
- Entity found in source → Provides file context for Step 3 (Diagnosis)
- Entity not found → Proceed directly to Step 1 (auto-detect from Editor log)
- Source file shows recent changes → Focus diagnosis on those changes

---

### Step 0.5: Engine Version Routing (UE 5.8 LiveCodingToolset coexistence)

> Detect engine version once so Step 3 (Diagnose) can frame its 5.8+ note accurately. Coexistence-only routing — NarshaMCP drives all 5 steps in both 5.7 and 5.8.

```python
# IMPORTANT API NOTES (verified against live daemon 2026-05-12):
#   - compact=True returns NarshaMCP daemon version, NOT engine version.
#     Must use the full (compact omitted / False) response for engine fields.
#   - The structured `engine_version` object reliably exposes `version` as a string
#     ("5.7" / "5.8"). Older daemon builds may omit the numeric `major`/`minor` fields,
#     so parse the string instead.
#   - The Editor's own engine version may differ from the daemon-resolved one
#     (mismatched engine_path vs. project association). For Live Coding coexistence,
#     prefer the Editor's engine if connected — Live Coding runs through that engine.
health = ue_check_health()  # full response (cost ~792ms p95; runs once per invocation)
editor_engine = (health.get("editor", {}) or {}).get("engine_version") or ""  # e.g. "5.8.0-0+UE5"
daemon_engine = ((health.get("engine_version", {}) or {}).get("version")) or ""  # e.g. "5.7"
version_str = editor_engine or daemon_engine  # Editor authoritative for Live Coding routing

try:
    parts = version_str.split("-", 1)[0].split(".")  # strip "-0+UE5" suffix, then ["5", "8", "0"]
    major, minor = int(parts[0]), int(parts[1])
    engine = (major, minor)
except (ValueError, IndexError, AttributeError):
    engine = None  # health failed or version absent → fall back to "assume 5.7"
```

**Decision tree** (tuple comparison handles UE 6+ correctly):
- `engine is None` → fall back to "assume 5.7" silent path. Mirrors Step 1's "Editor offline → scan_build_log" fallback pattern.
- `engine < (5, 8)` → silently proceed to Step 1. No extra output.
- `engine >= (5, 8)` → emit one informational line, then proceed to Step 1:
  > "Epic LiveCodingToolset (5.8+ Experimental plugin) exposes a raw `CompileLiveCoding()` MCP tool. NarshaMCP wraps it with PDB-verified diagnosis + retry loop. See the **Coexistence with Epic LiveCodingToolset** section below for the full boundary."

The decision is **informational only** — we do NOT call Epic's `CompileLiveCoding()`. NarshaMCP continues to drive Steps 1-5 unchanged in all versions. Two-way integration (calling Epic's tool from inside this skill) is tracked separately in #9879 and is out of scope here.

---

### Step 1: Status Check (Live Coding State)

```python
ue_editor_automation(operation="get_livecoding_status")
# Returns: is_compiling, last_result, is_enabled
```text

**Decision Tree**:
- `is_compiling == true` -> Wait 5 seconds using `sleep 5`, then re-check with `get_livecoding_status`. Repeat up to 3 times before reporting timeout.
- `is_enabled == false` -> Report: "Live Coding is disabled. Enable via Editor Preferences > Live Coding"
- `last_result == "Success"` -> Report: "Last Live Coding compile succeeded. No action needed."
- Editor offline (error response) -> Skip to Step 3 with `ue_fix_errors(operation="scan_build_log")` instead

### Step 2: Extract Errors from Editor Log

```python
ue_editor_automation(operation="get_error_log", severity_filter="error", max_entries=200)
```
Extract compile error lines containing: `error C`, `error LNK`, `Compile failed`, `Live coding failed`.

If no errors found -> Report: "No compile errors in Editor log. Check manually or retry compile."

### Step 3: Diagnose Errors (PDB-verified)

For each extracted error, use the error analysis pipeline:

```python
# Option A: Smart mode (auto-detects best approach)
ue_fix_errors(operation="smart")

# Option B: Manual mode for specific errors
ue_fix_errors(operation="manual", error_message="<error text from Step 2>")
```text

This returns:
- Error classification (missing include, linker, syntax, etc.)
- PDB-verified fix suggestions with confidence scores
- File paths and line numbers for each error

**UE 5.8+ note**: Epic's `ULiveCodingToolset::CompileLiveCoding()` returns a single text blob (compile result + LogLiveCoding output + UBT MSVC diagnostics). NarshaMCP's `ue_fix_errors` post-processes the same underlying surface with PDB-verified fix suggestions — Epic does not synthesize fixes.

### Step 4: Hot Reload Safety Check

```python
ue_fix_errors(operation="hotreload_check", auto_detect_git=true)
```text

**Check for**:
- Template changes in UCLASS (not hot-reload safe)
- Static UPROPERTY modifications (may cause issues)
- Struct layout changes (requires full rebuild)

If `hot_reload_safe == false` -> Warn: "Changes include hot-reload-incompatible modifications. Full rebuild recommended instead of Live Coding."

### Step 5: Apply Fixes & Retry

1. Present fix suggestions to user with confidence scores
2. For high-confidence fixes (>= 0.85): Show diff and recommend applying
3. For lower-confidence fixes: Show details for manual review
4. Apply fixes using the Edit tool on the affected source files. After editing, the user should trigger recompile via Ctrl+Alt+F11 or the skill proceeds to retry compile automatically.
5. After fixes are applied, retry compilation with the **`compile_project`** op of **`ue_editor_assets`** — it triggers Live Coding, polls the error log, and returns an authoritative result in a single call:

```python
# Retry Live Coding compile via the compile_project op.
# WARNING (Issue #6760): do NOT route the retry through the execute_console_command op with the
# LiveCoding.Compile console command. Over the Remote Control path that does NOT trigger Live
# Coding, so the recompile silently no-ops and a following get_livecoding_status reads the
# *previous* log (false pass). compile_project routes to rc_compile_project, which triggers via
# the editor's Python interpreter (execute_python -> unreal.SystemLibrary.execute_console_command),
# polls the error log (3x), and auto-falls back to a UBT subprocess if the Editor is offline.
#
# TOOL NOTE: compile_project is an op of ue_editor_assets, NOT ue_editor_automation.
# ue_editor_automation's allow-list excludes it, so calling it there returns
# "Operation 'compile_project' is not available in ue_editor_automation" (hard error, never compiles).
ue_editor_assets(operation="compile_project")
# Returns: { success, operation:"compile_project", method:"live_coding"|"ubt_subprocess",
#            compile_status, poll_attempts, errors?, ... }
```
6. Decide the retry from the **`compile_status`** field of that result (authoritative — no separate status query needed):
   - `compile_status == "succeeded"` -> done, report success.
   - `compile_status == "failed"` -> read `errors[]`, loop back to Step 2 with the updated error log (max 3 retries).
   - `compile_status == "triggered"` -> compile was launched but no completion/error detected within the poll budget; re-query once with `get_livecoding_status`, then treat a still-unknown result as a retry.
   - `compile_status == "error"` / `"timeout"`, or `method == "ubt_subprocess"` -> Editor offline (or the WS Python trigger did not fire) so the UBT subprocess fallback ran — inspect `message`/log and use offline diagnosis (`ue_fix_errors(operation="scan_build_log")`).
7. If a retry is needed -> Return to Step 2 (max 3 retry cycles)
8. Once `compile_status == "succeeded"` (or a `triggered` re-query resolves clean) -> report success and exit the loop

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `get_livecoding_status` fails | Editor offline | Use `ue_fix_errors(operation="scan_build_log")` for offline diagnosis |
| `get_error_log` returns empty | Log rotated or cleared | Use `ue_fix_errors(operation="scan_build_log")` to check Saved/Logs/ |
| `hotreload_check` finds template issues | Template in UCLASS | Recommend full rebuild via `ue_editor_automation(operation="execute_console_command")` or manual UBT |
| LiveCoding compile timeout | Large project | Suggest checking Editor output log manually |
| 3 retries exhausted | Deep/cascading errors | Escalate to `/ue-debug` skill for full diagnosis |
| No PDB index available | First build or index missing | Suggestions fall back to heuristic mode (lower confidence) |

---

## Output Format

```text
=== Live Coding Fix Report ===

--- Status ---
Live Coding: Enabled
Last Result: Failed
Is Compiling: No

--- Diagnosis (N errors found) ---
1. [MyCharacter.cpp:42] error C2065: 'MyVar' undeclared identifier
   Fix (95% confidence): Add #include "MyVar.h"

2. [MyCharacter.cpp:88] error C2039: 'OldFunc' not a member of 'AMyActor'
   Fix (85% confidence): Rename to 'NewFunc' (API changed in 5.4)

--- Hot Reload Safety ---
All changes are hot reload safe.

--- Retry ---
Applying Live Coding compile (compile_project)...
Result: Success! (compile_status: succeeded) Patch applied in 3.2s.
```

---

## Common Failures

| Error | Cause | Solution |
|-------|-------|----------|
| Live Coding disabled | Editor preference | Enable in Edit > Editor Preferences > Live Coding |
| "Modules updated" but changes not visible | DLL not patched | Restart Editor (rare edge case with static init) |
| Linker errors after fix | Missing module dependency | Check Build.cs for required modules |
| Repeated failure on same error | Cascading error | Fix the first error only, then retry |

---

## Coexistence with Epic LiveCodingToolset (UE 5.8+)

UE 5.8 ships `Engine/Plugins/Experimental/Toolsets/LiveCodingToolset/` (Epic commit `9838587a4191`, 2026-04-23) — an Experimental, editor-only plugin off by default. Both tools are non-conflicting and can be enabled side-by-side.

- **Epic's surface**: A single MCP tool, `ULiveCodingToolset::CompileLiveCoding()`, that calls `ILiveCodingModule::Compile(WaitForCompletion)` and returns one text blob (compile result enum + captured `LogLiveCoding` output + UBT MSVC diagnostics). No status query, no error-log extractor, no PDB-verified fix synthesis, no retry orchestration.
- **NarshaMCP's surface**: This skill wraps the same underlying Live Coding pipeline with 5-step orchestration — discovery → status → log extract → PDB-verified diagnose → hot-reload safety → retry loop.
- **Boundary**: Epic owns the compile primitive; NarshaMCP owns fix synthesis + retry. Epic's plugin is toggled at runtime via the `LiveCodingToolset.Enable` CVar (default `true` when the plugin is loaded).
- **Cross-refs**: Parent watch #9328 (Section 2.B for positioning); 5.8 deep dive #9879; project-wide Epic-toolset delegation strategy in [`docs/migration/EPIC_5.8_DELEGATION_MAP.md`](../../../docs/migration/EPIC_5.8_DELEGATION_MAP.md) (classifies `LiveCodingToolset` as ★ COEXIST).

---

## Related

- **Issue #5297**: Auto-diagnose Live Coding failures + retry loop
- **Issue #4843**: compile_project LiveCoding pipeline
- **Issue #6760**: RC `execute_console_command("LiveCoding.Compile")` does NOT trigger Live Coding (root cause of #16683)
- **Issue #16683**: Step 5 retry switched to the `compile_project` op (the non-triggering console path no-op'd the retry loop)
- **Issue #4319**: hotreload_check operation
- **Issue #5291**: Build Error Pipeline (reused error analysis)
- **Issue #11394**: UE 5.8 LiveCodingToolset awareness + coexistence (this update)
- **Issue #9328**: Epic "Unreal MCP" productization watch (parent)
- **Issue #9879**: UE 5.8 readiness watch (deep dive)
- **Skills**: `/ue-debug` (full diagnosis), `/unreal-error-doctor` (compilation errors)

---

**Status**: Phase 1 MVP
**Last Updated**: 2026-06-25
