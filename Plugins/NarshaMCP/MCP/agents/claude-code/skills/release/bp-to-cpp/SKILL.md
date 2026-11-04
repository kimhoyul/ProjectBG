---
name: bp-to-cpp
model: opus
description: "Purpose: BP logic-to-C++ auto conversion. Analyzes Blueprint node graphs and generates equivalent C++ code. 6-phase pipeline: Discovery -> Structure Analysis -> Execution Flow -> Node Mapping -> Code Generation -> Validation. Not for project infrastructure (use bp-cpp-project-init). Not for migration validation only (use bp-to-cpp-validator). Triggers: 'BP를 C++로', 'BP 로직 C++로', 'convert BP to C++', 'Blueprint logic to C++', 'BP 자동 변환'."
allowed-tools:
  - mcp__narshamcp__ue_manage_blueprint
  - mcp__narshamcp__ue_generate_code
  - mcp__narshamcp__ue_analyze_symbols
  - mcp__narshamcp__ue_trace_execution
  - mcp__narshamcp__ue_run_workflow
  - mcp__narshamcp__ue_fix_errors
  - mcp__narshamcp__ue_search_assets
  - mcp__narshamcp__ue_grep
  - mcp__narshamcp__ue_read
  - mcp__narshamcp__ue_glob
  - mcp__narshamcp__ue_check_health
argument-hint: ["Blueprint name to convert (e.g., BP_PlayerCharacter)", "--dry-run (preview only)", "--output Source/MyGame/ (output directory)"]
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7593"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# BP-to-C++ Auto Conversion

**Status**: Beta
**Purpose**: Analyze Blueprint node graphs and generate equivalent C++ code

---

## Purpose

6-phase pipeline that **converts Blueprint logic to C++ code**:

1. **Discovery** - Verify BP exists, check project readiness
2. **Structure Analysis** - Extract complete BP graph (nodes, variables, events)
3. **Execution Flow** - Trace execution paths and dependencies
4. **Node Mapping** - Map BP nodes to C++ equivalents (core new value)
5. **Code Generation** - Generate .h/.cpp with mapped function bodies
6. **Validation** - Verify completeness and provide replacement guide

**Key Innovation**: Combines existing skill capabilities (blueprint-flow analysis + code-generation) with a novel node-to-C++ mapping table to automate the most tedious part of BP migration.

**Reuse Strategy**: ~70% inline from existing skills:
- Phase 1-2: blueprint-flow (structure + execution analysis)
- Phase 4: code-generation (Pipeline Mode with derive_class)
- Phase 5: bp-to-cpp-validator (Core 5-tool validation)

---

## Trigger Phrases

### Korean

- "BP를 C++로", "BP 로직 C++로"
- "BP 노드를 C++로", "BP 자동 변환"
- "블루프린트 C++ 변환", "C++로 변환"
- "C++로 빼", "BP 코드 변환"
- "블루프린트 로직 변환"

### English

- "convert BP to C++", "Blueprint to C++"
- "convert BP logic", "BP auto convert"
- "convert Blueprint to C++", "convert to C++"
- "Blueprint logic to C++", "BP to C++ conversion"

### Negative (do NOT trigger)

- "프로젝트 전환", "Source 디렉토리", "프로젝트 초기화" -> use `bp-cpp-project-init`
- "BP 분석", "BP 구조 분석" -> use `blueprint-flow`
- "BP 생성", "create Blueprint" -> use `blueprint-generator`
- "변환 검증", "migration validation" -> use `bp-to-cpp-validator`
- "BP 리팩터링", "refactor Blueprint" -> use `ue-refactor-bp`

---

## Skill Family

| Skill | Purpose | When to use |
|---|---|---|
| **bp-to-cpp** (this) | Analyze BP logic, generate C++ code | "BP를 C++로 변환해줘" |
| bp-cpp-project-init | Add Source/ structure to BP-only project | "C++ 프로젝트로 전환해줘" |
| bp-to-cpp-validator | Validate migration completeness | "변환 잘 됐는지 검증해줘" |

**Typical workflow**: bp-cpp-project-init (if no Source/) -> **bp-to-cpp** -> bp-to-cpp-validator

---

## Phase 0: Discovery + Pre-flight

**Goal**: Verify BP exists and project is ready for conversion.

