---
name: safe-class-rename
model: opus
description: "Purpose: Safely rename C++ classes with CoreRedirects and Blueprint impact analysis. Issue #2700."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools: [Bash, Read, Grep, Glob, Edit, Write]
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Safe Class Rename Agent Skill

**Purpose**: Safely rename C++ classes with automatic CoreRedirects, Blueprint impact analysis, and project-wide file updates. Rider replacement ($299/year value).

**Issue**: #2700 (Rider Refactoring Parity)
**Status**: Production Ready

---

## Overview

This Agent Skill provides JetBrains Rider's premium C++ class rename functionality for free by automating the complete 7-step rename process:

1. Add CoreRedirects to DefaultEngine.ini (Blueprint auto-redirect)
2. Analyze Blueprint impact (warnings, no modification needed)
3. Rename .h and .cpp files
4. Update class definitions in source files
5. Update #include directives project-wide
6. Update forward declarations
7. Regenerate Visual Studio project files

**Key Benefits**: 10-30x faster (Rust-based), zero Blueprint breakage (CoreRedirects), safe dry_run preview, Git rollback.

---

## Activation Triggers

**Korean**: "클래스 이름 변경", "클래스명 변경", "클래스 리네임", "안전하게 클래스 이름 바꿔"
**English**: "rename class", "safe rename", "refactor class name", "change class name"

**Examples**: "ALCPlayerController를 ALCNewController로 이름 변경해줘", "Rename AMyCharacter to AShooterCharacter"

**Manual Invocation**:
```python
ue_generate_code(
    operation="rename_class",
    old_class_name="ALCPlayerController",
    new_class_name="ALCNewController",
    dry_run=true,
)
```

---

## Workflow

### Phase 1: Pre-Flight Validation (5-10 seconds)

**Objective**: Verify project structure and class existence before making changes.

```python
# 1.1 Project root validation
project_path = Path(project_root)
if not project_path.exists():
    return ERROR: "Project root not found"

# 1.2 .uproject file check
uproject_files = list(project_path.glob("*.uproject"))
if not uproject_files:
    return ERROR: "No .uproject file found"

# 1.3 Source directory check
if not (project_path / "Source").exists():
    return ERROR: "No Source/ directory - is this a Blueprint-only project?"

# 1.4 Class file existence check
header_file = find_class_header(old_class_name)
if not header_file:
    return ERROR: f"Class {old_class_name} not found in Source/"
```

**Critical Checks**:
- Project root exists
- .uproject file present
- Source/ directory exists
- Class files (.h/.cpp) found

### Phase 2: Dry-Run Preview (10-20 seconds)

**Objective**: Show all changes that will be made without modifying any files.

```python
result = ue_generate_code(
    operation="rename_class",
    old_class_name="ALCPlayerController",
    new_class_name="ALCNewController",
    dry_run=true,
)
```

**Preview Output**:

```yaml
success: true
dry_run: true
changes:
  core_redirects: '+ClassRedirects=(OldName="LCPlayerController",NewName="LCNewController")'
  files_renamed:
    - ["ALCPlayerController.h", "ALCNewController.h"]
    - ["ALCPlayerController.cpp", "ALCNewController.cpp"]
  class_definitions_updated:
    - "Source/MyGame/Controllers/ALCNewController.h"
    - "Source/MyGame/Controllers/ALCNewController.cpp"
  includes_updated:
    - "Source/MyGame/Characters/ALCCharacter.cpp"
    - "Source/MyGame/GameModes/ALCGameMode.cpp"
  forward_declarations_updated:
    - "Source/MyGame/MyGame.h"
  affected_blueprints:
    - name: "BP_PlayerController"
      path: "/Game/Blueprints/BP_PlayerController"
      parent: "ALCPlayerController"
      reason: "Direct child of renamed class"
warnings:
  - "DRY RUN MODE - No actual changes made"
  - "Close Unreal Editor before executing (dry_run=False)"
  - "5 Blueprint(s) will be affected (CoreRedirects will handle automatically)"
```

**User Confirmation Required**:

```text
The following changes will be made:
- 2 files renamed
- 5 includes updated
- 1 forward declaration updated
- 5 Blueprints affected (auto-redirected)

Proceed with dry_run=False? (Ensure Editor is closed and Git committed)
```

### Phase 3: User Confirmation

**Objective**: Get explicit user approval before modifying files.

**Confirmation Checklist**:
1. [ ] Unreal Editor is closed
2. [ ] Changes committed to Git (for rollback)
3. [ ] Reviewed affected Blueprints
4. [ ] Understood next steps

**If User Declines**:
- No changes made
- Skill exits gracefully
- User can re-run with modifications

### Phase 4: Execute Changes (20-60 seconds)

**Objective**: Apply all changes with detailed progress reporting.

