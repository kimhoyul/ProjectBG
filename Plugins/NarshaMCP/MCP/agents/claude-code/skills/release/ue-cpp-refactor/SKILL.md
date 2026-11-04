---
name: ue-cpp-refactor
model: opus
description: "Purpose: Safe C++ refactoring operations using ue_generate_code refactoring ops. Rename classes/functions with CoreRedirects, extract methods, change signatures, introduce fields, inline, and safe delete. All operations support dry_run preview. Triggers: '리팩터링', '리네임', '클래스 이름 변경', '함수 추출', 'refactor', 'rename class', 'rename function', 'extract method', 'change signature', 'safe delete'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "operation: rename|extract|inline|delete|signature, target class or function name, --dry-run (preview only)"
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE C++ Refactor

**Issue**: #6100
**Purpose**: Safe C++ refactoring with dry-run preview and CoreRedirects

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements
1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what class or function to refactor and what operation (rename, extract, inline, delete, signature).
2. **Execute dry_run first** for safety preview
3. **Apply refactoring** only after dry_run confirms safety
4. **Verify result** with symbol analysis

---

## Auto-Trigger Phrases

### Korean
- "클래스 이름 변경", "클래스 리네임"
- "함수 이름 변경", "함수 리네임"
- "메서드 추출", "함수 추출"
- "시그니처 변경", "파라미터 변경"
- "안전 삭제", "사용하지 않는 코드 삭제"
- "인라인", "리팩터링"

### English
- "Rename class", "Rename function"
- "Extract method", "Extract function"
- "Change signature", "Modify parameters"
- "Safe delete", "Remove unused code"
- "Inline function", "Refactor"

---

## Workflow

### Step 1: Identify Refactoring Type

```python
REFACTOR_ROUTES = {
    "rename_class":     ["클래스 이름", "rename class", "리네임 클래스"],
    "rename_function":  ["함수 이름", "rename function", "리네임 함수"],
    "extract_method":   ["메서드 추출", "extract method", "함수 추출"],
    "change_signature": ["시그니처", "signature", "파라미터 변경"],
    "safe_delete":      ["안전 삭제", "safe delete", "삭제"],
    "introduce_field":  ["필드 추가", "introduce field", "멤버 추가"],
    "inline":           ["인라인", "inline"]
}
```
### Step 2: Pre-Refactoring Analysis

Verify the project has C++ source and the target exists:

```python
# 0. Verify project has C++ source files in Source/
# If Source/ is empty (Blueprint-only or engine-code project):
#   → Report: "No user C++ source in Source/. Refactoring engine code requires engine source access."
#   → Suggest: Use ue_analyze_symbols to locate the symbol and understand its scope

# 1. Verify target symbol exists
ue_analyze_symbols(operation="search_symbols", query="<target_class_or_function>")

# 2. Check references (impact scope)
# For class-level operations (rename_class, safe_delete), use impact_analysis:
ue_analyze_symbols(operation="impact_analysis", class_name="<target_class>")
# For function-level operations (rename_function, extract_method, change_signature, inline), use find_callers:
ue_analyze_symbols(operation="find_callers", function_name="<target_function>")
# Note: For class rename/delete, use `impact_analysis` to capture subclass, BP reference,
# and config dependencies. For function-level ops, `find_callers` is sufficient.

# 3. Verify the new name doesn't already exist (for rename operations)
ue_analyze_symbols(operation="search_symbols", query="<new_name>")
# If results found, warn user about potential collision.
```
### Step 3: Dry Run Preview

Always preview before applying:

#### 3A: Rename Class

```python
ue_generate_code(operation="rename_class", class_name="<OldName>", new_name="<NewName>", dry_run=true)
# Generates CoreRedirects for Blueprint compatibility
```
#### 3B: Rename Function

```python
ue_generate_code(
    operation="rename_function",
    class_name="<ClassName>",
    function_name="<OldName>",
    new_name="<NewName>",
    dry_run=true,
)
```
#### 3C: Extract Method

```python
ue_generate_code(
    operation="extract_method",
    class_name="<ClassName>",
    source_function="<SourceFunction>",
    new_method_name="<ExtractedName>",
    start_line=45,
    end_line=72,
    access_specifier="private",
    dry_run=true,
)
```
#### 3D: Change Signature