```python
# 1. Check project has C++ support (Source/ directory)
ue_glob(pattern="Source/**/*.Build.cs")
# If no results → recommend: "Run /bp-cpp-project-init first to add C++ support"

# 2. Verify Blueprint exists
ue_grep(query="<BP_NAME>", domain="assets")

# 3. Quick complexity assessment
ue_manage_blueprint(operation="get_structure", blueprint_name="<BP_NAME>")
```

**Decision gate**:
- No Source/ directory → abort, suggest `/bp-cpp-project-init` first
- BP not found → abort with error
- Node count < 20 → single-class path (Phase 4a)
- Node count >= 20 → multi-class consideration (Phase 4b)

---

## Phase 1: Structure Analysis

**Goal**: Extract complete BP graph — all functions, variables, events, interfaces.
**Source**: Inline from `blueprint-flow` Phase 1.

```python
# Get full Blueprint structure
structure = ue_manage_blueprint(operation="get_structure", blueprint_name="<BP_NAME>")
```

**Extract and classify**:
- **Event Handlers**: BeginPlay, Tick, Custom Events, Input Actions
- **Functions**: Pure functions, impure functions, interface implementations
- **Variables**: UPROPERTY candidates (type, default value, replication)
- **Event Dispatchers**: DECLARE_DYNAMIC_MULTICAST_DELEGATE candidates
- **Components**: CreateDefaultSubobject candidates
- **Interfaces**: Interface implementations to preserve

---

## Phase 2: Execution Flow Tracing

**Goal**: Trace execution paths to understand function call order and dependencies.
**Source**: Inline from `blueprint-flow` Phase 2.

### Workflow Selection (Decision Tree)

Choose workflow based on BP entry pattern detected in Phase 1:

| BP Pattern | Workflow | Tool Call | When |
|---|---|---|---|
| **Input-driven** (Character, Controller) | `input-to-action` | `ue_run_workflow(workflow="input-to-action", target="<BP>")` | Has InputAction/EnhancedInput nodes |
| **Overlap/Trigger** (Pads, Volumes) | `trace_execution_flow` | `ue_trace_execution(operation="trace_execution_flow", blueprint_name="<BP>", start_node="<EventNode>")` | Has ComponentBoundEvent nodes |
| **Tick-driven** (continuous logic) | `trace_execution_flow` | `ue_trace_execution(..., start_node="EventTick")` | Has EventTick node |
| **BeginPlay/Timer** (init + delayed) | `trace_execution_flow` | `ue_trace_execution(..., start_node="EventBeginPlay")` | Default fallback |

```python
# Step 1: Detect entry pattern from Phase 1 node list
events = [n for n in nodes if n["node_class"] == "K2Node_ComponentBoundEvent"]
has_input = any("Input" in n["node_title"] for n in nodes)

# Step 2: Route to correct workflow
if has_input:
    # Input-driven BP (57% faster via workflow shortcut)
    flow = ue_run_workflow(workflow_type="input-to-action", blueprint_name="<BP_NAME>")
elif events:
    # Overlap/Trigger-driven BP — trace each event entry point
    for event in events:
        flow = ue_trace_execution(operation="trace_execution_flow", blueprint_name="<BP_NAME>", start_node=event["node_id"])
else:
    # Fallback: trace from BeginPlay
    flow = ue_trace_execution(operation="trace_execution_flow", blueprint_name="<BP_NAME>", start_node="EventBeginPlay")
```

**Output**: Execution dependency graph showing which functions call which, in what order.

---

## Phase 3: Node-to-C++ Mapping (Core New Value)

**Goal**: Map each BP node to its C++ equivalent code.

This is the **novel contribution** of this skill. For each BP function extracted in Phase 1:

1. Walk through the node graph
2. Map each node type to C++ code using the **[Node Mapping Table](REFERENCE.md)**
3. Preserve execution order from Phase 2
4. Handle connections (data wires → function parameters, exec wires → sequential statements)

### Mapping Categories

