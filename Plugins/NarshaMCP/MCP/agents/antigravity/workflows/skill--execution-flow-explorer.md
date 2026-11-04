---
description: "Purpose: code understanding. Bidirectional call graph — traces callers (who calls this?) AND callees (what does this call?) with Mermaid visualization and hotspot detection. Not for refactoring safety (use caller-graph-visualizer). Not for Input→Animation chains (use blueprint-flow or cross-domain-flow). Triggers: '호출 관계 보여줘', 'show call graph', 'dependency trace', '양방향 추적'."
source: MCP/agents/claude-code/skills/release/execution-flow-explorer/SKILL.md
type: skill
---

# execution-flow-explorer

Purpose: code understanding. Bidirectional call graph — traces callers (who calls this?) AND callees (what does this call?) with Mermaid visualization and hotspot detection. Not for refactoring safety (use caller-graph-visualizer). Not for Input→Animation chains (use blueprint-flow or cross-domain-flow). Triggers: '호출 관계 보여줘', 'show call graph', 'dependency trace', '양방향 추적'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/execution-flow-explorer/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- show call graph
- dependency trace
- bidirectional trace
- who calls
- what does it call
- call chain
- function internals
- call flow

## MCP Tools

- ue_analyze_symbols
