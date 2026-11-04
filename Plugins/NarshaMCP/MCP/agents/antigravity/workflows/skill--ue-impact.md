---
description: "Purpose: Change impact analysis for UE entities. Answers 'What breaks if I change X?' by tracing callers, subclasses, Blueprint references, and config dependencies. Auto-detects target type and runs multi-tool dependency analysis. Triggers: 'impact', 'what breaks', 'change X', 'dependency', '영향 분석', '뭐가 깨져', '변경 영향', '의존성', 'who uses', 'references to', '크로스 레퍼런스', '어떤 BP가 사용', 'cross reference', 'xref', 'references to'."
source: MCP/agents/claude-code/skills/release/ue-impact/SKILL.md
type: skill
---

# ue-impact

Purpose: Change impact analysis for UE entities. Answers 'What breaks if I change X?' by tracing callers, subclasses, Blueprint references, and config dependencies. Auto-detects target type and runs multi-tool dependency analysis. Triggers: 'impact', 'what breaks', 'change X', 'dependency', '영향 분석', '뭐가 깨져', '변경 영향', '의존성', 'who uses', 'references to', '크로스 레퍼런스', '어떤 BP가 사용', 'cross reference', 'xref', 'references to'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-impact/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- impact
- breaks if
- depends on
- who uses
- references to
- xref
- affected
- who is affected

## MCP Tools

- ue_grep
- ue_read
- ue_analyze_symbols
- ue_manage_blueprint
