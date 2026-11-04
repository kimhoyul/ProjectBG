---
description: "Purpose: Systematic UE debugging assistant with symptom-to-cause diagnosis. Answers 'Why is X happening?' by classifying symptoms (compile error, runtime crash, unexpected behavior, performance, build_system) and routing to optimal diagnostic tools. Medical 4-step model: Symptoms -> Diagnosis -> Root Cause -> Treatment. Triggers: 'debug', 'why is', 'error', 'crash', 'not working', '디버그', '왜 이런', '에러', '크래시', 'build error detail', 'compiler results'."
source: MCP/agents/claude-code/skills/release/ue-debug/SKILL.md
type: skill
---

# ue-debug

Purpose: Systematic UE debugging assistant with symptom-to-cause diagnosis. Answers 'Why is X happening?' by classifying symptoms (compile error, runtime crash, unexpected behavior, performance, build_system) and routing to optimal diagnostic tools. Medical 4-step model: Symptoms -> Diagnosis -> Root Cause -> Treatment. Triggers: 'debug', 'why is', 'error', 'crash', 'not working', '디버그', '왜 이런', '에러', '크래시', 'build error detail', 'compiler results'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-debug/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- error
- crash
- fix
- not working
- cannot
- incompatible
- assertion failed
- sound not playing
- audio bug
- MetaSound issue
- sound not restarting
- spatialization issue

## MCP Tools

- ue_grep
- ue_read
- ue_fix_errors
- ue_editor_debug
