---
name: ue-test-autofix
model: opus
description: "Purpose: Test failure → auto-fix → rerun closed loop for UE Automation Tests. Classifies failures (compile/runtime/assertion/flaky/network) from structured TestResult JSON, routes each to ue_fix_errors or manage_automation_test(diagnose_network), rebuilds, reruns. Max-retries guard + stagnation detection + regression warning. Activates on: '테스트 실패 자동 수정', 'test autofix loop', 'failure fix rerun', '자동 테스트 루프', 'autofix test failures'."
compatibility: "Requires NarshaMCP v7.0.0+ with a UE 5.x C++ project. Reads structured TestResult from Issue #7924 (test_result_types.rs, test_history.rs). Editor must be reachable for run_automation_tests."
allowed-tools: [Bash, Read, Edit, Write, TodoWrite]
argument-hint: "--test-filter <UE-filter> [--max-retries N=3] [--only compile|runtime|assertion|flaky|network] [--attempt-assertion-fix] [--dry-run] [--self-test] [--report-flaky-adoption]"
metadata:
  version: "1.0.0"
  issue: "#7925"
  epic: "#7910"
  depends_on_issue: "#7924"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Test Autofix — Failure → Auto-Fix → Rerun Closed Loop

**Issue**: #7925 (ADK Phase 3)
**Purpose**: Close the test-automation loop. Run → classify failure → call `ue_fix_errors` → rebuild → rerun, until SUCCESS / MAX_RETRIES / STAGNATION.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the 6-phase workflow automatically**.
> Do NOT just display documentation — actually run the MCP tools and the loop.

### Execution Requirements

