# MCP Restart Examples

## Example 1: Basic Restart (Daemon Mode)

```bash
/mcp-restart
```

**Scenario**: After `cargo build --release`, reconnect MCP with the new binary.

**Expected output**:
```text
=== MCP Restart Report ===
Mode: Daemon (zero-click)
Binary: D:/UECodeGen_Unit/ue-code-mcp/MCP/native/build/thin-lto/narshamcp.exe   # canonical (Issue #10380, Rule 7B)
Daemon Port: 19620
Startup: 3s
Health: PASS
Status: RESTARTED SUCCESSFULLY
```

> *Per-project ports are allocated from `~/.narshamcp/daemons/registry.json` (deterministic hash of canonical project path); 19620 is the cold-start default since Issue #7962. The actual port shown depends on the project's registry entry.*

---

## Example 2: Build + Restart

```bash
/mcp-restart --build
```

**Scenario**: Rebuild `narshamcp.exe` from source, then restart.

**Steps executed**:
1. `cargo build --release -p narshamcp` in `MCP/native/mcp_server/`
2. Verify binary timestamp updated
3. Kill existing daemon, start new one
4. Wait for TCP `${DAEMON_PORT}` ready (resolved via `scripts/resolve_daemon_port.py`; cold-start default 19620, registry-allocated for known projects)
5. `ue_check_health(compact=true)` verification

---

## Example 3: STDIO Mode Fallback

```bash
/mcp-restart
```

**Scenario**: `.mcp.json` does not contain `"connect"` arg — STDIO mode detected.

**Steps executed**:
1. Kill `narshamcp.exe` process
2. Display manual reload instructions

**Expected output**:
```text
=== MCP Restart Report ===
Mode: STDIO (manual reload required)
Binary: D:/UECodeGen_Unit/ue-code-mcp/MCP/native/build/thin-lto/narshamcp.exe   # canonical (Issue #10380, Rule 7B)
Process: Killed
Status: AWAITING MANUAL RELOAD

Next step: Press Ctrl+Shift+P → type "Reload Window" → Enter

TIP: Run `/mcp-restart --switch-daemon` to switch to Daemon mode
   for zero-click restarts (no manual reload needed).
```

---

## Example 4: Switch to Daemon Mode

```bash
/mcp-restart --switch-daemon
```

**Scenario**: One-time migration from STDIO to Daemon mode for reliable zero-click restarts.

**Steps executed**:
1. Read current `.mcp.json` (STDIO config)
2. Extract `--project-path` and `--engine-path`
3. Rewrite `.mcp.json` with `connect` args
4. Start daemon process (detached)
5. Wait for TCP `${DAEMON_PORT}` (resolved via `scripts/resolve_daemon_port.py`; cold-start default 19620)

**Expected output**:
```text
=== Daemon Mode Migration ===
Previous: STDIO (direct execution)
Current:  Daemon (connect + auto-reconnect)
Port: 19620 (cold-start default; per-project ports allocated from registry)

.mcp.json updated. Future /mcp-restart calls will use zero-click daemon restart.
Reload VSCode once to activate the new .mcp.json config.
```

---

## Example 5: Build + Switch Daemon (Combined)

```bash
/mcp-restart --build --switch-daemon
```

**Scenario**: First-time setup — build binary, switch to daemon mode, verify health.

---

## Example 6: Called from Other Skills

Other skills call `/mcp-restart` automatically after binary operations:

```python
# In /mcp-deploy Step 6:
Skill("mcp-restart")

# In /deploy-test Phase 7:
Skill("mcp-restart")

# In /mcp-live-test STEP 1 (after successful build):
Skill("mcp-restart")
```

These calls auto-detect the current mode (Daemon/STDIO) and restart accordingly.

---

## Example 7: Bad `--engine-path` → A2 Ack ERROR (Issue #11632)

**Scenario**: `.mcp.json` was hand-edited and the `--engine-path` now points
at a path that does not exist (typo, deleted UE install, wrong drive letter).
Pre-#11632 the daemon would spawn, immediately die, and `/mcp-restart` would
report success because PowerShell `Start-Process` does not return an exit
code; the next MCP tool call would fail mysteriously several minutes later.

After the fix, the failure surfaces at A2 before the skill even reaches A3.

The hand-edited `.mcp.json` (note `UE_5.7_TYPO` does not exist on disk):

```json
{
  "mcpServers": {
    "narshamcp": {
      "command": "...narshamcp.exe",
      "args": ["connect", "--project-path", "C:/proj",
               "--engine-path",  "C:/UE_5.7_TYPO"]
    }
  }
}
```

Running `/mcp-restart` then produces (only the `ERROR:` lines are literal
stderr output from the helper scripts; surrounding text is annotation):

```text
$ /mcp-restart
… (A1 graceful daemon kill — no-op when none running for this project) …
… (A2 invokes spawn_daemon.sh, which preflight-checks the engine path) …
ERROR: engine path missing or not a directory: C:/UE_5.7_TYPO
ERROR: A2 spawn preflight failed
$ echo $?
1
```

**What changed under the hood**:

1. `spawn_daemon.sh` preflight detects the missing path **before** invoking
   the binary, exiting 2 with a precise stderr message.
2. Even if preflight were bypassed (e.g. the path existed but the binary
   crashed for another reason): `wait_for_daemon_ack.py` stage 3 (registry
   `status=Running`, the authoritative ack) would not see Running and exit
   1 within 5 s, and stage 4 (registry-PID liveness) would catch the
   register-then-die race. Stage 1 (spawn-PID) is advisory on Windows
   because `$!` returns the bash wrapper PID rather than the daemon's
   real OS PID — see [REFERENCE.md § A2 Acknowledgement Contract](REFERENCE.md#a2-acknowledgement-contract-issue-11632).

**Manual recovery**: fix `.mcp.json`'s `--engine-path`, then rerun
`/mcp-restart`. No daemon cleanup needed because nothing successfully spawned.
