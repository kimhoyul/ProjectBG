---
name: ue-project-init
model: opus
description: "Purpose: Generate persistent project domain context files that auto-load in every Claude Code session. 3-phase scan: discovery (13 asset types + C++ classes), deep inspection (GameMode, PlayerPawn, hierarchy), context file write to .claude/CLAUDE.md. Triggers: 'project init', 'generate context', 'save project info', '프로젝트 초기화', '컨텍스트 생성', '프로젝트 도메인'."
argument-hint: "(no args = full scan), --refresh (update existing context), --dry-run (preview only)"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Project Init

**Issue**: #6390
**Purpose**: Auto-scan a UE project using MCP tools and generate **persistent context files** that Claude Code loads automatically in every future session. Eliminates repeated "what does this project do?" discovery.

**Difference from `/project-onboarding`**:
- `/project-onboarding` → ephemeral console output + tool/skill recommendations
- `/ue-project-init` → persistent `.claude/CLAUDE.md` file with project domain context

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the project init workflow automatically**.
> Do NOT just display documentation — actually call the MCP tools and write the context file!

### Execution Requirements

**Options**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, run the full 3-phase workflow.

1. Parse `$ARGUMENTS` for flags:
   - `--refresh` — Re-run all phases and update existing delimited block only. If no block exists yet, perform full scan and create it.
   - `--dry-run` — Execute all phases but output to console instead of writing files
2. Execute Phase 1 (Discovery Scan) immediately
3. Execute Phase 2 (Deep Inspection) with results from Phase 1
4. Execute Phase 3 (Write Context) — write `.claude/CLAUDE.md`
5. Report completion summary to user

---

## Auto-Trigger Phrases

### Korean
- "프로젝트 초기화", "컨텍스트 생성"
- "프로젝트 도메인", "프로젝트 정보 저장"
- "세션 컨텍스트 만들어줘"

### English
- "project init", "generate context"
- "save project info", "project domain"
- "init project context", "generate project context"

---

## MCP Tool Usage Examples

```python
# Phase 1: Health + Cache status
ue_check_health()
ue_cache_control(operation="get_stats")

# Phase 1: System detection (13 parallel calls)
ue_search_assets(asset_type="blueprint", query="*")
ue_search_assets(asset_type="gameplayability", query="*")
ue_search_assets(asset_type="NiagaraSystem", query="*")

# Phase 1: C++ architecture + source modules
ue_analyze_symbols(operation="search_symbols", query="A*", limit=30)
ue_glob(pattern="*", domain="source", limit=50)

# Phase 2: Config detection
ue_analyze_config(operation="search_config", query="DefaultPawnClass")
ue_analyze_config(operation="search_config", query="GameModeClass")

# Phase 2: GAS tags (if detected)
ue_manage_gameplay(operation="search_tags", query="*", limit=20)

# Phase 2: Naming convention detection
ue_grep(query="UCLASS", domain="source", limit=20)
```

---

## Workflow

### Phase 1: Discovery Scan

#### Step 1.1: Health Check

Call `ue_check_health()`.

**Extract**: `status`, `version`, `project_path`, `engine_path`

**Decision**:
- If NOT "healthy": **STOP** — report error with recovery guidance
- If `path_validation.status` is NOT "valid": **WARN** — proceed with fallback path

Call `ue_cache_control(operation="get_stats")`.

**Extract**: `stats.loaded`, `stats.symbols_cached`

- If `stats.loaded` is false or `stats.symbols_cached` is 0: **WARN** — "PDB index not loaded. Symbol counts may be incomplete. Re-run `/ue-project-init --refresh` after indexing."

#### Step 1.2: System Detection (13 parallel calls)

Execute `ue_search_assets` for each asset type. These are independent — **call all in parallel**.

| # | System | Call |
|---|--------|------|
| 1 | GAS (Abilities) | `ue_search_assets(asset_type="gameplayability", query="*")` |
| 2 | GAS (Effects) | `ue_search_assets(asset_type="gameplayeffect", query="*")` |
| 3 | PCG | `ue_search_assets(asset_type="pcg", query="*")` |
| 4 | Enhanced Input | `ue_search_assets(asset_type="inputaction", query="*")` |
| 5 | Niagara | `ue_search_assets(asset_type="NiagaraSystem", query="*")` |
| 6 | Materials | `ue_search_assets(asset_type="material", query="*")` |
| 7 | Blueprints | `ue_search_assets(asset_type="blueprint", query="*")` |
| 8 | Animation | `ue_search_assets(asset_type="animation", query="*")` |
| 9 | AI (StateTree) | `ue_search_assets(asset_type="statetree", query="*")` |
| 10 | AI (BehaviorTree) | `ue_search_assets(asset_type="behaviortree", query="*")` |
| 11 | Cinematics | `ue_search_assets(asset_type="levelsequence", query="*")` |
| 12 | Control Rig | `ue_search_assets(asset_type="controlrig", query="*")` |
| 13 | IK Rig | `ue_search_assets(asset_type="ikrig", query="*")` |

