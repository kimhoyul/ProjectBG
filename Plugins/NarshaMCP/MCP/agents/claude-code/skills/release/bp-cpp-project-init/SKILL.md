---
name: bp-cpp-project-init
model: opus
description: "Purpose: Initialize C++ project structure for BP-only UE projects. Adds Source/, .Build.cs, .Target.cs for NarshaMCP plugin usage. Not for BP logic conversion (use bp-to-cpp). Not for BP analysis (use blueprint-flow). Triggers: 'C++ 프로젝트로 전환', 'Source 디렉토리 만들어', 'init C++ project', 'add C++ to project'."
context: fork
agent: general-purpose
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "Proprietary"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Blueprint to C++ Project Initializer

**Purpose**: Add C++ project structure (Source/, .Build.cs, .Target.cs) to Blueprint-only Unreal Engine projects for NarshaMCP plugin usage.

**Issue**: [#1055](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1055)
**Status**: Production Ready (28/28 tests passing)

---

## Activation Triggers

### Auto-Trigger Conditions

**1. Plugin Installation Failure**

- User runs `scripts/install-to-plugin.bat` on Blueprint-only project
- Installer detects missing Source/ directory
- Agent offers automatic conversion before retry

**2. Metadata Generation Failure**

- User attempts to run CUBatch commandlet
- Build fails with "FailedDueToEngineChange" or "Please rebuild from IDE"
- Agent detects Blueprint-only project and offers conversion

**3. Explicit User Request**

- Keywords: "convert to C++", "BP to C++", "add C++ code", "enable C++"
- User explicitly requests Blueprint project conversion

**4. Project Structure Detection**

- Project has .uproject file but no Source/ directory
- .uproject has no "Modules" section

### Manual Invocation

```bash
# Direct script execution
python scripts/convert_bp_to_cpp.py "E:/MyProject/MyProject.uproject"

# Claude Code skill trigger
"Convert this Blueprint project to C++"
```

---

## Workflow

### Phase 1: Pre-Flight Validation (5-10 seconds)

1. Verify `.uproject` exists and is valid JSON
2. Check if already a C++ project (Source/ exists) — skip if so
3. Verify engine templates are available (6 required templates)
4. Create `.uproject.bak` backup

### Phase 2: C++ Structure Generation (10-15 seconds)

Creates the following directory structure:

```text
Source/
├── {ProjectName}/
│   ├── {ProjectName}.Build.cs
│   ├── {ProjectName}.h
│   └── {ProjectName}.cpp
├── {ProjectName}.Target.cs
└── {ProjectName}Editor.Target.cs
```

Template variables substituted: `%COPYRIGHT_LINE%`, `%MODULE_NAME%`, `%TARGET_TYPE%`, `%DEFAULT_BUILD_SETTINGS_VERSION%`, `%EXTRA_MODULE_NAMES%`, `%PUBLIC_DEPENDENCY_MODULE_NAMES%`, `%PRIVATE_DEPENDENCY_MODULE_NAMES%`.

### Phase 3: Project Configuration (5 seconds)

Adds `Modules` section to `.uproject`:

```json
{
    "Modules": [
        {
            "Name": "ProjectName",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        }
    ]
}
```

### Phase 4: Project File Generation (30-60 seconds)

Runs `GenerateProjectFiles.bat` using auto-detected engine path from `EngineAssociation`.

**Fallback**: Right-click `.uproject` -> "Generate Visual Studio project files".

### Phase 5: Validation (5 seconds)

1. Verify all 5 C++ files exist
2. Verify `.uproject` has `Modules` section with correct module name
3. Report success with next steps

**Total time**: ~60-90 seconds.

---

## Success Criteria

| Outcome | Condition |
|---------|-----------|
| **Success** | All 5 C++ files generated, `.uproject` has Modules, backup created |
| **Partial** | C++ files + `.uproject` OK, but `.sln` generation failed (manual fix possible) |
| **Failure** | Any C++ file missing, `.uproject` malformed, or template error — triggers rollback |

### Rollback Procedure

On failure, the converter automatically:
1. Restores `.uproject` from `.uproject.bak`
2. Deletes generated `Source/` directory
3. Cleans up `.sln`, `.vcxproj`, `Binaries/`, `Intermediate/`

---

**Detailed reference**: [REFERENCE.md](REFERENCE.md)
**Usage examples**: [EXAMPLES.md](EXAMPLES.md)

## Output Format

```text
BP → C++ Conversion Report
──────────────────────────
Project: <project name>
Status: SUCCESS | PARTIAL | FAILED

[Generated Files]
  - Source/<Project>.Target.cs
  - Source/<Project>Editor.Target.cs
  - Source/<Project>/<Project>.Build.cs
  - Source/<Project>/<Project>.h
  - Source/<Project>/<Project>.cpp

[.uproject Update]
  Modules: [{"Name": "<Project>", "Type": "Runtime", "LoadingPhase": "Default"}]
  Backup: <Project>.uproject.bak

Next Steps:
  1. Open <Project>.sln in Visual Studio
  2. Build project (Ctrl+Shift+B)
  3. Run: scripts/install-to-plugin.bat "<ProjectRoot>"
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| Missing engine templates | Template files not found at expected path | Copy templates from `C:/UE_5.7/Engine/Content/Editor/Templates/*.template` |
| `.uproject` write permission denied | File is read-only or Editor has it locked | Close Unreal Editor, remove read-only attribute, retry conversion |
| `GenerateProjectFiles.bat` not found | Engine path not detected correctly | Right-click `.uproject` -> "Generate Visual Studio project files" manually |
| Source/ directory already exists | Project already has C++ code | Verify existing Source/ files are valid; delete and retry if corrupted |

---

**Script Location**: `scripts/convert_bp_to_cpp.py`
**Last Updated**: 2025-11-16
**Maintainer**: NarshaMCP Development Team
## MCP Tool Examples

```python
# Read Blueprint structure for conversion
ue_manage_blueprint(operation="smart", blueprint_name="BP_Player")
# Generate C++ equivalent
ue_generate_code(operation="smart", base_class="ACharacter", class_name="APlayerCharacter")
# Analyze existing C++ patterns
ue_analyze_symbols(operation="smart", query="ACharacter")
```
