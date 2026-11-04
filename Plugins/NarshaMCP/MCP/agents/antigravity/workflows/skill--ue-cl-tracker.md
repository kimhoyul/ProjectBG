---
description: "Purpose: Epic Games UnrealEngine GitHub에서 엔진 소스를 8가지 모드로 탐색한다. 모드: CL 상세, 파일 이력, 커밋 메시지 검색, 크래시 원인 추적, 트리 워크(디렉토리 구조), 콘텐츠 코드 grep, 모듈 일괄 read, 브런치 diff. Triggers: 'CL 조회', 'CL 번호', '엔진 코드 변경 이력', 'which CL', 'is this fixed', 'version history', 'backport', '디렉토리 구조', 'tree walk', '코드 grep', '엔진 코드 검색', '모듈 전체 읽기', 'branch diff', '버전 간 차이'."
source: MCP/agents/claude-code/skills/release/ue-cl-tracker/SKILL.md
type: skill
---

# ue-cl-tracker

Purpose: Epic Games UnrealEngine GitHub에서 엔진 소스를 8가지 모드로 탐색한다. 모드: CL 상세, 파일 이력, 커밋 메시지 검색, 크래시 원인 추적, 트리 워크(디렉토리 구조), 콘텐츠 코드 grep, 모듈 일괄 read, 브런치 diff. Triggers: 'CL 조회', 'CL 번호', '엔진 코드 변경 이력', 'which CL', 'is this fixed', 'version history', 'backport', '디렉토리 구조', 'tree walk', '코드 grep', '엔진 코드 검색', '모듈 전체 읽기', 'branch diff', '버전 간 차이'.

## Source

Auto-generated from `MCP/agents/claude-code/skills/release/ue-cl-tracker/SKILL.md`. Do not edit manually.
Run: `python scripts/generate_antigravity_workflows.py`

## Phase Guidance

See `.antigravity/rules.md` → Phase Behavior for plan/execute/verify restrictions.

## Triggers

- CL history
- when was X added
- which CL
- is this fixed
- version history
- regression
- backed out
- fixed in
- backport
- Perforce history
- tree walk
- browse path
- directory structure on branch
- search repo content
- code grep on repo
- github code search
- module read
- bulk read
- branch diff
- compare branches
- ue-cl-tracker
- Browse Engine/Source
- Compare branches
- on ue5-main
- related changes
- crash origin
- EpicGames UnrealEngine
- related changes find