Record: system name, count, and up to 5 representative asset names per system.

#### Step 1.3: C++ Architecture Scan

Call `ue_analyze_symbols(operation="search_symbols", params={"query": "A*", "limit": 30})`.

**Classify**:
- 0 classes → **Blueprint-only**
- 1-10 classes → **Small C++**
- 11+ classes → **Heavy C++**

> **Note**: If PDB index is not loaded (`stats.loaded` is false from Step 1.1), this query may return only engine classes. Mark complexity as "Unknown (PDB not loaded)" and recommend re-running with `--refresh` after indexing completes.

#### Step 1.4: Source Module Structure

Call `ue_glob(pattern="*", domain="source", limit=50)`.

Extract module directory names from paths (e.g., `Source/MyGame/Characters/`, `Source/MyGame/Abilities/`).

---

### Phase 2: Deep Inspection (max 5 MCP calls)

These calls provide richer context about the project's core architecture.

#### Step 2.1: Player Character Detection

Call `ue_analyze_config(operation="search_config", params={"query": "DefaultPawnClass"})`.

Extract the configured pawn class name. If it's a Blueprint reference (e.g., `BP_ThirdPersonCharacter`), note it.

#### Step 2.2: GameMode Detection

Call `ue_analyze_config(operation="search_config", params={"query": "GameModeClass"})`.

Extract the configured game mode class name.

#### Step 2.3: Key Class Hierarchies (top 3)

For the 3 most important classes identified (e.g., the GameMode, PlayerPawn, and the most common base Actor class), call:

```
ue_analyze_symbols(operation="trace_hierarchy", params={
  "class_name": "<class>", "direction": "down", "max_depth": 2
})
```

Record short hierarchy chains (e.g., `ACharacter → AMyCharacterBase → AMyHero`).

#### Step 2.4: GAS Tag Discovery (if GAS detected)

Only if Phase 1 detected GameplayAbilities or GameplayEffects (count > 0):

Call `ue_manage_gameplay(operation="search_tags", params={"query": "*", "limit": 20})`.

Record tag hierarchy roots (e.g., `Ability.*`, `GameplayCue.*`, `InputTag.*`).

#### Step 2.5: Naming Convention Detection

Call `ue_grep(query="UCLASS", domain="source", limit=20)`.

Analyze results to detect:
- Class prefixes used (A, U, F, S, etc.)
- Module grouping patterns
- Any project-specific conventions

---

### Phase 3: Write Context File

#### Step 3.1: Generate Content

Assemble the context markdown from Phase 1 and Phase 2 results using the **Output Template** below.

**Token Budget Enforcement**:
- Overview section: max 200 tokens
- Key Classes section: max 300 tokens
- Active Systems table: max 500 tokens (only systems with count > 0, max 5 asset names per system, append `(+N more)` if truncated)
- Architecture section: max 500 tokens
- **Total ceiling: 2500 tokens** — truncate aggressively if needed

#### Step 3.2: Write to `.claude/CLAUDE.md`

**If `--dry-run`**: Print the generated content to console. Do NOT write any files. **STOP**.

**If file does not exist**: Create `.claude/CLAUDE.md` with the generated block.

**If file exists and contains `<!-- BEGIN ue-project-init` delimiter**:
- Replace everything between `<!-- BEGIN ue-project-init ... -->` and `<!-- END ue-project-init -->` with the new content.
- Preserve all content outside the delimiters.

**If file exists but has NO delimiter**:
- Prepend the generated block before existing content, separated by a blank line.

#### Step 3.3: Report to User

After writing, display a completion summary:

```
Project context written to .claude/CLAUDE.md
  - Systems detected: {N}/13
  - C++ complexity: {type}
  - Key classes: {GameMode}, {PlayerPawn}
  - Token estimate: ~{N} tokens

This context will auto-load in every future Claude Code session.
To update: /ue-project-init --refresh
```

---

## Output Template

The generated content in `.claude/CLAUDE.md`:

