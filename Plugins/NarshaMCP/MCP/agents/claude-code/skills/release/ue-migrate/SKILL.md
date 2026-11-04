---
name: ue-migrate
model: opus
description: "Purpose: UE version migration assistant. Scans project for breaking changes, deprecated APIs, outdated assets, and deprecated config settings. Generates comprehensive migration report and applies auto-fixes. Triggers: '버전 업그레이드', '마이그레이션', 'version upgrade', 'migrate', 'UE 버전', '엔진 업그레이드'."
compatibility: "Requires NarshaMCP MCP server (v0.9.7+) with Unreal Engine 5.x C++ project"
argument-hint: "source_version target_version (e.g., 5.4 5.7) | --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Migration Assistant

**Issue**: #7224
**Purpose**: Comprehensive UE engine version migration: scan deprecated APIs/configs/assets, generate migration report, apply auto-fixes.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Flow

1. **Parse arguments**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user for source and target UE versions.
   > If only one version is given, treat it as target and auto-detect source.
2. **Detect versions** (if not provided)
3. **Run scan phase** using MCP tools
4. **Generate report**
5. **Offer auto-fix** (with user confirmation)

---

## Auto-Trigger Phrases

### Korean
- "버전 업그레이드", "마이그레이션 도와줘"
- "UE 5.6에서 5.7로 업그레이드", "엔진 업그레이드"
- "엔진 마이그레이션", "UE 버전 올려줘"

### English
- "version upgrade", "migrate to UE 5.7"
- "engine upgrade", "migration assistant"
- "upgrade from 5.4 to 5.7"

### Disambiguation
- `/ue-migrate`: UE engine version migration (scanning, reports, auto-fixes)
- `/ue-cl-tracker`: CL/commit history tracking (when was X added, which CL fixed Y)
- `/ue-debug`: Error diagnosis (current compile/runtime errors, not version migration)
- NOT for: data migration, database migration, asset type conversion

---

## Workflow

### Step 1: Detect Versions

**Auto-detect source version**:
```
ue_glob(pattern="*.uproject")
```
Then read the `.uproject` file and extract `EngineAssociation` field.

**Target version**: From user input or default to latest in migration DB.

**Validate migration path**:
- Source must be < Target
- Both must be UE 5.x (5.1 through 5.7 supported)

### Step 2: Scan Phase

Run these scans (sequentially for dependency):

#### 2a. Deprecated Config Scan
```python
ue_analyze_config(
    operation="scan_deprecated",
    source_version="5.4",
    target_version="5.7"
)
```

Reports deprecated `.ini` settings found in `Config/` directory.

#### 2b. Migration Database Query
The scan_deprecated call internally queries the migration database for:
- Breaking changes (signature changes, parameter additions)
- Deprecated APIs
- Class removals
- Header relocations
- Config changes

### Step 3: Generate Report

```python
ue_analyze_config(
    operation="generate_migration_report",
    source_version="5.4",
    target_version="5.7"
)
```