```python
result = ue_generate_code(
    operation="rename_class",
    old_class_name="ALCPlayerController",
    new_class_name="ALCNewController",
    dry_run=false,
)
```

**Execution Steps**:

```text
[1/7] Adding CoreRedirects to DefaultEngine.ini...
      +ClassRedirects=(OldName="LCPlayerController",NewName="LCNewController")

[2/7] Analyzing Blueprint impact...
      Found 5 affected Blueprints (CoreRedirects will handle automatically)

[3/7] Renaming files...
      ALCPlayerController.h -> ALCNewController.h
      ALCPlayerController.cpp -> ALCNewController.cpp

[4/7] Updating class definitions...
      Updated 2 files

[5/7] Updating #include directives...
      Updated 5 files

[6/7] Updating forward declarations...
      Updated 1 file

[7/7] Regenerating Visual Studio project...
      Running UnrealVersionSelector.exe...
      Done!
```

### Phase 5: Result Verification (5-10 seconds)

**Objective**: Confirm all changes applied successfully.

```python
# Verification checks
assert new_header.exists(), "New header file not created"
assert new_cpp.exists(), "New cpp file not created"
assert not old_header.exists(), "Old header still exists"
assert "ClassRedirects" in config_content, "CoreRedirects not added"
```

**Success Output**:

```yaml
success: true
dry_run: false
changes:
  core_redirects: '+ClassRedirects=(...)'
  files_renamed: 2
  class_definitions_updated: 2
  includes_updated: 5
  forward_declarations_updated: 1
  project_regenerated: true
warnings:
  - "Changes applied successfully"
  - "Binaries/, Intermediate/, Saved/ NOT deleted (manual cleanup recommended)"
next_steps:
  - "1. CoreRedirects added to DefaultEngine.ini"
  - "2. C++ files renamed and updated"
  - "3. Project files regenerated"
  - "4. Open Unreal Editor - Blueprints will auto-redirect"
  - "5. (Optional) Save affected Blueprints to update .uasset files"
```

### Phase 6: Next Steps Guidance

**Post-Execution Instructions**:

1. **Open Unreal Editor**
   - Blueprints will automatically redirect via CoreRedirects
   - No manual Blueprint editing required

2. **Verify Blueprint Loading**
   - Check for any load errors in Output Log
   - Affected Blueprints should open normally

3. **Optional: Save Blueprints**
   - Resave affected Blueprints to update .uasset files
   - Removes dependency on CoreRedirects entry

4. **Build Project**
   - Full rebuild recommended after major class rename
   - `Build > Rebuild Solution` in Visual Studio

5. **Clean Intermediate Files** (if issues occur)
   ```bash
   # Delete cached data
   rm -rf Binaries/ Intermediate/ Saved/
   # Regenerate project
   right-click .uproject > "Generate Visual Studio project files"
   ```

---

## ⚠️ Common Failures

| 에러 유형 | 원인 | 해결 방법 |
|-----------|------|-----------|
| `Class not found in Source/` | 클래스명 오타 또는 Engine 클래스 (프로젝트 외부) | `ue_analyze_symbols`로 정확한 클래스명 검색, 프로젝트 Source/ 내 클래스만 지원 |
| `Access denied` 파일 이름 변경 실패 | Unreal Editor 또는 Visual Studio가 파일 잠금 | Editor와 VS 모두 종료 후 재실행 |
| `Target file already exists` 충돌 | 새 클래스명과 동일한 파일이 이미 존재 | 다른 이름 선택 또는 기존 충돌 파일 삭제/이동 |
| CoreRedirects 적용 후 Blueprint 로드 실패 | `DefaultEngine.ini` 저장 인코딩 문제 또는 잘못된 OldName 형식 | OldName에서 접두사(A/U/F) 제거 확인, INI 파일 UTF-8 인코딩 검증 |
| Blueprint metadata JSON 미존재 | Blueprint 분석용 메타데이터 미생성 | v7.0.0: Blueprint metadata is now auto-generated by `narshamcp.exe`. If metadata is missing, run: `UnrealEditor-Cmd.exe <project>.uproject -run=NarshaMCP.MetadataCommandlet` |
| `#include` 업데이트 누락 (일부 파일) | 비표준 include 경로 또는 조건부 include | `Grep`으로 이전 클래스명 잔여 참조 수동 확인 |

## Error Handling

### Error 1: Class Not Found

**Symptom**:
```text
Error: Class "AMyController" not found in Source/
```

**Causes**:
- Typo in class name
- Class in a different module
- Class is Engine class (not project class)

**Resolution**:
```python
# Use PDB symbol search to find correct name
ue_analyze_symbols(operation="search_symbols", query="*Controller*")
```

### Error 2: Project Root Invalid

**Symptom**:
```text
Error: Project root not found: E:/WrongPath
```

