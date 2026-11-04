---
description: "Purpose: Sequencer cinematics workflow orchestration. Analyze, build, and debug Level Sequence cinematics using 4 sequencer sub-tools (107 ops). Structure analysis, track management, keyframe editing, camera cuts, and playback control. Triggers: 'cinematics', 'sequencer workflow', '시네마틱', '시퀀서 워크플로우', 'camera cut', 'level sequence', '레벨 시퀀스'."
source: MCP/agents/claude-code/skills/release/ue-sequencer-cinematics/SKILL.md
type: skill
---

# ue-sequencer-cinematics

Purpose: Sequencer cinematics workflow orchestration. Analyze, build, and debug Level Sequence cinematics using 4 sequencer sub-tools (107 ops). Structure analysis, track management, keyframe editing, camera cuts, and playback control. Triggers: 'cinematics', 'sequencer workflow', '시네마틱', '시퀀서 워크플로우', 'camera cut', 'level sequence', '레벨 시퀀스'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-sequencer-cinematics/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- cinematics
- sequencer workflow
- level sequence
- camera cut

## MCP Tools

- ue_sequencer_structure
- ue_sequencer_tracks
- ue_sequencer_keyframes
- ue_sequencer_playback
