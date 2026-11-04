---
name: ue-vcs-diff
model: opus
description: "Purpose: VCS change detection + structured diff orchestration. Auto-detects Git/SVN/P4, extracts changed files between revisions, routes by file type (.uasset → binary diff, .cpp/.h → text diff, .ini → config hint), and optionally traces changed BP execution flows. Triggers: 'VCS 변경사항', '뭐가 바뀌었어', 'svn diff', 'git diff', 'p4 diff', 'revision diff', 'what changed', 'compare revisions', 'asset changes'."
compatibility: "Requires NarshaMCP MCP server (v0.9.7+) with Unreal Engine 5.x project under Git, SVN, or Perforce"
argument-hint: "revision range (e.g., r5600..5700, HEAD~3..HEAD, abc123..def456), optional --vcs=git|svn|p4, optional --filter=*.uasset, optional --trace-bp, --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE VCS Diff -- VCS Change Detection & Structured Report

**Issue**: #7423
**Purpose**: Detect VCS changes between revisions and generate structured per-file diff report with optional Blueprint execution flow tracing
**Depends on**: `ue_diff(compare_revision)` from Issue #7422

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the analysis automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Parse `$ARGUMENTS`** to extract revision range, VCS type, and flags
2. **Call `ue_diff(compare_revision)`** with parsed parameters
3. **Optionally trace changed BPs** with `ue_trace_execution` (when `--trace-bp` flag or BP changes detected)
4. **Generate formatted report** with per-file structured diff

> If `$ARGUMENTS` is empty, ask the user for a revision range or commit reference.

---

## CRITICAL RULES -- Call Count Minimization

> **MANDATORY**: This skill MUST complete in **2-5 MCP/tool calls** (Bash excluded).
> Optimal: 2 calls. Maximum: 5 calls.

| # | Rule |
|---|------|
| 1 | **ToolSearch 1x only** -- fetch `ue_diff` and `ue_trace_execution` schemas in one call |
| 2 | **compare_revision does the heavy lifting** -- VCS detection, file list, per-file diff all internally |
| 3 | **No redundant git/svn/p4 Bash calls** -- compare_revision handles VCS CLI internally |
| 4 | **BP trace is optional** -- only when .uasset files are modified AND `--trace-bp` flag or user wants deep analysis |
| 5 | **Cap BP traces at 3** -- pick by highest change impact (most diff fields) |

### Target Call Sequences

```text
Optimal (2 calls):                    Standard (3 calls):                   Extended (5 calls):
1. ToolSearch (1x)                    1. ToolSearch (1x)                     1. ToolSearch (1x)
2. ue_diff(compare_revision)          2. ue_diff(compare_revision)           2. ue_diff(compare_revision)
                                      3. ue_trace_execution (1 BP)           3-5. ue_trace_execution (up to 3 BPs)
```

---

## Input Pattern Routing

Parse `$ARGUMENTS` to extract `revision_a`, `revision_b`, `vcs_type`, `filter`, and flags.

| Input Pattern | Parsing | Example |
|---|---|---|
| `rNNNN..rMMMM` | SVN: revision_a=NNNN, revision_b=MMMM | `r5600..5700` |
| `NNNN..MMMM` (pure numeric) | SVN: revision_a=NNNN, revision_b=MMMM | `5600..5700` |
| `HASH..HASH` (hex 7+ chars) | Git: revision_a=hash1, revision_b=hash2 | `abc1234..def5678` |
| `HEAD~N` or `HEAD~N..HEAD` | Git: revision_a=HEAD~N, revision_b="" (HEAD) | `HEAD~3` |
| `TAG..TAG` | Git: revision_a=tag1, revision_b=tag2 | `v1.0..v2.0` |
| `@CL_NUMBER` or `CL NNNN` | P4: revision_a=CL, vcs_type=p4 | `@12345` |
| `--vcs=TYPE` | Override auto-detection | `--vcs=svn` |
| `--filter=PATTERN` | File pattern filter | `--filter=*.uasset` |
| `--shelved` | P4 shelved changes | `@12345 --shelved` |
| `--trace-bp` | Enable BP execution flow tracing | `HEAD~3 --trace-bp` |
| Korean: "최근 N커밋" | Git: revision_a=HEAD~N | "최근 3커밋" |
| Korean: "마지막 커밋" | Git: revision_a=HEAD~1 | "마지막 커밋 뭐가 바뀌었어" |

