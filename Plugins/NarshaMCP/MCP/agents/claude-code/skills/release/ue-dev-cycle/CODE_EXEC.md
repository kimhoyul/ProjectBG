<!-- SENTINEL: ue-dev-cycle-CODE_EXEC-MARKER-10454 -->

> **Loaded only when `--code-exec` is passed.**
> SKILL.md instructs Claude to `Read()` this file as the first Phase 2-CE
> action when the flag is detected. If the flag is absent, this file MUST
> NOT be loaded into context (verified by sentinel-string grep on the
> conversation transcript — see Issue #10454 Acceptance Contract).

**Skill**: [SKILL.md](SKILL.md) | **Reference**: [REFERENCE.md](REFERENCE.md) | **Examples**: [EXAMPLES.md](EXAMPLES.md)

---

## Phase 2-CE: CODE-EXEC ORCHESTRATION (opt-in via `--code-exec`, Issue #9581)

**Goal**: Replace the turn-based Phase 2-4 chain (5-15 tool calls per iteration)
with a single Claude-generated Python script that orchestrates the wrappers
in `MCP/tools/code_exec_wrappers/`. Token-economy goal: only the script's
final `print()` summary flows back into context (vs. raw tool result blobs).

> **Status**: PoC. Run an A/B vs. the default turn-based path before
> trusting it for production work. See `docs/research/CODE_EXEC_UE_DEV_CYCLE_AB.md`
> for the latest decision (`expand` / `hold` / `pivot`).

### Step 2-CE-a: Author the orchestration script

Write a Python script that uses the wrappers — `narsha.analyze`,
`narsha.fix`, `narsha.build`, `narsha.generate`, `narsha.grep`,
`narsha.read` — to drive
analyze → suggest_class → derive_class → preflight → build_and_fix → report
in a SINGLE process. Filter intermediate results inside the script; print
ONLY the structured summary the parent skill needs.

> **Why narsha.generate is in the chain**: arm A (turn-based) uses
> `ue_generate_code(derive_class)` for PDB-validated class generation.
> arm B must use the same path (via `narsha.generate.derive_class`) so the
> A/B measures the orchestration pattern only, not codegen quality
> divergence.

Save the script via the `Write` tool to:

```
<project_root>/Saved/CodeExec/<feature_slug>_<YYYYMMDD_HHMMSS>.py
```

Required script structure (template — adapt the body to the feature):

```python
"""Phase 2-CE orchestration for: <feature_description>"""
import json, sys, traceback

from MCP.tools.code_exec_wrappers import narsha

# One mutable shape for the entire run. The runner only sees what we print.
report = {
    "feature": "<feature_description>",
    "steps": [],          # appended per phase
    "error": None,        # set on exception; None = success
    "traceback": None,
}

try:
    with narsha.session() as s:
        # 1. Analyze — cheap reads, filter in-process
        symbols = s.analyze.search_symbols("<base_class>", limit=5)
        callers = s.analyze.find_callers("BeginPlay", limit=50)
        # ... in-process filter / summarize ...
        report["steps"].append({"phase": "analyze", "symbol_hits": len(symbols or [])})

        # 2. Architecture suggestion (dry-run, read-only) -> codegen
        suggestion = s.generate.suggest_class(
            description="<feature_description>",
            project_context="<from analyze>",
        )
        report["steps"].append({"phase": "suggest_class", "suggestion": suggestion})

        gen = s.generate.derive_class(
            base_class="<from suggestion>",
            class_name="<from suggestion>",
            features=[],   # add bundles like "gas_ability" if the feature warrants
        )
        report["steps"].append({"phase": "derive_class", "gen": gen})

        # 3. Preflight (static pre-compile check)
        preflight = s.fix.preflight()
        report["steps"].append({"phase": "preflight", "errors": len(preflight or [])})

        # 4. Build + auto-fix loop (the expensive bit)
        build = s.fix.build_and_fix(max_iterations=3, timeout=900)
        report["steps"].append({
            "phase": "build_and_fix",
            "ok": bool(build and build.get("success")),
            "iterations": build.get("iterations") if isinstance(build, dict) else None,
        })

        # 5. Stats (optional)
        stats = s.build.get_build_stats()
        report["steps"].append({"phase": "stats", "stats": stats})
except Exception as e:
    report["error"] = f"{type(e).__name__}: {e}"
    report["traceback"] = traceback.format_exc()[-2000:]

print(json.dumps(report, indent=2))
sys.exit(0 if report["error"] is None else 1)
```

### Step 2-CE-b: Execute the script via the runner

```python
# Bash invocation pattern — `runner_path` resolves under this skill's dir.
runner_path = ".claude/skills/ue-dev-cycle/scripts/code_exec_runner.py"
result = Bash(
    command=f"python {runner_path} --script {script_path} --timeout 1800",
    description="Phase 2-CE: run orchestration script"
)
# `result` is JSON on stdout: {ok, stdout, stderr, returncode, wall_secs, script_path}
parsed = json.loads(result.stdout.strip().splitlines()[-1])
```

### Step 2-CE-c: Retry-with-fix loop (max 3 iterations total)

```python
max_iterations = 3  # mirrors Phase 3+4 max
for iteration in range(max_iterations):
    parsed = run_runner(script_path)
    if parsed["ok"]:
        break
    # Surface stderr + the script's last summary attempt to the parent.
    # Author a NEW script that takes the failure context into account.
    error_context = {
        "previous_returncode": parsed["returncode"],
        "previous_stderr_tail": parsed["stderr"][-2000:],
        "previous_stdout_tail": parsed["stdout"][-2000:],
    }
    # Generate a new script_path with error_context injected as a comment
    # block at the top + an adapted recovery path. Loop.
else:
    # Fell through all iterations — emit a STAGNATION report and proceed
    # to Phase 6 SUMMARY with `phase_2_ce_status="failed"`.
    pass
```

### Outputs (consumed by Phase 5-6)

- `phase_2_ce_status`: `success` / `failed` / `stagnation`
- `phase_2_ce_summary`: parsed JSON from script stdout
- `phase_2_ce_iterations`: how many script iterations were needed
- `phase_2_ce_total_wall_secs`: sum of `wall_secs` across iterations

Phase 5 (TEST) and Phase 6 (SUMMARY) consume these as if they came from
Phase 3+4 — the rest of the skill is unchanged.

---

## Provenance

The Phase 2-CE protocol body above (between the `## Phase 2-CE` heading and this
`---` separator) is **byte-identical** to the original SKILL.md lines 177-315
prior to Issue #10454 extraction (139 lines, 5504 chars, verified). The sentinel
HTML comment, load-gate header note, sibling link bar, and this Provenance
section are the only additions made during the move.

**Related**: Issue #9581 (Phase 2-CE PoC), Issue #10454 (this file extracted from SKILL.md), Epic #10451 (SKILL.md hot-context bloat reduction)
