---
name: session-health-check
model: sonnet
context: fork
agent: general-purpose
user-invokable: false
description: "Purpose: MCP session readiness deep check. Calls ue_check_health + ue_cache_control(get_stats) + orphan_process_check.py for full server status. Triggers: 'session check', 'MCP ready?', 'server status', '세션 체크', '서버 상태', 'health check'."
compatibility: "Requires NarshaMCP MCP server (v0.9.13+) with Unreal Engine 5.x C++ project"
argument-hint: "(no args = full check, --self-test = validate skill structure only)"
allowed-tools:
  - mcp__narshamcp__ue_check_health
  - mcp__narshamcp__ue_cache_control
  - Bash
metadata:
  version: "1.3.0"
  author: "Next-Stage-Inc"
  changelog:
    - "1.3.0 — Issue #10768 Phase 2 PR 5/5: add context:fork + agent:general-purpose to prevent 1M-context billing inheritance from Opus parent (daemon-side state preserved across fork)."
    - "1.2.0 — Issue #11269: orphan detector covers narshamcp.exe zombies (registry+port live exclusion). Step 3 split #9360/#11269 sub-rules; Self-Test 6th check added."
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Session Health Check

**Issue**: #5120
**Purpose**: Deep MCP session readiness verification beyond standard `initialize` handshake.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the check workflow automatically**.
> Do NOT just display documentation - actually call the MCP tools!

---

## Auto-Trigger Phrases

### Korean
- "세션 체크", "서버 상태 확인"
- "MCP 준비 됐어?", "헬스 체크"
- "세션 검증", "서버 점검"

### English
- "Session check", "Server status"
- "Is MCP ready?", "Health check"
- "Session verify", "Server readiness"

---

**Target**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, run the full health check (all 3 steps). No target required for this skill.

---

## Workflow

### Step 1: Server Health Check

Call `ue_check_health()` with no arguments.

**Extract from response**:
- `status` - Should be "healthy"
- `version` - Server version
- `project_path` - Configured project
- `engine_path` - Configured engine
- `path_validation.status` - Should be "valid"
- `ports.http` / `ports.ws` - Editor Remote Control ports
- `build_info.git_commit` - Build commit hash

### Step 2: Cache & Index Status

Call `ue_cache_control(operation="get_stats")`.

**Extract from response**:
- `symbol_index.loaded` - PDB symbols available?
- `symbol_index.symbol_count` - How many symbols indexed
- `metadata.status` - Metadata loaded?
- `four_phase.complete` - Indexing finished?
- `four_phase.running` - Still indexing?
- `four_phase.symbols` - Symbol count from 4-phase

### Step 3: Orphan Process Detection (Issues #9360 + #11269)

> **Run this step automatically if session wall-time > 1h** OR on every
> manual `/session-health-check` invocation.
> Detects two classes of zombies:
> - **#9360 shell orphans** — PowerShell/cmd polling-loop survivors
>   (single session leaked 46.3 GB of orphans).
> - **#11269 narshamcp zombies** — detached `narshamcp.exe` daemons that
>   hold file locks on `test-outputs/` after a session ends.

Run the orphan detector script:

```bash
python .claude/hooks/checks/orphan_process_check.py
```

#### Shell orphans (Issue #9360)

Detection rules (all must match):
- Image: `powershell.exe` / `pwsh.exe` / `cmd.exe`
- Parent: `bash.exe` / `sh.exe` / `git-bash.exe` / `mintty.exe`, OR deceased
- RSS: > 1 GB (override: `ORPHAN_GUARD_RSS_MB`)
- Age: > 5 min (override: `ORPHAN_GUARD_AGE_S`)

**AccessDenied escalation**: if the parent process is alive but unreadable
(low-privilege account), the orphan is still flagged when both RSS > 5 GB
**and** age > 1 h — i.e., the footprint is too extreme to dismiss even
without parent identification. See `REFERENCE.md` § AccessDenied for the
constants (`EXTREME_RSS_BYTES`, `EXTREME_AGE_SECONDS`).

#### narshamcp zombies (Issue #11269)

Detection rules (all must match):
- Image: `narshamcp.exe`
- PID **NOT** in `~/.narshamcp/daemons/registry.json` `entries[*].pid`
- PID **NOT** LISTEN-ing on the daemon port (default 19620; override
  `NARSHAMCP_BASE_PORT`)
- RSS: > 100 MB (override: `ORPHAN_GUARD_NARSHAMCP_RSS_MB`)
- Age: > 10 min (override: `ORPHAN_GUARD_NARSHAMCP_AGE_S`)

The two-layer exclusion (registry pid set ∪ listen-port pid set) catches
both the normal case (registered daemon) and the stale-registry case
(daemon crashed without cleanup but is still LISTEN-ing). Test override:
`NARSHAMCP_DAEMONS_DIR_OVERRIDE`. Output rows for this rule carry
`kind=narshamcp-zombie`; cross-reference the
[`mcp-doctor` SKILL.md daemon-registry section](../mcp-doctor/SKILL.md)
before killing — confirm the PID is not the live session.

**Critical safety rule**: NEVER auto-kill. Always present the table to the
user and require explicit confirmation per PID before terminating. A
legitimate user-launched powershell session being killed is a worse failure
than the leak itself.

**Output behaviors**:

