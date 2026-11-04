---
name: ue-dev-cycle
model: opus
description: "Purpose: UE C++ development loop automation (analyze → implement → build → fix → test → summary). Single command for code-build-test cycle with auto error fixing. Triggers: 'dev cycle', 'dev loop', '개발 루프', '개발 사이클', '빌드 루프', 'implement and build'."
argument-hint: ["feature description (e.g., 'AMyCharacter에 Health 추가', 'GAS 공격 어빌리티 시스템', 'create melee combat character')"]
metadata:
  version: "1.2.0"
  author: "Next-Stage-Inc"
  changelog: "1.2.0 — modularized into REFERENCE.md / CODE_EXEC.md / EXAMPLES.md siblings (Issue #10454, Epic #10451). Phase 2-CE moved to CODE_EXEC.md (loaded only when --code-exec passed). 1.1.0 — added --code-exec flag (Issue #9581 PoC, opt-in Python orchestration script alternative to turn-based Phase 2-4)."
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Dev Cycle — Development Loop Automation

**Issue**: #6730 | **Refactor**: #10454 (Epic #10451)
**Purpose**: Automate the UE C++ developer loop: analyze → implement → build → fix → test → summary

**Reference**: [REFERENCE.md](REFERENCE.md) (Output Format / Error Recovery / Anti-Patterns / Evaluation Criteria / Known Limitations) | **Examples**: [EXAMPLES.md](EXAMPLES.md) (6 walkthroughs) | **Code-Exec (opt-in)**: [CODE_EXEC.md](CODE_EXEC.md) (loaded only when `--code-exec` is passed)

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the 6-phase workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Initialize** — parse feature description, probe Editor availability
2. **Analyze** — discover project context, existing classes, target paths
3. **Implement** — generate C++ code via `ue_generate_code`
4. **Build + Fix** — build via `ue_build_pipeline`, auto-fix errors (max 3 iterations)
5. **Test** — generate automation test, execute if Editor available
6. **Summarize** — output Dev Cycle Report with generated files and next steps

---

## Auto-Trigger Phrases

### Korean
- "개발 루프 돌려줘", "개발 사이클 실행"
- "코드 빌드 테스트 자동화", "빌드 루프"
- "구현하고 빌드해줘", "코드 짜고 테스트"
- "이 기능 구현하고 빌드까지"

### English
- "Run dev cycle", "Development cycle"
- "Code build test", "Build loop"
- "Implement and build", "Implement and test"
- "Build this feature end to end"

### Should NOT Activate
- "만들어줘" (no build/test intent) -> `/ue-scaffold` (scaffolding only)
- "빌드 에러 해결해줘" -> `/ue-debug` (error diagnosis only)
- "테스트 생성해줘" -> `/ue-test-gen` (test generation only)
- "풀사이클", "full cycle" -> `/full-cycle` (internal issue lifecycle)
- "라이브 코딩 실패" -> `/ue-livecoding-fix` (hot reload fix only)

---

## STEP 0: INITIALIZE

```python
# 1. Parse feature description from $ARGUMENTS[0]
# REQUIRED: If empty, ask user: "What feature should I implement?"
feature_description = $ARGUMENTS[0]

# 2. Probe Editor availability
health = ue_check_health()
editor_available = health.editor.connected  # true/false
project_root = health.project_root

# 3. Parse options (optional)
# --skip-test     : Skip Phase 5 (test generation/execution)
# --build-only    : Skip Phase 2 (code already written, just build+fix)
# --from N        : Start from Phase N (resume interrupted cycle)
# --code-exec     : (Issue #9581 PoC) Replace Phase 2-4 with a single
#                    Claude-generated Python orchestration script.
#                    See CODE_EXEC.md (loaded only when this flag is set).
```

**Decision Tree**:
- `editor_available == true` -> Full pipeline (Live Coding + test execution)
- `editor_available == false` -> Reduced pipeline (UBT build + test generation only)
- `--build-only` -> Skip Phase 2, start at Phase 3
- `--code-exec` -> Run Phase 1 (ANALYZE) normally, then route to **Phase 2-CE** (see below) instead of the turn-based Phase 2-4 chain. Phase 5-6 still run after Phase 2-CE returns.

---

## Phase 1: ANALYZE

**Goal**: Gather project context to inform code generation.

```python
# Step 0 Discovery (lightweight pre-exploration)
# Extract key terms from feature description
ue_grep(query="<base class or keyword from description>", scope="source")
# If matches found:
ue_read(identifier="<discovered class>")

# Verify base class exists in PDB index
ue_analyze_symbols(operation="search_symbols", query="<base class>")

# Discover project structure
ue_grep(query="Build.cs", scope="source")
```

