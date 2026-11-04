---
description: "Purpose: Auto-diagnose and retry Live Coding (Hot Reload) failures. 5-step workflow (Status Check -> Error Extract -> Diagnose -> Fix Apply -> Retry Compile). Orchestrates ue_editor_automation + ue_fix_errors for seamless compile-fix-retry loop. UE 5.8 LiveCodingToolset awareness — coexists with Epic's raw CompileLiveCoding() MCP tool. Triggers: 'live coding 실패', 'hot reload 에러', '핫 리로드', 'live coding fix', '라이브 코딩', 'hot reload failed', 'live coding error', 'patch failed'."
source: MCP/agents/claude-code/skills/release/ue-livecoding-fix/SKILL.md
type: skill
---

# ue-livecoding-fix

Purpose: Auto-diagnose and retry Live Coding (Hot Reload) failures. 5-step workflow (Status Check -> Error Extract -> Diagnose -> Fix Apply -> Retry Compile). Orchestrates ue_editor_automation + ue_fix_errors for seamless compile-fix-retry loop. UE 5.8 LiveCodingToolset awareness — coexists with Epic's raw CompileLiveCoding() MCP tool. Triggers: 'live coding 실패', 'hot reload 에러', '핫 리로드', 'live coding fix', '라이브 코딩', 'hot reload failed', 'live coding error', 'patch failed'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-livecoding-fix/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- live coding
- hot reload failed
- patch failed

## MCP Tools

- ue_editor_automation
- ue_fix_errors
