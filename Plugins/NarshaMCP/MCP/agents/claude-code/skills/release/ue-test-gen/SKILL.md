---
name: ue-test-gen
description: "Purpose: One-shot UE Automation Test code generation from target class/function. Uses ue_generate_code(generate_test) for template-based test creation with correct macros, assertions, and flags. Fast single-pass alternative to ue-automation-test-builder. Triggers: '테스트 생성', '테스트 만들어줘', 'generate test for', 'create test for', '유닛 테스트 생성'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running"
argument-hint: "target class or function name (e.g., ACharacter::TakeDamage)"
metadata:
  version: "1.2.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Test Generator

**Priority**: High
**Goal**: One-shot generation of compilable UE Automation Test code from a target class or function
**Issue**: #5294, #6352, #7131, #10283 (intent-based multi-test mode)

---

## Auto-Execution: Non-Negotiable Tool-Call Gate (MUST)

> **CRITICAL — mirrors ue-scaffold's #17830 generation gate.** When this skill is loaded, Claude
> **MUST execute the Step 0 → Step 1 workflow automatically and actually CALL the MCP tool** — do NOT
> just display documentation or hand-author test code.
>
> **Non-negotiable generation gate.** A test-generation request — **however terse**
> ("ACharacter 테스트 생성해줘", "Generate test for TakeDamage") — is a **tool-backed code-generation**
> request: EVERY invocation MUST actually call `ue_generate_code(operation="generate_test")`. (Intent
> mode only: call `ue_analyze_symbols(operation="derive_test_intents")` FIRST, then one `generate_test`
> call per `framework_hint` group.) **Writing UE test code (`BEGIN_DEFINE_SPEC` /
> `IMPLEMENT_SIMPLE_AUTOMATION_TEST` / `IMPLEMENT_COMPLEX_AUTOMATION_TEST` / `TEST_CLASS`) from your own
> knowledge WITHOUT invoking the tool is a FAILED generation** — even if the macros and assertions look
> correct, and even if you narrate "I called ue_generate_code…". The authoritative PostToolUse hook
> records whether the tool truly ran; a self-reported call with no real `tool_use` is scored as a miss.
> A prose / general-knowledge answer is a **FAILED test-gen**. In a non-interactive run, do NOT pause
> for confirmation before generating.

---

## Purpose

Generate **complete, compilable** UE Automation Test code from a target class/function name.
This skill uses `ue_generate_code(generate_test)` for template-based generation — fast, deterministic, and accurate.

**Difference from `/ue-automation-test-builder`**:
- `/ue-test-gen` = **Fast one-shot** generation (seconds, template-based)
- `/ue-automation-test-builder` = **Interactive 6-phase cycle** (minutes, AI-guided analysis/execution/evaluation)

