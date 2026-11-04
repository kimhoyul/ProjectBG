# Module Mapper - Complete Reference

Complete workflow details, tool parameters, and Epic Games standards.

---

## Complete Workflow Details

### Step 1: Query UBT Manifest (Find Module)

**Purpose**: Find which module defines the missing class/struct.

**Tool Call**:

```python
ue_analyze_config(
    operation="search",
    query="FAssetData",  # Class, struct, or enum name
    project_root="<auto-detected>"
)
```

**Parameters**:

- `operation` (required): `"search"`
- `query` (required): Class/struct/enum name (e.g., "FAssetData", "UGameplayAbility")
- `project_root` (optional): Project root path (auto-detected if omitted)

**Expected Output**:

```json
{
    "class_name": "FAssetData",
    "module": "AssetRegistry",
    "header": "AssetRegistry/Public/AssetData.h",
    "api_macro": "ASSETREGISTRY_API",
    "module_type": "Runtime",
    "dependencies": ["CoreUObject", "Core"]
}
```

**Response Template**:

```
🔍 Step 1: Module Query

[ClassName] is defined in: **[ModuleName]** module
- Header: [HeaderPath]
- API Macro: [API_MACRO]
- Type: [Runtime/Editor] module
- Dependencies: [Dep1, Dep2, ...]

다음 단계: .Build.cs에 어떻게 추가하는지 보여드릴게요.
```

---

### Step 2: Show Module Details (Dependencies)

**Purpose**: Provide complete module information including transitive dependencies.

**Tool Call**:

```python
ue_analyze_config(
    operation="hierarchy",
    option="AssetRegistry",  # Module name from Step 1
    project_root="<auto-detected>"
)
```

**Parameters**:

- `operation` (required): `"hierarchy"`
- `option` (required): Module name (from Step 1 output)
- `project_root` (optional): Project root path

**Expected Output**:

```json
{
    "module": "AssetRegistry",
    "location": "Runtime/AssetRegistry",
    "type": "Runtime",
    "api_macro": "ASSETREGISTRY_API",
    "direct_dependencies": ["CoreUObject", "Core"],
    "transitive_dependencies": ["CoreUObject", "Core", "TraceLog"],
    "dependent_modules": ["Engine", "UnrealEd", "AssetTools"],
    "circular_dependencies": []
}
```

**Response Template**:

```
📦 Step 2: Module Details

[ModuleName] Module:
- Location: [ModulePath]
- Type: [Runtime/Editor] module
- API Macro: [API_MACRO]

Dependencies:
- Direct: [Dep1, Dep2, ...]
- Transitive: [TransDep1, TransDep2, ...]

Dependent Modules (who uses this):
- [Consumer1, Consumer2, ...]

[Circular dependency warning if applicable]

다음 단계: .Build.cs 수정 방법을 보여드릴게요.
```

---

### Step 3: Generate .Build.cs Diff (Modification Plan)

**Purpose**: Show exact .Build.cs modification with unified diff format.

**Tool Call**:

```python
ue_analyze_config(
    operation="modify",
    option="AssetRegistry",  # Module to add
    new_value="add_to_public_dependencies",
    project_root="<auto-detected>",
    target_layer="MyModule.Build.cs"  # Optional: specific .Build.cs file
)
```

**Parameters**:

- `operation` (required): `"modify"`
- `option` (required): Module name to add
- `new_value` (required): `"add_to_public_dependencies"` or `"add_to_private_dependencies"`
- `project_root` (optional): Project root path
- `target_layer` (optional): Specific .Build.cs file name

**Expected Output**:

```json
{
    "modification_plan": {
        "file": "Source/MyModule/MyModule.Build.cs",
        "action": "add_to_array",
        "array_name": "PublicDependencyModuleNames",
        "value": "AssetRegistry",
        "line_number": 15
    },
    "diff": "--- a/Source/MyModule/MyModule.Build.cs\n+++ b/Source/MyModule/MyModule.Build.cs\n@@ -12,6 +12,7 @@\n         \"Core\",\n         \"CoreUObject\",\n         \"Engine\",\n+        \"AssetRegistry\",\n     });\n",
    "impact": {
        "will_override": [],
        "will_be_overridden_by": [],
        "circular_risk": false
    }
}
```

