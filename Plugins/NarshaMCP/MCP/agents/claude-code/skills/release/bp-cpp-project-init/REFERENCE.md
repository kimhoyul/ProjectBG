# BP-to-C++ Converter: Technical Reference

Complete technical reference for the Blueprint-to-C++ converter implementation.

---

## API Reference

### BPToCppConverter Class

**Location**: `scripts/convert_bp_to_cpp.py`

```python
class BPToCppConverter:
    """
    Converts Blueprint-only Unreal Engine projects to C++ projects.

    Attributes:
        uproject_path (Path): Path to .uproject file
        project_root (Path): Project root directory
        project_name (str): Project name (from .uproject filename)
        source_dir (Path): Source/ directory path
        module_dir (Path): Source/{ProjectName}/ directory path
        template_dir (Path): Templates directory path
    """
```

---

### Constructor

```python
def __init__(self, uproject_path: Path):
    """
    Initialize converter with project path.

    Args:
        uproject_path (Path): Absolute path to .uproject file

    Raises:
        FileNotFoundError: If .uproject file doesn't exist
        ValueError: If .uproject file is invalid JSON

    Example:
        converter = BPToCppConverter(Path("E:/MyProject/MyProject.uproject"))
    """
```

---

### Core Methods

#### is_cpp_project()

```python
def is_cpp_project(self) -> bool:
    """
    Check if project already has C++ code.

    Returns:
        bool: True if Source/ directory exists and is valid

    Example:
        if converter.is_cpp_project():
            print("Already a C++ project!")
            return
    """
```

---

#### create_source_structure()

```python
def create_source_structure(self) -> None:
    """
    Create Source/ directory structure.

    Creates:
        - Source/
        - Source/{ProjectName}/

    Raises:
        PermissionError: If cannot create directories
        OSError: If filesystem error occurs

    Example:
        converter.create_source_structure()
        assert converter.source_dir.exists()
        assert converter.module_dir.exists()
    """
```

---

#### generate_build_cs()

```python
def generate_build_cs(self) -> None:
    """
    Generate {ProjectName}.Build.cs file.

    Template: templates/GameModule.Build.cs.template

    Variables:
        %MODULE_NAME% → project_name
        %COPYRIGHT_LINE% → copyright header
        %PUBLIC_DEPENDENCY_MODULE_NAMES% → "Core", "CoreUObject", ...

    Output:
        Source/{ProjectName}/{ProjectName}.Build.cs

    Example:
        converter.generate_build_cs()
        assert (converter.module_dir / f"{converter.project_name}.Build.cs").exists()
    """
```

---

#### generate_module_header()

```python
def generate_module_header(self) -> None:
    """
    Generate {ProjectName}.h module header file.

    Template: templates/GameModule.h.template

    Variables:
        %MODULE_NAME% → project_name
        %COPYRIGHT_LINE% → copyright header

    Output:
        Source/{ProjectName}/{ProjectName}.h

    Example:
        converter.generate_module_header()
        header = converter.module_dir / f"{converter.project_name}.h"
        assert "#pragma once" in header.read_text()
    """
```

---

#### generate_module_cpp()

```python
def generate_module_cpp(self) -> None:
    """
    Generate {ProjectName}.cpp module implementation file.

    Template: templates/GameModule.cpp.template

    Variables:
        %MODULE_NAME% → project_name
        %COPYRIGHT_LINE% → copyright header

    Output:
        Source/{ProjectName}/{ProjectName}.cpp

    Example:
        converter.generate_module_cpp()
        cpp = converter.module_dir / f"{converter.project_name}.cpp"
        assert "IMPLEMENT_PRIMARY_GAME_MODULE" in cpp.read_text()
    """
```

---

#### generate_game_target()

```python
def generate_game_target(self) -> None:
    """
    Generate {ProjectName}.Target.cs (Game target).

    Template: templates/GameModule.Target.cs.template

    Variables:
        %MODULE_NAME% → project_name
        %TARGET_TYPE% → "Game"
        %DEFAULT_BUILD_SETTINGS_VERSION% → "BuildSettingsVersion.V5"
        %EXTRA_MODULE_NAMES% → '"{project_name}"'

    Output:
        Source/{ProjectName}.Target.cs

    Example:
        converter.generate_game_target()
        target = converter.source_dir / f"{converter.project_name}.Target.cs"
        assert "TargetType.Game" in target.read_text()
    """
```

---

#### generate_editor_target()

```python
def generate_editor_target(self) -> None:
    """
    Generate {ProjectName}Editor.Target.cs (Editor target).

    Template: templates/EditorModule.Target.cs.template

    Variables:
        %MODULE_NAME% → project_name
        %TARGET_TYPE% → "Editor"
        %DEFAULT_BUILD_SETTINGS_VERSION% → "BuildSettingsVersion.V5"
        %EXTRA_MODULE_NAMES% → '"{project_name}"'

    Output:
        Source/{ProjectName}Editor.Target.cs

    Example:
        converter.generate_editor_target()
        editor_target = converter.source_dir / f"{converter.project_name}Editor.Target.cs"
        assert "TargetType.Editor" in editor_target.read_text()
    """
```

