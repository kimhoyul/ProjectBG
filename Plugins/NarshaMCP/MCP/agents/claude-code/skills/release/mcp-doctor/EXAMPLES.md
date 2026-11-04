# mcp-doctor — Worked Examples

Concrete invocations of `/mcp-doctor` covering the most common failure
modes. For the workflow protocol see [SKILL.md](SKILL.md); for cold-path
behavior tables and the process model see [REFERENCE.md](REFERENCE.md).

---

## Example 1 — "내 도구가 안 보여" (process not running)

User said: *"MCP 도구 안 보여"* → Auto-Trigger matches `MCP not working`.

```bash
/mcp-doctor
```

Skill executes Phase 1 → Phase 5:

| Phase | Result |
|---|---|
| 1 Discovery | `.mcp.json` lists `narshamcp` (Daemon mode) |
| 2 Process Audit | 0 narshamcp processes; registry has stale PID 12345 |
| 3 Diagnosis | D3 `DAEMON_NOT_STARTED` (HIGH) + D5 `STALE_REGISTRY` (MEDIUM) |
| 4 Auto-Fix | `narshamcp status --all` cleans registry → `Start-Process narshamcp daemon` |
| 5 Verify | `ue_check_health(compact=true)` returns `{status: "ok"}` — done |

User sees: "✅ Daemon restarted. Action Required: Ctrl+Shift+P → Reload Window."

---

## Example 2 — "여러 개 떴어" (multi-instance bug)

```bash
/mcp-doctor
```

Phase 2 detects 3 narshamcp processes (one was a leftover from a previous
session that didn't terminate cleanly). Phase 3 reports D4 `MULTI_INSTANCE`
HIGH. Phase 4 PID-tree kills via registry — keeps other-worktree daemons
and IDE bridges untouched (Issue #11102 bridge-aware). Phase 5 confirms
1 process remains; tools are visible in ToolSearch again.

---

## Example 3 — "런처에서 풀소스로 갈아탔는데 안 돼" (scenario F, Issue #11150)

User said: *"런처에서 풀소스로 갈아탔는데"* → Auto-Trigger matches the new
Phase 2b config-audit triggers (KO/EN list in Activation Triggers).

```bash
/mcp-doctor --config-only
```

Phase 1 minimal Discovery → SKIP Phase 2/3/4 (process audit) → Phase 2b
queries `GET /api/daemon/status`:

```json
{
  "degraded_mode": true,
  "config_errors": [
    {
      "code": "ENGINE_PATH_NOT_FOUND",
      "user_path": "C:/UE_5.6_LAUNCHER",
      "suggested_paths": [
        {"source": "env", "verified": false, "note": "stale env"},
        {"source": "config_ini", "path": "C:/UnrealEngine/Engine", "verified": true},
        {"source": "uproject", "path": "C:/UnrealEngine/Engine", "verified": true,
         "note": "EngineAssociation = 5.7"},
        {"source": "scan", "path": "C:/Program Files/Epic/UE_5.7", "verified": true}
      ]
    }
  ]
}
```

Phase 4b raises an `AskUserQuestion` for each `verified == true` candidate:

```text
Apply config_ini path 'C:/UnrealEngine/Engine' for ENGINE_PATH_NOT_FOUND?
  [Yes]  [No, try next candidate]
```

User clicks **Yes** → `apply_recovery.py --source config_ini` atomic-writes
`.env` (with `.env.bak` backup) → Phase 4b invokes `Skill('mcp-restart')`
once → Phase 5 polls `/api/daemon/status` (5-attempt budget) → `degraded_mode
== false` → ✅ Recovery succeeded.

> ⚠️ **The daemon does NOT hot-reload from `.env` mutations.** `degraded_reasons`
> is cached at `DaemonServer::new` and never recomputed
> ([`project_watcher.rs:27-32`](../../../MCP/native/mcp_server/src/daemon/project_watcher.rs#L27-L32):
> *"In-place reload (still NO) ... Restart-Daemon button is the recovery path"*).
> The SSE `ConfigChanged` event fires when `.env` is touched but only updates a
> dashboard toast — it does NOT re-validate
> ([`dashboard_api.rs:402-407`](../../../MCP/native/mcp_server/src/daemon/dashboard_api.rs#L402-L407)
> reads from the cached `server.config.degraded_reasons`). The explicit
> `Skill('mcp-restart')` in Phase 4b is **mandatory** — without it Phase 5
> verify will see `degraded_mode == true` indefinitely and the user will hit
> the 3-attempt fallback with no actual recovery.

---

## Example 4 — "engine path 안 맞아" (env source — manual shell)

```bash
/mcp-doctor --config-only
```

Same Phase 2b detection. In Phase 4b the user picks the `[env]` candidate
(maybe they want a one-shot test without committing `.env` change).
`apply_recovery.py --source env` prints to stdout:

```text
# cmd.exe (current process only — close-and-reopen does NOT persist)
set UECODEGEN_ENGINE_DIR=C:/UnrealEngine/Engine

# PowerShell (current process only)
$env:UECODEGEN_ENGINE_DIR="C:/UnrealEngine/Engine"

# Persist across sessions (PowerShell):
[Environment]::SetEnvironmentVariable("UECODEGEN_ENGINE_DIR", "C:/UnrealEngine/Engine", "User")
```

User pastes one line into their own shell, then re-launches the daemon
themselves (the running daemon's `std::env::var` snapshot is fixed at fork).

---

## Example 5 — "MCP 안돼" (diagnose-only, no mutation)

```bash
/mcp-doctor --diagnose-only
```

Runs Phase 1–3 (full audit + Phase 2b config check) but **skips Phase 4 +
Phase 4b**. Useful when:
- Investigating before pulling any trigger
- Sharing a report with a teammate before action
- Working on a daemon owned by a different worktree

Output is the diagnosis table + 7-day cumulative crash count (Issue #11117
addon) — no kills, no `.env` edits, no daemon restarts.

---

## Example 6 — "narshamcp 자꾸 죽어" (D9 repeated panic)

```bash
/mcp-doctor
```

Phase 3 calls `detect_repeated_panic.py` against
`~/.narshamcp/logs/crashes/`. Returns exit 1 with advisory JSON when the
same `reason` literal appears ≥2 times within a 5-minute window.

```text
D9 REPEATED_PANIC (HIGH)
  reason: "stack overflow on bp_get_structure"
  count:  3 / 5min
  Advisory: stale 메타데이터 캐시 가능성.
            ue_cache_control(operation='clear_all', dry_run=True) 로
            먼저 검증 후 실제 실행 권고.
```

**Advisory only** — Phase 4 does NOT auto-execute the cache clear (Issue #11117). Operator decides.

---

## Example 7 — Self-test (no side effects)

```bash
/mcp-doctor --self-test
```

Validates skill structure + grep-guards (8 checks total, including the
new D10/Phase 2b/4b wiring + atomic-write invariants from Issue #11150).
Exits 0 on PASS, 1 on any FAIL. Does NOT execute the diagnosis pipeline.

Use in CI to catch SKILL.md drift after edits.
