# /ue-migrate Examples

## Example 1: Basic Migration (5.4 to 5.7)

**User**: "UE 5.4에서 5.7로 마이그레이션 도와줘"

**Invocation**:
```bash
/ue-migrate 5.4 5.7
```

**Workflow**:
1. Scan deprecated configs via `ue_analyze_config(operation="scan_deprecated", source_version="5.4", target_version="5.7")`
2. Generate migration report via `ue_analyze_config(operation="generate_migration_report", source_version="5.4", target_version="5.7")`
3. Report saved to `Intermediate/NarshaMCP/reports/migration_report.md`
4. Config auto-fix: `ue_analyze_config(operation="apply_migration", dry_run=true)` preview, then after confirmation `dry_run=false` (Issue #20055 / #24720 — not per-key `modify_config`)
5. Generate CoreRedirects for class/function renames
6. Verify build via `ue_build_pipeline`

**Output**: Markdown report with 7 sections (Summary, Breaking Changes, Deprecated APIs, Asset Updates, Config Changes, Header Relocations, Manual Actions). Header Relocations added in Issue #11492.

---

## Example 2: Single Target Version (Auto-Detect Source)

**User**: "Migrate to UE 5.7"

**Invocation**:
```bash
/ue-migrate 5.7
```

**Workflow**: Same as Example 1, but source version is auto-detected from the project's `.uproject` EngineAssociation field.

---

## Example 3: 5.7 → 5.8 (ECABridge removal — Issue #11395)

**User**: "UE 5.7에서 5.8로 마이그레이션 도와줘"

**Invocation**:
```bash
/ue-migrate 5.7 5.8
```

**Workflow**:
1. Daemon resolves `MCP/data/engine_migrations/5.7_to_5.8.json` via `MigrationDatabase::load_migration("5.7", "5.8", false)`.
2. Statistics validator confirms counts: 2 breaking_changes + 3 header_relocations = 5 entries, 3 auto-fixable (60.0%).
3. Report rendered to `Intermediate/NarshaMCP/reports/migration_report.md`.

**Output**:
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
```

**Notes**:
- `BreakingChange::category` uses `member_removed` for plugin-level changes (enum is restricted to method-level variants). Descriptive `change_type` free-text field carries the real semantics — see [`5.7_to_5.8.json#notes`](../../../MCP/data/engine_migrations/5.7_to_5.8.json) for the full design rationale.
- StateTree API churn between 5.7 and 5.8 is intentionally deferred until git/`radbin` access to the engine mirror is available.

---

## Example 4: Korean Trigger

**User**: "엔진 업그레이드 5.5에서 5.6"

**Invocation**:
```bash
/ue-migrate 5.5 5.6
```

**Output**:
```
=== Migration Report: UE 5.5 → 5.6 ===

## 1. Summary
| Metric | Count |
|--------|-------|
| Deprecated configs found | 6 |
| Auto-fixable | 2 |
| Manual required | 4 |

## 5. Config Changes
| Setting | File | Line | Type | Auto-Fix |
|---------|------|------|------|----------|
| r.DistanceFieldShadowing | DefaultEngine.ini | 45 | deprecated | No |
| VolumetricFog.HistoryMissSupersampleCount | DefaultEngine.ini | 102 | removed | No |
...
```