---

#### update_uproject_file()

```python
def update_uproject_file(self) -> None:
    """
    Update .uproject file to add Modules section.

    Modifications:
        - Adds "Modules" array if not present
        - Adds module entry with Name, Type, LoadingPhase

    Module Entry:
        {
            "Name": "{ProjectName}",
            "Type": "Runtime",
            "LoadingPhase": "Default"
        }

    Backup:
        Creates {ProjectName}.uproject.bak before modification

    Raises:
        PermissionError: If .uproject is read-only
        json.JSONDecodeError: If .uproject is malformed

    Example:
        converter.update_uproject_file()
        uproject_data = json.loads(converter.uproject_path.read_text())
        assert "Modules" in uproject_data
        assert uproject_data["Modules"][0]["Name"] == converter.project_name
    """
```

---

#### verify_conversion()

```python
def verify_conversion(self) -> bool:
    """
    Verify conversion completed successfully.

    Checks:
        - All 5 required files exist
        - .uproject has Modules section
        - Modules section contains project module

    Returns:
        bool: True if conversion is complete and valid

    Example:
        if converter.verify_conversion():
            print("Conversion successful!")
        else:
            print("Conversion incomplete!")
    """
```

---

#### convert()

```python
def convert(self) -> bool:
    """
    Execute full conversion workflow.

    Workflow:
        1. Pre-flight validation
        2. Create source structure
        3. Generate all C++ files
        4. Update .uproject file
        5. Verify conversion

    Returns:
        bool: True if conversion successful, False otherwise

    Example:
        converter = BPToCppConverter(uproject_path)
        if converter.convert():
            print("Conversion successful!")
        else:
            print("Conversion failed!")
    """
```

---

## Template Variables

Complete reference for all template variable substitutions.

### Common Variables

| Variable | Description | Example Value |
|----------|-------------|---------------|
| `%COPYRIGHT_LINE%` | Copyright header | `// Copyright Epic Games, Inc. All Rights Reserved.` |
| `%MODULE_NAME%` | Project/module name | `ContentExamples` |

### Build.cs Variables

| Variable | Description | Example Value |
|----------|-------------|---------------|
| `%PUBLIC_DEPENDENCY_MODULE_NAMES%` | Public module dependencies | `"Core", "CoreUObject", "Engine", "InputCore"` |
| `%PRIVATE_DEPENDENCY_MODULE_NAMES%` | Private module dependencies | `""` (empty) |

### Target.cs Variables

| Variable | Description | Example Value |
|----------|-------------|---------------|
| `%TARGET_TYPE%` | Target type (Game/Editor) | `Game` or `Editor` |
| `%DEFAULT_BUILD_SETTINGS_VERSION%` | UBT build settings version | `BuildSettingsVersion.V5` |
| `%EXTRA_MODULE_NAMES%` | Additional module names | `"ContentExamples"` |

---

## File Structure

### Generated Directory Hierarchy

```text
{ProjectRoot}/
├── {ProjectName}.uproject         # Modified (Modules section added)
├── {ProjectName}.uproject.bak     # Backup (original)
├── Source/                        # Created
│   ├── {ProjectName}/             # Created
│   │   ├── {ProjectName}.Build.cs    # Generated
│   │   ├── {ProjectName}.h           # Generated
│   │   └── {ProjectName}.cpp         # Generated
│   ├── {ProjectName}.Target.cs       # Generated
│   └── {ProjectName}Editor.Target.cs # Generated
└── {ProjectName}.sln              # Optional (if GenerateProjectFiles.bat succeeds)
```

---

## Template Files

### Required Templates

All templates must exist in `templates/` directory:

1. **GameModule.Build.cs.template**
   - Purpose: Build configuration for module
   - Variables: `%MODULE_NAME%`, `%COPYRIGHT_LINE%`, `%PUBLIC_DEPENDENCY_MODULE_NAMES%`

2. **GameModule.h.template**
   - Purpose: Module header file
   - Variables: `%MODULE_NAME%`, `%COPYRIGHT_LINE%`

3. **GameModule.cpp.template**
   - Purpose: Module implementation
   - Variables: `%MODULE_NAME%`, `%COPYRIGHT_LINE%`

4. **GameModule.Target.cs.template**
   - Purpose: Game target configuration
   - Variables: `%MODULE_NAME%`, `%TARGET_TYPE%`, `%EXTRA_MODULE_NAMES%`

5. **EditorModule.Target.cs.template**
   - Purpose: Editor target configuration
   - Variables: `%MODULE_NAME%`, `%TARGET_TYPE%`, `%EXTRA_MODULE_NAMES%`