**Response Template**:

```
✏️ Step 3: .Build.cs Modification

Add to [PublicDependencyModuleNames/PrivateDependencyModuleNames] in [FilePath]:

```diff
[Unified diff output]
```

Impact Analysis:

- [Circular dependency check]
- [Conflicts check]

Next Steps:

1. Add "[ModuleName]" to [ArrayName] array (line [N])
2. Rebuild your project
3. The linker error should be resolved

수정해드릴까요?

```

---

## Tool Parameter Reference

### `ue_analyze_config`

**Operations**:
| Operation | Purpose | Required Params | Optional Params |
|-----------|---------|-----------------|-----------------|
| `search` | Find module for class | `query` | `project_root` |
| `hierarchy` | Get module dependencies | `option` (module name) | `project_root` |
| `modify` | Generate .Build.cs diff | `option`, `new_value` | `project_root`, `target_layer` |

**Common Parameters**:
- `project_root`: Auto-detected from MCP config, can be overridden
- `option`: Module name for hierarchy/modify operations
- `new_value`: `"add_to_public_dependencies"` or `"add_to_private_dependencies"`
- `target_layer`: Specific .Build.cs file name (e.g., "MyModule.Build.cs")

---

## Epic Games Standards

### Module Organization Standards

**Runtime Modules**:
- Location: `Runtime/[ModuleName]/`
- Can depend on: Other Runtime modules only
- Cannot depend on: Editor modules (UnrealEd, etc.)
- Examples: Engine, CoreUObject, AssetRegistry, GameplayAbilities

**Editor Modules**:
- Location: `Editor/[ModuleName]/`
- Can depend on: Runtime + Editor modules
- Examples: UnrealEd, AssetTools, Kismet, BlueprintGraph

**Plugin Modules**:
- Location: `Plugins/[PluginName]/Source/[ModuleName]/`
- Follow same Runtime/Editor rules
- Should not depend on project modules (circular risk)

---

### Dependency Best Practices

**1. Minimize Dependencies**:
```cpp
❌ Bad: Add every module "just in case"
✅ Good: Add only modules you actually use
```

**2. Use Private When Possible**:

```cpp
❌ Bad: Everything in PublicDependencyModuleNames
✅ Good: Headers exposed = Public, .cpp only = Private
```

**3. Avoid Circular Dependencies**:

```cpp
❌ Bad: MyGameModule → UnrealEd (circular!)
✅ Good: MyGameEditorModule → UnrealEd, MyGameModule
```

**4. Check Module Type**:

```cpp
❌ Bad: Runtime module depends on Editor module
✅ Good: Runtime depends on Runtime, Editor depends on both
```

---

### .Build.cs File Structure

**Standard Structure**:

```csharp
// MyModule.Build.cs
public class MyModule : ModuleRules
{
    public MyModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Public dependencies (exposed in headers)
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AssetRegistry",  // ← Add here if FAssetData in .h
        });

        // Private dependencies (.cpp only)
        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            // Add here if only used in .cpp
        });

        // Forward declarations only (rare)
        PublicIncludePathModuleNames.AddRange(new string[]
        {
            // Rarely used
        });
    }
}
```

---

### Common Mistakes

**Mistake 1**: Adding Editor module to Runtime module

```csharp
❌ Bad:
// MyGameModule.Build.cs (Runtime)
PublicDependencyModuleNames.Add("UnrealEd");  // WRONG!

✅ Good:
// MyGameEditorModule.Build.cs (Editor)
PublicDependencyModuleNames.Add("UnrealEd");  // Correct
```

**Mistake 2**: Using Public when Private sufficient

```csharp
❌ Bad:
// MyModule.Build.cs
PublicDependencyModuleNames.Add("AssetRegistry");
// But FAssetData only used in .cpp files!

✅ Good:
// MyModule.Build.cs
PrivateDependencyModuleNames.Add("AssetRegistry");
// FAssetData only in .cpp = Private
```

