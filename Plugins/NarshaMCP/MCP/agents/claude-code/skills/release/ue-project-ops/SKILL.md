---
name: ue-project-ops
model: opus
description: "Purpose: Comprehensive project operations dashboard combining World Partition status, DDC health monitoring, and Source Control diagnostics. Answers 'How is my project infrastructure?' with unified WP/DDC/SC status report. Triggers: 'project ops', 'infrastructure status', 'WP status', 'DDC health', 'source control', 'project dashboard', '프로젝트 운영', '인프라 상태', 'WP 상태', 'DDC 건강', '소스 컨트롤', '프로젝트 대시보드'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "(no args = full dashboard), --wp (World Partition only), --ddc (DDC only), --sc (Source Control only)"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Project Operations Dashboard

**Issue**: #5539
**Purpose**: Unified project infrastructure dashboard covering World Partition, DDC, and Source Control health

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the dashboard workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Scope**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, run the full dashboard (all 3 domains: WP, DDC, SC).
2. **Execute all applicable domain checks** sequentially
3. **Score each domain** (Healthy/Warning/Critical)
4. **Generate unified dashboard report** with overall infrastructure grade and action items

---

## Auto-Trigger Phrases

### Korean
- "프로젝트 인프라 상태", "프로젝트 운영 대시보드"
- "WP 상태 확인", "월드 파티션 상태"
- "DDC 건강 확인", "DDC 캐시 상태"
- "소스 컨트롤 상태", "SC 상태 확인"
- "프로젝트 대시보드", "인프라 점검"
- "HLOD 상태", "캐시 히트율", "변경 목록 확인"

### English
- "Project infrastructure status", "Project ops dashboard"
- "World Partition status", "WP health"
- "DDC health check", "DDC cache status"
- "Source control status", "SC diagnostics"
- "How is my project infrastructure?", "Infrastructure check"
- "HLOD status", "Cache hit rate", "Pending changelists"

---

## Scope Selection

```python
DASHBOARD_SCOPES = {
    "full":  ["wp", "ddc", "sc"],   # Default: all 3 domains
    "--wp":  ["wp"],                 # World Partition only
    "--ddc": ["ddc"],                # DDC only
    "--sc":  ["sc"],                 # Source Control only
}
```
---

## Workflow

### Step 0: Server Readiness

Call `ue_check_health()` to verify MCP server is connected and project path is valid.

**Extract from response**:
- `status` - Must be "healthy" to proceed
- `project_path` - Used as `project_root` for subsequent calls

**If unhealthy**: Stop and report connection issue. Do not proceed with domain checks.

### Step 1: World Partition Status (WP)

> Skip if scope excludes `wp`.

**Parallelization**: Steps 1A (`wp_get_status`), 1B (`wp_get_hlod_status`), 1C (`wp_list_data_layers`), and 1D (`wp_validate_cells`) are independent -- run all in parallel for faster results.

**WP-Disabled Handling**: If the project does not use World Partition (e.g., non-open-world), report: "World Partition is not enabled for this project. WP is used for large open-world maps. Your project uses standard level streaming." Skip all WP operations and mark WP as N/A.

#### 1A: Get WP Overview

```python
ue_manage_project_ops(operation="wp_get_status", project_root="<project_path>")
```text

**Extract from response**:
- WP enabled/disabled
- Grid cell count and loaded cell count
- Streaming state
- Data layer information

#### 1B: HLOD Status

```python
ue_manage_project_ops(operation="wp_get_hlod_status", project_root="<project_path>")
```text

**Extract from response**:
- HLOD build status (up-to-date / stale / missing)
- HLOD layer count
- Last build timestamp

#### 1C: Data Layers

```python
ue_manage_project_ops(operation="wp_list_data_layers", project_root="<project_path>")
```text

**Extract from response**:
- Data layer names and types
- Active/inactive layers
- Layer asset counts

#### 1D: Cell Validation (optional, if WP enabled)

```python
ue_manage_project_ops(operation="wp_validate_cells", project_root="<project_path>")
```text

**Extract from response**:
- Invalid cell count
- Validation warnings/errors

**WP Scoring**:
- Healthy: WP enabled, cells valid, HLOD up-to-date
- Warning: HLOD stale (>24h) or minor validation warnings
- Critical: WP enabled but cells invalid, HLOD missing, or errors detected
- N/A: WP not enabled in project (report as informational)

### Step 2: DDC Health (DDC)

> Skip if scope excludes `ddc`.

#### 2A: DDC Health Overview

```python
ue_manage_project_ops(operation="ddc_get_health", project_root="<project_path>")
```text

**Extract from response**:
- Overall DDC status
- Active tiers (Local, ZenLocal, Shared, Cloud, Pak, EnginePak, Root)
- Any error states

#### 2B: Cache Hit Rate

```python
ue_manage_project_ops(operation="ddc_get_hit_rate", project_root="<project_path>")
```text

**Extract from response**:
- Hit rate percentage per tier
- Total requests vs hits
- Miss rate trends

#### 2C: Cache Size Estimation

```python
ue_manage_project_ops(operation="ddc_estimate_size", project_root="<project_path>")
```
**Extract from response**:
- Estimated cache size per tier
- Disk usage warnings

#### 2D: Tier Configuration

```python
ue_manage_project_ops(operation="ddc_get_tier_config", project_root="<project_path>", tier="Local")
```
**Extract from response**:
- Tier configuration details
- Path, max size, read/write policy

#### 2E: Boot Stats

```python
ue_manage_project_ops(operation="ddc_get_boot_stats", project_root="<project_path>")
```text

**Extract from response**:
- Boot-time DDC population duration
- Assets loaded from cache vs generated

