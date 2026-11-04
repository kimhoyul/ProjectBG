---
name: mcp-restart
description: "Restart MCP server after binary rebuild. Daemon mode: zero-click auto-reconnect via connect stub. STDIO mode: process kill + manual reload. Supports --build (rebuild before restart), --force (bypass the already-current guard), --switch-daemon (one-time STDIO→Daemon migration)."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) on Windows"
allowed-tools: [Bash, Read, Edit, ue_check_health]
argument-hint: |
  /mcp-restart                     # restart only (auto-detect mode); skips if daemon already current (Issue #16636)
  /mcp-restart --force             # restart even if the daemon is already current (bypass the A0 guard)
  /mcp-restart --build             # cargo build + restart (a build implies --force: always restart onto the new binary)
  /mcp-restart --switch-daemon     # migrate .mcp.json to daemon mode + restart
  /mcp-restart --build --switch-daemon  # build + migrate + restart
  /mcp-restart --self-test         # validate skill structure (no restart)
metadata:
  version: "1.5.1"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

## Current State (auto-collected)
- MCP mode: !`grep -q '"connect"' .mcp.json 2>/dev/null && echo "daemon" || echo "stdio"`
- Binary age: !`BIN=$(python scripts/build/narshamcp_bin_path.py 2>/dev/null) && stat -c '%y' "$BIN" 2>/dev/null | cut -d. -f1 || echo "not built"`

# MCP Restart

Zero-click MCP server restart for development workflow. **Issue**: #8048, #8546

> **CRITICAL**: When loaded, execute the workflow automatically. Parse `$ARGUMENTS`, detect mode, run the appropriate path.

## Workflow

### Step 0: Parse arguments and detect mode

