# /vscode-restart — Reference

**Issue**: [#16509](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/16509) (Windows v1.0) · [#16596](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/16596) (macOS/Linux v1.1) · version: see SKILL.md frontmatter `metadata.version`

Cold-context detail for the `/vscode-restart` skill. The hot path lives in [SKILL.md](SKILL.md).
The sections below describe the Windows worker; the **macOS/Linux** equivalents are in
[§ Platform support](#platform-support).

## Process model — why WMI, not the same shell

Claude Code runs inside the VSCode **extension host** process. The Bash/PowerShell
tools Claude invokes are children of that host. When VSCode is killed, the whole
tree (including those children) dies. So this naive sequence **fails**:

```text
session shell:  Stop-Process Code  →  (shell itself is killed here)  →  Start-Process Code  ✗ never runs
```

The fix is to hand the kill+relaunch work to a process that is **not** in VSCode's
tree. `Invoke-CimMethod -ClassName Win32_Process -MethodName Create` asks the WMI
provider service (`WmiPrvSE.exe`) to create the process, so the new process is
parented to `WmiPrvSE` — an OS service unaffected by VSCode dying:

```text
session shell:  WMI Create(worker)  →  returns immediately (ReturnValue=0)
WmiPrvSE child: sleep grace  →  Stop-Process Code  →  poll until gone  →  Start-Process Code  ✓
```

The grace sleep (default 4s, `--grace N`) gives the triggering session time to flush
its final response before VSCode is torn down.

### Rejected alternative: bash `disown`
Backgrounding the worker in bash and `disown`-ing it does **not** survive on Windows:
`disown` only removes the job from bash's job table; the OS process is still a
descendant of the dying VSCode tree. WMI reparenting is what actually survives.
(`/mcp-restart` uses a bash-background helper because there the session does *not*
die — only the daemon is replaced. Different problem.)

## Kill scope — image-name, by design

The worker uses `Stop-Process -Name Code -Force` (all `Code.exe`). This is the
**opposite** of `/mcp-restart`'s PID-scoped, bridge-aware kill (#11102), and it is
intentional: the goal is a full-app restart of every window, and `window.restoreWindows`
reopens them all. There is no global hook/CI ban on image-name kills — the only such
guard is `/mcp-restart`'s own self-test, scoped to its own SKILL.md.

## window.restoreWindows handling (AC #4)

The worker reads `window.restoreWindows` from `%APPDATA%\Code\User\settings.json`
(regex parse, so JSONC comments / trailing commas don't break it):

| restoreWindows value | launchMode | behavior |
|----------------------|------------|----------|
| `all`, `folders`, `preserve`, unset | `restore` | relaunch with **no args**; VSCode restores all windows |
| `none`, `one` + folders captured | `explicit` | relaunch **one window per captured folder** |
| `none`, `one` + capture failed | `warn` | relaunch a single window; log a warning recommending `"window.restoreWindows": "all"` |

### Open-folder capture is best-effort
For `none`/`one`, the worker tries to read open folders from
`%APPDATA%\Code\User\globalStorage\storage.json` (`windowsState.openedWindows[].folder`
/ `.folderUri` / `.workspace.configPath`, plus `lastActiveWindow`). `file:///c%3A/…`
URIs are decoded to Windows paths. This format is **version-dependent** and may change
between VSCode releases; on any parse failure the worker falls back to `warn` mode.
The robust user-facing fix is simply `"window.restoreWindows": "all"` (the default).

Explicit relaunch passes **one folder per `Start-Process` call** with the path quoted as
a single argument — this sidesteps the `Start-Process -ArgumentList` space-mangling
pitfall (#11632) that bit `/mcp-restart`.

## Worker parameters (scripts/vscode-restart-detached.ps1)

| param | default | purpose |
|-------|---------|---------|
| `-GraceSeconds` | 4 | sleep before kill (abort window) |
| `-DryRun` | off | discover + print plan JSON, no kill/launch, no sleep |
| `-SettingsPath` | `%APPDATA%\Code\User\settings.json` | override for testing |
| `-StoragePath` | `%APPDATA%\…\globalStorage\storage.json` | override for testing |
| `-CodePath` | auto-discovered | override the Code.exe path |
| `-LogFile` | `%TEMP%\narsha-vscode-restart.log` | evidence log (`""` disables) |

Exit codes: `0` = ok (success or dry-run) / `3` = Code.exe path unresolved (aborts WITHOUT killing).

### Plan JSON schema (`--dry-run` output)
```json
{"schema":"vscode-restart-plan/1","dryRun":true,"codePath":"…\\Code.exe","codeFound":true,
 "restoreMode":"all","launchMode":"restore","folders":[],"graceSeconds":4}
```

## Verifying detached survival
`%TEMP%\narsha-vscode-restart.log` records `START`/`KILL`/`POLL`/`LAUNCH`/`DONE` with
timestamps. After a real `/vscode-restart`, the `LAUNCH`/`DONE` lines are written
*after* the session that spawned the worker is already dead — proof the worker ran
independently.

### Real-process E2E proof (`scripts/verify_e2e_decoy.ps1`)
A manual, opt-in verifier (not CI — it spawns/kills real harmless decoys) proves the
destructive path the dry-run fixture test cannot, without touching a real VSCode session:
- **A** a WMI-spawned process reparents to `WmiPrvSE`, outlives its ancestor being
  `taskkill /T`'d, and runs to completion afterward.
- **B** the worker (via the `-ProcessName` seam) kills live decoys → polls to zero →
  relaunches, with the folder arg intact incl. **spaces** (#11632); plus the
  production-default no-arg `restore` arm.
- **C** a WMI-spawned child is **not** a member of the creator's Win32 Job object →
  immune to `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (the realistic way an Electron parent
  reaps children). This is *why* WMI detach survives VSCode dying, not just a tree-kill.
- **D** the **real** worker, WMI-detached, survives its ancestor dying mid-grace and
  still completes the kill→relaunch.

The one thing inherently un-self-testable is the literal `Code.exe` kill while *this*
session dies (it would end the test) — both halves are proven separately above, and the
issue reporter confirmed the full round-trip empirically in a live session.

## Self-Test checks (exact commands)
`/vscode-restart --self-test` runs these (no side effects):
- **5 — detached-spawn guard** (the core invariant, both platforms): each body spawns its
  worker detached and **neither** body kills VSCode itself.
  ```bash
  # Windows: body spawns via WMI, never kills Code.exe
  grep -q "Invoke-CimMethod" .claude/skills/vscode-restart/SKILL.md && \
  grep -q "Win32_Process"   .claude/skills/vscode-restart/SKILL.md || { echo "FAIL: body must spawn via WMI Win32_Process.Create"; exit 1; }
  if grep -qE '^[[:space:]]*Stop-Process -Name Code' .claude/skills/vscode-restart/SKILL.md; then
    echo "FAIL: SKILL.md body must NOT kill Code.exe — only the detached worker may"; exit 1
  fi
  grep -qE '^[[:space:]]*Stop-Process -Name (Code|\$ProcessName)' .claude/skills/vscode-restart/scripts/vscode-restart-detached.ps1 || { echo "FAIL: .ps1 worker must contain the Stop-Process kill"; exit 1; }
  # POSIX: body spawns via start_new_session + references the .sh worker, never kills VSCode itself
  grep -q "start_new_session"          .claude/skills/vscode-restart/SKILL.md || { echo "FAIL: POSIX body must spawn the worker via start_new_session"; exit 1; }
  grep -q "vscode-restart-detached.sh" .claude/skills/vscode-restart/SKILL.md || { echo "FAIL: POSIX body must reference the bash worker"; exit 1; }
  if grep -qiE '^[[:space:]]*(pkill|osascript .*quit)' .claude/skills/vscode-restart/SKILL.md; then
    echo "FAIL: SKILL.md body must NOT kill VSCode (pkill/osascript-quit) — only the detached worker may"; exit 1
  fi
  grep -q "kill_targets" .claude/skills/vscode-restart/scripts/vscode-restart-detached.sh || { echo "FAIL: .sh worker must contain the kill"; exit 1; }
  ```
- **6 — both workers parse**:
  - Windows: `powershell -NoProfile -Command "[void][System.Management.Automation.Language.Parser]::ParseFile((Resolve-Path '.claude/skills/vscode-restart/scripts/vscode-restart-detached.ps1').Path,[ref]\$null,[ref]\$null)"`
  - POSIX: `bash -n .claude/skills/vscode-restart/scripts/vscode-restart-detached.sh`
- **7 — plan fixture test** (OS-aware: drives the `.ps1` on Windows, the `.sh` on macOS/Linux): `python .claude/skills/vscode-restart/scripts/test_vscode_restart_plan.py`
- **8 — manual real-process E2E** (opt-in): Windows `powershell -File .claude/skills/vscode-restart/scripts/verify_e2e_decoy.ps1`; macOS/Linux `bash .claude/skills/vscode-restart/scripts/verify_e2e_decoy.sh` → expects all checks PASS (see § Real-process E2E proof above).

## Platform support
- **Windows**: supported (v1.0) — WMI worker `vscode-restart-detached.ps1`.
- **macOS / Linux**: supported (v1.1, Issue #16596) — bash worker `vscode-restart-detached.sh`.
- **Single window reload**: out of scope — use the Command Palette → "Developer: Reload Window".

### macOS/Linux worker (`scripts/vscode-restart-detached.sh`)
POSIX counterpart of the `.ps1` worker. Same plan-decision logic, same `vscode-restart-plan/1`
dry-run JSON, same `restore`/`explicit`/`warn` launch modes. What differs from Windows:

- **Detach primitive**: the SKILL.md body spawns the worker via `python3 … Popen(start_new_session=True)`
  (`os.setsid` — the worker becomes its own session/process-group leader). This is the repo's
  established detach convention (Issue #16307; `MCP/core/indexer/commandlet.py`,
  `tests/unit/test_acceptance_proc_tree_kill.py`). It escapes the spawner's process group the
  way WMI escapes VSCode's tree + Job. **macOS ships no `setsid` binary**, so the python shim is
  used uniformly on both macOS and Linux instead of a `setsid`/`nohup` one-liner.
- **Process selector + kill**: `pgrep -f` / `kill` against a match pattern (default per-OS:
  macOS `Visual Studio Code.app/Contents/`, Linux the resolved install dir). The worker
  **never kills itself** — `target_pids()` excludes its own pid (`$$`) and any process whose
  command contains `vscode-restart-detached.sh`, so a pattern that appears in the worker's own
  argv (e.g. the test `--match-pattern`) is safe. `--match-pattern` is the test seam (the POSIX
  analog of `-ProcessName`) that lets the decoy E2E target a harmless process.
- **Config paths**: macOS `~/Library/Application Support/Code/User/{settings.json,globalStorage/storage.json}`;
  Linux `${XDG_CONFIG_HOME:-~/.config}/Code/User/…`. JSON parse + `file://` URI decode use `python3`
  (jq is not guaranteed; python3 is a hard repo dependency).
- **Launcher / relaunch**: `code` CLI when on PATH, else macOS `/Applications/Visual Studio Code.app`
  (relaunch via `open -a "Visual Studio Code"`, which respects `restoreWindows`) / Linux standard
  install paths. Explicit reopen passes **one folder per call** (the #11632 quoting fix).
- **Long-option flags** (vs the `.ps1` `-PascalCase`): `--grace-seconds --dry-run --settings-path
  --storage-path --code-path --match-pattern --log-file`. Exit codes match: `0` ok / `2` bad args /
  `3` launcher unresolved (aborts WITHOUT killing). Log: `$TMPDIR/narsha-vscode-restart.log`.

### POSIX real-process E2E (`scripts/verify_e2e_decoy.sh`)
The bash twin of `verify_e2e_decoy.ps1` — same A–D structure, decoys are `sleep`-based with a
unique argv0 token (no real VSCode touched):
- **A** a `start_new_session` process is its own session leader (pgid==pid), outlives its
  ancestor's process group being killed, and runs to completion afterward.
- **B** the worker (via `--match-pattern`) kills live decoys → polls to zero → relaunches, folder
  arg intact incl. **spaces** (#11632), plus the production-default no-arg `restore` arm.
- **C** a `start_new_session` child is **not** in the caller's process group → immune to
  `kill -- -<pgid>` teardown (the POSIX analog of Win32 Job `KILL_ON_JOB_CLOSE`). This is *why*
  the detach survives VSCode dying.
- **D** the **real** worker, detached, survives its ancestor dying mid-grace and still completes
  the kill→relaunch.

Verified on this repo's Mac mini (Darwin/arm64): `15/15 checks passed`. Linux is verified by the
same portable script on a Linux host / CI (it cannot run on the macOS host).