**Outputs**:
- `project_name`: from `ue_check_health`
- `existing_classes`: related classes found via `ue_grep`
- `source_dir`: project Source/ path
- `build_cs_path`: path to relevant Build.cs
- `base_class_verified`: true/false from symbol search

**Feature Complexity Auto-Detection**:

| Complexity | Signal | Phase 2 Behavior |
|------------|--------|-------------------|
| Simple | Single property/function add | Manual edit guidance, skip `derive_class` |
| Normal | Single class derivation | 1x `derive_class` call |
| Complex | GAS/Component system | 1x `derive_class` + features |
| Multi-class | 3+ classes mentioned | Multiple sequential `derive_class` calls |

**NEW-class decision rule (MUST run codegen)** — apply BEFORE picking a path. The discriminator is **whether the target class already exists** in Phase-1 `existing_classes`:
- **NEW class** → the request names a class in UE naming (`A*`/`U*`/`F*`/`S*`/`I*`/`E*`, e.g. `ADamageVolume`, `AHealthPickup`) that is **NOT** in `existing_classes`, OR explicitly asks to create a *new class* ("create / implement / make / build **a new** `<Class>`", "`<Class>` 클래스/액터 **만들어 / 생성 / 구현**"). → You **MUST** run Phase 2 Steps 2a–2d (`ue_generate_code` + `dependency_check`); the Simple Feature Path does NOT apply.
- **Modify existing** → the request adds a property/function to, or edits, a class **already in** `existing_classes` (e.g. "AMyCharacter에 float MaxHealth 프로퍼티 추가", "add a UPROPERTY to AExisting"). A bare add-a-member verb (`프로퍼티/함수 추가`, `add a UPROPERTY/function`) on an existing class is **NOT** a NEW-class request → take the Simple Feature Path.

This rule is phrasing-agnostic for the NEW-class case: the English "implement a new `<Class>` actor" and the Korean "`<Class>` 액터 만들어" both name a class absent from `existing_classes`, so both require codegen regardless of language.

---

## Phase 2: IMPLEMENT

**Goal**: Generate C++ code from the feature description.

### Step 2a: Architecture Suggestion (Dry-Run)

```python
suggestion = ue_generate_code(
    operation="suggest_class",
    description=feature_description,
    project_context="<from Phase 1 analysis>",
)
# Returns: recommended class name, base class, features, file paths
```

### Step 2b: Pre-Generation Conflict Check

```python
# Check if target files already exist
Glob(pattern="Source/**/<ClassName>.h")
Glob(pattern="Source/**/<ClassName>.cpp")
# If exists: WARN and skip (do not overwrite)
```

### Step 2c: Generate Code

```python
# For each class in the suggestion:
result = ue_generate_code(
    operation="derive_class",
    base_class="<from suggestion>",
    class_name="<from suggestion>",
    output_dir="<from suggestion>",
    features=["<auto-detected features>"],
)
# IMPORTANT: Sequential calls only — never parallel ue_generate_code
```

### Step 2d: Dependency Check

```python
ue_fix_errors(operation="dependency_check")
# Catches missing Build.cs modules before build
# If missing modules found: Add to Build.cs automatically
```

**Simple Feature Path** (modifying a class ALREADY in `existing_classes` — UPROPERTY add, function add, config change):
- **Precondition**: the target class already exists (in Phase-1 `existing_classes`) AND the request is NOT a NEW-class request per the rule above. A new-class request never reaches this path — it MUST run Steps 2a-2d.
- Skip Steps 2a-2c
- Use `Read` + `Edit` to modify the existing file directly
- Proceed to Phase 3

---

## Phase 2-CE: CODE-EXEC ORCHESTRATION (opt-in via `--code-exec`, Issue #9581)

> **If `--code-exec` flag is NOT set: skip this phase entirely; the default
> turn-based Phase 2 → Phase 3+4 chain handles implementation. Do NOT load CODE_EXEC.md.**
>
> **If `--code-exec` IS set:**
> 1. Call `Read(".claude/skills/ue-dev-cycle/CODE_EXEC.md")` as the first
>    Phase 2-CE action. Do NOT proceed without the protocol loaded.
> 2. Follow the orchestration-script procedure documented there
>    (Step 2-CE-a: author script → Step 2-CE-b: execute via runner →
>    Step 2-CE-c: retry-with-fix loop, max 3 iterations).
> 3. Phase 5 (TEST) and Phase 6 (SUMMARY) consume `phase_2_ce_*` outputs
>    produced by the runner — names unchanged.

For details on Phase 2-CE: [CODE_EXEC.md](CODE_EXEC.md)

