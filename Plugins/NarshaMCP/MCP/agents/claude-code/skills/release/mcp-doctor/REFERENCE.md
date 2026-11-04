# mcp-doctor — Reference

Companion to [`SKILL.md`](SKILL.md). Cold-context details on the daemon process model, why image-name kill is forbidden, and the cross-worktree damage scenarios that motivated Issue #11102.

## Process Model

NarshaMCP runs in three distinct process modes. They look identical in `tasklist` (all named `narshamcp.exe`), but differ in argv mode token, parent process, and lifecycle:

| Mode | argv shape | Parent | Lifecycle | Registry tracked? |
|------|-----------|--------|-----------|-------------------|
| **Daemon** | `narshamcp.exe daemon --project-path X --engine-path Y` | Spawning shell (`/mcp-restart`, `Start-Process`, etc.) | Long-lived; one per project-path; owns a TCP port | Yes — entry in `~/.narshamcp/daemons/registry.json` keyed by canonical project_id |
| **Connect bridge** | `narshamcp.exe connect --project-path X --engine-path Y` | IDE process (Claude Code, VS Code Claude extension) | Tied to IDE session; respawned by IDE on TCP EOF | No — bridges discover their daemon via the registry but don't write to it |
| **STDIO** | `narshamcp.exe` (no subcommand) or `narshamcp.exe stdio --project-path X` | IDE process or test harness | Per-IDE-session; default if `.mcp.json` lacks `"connect"` arg | No |

The bridge's parent is the **IDE**, not the daemon. So a PID-tree force-kill anchored on the daemon's PID (via `taskkill /T /F /PID <daemon_pid>`) walks the daemon's children only — it never reaches bridges. This is the invariant that makes Issue #11102's port/PID-scoped `--nuke` safe.

## Why Image-Name Kill Is Forbidden

`Stop-Process -Name narshamcp -Force` (or `taskkill /F /IM narshamcp.exe`) matches every running `narshamcp.exe` instance regardless of mode, project, or worktree:

- **Daemons in unrelated worktrees** — developers working on multiple worktrees simultaneously (e.g., `D:\UECodeMCP\B`, `D:\UECodeMCP\C`, `D:\UECodeMCP\F`) each run their own daemon on its own port. Image-name kill takes them all down at once.
- **IDE-spawned bridges** — when Claude Code or VS Code launches, it spawns a `narshamcp connect ...` bridge per active IDE session. Killing the bridge breaks the IDE's tool calls until the IDE detects EOF and respawns (~5-15s of dead air, sometimes longer).
- **STDIO sessions in other IDEs** — same problem; an unrelated test harness or alternate IDE running its own STDIO narshamcp is killed.

### Real-cost evidence

2026-05-07 `/mcp-live-test` debug detour: ~3 hours lost when `/mcp-doctor --nuke` invoked from worktree F killed daemons in B and C plus the F worktree's IDE bridge. Diagnosis was compounded by Issue #8355 (CREATE_NO_WINDOW migration made the daemon invisible to plain `Get-Process`), so the operator could not see which processes had survived the kill. The empirical timeline is captured in [Issue #11102](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/11102) and the parent epic [#8450 / PR #11078](https://github.com/Next-Stage-Inc/ue-code-mcp/pull/11078).

## Issue #8355 — CREATE_NO_WINDOW Invisibility

