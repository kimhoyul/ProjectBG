---
name: ue-pre-commit
model: opus
description: "Purpose: Pre-commit safety check analyzing staged C++ changes to map impact on BPs/assets, with optional Phase 6 test-intent suggestions (#10284). 6-phase workflow: VCS scan -> function extract -> impact analysis -> cross-ref -> report -> test intent suggestion. Orchestrates git diff + ue_analyze_symbols + ue_fix_errors. Triggers: 'pre-commit', 'commit check', '커밋 전', '푸시 전 검사', 'safe to commit', 'commit impact', '커밋 영향'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools: [Bash, Read, Glob, Grep]
argument-hint: "[--staged | --branch | --range=main...HEAD | --intents-only]"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

## Current State (auto-collected)
- Staged files: !`git diff --cached --name-only`
- Branch: !`git branch --show-current`

> If the above data is empty, collect staged files manually in Phase 1.

# UE Pre-Commit Impact Analysis -- Commit Risk Assessment

**Issue**: #5299, #6352
**Purpose**: Answer "Is it safe to commit?" by analyzing C++ changes and mapping affected BPs/assets
**Requires**: #5292 (C++<->BP cross-reference) for full BP impact mapping

> **Reference docs**: [REFERENCE.md](REFERENCE.md) (Step 0 full / Phase 4 full / Phase 6 full + edge-case matrix + trust boundaries / Per-Symptom Diagnostic Dispatch / Phase 5 risk math) | [EXAMPLES.md](EXAMPLES.md) (5 walkthroughs + activation test cases + quantitative metrics)

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the 5-phase workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Diff scope**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, default to `--staged`.
2. **Run the repository VCS gate first** via git diff, then filter to C++ files
3. **Extract changed targets** (classes, functions, properties) from diff hunks
4. **Run impact analysis** via MCP tools for each target
5. **Generate consolidated report** with risk level and recommendations

### Mandatory VCS Gate (first executable action)

For the default `--staged` mode, obtain `git diff --cached --name-only` for the
current repository **before any MCP or discovery call**. Normally, execute the
command directly. An evaluator transport may instead provide an explicit
`<transport-vcs-snapshot authoritative="true">` collected by its parent in the
same repository immediately before this run; when its `verified` field is true,
consume that result as the gate and do not duplicate the command. Do not infer
the result from the literal `Current State` lines above: transports that inline
this skill body do not execute the `!` interpolation syntax. If a supplied
snapshot is unverified, stop and report that the VCS gate is blocked.

- If the command returns no paths, stop immediately without MCP calls and report:
  `No staged changes. There is nothing to analyze and no affected Blueprints. Safe to commit from this staged-change check.`
- If paths exist but none are C/C++ (`.h`, `.hpp`, `.c`, `.cc`, `.cpp`, `.cxx`),
  stop immediately without MCP calls and report that no staged C++ changes or
  affected Blueprints were found and that the staged-change check is safe.
- Only a non-empty staged C/C++ set proceeds to Phase 0 and the MCP workflow.

For `--branch` / `--range`, run the corresponding scope-aware git command first
and apply the same empty/no-C++ short-circuit semantics.

---

## Auto-Trigger Phrases

### Korean
- "커밋 전 분석해줘", "커밋 전 검사"
- "커밋 검사", "푸시 전 검사", "커밋해도 돼?"
- "이 변경 커밋해도 안전해?", "커밋 영향 분석"
- "스테이지된 파일 영향 분석"
- "커밋 전에 BP 영향 확인"

### English
- "Pre-commit check", "Pre-commit analysis"
- "Is it safe to commit?", "Check before commit"
- "Commit impact analysis", "What does my commit affect?"
- "Push safety check", "Analyze staged changes"
- "Impact before commit"

---

