# UE Test Generator — Reference

## TestEqual Type Mapping

UE provides 15+ `TestEqual` overloads in `FAutomationTestBase`. The `generate_test` operation auto-selects based on C++ types from PDB signatures.

| C++ Type | Assertion Macro | Notes |
|----------|----------------|-------|
| `bool` | `TestTrue` / `TestFalse` | Single-value check |
| `int32`, `int64`, `uint32`, `uint8` | `TestEqual` | Exact integer comparison |
| `float`, `double` | `TestNearlyEqual` | With tolerance (default `KINDA_SMALL_NUMBER`) |
| `FVector`, `FVector2D`, `FVector4` | `TestEqual` | UE has vector overloads |
| `FRotator`, `FQuat` | `TestEqual` | Rotation comparison |
| `FTransform` | `TestEqual` | Full transform comparison |
| `FString` | `TestEqual` | String comparison |
| `FName` | `TestEqual` | Name comparison |
| `FText` | `TestEqual` | Localized text comparison |
| `FColor`, `FLinearColor` | `TestEqual` | Color comparison |
| Pointer types (`AActor*`, etc.) | `TestNotNull` | Null check |
| Other types | `TestEqual` | Fallback to generic comparison |

## EAutomationTestFlags Reference

| Flag | Value | When to Use |
|------|-------|-------------|
| `EditorContext` | — | Test runs in Editor (most common) |
| `ClientContext` | — | Test runs on game client |
| `ServerContext` | — | Test runs on dedicated server |
| `SmokeFilter` | — | Fast, essential tests (< 1 second) |
| `EngineFilter` | — | Engine-level tests |
| `ProductFilter` | — | Game-specific tests |
| `PerfFilter` | — | Performance benchmarks |
| `StressFilter` | — | Stress/load tests |
| `CriticalPriority` | — | Must-pass tests |
| `HighPriority` | — | Important but not blocking |

### Default Flag Combinations

