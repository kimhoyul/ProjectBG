# BP → C++ Migration Validator Reference

**Version**: 1.1.0 (Issue #4442 - Added Parameter Auto-Detection + Core/Extended split)
**Parent**: [SKILL.md](SKILL.md)

---

## Tool Reference

### Stage 1: ue_manage_blueprint (get_structure)

Extracts Blueprint structure for migration analysis.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"get_structure"` |
| `params.blueprint_name` | str | Yes | Blueprint name (e.g., `"GA_BackDoor_Attack01"`) |
| `params.compress` | bool | No | Compress output (default: true) |
| `params.include_variables` | bool | No | Include variable definitions |

**Returns**:

```python
{
    "blueprint_name": str,
    "node_count": int,
    "source": str,                 # "execution_graph" | "asset_cache"
    "functions": int,              # Optional
    "variables": int,              # Optional, if include_variables
    "events": int,
    "custom_events": int,
    "components": int,
    "success": bool,
    "total_time_ms": int
}
```markdown

**Error Codes**:

| Code | Message | Cause |
|------|---------|-------|
| BP_001 | `Blueprint not found` | Invalid name |
| BP_002 | `Failed to load Blueprint` | Asset error |

---

### Stage 2: ue_analyze_symbols (search_symbols)

Searches for corresponding C++ class.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"search_symbols"` |
| `params.query` | str | Yes | Search pattern (e.g., `"*EnemyAttack*"`) |
| `params.limit` | int | No | Max results (default: 10) |

**Returns**:

```python
{
    "results": [
        {
            "name": str,
            "type": str,           # "class" | "function" | "struct"
            "pdb_source": str      # "MyProject.pdb" | "UE5Editor.pdb"
        }
    ],
    "total_results": int
}
```markdown

**Fallback Operation: get_methods**

If direct class not found, search base class methods:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"get_methods"` |
| `params.class_name` | str | Yes | Base class name |
| `params.limit` | int | No | Max methods |

**Returns**:

```python
{
    "class_name": str,
    "methods": [
        {"name": str, "full_name": str}
    ],
    "methods_count": int
}
```markdown

---

### Stage 3: ue_search_assets (Blueprint references)

Finds assets referencing the Blueprint.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_type` | str | Yes | `"Blueprint"` |
| `params.query` | str | Yes | Search pattern (e.g., `"*BackDoor*"`) |
| `params.limit` | int | No | Max results (default: 5) |

**Returns**:

```python
{
    "assets": [
        {
            "name": str,
            "path": str
        }
    ],
    "total_found": int
}
```markdown

---

### Stage 3A (Extended): ue_trace_execution (trace_execution_flow)

Compares BP execution flow with C++ implementation.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"trace_execution_flow"` |
| `params.blueprint_name` | str | Yes | Blueprint to trace |
| `params.start_node` | str | Yes* | Entry point node |

*Auto-detected from Stage 1 results

**Start Node Auto-Detection Priority**:

| BP Type | Priority Order |
|---------|----------------|
| Actor | BeginPlay → Tick → Construction Script |
| Ability | ActivateAbility → EndAbility → CanActivate |
| Widget | Construct → OnInitialized → PreConstruct |

**Returns**:

```python
{
    "flow": {
        "entry_point": str,
        "nodes": list,
        "execution_paths": list
    },
    "success": bool
}
```markdown

---

### Stage 4: ue_fix_errors (preflight)

Checks for migration-related build issues.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `mode` | str | Yes | `"preflight"` |
| `params.build_log_path` | str | No* | Path to build log |

*If not provided, Stage 4 is skipped

**Build Log Auto-Detection**:

```python
log_patterns = [
    "Intermediate/Build/**/*.log",
    "Saved/Logs/UnrealEditor-*.log",
    "Saved/Logs/Cook-*.log"
]
```text

**Returns**:

```python
{
    "errors": list,
    "warnings": list,
    "suggestions": list,
    "success": bool,
    "error": str                   # "preflight mode requires 'build_log_path' parameter"
}
```markdown

---

### Stage 5A (Extended): ue_editor_* (compile_blueprints)

Compiles Blueprint to verify migration.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | str | Yes | `"compile_blueprints"` |
| `params.target` | str | No | Specific class to compile |

**Returns**:

```python
{
    "success": bool,
    "compiled_count": int,
    "errors": list
}
```markdown

**Requirement**: Editor must be connected

---

## Type Definitions

### MigrationReport

```python
class MigrationReport(TypedDict):
    blueprint_name: str
    cpp_class: Optional[str]
    validation_stages: List[StageResult]
    completeness_score: float      # 0-100%
    migration_type: MigrationType
    action_items: List[ActionItem]
```markdown

### MigrationType

```python
MigrationType = Literal[
    "FULL",           # Complete C++ replacement exists
    "PARTIAL",        # BP/C++ hybrid (intentional)
    "NOT_STARTED",    # No C++ equivalent
    "IN_PROGRESS"     # Migration incomplete
]
```markdown

### StageResult

```python
class StageResult(TypedDict):
    stage: int
    name: str
    status: str                    # "Pass" | "Warn" | "Skip" | "Fail"
    details: str
```markdown

### ActionItem

```python
class ActionItem(TypedDict):
    priority: str                  # "CRITICAL" | "OPTIONAL"
    task: str
    completed: bool
```markdown

### FunctionMigrationStatus

```python
class FunctionMigrationStatus(TypedDict):
    bp_name: str
    cpp_name: Optional[str]
    migrated: bool
    reason: Optional[str]          # "Keep in BP (debug only)"
```markdown

### VariableMigrationStatus

```python
class VariableMigrationStatus(TypedDict):
    bp_name: str
    cpp_property: Optional[str]
    migrated: bool
    specifiers: List[str]          # ["UPROPERTY", "BlueprintReadWrite"]
```

---

## Workflow Stages Summary

### Core Workflow (5 Stages - Required)

| Stage | Tool | Operation | Offline | Required |
|-------|------|-----------|---------|----------|
| 1 | `ue_manage_blueprint` | get_structure | ✅ | ✅ |
| 2 | `ue_analyze_symbols` | search_symbols | ✅ | ⚠️ Warn |
| 3 | `ue_search_assets` | Blueprint refs | ✅ | ⚠️ Warn |
| 4 | `ue_fix_errors` | preflight | ✅ | ⏭️ Skip |
| 5 | Report | Generation | ✅ | ✅ |

### Extended Workflow (7 Stages - Optional)

| Stage | Tool | Operation | Condition |
|-------|------|-----------|-----------|
| 3A | `ue_trace_execution` | trace_flow | start_node available |
| 5A | `ue_editor_*` | compile | Editor connected |

---

## Error Code Reference

| Stage | Error | Cause | Recovery |
|-------|-------|-------|----------|
| 1 | `Blueprint not found` | Invalid name | Use `ue_search_assets` |
| 2 | `No results` | C++ class doesn't exist | Search base class |
| 3 | `No references` | BP not used | Normal for unused BP |
| 3A | `start_node required` | No entry point | Skip stage |
| 4 | `build_log_path required` | No log available | Skip stage |
| 5A | `Editor not connected` | No RC connection | Skip stage |

---

## Parameter Auto-Detection

### Blueprint Name → C++ Class Candidates

| BP Pattern | Auto-detected C++ Patterns |
|------------|---------------------------|
| `BP_Character` | `ACharacter`, `UCharacter`, `ACharacterBase` |
| `BP_Weapon` | `AWeapon`, `UWeapon`, `AWeaponBase` |
| `GA_Attack` | `UGA_Attack`, `AGA_Attack`, `GA_Attack` |
| `BP_Actor` | `AActor`, `ABP_Actor` |
| `WBP_Widget` | `UUserWidget`, `UWBP_Widget` |

### Common Base Classes by Type

| BP Type | Common Base Classes |
|---------|---------------------|
| GameplayAbility | `UGameplayAbility`, `UAAGameplayAbilityBase` |
| Character | `ACharacter`, `APawn` |
| Actor | `AActor` |
| Widget | `UUserWidget`, `UCommonActivatableWidget` |
| Component | `UActorComponent`, `USceneComponent` |

---

## Migration Completeness Scoring

| Stage | Weight | Pass | Warn | Skip | Fail |
|-------|--------|------|------|------|------|
| 1. BP Structure | 20% | 20 | 10 | 5 | 0 |
| 2. C++ Class | 25% | 25 | 15 | 10 | 0 |
| 3. References | 15% | 15 | 10 | 5 | 0 |
| 4. Build Check | 20% | 20 | 10 | 10 | 0 |
| 5. Report | 20% | 20 | 20 | 20 | 0 |

**Score Interpretation**:
- 90-100%: Migration complete, ready for BP deletion
- 70-89%: Migration functional, minor items remaining
- 50-69%: Migration in progress, significant work needed
- <50%: Migration not started or major issues

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios with MCP results |

---

**Tracking**: Issue #4442 - Skills & SubAgents Improvement
**Last Updated**: 2026-02-06
