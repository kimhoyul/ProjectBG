# Advanced Features - Blueprint Flow Workflow

**Purpose**: Advanced workflow features beyond standard Input → Animation flow

**Topics**:

1. Cross-Domain Tracing (C++ ↔ Blueprint)
2. Delegate Tracking
3. Real-Time Updates (Delta JSON)
4. Widget Event Propagation
5. Method A vs B — Structural Difference

---

## 1. Cross-Domain Tracing (C++ ↔ Blueprint)

**Scenario**: Trace flow across C++ and Blueprint boundaries

### When to Use

- Blueprint calls C++ UFUNCTION
- C++ broadcasts delegates to Blueprint
- Native code interop debugging

### Method A Required

**Why**: Workflow doesn't support C++ ↔ Blueprint boundary crossing (yet)

### Example: Blueprint → C++ → Blueprint Chain

**User Query**: "SetActorLocation 호출하는 Blueprint 찾고, C++ 구현까지 추적해줘"

**Step 1**: Find Blueprint Calls

```python
bp_calls = ue_analyze_blueprint(
    operation="find_nodes",
    params={
        "node_type": "function",
        "title_pattern": "SetActorLocation"
    }
)
# Returns: BP_Enemy::MoveToPlayer uses SetActorLocation
```

**Step 2**: Find C++ Implementation

```python
cpp_impl = ue_analyze_symbols(
    operation="search_symbols",
    query="SetActorLocation",
    symbol_type="function"
)
# Returns: AActor::SetActorLocation (Engine/Source/Runtime/Engine/Private/Actor.cpp)
```

**Step 3**: Trace C++ Callers

```python
cpp_callers = ue_analyze_symbols(
    operation="find_callers",
    function_name="SetActorLocation"
)
# Returns direct C++ and Blueprint callers; repeat per frontier client-side if needed.
```

**Result**:

```text
Blueprint → C++ → Blueprint Chain:
1. BP_Enemy::MoveToPlayer (Blueprint)
2. AActor::SetActorLocation (C++)
3. AActor::UpdateComponentTransforms (C++)
4. USceneComponent::OnUpdateTransform (C++)
5. [Delegate] OnActorMoved → BP_CameraManager::TrackTarget (Blueprint)
```

**Cost**: multiple cross-domain queries (BP → C++ → BP); wall-clock not benchmarked.

---

## 2. Delegate Tracking

**Issue**: Issue #107 - Delegate Tracking Complete

### Features

- Delegate declaration → Binding → Broadcast → Execution
- Multi-cast delegate tracing
- Dynamic delegate resolution

### Method A Required

**Why**: Workflow doesn't include delegate-specific operations

### Example: OnDamageReceived Delegate Flow

**User Query**: "OnDamageReceived 델리게이트 선언부터 실행까지 추적해줘"

**Step 1**: Find Delegate Declaration

```python
delegate_decl = ue_analyze_blueprint(
    operation="find_nodes",
    params={
        "node_type": "delegate",
        "title_pattern": "OnDamageReceived"
    }
)
# Returns: BP_Character::OnDamageReceived (Multi-cast Delegate)
```

**Step 2**: Find Bind Calls

```python
bindings = ue_analyze_blueprint(
    operation="trace_delegate_flow",
    params={
        "delegate_name": "OnDamageReceived",
        "flow_type": "bindings"
    }
)
# Returns: 3 Blueprints bind to this delegate
```

**Step 3**: Trace Broadcast Sites

```python
broadcasts = ue_analyze_blueprint(
    operation="trace_delegate_flow",
    params={
        "delegate_name": "OnDamageReceived",
        "flow_type": "broadcast"
    }
)
# Returns: Broadcasted from BP_DamageSystem::ApplyDamage
```

**Step 4**: Trace Execution from Each Binding

```python
# For each binding, trace execution
for binding in bindings['bindings']:
    execution = ue_analyze_blueprint(
        operation="trace_execution_flow",
        params={
            "start_node": binding['bound_function'],
            "max_depth": 10
        }
    )
```

**Result**:

```text
Delegate Flow: OnDamageReceived

Declaration
   BP_Character::OnDamageReceived (Params: Damage, Instigator)

Bindings (3 found)
   1. BP_HealthComponent::OnDamageReceived_Handler
   2. BP_HitReaction::PlayHitAnimation
   3. BP_DamageUI::ShowDamageNumber

Broadcast Sites (1 found)
   BP_DamageSystem::ApplyDamage (Line 42)

Execution Flow
   Broadcast → 3 bound functions execute in parallel
   - HealthComponent: Reduce health by damage amount
   - HitReaction: Play hit animation based on damage direction
   - DamageUI: Show floating damage number
```

**Cost**: delegate-specific tracing; wall-clock not benchmarked.

---

## 3. Real-Time Updates (Delta JSON)

**Issue**: Issue #29 - Real-Time Blueprint Updates

### Feature

- Monitor Blueprint changes without full rebuild
- Delta JSON updates (only changed nodes)
- Hot-reload flow updates

### Method A Required

**Why**: Workflow operates on static metadata, not real-time updates

### Example: Monitor Blueprint Changes

**User Query**: "BP_PlayerController 수정하면서 실시간으로 흐름 변화 보여줘"

**Setup**: Enable real-time monitoring

