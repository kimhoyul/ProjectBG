# ue-pre-commit Reference

**Version**: 1.0.0
**Skill**: [SKILL.md](SKILL.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md)

> Companion to [SKILL.md](SKILL.md). Hot-path 5-phase scan + impact + report stays in SKILL.md; reference-grade detail (Step 0 Discovery, Phase 4 cross-ref full code, Phase 6 test-intent full workflow + edge cases + trust boundaries, Per-Symptom Diagnostic Dispatch, Phase 5 risk math) lives here.
>
> **Intentional asymmetry — Phase 3 lives ONLY in SKILL.md.** Phase 1-2 (`detect_changes` entry point) and Phase 3 (predictive_check + impact_analysis loop + hotreload_check + replication_audit) are the hottest path — every `/ue-pre-commit` invocation runs the full 4-substep MCP orchestration. Pulling Phase 3 detail out to REFERENCE.md would force the operator to context-switch on every run. By contrast, Step 0 / Phase 4 / Phase 6 detail are HIGH-risk-only or operator-initiated (`--intents-only`), so summary-in-SKILL + full-in-REFERENCE wins on the common case. This section therefore has NO "Phase 3 (Full)" subsection — `git grep -n 'Phase 3' SKILL.md` is the canonical reference.

---

## Phase Summary

| Phase | Purpose | Key Output |
|-------|---------|------------|
| Step 0 | Discovery pre-exploration (verify PDB presence) | `step0_target_exists`, `step0_verified_targets` |
| Phase 0 | Discovery pre-exploration (file-level) | HIGH/LOW risk routing |
| Phase 1-2 | Accelerated change detection | `changed_files`, `changed_symbols`, `affected_blueprints`, `risk_level` |
| Phase 3 | Impact analysis (predictive + impact + hotreload + replication) | per-target impact reports |
| Phase 4 | Cross-reference extension (gated on HIGH risk) | BP caller mapping, dependency graph, BP structure |
| Phase 5 | Consolidated report | Overall risk, recommendations |
| Phase 6 | Test intent suggestion (HIT + MISS, 30s budget) | Markdown rerun-existing + suggest-new tests |

---

## Step 0: Discovery Pre-Exploration (Full)

> **Purpose**: Before running heavy `impact_analysis` in Phase 3, use lightweight Discovery Tools (`ue_grep` / `ue_read`) to verify changed functions/classes exist in PDB index. Avoids wasted `impact_analysis` calls on targets that PDB can't resolve (~27x token savings).

### When to Run Step 0

- **ALWAYS** run Step 0 after Phase 2 (function extraction), before Phase 3 (impact analysis)
- Step 0 uses the extracted targets from Phase 2 as input

### Step 0 Workflow

```python
# 0a. For each extracted target, quick grep to verify PDB presence
for target in extracted_targets:
    grep_result = ue_grep(query=target.name, limit=5)
    # Returns: matches across symbols/assets/metadata (~50 tokens)

# 0b. For HIGH-risk targets, read detail for signature pre-check
for target in high_risk_targets:
    if target.type == "function":
        detail = ue_read(identifier=target.name)
        # Returns: function signature, parameter types (~100 tokens)
        # Use to pre-verify signature changes before impact_analysis

# 0c. Use Step 0 results to:
#   - Skip impact_analysis for targets NOT in PDB (step0_target_exists=false)
#   - Pre-confirm function signatures before calling impact_analysis
#   - Reduce Phase 3 calls to only PDB-verified targets
```

### Step 0 Result Variables (downstream 참조용)

Phase 3/4에서 아래 변수명으로 Step 0 결과를 참조하여 불필요한 호출을 스킵합니다:

- `step0_target_exists` — 각 타겟의 PDB 존재 여부 (false면 impact_analysis 스킵)
- `step0_read_detail` — `ue_read` 시그니처 응답 (HIGH-risk 타겟의 파라미터 사전 확인)
- `step0_verified_targets` — PDB에서 확인된 타겟 목록 (Phase 3에서 이 목록만 분석)

