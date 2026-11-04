# UE Test Generator — Examples

## Example 1: BDD Spec for Character (Default)

**Request**: "AMyCharacter 테스트 생성해줘"

**Step 1**: Analyze target
```python
ue_analyze_symbols(operation="get_methods", class_name="AMyCharacter")
# Returns: TakeDamage(float), GetHealth() -> float, Die()
```

**Step 2**: Generate test
```python
ue_generate_code(operation="generate_test", params={
    "test_target": "AMyCharacter",
    "module_name": "MyGame",
    "functions": ["TakeDamage", "GetHealth", "Die"],
    "output_dir": "Source/MyGame/Tests"
})
```

**Output**: `Source/MyGame/Tests/MyCharacterSpec.cpp`
```cpp
// Auto-generated UE BDD Spec Test (Issue #5294)
#include "Misc/AutomationTest.h"
#include "CoreMinimal.h"
#include "MyCharacter.h"

BEGIN_DEFINE_SPEC(FMyCharacterSpec, TEXT("MyGame.MyCharacter"),
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
    EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::CriticalPriority)
END_DEFINE_SPEC(FMyCharacterSpec);

void FMyCharacterSpec::Define()
{
    Describe(TEXT("AMyCharacter::TakeDamage"), [this]()
    {
        It(TEXT("Should execute successfully"), [this]()
        {
            // Arrange, Act, Assert
        });
        It(TEXT("Should handle edge cases"), [this]()
        {
            // Edge case tests
        });
    });

    Describe(TEXT("AMyCharacter::GetHealth"), [this]()
    {
        // ...
    });

    Describe(TEXT("AMyCharacter::Die"), [this]()
    {
        // ...
    });
}
```

---

## Example 2: Simple Test for Single Function

**Request**: "Generate test for AMyCharacter::TakeDamage"

```python
ue_generate_code(operation="generate_test", params={
    "test_target": "AMyCharacter::TakeDamage",
    "test_type": "simple"
})
```

**Output**: `Source/Tests/MyCharacterTakeDamageTest.cpp`
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyCharacterTakeDamageTest,
    TEXT("Game.MyCharacter.TakeDamage"),
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
    EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::CriticalPriority)

bool FMyCharacterTakeDamageTest::RunTest(const FString& Parameters)
{
    // Arrange
    // TODO: Create instance of AMyCharacter

    // Act
    // TODO: Call AMyCharacter::TakeDamage

    // Assert
    TestTrue(TEXT("AMyCharacter::TakeDamage behaves correctly"), true);

    return true;
}
```

---

## Example 3: Complex Latent Test for Async Systems

**Request**: "Create async test for AMyCharacter with TakeDamage and Heal"

```python
ue_generate_code(operation="generate_test", params={
    "test_target": "AMyCharacter",
    "test_type": "complex_latent",
    "functions": ["TakeDamage", "Heal"]
})
```

**Output**: Includes `DEFINE_LATENT_AUTOMATION_COMMAND`, `IMPLEMENT_COMPLEX_AUTOMATION_TEST`, and `ADD_LATENT_AUTOMATION_COMMAND`.

---

## Example 3.5: CQTest Phase 3 — Cell 3 / 4 / 8 body emission (Issue #11358)

**Request A**: "Generate CQTest for `ATestPawn` (Actor convention, no replication)"

```python
ue_generate_code(operation="generate_test", params={
    "test_target": "ATestPawn",
    "test_type": "cqtest",
})
```

**Output** — cell 3 (FActorTestSpawner) fires:

```cpp
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
TEST_CLASS_WITH_FLAGS(FCQTestATestPawn, "Game.ATestPawn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
    FActorTestSpawner Spawner;

    TEST_METHOD(Spawn_TestPawn_DefaultActorSpawned)
    {
        // Arrange — UE 5.8 CQTest FActorTestSpawner
        // Header: Engine/Source/Developer/CQTest/Public/Components/ActorTestSpawner.h
        ATestPawn& Actor = Spawner.SpawnActor<ATestPawn>();

        ASSERT_THAT(IsTrue(Actor.GetFName() == NAME_None));
    }
};
#else
// bare TEST_CLASS_WITH_FLAGS fallback for 5.7 (no ActorTestSpawner.h)
#endif
```

---

**Request B**: "Generate CQTest for `UMyAttributeSet` (UPROPERTY-heavy non-Actor UObject)"

```python
ue_generate_code(operation="generate_test", params={
    "test_target": "UMyAttributeSet",
    "test_type": "cqtest",
})
```

**Output** — cell 4 (TObjectBuilder) fires. The `.SetParam(...)` chain is intentionally
TODO-commented because PDB-based UPROPERTY name/type discovery is deferred to a follow-up
Epic #8187 sub-issue. Fill in real UPROPERTYs before the test asserts behavior:

```cpp
TEST_METHOD(Build_MyAttributeSet_SpawnsDefault)
{
    UMyAttributeSet& Instance = TObjectBuilder<UMyAttributeSet>()
        // .SetParam(FName(TEXT("PropertyName")), /* Value */ 0)
        .Spawn();
    ASSERT_THAT(IsNotNull(&Instance));
}
```

---

**Request C**: "Generate CQTest for `UEnemySpawner` (loads Blueprint via TSubclassOf<>)"

If the target has any of:
- Function body `"/Game/..."` package-path literal (e.g. `LoadObject<UClass>(nullptr, TEXT("/Game/Enemies/BP_Goblin"))`)
- Function body `LoadClass<>` / `StaticLoadObject` / `StaticLoadClass` / `LoadObject<UClass>` call
- Header `TSubclassOf<>` / `TSoftClassPtr<>` / `TSoftObjectPtr<>` UPROPERTY

→ **cell 8 (CQTestAssetHelper)** wins over cells 3 / 4. Plan adds `"AssetRegistry"` to
`required_modules` (Phase 5 / #11528 will auto-Build.cs; until then a generated `// TODO`
comment surfaces the manual step).

