---
name: ue-deep-research
description: "Purpose: Multi-phase deep engine research with source code evidence, CL history, and cross-verification. USE WHEN user needs: deep root cause analysis, '코드 레벨에서 원인', 'root cause and fix', '근본 원인', '소스 코드 기반 분석', 'CL 히스토리 포함', '수정 이력', '상세 분석', 'deep dive', 'source level analysis'. 4 phases: Discovery → Source Read → CL/Version → Cross-verify. NOT for: quick explanations (use ue-explain), simple error fixes (use ue-debug)."
argument-hint: ["detailed question requiring deep engine analysis"]
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Deep Research — Multi-Phase Engine Analysis

**Issue**: #6002
**Purpose**: Deep root cause analysis with source code evidence, CL history, and cross-verification

---

## Auto-Execution Instructions

> **CRITICAL**: This skill performs a **4-phase deep dive**. Each phase builds on the previous.
> Do NOT skip phases. Execute ALL 4 phases sequentially before writing your answer.
> Budget: $2.50, 15+ tool calls expected. This is NOT a quick answer skill.

### When This Skill Should Be Used (vs ue-debug/ue-explain)

| Criteria | ue-explain | ue-debug | **ue-deep-research** |
|----------|-----------|---------|---------------------|
| Tool calls | 3-6 | 3-8 | **12-20** |
| Source file reads | 0-1 | 1-2 | **3-5** |
| CL/version info | No | No | **Yes (gh api)** |
| Cross-verification | No | No | **Yes** |
| Use for | "What is X?" | "Fix this error" | **"Root cause + evidence + history"** |

---

## Phase 1: Discovery (4-5 tool calls)

Search broadly to identify all relevant entities.

```python
# 1a. Read .env for engine path
Read(".env")  # Extract UECODEGEN_ENGINE_DIR

# 1b. MCP symbol search — find related classes/functions
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")
ue_analyze_symbols(operation="search_symbols", query="<key term from question>")

# 1c. Engine source grep — search by domain
# Use ALL paths for the relevant domain (see Engine Source Paths table below)
Grep(pattern="<key function or class>", path="{engine_dir}/Engine/Source/...")

# 1d. Config search if relevant
ToolSearch("select:mcp__narshamcp__ue_analyze_config")
ue_analyze_config(operation="search_config", key="<relevant CVar or config>")

# 1e. Non-C++ files if relevant domain
Grep(pattern="<key term>", path="{engine_dir}/Engine/Shaders/Private/", glob="*.usf")
Grep(pattern="<key term>", path="{engine_dir}/Engine/Build/", glob="*.xml")
```

### Engine Source Paths by Domain

| Domain | Search Paths |
|--------|-------------|
| Audio | `Engine/Plugins/Runtime/Metasound/`, `Engine/Source/Runtime/SignalProcessing/`, `Engine/Plugins/Runtime/Metasound/Source/MetasoundFrontend/` |
| Rendering | `Engine/Source/Runtime/Renderer/`, `Engine/Shaders/Private/` |
| Build | `Engine/Source/Programs/`, `Engine/Build/Graph/` |
| Animation | `Engine/Source/Runtime/Engine/Private/Animation/`, `Engine/Source/Editor/AnimGraph/`, `Engine/Plugins/Animation/` |
| Navigation | `Engine/Source/Runtime/NavigationSystem/` |
| Physics | `Engine/Source/Runtime/Engine/Private/PhysicsEngine/`, `Engine/Source/Runtime/PhysicsCore/` |
| Sequencer | `Engine/Source/Runtime/MovieScene/`, `Engine/Source/Runtime/Engine/Private/Animation/` |
| Editor Plugins | `Engine/Plugins/Mutable/`, `Engine/Plugins/Experimental/` |

---

## Phase 2: Source Deep Read (4-6 tool calls)

Read the actual source files found in Phase 1. Trace the call chain.