1. Parse `$ARGUMENTS`: `--build` (rebuild first), `--force` (Issue #16636: bypass the A0 "already current" guard — always restart), `--switch-daemon` (migrate to daemon), `--self-test` (validate only). **`--build` implies `--force`** (a fresh build must restart onto the new binary; the A0 guard's path-equality would otherwise false-match a stale daemon at the same path — Issue #16636 B1).
2. If `--self-test`, jump to **Self-Test Mode**.
3. Read `.mcp.json` — if `args` contains `"connect"` → **Daemon mode**, else → **STDIO mode**.
4. Read binary path from `command` field into `$BINARY` (the daemon's **deploy path**). The build profile and deploy mechanics are derived from this path (Issue #17089) — see Step 1 (profile-matched build) and A1.5 (deploy into `$BINARY`).
5. If `--switch-daemon`, jump to **Step 3** after build (if `--build`).

### Step 1: Optional build (skip if `--build` not set)

```bash
set -o pipefail
# Issue #17089: build the profile the DEPLOY PATH advertises (the .mcp.json `command`),
# NOT a hardcoded --release. The daemon respawns from $BINARY = the command field (the
# fleet default is build/thin-lto); building --release writes target/release, which that
# deploy path never reads -> the daemon comes back on STALE code (silent). Derive the
# matching cargo flag here; A1.5 then copies the fresh target/<profile> artifact INTO the
# deploy path after the A1 kill releases its lock.
BINARY=$(python -c "import json; d=json.load(open('.mcp.json')); s=next(iter(d.get('mcpServers',{}).values()),{}); print(s.get('command',''))" 2>/dev/null)
CARGO_FLAG=$(python .claude/skills/mcp-restart/scripts/deploy_built_binary.py resolve --command-path "$BINARY" --field cargo_flag 2>/dev/null)
if [ $? -ne 0 ] || [ -z "$BINARY" ]; then
  echo "[mcp-restart] WARN: could not derive deploy profile from .mcp.json command - falling back to --release"
  CARGO_FLAG="--release"
fi
# Issue #17812: if the deploy path IS the build output (`command` -> target/<profile>/, so
# `needs_copy` is false), the LIVE daemon holds a Windows image lock on the very file the
# next cargo build must overwrite -> `error: failed to remove file ... (os error 5)`. Killing
# first does NOT help: the `connect` stub auto-respawns within seconds and re-locks the path.
# Free it by renaming the running image aside BEFORE the build — killing NOTHING, so the
# daemon stays up on the renamed copy and the stub never sees a dead daemon to respawn during
# the build window. cargo then writes fresh bytes to the freed path; A1 kill + A2 spawn below
# bring the daemon back on the fresh bytes. NO-OP on the fleet-default build/<profile> layout
# (needs_copy=true) and when the deploy path is absent. (Honest residual: if the daemon dies of
# its OWN accord mid-build, the stub respawns from its own current_exe() = the deploy path cargo
# is writing and can re-lock/break the build — no worse than today; the BUILD_RC guard below is
# the backstop.) rc=1 -> warn (build may hit the lock); rc=2 -> logged no-op (unresolvable
# layout; the build proceeds and BUILD_RC catches a genuine lock).
python .claude/skills/mcp-restart/scripts/deploy_built_binary.py prebuild-swap --command-path "$BINARY" 2>&1
PREBUILD_RC=$?
if [ "$PREBUILD_RC" -eq 1 ]; then
  echo "[mcp-restart] WARN: pre-build rename-aside failed; the build may hit the daemon's image lock (os error 5)."
elif [ "$PREBUILD_RC" -eq 2 ]; then
  echo "[mcp-restart] INFO: prebuild-swap skipped — could not resolve a known build/<profile>|target/<profile> layout from .mcp.json command; proceeding to build (BUILD_RC below catches a genuine lock)."
fi
# Issue #16820: `cargo.exe` is the rustup PATH shim — portable across the fleet. Do NOT
# hardcode an absolute per-user `~/.cargo/bin` path (breaks --build on every non-author PC),
# and keep the `.exe` (the rustup shim). Issue #17468: the `.exe` is NO LONGER a guard bypass —
# #17070 made cargo_build_guard strip `.exe`, so `cargo.exe build -p narshamcp` now denies just
# like `cargo build`. The deliberate raw-cargo bypass is now the explicit, reason-bearing
# `# narsha-build-guard-ok:` token on the build line below (this skill MUST build the
# .mcp.json-advertised profile directly — build-rust.bat can't take $CARGO_FLAG and sweeps
# artifacts). An un-tokened narshamcp build still denies (Issue #17070 evasion detection intact).
# $CARGO_FLAG is UNQUOTED on purpose: "--profile thin-lto" must word-split into two args,
# "--release" into one, "" (dev profile) into none.
( cd MCP/native/mcp_server && cargo.exe build $CARGO_FLAG -p narshamcp 2>&1 ); BUILD_RC=$?  # narsha-build-guard-ok: mcp-restart deploy-profile raw build (#17468)
# Issue #16212: ENFORCE the build-exit check (was prose-only below). A failed build
# MUST stop before the resolve — otherwise mcp-restart resolves a STALE narshamcp.exe
# and restarts the daemon on it. This skill builds via raw cargo (bypasses
# build-rust.bat), so the build-rust.bat stale-purge does NOT cover it; this guard is
# the protection. (Subshell keeps cwd at repo root for the resolver path below.)
if [ "$BUILD_RC" -ne 0 ]; then
  echo "[mcp-restart] BUILD FAILED (exit $BUILD_RC) - STOP. Not restarting on a stale binary."
  exit 1
fi
```
After a SUCCESSFUL build, verify the timestamp of the **freshly-built** artifact — the build wrote `target/<profile>/`, which the default resolver does not yet point at (it returns the still-stale deploy path): `FRESH=$(python .claude/skills/mcp-restart/scripts/deploy_built_binary.py resolve --command-path "$BINARY" --field built_artifact 2>/dev/null) && stat -c '%y' "$FRESH"`. **A1.5 then deploys it into `$BINARY`** (the daemon spawn path) after the kill.

### Step 2: Branch by mode

#### Path A: Daemon Mode (zero-click restart)

**A0.** "Already current" pre-guard (Issue #16636) — skip the kill entirely when the running daemon is **already** the freshly-built binary AND healthy. The unnecessary kill is exactly what storms attached bridges into the 50-conn limit (the hang this issue fixes). **Skip this whole step when `--force` or `--build` is set** (a rebuild means "run the new binary"). Daemon mode only; runs BEFORE A1.

The agent calls `ue_check_health(compact=false)` and reads two fields from the JSON response — `daemon.executable_path` (**`compact=false` is REQUIRED**; the compact payload omits it) and `status` — then runs the freshness gate below with `DAEMON_EXE` = the reported `executable_path` and `HEALTH_OK` = `true` iff status is healthy. The skip requires **ALL THREE**; any miss / empty / uncertainty falls through to A1, because a false "already current" skip would leave a STALE daemon running — worse than the storm. Do NOT use `build_info.{git_commit,build_time,version}` for this gate: `version` does not bump for code-only changes, and `git_commit`/`build_time` — which ARE real commits on release **and** thin-lto builds (`[profile.thin-lto] inherits="release"` → cargo reports `PROFILE=release` → build.rs embeds the real `git rev-parse --short HEAD`; only `build-rust.bat debug` yields `"dev"`; Issue #17543 corrected the earlier "thin-lto ⇒ dev" claim) — still cannot distinguish a pre- from post- *in-place same-commit* rebuild, the exact A0 case. Use the path-match + mtime gate below.

```bash
# Run ONLY if neither --force nor --build was passed.
PROJECT_PATH=$(python -c "import json; d=json.load(open('.mcp.json')); s=next(iter(d.get('mcpServers',{}).values()),{}); a=s.get('args',[]); print(a[a.index('--project-path')+1] if '--project-path' in a else '')" 2>/dev/null)
DAEMON_EXE="<daemon.executable_path from the ue_check_health(compact=false) JSON>"   # agent fills from the MCP response
HEALTH_OK="<true iff ue_check_health status is healthy>"                              # agent fills
ARTIFACT=$(python scripts/build/narshamcp_bin_path.py --strict 2>/dev/null)

# (1) Path match. Normalize like the Python resolver's canonical_project_id STRING tail
# (resolve_daemon_port.py:53-61, NOT its p.resolve() head — both A0 inputs are already
# absolute+resolved, so no ../symlink collapse is needed): backslash→slash, STRIP the Windows \\?\ extended-length
# prefix (std::env::current_exe().canonicalize() can emit it — check_health.rs:78), lowercase,
# drop trailing slash. Done in python (not sed/tr) to avoid shell backslash-escaping fragility
# and to reuse the same logic the resolver tests already cover. (`realpath -m` would NOT strip
# \\?\, so a daemon path carrying the prefix would never match → A0 silently inert. Issue #16636 W1.)
canon_path() { python -c "import sys; s=sys.argv[1].replace(chr(92),'/'); s=s[4:] if s.startswith('//?/') else s; sys.stdout.write(s.rstrip('/').lower())" "$1"; }
PATH_MATCH=no
if [ -n "$DAEMON_EXE" ] && [ -n "$ARTIFACT" ] && \
   [ "$(canon_path "$DAEMON_EXE")" = "$(canon_path "$ARTIFACT")" ]; then
  PATH_MATCH=yes
fi

# (2) Freshness — daemon launched AFTER the artifact was last written (Issue #16636 B1). Epoch seconds.
#     executable_path is only a PATH STRING (no mtime/hash) + build-rust.bat overwrites IN PLACE at the
#     same path, so path-equality ALONE false-matches a stale same-path-rebuilt daemon. This gate closes it.
ART_MTIME=$(stat -c %Y "$ARTIFACT" 2>/dev/null)   # %Y = EPOCH (never %y — that is local-time, hours-off vs UTC)
STARTED_AT=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
  --project-path "$PROJECT_PATH" --field started_at --default "" 2>/dev/null)
DAEMON_START=""
if [ -n "$STARTED_AT" ]; then
  DAEMON_START=$(date -d "$STARTED_AT" +%s 2>/dev/null)        # PRIMARY: registry wall-clock UTC (sub-second)
else
  # FALLBACK: /api/daemon/status uptime_secs (monotonic, second-truncated). Subtract 2s so the floor error
  # biases an uncertain result to "daemon older -> restart" (safe), never a false skip.
  DASH=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py --project-path "$PROJECT_PATH" --field dashboard_port --default 0)
  UPTIME=$(curl -s "http://127.0.0.1:$DASH/api/daemon/status" 2>/dev/null | python -c "import json,sys; print(json.load(sys.stdin).get('uptime_secs',''))" 2>/dev/null)
  [ -n "$UPTIME" ] && DAEMON_START=$(( $(date +%s) - UPTIME - 2 ))
fi
FRESH=no
if [ -n "$DAEMON_START" ] && [ -n "$ART_MTIME" ] && [ "$DAEMON_START" -ge "$ART_MTIME" ] 2>/dev/null; then FRESH=yes; fi

# (3) Skip ONLY if all three hold.
if [ "$PATH_MATCH" = yes ] && [ "$FRESH" = yes ] && [ "$HEALTH_OK" = true ]; then
  echo "ALREADY_CURRENT"
