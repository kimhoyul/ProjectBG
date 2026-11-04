# session-health-check — Examples

Worked invocations of `/session-health-check`. See `SKILL.md` for the workflow contract and `REFERENCE.md` for the response schema.

---

## Example 1 — Healthy session (READY)

**Invocation**:
```text
/session-health-check
```

**Step 1 — `ue_check_health()`**:
```python
ue_check_health()
```
Response (abbreviated):
```json
{
  "status": "healthy",
  "version": "0.9.13",
  "project_path": "I:/Lyra/LyraStarterGame",
  "engine_path": "C:/Program Files/Epic Games/UE_5.7",
  "path_validation": {"status": "valid", "is_ue_project": true, "engine_exists": true},
  "ports": {"http": 30010, "ws": 30020},
  "build_info": {"git_commit": "f9720d99c"}
}
```

**Step 2 — `ue_cache_control(operation="get_stats")`**:
```python
ue_cache_control(operation="get_stats")
```
Response:
```json
{
  "symbol_index": {"loaded": true, "symbol_count": 164032, "pdb_count": 121},
  "metadata": {"status": "loaded", "node_count": 8721, "last_error": null},
  "four_phase": {"complete": true, "running": false, "symbols": 164032}
}
```

**Step 3 — Orphan detection**:
```bash
python .claude/hooks/checks/orphan_process_check.py
```
Output:
```text
No orphan polling-loop processes detected.
```
→ row omitted from report (no signal = no noise).

**Step 4 — Report**:
```markdown
## Session Health Report

| Check        | Status | Details                              |
|--------------|--------|--------------------------------------|
| Server       | PASS   | v0.9.13, status=healthy              |
| Project Path | PASS   | I:/Lyra/LyraStarterGame              |
| Engine Path  | PASS   | C:/Program Files/Epic Games/UE_5.7   |
| PDB Index    | PASS   | 164,032 symbols, 121 PDBs            |
| Metadata     | PASS   | 8,721 nodes                          |
| Editor Ports | PASS   | http=30010, ws=30020                 |

### Overall: READY
```

---

## Example 2 — Editor offline + indexing in progress (PARTIALLY READY)

**Invocation**: `세션 체크` (auto-trigger via Korean keyword)

**Step 1 response** highlights:
```json
{"status": "healthy", "ports": {"http": 0, "ws": 0}}
```
→ Editor offline (default 0/0). Offline analysis still works.

**Step 2 response** highlights:
```json
{
  "symbol_index": {"loaded": false, "symbol_count": 0},
  "four_phase": {"complete": false, "running": true, "symbols": 42100}
}
```
→ PDB Index = WARN (running, not failed).

**Step 4 — Report**:
```markdown
| Check        | Status | Details                       |
|--------------|--------|-------------------------------|
| Server       | PASS   | v0.9.13, status=healthy       |
| PDB Index    | WARN   | indexing in progress (42,100/~) |
| Editor Ports | WARN   | Editor offline (default 0/0)  |

### Overall: PARTIALLY READY
```

Recovery: wait for indexing to finish (~30s for Lyra), or launch Editor for live operations.

---

## Example 3 — Long session with shell-orphan detection (NOT READY pending kill)

**Trigger**: Manual `/session-health-check` after a 4-hour session.

**Step 3 — Orphan detection** (non-empty, Kind column added in v1.2.0):
```bash
python .claude/hooks/checks/orphan_process_check.py
```
Output:
```text
Detected 2 orphan candidate(s) (Issues #9360 + #11269 footprints):

| PID   | Image          | Kind         | Parent PID | Parent      | Parent Alive | RSS (MB) | Age (s) |
|-------|----------------|--------------|------------|-------------|--------------|----------|---------|
| 29052 | powershell.exe | shell-orphan | 9999       | (deceased)  | False        | 31876.4  | 7843    |
| 21876 | powershell.exe | shell-orphan | 8888       | bash.exe    | True         | 14492.1  | 5210    |

These processes match a known leak footprint. Review carefully -- do NOT auto-kill.
```

**Step 4 — Report** (Orphans row included):
```markdown
| Check    | Status | Details                                           |
|----------|--------|---------------------------------------------------|
| Server   | PASS   | v0.9.13                                           |
| Orphans  | WARN   | 2 leak candidates (Issues #9360 + #11269)         |
```

**User confirmation prompt**:
```text
Kill PIDs [29052, 21876]? (yes/no per PID)
```

On `yes` for PID 29052:
```bash
taskkill /PID 29052 /F
# → SUCCESS: The process with PID 29052 has been terminated.
```

After both confirmed kills:
```text
Recovered ~46.3 GB RSS. Re-running Step 3 to confirm.
```

---

## Example 4 — Self-test mode (no MCP calls)

**Invocation**: `/session-health-check --self-test`

```bash
# 1. Frontmatter validity
grep -E "^(name|description|allowed-tools|argument-hint):" \
  .claude/skills/session-health-check/SKILL.md
# → all 4 lines present

# 2. Companion files exist
ls .claude/skills/session-health-check/{SKILL.md,REFERENCE.md,EXAMPLES.md}
# → all 3 present

# 3. Workflow steps in order
grep -nE "^### Step [1-4]:" .claude/skills/session-health-check/SKILL.md
# → 4 lines, ascending

# 4. Hook script importable
python -c "import sys; sys.path.insert(0,'.claude/hooks/checks'); \
           from orphan_process_check import detect_orphans; print('OK')"
# → OK

# 5. Routing entry exists
python -c "import json; r=json.load(open('.claude/skills/routing.json')); \
           print(any(x.get('skill')=='session-health-check' for x in r['rules']))"
# → True

# 6. narshamcp helpers reachable (Issue #11269 — added in v1.2.0)
python -c "import sys; sys.path.insert(0,'.claude/hooks/checks'); \
           from orphan_process_check import (_process_matches_narshamcp_zombie, \
           _registered_narshamcp_pids, _listening_pids_on_port); print('OK')"
# → OK
```

