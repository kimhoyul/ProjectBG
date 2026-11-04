# Blueprint Flow Workflow - Complete Reference

Complete API reference for Blueprint flow analysis with hybrid Method A/B execution.

---

## Table of Contents

1. [3-Phase Workflow Details](#1-3-phase-workflow-details)
2. [MCP Tool Parameters](#2-mcp-tool-parameters)
3. [Workflow Definition](#3-workflow-definition)
4. [GAS Routing Logic](#4-gas-routing-logic)
5. [Method Comparison](#5-method-comparison)
6. [Error Handling](#6-error-handling)
7. [Output Formats](#7-output-formats)
8. [External Links](#8-external-links)

---

## 1. 3-Phase Workflow Details

### Phase 1: Structure Analysis

**Purpose**: Get Blueprint graph structure before tracing

**Tool**: `ue_analyze_blueprint(operation="get_structure")`

**Output**: Entry points, nodes, connections, components

### Phase 2: Execution Tracing (Hybrid)

#### Method A: Step-by-step (Baseline)

**Step 1: trace_from_input**

```python
ue_analyze_blueprint(
    operation="trace_from_input",
    params={"key": "{TARGET}"}
)
```

**Step 2: find_input_events**

```python
ue_analyze_blueprint(
    operation="find_input_events",
    params={"action_name": "IA_Attack"}
)
```

**Step 3: trace_execution_flow**

```python
ue_analyze_blueprint(
    operation="trace_execution_flow",
    params={
        "start_node": "K2Node_InputAction_123",
        "blueprint_name": "BP_PlayerCharacter",
        "max_depth": 10
    }
)
```

**Step 4: search_animations (optional)**

```python
ue_analyze_blueprint(
    operation="search_animations",
    params={"query": "Attack_Combo1"}
)
```

#### Method B: Workflow Shortcut

```python
ue_run_workflow(
    workflow="input-to-action",
    target="LeftMouseButton"
)
```

### Phase 3: Result Review

**Markdown Format**:

```markdown
Complete Execution Flow: {TARGET}

Input Mapping
   {Key} → {InputAction} ({Context})

Event Binding
   {InputAction} → {Blueprint}::{Event Node}

Blueprint Logic ({N} steps)
   1. {Step 1}
   2. {Step 2}
   ...

Animation (if applicable)
   {Montage Name} (Length: {Duration}s)
   - Frame {X}: {AnimNotify} ({Description})
```

---

## 2. MCP Tool Parameters

### ue_analyze_blueprint

**Operation**: `get_structure`

```python
{"operation": "get_structure", "params": {"blueprint_name": str, "project_root": str (optional)}}
```

**Operation**: `trace_from_input`

```python
{"operation": "trace_from_input", "params": {"key": str, "project_root": str (optional)}}
```

**Operation**: `find_input_events`

```python
{"operation": "find_input_events", "params": {"action_name": str, "blueprint_name": str (optional)}}
```

**Operation**: `trace_execution_flow`

```python
{"operation": "trace_execution_flow", "params": {
    "start_node": str, "blueprint_name": str, "max_depth": int (default: 10), "end_node": str (optional)
}}
```

**Operation**: `search_animations`

```python
{"operation": "search_animations", "params": {"query": str}}
```

**Operation**: `analyze_execution_depth`

```python
{"operation": "analyze_execution_depth", "params": {"blueprint_name": str}}
```

### ue_trace_execution

| Operation | Purpose | Parameters |
|-----------|---------|------------|
| `trace_execution_flow` | Trace from start node | `blueprint_name`, `start_node`, `max_depth` |
| `trace_ability_flow` | Trace GAS ability | `ability_name`, `blueprint_name` |

### ue_run_workflow

```python
{"workflow": str, "target": str}
```

**Available Workflows**:

- `input-to-action`: Hardware key → Execution flow
- `ability-tag-trace`: GameplayTag → Abilities/Blueprints
- `quick-inspect`: Class → Methods/Properties
- `animation-notify-impact`: AnimNotify → Gameplay impact

### ue_manage_blueprint

| Operation | Purpose | Parameters |
|-----------|---------|------------|
| `get_structure` | Get Blueprint overview | `blueprint_name` |
| `search_blueprints` | Find by pattern | `query`, `parent_class` |
| `get_functions` | List all functions | `blueprint_name` |
| `get_variables` | List all variables | `blueprint_name` |
| `visualize_structure` | Generate diagram | `blueprint_name` |

---

## 3. Workflow Definition

**Location**: `docs/user-guides/workflows/input-to-action.md`

**Metadata**:

```yaml
workflow_id: input-to-action
name: Input to Action Flow Tracer
category: gameplay
version: 1.0.0
estimated_time: 12-18 seconds
tags: [input, enhanced-input, blueprint, execution-flow]
token_usage: ~800 tokens
trigger_keywords:
  korean: ["키 누르면", "입력 추적", "E키", "버튼 눌렀을 때"]
  english: ["what happens when", "key press", "input trace", "button action"]
```

**Variables**: `{TARGET}` - Hardware key from user query

**Steps**: 3 (trace_from_input → find_input_events → trace_execution_flow)

---

## 4. GAS Routing Logic (Issue #3876)

### When to Apply

- Query contains: "어빌리티", "ability", "GA_", "쿨다운", "발사 속도", "cooldown", "fire rate"
- Lyra-style projects with InputTag → Ability chains

### Routing Table

```python
GAS_ROUTING = {
    "ability_activation": ("ue_manage_gameplay", "trace_abilities"),  # FIRST CHOICE
    "ability_trigger": ("ue_manage_gameplay", "trace_abilities"),
    "fire_rate": ("ue_manage_gameplay", "trace_abilities"),
    "cooldown": ("ue_manage_gameplay", "trace_abilities"),
    "tag_search": ("ue_manage_gameplay", "search_tags"),
}
```

### Priority

1. `ue_manage_gameplay(trace_abilities)` — FIRST for Ability queries
2. `ue_trace_execution(trace_ability_flow)` — if trace_abilities insufficient
3. `ue_analyze_blueprint(get_structure)` — for Blueprint-level structure

---

## 5. Method Comparison

The two methods differ in the number of MCP calls they issue for the standard
input-to-action flow — the one measure derivable from the workflow definitions
themselves:

| Metric | Method A (Baseline) | Method B (Workflow) |
|--------|---------------------|---------------------|
| **MCP Calls** | 4 (Steps 1-4) | 1 (`ue_run_workflow`) |
| **Claude tool-call round-trips** | 4 | 1 |

> **No benchmarked speed, token, or accuracy figures are published for this skill.**
> Earlier revisions of this doc quoted specific timings (e.g. "4.2s → 1.8s"),
> token counts, and "95% accuracy" / "90% success rate" numbers that had no
> benchmark or smoke behind them; they have been removed. The only verifiable
> benefit of Method B is the call-count reduction (4 → 1) and the fewer Claude
> tool-call round-trips that follows from it.

---

## 6. Error Handling

### Common Errors

**Key not mapped**:

```json
{"success": false, "error": "Key 'Q' has no InputAction mapping.", "suggestion": "Check InputMappingContext in Project Settings"}
```

**No event bindings**:

```json
{"success": false, "error": "InputAction 'IA_Attack' found but no Blueprint uses it.", "suggestion": "Use ue_analyze_symbols to find C++ handlers"}
```

**Workflow timeout**:

```json
{"success": false, "error": "Workflow Step 3 timeout", "suggestion": "Use Method A with max_depth=5"}
```

### Fallback Strategies

**If Method B fails** → Fallback to Method A

```python
try:
    result = ue_run_workflow("input-to-action", target)
except WorkflowTimeout:
    result = manual_trace_step_by_step(target)
```

**If Step 3 times out** → Reduce max_depth

```python
try:
    flow = ue_analyze_blueprint(operation="trace_execution_flow", max_depth=10)
except Timeout:
    flow = ue_analyze_blueprint(operation="trace_execution_flow", max_depth=5)
```

---

## 7. Output Formats

### Mermaid Diagram Template

```mermaid
graph TD
    A[{Key}] --> B[{InputAction}]
    B --> C[{Blueprint} Event]
    C --> D{{Condition}}
    D -->|Yes| E[{Ability}]
    E --> F[{Montage}]
    F --> G[{AnimNotify}]
    D -->|No| H[Ignore]

    style A fill:#f9f
    style E fill:#bbf
    style G fill:#bfb
```

### JSON Structure

```json
{
  "blueprint_name": "BP_Player",
  "functions": [],
  "variables": [],
  "components": []
}
```

### Entry Point Detection

| Entry Point | Description |
|-------------|-------------|
| `Event BeginPlay` | Actor initialization |
| `Event Tick` | Per-frame update |
| `InputAction.*` | Player input |
| `Event Construct` | Widget construction |

---

## 8. External Links

### Issues

- Issue #1715 - Blueprint Flow Workflow
- Issue #262 - Workflow-Skill integration POC
- Issue #3876 - GAS Tool Selection
- Issue #107 - Delegate tracking
- Issue #29 - Real-time updates

### Unreal Engine Documentation

- [Enhanced Input System](https://docs.unrealengine.com/5.0/enhanced-input-in-unreal-engine/)
- [Blueprint Execution Flow](https://docs.unrealengine.com/5.0/blueprint-execution-flow-in-unreal-engine/)
- [Gameplay Ability System](https://docs.unrealengine.com/5.0/gameplay-ability-system-in-unreal-engine/)

---

## 9. GAS Routing Dictionary (Detail)

Full `GAS_ROUTING` dict moved from SKILL.md. Issue #3876: GAS/Ability queries should use `trace_abilities` FIRST — this returns the InputTag chain for Lyra-style projects.

```python
# Issue #3876: GAS/Ability queries should use trace_abilities FIRST
# This returns InputTag chain for Lyra-style projects
GAS_ROUTING = {
    "ability_activation": ("ue_manage_gameplay", "trace_abilities"),  # FIRST CHOICE
    "ability_trigger": ("ue_manage_gameplay", "trace_abilities"),     # How is GA_X triggered?
    "fire_rate": ("ue_manage_gameplay", "trace_abilities"),           # 총을 쏘는 간격 → trace GA
    "cooldown": ("ue_manage_gameplay", "trace_abilities"),            # 쿨다운 시간 → trace GA
    "tag_search": ("ue_manage_gameplay", "search_tags"),              # What abilities use tag X?
}
```

---

## 10. Phase 2 Method A — Step-by-step Example

Worked 4-call Method A sequence moved from SKILL.md.

```python
# Step 1: Input Detection
result_1 = ue_trace_execution(operation="trace_from_input", key="LeftMouseButton")

# Step 2: Find Input Events (InputAction → Blueprint binding)
result_2 = ue_manage_gameplay(operation="trace_input_flow", action_name="IA_Attack")

# Step 3: Execution Flow (with optional visualization)
result_3 = ue_trace_execution(
    operation="trace_execution_flow",
    blueprint_name="BP_PlayerCharacter",
    start_node="K2Node_InputAction_123",
    max_depth=10,
    output_format="html",
)
# output_format="html" generates interactive HTML for browser-based debugging
# output_format="mermaid" generates Mermaid diagram for inline visualization

# Step 4: Animation Search (if mentioned in flow)
result_4 = ue_search_assets(asset_type="animation", query="Attack_Combo1")
```

---

## 11. Quick Example Walkthrough

Full 3-phase walkthrough moved from SKILL.md.

**User**: "BP_ShooterCharacter의 실행 흐름 분석해줘"

**Phase 1: Structure Analysis**
```python
ue_manage_blueprint(operation="get_structure", blueprint_name="BP_ShooterCharacter")
# → Entry Points: [InputAction IA_Attack, BeginPlay, Tick]
# → Key Nodes: 15 function calls, 3 custom events
```

**Phase 2: Execution Tracing (Method B)** ⭐
```python
result = ue_run_workflow("input-to-action", "LeftMouseButton")
# → Complete flow in a single atomic workflow call
```

**Phase 3: Result Integration**
```text
🎯 Complete Execution Flow: LeftMouseButton

📍 Input Mapping
   LeftMouseButton → IA_Attack (IMC_Default)

🔗 Event Binding
   IA_Attack → BP_PlayerCharacter::InputAction_IA_Attack

⚙️ Blueprint Logic (7 steps)
   1. InputAction_IA_Attack (Event)
   2. Branch → IsAttackReady
   3. ActivateAbilityByClass → GA_Attack_Melee
   4. PlayMontage → Attack_Combo1
   5. SendGameplayEvent → Event.Attack.Start
   6. SetTimerByEvent → ComboWindow (0.5s)
   7. PrintString → "Attack Started"

🎬 Animation
   Attack_Combo1 (Length: 1.2s)
   - Frame 15-20: ANS_AttackHit (Apply Damage)
   - Frame 10: AN_FootstepSound (Play Audio)
```

**MCP calls**: 1 atomic `ue_run_workflow` call (Method A would issue 4 separate calls); wall-clock not benchmarked.

---

## 12. Performance Comparison (Method A vs B)

Method comparison moved from SKILL.md. See § 5 for detail. The only verifiable
difference is the MCP call count for the standard input-to-action flow:

| Metric | Method A (Baseline) | Method B (Workflow) | Difference |
|--------|---------------------|---------------------|------------|
| **MCP Calls** | 4 | 1 | 4 → 1 (fewer round-trips) |

> No benchmarked speed, token, or accuracy figures are published for this skill.
> Previously quoted "57% faster / 62% token reduction / 95% accuracy" numbers were
> unsubstantiated and have been removed (see § 5).

---

## 13. Common Failures

Korean-language failure/cause/resolution table moved from SKILL.md.

| 에러 유형 | 원인 | 해결 방법 |
|-----------|------|-----------|
| `get_structure` 빈 결과 | Blueprint 이름 오타 또는 에셋 미로드 | `ue_search_assets`로 정확한 Blueprint 이름 확인 후 재시도 |
| `trace_execution_flow` 실패 | Phase 1에서 entry point를 찾지 못함 | Blueprint에 유효한 EventGraph 또는 InputAction이 있는지 확인 |
| GAS 라우팅 오류 | Ability 쿼리를 `ue_search_assets` 대신 `ue_manage_gameplay`로 보내지 않음 | Issue #3876 라우팅 로직 참조, `trace_abilities` 사용 |
| Phase 간 컨텍스트 유실 | Phase 1 결과가 Phase 2로 전달되지 않음 | 각 Phase 출력을 명시적으로 다음 Phase 입력에 전달 |
| 분석 타임아웃 | 대형 Blueprint (500+ 노드) 처리 시간 초과 | `--max-depth` 제한 또는 서브그래프 단위 분석 |
| `ue_run_workflow` 실패 | 등록되지 않은 workflow 이름 사용 | `input-to-action` 등 정확한 workflow 이름 확인 |
| Method B 타임아웃 | 복잡한 cross-domain 플로우 | Method A (step-by-step)로 전환하여 단계별 디버깅 |
| 빈 Ability 플로우 | `trace_ability_flow`에 잘못된 GA_ 클래스명 전달 | `ue_manage_gameplay(search_tags)`로 유효한 Ability 목록 먼저 조회 |

---

## 14. Output Format Template

Status-report output template moved from SKILL.md.

```
Blueprint Execution Flow: <Blueprint name>
──────────────────────────────────────────
Method: A (Step-by-step) | B (Workflow Shortcut)
Time: <seconds>

[Input Mapping]
  <Key> → <InputAction> (<IMC>)

[Event Binding]
  <InputAction> → <Blueprint>::<Event>

[Execution Steps] (<N> steps)
  1. <Node> (Event)
  2. <Node> → <condition>
  3. <Node> → <action>
  ...

[Animation] (if applicable)
  <Montage> (Length: <duration>)
  - <Notify> at <time>

Status: COMPLETE | PARTIAL | FAILED
```

---

## 15. Error Recovery (English)

English error/cause/fallback table moved from SKILL.md.

| Error | Cause | Fallback |
|-------|-------|----------|
| `get_structure` empty result | Blueprint name typo or asset not loaded | Use `ue_search_assets(asset_type="blueprint")` to find correct name |
| `trace_execution_flow` fails | No valid entry point found in Blueprint | Verify Blueprint has EventGraph or InputAction; try `get_structure` for static analysis |
| `ue_run_workflow` timeout | Complex cross-domain flow exceeds time limit | Switch to Method A (step-by-step) for incremental debugging |
| GAS routing error | Ability query sent to wrong tool | Use `ue_manage_gameplay(trace_abilities)` for InputTag chain resolution per Issue #3876 |

---

## 16. Related Files

Companion-doc and issue index moved from SKILL.md.

- **EXAMPLES.md** - Complete usage examples (Method A vs B comparison, 5+ scenarios)
- **ADVANCED.md** - Cross-domain tracing, delegate tracking, real-time updates, widget events
- **REFERENCE.md** - Complete stage details, Mermaid templates, tool parameters, performance metrics

**Issues**:
- Issue #1715 - Blueprint Flow Workflow (Phase 2)
- Issue #262 - Workflow-Skill integration POC
- Issue #3876 - GAS Tool Selection
- Issue #107 - Delegate tracking

---

**Last Updated**: 2026-02-13