---

## Phase 3+4: BUILD-FIX Loop

**Goal**: Build the project and auto-fix any errors. Interleaved loop.

```python
max_iterations = 3
iteration = 0
build_success = false
error_history = []

while iteration < max_iterations and not build_success:
    iteration += 1

    # ── BUILD ──
    # Detect new .cpp files that require full rebuild (LiveCoding can't detect new TUs)
    new_cpp = Bash("git diff --name-only --diff-filter=A HEAD | grep -c '\\.cpp$'")
    needs_full_rebuild = int(new_cpp) > 0

    if editor_available and needs_full_rebuild:
        # New .cpp: must stop editor (DLL lock), full UBT build, relaunch after (Issue #6782)
        build_result = ue_build_pipeline(operation="build_editor", auto_restart=true)
        # After build: relaunch editor with /launch-editor workflow
        # start "" "<engine>/Engine/Binaries/Win64/UnrealEditor.exe" "<project>.uproject" -UNATTENDED
        # Poll RC port 30010 for readiness
    elif editor_available:
        build_result = ue_build_pipeline(operation="build_editor")
    else:
        # Fallback: UBT via Bash (ue_fix_errors may timeout on large projects)
        # Build.bat LyraEditor Win64 Development <project>.uproject -WaitMutex
        build_result = Bash("Build.bat <Target> Win64 Development <project>.uproject")
        # Parse stdout for "Result: Succeeded" or "error C" lines

    if build_result.success:
        build_success = true
        break

    # ── FIX ──
    if editor_available:
        errors = ue_fix_errors(operation="smart")
    else:
        # Editor offline: parse UBT stdout directly (see REFERENCE.md § Known Limitations)
        # ue_fix_errors(scan_build_log) reads Editor log, not UBT output
        # Instead: read Bash build output for "error C", "fatal error" lines
        errors = parse_build_errors_from_bash_output(build_result.stdout)

    # Stagnation check: same errors as previous iteration
    current_errors = extract_error_signatures(errors)
    if current_errors == error_history[-1] if error_history else []:
        # STAGNATION: same errors repeating, manual intervention needed
        break

    error_history.append(current_errors)

    # Apply suggested fixes
    for fix in errors.suggestions:
        if fix.confidence >= 0.7:
            # Apply fix via Edit tool
            Edit(file_path=fix.file, old_string=fix.old, new_string=fix.new)

    # Loop back to BUILD
```

**Exit Conditions**:
1. `SUCCESS` — build passes cleanly
2. `MAX_ITERATIONS` — 3 attempts exhausted
3. `STAGNATION` — same errors repeat (can't auto-fix)

---

## Phase 5: TEST

**Goal**: Generate and optionally execute UE Automation Tests.

```python
# Skip if --skip-test flag set
if skip_test:
    test_status = "SKIPPED"
    goto Phase 6

# Step 5a: Generate test code
test_result = ue_generate_code(operation="generate_test", params={
    "test_target": "<generated class>::<key function>",
    "test_type": "bdd_spec",
    "output_dir": "Source/<Module>/Tests"
})

# Step 5b: Write test file
Write(file_path=test_result.cpp_path, content=test_result.cpp_code)

# Step 5c: Execute test (Editor ON only)
if editor_available:
    # Rebuild to include new test file
    ue_build_pipeline(operation="build_editor")

    # Run the generated test
    ue_editor_automation(operation="run_automation_tests",
        test_name=test_result.pretty_name)
    # Check results
    test_status = "PASS" or "FAIL"
else:
    test_status = "GENERATED (execution requires Editor)"
```

**If test execution fails**:
- Report failing assertions in Phase 6 summary
- Do NOT iterate test fixes (that's `/ue-automation-test-builder`'s job)

---

## Phase 6: SUMMARY

**Goal**: Output comprehensive Dev Cycle Report.

```python
# Verify generated classes exist in PDB index
for class_name in generated_classes:
    ue_analyze_symbols(operation="search_symbols", query=class_name)
```

**Output the report** — full template in [REFERENCE.md § Output Format](REFERENCE.md#output-format).

---

**Status**: Phase 1 MVP (modularized in v1.2.0 per Issue #10454)
**Related**: Issue #6730 (origin), Issue #10454 (this refactor), Epic #10451 (hot-context bloat reduction), `/ue-scaffold` (scaffolding only), `/ue-test-gen` (test only), `/ue-debug` (error diagnosis only)
**See also**: [REFERENCE.md](REFERENCE.md) | [EXAMPLES.md](EXAMPLES.md) | [CODE_EXEC.md](CODE_EXEC.md) (opt-in)
