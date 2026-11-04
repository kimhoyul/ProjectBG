---
name: cross-domain-flow
model: opus
description: "Purpose: GAS-aware deep execution chain. Traces Input → InputTag → GameplayAbility → Blueprint → C++ → Animation via 5-tool orchestration with error recovery. For GAS/Lyra projects needing InputTag→Ability resolution. Lighter alternative without GAS: blueprint-flow. Triggers: 'GAS ability flow', 'InputTag chain', 'Ability 전체 흐름', '공격 Ability GAS 추적'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
allowed-tools:
  - mcp__narshamcp__ue_manage_gameplay
  - mcp__narshamcp__ue_manage_blueprint
  - mcp__narshamcp__ue_trace_execution
  - mcp__narshamcp__ue_analyze_symbols
  - mcp__narshamcp__ue_search_assets
argument-hint: "input action or ability name, depth: quick/full"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Cross-Domain Execution Flow

**Status**: Beta
**Purpose**: Answer "What happens when I press X?" with complete flow tracing

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the 5-Stage workflow automatically**.
> Do NOT just display documentation - actually run the tools!

### Execution Requirements

1. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user which input action or ability to trace.
2. **Execute ALL 5 stages sequentially**: Each stage feeds into the next
3. **Generate Complete Flow Report**: Combine all results into unified visualization
4. **Handle errors gracefully**: Use fallback strategies per stage

### Quick Start Template

When user says "공격 버튼 누르면?" or "What happens when I press attack?":

```python
# Stage 1: Find InputAction → InputTag
input_result = ue_manage_gameplay(operation="trace_input_flow", input_action="IA_Attack")
# → Extract InputTag from result

# Stage 2: Find InputTag → Ability
gas_result = ue_manage_gameplay(operation="trace_abilities", tag=input_result["input_tag"])
# → Extract Ability class from result

# Stage 3: Trace Ability Blueprint execution
bp_result = ue_trace_execution(operation="trace_ability_flow", ability_name=gas_result["ability"])
# → Extract key functions and montage names

# Stage 4: Find C++ implementations
cpp_result = ue_analyze_symbols(operation="find_callers", function_name=f"{gas_result["ability"]}::ActivateAbility")
# → Extract source file locations

# Stage 5: Find Animation assets
anim_result = ue_search_assets(asset_type="animation", query=bp_result["montage_name"])
# → Complete the flow with animation details
```markdown

---

## Purpose

5-tool orchestration that traces **complete execution flow across all domains**:

- **Input Layer**: InputAction → InputTag mapping
- **GAS Layer**: InputTag → Ability activation → Effect application
- **Blueprint Layer**: Ability BP nodes → Task execution
- **C++ Layer**: Native function implementations
- **Animation Layer**: Montages, notifies, state machines

**Key Value**: This workflow **cannot be done with single MCP calls** - requires 5 tools working together with context passing.

---

## Trigger Phrases

### Korean
- "공격 버튼 누르면 어떤 일이 일어나?"
- "점프 키 누르면 전체 흐름"
- "Input부터 Animation까지 추적"
- "GA_Attack 전체 실행 흐름"
- "이 Ability가 어떻게 애니메이션까지 연결돼?"

### English
- "What happens when I press attack?"
- "trace full execution flow from jump"
- "input to animation complete chain"
- "GA_Attack full execution path"
- "how does this ability connect to animation?"

**Keywords**: `execution flow`, `input to animation`, `cross-domain`, `what happens when`, `full trace`, `전체 흐름`, `실행 추적`

---

## 5-Stage Workflow

```mermaid
graph TD
    A[User Query] --> B[Stage 1: Input Layer]
    B --> C[ue_manage_gameplay: trace_input_flow]
    C --> D[Stage 2: GAS Layer]
    D --> E[ue_manage_gameplay: trace_abilities]
    E --> F[Stage 3: Blueprint Layer]
    F --> G[ue_trace_execution: trace_ability_flow]
    G --> H[Stage 4: C++ Layer]
    H --> I[ue_analyze_symbols: find_callers]
    I --> J[Stage 5: Animation Layer]
    J --> K[ue_search_assets: animation]
    K --> L[Complete Flow Report]
```markdown

---

## Stage 1: Input Layer

**Tool**: `ue_manage_gameplay(operation="trace_input_flow")`

**Goal**: Find what InputAction maps to what InputTag