**Exit**: `PASS` — all 6 checks succeeded.

No `ue_check_health` or `ue_cache_control` invoked. Pure static validation, suitable for `/agent-test` and CI.

---

## Example 5 — Server unreachable (NOT READY)

**Step 1 — `ue_check_health()`** raises connection error / returns nothing within 5s.

**Step 4 — Report**:
```markdown
| Check  | Status | Details                                         |
|--------|--------|-------------------------------------------------|
| Server | FAIL   | No response from MCP server (connection timeout)|

### Overall: NOT READY
```

**Recovery**:
1. Run `/mcp-doctor` for full diagnosis + auto-fix (preferred).
2. Or manually: `/mcp-restart` → wait 5s → re-run `/session-health-check`.
3. If still failing: verify `narshamcp.exe` path in `.mcp.json` is absolute and correct.

---

## Example 6 — narshamcp zombie detection after a test-outputs lock (Issue #11269)

**Scenario**: Yesterday's real-data validation run used `Start-Process narshamcp.exe -ArgumentList "--idle"` to spawn 3 detached daemons. Session ended, daemons survived, and the next session's `cargo test` hit `os error 5` because `test-outputs/` was still locked. Today's `/session-health-check` catches them.

**Trigger**: Manual `/session-health-check` (or auto on session wall-time > 1h).

**Step 3 — Orphan detection** (non-empty, narshamcp rule fires):
```bash
python .claude/hooks/checks/orphan_process_check.py
```
Output:
```text
Detected 3 orphan candidate(s) (Issues #9360 + #11269 footprints):

| PID   | Image         | Kind             | Parent PID | Parent       | Parent Alive | RSS (MB) | Age (s) |
|-------|---------------|------------------|------------|--------------|--------------|----------|---------|
| 27796 | narshamcp.exe | narshamcp-zombie | 5108       | claude.exe   | True         | 2808.1   | 1966    |
| 25856 | narshamcp.exe | narshamcp-zombie | 10304      | (deceased)   | False        | 103.2    | 1959    |
| 27108 | narshamcp.exe | narshamcp-zombie | 30368      | (deceased)   | False        | 102.3    | 1948    |

These processes match a known leak footprint. Review carefully -- do NOT auto-kill. To terminate manually:
    Windows: taskkill /PID <pid> /F
    POSIX:   kill -9 <pid>

Narshamcp zombies (Issue #11269): before killing, confirm the PID is
NOT the live session daemon. The /mcp-doctor skill documents how to
inspect ~/.narshamcp/daemons/registry.json (see
.claude/skills/mcp-doctor/SKILL.md, daemon registry section).
```

**Why these were flagged but the live session daemon was NOT** (three-layer exclusion):
- `_registered_narshamcp_pids()` — reads `~/.narshamcp/daemons/registry.json` → returns PIDs the daemon system marks as live. Detached zombies were never written there.
- `_listening_pids_for_image("narshamcp.exe")` — port-agnostic check (Issue #11269 ns-review C-2): any narshamcp.exe holding ANY LISTEN socket is live. Catches per-project port allocation (registry.rs hands out 19620–19719+, so a single-port check would miss daemons on 19661 / 19708 / etc.).
- `_listening_pids_on_port(NARSHAMCP_DEFAULT_PORT)` — kept for back-compat with deployments pinned to one port.
- A `narshamcp.exe` process must be in `registered ∪ image_listening ∪ port_listening` to be excluded. The 3 zombies satisfy none.

**Step 4 — Report**:
```markdown
| Check    | Status | Details                                           |
|----------|--------|---------------------------------------------------|
| Server   | PASS   | v0.9.13                                           |
| Orphans  | WARN   | 3 leak candidates (Issues #9360 + #11269)         |
```

**User confirmation flow** (per-PID, never auto-kill):
```text
Kill PIDs [27796, 25856, 27108]? (yes/no per PID — confirm each is NOT the live session)
```

Verify against the registry before each kill:
```bash
cat ~/.narshamcp/daemons/registry.json | python -c "import json,sys; d=json.load(sys.stdin); print({e['pid'] for e in d['entries'].values()})"
# → {15592}     ← only PID 15592 is the live session; 27796/25856/27108 are safe to kill
taskkill /PID 27796 /F
taskkill /PID 25856 /F
taskkill /PID 27108 /F
```

After confirmed kills:
```text
Recovered ~3.0 GB RSS. test-outputs/ file lock released. Re-running Step 3 to confirm.
```

**Threshold tuning**: For a stricter hygiene policy (e.g. catch 50 MB / 5 min daemons), override the env vars before invoking:
```bash
ORPHAN_GUARD_NARSHAMCP_RSS_MB=50 ORPHAN_GUARD_NARSHAMCP_AGE_S=300 \
  python .claude/hooks/checks/orphan_process_check.py
```
