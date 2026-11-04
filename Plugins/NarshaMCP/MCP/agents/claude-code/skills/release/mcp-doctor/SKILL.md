---
name: mcp-doctor
description: "Universal MCP server diagnosis and recovery. Reads .mcp.json, audits processes, detects multi-instance/stale-registry/hung-process issues, auto-fixes by severity, verifies via health check. Bridge-aware: --nuke is scoped to THIS project's daemon (PID-tree kill via registry), preserves other-worktree daemons and IDE bridges. Config-aware (Issue #11150): --config-only / Phase 2b detect daemon degraded-mode (stale UECODEGEN_ENGINE_DIR / UECODEGEN_PROJECT_DIR / EngineAssociation mismatch) via /api/daemon/status and offer engine resolve chain (env → config.ini → .uproject → install scan) recovery."
allowed-tools: [Bash, Read, Edit, Skill, AskUserQuestion, ue_check_health, ue_cache_control]
argument-hint: |
  /mcp-doctor                    # Full diagnosis + auto-fix (process audit + config audit)
  /mcp-doctor --diagnose-only    # Diagnosis only (no modifications)
  /mcp-doctor --server <name>    # Target specific server
  /mcp-doctor --nuke             # Kill THIS project's daemon (port/PID-scoped) + caches
  /mcp-doctor --nuke --port <p>  # Kill daemon owning port <p> + its dashboard children
  /mcp-doctor --config-only      # Skip process audit; only Phase 2b (degraded-mode) + Phase 4b recovery
  /mcp-doctor --self-test        # Validate skill structure + grep guards (no kill)
metadata:
  version: "1.3.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# MCP Doctor — Universal MCP Server Diagnosis & Recovery

Diagnose and fix MCP server connection issues across all servers in `.mcp.json`.

**Issue**: #8340
**Related**: #8355 (daemon auto-start GUI fix — `DETACHED_PROCESS` → `CREATE_NO_WINDOW`)

## Usage

```bash
/mcp-doctor                    # Full diagnosis + auto-fix (process audit + config audit)
/mcp-doctor --diagnose-only    # Diagnosis only (no modifications)
/mcp-doctor --server <name>    # Target specific server
/mcp-doctor --nuke             # Kill all + clean all caches + request reload
/mcp-doctor --config-only      # Skip process audit; only Phase 2b (degraded-mode) + Phase 4b recovery (Issue #11150)
```