**Mistake 3**: Not adding required dependencies

```csharp
❌ Bad:
PublicDependencyModuleNames.Add("GameplayAbilities");
// Missing GameplayTags and GameplayTasks!

✅ Good:
PublicDependencyModuleNames.AddRange(new string[]
{
    "GameplayAbilities",
    "GameplayTags",      // Required
    "GameplayTasks",     // Required
});
```

---

## Safety & Best Practices

### Read-Only Query

Module Mapper is **100% read-only** by default:

- ✅ Shows diff preview before any changes
- ✅ Validates circular dependency risk
- ✅ No modifications without explicit approval

**Workflow**:

```
1. Query module → Shows module info
2. Show details → Shows dependencies
3. Generate diff → Shows preview
4. Ask user → "수정해드릴까요?"
5. Only modify if user approves
```

---

### Validation Checks

Before applying any .Build.cs modification, Module Mapper validates:

**1. Circular Dependency Check**:

```
IF module creates circular dependency:
    Show warning + refactoring suggestions
    Do NOT apply modification
```

**2. Module Type Check**:

```
IF Runtime module + Editor dependency:
    Show warning + best practices
    Suggest creating separate Editor module
```

**3. Conflict Check**:

```
IF module already in .Build.cs:
    Show info: "Module already added"
    Skip modification
```

**4. File Existence Check**:

```
IF .Build.cs file not found:
    Show error + file location suggestion
    Do NOT apply modification
```

---

## Error Handling

### Error 1: Class Not Found

**Error**: "Unable to find class 'FMyCustomClass' in UBT Manifest"

**Response**:

```
❌ Class not found in UBT Manifest

Possible reasons:
1. Class name misspelled (check case sensitivity)
2. Class defined in project (not in Manifest yet)
3. Class from third-party plugin (not built yet)
4. Manifest outdated (rebuild project)

Suggestions:
1. Rebuild project to update UBT Manifest
2. Check class name spelling
3. Verify class actually exists in codebase
4. Use ue_analyze_symbols(operation="search", query="FMyCustomClass")
```

---

### Error 2: Module Not Found

**Error**: "Module 'MyCustomModule' not found in UBT Manifest"

**Response**:

```
❌ Module not found in UBT Manifest

Possible reasons:
1. Module not built yet
2. Module defined but not included in .uproject
3. Plugin not enabled in project
4. Manifest outdated (rebuild project)

Suggestions:
1. Check .uproject file for module entry
2. Enable plugin in Unreal Editor (if plugin module)
3. Rebuild project to update Manifest
4. Verify module .Build.cs file exists
```

---

### Error 3: Circular Dependency

**Error**: "Adding module would create circular dependency"

**Response**:

```
⚠️ Circular Dependency Risk

Module dependency chain:
[YourModule] → [TargetModule] → ... → [YourModule]

Epic Games Best Practice:
- Runtime modules should NOT depend on Editor modules
- Plugins should NOT depend on project modules

Refactoring Options:
1. Create separate Editor module for editor-specific code
2. Use interfaces to break dependency
3. Restructure code to remove circular reference

Need help refactoring? Ask: "순환 의존성 해결 방법"
```

---

## External Links

**Unreal Engine Documentation**:

- [Build Configuration](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-configuration-for-unreal-engine)
- [Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules)
- [Build.cs Files](https://dev.epicgames.com/documentation/en-us/unreal-engine/build-cs-file-structure)

**NarshaMCP Documentation**:

- [Policy Tools Cheatsheet](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md)
- UBT Manifest Parser
- [Issue #19 (UBT Manifest Parser)](../../../docs/issues/)
- Issue #89 (File-based Cache)

**Related Issues**:

- [Issue #19: UBT Manifest Parser](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/19)
- [Issue #89: File-based Cache](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/89)
- [Issue #117: Agent Skills](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/117)

---

**Version**: 1.1.0
**Status**: ✅ Production Ready
**Date**: 2025-10-20