```python
# Find the InputAction and its tag binding
input_result = ue_manage_gameplay(operation="trace_input_flow", input_action="IA_Attack")
# Returns: InputAction → InputTag mapping (e.g., "InputTag.Ability.Attack")
```text

**Output Example**:
```text
🎮 Stage 1: Input Layer

Input Action: IA_Attack
├─ Bound Key: Left Mouse Button
├─ Input Mapping Context: IMC_Default
└─ Maps to InputTag: InputTag.Ability.Attack
```markdown

---

## Stage 2: GAS Layer

**Tool**: `ue_manage_gameplay(operation="trace_abilities")`

**Goal**: Find which Ability activates from the InputTag

```python
# Find abilities bound to the InputTag
gas_result = ue_manage_gameplay(operation="trace_abilities", tag="InputTag.Ability.Attack")
# Returns: Abilities, effects, cues triggered
```text

**Output Example**:
```text
⚡ Stage 2: GAS Layer

InputTag: InputTag.Ability.Attack
├─ Activates Ability: GA_Melee_Attack
│   ├─ Ability Tags: Ability.Melee, Ability.Attack
│   ├─ Blocked By: State.Dead, State.Stunned
│   └─ Cost: 10 Stamina
│
├─ Applied Effects:
│   ├─ GE_ApplyDamage → Target.Health
│   ├─ GE_AddCombo → Self.ComboCount
│   └─ GE_PlayMontage → Animation
│
└─ Gameplay Cues:
    ├─ GC_Attack_Hit (on damage applied)
    └─ GC_Attack_Whoosh (on swing)
```markdown

---

## Stage 3: Blueprint Layer

**Tool**: `ue_trace_execution(operation="trace_ability_flow")`

**Goal**: Trace Blueprint execution within the Ability

```python
# Trace the ability's internal execution (with optional visualization)
bp_result = ue_trace_execution(operation="trace_ability_flow", ability_name="GA_Melee_Attack", output_format="mermaid")
# Returns: BP node execution order, tasks, events (+ Mermaid/HTML if requested)
```text

**Output Example**:
```text
📘 Stage 3: Blueprint Layer

GA_Melee_Attack Execution Flow:
├─ ActivateAbility
│   ├─ [1] Check CanActivate → True
│   ├─ [2] CommitAbilityCost (Stamina: -10)
│   ├─ [3] PlayMontageAndWait (AM_Attack_Combo)
│   │       └─ Montage Section: "Attack_1"
│   └─ [4] WaitGameplayEvent (Event.Montage.Hit)
│
├─ OnHitEventReceived
│   ├─ [5] GetTargetsInRange (radius: 150)
│   ├─ [6] ApplyGameplayEffectToTarget (GE_ApplyDamage)
│   └─ [7] ExecuteGameplayCue (GC_Attack_Hit)
│
└─ OnMontageCompleted
    ├─ [8] IncrementComboCount
    └─ [9] EndAbility
```markdown

---

## Stage 4: C++ Layer

**Tool**: `ue_analyze_symbols(operation="find_callers")`

**Goal**: Find C++ implementations for key functions

```python
# Find native implementations
cpp_result = ue_analyze_symbols(operation="find_callers", function_name="UGA_Melee_Attack::ActivateAbility")
# Returns direct C++/Blueprint caller locations; expand another round client-side if needed.
```text

**Output Example**:
```text
💻 Stage 4: C++ Layer

C++ Implementations:
├─ UGA_Melee_Attack::ActivateAbility
│   └─ Source/Abilities/GA_Melee_Attack.cpp:42
│
├─ Inherited from: UGameplayAbility
│   └─ Engine/Plugins/GameplayAbilities/Source/GameplayAbilities.cpp:1234
│
├─ Key Native Functions:
│   ├─ UAbilityTask_PlayMontageAndWait::CreateTask
│   │   └─ Engine/Plugins/GAS/AbilityTasks/AbilityTask_PlayMontage.cpp:89
│   │
│   └─ UGameplayAbility::ApplyGameplayEffectToTarget
│       └─ Engine/Plugins/GAS/GameplayAbility.cpp:567
│
└─ Overridden Methods:
    ├─ CanActivateAbility() - GA_Melee_Attack.cpp:28
    └─ GetCooldownTags() - GA_Melee_Attack.cpp:35
```markdown

---

## Stage 5: Animation Layer

**Tool**: `ue_search_assets(asset_type="animation")`

