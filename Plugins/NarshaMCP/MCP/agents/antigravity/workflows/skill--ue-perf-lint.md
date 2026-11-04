---
description: "Purpose: Static performance anti-pattern scanner for UE C++ code. Scans Tick/BeginPlay/Constructor for PERF-001~023 rules. Triggers: '성능 린트', 'perf lint', 'anti-pattern check', 'Tick 성능 검사', '코드 성능 검사'."
source: MCP/agents/claude-code/skills/release/ue-perf-lint/SKILL.md
type: skill
---

# ue-perf-lint

Purpose: Static performance anti-pattern scanner for UE C++ code. Scans Tick/BeginPlay/Constructor for PERF-001~023 rules. Triggers: '성능 린트', 'perf lint', 'anti-pattern check', 'Tick 성능 검사', '코드 성능 검사'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-perf-lint/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- perf lint
- performance lint
- anti-pattern check
- tick performance scan
- static perf analysis

## MCP Tools

- ue_grep
- ue_read
- ue_generate_code
