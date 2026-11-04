---
name: ue-validate
model: opus
description: "Purpose: Automated asset quality validation using UE's FDataValidationContext. Validates assets against project rules, checks references, detects invalid states, and reports issues with priority. Answers 'Is this asset valid?' with Pass/Fail report. Triggers: 'validate', 'data validation', '에셋 검증', '유효성 검사', 'asset quality', 'check assets', '밸리데이션'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
argument-hint: "(no args = validate all project assets), --path /Game/Characters (specific directory), --usecase PreSubmit (validation context)"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Validate -- Asset Quality Validation

**Issue**: #5300
**Purpose**: Run UE's FDataValidationContext-based validation on project assets with priority-ranked issue reporting

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the validation workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Scope**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, validate all project assets (default scope).
2. **Collect asset list** using `ue_search_assets` or accept user-provided paths
3. **Execute validation** via `ue_editor_assets(operation="validate_asset")`
4. **Aggregate issues** and categorize by severity (Error/Warning)
5. **Rank by reference count** using `ue_editor_assets(operation="find_asset_references")`
6. **Generate validation report** with prioritized action items

---

## Auto-Trigger Phrases

### Korean
- "에셋 검증해줘", "유효성 검사"
- "에셋 품질 확인", "데이터 유효성"
- "에셋 밸리데이션", "밸리데이트"
- "제출 전 검증", "커밋 전 확인"
- "이 에셋 유효해?"

### English
- "Validate assets", "Run data validation"
- "Check asset quality", "Asset validation"
- "Pre-submit check", "Validate before commit"
- "Are these assets valid?", "Check for validation errors"
- "Run IsDataValid"

---

## Validation Workflow

### Step 0: Discovery Pre-Exploration (Issue #6289)

> Before collecting assets for validation, estimate scope with `ue_glob` to set expectations.

```python
# 0a. Estimate asset count in target scope
ue_glob(pattern="<scope_pattern>", domain="assets", limit=50)
# Returns: asset names with types (~50 tokens)
# Example: ue_glob(pattern="BP_*", domain="assets") for Blueprint-only validation
```text

**Step 0 Decision Logic**:
- Zero assets → Report "no assets found matching scope", STOP
- 1-10 assets → Validate all individually (detailed report)
- 11-50 assets → Validate with summary grouping
- 50+ assets → Warn about validation time, suggest narrowing scope with `--path`

---

### Step 1: Collect Assets

```python
ToolSearch("select:mcp__narshamcp__ue_search_assets")

# Option A: Specific path from user
assets = ue_search_assets(query="<user_path>/*", asset_type="blueprint")

# Option B: Full project scan (default)
bp_assets = ue_search_assets(query="BP_*", asset_type="blueprint")
mat_assets = ue_search_assets(query="M_*", asset_type="material")
# Combine all asset paths into a list (max 50 per validation call)
```markdown

### Step 2: Validate Assets

```python
ToolSearch("select:mcp__narshamcp__ue_editor_assets")

result = ue_editor_assets(
    operation="validate_asset",
    asset_paths=["/Game/BP_Player", "/Game/M_Rock", ...],
    validate_usecase="Manual",
    include_warnings=True,
)
# Returns: num_requested, num_checked, num_valid, num_invalid, num_warnings, num_skipped
```markdown

**Usecase Options**:
| Usecase | When to Use |
|---------|-------------|
| `Manual` | Default, general-purpose validation |
| `Save` | Validating before saving assets |
| `PreSubmit` | Pre-commit/pre-submit quality gate |
| `Commandlet` | CI/CD pipeline validation |
| `Script` | Scripted/automated validation |

### Step 3: Reference Analysis (Priority Ranking)

For each invalid asset, determine fix priority based on reference count:

```python
# For each invalid asset in results
for invalid in result.assets where status == "Invalid":
    refs = ue_editor_assets(
        operation="find_asset_references",
        asset_path=invalid.asset_path
    )
    # More references = higher priority to fix
```markdown

### Step 4: Generate Report

---

## Output Format

