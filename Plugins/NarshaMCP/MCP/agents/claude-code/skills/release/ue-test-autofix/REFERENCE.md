# ue-test-autofix — Reference

**Version**: 1.0.0
**Issue**: #7925

This document is the normative reference for the closed-loop algorithm, the failure classifier, and the `ue_fix_errors` routing table. See [SKILL.md](SKILL.md) for the user-facing workflow and [EXAMPLES.md](EXAMPLES.md) for worked runs.

---

## 1. TestResult schema (input)

The skill consumes `TestResult` objects returned by
`ue_editor_automation(operation="run_automation_tests")`. Schema (from
[MCP/native/mcp_server/src/tools/test_result_types.rs](../../../MCP/native/mcp_server/src/tools/test_result_types.rs)):

| Field | Type | Used for |
|-------|------|----------|
| `name` | string | Fully-qualified UE automation test name |
| `status` | `"passed"` \| `"failed"` | Failure filter |
| `test_index` | u32 | Stable ordering |
| `duration_ms` | i64? | Regression warn (prev × 1.5 threshold) |
| `message` | string? | Primary signal for **compile** class |
| `stack_trace` | string[]? | Primary signal for **runtime** class |
| `related_files` | string[]? | Passed to `ue_fix_errors` as hint |
| `assertions` | `TestAssertion[]` | Primary signal for **assertion** class |

`TestAssertion` = `{ name, pass, detail, actual?, expected? }`.

`TestRunSummary` wraps a `results: TestResult[]` array plus `passed`/`failed`/`tests_run` counts.

**History persistence** (already automatic in Rust): each run is appended as one
line to `{project_root}/Intermediate/NarshaMCP/test_history.jsonl` by
[test_history.rs](../../../MCP/native/mcp_server/src/tools/test_history.rs).
This skill **reads** the last 1–2 entries to detect regressions; it never writes
to that file.

---

## 2. Classification rules

For each `TestResult` with `status == "failed"`, evaluate signals in this exact
precedence order and return the **first** match.

### 2.1 Compile signal

**Match if** `message` (case-insensitive) contains any of:

- `error C####:` (MSVC, `####` = 1-5 digits)
- `fatal error`
- `ld: ` or `undefined symbol` (linker)
- `: error:` after a `path/file:line:col:` prefix (clang/gcc)
- `ERROR: ` followed by `UnrealBuildTool`

**Rationale**: a test that fails at compile time has no meaningful
`stack_trace` or `assertions`; `message` carries the compiler output.

### 2.2 Runtime signal

**Match if** compile did not match **and** any of:

- `stack_trace` is non-empty
- `message` contains `Assertion failed: `, `Fatal error:`, `Check failed`, `GPF`, `EXCEPTION_`, or `SIGSEGV`
- `message` starts with `LogOutputDevice: Error:`

**Rationale**: these are engine-level crashes surfaced as test failures. They
warrant `crash_analysis` / `runtime_analysis`.

### 2.3 Assertion signal

**Match if** compile and runtime did not match **and** `assertions` contains at
least one entry with `pass == false`.

**Rationale**: ordinary test-logic failure (`TestEqual`, `TestTrue`, etc.).
These are notoriously hard to auto-fix because the "bug" may be the test, the
production code, or the fixture. Default: **report only**.

### 2.4 Flaky signal (Issue #10942)

