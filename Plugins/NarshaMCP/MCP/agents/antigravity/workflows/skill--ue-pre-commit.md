---
description: "Purpose: Pre-commit safety check analyzing staged C++ changes to map impact on BPs/assets, with optional Phase 6 test-intent suggestions (#10284). 6-phase workflow: VCS scan -> function extract -> impact analysis -> cross-ref -> report -> test intent suggestion. Orchestrates git diff + ue_analyze_symbols + ue_fix_errors. Triggers: 'pre-commit', 'commit check', '커밋 전', '푸시 전 검사', 'safe to commit', 'commit impact', '커밋 영향'."
source: MCP/agents/claude-code/skills/release/ue-pre-commit/SKILL.md
type: skill
---

# ue-pre-commit

Purpose: Pre-commit safety check analyzing staged C++ changes to map impact on BPs/assets, with optional Phase 6 test-intent suggestions (#10284). 6-phase workflow: VCS scan -> function extract -> impact analysis -> cross-ref -> report -> test intent suggestion. Orchestrates git diff + ue_analyze_symbols + ue_fix_errors. Triggers: 'pre-commit', 'commit check', '커밋 전', '푸시 전 검사', 'safe to commit', 'commit impact', '커밋 영향'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-pre-commit/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- pre-commit
- commit check
- push check
- safe to commit
- safe to push
- before pushing
- intents-only

## MCP Tools

- ue_analyze_symbols
- ue_manage_blueprint
- ue_fix_errors
- ue_diff