| Test Type | Flags |
|-----------|-------|
| `simple` | `EditorContext \| EngineFilter \| CriticalPriority` (no `SmokeFilter` — Issue #27969; A* explicit SmokeFilter is stripped) |
| `bdd_spec` | `EditorContext \| EngineFilter \| CriticalPriority` (no `SmokeFilter` — Issue #27969; A* explicit SmokeFilter is stripped) |
| `complex_latent` | `EditorContext \| EngineFilter \| ProductFilter` |
| `cqtest` | `EditorContext \| EngineFilter \| CriticalPriority` (single filter bit — Issue #8183) |
| `functional` | `EditorContext \| ProductFilter` |

## Framework Hint Mapping (Issue #10283)

The Test Intent Mapping Layer
([`derive_test_intents`](../../../MCP/native/mcp_server/src/tools/analysis_symbols_test_intents.rs#L48-L211),
Issue #10282) emits a `framework_hint` field on every `TestIntent` record.
This skill (Phase 1.5 intent mode) maps those values to
`ue_generate_code(generate_test).test_type` as follows:

| `framework_hint` (JSON) | `test_type` | UE macro family |
|---|---|---|
| `SimpleAutomation` | `simple` | `IMPLEMENT_SIMPLE_AUTOMATION_TEST` |
| `ComplexAutomation` | `complex_latent` | `IMPLEMENT_COMPLEX_AUTOMATION_TEST` + `DEFINE_LATENT_AUTOMATION_COMMAND` |
| `NetworkAutomation` | `complex_latent` | Same as Complex (server↔client sync needs Latent timing) |
| `Spec` | `bdd_spec` | `BEGIN_DEFINE_SPEC` / `END_DEFINE_SPEC` |
| `CQTest` | `cqtest` | `TEST_CLASS` / `TEST_METHOD` |

Source enum: [`TestKind`](../../../MCP/native/mcp_server/src/tools/analysis_symbols_test_impact.rs#L51-L57)
re-exported as `TestFramework`. The 5 variants are exhaustive — `derive_test_intents` cannot emit any other value.

### Operator-Friendly Aliases

Plan documents and acceptance criteria sometimes use informal names
(`Simple`, `ComplexLatent`, `Spec`, `CQTest`). The actual JSON values
emitted by the rule engine are the **left column above**. Normalize
before mapping:

| Informal alias (paste-in / docs) | Canonical JSON | `test_type` |
|---|---|---|
| `Simple` | `SimpleAutomation` | `simple` |
| `ComplexLatent` | `ComplexAutomation` | `complex_latent` |
| `Network` | `NetworkAutomation` | `complex_latent` |
| `Spec` | `Spec` (no change) | `bdd_spec` |
| `CQTest` | `CQTest` (no change) | `cqtest` |

### Why Network Maps to `complex_latent` (and not its own slot)

`NetworkAutomation` intents (e.g. server-write → client-read sync,
`OnRep_*` firing on the receiving end) need **multi-frame timing** to
verify replication crossed the network. UE's only way to express that
is the Latent automation command family — which lives under the
`complex_latent` template. There is no dedicated network test macro to
target. The mapping is intentional, not a workaround.

### Source-of-Truth Reminder

If you change the framework enum upstream, this table must be
regenerated. The five `framework_hint` values must match
[`TestKind` enum source](../../../MCP/native/mcp_server/src/tools/analysis_symbols_test_impact.rs#L51-L57) verbatim.

## UE Automation Test Macros Quick Reference

### IMPLEMENT_SIMPLE_AUTOMATION_TEST
```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyTest, TEXT("MyGame.MyClass.MyTest"), Flags)

bool FMyTest::RunTest(const FString& Parameters)
{
    // Test body
    return true;
}
```

### BEGIN_DEFINE_SPEC / END_DEFINE_SPEC (BDD)
```cpp
BEGIN_DEFINE_SPEC(FMySpec, TEXT("MyGame.MyClass"), Flags)
    // Shared state here
END_DEFINE_SPEC(FMySpec);

void FMySpec::Define()
{
    Describe(TEXT("Feature"), [this]()
    {
        It(TEXT("Should do X"), [this]()
        {
            TestTrue(TEXT("X is true"), true);
        });
    });
}
```

### IMPLEMENT_COMPLEX_AUTOMATION_TEST (Parameterized)
```cpp
IMPLEMENT_COMPLEX_AUTOMATION_TEST(FMyTest, TEXT("MyGame.MyClass"), Flags)

void FMyTest::GetTests(TArray<FString>& Out, TArray<FString>& Cmds) const
{
    Out.Add(TEXT("Param1"));
    Cmds.Add(TEXT("Param1"));
}

bool FMyTest::RunTest(const FString& Parameters)
{
    // Parameters contains the test command string
    return true;
}
```

### DEFINE_LATENT_AUTOMATION_COMMAND (Async)
```cpp
DEFINE_LATENT_AUTOMATION_COMMAND_ONE_PARAMETER(FWaitForReady, FAutomationTestBase*, Test);

bool FWaitForReady::Update()
{
    // Return true when ready
    return true;
}

// In RunTest:
ADD_LATENT_AUTOMATION_COMMAND(FWaitForReady(this));
```

## Test File Organization

UE convention for test file placement:

```text
Source/
└── MyGame/
    ├── Characters/
    │   └── MyCharacter.cpp
    └── Tests/                    ← Test files here
        ├── MyCharacterSpec.cpp   ← BDD spec (single .cpp)
        ├── MyCharacterTest.cpp   ← Simple test (single .cpp)
        └── MyFunctionalTest.h    ← Functional test (.h + .cpp pair)
        └── MyFunctionalTest.cpp
```

Non-functional tests (simple, bdd_spec, complex_latent) are **single .cpp files** — the macros define entire test classes.
Functional tests use the standard UCLASS .h/.cpp pattern since `AFunctionalTest` is an Actor.
