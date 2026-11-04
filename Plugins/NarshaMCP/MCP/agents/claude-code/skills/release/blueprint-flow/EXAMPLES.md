# Blueprint Flow Workflow - Examples

**Purpose**: Complete usage scenarios comparing Method A (step-by-step) vs Method B (workflow shortcut)

**Key**: Method B (Workflow) is recommended for standard flows.

---

## Example 1: Character Attack Flow (Method B - Workflow)

**Scenario**: User wants to understand the complete attack execution flow.

**User Input**: "BP_ShooterCharacter의 실행 흐름을 분석해줘"

### Phase 1: Structure Analysis

```python
ue_analyze_blueprint(
    operation="get_structure",
    params={
        "blueprint_name": "BP_ShooterCharacter",
        "project_root": "E:/MyProject"
    }
)
```

Result:

```json
{
  "blueprint": "BP_ShooterCharacter",
  "entry_points": [
    {"name": "InputAction IA_Attack", "type": "input"},
    {"name": "BeginPlay", "type": "event"},
    {"name": "Tick", "type": "event"}
  ],
  "nodes": {"total": 45, "function_calls": 15, "custom_events": 3, "branches": 8},
  "connections": 42
}
```

### Phase 2: Execution Tracing (Method B)

```python
result = ue_run_workflow("input-to-action", "LeftMouseButton")
# → Complete flow in a single atomic workflow call
```

### Phase 3: Result Integration

```text
Blueprint Flow Analysis: BP_ShooterCharacter

Entry Point: InputAction IA_Attack

Execution Flow:
  1. InputAction IA_Attack (Input)
  2. Branch: CanAttack?
  3.    TryActivateAbility(GA_Attack)
  4.       PlayMontage(AM_Attack_Primary)
  5.          AnimNotify_ApplyDamage
  6.             ApplyGameplayEffectToTarget

Analysis:
  - Execution Depth: 5 levels
  - GAS Integration: Yes (GA_Attack)
  - Animation Assets: AM_Attack_Primary
  - Condition Checks: 1 (CanAttack)
```

**Why Method B**: Standard flow, user wants quick overview, single atomic workflow call.

---

## Example 2: Jump Flow (Method A - Step-by-step)

**Scenario**: User wants step-by-step explanation of Jump logic.

**User Input**: "Jump 버튼 눌렀을 때 각 단계별로 설명해줘"

### Phase 2: Step-by-Step (Method A)

**Step 1**: Input Mapping

```python
result_1 = ue_analyze_blueprint(operation="trace_from_input", params={"key": "SpaceBar"})
# Result: SpaceBar → IA_Jump
```

"Space키가 IA_Jump InputAction에 매핑되어 있습니다."

**Step 2**: Event Binding

```python
result_2 = ue_analyze_blueprint(operation="find_input_events", params={"action_name": "IA_Jump"})
# Result: BP_PlayerCharacter::InputAction_IA_Jump (Node: K2Node_123)
```

"IA_Jump 이벤트는 BP_PlayerCharacter의 EventGraph에서 처리됩니다."

**Step 3**: Execution Flow

```python
result_3 = ue_analyze_blueprint(operation="trace_execution_flow", params={
    "start_node": "K2Node_123", "blueprint_name": "BP_PlayerCharacter", "max_depth": 10
})
```

"실행 흐름:
1. InputAction_IA_Jump (Event)
2. CanJump? (Branch) → 점프 가능 여부 확인
3. LaunchCharacter (Function Call) → 캐릭터에 Z 속도 적용
4. PlayMontage → Jump_Start (애니메이션 재생)
5. SetJumpCount++ (Variable Set) → 점프 카운트 증가"

**MCP Calls**: 4 separate calls (Steps 1-4) — see SKILL.md § Phase 2 Method A.

**Why Method A**: User explicitly requested step-by-step ("단계별로 설명해줘").

---

## Example 3: GAS Ability Flow (Method B - Workflow)

**Scenario**: GAS Ability activation flow.

**User Input**: "Ability.Attack.Heavy 태그 발동 흐름 보여줘"

### Phase 2: Workflow Shortcut (Method B)

```python
result = ue_run_workflow("input-to-action", "RightMouseButton")
```

### Phase 3: Result

```text
Complete Execution Flow: RightMouseButton

Input → IA_HeavyAttack

Logic (8 steps)
   1. InputAction_IA_HeavyAttack
   2. Branch → HasEnoughStamina
   3. ActivateAbilityByClass → GA_Attack_Heavy
   4. [GAS Internal] → ActivateAbility()
   5. CommitAbility → Consume Stamina (30)
   6. PlayMontageAndWait → Attack_Heavy_Swing
   7. SendGameplayEventToActor → Ability.Attack.Heavy
   8. WaitGameplayEvent → Ability.Attack.Heavy.Hit

Animation → Attack_Heavy_Swing (2.1s)
   - Frame 30-40: ANS_HeavyAttackHit (200 Damage)
```

**Why Method B**: Standard GAS flow (single atomic workflow call).

---

