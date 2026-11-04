---
name: ue-test-framework-advisor
description: "Purpose: Auto-select the best UE test framework for a target, then route to generation. Analyzes target characteristics (sync/async, world dependency, GAS, networking) and recommends Simple/BDD Spec/Complex+Latent/CQTest/Functional/Gauntlet. Routes to /ue-test-gen, /ue-automation-test-builder, or /ue-test-scenario. Triggers: '테스트 프레임워크', '어떤 테스트', '테스트 추천', 'test framework', 'which test type', 'best test for', 'recommend test'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "target class or function (e.g., 'AMyCharacter::TakeDamage', 'AMyCharacter', '--self-test')"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  issue: "#7600"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Test Framework Advisor

Auto-select the best UE test framework for a given target, then route to the appropriate generation skill.
This skill is **Phase 0** — it analyzes and recommends, never generates code directly.

## When to Use

- "ACharacter에 어떤 테스트가 좋을까?"
- "What's the best test framework for TakeDamage?"
- "테스트 프레임워크 추천해줘"
- User is unsure which of the 6+ UE test frameworks to use

## When NOT to Use (route directly)

- User says "테스트 생성/만들어" → `/ue-test-gen` (they already know what they want)
- User says "자동화 테스트 만들어" → `/ue-automation-test-builder`
- User describes a gameplay scenario → `/ue-test-scenario`

## Workflow

### Phase 1: Target Analysis

Analyze the test target using **built-in tools only** (0 MCP calls):

```
Step 1.1: Find target files
  Glob("*{ClassName}*.h") → Read header
  Glob("*{ClassName}*.cpp") → Read implementation

Step 1.2: Extract characteristics
  From header:
  - Inheritance: AActor? APawn? ACharacter? UObject? Plain struct/class?
  - UFUNCTION macros: BlueprintCallable? Server? Client? NetMulticast?
  - UPROPERTY: Replicated? ReplicatedUsing?
  - Subsystems: UAbilitySystemComponent? UInputComponent? UEnhancedInputComponent?

  From cpp:
  - Async patterns: FTimerHandle, FLatentActionInfo, Delegate bindings, AnimMontage
  - GAS: GameplayAbility, GameplayEffect, GameplayTag
  - World dependency: SpawnActor, GetWorld(), WorldContext
  - Tick usage: TickComponent, SetActorTickEnabled

Step 1.3: Detect project test patterns
  Glob("Source/**/Tests/**") → existing test files
  Grep("Build.cs", "GameplayAbilities|FunctionalTesting|Gauntlet|CQTest")
  Grep("Source/**/Tests/*.cpp", "IMPLEMENT_SIMPLE|BEGIN_DEFINE_SPEC|TEST_CLASS|IMPLEMENT_COMPLEX")
```

### Phase 2: Framework Selection (Decision Tree)

Apply rules **in order** — first match wins:

