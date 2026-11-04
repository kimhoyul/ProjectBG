---
description: "Purpose: One-shot UE Automation Test code generation from target class/function. Uses ue_generate_code(generate_test) for template-based test creation with correct macros, assertions, and flags. Fast single-pass alternative to ue-automation-test-builder. Triggers: '테스트 생성', '테스트 만들어줘', 'generate test for', 'create test for', '유닛 테스트 생성'."
source: MCP/agents/claude-code/skills/release/ue-test-gen/SKILL.md
type: skill
---

# ue-test-gen

Purpose: One-shot UE Automation Test code generation from target class/function. Uses ue_generate_code(generate_test) for template-based test creation with correct macros, assertions, and flags. Fast single-pass alternative to ue-automation-test-builder. Triggers: '테스트 생성', '테스트 만들어줘', 'generate test for', 'create test for', '유닛 테스트 생성'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-test-gen/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- generate test
- create test
- tests from intents
- generate from intents
- from-intents
- intent-based tests

## MCP Tools

- ue_generate_code
- ue_analyze_symbols
