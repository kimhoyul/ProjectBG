# `ue-insights-profiler` evals

Two eval suites live here:

| File | Tests | Trigger |
|---|---|---|
| `trigger-grading.json` | Whether the skill activates on the right query phrasings (precision/recall) | `/agent-test` or the skill router itself |
| `verdict-consistency.json` | Whether the skill cites bottleneck verdicts consistently across 20 question phrasings on the SAME trace (Issue #96 F-006/F-007) | Manual eval runner with judge LLM |

## verdict-consistency.json — how to run

This is W4-pre/W4 of Issue #96. The skill previously gave opposite verdicts
("GameThread-bound" vs "GPU-bound") on the same trace depending on question
phrasing. The fix is structural — `get_thread_breakdown` now exposes
`verdict_source`/`verdict_value`/`verdict_warning` (Layer 1) and SKILL.md
mandates citing those fields verbatim (Layer 2). This eval enforces the
contract.

### Prerequisites

- A `.utrace` file with both CPU + thread data (the eval is meaningless on
  empty traces).
- Two distinct LLM endpoints for the **3-role blind protocol**:
  - **Responder**: invokes `/ue-insights-profiler` on each of 20 scenario
    phrasings and produces an answer
  - **Judge**: scores each (response, answer) pair without seeing the
    scenario phrasing or expected verdict
- `judge` config: see `verdict-consistency.json` → `judge` (default:
  `claude-sonnet-4-6`, `temperature=0.0`, blind protocol)

### Step-by-step

1. **Pick a trace** with a clear bottleneck (e.g. a known GameThread-heavy
   PIE recording). Same trace is reused across all 20 scenarios.
2. **Run the 3 gold fixture questions first** through the judge with the
   actual responder skipped (gold answers are pre-canned in
   `verdict-consistency.json`). Threshold: 3/3 correct → judge is calibrated;
   ≤2/3 → judge drift → abort, swap judge model, retry.
3. **Run each of the 20 scenarios** through the responder:
   ```text
   /ue-insights-profiler <scenario.phrasing> --trace=<your.utrace>
   ```
   Capture the responder's text answer.
4. **Submit (trace_response, answer) pairs to the judge** per
   `verdict-consistency-judge.md`. The judge returns
   `PASS | FAIL_CRITICAL | FAIL_PHRASING_DRIFT` per scenario.
5. **Apply pass thresholds** from `verdict-consistency.json`:
   - **20 of 20 critical-category passes required** (unit_violation /
     label_citation_missing / verdict_value_recomputed): ANY critical fail
     blocks the eval.
   - **≤1 phrasing_drift failure allowed** (i.e. 19/20 on phrasing alone).
   - All other failures: block.

### Baseline (W4-pre)

Run the eval ONCE before any W4 source/skill changes to record the baseline
verdict consistency. Then run again after W4 to prove improvement. Without
the baseline number, "≥19/20" is not falsifiable (could be the pre-fix
score, in which case W4 changed nothing).

Baseline file format (suggested): `verdict-consistency-baseline-<sha>.json`
with `{commit_sha, judge_model, pass_count, critical_fails, phrasing_drifts}`.

### Failure follow-ups

- **All-PASS** → W4 is closed. Commit baseline.
- **Phrasing drift > 1** → Layer 2 SKILL.md rules need stronger language
  or Layer 1 needs to surface more fields (e.g. `verdict_source` not
  prominent enough).
- **Critical fail (unit_violation)** → Layer 3 sanity check is being
  ignored; consider hoisting into Layer 1 (response-level reject).
- **Critical fail (label_citation_missing)** → Layer 2 "Citation Rule"
  needs concrete examples for the failing phrasing.
- **Critical fail (verdict_value_recomputed)** → Layer 1's `verdict_value`
  field name may be unclear; consider adding `verdict_value_human`
  pre-formatted string.

### Why we don't run this in CI

Each eval costs ~20 LLM calls (responder) + ~23 LLM calls (judge: 20
scenarios + 3 gold). At ~$0.05/call that's ~$2 per run — not free, not
nightly. Run on PRs that touch:
- `.claude/skills/ue-insights-profiler/SKILL.md`
- `MCP/native/mcp_server/src/insights/csv_backend/analysis_ext.rs`
  (`compute_verdict_fields` or `get_thread_breakdown`)
- `MCP/native/mcp_server/src/insights/csv_backend/mod.rs`
  (`EmptyResponseKind`)

A grep guard in `pre-commit` or `pr_quality_gate` can prompt "run
verdict-consistency.json eval before merging" when these paths change.
