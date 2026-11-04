---
name: ue-level-analysis
model: opus
description: "Purpose: Level content analysis for World Partition projects. Scans OFPA actors, analyzes spatial distribution, diagnoses streaming/DataLayer issues, and generates composition graphs. Answers 'What actors are in this level?', 'Where is BP_X placed?', 'Is this level healthy?'. Triggers: '레벨 분석', '레벨 내용', '레벨 액터', '어떤 액터', '레벨 구성', 'level analysis', 'level content', 'what actors', 'level composition', 'level health', 'level density', 'spatial analysis', 'OFPA scan', 'DataLayer audit'."
compatibility: "Requires NarshaMCP MCP server (v0.9.0+). World Partition projects only (needs __ExternalActors__)."
argument-hint: "(no args = full scan + summary), --level <name> (specific level), --blueprint <bp> (BP usage), --diagnose (health check), --spatial (density analysis), --graph (Mermaid composition)"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Level Analysis — World Partition Content Scanner

**Issue**: #8564
**Purpose**: Answer "What's in this level?" by scanning OFPA actors, analyzing spatial distribution, and diagnosing streaming issues.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the analysis workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Scope**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, run a full scan + top-level summary.
2. **Execute all applicable analysis steps** using `ue_manage_project_ops` with `level_*` operations.

---

## Workflow

### Step 1: Scan (if not cached)

```
ue_manage_project_ops(operation="level_scan")
```

Reports: total actors, levels, BP classes, scan time.

### Step 2: Route by argument

| Argument | Operation | What it does |
|----------|-----------|-------------|
| `--level <name>` | `level_get_summary(level_name="<name>")` | Detailed level breakdown |
| `--blueprint <bp>` | `level_bp_usage(blueprint="<bp>")` | Where is this BP placed? |
| `--diagnose` | `level_diagnose()` | 8 diagnostic rules + 6 suggestions |
| `--spatial` | `level_spatial_analysis()` | Cell density + clustering |
| `--graph` | `level_get_graph()` | Mermaid level composition |
| `--tag <tag>` | `level_find_by_tag(tag="<tag>")` | Find actors by tag |
| `--asset <asset>` | `level_find_by_asset(asset="<asset>")` | Find actors using asset |
| (no args) | `level_list_levels(limit=20)` | Top 20 levels by actor count |

### Step 3: Present Results

Format output as:
1. **Summary table** — key metrics (actors, levels, BPs)
2. **Findings** — diagnostics/suggestions if `--diagnose`
3. **Mermaid diagram** — if `--graph`
4. **Recommendations** — based on suggestions engine

---

## Available MCP Operations (13 total)

| Operation | Stage | Purpose |
|-----------|-------|---------|
| `level_scan` | INFRA | Trigger OFPA scan |
| `level_list_levels` | FIND | List all levels sorted by actor count |
| `level_find_actors` | FIND | Filter by class/category/level/blueprint |
| `level_find_by_tag` | FIND | Search by GameplayTag |
| `level_find_by_asset` | FIND | Search by asset reference |
| `level_get_summary` | FIND | Detailed level breakdown |
| `level_get_actor_detail` | FIND | Full actor info with transform |
| `level_get_graph` | FIND | Mermaid level composition |
| `level_cross_reference` | FIND | BP↔Level bidirectional query |
| `level_bp_usage` | FIND | Where is BP placed? |
| `level_diagnose` | DIAGNOSE | 8 rules + 6 suggestions |
| `level_spatial_analysis` | ANALYZE | Cell density + clustering |
| `level_get_workflow_log` | ANALYZE | Editor transaction log |

---

## Example Prompts

- "이 프로젝트 레벨 분석해줘" → full scan + list
- "LV_Main 레벨 분석" → `--level LV_Main`
- "BP_Enemy가 어디에 배치됐어?" → `--blueprint BP_Enemy`
- "레벨 건강 상태 확인" → `--diagnose`
- "액터 밀도 분석" → `--spatial`
- "레벨 구성도 그려줘" → `--graph`

## MCP Tool Usage

```python
# Analyze level assets and actors
ue_search_assets(asset_type="level", query="LV_*")
# World Partition status — pass the operation value directly.
# The schema exposes ONLY `operation` (enum); there is no `operation_type`
# parameter, so a value passed that way is silently dropped and `operation="smart"`
# mis-routes. Use the concrete op name instead of smart auto-routing here.
ue_manage_project_ops(operation="wp_get_status")
ue_glob(pattern="Content/Levels/**/*.umap")
```
