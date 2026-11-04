---
name: ue-scaffold
description: "Purpose: UE feature scaffolding with architecture design + code generation. Answers 'Build feature X' by analyzing requirements, suggesting architecture, and generating C++ classes with proper UE patterns. Auto-detects feature type (Character, GAS, Weapon, UI, AI). Triggers: 'scaffold', 'create feature', 'build', 'set up', 'make', '만들어줘', '기능 생성', '구현해줘', '스캐폴드', 'new feature', 'implement'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "feature description (e.g., 'melee combat character', 'inventory system', 'AI patrol enemy') or --self-test"
metadata:
  version: "1.6.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Scaffold — Feature Scaffolding

> **Reference (cold)**: [REFERENCE.md](REFERENCE.md) — FEATURE_PATTERNS keyword dict, full MCP call-budget table, Step 0 result variables, output-format template, error-recovery table, evaluation criteria, Step 4.5 history, feature→atom mapping, self-test.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the documented Step 0–5 workflow automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

Classify `$ARGUMENTS` (ask if empty), then execute Steps 0–5: discover, suggest, check conflicts,
generate, validate, and echo the generated `UCLASS`/`UPROPERTY`/`UFUNCTION` code in a ` ```cpp ` block.
Every scaffold request MUST reach Step 4 and call `derive_class` or `scaffold_class`; a
`suggest_class`-only/prose answer fails. In non-interactive runs, proceed without confirmation except
for the Step 3 overwrite decision (#17830).

---

## MCP Call Budget + Generation Limits

Use at most two `ue_generate_code` calls: `suggest_class`, then one generation operation. Do not
retry with varied parameters unless the user refines the request. Use `ue_read`/`ue_grep`, not
`ue_analyze_symbols` or `trace_hierarchy`. Full budget: [REFERENCE.md § MCP Call Budget](REFERENCE.md#mcp-call-budget-current-7131).

---

## Step 0: Discovery Pre-Exploration (Issue #6352)

ALWAYS use `ue_grep` then `ue_read` before `suggest_class`, unless the user supplied both an
explicit base class and output directory. Use the nearest class locally to resolve the supported
`class_name` and `base_class` arguments, detect name/path conflicts, and do not pass unsupported
context fields or call `trace_hierarchy`. Full decisions and result variables:
[REFERENCE.md § Step 0 Detail](REFERENCE.md#step-0-detail).

---

### Step 1: Classify Feature Request

Classify `$ARGUMENTS` into one feature type and Unreal base class using [REFERENCE.md §
FEATURE_PATTERNS](REFERENCE.md#feature_patterns-classification-dict); fall back to `character` /
`ACharacter` only when no stronger cue exists.

### Step 2: Architecture Suggestion (Dry-Run)

```python
ToolSearch("select:mcp__narshamcp__ue_generate_code")