6. **Stub.Target.cs.template**
   - Purpose: Fallback target template
   - Variables: `%MODULE_NAME%`, `%TARGET_TYPE%`

---

## Command-Line Interface

### Basic Usage

```bash
python scripts/convert_bp_to_cpp.py <uproject_path> [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `<uproject_path>` | Path to .uproject file (required) | - |
| `--dry-run` | Preview changes without modifying | `False` |
| `--template-dir <path>` | Custom templates directory | `templates/` |
| `--skip-sln` | Skip .sln generation | `False` |
| `--rollback` | Rollback previous conversion | `False` |
| `--verbose` | Enable verbose output | `False` |
| `--quiet` | Suppress output (errors only) | `False` |

### Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success |
| `1` | General error |
| `2` | Invalid arguments |
| `3` | Project already C++ |
| `4` | Missing templates |
| `5` | Permission denied |
| `6` | Conversion verification failed |

---

## Configuration

### Environment Variables

| Variable | Description | Example |
|----------|-------------|---------|
| `UECODEGEN_ENGINE_DIR` | Unreal Engine installation path | `C:/UE_5.7/Engine` |
| `UECODEGEN_TEMPLATE_DIR` | Custom templates directory | `D:/Templates` |

### .env File

```ini
# Engine configuration
UECODEGEN_ENGINE_DIR=C:/UE_5.7/Engine

# Template configuration
UECODEGEN_TEMPLATE_DIR=templates/

# Conversion options
BP_TO_CPP_SKIP_SLN_GENERATION=false
BP_TO_CPP_VERBOSE=false
```

---

## Testing Framework

### Test Fixtures

**Unit Test Fixtures** (`tests/unit/test_bp_to_cpp_converter.py`):

```python
@pytest.fixture
def blueprint_project_dir(tmp_path: Path) -> Path:
    """Create temporary Blueprint-only project directory."""

@pytest.fixture
def valid_uproject_file(blueprint_project_dir: Path) -> Path:
    """Create valid .uproject file."""

@pytest.fixture
def mock_templates_dir(tmp_path: Path) -> Path:
    """Create mock template files."""
```

**Integration Test Fixtures** (`tests/integration/test_bp_to_cpp_integration.py`):

```python
@pytest.fixture
def bp_project_fixture(tmp_path: Path) -> Path:
    """Create realistic Blueprint-only project."""

@pytest.fixture
def real_templates_dir() -> Path:
    """Point to actual template files."""
```

---

### Test Categories

**Unit Tests** (20 tests):

- Project Detection: 4 tests
- Template System: 5 tests
- File Generation: 5 tests (parametrized)
- Project Modification: 3 tests
- Validation System: 3 tests

**Integration Tests** (8 tests):

- Full Conversion Workflow: 2 tests
- File Generation Validation: 3 tests
- Error Recovery & Rollback: 2 tests
- Edge Cases: 1 test

---

## Performance Benchmarks

### Phase Timing (Typical Project)

| Phase | Time | Description |
|-------|------|-------------|
| Pre-Flight | 5-10s | Validation and template checks |
| Generation | 10-15s | C++ file creation |
| Configuration | 5s | .uproject modification |
| Project Files | 30-60s | .sln generation (optional) |
| Validation | 5s | Verification checks |
| **Total** | **60-90s** | Complete conversion |

### Test Execution Performance

| Test Suite | Tests | Time | Pass Rate |
|------------|-------|------|-----------|
| Unit Tests | 20 | 0.51s | 100% |
| Integration Tests | 8 | 0.40s | 100% |
| **Total** | **28** | **0.91s** | **100%** |

---

## Error Codes Reference

### Validation Errors (1xx)

| Code | Error | Resolution |
|------|-------|------------|
| 101 | `.uproject not found` | Verify project path |
| 102 | `.uproject invalid JSON` | Fix JSON syntax |
| 103 | `Already C++ project` | Skip conversion |

### Template Errors (2xx)

| Code | Error | Resolution |
|------|-------|------------|
| 201 | `Template not found` | Copy from engine |
| 202 | `Template malformed` | Restore from backup |
| 203 | `Variable substitution failed` | Check template syntax |

### Filesystem Errors (3xx)

| Code | Error | Resolution |
|------|-------|------------|
| 301 | `Permission denied` | Close Unreal Editor |
| 302 | `Read-only file` | Remove read-only attribute |
| 303 | `Disk full` | Free disk space |

### Generation Errors (4xx)

| Code | Error | Resolution |
|------|-------|------------|
| 401 | `.sln generation failed` | Manual right-click generation |
| 402 | `UBT not found` | Set UECODEGEN_ENGINE_DIR |
| 403 | `Build.cs generation failed` | Check template |

---

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2025-11-16 | Initial release with 28 tests (Issue #1055) |

---

**See Also**:

- [SKILL.md](SKILL.md) - Main documentation
- EXAMPLES.md - Usage examples
- [Issue #1055](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1055) - Implementation details