```python
ue_generate_code(
    operation="change_signature",
    class_name="<ClassName>",
    function_name="<FunctionName>",
    changes=[{"action": "add", "new_name": "DamageSource", "param_type": "AActor*", "position": 2}, {"action": "remove", "old_name": "OldParam"}],
    update_overrides=true,
    dry_run=true,
)
```
#### 3E: Safe Delete

```python
ue_generate_code(operation="safe_delete", file_path="<path_to_file>", dry_run=true)
```
#### 3F: Introduce Field

```python
ue_generate_code(operation="introduce_field", class_name="<ClassName>", field_name="<FieldName>", field_type="float")
```
#### 3G: Inline

```python
ue_generate_code(
    operation="inline",
    class_name="<ClassName>",
    function_name="<FunctionName>",
    inline_all=true,
    dry_run=true,
)
```
### Step 4: Apply (after user reviews dry_run)

```python
# Same call with dry_run=false (or omitted)
ue_generate_code(operation="<selected_op>", **same_params, dry_run=false)
# Note: For `rename_class`, the tool also renames source files
# (e.g., `AMyPawn.h` → `AMyCharacter.h`, `AMyPawn.cpp` → `AMyCharacter.cpp`)
# and updates `#include` directives.
```
### Step 5: Post-Refactoring Verification

```python
# Verify the refactoring was applied correctly
ue_analyze_symbols(operation="search_symbols", query="<new_name>")

# Check for undo capability
ue_generate_code(operation="list_transactions", limit=1)
```
---

## Output Format

```text
=== C++ Refactoring Report ===

Operation: <rename_class|rename_function|extract_method|...>
Target: <class::function or class>
Mode: DRY_RUN / APPLIED

--- Preview (dry_run) ---
Files affected: N
| # | File | Changes |
|---|------|---------|
| 1 | MyCharacter.h | Class rename: AEnemy → ABaseEnemy |
| 2 | MyCharacter.cpp | 12 references updated |
| 3 | DefaultEngine.ini | CoreRedirect added |

--- CoreRedirects (if rename) ---
[CoreRedirects]
+ClassRedirects=(OldName="/Script/MyGame.AEnemy", NewName="/Script/MyGame.ABaseEnemy")

--- Impact ---
Direct call sites updated: N
Blueprint references: M (CoreRedirect handles)
Override sites: K

--- Undo ---
Transaction ID: txn_<id>
Undo command: ue_generate_code(operation="undo_refactoring", transaction_id="txn_<id>")
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Symbol not found in PDB | Class/function not in PDB index | `ue_analyze_symbols(operation="search_symbols")` with broader query or wildcard |
| Function not found in Source/ | Project has no C++ source (Blueprint-only) or target is engine code | 1) Verify `Source/` contains `.h`/`.cpp` files 2) If engine function: cannot refactor without engine source access 3) Use `ue_analyze_symbols` to locate the function and understand its scope |
| Rename conflicts | New name already exists | Report conflict; suggest alternative name |
| Extract method fails | Line range doesn't form valid extraction block | Adjust line range; check for control flow crossing extraction boundary |
| Safe delete blocked | References still exist | `ue_analyze_symbols(operation="find_callers")` to list all references |
| Undo fails | Transaction expired or already undone | `ue_generate_code(operation="list_transactions")` to check history |

---

## Activation Test Cases

**Positive (5)** - Should activate:
1. "AEnemy를 ABaseEnemy로 리네임" -> rename_class
2. "Rename TakeDamage to ApplyDamageToTarget" -> rename_function
3. "BeginPlay에서 메서드 추출" -> extract_method
4. "Change TakeDamage signature to add DamageSource param" -> change_signature
5. "안 쓰는 OldHelper.h 안전 삭제" -> safe_delete

**Negative (3)** - Should NOT activate:
1. "ACharacter 상속받아 새 클래스 만들어줘" -> ue-scaffold skill
2. "빌드 에러 수정" -> ue-debug skill
3. "코드 설명해줘" -> ue-explain skill

---

**Status**: v1.1.0
**Related**: Issue #6100
