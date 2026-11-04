# Performance Health Check - Complete Reference

Complete category details, tools used, and safety guidelines.

---

## Complete Category Details

### 1. Blueprint Health (Full Specification)

**Tools Used**:

- `ue_analyze_blueprint(operation="find_nodes")` - Node count analysis
- `BlueprintCommandletParser` - Metadata parsing
- `ue_search_assets` - Blueprint inventory

**Detection Rules**:

```python
# Mega-function detection
if node_count > 50:
    priority = "high" if node_count > 100 else "medium"

# Complexity detection
if execution_wires > 80:
    priority = "high"

# Unused detection
if variable.references == 0:
    priority = "medium"

# Naming detection
if not blueprint_name.startswith("BP_"):
    priority = "medium" if in_content_folder else "low"
```

**Output Format**:

```json
{
    "category": "Blueprint Health",
    "issues": [
        {
            "type": "mega-function",
            "priority": "high",
            "blueprint": "BP_GameMode",
            "function": "BeginPlay",
            "node_count": 152,
            "recommendation": "Extract to 3 helper functions",
            "estimated_improvement": "96% reduction to 6 nodes"
        }
    ]
}
```

---

### 2. C++ Architecture (Full Specification)

**Tools Used**:

- `ue_analyze_config(operation="hierarchy")` - Module dependencies
- `ue_analyze_symbols(operation="search")` - Class size analysis
- `tree-sitter` - Header parsing for include count

**Detection Rules**:

```python
# Cyclic dependency detection
if module_a in dependencies_of(module_b) and module_b in dependencies_of(module_a):
    priority = "high"

# Include bloat detection
if include_count > 30:
    priority = "high"
elif include_count > 20:
    priority = "medium"

# Large class detection
if line_count > 1500:
    priority = "high"
elif line_count > 1000:
    priority = "medium"

# UPROPERTY validation
if uproperty_has_no_category():
    priority = "medium"
```

**Output Format**:

```json
{
    "category": "C++ Architecture",
    "issues": [
        {
            "type": "cyclic_dependency",
            "priority": "high",
            "modules": ["GameplayCore", "UISystem"],
            "chain": ["GameplayCore", "UISystem", "GameplayCore"],
            "recommendation": "Extract shared interfaces to GameplayInterfaces module",
            "impact": "Build time, maintainability"
        }
    ]
}
```

---

### 3. Memory/Performance (Full Specification)

**Tools Used**:

- `ue_search_assets` - Asset size detection
- `ue_analyze_blueprint(operation="find_nodes", node_type="K2Node_CallFunction")` - Tick detection
- `ue_analyze_blueprint(operation="find_nodes", node_type="K2Node_ForEachLoop")` - Loop detection

**Detection Rules**:

```python
# Large asset detection
if asset_size > 10_000_000:  # 10MB
    priority = "high"
elif asset_size > 5_000_000:  # 5MB
    priority = "medium"

# Tick detection
if blueprint_has_tick_node():
    priority = "high" if tick_frequency == "every_frame" else "medium"

# Loop optimization
if foreach_loop_array_size > 500:
    priority = "high"
```

**Output Format**:

```json
{
    "category": "Memory/Performance",
    "issues": [
        {
            "type": "large_asset",
            "priority": "high",
            "asset": "BP_EnemyManager.uasset",
            "size_mb": 8.3,
            "recommendation": "Split into smaller Blueprints (BP_EnemySpawner, BP_EnemyTracker)",
            "estimated_savings": "6MB reduction"
        }
    ]
}
```

---

### 4. Build Time (Full Specification)

**Tools Used**:

- `PDB timestamp analysis` - Compile time measurement
- `ue_analyze_config` - Module .Build.cs parsing
- `tree-sitter` - Include analysis

**Detection Rules**:

