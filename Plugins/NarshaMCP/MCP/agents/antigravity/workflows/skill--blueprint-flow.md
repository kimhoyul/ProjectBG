---
description: "Purpose: Blueprint execution flow. Structure analysis + execution tracing with dual methods (step-by-step, or a single atomic workflow shortcut). Input→Animation, BP structure, ability flow. For GAS/InputTag deep chain (5-stage), use cross-domain-flow. Not for creating new BPs (use blueprint-generator). Triggers: 'BP 실행 흐름', 'BP 분석', 'animation trace', '버튼 누르면', 'Blueprint 구조'."
source: MCP/agents/claude-code/skills/release/blueprint-flow/SKILL.md
type: skill
---

# blueprint-flow

Purpose: Blueprint execution flow. Structure analysis + execution tracing with dual methods (step-by-step, or a single atomic workflow shortcut). Input→Animation, BP structure, ability flow. For GAS/InputTag deep chain (5-stage), use cross-domain-flow. Not for creating new BPs (use blueprint-generator). Triggers: 'BP 실행 흐름', 'BP 분석', 'animation trace', '버튼 누르면', 'Blueprint 구조'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/blueprint-flow/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- BP execution flow
- blueprint structure
- BP analysis
- BP graph
- blueprint flow
- what triggers when
- node execution order
- BP internals

## MCP Tools

- ue_manage_blueprint
- ue_trace_execution
