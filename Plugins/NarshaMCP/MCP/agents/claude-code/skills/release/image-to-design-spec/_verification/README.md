# `_verification/` — Empirical Grounding for `/image-to-design-spec`

Purpose: capture real SubAgent outputs + golden references that prove the skill
behaves as documented. Surfaces silent regressions when SubAgent contracts drift.

## Files

| File | Purpose |
|---|---|
| `op_responses.json` | Captured `image-to-spec-extractor` SubAgent output (stub form: Vision-classified components, design_intent) for V7 fixture |
| `trigger_test.py` | Static eval runner — reads `evals/trigger-grading.json` and validates routing.json word-boundary match per case |
| `golden_stage1.json` | Frozen stage1_analysis.json from V7 pilot (`wf_smoke_001`) — byte-stable comparison target |

## Regeneration

```bash
# Re-capture SubAgent output (requires live Claude Code session + image fixture)
python .claude/skills/image-to-design-spec/_verification/_capture.py \
    --image tests/fixtures/wf_smoke_001/state/wf_20260501_a1b2c3/images/img_001.png \
    --out .claude/skills/image-to-design-spec/_verification/op_responses.json

# Validate trigger eval against current routing.json
python .claude/skills/image-to-design-spec/_verification/trigger_test.py
```

## CI Integration

`scripts/telemetry/routing_accuracy.py --skill image-to-design-spec` consumes
`evals/trigger-grading.json` (NOT `_verification/`) — this directory is
forensic evidence for debugging routing drift, not the gate itself.

Issue: #9998 cluster, Empirical Grounding pattern (Issue #9965).
