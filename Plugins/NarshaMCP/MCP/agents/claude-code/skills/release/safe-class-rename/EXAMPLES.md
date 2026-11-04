# Safe Class Rename - Examples

## Example 1: Basic PlayerController Rename

### Scenario

Rename `ALCPlayerController` to `ALCNewController` in a project with 5 dependent Blueprints.

### Step 1: Dry-Run Preview

**User Request**:
```text
"ALCPlayerController를 ALCNewController로 이름 변경해줘"
```

**Skill Execution**:
```python
result = ue_generate_code(operation="rename_class"(
    old_class_name="ALCPlayerController",
    new_class_name="ALCNewController",
    project_root="E:/LCP_B",
    dry_run=True
)
```

**Output**:
```yaml
success: true
dry_run: true
changes:
  core_redirects: '+ClassRedirects=(OldName="LCPlayerController",NewName="LCNewController")'
  files_renamed:
    - ["Source/LastContact/Controllers/ALCPlayerController.h", "Source/LastContact/Controllers/ALCNewController.h"]
    - ["Source/LastContact/Controllers/ALCPlayerController.cpp", "Source/LastContact/Controllers/ALCNewController.cpp"]
  class_definitions_updated:
    - "Source/LastContact/Controllers/ALCNewController.h"
    - "Source/LastContact/Controllers/ALCNewController.cpp"
  includes_updated:
    - "Source/LastContact/Characters/ALCCharacter.cpp"
    - "Source/LastContact/GameModes/ALCGameMode.cpp"
    - "Source/LastContact/UI/ALCHUD.cpp"
  forward_declarations_updated:
    - "Source/LastContact/LastContact.h"
  affected_blueprints:
    - name: "BP_PlayerController"
      path: "/Game/Blueprints/Controllers/BP_PlayerController"
      parent: "ALCPlayerController"
      reason: "Direct child of renamed class"
    - name: "BP_AIController"
      path: "/Game/Blueprints/AI/BP_AIController"
      parent: "ALCPlayerController"
      reason: "Direct child of renamed class"
warnings:
  - "DRY RUN MODE - No actual changes made"
  - "Close Unreal Editor before executing (dry_run=False)"
  - "Commit to Git for easy rollback"
  - "2 Blueprint(s) will be affected (CoreRedirects will handle automatically)"
next_steps:
  - "1. Review changes above"
  - "2. Close Unreal Editor if open"
  - "3. Commit to Git (for rollback safety)"
  - "4. Run with dry_run=False to execute"
  - "5. Open Editor - Blueprints will auto-update via CoreRedirects"
```

### Step 2: User Confirmation

**Assistant Response**:
```text
The following changes will be made:

Files to rename (2):
  - ALCPlayerController.h -> ALCNewController.h
  - ALCPlayerController.cpp -> ALCNewController.cpp

Code updates:
  - 2 class definitions
  - 3 #include directives
  - 1 forward declaration

Affected Blueprints (2):
  - BP_PlayerController (direct child)
  - BP_AIController (direct child)

CoreRedirects will be added to automatically redirect Blueprints.

Before proceeding, please:
1. Close Unreal Editor
2. Commit current changes to Git

Ready to execute? (yes/no)
```

### Step 3: Execute Changes

**User**: "yes"

**Skill Execution**:
```python
result = ue_generate_code(operation="rename_class"(
    old_class_name="ALCPlayerController",
    new_class_name="ALCNewController",
    project_root="E:/LCP_B",
    dry_run=False  # EXECUTE
)
```

**Output**:
```yaml
success: true
dry_run: false
changes:
  core_redirects: '+ClassRedirects=(OldName="LCPlayerController",NewName="LCNewController")'
  files_renamed:
    - ["ALCPlayerController.h", "ALCNewController.h"]
    - ["ALCPlayerController.cpp", "ALCNewController.cpp"]
  class_definitions_updated: 2
  includes_updated: 3
  forward_declarations_updated: 1
  affected_blueprints: 2
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

### Step 4: Verification

**Assistant Response**:
```text
Rename completed successfully!

