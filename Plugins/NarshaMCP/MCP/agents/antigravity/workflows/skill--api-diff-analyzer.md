---
description: "Purpose: UE API change analyzer for version migration. Generates breaking changes and migration guides. Triggers on 'API 변경점', 'migration guide', 'breaking changes'."
source: MCP/agents/claude-code/skills/release/api-diff-analyzer/SKILL.md
type: skill
---

# api-diff-analyzer

Purpose: UE API change analyzer for version migration. Generates breaking changes and migration guides. Triggers on 'API 변경점', 'migration guide', 'breaking changes'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/api-diff-analyzer/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- API diff
- API changes
- version migration analysis
- API_CHANGES.md
- engine API differences

## MCP Tools

- ue_grep
- ue_read
- ue_analyze_symbols
