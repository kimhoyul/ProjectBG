# ue-test-autofix — Examples

**Version**: 1.0.0
**Issue**: #7925

Three worked runs showing the classifier + loop in action. Each example
includes a fixture `TestRunSummary`, the expected classification, and the
expected fix-history report. Used by `--self-test` to verify the classifier.

---

## Example 1: Compile error → auto-fix → success

### Input (fixture 1: `pure_compile.json`)

```json
{
  "passed": 2,
  "failed": 1,
  "tests_run": 3,
  "total_assertions": 2,
  "assertions_passed": 2,
  "assertions_failed": 0,
  "results": [
    {"name": "MyGame.Systems.Passes", "status": "passed", "test_index": 1, "duration_ms": 12},
    {"name": "MyGame.Systems.AlsoPasses", "status": "passed", "test_index": 2, "duration_ms": 8},
    {
      "name": "MyGame.Systems.BrokenCompile",
      "status": "failed",
      "test_index": 3,
      "message": "MyGameTest.cpp(42): error C2065: 'FoobarType': undeclared identifier",
      "related_files": ["MyGameTest.cpp:42"]
    }
  ]
}
```

### Expected classification

| Test | Class | Reasoning |
|------|-------|-----------|
| `MyGame.Systems.BrokenCompile` | **compile** | `message` contains `error C####:` pattern (§2.1) |

### Expected tool calls (iteration 1)

```
ue_fix_errors(operation="smart", params={
  "error_message": "MyGameTest.cpp(42): error C2065: 'FoobarType': undeclared identifier",
  "related_files": ["MyGameTest.cpp:42"]
})
# → applied an include fix
ue_build_pipeline(operation="build_editor")
# → success
```

### Iteration 2

- Run tests again → 3 passed, 0 failed → exit `SUCCESS`.

### Expected fix-history report

```json
{
  "run_id": "2026-04-18T12:34:56Z",
  "verdict": "SUCCESS",
  "iterations": 2,
  "test_filter": "MyGame.Systems.",
  "initial": {"passed": 2, "failed": 1},
  "final":   {"passed": 3, "failed": 0},
  "fixes": [
    {"iter": 1, "test": "MyGame.Systems.BrokenCompile", "class": "compile",
     "op": "smart", "outcome": "applied"}
  ],
  "regressions": []
}
```

---

## Example 2: Runtime crash → report + retry

### Input (fixture 2: `pure_runtime.json`)

```json
{
  "passed": 5,
  "failed": 1,
  "tests_run": 6,
  "total_assertions": 8,
  "assertions_passed": 8,
  "assertions_failed": 0,
  "results": [
    {"name": "MyGame.World.Spawn", "status": "passed", "test_index": 1, "duration_ms": 42},
    {
      "name": "MyGame.World.NullDeref",
      "status": "failed",
      "test_index": 2,
      "duration_ms": 120,
      "message": "LogOutputDevice: Error: Assertion failed: IsValid(Target)",
      "stack_trace": [
        "UnrealEditor-MyGame.dll!AMyCharacter::HandleHit() [MyCharacter.cpp:187]",
        "UnrealEditor-Engine.dll!AActor::ReceiveHit()",
        "UnrealEditor-Engine.dll!FTickFunction::ExecuteTick()"
      ],
      "related_files": ["MyCharacter.cpp:187"]
    }
  ]
}
```

### Expected classification

| Test | Class | Reasoning |
|------|-------|-----------|
| `MyGame.World.NullDeref` | **runtime** | Non-empty `stack_trace`; `message` matches `Assertion failed:` pattern (§2.2). No compile markers. |

### Expected tool calls (iteration 1)

```
ue_fix_errors(operation="crash_analysis", params={
  "stack_trace": ["UnrealEditor-MyGame.dll!AMyCharacter::HandleHit() [MyCharacter.cpp:187]", "..."],
  "message":     "LogOutputDevice: Error: Assertion failed: IsValid(Target)",
  "related_files": ["MyCharacter.cpp:187"]
})
# → suggests adding IsValid guard; outcome: "applied"
# No rebuild needed (no compile class in this iteration)
```

### Iteration 2

- Run tests again → 6 passed, 0 failed → exit `SUCCESS`.

### Expected fix-history report

```json
{
  "run_id": "2026-04-18T13:10:22Z",
  "verdict": "SUCCESS",
  "iterations": 2,
  "fixes": [
    {"iter": 1, "test": "MyGame.World.NullDeref", "class": "runtime",
     "op": "crash_analysis", "outcome": "applied"}
  ],
  "regressions": []
}
```

