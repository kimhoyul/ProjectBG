---
description: "Purpose: refactoring safety. Answers 'Can I rename/delete this function safely?' Traces upstream callers across C++/Blueprint/Delegate with virtual override detection and breaking change assessment. Not for general code understanding (use execution-flow-explorer). Triggers: '이름 바꿔도 돼?', 'refactoring impact', '영향 범위', 'Can I rename safely?', 'delegate binding'."
source: MCP/agents/claude-code/skills/release/caller-graph-visualizer/SKILL.md
type: skill
---

# caller-graph-visualizer

Purpose: refactoring safety. Answers 'Can I rename/delete this function safely?' Traces upstream callers across C++/Blueprint/Delegate with virtual override detection and breaking change assessment. Not for general code understanding (use execution-flow-explorer). Triggers: '이름 바꿔도 돼?', 'refactoring impact', '영향 범위', 'Can I rename safely?', 'delegate binding'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/caller-graph-visualizer/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- can I rename safely
- can I safely rename
- refactoring impact analysis
- is it safe to change
- is it safe to rename
- delegate binding
- what will break if I change
- signature change impact
- delete function
- replace with
- breaking change for rename
- safe to rename

## MCP Tools

- ue_analyze_symbols
- ue_manage_blueprint
