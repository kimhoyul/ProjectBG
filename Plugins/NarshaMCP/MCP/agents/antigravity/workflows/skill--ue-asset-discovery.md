---
description: "Purpose: Asset reference discovery, dependency analysis, and circular dependency detection using ue_search_assets. Answers 'What references X?', 'What does X depend on?', 'Are there circular deps?'. Triggers: '에셋 참조', '에셋 의존성', '순환 참조', '레퍼런스 찾기', 'asset references', 'asset dependencies', 'circular dependencies', 'what uses this asset'."
source: MCP/agents/claude-code/skills/release/ue-asset-discovery/SKILL.md
type: skill
---

# ue-asset-discovery

Purpose: Asset reference discovery, dependency analysis, and circular dependency detection using ue_search_assets. Answers 'What references X?', 'What does X depend on?', 'Are there circular deps?'. Triggers: '에셋 참조', '에셋 의존성', '순환 참조', '레퍼런스 찾기', 'asset references', 'asset dependencies', 'circular dependencies', 'what uses this asset'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-asset-discovery/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- asset references
- asset dependencies
- circular dependencies
- what uses this asset
- reference graph

## MCP Tools

- ue_search_assets
