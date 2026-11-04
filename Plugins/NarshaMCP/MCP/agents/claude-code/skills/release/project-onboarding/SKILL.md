---
name: project-onboarding
model: opus
description: "Purpose: Auto-analyze UE project and recommend relevant MCP tools/skills. 4-phase workflow: server health, system detection (13 asset types), C++ architecture sampling, personalized recommendations. Triggers: 'onboarding', 'project analysis', 'get started', 'what can you do', '온보딩', '프로젝트 분석', '프로젝트 시작', '뭘 할 수 있어', '도구 추천', '첫 세션'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "(no args = full onboarding), --quick (health + detect only, skip C++ sampling), --self-test (validate skill structure)"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Project Onboarding

**Issue**: #6230
**Purpose**: First-session project analysis — detect which UE systems the project uses and recommend relevant MCP tools and skills.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the onboarding workflow automatically**.
> Do NOT just display documentation — actually call the MCP tools and generate the report!

### Execution Requirements

**Options**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, run the full onboarding workflow (all 4 phases).

1. Parse `$ARGUMENTS` for flags (e.g., `--quick` to skip Phase 3)
2. Execute Phase 1 (Server & Environment Check) immediately
3. If server is healthy, proceed to Phase 2 (System Detection)
4. Unless `--quick` is specified, execute Phase 3 (C++ Architecture Sample)
5. Generate Phase 4 recommendations based on detected systems
6. Present the full Onboarding Report to the user

---

## Auto-Trigger Phrases

### Korean
- "온보딩", "프로젝트 분석해줘"
- "프로젝트 시작", "첫 세션"
- "뭘 할 수 있어?", "도구 추천"
- "어떤 도구 써야 해?", "프로젝트 소개"

### English
- "onboarding", "project analysis"
- "get started", "first session"
- "what can you do?", "tool recommendations"
- "project setup", "analyze my project"

---

## Workflow

### Phase 1: Server & Environment Check

Call `ue_check_health()` with no arguments.

**Extract from response**:
- `status` — Should be "healthy"
- `version` — Server version
- `project_path` — Configured UE project path
- `engine_path` — Configured engine path
- `path_validation.status` — Should be "valid"

Call `ue_cache_control(operation="get_stats")`.

**Extract from response**:
- `symbol_index.loaded` — PDB symbols available?
- `symbol_index.symbol_count` — How many symbols indexed
- `metadata.status` — Asset metadata loaded?
- `four_phase.complete` — Indexing finished?
- `four_phase.running` — Still indexing?

**Decision**:
- If server status is NOT "healthy": **STOP** — report connection error with recovery guidance. Do not proceed to Phase 2.
- If `path_validation.status` is NOT "valid": **WARN** — report path issue (e.g., `"path_not_found"`, `"not_ue_project"`). The server may fall back to its configured default project path. Proceed to Phase 2 using the fallback `project_path` from the response.
- If PDB indexing is incomplete (`four_phase.running=true`): **WARN** — proceed but note "Asset counts may be incomplete — indexing in progress. Re-run after indexing completes for accurate results."

### Phase 2: System Detection

Execute `ue_search_assets` for each of the 13 UE system categories below. Record the count of assets found for each.

**IMPORTANT**: These calls are independent — execute them in parallel (multiple `ue_search_assets` tool calls in a single message) for better performance.

| # | System | Asset Type | Search Call |
|---|--------|-----------|-------------|
| 1 | GAS (Abilities) | `gameplayability` | `ue_search_assets(asset_type="gameplayability", query="*")` |
| 2 | GAS (Effects) | `gameplayeffect` | `ue_search_assets(asset_type="gameplayeffect", query="*")` |
| 3 | PCG | `pcg` | `ue_search_assets(asset_type="pcg", query="*")` |
| 4 | Enhanced Input | `inputaction` | `ue_search_assets(asset_type="inputaction", query="*")` |
| 5 | Niagara | `NiagaraSystem` | `ue_search_assets(asset_type="NiagaraSystem", query="*")` |
| 6 | Materials | `material` | `ue_search_assets(asset_type="material", query="*")` |
| 7 | Blueprints | `blueprint` | `ue_search_assets(asset_type="blueprint", query="*")` |
| 8 | Animation | `animation` | `ue_search_assets(asset_type="animation", query="*")` |
| 9 | AI (StateTree) | `statetree` | `ue_search_assets(asset_type="statetree", query="*")` |
| 10 | AI (BehaviorTree) | `behaviortree` | `ue_search_assets(asset_type="behaviortree", query="*")` |
| 11 | Cinematics | `levelsequence` | `ue_search_assets(asset_type="levelsequence", query="*")` |
| 12 | Control Rig | `controlrig` | `ue_search_assets(asset_type="controlrig", query="*")` |
| 13 | IK Rig | `ikrig` | `ue_search_assets(asset_type="ikrig", query="*")` |