fi
```

**If the block printed `ALREADY_CURRENT`**: the daemon needs no restart — emit the A5-style report with `Status: ALREADY CURRENT (no restart needed)` and **STOP (do NOT run A1–A5)**:
```text
✅ Daemon already current — no restart needed (use /mcp-restart --force to restart anyway)
   Binary: <DAEMON_EXE>   Daemon started ≥ artifact mtime
Status: ALREADY CURRENT (no restart needed)
```
Otherwise (no `ALREADY_CURRENT`, or `--force`/`--build`) → continue to A1.

**A1.** Kill daemon — bridge-aware (Issue #11102): graceful TCP `narshamcp.exe stop --port` first, then PID-tree force-kill. **NEVER** use `Stop-Process -Name narshamcp` — it would kill bridges from other IDEs and daemons in unrelated worktrees (~3 hour debug detour during 2026-05-07 `/mcp-live-test`).

```bash
# P1a (Issue #16636): warn if other bridges are attached — killing the daemon disconnects
# all of them (incl. other worktrees) and is what triggers the reconnect storm. One daemon
# per project; each attached bridge = one session, so session_count is the blast radius.
P1A_DASH=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py --project-path "$PROJECT_PATH" --field dashboard_port --default 0)
P1A_SESS=$(curl -s "http://127.0.0.1:$P1A_DASH/api/daemon/status" 2>/dev/null | python -c "import json,sys; print(json.load(sys.stdin).get('session_count',''))" 2>/dev/null)
if [ -n "$P1A_SESS" ] && [ "$P1A_SESS" -gt 1 ] 2>/dev/null; then
  echo "⚠️  $P1A_SESS bridge(s) attached to this daemon (incl. other worktrees) — restart disconnects all & may storm the reconnect path."
elif [ -z "$P1A_SESS" ]; then
  echo "ℹ️  bridge count unknown (/api/daemon/status unreachable) — proceeding with restart."
fi

# Resolve THIS project's daemon PID + port from registry
DAEMON_PID=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
  --project-path "$PROJECT_PATH" --field pid --default 0)
RESOLVED_PORT=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
  --project-path "$PROJECT_PATH" --field port --default 0)

if [ "$DAEMON_PID" != "0" ]; then
  # Step 1: graceful TCP stop (5s budget — see cli.rs:372-391)
  # Issue #11630: pass --project-path so daemons running v7.x+ refuse the
  # shutdown when another worktree happens to share our port (Epic #11629
  # forensic timeline step 2-3). Older daemons ignore the extra param and
  # shut down as before — the protection only kicks in when both endpoints
  # are upgraded.
  if [ "$RESOLVED_PORT" != "0" ]; then
    if [ -n "$PROJECT_PATH" ]; then
      timeout 5 narshamcp.exe stop --port "$RESOLVED_PORT" --project-path "$PROJECT_PATH" 2>/dev/null || true
    else
      timeout 5 narshamcp.exe stop --port "$RESOLVED_PORT" 2>/dev/null || true
    fi
  fi
  # Step 2: PID-tree force-kill if graceful failed (taskkill is no-op if PID dead)
  powershell -NoProfile -Command "if (Get-Process -Id $DAEMON_PID -ErrorAction SilentlyContinue) { taskkill /T /F /PID $DAEMON_PID 2>&1 | Out-Null }"
fi

# Step 3: stale-registry fallback — registry empty OR PID dead
# Match by mode token (`\sdaemon\s`) + project path (excludes bridges and STDIO)
if [ "$DAEMON_PID" = "0" ] && [ -n "$PROJECT_PATH" ]; then
  powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='narshamcp.exe'\" | Where-Object { \$_.CommandLine -match '\sdaemon\s' -and \$_.CommandLine -like '*$PROJECT_PATH*' } | ForEach-Object { taskkill /T /F /PID \$_.ProcessId 2>&1 | Out-Null }"
fi

powershell -NoProfile -Command "Start-Sleep -Seconds 2"
```

**A1.5.** Deploy the freshly-built binary into the daemon's deploy path (Issue #17089) — **run ONLY when `--build` was set**. Step 1 built into `target/<profile>/` (the unlocked direct cargo output); the A1 kill just released the file lock on `$BINARY` (the `.mcp.json` `command` path, e.g. `build/thin-lto/narshamcp.exe`). Copy the fresh artifact into `$BINARY` so the A2 respawn — and every future cold spawn via the `.mcp.json` `connect` stub — runs the NEW code. Without this, `--build` deploys nothing (the exact bug this fixes). The helper is a **no-op** when the deploy path already IS the build output (setups whose `command` points at `target/<profile>/`). **Deploy-path lock-holder fallback (Issue #17129):** if the copy is blocked by a Windows image lock, the helper enumerates `narshamcp.exe` processes whose `ExecutablePath == $BINARY`, kills the non-`connect` ones (an **orphaned, unregistered gui-mode** daemon — `gui --port N`, no `daemon` token, no registry entry, not in any killed daemon's tree — that A1 cannot reach, plus any stale same-path daemon), and retries once. It is worktree-safe (a sibling worktree's daemon runs from a *different* path) and **spares `connect` bridges** (#11102). **Lock-immune rename-aside swap (Issue #17227):** if a spared `connect` bridge still holds `$BINARY` (the common `build/`-deploy + active-session case), the helper deploys *around* it — renaming the locked running image to `$BINARY.old` (allowed on Windows: the image is opened `FILE_SHARE_DELETE`, the live bridge keeps running from the renamed file) and writing fresh bytes to `$BINARY`, **killing nothing**. So `--build` now lands FRESH instead of respawning STALE; the honest failure below fires only if the rename-aside *itself* fails. **Build-output deploy path (Issue #17812):** when `command` points at `target/<profile>/` itself (`needs_copy=false`), A1.5 `deploy` is a no-op — the fresh bytes were written directly by Step 1's cargo build, which only succeeds because the Step 1 `prebuild-swap` already renamed the locked running image aside *before* the build. For that layout the meaningful post-restart freshness signal is the `build_info.git_commit == HEAD` check in A5 below (A4.5b's `sha256(deploy)==sha256(built)` is vacuously true when they are the same path).

```bash
# Run ONLY if --build was passed.
python .claude/skills/mcp-restart/scripts/deploy_built_binary.py deploy --command-path "$BINARY"
DEPLOY_RC=$?
if [ "$DEPLOY_RC" -eq 2 ]; then
  # rc=2 = config error: $BINARY did not resolve to a known build/<profile>|target/<profile> path
  # (e.g. malformed .mcp.json command; Step 1 then fell back to --release). No copy attempted.
  echo "⚠️  A1.5 skipped - could not resolve a deploy path/profile from .mcp.json command ($BINARY)."
  echo "   Check the \`command\` field; the daemon will respawn from whatever \$BINARY points at."
