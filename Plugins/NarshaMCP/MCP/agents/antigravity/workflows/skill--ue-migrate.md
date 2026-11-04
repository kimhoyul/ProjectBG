---
description: "Purpose: UE version migration assistant. Scans project for breaking changes, deprecated APIs, outdated assets, and deprecated config settings. Generates comprehensive migration report and applies auto-fixes. Triggers: '버전 업그레이드', '마이그레이션', 'version upgrade', 'migrate', 'UE 버전', '엔진 업그레이드'."
source: MCP/agents/claude-code/skills/release/ue-migrate/SKILL.md
type: skill
---

# ue-migrate

Purpose: UE version migration assistant. Scans project for breaking changes, deprecated APIs, outdated assets, and deprecated config settings. Generates comprehensive migration report and applies auto-fixes. Triggers: '버전 업그레이드', '마이그레이션', 'version upgrade', 'migrate', 'UE 버전', '엔진 업그레이드'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-migrate/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- version upgrade
- migrate
- migration
- engine upgrade

## MCP Tools

- ue_analyze_config
- ue_fix_errors
- ue_build_pipeline
- ue_glob
- ue_read