**Interpretation**:
- Count > 0 → System is **detected** (mark with ✅)
- Count = 0 AND indexing complete → System is **not used** (mark with ❌)
- Count = 0 AND indexing incomplete → System is **unknown** (mark with ❓, add note)
- If a specific asset type call fails → Mark as **N/A**, continue with remaining types

**IMPORTANT**: `query="*"` may return 0 if metadata cache is not fully loaded. When PDB indexing is incomplete, never assert "system not used" — use "unknown" instead.

### Phase 3: C++ Architecture Sample

> Skip this phase if `--quick` was specified.

Call `ue_analyze_symbols(operation="search_symbols", params={"query": "A*", "limit": 20})`.

**Extract from response**:
- List of Actor class names found
- Count of unique classes

**Interpretation**:
- 0 classes: **Blueprint-only project** — focus recommendations on BP tools
- 1–10 classes: **Small C++ project** — basic C++ + BP workflow
- 11+ classes: **Heavy C++ project** — recommend advanced C++ tools (hierarchy tracing, refactoring, test generation)

### Phase 4: Recommendation Engine

Map detected systems to relevant MCP tools and skills using this table:

| Detected System | Recommended Skills | Recommended MCP Tools |
|-----------------|-------------------|----------------------|
| GAS (ability or effect) | `/gas-development`, `/ue-execution-trace` | `ue_manage_gameplay(operation="smart", ...)` |
| PCG | `/pcg-workflow` | `ue_manage_pcg(operation="smart", ...)` |
| Niagara | — | `ue_manage_niagara(operation="smart", ...)` |
| Materials | `/material-analysis` | `ue_manage_material(operation="smart", ...)` |
| AI (StateTree or BT) | `/game-design-intelligence` | `ue_manage_ai(operation="smart", ...)` |
| Blueprints | `/blueprint-flow` | `ue_manage_blueprint(operation="smart", ...)` |
| Enhanced Input | `/ue-execution-trace` | `ue_manage_gameplay(operation="smart", tag="InputTag.*")` |
| Level Sequences | `/ue-sequencer-cinematics` | `ue_sequencer_structure(operation="smart", ...)` |
| Control Rig | `/control-rig-workflow` | `ue_manage_rigging(operation="smart", rig_type="control_rig", ...)` |
| IK Rig | `/ue-ik-rig-analysis` | `ue_manage_rigging(operation="smart", rig_type="ik_rig", ...)` |
| Animation | `/animation-workflow` | `ue_search_assets(asset_type="animation", ...)` |
| Always (any project) | `/ue-debug`, `/ue-audit`, `/ue-pre-commit` | `ue_fix_errors(operation="smart", ...)` |

**C++ Complexity adjustments** (from Phase 3):
- If 11+ Actor classes: additionally recommend `ue_analyze_symbols(trace_hierarchy)`, `/ue-cpp-refactor`, `/ue-test-gen`
- If 0 Actor classes (BP-only): additionally recommend `/bp-cpp-project-init` for migration option

**Minimal project** (0–1 systems detected):
- Output: "New or minimal project detected"
- Recommend: `/ue-scaffold` to start building features

**Skill availability check**: When recommending skills, check if they are installed by verifying the skill directory exists: `Glob('.claude/skills/<skill-name>/SKILL.md')`. Mark installed skills as "Ready" and marketplace-only skills as "Install: `/mcp-skill-builder install <name>`".

**Generate 3 "Try it now" commands**: Pick the top 3 most relevant MCP tool calls based on detected systems, formatted as copy-pasteable commands the user can run immediately. If Phase 2 did not detect a specific asset name for a placeholder (e.g., `BP_{main_character}`), use a generic example such as `BP_ThirdPersonCharacter` (Lyra default) or `BP_Player`, and note to the user: "Replace with your project's actual asset name."

---

## Output Format

```
=== Project Onboarding Report ===

--- 1. Environment ---
  Server:   v{version} ({status})
  Project:  {project_path} (UE {engine_version})
  PDB:      {symbol_count} symbols loaded ({pdb_status})
  Metadata: {metadata_status}

--- 2. Detected Systems ({detected_count}/13) ---
  [✅] GAS (GameplayAbility)    — {count} abilities, {count} effects
  [✅] Enhanced Input            — {count} input actions
  [✅] Blueprints                — {count} blueprints
  [✅] Materials                 — {count} materials
  [❌] PCG
  [❌] Niagara
  [❌] AI (StateTree/BehaviorTree)
  [❌] Level Sequences
  [❌] Control Rig / IK Rig
  [❌] Animation

--- 3. C++ Architecture ---
  Actor classes found: {count}
  Key classes: {class_list}
  Complexity: {Blueprint-only | Small C++ | Heavy C++}

--- 4. Recommended Tools & Skills ---

  Priority 1 — Detected Systems:
    /gas-development          — GAS ability workflow orchestration
    ue_manage_gameplay(smart) — Tag/ability/input queries
    /material-analysis        — Material structure analysis

  Priority 2 — Always Useful:
    /ue-debug                 — Error resolution
    /ue-audit                 — Full project health check
    ue_fix_errors(smart)      — Auto error fixing

  Priority 3 — C++ Tools:
    ue_analyze_symbols(trace_hierarchy) — Class inheritance
    /ue-pre-commit            — Pre-commit safety check

--- Try It Now ---
  1. ue_manage_gameplay(operation="smart", tag="Ability.*")
  2. ue_manage_blueprint(operation="smart", blueprint_name="BP_{main_character}")
  3. ue_fix_errors(operation="smart", project_root="{project_path}")
```text