**Goal**: Find animation assets involved in the flow

```python
# Find animation assets
anim_result = ue_search_assets(asset_type="animation", query="AM_Attack*")
# Returns: Animation sequences, notifies, state machine connections
```text

**Output Example**:
```text
🎬 Stage 5: Animation Layer

Animation Montage: AM_Attack_Combo
├─ Path: /Game/Animations/Combat/AM_Attack_Combo
├─ Duration: 1.2s
├─ Skeleton: SK_Mannequin
│
├─ Sections:
│   ├─ Attack_1 (0.0s - 0.4s)
│   ├─ Attack_2 (0.4s - 0.8s)
│   └─ Attack_3 (0.8s - 1.2s)
│
├─ Anim Notifies:
│   ├─ AN_Hit (0.25s) → Triggers Event.Montage.Hit
│   ├─ AN_ComboWindow (0.3s - 0.5s) → Allows combo input
│   └─ AN_FootStep (0.6s) → Sound/VFX
│
└─ Notify States:
    └─ ANS_WeaponTrail (0.1s - 0.3s) → Weapon trail VFX
```markdown

---

## Complete Flow Report

After all 5 stages, generate unified report:

```text
================================================================================
🎯 COMPLETE EXECUTION FLOW: "Press Attack Button"
================================================================================

📍 SUMMARY
Total Flow Time: ~1.2 seconds
Domains Crossed: Input → GAS → Blueprint → C++ → Animation

📍 FLOW VISUALIZATION

[Input]                    [GAS]                      [Blueprint]
Left Mouse Button ────────► InputTag.Ability.Attack ──► GA_Melee_Attack
                                     │                        │
                                     ▼                        ▼
                            Check Tags/Cost ◄────────── CommitAbilityCost
                                     │                        │
                                     ▼                        ▼
[Animation]               [C++]                      [Blueprint]
AM_Attack_Combo ◄──────── PlayMontageAndWait ◄────── PlayMontageAndWait
       │                         │
       ▼                         ▼
AN_Hit (0.25s) ──────────► Event.Montage.Hit ──────► OnHitEventReceived
       │                                                    │
       ▼                                                    ▼
[VFX/Sound]              [GAS]                       [Damage]
GC_Attack_Hit ◄───────── ExecuteGameplayCue ◄────── ApplyGE_Damage
                                                            │
                                                            ▼
                                                    Target.Health -= Damage

📍 KEY TIMESTAMPS
0.00s - Button pressed, ability activates
0.05s - Stamina cost committed
0.10s - Montage starts playing
0.25s - AN_Hit triggers, damage applied
0.30s - Combo window opens
0.50s - Combo window closes
1.20s - Montage ends, ability completes

📍 FOUND FILES
- Source/Abilities/GA_Melee_Attack.cpp:42 (ActivateAbility)
- Source/Abilities/GA_Melee_Attack.cpp:28 (CanActivateAbility)
- /Game/Animations/Combat/AM_Attack_Combo.uasset
- /Game/Blueprints/Abilities/GA_Melee_Attack.uasset

================================================================================
```markdown

---

## Evaluation Criteria

### Auto-Execution Verification (v1.1.0)

| Criteria | Pass Condition | Fail Condition |
|----------|----------------|----------------|
| **Auto-Execute** | Tools called immediately after skill load | Only documentation displayed |
| **5-Stage Complete** | All 5 stages executed with results | Some stages skipped without fallback |
| **Error Recovery** | Fallback used when primary fails | Stage fails without recovery attempt |
| **Context Passing** | Stage N uses Stage N-1 results | Stages run independently |

### Quantitative Metrics

| Metric | Target | Measurement |
|--------|--------|-------------|
| Full flow trace time | **<3 min** | From query to complete report |
| Domain coverage | **100%** | All 5 domains traced |
| C++ source accuracy | **>90%** | Correct file/line numbers |
| Animation asset accuracy | **>95%** | Correct montage/notify mapping |
| **Auto-execution rate** | **100%** | Tools called without user re-prompt |

### Activation Test Cases

**Positive (5)** - Should activate:
1. "공격 버튼 누르면 어떤 일이 일어나?" → ✅ Activate
2. "GA_Attack 전체 실행 흐름 추적해줘" → ✅ Activate
3. "Input to animation complete chain" → ✅ Activate
4. "점프 키 누르면 뭐가 실행돼?" → ✅ Activate
5. "이 Ability가 애니메이션까지 어떻게 연결돼?" → ✅ Activate