> **Note**: If the same test crashes with the same `stack_trace` in iteration
> 2, the loop exits `STAGNATION` — the fix did not stick. User should
> investigate manually.

---

## Example 3: Assertion failure → report only (opt-in required)

### Input (fixture 3: `pure_assertion.json`)

```json
{
  "passed": 3,
  "failed": 2,
  "tests_run": 5,
  "total_assertions": 10,
  "assertions_passed": 8,
  "assertions_failed": 2,
  "results": [
    {"name": "MyGame.Math.Add", "status": "passed", "test_index": 1, "duration_ms": 1},
    {
      "name": "MyGame.Math.Multiply",
      "status": "failed",
      "test_index": 2,
      "duration_ms": 1,
      "assertions": [
        {"name": "TestEqual", "pass": false, "detail": "Multiply(3, 4) returned wrong value",
         "actual": 11.0, "expected": "12"}
      ]
    },
    {
      "name": "MyGame.Math.Divide",
      "status": "failed",
      "test_index": 3,
      "duration_ms": 1,
      "assertions": [
        {"name": "TestTrue", "pass": false, "detail": "Divide(10, 2) == 5 expected true",
         "expected": "true"}
      ]
    }
  ]
}
```

### Expected classification (default, no `--attempt-assertion-fix`)

| Test | Class | Action |
|------|-------|--------|
| `MyGame.Math.Multiply` | **assertion** | REPORT_ONLY |
| `MyGame.Math.Divide`   | **assertion** | REPORT_ONLY |

### Expected tool calls (iteration 1)

**None.** All failures classified as assertion → recorded to `fix_log` with
`op: "REPORT_ONLY"` and a note. No `ue_fix_errors` call, no rebuild.

### Iteration 2

Loop **exits immediately** because:
1. No fixes were applied → same failures on rerun.
2. `{(test, class)}` set identical to iteration 1 → stagnation check.

Verdict: `STAGNATION` (with REPORT_ONLY entries).

### Expected fix-history report

```json
{
  "run_id": "2026-04-18T14:05:41Z",
  "verdict": "STAGNATION",
  "iterations": 2,
  "fixes": [
    {"iter": 1, "test": "MyGame.Math.Multiply", "class": "assertion",
     "op": "REPORT_ONLY", "outcome": "skipped",
     "note": "Assertion auto-fix is off by default. Re-run with --attempt-assertion-fix to try."},
    {"iter": 1, "test": "MyGame.Math.Divide", "class": "assertion",
     "op": "REPORT_ONLY", "outcome": "skipped",
     "note": "Assertion auto-fix is off by default. Re-run with --attempt-assertion-fix to try."}
  ],
  "regressions": []
}
```

### Opt-in variant: `--attempt-assertion-fix`

With the flag, iteration 1 calls:

```
ue_fix_errors(operation="smart", params={
  "assertion_name": "TestEqual",
  "expected": "12",
  "actual": 11.0,
  "detail": "Multiply(3, 4) returned wrong value",
  "test_name": "MyGame.Math.Multiply"
})
```

Outcome depends on whether `ue_fix_errors` can guess the production-code bug
vs. test-code bug. Higher false-fix risk — review changes carefully.

---

## Example 4 (optional): Mixed classes in one run

When a run has failures of different classes, the loop fixes them **all in
the same iteration**, then rebuilds once (if any was `compile`), then reruns:

1. `FailA` (compile) → `ue_fix_errors(smart)`
2. `FailB` (runtime) → `ue_fix_errors(crash_analysis)`
3. `FailC` (assertion, default) → REPORT_ONLY
4. `needs_rebuild` = true → `ue_build_pipeline(build_editor)`
5. Loop back to iteration 2 → rerun.

If any compile-class failures persist and `iter == max_retries`, verdict is
`MAX_RETRIES`, not `STAGNATION` — because progress may have been made on other
classes.

---

## Self-test fixtures

The three fixtures above (`pure_compile.json`, `pure_runtime.json`,
`pure_assertion.json`) are the canonical classifier unit tests.
`/ue-test-autofix --self-test` parses this file's JSON code blocks and runs
the classifier on each, asserting:

- `pure_compile.json[2]` → `compile`
- `pure_runtime.json[1]` → `runtime`
- `pure_assertion.json[1]` and `[2]` → `assertion`

Fails loudly if any classification disagrees.
