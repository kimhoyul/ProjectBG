---
name: ue-editor-launcher
model: sonnet
context: fork
agent: general-purpose
description: "Purpose: Launch Unreal Editor with the project configured in .env. Reads UECODEGEN_ENGINE_DIR and UECODEGEN_PROJECT_DIR from .env file and starts the editor. Monitors for modal dialogs that block Remote Control startup (Issue"
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
metadata:
  version: "1.2.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Unreal Editor Launcher Skill

This Skill launches Unreal Editor using project/engine paths from `.env` configuration.
Includes **dialog-aware polling** (Issue #5418) to detect and handle modal dialogs that block Remote Control.

## When to Use This Skill

Activate this Skill when the user requests:

- "에디터 열어줘" / "Open the editor"
- "언리얼 실행해줘" / "Launch Unreal"
- "에디터 시작" / "Start the editor"
- "UE 열어" / "Open UE"
- "프로젝트 열어줘" / "Open project"

## Configuration

### Required .env Variables

```bash
# Engine directory (where UE is installed)
UECODEGEN_ENGINE_DIR=C:/Program Files/Epic Games/UE_5.7

# Project directory (where .uproject file is located)
UECODEGEN_PROJECT_DIR=E:/last-contact-prod
```

## Launch Process

### Step 1: Read Configuration

```bash
# Read .env file from project root
cat .env | grep -E "UECODEGEN_ENGINE_DIR|UECODEGEN_PROJECT_DIR"
```

### Step 2: Find .uproject File

```bash
# List .uproject files in project directory
ls "$UECODEGEN_PROJECT_DIR"/*.uproject
```

### Step 2.5: Build Preflight Gate (Issue #19884)

Build the editor target headlessly BEFORE launch so UE's missing-modules rebuild popup
never appears (the popup only fires when `IncompatibleFiles.Num() > 0` — i.e. modules are
stale). This gate replaces the "launch → wait for rebuild popup → dismiss/hang" failure mode.

```python
ToolSearch("select:mcp__narshamcp__ue_build_pipeline")

# Lock the exact editor target (don't hard-code). Pass project_root EXPLICITLY — without
# it the daemon falls back to its default project and could build the WRONG project on a
# shared/daemon host.
targets = ue_build_pipeline(operation="list_targets", project_root="$UECODEGEN_PROJECT_DIR")
# → targets.recommended_editor_target

# (Incremental) build of the editor target. skip_if_current=true skips the multi-second UBT
# spawn when modules are already up to date (mtime vs Source/). configuration + platform MUST
# match what the editor launches (Development editor / Win64), else the built DLL is rejected
# on a BuildId/config mismatch.
result = ue_build_pipeline(operation="build_editor", project_root="$UECODEGEN_PROJECT_DIR",
                           configuration="Development", platform="Win64",
                           skip_if_current=True)
```

**GATE — only continue to Step 3 when `result.success == true`:**
- `success:true` (real build) OR `success:true, skipped:true` (modules already current) → launch.
- `success:false` → report `result.errors` / `result.summary` and **ABORT** — do NOT launch a
  project whose editor modules failed to compile (launching stale modules is what triggers the popup).

### Step 3: Launch Editor

```bash
# Windows (with -UNATTENDED to suppress modal dialogs like auto-save recovery)
# ONLY reached when the Step 2.5 build gate returned success:true.
start "" "$UECODEGEN_ENGINE_DIR/Engine/Binaries/Win64/UnrealEditor.exe" "$UECODEGEN_PROJECT_DIR/<ProjectName>.uproject" -UNATTENDED
```

> **Note**: The `-UNATTENDED` flag automatically dismisses many modal dialogs (shader compilation prompts, etc.) but some dialogs like "Restore Packages" or "Package Recovery" may still appear.

## Complete Launch Workflow

### Automated Launch

```bash
# 1. Read paths from .env
ENGINE_DIR=$(grep UECODEGEN_ENGINE_DIR .env | cut -d'=' -f2 | tr -d '\r')
PROJECT_DIR=$(grep UECODEGEN_PROJECT_DIR .env | cut -d'=' -f2 | tr -d '\r')

# 2. Find .uproject file
UPROJECT=$(ls "$PROJECT_DIR"/*.uproject 2>/dev/null | head -1)

# 2.5. Build preflight gate (Issue #19884): run ue_build_pipeline(build_editor,
#      project_root="$PROJECT_DIR", configuration=Development, platform=Win64,
#      skip_if_current=True) and ONLY proceed when success==true (see Step 2.5).

# 3. Launch editor (background, -UNATTENDED suppresses modal dialogs) — gated on the build
start "" "$ENGINE_DIR/Engine/Binaries/Win64/UnrealEditor.exe" "$UPROJECT" -UNATTENDED
```

### Manual Launch (if automated fails)

```bash
# Direct launch with known paths (-UNATTENDED skips auto-save recovery, shader prompts, etc.)
start "" "C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe" "E:/last-contact-prod/LastContact.uproject" -UNATTENDED
```

## Post-Launch Verification (Issue #5418: Dialog-Aware Polling)

**IMPORTANT**: Use a polling loop with dialog detection, NOT a one-shot curl check.
The editor takes 30-90 seconds to load, and modal dialogs can block Remote Control indefinitely.

### Step 4: Poll for Readiness (max 120s, every 10s)

**Tier 1 — MCP ping (preferred; on a ping *failure* it runs timeout-bounded closed-loop dialog dismissal)**:

```python
# Load the MCP tool first
ToolSearch("select:mcp__narshamcp__ue_editor_automation")

# Call ping — only on a FAILING ping does the MCP layer detect modal dialogs and
# attempt dismissal (BM_CLICK to a native button, else timeout-bounded WM_CLOSE).
# A successful dismiss result means the window closure was confirmed. Always
# re-ping to verify RC readiness. Windows-only: on mac/Linux the
# response carries dialog_detection_supported:false and never dismisses.
result = ue_editor_automation(operation="ping")
```

Parse the JSON response in each poll iteration:

| Response | Meaning | Action |
|----------|---------|--------|
| `success: true` | Editor ready, RC responding | Report success, exit loop |
| `success: false` +<br/>`editor_diagnostics.likely_blocked_by_dialog: true` | Modal dialog blocking RC | Report dialog name to user.<br/>MCP makes a timeout-bounded dismiss attempt and reports success only after confirmed closure.<br/>Wait 5s and **re-ping to verify RC readiness**.<br/>If dismissal is unsuccessful or the dialog persists, tell user:<br/>"Please close '{dialog_title}' manually." |
| `success: false` +<br/>`editor_diagnostics.editor_running: true` | Editor still loading | Report: "Editor loading, waiting...<br/>(attempt N/12)". Continue polling. |
| `success: false` +<br/>`editor_diagnostics.editor_running: false` | Editor not running / crashed | Report: "Editor process not found.<br/>May have crashed — check Saved/Logs/".<br/>Abort. |

**Tier 2 — Fallback (if MCP tool call fails, e.g., MCP server not running)**:

```bash
# Check if editor process is alive
tasklist /FI "IMAGENAME eq UnrealEditor.exe" /FO CSV /NH 2>/dev/null

# Check MainWindowTitle for dialog indicators (PowerShell)
powershell -Command "
  Get-Process UnrealEditor -ErrorAction SilentlyContinue | ForEach-Object {
    \$t = \$_.MainWindowTitle
    if (\$t -match 'Recovery|Restore|Save|Update|Error|Warning|Crash') {
      Write-Output \"DIALOG:\$t\"
    } elseif (\$t -match 'Unreal Editor') {
      Write-Output \"LOADING:\$t\"
    } else {
      Write-Output \"TITLE:\$t\"
    }
  }
"

# Try RC endpoint
curl -s --max-time 3 http://localhost:30010/remote/info
```

### Step 5: Report Result

After the polling loop completes, report one of:

- **SUCCESS**: "Editor ready! Remote Control responding on port 30010."
- **DIALOG_BLOCKED**: "Editor blocked by modal dialog '{title}'. Please close it manually."
- **TIMEOUT**: "Editor did not become ready after 120s. Check Saved/Logs/ for errors."
- **CRASHED**: "Editor process exited unexpectedly. Check Saved/Logs/ for crash details."

### Known Modal Dialogs (Issue #5418)

| Dialog | Trigger | Timeout-bounded dismiss |
|--------|---------|-------------|
| Restore Packages / Package Recovery | Package version mismatch | Attempts "Skip"/"Don't Restore"; success means window closure was confirmed |
| Crash Recovery | Previous crash | Attempts "Skip"/"Don't Restore"; success means window closure was confirmed |
| Save Content | Unsaved changes | Attempts "Don't Save"; success means window closure was confirmed |
| Update Project | Engine version upgrade | Attempts "Skip"/"No"; success means window closure was confirmed |
| Source Control Login | SCC configuration | No (user decision needed) |
| Shader Compilation | First load | Wait (auto-completes) |

> Dismissal runs **only on a ping failure**, finds a safe native Win32 button and sends timeout-bounded `BM_CLICK` (otherwise `WM_CLOSE` where allowed). Slate-rendered dialogs may still ignore the action, but `success` is reported only after the target window is confirmed closed. Re-ping to confirm RC readiness; close manually when the result is unsuccessful. Detection/dismiss is **Windows-only** (`dialog_detection_supported: false` elsewhere).

## Common Failures

| Error | Cause | Solution |
|-------|-------|----------|
| `build_editor` preflight returns `success:false` (Issue #19884) | Editor target fails to compile before launch | Report `result.errors` / `result.summary`; fix the compile error and re-run. **Do not launch** — a project with stale/failed editor modules is exactly what triggers the missing-modules rebuild popup |
| `UnrealEditor.exe not found` | Wrong `UECODEGEN_ENGINE_DIR` in `.env` | Verify engine path, check `UnrealEditor.exe` exists |
| `.uproject file not found` | Wrong `UECODEGEN_PROJECT_DIR` in `.env` | Verify project path has `.uproject` file |
| Editor crashes within 30s | Corrupted project or plugin incompatibility | Check `Saved/Logs/`, disable problematic plugins |
| RC not responding (port 30010) | Editor still loading or RC plugin disabled | Wait 60-90s, enable RemoteControl plugin |
| `.env` parse failure | Windows `\r\n` line endings | Use `tr -d '\r'` when reading values |
| Multiple Editor instances | Already running editor process | Check `tasklist /FI "IMAGENAME eq UnrealEditor.exe"`, close extras |
| Modal dialog blocking RC | Post-crash recovery or package version mismatch | Failing MCP ping makes a timeout-bounded dismiss attempt and reports success only after confirmed closure; re-ping for RC readiness and close the dialog manually if it persists |

## Auto-Enable Remote Control Plugin

If Remote Control plugins are missing from the project, add them automatically:

```python
import json

# Update with your project path
uproject_path = 'E:/last-contact-prod/LastContact.uproject'

with open(uproject_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

plugin_names = [p['Name'] for p in data.get('Plugins', [])]

# RemoteControl only. It carries the WebRemoteControl module, which is the HTTP API
# on port 30010 that NarshaMCP talks to.
#
# Do NOT add RemoteControlWebInterface here (Issue #28398). That is the separate
# browser control panel: NarshaMCP holds zero references to it, it is default-OFF in
# UE 5.8, it ships a 508 MB WebApp with a bundled Node runtime, and our own CI strips
# it from test projects because it blocks editor startup.
if 'RemoteControl' not in plugin_names:
    data['Plugins'].append({'Name': 'RemoteControl', 'Enabled': True})
    print('Added RemoteControl plugin')

with open(uproject_path, 'w', encoding='utf-8') as f:
    json.dump(data, f, indent=2)
print('Restart editor to apply changes')
```

**After enabling**: Restart Unreal Editor for plugins to load

## Supported Projects

The skill supports any project configured in .env:

- `UECODEGEN_PROJECT_DIR` - Primary project
- `LYRA_PROJECT_DIR` - Lyra Starter Game
- `LCP_B_PROJECT_DIR` - Alternative project

## Performance

- **Launch time**: 2-5 seconds (process start)
- **Full load time**: 30-90 seconds (project dependent)
- **Remote Control ready**: After full load
- **Dialog detection**: Zero overhead on success (diagnostics only run on ping failure)

## Related Documentation

- [UE_EDITOR_LAUNCH_GUIDE.md](../../../docs/getting-started/UE_EDITOR_LAUNCH_GUIDE.md) - Full launch guide
- [CONFIGURATION.md](../../../docs/getting-started/CONFIGURATION.md) - .env configuration
- [ue_editor_*](../../../docs/policy-tools/modify_tools.md) - Editor control via MCP

## Output Format

```
=== Unreal Editor Launch Report ===

Engine:  C:/Program Files/Epic Games/UE_5.7
Project: E:/last-contact-prod/LastContact.uproject
Flags:   -UNATTENDED

Launch:  Process started (PID 12345)
Polling: attempt 8/12...

Result:  SUCCESS
  Remote Control: responding on port 30010
  Load Time: ~62s
  Dialogs: 1 dismiss confirmed closed (Crash Recovery); re-ping verifies RC readiness
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `UnrealEditor.exe not found` | Incorrect `UECODEGEN_ENGINE_DIR` in `.env` | Verify engine path; check that `Engine/Binaries/Win64/UnrealEditor.exe` exists at that location |
| Editor blocked by modal dialog (RC not responding) | Post-crash recovery or package version mismatch dialog | A failing MCP ping makes a timeout-bounded dismiss attempt and reports success only after confirmed closure; re-ping for RC readiness and close the dialog manually if the result is unsuccessful |
| Editor process exits within 30s | Corrupted project, plugin incompatibility, or missing dependencies | Check `Saved/Logs/` for crash details; disable problematic plugins in `.uproject` |