**Resolution**:
- Verify path exists
- Use absolute path, not relative
- Check for typos in path

### Error 3: Blueprint Analyzer Initialization Failed

**Symptom**:
```text
Warning: Blueprint impact analysis failed (metadata JSON not found)
```

**Cause**: Blueprint metadata not generated yet.

**Resolution**:
```bash
# v7.0.0: Blueprint metadata is now auto-generated by narshamcp.exe
# If metadata is missing, run:
UnrealEditor-Cmd.exe <project>.uproject -run=NarshaMCP.MetadataCommandlet
```

### Error 4: File Rename Conflict

**Symptom**:
```text
Error: Target file already exists: ANewController.h
```

**Cause**: New class name already exists in project.

**Resolution**:
- Choose a different new class name
- Or manually delete/rename the conflicting file

### Error 5: Editor Lock

**Symptom**:
```text
Error: Cannot rename file - access denied (file in use)
```

**Cause**: Unreal Editor has files locked.

**Resolution**:
1. Close Unreal Editor completely
2. Close Visual Studio (optional but recommended)
3. Re-run the rename command

---

## Rollback Procedure

### Option A: Git Rollback (Recommended)

```bash
# If committed before rename
git checkout -- .

# If committed after rename
git revert HEAD
```

### Option B: Manual Rollback

1. Rename files back:
   ```bash
   mv ANewController.h AOldController.h
   mv ANewController.cpp AOldController.cpp
   ```

2. Remove CoreRedirects entry from DefaultEngine.ini

3. Find/replace class name in source files

4. Regenerate project files

---

## Success Criteria

- [ ] CoreRedirects entry added to DefaultEngine.ini
- [ ] Header and source files renamed
- [ ] All class definitions updated
- [ ] All #include directives updated
- [ ] All forward declarations updated
- [ ] Project files regenerated
- [ ] Blueprints load without errors

---

## Performance Metrics

| Operation | Time (500 files) |
|-----------|-----------------|
| Pre-flight validation | ~5 sec |
| Dry-run preview | ~10 sec |
| CoreRedirects | <1 sec |
| Blueprint analysis | ~5 sec |
| File rename | ~2 sec |
| Code updates (Rust) | ~3 sec |
| Project regeneration | ~10 sec |
| **Total** | **~35 sec** |

**Comparison**:
- Manual process: 5-10 minutes
- This skill: ~35 seconds
- **Speedup: 10-17x**

---

## Integration Points

### Related Tools

| Tool | Purpose |
|------|---------|
| `ue_analyze_symbols` | Find class names and callers |
| `ue_fix_errors` | Fix compilation errors after rename |
| `ue_generate_code` | Generate new classes |

### Module Dependencies

```text
MCP/tools/safe_rename_tools.py
    ├── MCP/parsers/class_renamer.py (Rust-based)
    ├── MCP/parsers/core_redirects_manager.py
    └── MCP/parsers/blueprint_impact_analyzer.py
```

---

## Limitations

1. **File Name = Class Name Assumption**
   - Assumes `AMyClass` is in `MyClass.h` and `MyClass.cpp`
   - Non-standard file naming may cause issues

2. **Single Module Projects**
   - Cross-module renames may require manual adjustments

3. **Blueprint-Only Projects**
   - Cannot rename Blueprints (C++ classes only)
   - Use `bp-cpp-project-init` skill first if needed

4. **Subclasses Not Auto-Renamed**
   - Only the specified class is renamed
   - Subclasses keep their original names

---

## Related Documentation

- REFERENCE.md - Technical details on CoreRedirects and Rust integration
- EXAMPLES.md - Step-by-step examples with expected output
- [Issue #2700](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/2700) - Implementation tracking

## Output Format

```
=== Safe Class Rename Report ===

Operation: ALCPlayerController → ALCNewController
Mode: dry_run=false

[1/7] CoreRedirects     +ClassRedirects=(OldName="LCPlayerController",NewName="LCNewController")
[2/7] Blueprint Impact  5 affected (auto-redirected via CoreRedirects)
[3/7] Files Renamed     ALCPlayerController.h → ALCNewController.h
                         ALCPlayerController.cpp → ALCNewController.cpp
[4/7] Definitions       2 files updated
[5/7] Includes          5 files updated
[6/7] Forward Decls     1 file updated
[7/7] Project Regen     Done

Result: SUCCESS
Next: Open Editor → Blueprints auto-redirect → Resave affected BPs
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `Class not found in Source/` | Typo in class name or class is an Engine class (outside project) | Use `ue_analyze_symbols(search_symbols)` to find correct name; only project Source/ classes supported |
| `Access denied` during file rename | Unreal Editor or Visual Studio has files locked | Close both Editor and VS completely, then re-run the rename |
| `Target file already exists` | New class name collides with existing file | Choose a different name or manually remove/rename the conflicting file |