**Negative (3)** - Should NOT activate:
1. "GA_Attack Ability 분석해줘" → ❌ Use gas-development skill
2. "AnimBP 구조 보여줘" → ❌ Use ue_manage_blueprint with AnimBP operations
3. "InputAction 에셋 찾아줘" → ❌ Use ue_search_assets directly

### Before/After Comparison

| Scenario | Without Skill | With Skill |
|----------|--------------|------------|
| Trace full attack flow | ~30 min (manual) | **<3 min** (automated) |
| Find animation from ability | ~10 min (manual) | **<1 min** (direct link) |
| Understand complex chain | ~1 hour (manual) | **<5 min** (complete) |

---

## Error Recovery (Stage-by-Stage Fallback)

Each stage has specific fallback strategies when primary tool fails:

| Stage | Primary Tool | Error Condition | Fallback Strategy |
|-------|--------------|-----------------|-------------------|
| **1. Input** | `trace_input_flow` | InputAction not found | `ue_search_assets(asset_type="inputaction")` - list all available |
| **2. GAS** | `trace_abilities` | No ability for tag | `ue_manage_gameplay(operation="search_tags", tag="Ability.*")` - find similar |
| **3. Blueprint** | `trace_ability_flow` | BP trace fails | `ue_manage_blueprint(operation="get_structure")` - get static structure |
| **4. C++** | `find_callers` | Symbol not found | Show message: "Engine PDB 인덱싱 필요. Run: `ue_check_health()` to verify" |
| **5. Animation** | `search_assets` | No montage found | Search AnimSequence: `ue_search_assets(asset_type="animation", query="*Attack*")` |

### Fallback Code Template

```python
def execute_with_fallback(stage, primary_call, fallback_call):
    try:
        result = primary_call()
        if not result or "error" in result:
            raise ValueError(f"Stage {stage} returned empty/error")
        return result
    except Exception as e:
        logger.warning(f"Stage {stage} failed: {e}")
        logger.info("Attempting fallback...")
        return fallback_call()

# Stage 1 with fallback
input_result = execute_with_fallback(stage=1, primary_call=lambda: ue_manage_gameplay(operation="trace_input_flow", input_action="IA_Attack"), fallback_call=lambda: ue_search_assets(asset_type="inputaction", query="*Attack*"))

# Stage 4 with fallback (PDB not indexed)
cpp_result = execute_with_fallback(stage=4, primary_call=lambda: ue_analyze_symbols(operation="find_callers", function_name=ability_class), fallback_call=lambda: {"warning": "C++ symbols not indexed. Project PDB required.", "suggestion": "Run ue_check_health() to verify PDB status"})
```markdown

### Graceful Degradation Modes

| Available Resources | Analysis Depth | Stages Executed |
|---------------------|----------------|-----------------|
| ✅ Full (PDB + Editor) | **Complete** | All 5 stages |
| ⚠️ No PDB | **Partial C++** | 1-3, 5 (skip 4) |
| ⚠️ No Ability System | **Animation Only** | 5 only |
| ❌ Nothing found | **Discovery Mode** | List available InputActions/Abilities |

---

## Related Files

- **EXAMPLES.md** - Complete flow examples for common scenarios
- **REFERENCE.md** - Tool parameter specifications

---

## Output Format

```text
Complete Execution Flow: "<user action>"
────────────────────────────────────────
Domains Crossed: Input → GAS → Blueprint → C++ → Animation
Total Flow Time: ~<N> seconds

[Stage 1: Input]    <Key> → <InputAction> → <InputTag>
[Stage 2: GAS]      <InputTag> → <Ability> (Cost: <cost>)
[Stage 3: Blueprint] <N> execution steps (entry → montage → damage → end)
[Stage 4: C++]      <source file>:<line> (<function>)
[Stage 5: Animation] <Montage> (<duration>, <N> notifies)

Key Timestamps:
  0.00s - <event>
  <time> - <event>
  ...

Found Files:
  - <source paths>
  - <asset paths>

Status: COMPLETE | PARTIAL (skipped: <stages>)
```

**Status**: Beta (New high-value 5-tool skill)
**Related**: Issue #4442
## MCP Tool Examples

```python
# Analyze Blueprint flow in cross-domain context
ue_manage_blueprint(operation="smart", blueprint_name="BP_GameMode")
```