The daemon is launched with the `CREATE_NO_WINDOW` Win32 process-creation flag (Issue #8355), which suppresses console window creation. Plain `Get-Process -Name narshamcp` may not list these "invisible" daemons in some PowerShell sessions depending on user privilege and shell mode. The reliable enumeration is `Get-CimInstance Win32_Process -Filter "Name='narshamcp.exe'"` which queries WMI and surfaces all narshamcp processes regardless of console subsystem.

For Phase 2 process audit consistency, we keep `Get-Process` (it's good enough for "is something running?" diagnostics), but for kill operations we always go through registry-PID lookup or CIM CommandLine match — never the image-name path.

## Bridge Auto-Reconnect Behavior

Bridges (mode = `connect`) maintain a TCP connection to their daemon. When the daemon's TCP listener goes away (graceful stop, force-kill, crash), the bridge sees a `WSAECONNRESET` / EOF and enters reconnect-with-backoff (per `daemon/client.rs`). The IDE typically respawns the bridge within 10 seconds.

This means: **killing a daemon while a bridge is connected is acceptable** — the bridge survives the daemon's death and reconnects when the daemon respawns. What is NOT acceptable is killing the bridge process itself. Image-name kill does both; PID-tree kill from the daemon's PID does only the first.

## CommandLine Anchor Patterns

When falling back to CIM-based scoped match (stale registry, PID dead, etc.), use these anchored regex patterns. They prevent over-matching short or substring-y project paths and exclude the wrong process modes:

| Target mode | PowerShell `-match` regex | Why this anchor |
|-------------|--------------------------|-----------------|
| Daemon only | `'\sdaemon\s'` (combined with `Name='narshamcp.exe'` filter) | Mode token appears in argv as a standalone word; `\s...\s` excludes substrings like `daemon-` or `daemonset` |
| Bridge only | `'\sconnect\s'` | Symmetric anchor for bridge detection |
| STDIO only | `-notmatch '\s(daemon|connect)\s'` | Negative match (STDIO has no subcommand or uses `stdio`) |
| Project scoping | `-like "*$projectPath*"` AND mode anchor above | Substring match on path is OK because it's combined with mode token (which is regex-anchored) |

**Example: stale-registry fallback** (kill daemons matching THIS project, leave bridges alone):

```powershell
Get-CimInstance Win32_Process -Filter "Name='narshamcp.exe'" | Where-Object {
    $_.CommandLine -match '\sdaemon\s' -and
    $_.CommandLine -like "*$projectPath*"
} | ForEach-Object { taskkill /T /F /PID $_.ProcessId | Out-Null }
```

**Example: STDIO-only kill** (kill THIS project's STDIO process, leave daemons and bridges alone):

```powershell
Get-CimInstance Win32_Process -Filter "Name='narshamcp.exe'" | Where-Object {
    $_.CommandLine -like "*$projectPath*" -and
    $_.CommandLine -notmatch '\s(daemon|connect)\s'
} | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
```

## Reverse Port Lookup

`/mcp-doctor --nuke --port <port>` resolves the daemon owning that port via `resolve_daemon_port.py --by-port <port> --field pid` (Issue #11102 added this mode). The resolver iterates `~/.narshamcp/daemons/registry.json` entries and returns the first `pid` whose `port` matches. Returns `--default 0` if no match — caller must check `[ "$pid" != "0" ]` before using.

## Related Tools

- `narshamcp status --all` — auto-cleans dead PIDs from registry. Preferred over manual `rm -rf ~/.narshamcp/daemons/`.
- `narshamcp stop --port <port>` — graceful TCP shutdown, sends shutdown_request to daemon (`cli.rs:372-391`). 5s budget recommended.
- `narshamcp cleanup --orphans` — terminates narshamcp processes whose parent is dead (Issue #9381). Recovery path for STDIO orphans pre-#9381 binaries.

## Self-Test Grep Guards

`/mcp-doctor --self-test` validates:

1. SKILL.md does NOT contain `Stop-Process -Name narshamcp -Force` (the dangerous executable form — anchored on `-Force` to avoid matching didactic prose that explains the prohibition)
2. SKILL.md DOES contain `Get-CimInstance Win32_Process` (CIM-based fallback is wired)
3. SKILL.md DOES contain `taskkill /T /F /PID` (PID-tree force-kill pattern)

These three greps catch the entire regression class introduced by Issue #11102.

## See Also

- [Issue #11102](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/11102) — bridge-aware kill mode
- [Issue #8450 / PR #11078](https://github.com/Next-Stage-Inc/ue-code-mcp/pull/11078) — parent epic-sub (priority queue + session isolation)
- [Issue #8355](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/8355) — CREATE_NO_WINDOW migration
- [Issue #9381](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/9381) — orphan stdio cleanup CLI
- [`SKILL.md`](SKILL.md) — main flow + Phase 4 `--nuke` implementation
- [`mcp-restart/SKILL.md`](../mcp-restart/SKILL.md) — sibling skill, same kill-pattern requirements