```python
# Slow module detection
if compile_time > 60:
    priority = "high"
elif compile_time > 30:
    priority = "medium"

# PCH detection
if not has_pch_file():
    priority = "high" if compile_time > 20 else "medium"

# Include optimization
if unnecessary_includes > 10:
    priority = "medium"
```

**Output Format**:

```json
{
    "category": "Build Time",
    "issues": [
        {
            "type": "slow_module",
            "priority": "high",
            "module": "GameplayCore",
            "compile_time_seconds": 47,
            "reasons": ["487 C++ files", "No PCH", "Heavy templates"],
            "recommendations": [
                "Enable Unity Build (-15s)",
                "Add precompiled headers (-12s)",
                "Split into GameplayCore + GameplayCoreExt (-10s)"
            ],
            "estimated_savings": "37 seconds (79% reduction)"
        }
    ]
}
```

---

### 5. Epic Standards Compliance (Full Specification)

**Tools Used**:

- `ue_analyze_symbols(operation="search")` - Class naming
- `ue_find_uproperty_usage` - UPROPERTY analysis
- `tree-sitter` - C++ parsing

**Detection Rules**:

```python
# Naming convention validation
if class_type == "Actor" and not name.startswith("A"):
    priority = "high"
if class_type == "Object" and not name.startswith("U"):
    priority = "high"
if struct and not name.startswith("F"):
    priority = "high"
if enum and not name.startswith("E"):
    priority = "high"

# UPROPERTY validation
if has_edit_anywhere_on_component():
    priority = "high"  # Should be EditDefaultsOnly
if uproperty_missing_category():
    priority = "medium"
if blueprint_readwrite_on_private():
    priority = "high"
```

**Output Format**:

```json
{
    "category": "Epic Standards Compliance",
    "issues": [
        {
            "type": "naming_violation",
            "priority": "high",
            "current_name": "MyGameMode",
            "suggested_name": "ALC_GameMode",
            "rule": "Actor classes must use A prefix + project prefix LC",
            "auto_fixable": true
        }
    ]
}
```

---

## Safety & Best Practices

### Read-Only Analysis

**Guarantees**:

- ✅ 100% read-only (no project modifications)
- ✅ Safe on production projects
- ✅ No side effects on project files
- ✅ No Unreal Editor interaction required

**What is Read**:

- Blueprint metadata (.uasset files via Commandlet)
- C++ source code (.h, .cpp via tree-sitter)
- PDB symbol indices (pre-generated)
- UBT Manifest (build system data)
- Asset sizes (filesystem)

**What is NOT Modified**:

- No file writes
- No Blueprint edits
- No .Build.cs modifications
- No config changes

---

### Performance Considerations

**Optimization Strategies**:

```python
# Parallel analysis (where possible)
await asyncio.gather(
    analyze_blueprints(),
    analyze_cpp(),
    analyze_performance()
)

# Cached data reuse
if pdb_index_exists():
    reuse_pdb_index()  # No re-indexing
if blueprint_metadata_cached():
    load_from_cache()  # No re-parsing

# Incremental updates
if last_run_timestamp < project_modified_time:
    analyze_only_changed_files()
```

**Typical Performance** (last-contact-prod):

- Cold start: ~12 minutes (first run)
- Warm start: ~5 minutes (cached data)
- Incremental: ~2 minutes (only changed files)

---

### Privacy & Data Handling

**Local-Only Processing**:

- ✅ All analysis done locally on user's machine
- ✅ No external data transmission
- ✅ No telemetry or analytics
- ✅ Reports stored locally only

**Report Storage**:

- Location: `.narshamcp-mcp-mcp/health_reports/` (local project directory)
- Format: JSON + HTML/Markdown (user choice)
- Retention: User-controlled (no auto-deletion)

---

## Epic Games Best Practices

### Naming Conventions

**Classes**:

```cpp
✅ ALC_GameMode (Actor, project prefix LC_)
✅ ULC_MyComponent (UObject, project prefix)
✅ FLCPlayerData (Struct, project prefix)
✅ ELC_GameState (Enum, project prefix)

❌ MyGameMode (missing A + project prefix)
❌ player_data (snake_case)
❌ WEAPON (all caps)
```