> **CI mode (Issue #8400 Phase 1)**: For headless CI pipelines, use the
> `narshamcp pre-build --diff <range> --format json` CLI subcommand
> instead of this skill. It runs the same replication cross-check
> (REPL-001/002/003 rules) without requiring Claude Code, PDB, or a
> daemon. See [narshamcp_pre_build.md](../../../docs/reference/tools/narshamcp_pre_build.md).

---

## Step 0: Discovery Pre-Exploration (Issue #6352)

After Phase 2 function extraction, verify each target exists in PDB before launching Phase 3 `impact_analysis`. Skip targets where `step0_target_exists=false` (not in PDB) — report as "PDB unindexed" in Phase 5. Pre-confirm function signatures via `ue_read` for HIGH-risk targets so Phase 3 can focus on BP impact only.

Full workflow + result variables (`step0_target_exists`, `step0_read_detail`, `step0_verified_targets`) + decision logic: see [REFERENCE.md § Step 0: Discovery Pre-Exploration (Full)](REFERENCE.md#step-0-discovery-pre-exploration-full).

---

## 5-Phase Workflow (with Step 0)

### Phase 0: Discovery Pre-Exploration (Issue #6289)

> After the mandatory VCS gate finds changed C++ files, use discovery tools to
> pre-assess their impact.

```python
# 0a. For each changed C++ file, check symbol presence
ue_glob(pattern="<changed_class>*", domain="symbols", limit=10)
# Returns: related symbols across project (~50 tokens)

# 0b. Quick cross-domain check for changed functions
ue_grep(query="<changed_function>", limit=10)
# Returns: references in symbols/assets/metadata/source (~100 tokens)
```

**Phase 0 Decision Logic**:
- Changed function found in metadata (BP refs) → HIGH risk, proceed to Phase 3+4
- Changed function found only in source → LOW risk, skip Phase 4 (Cross-Reference)
- Changed class not in PDB → New class, skip impact_analysis, check compile only
- Multiple cross-domain references → Flag for careful review

---

```text
Phase 1-2 (ACCELERATED via Issue #6862):
   | ue_analyze_symbols(operation="detect_changes", scope="staged")
   | → changed_files, changed_symbols, affected_blueprints, risk_level in ONE call (<200ms)
   v
Step 0: Discovery Pre-Exploration
   | ue_grep(target) + ue_read(target) -> verify PDB presence
   v
Phase 3: Impact Analysis (MCP) [only step0_verified_targets]
   | ue_analyze_symbols(impact_analysis) + ue_fix_errors(hotreload_check)
   v
Phase 4: Cross-Reference Extension (MCP) [gated on HIGH risk]
   | ue_analyze_symbols(find_cpp_to_bp) for function-level BP mapping
   v
Phase 5: Consolidated Report
   | Risk assessment, affected BPs, hot-reload safety, recommendations
   v
[Pre-Commit Impact Report]
```

### Phase 1-2: Accelerated Change Detection (Issue #6862)

> **Replaces manual git diff + text parsing with single MCP call.**

```python
# Single MCP call replaces Phase 1 (VCS scan) + Phase 2 (symbol extraction)
# Uses GitDiffAnalyzer (libgit2) + PDB symbol index + BP cross-ref index
scope = "staged"
result = ue_analyze_symbols(operation="detect_changes", scope=scope)
# Alt scopes: "unstaged" (working tree), "all" (both)

# Response includes:
# - changed_files: ["Source/MyGame/MyCharacter.cpp"]
# - changed_symbols: ["AMyCharacter"]
# - affected_blueprints: ["BP_Hero", "BP_Enemy"]
# - risk_level: "none" | "low" | "medium" | "high"
# - pdb_status: "ready" | "not_loaded"
# - cross_ref_status: "available" | "not_loaded"
# - scan_completeness: {"complete": true|false, ...}

# SHORT-CIRCUIT: An empty result is authoritative only after a complete scan.
changed_files = result.get("changed_files")
scan_complete = result.get("scan_completeness", {}).get("complete") is True
response_complete = (
    result.get("success") is not False
    and isinstance(changed_files, list)
    and result.get("degraded") is not True
    and not result.get("failed_stages")
    and not result.get("skipped_stages")
)
if not changed_files and scan_complete and response_complete:
    print("No C++ files changed. Safe to commit (no BP/asset impact).")
    return

# Empty + incomplete/ambiguous is UNKNOWN, not safe. Enumerate staged files
# with git diff before deciding whether the C++ path can short-circuit.
if not response_complete or not changed_files:
    commands = {
        "staged": ["git diff --cached --name-only"],
        "unstaged": ["git diff --name-only"],
        "all": ["git diff --cached --name-only", "git diff --name-only"],
    }[scope]
    files = {f for command in commands for f in Bash(command)}
    changed_files = sorted(f for f in files if f.endswith((".h", ".cpp")))
    result["changed_files"] = changed_files

# Use result for Phase 3+ inputs
changed_symbols = result.get("changed_symbols", [])
risk_level = result.get("risk_level", "unknown")
affected_bps = result.get("affected_blueprints", [])
```

`pdb_status = "not_loaded"` or `cross_ref_status = "not_loaded"` means symbol/BP
impact evidence is incomplete even when file enumeration completed. Preserve the
changed files, report that limitation, and do not turn an empty impact subset into
a clean/safe claim.

The scope-aware Git enumeration above is the authoritative fallback. Do not
replace it with a staged-only diff: `unstaged` and `all` must retain their
working-tree inputs.

### Phase 3: Impact Analysis (MCP)

```python
ToolSearch(query="select:mcp__narshamcp__ue_analyze_symbols")
ToolSearch(query="select:mcp__narshamcp__ue_fix_errors")

# 3a: Predictive error check (Issue #5505)
# Detects missing includes, deprecated APIs, BP breakage risks BEFORE compilation
predictive = ue_fix_errors(operation="predictive_check", auto_detect_git=true, staged=true, unstaged=false)
# Returns: build_safe, warnings[], validators_run[]

# 3b: Impact analysis per target (skip NONE category + step0 unverified)
all_impacts = []
for target in targets:
    if target.change_category == "NONE":
        continue
    if not target.step0_target_exists:  # Step 0 filtered — not in PDB
        continue  # Report as "PDB unindexed" in Phase 5

    impact = ue_analyze_symbols(operation="impact_analysis", target=target.name, depth=2)
    # Returns: risk_level, total_affected, affected_blueprints[],
    #   summary{direct_callers, delegate_binders, interface_implementors},
    #   blueprint_impact{direct_bp_callers[]}
    all_impacts.append((target, impact))

# 3c: Hot-reload safety check
hotreload = ue_fix_errors(operation="hotreload_check", auto_detect_git=true, staged=true, unstaged=false)
# Returns: issues[], checked_files, hot_reload_safe

# 3d: Replication Safety Check (Issue #7315)
# If staged changes touch Replicated UPROPERTY declarations,
# audit for declared-but-not-registered mismatches.
ToolSearch(query="select:mcp__narshamcp__ue_diff")

staged_diff = Bash("git diff --cached")
replication_gate_failures = []
if "Replicated" in staged_diff:
    for target in targets:
        if target.step0_target_exists and target.name.startswith(("A", "U")):
            repl_audit = ue_diff(operation="replication_audit", class_name=target.name)
            # Fail closed: an incomplete audit is never equivalent to a clean audit.
            replication_gate_failed = (
                repl_audit.get("success") is not True
                or repl_audit.get("validation_complete") is not True
                or repl_audit.get("has_issues") is not False
            )
            if replication_gate_failed:
                replication_gate_failures.append((target, repl_audit))
                all_impacts.append((target, {"replication_audit": repl_audit}))

replication_gate_passed = len(replication_gate_failures) == 0
# Phase 5 MUST emit a BLOCKED verdict and MUST NOT recommend committing
# whenever replication_gate_passed is false.
```

### Phase 4: Cross-Reference Extension (Gated)

Only execute for targets with `change_category == HIGH` OR Phase 3 `impact_analysis` found affected BPs (`total_affected > 0`). Three sub-steps: 4a function-level BP caller mapping (PRIMARY: reuse Phase 3 `blueprint_impact.direct_bp_callers`; FALLBACK: `find_cpp_to_bp` direct call + Issue #7068 EFG fallback), 4b class-level BP dependency graph (when many BPs affected), 4c inspect top 3 affected BP structures.

Full code blocks + EFG fallback semantics: see [REFERENCE.md § Phase 4: Cross-Reference Extension (Full)](REFERENCE.md#phase-4-cross-reference-extension-full).

### Phase 5: Consolidated Report

Aggregate all results into a single report (see Output Format below).

**Overall risk calculation**:
- Count all affected BPs across all targets (deduplicated)
- Factor in hot-reload safety status
- Factor in change categories (HIGH changes escalate risk)
- Force `BLOCKED` when `replication_gate_passed` is false (`success != true`,
  `validation_complete != true`, or `has_issues != false` for any audit)

`RISK_LEVELS` table (SAFE / CAUTION / BLOCKED thresholds): see [REFERENCE.md § Phase 5: Risk Level Calculation](REFERENCE.md#phase-5-risk-level-calculation).

---

### Phase 6: Test Intent Suggestion (Issue #10284, Epic #8186)

> **Optional, additive phase.** Builds on Phase 1-2's `changed_symbols` to surface *what to test*: existing tests that already cover the change (HIT path) or intent-driven suggestions for new tests (MISS path). Output is Markdown, directly pasteable into a PR description.
>
> **Wall-clock budget: 30s.** Partial result + "rerun with `--intents-only`" fallback if the budget runs out mid-phase. Use the `--intents-only` flag to skip Phases 0-5 entirely and run only Phase 6 (useful for big refactors where the operator already saw the risk verdict on a previous run).

#### When to Run Phase 6

- **Default**: run after Phase 5 if `changed_symbols` from Phase 1-2 is non-empty.
- **Skip**: if the short-circuit ("No C++ files changed") fired, Phase 6 is a no-op.
- **`--intents-only` mode**: skip Phases 0-5; run Phase 1-2 (`detect_changes`) + Phase 6 only.

#### Phase 6 Workflow Summary

- **6a `find_test_impact`** (HIT path): wrapped in try/except + 30s post-call deadline check. On exception or budget overrun → skip Phase 6 with single warning, do NOT fall through to 6b.
- **6b `derive_test_intents`** (MISS path): only runs when 6a `summary.truncated == false` (else mark non-HIT symbols as "unknown coverage" to prevent FP). Per-iteration deadline check; partial result on budget exhaustion. **Per-symbol `success` is checked**: `derive_test_intents` returns `success=false` for symbols it cannot resolve (e.g. the PDB *detail store* isn't loaded, even when the symbol index itself is ready), so each such symbol is recorded in an `unanalyzed` dict (`symbol -> {reason, hint}`) instead of being dropped. Only symbols with `success=true` go into `derived_intents`.
- **6c Markdown rendering**: every `intent.description` sanitized through `_safe_desc` (200-char cap, backtick→`'`, `\r`/`\n`→space) before interpolation into list-item context. The `unanalyzed` dict is rendered as a dedicated **"Could not analyze"** block (one bullet per symbol with its `reason`/`hint`), so a PDB-state failure surfaces honestly rather than collapsing into a misleading "0 new tests suggested" — see the edge-case note below.

Full code (try/except + budget guards + response shapes + `unanalyzed` capture + `_safe_desc` helper): see [REFERENCE.md § Phase 6: Test Intent Suggestion — Full Workflow](REFERENCE.md#phase-6-test-intent-suggestion--full-workflow).

8-row edge-case rendering matrix (all-HIT / all-MISS / `truncated: true` / empty intents / `success: false` / budget exhausted / 6a failure / markdown injection): see [REFERENCE.md § Phase 6 Edge-Case Rendering Matrix](REFERENCE.md#phase-6-edge-case-rendering-matrix).

| Phase 6 edge case | Rendering in report |
|---|---|
| `derive_test_intents` returns `success=false` for a symbol (e.g. `"PDB detail store not loaded"`; verified live 2026-05-02 against narshamcp 0.9.13) | Symbol is captured in the `unanalyzed` dict and rendered under a **"⚠️ Could not analyze"** block as ``⚠️ Could not analyze `<symbol>` — <reason>. Hint: <hint>``, never silently dropped. Prevents a misleading "0 new tests suggested" when the real cause is a PDB-state issue. |

Trust boundaries (sister-epic split #8188 / no file generation / `framework_hint` canonical): see [REFERENCE.md § Phase 6 Trust Boundaries](REFERENCE.md#phase-6-trust-boundaries).

---

## Output Format

```text
=== Pre-Commit Impact Report ===

--- Scope ---
Mode: Staged files (git diff --cached)
Changed C++ files: 3 (.h: 2, .cpp: 1)
Targets analyzed: 5 (classes: 2, functions: 3)

--- Risk Summary ---
Overall Risk: HIGH
Hot Reload Safe: NO (2 issues found)

--- Changed Files ---
  [H] Source/MyGame/Characters/MyCharacter.h (signature_change)
  [M] Source/MyGame/Characters/MyCharacter.cpp (function_body)
  [H] Source/MyGame/Weapons/WeaponBase.h (property_change)

--- Impact by Target ---

1. AMyCharacter (class, HIGH)
   Affected BPs: 7
   Risk Level: high
   - BP_PlayerCharacter (hop 1) - direct caller
   - BP_EnemyBase (hop 1) - subclass
   - BP_BossEnemy (hop 2) - via BP_EnemyBase
   - BP_Companion (hop 1) - direct caller
   - [+3 more...]
   Direct BP Callers: 4
   Delegate Bindings: 1

2. AMyCharacter::TakeDamage (function, HIGH - signature changed)
   BP Callers: 3
   - BP_PlayerController -> EventGraph::CallFunction_42
   - BP_DamageSystem -> DamageCalculation::CallFunction_17
   - BP_HUD -> HealthUpdate::CallFunction_8

3. UWeaponBase (class, HIGH - UPROPERTY changed)
   Affected BPs: 4
   - BP_Rifle (hop 1) - subclass
   - BP_Pistol (hop 1) - subclass
   - BP_MeleeWeapon (hop 1) - subclass
   - BP_WeaponPickup (hop 2) - references weapon

--- Hot Reload Safety ---
Status: NOT SAFE (2 issues)
  1. [MyCharacter.h] UCLASS layout changed (struct size affected)
     -> Full rebuild recommended
  2. [WeaponBase.h] UPROPERTY added (may invalidate serialized data)
     -> Resave affected Blueprints after rebuild

--- Recommendations ---
  1. [CRITICAL] Rebuild project before testing (hot reload not safe)
  2. [HIGH] Verify BP_PlayerController cast compatibility (TakeDamage signature changed)
  3. [HIGH] Resave BP_Rifle, BP_Pistol, BP_MeleeWeapon (UPROPERTY layout change)
  4. [MEDIUM] Check 3 BP callers of TakeDamage for parameter mismatch
  5. [LOW] 1 function body change only - API preserved, BPs unaffected

--- Verdict ---
PROCEED WITH CAUTION: 11 BPs affected, 2 hot-reload issues.
Recommend: Full rebuild + resave affected BPs before commit.

### Test Coverage (Phase 6)

🔁 **Rerun 1 existing test** (HIT path):
- `MyGame.MyCharacter.TakeDamage_Spec` exercises `AMyCharacter::TakeDamage`

🆕 **2 new tests suggested** (MISS path):
- `UWeaponBase::DamageMultiplier` (P1, Spec): UPROPERTY default value coverage
- `UWeaponBase::DamageMultiplier` (P2, SimpleAutomation): Replication boundary case

> Generate: `/ue-test-gen --intents-from UWeaponBase::DamageMultiplier`
```

---

## Error Recovery

Common short-circuits (4-row brief): not a git repo / no staged files / no C++ files in diff / `impact_analysis` PDB miss → report partial. Full 14-row Per-Symptom Diagnostic Dispatch table (cross-ref unavailable, `find_cpp_to_bp` predates #5292, EFG fallback, `hotreload_check` no-Source, large refactor batching, git diff timeout, all 4 Phase 6 failure modes): see [REFERENCE.md § Error Recovery & Per-Symptom Diagnostic Dispatch](REFERENCE.md#error-recovery--per-symptom-diagnostic-dispatch).

---

## Evaluation Criteria

Activation test cases (5 Positive + 4 Negative) + Quantitative Metrics (5-row target table — extraction accuracy, BP detection completeness, hot-reload accuracy, response time, FP rate): see [EXAMPLES.md § Appendix A: Activation Test Cases](EXAMPLES.md#appendix-a-activation-test-cases) and [§ Appendix B: Quantitative Metrics](EXAMPLES.md#appendix-b-quantitative-metrics).

---

## Related

- **Issue #5299**: Pre-commit impact analysis linking C++ changes to affected BP/assets
- **Issue #5292**: C++<->BP cross-reference bridge (dependency for full BP mapping)
- **Issue #10284**: Phase 6 — Test Intent Suggestion (this revision; Sub-issue 4 of Epic #8186)
- **Issue #9143**: `find_test_impact` op (Phase 6 step 6a)
- **Issue #10282**: `derive_test_intents` op (Phase 6 step 6b)
- **Issue #10283**: `/ue-test-gen --intents-from <FQN>` consumer (Phase 6 step 6c footer)
- **Skills**: `/ue-impact` (entity-specific impact), `/ue-livecoding-fix` (hot-reload diagnostics), `/ue-test-gen` (Phase 6 follow-up generation)
- **MCP Tools**: `ue_analyze_symbols(impact_analysis)`, `ue_analyze_symbols(find_cpp_to_bp)`, `ue_analyze_symbols(find_test_impact)`, `ue_analyze_symbols(derive_test_intents)`, `ue_fix_errors(hotreload_check)`, `ue_fix_errors(predictive_check)` (Issue #5505)

---

**Status**: Phase 1 MVP + Phase 6 (test-intent suggestion, Issue #10284)
**Last Updated**: 2026-05-02