**`--config-only` mode (Issue #11150)**: Phase 1 runs minimal discovery only. Phase 2/3/4 (process audit, multi-instance, registry, kill) are SKIPPED. Phase 2b (config audit) and Phase 4b (config recovery) run as the primary path, then Phase 5 verifies via `/api/daemon/status`. Use this when you already know the daemon is running but suspect stale `UECODEGEN_ENGINE_DIR` / `UECODEGEN_PROJECT_DIR` / `EngineAssociation` mismatch.

## Activation Triggers

**Korean**: "MCP 안돼", "MCP 연결 안됨", "도구 안 보여", "MCP 복구", "MCP 진단", "MCP 안 됨", "engine path 안 맞아", "engine path 잘못됨", "stale path", "런처에서 풀소스로 갈아탔는데"
**English**: "MCP not working", "tools not showing", "MCP stuck", "fix MCP", "MCP doctor", "MCP broken", "engine path mismatch", "stale engine path"

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the workflow automatically**.
> Parse arguments, detect mode, and run the diagnostic pipeline without waiting for further input.

**Mode dispatch** (parse `$@` once, set environment flags before Phase 1):

| Flag | Behavior |
|------|----------|
| `--diagnose-only` | Skip Phase 4 + Phase 4b mutations. Phase 2b detection still runs (read-only). |
| `--server <name>` | Filter Phase 1–5 to one server. Phase 2b/4b only run when `name == "narshamcp"`. |
| `--nuke [--port <p>]` | Phase 4 destructive path. Independent of `--config-only`. |
| `--config-only` (Issue #11150) | Phase 1 minimal Discovery only → SKIP Phase 2/3/4 → Phase 2b → Phase 4b → Phase 5 (config verify only). Phase 3's process-issue table is suppressed; only D10 entries appear. |
| `--self-test` | Run the validation block at bottom of this file; exit. No diagnosis. |

---

## Workflow

### Phase 1: Discovery

Read `.mcp.json` and enumerate all registered MCP servers.

```bash
cat .mcp.json
```

For each server entry, extract:
- **name**: key in `mcpServers` object
- **command**: binary path
- **args**: argument list
- **mode**: `"connect"` in args → Daemon, otherwise → STDIO

If `--server <name>` is specified, filter to that single server.

Check binary existence:
```bash
ls -la <command_path>
```

### Phase 2: Process Audit

For each server, check running processes.

**Extract binary name** from `command` field (basename without path):

```bash
# Windows
powershell -Command "Get-Process -Name '<binary_name_without_ext>' -ErrorAction SilentlyContinue | Select-Object Id, StartTime, WorkingSet64 | Format-Table -AutoSize"
```

**Classification**:
- `0 processes` → Server not running
- `1 process` → Normal
- `2+ processes` → Multi-instance problem

**Daemon mode additional checks**:

Check daemon registry (narshamcp-specific):
```bash
cat ~/.narshamcp/daemons/registry.json 2>/dev/null
```

For each registry entry, verify PID is alive:
```bash
powershell -Command "Get-Process -Id <pid> -ErrorAction SilentlyContinue | Select-Object Id"
```

Check port availability:
```bash
powershell -Command "netstat -ano | Select-String '<port>'"
```

**Engine-path collision check (Issue #13144)**: scan the registry for daemons
pointing at the same `engine_path`. Concurrent cold-loads against a shared
engine path is the live trigger for the [14-min hang regression](../../../docs/development/testing/REPRO_13144.md);
flag it advisory-only here (do NOT auto-kill — the second daemon may
legitimately be a different project's MCP that happens to share `UE_5.7`).

```bash
python -c "
import json, sys, collections
try:
    reg = json.load(open(__import__('os').path.expanduser('~/.narshamcp/daemons/registry.json')))
except FileNotFoundError:
    sys.exit(0)
entries = reg.get('daemons', reg) if isinstance(reg, dict) else reg
buckets = collections.defaultdict(list)
for e in entries:
    ep = (e.get('engine_path') or '').strip().rstrip('/').rstrip('\\\\').lower()
    if ep:
        buckets[ep].append({'pid': e.get('pid'), 'project_path': e.get('project_path'), 'port': e.get('port')})
collisions = {k: v for k, v in buckets.items() if len(v) > 1}
if collisions:
    print('[engine-path-collision] WARN: 2+ daemons share an engine_path — Issue #13144 hang risk:')
    for ep, daemons in collisions.items():
        print(f'  engine_path={ep}')
        for d in daemons:
            print(f'    pid={d[\"pid\"]} port={d[\"port\"]} project={d[\"project_path\"]}')
    print('  Workaround: stagger daemon starts OR set engine_source_scan=false in one worktree.')
    print('  See: docs/user-guides/tools/DAEMON_MODE_GUIDE.md#multi-worktree-caveats-issue-13144')
" 2>&1
```

Verdict: `[engine-path-collision] WARN` line surfaced in the audit output;
operator decides whether to stop the second daemon. No auto-fix.

**Tool availability check**:

```bash
# Use ToolSearch to check if any tools from this server are available
# Pattern: mcp__<server_name>__
```

Call `ToolSearch("select:mcp__<server_name>__ue_check_health")` or similar known tool.
If found → tools available. If not found → tools disconnected.

### Phase 2b: Config Audit (Issue #11150 — narshamcp only)

> **Skip when `server_name != "narshamcp"`** — only the narshamcp daemon publishes
> the `/api/daemon/status` payload introduced by Issue #11147 (commit `7ffa60469`).
>
> **In `--config-only` mode this phase is the primary detection path** — Phases 2, 3, 4
> (process audit / multi-instance / kill) are skipped entirely.

Detects `degraded_mode == true` daemons even when the process is alive and the port is
listening. Catches the "scenario F" failure mode where `UECODEGEN_ENGINE_DIR` is stale
(e.g., user moved from launcher build to full-source build) — these slip past D1–D9.

Resolve dashboard port from registry (NOT the MCP daemon port — `/api/daemon/status` lives
on the dashboard):

```bash
# Resolve dashboard_port for THIS project's daemon. Same resolver as Phase 4 --nuke.
DASHBOARD_PORT=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
    --project-path "$PROJECT_PATH" --field dashboard_port --default 19620)

python .claude/skills/mcp-doctor/scripts/check_degraded.py \
    --port "$DASHBOARD_PORT" --json --timeout 3
# Exit 0   → degraded_mode == false (config healthy). Proceed to Phase 3.
# Exit 1   → degraded_mode == true. JSON on stdout; record for Phase 4b.
# Exit 2   → daemon returned non-conforming JSON (e.g., predates #11147). WARN + skip.
# Exit 127 → daemon unreachable. WARN + skip (Phase 3/4 will handle DAEMON_NOT_STARTED).
```

The abridged JSON payload has shape:

```json
{
  "degraded_mode": true,
  "config_errors": [
    {
      "code": "ENGINE_PATH_NOT_FOUND",
      "user_path": "C:/UE_5.6",
      "fix_instruction_key": "degraded.engine_path_not_found.fix",
      "severity": "error",
      "extra": [],
      "suggested_paths": [
        {"source": "env",        "path": "C:/UE_5.6",                "verified": false, "note": "stale env"},
        {"source": "config_ini", "path": "C:/UnrealEngine/Engine",   "verified": true},
        {"source": "uproject",   "path": "C:/UnrealEngine/Engine",   "verified": true,  "note": "EngineAssociation = 5.7"},
        {"source": "scan",       "path": "C:/Program Files/Epic/UE_5.7", "verified": true}
      ]
    }
  ]
}
```

For each `config_errors[i]` render an entry under D10 (CONFIG_DEGRADED) in the Phase 3
issue table. Display per error:

```text
D10  CONFIG_DEGRADED (severity=error)
     code: ENGINE_PATH_NOT_FOUND
     user_path: C:/UE_5.6 (missing)
     Try one of these detected paths:
       [env]        C:/UE_5.6                          ✗ stale env
       [config.ini] C:/UnrealEngine/Engine             ✓ verified
       [.uproject]  C:/UnrealEngine/Engine             ✓ verified  (EngineAssociation = 5.7)
       [scan]       C:/Program Files/Epic/UE_5.7      ✓ verified
```

Each `code` enumerated by `degraded_state.rs`:

| Code | Sink env-var (Phase 4b) |
|------|-------------------------|
| `MISSING_PROJECT_PATH` | `UECODEGEN_PROJECT_DIR` |
| `PROJECT_PATH_NOT_FOUND` | `UECODEGEN_PROJECT_DIR` |
| `PROJECT_PATH_NOT_UE_PROJECT` | `UECODEGEN_PROJECT_DIR` |
| `ENGINE_PATH_NOT_FOUND` | `UECODEGEN_ENGINE_DIR` |
| `ENGINE_PATH_NO_BINARIES` | `UECODEGEN_ENGINE_DIR` |
| `ENGINE_VERSION_MISMATCH` | `UECODEGEN_ENGINE_DIR` |

### Phase 3: Diagnosis

Map collected data to issue table:

| # | Condition | Issue | Severity | Auto-Fix |
|---|-----------|-------|----------|----------|
| D1 | Binary not found at command path | BINARY_MISSING | CRITICAL | Report only |
| D2 | 0 processes, STDIO mode | SERVER_NOT_RUNNING | INFO | Normal for STDIO (spawned on demand) |
| D3 | 0 processes, Daemon mode | DAEMON_NOT_STARTED | HIGH | Start daemon |
| D4 | 2+ processes | MULTI_INSTANCE | HIGH | Kill all, keep none, request reload |
| D5 | Registry PID ≠ any running PID | STALE_REGISTRY | MEDIUM | Clean registry |
| D6 | Port in use but process not responding | HUNG_PROCESS | HIGH | Force kill + restart |
| D7 | Process running but ToolSearch empty | CLIENT_DISCONNECTED | MEDIUM | Advise reload |
| D8 | Binary older than source changes | OUTDATED_BINARY | LOW | Advise rebuild |
| D9 | narshamcp `recent_crashes` shows same `reason` ≥2 times within 5 min | REPEATED_PANIC | HIGH | Advise ue_cache_control(clear_all) |
| D10 | `/api/daemon/status.degraded_mode == true` (one or more `config_errors[]` blocking startup) | CONFIG_DEGRADED | HIGH | Phase 4b — apply `suggested_paths` candidate (Issue #11150) |
| D11 | Daemon binary outside current worktree root OR ≥2 entries with same `project_path`/`port` (cross-worktree collision) | CROSS_WORKTREE_DAEMON | MEDIUM | Report only — Phase 4 advisory + AskUserQuestion (Issue #11633) |
| D12 | narsha-adk core plugin `.mcp.json` present but `${CLAUDE_PLUGIN_ROOT}/bin/narshamcp{EXE_SUFFIX}` missing OR present-but-unrunnable (`--version` fails) | PLUGIN_BINARY_MISSING | CRITICAL | Report only — rebuild + redeploy (`scripts/build/build-rust.bat release` then `/mcp-deploy`), Issue #14128 |
| D13 | ≥ `NARSHAMCP_KILL_CIRCUIT_THRESHOLD` (default 3) `external_kill` crash records within `NARSHAMCP_KILL_CIRCUIT_WINDOW_SECS` (default 5 min) — the daemon gate would refuse startup and the connect stub would return `CircuitBreakerOpenError` (client exit 78) | CIRCUIT_BREAKER_OPEN | HIGH | Report only — investigate external kills / run breaker recovery (Issue #14991) |
| D14 | Connected daemon `started_at` predates the newest `MCP/native` op-source mtime (the *running* daemon may be missing ops the repo HEAD has — distinct from D8 which checks the *disk* binary) | STALE_DAEMON | LOW | Report only — advise `/mcp-restart --build` (Issue #17469) |
| D15 | `narshamcp connect` **or** `narshamcp daemon` exits **77** (`EXIT_SLOT_OWNER_DENIED`), or `ue_check_health(compact=false).daemon.slot_owner.exists == true` with a `path` outside this worktree — the project's daemon slot is sticky-owned by another clone binary | SLOT_OWNER_DENIED | HIGH | Report only — use the owning clone, or `NARSHAMCP_DAEMON_RELEASE_SLOT=1` once; an owner whose exe is gone is now taken over automatically (Issue #27868) |

Print diagnosis table for each server.

**D12 detection — installed-plugin binary** (Issue #14128). The `core` plugin's
`.mcp.json` + SessionStart hook both reference `${CLAUDE_PLUGIN_ROOT}/bin/narshamcp{EXE_SUFFIX}`;
when that bundled binary is absent the MCP server shows `✗ Failed to connect`
AND the MCP-First hook dies silently. Run the helper (existence + `--version`
run smoke — the runnability half `ue_check_health` omits to stay off the
session-start hot path):

```bash
python .claude/skills/mcp-doctor/scripts/verify_plugin_binary.py
# exit 0 healthy · 1 missing/unrunnable (D12 CRITICAL) · 2 not_installed (no nag)
```

Detection-only — on exit 1, report the JSON `hint` (rebuild + redeploy). Never
auto-copies a binary.

**D9 detection — narshamcp only** (Issue #11117). Run only when `server_name == "narshamcp"` AND ToolSearch confirms tools available. Phase 5 verify uses `ue_check_health(compact=true)`, which returns only the `recent_crash_count` hint when non-zero — not the full `recent_crashes` array. D9 needs the array, so issue an extra full call here:

```bash
# Pseudocode — pre-resolve the temp dir + home before substituting.
# - $TMP_DIR: $TMPDIR (POSIX) | $env:TEMP (PowerShell) | platform default
# - $HOME_DIR: $HOME (POSIX) | $env:USERPROFILE (PowerShell)
TMP_DIR="${TMPDIR:-${TMP:-/tmp}}"
HOME_DIR="${HOME:-$USERPROFILE}"

# (1) Issue full ue_check_health() call (no compact=true) and save the response
#     JSON body to $TMP_DIR/narshamcp_health.json. The skill SHOULD use the
#     MCP tool directly; the file write is so detect_repeated_panic.py can
#     read it via --health-json.

# (2) D9 burst detection — 5-minute window, threshold 2.
python .claude/skills/mcp-doctor/scripts/detect_repeated_panic.py \
    --health-json "$TMP_DIR/narshamcp_health.json" \
    --window-minutes 5 \
    --threshold 2
# Exit 0 → no D9.
# Exit 1 → D9 detected, JSON advisory on stdout (parse for advisory text).
# Exit 2 → bad input (e.g., temp file write failed). Log a one-line WARN
#          ("narshamcp 헬스 JSON 파싱 실패, D9 점검 스킵") and continue —
#          do NOT abort Phase 3 because of a transient probe failure.

# (3) 7-day chronic crash count (Issue #11117 addon — author field repro).
# Always informational, never blocks. Output goes straight to Phase 3 report.
python .claude/skills/mcp-doctor/scripts/cumulative_crash_count.py \
    --crashes-dir "$HOME_DIR/.narshamcp/logs/crashes" \
    --days 7
# Output example: "최근 7일 누적 N건 (panic=A, external_kill=B)"
# When some files were unreadable: "... (skipped=K)" suffix appended.

# (4) D13 circuit-breaker-open signature (Issue #14991). Reuses the same
#     crash dir; trips when external_kill count >= threshold within the
#     window (defaults mirror the Rust gate SSOT: 3 kills / 5 min). Report-only.
python .claude/skills/mcp-doctor/scripts/detect_circuit_breaker_open.py \
    --crashes-dir "$HOME_DIR/.narshamcp/logs/crashes" \
    --window-minutes 5 \
    --threshold 3
# Exit 0 → breaker not open (normal). No D13 line.
# Exit 1 → D13 detected, JSON advisory on stdout (parse for advisory text).
#          Append the advisory as a CIRCUIT_BREAKER_OPEN line in the Phase 3
#          report. Corroborate with client exit code 78 / the
#          "[NARSHAMCP CIRCUIT BREAKER]" stderr WARN if you have the stub log.
# Exit 2 → bad input (crashes-dir is a file / unreadable). Log one-line WARN
#          ("D13 점검 스킵") and continue — never abort Phase 3.
```

Append the 7-day count line to the Phase 3 report unconditionally (cost is one directory listing). Add the D9 advisory line only when `detect_repeated_panic.py` exited with code 1. On exit 2, log the WARN once and elide the D9 advisory. Add the D13 advisory line only when `detect_circuit_breaker_open.py` exited with code 1; on its exit 2, log a one-line WARN and elide D13.

**D11 detection — cross-worktree daemon collision** (Issue #11633). Runs unconditionally (cross-worktree applies to all narshamcp daemons, not gated by `server_name`):

```bash
# Detect daemons whose ExecutablePath is outside this worktree, or registry
# entries with duplicate project_path / port (advisory-only — Issue #11102
# + Epic #11629 forbid auto-killing cross-worktree daemons).
python .claude/skills/mcp-doctor/scripts/detect_cross_worktree.py \
    --cwd "$PWD"
# Exit 0 → no D11. Output: {"violations": []}
# Exit 1 → D11 detected. Parse violations[] JSON; append each advisory to
#          the Phase 3 report. Carry forward to Phase 4 CROSS_WORKTREE_DAEMON
#          action (AskUserQuestion).
# Exit 2 → bad input (missing registry on non-Windows or fixture path).
#          Log one-line WARN ("D11 점검 스킵") and continue — never block Phase 3.
```

The detector reuses `canonical_project_id()` from `mcp-restart/scripts/resolve_daemon_port.py` so registry normalization matches the Rust SSOT (`MCP/native/mcp_server/src/daemon/registry.rs:447-472`). Filters daemon-mode processes only via `\sdaemon\s` CommandLine anchor; bridges (`\sconnect\s`) and STDIO are excluded.

**D14 detection — stale daemon advisory** (Issue #17469). Run only when `server_name == "narshamcp"`. Compares the connected daemon's *effective vintage* against the newest local `MCP/native` build-input mtime, reusing the proven #16636 mtime gate. Catches the ambient *"connected to a long-lived daemon, never rebuilt this session"* path that `/mcp-restart`'s A0/A4.5/A4.5b miss (they require an active rebuild) and that D8 misses (D8 checks the disk binary, which can be fresh while the running daemon is stale):

```bash
# The detector resolves the daemon's started_at DIRECTLY from the registry
# (~/.narshamcp/daemons/registry.json) — no .mcp.json / $PROJECT_PATH needed, so it
# also fires in plugin-mode. Identify the CONNECTED daemon and re-anchor on its
# binary from the daemon's `/api/daemon/status` (the dashboard endpoint Phase 2b
# already resolves DASHBOARD_PORT for) — its top-level payload is live-verified to
# expose BOTH fields. Fill both:
#  • DAEMON_PROJECT = status.project_path (the UE project the connected daemon
#    serves). Passed as --project-path so the detector picks THIS session's daemon,
#    not just "the only Running one" — required on a multi-clone / multi-daemon PC
#    (the target audience) and harmless when there is only one.
#  • DAEMON_EXE = status.executable_path (check_health.rs:79 = current_exe). Passed
#    as --daemon-binary-path so the detector re-anchors on min(started_at,
#    binary_mtime) — started_at is LAUNCH time (registry.rs:826), not build time, so
#    a freshly-launched STALE binary false-negatives WITHOUT this anchor.
# (Both also appear in ue_check_health(compact=false); /api/daemon/status is the
#  confirmed single source for the pair.)

DAEMON_PROJECT="<status.project_path from GET /api/daemon/status; '' if unknown>"
DAEMON_EXE="<status.executable_path from GET /api/daemon/status; '' if unknown>"
# Issue #17543: also fill the daemon's real git_commit for the Tier 2 positive-clear.
DAEMON_GIT_COMMIT="<build_info.git_commit from ue_check_health(compact=false); '' if unknown>"

python .claude/skills/mcp-doctor/scripts/detect_stale_daemon.py \
    --source-root "MCP/native" \
    --check-opset \
    ${DAEMON_PROJECT:+--project-path "$DAEMON_PROJECT"} \
    ${DAEMON_EXE:+--daemon-binary-path "$DAEMON_EXE"} \
    ${DAEMON_EXE:+--bridge-binary "$DAEMON_EXE"} \
    ${DAEMON_GIT_COMMIT:+--daemon-git-commit "$DAEMON_GIT_COMMIT"} --json
# Tiers (Issue #17543), most precise first; first conclusive tier wins:
#   Tier 1 (--check-opset): probes the LIVE daemon's tools/list via `narshamcp connect
#     --no-auto-start` and compares per-tool operation enums against the committed
#     fingerprint docs/generated/opset_fingerprint.json. Fires STALE iff a shared tool
#     is MISSING ops HEAD defines (the #17302 niagara 29-vs-31 case). JSON has
#     "signal":"opset" + "stale_tools".
#   Tier 2 (--daemon-git-commit): if the daemon's real git_commit == `git rev-parse
#     --short HEAD`, the daemon is exactly HEAD → CURRENT (exit 0), skipping the mtime
#     tier's checkout over-fire. (Mismatch never accuses — it falls through.)
#   Tier 3 (mtime): the always-available #17469 floor (min(started_at, binary_mtime)).
# Exit 0 → daemon current. No D14 line.
# Exit 1 → D14 detected, advisory JSON on stdout. Append the `advisory` text as a
#          STALE_DAEMON line in the Phase 3 report; carry to Phase 4 advisory.
# Exit 2 → nothing could evaluate (no op-set probe, no git match, no resolvable
#          started_at — e.g. >1 Running daemon and DAEMON_PROJECT empty, or wrong
#          source-root). Log one-line WARN ("D14 점검 스킵") and continue — never block.
```

The mtime source-set is path-filtered to `MCP/native` production sources (NOT broad repo HEAD, and EXCLUDING `**/tests/`·`test_*.rs`·`*_tests.rs` — those compile only into the test binary), so doc/CI/skill commits and test-only edits do not trip it. The advisory is report-only: it nudges `/mcp-restart --build` and never auto-restarts. **Scope (Issue #17543)**: D14 is now a 3-tier detector — the op-set fingerprint (Tier 1) is the faithful, deployment-independent signal that catches the #17302 stale-op class the mtime heuristic could not; the git_commit positive-clear (Tier 2) suppresses the mtime tier's known `git checkout`/branch-switch over-fire on a HEAD-matching daemon; the mtime check (Tier 3) remains the always-available floor. The op-set comparison is per-tool and only for tools present in BOTH the live set and the fingerprint, so an env-filtered daemon (maturity / `NARSHAMCP_TOOL_SEARCH_NATIVE`) never false-positives. Multi-clone comparisons stay conservative (the nudge targets THIS clone). The committed fingerprint SSOT's internal consistency is guarded in the Pure Tests lane by `tests/unit/release/test_opset_fingerprint_drift.py` (self-consistency + canonical serialization) plus the Rust op-count guard (`schema_registry::tests::test_issue_7019_operation_enums_preserved`); regenerating it after an op change needs a UE-engine environment (a full `tools/list` requires a non-degraded daemon — engine-less CI `serve` is degraded).

### Phase 4: Auto-Fix

**Skip if `--diagnose-only`.**

Execute fixes in severity order (CRITICAL → HIGH → MEDIUM → LOW):

| Issue | Fix Action |
|-------|-----------|
| MULTI_INSTANCE | Per registry entry: `taskkill /T /F /PID <pid>` (port/PID-scoped — see Issue #11102) |
| STALE_REGISTRY | `narshamcp status --all` (auto-cleans dead PIDs); never delete the whole registry directory |
| HUNG_PROCESS | `taskkill /T /F /PID <pid>` (PID-scoped, no image-name match) → advise reload. **If `taskkill` fails on a kernel-wedged process** ("no running instance" while still listed), escalate: `python scripts/windows/kill_wedged_process.py --pid <pid> --kill` (taskkill → WMI `Win32_Process.Terminate` as SYSTEM → reboot advisory; Issue #16786, see [MONITOR_TOOL_USAGE.md](../../../docs/development/MONITOR_TOOL_USAGE.md)) |
| ABORT_TEST_SPINNER | Deliberate-crash probe wedged + spinning (`orphan_process_check.py` `kind: abort-test-spinner`, Issue #16786). **Operator-confirmed**, never auto: `python scripts/windows/kill_wedged_process.py --pid <pid> --kill`. Prevent recurrence by running probes under `scripts/windows/safe_crash_probe.py` |
| DAEMON_NOT_STARTED | Start daemon via `Start-Process` (narshamcp) or generic spawn |
| CLIENT_DISCONNECTED | Print: "Action Required: Ctrl+Shift+P → Reload Window" |
| BINARY_MISSING | Print: "Binary not found. Run build script first." |
| OUTDATED_BINARY | Print: "Binary may be outdated. Consider rebuilding." |
| REPEATED_PANIC | Print: "narshamcp가 동일 사유로 반복 panic 중 (reason='<X>', N회 / 5분). stale 메타데이터 캐시 가능성. `ue_cache_control(operation='clear_all', dry_run=True)`로 먼저 검증 후 실제 실행 권고." (advisory only — never auto-execute, see Issue #11117) |
| CIRCUIT_BREAKER_OPEN | Print the `detect_circuit_breaker_open.py` advisory: "narshamcp circuit breaker OPEN 신호 — 최근 N분 내 external_kill K회 (threshold T). … /mcp-doctor 권고. 우회: `NARSHAMCP_DISABLE_KILL_CIRCUIT_BREAKER=1` (무한 재시작 위험)." (advisory only — never auto-execute or auto-set the escape-hatch env; investigate the external-kill cause first, see Issue #14991) |
| STALE_DAEMON | Print: "연결된 narshamcp 데몬이 로컬 MCP/native 빌드 입력보다 오래됨 (vintage `<anchor>`, started `<started_at>`, 최신 입력 `<newest_file>`, drift `<N>`). 데몬이 최신 변경을 누락했을 수 있음 — `/mcp-restart --build` 권고." (advisory only — never auto-restart, best-effort mtime heuristic, see Issue #17469) |
| SLOT_OWNER_DENIED | Print: "이 프로젝트의 데몬 슬롯을 다른 클론 바이너리가 sticky 소유 중 (owner `<path>`, exists `<yes|no>`). 소유 클론이 살아있으면 그 클론의 MCP 클라이언트를 사용하고, 이 바이너리가 소유해야 하면 `NARSHAMCP_DAEMON_CLAIM_SLOT=1`, 소유권을 지우려면 `NARSHAMCP_DAEMON_RELEASE_SLOT=1` 을 1회 지정. attach만 필요하면 `--no-auto-start`." (advisory only — 이 env 들을 대신 설정하지 말 것. 소유자 exe 가 사라졌고 살아있는 소유자도 없으면 #27868 이후 첫 연결이 자동 인계하므로 거부가 지속되면 살아있는 소유자가 실재한다는 뜻이다. Issue #24414/#24441/#27868) |
| CONFIG_DEGRADED | Phase 4b drives the recovery flow (per-candidate AskUserQuestion → `apply_recovery.py`). Phase 4b MUST invoke `Skill('mcp-restart')` once after the user accepts at least one candidate — daemon caches `degraded_reasons` at `DaemonServer::new` and does NOT hot-reload from `.env` mutations (see `project_watcher.rs:27-32` SSOT). (Issue #11150) |
| CROSS_WORKTREE_DAEMON | Advisory only + `AskUserQuestion`: "다른 worktree daemon이 같은 project를 서비스 중입니다 (PID `<pid>`, binary `<path>`). [A] 유지 (다른 worktree가 owner — 본 세션은 그것을 사용) [B] 우리 worktree binary로 교체 (`/mcp-restart` 호출, 다른 worktree 사용자에게 영향)". **NEVER auto-kill** cross-worktree process (Issue #11102 + Epic #11629). On option B, invoke `Skill('mcp-restart')` after confirming the operator understands the impact. |

> **Why no image-name kill** (Issue #11102): `Stop-Process -Name narshamcp` matches every running narshamcp instance — including daemons in OTHER worktrees and `narshamcp connect ...` bridges spawned by IDE sessions. PID-tree kill via registry isolates the blast radius to one project. See [REFERENCE.md § Process Model](REFERENCE.md#process-model) for parent-relationship details.

**`--nuke` mode** (Issue #11102: port/PID-scoped, bridge-aware):

The `--nuke` mode targets ONE daemon (resolved by `--port <p>` or by `.mcp.json` `--project-path`) and its child tree. It does NOT kill daemons in other worktrees and does NOT kill IDE-spawned `narshamcp connect ...` bridges (bridges are parented to the IDE, not the daemon, so PID-tree kill never reaches them).

```bash
# 0. Resolve target daemon PID + port
PROJECT_PATH=$(python -c "import json; d=json.load(open('.mcp.json')); s=d['mcpServers']['narshamcp']; a=s.get('args',[]); print(a[a.index('--project-path')+1] if '--project-path' in a else '', end='')")

if [ -n "$NUKE_PORT" ]; then
  # Reverse-lookup: find daemon owning this port (Issue #11102 AC-B1)
  DAEMON_PID=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
    --by-port "$NUKE_PORT" --field pid --default 0)
  RESOLVED_PORT="$NUKE_PORT"
else
  # Default: THIS project's daemon (resolve via .mcp.json)
  DAEMON_PID=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
    --project-path "$PROJECT_PATH" --field pid --default 0)
  RESOLVED_PORT=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
    --project-path "$PROJECT_PATH" --field port --default 0)
fi

# 1. Graceful TCP shutdown first (5s budget)
if [ "$DAEMON_PID" != "0" ] && [ "$RESOLVED_PORT" != "0" ]; then
  timeout 5 narshamcp.exe stop --port "$RESOLVED_PORT" 2>/dev/null || true
fi

# 2. PID-tree force-kill (covers daemon + dashboard children, NOT bridges)
if [ "$DAEMON_PID" != "0" ]; then
  powershell -NoProfile -Command "if (Get-Process -Id $DAEMON_PID -ErrorAction SilentlyContinue) { taskkill /T /F /PID $DAEMON_PID 2>&1 | Out-Null }"
fi

# 3. Stale-registry fallback (AC-B3): registry PID is dead OR not in registry.
#    Use Get-CimInstance Win32_Process to find narshamcp.exe processes whose
#    CommandLine contains both `\sdaemon\s` (mode token, NOT `connect`/`stdio`)
#    AND the project path. This excludes bridges and STDIO children.
#    NEVER use `Stop-Process -Name narshamcp` — it would kill bridges too.
if [ "$DAEMON_PID" = "0" ] && [ -n "$PROJECT_PATH" ]; then
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='narshamcp.exe'\" | Where-Object { \$_.CommandLine -match '\sdaemon\s' -and \$_.CommandLine -like '*$PROJECT_PATH*' } | ForEach-Object { taskkill /T /F /PID \$_.ProcessId 2>&1 | Out-Null }"
fi

# 4. Wait for graceful + force-kill to settle
powershell -NoProfile -Command "Start-Sleep -Seconds 2"

# 5. Report scope
echo "Daemon for $PROJECT_PATH (port $RESOLVED_PORT) terminated."
echo "Other-worktree daemons and IDE-spawned bridges left intact."
echo "Action Required: Ctrl+Shift+P → Reload Window (if Claude Code lost connection)"
```

> **For non-narshamcp servers**: image-name kill is still acceptable for unknown server types because they don't have the cross-worktree daemon problem (only narshamcp keeps a per-project registry). Apply the bridge-aware pattern above only when `server_name == "narshamcp"`. Non-narshamcp servers reach this fallback:
> ```powershell
> Get-Process <other_server_name> -ErrorAction SilentlyContinue | Stop-Process -Force
> ```

### Phase 4b: Config Recovery (Issue #11150 — narshamcp only)

**Skip when**:
- `server_name != "narshamcp"`, OR
- `--diagnose-only` is set, OR
- Phase 2b returned exit 0 (no degraded mode), OR
- Phase 2b returned exit 2 / 127 (daemon non-conforming or unreachable — Phase 4 should have handled startup).

For each entry in `config_errors[]`:

1. Sort `suggested_paths[]` so `verified == true` candidates come first.
2. For each candidate (verified first), raise an `AskUserQuestion`:
   ```text
   "Apply <source> path '<path>' for <code>? (note: <note if present>)"
   options: ["Yes", "No, try next candidate"]
   ```
3. On **Yes**, invoke the applicator. The script writes `.env` (or prints shell
   commands) but does NOT restart the daemon by itself — see "Why a restart is
   required" admonition below:

   ```bash
   # Variable name routing is per-`code` (SSOT in apply_recovery.py CODE_TO_VAR).
   python .claude/skills/mcp-doctor/scripts/apply_recovery.py \
       --env-path "$PROJECT_PATH/.env" \
       --code "$CODE" \
       --source "$SOURCE" \
       --path "$PATH"
   # Exit 0   → patched (.env modified) OR shell command printed (source==env).
   #            stderr line includes the .bak backup path when one was written.
   # Exit 1   → already-correct (no change). Skip restart, skip Phase 5 wait.
   # Exit 2   → I/O failure. Surface to user, advise manual edit.
   ```

   - For `source == "env"`, the script prints `set <VAR>=<path>` (cmd) and
     `$env:<VAR>="<path>"` (PowerShell) lines to stdout — the user runs them in
     their own shell. **`.env` is not modified** (we cannot mutate the parent
     shell's environment from a script).
   - For `source ∈ {config_ini, uproject, scan}`, the script atomic-writes
     `.env` via `os.replace` and creates a `.env.bak` backup of the prior content.
4. After at least one `apply_recovery.py` exit-0 in this run AND any candidate
   acceptance (`source == "env"` included), invoke `Skill('mcp-restart')` once
   to bring the daemon up against the new env. Do NOT call it per-candidate —
   restart the daemon ONCE after the user is done choosing for ALL `config_errors[i]`
   entries. Skip the restart entirely when every applicator returned exit 1 (no-op).
5. On **No** for all verified candidates, advance to unverified ones. If those are
   also rejected, render the manual escape: print the cmd + PowerShell lines for
   the first `verified == true` candidate in resolver-chain order (env →
   config_ini → uproject → scan). If none verified, fall back to the first
   unverified candidate in the same order — note that `RecoveryCandidate` has
   no explicit `priority` field; ordering is the order suggested by
   `engine_resolution.rs::resolve_engine_candidates` / `resolve_project_candidates`,
   preserved into `suggested_paths[]`. Use `--print-only`:

   ```bash
   python .claude/skills/mcp-doctor/scripts/apply_recovery.py \
       --env-path "$PROJECT_PATH/.env" \
       --code "$CODE" \
       --source "$SOURCE" \
       --path "$PATH" \
       --print-only
   # Exit 0 — stdout has cmd + PowerShell snippets the user can paste manually.
   # No .env mutation, no restart prompt. User chooses when (and whether) to retry.
   ```
6. Skip to the next `config_errors[i]`.

**Headless / non-interactive mode**: If `--config-only --auto` (future flag, not yet
wired) OR env `MCP_DOCTOR_AUTO=1` is set, automatically pick the first `verified == true`
candidate without prompting. Default OFF — never modify `.env` silently.

> **Why a restart IS required (not a hot-reload)**: I documented this as a
> "watcher hot-reload" in an earlier draft and it was wrong. The daemon's
> [`project_watcher.rs:27-32`](../../../MCP/native/mcp_server/src/daemon/project_watcher.rs)
> explicitly states: *"In-place reload (still NO) ... daemon caches dozens of
> resolutions during `DaemonServer::new`; in-place env-var swap would invalidate
> them incoherently. **Restart-Daemon button is the recovery path.**"* The SSE
> `ConfigChanged` event fires on `.env` writes BUT only updates a dashboard toast;
> [`dashboard_api.rs:402-407`](../../../MCP/native/mcp_server/src/daemon/dashboard_api.rs)
> reads `server.config.degraded_reasons` which is populated once at `DaemonServer::new`
> and never recomputed. Without the explicit restart, Phase 5 verify will see
> `degraded_mode == true` forever and the user will hit the 3-attempt fallback
> with no actual recovery. The `source == "env"` path additionally requires the
> user's NEW shell to launch the daemon (the running daemon's `std::env::var`
> snapshot is fixed at fork) — `/mcp-restart` does this when invoked from the
> shell where the user just ran the printed `set`/`$env:` lines.

### Phase 5: Verify

After fixes, re-run Phase 2 checks (skipped in `--config-only` mode):
- Process count correct (0 for STDIO, 1 for Daemon)
- Registry clean (no stale entries)
- Port available or listening correctly

If server-specific health check is available (e.g., `ue_check_health` for narshamcp):
- Call health check tool
- Report result

**Config verify (Issue #11150 — narshamcp only)**: if Phase 4b ran any recovery actions
AND invoked `Skill('mcp-restart')` (i.e., at least one applicator exited 0), re-poll
`/api/daemon/status` to confirm the freshly restarted daemon left degraded mode. Poll
with a small budget so the restarted daemon has time to bind, register, and respond:

```bash
# /mcp-restart already waited for the daemon to come back up before returning,
# but give the dashboard port one extra polling iteration in case of a slow start.
for attempt in 1 2 3 4 5; do
  python .claude/skills/mcp-doctor/scripts/check_degraded.py \
      --port "$DASHBOARD_PORT" --json --timeout 3
  RC=$?
  if [ "$RC" != "127" ]; then break; fi
  powershell -NoProfile -Command "Start-Sleep -Seconds 1"
done

# RC dispatch:
# Exit 0   → degraded_mode == false. Recovery succeeded.
# Exit 1   → still degraded (same or different errors). Raise AskUserQuestion:
#            "Daemon is still in degraded mode after restart. Try the next candidate?"
#            On Yes → loop back to Phase 4b with remaining candidates. On No → unrecoverable.
# Exit 2   → daemon JSON shape changed mid-recovery (rare — implies a daemon upgrade
#            landed mid-session). Surface raw response to user, advise re-running
#            /mcp-doctor from scratch; do NOT retry-loop because the schema delta
#            could mean the suggested_paths contract itself is stale.
# Exit 127 → daemon never came back after restart (5 polling attempts exhausted).
#            Fallback: advise running `/mcp-restart` manually and inspecting
#            ~/.narshamcp/logs/rust_mcp.log.* for crash signatures.
```

If still degraded after 3 candidate attempts, stop the loop, surface
`mode=needs_manual_action` in the report, and link to
[CONFIGURATION.md § Daemon Memory & Concurrency](../../../docs/getting-started/CONFIGURATION.md#daemon-memory--concurrency-epic-9395--perfrust-daemon-bundle).

### Output Format

```text
=== MCP Doctor Report ===

Servers: {N} found in .mcp.json

[{server_name}]
  Mode:    {STDIO | Daemon}
  Binary:  {path} ({size}, {date})  {ok|MISSING}
  Process: {N} running {details}  {ok|MULTI_INSTANCE|NOT_RUNNING}
  Tools:   {N} available  {ok|DISCONNECTED}
  {Registry: {status}}  (Daemon only)
  {Health:  {status}}   (if health check available)

Issues Found: {N}
  [{severity}] {description} → {action taken | action required}

{Action Required: Ctrl+Shift+P → Reload Window}  (if needed)
Status: {ALL HEALTHY | FIXED (pending reload) | NEEDS MANUAL ACTION}
```

---

## Server-Specific Extensions

### narshamcp

When `server_name == "narshamcp"`:
- Check `~/.narshamcp/daemons/registry.json` for stale entries
- Use `ue_check_health(compact=true)` for health verification
- Check daemon log: `~/.narshamcp/logs/rust_mcp.log.*` (latest by date)
- Dashboard port check: registry `dashboard_port` field

### Other MCP Servers

For unknown servers:
- Process audit only (no registry/cache knowledge)
- No health check (just process + ToolSearch)
- Generic kill + reload recommendation

---

## Relationship to Existing Skills

| Skill | Role | How mcp-doctor interacts |
|-------|------|--------------------------|
| `mcp-restart` | Build + restart | mcp-doctor may call for Daemon restart in Phase 4 |
| `mcp-deploy` | Binary deployment | Separate concern (build+copy) |
| `session-health-check` | Deep MCP session check | mcp-doctor uses in Phase 5 verification |

---

## Safety

- `--diagnose-only` is read-only, safe to run anytime
- Auto-fix only kills processes matching `.mcp.json` server binaries (no wild kills)
- `--nuke` requires explicit flag — never triggered by default
- Never modify `.mcp.json` (that's `mcp-restart --switch-daemon`'s job)
- Never delete project files or build artifacts
- Always advise "Reload Window" rather than claiming reconnection (STDIO mode limitation)

---

## Self-Test Mode (Issue #11102)

`/mcp-doctor --self-test` runs a focused validation of this skill — does NOT execute the diagnostic pipeline:

1. **Structure**: Verify `SKILL.md` and `REFERENCE.md` exist
2. **Frontmatter**: Parse YAML for required fields (`name`, `description`, `argument-hint`)
3. **Bridge-aware kill guard** (Issue #11102): SKILL.md MUST NOT contain an executable `Stop-Process -Name narshamcp` statement at the start of a code-block line (the bare phrase appears in didactic prose explaining the prohibition, so the guard anchors on line-start `^\s*` to discriminate prose from executable code). Run:
   ```bash
   if grep -qE '^[[:space:]]*Stop-Process -Name narshamcp' .claude/skills/mcp-doctor/SKILL.md; then
     echo "FAIL: SKILL.md contains forbidden image-name kill (Issue #11102)"; exit 1
   else
     echo "PASS: no image-name kill found"
   fi
   ```
4. **CIM fallback wired**: SKILL.md DOES contain `Get-CimInstance Win32_Process` (stale-registry fallback). Run:
   ```bash
   grep -q "Get-CimInstance Win32_Process" .claude/skills/mcp-doctor/SKILL.md || { echo "FAIL: CIM fallback missing"; exit 1; }
   ```
5. **PID-tree kill wired**: SKILL.md DOES contain `taskkill /T /F /PID` (force-kill pattern). Run:
   ```bash
   grep -q "taskkill /T /F /PID" .claude/skills/mcp-doctor/SKILL.md || { echo "FAIL: taskkill /T pattern missing"; exit 1; }
   ```
6. **D9 advisory-only guard** (Issue #11117): The `REPEATED_PANIC` row in the Phase 4 table MUST be advisory-only (`Print:` pattern with no destructive verbs). Catches regressions where a future edit accidentally turns the advisory into auto-execution. Run:
   ```bash
   # The D9 fix row must EXIST. (We check destructive verbs first so a
   # mutation that swaps `Print:` for `taskkill` reports the real failure
   # mode, not just "missing Print:".)
   if ! grep -qE '^\| REPEATED_PANIC \|' .claude/skills/mcp-doctor/SKILL.md; then
     echo "FAIL: D9 REPEATED_PANIC row missing from Phase 4 table"; exit 1
   fi

   # 6a. D9 row must NOT contain destructive verbs.
   #     - taskkill / Stop-Process / Remove-Item are PowerShell process/file kill verbs.
   if grep -E '^\| REPEATED_PANIC \|' .claude/skills/mcp-doctor/SKILL.md \
        | grep -qE 'taskkill|Stop-Process|Remove-Item'; then
     echo "FAIL: D9 row contains a destructive process/file kill verb"; exit 1
   fi

   # 6b. D9 row must NOT contain `dry_run=False` anywhere.
   #     The advisory should always recommend dry_run=True first.
   if grep -E '^\| REPEATED_PANIC \|' .claude/skills/mcp-doctor/SKILL.md \
        | grep -qE 'dry_run\s*=\s*False'; then
     echo "FAIL: D9 row references dry_run=False (forbidden — must stay advisory)"; exit 1
   fi

   # 6c. D9 row must follow the `Print:` advisory pattern (after destructive
   #     and dry_run checks have passed — those are stricter failures).
   if ! grep -qE '^\| REPEATED_PANIC \| Print:' .claude/skills/mcp-doctor/SKILL.md; then
     echo "FAIL: D9 row exists but is not in 'Print: ...' advisory form"; exit 1
   fi

   echo "PASS: D9 row is advisory-only"
   ```
7. **D10 + Phase 2b/4b wired** (Issue #11150): SKILL.md MUST include the D10 (CONFIG_DEGRADED) row, the Phase 2b/4b headings, and reference both new helper scripts. Run:
   ```bash
   for marker in \
     '^\| D10 \| `/api/daemon/status\.degraded_mode' \
     '^### Phase 2b: Config Audit' \
     '^### Phase 4b: Config Recovery' \
     'scripts/check_degraded\.py' \
     'scripts/apply_recovery\.py'; do
     if ! grep -qE "$marker" .claude/skills/mcp-doctor/SKILL.md; then
       echo "FAIL: SKILL.md missing required marker: $marker"; exit 1
     fi
   done
   echo "PASS: D10 + Phase 2b/4b wiring intact"
   ```
8. **Recovery script is read-then-write atomic** (Issue #11150): `apply_recovery.py` MUST use `os.replace` (atomic on POSIX + Windows) and create a `.bak` backup before any `.env` mutation. Run:
   ```bash
   if ! grep -qE 'os\.replace\(' .claude/skills/mcp-doctor/scripts/apply_recovery.py; then
     echo "FAIL: apply_recovery.py missing os.replace (atomicity broken)"; exit 1
   fi
   if ! grep -qE 'shutil\.copy2\(' .claude/skills/mcp-doctor/scripts/apply_recovery.py; then
     echo "FAIL: apply_recovery.py missing shutil.copy2 (.bak backup not created)"; exit 1
   fi
   echo "PASS: apply_recovery.py atomic + backup invariants intact"
   ```
9. **No false hot-reload claim in scripts** (Issue #11150 iter-2): script docstrings/comments MUST NOT POSITIVELY assert that the daemon hot-reloads from `.env` mutations — `project_watcher.rs:27-32` SSOT explicitly says "In-place reload (still NO)". Phase 4b's explicit `Skill('mcp-restart')` is the recovery path. Catches docstring/comment regressions where a future edit reintroduces the false hot-reload claim that iter-1 BLOCKER F1+F2 already addressed in SKILL.md but iter-2 had to re-fix in `apply_recovery.py`. The regex matches POSITIVE claim phrases only (e.g., `"and reloads"`, `"auto-reload"`, `"automatically reload"`) — negations like `"does NOT hot-reload"` are intentionally NOT matched. Run:
   ```bash
   # Positive-assertion patterns only — negations are SSOT-aligned and OK
   if grep -nE 'and reload|auto[- ]reload|automatically reload|reloads (its|the) config' \
            .claude/skills/mcp-doctor/scripts/*.py; then
     echo "FAIL: script comment/docstring asserts daemon hot-reload — contradicts project_watcher.rs:27-32 SSOT"; exit 1
   fi
   echo "PASS: no positive hot-reload assertion in scripts"
   ```
10. **D11 cross-worktree detection wired** (Issue #11633): SKILL.md MUST include the D11 (CROSS_WORKTREE_DAEMON) row, reference `detect_cross_worktree.py`, and the D11 row MUST NOT contain destructive verbs (advisory-only per Issue #11102 + Epic #11629). Run:
    ```bash
    if ! grep -qE '^\| D11 \| ' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: D11 row missing from Phase 3 table"; exit 1
    fi
    if grep -E '^\| D11 \|' .claude/skills/mcp-doctor/SKILL.md \
         | grep -qE 'taskkill|Stop-Process|Remove-Item'; then
      echo "FAIL: D11 row contains destructive verb (must be advisory-only per #11102/#11629)"; exit 1
    fi
    if [ ! -f .claude/skills/mcp-doctor/scripts/detect_cross_worktree.py ]; then
      echo "FAIL: detect_cross_worktree.py missing"; exit 1
    fi
    if ! grep -qE 'scripts/detect_cross_worktree\.py' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: SKILL.md does not reference detect_cross_worktree.py"; exit 1
    fi
    echo "PASS: D11 cross-worktree detection wired and advisory-only"
    ```
11. **D12 plugin-binary detection wired** (Issue #14128): SKILL.md MUST include the D12 (PLUGIN_BINARY_MISSING) row, reference `verify_plugin_binary.py`, and the D12 row MUST NOT contain destructive verbs (detection-only — the deploy-gap is fixed at the source, never by auto-copying a binary). Run:
    ```bash
    if ! grep -qE '^\| D12 \| ' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: D12 row missing from Phase 3 table"; exit 1
    fi
    if grep -E '^\| D12 \|' .claude/skills/mcp-doctor/SKILL.md \
         | grep -qE 'taskkill|Stop-Process|Remove-Item|fs::copy'; then
      echo "FAIL: D12 row contains destructive/mutating verb (must be report-only)"; exit 1
    fi
    if [ ! -f .claude/skills/mcp-doctor/scripts/verify_plugin_binary.py ]; then
      echo "FAIL: verify_plugin_binary.py missing"; exit 1
    fi
    if ! grep -qE 'scripts/verify_plugin_binary\.py' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: SKILL.md does not reference verify_plugin_binary.py"; exit 1
    fi
    echo "PASS: D12 plugin-binary detection wired and report-only"
    ```
12. **D13 circuit-breaker-open detection wired** (Issue #14991): SKILL.md MUST include the D13 (CIRCUIT_BREAKER_OPEN) row, reference `detect_circuit_breaker_open.py`, and the D13 row MUST NOT contain destructive verbs (report-only — never auto-set the escape-hatch env or kill a process). Run:
    ```bash
    if ! grep -qE '^\| D13 \| ' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: D13 row missing from Phase 3 table"; exit 1
    fi
    if grep -E '^\| D13 \|' .claude/skills/mcp-doctor/SKILL.md \
         | grep -qE 'taskkill|Stop-Process|Remove-Item'; then
      echo "FAIL: D13 row contains destructive verb (must be report-only)"; exit 1
    fi
    if [ ! -f .claude/skills/mcp-doctor/scripts/detect_circuit_breaker_open.py ]; then
      echo "FAIL: detect_circuit_breaker_open.py missing"; exit 1
    fi
    if ! grep -qE 'scripts/detect_circuit_breaker_open\.py' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: SKILL.md does not reference detect_circuit_breaker_open.py"; exit 1
    fi
    echo "PASS: D13 circuit-breaker-open detection wired and report-only"
    ```
13. **D14 stale-daemon detection wired** (Issue #17469): SKILL.md MUST include the D14 (STALE_DAEMON) row in the Phase 3 table, the STALE_DAEMON Phase 4 advisory row in `Print:` form with no destructive verbs (advisory-only — never auto-restart), reference `detect_stale_daemon.py`, and the script MUST exist. Run:
    ```bash
    if ! grep -qE '^\| D14 \| ' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: D14 row missing from Phase 3 table"; exit 1
    fi
    if ! grep -qE '^\| STALE_DAEMON \| Print:' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: STALE_DAEMON Phase 4 row missing or not in 'Print:' advisory form"; exit 1
    fi
    if grep -E '^\| STALE_DAEMON \|' .claude/skills/mcp-doctor/SKILL.md \
         | grep -qE 'taskkill|Stop-Process|Remove-Item'; then
      echo "FAIL: STALE_DAEMON row contains destructive verb (must be advisory-only per #17469)"; exit 1
    fi
    if [ ! -f .claude/skills/mcp-doctor/scripts/detect_stale_daemon.py ]; then
      echo "FAIL: detect_stale_daemon.py missing"; exit 1
    fi
    if ! grep -qE 'scripts/detect_stale_daemon\.py' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: SKILL.md does not reference detect_stale_daemon.py"; exit 1
    fi
    echo "PASS: D14 stale-daemon detection wired and advisory-only"
    ```
14. **D14 op-set fingerprint tier wired** (Issue #17543): the root-cause Tier 1 (op-set) + Tier 2 (git_commit) must be wired — the two helper scripts and the committed fingerprint SSOT MUST exist, the Phase 3 invocation MUST pass `--check-opset`, and the detector MUST accept it. Run:
    ```bash
    for f in opset_fingerprint.py probe_daemon_opset.py; do
      if [ ! -f ".claude/skills/mcp-doctor/scripts/$f" ]; then
        echo "FAIL: $f missing"; exit 1
      fi
    done
    if [ ! -f docs/generated/opset_fingerprint.json ]; then
      echo "FAIL: committed op-set fingerprint SSOT missing"; exit 1
    fi
    if ! grep -qE '\-\-check-opset' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: Phase 3 D14 invocation does not pass --check-opset"; exit 1
    fi
    if ! grep -qE '\-\-check-opset' .claude/skills/mcp-doctor/scripts/detect_stale_daemon.py; then
      echo "FAIL: detect_stale_daemon.py does not accept --check-opset (Tier 1 not wired)"; exit 1
    fi
    echo "PASS: D14 op-set fingerprint tier wired (Issue #17543)"
    ```
15. **D15 slot-owner-denied detection wired** (Issue #27868): SKILL.md MUST include the D15 (SLOT_OWNER_DENIED) row in the Phase 3 table, the SLOT_OWNER_DENIED Phase 4 advisory row in `Print:` form with no destructive verbs (advisory-only — never auto-set the CLAIM/RELEASE escape-hatch envs, never kill the owning clone), and both rows MUST name the exit code `77` / the `daemon.slot_owner` health field so the diagnosis is reachable from data Phase 3 already collects. Run:
    ```bash
    if ! grep -qE '^\| D15 \| ' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: D15 row missing from Phase 3 table"; exit 1
    fi
    if ! grep -qE '^\| SLOT_OWNER_DENIED \| Print:' .claude/skills/mcp-doctor/SKILL.md; then
      echo "FAIL: SLOT_OWNER_DENIED Phase 4 row missing or not in 'Print:' advisory form"; exit 1
    fi
    if grep -E '^\| (D15|SLOT_OWNER_DENIED) \|' .claude/skills/mcp-doctor/SKILL.md | grep -qE 'taskkill|Stop-Process|Remove-Item'; then
      echo "FAIL: D15/SLOT_OWNER_DENIED row contains destructive verb (must be advisory-only per #27868)"; exit 1
    fi
    if ! grep -E '^\| D15 \|' .claude/skills/mcp-doctor/SKILL.md | grep -qE 'slot_owner'; then
      echo "FAIL: D15 row does not name the daemon.slot_owner health field"; exit 1
    fi
    if ! grep -E '^\| D15 \|' .claude/skills/mcp-doctor/SKILL.md | grep -qE '\*\*77\*\*|exit 77|exits \*\*77\*\*'; then
      echo "FAIL: D15 row does not name exit code 77 (EXIT_SLOT_OWNER_DENIED)"; exit 1
    fi
    echo "PASS: D15 slot-owner-denied detection wired and advisory-only"
    ```

**Output**: PASS / FAIL with per-check results. Does not execute diagnosis or kill workflow.

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `.mcp.json` not found | Wrong working directory | Print: "No .mcp.json found in current directory" |
| Process kill fails (access denied) | Elevated process | Print: "Run terminal as Administrator" |
| ToolSearch returns no tools after fix | Client cache stale | Print: "Reload Window required" |
| Daemon starts but crashes immediately | Binary incompatible | Check stderr log, advise rebuild |
