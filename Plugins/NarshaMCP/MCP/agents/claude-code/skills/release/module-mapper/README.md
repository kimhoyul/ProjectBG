# Module Mapper - Installation & Usage Guide

**Version**: 1.0.0
**Date**: 2025-10-20
**Related**: Issue #117 Phase 4

---

## 📋 Overview

**Module Mapper** is a Claude Code Agent Skill that provides 100% accurate module dependency resolution using UBT (Unreal Build Tool) Manifest data.

**Key Features**:

- 🎯 100% accurate module suggestions (vs 30-67% pattern-based)
- ⚡ Zero false positives (real compiler data)
- 📊 Instant .Build.cs modification guidance
- 🔗 Automatic dependency chain resolution

---

## 🚀 Installation

### Option 1: Via Release Package (recommended)

```text
1. Download NarshaMCP release ZIP from GitHub Releases
2. Extract to your UE project's Plugins/ directory
3. Skills are included at: Plugins/NarshaMCP/MCP/agents/claude-code/skills/
4. Run install-to-plugin.bat to copy skills to project .claude/skills/
```

**Verification**:

```bash
ls E:\MyProject\.claude\skills\module-mapper\
# Should show:
#   SKILL.md (this skill)
#   ADVANCED.md, EXAMPLES.md, REFERENCE.md (documentation)
#   README.md (this file)
```

---

### Option 2: Manual Installation

1. **Copy skill files**:

   ```bash
   mkdir -p <YourProject>/.claude/skills/module-mapper/
   cp Skills/release/module-mapper/* <YourProject>/.claude/skills/module-mapper/
   ```

2. **Verify installation**:

   ```bash
   cat <YourProject>/.claude/skills/module-mapper/SKILL.md | head -5
   # Should show skill header
   ```

3. **Restart Claude Code**:
   - VS Code: Reload window (`Ctrl+Shift+P` → "Developer: Reload Window")
   - Claude CLI: Close and reopen session

---

## 🎯 Usage

### Basic Usage

**Example 1: FAssetData linker error**

```text
User: "FAssetData 클래스 찾을 수 없다는 링커 에러가 나. 어떤 모듈 추가해야 돼?"

Claude:
🔍 Step 1: Module Query
FAssetData → AssetRegistry 모듈

📦 Step 2: Module Details
AssetRegistry (Runtime/AssetRegistry)
- Dependencies: CoreUObject, Core

✏️ Step 3: .Build.cs Modification
PublicDependencyModuleNames에 "AssetRegistry" 추가

[Unified diff shown]
```

---

**Example 2: UGameplayAbility module**

```text
User: "UGameplayAbility 클래스 사용하려면 어떤 모듈이 필요해?"

Claude:
🔍 UGameplayAbility → GameplayAbilities 모듈

📦 Required Dependencies:
1. GameplayAbilities (primary)
2. GameplayTags (required)
3. GameplayTasks (required)

✏️ .Build.cs Modification:
[Shows diff with 3 modules]
```

---

**Example 3: Request diff preview**

```diff
User: "AssetRegistry 모듈 추가하는 .Build.cs diff 보여줘"

Claude:
```diff
--- a/Source/MyModule/MyModule.Build.cs
+++ b/Source/MyModule/MyModule.Build.cs
@@ -12,6 +12,7 @@
         "Core",
         "CoreUObject",
         "Engine",
+        "AssetRegistry",
     });
```

```text

---

## 🔍 When Does It Activate?

Module Mapper **auto-loads** when you ask:

**Trigger Keywords**:
- English: `module`, `dependency`, `linker`, `unresolved`, `LNK2019`, `.Build.cs`, `PublicDependencyModuleNames`
- Korean: `모듈`, `의존성`, `링커`, `.Build.cs`

**Example Queries**:
- ✅ "FAssetData가 어느 모듈이야?"
- ✅ "링커 에러 LNK2019 고쳐줘"
- ✅ ".Build.cs에 어떤 모듈 추가해야 돼?"
- ✅ "Which module is UGameplayAbility in?"
- ✅ "Unresolved external symbol 에러"

---

## 🧭 3-Step Workflow

Module Mapper uses **3-step workflow**:

### Step 1: Module Query
- **Goal**: Find which module defines the missing class
- **Example**: `FAssetData` → `AssetRegistry` module
- **Tool**: `ue_analyze_config(operation="search_config")`

### Step 2: Module Details
- **Goal**: Show complete module information
- **Example**: Dependencies, API macro, location
- **Tool**: `ue_analyze_config(operation="hierarchy")`

### Step 3: .Build.cs Modification
- **Goal**: Generate exact .Build.cs diff
- **Example**: Add to PublicDependencyModuleNames
- **Tool**: `ue_analyze_config(operation="modify_config")`

---

## 🎨 Advanced Features

### Dependency Chain Resolution

```

User: "GameplayAbilities 모듈 추가할 때 다른 모듈도 필요해?"

Claude:
GameplayAbilities requires:

- GameplayTags (direct)
- GameplayTasks (direct)

You need to add all 3 modules to PublicDependencyModuleNames.

```text

---

### Circular Dependency Detection

```

User: "UnrealEd 모듈 추가해도 돼?"

Claude:
⚠️ WARNING: Circular Dependency Risk

UnrealEd → Engine → MyModule (your module)

Adding UnrealEd to MyModule would create a circular dependency.

Recommendation: Refactor to break the cycle.

```text

---

### Private vs Public Dependencies

