# `_verification/` — Empirical Grounding for `/image-bbox-refine`

Captures bbox-refinement-checker SubAgent output contracts + domain_gate.py
F-010 decision boundary evidence.

## Files

| File | Purpose |
|---|---|
| `op_responses.json` | Expected refinement_iter<N>.json shape per SubAgent contract (discrepancies + structural_changes + verdict) |
| `trigger_test.py` | Routing validator using SSOT skill_routing_match algorithm |
| `domain_gate_decisions.json` | F-010 gate decision matrix (UI=PROCEED, system_diagram=SKIP, concept_art=SKIP, mixed=PER-IMAGE) |

## Regeneration

```bash
python .claude/skills/image-bbox-refine/_verification/trigger_test.py
```

CI integration: `scripts/telemetry/routing_accuracy.py --skill image-bbox-refine`
(reads `evals/trigger-grading.json`).

Issue: #9998 + #9965 (Empirical Grounding).
