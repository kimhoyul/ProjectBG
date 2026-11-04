---
name: ue-asset-discovery
model: opus
description: "Purpose: Asset reference discovery, dependency analysis, and circular dependency detection using ue_search_assets. Answers 'What references X?', 'What does X depend on?', 'Are there circular deps?'. Triggers: '에셋 참조', '에셋 의존성', '순환 참조', '레퍼런스 찾기', 'asset references', 'asset dependencies', 'circular dependencies', 'what uses this asset'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "asset name or path, --refs (references only), --deps (dependencies only), --circular (circular check)"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Asset Discovery

**Issue**: #6097
**Purpose**: Asset reference/dependency discovery and circular dependency detection

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements
1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what asset to analyze for references or dependencies.
2. **Detect intent**: references, dependencies, circular check, or full analysis
3. **Execute MCP tool calls** sequentially
4. **Generate structured report** with findings

---

## Auto-Trigger Phrases

### Korean
- "에셋 참조 찾기", "뭐가 이 에셋 사용해"
- "에셋 의존성 분석", "디펜던시 확인"
- "순환 참조 검사", "순환 의존성"
- "레퍼런스 그래프", "에셋 검색"

### English
- "Find asset references", "What uses this asset"
- "Asset dependencies", "Dependency graph"
- "Circular dependency check", "Circular references"
- "Reference graph", "Asset search"

---

## Workflow

### Step 1: Target Resolution

Identify the target asset from user query:

```python
# Search for the asset by name/pattern
ue_search_assets(operation="smart", query="<asset_name_or_pattern>", asset_type="<type_if_known>")
```markdown

If multiple matches, list candidates and ask for clarification.

**Argument-based step scoping** (skip unnecessary steps):
- `--deps` or query contains only "dependencies"/"의존성" → run Steps 1, 3, 5 (skip 2, 4)
- `--refs` or query contains only "references"/"참조" → run Steps 1, 2, 5 (skip 3, 4)
- `--circular` or query contains only "circular"/"순환" → run Steps 1, 4, 5 (skip 2, 3)
- Default (no flags, full analysis query) → run all Steps 1-5

### Step 2: Reference Analysis

Find what references (uses) the target asset:

```python
# Find all assets that reference target
ue_search_assets(operation="find_references", asset_path="<resolved_asset_path>")
```markdown

### Step 3: Dependency Analysis

Find what the target asset depends on:

```python
# Find all dependencies of target
ue_search_assets(operation="find_dependencies", asset_path="<resolved_asset_path>")
```markdown

### Step 4: Impact & Circular Check

Assess change impact and detect circular dependencies:

```python
# Impact analysis (what breaks if this asset changes)
ue_search_assets(operation="find_impact", asset_path="<resolved_asset_path>")

# Circular dependency detection
ue_search_assets(operation="find_circular_deps", asset_path="<resolved_asset_path>")
```markdown

**Parallelization**: Steps 2+3 (refs & deps) are independent — run in parallel.
Steps 4a+4b (impact & circular) are also independent — run in parallel.

### Step 5: Graph Statistics

Get overall reference graph metrics:

```python
# Reference count summary
ue_search_assets(operation="get_ref_counts", asset_path="<resolved_asset_path>")

# Graph-level statistics
ue_search_assets(operation="get_graph_stats")
```markdown

---

## Output Format

```text
=== Asset Discovery Report ===

Target: <asset_name> (<asset_path>)
Type: <asset_type>

--- References (N assets use this) ---
| # | Asset | Type | Path |
|---|-------|------|------|
| 1 | BP_Player | Blueprint | /Game/Blueprints/BP_Player |
| 2 | M_Character | Material | /Game/Materials/M_Character |

--- Dependencies (this depends on N assets) ---
| # | Asset | Type | Path |
|---|-------|------|------|
| 1 | SK_Mannequin | SkeletalMesh | /Game/Meshes/SK_Mannequin |

--- Impact Analysis ---
Direct dependents: N
Transitive dependents: M
Risk level: LOW/MEDIUM/HIGH

--- Circular Dependencies ---
Status: NONE DETECTED / FOUND
[If found: A → B → C → A]

--- Graph Stats ---
Total references: N
Total dependencies: M
Ref count (incoming): X
Dep count (outgoing): Y
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Asset not found | Typo or asset not indexed | `ue_search_assets(operation="search", query="*<partial>*")` with broader pattern |
| Empty references | Asset truly unused or cache stale | Report as orphan asset; suggest `ue_cache_control(get_stats)` to verify cache freshness |
| Circular check timeout | Very large dependency graph | Limit depth with targeted `find_dependencies` on subgraph |
| find_impact returns empty | Asset has no downstream dependents | Report "No impact — safe to modify" |

---

## Activation Test Cases

**Positive (4)** - Should activate:
1. "BP_Player 에셋 참조 찾아줘" -> references mode
2. "What depends on M_Rock?" -> dependencies mode
3. "Check for circular dependencies" -> circular check
4. "에셋 디펜던시 그래프 보여줘" -> full analysis

**Negative (3)** - Should NOT activate:
1. "BP_Player 구조 설명해" -> ue-explain skill
2. "에셋 검증해줘" -> ue-validate skill
3. "머티리얼 파라미터 분석" -> material-analysis skill

---

**Status**: v1.0.0
**Related**: Issue #6097