```cpp
TEST_CLASS_WITH_FLAGS(FCQTestUEnemySpawner, "Game.UEnemySpawner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
{
    // Triggered by: TSubclassOf<AEnemyBase> (#8187 cell 8 / Issue #11358)
    // TODO: ensure 'flags' includes `EAutomationTestFlags::EditorContext` —
    //       the AssetRegistry-backed helpers run only in the editor context.

    TEST_METHOD(Load_EnemySpawner_FromAssetRegistry)
    {
        // TODO: replace '/Game/Blueprints' + 'B_TODO' with the real package path + asset name.
        FARFilter Filter = CQTestAssetHelper::FAssetFilterBuilder()
            .WithPackagePath(FName(TEXT("/Game/Blueprints")))
            .Build();

        UClass* LoadedClass = CQTestAssetHelper::GetBlueprintClass(Filter, TEXT("B_TODO"));
        ASSERT_THAT(IsNotNull(LoadedClass));
    }
};
```

**Dispatch priority (5 cells)**: `NETWORK_TEST_CLASS` (cell 1) > `CQTestAssetHelper` (cell 8)
> `TObjectBuilder` (cell 4) > `FActorTestSpawner` (cell 3) > bare `TEST_CLASS_WITH_FLAGS`.

---

## Example 4: Smart Mode (Auto-Routing)

**Request**: Just provide `test_target` — smart mode auto-routes:

```python
ue_generate_code(operation="smart", test_target="AMyCharacter::TakeDamage")
# Auto-routes to generate_test, produces BDD spec by default
```

---

## Example 5: Full Workflow with Build Verification

**Request**: "TakeDamage 테스트 만들고 빌드해줘"

```python
# Step 1: Generate
result = ue_generate_code(operation="generate_test", params={
    "test_target": "AMyCharacter::TakeDamage",
    "test_type": "bdd_spec",
    "module_name": "LyraGame",
    "output_dir": "Source/LyraGame/Tests"
})

# Step 2: Write file (Claude writes cpp_code to cpp_path)

# Step 3: Build to verify compilation
ue_fix_errors(operation="build_and_fix", project_root="I:/Lyra/LyraStarterGame")

# Step 4: Run the test (requires Editor)
ue_editor_automation(operation="run_automation_tests",
    test_name="LyraGame.MyCharacter.TakeDamage")
```

---

## Example 6: Intent-Based Multi-Test from `derive_test_intents` (Issue #10283)

**Request**: "ACharacter::TakeDamage 의 모든 패턴 테스트 생성해줘 — intents 기반으로"

