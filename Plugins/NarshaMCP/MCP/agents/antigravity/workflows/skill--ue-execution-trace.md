---
description: "Purpose: Trace Blueprint/GAS execution flows using ue_trace_execution. Answers 'What happens when X fires?', 'Trace the ability flow', 'Follow input to action'. Supports BP flow, ability chains, input routing, gameplay tag propagation, and widget events. Triggers: '실행 추적', '실행 플로우', '어빌리티 플로우', '입력 추적', 'trace execution', 'execution flow', 'ability flow', 'trace from input', 'what happens when'."
source: MCP/agents/claude-code/skills/release/ue-execution-trace/SKILL.md
type: skill
---

# ue-execution-trace

Purpose: Trace Blueprint/GAS execution flows using ue_trace_execution. Answers 'What happens when X fires?', 'Trace the ability flow', 'Follow input to action'. Supports BP flow, ability chains, input routing, gameplay tag propagation, and widget events. Triggers: '실행 추적', '실행 플로우', '어빌리티 플로우', '입력 추적', 'trace execution', 'execution flow', 'ability flow', 'trace from input', 'what happens when'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-execution-trace/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- trace execution
- execution flow
- ability flow
- trace from input
- what happens when

## MCP Tools

- ue_trace_execution