**5 Test Types Supported** (Issue #8184 — CQTest documented, was previously omitted from this table; #7600 added the generator, #8183 added the auto-decision matrix):

| Type | UE Macro | When to Use |
|------|----------|-------------|
| `simple` | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` | Pure unit tests, no world needed |
| `bdd_spec` (default) | `BEGIN_DEFINE_SPEC / END_DEFINE_SPEC` | Recommended — Given/When/Then |
| `complex_latent` | `IMPLEMENT_COMPLEX_AUTOMATION_TEST` + latent commands | Multi-frame async tests (5.7-) — for 5.8+ prefer **`cqtest`** + TestCommandBuilder |
| `cqtest` | `TEST_CLASS` / `TEST_METHOD` (CQTest plugin, in `Private`Build.cs deps) | JUnit-style class+method tests. **Issue #8187 / #8183 Phase 1 auto-decision matrix** routes pattern signals (Replicated → `NETWORK_TEST_CLASS`, Timer → `TestCommandBuilder`, A* → `FActorTestSpawner`, U* → `TObjectBuilder`) into `#if`-guarded 5.8+ emission with 5.7 fallback. Optional `engine_version` + `apply_build_cs` MCP params control Build.cs auto-mod. **Phase 4 (Issue #11359)** adds operator-supplied `base_class` / `lifecycle` / `tags` args for macro variants (`TEST_CLASS_WITH_BASE`, `BEFORE_ALL`/`AFTER_ALL`, `TEST_CLASS_WITH_TAGS`). |
| `functional` | `AFunctionalTest` subclass | Level-based actor tests |

---

## ⚠️ Final Response Contract (MUST — eval-gated)

Your **final chat message MUST begin with a fenced ` ```cpp ` block** copied from the real
`generate_test.cpp_code` seed (failure/empty output is the only exception). Paste the **complete,
contiguous, unmodified** seed; token lists, non-contiguous excerpts, ellipses, or synthetic comments
do not count as generated code. It **MUST literally include**:

- one test-class macro: `BEGIN_DEFINE_SPEC` **or** `IMPLEMENT_SIMPLE_AUTOMATION_TEST` **or** `IMPLEMENT_COMPLEX_AUTOMATION_TEST` **or** `TEST_CLASS`, **and**
- one assertion macro appropriate to the test type: `TestEqual` / `TestTrue` / `TestNotNull` /
  `TestNearlyEqual`, or CQTest `ASSERT_THAT` / another returned `ASSERT_*` macro.

Immediately after the block, label it
`Provenance: complete unmodified tool-returned seed: <cpp_path>`. If Step 2 later
injects intents or enriches the skeleton, that transformed code is an
`assistant-enriched final artifact`, never a tool-returned block.

Then emit this receipt from authoritative tool fields and any observed transformation/write result:

```text
Status: <GENERATED|PARTIAL|FAILED>
Target: <test_target>
Derivation Mode: <single-shot generate_test|derive_test_intents|supplied intents|zero-intent fallback>
Intent Coverage: <each input intent -> group -> preserved test-case name/assertion_template evidence>
Intent Coverage Count: <reported>/<requested>
Framework Groups: <hint -> type -> macro -> path -> explicit status -> assertion evidence -> verification>
Outputs: <cpp_path -> written|in-memory|missing>
Transformations: <none|intent injection|assertion/edge-case enrichment -> final artifact path/state>
Assertions: <assertions_generated and observed Test*/ASSERT_* macros, or missing>
Verification: <build/run result or exact reason not run>
```

- List every framework group separately. In intent mode, preserve the canonical
  `framework_hint -> test_type` mapping and do not report global success if any group failed or
  returned empty code.
- List **every input intent separately** under `Intent Coverage`; naming only the framework groups is
  insufficient. Confirm whether its `description` became a test-case name and its
  `assertion_template` became the test body, or identify the exact loss.
- Preserve each supplied intent label **verbatim before** any derived case names (including `x2` or
  spaces). Do not silently rename `UPropertyReplicated (NetworkAutomation, x2)` into invented intent
  labels.
- Expand multiplicity into one numbered receipt row per requested case. For example, an `x2` request
  must produce two rows such as `I2: UPropertyReplicated (1/2)` and
  `I3: UPropertyReplicated (2/2)`; a single combined `x2` row does **not** prove coverage of both
  requested intents. End the section with `Intent Coverage Count: <reported>/<requested>`.
- If the input omits `assertion_template` and the generator returns a generic assertion, label it
  `tool-generated fallback assertion (not a supplied intent template)`. This reports authoritative
  output without pretending the caller supplied the template or the assistant authored it.
- Repeat `supplied assertion_template=ABSENT` on every numbered intent row and on every framework
  group row whose inputs omitted templates. A global statement or an unlabeled `fallback assertions`
  shorthand is insufficient provenance for a mixed-framework result.
- For every group, distinguish `tool reported assertions=N` from `complete seed shows observed
  <macro>` and include both `status=GENERATED|PARTIAL|FAILED` and
  `verification=PASS|FAILED|NOT RUN`; do not present those counts as equivalent. When the reported
  count and observed code disagree (for example `assertions_generated=0` but the returned code
  contains `ASSERT_THAT`), state `assertion-count mismatch` and make that group at least `PARTIAL`.
- Use one compact row for every framework group with this exact field order:
  `G#: <hint> -> <type>; seed=<exact cpp_path>; final=<exact path|NONE>; transform=<none|intent
  injection|enrichment>; status=<GENERATED|PARTIAL|FAILED>; assertions=<reported + observed>;
  verification=<PASS|FAILED|NOT RUN>`. Do not replace paths with `all paths`, `four paths`, or another
  aggregate shorthand.
- The `ue-test-gen` judges inspect up to 20,000 response characters (the evaluator's bounded ceiling).
  Keep the receipt visible by
  placing it immediately after the first complete seed and its provenance line; do not shorten code
  into tokens to move the receipt earlier.
- For multi-framework output, use this exact order: (1) complete seed A in a fenced `cpp` block,
  (2) `Provenance: complete unmodified tool-returned seed: <cpp_path>`, (3) the receipt, and
  (4) complete seed B plus its provenance line. Then emit every remaining complete seed. At least two
  distinct complete seed files must be present for a multi-framework set; a macro/assertion snippet
  is not an output. Keep `Intent Coverage` (all numbered rows and the count) before
  `Framework Groups` in the receipt.
- If Step 2 injected intents, enriched assertions, or wrote different bytes, emit the **complete
  resulting file** after the seed evidence as
  `Provenance: assistant-enriched final artifact: <path>; write=<written|in-memory|missing>`. Never
  omit the actual final artifact or relabel transformed code as tool-returned.
- If `derive_test_intents` returns zero intents, explicitly use `Derivation Mode: zero-intent
  fallback`; never claim the fallback file was seeded from `assertion_template` records.
- If `derive_test_intents` fails (for example, the PDB detail store is unavailable), do **not** stop
  after reporting the analysis error. When the requested `ClassName::Function` still identifies a
  target, invoke one real single-shot `ue_generate_code(operation="generate_test")` call using the
  default `bdd_spec` type. Report `Derivation Mode: zero-intent fallback (derive failed: <error>)` and
  aggregate `Status: PARTIAL`; the fallback is tool-generated but has no intent/template provenance.
- `GENERATED` requires complete non-empty returned/final code plus the expected macro and at least one
  semantic assertion tied to an observable result or post-state. A missing file
  write, unverified build, TODO-only skeleton, truncated intents, or failed group is `PARTIAL` and the
  missing evidence must be named.
- If `generate_test` fails or returns empty `cpp_code`, the failure rule overrides the normal
  code-block-first rule: do **not** fabricate a C++ block. Start with `Status: FAILED`, include the
  exact error/empty field, and give a concrete remediation.
- Before ending, re-read the response: every shown code block must have explicit provenance. A
  tool-returned seed block is the complete exact `cpp_code`; intent-injected/enriched code is
  labeled `assistant-enriched final artifact` and includes its actual write state. Every status,
  output path, mapping, transformation, and assertion count must agree with the corresponding tool or
  file-operation result.

Pull the seed code from `generate_test`'s `cpp_code` field (Step 1) — **the opening ` ```cpp ` block
MUST contain the complete `cpp_code` returned by a real `ue_generate_code(generate_test)` invocation,
not excerpts or test code you wrote yourself.** Step 2 may transform that seed, but the complete resulting block must be
labeled `assistant-enriched final artifact` rather than tool-returned. Echoing hand-authored macros without first invoking the tool fails the eval's tool-call
gate even when the ` ```cpp ` block and macros are present (self-reporting the call ≠ invoking it).
**Before ending your turn, re-read your drafted message: if you have not actually invoked `generate_test`,
invoke it now; if the message has no ` ```cpp ` block, or the block is missing a macro above, re-emit
the full block.** Summarizing the result instead of pasting the code is a failure — the actual test
text must be in the chat, not only written to disk. (Enforced by the output-quality eval, #16465/#16691.)

---

## Trigger Phrases

### Korean
- "테스트 생성", "테스트 만들어줘"
- "유닛 테스트 생성", "테스트 코드 만들어"
- "AMyCharacter 테스트 생성해줘"
- "TakeDamage 테스트 만들어"

### English
- "generate test for X", "create test for X"
- "generate unit test", "create automation test for"
- "test code for AMyCharacter"

### Should NOT Activate
- "자동화 테스트 만들어" → `/ue-automation-test-builder` (interactive cycle)
- "오토메이션 테스트" → `/ue-automation-test-builder`
- "테스트 반복 50번" → `/test-iterate` (edge case iteration)
- "테스트 실행" → `/test-run` (execution, not generation)

---

## MCP Call Budget (Issue #7131, #10283)

> **Single-shot mode target: 2 core MCP calls per invocation.**
> **Intent mode target (#10283): 1 + N core MCP calls** where N = distinct
> `framework_hint` count from the input intent set (capped at 5).

| Tool | Max Calls (single-shot) | Max Calls (intent mode) | Notes |
|------|---|---|-------|
| `ToolSearch` | **1** | **1-2** | Full name: `select:mcp__narshamcp__ue_generate_code` (intent mode also fetches `mcp__narshamcp__ue_analyze_symbols` if `--intents-from` is used) |
| `ue_analyze_symbols` | **0 — DO NOT USE** | **0-1** | Single-shot: `Read` provides signatures. Intent mode: **1 call to `derive_test_intents`** when `--intents-from <FQN>` is used (caller may also pre-cook intents and skip this) |
| `ue_generate_code` | **1** | **N (≤ 5)** | `generate_test` — one call per distinct `framework_hint` group |
| `ue_grep` (MCP) | **0-1** | **0-1** | Fallback only — if `Glob` can't find the header file |
| `Grep` (built-in) | **0 — DO NOT USE for target analysis** | same | `Read` header/cpp directly instead |
| `Read` (built-in) | **2-3** | **2-3** | Header + cpp + optional existing test pattern |

**Why** (single-shot): `ue_analyze_symbols(get_methods)` returns 500-4000 tokens for a full class.
`Read(header)` returns exact signatures for the target function in ~100-200 tokens.

**Why N ≤ 5 in intent mode**: `TestFramework` is a 5-variant enum
([analysis_symbols_test_impact.rs:51-57](../../../MCP/native/mcp_server/src/tools/analysis_symbols_test_impact.rs#L51-L57))
— `SimpleAutomation`, `ComplexAutomation`, `NetworkAutomation`, `Spec`, `CQTest`.
A single function rarely emits more than 2-3 distinct hints. Truncation
upstream caps the input at 10 intents per
[`MAX_INTENTS_PER_FUNCTION`](../../../MCP/native/mcp_server/src/tools/codegen/test_intent.rs#L42).

---

## Step 0: Discovery via Read (Issue #6352, #7131)

```text
Step 0: Discovery  →  Step 1: Generate  →  Step 2: Write & Verify
(Read header/cpp)     (ue_generate_code)    (Write + optional build)
```

**Target**: `$ARGUMENTS`

> If `$ARGUMENTS` is empty, ask the user what class or function to generate tests for.
> **Purpose**: Use NarshaMCP `ue_grep` + `ue_read` to get function signatures from indexed
> header/cpp files without relying on a shell or workspace file-read policy. Use `ue_glob` only to
> discover existing test-file conventions.

### Bare Function Resolution

If the target is a bare function name without `::`, call `ue_grep(query="<FunctionName>",
domain="source")`, read the best owning-class match with `ue_read`, and resolve the unique owning class
before generation. Canonicalize the target as
`ClassName::FunctionName`. If ownership is ambiguous, narrow the search or ask the user; never pass
the bare function name as though it were a class name.

### When to Run Step 0

- **ALWAYS** run Step 0 before Step 1 (Generate)
- Step 0 provides signatures needed for type-specific assertions

### Step 0 Workflow

```python
# 0a. Find the target class/function in indexed source
ue_grep(query="<ClassName or FunctionName>", domain="source", limit=20)

# 0b. Read the resolved class/function to get signatures and source location
ue_read(identifier="<ClassName::FunctionName or ClassName>")
# Extract: parameter types, return types for target function(s)
# This enables type-specific assertion selection:
#   float → TestNearlyEqual (with tolerance)
#   bool → TestTrue / TestFalse
#   int32 → TestEqual
#   FVector → TestEqual (UE has overloads)
#   AActor* → TestNotNull

# 0c. Read the owning class when function detail does not include implementation paths
ue_read(identifier="<resolved owning class>")
# Extract: branching logic, edge cases, dependencies
# This helps generate meaningful test scenarios

# 0d. (Optional) discover existing test files for project patterns
ue_glob(pattern="Source/**/Tests/**/*.cpp", domain="source", limit=10)
# If found, ue_read one indexed test symbol/file identifier to match conventions
```

**Token Savings**: targeted `ue_grep` + `ue_read` avoids broad symbol dumps and shell-policy retries.

---

### Step 0 Result Variables (downstream 참조용)

Step 1에서 아래 변수를 참조하여 generate_test 호출 품질을 높입니다:

- `step0_target_exists` — 타겟 존재 여부 (false면 즉시 종료 + 유사 이름 제안)
- `step0_signatures` — Read에서 확인한 함수 시그니처 (파라미터 타입 포함)
- `step0_source_path` — 소스 파일 경로 (output_dir 자동 결정 근거)
- `step0_test_patterns` — 프로젝트 기존 테스트 패턴 (있으면 참고)

### Step 0 Decision Logic

```text
Step 0 result → Decision:
  Source match + signature extracted  → Pass signature context to generate_test
  Target not found via ue_grep         → Report the searched scope; do not invent an owner
  Source path available               → Auto-detect output_dir from source location
  Existing tests found                → Match project test naming/structure conventions
```

---

## Phase 1.5: Intent-Based Mode (Multi-Test Generation, Issue #10283)

> **Activation**: this mode replaces Step 0 + Step 1 when **either** of
> these is true on entry:
>
> 1. The caller passes a `TestIntent[]` JSON array as `intents=` arg, **or**
> 2. The caller passes `--intents-from <ClassName::FunctionName>` (skill
>    invokes `derive_test_intents` itself first).
>
> Otherwise, fall through to the standard "2-Step Workflow" below — the
> single-shot path is **unchanged** (backward compat for `tp-01..tp-20`
> + `QUAL-01..02` eval cases).

### Why this mode exists

Sub-issue 2 of Epic #8186 ([#10282](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/10282))
shipped the **Test Intent Mapping Layer** as MCP op
`ue_analyze_symbols(operation="derive_test_intents", function="...")`.
It returns a priority-sorted list of `TestIntent` records — each one is
an actionable test idea distilled from one detected source pattern (8
patterns: `NumericParam`, `PointerParam`, `UPropertyReplicated`, …) or
one control-flow branch (`if`/`case`/`early return`). Intent mode lets
this skill consume that output: **N intents → N test cases in 1-3
grouped files**, instead of the operator hand-driving N separate
single-shot invocations.

Upstream handler reference:
[`analysis_symbols_test_intents.rs`](../../../MCP/native/mcp_server/src/tools/analysis_symbols_test_intents.rs#L48-L211).
Response shape (the contract this skill consumes):

```json
{
  "success": true,
  "operation": "derive_test_intents",
  "_version": 1,
  "function": "ACharacter::TakeDamage",
  "intents": [
    {
      "description": "Damage = MIN (int32) — boundary case",
      "assertion_template": "TestEqual(\"Damage=MIN\", subject->Damage, /* expected */ MIN)",
      "framework_hint": "SimpleAutomation",
      "priority": "P1",
      "source_pattern": { "kind": "pattern", "name": "NumericParam" },
      "change_driven": false
    }
  ],
  "summary": {
    "intent_count": 4,
    "truncated": false,
    "patterns_detected": 1,
    "code_paths": 0,
    "extraction_method": "pdb"
  }
}
```

### Framework Hint Mapping

The 5 `framework_hint` values map to `ue_generate_code(generate_test).test_type`:

| `framework_hint` (JSON) | `test_type` | UE macro family |
|---|---|---|
| `SimpleAutomation` | `simple` | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| `ComplexAutomation` | `complex_latent` | `IMPLEMENT_COMPLEX_AUTOMATION_TEST` + `DEFINE_LATENT_AUTOMATION_COMMAND` |
| `NetworkAutomation` | `complex_latent` | Same as Complex (server↔client sync needs Latent timing) |
| `Spec` | `bdd_spec` | `BEGIN_DEFINE_SPEC` / `END_DEFINE_SPEC` |
| `CQTest` | `cqtest` | `TEST_CLASS` / `TEST_METHOD` |

> **Operator-friendly aliases**: some docs use `Simple` / `ComplexLatent` /
> `Spec` / `CQTest` informally. The actual JSON values are the left
> column above. If a paste-in JSON has `"framework_hint": "ComplexLatent"`,
> normalize it to `ComplexAutomation` before mapping. Full table also
> lives in [REFERENCE.md](REFERENCE.md#framework-hint-mapping).

### Intent Dispatch Loop (pseudocode)

> **This loop is REAL `generate_test` calls — one per framework_hint group, not an illustration.**
> After `derive_test_intents`, you MUST invoke `ue_generate_code(operation="generate_test")` **once for
> EACH framework_hint group** (N distinct groups → N real calls) and emit each returned `cpp_code`.
> Deriving the intents and then hand-authoring the test code — **or calling `generate_test` only once
> for multiple groups** — is a **FAILED generation**: the eval requires ≥1 real `generate_test` call per
> group, ≥2 distinct `.cpp` outputs for a multi-framework set, with each intent's `description` preserved
> as a test-case name and its `assertion_template` as the test body. `derive_test_intents` alone (no
> `generate_test`) is a FAILED test-gen.

```text
INPUT_RESOLUTION:
  if --intents-from <ClassName::Func>:
    response = ue_analyze_symbols(
        operation="derive_test_intents",
        function="<ClassName::Func>"
    )
    if not response.success:
      WARN response.error + response.hint
      target_class = ClassName from the requested function
      fall through to single-shot Step 0 + Step 1
      # MUST call generate_test once; final status remains PARTIAL because intent derivation failed
    intents = response.intents
    target_class = ClassName from response.function
    if response.summary.truncated:
      WARN "Upstream capped at MAX_INTENTS_PER_FUNCTION=10 — additional
            intents dropped (see #10282 cap policy)"

  elif intents JSON pasted:
    intents = parsed JSON array
    # Best-effort target class inference; may require explicit `test_target` arg
    target_class = caller-supplied or inferred from first intent's description

  if intents is empty (no patterns detected upstream):
    fall through to single-shot Step 0 + Step 1 with a warning

DISPATCH (≤ 5 calls to ue_generate_code):
  groups = group_by(intents, key=lambda i: i.framework_hint)

  for framework_hint, group_intents in groups:
    test_type = MAP[framework_hint]   # see Framework Hint Mapping
    test_name = f"F{target_class}{framework_hint}MultiTest"

    cpp = ue_generate_code(operation="generate_test", params={
      "test_target": target_class,           # whole-class scope
      "test_type": test_type,
      "test_name": test_name,
      "module_name": <inferred>,
      "output_dir": <inferred>,
    })

    # Inline injection: each intent → one test body in the generated skeleton.
    #   Spec        → one Describe/It per intent
    #   CQTest      → one TEST_METHOD per intent
    #   complex_lat → one ADD_LATENT_AUTOMATION_COMMAND per intent
    #   simple      → one TestX(...) line per intent (single RunTest body)
    cpp.cpp_code = inject_intents(cpp.cpp_code, group_intents, test_type)

    Write(cpp.cpp_path, cpp.cpp_code)
```

### Trust Boundary: framework_hint vs `/ue-test-framework-advisor`

This dispatch loop **trusts** `framework_hint` from
`derive_test_intents` and does **not** re-invoke
`/ue-test-framework-advisor` per intent. Justification:

- The advisor is the **Phase 0 "user is unsure"** entry point.
- Once intents exist, the framework choice has already been baked in by
  the per-pattern generators in
  [`test_intent.rs:262-440`](../../../MCP/native/mcp_server/src/tools/codegen/test_intent.rs#L262-L440)
  (e.g. `intents_from_replicated` always picks `NetworkAutomation`,
  `intents_from_timer` always picks `ComplexAutomation`).
- Re-running the advisor would either no-op (advisor agrees) or
  contradict the rule engine — the second case is a #10282 bug to file
  against the rule engine, not patched in this skill.

If you want the advisor's recommendation independently, invoke
`/ue-test-framework-advisor <Class>::<Func>` separately before this
skill — it's still the correct Phase 0 entry point.

### Token Budget Guardrails

- Upstream `MAX_INTENTS_PER_FUNCTION = 10`
  ([source](../../../MCP/native/mcp_server/src/tools/codegen/test_intent.rs#L42)) —
  excess intents are dropped before they reach this skill.
- `summary.truncated == true` → SURFACE the warning so the operator
  knows N more were available.
- Practical recommendation: most functions yield 2-5 intents across 1-2
  framework groups → 1-2 `generate_test` calls. The 5-call ceiling only
  trips on highly polymorphic functions (e.g. RPC + replicated UPROPERTY
  + timer + virtual override + numeric param all in one body).

---

## 2-Step Workflow (with Step 0) — Single-Shot Mode (default)

```text
Step 0: Discovery  →  Step 1: Generate  →  Step 2: Write & Verify
(Read header/cpp)     (ue_generate_code)    (Write + optional build)
```

### Step 1: Generate Test Code

> **This is a real call you must make, not an illustration.** The many `ue_generate_code(...)` blocks
> elsewhere in this skill (intent-dispatch loop, NETWORK_TEST_CLASS / Phase-4 / Build.cs examples) are
> reference examples; THIS one you MUST really invoke. Do not transcribe its `cpp_code` from memory.

```python
ToolSearch("select:mcp__narshamcp__ue_generate_code")

ue_generate_code(
    operation="generate_test",
    test_target="AMyCharacter::TakeDamage",
    test_type="bdd_spec",
    module_name="MyGame",
    output_dir="Source/MyGame/Tests",
    functions=["TakeDamage", "GetHealth"],
)
```

**Smart mode** also works — just provide `test_target`:
```python
ue_generate_code(operation="smart", test_target="AMyCharacter")
# Auto-routes to generate_test
```

> **Output gate (MUST — see [Final Response Contract](#️-final-response-contract-must--eval-gated)):**
> the moment `generate_test` returns, capture its `cpp_code` as the immutable tool seed. Your final
> message must begin with the complete unmodified seed inside a ` ```cpp ` block, followed by the
> provenance line. If Step 2 changes the seed, separately label and show the complete actual
> `assistant-enriched final artifact`; do **not** call transformed code tool-returned or assume
> "written to disk" is enough.

### Step 2: Write & Verify

1. Write the generated `.cpp` file to the project
2. Apply a **semantic assertion gate** to the seed and any enriched artifact. Assertion count alone is
   not evidence. Reject `TODO`, `TestTrue(..., true)`, `ASSERT_THAT(IsTrue(true))`, placeholder
   `Expected, Actual` comparisons, and assertions over an uninitialized fixture. At least one
   assertion must verify an observable result or post-state from the confirmed signature or
   implementation.
   - Enrich only from Step 0 evidence: parameter/return types, branches, side effects, and existing
     project test patterns. Do not invent behavior.
   - If no evidence-backed semantic assertion can be produced, keep the returned seed and report
     `Status: PARTIAL` with the missing behavior named.
3. Optionally verify compilation:
```python
ue_fix_errors(operation="build_and_fix", project_root="<project>")
```
4. Optionally run the test:
```python
ue_editor_automation(operation="run_automation_tests",
    test_name="MyGame.MyCharacter.TakeDamage")
```

---

## Parameters Reference

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `test_target` | string | Yes | — | Class or function: `"AMyCharacter"` or `"AMyCharacter::TakeDamage"` |
| `test_type` | string | No | `"bdd_spec"` | `simple`, `bdd_spec`, `complex_latent`, `cqtest`, `functional` |
| `test_name` | string | No | Auto-generated | Test class name (e.g., `"FMyCharacterSpec"`) |
| `test_flags` | string | No | Auto-detected | UE `EAutomationTestFlags` expression |
| `output_dir` | string | No | `"Source/Tests"` | Output directory for generated files |
| `module_name` | string | No | `"Game"` | Module name for test pretty name path |
| `functions` | array | No | `[]` | Specific functions to generate tests for |
| `base_class` | string | No | — | **[cqtest 5.8+]** Custom CQTest base class for `TEST_CLASS_WITH_BASE`. Must derive from `ICQTestBase` (or its template variant). Engine-class headers (`AActor`, `ACharacter`, `APawn`, …) are auto-included; project classes warn and require the operator to ensure header reachability. 5.7 `#else` arm falls back to bare `TEST_CLASS_WITH_FLAGS`. (Issue #11359) |
| `lifecycle` | string | No | — | **[cqtest 5.8+]** One of `before_all`, `after_all`, `both`. Emits static `BEFORE_ALL()` / `AFTER_ALL()` hooks for one-time setup/teardown shared across all TEST_METHODs. Single-method tests trigger an advisory warning. 5.7 `#else` arm omits the blocks (BEFORE_EACH still works per-method). (Issue #11359) |
| `tags` | string | No | — | **[cqtest]** Tag string `"[tag1][tag2]..."` (alphanumeric, `_`, `-`). Drives `TEST_CLASS_WITH_TAGS` for class-level filtering. 5.7 engine header silently drops the tag argument — no emission change needed. Invalid formats produce a warning and are dropped. (Issue #11359) |
| `method_tags` | string | No | — | **[cqtest]** Per-method tag string `"[tag1][tag2]..."` → `TEST_METHOD_WITH_TAGS` on each generated method. Composes independently with `tags` (class-level). 5.7 engine header silently collapses to bare `TEST_METHOD`. (Issue #11830) |

---

## Example: Phase 4 macro variants (cqtest 5.8+, Issue #11359)

Use `base_class` / `lifecycle` / `tags` to opt into the 5.8 CQTest macro variants:

**Input** — a fixture with custom base class + shared lifecycle + tag filter:
```python
ue_generate_code(
    operation="generate_test",
    test_type="cqtest",
    test_target="FMyHelper",
    functions=["TestA", "TestB"],
    base_class="ACharacter",     # → TEST_CLASS_WITH_BASE, auto-includes GameFramework/Character.h
    lifecycle="both",            # → BEFORE_ALL() + AFTER_ALL() blocks at top of body
    tags="[unit][slow]",         # → tag filter (BASE wins at the class header — TAGS is logged with a warning per #11359 scope)
)
```

**Generated cpp_code (5.8 branch, abbreviated)**:
```cpp
// Auto-generated CQTest (Issue #7600 + #8187)
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
#include "GameFramework/Character.h"
#endif

// CQTest decisions detected (#8187):
// - TEST_CLASS_WITH_BASE — operator-supplied base 'ACharacter' (Issue #11359 Phase 4)
// - Static lifecycle — BEFORE_ALL + AFTER_ALL (Issue #11359 Phase 4, 5.8+ only)
// - CQTest tags — class='[unit][slow]' (Issue #11359 Phase 4, 5.7 silently drops)
// Phase 4 macro variants (Issue #11359) emitted inline below; 5.7 fallback in #else arm.

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TEST_CLASS_WITH_BASE(FMyHelperCQTest, "Game.MyHelper", ACharacter)
{
    BEFORE_ALL() { /* TODO: shared setup */ }
    AFTER_ALL()  { /* TODO: shared teardown */ }
    TEST_METHOD(TestA) { /* … */ }
    TEST_METHOD(TestB) { /* … */ }
};
#else
TEST_CLASS_WITH_FLAGS(FMyHelperCQTest, "Game.MyHelper", ...) { /* bare body */ }
#endif
```

> Note: `test_name` is auto-derived from `test_target` via `auto_test_name()` unless overridden via the `test_name=` param.

**Returned `warnings`** include a transparency line. With both `base_class` and `tags` provided, the BASE + class-TAGS combination falls back to BASE-only at the class header — the 5 `_AND_*` combination macros (`TEST_CLASS_WITH_BASE_AND_TAGS` etc.) are deferred until operator demand appears. The warning reads roughly: *"Phase 4 macro variants emitted (Issue #11359): BASE=ACharacter, lifecycle=[BEFORE_ALL,AFTER_ALL], tags=[class=[unit][slow] — DROPPED at class header]. On UE 5.7 the #else arm falls back…"*. This lets the operator spot a 5.7 build that silently dropped lifecycle / tags AND the dropped class-tags. Note: `method_tags` is unaffected by this — it composes independently and always emits `TEST_METHOD_WITH_TAGS`.

**Conflict precedence**: when a Phase 1/2/3 cell fires (NETWORK_TEST_CLASS, CQTestAssetHelper, TObjectBuilder, or FActorTestSpawner), the Phase 4 args are **ignored** with a warning — those body shapes are incompatible with `TEST_CLASS_*` macro variants. To use Phase 4 features, remove the source patterns that trigger the earlier cells.

---

## Test Type Selection Guide

```text
Is it a pure C++ value check?           → simple
Need Given/When/Then structure?         → bdd_spec (recommended)
Does it require multiple frames/ticks?  → complex_latent
Does it need a level with actors?       → functional
```

---

## Example: NETWORK_TEST_CLASS auto-emission (cqtest 5.8+, Phase 2 — Issue #11357)

When the target class has `UPROPERTY(Replicated)` or `UPROPERTY(ReplicatedUsing=…)`,
the `cqtest` auto-decision matrix (Cell 1) fires and emits `NETWORK_TEST_CLASS` body
inline inside the 5.8 `#if` branch, with bare `TEST_CLASS` fallback in `#else`.

**Input**:
```python
ue_generate_code(
    operation="generate_test",
    test_type="cqtest",
    class_name="ALyraCharacter",   # header contains: UPROPERTY(Replicated) float Health;
    apply_build_cs=True,            # auto-adds CQTest + NetCore to Build.cs
)
```

**Generated `cpp_code` (5.8 branch, abbreviated)**:
```cpp
// Auto-generated CQTest (Issue #7600 + #8187)
#include "CQTest.h"
#include "CoreMinimal.h"
#include "LyraCharacter.h"
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
#include "Components/PIENetworkComponent.h"
#include "GameFramework/GameModeBase.h"
#endif

// CQTest decisions detected (#8187):
// - NETWORK_TEST_CLASS — UPROPERTY(Replicated) on 'Health' (#8187 cell 1)
// NOTE: NETWORK_TEST_CLASS body emitted inline below (Phase 2 — Issue #11357).

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
NETWORK_TEST_CLASS(FLyraCharacterReplicationTest, "Game.LyraCharacter.Replication")
{
    struct State : public FBasePIENetworkComponentState
    {
        ALyraCharacter* ReplicatedHealth = nullptr;
    };

    FPIENetworkComponent<State> Network{ TestRunner, TestCommandBuilder, bInitializing };

    BEFORE_EACH()
    {
        FNetworkComponentBuilder<State>()
            .WithClients(2)
            .AsDedicatedServer()
            .WithGameMode(AGameModeBase::StaticClass())
            .Build(Network);
    }

    TEST_METHOD(SpawnAndReplicate_Health_ReachesAllClients)
    {
        Network.SpawnAndReplicate<ALyraCharacter, &State::ReplicatedHealth>()
            .ThenServer([this](State& s) { ASSERT_THAT(IsNotNull(s.ReplicatedHealth)); })
            .ThenClients([this](State& s) { ASSERT_THAT(IsNotNull(s.ReplicatedHealth)); });
    }
};
#else
TEST_CLASS_WITH_FLAGS(FLyraCharacterReplicationTest, "Game.LyraCharacter.Replication", ...) { ... }
#endif
```

**Returned `warnings`** include a pointer-semantics transparency notice — if `Health`
is a value-typed UPROPERTY (int32 / float / struct), manually adjust the `State`
struct member type. The auto-generated body assumes actor-pointer semantics.

**Build.cs side-effect** (when `apply_build_cs=true`): `CQTest` + `NetCore` are
inserted into `PrivateDependencyModuleNames`. Idempotent — re-runs do not duplicate.

---

## Example: Build.cs Auto-Modification — Dry-run + Apply (Phase 5 — Issue #11528)

The cqtest response always carries a structured `build_cs_modifications` field
when the decision matrix produces required modules. Default is **dry-run**
(`apply_build_cs=false`) so an operator can review the diff before any file is
written. Set `apply_build_cs=true` to commit the change (creates a `.backup`
sibling file alongside the modified `.Build.cs`).

### Dry-run: see the diff without touching disk

```python
ue_generate_code(
    operation="generate_test",
    test_target="AReplicatedActor",
    test_type="cqtest",
    module_name="MyGame",
    output_dir="Source/MyGame/Tests",
    # apply_build_cs omitted → defaults to False (dry-run)
)
```

Response excerpt (the `diff` field uses a hardcoded `Build.cs` label
regardless of the actual filename — paired with `build_cs_path` it's
unambiguous):

```json
"build_cs_modifications": {
  "build_cs_path": "Source/MyGame/MyGame.Build.cs",
  "modules_added": ["CQTest", "NetCore"],
  "modules_already_present": [],
  "conditional_blocks_added": [],
  "diff": "--- a/Build.cs\n+++ b/Build.cs\n@@ -10,1 +10,1 @@\n-    PrivateDependencyModuleNames.AddRange(new string[] {  });\n+    PrivateDependencyModuleNames.AddRange(new string[] { \"CQTest\", \"NetCore\" });",
  "diff_truncated": false,
  "applied": false,
  "backup_path": null,
  "warnings": []
}
```

### Apply: write the patch (idempotent)

```python
ue_generate_code(
    operation="generate_test",
    test_target="AReplicatedActor",
    test_type="cqtest",
    module_name="MyGame",
    output_dir="Source/MyGame/Tests",
    apply_build_cs=True,
)
```

Response excerpt:

```json
"build_cs_modifications": {
  "build_cs_path": "Source/MyGame/MyGame.Build.cs",
  "modules_added": ["CQTest", "NetCore"],
  "modules_already_present": [],
  "conditional_blocks_added": [],
  "diff": "...",
  "applied": true,
  "backup_path": "Source/MyGame/MyGame.Build.cs.backup",
  "warnings": []
}
```

Re-running with the same input is a no-op — `modules_added` becomes empty,
`modules_already_present` lists both modules, `applied=false`, no backup
written. Safe to call from CI.

### 5.7-fallback case — Target.Version guard

When the project resolves to UE 5.7 (no `EngineAssociation` in `.uproject`,
no `ProjectVersion` in `Config/DefaultEngine.ini`) but the CQTest plan
requires a 5.8 feature (`UseNetworkTestClass`, `UseTestCommandBuilder`,
`UseActorTestSpawner`, `UseObjectBuilder`), modules are inserted inside a
guard block at the **end of the constructor** so 5.7 builds keep compiling:

```csharp
public MyGame(ReadOnlyTargetRules Target) : base(Target) {
    PrivateDependencyModuleNames.AddRange(new string[] {  });

    if (Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 8))
    {
        PrivateDependencyModuleNames.Add("CQTest");
        PrivateDependencyModuleNames.Add("NetCore");
    }
}
```

Response excerpt:

```json
"build_cs_modifications": {
  "modules_added": ["CQTest", "NetCore"],
  "conditional_blocks_added": ["if (Target.Version >= 5.8) { CQTest, NetCore }"],
  "applied": true,
  "warnings": []
}
```

After the project upgrades to 5.8+ (`EngineAssociation` bumped), the guard
block **stays in place** — `module_already_present` finds `"CQTest"` inside
the existing guard and short-circuits, so subsequent regenerations are a
no-op. The guard remains as harmless boilerplate (the condition becomes
trivially true on 5.8+). Operators wanting a cleaner `AddRange`-only form
must remove the guard block manually — auto-collapse is **not** implemented
and is tracked as a follow-up.

### Failure mode: no Build.cs found

If the test project has no `*.Build.cs` (or no `Source/` directory at all),
the response carries a populated `error` field and empty module lists:

```json
"build_cs_modifications": {
  "build_cs_path": null,
  "modules_added": [],
  "modules_already_present": [],
  "conditional_blocks_added": [],
  "diff": "",
  "diff_truncated": false,
  "applied": false,
  "backup_path": null,
  "error": "No Build.cs file found under /path/to/project/Source/"
}
```

The C++ test file is still generated (`cpp_code` is non-empty) — only the
Build.cs side-effect is skipped. The accompanying `warnings` array carries
the operator-facing summary line.

### Constructor anchor degrade (graceful)

If the target Build.cs uses a non-standard constructor signature (no
substring `: base(` — the literal anchor) the conditional-block insertion
is **skipped** with a warning rather than mangling the file. The plain
`AddRange` splice path still works. Operators can patch the conditional
block manually using the `diff` field as a guide.

---

## Output Format

The `generate_test` operation returns:

```json
{
  "success": true,
  "operation": "generate_test",
  "test_type": "bdd_spec",
  "test_target": "AMyCharacter::TakeDamage",
  "test_name": "FMyCharacterTakeDamageSpec",
  "cpp_code": "// ... complete test code ...",
  "cpp_path": "Source/Tests/MyCharacterTakeDamageSpec.cpp",
  "test_flags": "EAutomationTestFlags::EditorContext | ...",
  "pretty_name": "MyGame.MyCharacter.TakeDamage",
  "functions_tested": ["TakeDamage"],
  "assertions_generated": 3,
  "warnings": []
}
```

**Key**: Non-functional tests (simple, bdd_spec, complex_latent) produce a **single .cpp file** — UE automation test macros define entire classes inline.

### Final response: echo the generated test code

**Always show generated code, not metadata alone.** Follow the hot-path provenance order exactly:

1. opening fenced `cpp` block containing the complete unmodified tool seed (with no path prefix),
2. `Provenance: complete unmodified tool-returned seed: <cpp_path>`,
3. the compact receipt, and
4. every remaining complete seed plus any separately labeled complete
   `assistant-enriched final artifact` actually written or proposed by Step 2.

Never put `cpp_path` or another assistant-authored label inside a tool-returned seed block. The final
artifact must include its `BEGIN_DEFINE_SPEC` / `END_DEFINE_SPEC` / `IMPLEMENT_*` macro and `Test*`
assertions (`TestEqual`, `TestTrue`, `TestNotNull`, `TestNearlyEqual`), or CQTest `TEST_CLASS` /
`TEST_METHOD` plus its returned `ASSERT_*`, so the user sees the actual
test, while the receipt distinguishes seed, transformation, write state, and verification. Mirrors
the [EXAMPLES.md](EXAMPLES.md) output pattern and the
[Final Response Contract](#️-final-response-contract-must--eval-gated).

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Target class not found | Typo or missing PDB index | Use `ue_grep(query="<target>")` to search project symbols |
| generate_test returns empty | Unsupported test pattern | Fall back to `derive_class` with test template features |
| Compilation failure after generation | Missing includes or dependencies | Run `ue_fix_errors(smart)` to auto-resolve |
| Editor not running for test execution | Editor required for automation tests | Skip execution step, output code only |
