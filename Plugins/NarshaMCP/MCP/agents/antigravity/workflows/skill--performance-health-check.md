---
description: "Purpose: project-wide static health audit. 5-category analysis (Blueprint Health, C++ Architecture, Memory/Performance, Build Time, Epic Standards) with priority ranking and trend tracking. Not for runtime .utrace profiling (use insights-profiler). Triggers: '프로젝트 건강도', 'optimization opportunities', 'Epic standards violations', 'performance analysis', '빌드 시간 분석'."
source: MCP/agents/claude-code/skills/release/performance-health-check/SKILL.md
type: skill
---

# performance-health-check

Purpose: project-wide static health audit. 5-category analysis (Blueprint Health, C++ Architecture, Memory/Performance, Build Time, Epic Standards) with priority ranking and trend tracking. Not for runtime .utrace profiling (use insights-profiler). Triggers: '프로젝트 건강도', 'optimization opportunities', 'Epic standards violations', 'performance analysis', '빌드 시간 분석'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/performance-health-check/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- project health check
- optimization opportunities
- Epic standards violations
- Blueprint complexity
- build time analysis
- code quality metrics
- performance report
- build time
- Epic standards
- blueprint complexity
- code quality
- circular dependency
- optimization scan

## MCP Tools

- ue_glob
- ue_analyze_symbols