Summary:
  - 2 files renamed
  - 6 code locations updated
  - 2 Blueprints will auto-redirect
  - VS project regenerated

Next steps:
1. Open Unreal Editor
2. Verify BP_PlayerController and BP_AIController load correctly
3. (Optional) Resave Blueprints to remove CoreRedirects dependency
```

---

## Example 2: Complex Scenario with Errors

### Scenario

Attempt to rename a class when Editor is still open.

### Step 1: Execute (Fails)

**User Request**:
```text
"Rename AMyGameMode to AShooterGameMode"
```

**Skill Execution**:
```python
result = ue_generate_code(operation="rename_class"(
    old_class_name="AMyGameMode",
    new_class_name="AShooterGameMode",
    project_root="E:/MyProject",
    dry_run=False
)
```

**Output**:
```yaml
success: false
dry_run: false
changes:
  core_redirects: '+ClassRedirects=(OldName="MyGameMode",NewName="ShooterGameMode")'
  files_renamed: []
  # ...
error: "Cannot rename file 'AMyGameMode.cpp': Access denied (file in use by another process)"
warnings:
  - "Unreal Editor may be locking files"
  - "Close Editor and retry"
```

### Step 2: Recovery

**Assistant Response**:
```text
Rename failed: File is locked by another process.

Likely cause: Unreal Editor is still open

Resolution:
1. Close Unreal Editor completely
2. Close Visual Studio (recommended)
3. Run the rename again

Note: CoreRedirects may have been partially added. Will be cleaned up on retry.
```

### Step 3: Retry After Closing Editor

**User**: "Editor 닫았어, 다시 시도해줘"

**Skill Execution**: (Same as before, now succeeds)

---

## Example 3: Class Not Found

### Scenario

User typos the class name.

### Step 1: Attempt

**User Request**:
```text
"Rename APlayerControler to ANewController"  # Note: typo "Controler"
```

**Skill Execution**:
```python
result = ue_generate_code(operation="rename_class"(
    old_class_name="APlayerControler",  # Typo
    new_class_name="ANewController",
    project_root="E:/MyProject",
    dry_run=True
)
```

**Output**:
```yaml
success: false
error: "Class 'APlayerControler' not found in Source/"
```

### Step 2: Find Correct Name

**Assistant Response**:
```text
Class not found. Let me search for similar classes...

[Uses ue_analyze_symbols(operation="search_symbols", query="*Controller*")]

Found matches:
  1. ALCPlayerController (Source/LastContact/Controllers/)
  2. ALCAIController (Source/LastContact/AI/)
  3. AMyPlayerController (Source/MyGame/Controllers/)

Did you mean "ALCPlayerController"?
```

### Step 3: Correct and Retry

**User**: "ALCPlayerController로 해줘"

**Skill Execution**: (Proceeds with correct name)

---

## Example 4: Blueprint-Only Project

### Scenario

User tries to rename a class in a Blueprint-only project.

### Step 1: Attempt

**User Request**:
```text
"Rename AGameCharacter to APlayerCharacter"
```

**Skill Execution**:
```python
result = ue_generate_code(operation="rename_class"(
    old_class_name="AGameCharacter",
    new_class_name="APlayerCharacter",
    project_root="E:/BlueprintOnlyProject",
    dry_run=True
)
```

**Output**:
```yaml
success: false
error: "No Source/ directory found - this appears to be a Blueprint-only project"
```

### Step 2: Suggest Conversion

**Assistant Response**:
```text
This is a Blueprint-only project (no C++ code).

To rename C++ classes, you need to first convert the project to C++:

Option 1: Use bp-cpp-project-init skill
  "Convert this project to C++"

Option 2: Manual conversion
  1. File > New C++ Class > Empty Class
  2. This creates Source/ directory
  3. Then retry class rename

Would you like me to convert the project to C++ first?
```

---

## Example 5: Dry-Run Only (No Execute)

### Scenario

User just wants to see what would change without executing.

### Step 1: Request Preview

**User Request**:
```text
"What would happen if I rename AMyCharacter to AHeroCharacter? Don't actually do it."
```

**Skill Execution**:
```python
result = ue_generate_code(operation="rename_class"(
    old_class_name="AMyCharacter",
    new_class_name="AHeroCharacter",
    project_root="E:/MyProject",
    dry_run=True  # Always True for preview
)
```

**Output**:
```yaml
success: true
dry_run: true
changes:
  core_redirects: '+ClassRedirects=(OldName="MyCharacter",NewName="HeroCharacter")'
  files_renamed:
    - ["AMyCharacter.h", "AHeroCharacter.h"]
    - ["AMyCharacter.cpp", "AHeroCharacter.cpp"]
  class_definitions_updated: 2
  includes_updated: 12
  forward_declarations_updated: 3
  affected_blueprints:
    - name: "BP_Player"
      parent: "AMyCharacter"
    - name: "BP_Enemy"
      parent: "AMyCharacter"
    - name: "BP_NPC"
      parent: "AMyCharacter"
```

### Step 2: Report Only

**Assistant Response**:
```text
Preview of renaming AMyCharacter to AHeroCharacter:

Files to rename: 2
  - AMyCharacter.h -> AHeroCharacter.h
  - AMyCharacter.cpp -> AHeroCharacter.cpp

Code updates needed: 17 locations
  - 2 class definitions
  - 12 #include directives
  - 3 forward declarations

Affected Blueprints: 3
  - BP_Player (direct child)
  - BP_Enemy (direct child)
  - BP_NPC (direct child)

This is a preview only - no changes have been made.
Let me know if you want to proceed with the rename.
```

---

## Example 6: Korean Language Workflow

### Complete Korean Interaction

**User**: "ALCPlayerController를 ALCNewController로 클래스 이름 변경해줘"

**Assistant**:
```text
클래스 이름 변경을 미리보기합니다...

[dry_run=True로 실행]

변경 사항 미리보기:

파일 이름 변경 (2개):
  - ALCPlayerController.h -> ALCNewController.h
  - ALCPlayerController.cpp -> ALCNewController.cpp

코드 수정 (6개 위치):
  - 클래스 정의 2개
  - #include 지시문 3개
  - 전방 선언 1개

영향받는 블루프린트 (2개):
  - BP_PlayerController (직접 상속)
  - BP_AIController (직접 상속)

CoreRedirects가 DefaultEngine.ini에 추가되어 블루프린트가 자동으로 리다이렉트됩니다.

실행 전 확인사항:
1. Unreal Editor 종료 여부
2. Git 커밋 완료 여부 (롤백용)

변경을 실행할까요?
```

**User**: "응, 에디터 닫고 커밋했어"

**Assistant**:
```text
변경을 실행합니다...

[dry_run=False로 실행]

완료!

요약:
  - CoreRedirects 추가됨
  - 2개 파일 이름 변경됨
  - 6개 코드 위치 수정됨
  - VS 프로젝트 재생성됨

다음 단계:
1. Unreal Editor 열기
2. BP_PlayerController, BP_AIController가 정상 로드되는지 확인
3. (선택) 블루프린트를 다시 저장하여 .uasset 업데이트
```

---

## Quick Reference

### Command Patterns

| User Intent | Trigger Keywords |
|-------------|------------------|
| Preview rename | "이름 변경해줘", "rename", "what if" |
| Execute rename | "실행해줘", "진행해", "do it" |
| Check status | "영향받는 블루프린트", "affected" |
| Rollback | "되돌려줘", "undo", "rollback" |

### Common Scenarios

| Scenario | Expected Result |
|----------|-----------------|
| Simple rename | Success in ~35 seconds |
| Many Blueprints (10+) | Warning about affected assets |
| Editor open | Error with close instruction |
| Class not found | Search suggestion |
| Blueprint-only project | Convert suggestion |
