---
description: "Purpose: Comprehensive UE Insights profiling analysis. Answers 'Why is my game slow?' by analyzing .utrace files, identifying hot functions, detecting performance regressions, and providing optimization suggestions. Orchestrates ue_analyze_insights for CPU/GPU bottleneck identification. Triggers: '성능 분석', '프로파일링', '왜 느려', 'FPS 분석', 'utrace 분석', 'performance profiling', 'bottleneck analysis', 'why is my game slow', 'CPU/GPU analysis', 'hot function', 'trace analysis'."
source: MCP/agents/claude-code/skills/release/ue-insights-profiler/SKILL.md
type: skill
---

# ue-insights-profiler

Purpose: Comprehensive UE Insights profiling analysis. Answers 'Why is my game slow?' by analyzing .utrace files, identifying hot functions, detecting performance regressions, and providing optimization suggestions. Orchestrates ue_analyze_insights for CPU/GPU bottleneck identification. Triggers: '성능 분석', '프로파일링', '왜 느려', 'FPS 분석', 'utrace 분석', 'performance profiling', 'bottleneck analysis', 'why is my game slow', 'CPU/GPU analysis', 'hot function', 'trace analysis'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-insights-profiler/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- performance profiling
- bottleneck
- hot function
- utrace
- frame drop
- loading hitch
- log spam
- rpc replication
- disk read
- string pool
- session info
- dll trace

## MCP Tools

- ue_analyze_insights