### Parsing Rules

1. **Split on `..`** to get revision_a and revision_b
2. **Strip flags** (`--vcs=`, `--filter=`, `--shelved`, `--trace-bp`) before revision parsing
3. **Strip `r` prefix** from SVN numeric revisions
4. **Strip `@` prefix** from P4 changelists
5. **Auto-detect VCS type** when `--vcs` not specified:
   - Pure numeric → SVN
   - Hex 7+ chars → Git
   - `@` prefix → P4
   - Otherwise → let compare_revision auto-detect from project directory

---

## Execution Workflow

### Step 1: Schema Fetch

```python
ToolSearch("select:mcp__narshamcp__ue_diff,mcp__narshamcp__ue_trace_execution")
```

### Step 2: Compare Revision (Main Operation)

```python
ue_diff(
    operation="compare_revision",
    revision_a="<parsed_revision_a>",
    revision_b="<parsed_revision_b>",
    vcs_type="<git|svn|p4>",
    filter="<pattern>",
    limit=50,
    shelved=false,
)
```

### Step 3: Optional BP Trace (when --trace-bp or BP changes detected)

For each modified `.uasset` file with `BP_` prefix in the filename (max 3):

```python
ue_trace_execution(
    operation="trace_execution_flow",
    blueprint_name="<BP_Name>",      # extracted from path, without .uasset
    max_depth=3,
    output_format="json"
)
```

**BP Selection Priority**: Pick BPs with the most diff fields from compare_revision results (typed+generic > generic_only).

---

## Report Format

Generate a structured markdown report from compare_revision output:

```markdown
## VCS Diff Report

**VCS**: git | **Revision**: abc1234 → def5678 | **Time**: 245ms

### Summary
| Category | Count |
|----------|-------|
| Total files changed | 15 |
| Source files (.h/.cpp) | 8 |
| Asset files (.uasset) | 4 |
| Config files (.ini) | 1 |
| Skipped/Other | 2 |

### Source Changes
| File | Status | +Lines | -Lines |
|------|--------|--------|--------|
| Source/MyGame/Hero.cpp | modified | +42 | -15 |
| Source/MyGame/Hero.h | modified | +8 | -2 |

### Asset Changes
| Asset | Status | Diff Method | Changes |
|-------|--------|-------------|---------|
| BP_Hero.uasset | modified | typed+generic | 3 exports changed, 2 imports added |
| M_Rock.uasset | modified | generic_only | 1 export changed |

### Config Changes
| Config | Status | Note |
|--------|--------|------|
| DefaultEngine.ini | modified | Use `/ue-diff` with `compare_configs` for detailed section diff |

### BP Execution Impact (optional, --trace-bp)
#### BP_Hero
- BeginPlay → InitAbilitySystem → BindInputActions
- TakeDamage → ApplyDamage → UpdateHealthUI → CheckDeath
```

### Report Mapping (compare_revision JSON → Report)

| JSON Field | Report Section |
|---|---|
| `summary.total_files_changed` | Summary table |
| `summary.text_files` | Source Changes count |
| `summary.uasset_files` | Asset Changes count |
| `summary.config_files` | Config Changes count |
| `files[].diff_method == "text"` → `files[].summary.lines_added/removed` | Source Changes table |
| `files[].summary.diff_method == "typed+generic" or "generic_only"` | Asset Changes table |
| `files[].diff_method == "config"` | Config Changes table |
| `files[].diff_method == "skipped"` | Omit or footnote |
| `summary.truncated == true` | Warning: "Showing first 50 of N files" |

> **⚠️ `diff_method` lives in two places** (the response shape is per-row-type — see `get_diff_method_from_result` in `analysis_diff_revision.rs`): source/config/skipped/error rows carry it at the **top level** (`files[].diff_method`), but a successfully-diffed `.uasset` row nests it **inside its embedded `compare_assets` result** (`files[].summary.diff_method`, e.g. `"typed+generic"` / `"generic_only"`). When categorizing, read `files[].diff_method` first and **fall back to `files[].summary.diff_method`** — do not assume a single flat location for asset rows.

---

## Error Handling

