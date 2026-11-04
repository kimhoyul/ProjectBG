---
description: "Purpose: Universal UE system explainer with auto-domain routing. Answers 'How does X work?' for any UE entity (C++ class, Blueprint, Material, Niagara, GAS, Config, AI, PCG, Sequencer). Auto-detects target type from name prefix and routes to optimal MCP tools. Triggers: 'explain', 'how does', 'what is', '설명해', '어떻게 동작', '구조 분석', 'show me how', 'tell me about'."
source: MCP/agents/claude-code/skills/release/ue-explain/SKILL.md
type: skill
---

# ue-explain

Purpose: Universal UE system explainer with auto-domain routing. Answers 'How does X work?' for any UE entity (C++ class, Blueprint, Material, Niagara, GAS, Config, AI, PCG, Sequencer). Auto-detects target type from name prefix and routes to optimal MCP tools. Triggers: 'explain', 'how does', 'what is', '설명해', '어떻게 동작', '구조 분석', 'show me how', 'tell me about'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-explain/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- explain
- how does
- how to
- what is
- what causes
- why does
- tell me about

## MCP Tools

- ue_grep
- ue_read
- ue_analyze_symbols
- ue_manage_blueprint
- ue_manage_material