```python
# (Hypothetical API - Issue #29 not fully implemented)
monitor = ue_analyze_blueprint(
    operation="start_realtime_monitor",
    params={
        "blueprint_name": "BP_PlayerController",
        "watch_events": ["node_added", "node_removed", "connection_changed"]
    }
)
```

**On Change**: Receive delta update

```python
# When user adds new node in Blueprint Editor
delta = {
    "change_type": "node_added",
    "node": {
        "type": "FunctionCall",
        "title": "PlaySound",
        "connections": ["InputAction_Attack", "EndAbility"]
    }
}

# Update flow visualization without full re-trace
updated_flow = apply_delta_to_flow(current_flow, delta)
```

**Cost**: incremental delta update avoids a full re-trace; wall-clock not benchmarked.

---

## 4. Widget Event Propagation

**Feature**: Trace UMG Widget button clicks → Blueprint logic

### Method A Required

**Why**: Widget-specific tracing not in workflow

### Example: Button Click to Gameplay

**User Query**: "WBP_MainMenu의 Play 버튼 누르면 뭐가 실행돼?"

**Step 1**: Find Widget Structure

```python
widget_structure = ue_analyze_blueprint(
    operation="get_structure",
    params={
        "structure_type": "widget",
        "widget_blueprint_name": "WBP_MainMenu"
    }
)
# Returns: Button_Play, Button_Settings, Button_Quit
```

**Step 2**: Trace Widget Event

```python
event_flow = ue_analyze_blueprint(
    operation="trace_widget_event",
    params={
        "widget_blueprint_name": "WBP_MainMenu",
        "widget_name": "Button_Play",
        "event_name": "OnClicked"
    }
)
# Returns: OnClicked → LoadLevel("MainLevel")
```

**Result**:

```text
Widget Event Flow: Button_Play OnClicked

UI Event
   WBP_MainMenu::Button_Play::OnClicked

Event Handler
   WBP_MainMenu::OnPlayButtonClicked (Custom Event)

Logic Flow
   1. OnPlayButtonClicked (Event)
   2. PlaySound2D → UI_Click_Sound
   3. CreateWidget → WBP_LoadingScreen
   4. OpenLevel → "MainLevel"
   5. RemoveFromParent (Remove menu)
```

**Cost**: single widget-event trace; wall-clock not benchmarked.

---

## 5. Method A vs B — Structural Difference

> **No benchmark exists for this skill.** Earlier revisions of this section
> presented a fabricated "10-iteration" study (average times, token counts,
> "90% success rate", and a "p < 0.001" statistical-significance claim) with no
> underlying measurement data or smoke behind any of it. All of those figures
> have been removed.

The one difference that is verifiable from the workflow definitions themselves
is the MCP call count for the standard input-to-action flow:

| Method | MCP Calls | Notes |
|--------|-----------|-------|
| **A (Manual)** | 4 (separate) | `trace_from_input` → `trace_input_flow` → `trace_execution_flow` → `search_assets` |
| **B (Workflow)** | 1 (atomic) | `ue_run_workflow("input-to-action", ...)` |

Method B issues fewer Claude tool-call round-trips; any speed/token benefit that
follows has not been benchmarked and must not be quoted as a figure.

---

## 6. Workflow vs Manual - When to Choose

### Decision Matrix

| Factor | Method B (Workflow) | Method A (Manual) |
|--------|---------------------|-------------------|
| **MCP Calls** | 1 (atomic) | 4 (separate) |
| **Flexibility** | Fixed flow | Any flow |
| **Cross-domain** | Not supported | Full support |
| **Delegates** | Not supported | Full support |
| **Real-time** | Not supported | Supported |
| **Learning** | Black box | Step-by-step |

### Recommendation Algorithm

```python
def choose_method(user_request, flow_type, user_intent):
    # Priority 1: Check if workflow supports flow type
    if flow_type in ["cross_domain", "delegate", "realtime", "widget"]:
        return "Method A"

    # Priority 2: Check user intent
    if "step-by-step" in user_request or "단계별" in user_request:
        return "Method A"

    if "learning" in user_intent or "debug" in user_intent:
        return "Method A"

    # Priority 3: Default to workflow for speed
    if flow_type == "standard_input_to_animation":
        return "Method B"

    # Fallback
    return "Method A"
```

---

## 7. Future Enhancements

### Planned Workflow Expansions (Phase 2)

1. **Cross-Domain Workflow** (`cpp-to-blueprint.md`)
   - C++ function → Blueprint callers → Execution flow
   - Estimated savings: 40-50% vs manual

2. **Delegate Workflow** (`delegate-complete.md`)
   - Declaration → Bindings → Broadcast → Execution
   - Estimated savings: 50-60% vs manual

3. **Widget Workflow** (`widget-event-flow.md`)
   - Button click → Event handler → Gameplay logic
   - Estimated savings: 45-55% vs manual

---

## Related

**Issues**:

- Issue #262 - Workflow-Skill integration POC
- Issue #1715 - Blueprint Flow Workflow
- Issue #107 - Delegate tracking
- Issue #29 - Real-time updates

**Documentation**:

- [SKILL.md](SKILL.md) - Main skill description
- [EXAMPLES.md](EXAMPLES.md) - Usage scenarios
- [REFERENCE.md](REFERENCE.md) - Complete reference

---

**Status**: Advanced features documented
**Last Updated**: 2026-02-13