elif [ "$DEPLOY_RC" -ne 0 ]; then
  echo "❌ A1.5 deploy FAILED (exit $DEPLOY_RC) - the deploy path still holds the OLD binary."
  echo "   The daemon is already down; A2 respawns it on the STALE binary so you are not left dead,"
  echo "   but this session's MCP calls will hit OLD code. The lock-holder fallback (Issue #17129)"
  echo "   already killed any non-connect holder; a remaining lock is most likely an active"
  echo "   'connect' bridge (spared per #11102 - see the PID hint above). Close it, then re-run."
fi
```

**A2.** Restart daemon (detached) — extract `--project-path` and `--engine-path` from `.mcp.json` args (skip `"connect"`), capture the daily-rolling log's pre-spawn size as a baseline (so stage 2 only greps lines from THIS spawn), then spawn via the bash-background helper (Issue #11632 — replaces PowerShell `Start-Process` whose args-quoting silently mangled paths with spaces):
```bash
LOG="$HOME/.narshamcp/logs/rust_mcp.log.$(date -u +%Y-%m-%d)"   # UTC — tracing-appender Rotation::DAILY rolls on UTC midnight
BASELINE=$([ -f "$LOG" ] && stat -c %s "$LOG" 2>/dev/null || echo 0)
SPAWN_PID=$(bash .claude/skills/mcp-restart/scripts/spawn_daemon.sh \
    "$BINARY" "$PROJECT_PATH" "$ENGINE_PATH" "$LOG") || {
  echo "ERROR: A2 spawn preflight failed"; exit 1; }
```

**A2.1.** Wait for daemon to acknowledge — 4-stage gate (spawn-PID liveness [advisory] → log `TCP listener bound on` grep (after baseline) → registry `status=Running` poll → registry-PID liveness). This closes the silent-success gap where the previous `Start-Process` returned no exit code (Issue #11632):
```bash
python .claude/skills/mcp-restart/scripts/wait_for_daemon_ack.py \
    --project-path "$PROJECT_PATH" --log-file "$LOG" --log-size-baseline "$BASELINE" \
    --spawn-pid "$SPAWN_PID" --log-timeout 5 --registry-timeout 5 || {
  echo "ERROR: A2 daemon spawn did not ack (PID $SPAWN_PID)"; exit 1; }
```

**A3.** Resolve daemon port from registry (Issue #10244), then TCP poll. **Validate `$PROJECT_PATH` is non-empty before invoking the resolver** — on extraction failure, surface `ERROR: Cannot extract --project-path from .mcp.json` and STOP.
```bash
DAEMON_PORT=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
  --project-path "$PROJECT_PATH" --default 19620)
```
Then TCP poll `127.0.0.1:$DAEMON_PORT` every 1s, timeout 30s. On success → "Daemon ready". On timeout → "ERROR: Daemon did not start on port $DAEMON_PORT".

**A3.1.** TCP-up-but-dead race guard (Issue #11632) — re-verify registry `status` once after TCP poll passes; if no longer `Running`, the daemon was reaped or panicked between A2.1 and A3:
```bash
STILL_RUNNING=$(python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py \
  --project-path "$PROJECT_PATH" --field status --default Missing)
if [ "$STILL_RUNNING" != "Running" ]; then
  echo "ERROR: A3.1 — daemon status flipped to '$STILL_RUNNING' after TCP poll passed (race with cleanup_sweeper or crash)"
  exit 1