Generates a comprehensive Markdown report with 7 sections (Issue #11492 added section 6):
1. **Summary**: Total issues, auto/manual ratio
2. **Breaking Changes**: Severity Error items
3. **Deprecated APIs**: Severity Warning items
4. **Asset Updates**: Outdated assets + ResavePackages command
5. **Config Changes**: Deprecated .ini settings found
6. **Header Relocations**: Module/path moves with .Build.cs update column
7. **Manual Actions**: Items requiring manual intervention

Report is saved to: `{project}/Intermediate/NarshaMCP/reports/migration_report.md`

### Step 4: Present Results

Display the report summary to the user:
- Total issues found
- Auto-fixable vs manual count
- Key breaking changes highlighted
- Link to full report file

### Step 5: Auto-Fix (Optional, User Confirmation Required)

> Always ask before writes. Config renames/removes/section-moves use
> **`apply_migration`** (Issue #20055 / #24720) — **not** per-key `modify_config`
> for `scan_deprecated` findings. Full params/schema: [REFERENCE.md](REFERENCE.md#apply_migration).

If auto-fixable items exist:

1. Dry-run: `ue_analyze_config(operation="apply_migration", source_version=…, target_version=…, dry_run=true)` (defaults: `dry_run=true`, `create_backup=true`, `only_auto_fixable=true`).
2. Show `planned` / `skipped`; get user confirmation.
3. Apply: same call with `dry_run=false`. Check `success`, `applied` / `errors`. Fail-closed → no partial multi-file write.
4. Ad-hoc non-migration edits only: `modify_config` / `modify_config_batch`.
5. C++/API fixes (optional): `ue_fix_errors(operation="engine_upgrade", …)`.

### Step 6: CoreRedirects Generation (Optional)

For class/function renames, generate CoreRedirects entries:
```ini
[CoreRedirects]
+ClassRedirects=(OldName="/Script/MyGame.OldClass",NewName="/Script/MyGame.NewClass")
```

### Step 7: Verification Build (Optional)

```python
ue_build_pipeline(operation="smart")
```

Or:
```python
ue_fix_errors(operation="build_and_fix")
```

### Error Handling

Each step is independent. If one fails:
- Log the failure
- Continue with remaining steps
- Report partial results
- Mark failed steps in the report

Example:
```text
Step 2a: Deprecated Config Scan ✅ (found 5 matches)
Step 2b: Migration DB Query ✅ (12 breaking, 8 deprecated)
Step 3:  Report Generated ✅
Step 5:  Auto-Fix ❌ (Editor not connected — skipped)
Step 7:  Build Verify ❌ (UBT not found — skipped)
```

---

## Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| source_version | string | auto-detect | Source UE version (e.g., "5.4") |
| target_version | string | latest | Target UE version (e.g., "5.7") |

---

## MCP Tools Used

| Tool | Operation | Purpose |
|------|-----------|---------|
| `ue_analyze_config` | `scan_deprecated` | Find deprecated config settings |
| `ue_analyze_config` | `apply_migration` | Apply auto-fixable config renames/removes/section-moves (dry_run default; Issue #20055) |
| `ue_analyze_config` | `generate_migration_report` | Generate comprehensive report |
| `ue_fix_errors` | `engine_upgrade` | Apply C++/API auto-fixes (optional) |
| `ue_build_pipeline` | `smart` | Verification build (optional) |
| `ue_glob` | - | Find .uproject for version detection |
| `ue_read` | - | Read .uproject for EngineAssociation |

---

## Example Output

```text
=== UE Migration Report: 5.4 → 5.7 ===

Summary:
  Total issues: 24
  Breaking changes: 8 (3 auto-fixable)
  Deprecated APIs: 10 (6 auto-fixable)
  Deprecated configs: 4 (2 auto-fixable)
  Assets needing resave: 142
  Auto-fixable: 11 / Manual: 13

Report saved to: Intermediate/NarshaMCP/reports/migration_report.md

Would you like to apply the 11 auto-fixable changes?
```

### Example: 5.7 → 5.8 (ECABridge removal — common case, Issue #11395)

```text
=== UE Migration Report: 5.7 → 5.8 ===

Summary:
  Breaking changes: 2 (0 auto-fixable)
  Header relocations: 0
  Deprecated APIs: 0
  Config changes: 0
  Auto-fixable: 0 / Manual: 2

Top change: ECABridge plugin removed.
  Action: Remove 'ECABridge' from .uproject Plugins[].
          Enable 'MCPClientToolset' in Editor Preferences > Plugins > MCP Toolset Servers.

Also renamed: AllRestrictedToolsets → AllToolsets (umbrella .uplugin).

Report saved to: Intermediate/NarshaMCP/reports/migration_report.md
```

---

## Self-Test Mode

`/ue-migrate --self-test` validates skill integrity without performing an actual migration.

### Checks

1. **MCP Health**: `ue_check_health()` responsive
2. **scan_deprecated**: `success:true`, `matches[]`, `total_deprecated_settings`
3. **apply_migration (dry-run)**: `dry_run=true` → `success:true`, `planned`/`skipped` present
4. **generate_migration_report**: 7-section md, `report_path` + `summary`
5. **Routing**: `routing.json` has `ue-migrate` triggers

```text
=== /ue-migrate Self-Test ===  → 5/5 PASS (health, scan_deprecated, apply_migration dry-run, report, routing)
```
## MCP Tool Examples

```python
# Find files to migrate
ue_glob(pattern="Source/**/*.h")
# Read migration target
ue_read(identifier="ABaseCharacter")
```
