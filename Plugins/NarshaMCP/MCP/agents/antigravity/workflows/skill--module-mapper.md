---
description: "Purpose: .Build.cs dependency resolution. 100% accurate module lookup using UBT Manifest for linker errors and .Build.cs modifications. 3-step workflow (Query → Details → Diff). Not for general compilation error fixing (use unreal-error-doctor). Triggers: '링커 에러', 'unresolved external symbol', 'Which module is FAssetData in?', '.Build.cs modification', '모듈 의존성'."
source: MCP/agents/claude-code/skills/release/module-mapper/SKILL.md
type: skill
---

# module-mapper

Purpose: .Build.cs dependency resolution. 100% accurate module lookup using UBT Manifest for linker errors and .Build.cs modifications. 3-step workflow (Query → Details → Diff). Not for general compilation error fixing (use unreal-error-doctor). Triggers: '링커 에러', 'unresolved external symbol', 'Which module is FAssetData in?', '.Build.cs modification', '모듈 의존성'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/module-mapper/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- unresolved external symbol
- which module is
- Build.cs module dependency
- UBT Manifest
- Build.cs
- linker module missing

## MCP Tools

- ue_analyze_symbols
- ue_grep