**`--quick` mode**: Omit Section 3 (C++ Architecture) entirely. Replace with:
```
--- 3. C++ Architecture ---
  ⏭️ Skipped (--quick mode)
```

---

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `ue_check_health` fails or returns error | MCP server not connected or binary crashed | Report "MCP server not connected. Verify `narshamcp.exe` path in `.mcp.json` and restart Claude Code." Stop onboarding — do not proceed to Phase 2. |
| `path_validation.status` is `"path_not_found"` or `"not_ue_project"` | Invalid or non-UE project path | Warn user about path issue. Server falls back to configured default — proceed using `project_path` from response. |
| All asset searches return 0 | Empty project or metadata not loaded | Check PDB status from Phase 1. If indexing incomplete: "Indexing in progress — re-run `/project-onboarding` after completion." If complete and truly 0: "New project detected — consider `/ue-scaffold` to build features." |
| `ue_analyze_symbols` returns empty | PDB not loaded or no C++ classes | Report "No C++ classes found — this appears to be a Blueprint-only project." Skip Phase 3, proceed to Phase 4 with BP-focused recommendations. |
| Individual asset type search fails | Unsupported asset type or server error | Mark that system as "N/A" in the report. Continue scanning remaining types. |
| Timeout on large project (>100K assets) | Too many assets to enumerate | Suggest `--quick` mode (skips Phase 3). Note that per-type counts may be truncated. |

---

## Evaluation Criteria

### Positive Test Cases (should activate)
- "프로젝트 분석해줘" → `/project-onboarding`
- "온보딩" → `/project-onboarding`
- "뭘 할 수 있어?" → `/project-onboarding`
- "What can you do for my project?" → `/project-onboarding`
- "Get started with this project" → `/project-onboarding`

### Negative Test Cases (should NOT activate)
- "프로젝트 진단해줘" → `/ue-audit` (diagnostic, not onboarding)
- "DDC 상태 확인" → `/ue-project-ops` (infrastructure, not orientation)
- "캐릭터 만들어줘" → `/ue-scaffold` (feature creation, not analysis)

### Quantitative Metrics
- System detection accuracy: >90% (correctly identifies present/absent systems)
- Tool recommendation relevance: >85% (recommended tools match detected systems)
- Total execution time: <45 seconds (standard project), <90 seconds (large project). Times are estimates — very large projects (>100K assets) may take longer.
- `--quick` mode time: <30 seconds

---

## Related

- `/session-health-check` — Phase 1 is a superset of this skill's output
- `/ue-audit` — Deeper project health diagnostic (not first-session orientation)
- `/ue-scaffold` — Feature scaffolding (recommended by onboarding for new projects)
- Issue #6229 — [Epic] Workflow Recipes (parent)
- [REFERENCE.md](REFERENCE.md) — Phase details, asset type table, error recovery
- [EXAMPLES.md](EXAMPLES.md) — 3 complete walkthrough scenarios

---

## Self-Test

`/project-onboarding --self-test` validates skill integrity without running the full workflow:

| # | Check | What |
|---|-------|------|
| 1 | Structure | SKILL.md, REFERENCE.md, EXAMPLES.md exist |
| 2 | Frontmatter | Required fields: `name`, `description`, `argument-hint`, `metadata.version` |
| 3 | Routing | `routing.json` entry exists with matching trigger patterns |
| 4 | Asset types | All 13 asset types in Phase 2 table are valid `ue_search_assets` types |
| 5 | Recommendation table | Every detected system maps to at least one skill or MCP tool |

**Output**: PASS / FAIL with per-check results. No MCP tool calls are made.
## MCP Tool Examples

```python
# Check server health during onboarding
ue_check_health(compact=True)
# Review cache statistics
ue_cache_control(operation="get_stats")
# Discover project assets
ue_search_assets(asset_type="blueprint", query="BP_*")
# Analyze key project symbols
ue_analyze_symbols(operation="smart", query="AGameMode")
```
