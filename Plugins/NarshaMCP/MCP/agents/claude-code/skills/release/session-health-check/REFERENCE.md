# session-health-check — Reference

Deep reference for the `/session-health-check` skill. Companion to `SKILL.md` (workflow) and `EXAMPLES.md` (worked invocations).

---

## 1. Tool Calls

### 1.1 `ue_check_health(compact=False)`

Full diagnostic mode. Returns the structure consumed by Step 1 (Server Health Check).

**Response fields used by this skill**:

| Field | Type | Used For |
|---|---|---|
| `status` | `"healthy" \| "degraded" \| "error"` | Server row PASS/FAIL |
| `version` | `string` | Server row Details column |
| `project_path` | `string` (abs path) | Project Path row |
| `engine_path` | `string` (abs path) | Engine Path row |
| `path_validation.status` | `"valid" \| "invalid"` | Project/Engine row validation |
| `path_validation.is_ue_project` | `bool` | Project Path PASS criterion |
| `path_validation.engine_exists` | `bool` | Engine Path PASS criterion |
| `ports.http` | `int` (0 if Editor offline) | Editor Ports row |
| `ports.ws` | `int` (0 if Editor offline) | Editor Ports row |
| `build_info.git_commit` | `string` (short SHA) | Server row Details (optional) |
| `editor` | `object` (only when full mode) | Editor liveness hint |
| `setup_warning` / `setup_status` | `string \| null` | Reported when present |

