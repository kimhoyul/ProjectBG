---
name: module-mapper
model: opus
description: "Purpose: .Build.cs dependency resolution. 100% accurate module lookup using UBT Manifest for linker errors and .Build.cs modifications. 3-step workflow (Query → Details → Diff). Not for general compilation error fixing (use unreal-error-doctor). Triggers: '링커 에러', 'unresolved external symbol', 'Which module is FAssetData in?', '.Build.cs modification', '모듈 의존성'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Module Mapper

**Purpose**: 100% accurate module dependency resolution using UBT Manifest
**Tools Used**: 2 (`ue_analyze_config` + `ue_editor_automation`)

---

## 🎯 Purpose

Resolve "unresolved external symbol" linker errors and module dependency issues using **UBT (Unreal Build Tool) Manifest** for 100% accurate module suggestions.

**Key Benefits**:

- 100% accurate module suggestions (vs 30-67% pattern-based)
- Zero false positives (real compiler data, not guesses)
- Instant .Build.cs modification guidance
- Automatic dependency chain resolution

---

## 🔍 Auto-Load Trigger Phrases

**Linker error queries**:

- "unresolved external symbol 링커 에러 고쳐줘" / "Fix unresolved external symbol"
- "FAssetData가 어느 모듈이야?" / "Which module is FAssetData in?"
- "링커 에러 LNK2019" / "Linker error LNK2019"

**Module dependency queries**:

- ".Build.cs에 어떤 모듈 추가해야 돼?" / "Which module should I add to .Build.cs?"
- "AssetRegistry 모듈 의존성" / "AssetRegistry module dependency"
- "모듈 추가 방법" / "How to add module"

**Build.cs modification queries**:

- ".Build.cs 수정해줘" / "Modify .Build.cs"
- "PublicDependencyModuleNames에 뭐 추가해?" / "What to add to PublicDependencyModuleNames?"

**Keywords**: `module`, `dependency`, `링커`, `linker`, `unresolved`, `LNK2019`, `.Build.cs`, `PublicDependencyModuleNames`

---

## 🧭 3-Step Workflow (2-Tool Orchestration) 🆕

### Complete Flow Path

```text
Step 1: Query UBT Manifest → Find class/struct definition module
        Tools: ue_analyze_config(operation="search")
Step 2: Show Module Details → Dependencies, API macro, location
        Tools: ue_analyze_config(operation="hierarchy")
Step 3: Generate .Build.cs Diff → Show exact modification needed
        Tools: ue_analyze_config(operation="modify")
Step 4: Runtime Verification (Optional) 🆕
        Tools: ue_editor_automation(operation="get_loaded_modules")
```markdown

**2-Tool Orchestration** 🆕:
- `ue_analyze_config`: UBT Manifest 기반 모듈 의존성 분석 (Offline)
- `ue_editor_automation`: 런타임 모듈 로딩 상태 검증 (Editor 필수)

---

### Step 0: Discovery Pre-Exploration (Issue #6289)

> Before querying UBT Manifest, use discovery tools to confirm the target symbol exists and locate its source file.

```python
# 0a. Quick symbol search
ue_grep(query="<class_or_struct>", domain="symbols", limit=5)
# Returns: symbol matches with source category (~50 tokens)

# 0b. Source file location
ue_grep(query="<class_or_struct>", domain="source", limit=5)
# Returns: header/source files containing the symbol (~50 tokens)
```text

**Step 0 Decision Logic**:
- Symbol found in PDB → Extract module info directly, may skip UBT Manifest query
- Source file found → Show `#include` path for `.Build.cs` guidance
- Not found → Likely engine-only symbol, proceed to UBT Manifest (Step 1)
- Multiple modules → Present disambiguation before Step 1

**Token Savings**: ~25% — `ue_grep(domain="symbols")` provides module hint in ~50 tokens

---

### Step 1: Query UBT Manifest

**Goal**: Find which module defines the missing class/struct

**Tool**: `ue_analyze_config(operation="search", query="FAssetData")`

**Output**:

```text
🔍 Step 1: Module Query

FAssetData is defined in: **AssetRegistry** module
- Header: AssetRegistry/Public/AssetData.h
- API Macro: ASSETREGISTRY_API
- Type: Runtime module
- Dependencies: CoreUObject, Core
```markdown

---

### Step 2: Show Module Details

**Goal**: Provide complete module information including transitive dependencies

**Tool**: `ue_analyze_config(operation="hierarchy", option="AssetRegistry")`

**Output**:

