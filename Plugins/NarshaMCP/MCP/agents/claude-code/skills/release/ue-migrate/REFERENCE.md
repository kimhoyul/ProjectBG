# /ue-migrate Reference

**Version**: 1.0.0
**Issue**: #7224

---

## MCP Operations

### `scan_asset_versions`

Scans .uasset/.umap file headers to detect assets saved in older UE versions. Reads `saved_by_engine_version` from FPackageFileSummary and compares against target version.

**Tool**: `ue_analyze_config`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | yes | `"scan_asset_versions"` |
| `target_version` | string | no | Target UE version (default: `"5.7"`) |

**Return schema**:
```json
{
  "operation": "scan_asset_versions",
  "target_version": "5.7",
  "success": true,
  "total_scanned": 1250,
  "outdated_count": 42,
  "parse_errors": 3,
  "version_distribution": {"5.4": 30, "5.5": 12, "5.7": 1208},
  "outdated_assets": [
    {
      "path": "Content/Effects/BP_Explosion.uasset",
      "asset_version": "5.4",
      "target_version": "5.7",
      "versions_behind": 3
    }
  ],
  "resave_command": "UnrealEditor-Cmd.exe \"D:/Project/Project.uproject\" -run=ResavePackages -ProjectOnly -AllowCommandletRendering -nosplash -unattended",
  "total_time_ms": 150.0
}
```

### `scan_deprecated`

Scans project Config/ directory for deprecated settings matching migration database entries.

**Tool**: `ue_analyze_config`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | yes | `"scan_deprecated"` |
| `source_version` | string | yes | Source UE version (e.g., `"5.4"`) |
| `target_version` | string | yes | Target UE version (e.g., `"5.7"`) |

**Return schema**:
```json
{
  "operation": "scan_deprecated",
  "source_version": "5.4",
  "target_version": "5.7",
  "success": true,
  "total_deprecated_settings": 11,
  "matches": [
    {
      "id": "CONFIG_001",
      "file": "Config/DefaultEngine.ini",
      "line": 42,
      "section": "[/Script/Engine.RendererSettings]",
      "key": "r.DefaultFeature.AutoExposure.Method",
      "current_value": "0",
      "change_type": "renamed",
      "auto_fixable": true,
      "new_key": "r.DefaultFeature.AutoExposure.MethodName",
      "severity": "warning",
      "deprecated_since": "5.4"
    }
  ],
  "total_time_ms": 1.0
}
```

### `apply_migration`