- Empty → print one line: `"No orphan polling-loop processes detected."`
  (Note: legacy phrasing from Issue #9360; the script also covers narshamcp
  zombies via Issue #11269 — same empty-state message applies.)
  Do NOT add this to the main health report (no signal = no noise).
- Non-empty → render the markdown table from the script's stdout into the
  health report under a new section **Orphan Processes (Issues #9360 + #11269)**,
  then ask the user: "Kill PIDs [list]? (yes/no)". On `yes`, terminate
  one PID at a time using `taskkill /PID <pid> /F` (or `kill -9 <pid>` on
  POSIX), reporting outcome per PID.

### Step 4: Report

Generate a structured report with Pass/Warn/Fail per category (include the
**Orphans** row only when Step 3 returned a non-empty list):

```markdown
## Session Health Report

| Check | Status | Details |
|-------|--------|---------|
| Server | PASS/FAIL | version, status |
| Project Path | PASS/FAIL | path, is_ue_project |
| Engine Path | PASS/FAIL | path, exists |
| PDB Index | PASS/WARN/FAIL | symbols, PDB count |
| Metadata | PASS/WARN/FAIL | nodes, blueprints |
| Editor Ports | PASS/WARN | http port, ws port |
| Orphans | PASS/WARN | 0 detected / N detected (Issues #9360 + #11269) |

### Overall: READY / PARTIALLY READY / NOT READY
```

### Judgment Criteria

| Check | PASS | WARN | FAIL |
|-------|------|------|------|
| Server | status=healthy | - | No response or error |
| Project Path | is_ue_project=true | - | path_not_found |
| Engine Path | exists=true | - | not found |
| PDB Index | complete=true, symbols>0 | running=true (in progress) | symbols=0 and not running |
| Metadata | node_count>0 | generation_in_progress | last_error present |
| Editor Ports | http>0 and ws>0 | default ports only | - |
| Orphans (#9360 + #11269) | 0 leak candidates | ≥1 candidate (review + confirm before kill) | - |

### Overall Verdict
- **READY**: All checks PASS
- **PARTIALLY READY**: Some WARN, no FAIL
- **NOT READY**: Any FAIL

### Error Recovery Guidance

If NOT READY, suggest:
1. **Server not responding**: Run `/mcp-doctor` for full diagnosis and auto-fix
2. **Project path invalid**: Update `--project-path` in `.mcp.json`
3. **No symbols**: Wait for indexing or run `ue_cache_control(operation="regenerate_pdb")`
4. **No metadata**: Run `ue_cache_control(operation="regenerate_metadata")`
5. **Editor ports default**: Editor not running (offline analysis still works)

## Output Format

```markdown
## Session Health Report

| Check        | Status | Details                          |
|--------------|--------|----------------------------------|
| Server       | PASS   | v0.9.13, status=healthy          |
| Project Path | PASS   | D:/MyProject, is_ue_project=true |
| Engine Path  | PASS   | C:/UE_5.7, exists=true           |
| PDB Index    | PASS   | 164,032 symbols, 121 PDBs        |
| Metadata     | WARN   | generation_in_progress           |
| Editor Ports | PASS   | http=30010, ws=30020             |
| Orphans      | PASS   | 0 leak candidates (Issues #9360 + #11269) |

### Overall: READY
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| Server not responding (FAIL) | MCP binary path wrong or process crashed | Verify `narshamcp.exe` path in `.mcp.json`, restart Claude Code |
| PDB symbols = 0 and not running | Index never generated or cache corrupted | Run `ue_cache_control(operation="regenerate_pdb")` or rebuild project to generate PDB files |
| Project path invalid (FAIL) | `--project-path` points to wrong directory | Update `.mcp.json` with correct absolute path containing `.uproject` file |

## Self-Test Mode

`/session-health-check --self-test` runs a structural validation of this skill **without** invoking MCP tools. Used by `/agent-test` and CI to verify the skill is well-formed.

**Checks performed**:

1. **Frontmatter validity**: `name`, `description`, `allowed-tools`, `argument-hint`, `metadata.version` all present.
2. **Companion files**: `REFERENCE.md` and `EXAMPLES.md` exist next to `SKILL.md`.
3. **Workflow contract**: Steps 1–4 headers present in order; Step 3 references `orphan_process_check.py` (Issues #9360 + #11269 acceptance).
4. **Hook script reachable**: `.claude/hooks/checks/orphan_process_check.py` exists and importable (`python -c "from orphan_process_check import detect_orphans"`).
5. **Routing entry**: `routing.json` has a `session-health-check` rule with at least one trigger pattern overlapping the activation triggers in this file.
6. **narshamcp helpers reachable** (Issue #11269): `_process_matches_narshamcp_zombie`, `_registered_narshamcp_pids`, `_listening_pids_on_port` all importable.

**Run**:
```bash
# From repo root
python .claude/hooks/checks/orphan_process_check.py >/dev/null && echo "hook OK"
ls .claude/skills/session-health-check/{SKILL.md,REFERENCE.md,EXAMPLES.md}
grep -nE "^### Step [1-4]:" .claude/skills/session-health-check/SKILL.md
python -c "import sys; sys.path.insert(0, '.claude/hooks/checks'); from orphan_process_check import _process_matches_narshamcp_zombie, _registered_narshamcp_pids, _listening_pids_on_port; print('narshamcp helpers OK')"
```

**Exit**: PASS if all 6 checks succeed, FAIL with per-check breakdown otherwise. Does NOT call `ue_check_health` or `ue_cache_control` — pure static validation.

## MCP Tool Examples

```python
# Check MCP server health
ue_check_health(compact=True)
# Review cache and module stats
ue_cache_control(operation="get_stats")
```