> **`compact=True` mode**: returns only `{status, version, editor_hint}` (~120 bytes, ~2ms post-#9241). Use for connectivity probes, **not** for this skill — we need the full diagnostic surface.

### 1.2 `ue_cache_control(operation="get_stats")`

Step 2 (Cache & Index Status). No params required beyond `operation`.

**Response fields used**:

| Field | Type | Used For |
|---|---|---|
| `symbol_index.loaded` | `bool` | PDB Index row exists |
| `symbol_index.symbol_count` | `int` | PDB Index Details column |
| `symbol_index.pdb_count` | `int` | PDB Index Details (optional) |
| `metadata.status` | `"loaded" \| "loading" \| "missing" \| "error"` | Metadata row |
| `metadata.node_count` | `int` | Metadata Details column |
| `metadata.last_error` | `string \| null` | Metadata FAIL reason |
| `four_phase.complete` | `bool` | PDB Index PASS criterion |
| `four_phase.running` | `bool` | PDB Index WARN criterion |
| `four_phase.symbols` | `int` | Cross-check vs `symbol_count` |

### 1.3 `python .claude/hooks/checks/orphan_process_check.py`

Step 3 (Orphan Process Detection — Issue #9360). CLI-only, no MCP wrapper.

**Output schema** (per orphan, when non-empty):

```json
{
  "pid": 29052,
  "name": "powershell.exe",
  "parent_pid": 9999,
  "parent_alive": false,
  "parent_name": null,
  "rss_mb": 31876.4,
  "age_seconds": 7843.2
}
```

**Detection rules** (all 4 must match):

| Rule | Constant | Default | Env Override |
|---|---|---|---|
| Image | `LEAK_PROCESS_NAMES` | `{powershell.exe, pwsh.exe, cmd.exe}` | — |
| Parent | `PARENT_NAMES_OF_INTEREST` ∪ deceased | `{bash.exe, sh.exe, git-bash.exe, mintty.exe}` | — |
| RSS | `RSS_THRESHOLD_MB` | `1024` (1 GB) | `ORPHAN_GUARD_RSS_MB` |
| Age | `AGE_THRESHOLD_SECONDS` | `300` (5 min) | `ORPHAN_GUARD_AGE_S` |

**Why these thresholds**: derived from Issue #9360 evidence (PIDs 29052/21876 with 31.8 GB / 14.5 GB RSS, multi-hour age, bash polling parents). Below 1 GB or 5 min → likely a legitimate one-shot, not a leak. Override via env vars for stricter / looser detection (e.g. `ORPHAN_GUARD_RSS_MB=512` flags >512 MB, useful in low-RAM CI).

**AccessDenied escalation** (when parent process is alive but unreadable):

When `psutil.Process(ppid)` raises `AccessDenied` (low-privilege account, parent owned by SYSTEM/another user), the parent name is unknown so the standard "shell parent or deceased" rule rejects the candidate. The detector escalates to flag the orphan **only** if both extreme thresholds are exceeded:

| Constant | Default | Purpose |
|---|---|---|
| `EXTREME_RSS_BYTES` | `5 * 1024 * 1024 * 1024` (5 GB) | Half of #9360's smallest victim (14.5 GB) |
| `EXTREME_AGE_SECONDS` | `3600` (1 h) | Below the bash-loop survival window |

Both required (AND), not OR — a fresh memory spike or a small long-runner isn't enough.

**psutil unavailable**: if `import psutil` fails, the detector emits a one-time stderr warning and returns `[]`. Install with `pip install psutil` to enable detection.

---

## 2. Judgment Criteria (full)

| Check | PASS | WARN | FAIL |
|---|---|---|---|
| **Server** | `status == "healthy"` | `status == "degraded"` | No response, error, or `status == "error"` |
| **Project Path** | `path_validation.is_ue_project == true` | — | `path_validation.status == "invalid"` or path missing |
| **Engine Path** | `path_validation.engine_exists == true` | — | Path not found on disk |
| **PDB Index** | `four_phase.complete == true` AND `symbol_count > 0` | `four_phase.running == true` (in progress) | `symbol_count == 0` AND not running |
| **Metadata** | `metadata.node_count > 0` AND `status == "loaded"` | `status == "loading"` | `last_error != null` OR `status == "error"` |
| **Editor Ports** | `ports.http > 0` AND `ports.ws > 0` | Default ports only (Editor offline — analysis still works) | — |
| **Orphans (#9360)** | `len(orphans) == 0` | `len(orphans) >= 1` (review + confirm before kill) | — (never auto-FAIL — always user-mediated) |

### Overall Verdict Aggregation

| All checks | Verdict |
|---|---|
| All PASS | **READY** |
| Any WARN, no FAIL | **PARTIALLY READY** |
| Any FAIL | **NOT READY** |

---

## 3. Auto-Trigger Policy

| Phrase locale | Phrases |
|---|---|
| Korean | "세션 체크", "서버 상태 확인", "MCP 준비 됐어?", "헬스 체크", "세션 검증", "서버 점검" |
| English | "Session check", "Server status", "Is MCP ready?", "Health check", "Session verify", "Server readiness" |

**Step 3 trigger conditions** (orphan detection):
- Always run on manual `/session-health-check` invocation.
- Run automatically if session wall-time > 1 hour (the leak window observed in #9360).

---

## 4. Safety Contract

This skill follows three non-negotiable rules:

1. **Never auto-kill orphan processes.** Always render the candidate table to the user and require explicit per-PID confirmation. A legitimate user-launched `powershell` session being killed is a worse failure than the leak itself.
2. **Never reintroduce `tasklist.exe` / `wmic.exe` subprocess spawns.** Issues #9322 / #9358 explicitly removed those from the daemon hot path. The orphan detector uses `psutil` (already a Python dep) which walks the process table without spawning processes.
3. **Step 3 is `python` script only, not a new MCP tool.** Adding it as an MCP operation would (a) require it to be Editor-aware (it isn't; it's a host-process audit) and (b) inflate the MCP tool surface for a once-per-session diagnostic.

---

## 5. Related

- **Issue #9360** — original leak report (46.3 GB combined RSS).
- **Issue #9322** — `tasklist.exe` subprocess removal (sibling cleanup).
- **Issue #9358** — `wmic.exe` removal + sampling fix.
- **Issue #9241** — `ue_check_health(compact=true)` ~2ms optimization.
- **CLAUDE.md → "Monitor Tool Usage (Issue #9360)"** — antipattern documentation + `monitor_guard.py` PreToolUse hook details.
- **`.claude/hooks/checks/monitor_guard.py`** — PreToolUse hook that prevents the antipattern at write time (warn-only, never blocks).
- **`.claude/hooks/checks/orphan_process_check.py`** — the detector script invoked by Step 3.
