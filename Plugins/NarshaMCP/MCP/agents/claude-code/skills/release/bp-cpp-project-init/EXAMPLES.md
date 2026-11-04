# BP-to-C++ Converter: Usage Examples

Real-world examples of using the Blueprint-to-C++ converter in different scenarios.

---

## Example 1: Convert Content Examples Project

**Scenario**: Converting Epic's Content Examples (Blueprint-only) to enable NarshaMCP plugin.

### Step 1: Verify Project Type

```bash
$ python scripts/convert_bp_to_cpp.py "E:/UnrealSample/ContentExamples/ContentExamples.uproject" --dry-run

[INFO] Project: ContentExamples
[INFO] Engine: 5.3
[INFO] Type: Blueprint-only (no Source/ directory)
[INFO] Templates: 6/6 found
[INFO] Dry-run mode: No changes will be made
```

### Step 2: Run Conversion

```bash
$ python scripts/convert_bp_to_cpp.py "E:/UnrealSample/ContentExamples/ContentExamples.uproject"

[*] Pre-Flight Validation...
   ✓ .uproject file valid
   ✓ Templates found (6/6)
   ✓ Backup created: ContentExamples.uproject.bak

[*] Creating Source/ directory structure...
   Created: E:/UnrealSample/ContentExamples/Source/ContentExamples/

[*] Generating ContentExamples.Build.cs...
   Created: Source/ContentExamples/ContentExamples.Build.cs

[*] Generating ContentExamples.h...
   Created: Source/ContentExamples/ContentExamples.h

[*] Generating ContentExamples.cpp...
   Created: Source/ContentExamples/ContentExamples.cpp

[*] Generating ContentExamples.Target.cs...
   Created: Source/ContentExamples.Target.cs

[*] Generating ContentExamplesEditor.Target.cs...
   Created: Source/ContentExamplesEditor.Target.cs

[*] Updating .uproject file...
   Added Modules section

[*] Generating Visual Studio project files...
   Running GenerateProjectFiles.bat...
   ✓ Generated: ContentExamples.sln

[SUCCESS] Conversion completed successfully!

Next steps:
1. Open ContentExamples.sln in Visual Studio
2. Build the project (Ctrl+Shift+B)
3. Run: scripts/install-to-plugin.bat "E:/UnrealSample/ContentExamples"
```

### Step 3: Verify Conversion

```bash
$ ls E:/UnrealSample/ContentExamples/Source/
ContentExamples/
ContentExamples.Target.cs
ContentExamplesEditor.Target.cs

$ ls E:/UnrealSample/ContentExamples/Source/ContentExamples/
ContentExamples.Build.cs
ContentExamples.cpp
ContentExamples.h
```

---

## Example 2: Claude Code Integration

**Scenario**: User wants to test NarshaMCP on Content Examples but encounters Blueprint-only project.

### Conversation Flow

**User**: "I need to test NarshaMCP on Content Examples but it's a Blueprint project"

**Claude**: "I'll convert Content Examples to C++ automatically using the bp-cpp-project-init skill."

**[Executes conversion script]**

**Claude**:

```text
✅ Conversion complete! Content Examples is now a C++ project.

Files created:
- Source/ContentExamples/ContentExamples.Build.cs
- Source/ContentExamples/ContentExamples.h
- Source/ContentExamples/ContentExamples.cpp
- Source/ContentExamples.Target.cs
- Source/ContentExamplesEditor.Target.cs

Next, I'll install NarshaMCP plugin and generate metadata.

Would you like me to proceed with:
1. Building the project
2. Installing NarshaMCP plugin
3. Generating Blueprint metadata
```

---

## Example 3: Error Recovery Workflow

**Scenario**: Plugin installation fails on Blueprint-only project, automatic recovery.

### Failure Detection

**User**: "Install NarshaMCP to my project"

**Claude**: [Runs installer]