| Category | BP Node Examples | C++ Pattern |
|---|---|---|
| **Flow Control** | Branch, ForEachLoop, Sequence, FlipFlop | if/else, range-for, sequential, bool toggle |
| **Events** | BeginPlay, Tick, Custom Event | virtual override, UFUNCTION() |
| **Variables** | Get/Set Variable | UPROPERTY member access |
| **Math** | Add, Multiply, MakeVector | operators, FVector() constructor |
| **Component** | GetComponentByClass, AddComponent | FindComponentByClass<T>(), CreateDefaultSubobject<T>() |
| **Actor** | SpawnActor, SetActorLocation, DestroyActor | GetWorld()->SpawnActor<T>(), SetActorLocation(), Destroy() |
| **Cast** | Cast To, Interface Call | Cast<T>(), Execute_Func() |
| **Timer** | SetTimerByEvent, ClearTimer | GetWorldTimerManager().SetTimer/ClearTimer |
| **Animation** | PlayMontage, StopMontage | PlayAnimMontage(), StopAnimMontage() |
| **Gameplay** | ApplyDamage, LineTrace | UGameplayStatics::ApplyDamage(), LineTraceSingle() |
| **GAS** | ActivateAbility, ApplyGE | TryActivateAbility(), ApplyGameplayEffectToSelf() |
| **Communication** | EventDispatcher, Interface Message | DECLARE_DYNAMIC_MULTICAST_DELEGATE, IInterface::Execute_ |

### Unsupported Nodes (v1.0)

These nodes require manual review — output `// TODO: Manual conversion required`:

| Node | Reason | Suggestion |
|---|---|---|
| **Timeline** | Complex struct (FTimeline + curves + delegates) | Generate FTimeline skeleton, mark curves for manual setup |
| **Delay** | Latent node, requires async pattern | Suggest FTimerHandle + lambda alternative |
| **AI MoveTo** | Latent, depends on navigation system | Suggest UAIBlueprintHelperLibrary pattern |
| **Widget creation** | UMG is not 1:1 with C++ | Skip, recommend separate widget refactor |
| **Macro nodes** | Must be inlined first | Inline expansion before mapping |

### Mapping Output Format

For each BP function, produce:

```cpp
// Mapped from BP Function: "CalculateDamage"
// Nodes: 12, Unsupported: 1 (Timeline)
float AMyCharacter::CalculateDamage(float BaseDamage, AActor* Target)
{
    // Branch node → if/else
    if (Target != nullptr)
    {
        // GetComponentByClass → FindComponentByClass
        UHealthComponent* Health = Target->FindComponentByClass<UHealthComponent>();
        if (Health != nullptr)
        {
            // Multiply → operator*
            float FinalDamage = BaseDamage * DamageMultiplier;
            // ApplyDamage → UGameplayStatics
            UGameplayStatics::ApplyDamage(Target, FinalDamage, GetController(), this, nullptr);
            return FinalDamage;
        }
    }
    return 0.0f;
}
```

---

## Phase 4: Code Generation

**Goal**: Generate production-ready .h/.cpp files.
**Source**: Inline from `code-generation` Pipeline Mode.

### Phase 4a: Single Class (< 20 nodes)

```python
# 1. Verify base class via PDB
ue_analyze_symbols(operation="search_symbols", query="<PARENT_CLASS_NAME>")

# 2. Generate skeleton with features
ue_generate_code(
    operation="derive_class",
    base_class="<PARENT_CLASS>",
    class_name="<CPP_CLASS_NAME>",
    output_dir="Source/<Module>/",
    features=[<detected_features>],
)

# 3. Check Build.cs module dependencies (CRITICAL for GAS/Niagara/AI)
# Scan Phase 3 node types → determine required modules → verify Build.cs
```

### Phase 4a-pre: Module Dependency Check

Before writing code, verify `.Build.cs` has required modules for the node types found in Phase 3:

| Node Type Found in Phase 3 | Required Module | Example |
|---|---|---|
| ApplyGE, ActivateAbility, GetAttribute | `GameplayAbilities` | GAS nodes |
| GameplayTag operations | `GameplayTags` | Tag queries |
| Niagara spawn/parameter | `Niagara` | VFX nodes |
| Enhanced Input callbacks | `EnhancedInput` | Input Action nodes |
| AI MoveTo, BehaviorTree | `AIModule` | AI task nodes |
| Navigation queries | `NavigationSystem` | Pathfinding |

```python
# Read Build.cs and check for missing modules
build_cs = read_file("Source/<Module>/<Module>.Build.cs")
# If GameplayAbilities module used but not in PublicDependencyModuleNames:
#   → Add "GameplayAbilities" to Build.cs before proceeding
```

### Phase 4a Step 3: Enrich skeleton → complete code

**`derive_class` output scope — what it generates vs what Claude must add:**