| # | Condition | Framework | test_type param |
|---|-----------|-----------|-----------------|
| 1 | Input is natural-language scenario (not class/function name) | — | Route → `/ue-test-scenario` |
| 2 | Target inherits `UGauntletTestController` or user mentions "cooked build", "package test" | Gauntlet | Route → `/ue-gauntlet-orchestration` |
| 3 | Target needs level with placed actors, map-specific setup, or inherits `AFunctionalTest` | FunctionalTest | `functional` |
| 4 | **NEW (Issue #8184)** — Target has `UPROPERTY(Replicated)` / `ReplicatedUsing=` or user mentions "리플리케이션", "network test", "multiplayer test" | **CQTest** `NETWORK_TEST_CLASS` (5.8+) / `complex_latent` (5.7) | `cqtest` |
| 5 | **CHANGED (Issue #8184)** — Target has async behavior: timers, latent actions, montages, AI BehaviorTree ticks, network replication waits | **CQTest + TestCommandBuilder** (5.8+) / `complex_latent` (5.7) | `cqtest` |
| 6 | Target is pure computation with no methods (single function, math utility) | Simple | `simple` |
| 7 | Target has 2+ testable methods (class with multiple public functions) | **CQTest** | `cqtest` |
| 8 | Default (none of the above) | **CQTest** | `cqtest` |
| 9 | **(Issue #11359 Phase 4 hint)** — Above rules picked `cqtest` AND user mentions any of: "shared setup base" / "fixture base class" / "공통 setup" → also pass `base_class=<FBase>`. Mentions "BEFORE_ALL" / "AFTER_ALL" / "shared lifecycle" → also pass `lifecycle=before_all\|after_all\|both`. Mentions "tag filter" / "category filter" / "[unit][slow]" → also pass `tags="[cat1][cat2]"`. 5.8+ only; 5.7 silently falls back. | **CQTest + Phase 4 args** | `cqtest` + `base_class` / `lifecycle` / `tags` |

> **Note (Issue #8184 — CQTest priority elevation)**:
> Epic ue5-main is migrating away from `Complex+Latent` toward CQTest's `TestCommandBuilder`
> `.Do().Until().Then()` chain (more concise, type-safe) and `NETWORK_TEST_CLASS` for replication
> tests. The `cqtest` test_type now triggers an **auto-decision matrix** in
> `ue_generate_code(generate_test, test_type=cqtest)` (Issue #8187 / #8183 Phase 1) that:
>
> 1. Detects `UPROPERTY(Replicated)` / Timer / Actor / UObject patterns from `pattern_detection`
> 2. Emits 5.8-only macros (`NETWORK_TEST_CLASS`, `TestCommandBuilder`, `FActorTestSpawner`,
>    `TObjectBuilder`) wrapped in `#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 &&
>    ENGINE_MINOR_VERSION >= 8)` guards with `5.7` fallback in `#else`
> 3. Surfaces required Build.cs `PrivateDependencyModuleNames` (`CQTest`, `NetCore`) — set
>    `apply_build_cs=true` to auto-modify
>
> 5.7 projects keep working via the `#else` fallback path. 5.8 macro body emission status:
>
> | Phase | Body emission | Status | Issue |
> |---|---|---|---|
> | **1** | `TestCommandBuilder.Do().Until().Then()` / `.WaitDelay()` / `.DoAsync<T>(action, callback)` chain in `TEST_METHOD` body | ✅ **Live** — auto-emitted when `TimerOrLatent` pattern fires (cell 2) or `TFuture<T>`/`TAsyncResult<T>` return type detected | #8183 / #11367 |
> | **2** | `NETWORK_TEST_CLASS` + `FBasePIENetworkComponentState`-derived State + `FPIENetworkComponent<State>` + `FNetworkComponentBuilder.WithClients/AsDedicatedServer/WithGameMode` chain + `SpawnAndReplicate<C, &State::M>().ThenServer(...).ThenClients(...)` body | ✅ **Live** — auto-emitted when `UPropertyReplicated` pattern fires (cell 1) | #11357 |
> | **3** | `FActorTestSpawner Spawner; Spawner.SpawnActor<T>()` body (cell 3) ∥ `TObjectBuilder<T>().SetParam(...).Spawn()` body (cell 4) ∥ `CQTestAssetHelper::FAssetFilterBuilder().WithPackagePath(...).Build() + GetBlueprintClass(...)` body (cell 8 — new `BlueprintDependency` detector) | ✅ **Live** — auto-emitted when `A*` Actor / `U*` non-Actor / Blueprint-asset-dep signals fire | #11358 (this PR) |
> | **4** | Macro variants (`TEST_CLASS_WITH_BASE`, `BEFORE_ALL`/`AFTER_ALL`, `TEST_CLASS_WITH_TAGS`, `TEST_METHOD_WITH_TAGS`) — operator args-driven | ✅ **Live** — emitted when operator passes `base_class` / `lifecycle` / `tags` (and no Phase 1/2/3 cell shadows them). 5.7 fallback omits lifecycle blocks and silently drops tags via engine header | #11359 |
>
> **Dispatch priority (Phase 3)**: `NETWORK_TEST_CLASS` (cell 1) > `CQTestAssetHelper` (cell 8) >
> `TObjectBuilder` (cell 4) > `FActorTestSpawner` (cell 3) > bare `TEST_CLASS_WITH_FLAGS`. AssetHelper
> wins over the spawner cells because Blueprint loading is orthogonal to actor vs non-actor — the
> helper is still needed even when the target is an Actor.
>
> **Cell 8 (BlueprintDependency) trigger signals** — OR-combined, any one fires the cell:
> - Function body contains a `"/Game/..."` package-path string literal (e.g.
>   `LoadObject<UClass>(nullptr, TEXT("/Game/Enemies/BP_Goblin"))`)
> - Function body calls `LoadClass<>` / `StaticLoadObject` / `StaticLoadClass` / `LoadObject<UClass>`
> - Sibling header has a `TSubclassOf<>` / `TSoftClassPtr<>` / `TSoftObjectPtr<>` UPROPERTY
> When cell 8 fires, `required_modules` gains `"AssetRegistry"` (Phase 5 / #11528 will auto-add to
> Build.cs; until then the generated `// TODO` comment surfaces the manual step).
>
> When a function's pattern matches **Phase 1** OR a class triggers **Phase 2**, emission lands inside the
> `#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)` guard
> while the `#else` branch retains the bare TEST_CLASS / TEST_METHOD body for 5.7 compatibility. Phase 1
> sync chains map `TimerKind::Delay` → `WaitDelay`, `SetTimer` / `SetTimerForNextTick` → `Until` poll;
> async chains require `TFuture<T>` / `TAsyncResult<T>` return type — single-token T extracts cleanly,
> nested generics (`TFuture<TArray<int32>>`) fall back to the sync chain so the file still compiles.
> Phase 2 NETWORK State member assumes `{class_name}*` actor-pointer semantics — value-typed UPROPERTYs
> (int32 / float / struct) require manual type adjustment, surfaced via a `warnings[]` channel hint.

**Confidence levels:**
- **HIGH**: Clear signal (e.g., inherits UGauntletTestController, has FTimerHandle)
- **MEDIUM**: Heuristic match (e.g., 3+ methods → bdd_spec)
- **LOW**: Default fallback

### Phase 3: Recommendation Presentation

Present structured recommendation:

```
=== Test Framework Recommendation ===

Target: {ClassName}::{FunctionName}
Detected: {inheritance}, {subsystems}, {async patterns}

Recommended: {framework} ({test_type})
  Reason: {why this framework fits}
  Confidence: {HIGH|MEDIUM|LOW}

Alternatives:
  1. {alt_framework} — {when to prefer it instead}
  2. {alt_framework} — {when to prefer it instead}

Generation options:
  A) Quick one-shot → /ue-test-gen (seconds, template-based)
  B) Iterative with build/test cycle → /ue-automation-test-builder (minutes, 6-phase)

Proceed with [A/B] or choose alternative?
```

### Phase 4: Route to Generation

Based on user choice, invoke downstream skill with context:

| Choice | Route | Passed Context |
|--------|-------|----------------|
| A (one-shot) | `/ue-test-gen` | `test_type`, `test_target`, detected patterns |
| B (iterative) | `/ue-automation-test-builder` | Phase 1 analysis as Project Pattern Profile |
| NL scenario | `/ue-test-scenario` | `project_context` with detected patterns |
| Gauntlet | `/ue-gauntlet-orchestration` | Target class info |

**Handoff format** (text in conversation, not a tool call):
```
--- Advisor Handoff ---
test_type: bdd_spec
test_target: AMyCharacter::TakeDamage
detected_patterns:
  inheritance: ACharacter
  gas: true
  enhanced_input: true
  async: false
  world_dependent: true
  existing_test_pattern: simple (in BlueprintExtractorsTest.cpp)
  build_cs_modules: [GameplayAbilities, EnhancedInput]
--- End Handoff ---
```

## Role Separation

| Skill | Role | When |
|-------|------|------|
| **This advisor** | Analyzes target → recommends framework | User unsure which framework |
| `/ue-test-gen` | One-shot code generation | User knows test_type or advisor routes |
| `/ue-automation-test-builder` | 6-phase iterative build/test | Complex tests needing iteration |
| `/ue-test-scenario` | NL scenario → Complex+Latent | Gameplay scenario descriptions |

## Supported Frameworks (7)

| Framework | Macro/Class | Best For | In ue_generate_code? |
|-----------|------------|----------|---------------------|
| Simple | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` | Pure sync logic, math, parsers | Yes (`simple`) |
| BDD Spec | `BEGIN_DEFINE_SPEC`/`END_DEFINE_SPEC` | Multiple related behaviors, Describe/It | Yes (`bdd_spec`) |
| Complex+Latent | `IMPLEMENT_COMPLEX_AUTOMATION_TEST` + `DEFINE_LATENT_AUTOMATION_COMMAND` | Async: timers, montages, network | Yes (`complex_latent`) |
| CQTest | `TEST_CLASS`/`TEST_METHOD` | JUnit-style, structured class/method tests | Yes (`cqtest`) — Issue #7600 |
| FunctionalTest | `AFunctionalTest` subclass | Level-based, actor placement tests | Yes (`functional`) |
| Gauntlet | `UGauntletTestController` | Cooked builds, CI/CD, soak tests | No (separate workflow) |
| EditorUtility | `UEditorUtilityTest` | Blueprint-compatible editor tests | Partial |

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| Target header not found | Wrong class name or engine-only class | Try `ue_grep` with `scope=all`, suggest checking spelling |
| No clear framework match | Ambiguous target | Default to bdd_spec with MEDIUM confidence, explain alternatives |
| CQTest recommended but not in project | Project doesn't include CQTest module | Suggest bdd_spec as alternative, provide CQTest setup guide from REFERENCE.md |

## Self-Test

Invoke with `--self-test` to validate skill configuration:

```bash
/ue-test-framework-advisor --self-test
```

**Checks performed**:
1. **Routing**: Verify `routing.json` has entry with `skill: "ue-test-framework-advisor"` at priority < ue-test-gen
2. **Trigger Patterns**: Verify trigger keywords don't overlap with `/ue-test-gen` negative patterns
3. **REFERENCE.md**: Verify framework comparison table has 7 rows
4. **EXAMPLES.md**: Verify 4+ example scenarios exist
5. **CQTest in ue_generate_code**: Verify `"cqtest"` is in TEST_TYPES constant

**Expected output**: `PASS: 5/5 checks passed` or details on failures.

## See Also

- [REFERENCE.md](REFERENCE.md) — Framework comparison table, CQTest API, decision tree diagram
- [EXAMPLES.md](EXAMPLES.md) — 4 complete usage scenarios