**Blueprints**:

```
✅ BP_PlayerCharacter
✅ BP_GameMode
✅ WBP_MainMenu (Widget Blueprint)

❌ MyPlayerCharacter (missing BP_)
❌ player_controller (snake_case)
```

---

### UPROPERTY Best Practices

**Category Usage**:

```cpp
✅ UPROPERTY(EditAnywhere, Category="Stats|Health")
   float Health;

❌ UPROPERTY(EditAnywhere)  // Missing category
   float Health;
```

**Component Exposure**:

```cpp
✅ UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components")
   UMeshComponent* MeshComp;

❌ UPROPERTY(EditAnywhere)  // Should be EditDefaultsOnly for components
   UMeshComponent* MeshComp;
```

**Access Control**:

```cpp
✅ protected:
   UPROPERTY(BlueprintReadOnly, Category="Stats")
   float Health;

❌ private:
   UPROPERTY(BlueprintReadWrite)  // Private shouldn't be writable from BP
   float Health;
```

---

## External Links

**Epic Games Documentation**:

- [Coding Standard](https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine)
- [Blueprint Best Practices](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-best-practices-in-unreal-engine)
- [Performance Guidelines](https://dev.epicgames.com/documentation/en-us/unreal-engine/performance-guidelines-for-unreal-engine)

**NarshaMCP Documentation**:

- [Policy Tools Cheatsheet](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md)
- BlueprintCommandletParser
- UBT Manifest Parser

**Related Issues**:

- [Issue #117: Agent Skills](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/117)
- [Issue #19: UBT Manifest Parser](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/19)

---

## Troubleshooting

### Issue 1: Analysis Takes Too Long

**Symptom**: Health check takes >30 minutes

**Possible Causes**:

- Very large project (>3000 Blueprints)
- Slow disk I/O
- Missing cached data

**Solutions**:

1. Enable incremental analysis (only changed files)
2. Use SSD for project location
3. Pre-generate Blueprint metadata (Commandlet)
4. Exclude test content from analysis

---

### Issue 2: False Positives in Naming

**Symptom**: Legitimate names flagged as violations

**Possible Causes**:

- Third-party plugins with different conventions
- Legacy code with old naming
- Special cases (editor-only, test code)

**Solutions**:

1. Configure exclusions in `.narshamcp-mcp-mcp/config.yaml`
2. Use `--exclude-pattern` flag
3. Accept some false positives in Medium/Low priority

---

### Issue 3: Missing Issues

**Symptom**: Known issues not detected

**Possible Causes**:

- Blueprint metadata not updated
- PDB indices outdated
- UBT Manifest stale

**Solutions**:

1. Rebuild project (generates fresh PDB + Manifest)
2. Force Blueprint metadata regeneration
3. Clear cache and re-run analysis

---

## Tool Reference

### MCP Tools Used

| Tool | Purpose | Category |
|------|---------|----------|
| `ue_search_assets` | Asset inventory + size | Memory/Performance |
| `ue_analyze_blueprint` | Node analysis | Blueprint Health |
| `ue_analyze_symbols` | C++ class analysis | C++ Architecture |
| `ue_analyze_config` | Module dependencies | C++ Architecture, Build Time |
| `ue_find_uproperty_usage` | UPROPERTY analysis | Epic Standards |

### Internal Parsers

| Parser | Purpose | Category |
|--------|---------|----------|
| BlueprintCommandletParser | Blueprint metadata | Blueprint Health |
| PDB Parser | C++ symbols | C++ Architecture |
| UBT Manifest Parser | Module dependencies | C++ Architecture, Build Time |
| tree-sitter | C++ source parsing | All C++ categories |

---

**Version**: 1.1.0
**Status**: ✅ Production Ready
**Date**: 2025-10-20