fi
```

> Resolution precedence: `NARSHAMCP_DAEMON_PORT` env > registry `~/.narshamcp/daemons/registry.json` matched by canonical project path > `--default 19620` (post-#7962 cold-start default).

**A4.** Verify: `ue_check_health(compact=true)`. If fail → check `~/.narshamcp/logs/mcp_server.log`, verify port not in use, try manual start.

**A4.5.** Binary version mismatch detection (Issue #12872). After the A4 `ue_check_health(compact=true)` call returns, the skill agent MUST compare the daemon-reported `.version` field against the build artifact's `Cargo.toml` `[package].version`. A mismatch means the daemon did NOT pick up the freshly-deployed binary — the current MCP session's tool calls will keep hitting the old code path. Concretely:

1. Extract `EXPECTED_VERSION` from `MCP/native/mcp_server/Cargo.toml` via:
   ```bash
   EXPECTED_VERSION=$(awk -F'"' '/^version =/{print $2; exit}' MCP/native/mcp_server/Cargo.toml)
   ```
2. Extract `DAEMON_VERSION` from the A4 `ue_check_health` response — it is the top-level `.version` string in the compact payload (per CLAUDE.md § Health Check Token Optimization, `compact=true` returns `{status, version, editor_hint}`).
3. If `EXPECTED_VERSION != DAEMON_VERSION`, emit the mismatch block below to stdout BEFORE the A5 report header (so it sits above the `=== MCP Restart Report ===` line in the final output):

   ```text
   ❌ Binary version mismatch — daemon did not reload
      Expected: <EXPECTED_VERSION>
      Got:      <DAEMON_VERSION>
      → This session's MCP context is now stale (still bound to old binary).
      → Action: kill daemon and start a fresh Claude Code session.
      → Diagnostic: `narshamcp status --all` then `/mcp-doctor --diagnose-only`
   ```

4. If versions match, proceed to A5 silently — no mismatch line.

**A4.5b.** Fresh-build hash check (Issue #17089) — **run ONLY when `--build` was set**. The `.version` comparison above is **blind to code-only changes**: the common iterative case bumps no version, so `EXPECTED_VERSION == DAEMON_VERSION` even when the daemon is running stale code (the failure this issue fixes). Confirm the deploy path is byte-identical to the freshly-built artifact — self-contained, no extra MCP call:

```bash
# Run ONLY if --build was passed.
python .claude/skills/mcp-restart/scripts/deploy_built_binary.py verify --command-path "$BINARY"
HASH_RC=$?
```

`HASH_RC == 2` means the deploy path couldn't be resolved from the `.mcp.json` command (exotic command / `--release` fallback). It is **not** staleness, but under `--build` it means the fresh build was **not** deployed/verified → feed it to the A5 "DEPLOY NOT CONFIRMED" status (do **not** skip silently). If `HASH_RC == 1` (genuine stale), emit the block below to stdout BEFORE the A5 report header (it means A1.5 deploy or the reload did not take — same remediation as a version mismatch):

```text
❌ Stale binary — daemon did not load the freshly-built code (Issue #17089)
   Deploy path: <$BINARY>
   → A1.5 deploy or the daemon reload did not take; this session's MCP calls hit OLD code.
   → Action: re-run /mcp-restart --build; if it persists, /mcp-doctor --diagnose-only.
```

The A2 spawn launched the daemon from `$BINARY` and A2.1/A3/A3.1 already proved it is `Running`, so a passing `verify` (deploy path == the `target/<profile>` artifact) means the daemon spawned from the freshly-deployed bytes. The Step 1 `BUILD_RC` gate (#16212) ensures the build **invocation** succeeded; cargo's incremental rebuild then keeps `target/<profile>` in sync with the current source (it recompiles whatever changed, or no-ops when already current). The one residual blind spot: `verify` cannot tell "built the right thing" from "built nothing new" — if Step 1 fell back to `--release` while the deploy path is a different profile, `verify` compares against the wrong `target/<profile>` and may pass spuriously (the `--release` fallback path only). (Optional cross-check if you already hold `compact=false` output from A0: confirm `daemon.executable_path` canon-equals `$BINARY`.)

> Why this check exists: see Issue #12872 — `/mcp-deploy` followed by failed daemon reload was misdiagnosed as "new session needed" when the real issue was a silent binary-pickup failure. Surfacing the mismatch explicitly tells the operator whether this session is salvageable (no mismatch → keep going) or must be restarted (mismatch → fresh session required).
>
> **Note**: Steps 1-3 are agent-executed (the skill agent reads the `ue_check_health` MCP-tool response as JSON, not as a shell variable). The Cargo.toml extractor IS a shell one-liner; the comparison and emission are agent logic. Do not assume `HEALTH_JSON` exists as a shell var — A4 invokes an MCP tool, which returns structured data to the agent, not to the shell.

**A4.5c.** Build-output layout freshness — **run ONLY when `--build` was set AND the deploy path IS the build output** (`needs_copy=false`; check via `python .claude/skills/mcp-restart/scripts/deploy_built_binary.py resolve --command-path "$BINARY" --field needs_copy`). On that layout A4.5b's `verify` is **vacuously true** — `deploy_path` and `built_artifact` are the *same file*, so `sha256(deploy)==sha256(built)` proves nothing about freshness — and the A4.5 `.version` check is blind to code-only changes. The meaningful signal is the embedded git commit. The agent issues **one** `ue_check_health(compact=false)` read (A4 uses `compact=true`, which omits `build_info`; the A0 `compact=false` read is skipped under `--build`), extracts `build_info.git_commit`, and compares it to `git rev-parse --short HEAD`:

```bash
HEAD_COMMIT=$(git rev-parse --short HEAD 2>/dev/null)
# DAEMON_COMMIT = build_info.git_commit from the agent's ue_check_health(compact=false) read.
```

thin-lto embeds a real commit (not `"dev"`) — `[profile.thin-lto] inherits="release"` → cargo reports `PROFILE=release` → `build.rs` embeds `git rev-parse --short HEAD` (Issue #17543). If `DAEMON_COMMIT != HEAD_COMMIT`, emit the block below BEFORE the A5 report header and feed the non-clean status into A5:

```text
❌ Build-output layout: daemon git_commit ≠ HEAD (Issue #17812)
   Deploy path: <$BINARY>   Expected: <HEAD_COMMIT>   Got: <DAEMON_COMMIT>
   → The prebuild-swap/build/respawn did not land HEAD bytes; this session hits OLD code.
   → Action: re-run /mcp-restart --build; if it persists, /mcp-doctor --diagnose-only.
```

Residual (state it, do not hide it): `git_commit==HEAD` cannot distinguish a same-HEAD in-place rebuild from a dirty working tree — the same limit the A0 gate calls out. For this layout it is the best available freshness signal; treat a match as PASS for the A5 `Status:` line.

**A5.** Report:

```text
✅ Daemon mode detected → this session survives the restart
   (Same-session ok: continue measurement / chained skill calls)

=== MCP Restart Report ===
Mode: Daemon (zero-click)
Binary: <binary_path>
Daemon Port: ${DAEMON_PORT}
Startup: {elapsed}s
Health: {pass/fail}
Status: RESTARTED SUCCESSFULLY
```

**Honest-status rule (Issue #17089) — applies whenever `--build` was set.** The `Status:` line MUST reflect whether the fresh bytes actually landed on the deploy path. `RESTARTED SUCCESSFULLY` is allowed **only** when the deploy *and* the A4.5b hash check both confirmed the fresh bytes are live. Two non-clean cases:

- **A1.5 deploy returned `1` (deploy failed) OR A4.5b reported stale (`HASH_RC` `1`)** — the daemon came back on OLD code. With the rename-aside swap (Issue #17227) a spared `connect`-bridge lock **no longer** causes this (the deploy lands FRESH around the live bridge); a `1` now means even the rename-aside failed — e.g. a non-`FILE_SHARE_DELETE` locker, or a disk/permission error. Set:
  ```text
  Status: ⚠️ RESTARTED ON STALE BINARY — deploy did not land (this session hits OLD code; see ❌ above; remediate the lock-holder PID, then re-run /mcp-restart --build)
  ```
- **A1.5 deploy OR A4.5b returned `2` (deploy path could not be resolved to a known `build/<profile>`|`target/<profile>` layout — e.g. an exotic `.mcp.json` command, which forced the Step 1 `--release` fallback).** Under `--build` this means **the fresh build was NOT deployed and could NOT be verified** — fresh code is *not* guaranteed (this is the same silent-stale class as #17089, just on a non-standard path). Set:
  ```text
  Status: ⚠️ RESTARTED — DEPLOY NOT CONFIRMED (.mcp.json command is not a known build/<profile> layout; fresh code NOT guaranteed — fix the command path or build via scripts/build/build-rust.bat)
  ```

**NEVER** print `RESTARTED SUCCESSFULLY` when the deploy/verify returned **any** non-zero (1 *or* 2) under `--build` — reporting success over a deploy that did not land is the exact #17089 deception this fix exists to kill.

#### Path B: STDIO Mode (process kill + manual reload)

**B1.** Kill process — bridge-aware (Issue #11102): STDIO has no daemon registry entry, so use project-path + mode-anchored CommandLine match (NOT image-name). This kills only THIS project's STDIO process and never touches bridges (`connect` mode) or daemons (`daemon` mode) in other worktrees.

```bash
powershell -NoProfile -Command "Get-CimInstance Win32_Process -Filter \"Name='narshamcp.exe'\" | Where-Object { \$_.CommandLine -like '*$PROJECT_PATH*' -and \$_.CommandLine -notmatch '\s(daemon|connect)\s' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force -ErrorAction SilentlyContinue }"
```

**B1.5.** Deploy the freshly-built binary into `$BINARY` (Issue #17089) — **run ONLY when `--build` was set**. STDIO reloads spawn a fresh process from the `.mcp.json` `command` path, so the new build must land there too (B1 just killed the holder, releasing the lock). Same helper, same no-op-when-already-the-deploy-path semantics as A1.5:

```bash
# Run ONLY if --build was passed.
python .claude/skills/mcp-restart/scripts/deploy_built_binary.py deploy --command-path "$BINARY"
B15_RC=$?
[ "$B15_RC" -eq 1 ] && echo "❌ B1.5 deploy FAILED - manual reload will load the OLD binary; re-run /mcp-restart --build."
[ "$B15_RC" -eq 2 ] && echo "⚠️  B1.5 deploy NOT CONFIRMED - .mcp.json command is not a known build/<profile> path; fresh code not guaranteed."
```

**B2.** Report and recommend daemon migration. **Honest-status rule (Issue #17089):** if `--build` was set and B1.5 deploy returned **any** non-zero, the `Status:` line MUST NOT read clean — use `⚠️ AWAITING MANUAL RELOAD ONTO STALE BINARY` for `B15_RC==1`, or `⚠️ AWAITING RELOAD — DEPLOY NOT CONFIRMED (.mcp.json command not a known build/<profile> layout)` for `B15_RC==2`.
```text
=== MCP Restart Report ===
Mode: STDIO (manual reload required)
Binary: <binary_path>
Process: Killed
Status: AWAITING MANUAL RELOAD

STDIO mode is deprecated for development workflows.
Daemon mode provides zero-click restarts with auto-reconnect.

Switch now (one-time, takes 5 seconds):
  /mcp-restart --switch-daemon

Manual reload (if staying on STDIO):
  Press Ctrl+Shift+P -> type "Reload Window" -> Enter
```

> STDIO requires manual Reload Window — neither Claude Code nor VSCode CLI support programmatic reload.
> **Always recommend `--switch-daemon` first** — STDIO restart is a last resort.

### Step 3: Switch to Daemon mode (`--switch-daemon` only)

1. Read current `.mcp.json`, extract `command` (→ `$BINARY`), `--project-path` (→ `$PROJECT_PATH`), `--engine-path`.
2. **Deploy the fresh build into `$BINARY` first — ONLY if `--build` was set (Issue #17089).** `--switch-daemon` jumps straight here, skipping Path A/B (and their A1.5/B1.5 deploy), so WITHOUT this step the new daemon spawns from the STALE deploy path — a **full reproduction of #17089** on exactly the first-time STDIO→daemon migration where `--build` is most common. The current STDIO MCP process holds the Windows image lock on `$BINARY`, so release it (bridge-aware, project-scoped — **never** `Stop-Process -Name`) before copying:
   ```bash
   # Run ONLY if --build was passed.
   PROJECT_PATH=$(python -c "import json; d=json.load(open('.mcp.json')); s=next(iter(d.get('mcpServers',{}).values()),{}); a=s.get('args',[]); print(a[a.index('--project-path')+1] if '--project-path' in a else '')" 2>/dev/null)
   # Kill the STDIO process holding $BINARY — same bridge-aware pattern as B1 (excludes
   # `daemon`/`connect` bridges & other worktrees' daemons). Releases the lock for the copy.
   powershell -NoProfile -Command "\$processName = 'narshamcp' + '.exe'; Get-CimInstance Win32_Process | Where-Object { \$_.Name -eq \$processName -and \$_.CommandLine -like '*$PROJECT_PATH*' -and \$_.CommandLine -notmatch '\s(daemon|connect)\s' } | ForEach-Object { Stop-Process -Id \$_.ProcessId -Force -ErrorAction SilentlyContinue }"
   powershell -NoProfile -Command "Start-Sleep -Seconds 2"
   python .claude/skills/mcp-restart/scripts/deploy_built_binary.py deploy --command-path "$BINARY"
   DEPLOY_RC=$?
   if [ "$DEPLOY_RC" -eq 2 ]; then
     echo "⚠️  Step 3 deploy skipped - could not resolve deploy path from .mcp.json command ($BINARY)."
   elif [ "$DEPLOY_RC" -ne 0 ]; then
     echo "❌ Step 3 deploy FAILED (exit $DEPLOY_RC) - the new daemon will spawn STALE bytes."
     echo "   Ensure nothing locks \$BINARY (see PID hint), then re-run /mcp-restart --build --switch-daemon."
   fi
   ```
3. Rewrite `.mcp.json` with `"args": ["connect", "--project-path", "<project_path>", "--engine-path", "<engine_path>"]` and `"env": {"RUST_LOG": "info"}`.
4. Start daemon (same as A2 + A2.1 ack), wait for TCP ready (same as A3 + A3.1 race guard).
5. **If `--build`, verify the daemon loaded the fresh bytes (same as A4.5b):** `python .claude/skills/mcp-restart/scripts/deploy_built_binary.py verify --command-path "$BINARY"; HASH_RC=$?` — if `HASH_RC` is non-zero **and not 2**, emit the `❌ Stale binary` block (Issue #17089) before the report.
6. Report: `.mcp.json updated → Daemon mode. Reload VSCode once to activate.` **Honest-status (Issue #17089):** if `--build` was set and step 2 deploy or step 5 verify returned **any** non-zero, never report a clean switch — prepend `⚠️ NEW DAEMON IS ON STALE BYTES — deploy did not land (see ❌ above)` for `rc==1`, or `⚠️ NEW DAEMON — DEPLOY NOT CONFIRMED (.mcp.json command not a known build/<profile> layout; fresh code NOT guaranteed)` for `rc==2`.

## Error Recovery

| Error | Fix |
|-------|-----|
| Binary not found | `/mcp-restart --build` (builds the deploy-path profile + deploys), or `scripts/build/build-rust.bat` (default thin-lto); resolver: `python scripts/build/narshamcp_bin_path.py` |
| Port not ready (30s) | `/mcp-doctor`; check `netstat -ano \| findstr "${DAEMON_PORT}"` (resolve via `scripts/resolve_daemon_port.py`) |
| Multiple instances | `/mcp-doctor --nuke` to kill all + clean + restart |
| Health check fails | `/mcp-doctor --diagnose-only`; verify `.mcp.json` paths |
| `.mcp.json` missing | Run from project root |
| Build fails | Fix Rust errors; `cargo check` for diagnostics |

## Safety

- Never modify `.mcp.json` without `--switch-daemon`. Verify binary exists before restart.
- Daemon: verify TCP port listening before declaring success. STDIO: kill process + manual reload only.

## Self-Test Mode

`/mcp-restart --self-test` runs a focused validation of this skill.

> **Intended environment**: run AFTER a successful `/mcp-restart`, with a healthy daemon running. `--self-test` is **strict** — check #6 will FAIL if no daemon exists.

1. **Structure**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
2. **Frontmatter**: Parse YAML for required fields (`name`, `description`, `allowed-tools`)
3. **Routing**: Verify routing.json entry exists and trigger patterns match
4. **Version sync**: SKILL.md `metadata.version` matches REFERENCE.md version field
5. **Mode detection**: `.mcp.json` parsing returns "daemon" or "stdio"
6. **Bridge-aware kill guard** (Issue #11102): SKILL.md MUST NOT contain an executable `Stop-Process -Name narshamcp` statement at the start of a code-block line (line-start anchor `^\s*` discriminates prose from executable code). Run:
   ```bash
   if grep -qE '^[[:space:]]*Stop-Process -Name narshamcp' .claude/skills/mcp-restart/SKILL.md; then
     echo "FAIL: SKILL.md contains forbidden image-name kill (Issue #11102)"; exit 1
   fi
   grep -q "narshamcp.exe stop --port" .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: graceful stop pattern missing"; exit 1; }
   ```
7. **Resolver / live daemon parity** (Issue #10244, strict): run `python .claude/skills/mcp-restart/scripts/resolve_daemon_port.py --project-path "$PROJECT_PATH" --default 19620`, then call `ue_check_health(compact=true)`.
   - **PASS** only if both: (a) a daemon process is running for this project AND (b) `ue_check_health` succeeds via the resolved port.
   - **FAIL** if no daemon running, with message: `FAIL: --self-test requires a running daemon. Run /mcp-restart first, then re-run --self-test.`
   - **FAIL** if daemon exists but health check via the resolved port fails (resolver / port mismatch — the bug class this skill fix is meant to prevent). Remediation: stale registry entry → `narshamcp status --all` (auto-cleans dead PIDs); persistent mismatch → `/mcp-doctor --diagnose-only` then `narshamcp stop --port <port>` + `/mcp-restart`.
8. **A2 ack helpers present** (Issue #11632): `spawn_daemon.sh` and `wait_for_daemon_ack.py` must exist under `.claude/skills/mcp-restart/scripts/` and the Python helper must respond to `--help` with exit 0.
   ```bash
   test -f .claude/skills/mcp-restart/scripts/spawn_daemon.sh || { echo "FAIL: spawn_daemon.sh missing"; exit 1; }
   python .claude/skills/mcp-restart/scripts/wait_for_daemon_ack.py --help >/dev/null 2>&1 || { echo "FAIL: wait_for_daemon_ack.py --help failed"; exit 1; }
   ```
9. **PowerShell `Start-Process` regression guard** (Issue #11632): SKILL.md MUST NOT contain an executable PowerShell `Start-Process -FilePath` spawn for the daemon (line-start anchor `^\s*` discriminates prose / fenced examples from executable code). The bash-background helper is the only sanctioned A2 spawn path.
   ```bash
   if grep -qE '^[[:space:]]*Start-Process -FilePath' .claude/skills/mcp-restart/SKILL.md; then
     echo "FAIL: SKILL.md still contains PowerShell Start-Process spawn (Issue #11632 regression)"; exit 1
   fi
   ```
10. **A0 "already current" guard present + correct** (Issue #16636): SKILL.md MUST contain the A0 pre-guard, it MUST use a **freshness/mtime check** (not path-equality alone — path-only is the #16636 B1 false-skip bug), `--force` MUST be parsed as a bypass, and **A0 must precede A1**. (Structural lint only — the false-skip *behavior* is proven by the live same-path-rebuild case, not this check.)
   ```bash
   grep -qE '^\*\*A0\.\*\*' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: A0 already-current guard missing (#16636)"; exit 1; }
   grep -qE 'started_at|stat -c %Y' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: A0 freshness/mtime check missing — path-only equality is the #16636 B1 false-skip bug"; exit 1; }
   grep -qE -- '--force' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: --force A0 bypass missing (#16636)"; exit 1; }
   A0L=$(grep -nE '^\*\*A0\.\*\*' .claude/skills/mcp-restart/SKILL.md | head -1 | cut -d: -f1)
   A1L=$(grep -nE '^\*\*A1\.\*\*' .claude/skills/mcp-restart/SKILL.md | head -1 | cut -d: -f1)
   { [ -n "$A0L" ] && [ -n "$A1L" ] && [ "$A0L" -lt "$A1L" ]; } || { echo "FAIL: A0 must precede A1 (#16636)"; exit 1; }
   ```
11. **No hardcoded per-user cargo path** (Issue #16820): the Step 1 build line MUST NOT carry an absolute `/c/Users/<name>/.cargo/...` cargo path (breaks `--build` on every non-author fleet PC — `No such file` / exit 127) and MUST keep the PATH `cargo.exe` shim (a bare `cargo build -p narshamcp` would trip `cargo_build_guard`). The build flag is now profile-driven (`$CARGO_FLAG`, Issue #17089), so the grep no longer requires the `--release` literal — only the portable `cargo.exe ... -p narshamcp` shape. The `^\(` line-start anchor matches only the executable subshell line, not this prose / the grep below.
   ```bash
   if grep -qE '^\([^)]*/c/Users/[^/]+/\.cargo/' .claude/skills/mcp-restart/SKILL.md; then
     echo "FAIL: Step 1 build line has a hardcoded per-user cargo path (Issue #16820)"; exit 1
   fi
   grep -qE '^\([^)]*cargo\.exe build .* -p narshamcp' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: portable cargo.exe build line missing (#16820)"; exit 1; }
   ```
12. **Deploy-into-`$BINARY` wiring present** (Issue #17089): the deploy helper must exist + answer `--help`, and SKILL.md must wire the profile-driven build (`$CARGO_FLAG`), the A1.5 `deploy`, and the A4.5b hash `verify` — so the build/deploy-path fix cannot silently regress to building a path the daemon never reads.
   ```bash
   python .claude/skills/mcp-restart/scripts/deploy_built_binary.py --help >/dev/null 2>&1 || { echo "FAIL: deploy_built_binary.py --help failed (#17089)"; exit 1; }
   grep -qE '^\([^)]*cargo\.exe build \$CARGO_FLAG -p narshamcp' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: Step 1 must build the deploy profile (\$CARGO_FLAG), not a hardcoded flag (#17089)"; exit 1; }
   grep -qF 'deploy_built_binary.py deploy' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: deploy invocation missing (#17089)"; exit 1; }
   grep -qF 'deploy_built_binary.py verify' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: hash verify missing (#17089)"; exit 1; }
   # Every --build spawn path MUST deploy first, else it reproduces #17089. There are three:
   # A1.5 (Path A daemon), B1.5 (Path B STDIO), Step 3 (switch-daemon migration). The
   # switch-daemon path was blind in the first cut (caught by the #17089 completeness audit).
   DEP_COUNT=$(grep -cF 'deploy_built_binary.py deploy' .claude/skills/mcp-restart/SKILL.md)
   [ "$DEP_COUNT" -ge 3 ] || { echo "FAIL: deploy must run on ALL --build spawn paths (A1.5/B1.5/Step 3); found $DEP_COUNT/3 — a spawn path is blind (#17089)"; exit 1; }
   ```

13. **Deploy-path lock-holder fallback present + spares `connect` bridges** (Issue #17129): the deploy helper must kill non-`connect` lock-holders running from `$BINARY` so an orphaned/unregistered gui-mode daemon can't keep `--build` deploying onto stale bytes — and it MUST exclude `connect` bridges (#11102). This lint fails if the kill, its `ExecutablePath`-scoping, or the `connect` exclusion is dropped.
   ```bash
   H=.claude/skills/mcp-restart/scripts/deploy_built_binary.py
   grep -qE '_select_lock_holders' "$H" || { echo "FAIL: lock-holder selection missing (#17129)"; exit 1; }
   grep -qE '_kill_lock_holders|taskkill' "$H" || { echo "FAIL: lock-holder kill missing (#17129)"; exit 1; }
   grep -qE 'ExecutablePath' "$H" || { echo "FAIL: lock-holder must scope by ExecutablePath==deploy path, not image name (#11102/#17129)"; exit 1; }
   grep -qE '_CONNECT_TOKEN' "$H" || { echo "FAIL: connect-bridge exclusion (_CONNECT_TOKEN) missing — would violate #11102"; exit 1; }
   grep -qE 'lock-holder fallback \(Issue #17129\)|Deploy-path lock-holder fallback \(Issue #17129\)' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: A1.5 prose must document the #17129 lock-holder fallback"; exit 1; }
   ```

14. **Step 1 build line actually PASSES cargo_build_guard** (Issue #17468): checks #11/#12 are grep-only and stayed green while #17070's `.exe` stripping silently blocked `--build` at runtime. This check EXECUTES the guard on the skill's exact Step 1 build line and asserts `allow`, and on the same line WITHOUT the `# narsha-build-guard-ok:` token asserts `deny` — so the override (not a guard regression) is what permits it, and #17070's evasion detection stays intact. This is the regression-proof the grep-only checks lacked.
   ```bash
   python - <<'PY'
import re, sys
sys.path.insert(0, ".claude/hooks/checks")
from cargo_build_guard import check_cargo_build
line = next((l for l in open(".claude/skills/mcp-restart/SKILL.md", encoding="utf-8")
             if re.match(r"^\(.*cargo\.exe build .* -p narshamcp", l)), None)
assert line, "FAIL: Step 1 cargo.exe build line not found"
tokened = line.strip()
untokened = re.sub(r"\s*#\s*narsha-build-guard-ok:.*$", "", tokened)
assert check_cargo_build(tokened)[0] == "allow", "FAIL: Step 1 build line is BLOCKED by cargo_build_guard (#17468 regression)"
assert check_cargo_build(untokened)[0] == "deny", "FAIL: un-tokened narshamcp build must still deny (#17070 evasion)"
print("ok: Step 1 allowed via override; un-tokened still denied (#17468)")
PY
   ```

15. **Build-output prebuild-swap wired BEFORE the build** (Issue #17812): when `command` points at `target/<profile>/` (the build output), the live daemon locks the file cargo must overwrite and a post-kill respawn re-locks it — so the running image MUST be renamed aside *before* Step 1's cargo build. This lint fails if the `prebuild-swap` subcommand is dropped, or if the Step 1 call is reordered to run at/after the `cargo.exe build` line (which would reintroduce the bug while the grep-only check stayed green).
   ```bash
   H=.claude/skills/mcp-restart/scripts/deploy_built_binary.py
   grep -qF "prebuild-swap" "$H" || { echo "FAIL: prebuild-swap subcommand missing from helper (#17812)"; exit 1; }
   python "$H" prebuild-swap --help >/dev/null 2>&1 || { echo "FAIL: prebuild-swap not registered / --help failed (#17812)"; exit 1; }
   grep -qF 'deploy_built_binary.py prebuild-swap --command-path "$BINARY"' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: Step 1 prebuild-swap wiring missing (#17812)"; exit 1; }
   grep -qF 'Build-output deploy path (Issue #17812)' .claude/skills/mcp-restart/SKILL.md || { echo "FAIL: A1.5 prose must document the #17812 build-output case"; exit 1; }
   # ORDERING: the Step 1 prebuild-swap call must precede the `cargo.exe build` line (line number).
   SWAP_L=$(grep -nF 'deploy_built_binary.py prebuild-swap --command-path "$BINARY"' .claude/skills/mcp-restart/SKILL.md | head -1 | cut -d: -f1)
   BUILD_L=$(grep -nE '^\([^)]*cargo\.exe build \$CARGO_FLAG -p narshamcp' .claude/skills/mcp-restart/SKILL.md | head -1 | cut -d: -f1)
   [ -n "$SWAP_L" ] && [ -n "$BUILD_L" ] && [ "$SWAP_L" -lt "$BUILD_L" ] || { echo "FAIL: prebuild-swap (line $SWAP_L) must come BEFORE the cargo build (line $BUILD_L) — reorder reintroduces #17812"; exit 1; }
   ```

**Output**: PASS / FAIL with per-check results. Does not execute restart workflow.
