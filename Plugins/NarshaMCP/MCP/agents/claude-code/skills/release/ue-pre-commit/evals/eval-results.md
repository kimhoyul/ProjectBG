# Tier 1 Skill Evaluation Results (Issue #6978)

**Date**: 2026-03-19
**Method**: Deterministic trigger pattern matching against routing.json v1.5.0
**Evaluator**: Automated script (substring match)
**Skills Evaluated**: 5 (top 3 high-confusion + 2 additional)

## Summary

- **Skills tested**: 5
- **Pass rate**: 5/5 (100%)
- **Threshold**: F1 >= 1.0 (perfect precision + recall)

## Trigger Eval Results

| Skill | TP | FP | TN | FN | Precision | Recall | F1 | Status |
|-------|---:|---:|---:|---:|----------:|-------:|---:|--------|
| ue-pre-commit | 10/10 | 0 | 10/10 | 0 | 1.00 | 1.00 | 1.00 | PASS |
| ue-execution-trace | 10/10 | 0 | 10/10 | 0 | 1.00 | 1.00 | 1.00 | PASS |
| ue-build-automate | 10/10 | 0 | 10/10 | 0 | 1.00 | 1.00 | 1.00 | PASS |
| ue-test-gen | 10/10 | 0 | 10/10 | 0 | 1.00 | 1.00 | 1.00 | PASS |
| ue-dev-cycle | 10/10 | 0 | 10/10 | 0 | 1.00 | 1.00 | 1.00 | PASS |

## Structural Eval (C1-C6) — Top 3 Skills

| Skill | Lines | C1 YAML | C2 Tools | C3 Triggers (ko/en) | C4 Steps | C5 Output | C6 Recovery | Score | Pass |
|-------|------:|---------|----------|---------------------|----------|-----------|-------------|------:|------|
| ue-pre-commit | 422 | OK | 2/2 (1.00) | 3/3, 4/4 | 26 >= 3.2 | OK | 9 >= 3 | 6/6 | PASS |
| ue-execution-trace | 197 | OK | 1/1 (1.00) | 5/5, 5/5 | 16 >= 1.6 | OK | 4 >= 3 | 6/6 | PASS |
| ue-build-automate | 285 | OK | 1/1 (1.00) | 5/5, 6/6 | 15 >= 1.6 | OK | 7 >= 3 | 6/6 | PASS |

## Output Quality Eval Results (skill-creator full pipeline)

**Method**: skill-creator plugin — 3 prompts x 2 configs (with_skill/without_skill) per skill, live MCP (Lyra project)
**Date**: 2026-03-19
**Pipeline**: subagent spawn → timing capture → grader agent → benchmark aggregation → eval viewer HTML
**Viewer**: `*-workspace/iteration-1/eval-viewer.html` (generated via skill-creator generate_review.py)

### ue-pre-commit (iteration-1: no C++ staged, iteration-2: C++ staged)

| Iter | Eval | with_skill | without_skill | Delta |
|------|------|-----------|--------------|-------|
| 1 | staged-analysis | 3/4 (75%) | 3/4 (75%) | 0 |
| 1 | commit-safety | 2/3 (67%) | 2/3 (67%) | 0 |
| 1 | bp-impact | 2/3 (67%) | 3/3 (100%) | -33% |
| **2** | **staged-analysis-cpp** | **4/4 (100%)** | **3/4 (75%)** | **+25%** |

**Iter-1 note**: No C++ changes → both variants correctly short-circuit → ue_analyze_symbols assertion fails.
**Iter-2 key finding**: With C++ staged (TestDummy.h), skill drives 5-phase PDB workflow (15 tool calls) while baseline does static code review only (5 tool calls).

### ue-execution-trace (iteration-1: live Lyra BP tracing)

| Eval | with_skill | without_skill | Delta |
|------|-----------|--------------|-------|
| bp-flow | 3/3 (100%) | 3/3 (100%) | 0 |
| ability-flow | 3/3 (100%) | 3/3 (100%) | 0 |
| input-trace | 3/3 (100%) | 3/3 (100%) | 0 |
| **Average** | **1.00** (151s, 51K tok) | **1.00** (170s, 50K tok) | **-19s faster** |

**Finding**: Both variants achieve 100% — CLAUDE.md routing to `ue_trace_execution` is strong enough that the skill adds marginal value. Skill variant is slightly faster (151s vs 170s avg).

### ue-build-automate (iteration-1: live Lyra build/cook/commandlet)

| Eval | with_skill | without_skill | Delta |
|------|-----------|--------------|-------|
| build-project | 3/3 (100%) | 3/3 (100%) | 0 |
| cook-windows | 3/3 (100%) | 3/3 (100%) | 0 |
| commandlet | 3/3 (100%) | 3/3 (100%) | 0 |
| **Average** | **1.00** (113s, 36K tok) | **1.00** (46s, 30K tok) | **+67s slower** |

**Finding**: Both 100% pass. Skill adds target discovery + build history but takes longer. Commandlet eval surfaced a real bug: `UCUChatPanelSubsystem::Initialize()` crashes in headless mode (FSlateApplication assert).

### Summary

| Skill | with_skill avg | without_skill avg | Skill Value |
|-------|---------------|-------------------|-------------|
| ue-pre-commit (iter2) | 100% | 75% | **+25% — PDB integration** |
| ue-execution-trace | 100% | 100% | Marginal (speed) |
| ue-build-automate | 100% | 100% | Marginal (richness) |

## Confusion Matrix Notes

### High-confusion pairs validated:
1. **ue-pre-commit vs ci-verify**: "커밋 전 CI 확인" correctly routes away (CI keyword)
2. **ue-execution-trace vs ue-impact**: "What breaks if I change X?" correctly routes to ue-impact
3. **ue-build-automate vs ue-debug**: "빌드 에러 해결해줘" correctly routes to ue-debug
4. **ue-test-gen vs ue-test-scenario**: "시나리오 테스트" correctly routes to ue-test-scenario
5. **ue-dev-cycle vs full-cycle**: "풀사이클" correctly routes away via negative pattern

---

## Methodology

- **Trigger eval**: Substring matching of routing.json `trigger_patterns.ko` + `trigger_patterns.en` against test query
- **False positive prevention**: Negative patterns in routing.json checked when trigger matches
- **Structural eval**: C1-C6 rubric from mcp-skill-builder/rubric.md v1.1.0
- **Pass threshold**: F1=1.0 for trigger eval; 5/6 for structural eval
