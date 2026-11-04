---
name: vscode-restart
description: "Restart all VSCode windows from inside a Claude session: spawn a detached OS-owned process that kills every VSCode process and relaunches, surviving this session's death; restores windows via window.restoreWindows. Cross-platform — Windows (WMI Win32_Process.Create) + macOS/Linux (python start_new_session detach). Supports --dry-run, --grace, --self-test."
compatibility: "Windows / macOS / Linux + VSCode; relies on window.restoreWindows (default 'all')"
allowed-tools: [Bash, Read]
argument-hint: |
  /vscode-restart                # warn, then detached kill-all + relaunch
  /vscode-restart --dry-run      # preview discovery + planned actions, no kill
  /vscode-restart --grace 6      # override pre-kill grace seconds (default 4)
  /vscode-restart --self-test    # validate skill integrity, no side effects
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
audience: end-user
---

# VSCode Restart

Full-app restart of **all** VSCode windows, triggered from inside a Claude Code session. **Issue**: #16509

> **CRITICAL**: When loaded, execute the workflow automatically. Parse `$ARGUMENTS`, then run the matching path.

## Why this needs a detached process

Claude Code runs **inside the VSCode extension host**, so killing VSCode kills this
session — a naive same-shell kill→relaunch dies at the kill step. The fix: spawn the
kill+relaunch worker **detached, outside VSCode's tree** (Windows: WMI `Win32_Process.Create`
→ `WmiPrvSE`; macOS/Linux: `python3 Popen(start_new_session=True)` → own session). The
worker — [.ps1](scripts/vscode-restart-detached.ps1) / [.sh](scripts/vscode-restart-detached.sh) —
does the kill+relaunch; **the skill body must never kill VSCode itself**. Detach mechanics +
per-platform rationale: [REFERENCE.md](REFERENCE.md).

## Workflow

### Step 0: Parse arguments
Parse `$ARGUMENTS`: `--dry-run`, `--grace <N>` (default 4), `--self-test`.
Bind the parsed `--grace` value to `$GRACE` (used in the Restart spawn below); default 4.
If `--self-test` → **Self-Test Mode**. If `--dry-run` → **Dry-Run**. Else → **Restart**.

### Step 1: Platform detect
Detect the OS and select the worker. Unknown platforms stop cleanly (do nothing):
```bash
case "$(uname -s)" in
  *NT*|*MINGW*|*MSYS*|*CYGWIN*) PLATFORM=windows ;;   # WMI worker (.ps1)
  Darwin|Linux)                 PLATFORM=posix   ;;   # start_new_session worker (.sh)
  *) echo "❌ /vscode-restart: unsupported platform '$(uname -s)'. Use the Command Palette → 'Developer: Reload Window', or restart VSCode manually."; exit 0 ;;
esac
```

### Dry-Run (`--dry-run`)
Run the worker in the **foreground** in dry-run mode — it discovers the VSCode launcher
path, the real `window.restoreWindows` setting, and (when restore is `none`/`one`) the
open folders, then prints the launch plan as JSON **without killing anything**:
```bash
if [ "$PLATFORM" = "windows" ]; then
  powershell -NoProfile -ExecutionPolicy Bypass -Command \
    "& (Resolve-Path '.claude/skills/vscode-restart/scripts/vscode-restart-detached.ps1').Path -DryRun"
else
  bash .claude/skills/vscode-restart/scripts/vscode-restart-detached.sh --dry-run
fi
```
Report the plan to the user (`codePath`, `restoreMode`, `launchMode`, `folders`). Stop.