```text
📦 Step 2: Module Details

AssetRegistry Module:
- Location: Runtime/AssetRegistry
- Type: Runtime module
- API Macro: ASSETREGISTRY_API

Dependencies:
- Direct: CoreUObject, Core
- Transitive: CoreUObject, Core, TraceLog

Dependent Modules (who uses this):
- Engine, UnrealEd, AssetTools

⚠️ No circular dependencies detected (safe to add)
```markdown

---

### Step 3: Generate .Build.cs Diff

**Goal**: Show exact .Build.cs modification with unified diff format

**Tool**: `ue_analyze_config(operation="modify", option="AssetRegistry", new_value="add_to_public_dependencies")`

**Output**:

```text
✏️ Step 3: .Build.cs Modification

Add to PublicDependencyModuleNames in Source/MyModule/MyModule.Build.cs:

```diff
--- a/Source/MyModule/MyModule.Build.cs
+++ b/Source/MyModule/MyModule.Build.cs
@@ -12,6 +12,7 @@
         "Core",
         "CoreUObject",
         "Engine",
+        "AssetRegistry",
     });
```text

Impact Analysis:

- ✅ No circular dependency risk
- ✅ No conflicts with existing modules

Next Steps:

1. Add "AssetRegistry" to PublicDependencyModuleNames array (line 15)
2. Rebuild your project
3. The linker error should be resolved

```markdown

---

### Step 4: Runtime Verification (Optional) 🆕

**Goal**: Verify module is actually loaded at runtime (Editor 연결 시)

**Tool**: `ue_editor_automation(operation="get_loaded_modules")`

```python
# Editor가 실행 중일 때 런타임 모듈 상태 확인
ue_cache_control(
    operation="get_loaded_modules",
    params={"filter": "AssetRegistry"}
)
```text

**Output**:

```text
🔄 Step 4: Runtime Module Verification

AssetRegistry Module:
- Build-time Status: ✅ Defined in UBT Manifest
- Runtime Status: ✅ Loaded in Editor
- Load Order: 45 (after CoreUObject)

⚠️ Discrepancy Detection:
- If module defined but not loaded → Plugin 설정 확인 필요
- If loaded but not in Manifest → Hot-reload 또는 플러그인 문제
```text

**이점**:
- 빌드 시점 vs 런타임 불일치 감지
- 모듈 로딩 순서 문제 진단
- 플러그인 활성화 상태 검증

---

## 📊 Quick Example

**User**: "FAssetData 클래스 찾을 수 없다는 링커 에러가 나. 어떤 모듈 추가해야 돼?"

**Module Mapper**:
```text

🔍 Step 1: FAssetData → AssetRegistry 모듈

📦 Step 2: AssetRegistry (Runtime/AssetRegistry)

- Dependencies: CoreUObject, Core
- No circular dependencies

✏️ Step 3: .Build.cs 수정 방법

[Diff shown above]

PublicDependencyModuleNames에 "AssetRegistry" 추가하시면 됩니다.
수정해드릴까요?

```markdown

---

## Output Format

```text
=== Module Mapping: {HeaderOrClass} ===

--- Step 1: UBT Manifest Lookup ---
Header: {header_path}
Module: {module_name}
Type: Runtime | Editor | Developer

--- Step 2: Module Details ---
Module: {module_name}
Build.cs: {path}
Dependencies: {dep1}, {dep2}, ...
Circular Risk: NONE | WARNING

--- Step 3: .Build.cs Diff ---
// Add to {YourModule}.Build.cs:
PublicDependencyModuleNames.AddRange(new string[] {
    "{module_name}"
});

--- Step 4: Runtime Verification ---
Module loaded: YES/NO
Version: {version}
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `Module not found in UBT Manifest` | Header belongs to a plugin not enabled in project | Check plugin `.uplugin` file; enable in project settings or add to `.uproject` |
| `Circular dependency detected` | Adding module would create A→B→A cycle | Use `PrivateDependencyModuleNames` instead, or refactor to break the cycle |
| `Runtime verification fails (module not loaded)` | Module is Editor-only but queried at Runtime | Wrap usage in `#if WITH_EDITOR` guards |

---

## 📚 Related Files

For detailed information, see:
- **EXAMPLES.md** - Complete usage examples and common module mappings
- **ADVANCED.md** - Dependency chains, circular dependency detection, accuracy comparison
- **REFERENCE.md** - Complete workflow details, tool parameters, Epic Games standards

---

**Status**: ✅ Production Ready (Issue #117 Phase 4, #4442 Enhancement)
**Date**: 2026-02-06
**Enhanced**: 2-Tool Orchestration (UBT Manifest + Runtime Verification)
## MCP Tool Examples

```python
# Analyze symbols to find module definitions
ue_analyze_symbols(operation="smart", query="FAssetData")
```