**Step 1**: Derive intents (Sub-issue 2 of Epic #8186 — `ue_analyze_symbols` op shipped in #10282).

```python
ue_analyze_symbols(operation="derive_test_intents",
                   function="ACharacter::TakeDamage")
```

**Sample response** (4 intents from a single `int32 Damage` numeric param):

```json
{
  "success": true,
  "operation": "derive_test_intents",
  "_version": 1,
  "function": "ACharacter::TakeDamage",
  "intents": [
    {
      "description": "Damage = zero (int32) — boundary case",
      "assertion_template": "TestEqual(\"Damage=0\", subject->Damage, /* expected */ 0)",
      "framework_hint": "SimpleAutomation",
      "priority": "P1",
      "source_pattern": { "kind": "pattern", "name": "NumericParam" },
      "change_driven": false
    },
    {
      "description": "Damage = MIN (int32) — boundary case",
      "assertion_template": "TestEqual(\"Damage=MIN\", subject->Damage, /* expected */ MIN)",
      "framework_hint": "SimpleAutomation",
      "priority": "P1",
      "source_pattern": { "kind": "pattern", "name": "NumericParam" },
      "change_driven": false
    },
    {
      "description": "Damage = MAX (int32) — boundary case",
      "assertion_template": "TestEqual(\"Damage=MAX\", subject->Damage, /* expected */ MAX)",
      "framework_hint": "SimpleAutomation",
      "priority": "P1",
      "source_pattern": { "kind": "pattern", "name": "NumericParam" },
      "change_driven": false
    },
    {
      "description": "Damage = -1 (int32) — negative input",
      "assertion_template": "TestEqual(\"Damage=-1\", subject->Damage, /* expected */ -1)",
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

**Step 2**: All 4 intents share `framework_hint = "SimpleAutomation"` → 1 group → 1 `generate_test` call with `test_type="simple"`.

```python
ue_generate_code(operation="generate_test", params={
    "test_target": "ACharacter::TakeDamage",
    "test_type": "simple",
    "test_name": "FACharacterSimpleAutomationMultiTest",
    "module_name": "MyGame",
    "output_dir": "Source/MyGame/Tests"
})
```

**Step 3**: Inject one assertion per intent into the generated `RunTest()` body. Final file:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FACharacterSimpleAutomationMultiTest,
    TEXT("MyGame.ACharacter.TakeDamage.MultiBoundary"),
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter |
    EAutomationTestFlags::SmokeFilter | EAutomationTestFlags::CriticalPriority)

bool FACharacterSimpleAutomationMultiTest::RunTest(const FString& Parameters)
{
    // Arrange: subject = NewObject<ACharacter>();   // ← caller must add setup;
    //                                                  inject_intents() preserves
    //                                                  the assertion_template stubs only.

    // Damage = zero — boundary case
    TestEqual(TEXT("Damage=0"), subject->Damage, /* expected */ 0);
    // Damage = MIN — boundary case
    TestEqual(TEXT("Damage=MIN"), subject->Damage, /* expected */ MIN);
    // Damage = MAX — boundary case
    TestEqual(TEXT("Damage=MAX"), subject->Damage, /* expected */ MAX);
    // Damage = -1 — negative input
    TestEqual(TEXT("Damage=-1"), subject->Damage, /* expected */ -1);
    return true;
}
```

**Output**: 1 test file × 4 cases × 1 framework — single `generate_test` call.

---

## Example 7: `--intents-from` Shorthand

**Request**: "TakeDamage intents 으로 테스트 만들어줘" (or English: "generate tests from intents for ACharacter::TakeDamage")

**Combined call** — skill invokes `derive_test_intents` itself, then dispatches:

```python
# Step 1 (skill-internal): derive
intents_response = ue_analyze_symbols(operation="derive_test_intents",
                                       function="ACharacter::TakeDamage")
intents = intents_response.intents
# Step 2 (skill-internal): single group → single generate_test call
ue_generate_code(operation="generate_test", params={
    "test_target": "ACharacter::TakeDamage",
    "test_type": "simple",  # mapped from SimpleAutomation
    "module_name": "MyGame",
    "output_dir": "Source/MyGame/Tests",
})
# Step 3 (skill-internal): inject the 4 assertions inline as in Example 6
```

**Why this exists**: saves the operator from copy-pasting 4-10 intent
JSON records between two MCP tool invocations. Same end result as
Example 6, one fewer manual step.

> **Truncation handling**: if `summary.truncated == true`, skill emits:
> `⚠️ Upstream capped at 10 intents/function — additional patterns dropped.
>  Re-run derive_test_intents and inspect summary.patterns_detected to see
>  the original count.`

---

## Example 8: Mixed-Framework Grouping (Offline Replay Fixture)

**Request**: "AReplicatedActor::OnDamageReceived intents 로 테스트 생성"

This example demonstrates **multi-framework grouping** — a function that
mixes a numeric param (`Damage`), a replicated property (`Health` with
`OnRep_Health`), and a `SetTimer` call yields intents across **3 frameworks**.

**Synthetic fixture** (no daemon required — paste directly into the
intent input mode for offline replay/review):

```json
{
  "intents": [
    {
      "description": "Damage = zero (int32) — boundary case",
      "assertion_template": "TestEqual(\"Damage=0\", subject->Damage, /* expected */ 0)",
      "framework_hint": "SimpleAutomation",
      "priority": "P1",
      "source_pattern": { "kind": "pattern", "name": "NumericParam" },
      "change_driven": false
    },
    {
      "description": "Health server→client value sync",
      "assertion_template": "TestEqual(\"client.Health matches server.Health\", client->Health, server->Health)",
      "framework_hint": "NetworkAutomation",
      "priority": "P0",
      "source_pattern": { "kind": "pattern", "name": "UPropertyReplicated" },
      "change_driven": false
    },
    {
      "description": "OnRep_Health fires on receiving client",
      "assertion_template": "TestTrue(\"OnRep_Health fired\", client->bOnRepHealthFired)",
      "framework_hint": "NetworkAutomation",
      "priority": "P0",
      "source_pattern": { "kind": "pattern", "name": "UPropertyReplicated" },
      "change_driven": false
    },
    {
      "description": "SetTimer() timing — callback fires after expected interval",
      "assertion_template": "ADD_LATENT_AUTOMATION_COMMAND(FWaitForTimerCallback(subject, 1.0f, TEXT(\"SetTimer\")))",
      "framework_hint": "ComplexAutomation",
      "priority": "P1",
      "source_pattern": { "kind": "pattern", "name": "TimerOrLatent" },
      "change_driven": false
    },
    {
      "description": "Branch 'if' coverage — input that reaches: bIsAlive == true",
      "assertion_template": "TestEqual(\"if branch reached\", subject->LastReturn, /* path-covering value */)",
      "framework_hint": "CQTest",
      "priority": "P1",
      "source_pattern": { "kind": "branch", "name": "if" },
      "change_driven": false
    }
  ]
}
```

**Group-by `framework_hint`**:

| Group | `framework_hint` | `test_type` | Intent count |
|---|---|---|---|
| 1 | `SimpleAutomation` | `simple` | 1 |
| 2 | `NetworkAutomation` | `complex_latent` | 2 |
| 3 | `ComplexAutomation` | `complex_latent` | 1 |
| 4 | `CQTest` | `cqtest` | 1 |

> **Note**: Groups 2 and 3 both map to `test_type = complex_latent` but
> are kept as **separate `generate_test` calls** with distinct
> `test_name` values (`F...NetworkAutomationMultiTest` vs.
> `F...ComplexAutomationMultiTest`). This preserves the framework_hint
> origin in the output filename and lets reviewers see which intents
> came from which detected pattern.

**Output**: 4 `.cpp` files written to `Source/MyGame/Tests/`:

- `AReplicatedActorSimpleAutomationMultiTest.cpp` — 1 boundary assertion
- `AReplicatedActorNetworkAutomationMultiTest.cpp` — 2 latent server↔client sync commands
- `AReplicatedActorComplexAutomationMultiTest.cpp` — 1 latent timer command
- `AReplicatedActorCQTestMultiTest.cpp` — 1 `TEST_METHOD` for branch coverage

**MCP call count**: 1× `ue_analyze_symbols(derive_test_intents)` + 4× `ue_generate_code(generate_test)` = 5 calls (at the documented N≤5 ceiling).

> **Operator override**: if 4 files are too many, re-run with
> `framework=Spec` (existing single-shot arg) to collapse all intents
> into one BDD spec — at the cost of losing `complex_latent`'s frame-aware
> timing semantics.
