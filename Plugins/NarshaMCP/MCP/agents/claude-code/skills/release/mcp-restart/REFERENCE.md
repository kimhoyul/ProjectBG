# MCP Restart Reference

**Version**: 1.5.1

**Issue**: #8048, #8546, #11632, #16636, #17089, #17129

## Mode Detection

| `.mcp.json` args | Mode | Restart Method |
|------------------|------|----------------|
| Contains `"connect"` | Daemon | Kill process + restart daemon + TCP poll |
| No `"connect"` | STDIO | Kill process + manual Reload Window |

## Files

| File | Purpose |
|------|---------|
| `.claude/skills/mcp-restart/SKILL.md` | Skill definition with dual-mode workflow |
| `scripts/resolve_daemon_port.py` | Registry resolver (port / pid / dashboard_port / status / **started_at**, Issue #10244, #11632, #16636) |
| `scripts/spawn_daemon.sh` | Bash-background daemon spawn (replaces PowerShell `Start-Process`, Issue #11632) |
| `scripts/wait_for_daemon_ack.py` | 3-stage spawn ack (PID liveness → log grep → registry status=Running, Issue #11632) |
| `scripts/deploy_built_binary.py` | Resolve the deploy profile from the `.mcp.json` `command`, deploy the fresh `target/<profile>` build into that path, and hash-verify it (Issue #17089) |

## Arguments

| Argument | Default | Description |
|----------|---------|-------------|
| (none) | — | Restart only, auto-detect mode. Skips the kill if the daemon is already current (A0 guard, Issue #16636) |
| `--force` | off | Issue #16636: bypass the A0 "already current" guard — always restart |
| `--build` | off | Build the **deploy-path profile** (`cargo.exe build $CARGO_FLAG -p narshamcp`, derived from the `.mcp.json` `command`; Issue #17089) before restart, then deploy + hash-verify the fresh binary into the deploy path (A1.5/A4.5b). **Implies `--force`** (a build must restart onto the new binary) |
| `--switch-daemon` | off | Rewrite `.mcp.json` from STDIO to Daemon mode |

## A0 "Already Current" Guard (Issue #16636)

In Daemon mode, before the kill (A1), `/mcp-restart` checks whether the running daemon is **already** the freshly-built binary and healthy — and if so, **skips the kill** (printing `Status: ALREADY CURRENT (no restart needed)`). This prevents the documented hang: a needless kill makes attached connect-bridges (often multiple worktrees on one project) poll the dead port with no backoff, exhausting the daemon's 50-connection limit.

**Skip predicate (ALL THREE; any miss/uncertainty → normal restart, never a false skip):**
1. **Path match** — `realpath`-canonicalized `ue_check_health(compact=false).daemon.executable_path` equals `scripts/build/narshamcp_bin_path.py --strict` (case-insensitive on Windows).
2. **Freshness (B1)** — `daemon_start_time >= artifact_mtime`, compared on **epoch seconds**. `artifact_mtime = stat -c %Y` (NOT `%y`, which is local-time and hours-off vs UTC). `daemon_start_time` PRIMARY = registry `started_at` (wall-clock UTC rfc3339) via `resolve_daemon_port.py --field started_at`; FALLBACK = `/api/daemon/status.uptime_secs` as `now − uptime_secs − 2` (the `−2s` biases an uncertain result to "daemon older → restart", since `uptime_secs` is monotonic + second-truncated). This freshness gate is what distinguishes a daemon **running** the build from a **same-path in-place rebuild** that replaced the loaded image (path-equality alone would false-match the stale daemon).
3. **Healthy** — `ue_check_health` status is healthy.

Do NOT use `build_info.{git_commit,build_time,version}` for the comparison — all are static `"dev"`/`"dev-build"` on non-release/thin-lto builds.

Bypass with `--force` (or `--build`, which implies it).

## P1a Multi-Bridge Warning (Issue #16636)

Before the kill, the skill queries `/api/daemon/status` `session_count` (resolving `dashboard_port` from the registry) and warns when `>1`: killing the daemon disconnects every attached bridge (incl. other worktrees) and is what triggers the reconnect storm. If the API is unreachable, it warns "count unknown" and proceeds.

## Build → Deploy → Verify (`--build`, Issue #17089)

`--build` previously ran a hardcoded `cargo.exe build --release`, whose artifact lands under
the `release` profile's `target/` output — **not** the deploy path. The daemon respawns from `$BINARY` = the
`.mcp.json` `command` field, which on the fleet default is `MCP/native/build/thin-lto/narshamcp.exe`
— a path written **only** by `build-rust.bat`'s post-signature copy, which this skill deliberately
bypasses. So the fresh build was orphaned and the daemon came back on **stale code** while the
skill reported success; the A4.5 `.version` check was blind because code-only changes bump no
version. Fix (three coordinated steps, all keyed off `$BINARY`):

1. **Step 1 — profile-matched build.** `deploy_built_binary.py resolve --field cargo_flag` derives
   the cargo flag from the deploy path's profile (`build/thin-lto` → `--profile thin-lto`,
   `target/release` → `--release`, `*/debug` → dev). The build lands in `target/<profile>/` (the
   unlocked direct cargo output) — **never the locked deploy path**, so the build itself can run
   before the kill (preserving the #16212 "build fails → STOP, don't kill" safety).
2. **A1.5 — deploy.** After the A1 kill releases the deploy-path lock,
   `deploy_built_binary.py deploy` copies `target/<profile>/narshamcp.exe` (+ `.sig`) into `$BINARY`
   and hash-verifies the copy. No-op when the deploy path already IS the build output (setups whose
   `command` points at `target/<profile>/`). Retries for Windows file-handle release latency.
   **Lock-holder fallback (Issue #17129):** if the copy is still blocked, the helper enumerates
   `narshamcp.exe` by `ExecutablePath == $BINARY`, kills the non-`connect` holders (an orphaned,
   unregistered gui-mode daemon — `gui --port N`, which A1's registry + `\sdaemon\s` paths cannot
   reach — plus any stale same-path daemon) and retries once. Worktree-safe (each worktree's deploy
   path is distinct); **`connect` bridges are spared** (#11102), so a bridge-held lock still surfaces
   via the honest status below. On persistent lock it names the holding PID(s).
3. **A4.5b — verify.** `deploy_built_binary.py verify` exits non-zero unless
   `sha256(deploy_path) == sha256(target/<profile>)`, catching the code-only staleness the
   `.version` check misses. Combined with the A2.1/A3/A3.1 liveness gates (the daemon spawned from
   `$BINARY`), a passing verify proves the live daemon is the new code.

The helper derives everything from the `--command-path` argument alone (no repo-root guessing),
so it is worktree-safe and unit-tested in `tests/unit/pure/skills/test_deploy_built_binary.py`.

**All three `--build` spawn paths deploy** (a completeness-audit finding): **A1.5** (Path A daemon),
**B1.5** (Path B STDIO), and **Step 3** (`--switch-daemon` migration — which skips Path A/B, so it
runs its own kill→deploy→verify, else `--build --switch-daemon` would reproduce #17089 on the
first-setup path). Self-test #12 enforces `deploy` on all three (count ≥ 3).

**Honest status:** when a deploy/verify fails — e.g. an active `connect` bridge holds the deploy-path
lock, which the #17129 lock-holder fallback deliberately does NOT kill (Rule #11102) — the report
Status reads `⚠️ RESTARTED ON STALE BINARY`, never `RESTARTED SUCCESSFULLY`. The #17129 fallback
clears orphaned/stale *non-`connect`* holders (the common silent-stale cause), but it still does not
force-copy past a live `connect` bridge, so that residual case is *surfaced*, not *silent* (#17089).

**Anti-drift:** `deploy_built_binary.py` mirrors `KNOWN_PROFILES` (SSOT: `narshamcp_bin_path.py`) and
the profile↔subdir contract (`build-rust.bat`). Parity tests in `test_deploy_built_binary.py` fail in
CI if either drifts, replacing the prior line-number-comment coupling.

## Daemon Mode Details

- **Port**: resolved at runtime via [`scripts/resolve_daemon_port.py`](scripts/resolve_daemon_port.py) (Issue #10244). Resolution precedence: `NARSHAMCP_DAEMON_PORT` env > `~/.narshamcp/daemons/registry.json` entry matched by canonical project path > `19620` cold-start default (Issue #7962, raised from former `9400`).
- **TCP poll timeout**: 30 seconds
- **Spawn**: bash background + `disown` (Issue #11632, replaces PowerShell `Start-Process` which silently mangled args containing spaces — see [`scripts/spawn_daemon.sh`](scripts/spawn_daemon.sh)).
- **Spawn ack**: 4-stage gate via [`scripts/wait_for_daemon_ack.py`](scripts/wait_for_daemon_ack.py) — see [A2 Acknowledgement Contract](#a2-acknowledgement-contract-issue-11632) below.
- **Verification**: `ue_check_health(compact=true)` after TCP ready + registry status re-poll (A3.1 race guard).

## A2 Acknowledgement Contract (Issue #11632)

Pre-#11632 A2 used PowerShell `Start-Process -WindowStyle Hidden`. That call returned no exit code, so an immediate-crash daemon (e.g. args quoting failure on an engine path with spaces) was reported as success. The fix replaces A2 with a bash spawn helper and adds a 4-stage acknowledgement gate.

| Stage | Budget | On failure | Source of truth |
|---|---|---|---|
| 1. Spawn-PID liveness | immediate | **WARN** (advisory only) | `psutil.pid_exists` / `tasklist` / `os.kill(pid, 0)` |
| 2. Log pattern grep `TCP listener bound on` (after `--log-size-baseline`) | 5 s (configurable `--log-timeout`) | WARN, continue to stage 3 | [`daemon/listener.rs:22`](../../../MCP/native/mcp_server/src/daemon/listener.rs#L22) |
| 3. Registry `status=Running` polling | 5 s (configurable `--registry-timeout`) | ERROR (exit 1) | [`daemon/registry.rs:679-720`](../../../MCP/native/mcp_server/src/daemon/registry.rs#L679-L720) |
| 4. Registry-PID liveness | immediate | ERROR (exit 1) | registry `pid` field ([`registry.rs:687`](../../../MCP/native/mcp_server/src/daemon/registry.rs#L687)) verified via `pid_alive()` |

**Why Stage 1 is advisory**: on Windows Git-Bash, `$!` after a `bash …exe &` returns the bash wrapper PID, not the spawned `.exe`'s Windows OS PID, so `pid_alive($SPAWN_PID)` can return `True` for a process that died instantly. Stage 4 recovers the fail-fast guarantee by reading the PID the daemon itself wrote into the registry (its real `std::process::id()`) and verifying that.

**Why Stage 2 honors `--log-size-baseline`**: the daemon log is a daily-rolling file (`rust_mcp.log.YYYY-MM-DD`) and accumulates "TCP listener bound on" lines from every successful spawn that day. Without the baseline, stage 2 would silently match an older line on the very first poll and report PASS even when the current daemon never bound. SKILL.md A2 captures the pre-spawn file size via `stat -c %s` and passes it to the script.

**Why Stage 3 is authoritative**: Rust `register_daemon()` writes `status=Running` only after the listener bind succeeds and the entry is persisted, so a successful poll proves the daemon is ready to accept connections. Stage 2 is best-effort because tracing-appender's daily rolling file may not be flushed within the 5 s window.

A3.1 re-runs the status resolver once after the TCP poll passes; if it returns anything other than `Running`, the daemon was reaped by `cleanup_sweeper` or panicked between A2.1 and A3 and the skill aborts with ERROR (no silent success).

## STDIO Mode Details

STDIO mode performs a project-path-scoped CommandLine kill and instructs the user to manually reload:

1. `Get-CimInstance Win32_Process -Filter "Name='narshamcp.exe'"` → filter `CommandLine -like "*$PROJECT_PATH*"` AND `CommandLine -notmatch '\s(daemon|connect)\s'` → `Stop-Process -Id <pid>` (Issue #11102: bridge-aware — never use `Stop-Process -Name`)
2. Display manual reload instruction: `Ctrl+Shift+P` → "Reload Window" → Enter
3. Recommend `--switch-daemon` for zero-click restarts

> **Note**: Keyboard automation was removed in v1.1.0 (Issue #8546).
> Daemon mode is recommended for all development workflows.

## Routing

**routing.json entry**:
- Priority: 114
- Korean triggers: MCP 재시작, MCP 리스타트, 서버 재시작, narshamcp 재시작, 빌드하고 재시작, MCP 리로드, 데몬 재시작, MCP 다시 시작
- English triggers: restart MCP, restart server, restart narshamcp, MCP restart, reload MCP, reconnect MCP, build and restart, daemon restart, restart daemon
- Negative: MCP deploy, MCP test, session check, health check, MCP live test

## Integration Points

| Skill | Where | How |
|-------|-------|-----|
| `/mcp-deploy` | Step 6 | `Skill("mcp-restart")` after binary copy |
| `/deploy-test` | Phase 7 | `Skill("mcp-restart")` after full install cycle |
| `/mcp-live-test` | STEP 1 | `Skill("mcp-restart")` after successful build |

## Error Recovery

| Error | Cause | Fix |
|-------|-------|-----|
| Binary not found | Build not completed | `/mcp-restart --build` or `scripts/build/build-rust.bat` (default thin-lto); locate via `python scripts/build/narshamcp_bin_path.py` |
| `❌ Stale binary — daemon did not load the freshly-built code` (A4.5b) | A1.5 deploy or the reload did not take — deploy path ≠ fresh build (Issue #17089). The #17129 fallback already kills orphaned non-`connect` holders, so a remaining lock is most likely an active `connect` bridge (spared per #11102) | Close the `connect` bridge holding `$BINARY` (see the A1.5 PID hint), then re-run `/mcp-restart --build`; persistent → `/mcp-doctor --diagnose-only` |
| `[deploy] FAIL — built artifact not found` (A1.5) | Step 1 built a different profile than the deploy path expects, or the build was skipped | Confirm `--build` was passed and `deploy_built_binary.py resolve --field profile` matches the `.mcp.json` `command` profile segment |
| A2 spawn preflight failed | `spawn_daemon.sh` exit 2 — binary, project, or engine path does not exist | Validate `.mcp.json` `--engine-path` / `--project-path` resolve to real directories; rerun |
| `stage 1 failed — spawn PID … is not alive` | Daemon crashed within milliseconds of spawn (bad args, missing dep) | Read the log tail printed below the ERROR; common cause is a misquoted `--engine-path`. Verify the binary boots manually: `"$BINARY" --no-gui daemon --project-path "$PROJECT_PATH" --engine-path "$ENGINE_PATH"` |
| `stage 3 failed — registry status='Pending' after 5s` | Daemon spawned but never reached `register_daemon()` — often a TCP port conflict or PDB cold-cache regen exceeding 5 s | Check `~/.narshamcp/logs/rust_mcp.log.*` for panics; if cold cache, raise `--registry-timeout` or wait and rerun (see #11631 for the planned heartbeat fix) |
| `stage 4 failed — registry PID <N> (status=Running) is not alive` | Daemon registered Running then died within the millisecond window before the PID-liveness probe | Read the log tail printed below the ERROR for the crash reason; rerun `/mcp-restart` once. If it recurs, this is likely a panic on first request — check `~/.narshamcp/logs/rust_mcp.log.*` |
| A3.1 — daemon status flipped to non-Running after TCP poll | TCP poll passed but `cleanup_sweeper` reaped a Pending entry, or the daemon panicked between A2.1 and A3 | `/mcp-doctor --diagnose-only`; if recurrent, this is the bug class #11631 targets |
| Daemon port not ready (30s) | Port conflict or crash | Resolve via `python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py --project-path "$PROJECT_PATH"`, then `netstat -ano \| findstr "${DAEMON_PORT}"` |
| Health check fails | Config mismatch | Verify `.mcp.json` paths, try `ue_cache_control(clear_all)` |
