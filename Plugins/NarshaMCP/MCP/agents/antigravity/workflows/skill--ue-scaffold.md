---
description: "Purpose: UE feature scaffolding with architecture design + code generation. Answers 'Build feature X' by analyzing requirements, suggesting architecture, and generating C++ classes with proper UE patterns. Auto-detects feature type (Character, GAS, Weapon, UI, AI). Triggers: 'scaffold', 'create feature', 'build', 'set up', 'make', '만들어줘', '기능 생성', '구현해줘', '스캐폴드', 'new feature', 'implement'."
source: MCP/agents/claude-code/skills/release/ue-scaffold/SKILL.md
type: skill
---

# ue-scaffold

Purpose: UE feature scaffolding with architecture design + code generation. Answers 'Build feature X' by analyzing requirements, suggesting architecture, and generating C++ classes with proper UE patterns. Auto-detects feature type (Character, GAS, Weapon, UI, AI). Triggers: 'scaffold', 'create feature', 'build', 'set up', 'make', '만들어줘', '기능 생성', '구현해줘', '스캐폴드', 'new feature', 'implement'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-scaffold/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- build
- create
- scaffold
- set up

## MCP Tools

- ue_generate_code
- ue_fix_errors
- ue_grep
- ue_read
- ue_check_health