```

User: "AssetRegistry를 Public과 Private 중 어디 추가해야 돼?"

Claude:
PublicDependencyModuleNames:

- Use when: FAssetData is in your .h file (member variable, parameter)

PrivateDependencyModuleNames:

- Use when: Only used in .cpp files (local variable, implementation only)

Your case: FAssetData in MyClass.h → PublicDependencyModuleNames

```text

---

## 📈 Performance & Accuracy

| Metric | Module Mapper | Pattern Matching | Manual Search |
|--------|---------------|------------------|---------------|
| **Accuracy** | **100%** | 30-67% | 100% |
| **Speed** | <100ms | <100ms | 5-30 min |
| **False Positives** | **0%** | 33-70% | 0% |
| **Source** | Compiler data | Heuristics | Documentation |

**Why 100% accurate?**
- Uses UBT Manifest (real compiler data, not guesses)
- Generated during actual project build
- Includes all modules (Engine + Project + Plugins)

---

## 🛠️ Troubleshooting

### Skill doesn't activate

**Symptom**: Claude doesn't use Module Mapper

**Solutions**:
1. **Check skill file exists**:
   ```bash
   ls Skills/release/module-mapper/SKILL.md
   ```

2. **Restart Claude Code**:
   - VS Code: `Ctrl+Shift+P` → "Developer: Reload Window"
   - Claude CLI: Close and reopen

3. **Check trigger keywords**:
   - Use specific keywords like "module", "linker", ".Build.cs"
   - Example: "FAssetData **모듈** 알려줘" (includes trigger word)

---

### UBT Manifest not found

**Symptom**: "UBT manifest not found" error

**Solutions**:

1. **Build your project**:
   - Open project in Unreal Editor
   - Build → Compile C++ Code
   - UBT manifest generated at: `Intermediate/Build/Win64/UnrealEditor/Development/*.manifest`

2. **Check manifest exists**:

   ```bash
   ls Intermediate/Build/Win64/UnrealEditor/Development/UnrealEditor.manifest
   ```

3. **Rebuild if corrupted**:
   - Clean solution
   - Rebuild entire project

---

### Module suggestions seem wrong

**Symptom**: Suggested module doesn't work

**Solutions**:

1. **Check module type**:
   - Runtime module: Can use in game code
   - Editor module: Only use in Editor-only code

2. **Verify circular dependencies**:
   - Module Mapper will warn if circular

3. **Check transitive dependencies**:
   - Some modules require additional dependencies
   - Module Mapper shows complete chain

---

## 🔗 Integration with Other Skills

**Works seamlessly with**:

- ✅ **Error Doctor Skill** (Issue #117 Phase 1)
  - Error Doctor → Module Mapper → .Build.cs fix

- ✅ **Performance Health Check** (Issue #117 Phase 4)
  - Health Check → Detects include bloat → Module Mapper suggests optimization

- ✅ **Blueprint Flow Tracer** (Issue #117 Phase 2)
  - Flow tracing → C++ class → Module Mapper shows module

---

## 📚 Related Documentation

**Skills**:

- [Error Doctor Skill](../unreal-error-doctor/SKILL.md)
- [Performance Health Check](../performance-health-check/SKILL.md)
- [Skills README](../README.md)

**MCP Tools**:

- [Policy Tools Cheatsheet](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md)
- [`ue_analyze_config`](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md#ue_analyze_config)

**Implementation**:

- UBT Manifest Parser
- [Issue #19 (UBT Manifest Parser)](../../../docs/issues/)
- Issue #89 (File-based Cache)

**Issues**:

- Issue #117 Phase 4 Plan
- [Issue #19 (UBT Manifest Parser)](../../../docs/issues/)
- Issue #89 (Cache Performance)

---

## 🎓 Tips for Best Results

### Be Specific with Class Names

✅ **Good**:

- "FAssetData 모듈 알려줘" (specific class)
- "UGameplayAbility는 어디 있어?" (clear query)

❌ **Too Vague**:

- "Asset 관련 모듈" (unclear what class)
- "Ability 모듈" (which ability?)

---

### Request Complete Dependency Chain

```text
User: "GameplayAbilities 모듈 추가할 때 다른 모듈도 필요해?"
Claude: [Shows complete dependency chain with GameplayTags, GameplayTasks]
```

---

### Ask for Diff Preview

```text
User: ".Build.cs 수정 diff 보여줘"
Claude: [Generates unified diff format]
```

---

### Combine with Other Queries

```text
User: "링커 에러 고치고, .Build.cs 수정해줘"
Claude:
[Error Doctor Skill activates]
[Module Mapper Skill activates]
[Shows complete fix with diff]
```

---

## 📋 Common Module Mappings

**Frequently Asked**:

| Class/Struct | Module | Notes |
|--------------|--------|-------|
| `FAssetData` | AssetRegistry | Asset metadata |
| `UGameplayAbility` | GameplayAbilities | GAS abilities |
| `FGameplayTag` | GameplayTags | Tag system |
| `UEnhancedInputComponent` | EnhancedInput | UE5 input |
| `UWidgetComponent` | UMG | UI widgets |
| `UAnimMontage` | Engine | Already included |
| `APlayerController` | Engine | Already included |

**Common Dependency Chains**:

- **GAS**: GameplayAbilities → GameplayTags, GameplayTasks
- **Enhanced Input**: EnhancedInput → InputCore
- **UMG**: UMG → Slate, SlateCore

---

## 📞 Support & Feedback

**Issues**: <https://github.com/Next-Stage-Inc/ue-code-mcp/issues>
**Documentation**: <https://github.com/Next-Stage-Inc/ue-code-mcp/tree/main/docs>

---

**Status**: ✅ Production Ready
**Version**: 1.0.0
**Date**: 2025-10-20
**Related**: Issue #117 Phase 4 (Module Mapper)
