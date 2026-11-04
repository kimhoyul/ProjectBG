---
description: "Purpose: Comprehensive UE project health audit with 6-point diagnostic. Checks server health, config consistency, asset quality, performance, code quality, and message log diagnostics. Answers 'Is this system healthy?' with scored report and prioritized recommendations. Triggers: 'audit', 'health check', 'project health', '감사', '건강 검진', '프로젝트 상태', 'system check', 'quality check', 'validate project', '경고 요약', '메시지 로그', 'message log', 'auto fix warnings'."
source: MCP/agents/claude-code/skills/release/ue-audit/SKILL.md
type: skill
---

# ue-audit

Purpose: Comprehensive UE project health audit with 6-point diagnostic. Checks server health, config consistency, asset quality, performance, code quality, and message log diagnostics. Answers 'Is this system healthy?' with scored report and prioritized recommendations. Triggers: 'audit', 'health check', 'project health', '감사', '건강 검진', '프로젝트 상태', 'system check', 'quality check', 'validate project', '경고 요약', '메시지 로그', 'message log', 'auto fix warnings'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-audit/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- audit
- health check
- diagnostic
- warning summary
- message log
- healthy
- auto fix warnings

## MCP Tools

- ue_check_health
- ue_editor_debug
- ue_search_assets
- ue_analyze_config
- ue_diff