### Step 0 Decision Logic

```text
Step 0 result → Decision:
  Target found in PDB     → Include in Phase 3 impact_analysis
  Target NOT in PDB       → Skip impact_analysis, report "PDB unindexed" in Phase 5
  Signature pre-confirmed → Phase 3 can focus on BP impact (skip signature re-check)
  No targets verified     → Short-circuit: "No indexed targets. Run ue_check_health()."
```

---

## Phase 4: Cross-Reference Extension (Full)

**Only execute for targets with**:
- `change_category` == HIGH, OR
- Phase 3 `impact_analysis` found affected BPs (total_affected > 0)

### 4a: Function-Level BP Caller Mapping

```python
ToolSearch(query="select:mcp__narshamcp__ue_analyze_symbols")

# PRIMARY: Use impact_analysis blueprint_impact (already fetched in Phase 3)
# Phase 3's impact_analysis response includes blueprint_impact.direct_bp_callers[]
# when BP callers exist (Issue #5292). Use this data first.
#
# FALLBACK: If blueprint_impact is missing from Phase 3, try find_cpp_to_bp directly.
# Note: find_cpp_to_bp requires MCP binary built after Issue #5292 (PR #5370).
for target in high_risk_targets:
    if target.type == "function":
        # Check if Phase 3 already has BP caller data
        if phase3_impact.get("blueprint_impact", {}).get("direct_bp_callers"):
            bp_callers = phase3_impact["blueprint_impact"]["direct_bp_callers"]
        else:
            # Fallback: direct find_cpp_to_bp call
            bp_callers_resp = ue_analyze_symbols(operation="find_cpp_to_bp", params={
                "function_name": target.name,  # Unqualified name recommended for find_cpp_to_bp
                "limit": 30
            })
            bp_callers = bp_callers_resp.get("blueprint_callers", [])
            # Returns: blueprint_callers[], total_callers
            # If operation not available (older binary), skip with warning

            # Issue #7068: EFG fallback when find_cpp_to_bp returns 0
            # MCP binary v0.9.7+ auto-includes efg_fallback when cross-ref returns 0.
            if not bp_callers and bp_callers_resp.get("efg_fallback"):
                efg = bp_callers_resp["efg_fallback"]
                bp_callers = efg.get("results", [])
                # These have confidence 0.60 (Statics wrapper pattern)
```

### 4b: Class-Level BP Dependency Graph (when many BPs affected)

```python
for target in high_risk_targets:
    if target.type == "class" and impact.total_affected > 5:
        bp_deps = ue_analyze_symbols(operation="find_cross_bp_dependencies", params={
            "query": target.name
        })
        # Returns: dependencies[], dependents[], has_circular_dependencies
```

### 4c: Inspect Affected BP Structure (top 3 most impacted)

```python
ToolSearch(query="select:mcp__narshamcp__ue_manage_blueprint")
for bp_name in top_affected_bps[:3]:  # Limit to top 3 most impacted
    bp_structure = ue_manage_blueprint(operation="get_structure", params={
        "blueprint_name": bp_name
    })
    # Helps identify which BP nodes/functions reference the changed C++ class
```

---

## Phase 5: Risk Level Calculation

```python
RISK_LEVELS = {
    "SAFE":    (0, 3),     # 0-3 affected, all LOW/MEDIUM changes
    "CAUTION": (4, 15),    # 4-15 affected, or any HIGH change
    "BLOCKED": (16, None), # 16+ affected, or hot-reload unsafe + HIGH changes
}
```

**Overall risk calculation**:
- Count all affected BPs across all targets (deduplicated)
- Factor in hot-reload safety status
- Factor in change categories (HIGH changes escalate risk)

---

## Phase 6: Test Intent Suggestion — Full Workflow

