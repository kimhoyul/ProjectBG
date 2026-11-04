---
description: "Purpose: Cross-check and mismatch detection for UE projects. Finds gaps between declarations and registrations, class hierarchy deltas, config override cascades, and binary asset diffs. Triggers: 'replication audit', 'config diff', 'config chain', 'diff hierarchy', 'class delta', 'compare configs', 'override cascade', 'compare assets', 'asset diff', 'uasset diff', '리플리케이션 감사', '설정 비교', '계층 비교', '클래스 차이', '에셋 비교', '에셋 차이'."
source: MCP/agents/claude-code/skills/release/ue-diff/SKILL.md
type: skill
---

# ue-diff

Purpose: Cross-check and mismatch detection for UE projects. Finds gaps between declarations and registrations, class hierarchy deltas, config override cascades, and binary asset diffs. Triggers: 'replication audit', 'config diff', 'config chain', 'diff hierarchy', 'class delta', 'compare configs', 'override cascade', 'compare assets', 'asset diff', 'uasset diff', '리플리케이션 감사', '설정 비교', '계층 비교', '클래스 차이', '에셋 비교', '에셋 차이'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-diff/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- replication cross-check
- config diff
- config chain
- diff hierarchy
- class delta
- compare configs
- override cascade
- compare assets
- asset diff
- uasset diff

## MCP Tools

- ue_diff
- ue_grep