```markdown
<!-- BEGIN ue-project-init v1.0 (auto-generated {YYYY-MM-DD}, do not edit manually) -->
<!-- Re-generate with: /ue-project-init --refresh -->

# Project Domain Context

## Overview
- **Project**: {project_name} (UE {engine_version})
- **Path**: {project_path}
- **Type**: {Heavy C++ | Small C++ | Blueprint-only}
- **Modules**: {comma-separated module list from Source/ structure}

## Key Classes
- **GameMode**: `{AMyGameMode}` (from project config)
- **Player Pawn**: `{ABP_PlayerCharacter}` (from DefaultPawnClass)
- **Player Controller**: `{AMyPlayerController}` (if detected, otherwise omit)
- **Key Actors** ({N} project classes): `{AClass1}`, `{AClass2}`, ... (max 10)

## Active Systems ({N}/13 detected)

| System | Count | Key Assets |
|--------|-------|-----------|
| Blueprints | {N} | {BP_Player, BP_Enemy, ...} (+{M} more) |
| GAS Abilities | {N} | {GA_Fireball, GA_Jump, ...} |
| Materials | {N} | {M_Character, MI_Rock, ...} |
| Enhanced Input | {N} | {IA_Move, IA_Jump, ...} |
| ... | ... | ... |

(Only list systems with count > 0. Omit systems with 0 assets.)

## Architecture & Conventions
- **Naming**: {detected patterns, e.g., "A prefix for Actors, BP_ for Blueprints, GA_ for Abilities"}
- **Module Structure**: {Source/MyGame/Characters/, Source/MyGame/Abilities/, ...}
- **GAS Tags**: {Ability.*, GameplayCue.*, InputTag.*} (if applicable, otherwise omit)
- **Key Hierarchy**: {ACharacter -> AMyCharacterBase -> AMyHero} (max 3 chains)
<!-- END ue-project-init -->
```

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `ue_check_health` fails | MCP server not connected | **STOP** — report "MCP server not connected. Verify `narshamcp.exe` path in `.mcp.json` and restart." |
| All asset searches return 0 | Empty project or metadata not loaded | Check PDB status. If incomplete: "Indexing in progress — run `/ue-project-init --refresh` after completion." If complete: write minimal context with "New/empty project". |
| `ue_analyze_symbols` returns empty | PDB not loaded or BP-only project | Mark as "Blueprint-only", skip hierarchy tracing. |
| `ue_analyze_config` returns no results | Config key not set (using defaults) | Write "Default (engine)" for that class. |
| Individual asset type search fails | Unsupported type or server error | Skip that system, note "N/A" in report. Continue others. |
| `.claude/CLAUDE.md` write fails | Permission issue or read-only filesystem | Fall back to console output with copy-paste instructions. |
| Existing `.claude/CLAUDE.md` has complex content | User-authored content outside delimiters | Only modify the delimited block. Never touch content outside delimiters. |

---

## Evaluation Criteria

### Positive Test Cases (should activate)
- "프로젝트 초기화해줘" → `/ue-project-init`
- "Generate project context" → `/ue-project-init`
- "Save project domain info" → `/ue-project-init`
- "컨텍스트 생성해줘" → `/ue-project-init`
- "Init project context" → `/ue-project-init`

### Negative Test Cases (should NOT activate)
- "프로젝트 분석해줘" → `/project-onboarding` (ephemeral analysis, not file generation)
- "프로젝트 진단해줘" → `/ue-audit` (health diagnostic, not context generation)
- "온보딩" → `/project-onboarding`
- "프로젝트 시작해줘" → `/project-onboarding` (session orientation, not file generation)

### Quantitative Metrics
- Context file accuracy: >90% (correctly identifies present systems and key classes)
- Token budget: 1500-2500 tokens total (never exceed 3000)
- Execution time: <60 seconds (standard project), <120 seconds (large project)
- Idempotency: Running twice produces identical output (deterministic)

---

## Safety

- All MCP calls are **read-only** — no project files or assets are modified
- Only writes to `.claude/CLAUDE.md` — never modifies source code, configs, or assets
- Delimiter-based block replacement preserves all user-authored content outside markers
- `--dry-run` available to preview output without any file writes

---

## Related

- `/project-onboarding` — Ephemeral session analysis + tool recommendations (complementary, not redundant)
- `/session-health-check` — MCP connection verification only
- `/ue-audit` — Deep project health diagnostic
- Reference: [ue-llm-toolkit](https://github.com/ColtonWilley/ue-llm-toolkit) `prep one time init` pattern
- Issue #6230 — project-onboarding (related predecessor)