**DDC Scoring**:
- Healthy: Hit rate >80%, all tiers operational, cache size within limits
- Warning: Hit rate 50-80%, or cache nearing capacity
- Critical: Hit rate <50%, tier errors, or cache overflow

### Step 3: Source Control Status (SC)

> Skip if scope excludes `sc`.

#### 3A: SC Provider Status

```python
ue_manage_project_ops(operation="sc_get_status", project_root="<project_path>")
```text

**Extract from response**:
- Provider type (Perforce, Git, SVN, None)
- Connection status (connected/disconnected)
- Workspace/depot info

#### 3B: Pending Changes

```python
ue_manage_project_ops(operation="sc_get_opened", project_root="<project_path>")
```text

**Extract from response**:
- Number of opened/modified files
- File types breakdown (code, assets, configs)
- Files checked out by others (lock conflicts)

#### 3C: Changelist Info (if applicable)

```python
ue_manage_project_ops(operation="sc_get_changelist", project_root="<project_path>", changelist="default")
```text

**Extract from response**:
- Changelist description
- File count
- Pending vs submitted status

#### 3D: Change Impact Analysis

```python
ue_manage_project_ops(operation="sc_get_change_impact", project_root="<project_path>")
```text

**Extract from response**:
- Impacted modules/plugins
- Dependent asset count
- Risk assessment

**SC Scoring**:
- Healthy: Connected, no conflicts, pending changes under control (<50 files)
- Warning: Connected but many pending changes (50-200) or minor conflicts
- Critical: Disconnected, unresolved conflicts, or lock conflicts on critical assets

---

## Output Format

```text
=== Project Operations Dashboard ===

Project: <project_name>
Path: <project_path>
Timestamp: <current_time>

--- Overall Grade: [A/B/C/D/F] ---
Domains: 3 checked | X Healthy | Y Warning | Z Critical

--- 1. World Partition: HEALTHY / WARNING / CRITICAL / N/A ---
  Status: Enabled / Disabled
  Grid Cells: <loaded>/<total> loaded
  HLOD: Up-to-date (built <time> ago) / Stale / Missing
  Data Layers: <count> layers (<active> active)
  Cell Validation: <valid>/<total> valid
  Issues:
    - <issue description if any>

--- 2. DDC Health: HEALTHY / WARNING / CRITICAL ---
  Overall Status: Operational / Degraded / Error
  Active Tiers: <tier list>
  Hit Rate: <percentage>% (Local: X%, Shared: Y%)
  Cache Size: <size> (Limit: <limit>)
  Boot Stats: <duration>s boot cache load
  Issues:
    - <issue description if any>

--- 3. Source Control: HEALTHY / WARNING / CRITICAL ---
  Provider: Perforce / Git / SVN / None
  Connection: Connected / Disconnected
  Pending Files: <count> (<code_count> code, <asset_count> assets)
  Changelists: <count> pending
  Conflicts: <count> (lock conflicts: <lock_count>)
  Issues:
    - <issue description if any>

--- Action Items (Priority Ordered) ---
  1. [CRITICAL] <action description>
  2. [WARNING] <action description>
  3. [INFO] <action description>

--- Optimization Suggestions ---
  - [DDC] Consider running ddc_fill_cache to pre-warm cache (hit rate < 80%)
  - [WP] Rebuild HLOD: ue_manage_project_ops(operation="wp_build_hlod")
  - [SC] Reconcile workspace: ue_manage_project_ops(operation="sc_reconcile", dry_run=true)
```

### Overall Grade Calculation

| Grade | Criteria |
|-------|----------|
| A | All 3 domains Healthy |
| B | 2 Healthy, 1 Warning (or N/A) |
| C | 1+ Warning, 0 Critical |
| D | 1 Critical |
| F | 2+ Critical |

**Single-Domain Grading**: When only one domain is queried (e.g., `--wp` for WP-only), grade that domain alone on a 3-tier scale: GREEN (healthy), YELLOW (warnings), RED (critical). Skip the combined Overall Grade.

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_check_health` fails | MCP server not connected | Report connection error, provide `.mcp.json` troubleshooting steps |
| WP operations return "not enabled" | Project does not use World Partition | Mark WP as N/A, skip WP checks, continue with DDC + SC |
| DDC tier not found | Tier not configured in project | Report available tiers from `ddc_get_health`, skip missing tier details |
| SC provider "None" | No source control configured | Mark SC as N/A with recommendation to set up version control |
| SC connection timeout | Perforce server unreachable | Report last known status if cached, suggest checking P4 connection settings |
| `ddc_get_hit_rate` returns empty | No DDC activity yet (fresh project) | Report "No data - run Editor to populate DDC statistics" |
| `wp_validate_cells` timeout | Very large world with many cells | Use `timeout` parameter with higher value, or skip validation with note |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (6)** - Should activate:
1. "프로젝트 인프라 상태 확인해줘" -> Activate (full dashboard)
2. "How is my project infrastructure?" -> Activate (full dashboard)
3. "DDC 캐시 상태 확인" -> Activate (DDC scope)
4. "World Partition status" -> Activate (WP scope)
5. "소스 컨트롤 상태" -> Activate (SC scope)
6. "Project ops dashboard" -> Activate (full dashboard)

**Negative (3)** - Should NOT activate:
1. "프로젝트 건강 검진" -> Use ue-audit skill (project-wide health, not infrastructure)
2. "빌드 에러 해결해줘" -> Use ue-debug skill
3. "DDC 캐시 비워줘" -> Direct MCP call `ue_manage_project_ops(operation="ddc_clear_tier")`, not a dashboard query

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Server readiness check | 100% |
| WP status detection accuracy | >90% |
| DDC health parsing accuracy | >90% |
| SC provider detection | 100% |
| Full dashboard time | <30 seconds |

---

**Status**: Phase 1 MVP
**Related**: Issue #5539
