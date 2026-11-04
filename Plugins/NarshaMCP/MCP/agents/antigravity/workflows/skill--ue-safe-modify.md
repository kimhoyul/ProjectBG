---
description: "Purpose: AI 변경 전 자동 스냅샷 + 검증 실패 시 원클릭 롤백. 5 modes: start(스냅샷), verify(검증), commit(확정), rollback(복원), auto(전자동). Git + filesystem dual-layer backup for C++/BP/Config. Triggers: '안전하게 수정', '롤백', '되돌려', 'safe modify', 'with rollback', 'undo changes', 'safely change', '안전장치', '백업하고 수정'."
source: MCP/agents/claude-code/skills/release/ue-safe-modify/SKILL.md
type: skill
---

# ue-safe-modify

Purpose: AI 변경 전 자동 스냅샷 + 검증 실패 시 원클릭 롤백. 5 modes: start(스냅샷), verify(검증), commit(확정), rollback(복원), auto(전자동). Git + filesystem dual-layer backup for C++/BP/Config. Triggers: '안전하게 수정', '롤백', '되돌려', 'safe modify', 'with rollback', 'undo changes', 'safely change', '안전장치', '백업하고 수정'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-safe-modify/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- safe modify
- safely modify
- snapshot before
- with rollback
- rollback changes
- undo AI changes
- safely change
- safe mode modify
- backup before change

## MCP Tools

- ue_fix_errors
- ue_manage_blueprint
- ue_analyze_config