| `derive_class` Generates | Claude MUST Add (from Phase 1-3) |
|---|---|
| Class declaration + GENERATED_BODY | UPROPERTY members (Phase 1 variables) |
| Constructor stub | CreateDefaultSubobject calls (Phase 1 components) |
| Base class inheritance | Function bodies (Phase 3 node mapping) |
| Feature-based overrides (e.g., ASC) | Delegate bindings (ComponentBoundEvent nodes) |
| Module export macro | Event handler signatures (UFUNCTION declarations) |

**Without Steps 3a-3d below, the skeleton will NOT compile.**

**Step 3 is the core code-writing step.** `derive_class` outputs a skeleton — Claude completes it:

#### 3a. Add ALL BP variables as UPROPERTY members

Use Phase 1 variable list (`search_variables` output) + REFERENCE.md Variable Type Mapping:

```cpp
// Phase 1 data: GameplayEffectToApply (TSubclassOf<GameplayEffect>)
// REFERENCE.md: Class Reference → TSubclassOf<T>
UPROPERTY(EditAnywhere, Category = "Gameplay")
TSubclassOf<UGameplayEffect> GameplayEffectToApply;
```

#### 3b. Add ALL components as CreateDefaultSubobject in constructor

```cpp
// Phase 1 data: Volume (USphereComponent*), Pad (UStaticMeshComponent*)
AMyClass::AMyClass()
{
    Volume = CreateDefaultSubobject<USphereComponent>(TEXT("Volume"));
    RootComponent = Volume;
    Pad = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Pad"));
    Pad->SetupAttachment(RootComponent);
}
```

#### 3c. Write function bodies from Phase 3 node mapping

For each function in Phase 1 (`get_function_details`), walk the node list and map each node to C++ using REFERENCE.md:

```cpp
// Phase 1: OnComponentBeginOverlap has 8 nodes:
//   ComponentBoundEvent → IsValid → GetASC → ApplyGE → GetFloat → BuildString → PrintString
// Phase 3 mapping (REFERENCE.md):
void AMyClass::OnOverlapBegin(UPrimitiveComponent* OverlappedComp,
    AActor* OtherActor, ...)
{
    // IsValid macro → nullptr check
    UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(OtherActor);
    if (!IsValid(ASC)) return;

    // BP_Apply GE To Self → ApplyGameplayEffectToSelf
    FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
    FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(GameplayEffectToApply, 1.0f, Context);
    ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());

    // Get Float Attribute → GetNumericAttribute
    float Value = ASC->GetNumericAttribute(UMyAttributeSet::GetHealthAttribute());

    // Build String + Print String → UE_LOG
    UE_LOG(LogTemp, Warning, TEXT("Health: %f"), Value);
}
```

#### 3d. Bind component overlap delegates

```cpp
// ComponentBoundEvent nodes → delegate binding in BeginPlay or constructor
Volume->OnComponentBeginOverlap.AddDynamic(this, &AMyClass::OnOverlapBegin);
Volume->OnComponentEndOverlap.AddDynamic(this, &AMyClass::OnOverlapEnd);
```

**The final output must be complete, compilable .h/.cpp — not just the `derive_class` skeleton.**

### Phase 4b: Multi-Class (>= 20 nodes, optional)

For complex BPs that should be split into multiple C++ classes (e.g., Character + Component + Manager):

```python
# Use code-generation-power pattern
# 1. Suggest class decomposition
ue_generate_code(operation="suggest_class", description="<BP functionality description>")

# 2. Generate each class in dependency order
for class_info in suggested_classes:
    ue_generate_code(operation="derive_class", params={...})

# 3. Enrich each skeleton with Phase 3 mapped bodies (same as 4a Step 3)
```

### Class Name Convention

| BP Name | C++ Name | Rule |
|---|---|---|
| BP_PlayerCharacter | APlayerCharacter | Remove BP_ prefix, add A for Actor |
| BP_HealthComponent | UHealthComponent | Remove BP_ prefix, add U for UObject |
| BP_GameMode | AGameMode_Custom | Add suffix if conflicts with engine |

---

## Phase 5: Validation

**Goal**: Verify generated C++ is correct and complete.
**Source**: Inline from `bp-to-cpp-validator` Core 5-tool workflow.