**Precedence**: this rule fires only after §2.1 compile / §2.2 runtime / §2.3
assertion / §2.5 timeout all miss, and **before** §2.6 network / §2.7
unclassified fallback. The combined classifier order is:
`compile → runtime → timeout → assertion → flaky → network → unclassified`
(Issue #11374 inserted `timeout` between runtime and assertion;
Issue #11375 inserted `network` between flaky and unclassified).

**Match if** the failed test's name appears in `reports[].name` of a single
`ue_manage_automation_test(operation="detect_flaky", window_size=10)` call
made once per loop iteration (cached for that iteration's classification pass).

**Rationale**: a test whose pass rate is 30–90% across recent runs is
non-deterministic. Auto-fix would chase phantoms (a "fix" applied to a passing
state would look successful even if the bug was unrelated). Record the
`suspected_cause` (BurstFailure / ScatteredFailure / TimeCorrelated / Unknown)
plus `suggested_fix` text from the report and **skip auto-fix**.

**Cost**: per-iteration overhead is dominated by JSONL parse + chrono RFC3339
parse over the last `window_size` entries (default 10). Empirical estimate
< 10 ms for histories ≤ 100 entries × ≤ 500 distinct tests. The handler runs
in `tokio::task::spawn_blocking` so it never blocks the daemon's executor.

### 2.5 Timeout signal (Issue #11374)

**Precedence**: this rule fires only after §2.1 compile / §2.2 runtime / §2.3
assertion all miss, and **before** §2.4 flaky / §2.6 network / §2.7
unclassified. Inserted between `runtime` and `assertion` because
`FWaitUntil exceeded timeout` blobs sometimes overlap with stack-trace-shaped
lines that §2.2 would otherwise swallow; checking the timeout marker first
keeps the routing stable.

**Match if** `stderr` contains one of the two literal CQTest engine markers
emitted by `Engine/Source/Developer/CQTest/Private/Commands/TestCommands.cpp`:

- `"Timed out waiting for <description> after <ms> milliseconds"` (line 38,
  description-bearing `Until(TEXT("desc"), predicate, FTimespan)` overload)
- `"Latent command timed out after <ms> milliseconds"` (line 42, no-description
  `Until(predicate, FTimespan)` overload)

Real-data calibration during PR1 review caught a critical gap: the original
issue body proposed fictional markers (`FWaitUntil exceeded timeout` /
`TestCommandBuilder timeout` / `Until exceeded timeout`) that do not exist
in any engine source. These were replaced empirically by reading the actual
`TestCommands.cpp::FWaitUntil::Update()` log-emission code.

**Rationale**: the CQTest `TestCommandBuilder.Until(TEXT("desc"), predicate,
FTimespan)` chain times out when `predicate` never returns true within the
configured wait. Two common root causes: (a) the timeout value is tuned for
local-dev and too tight for CI bots under load (predicate IS reachable, just
slower), (b) the predicate has no reachable path to `true` (logic bug). The
`ue_manage_automation_test(diagnose_timeout)` op:

1. Extracts the stderr-embedded `elapsed_ms` (runtime truth — reflects whatever
   timeout actually fired: a `FTimespan::FromSeconds(N)` literal, a
   `TimeoutValue` variable, or `CQTest::DefaultTimeout = NullOpt` resolving to
   `CVar CQTest.CommandTimeout`).
2. Extracts the step description from the marker line and source-targets
   `.Until(TEXT("<desc>")` for high-confidence step location.
3. Emits one of three suggestion types: `timeout_value_adjustment` (uses
   elapsed-ms × 3 CI multiplier with optional source-literal-extraction for
   precise `FTimespan::FromSeconds` rewrites), `predicate_review` (step found
   but no elapsed-ms — atypical), or `no_step_extracted` (description not
   matched to any source file — macro-built chain or out-of-scope).

**Cost**: dominated by the source-tree walk (`Source/**/*.cpp`,
class-name-filtered) + per-file regex pass. Empirical estimate < 50 ms for
projects ≤ 5000 .cpp files; the walk is capped at `MAX_SCAN_FILES = 5000`
and `MAX_SCAN_DEPTH = 12`. The handler runs in `tokio::task::spawn_blocking`
so it never blocks the daemon's executor. Telemetry snapshot appended to
`Intermediate/NarshaMCP/diagnosis_snapshots.jsonl` (fire-and-forget; shares
1000-line FIFO + 6h dedup contract with `flaky_snapshots.jsonl`).

### 2.6 Network signal (Issue #11375)

**Precedence**: this rule fires only after §2.1 compile / §2.2 runtime / §2.3
assertion / §2.5 timeout / §2.4 flaky all miss, and **before** §2.7
unclassified fallback.

**Match if** any of the following markers appear in the failed test's stderr
or stdout tail (case-insensitive substring):

- `PIENetworkComponent`
- `UntilClients`
- `SpawnAndReplicate`
- `ReplicatedPawn is null` (or `... is null on client`)
- `Replication not synchronized`

When matched, route to `ue_manage_automation_test(operation="diagnose_network",
project_root=<root>)`. The diagnostic scans the project's CQTest .cpp files
(default globs: `Source/**/Tests/**/*.cpp`, `Source/**/Private/Tests/**/*.cpp`,
`Plugins/**/Source/**/Tests/**/*.cpp`) for the canonical Issue #11375 bug
pattern: `Network.SpawnAndReplicate<_, &State::M>()` chains that flow into
`.ThenClients(...)` accessing `M` without an intervening `.UntilClients(...)`
sync. For each hit it emits a per-site `NetworkDiagnosisReport` with a
patch-style `.UntilClients([](auto& s) { return s.M != nullptr; })` insertion
suggestion.

**Detection pattern** (LazyLock regex + manual bracket-balancer in
[`tools/network_diagnosis.rs`](../../../MCP/native/mcp_server/src/tools/network_diagnosis.rs)):

```text
SpawnAndReplicate < ... &Type::Member > ( ... )<chain_tail>;
                                  ^                       ^
                          captured member             scanned for
                          (e.g. "ReplicatedPawn")     UntilClients/ThenClients
```

C++ comments (`//`, `/* */`) are stripped before matching so a comment
containing the literal token `UntilClients` cannot cause a false negative.
String literals are preserved verbatim.

**Diff output format**: a single inserted line, 4-space indented, with an
anonymous-state lambda. Operators substitute the real state type when
applying (the diagnostic intentionally uses `auto&` to stay project-neutral):

```cpp
    .UntilClients([](auto& s) { return s.ReplicatedPawn != nullptr; })
```

**Snapshot location**: each `diagnose_network` invocation appends one
`NetworkSnapshot` line to
`{project_root}/Intermediate/NarshaMCP/network_snapshots.jsonl`. Same FIFO
(1000-line cap) + 6-hour same-session dedup contract as
`flaky_snapshots.jsonl` — helpers reused via `pub(crate)` from
[`flaky_snapshot.rs`](../../../MCP/native/mcp_server/src/tools/flaky_snapshot.rs)
(`snapshot_path_for`, `write_jsonl_atomic`, `read_jsonl`). Adoption-rate
computation is deferred to a follow-up sub-issue.

**Limitations** (documented misses):

- **Aliased builder** (`auto b = Network.SpawnAndReplicate(...); b.ThenClients(...);`):
  the chain tail terminates at the `;` after `()`, so the `.ThenClients`
  access on the alias is not connected to the original spawn site. Without a
  symbol table we cannot follow the alias. Tracked by the
  `aliased_builder.cpp` acceptance fixture (Issue #11375 expects ≥4/5
  fixtures correct — this is the allowed miss).
- **Conditional chains** (`if (cond) Network.SpawnAndReplicate(...).ThenClients(...);`):
  the bracket-balancer treats `if`-bodies as opaque; chains nested two-deep
  inside conditional bodies still detect, but chains where the
  `SpawnAndReplicate` and `.ThenClients` are split across separate
  conditional branches are not connected.
- **UntilServer-only chains**: `.ThenClients` lambdas that legitimately need
  no client-side replication sync are flagged spuriously unless their body
  does not reference the captured member name. The classifier filters on
  member-name presence in the chain tail (see
  `classify_site_then_without_member_ref_returns_none` test) to suppress
  most of these.

### 2.7 Fallback

No signal matched → **unclassified**. Record verbatim in `fix_log` with
`class: "unclassified"` and skip auto-fix.

---

## 3. `ue_fix_errors` routing

| Class | Default op | Fallback op | Params passed |
|-------|------------|-------------|---------------|
| `compile` | `smart` | `scan_build_log` | `{ error_message: message, related_files }` — `smart` internally routes to `build_and_fix` when the project can be rebuilt. |
| `runtime` | `crash_analysis` | `runtime_analysis` | `{ stack_trace, message, related_files }` |
| `timeout` | `ue_manage_automation_test(diagnose_timeout)` | — (REPORT_ONLY) | `{ stderr_tail, project_root, source_glob? }`. Returns `candidate_steps` (Until/DoAsync file:line + text) + `suggestion` (`timeout_value_adjustment` / `predicate_review` / `no_step_extracted` / `no_marker_found`). Issue #11374. |
| `assertion` | — (REPORT_ONLY) | `smart` if `--attempt-assertion-fix` | `{ assertion_name, expected, actual, detail, test_name }` |
| `flaky` | — (REPORT_ONLY) | — | Record `{ suspected_cause, pass_rate, run_count, fail_count, suggested_fix }` from the report. Issue #10942. |
| `network` | — (REPORT_ONLY) | `manage_automation_test(diagnose_network)` | Record per-site `{ file, line, member_name, diagnosis_kind, suggested_diff, confidence }` from `diagnoses[]`. Issue #11375. |
| `unclassified` | — (REPORT_ONLY) | — | Record only |

**Why `smart` for compile**: `ue_fix_errors(smart)` auto-detects compile vs
linker vs config and either parses an existing build log or spawns a build
(`build_and_fix`). We don't need to second-guess it — we give it the message
text and let it route.

**Why `crash_analysis` for runtime**: it specifically consumes `stack_trace`
and matches against known engine crash patterns. `runtime_analysis` is the
broader fallback when crash patterns don't match.

**Cross-layer note (Issue #11374)**: the legacy `classify_failure()` helper
inside `MCP/native/mcp_server/src/tools/manage_automation_test/mod.rs`
(grep for the function name; deliberately not pinned to a line number so the
reference does not rot as the file evolves) uses a DIFFERENT precedence
(`assertion > timeout > compile > runtime`) for the
`ue_manage_automation_test(categorize_failures)` op's UI-grouping buckets.
That's a separate layer — bucket assignment for surfacing failure groupings
in a UI — and is intentionally NOT synchronized with this classifier chain.
The two layers serve different consumers and changing the legacy helper's
order would be a behavior change for any caller relying on its current bucket
assignment. See Issue #11374 plan for rationale.

---

## 4. Loop algorithm (normative)

```text
input: filter, max_retries, only_classes, attempt_assertion_fix, dry_run
state: iteration=0, fix_log=[], prev_sigs=None, prev_run=read_last_history()

preflight:
  if not ue_check_health(): return EDITOR_UNAVAILABLE
  if filter is empty:        return MISSING_FLAG
  matches = count_tests(filter)     # via list_automation_tests or trial-run
  if matches == 0:           return NO_TESTS_MATCH
  if matches > 500:          return FILTER_TOO_BROAD

loop:
  while iteration < max_retries:
    iteration += 1
    run = ue_editor_automation("run_automation_tests", {test_name: filter})
    failures = [r for r in run.results if r.status == "failed"]
    regressions = diff_regressions(run, prev_run)   # warn-only

    if not failures:
      emit_report("SUCCESS", fix_log, regressions); return

    # Issue #10942: identify flaky tests once per iteration so the classifier
    # below can route them to REPORT_ONLY before any auto-fix is attempted.
    flaky_set = {r.name for r in
                 ue_manage_automation_test(operation="detect_flaky", window_size=10).reports}

    classified = map(lambda f: classify(f, flaky_set), failures)
    if only_classes:
      classified = [(f, c) for (f, c) in classified if c in only_classes]

    sigs = frozenset((f.name, c) for (f, c) in classified)
    if sigs == prev_sigs:
      emit_report("STAGNATION", fix_log, regressions); return
    prev_sigs = sigs

    if dry_run:
      emit_dry_run_table(classified); return

    needs_rebuild = False
    for (f, cls) in classified:
      if cls == "assertion" and not attempt_assertion_fix:
        fix_log.append({iteration, test:f.name, class:cls, op:"REPORT_ONLY"})
        continue
      if cls == "flaky":
        # Issue #10942: never auto-fix a non-deterministic test.
        fix_log.append({iteration, test:f.name, class:cls, op:"REPORT_ONLY"})
        continue
      if cls == "unclassified":
        fix_log.append({iteration, test:f.name, class:cls, op:"REPORT_ONLY"})
        continue
      op = route_for(cls)
      res = ue_fix_errors(op, params_for(f, cls))
      fix_log.append({iteration, test:f.name, class:cls, op, outcome:res.status, duration_ms:res.duration_ms})
      if cls == "compile" and res.status == "applied":
        needs_rebuild = True

    if needs_rebuild:
      ue_build_pipeline("build_editor")
    prev_run = run

  emit_report("MAX_RETRIES", fix_log, regressions)
```

### Stagnation definition

Two consecutive iterations produce the same `{(test_name, class)}` set. That
means: same tests failing, same classification — no fix applied made progress.
Exit to avoid burning editor time.

### Regression definition (warn-only)

For each test in the current run, if the prior `test_history.jsonl` entry shows
the same test with `status == "passed"` and the current run has `status ==
"failed"`, it is a **regression**. Emit it in the final report under
`"regressions": [...]` but do **not** exit early on it — the loop should still
try to fix.

---

## 5. Fix-history schema (output)

Written to stdout as JSON and appended to
`.claude/project-memory/ue-test-autofix/runs.jsonl` on every non-error verdict.

```json
{
  "run_id": "ISO-8601 timestamp (UTC)",
  "verdict": "SUCCESS | MAX_RETRIES | STAGNATION | EDITOR_UNAVAILABLE | NO_TESTS_MATCH | FILTER_TOO_BROAD | MISSING_FLAG",
  "test_filter": "string",
  "iterations": 0,
  "initial": { "passed": 0, "failed": 0 },
  "final":   { "passed": 0, "failed": 0 },
  "fixes": [
    {
      "iter": 1,
      "test": "FullyQualified.Test.Name",
      "class": "compile | runtime | assertion | flaky | network | unclassified",
      "op":    "smart | crash_analysis | runtime_analysis | scan_build_log | manage_automation_test(diagnose_network) | REPORT_ONLY",
      "outcome": "applied | noop | error | skipped",
      "duration_ms": 0,
      "note": "optional"
    }
  ],
  "regressions": [
    {"test": "...", "previously": "passed", "now": "failed"}
  ]
}
```

---

## 6. Disambiguation vs neighboring skills

| Skill | Loop? | Runs tests? | Calls `ue_fix_errors`? | Scope |
|-------|:----:|:-----------:|:----------------------:|-------|
| **`/ue-test-autofix`** (this) | ✅ | ✅ every iter | ✅ on every classified failure | Fix existing test failures. |
| `/test-iterate` | ✅ | ✅ every iter | ✴ when bug found | Hunt new edge cases until N clean passes. |
| `/ue-dev-cycle` | ✅ (build only) | ✴ once, after loop | ✅ | Implement + build + fix + test-once. |
| `/ue-automation-test-builder` | ✴ (score-iterate) | ✅ | ✘ | Generate new tests. |
| `/ue-test-gen` | ✘ | ✘ | ✘ | One-shot template gen. |
| `/ue-test-scenario` | ✘ | ✘ (builds, no exec) | ✘ | Scenario → Complex+Latent test. |
| `/issue-test-verify` | ✴ | ✅ | ✴ | Goal-oriented verification. |

**When to pick which**:

- Tests exist, some are failing, I want them fixed → **`/ue-test-autofix`**
- I want to discover new bugs via edge cases → `/test-iterate`
- I need to write new C++ + build + test → `/ue-dev-cycle`
- I need new test scaffolds → `/ue-test-gen` or `/ue-automation-test-builder`

---

## 7. Safety invariants

1. **Max-retries is a hard cap.** Never loop more than `max_retries` iterations, even if progress is being made.
2. **Stagnation halts the loop.** If two consecutive iterations produce the same `{(test, class)}` set, exit.
3. **Assertion auto-fix is opt-in.** Never call `ue_fix_errors` on assertion-class failures unless `--attempt-assertion-fix` is set.
4. **Editor preflight is mandatory.** Never attempt a run without confirming editor health.
5. **Filter width is bounded.** Abort if `--test-filter` resolves to 0 or more than 500 tests.
6. **Dry-run must not call `ue_fix_errors`.** The classification table is printed; no fixes attempted; no rebuild.
7. **Fix-history is append-only.** Never rewrite `runs.jsonl`.

---

## 8. Mapping to issue #7925 acceptance criteria

| Acceptance criterion | Satisfied by |
|----------------------|--------------|
| Classify failures (compile / runtime / assertion) | §2 classification rules |
| Route to `ue_fix_errors` per class | §3 routing table |
| Auto rebuild + rerun after fix | §4 loop, `ue_build_pipeline("build_editor")` + next iteration |
| Max retry (no infinite loop) | §4 preflight + §7 invariant 1, 2 |
| Fix-history report | §5 schema + SKILL.md stdout + `runs.jsonl` |