## Example 4: GAS Ability with Routing (Phase 1 + GAS_ROUTING)

**Scenario**: User asks about ability activation - requires GAS routing (Issue #3876).

**User Input**: "GA_Dash 어빌리티 실행 흐름 전체 분석"

### Phase 1: Structure Analysis

```python
ue_analyze_blueprint(operation="get_structure", params={"blueprint_name": "GA_Dash"})
```

Result:

```json
{
  "blueprint": "GA_Dash",
  "parent_class": "UGameplayAbility",
  "entry_points": [
    {"name": "ActivateAbility", "type": "override"},
    {"name": "EndAbility", "type": "override"}
  ],
  "gameplay_tags": {
    "ability_tags": ["Ability.Movement.Dash"],
    "block_tags": ["State.Dead", "State.Stunned"]
  }
}
```

### GAS Routing (Issue #3876)

Since query contains "어빌리티", use `ue_manage_gameplay(trace_abilities)` FIRST:

```python
ue_manage_gameplay(operation="trace_abilities", params={"ability_name": "GA_Dash"})
```

### Phase 2: Trace Ability Flow

```python
ue_trace_execution(operation="trace_ability_flow", params={"ability_name": "GA_Dash"})
```

### Phase 3: Result Integration

```text
Blueprint Flow Analysis: GA_Dash

Ability: GA_Dash (UGameplayAbility)

Gameplay Tags:
  - Ability Tags: Ability.Movement.Dash
  - Block Tags: State.Dead, State.Stunned

Activation Flow:
  1. ActivateAbility (Entry)
  2. CommitAbility (Check cost/cooldown)
  3.    PlayMontage(AM_Dash)
  4.       ApplyRootMotionForce
  5.          WaitForMontageEnd
  6.             EndAbility

Cost & Cooldown:
  - Cost: GE_DashCost (Stamina -20)
  - Cooldown: GE_DashCooldown (2.0s)
```

---

## Example 5: Cross-Domain Tracing (Method A - Manual)

**Scenario**: C++ → Blueprint tracing requires Method A.

**User Input**: "C++에서 SetActorLocation 호출하는 곳 찾고, Blueprint까지 추적해줘"

### Phase 2: Manual Cross-Domain (Method A)

**Why not Method B?** Workflow doesn't support C++ → Blueprint cross-domain tracing.

```python
# Step 1: Find C++ Callers
cpp_callers = ue_analyze_symbols(operation="find_callers", function_name="SetActorLocation")

# Step 2: Identify Blueprint Calls
for caller in cpp_callers['callers']:
    bp_calls = ue_analyze_blueprint(operation="find_nodes", params={
        "node_type": "function", "title_pattern": caller['function']
    })

# Step 3: Trace Blueprint Flow
flow = ue_analyze_blueprint(operation="trace_execution_flow", params={
    "start_node": bp_call_node, "max_depth": 5
})
```

**Why Method A**: Cross-domain not in workflow (multiple separate MCP calls).

---

## Example 6: UI Button Flow (Phase 1 Structure)

**Scenario**: Trace what happens when a UI button is clicked.

**User Input**: "WBP_MainMenu의 Play 버튼 클릭하면 어떻게 되는지 추적해줘"

### Phase 1: Structure Analysis

```python
ue_analyze_blueprint(operation="get_structure", params={"blueprint_name": "WBP_MainMenu"})
```

### Phase 2: Execution Tracing

```python
ue_trace_execution(operation="trace_execution_flow", params={
    "blueprint_name": "WBP_MainMenu", "entry_point": "OnClicked_PlayButton"
})
```

### Phase 3: Result

```text
Blueprint Flow Analysis: WBP_MainMenu

Entry Point: OnClicked_PlayButton

Execution Flow:
  1. OnClicked_PlayButton (Delegate)
  2. PlaySound2D(S_ButtonClick)
  3. OpenLevel(L_Gameplay)
  4. RemoveFromParent()

Key Insights:
  - Standard button flow with audio feedback
  - Direct level loading
  - Widget self-destructs after action
```

---

## Decision Tree: Method A vs Method B

| Scenario | Method | Reason |
|----------|--------|--------|
| Attack Button | B | Standard flow (single atomic workflow call) |
| Jump (Learning) | A | User requested step-by-step |
| GAS Ability | B | Standard GAS flow |
| GAS with Routing | A+GAS | Needs ue_manage_gameplay first |
| Cross-Domain | A | Workflow doesn't support |
| UI Button | A | Widget-specific tracing |

**Quick Rules**:

- Use **Method B** (Workflow): Standard flow, fast results, default choice
- Use **Method A** (Manual): Cross-domain, delegates, step-by-step learning, debugging
- Use **GAS Routing** (Issue #3876): Any query with "어빌리티", "ability", "GA_", "쿨다운", "발사 속도"

---

## Integration with Error Resolution

If Blueprint Flow analysis reveals missing nodes or errors:
1. Blueprint Flow detects issue
2. Automatically triggers Error Resolution workflow
3. Fixes applied
4. Blueprint Flow re-runs for verification

---

**Last Updated**: 2026-02-13