```python
# Stage 1: Compare function lists
# BP functions from Phase 1 vs generated C++ functions

# Stage 2: Symbol verification
ue_analyze_symbols(operation="search_symbols", query="<CPP_CLASS_NAME>")

# Stage 3: Build check
ue_fix_errors(operation="preflight", project_root=".")

# Stage 4: Find remaining BP references
ue_search_assets(asset_type="blueprint", query="<BP_NAME>")
```

### Validation Report

```text
=== BP-to-C++ Conversion Report ===

Source: BP_PlayerCharacter
Target: APlayerCharacter (.h + .cpp)

Functions Converted: 8/10 (80%)
  - BeginPlay ............... CONVERTED
  - Tick .................... CONVERTED
  - CalculateDamage ......... CONVERTED
  - OnHit ................... CONVERTED
  - PlayAttackMontage ....... CONVERTED
  - HandleDeath ............. CONVERTED
  - UpdateTimeline .......... TODO (Timeline node)
  - SpawnVFX ................ TODO (Niagara system)
  - OnInputAction_Jump ...... CONVERTED
  - OnInputAction_Attack .... CONVERTED

Variables Migrated: 12/12 (100%)
Build Check: PASS (no errors)
Unsupported Nodes: 2 (marked with TODO)
```

---

## Phase 6: BP Replacement Guide

**Goal**: Provide step-by-step migration instructions.

### Output

```text
=== Migration Guide: BP_PlayerCharacter → APlayerCharacter ===

1. REPARENT Blueprint:
   - Open BP_PlayerCharacter in Editor
   - File → Reparent Blueprint → Select APlayerCharacter
   - This preserves all instances and references

2. REMOVE migrated logic:
   - Delete Event Graph nodes that are now in C++
   - Keep: Timeline nodes (marked TODO), widget references

3. CoreRedirects (if replacing BP entirely):
   Add to DefaultEngine.ini:
   [CoreRedirects]
   +ClassRedirects=(OldName="/Script/MyGame.BP_PlayerCharacter_C", NewName="/Script/MyGame.APlayerCharacter")

4. UPDATE references:
   - 3 assets reference BP_PlayerCharacter
   - Update spawners, data tables, and level placements

5. MANUAL TODO items:
   - [ ] Convert Timeline node in UpdateTimeline()
   - [ ] Set up Niagara system in SpawnVFX()
```

---

## Complexity Handling

| Complexity | Node Count | Strategy | Time |
|---|---|---|---|
| **Simple** | 1-10 | Direct 1:1 mapping, single class | ~1 min |
| **Medium** | 11-30 | Single class, some TODO nodes likely | ~3 min |
| **Complex** | 31-100 | Consider class decomposition | ~5 min |
| **Very Complex** | 100+ | Recommend incremental migration | ~10 min |

---

## Limitations (v1.0)

| Limitation | Reason | Workaround |
|---|---|---|
| Widget Blueprint | UMG not 1:1 with C++ | Manual widget C++ migration |
| Animation Blueprint | AnimGraph is separate system | Use Animation workflow skill |
| Level Blueprint | Not Actor-based | Extract to Actor-based class first |
| Macro Library | Shared macros need special handling | Inline first, then convert |
| Latent Nodes | Async patterns vary | FTimerHandle + lambda suggestion |
| Timeline Nodes | FTimeline setup is complex | Skeleton generated, curves manual |

---

## Error Recovery

| Error | Cause | Recovery |
|---|---|---|
| BP not found | Wrong name or not indexed | Try `ue_glob("*<partial_name>*")` to find correct name |
| Parent class unknown | Engine class not in PDB | Use `ue_analyze_symbols` to search, fall back to common base |
| Generated code won't compile | Missing includes or types | Run `ue_fix_errors(smart)` for auto-resolution |
| Too many unsupported nodes | Complex BP with many latent/timeline nodes | Suggest incremental migration: convert supported nodes first |

---

## Anti-Patterns

| Pattern | Problem | Correct |
|---|---|---|
| Convert without Source/ | No C++ project structure | Run `/bp-cpp-project-init` first |
| Convert Widget BP | UMG is not 1:1 | Skip or manual conversion |
| Convert AnimBP | AnimGraph needs AnimInstance | Use dedicated animation workflow |
| Convert Level BP | Not Actor-based | Extract to Actor class first |
| Skip validation | May miss broken references | Always run Phase 5 |
| Ignore TODO nodes | Incomplete migration | Address or document each TODO |
