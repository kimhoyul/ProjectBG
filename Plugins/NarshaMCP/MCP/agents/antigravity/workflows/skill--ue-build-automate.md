---
description: "Purpose: Automate UE build/cook/package pipelines using ue_build_pipeline. Answers 'Build my project', 'Cook for Windows', 'Package for shipping', 'Run commandlet'. 12 operations: build, cook, package, BuildCookRun, commandlet, Gauntlet, list_targets, get_build_history, get_build_stats. Target auto-discovery from .Target.cs + build history tracking. Triggers: '빌드 자동화', '쿡', '패키징', '커맨드릿 실행', 'build project', 'cook', 'package', 'run commandlet', 'BuildCookRun', '빌드 이력', 'build history', '빌드 타겟'."
source: MCP/agents/claude-code/skills/release/ue-build-automate/SKILL.md
type: skill
---

# ue-build-automate

Purpose: Automate UE build/cook/package pipelines using ue_build_pipeline. Answers 'Build my project', 'Cook for Windows', 'Package for shipping', 'Run commandlet'. 12 operations: build, cook, package, BuildCookRun, commandlet, Gauntlet, list_targets, get_build_history, get_build_stats. Target auto-discovery from .Target.cs + build history tracking. Triggers: '빌드 자동화', '쿡', '패키징', '커맨드릿 실행', 'build project', 'cook', 'package', 'run commandlet', 'BuildCookRun', '빌드 이력', 'build history', '빌드 타겟'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-build-automate/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- build project
- cook
- package
- run commandlet
- BuildCookRun
- build pipeline
- Build the project

## MCP Tools

- ue_build_pipeline
