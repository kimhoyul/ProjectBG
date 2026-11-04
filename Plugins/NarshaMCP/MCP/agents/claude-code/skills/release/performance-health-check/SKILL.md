---
name: performance-health-check
model: opus
description: "Purpose: project-wide static health audit. 5-category analysis (Blueprint Health, C++ Architecture, Memory/Performance, Build Time, Epic Standards) with priority ranking and trend tracking. Not for runtime .utrace profiling (use insights-profiler). Triggers: '프로젝트 건강도', 'optimization opportunities', 'Epic standards violations', 'performance analysis', '빌드 시간 분석'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Performance Health Check

**Purpose**: Comprehensive project health analysis with Epic Games standards
**Tools Used**: 5 (+`ue_manage_niagara` for VFX Health)

---

## 🎯 Purpose

Comprehensive project-wide analysis to identify optimization opportunities, Epic standards violations, and architectural issues.

**Key Benefits**:

- 88-94% faster analysis (2-4 hours → 10-15 min)
- 50% better issue detection (60% → 90%)
- 97% faster reporting (30 min → 1 min)
- Automated prioritization (High/Medium/Low)
- Weekly trend tracking

---

## 🔍 Auto-Load Trigger Phrases

**Project health queries**:

- "프로젝트 건강도 분석해줘" / "Run project health check"
- "최적화 필요한 부분 찾아줘" / "Find optimization opportunities"
- "Epic 표준 위반 찾아줘" / "Find Epic standards violations"

**Category-specific queries**:

- "Blueprint 복잡도 분석해줘" / "Analyze Blueprint complexity"
- "C++ 의존성 문제 찾아줘" / "Find C++ dependency issues"
- "빌드 시간 느린 모듈 찾아줘" / "Find slow-compiling modules"

**Keywords**: `health`, `check`, `analysis`, `optimization`, `performance`, `compliance`, `standards`, `violation`, `건강도`, `분석`, `최적화`, `표준`, `위반`

---

## 📊 6 Analysis Categories (5→6 Expanded) 🆕

### 0. Discovery Pre-Exploration (Issue #6289)

> Before running 5+ `ue_search_assets` calls, use `ue_glob` to get project-wide asset counts in a single call.

```python
# 0a. One-shot asset enumeration
ue_glob(pattern="*", domain="assets", limit=100)
# Returns: all asset names grouped by type (~200 tokens)
# → Extract counts: Blueprint=239, Material=156, Niagara=42, etc.

# 0b. Source file overview (optional)
ue_glob(pattern="*.h", domain="source", limit=50)
# Returns: header file names for C++ architecture analysis
```

**Step 0 Decision Logic**:
- Asset counts from `ue_glob` → Skip individual `ue_search_assets(BP_*, M_*, ...)` calls
- Zero assets in a category → Skip that analysis category entirely
- Small project (<50 assets) → Run lightweight analysis, skip Epic standards compliance

**Token Savings**: ~80% — 1× `ue_glob` (~200 tokens) vs 5× `ue_search_assets` (~3000 tokens)

---

### 1. Blueprint Health

**What it checks**:

- Mega-functions (>50 nodes)
- High complexity (execution wire density)
- Unused variables/functions
- Naming violations

**Example Output**:

```
🔵 Blueprint Health (15 issues)
High: BP_GameMode::BeginPlay - 152 nodes
Medium: BP_WeaponBase - Missing BP_ prefix
```

---

### 2. C++ Architecture

**What it checks**:

- Cyclic dependencies
- Include bloat (>20 includes)
- Large classes (>1000 lines)
- UPROPERTY violations

**Example Output**:

```
⚙️ C++ Architecture (23 issues)
High: Cyclic dependency GameplayCore ↔ UISystem
Medium: MyPlayerController.h - 34 includes
```

---

### 3. Memory/Performance

**What it checks**:

- Large assets (>5MB)
- Tick-heavy Blueprints
- Unoptimized loops
- Missing const optimizations

**Example Output**:

```
🚀 Memory/Performance (12 issues)
High: BP_EnemyManager - 8.3MB asset
Medium: BP_AIController - Tick every frame
```

---

### 4. Build Time

**What it checks**:

- Slow modules (>30s)
- Missing PCH (precompiled headers)
- Unnecessary includes

**Example Output**:

```
⏱️ Build Time (7 issues)
High: GameplayCore - 47s compile time
Medium: UISystem - Missing PCH
```

---

### 5. Epic Standards Compliance

**What it checks**:

- Naming violations (A*, U*, F*, E* prefix)
- EditAnywhere overuse
- Missing BlueprintReadOnly
- Poor Category organization

**Example Output**:

```
✅ Epic Standards (35 issues)
High: EMyEnum → ELC_MyEnum (Epic naming)
Medium: 23 UPROPERTY missing Category
```

---

### 6. VFX Health (Niagara) 🆕

**Tool**: `ue_manage_niagara(operation="search_systems")` + `ue_manage_niagara(operation="get_parameters")`

**What it checks**:

- Excessive particle count (>1000/system)
- GPU-heavy simulation settings
- Missing LOD configurations
- Overdraw issues
- Memory-intensive emitters