```bash
$ scripts/install-to-plugin.bat "D:/MyGame"

[ERROR] Project is Blueprint-only!
NarshaMCP requires a C++ project with Source/ directory.

Project: MyGame
Engine: 5.4
Source/: Not found
```

### Automatic Recovery Offer

**Claude**:

```text
I detected that your project is Blueprint-only. NarshaMCP requires C++ projects.

I can automatically convert your project to C++. This will:
✓ Create Source/ directory with minimal C++ module
✓ Preserve all Blueprint content
✓ Enable NarshaMCP plugin compatibility
✓ Take approximately 60-90 seconds

Would you like me to proceed with conversion?
```

**User**: "Yes"

**Claude**: [Executes bp-cpp-project-init skill]

```text
[*] Converting MyGame to C++ project...

Phase 1: Pre-Flight Validation... ✓
Phase 2: C++ Structure Generation... ✓
Phase 3: Project Configuration... ✓
Phase 4: Project File Generation... ✓
Phase 5: Validation... ✓

[SUCCESS] Conversion complete!

Now retrying NarshaMCP installation...
```

**Claude**: [Retries installation]

```bash
$ scripts/install-to-plugin.bat "D:/MyGame"

[*] Installing NarshaMCP to MyGame...
   ✓ Copied plugin files
   ✓ Updated .mcp.json
   ✓ Configured PostBuildSteps

[SUCCESS] NarshaMCP installed successfully!
```

---

## Example 4: Batch Conversion

**Scenario**: Converting multiple Blueprint projects for testing.

### Batch Script

```bash
#!/bin/bash
# convert_multiple_projects.sh

projects=(
    "E:/UE_Projects/Puzzle/Puzzle.uproject"
    "E:/UE_Projects/ThirdPerson/ThirdPerson.uproject"
    "E:/UE_Projects/TopDown/TopDown.uproject"
)

for project in "${projects[@]}"; do
    echo "Converting: $project"
    python scripts/convert_bp_to_cpp.py "$project"

    if [ $? -eq 0 ]; then
        echo "✓ Success: $project"
    else
        echo "✗ Failed: $project"
    fi
    echo "---"
done
```

### Execution

```bash
$ bash convert_multiple_projects.sh

Converting: E:/UE_Projects/Puzzle/Puzzle.uproject
[SUCCESS] Conversion completed successfully!
✓ Success: E:/UE_Projects/Puzzle/Puzzle.uproject
---
Converting: E:/UE_Projects/ThirdPerson/ThirdPerson.uproject
[SUCCESS] Conversion completed successfully!
✓ Success: E:/UE_Projects/ThirdPerson/ThirdPerson.uproject
---
Converting: E:/UE_Projects/TopDown/TopDown.uproject
[SUCCESS] Conversion completed successfully!
✓ Success: E:/UE_Projects/TopDown/TopDown.uproject
---
```

---

## Example 5: Rollback After Failed Conversion

**Scenario**: Conversion fails midway, need to rollback.

### Failed Conversion

```bash
$ python scripts/convert_bp_to_cpp.py "E:/BadProject/BadProject.uproject"

[*] Pre-Flight Validation... ✓
[*] Creating Source/ directory structure... ✓
[*] Generating BadProject.Build.cs... ✓
[*] Generating BadProject.h... ✓
[*] Generating BadProject.cpp... ✓
[ERROR] Failed to update .uproject file: Permission denied

[ROLLBACK] Reverting changes...
   Restored: BadProject.uproject.bak → BadProject.uproject
   Deleted: Source/ directory
   Deleted: Generated .sln files

[ERROR] Conversion failed. Project restored to original state.
```

### Manual Rollback

```bash
$ python scripts/convert_bp_to_cpp.py "E:/MyProject/MyProject.uproject" --rollback

[*] Rolling back BP-to-C++ conversion...
   ✓ Restored .uproject from backup
   ✓ Removed Source/ directory
   ✓ Cleaned up generated files

[SUCCESS] Project restored to Blueprint-only state
```

---

