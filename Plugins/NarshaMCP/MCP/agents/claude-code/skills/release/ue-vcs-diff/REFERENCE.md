# UE VCS Diff — Reference

## ue_diff Operations (Exact)

`ue_diff.operations`: smart, auto, replication_audit, diff_hierarchy, compare_configs, compare_config_chain, compare_assets, extract_asset, render_blueprintue, compare_revision, verify_identical, merge_preview, bisect_asset, diff_asset_revision, snapshot, compare_to_snapshot, compare_folders, diff_level, diff_datatable, compare_cooked_builds, export_changeset, compare_collections, open_visual_diff

## MCP Tools Used

### ue_diff (compare_revision)

**Source**: [analysis_diff_revision.rs](../../../MCP/native/mcp_server/src/tools/analysis_diff_revision.rs)

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | yes | `"compare_revision"` |
| `revision_a` | string \| integer | yes | Start revision (commit hash, SVN rev, P4 CL) |
| `revision_b` | string \| integer | no | End revision (default: HEAD/working copy) |
| `vcs_type` | string | no | `"git"`, `"svn"`, `"p4"` (auto-detected if omitted) |
| `filter` | string | no | Glob pattern for file filtering (e.g., `"*.uasset"`) |
| `limit` | integer | no | Max files to process (default: 50) |
| `shelved` | boolean | no | Include shelved P4 changes (default: false) |
| `project_root` | string | no | Working-tree root override consumed by `compare_revision` |

**Response fields**:
- `summary.total_files_changed` — total count
- `summary.text_files` — .h/.cpp count
- `summary.uasset_files` — .uasset count
- `summary.config_files` — .ini count
- `summary.truncated` — true when exceeding limit
- `files[]` — per-file results with `path`, `status`, `diff_method`, `summary`

### ue_trace_execution (optional)

Used when `--trace-bp` flag is set or BP changes are detected.

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `operation` | string | yes | `"trace_execution_flow"` |
| `blueprint_name` | string | yes | BP name without `.uasset` extension |
| `max_depth` | integer | no | Trace depth (default: 3) |
| `output_format` | string | no | `"json"` or `"text"` |

## VCS Auto-Detection

`detect_vcs()` in [analysis_diff_revision.rs](../../../MCP/native/mcp_server/src/tools/analysis_diff_revision.rs) probes in this **exact order** (short-circuits on the first hit). The order is deliberate (Issue #20711): the cheap offline filesystem checks and the git walk-up run *before* any subprocess probe, so a git project — even a monorepo sub-directory — never pays an `svn`/`p4` spawn, and a git repo nested in an svn checkout resolves to git.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 0 | explicit `vcs_type` hint (`--vcs=`) | that VCS | Highest precedence; unknown value → error |
| 1 | `.svn/` directory at project root | SVN | Cheap filesystem check |
| 2 | `.git` (dir **or** file) via **walk-up** to ancestors | Git | Offline; handles worktrees/submodules + monorepo sub-dirs without the git binary |
| 3 | `git rev-parse --is-inside-work-tree` == `true` | Git | Fallback for bare repos / non-standard gitdir when the walk-up misses |
| 4 | `svn info --show-item url` succeeds | SVN | Probe for an svn 1.7+ working copy whose single `.svn` lives at the wc root (sub-dir has none) |
| 5 | `.p4config` file at project root | Perforce | Definitive on-disk P4 marker |
| 6 | `P4CONFIG`/`P4PORT` env set **and** `p4 where .` succeeds | Perforce | Env alone is only a hint (P4PORT is often machine-global), so a real client mapping must be confirmed |

If none match, the tool returns `Could not detect VCS in '{path}'. …` rather than guessing.

**Priority (highest → lowest):** explicit `vcs_type` > `.svn/` dir > `.git` walk-up > `git rev-parse` probe > `svn info` probe > `.p4config` file > `P4CONFIG`/`P4PORT` + `p4 where .`

## File Type Routing

| Extension | Diff Method | Output |
|-----------|-------------|--------|
| `.h`, `.cpp`, `.c` | `text` | Line counts (+added/-removed) |
| `.uasset`, `.umap` | `typed+generic` or `generic_only` | Export/import change summary |
| `.ini`, `.cfg` | `config` | Hint to use `compare_configs` for detail |
| Other | `skipped` | Listed but not diffed |

## P4CONFIG Environment

The server automatically sets `P4CONFIG=.p4config` when `.p4config` exists in the project root. This enables P4 to read connection settings (P4PORT, P4USER, P4CLIENT) from the project-local config file.

**Implementation**: `p4_command()` helper in [analysis_diff_revision.rs](../../../MCP/native/mcp_server/src/tools/analysis_diff_revision.rs) (3 call sites).

## Related Skills

| Skill | Purpose | When to use instead |
|-------|---------|-------------------|
| `/ue-diff` | Entity-level diff (replication audit, hierarchy delta, config comparison) | Specific entity comparison, not revision-range |
| `/ue-pre-commit` | Pre-commit safety check | Staged file impact before committing |
| `/ue-impact` | Hypothetical change impact | "What breaks if I change X?" |


### open_visual_diff
| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_a`, `asset_b` | string | Yes | Assets to open in editor Diff window |

Editor enrichment (Rule 20). Offline: structured hint to `compare_assets`. Explicit `operation=open_visual_diff` required.