> Optional, additive phase that surfaces "what to test" — HIT path (existing test reuse) or MISS path (intent-driven suggestions). The triage decision (default / skip / `--intents-only`) and 30s wall-clock budget summary live in [SKILL.md § Phase 6](SKILL.md#phase-6-test-intent-suggestion-issue-10284-epic-8186) — operators consult that hot-path block first. This section is the implementation detail.

### 6a: find_test_impact (HIT path)

```python
ToolSearch(query="select:mcp__narshamcp__ue_analyze_symbols")

import time
phase6_start = time.monotonic()
PHASE6_BUDGET_S = 30.0

# 6a runs under the same 30s wall-clock budget as 6b. The MCP op itself
# does not enforce a per-call timeout, so we wrap it in a try/except and
# do a post-call deadline check — if either path trips, skip Phase 6
# with a single warning line. Do NOT fall through to 6b.
try:
    impact_resp = ue_analyze_symbols(operation="find_test_impact", params={
        "symbols": changed_symbols,    # from Phase 1-2 detect_changes
        "max_depth": 2                  # default depth (1, 2, 3, 4 — clamped to 4)
    })
except Exception as e:
    # Op unavailable (PDB not loaded, call graph not built, MCP transport error).
    return _render_phase6_skipped(
        reason=f"find_test_impact unavailable: {e}",
        rerun_hint="/ue-pre-commit --intents-only",
    )

if time.monotonic() - phase6_start > PHASE6_BUDGET_S:
    # 6a alone exceeded the budget — do not start 6b derivations.
    return _render_phase6_skipped(
        reason=f"6a exceeded {PHASE6_BUDGET_S}s budget",
        rerun_hint="/ue-pre-commit --intents-only",
    )
```

**Response shape (Issue #9143)**:
```python
{
  "success": True,
  "operation": "find_test_impact",
  "input_symbols": [...],
  "affected_tests": [
    {"test_file": "...", "test_symbol": "...", "changed_symbol": "...",
     "impact_depth": 1, "test_kind": "SimpleAutomation"|"Spec"|"CQTest"|...}
  ],
  "summary": {
    "input_symbol_count": N, "affected_test_count": M,
    "total_callers_traversed": ..., "truncated": False
  }
}
```

### 6b: derive_test_intents (MISS path)

```python
# For each symbol that has NO HIT (i.e., not in affected_tests[].changed_symbol),
# derive intents — but ONLY if 6a's summary.truncated == false (else mark
# non-HIT symbols as "unknown coverage" rather than "miss" to prevent FP).
hit_symbols = {row["changed_symbol"] for row in impact_resp["affected_tests"]}
truncated = impact_resp["summary"]["truncated"]

if truncated:
    miss_symbols = []          # treat as unknown — no derive_test_intents
    unknown_symbols = [s for s in changed_symbols if s not in hit_symbols]
else:
    miss_symbols = [s for s in changed_symbols if s not in hit_symbols]
    unknown_symbols = []

derived_intents = {}      # symbol -> {intents: [...], truncated: bool}
unanalyzed = {}           # symbol -> {reason: str, hint: str}  (verified live 2026-05-02)
for symbol in miss_symbols:
    # Per-iteration deadline check — partial result if budget runs out.
    if time.monotonic() - phase6_start > PHASE6_BUDGET_S:
        # Stop derivation; remaining miss_symbols become "not analyzed".
        break

    intent_resp = ue_analyze_symbols(operation="derive_test_intents", params={
        "function": symbol  # FQN like "AMyCharacter::TakeDamage"
    })

    if intent_resp.get("success"):
        derived_intents[symbol] = {
            "intents": intent_resp["intents"],
            "truncated": intent_resp.get("summary", {}).get("truncated", False)
        }
    else:
        unanalyzed[symbol] = {
            "reason": intent_resp.get("error", "derive_test_intents failed"),
            "hint": intent_resp.get("hint", ""),
        }
```

**Success response shape (Issue #10282)**:
```python
{
  "success": True,
  "operation": "derive_test_intents",
  "function": "...",
  "intents": [
    {"description": "...", "assertion_template": "...",
     "framework_hint": "SimpleAutomation"|"Spec"|"CQTest"|...,
     "priority": "P1"|"P2"|"P3", "source_pattern": {...},
     "change_driven": False}
  ],
  "summary": {"intent_count": N, "truncated": False, ...}
}
```

**Failure response shape** (verified live against narshamcp 0.9.13):
```python
{"success": False, "operation": "derive_test_intents",
 "error": "PDB detail store not loaded",
 "hint":  "Call ue_check_health first to confirm PDB is indexed."}
```

The op returns success=false on PDB-state issues, op-not-available, or symbol-resolution failures. Capture the error+hint so 6c can surface a per-symbol diagnostic instead of silently dropping the symbol (which would render misleading "0 new tests suggested").

### 6c: Markdown Rendering + Injection Safety

```python
# IMPORTANT: every `intent.description` is sanitized through `_safe_desc`
# before interpolation. The output bullets are list items (not code-fenced
# blocks), so a malicious description containing backticks, newlines,
# `](evil)` markdown links, or heading lines could otherwise break the
# surrounding PR-description block when copy-pasted.

def _safe_desc(s: str, max_len: int = 200) -> str:
    """Strip markdown-injection vectors from upstream intent descriptions.

    Backticks would terminate any inline code span the operator might wrap
    the bullet in; \\r and \\n would split a single bullet into a new
    sibling line; the 200-char cap bounds runaway descriptions from a
    misbehaving rule engine. Caller is expected to render the result inside
    a list-item context, e.g. `f"- `{symbol}` ({prio}, {fw}): {_safe_desc(desc)}"`.
    """
    s = s.replace("`", "'").replace("\r", " ").replace("\n", " ")
    return s[:max_len]
```

---

## Phase 6 Edge-Case Rendering Matrix

| Edge Case | Rendering |
|---|---|
| All symbols HIT | "🔁 Rerun N existing tests" block only; no MISS block. |
| All symbols MISS | "🆕 K new tests suggested" block only; no HIT block. |
| `truncated: true` from 6a | "ℹ️ Existing-test coverage incomplete (call graph truncated) — N symbols not analyzed."; suppress MISS suggestions for non-HIT symbols to avoid false positives. |
| `derive_test_intents` empty intents (success=true, intents=[]) | "⚠️ No patterns detected for `<symbol>` — manual test design needed." |
| `derive_test_intents` failure (success=false) — verified live 2026-05-02 against narshamcp 0.9.13 | "⚠️ Could not analyze `<symbol>` — `<error>`. Hint: `<hint>`." Symbol stays in the report under a dedicated "Could not analyze" block; never silently dropped. **Common cause: `detail_store` not populated.** This is a *separate state* from the PDB symbol index — `ue_check_health` may report `loading_status: ready` and `symbols_cached: 1269663` while `derive_test_intents` still fails with `"PDB detail store not loaded"`. Detail store is built only during the four-phase PDB load (see [server_lifecycle/symbol_loading.rs:1160](../../../MCP/native/mcp_server/src/tools/server_lifecycle/symbol_loading.rs#L1160)); a session that loaded from the bincode snapshot won't have it. Workaround: `ue_cache_control(operation="regenerate_pdb", wait=true)` to force a four-phase rebuild (~5 min on LCP-class projects). |
| Budget exhausted mid-6b | Show partial intents + "⚠️ N symbols not analyzed (30s budget exhausted) — rerun with `/ue-pre-commit --intents-only` for full coverage." |
| 6a itself fails/times out | "⚠️ Phase 6 skipped — `find_test_impact` unavailable. Rerun with `/ue-pre-commit --intents-only` later." Full report (Phases 0-5) still shown. |
| Markdown injection in `intent.description` | Backtick-escape and replace newlines with `\\n` before rendering — preserves the surrounding code-fenced block when pasted into PR. |

---

## Phase 6 Trust Boundaries

- **Sister-epic split (#8188)**: Phase 6 owns the **MISS** path only. The **HIT** block is a hand-off note — when #8188 ships its richer existing-test router, the HIT block becomes the operator's entry point to that workflow.
- **No file generation in Phase 6**: Phase 6 emits *suggestions* as Markdown. Actual `.cpp` test file generation happens via a separate, explicit `/ue-test-gen --intents-from <FQN>` invocation by the operator (Issue #10283). This keeps the pre-commit fast and side-effect-free.
- **`framework_hint` is canonical**: trust the upstream `derive_test_intents` rule engine; do NOT re-invoke `/ue-test-framework-advisor` per intent.

---

## Error Recovery & Per-Symptom Diagnostic Dispatch

| Error | Cause | Fallback |
|-------|-------|----------|
| Not a git repo | Wrong directory | Report: "Not a git repository. Run from project root." |
| No staged files | Nothing staged | Report: "No staged files. Use `git add` first, or try `--branch` mode." |
| No C++ files in diff | Non-C++ changes only | Short-circuit: "No C++ files changed. Safe to commit." |
| `impact_analysis` fails | PDB not indexed | Report partial: "PDB not available. Run `ue_check_health()`. Showing hotreload_check only." |
| Cross-ref index unavailable | BP metadata not loaded | Skip Phase 4, report Phase 3 results with note |
| `find_cpp_to_bp` unavailable | MCP binary predates Issue #5292 | Use `impact_analysis` response's `blueprint_impact.direct_bp_callers[]` instead |
| `find_cpp_to_bp` returns 0 | Statics wrapper pattern (Issue #7068) | Check `efg_fallback` in response; use those results with lower confidence (0.60) |
| `hotreload_check` fails | No Source directory found | Skip hot-reload check, warn in report |
| Too many targets (>20) | Large refactor | Batch to class-level only, cap at 20 MCP calls |
| `git diff` timeout | Very large diff | Limit to first 50 files, warn about truncation |
| Phase 6 6a (`find_test_impact`) fails | PDB not indexed / call graph not loaded | Skip Phase 6 entirely with single warning line. Phases 0-5 still shown. Suggest `/ue-pre-commit --intents-only` after `ue_check_health` confirms PDB ready. |
| Phase 6 6a returns `truncated: true` | Call graph too large for full traversal | Treat non-HIT symbols as "unknown coverage" rather than "miss". Emit "ℹ️ Existing-test coverage incomplete (call graph truncated)". Do NOT call `derive_test_intents` for unknown symbols (false-positive prevention). |
| Phase 6 6b budget exhausted | 30s wall-clock cap hit mid-`derive_test_intents` loop | Emit partial intents + "⚠️ N symbols not analyzed (30s budget exhausted) — rerun with `/ue-pre-commit --intents-only` for full coverage". |
| Phase 6 6b empty intents on a symbol | `derive_test_intents` rule engine had no match | Emit "⚠️ No patterns detected for `<symbol>` — manual test design needed" instead of silently dropping. |

---

## Related

- **SKILL.md** — hot-path 5-phase workflow + Output Format spec
- **EXAMPLES.md** — activation test cases + quantitative metrics + sample outputs
- **Issue #5299** — Pre-commit impact analysis linking C++ changes to affected BP/assets
- **Issue #5292** — C++↔BP cross-reference bridge (dependency for full BP mapping)
- **Issue #10284** — Phase 6 — Test Intent Suggestion (Sub-issue 4 of Epic #8186)
- **Issue #9143** — `find_test_impact` op (Phase 6 step 6a)
- **Issue #10282** — `derive_test_intents` op (Phase 6 step 6b)
- **Issue #10283** — `/ue-test-gen --intents-from <FQN>` consumer (Phase 6 step 6c footer)
