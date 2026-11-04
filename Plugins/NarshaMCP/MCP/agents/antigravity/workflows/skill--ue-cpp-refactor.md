---
description: "Purpose: Safe C++ refactoring operations using ue_generate_code refactoring ops. Rename classes/functions with CoreRedirects, extract methods, change signatures, introduce fields, inline, and safe delete. All operations support dry_run preview. Triggers: '리팩터링', '리네임', '클래스 이름 변경', '함수 추출', 'refactor', 'rename class', 'rename function', 'extract method', 'change signature', 'safe delete'."
source: MCP/agents/claude-code/skills/release/ue-cpp-refactor/SKILL.md
type: skill
---

# ue-cpp-refactor

Purpose: Safe C++ refactoring operations using ue_generate_code refactoring ops. Rename classes/functions with CoreRedirects, extract methods, change signatures, introduce fields, inline, and safe delete. All operations support dry_run preview. Triggers: '리팩터링', '리네임', '클래스 이름 변경', '함수 추출', 'refactor', 'rename class', 'rename function', 'extract method', 'change signature', 'safe delete'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-cpp-refactor/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- refactor
- rename class
- rename function
- extract method
- change signature
- safe delete

## MCP Tools

- ue_generate_code