Error strings below are the **exact** messages emitted by `analysis_diff_revision.rs` (cross-checked in Issue #20718 — do not paraphrase).

| Error (exact) | Cause | Recovery |
|-------|-------|----------|
| `compare_revision requires 'revision_a'` | No revision provided and $ARGUMENTS empty | Ask user for revision range |
| `Could not detect VCS in '{path}'. No .svn, .git, or .p4config found.` | Project not under Git/SVN/P4 | Ask user to specify `--vcs=` or pass `project_root` at a working tree |
| `Invalid revision '{rev}': contains disallowed characters` (also `… must not start with '-'`) | Special chars / leading `-` in revision string | Revisions must be alphanumeric + `.-_/~^:` only |
| `Failed to get changed files from {Vcs} in '{path}': {err}` | svn/git/p4 CLI error or auth issue (`{err}` = e.g. `git diff failed: …`, `svn diff failed: …`, `p4 describe failed: …`) | Check VCS CLI installation and credentials; confirm the dir is a working tree for the given `vcs_type` |
| Empty result (0 files) | No changes between revisions | Report "No changes found between revision_a and revision_b" |
| `summary.truncated == true` | More than `limit` (default 50) changed files | Display warning, suggest `--filter` to narrow scope |

---

## Activation Test Cases

### Positive (SHOULD trigger this skill)
- "r5600..5700 변경사항 분석해줘" → compare_revision with SVN revisions
- "What changed between HEAD~3 and HEAD?" → compare_revision with Git
- "svn diff 분석해줘" → compare_revision with SVN hint
- "변경된 에셋 분석해줘" → compare_revision + asset focus
- "최근 3커밋 뭐가 바뀌었어?" → compare_revision with HEAD~3

### Negative (should NOT trigger this skill)
- "ALyraPlayerState 리플리케이션 감사" → `/ue-diff` (entity-specific diff)
- "DefaultEngine vs WindowsEngine 설정 비교" → `/ue-diff` (config comparison)
- "커밋 전 분석해줘" → `/ue-pre-commit` (pre-commit safety)
- "APawn 변경하면 뭐가 깨져?" → `/ue-impact` (hypothetical impact analysis)

---

## Self-Test

When invoked with `--self-test`, verify skill health without requiring an active UE project:

```bash
/ue-vcs-diff --self-test
```

### Self-Test Checks

1. **Schema availability**: `ToolSearch("select:mcp__narshamcp__ue_diff")` returns compare_revision operation
2. **Routing consistency**: Verify this skill's `ue-vcs-diff` entry exists in `.claude/skills/routing.json`, and **read its `priority` from that SSOT** — do NOT assert a hardcoded constant. A pinned literal drifts from routing.json silently (this was the bug in Issue #20718: the doc said `66` while routing.json said `109`), so the check must echo whatever value the routing entry currently carries.
3. **Argument parsing**: Test pattern parsing for each VCS type:
   - `HEAD~3` → git, revision_a=HEAD~3
   - `r5600..5700` → svn, revision_a=5600, revision_b=5700
   - `@12345` → p4, revision_a=12345
   - `abc1234..def5678` → git (hex detection)
4. **Report**: Pass/Fail summary for each check

### Expected Output

```text
Self-Test: /ue-vcs-diff
  [PASS] ue_diff schema: compare_revision available
  [PASS] Routing: ue-vcs-diff entry found (priority 109, read from routing.json)
  [PASS] Parse HEAD~3: git, revision_a=HEAD~3
  [PASS] Parse r5600..5700: svn, revision_a=5600, revision_b=5700
  [PASS] Parse @12345: p4, revision_a=12345
  [PASS] Parse abc1234..def5678: git, revision_a=abc1234, revision_b=def5678
  Result: 6/6 PASS
```

---

## Related

- Issue #7423 -- This skill
- Issue #7422 -- `compare_assets` + `compare_revision` operations (dependency, merged)
- Issue #7315 -- Parent: `ue_diff` tool implementation
- `/ue-diff` -- Entity-level diff (replication audit, hierarchy delta, config comparison)
- `/ue-pre-commit` -- Pre-commit safety check (staged files impact analysis)
- `/ue-impact` -- Hypothetical change impact analysis ("what breaks if I change X?")