```text
=== Asset Validation Report ===

--- Summary ---
Validation Usecase: Manual
Total Requested: 45
Checked: 43 | Valid: 38 | Invalid: 3 | Warnings: 2 | Skipped: 2

--- Critical Issues (Errors) ---
  Priority  Asset                    Refs  Issue
  1         /Game/BP_Enemy           23    Missing parent class reference
  2         /Game/M_BrokenMat        12    Invalid material expression node
  3         /Game/NS_Explosion       5     Missing required Niagara emitter

--- Warnings ---
  Priority  Asset                    Refs  Issue
  1         /Game/BP_Item            8     Deprecated function usage in graph
  2         /Game/WBP_HUD            3     Unbound widget property

--- Skipped (2) ---
  /Game/DT_Loot — No IsDataValid override
  /Game/Curve_Damage — No IsDataValid override

--- Recommendations (Priority Ordered) ---
  1. [CRITICAL] Fix BP_Enemy (23 downstream refs affected)
  2. [CRITICAL] Fix M_BrokenMat (12 refs affected)
  3. [CRITICAL] Fix NS_Explosion (5 refs affected)
  4. [WARNING]  Review BP_Item deprecated functions (8 refs)
  5. [WARNING]  Review WBP_HUD bindings (3 refs)
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| EditorValidatorSubsystem unavailable | DataValidation plugin not enabled | Report: "Enable DataValidation plugin in your .uproject file" |
| Editor not connected | No live Editor instance | Report: "Asset validation requires a running Editor. Start Unreal Editor first." |
| Asset not found | Invalid path | Skip asset, include in "Skipped" section with error |
| Batch too large (>50 assets) | Too many assets | Auto-chunk into batches of 50, aggregate results |
| Timeout | Complex validators take too long | Report partial results, suggest reducing batch size |
| UE < 5.4 | ValidateAssetsWithSettings unavailable | Falls back to per-asset IsDataValid() automatically |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** - Should activate:
1. "에셋 검증해줘" -> Activate (full project validation)
2. "Validate /Game/Characters assets" -> Activate (scoped validation)
3. "/ue-validate --usecase PreSubmit" -> Activate (pre-submit mode)
4. "이 에셋들 유효해?" -> Activate
5. "Run data validation on blueprints" -> Activate

**Negative (3)** - Should NOT activate:
1. "프로젝트 진단해줘" -> Use `/ue-audit` skill (project-wide health, not asset validation)
2. "빌드 에러 해결해줘" -> Use `/ue-debug` skill (compilation errors, not data validation)
3. "에셋 검색해줘" -> Use `ue_search_assets` directly (search, not validate)

### Disambiguation from `/ue-audit`

| Query Contains | Route To | Reason |
|----------------|----------|--------|
| "검증", "유효성", "validate", "valid" | `/ue-validate` | Asset correctness checking |
| "진단", "감사", "audit", "health", "건강" | `/ue-audit` | Project-wide health scoring |

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Activation accuracy | >95% |
| Validation detection rate | >90% of UE-reported issues |
| Priority ranking accuracy | >80% correlation with actual impact |
| Full project validation time | <60 seconds (50 assets) |

---

**Status**: Phase 1 MVP
**Related**: Issue #5300 (FDataValidationContext Integration), #5296 (Project Health Report)

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

1. **Collect findings during validation** as an in-memory list of
   `AnalysisResult` objects, one per validation issue from
   `EditorValidatorSubsystem` / `FDataValidationContext`:
   ```json
   {
     "tool": "ue-validate",
     "rule_id": "VALIDATE-MISSING-REFERENCE",
     "severity": "error",
     "file_path": "Content/MyGame/Materials/M_Rock.uasset",
     "line": 1,
     "column": null,
     "message": "Texture reference not found: T_Rock_BaseColor",
     "suggestion": "Re-import or restore T_Rock_BaseColor; or remove parameter"
   }
   ```
   `severity` MUST be one of `error` / `warning` / `info` (lowercase). Map
   `EMessageSeverity` (Error → `error`, Warning → `warning`, Info → `info`).
   `column` and `suggestion` are optional (omit or `null`). Asset-level
   findings use line=1.

2. **Write to temp file** as a bare JSON array (CLI accepts envelope or array):
   ```bash
   TMPFILE="${TMPDIR:-/tmp}/validate-findings-$$.json"
   ```

3. **Shell out to `narshamcp analyze`** for format conversion:
   ```bash
   narshamcp analyze \
     --source ue-validate \
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
- `--output-format=json` (equals form) ❌ — emit error

### Examples

```bash
# Local dev: Markdown validation report
/ue-validate

# CI: JSON for pre-submit gate
CI=true /ue-validate --output-format json
```

### Backward compatibility (AC #8)

The asset validation workflow above is **unchanged**. Trigger phrases,
argument parsing, and Markdown report generation remain byte-identical
to the pre-Phase-4 baseline.