## Example 6: Conversion with Custom Templates

**Scenario**: Using custom company-specific templates.

### Custom Template Setup

```bash
# Copy custom templates
cp company_templates/*.template templates/

# templates/
# ├── GameModule.Build.cs.template (custom)
# ├── GameModule.h.template (custom)
# └── ... (5 more custom templates)
```

### Conversion with Custom Templates

```bash
$ python scripts/convert_bp_to_cpp.py "E:/CompanyProject/CompanyProject.uproject" --template-dir="templates/"

[*] Using custom templates from: templates/
[*] Pre-Flight Validation...
   ✓ Custom templates found (6/6)

[*] Generating files from custom templates...
   Created: Source/CompanyProject/CompanyProject.Build.cs
   (using custom company copyright header)

[SUCCESS] Conversion completed with custom templates!
```

---

## Example 7: Dry-Run Mode

**Scenario**: Preview changes before actual conversion.

### Dry-Run Execution

```bash
$ python scripts/convert_bp_to_cpp.py "E:/TestProject/TestProject.uproject" --dry-run

[DRY-RUN MODE] No changes will be made

[*] Pre-Flight Validation...
   ✓ .uproject file: E:/TestProject/TestProject.uproject
   ✓ Project name: TestProject
   ✓ Engine version: 5.3
   ✓ Templates: 6/6 found

[*] Planned Changes:
   CREATE: E:/TestProject/Source/
   CREATE: E:/TestProject/Source/TestProject/
   CREATE: E:/TestProject/Source/TestProject/TestProject.Build.cs
   CREATE: E:/TestProject/Source/TestProject/TestProject.h
   CREATE: E:/TestProject/Source/TestProject/TestProject.cpp
   CREATE: E:/TestProject/Source/TestProject.Target.cs
   CREATE: E:/TestProject/Source/TestProjectEditor.Target.cs
   MODIFY: E:/TestProject/TestProject.uproject (add Modules section)
   BACKUP: E:/TestProject/TestProject.uproject.bak

[*] Template Variables:
   %MODULE_NAME% = TestProject
   %COPYRIGHT_LINE% = // Copyright Epic Games, Inc. All Rights Reserved.
   %TARGET_TYPE% = Game | Editor
   %PUBLIC_DEPENDENCY_MODULE_NAMES% = "Core", "CoreUObject", "Engine", "InputCore"

[DRY-RUN] Conversion plan validated. Run without --dry-run to proceed.
```

---

## Example 8: CI/CD Integration

**Scenario**: Automated testing pipeline with BP-to-C++ conversion.

### GitHub Actions Workflow

```yaml
name: Test NarshaMCP with Blueprint Projects

on:
  push:
    branches: [ main ]

jobs:
  test-bp-conversion:
    runs-on: windows-latest

    steps:
      - uses: actions/checkout@v2

      - name: Setup Python
        uses: actions/setup-python@v2
        with:
          python-version: '3.10'

      - name: Download Blueprint Test Project
        run: |
          curl -O https://example.com/test-bp-project.zip
          unzip test-bp-project.zip

      - name: Convert BP to C++
        run: |
          python scripts/convert_bp_to_cpp.py "TestBPProject/TestBPProject.uproject"

      - name: Verify Conversion
        run: |
          test -d "TestBPProject/Source" || exit 1
          test -f "TestBPProject/Source/TestBPProject.Target.cs" || exit 1
          test -f "TestBPProject/Source/TestBPProject/TestBPProject.Build.cs" || exit 1

      - name: Run Tests
        run: |
          pytest tests/unit/test_bp_to_cpp_converter.py -v
          pytest tests/integration/test_bp_to_cpp_integration.py -v
```

---

**See Also**:

- [SKILL.md](SKILL.md) - Main documentation
- REFERENCE.md - Technical reference
- [QUICK_START_INSTALLATION.md](../../../docs/getting-started/QUICK_START_INSTALLATION.md) - Installation guide