1. **Preflight** — parse flags; gate on `ue_check_health()`; validate `--test-filter` match count.
2. **Run tests** — `ue_editor_automation(operation="run_automation_tests", params={"test_name": filter})`.
3. **Classify failures** — apply the TestResult signal rules in [REFERENCE.md](REFERENCE.md#classification-rules) to each failed `TestResult`.
4. **Apply fixes** — route by class to `ue_fix_errors` (compile / runtime) or report-only (assertion, unless `--attempt-assertion-fix`).
5. **Rebuild if needed** — `ue_build_pipeline(operation="build_editor")` when any compile class was fixed.
6. **Rerun & check exit** — loop until SUCCESS / MAX_RETRIES / STAGNATION / EDITOR_UNAVAILABLE; emit fix-history report.

---

## Auto-Trigger Phrases

### Korean
- "테스트 실패 자동 수정", "테스트 자동 고치기", "자동 테스트 루프"
- "실패하면 자동 고쳐서 재실행", "테스트 실패 자동 루프"
- "테스트 자동 수정 루프"

### English
- "test autofix loop", "failure fix rerun", "autofix test failures"
- "test failure auto-fix", "fix tests and rerun loop"
- "closed-loop test autofix"

### Do NOT trigger on
- "edge case test", "bug hunting", "clean pass" → route to `/test-iterate`
- "dev cycle", "dev loop", "build loop" → route to `/ue-dev-cycle`
- "generate test", "create test", "테스트 생성" → route to `/ue-test-gen`
- "test scenario", "시나리오 테스트" → route to `/ue-test-scenario`

See [REFERENCE.md#disambiguation](REFERENCE.md#disambiguation-vs-neighboring-skills) for the full split.

---

## Flags

| Flag | Default | Purpose |
|------|---------|---------|
| `--test-filter <filter>` | **required** | UE automation test filter (e.g. `MyGame.Systems.`) |
| `--max-retries N` | 3 | Infinite-loop guard (hard cap). |
| `--only compile\|runtime\|assertion\|flaky\|network` | all | Only auto-fix the listed class(es). flaky (#10942), network (#11375). |
| `--attempt-assertion-fix` | off | Opt-in: let `ue_fix_errors` try assertion-class failures. Higher false-fix risk. |
| `--dry-run` | off | Classify + print plan. No `ue_fix_errors`, no rebuild, no rerun. |
| `--self-test` | off | Run skill structure check only (no UE tools). |
| `--report-flaky-adoption` | off | Manual fallback mode (Issue #11910 P2). Runs `detect_flaky` once, emits one GA4 `flaky_snapshot` event for adoption-rate telemetry, prints the snapshot_id so the user can verify in GA4 DebugView. Exits 0 immediately afterward — no autofix loop, no rebuild. Requires `--test-filter` (used as the project-root resolution hint) and telemetry consent enabled. |

---

## Manual Flaky-Adoption Report Mode (Issue #11910 P2)

When `--report-flaky-adoption` is set, the skill **skips the autofix loop entirely** and runs a single MCP call that:

1. Reads `Intermediate/NarshaMCP/test_history.jsonl` for the current project.
2. Invokes `manage_automation_test(operation="detect_flaky", window_size=10)` once.
3. The MCP daemon writes a snapshot line to `Intermediate/NarshaMCP/flaky_snapshots.jsonl` AND fires one GA4 `flaky_snapshot` event (see `MCP/native/mcp_server/src/telemetry/manager.rs::track_flaky_snapshot`).
4. Prints the returned `snapshot_id` + flaky_count so the user can cross-reference in GA4 DebugView under event name `flaky_snapshot`.

**Why this mode exists.** The 12-week adoption gate (`scripts/telemetry/improvement_detector.py::detect_flaky_adoption`) needs cross-population data to count down. The primary path is passive — Issue #11910 P3 (PR #11942) auto-snapshots every 20 test runs. This manual mode is the **explicit opt-in path** for users who want to contribute a single data point without enabling the passive auto-snapshot trigger.

**Privacy guarantee.** The GA4 event only contains sha256-truncated test name hashes (16 hex chars / 64 bits). Raw test names never leave the user's machine. Verified by the GA4 dimension list in `scripts/telemetry/fetch_ga4_flaky_snapshots.py::run_flaky_snapshot_report`.

**Preconditions:**
- Telemetry consent enabled (`~/.narshamcp/.telemetry_consent` present + accepted, OR `NARSHA_TELEMETRY_INTERNAL=1` for internal builds).
- `Intermediate/NarshaMCP/test_history.jsonl` has at least 3 entries (the minimum window for `detect_flaky`).

**Pseudocode:**

```python
if "--report-flaky-adoption" in argv:
    health = ue_check_health()
    if not health.ok:
        exit("EDITOR_UNAVAILABLE")
    result = manage_automation_test(operation="detect_flaky", window_size=10)
    print(f"[flaky-adoption] snapshot_id={result.snapshot_id}")
    print(f"[flaky-adoption] flaky_count={result.flaky_count}")
    print(f"[flaky-adoption] GA4 event 'flaky_snapshot' emitted (verify in DebugView)")
    exit(0)   # No autofix loop, no rebuild.
```

---

## Preflight Gate

Before Phase 2, **abort** with a clear message if any of these fail:

| Check | Action on fail |
|-------|----------------|
| `ue_check_health()` returns ok | Print "Editor unreachable — run `/launch-editor` first." Exit `EDITOR_UNAVAILABLE`. |
| `--test-filter` present | Print flag help, exit `MISSING_FLAG`. |
| `--test-filter` matches in `[1, 500]` tests | 0 → exit `NO_TESTS_MATCH`. >500 → exit `FILTER_TOO_BROAD`, ask user to narrow. |
| `test_history.jsonl` is readable (for regression) | Non-fatal: warn, continue without regression detection. |

(Use `ue_editor_automation(operation="list_automation_tests", params={"filter": filter})` to count matches when available; otherwise fall back to a trial run and inspect `TestRunSummary.tests_run`.)

---

## The Loop

```python
# Pseudocode — see REFERENCE.md for the full algorithm
iteration  = 0
fix_log    = []
prev_sigs  = None
prev_run   = read_last_test_history_entry()   # for regression warn

while iteration < max_retries:
    iteration += 1
    run = ue_editor_automation("run_automation_tests", {"test_name": filter})

    failures = [r for r in run.results if r.status == "failed"]
    regressions = detect_regressions(run, prev_run)   # warn only

    if not failures:
        return finish("SUCCESS", fix_log, regressions)

    classified = [(f, classify(f)) for f in failures]   # see REFERENCE.md
    if only_flag:
        classified = [(f, c) for f, c in classified if c in only_flag]

    sigs = frozenset((f.name, c) for f, c in classified)
    if sigs == prev_sigs:
        return finish("STAGNATION", fix_log, regressions)
    prev_sigs = sigs

    needs_rebuild = False
    for f, cls in classified:
        if cls == "assertion" and not attempt_assertion_fix:
            fix_log.append(record(iteration, f, cls, op="REPORT_ONLY"))
            continue
        op = route_for(cls)                     # see REFERENCE.md
        res = ue_fix_errors(operation=op, params=context_from(f))
        fix_log.append(record(iteration, f, cls, op, res.outcome))
        if cls == "compile":
            needs_rebuild = True

    if needs_rebuild:
        ue_build_pipeline(operation="build_editor")
    prev_run = run

return finish("MAX_RETRIES", fix_log, regressions)
```

**Exit verdicts**: `SUCCESS` | `MAX_RETRIES` | `STAGNATION` | `EDITOR_UNAVAILABLE` | `NO_TESTS_MATCH` | `FILTER_TOO_BROAD` | `MISSING_FLAG`.

---

## Fix-History Report

Print to stdout as JSON + human-readable table. On any non-error verdict, append the JSON to `.claude/project-memory/ue-test-autofix/runs.jsonl` (create directory if missing).

```json
{
  "run_id": "2026-04-18T12:34:56Z",
  "verdict": "SUCCESS",
  "iterations": 2,
  "test_filter": "MyGame.Systems.",
  "initial": {"passed": 4, "failed": 3},
  "final":   {"passed": 7, "failed": 0},
  "fixes": [
    {"iter": 1, "test": "MyGame.Systems.FooTest", "class": "compile",
     "op": "smart", "outcome": "applied", "duration_ms": 1840},
    {"iter": 1, "test": "MyGame.Systems.BarTest", "class": "assertion",
     "op": "REPORT_ONLY", "outcome": "skipped",
     "note": "pass --attempt-assertion-fix to try auto-fix"}
  ],
  "regressions": []
}
```

This file records **fix attempts**, complementing the Rust-side `Intermediate/NarshaMCP/test_history.jsonl` which records **test runs**. The two are joinable by timestamp.

---

## Self-Test Mode

`/ue-test-autofix --self-test` runs a zero-UE sanity check:

1. **Structure** — verify `SKILL.md`, `REFERENCE.md`, `EXAMPLES.md` all exist in `.claude/skills/ue-test-autofix/`.
2. **Frontmatter** — parse this file's YAML; require `name`, `description`, `allowed-tools`.
3. **Routing** — read `.claude/skills/routing.json`; require an entry with `"skill": "ue-test-autofix"` and ≥3 ko + ≥3 en trigger patterns.
4. **Version consistency** — `SKILL.md`'s `metadata.version` must match the version field in `REFERENCE.md`.
5. **Classifier fixtures** — run classifier on the three fixtures defined in `EXAMPLES.md`; require `compile`, `runtime`, `assertion` respectively.

**Output**: `PASS` or `FAIL` with per-check results. Does **not** call any MCP tool or touch the editor.

Implementation reference (shell logic):

```bash
SKILL_DIR=".claude/skills/ue-test-autofix"
[ -f "$SKILL_DIR/SKILL.md" ] && [ -f "$SKILL_DIR/REFERENCE.md" ] && [ -f "$SKILL_DIR/EXAMPLES.md" ] \
  || { echo "FAIL: structure"; exit 1; }
grep -q '^name: ue-test-autofix' "$SKILL_DIR/SKILL.md" || { echo "FAIL: frontmatter"; exit 1; }
grep -q '"skill": "ue-test-autofix"' .claude/skills/routing.json || { echo "FAIL: routing"; exit 1; }
echo "PASS"
```

---

## Related Skills & Boundaries

| Skill | Scope | This skill's difference |
|-------|-------|-------------------------|
| `/test-iterate` | 12-category edge-case rotation until N clean passes | Rotates hypotheses; this skill reacts to **existing** failures. |
| `/ue-dev-cycle` | Code → build → fix → (test once) | No test in the inner loop; this skill puts test **in the loop**. |
| `/ue-automation-test-builder` | Generate new tests | Does not fix failures. Complements this skill. |
| `/ue-test-gen` | One-shot test generation | No loop. |
| `/issue-test-verify` | Goal-oriented verification via tests | Can call this skill for the fix-loop portion. |

See [REFERENCE.md](REFERENCE.md) for classification rules, routing tables, and the TestResult field → signal map.
See [EXAMPLES.md](EXAMPLES.md) for three worked runs (compile, runtime, assertion).
