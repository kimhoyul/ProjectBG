# Unreal Error Doctor - Complete Reference

Complete workflow details, error patterns, and external links.

---

## Complete Workflow Details

### Step 1: Diagnosis

- Read build log or receive error from user
- Parse errors with `ue_fix_errors(operation="smart")`
- Classify into Top 10 patterns
- Identify affected files
- **Capture `errors_before` count** for later verification

### Step 2: Analysis

- Show code context
- Explain Epic Games best practices
- Suggest prevention strategies

### Step 3: Preview

- Generate unified diff
- Show safety info (.backup)
- Display confidence score

### Step 4: Apply

- Create .backup file
- Apply fix using Edit tool or `ue_fix_errors(operation="autofix")`
- Proceed to Build step

### Step 5: Build

Trigger a project rebuild to validate the fix.

**Smart routing decision**:

| Error Type | Editor State | Tool | Duration |
|-----------|-------------|------|----------|
| Blueprint error | Running | `ue_editor_automation(operation="compile_blueprints")` | ~5s |
| C++ error | Any | `ue_build_pipeline(operation="build_editor", params={"configuration":"Development","platform":"Win64","timeout":600})` | 2-10min |
| Unknown/mixed | Any | `ue_fix_errors(operation="build_and_fix", params={"project_root":"<path>","timeout":600})` | 2-10min |

**Error type detection**:
- `.h` / `.cpp` file in diagnosis → C++ error → `build_editor`
- Blueprint node reference in diagnosis → BP error → `compile_blueprints` if Editor running
- If unsure → default to `build_editor` (works offline)

**Timeout handling**:
- Default 600s (10 min) for `build_editor`, 300s (5 min) for `build_and_fix`
- Both tools support dynamic timeout at dispatcher level (Issue #6231) — user `timeout` param is respected
- If timeout → report partial result, suggest manual build
- Large projects: set `timeout: 900+` (max 3700s ≈ 1h)

### Step 6: Verify

Check post-build error state and compare against baseline.

**Verification routing**:

| Editor State | Tool | Speed |
|-------------|------|-------|
| Running | `ue_fix_errors(operation="get_compiler_results", params={"project_root":"<path>"})` | <1s |
| Not running | `ue_fix_errors(operation="scan_build_log", params={"project_root":"<path>"})` | <500ms |

**Resolution judgment**:

| Condition | Verdict | Next Action |
|-----------|---------|-------------|
| `errors_after == 0` | SUCCESS | Report complete resolution |
| `errors_after < errors_before` | PARTIAL | Loop back to Step 1 (if iteration < 3) |
| `errors_after >= errors_before` | FAILED | Suggest .backup restore, escalate to manual |
| `errors_after > errors_before` | REGRESSION | Restore .backup immediately, report regression |

**Loop-back mechanism**:
- Maximum 3 iterations per session
- Each iteration targets the next remaining error
- Cumulative progress tracked: "Fixed N/M errors across K iterations"
- Stagnation detection: if no error reduction in an iteration → stop

---

## Top 5 Error Patterns

1. **Missing Semicolon** (48%) - 95% confidence
2. **.generated.h Position** (25%) - 90% confidence
3. **Missing Category** (15%) - 85% confidence
4. **EditAnywhere without Blueprint** (8%) - 85% confidence
5. **Blueprint Private Mismatch** (4%) - 80% confidence

---

## External Links

- [Epic C++ Coding Standard](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)
- [UPROPERTY Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/uproperty-specifiers)
- [Claude Agent Skills](https://docs.claude.com/en/docs/agents-and-tools/agent-skills/best-practices)
