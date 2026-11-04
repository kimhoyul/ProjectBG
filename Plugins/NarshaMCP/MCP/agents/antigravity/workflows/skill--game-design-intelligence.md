---
description: "Purpose: Generate game design intelligence documents. GAS balance analysis + AI behavior documentation (StateTree/BT/EQS). Orchestrates ue_manage_gameplay(generate_balance_report) + ue_manage_ai(generate_doc). Triggers: '밸런스 분석', 'balance analysis', '게임 디자인 문서', 'AI 행동 문서', 'game design report', 'GAS 밸런스'."
source: MCP/agents/claude-code/skills/release/game-design-intelligence/SKILL.md
type: skill
---

# game-design-intelligence

Purpose: Generate game design intelligence documents. GAS balance analysis + AI behavior documentation (StateTree/BT/EQS). Orchestrates ue_manage_gameplay(generate_balance_report) + ue_manage_ai(generate_doc). Triggers: '밸런스 분석', 'balance analysis', '게임 디자인 문서', 'AI 행동 문서', 'game design report', 'GAS 밸런스'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/game-design-intelligence/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- balance analysis
- game design report
- AI behavior doc

## MCP Tools

- ue_manage_gameplay
- ue_manage_ai
