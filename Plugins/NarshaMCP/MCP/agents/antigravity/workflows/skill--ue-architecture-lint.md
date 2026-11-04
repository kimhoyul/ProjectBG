---
description: "Purpose: Static architecture violation scanner for AI-generated UE C++ code. 11 rules (ARCH-001~011): inheritance depth, God Class, missing components, circular deps, Runtime/Editor xref, excessive deps, replication mismatch, GAS bypass, Tick abuse, missing UPROPERTY, unguarded FSlateApplication::Get(). Triggers: '아키텍처 린트', 'architecture lint', '구조 검사', 'design check', '코드 구조 분석'."
source: MCP/agents/claude-code/skills/release/ue-architecture-lint/SKILL.md
type: skill
---

# ue-architecture-lint

Purpose: Static architecture violation scanner for AI-generated UE C++ code. 11 rules (ARCH-001~011): inheritance depth, God Class, missing components, circular deps, Runtime/Editor xref, excessive deps, replication mismatch, GAS bypass, Tick abuse, missing UPROPERTY, unguarded FSlateApplication::Get(). Triggers: '아키텍처 린트', 'architecture lint', '구조 검사', 'design check', '코드 구조 분석'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-architecture-lint/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- architecture lint
- arch lint
- architecture check
- structure check
- design check

## MCP Tools

- ue_analyze_symbols
- ue_analyze_source
- ue_diff
- ue_grep
- ue_read