# Dry-run: shows what WILL be generated without writing files. ALWAYS do this before generating.
suggestion = ue_generate_code(
    operation="suggest_class",
    class_name="<resolved class name>",
    base_class="<resolved Unreal base class>",
)
# Returns: recommended class name, base class, components, features, file list.
# Present the suggestion (class names, base classes, file paths, features, Build.cs deps), then PROCEED to Steps 3-4.
# Non-interactive / single-shot run: do NOT wait for confirmation — the dry-run is a preview, not the
# deliverable. The only stop is a destructive-overwrite prompt in Step 3 (Issue #17830).
```

### Step 3: Pre-Generation Checks

```python
# Check if target files already exist (for each file suggest_class recommended):
Glob(pattern="Source/**/MyHero.h")
Glob(pattern="Source/**/MyHero.cpp")
# If files exist: WARN with the path, ask overwrite / rename (MyHero2) / abort. Do NOT silently overwrite.
```

In a **non-interactive / single-shot** run, do not overwrite and do not stop at the conflict: choose
a conflict-free class name with a unique suffix (for example `MyHero2`), report the original path
and selected name, update both `class_name` and artifact paths, then continue to Step 4. This is a
single safe naming decision, not a generation retry.

### Step 4: Generate Code

Use **one** operation. Provide **all required parameters** on the first call.

- **Feature list non-empty** → use `derive_class`; it is the operation that expands supported
  feature atoms into generated code.
- **Feature list empty** → use `scaffold_class` only for a featureless class shell. Do not attach a
  `features` parameter to this operation.

```python
# A: Derive from existing class (common)
result = ue_generate_code(
    operation="derive_class", base_class="ACharacter", class_name="AMyHero",
    output_dir="Source/MyGame/Characters", module_name="MyGame",
    features=["gas_setup", "enhanced_input", "replication"],
)
# B: Scaffold a featureless class shell
result = ue_generate_code(
    operation="scaffold_class", class_name="AMyWeapon", base_class="AActor",
    output_dir="Source/MyGame/Weapons", module_name="MyGame",
)
```

**Exact per-type param examples** → [REFERENCE.md § Exact Param Examples](REFERENCE.md#exact-param-examples-by-feature-type).

> **Step 4.5 (ECA asset creation) REMOVED** (#11231 / Epic #11230) — Step 4 C++ scaffolding is the final automated output; BP/VFX/Material/UI authoring is manual or via Editor-RC skills. Detail: [REFERENCE.md § Step 4.5 Generation Path](REFERENCE.md#step-45-generation-path).

**Echo generated code (MUST).** Paste each generated header into your final response in a ` ```cpp `
block **first** (path-prefixed, from `generated.header.content` / `skeleton.header`). It **must show**
`UCLASS`/`UPROPERTY`/`UFUNCTION` (+ `BindWidget` meta for widgets); never just summarize.
**Re-check the block + macros before sending.**
[REFERENCE.md § Output Format](REFERENCE.md#output-format).

---

### Step 5: Post-Generation Validation

```python
Glob(pattern="Source/MyGame/Characters/MyHero.*")        # verify files exist
validation = ue_fix_errors(operation="dependency_check")  # missing modules / circular deps
```

## Final Response Contract (MUST)

When `ue_generate_code` returns code, keep the existing **generated-code block first** rule, then emit
this receipt from the actual suggestion, generation, file check, and validation responses:

```text
Status: <GENERATED|PARTIAL|FAILED>
Architecture: <feature type>; <GeneratedClass : UnrealBaseClass hierarchy>
Components / Features: <observed components/features, or missing>
Artifacts: <path -> generated symbol -> written|in-memory|missing>
UE Patterns: <UCLASS/USTRUCT, UPROPERTY, UFUNCTION, BindWidget actually present or missing>
Validation: <dependency_check result or exact reason not run>
Next Steps: <specific Build.cs/build/Editor integration still required>
```

- `GENERATED` requires the expected artifacts to exist at the reported paths and the required Unreal
  base type/reflection patterns to be present. An in-memory skeleton, missing file, missing requested
  feature, or failed validation is `PARTIAL`, even when the tool response itself says `success`.
- On empty/failed generation, do not invent a code block, file, symbol, component, or reflection
  macro. Report `FAILED` with the authoritative error and remediation instead.
- Hand-authored completion ideas may follow under `Suggested completion (not generated)`, but must
  never be mixed into the `Artifacts` or generated-code block.
- Report each generated class/file independently so one successful skeleton cannot hide another
  missing artifact.
- For GAS requests, use canonical symbols when reporting present **or missing** integration pieces:
  `UAbilitySystemComponent`, `IAbilitySystemInterface`, `InitializeAbilitySystem`/`GiveAbility`, and
  `UAttributeSet`. Do not replace them with a vague phrase such as "ability system component owner".
- For inventory requests, report whether the generated class is a `UActorComponent`, whether the
  `inventory_slot` atom produced inventory storage/slot data, and whether observable add, remove,
  capacity, and replication behavior exists. Missing requested behavior makes the result `PARTIAL`.

---

Output-format template, error-recovery table, and evaluation criteria → [REFERENCE.md](REFERENCE.md). Self-test: `/ue-scaffold --self-test` ([REFERENCE.md § Self-Test](REFERENCE.md#self-test)).
