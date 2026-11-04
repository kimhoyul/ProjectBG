---
description: "Purpose: UE compilation error resolution. 4-step medical workflow (Diagnosis → Analysis → Preview → Apply) with multi-tool escalation for complex errors (symbol + config analysis). Handles 95%+ common errors AND complex cross-domain issues. Triggers: '빌드 에러', 'compile error', 'linker error', 'symbol not found', 'config mismatch', 'UPROPERTY error'."
source: MCP/agents/claude-code/skills/release/unreal-error-doctor/SKILL.md
type: skill
---

# unreal-error-doctor

Purpose: UE compilation error resolution. 4-step medical workflow (Diagnosis → Analysis → Preview → Apply) with multi-tool escalation for complex errors (symbol + config analysis). Handles 95%+ common errors AND complex cross-domain issues. Triggers: '빌드 에러', 'compile error', 'linker error', 'symbol not found', 'config mismatch', 'UPROPERTY error'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/unreal-error-doctor/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- compile error
- build error
- build failed
- linker error
- header error
- UPROPERTY error
- UCLASS error
- fix build errors
- resolve errors

## MCP Tools

- ue_fix_errors
- ue_build_pipeline
- ue_editor_automation