### Restart (default)
**1. Warn the user (AC #5).** Print verbatim, then proceed (explicit invocation = consent):
```text
⚠️  /vscode-restart will close ALL VSCode windows — INCLUDING this Claude session.
    Every VSCode process is terminated (incl. other VSCode installs / Code-OSS, if any).
    Unsaved changes in any window will be LOST unless saved first.
    Windows will reopen automatically (window.restoreWindows). Starting in a few seconds…
```

**2. Spawn the detached worker.** It survives this session dying because it is detached
outside VSCode's tree (Windows: reparented to `WmiPrvSE`; macOS/Linux: its own session via
`start_new_session`). `$GRACE` is the parsed `--grace` value (default 4). The body NEVER
kills VSCode — only the worker does:
```bash
GRACE="${GRACE:-4}"   # the value bound from --grace in Step 0; default 4
case "$GRACE" in (*[!0-9]*|"") echo "❌ --grace must be a non-negative integer"; exit 1 ;; esac
if [ "$PLATFORM" = "windows" ]; then
  # Windows: WMI Win32_Process.Create reparents the worker to WmiPrvSE.
  # Resolve-Path yields an absolute Windows path (robust to spaces).
  powershell -NoProfile -Command "
    \$s = (Resolve-Path '.claude/skills/vscode-restart/scripts/vscode-restart-detached.ps1').Path;
    \$cmd = 'powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"' + \$s + '\" -GraceSeconds $GRACE';
    \$r = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{ CommandLine = \$cmd };
    Write-Output ('spawn ReturnValue=' + \$r.ReturnValue + ' ProcessId=' + \$r.ProcessId)
  "
else
  # macOS/Linux: detach via python start_new_session (os.setsid) — one path for both,
  # and macOS has no `setsid` binary. The worker becomes its own session leader.
  python3 - "$GRACE" <<'PY'
import os, subprocess, sys
worker = os.path.abspath(".claude/skills/vscode-restart/scripts/vscode-restart-detached.sh")
p = subprocess.Popen(
    ["bash", worker, "--grace-seconds", sys.argv[1]],
    start_new_session=True,
    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
)
print("spawn ReturnValue=0 ProcessId=%d" % p.pid)
PY
fi
```
`ReturnValue=0` ⇒ the detached worker started. Any other value (Windows) ⇒ spawn failed (report it; nothing was killed).

**3. Report and stop.** Do NOT run any further commands — the kill is imminent:
```text
✅ Detached restart worker started (PID <id>). VSCode (and this session) will close in ~<GRACE>s, then all windows reopen.
   Worker log: %TEMP%\narsha-vscode-restart.log (Windows) | $TMPDIR/narsha-vscode-restart.log (macOS/Linux)
```

## Output Format
- Dry-Run: the plan JSON + a one-line human summary.
- Restart: the warning block, the `spawn ReturnValue=…` line, and the final report.

## Error Recovery

| Symptom | Cause | Fix |
|---------|-------|-----|
| `spawn ReturnValue` ≠ 0 (Windows) | WMI blocked / no rights | Run VSCode normally; restart manually. Check the value against `Win32_Process.Create` return codes. |
| VSCode did not reopen | `window.restoreWindows` is `none`/`one` and capture failed | Set `"window.restoreWindows": "all"` in settings.json, or reopen folders manually. See [REFERENCE.md](REFERENCE.md). |
| `launcher could not be resolved` (worker log) | VSCode not running and not on PATH / not in a standard install path | Install VSCode to a standard location or put `code` on PATH (macOS: Command Palette → "Shell Command: Install 'code' command"). Worker aborts WITHOUT killing. |
| Session died but VSCode stayed open | grace too short / worker errored | Check the worker log (`%TEMP%\narsha-vscode-restart.log` on Windows, `$TMPDIR/narsha-vscode-restart.log` on macOS/Linux); re-run `/vscode-restart --grace 8`. |

## Safety
- The skill body never kills VSCode — only the detached worker does (so a half-completed restart is impossible from the body).
- `--dry-run` is side-effect-free and is the safe way to inspect what would happen.
- Full-app intent: the worker kills all VSCode processes (Windows: `Code.exe` by image name; macOS/Linux: by app-path `pgrep -f`) and relies on `restoreWindows` to bring every window back. This is intentional (not the PID-scoped bridge-aware kill that `/mcp-restart` uses).

## Self-Test Mode
`/vscode-restart --self-test` validates skill integrity (no restart, no side effects). Checks: (1) structure — SKILL.md/REFERENCE.md/both workers (`.ps1` + `.sh`) exist; (2) frontmatter required fields (`name`, `description`, `allowed-tools`); (3) a `vscode-restart` rule in `routing.json`; (4) frontmatter `metadata.version` is valid semver; (5) **detached-spawn guard** — the Windows body spawns via WMI and the POSIX body via `start_new_session`, and **neither** body kills VSCode itself (only the workers do); (6) both workers parse (`powershell` parser + `bash -n`); (7) the OS-aware plan fixture test passes; (8) optional manual real-process E2E (`verify_e2e_decoy.ps1` on Windows / `verify_e2e_decoy.sh` on macOS/Linux). Exact commands: [REFERENCE.md § Self-Test checks](REFERENCE.md#self-test-checks-exact-commands). Output: PASS/FAIL per check; never runs the restart workflow.