Batch-applies **auto-fixable** config findings from the same path as
`scan_deprecated` (rename / remove / section_move). Prefer this over looping
`modify_config` per key (Issue #20055 / #24720).

**Tool**: `ue_analyze_config`

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `operation` | string | yes | — | `"apply_migration"` |
| `source_version` | string | no | op default | Source UE version (e.g., `"5.4"`) |
| `target_version` | string | no | op default | Target UE version (e.g., `"5.7"`) |
| `dry_run` | boolean | no | `true` | Preview only — no file writes when true |
| `create_backup` | boolean | no | `true` | Write `.bak` siblings when applying (`dry_run=false`) |
| `only_auto_fixable` | boolean | no | `true` | Skip findings with `auto_fixable=false` |

**Workflow**:

1. Always run with `dry_run=true` first; show `planned` / `skipped` to the user.
2. After confirmation, re-call with `dry_run=false` (keep `create_backup=true`).
3. Fail-closed: any per-edit error → `success:false`, `applied_count:0` (no partial multi-file commit).
4. Use `modify_config` / `modify_config_batch` only for ad-hoc edits outside the migration scan.

**Return schema (dry-run success)**:
```json
{
  "operation": "apply_migration",
  "success": true,
  "dry_run": true,
  "source_version": "5.4",
  "target_version": "5.7",
  "matches_scanned": 4,
  "planned_count": 2,
  "applied_count": 0,
  "skipped_count": 2,
  "planned": [{"id": "CONFIG_001", "file": "Config/DefaultEngine.ini", "action": "rename_key"}],
  "skipped": [{"id": "CONFIG_002", "reason": "not_auto_fixable"}],
  "errors": [],
  "total_time_ms": 12
}
```

**Write path (`dry_run=false`)**: same shape with `applied[]` filled and
`applied_count > 0` on success; backups via `create_backup`.

### `generate_migration_report`

Generates comprehensive Markdown migration report aggregating all migration data.

**Tool**: `ue_analyze_config`

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | yes | `"generate_migration_report"` |
| `source_version` | string | yes | Source UE version |
| `target_version` | string | yes | Target UE version |

**Return schema**:
```json
{
  "operation": "generate_migration_report",
  "source_version": "5.4",
  "target_version": "5.7",
  "success": true,
  "report_markdown": "# Migration Report: UE 5.4 → 5.7\n...",
  "report_path": "Intermediate/NarshaMCP/reports/migration_report.md",
  "summary": {
    "total_issues": 15,
    "breaking_changes": 3,
    "deprecated_apis": 8,
    "class_removals": 1,
    "header_relocations": 2,
    "config_matches": 1
  }
}
```

---

## Version Chaining

Multi-version migration iterates intermediate versions:

```
5.4 → 5.7 = (5.4→5.5) + (5.5→5.6) + (5.6→5.7)
```

Each step loads from `MCP/data/engine_migrations/{from}_to_{to}.json`. Results are aggregated across all intermediate versions into a single report.

---

## Config Matching Logic

The scanner uses **Section+Key** combination matching (not key-only):

1. Walk `{project_root}/Config/` for `.ini` files (including platform subdirs)
2. Parse each file tracking `[Section]` headers
3. For each `key=value` line, match against `(section, key)` pairs from migration database
4. Report matches with file path, line number, current value, and recommendation

**Supported ini patterns**:
- `Key=Value` — standard key-value
- `+Key=Value` — array append
- `-Key=Value` — array remove
- `;comment` and `#comment` — skipped

---

## Report Sections

The migration report contains 6 mandatory sections (empty sections show "None found"):

| # | Section | Content |
|---|---------|---------|
| 1 | Summary | Counts table: total issues, breaking, deprecated, config, assets, auto-fixable ratio |
| 2 | Breaking Changes | Severity Error items from migration database |
| 3 | Deprecated APIs | Severity Warning items with replacement info |
| 4 | Asset Updates | Assets needing resave (PackageFileVersion mismatch) |
| 5 | Config Changes | Deprecated settings found in project .ini files |
| 6 | Manual Actions | Items requiring manual developer intervention |

Output: MCP response + `Intermediate/NarshaMCP/reports/migration_report.md`

---

## Seed Data Coverage

20 curated `config_changes` entries across 4 version transitions:

| Version Range | Count | Example Settings |
|---------------|-------|-----------------|
| 5.2 → 5.3 | 4 | gc.MaxObjectsNotConsideredByGC, n.VerifyPeer, bEnableAsyncScene |
| 5.3 → 5.4 | 5 | AutoExposure.Method, s.UseFixedPoolSize, r.RayTracing.Shadows |
| 5.4 → 5.5 | 5 | gc.TimeBetweenPurgingPendingKillObjects, r.SkinCache.CompileShaders |
| 5.5 → 5.6 | 6 | r.DistanceFieldShadowing, VolumetricFog.HistoryMissSupersampleCount |

---

## Error Handling

Each workflow step is independent — failure in one step produces partial results:

| Step | On Failure | Behavior |
|------|-----------|----------|
| scan_deprecated | DB missing | Returns empty matches, `success: true` |
| apply_migration | Edit/txn error | `success:false`, no partial multi-file write; inspect `errors[]` |
| generate_migration_report | No data | Report with "None found" in all sections |
| Auto-fix (C++ engine_upgrade) | Build failure | Partial fixes applied, errors listed |
| CoreRedirects | No renames | Step skipped |
| Build verification | Compile error | Report errors, don't revert |

---

## Limitations

- Seed data covers 5.2–5.6 transitions only (not 5.0/5.1 or 5.7+)
- No runtime migration (source code + config only)
- Asset version scan reads full files (not memory-mapped); may be slow on very large projects (10K+ assets)
- Config matching is exact (no fuzzy/regex matching)