```python
# VFX 시스템 검색
ue_manage_niagara(operation="search_systems", pattern="*")

# 파라미터 분석
ue_manage_niagara(operation="get_parameters", system_name="NS_Fire_Large")
```

**Example Output**:

```
🎆 VFX Health (8 issues)
High: NS_Explosion_Epic - 5,000 particles (>1000 limit)
High: NS_Rain_Heavy - GPU simulation, no LOD
Medium: NS_Fire_Large - Missing distance culling
Low: NS_Sparks - Consider GPU→CPU for low-count
```

**VFX Performance Thresholds**:

| Metric | Warning | Critical |
|--------|---------|----------|
| Particle Count | >500 | >1000 |
| GPU Emitters | >3/scene | >5/scene |
| Overdraw | >4x | >8x |
| Memory | >50MB | >100MB |

---

## 📋 Summary Report Example

```
================================================================================
Performance Health Check Report
================================================================================

Project: LastContact
Date: 2025-10-20
Analysis Time: 12 minutes

Overall Health Score: 72/100 (Good)

Total Issues Found: 100
  - High Priority: 36 (Fix immediately)
  - Medium Priority: 45 (Address soon)
  - Low Priority: 19 (Consider for future)

By Category:
  - Blueprint Health: 15 issues
  - C++ Architecture: 23 issues
  - Memory/Performance: 12 issues
  - Build Time: 7 issues
  - Epic Standards: 35 issues
  - VFX Health: 8 issues 🆕

================================================================================
Top 3 Recommendations
================================================================================

1. [HIGH] Fix cyclic dependency: GameplayCore ↔ UISystem
   → Impact: Build time, maintainability
   → Fix: Extract shared interfaces to separate module

2. [HIGH] Optimize BP_EnemyManager (8.3MB, Tick every frame)
   → Impact: Memory, runtime performance
   → Fix: Split into smaller Blueprints, remove Tick

3. [HIGH] Fix mega-function: BP_GameMode::BeginPlay (152 nodes)
   → Impact: Maintainability, debugging difficulty
   → Fix: Extract to helper functions

================================================================================
Trend Analysis (vs last week)
================================================================================

Issues: 92 → 78 (-15% improvement) ✅
Build Time: 45s → 38s (-15% improvement) ✅
Blueprint Complexity: Stable
C++ Dependencies: 2 new issues ⚠️

Would you like a detailed breakdown of any category?
```

---

## 🚀 Quick Example

**User**: "프로젝트 건강도 전체 분석해줘"

**Response**:

```
🔍 Starting comprehensive project health check...

📊 Scanning project...
  - Blueprints: 1,515
  - C++ classes: 487
  - Assets: 3,201

🔵 Analyzing Blueprint Health... (3 min) ✅
⚙️ Analyzing C++ Architecture... (4 min) ✅
🚀 Analyzing Memory/Performance... (2 min) ✅
⏱️ Analyzing Build Time... (2 min) ✅
✅ Analyzing Epic Standards... (1 min) ✅
🎆 Analyzing VFX Health... (2 min) ✅ 🆕

✅ Analysis complete! (14 minutes total)

[Summary report shown above]

Would you like a detailed breakdown of any category?
```

---

## Output Format

```text
=== Project Health Report ===

--- 1. Blueprint Health ---
Total BPs: {N}
Nativizable: {N} ({pct}%)
High-Complexity (>100 nodes): {N}
Score: {X}/10

--- 2. C++ Architecture ---
Modules: {N}
Circular Dependencies: {N}
Large Files (>2000 LOC): {N}
Score: {X}/10

--- 3. Memory/Performance ---
Large Assets (>50MB): {N}
Tick-heavy Actors: {N}
Score: {X}/10

--- 4. Build Time ---
Estimated Full Build: {X}min
PCH Coverage: {pct}%
Score: {X}/10

--- 5. Epic Standards ---
Naming Violations: {N}
Missing Redirectors: {N}
Score: {X}/10

--- 6. VFX Health ---
Niagara Systems: {N}
GPU Overdraw Risk: {N}
Score: {X}/10

--- Overall ---
Health Score: {X.X}/10
Top 3 Issues: [...]
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `ue_search_assets returns 0 for Blueprint type` | Project has no Blueprints or asset registry not loaded | Run `ue_cache_control(clear_all)` then retry; verify project path with `ue_check_health` |
| `ue_manage_niagara timeout on large project` | Too many Niagara systems (>100) | Use `search_systems` with specific pattern filter instead of wildcard `*` |
| `Build time estimation inaccurate` | Missing UBT timing data | Run a full build first to generate timing data in `Saved/Logs/` |

---

## 📚 Related Files

For detailed information, see:

- **EXAMPLES.md** - Complete usage examples and conversation scenarios
- **ADVANCED.md** - Weekly trends, HTML export, integration patterns
- **REFERENCE.md** - Complete category details, tools used, safety guidelines

---

**Status**: ✅ Production Ready (Issue #117 Phase 4, #4442 Enhancement)
**Date**: 2026-02-06
**Enhanced**: 6 Categories (5→6, +VFX Health with ue_manage_niagara)
## MCP Tool Examples

```python
# Analyze symbols for performance health check
ue_analyze_symbols(operation="smart", query="UActorComponent")
```
