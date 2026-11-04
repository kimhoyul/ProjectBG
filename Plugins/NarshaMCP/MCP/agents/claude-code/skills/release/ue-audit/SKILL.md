---
name: ue-audit
model: opus
description: "Purpose: Comprehensive UE project health audit with 6-point diagnostic. Checks server health, config consistency, asset quality, performance, code quality, and message log diagnostics. Answers 'Is this system healthy?' with scored report and prioritized recommendations. Triggers: 'audit', 'health check', 'project health', '감사', '건강 검진', '프로젝트 상태', 'system check', 'quality check', 'validate project', '경고 요약', '메시지 로그', 'message log', 'auto fix warnings'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
argument-hint: "(no args = full audit), --config (config only), --performance (perf only), --assets (assets only), --messages (message log only)"
metadata:
  version: "1.4.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Audit — System Health Audit

**Issue**: #4800, #5296, #5458, #6352, #25735
**Purpose**: Answer "Is this system/project healthy?" with comprehensive 6-point diagnostic.

> **Reference**: Detailed workflows, templates, recovery, and evaluation criteria:
> [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the audit workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

1. **Scope**: `$ARGUMENTS`. Empty `$ARGUMENTS` → full audit (all 6 categories).
   Per-category flags (`--config`, `--assets`, `--perf`, `--code`, `--messages`, `--health`):
   see [REFERENCE.md § Scope Selection](REFERENCE.md#6-point-audit-framework--scope-selection).
2. **Execute all applicable audit checks** sequentially.
3. **Score each category** (Pass/Warning/Fail).
4. **Generate audit report** with overall score and **priority-ordered actionable
   recommendations** — see [Final Response Contract](#️-final-response-contract-must--eval-gated)
   (layout template: [REFERENCE.md § Output Format](REFERENCE.md#output-format)).

---

## Step 0: Discovery Pre-Exploration (Issue #6352)

Run Step 0 before a full audit; skip it for a single-category request.

```python
ue_grep(query="Error|Warning|Deprecated", limit=10)
Glob(pattern="**/*.utrace")
Glob(pattern="Saved/Logs/*.log")
```

Use these probes to skip unavailable detail and prioritize error-bearing categories.
[Full decision workflow](REFERENCE.md#step-0-discovery-pre-exploration-full-workflow-issue-6352).

---

## 6-Point Audit Framework

### Check 1: Server Health

```python
health = ue_check_health()
```

**Scoring**: Pass = all checks green | Warning = PDB outdated (>24h) or partial index | Fail = server unreachable or invalid project path.

If `epic_mcp_coexist.detected`, emit its message/doc URL as **Warning**. On UE
5.8+, show MCP capability fields as **Info**; omit them on ≤5.7. [Details](REFERENCE.md#check-1-server-health--version-gated-surfaces-rationale).

### Check 2: Configuration Audit

```python
# Deprecated CVars: search_config rejects "*"/empty (wildcard); use exact key or partial substring
deprecated_check = ue_analyze_config(operation="search_config", key="r.AllowOcclusionQueries")
override_check = ue_analyze_config(operation="find_overrides", option="r.Shadow")
```

**2b: Config Override Cascade (Issue #7315)** — if ≥2 platform configs exist, trace the override chain via `ue_diff(operation="compare_config_chain")`. Example: [REFERENCE.md § Check 2 Config Override Cascade example](REFERENCE.md#check-2-config-override-cascade-example-issue-7315).

**Scoring**: Pass = clean | Warning = deprecated/platform override | Fail = critical conflicts.

### Check 3: Asset Quality

```python
bp_assets = ue_search_assets(query="BP_*", asset_type="blueprint")
material_assets = ue_search_assets(query="M_*", asset_type="material")
niagara_assets = ue_search_assets(query="NS_*", asset_type="niagara")

ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")
cross_bp = ue_analyze_symbols(operation="find_cross_bp_dependencies", class_name="<main game class>")
```

Query each asset type by its expected prefix; use the
[full prefix table](REFERENCE.md#check-3-naming-convention-rules-full-table).

**Scoring**: Pass = >90% compliance | Warning = 70-90% | Fail = <70%.

### Check 4: Performance Check

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_insights")
perf_stats = ue_analyze_insights(operation="get_stats")
hot_functions = ue_analyze_insights(operation="get_hot_functions")
```

**Scoring**: Pass = budget OK | Warning = hot functions | Fail = sustained drops.

**Note**: Requires .utrace data. If `step0_has_trace` is false, report as "Skipped — No trace data. Run Unreal Insights to capture."

### Check 5: Code Quality

```python
ToolSearch("select:mcp__narshamcp__ue_fix_errors")
preflight  = ue_fix_errors(mode="preflight", build_log_path="<path>/Saved/Logs/Build.log")  # needs build log
dep_check  = ue_fix_errors(mode="dependency_check")
hotreload  = ue_fix_errors(mode="hotreload_check", auto_detect_git=True)
```

> `preflight` requires `build_log_path`. If absent, fall back to `dependency_check` (always works) or `hotreload_check` (needs git).

Extra options (D: `ufunction_usage` dead-UFUNCTION; E: `replication_audit` for replicated Actors) with full per-option rationale: [REFERENCE.md § Check 5 Code Quality option variants](REFERENCE.md#check-5-code-quality--option-variants-rationale).

**Scoring**: Pass = no errors, <5 warnings | Warning = no errors, 5-20 warnings or minor dependency issues | Fail = any errors, missing critical deps, or circular deps.

### Check 6: Message Log Diagnostics (Issue #5296, #5458)

```python
ToolSearch("select:mcp__narshamcp__ue_editor_debug")
ue_editor_debug(operation="get_message_log")
ue_editor_debug(operation="get_message_log", log_category="<category>", severity_filter="Warning")
ue_editor_debug(operation="execute_message_fix", log_category="<category>", message_index=0, fix_token_index=0, dry_run=True)
```

Full flow and category list: [REFERENCE.md § Check 6](REFERENCE.md#check-6-message-log-diagnostics--full-5-step-detail-issue-5296-5458).

**SAFETY**: Audit mode is always `dry_run=True`; execute fixes only after explicit user approval.

**Scoring**: Pass = no errors across all categories | Warning = warnings only, or fixable errors (Fix tokens available) | Fail = unfixable errors (no Fix tokens) in critical categories (MapCheck, PIE, LoadErrors) | Skipped = Editor offline.

If Editor is offline, report the check as skipped and follow the contract below.

---

## ⚠️ Final Response Contract (MUST — eval-gated)

Issue #25735 / QUAL-03: in human Markdown mode, end with one truthful terminal
shape. CI/automation mode uses its artifact-only routing instead.

- Start with the Markdown heading `## Audit status`, followed by
  `**Overall status**: healthy|warning|fail` and
  `**Coverage**: complete|partial`. Coverage never replaces a verified warning
  or fail status; use `partial` when requested coverage is unverified/skipped.
- **Findings**: use priority order. Each finding labels the fields `severity`,
  `category`, `evidence`, `action`, and `verification` (rendered as `Severity`,
  `Category`, `Evidence`, `Action`, and `Verification`). Action names an exact
  target and concrete step; verification names a command or observable pass state.
- **Clean**: give the evidence-backed healthy verdict and say no action is
  required. Do not invent recommendations.
- **Unverified/skipped**: name the limitation and missing coverage; do not claim
  it is healthy. For Editor offline, name the exact category and a revalidation
  command. Verification must re-run `/ue-audit --messages` after reconnecting;
  do **not** stop at launching the Editor alone.

"Review warnings", "investigate further", "check the project", "consider fixing
issues", "fix the warnings", or "run the checks again" are not actionable
without a category-scoped target, action, and verification. Detailed terminal
shapes and offline cases: [REFERENCE.md](REFERENCE.md#terminal-shapes-and-offline-coverage-issue-25735).

---

## CI / automation mode

When `--output-format json|sarif|junit` is passed OR `CI=true` env is set, skip the
Markdown report and shell out to `narshamcp analyze` for format conversion. Full
routing rules: [REFERENCE.md § Output Format Routing](REFERENCE.md#output-format-routing-issue-8413-phase-4).
This artifact-only route replaces the human `## Audit status` terminal shape.
When CI emits `AnalysisResult.suggestion`, it follows the same concrete-action contract.

---

## See Also

[REFERENCE.md](REFERENCE.md) — trigger phrases, scope enum, Check 0 narrative, output template, error recovery, evaluation criteria, Output Format Routing, plus relocated Step 0 / Check 1 / Check 2 / Check 3 / Check 5 / Check 6 detail.
