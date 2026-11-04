---
name: ue-config-audit
model: opus
description: "Purpose: Whole-config health audit for UE projects. Rolls up the lint_config op (unknown CVar / type-range / conflicting-override / platform-sanity / redundant-override) plus scan_deprecated into one health scorecard with fix suggestions. Answers 'Is this config healthy?'. Triggers: 'config audit', 'config health', 'config lint', 'config 감사', '설정 감사', 'config 헬스', '설정 건강 진단'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.8+ C++ project"
argument-hint: "config file name (optional, e.g. DefaultEngine.ini) or empty for whole-config audit; optional --target 5.8 for the deprecated lane"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Config Audit — Whole-Config Health Rollup

**Issue**: #20050 (Epic #20041)
**Purpose**: Two-tier config health diagnostic. The `lint_config` op (Issue #20051) is the
substrate — a version-agnostic, fully-offline lint over every project `.ini`; this skill is
the second tier that rolls its five categories (plus a deprecated lane via `scan_deprecated`)
into a single "is my config healthy?" scorecard with prioritized fix suggestions.

---

## Auto-Execution Instructions

> **CRITICAL**: When loaded, Claude **MUST execute the rollup automatically** — run the MCP
> tools and score the result, do NOT just print documentation.

1. **Target**: `$ARGUMENTS`. Empty → whole-config audit; a file name (`DefaultEngine.ini`) →
   scope `lint_config` to that file; `--target <version>` → engine version for the deprecated
   lane (defaults to the live project engine version).
2. **Run the 2-tier rollup** (Tier 1 = substrate op calls, Tier 2 = score + suggest).
3. **Emit a scorecard** across the five dimensions with ranked fix suggestions.

**Call-count minimization** (MANDATORY): complete in **2-4 MCP calls** (Bash excluded).
Optimal 2 = `lint_config` + `scan_deprecated`. `lint_config` is the primary call (4 of 5
dimensions in one offline pass); `scan_deprecated` supplies the version-aware deprecated
dimension `lint_config` omits by design. Do NOT re-query each key with `search_config`/
`explore_cvar` — the rollup scores from the batch response.

---

## Auto-Trigger Phrases

KO: "config 감사해줘", "설정 감사", "config 헬스", "설정 건강 진단", "config 린트", "이 config 건강한가".
EN: "config audit", "config health", "config lint", "audit config files", "is my config healthy".
Full KO/EN lists + disambiguation table: [REFERENCE.md § Triggers & Disambiguation](REFERENCE.md#triggers--disambiguation).

**Disambiguation (one-liner)**: single-CVar deep dive → `/ue-cvar-explorer`; whole-project
6-point health → `/ue-audit`; broad "find all settings" sweep → `ue_grep`. This skill is the
config-scoped **health** lane.

---

## 2-Tier Rollup Workflow

```text
Tier 1 (op call): lint_config -> 5 categories | scan_deprecated -> deprecated keys
Tier 2 (skill):   map categories -> 5 dimensions, grade each, rank fixes -> scorecard
```

### Step 1 — Tier 1 substrate op calls

```python
# PRIMARY call: 4 of 5 dimensions in one offline pass. config_file optional
# (omit for whole-config, or pass a file name parsed from $ARGUMENTS to scope).
# Issue #23837: BOTH lint_config AND scan_deprecated must receive the SAME config_file
# so a single-file audit cannot mix sibling-ini deprecated findings into the score.
lint = ue_analyze_config(
    operation="lint_config",
    project_root="<auto>",
    config_file="<optional file from $ARGUMENTS>",
    limit=100,
)
# -> category_totals {unknown_cvar,type_range,conflict_shadow,platform_sanity,redundant_override},
#    findings_by_category, files_scanned, engine_defaults_compared, skipped_checks

# DEPRECATED lane: the version-aware dimension lint_config omits by design.
# Pass the same config_file scope as lint (Issue #23837).
deprecated = ue_analyze_config(
    operation="scan_deprecated",
    project_root="<auto>",
    config_file="<same optional file from $ARGUMENTS>",
    target_version="<--target arg or live engine version>",
)
# -> deprecated config keys/values + suggested replacements for the target version
#    + migration_db {available, data_dir, stems_loaded, load_errors, rules_loaded}
#    + files_scanned (1 when config_file set)
```

> **Honesty gate (MCP Empty-Result Fallback)**: inspect `lint.skipped_checks`. When the
> engine path is unavailable, the `unknown_cvar` and engine-`redundant_override` checks are
> **skipped** — grade those dimensions **N/A (engine offline)**, never PASS. A skipped check
> is not a passing check.
>
> **Migration DB honesty (Issue #23837)**: if `deprecated.success` is false and
> `deprecated.migration_db.available` is false (dir missing, empty, or unreadable JSON),
> grade the **Deprecated** dimension **N/A (migration DB unavailable)** with the
> `error` / `load_errors` text — never treat 0 matches as “no deprecated keys”.

### Step 2 — Tier 2 dimension scoring

Roll each `category_totals` count (and the deprecated count) into a per-dimension grade,
weighted by finding severity (`error` dominates; `info` shadow/redundant is hygiene).

| Dimension | Source |
|-----------|--------|
| Unknown CVar | `lint.category_totals.unknown_cvar` |
| Deprecated | `deprecated` matches |
| Type error | `lint.category_totals.type_range` |
| Conflicting override | `lint.category_totals.conflict_shadow` |
| Platform sanity | `lint.category_totals.platform_sanity` |
| Hygiene (advisory) | `lint.category_totals.redundant_override` |

Grade rubric: 0 findings → PASS · 1-3 warnings → WARN · >3 warnings or any error → FAIL ·
skipped → N/A. **Overall** = worst dimension (any FAIL→FAIL, else any WARN→WARN, else PASS);
report N/A dimensions separately so an offline check never inflates the grade.

### Step 3 — Rank fix suggestions

Each finding carries `file`, `line`, `section`, `key`, `value`, and a human `message`. Rank
top offenders per dimension (error > warning > info) with concrete `file:line` and the
suggested action (fix the typo'd CVar, migrate the deprecated key, correct the value type,
delete the dead override, rename the never-loaded platform file).

---

## Output, Errors, Evaluation

Scorecard output template, the full error-recovery table, and activation/quantitative eval
criteria live in [REFERENCE.md](REFERENCE.md) (progressive disclosure) — sections
[§ Output Format](REFERENCE.md#output-format), [§ Error Recovery](REFERENCE.md#error-recovery),
[§ Evaluation Criteria](REFERENCE.md#evaluation-criteria).