```python
# 2a. Read TOP 3 most relevant source files from Phase 1
Read("<most relevant file from Phase 1 grep results>")
Read("<second relevant file>")
Read("<third relevant file>")  # if needed

# 2b. Multi-hop call chain tracing
# Found function F1 in FileA → Grep for what F1 calls
Grep(pattern="<function F1 calls>", path="{engine_dir}/Engine/...")
# Read the callee file to trace deeper
Read("<callee file>")

# 2c. Identify:
# - Root cause function (exact name, file, line number)
# - Code path (A calls B calls C)
# - Why it breaks (the specific logic/condition)
```

**CRITICAL**: Do NOT stop after finding the first file. The root cause is often **2-3 hops deep**.

---

## Phase 3: CL/Version Context (2-3 tool calls)

Search Epic GitHub for version history of the root cause function.

```python
# 3a. Search commits for the root cause function/class
Bash('gh api search/commits --method GET -f "q=<root cause function> repo:EpicGames/UnrealEngine" --jq ".items[:5] | .[] | {sha: .sha[:12], date: .commit.author.date, message: .commit.message[:200]}"')

# 3b. If commits found, get changed files for the most relevant one
Bash('gh api repos/EpicGames/UnrealEngine/commits/<sha> --jq "{message: .commit.message[:300], files: [.files[:10] | .[] | {filename, additions, deletions}]}"')

# 3c. Read current file from Epic GitHub if needed (to compare with local)
# Bash('gh api repos/EpicGames/UnrealEngine/contents/<file_path> --jq ".content" | base64 -d | head -50')
```

**Date → Version mapping**:
| Commit Date | Approximate UE Version |
|-------------|----------------------|
| 2024-01 ~ 2024-06 | UE 5.4 |
| 2024-07 ~ 2024-12 | UE 5.5 |
| 2025-01 ~ 2025-06 | UE 5.6 / 5.7 |
| 2025-07+ | UE 5.7 / 5.8 |

---

## Phase 4: Cross-Verification (2-3 tool calls)

Verify findings from a different angle.

```python
# 4a. Cross-check: Search for the root cause from a different keyword
Grep(pattern="<alternative keyword for same root cause>", path="{engine_dir}/Engine/...")

# 4b. Check if a workaround exists
ue_analyze_config(operation="search_config", key="<related CVar>")

# 4c. Verify the fix status
# If Phase 3 found a fix commit, verify it's in the current engine version
Read("<file where fix should be applied>")
# Check: does the current file contain the fix from the commit?
```

---

## Output Format

```text
=== Deep Research Report: [Topic] ===

--- Phase 1: Discovery ---
Entities found: [list of classes/functions/CVars]
Relevant files: [list with paths]
Domain: [Audio/Rendering/Build/etc.]

--- Phase 2: Root Cause Analysis ---
Root cause: [specific function, file:line]
Code path: [A::MethodX() → B::MethodY() → C::MethodZ()]
Mechanism: [why it breaks — specific logic/condition]
Evidence: [line numbers from Read(), not fabricated]

--- Phase 3: Version Context ---
Introduced in: [CL#/commit SHA, date, approximate UE version]
Recent changes: [list of commits affecting this code]
Current status: [fixed in 5.x / pending / no fix]

--- Phase 4: Verification ---
Cross-verified via: [alternative search path that confirmed same conclusion]
Workaround: [CVar, config change, or code modification]
Additional resources:
- Epic Release Notes: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-release-notes
- UDN: https://udn.unrealengine.com/
- Epic Developer Community: https://dev.epicgames.com/community/
- UE GitHub: https://www.unrealengine.com/ue-on-github
```

---

## Answer Quality Rules

1. **NEVER fabricate** — Only cite file names, function names, line numbers from tools you actually called.
2. **Multi-hop required** — Don't stop at the first file. Trace 2-3 levels deep.
3. **CL context always** — Phase 3 is not optional. Always search Epic GitHub.
4. **Cross-verify** — Phase 4 confirms Phase 2. If they contradict, investigate further.
5. **Synthesized answer** — Final output is a coherent report, not search logs.
