---
name: blueprint-flow
model: opus
description: "Purpose: Blueprint execution flow. Structure analysis + execution tracing with dual methods (step-by-step, or a single atomic workflow shortcut). Input→Animation, BP structure, ability flow. For GAS/InputTag deep chain (5-stage), use cross-domain-flow. Not for creating new BPs (use blueprint-generator). Triggers: 'BP 실행 흐름', 'BP 분석', 'animation trace', '버튼 누르면', 'Blueprint 구조'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Blueprint Flow Workflow

**Priority**: High
**Goal**: Automate multi-tool workflow for Blueprint analysis with optimized execution paths

---

## Purpose

Orchestrates Blueprint analysis for **complete flow understanding**:

1. **Structure Analysis** - Get Blueprint graph structure (entry points, nodes, connections)
2. **Execution Tracing** - Trace execution from input to output
3. **Result Integration** - Merge structure and flow data with Mermaid visualization

**Key Innovation**: Hybrid approach with two execution methods:
- **Method A** (Step-by-step): 4 separate MCP calls for flexibility
- **Method B** (Workflow Shortcut): 1 atomic `ue_run_workflow` call — **Recommended**

**Trade-off**: Method B trades Method A's 4 MCP calls for 1 (fewer round-trips). No
speed/token/accuracy figures are benchmarked — see [REFERENCE.md § 5](REFERENCE.md#5-method-comparison).

---

## Trigger Phrases

### Korean

- "Blueprint 구조 분석", "BP 분석"
- "실행 흐름 추적", "실행 흐름 보여줘"
- "BP 내부 흐름", "Blueprint 노드 실행"
- "Blueprint 전체 분석", "BP 실행 흐름"
- "BP 그래프에서 뭐가 실행돼?"
- "E키 누르면 어떤 로직 트리거돼?"

**설정/파라미터 찾기 (Issue #3876 - 일반화)**:
- "어디서 설정", "어떻게 세팅", "설정값 찾기", "파라미터 찾기"
- "값 변경하고 싶", "설정 위치", "어디서 바꾸니"

**플로우/로직 이해**:
- "전체 순서", "로직 흐름", "어떻게 작동", "실행 흐름"
- "각 단계마다", "프로세스 흐름", "시퀀스"

**구조/구현 탐색**:
- "어디서 구현", "구조 파악", "연결 관계", "의존성"
- "어빌리티 추적", "어빌리티 흐름", "GA_ 트리거"

### English

- "Blueprint structure analysis", "BP analysis"
- "execution flow trace", "show execution flow"
- "BP internal flow", "Blueprint node execution"
- "full Blueprint analysis", "BP execution flow"
- "What runs in this BP graph?"
- "What does E key trigger?"

**Settings/Parameter discovery (Issue #3876 - generalized)**:
- "where to set", "how to configure", "find setting", "find parameter"
- "want to change value", "setting location", "where to modify"

**Flow/Logic understanding**:
- "full sequence", "logic flow", "how does it work", "execution flow"
- "each step", "process flow", "sequence"

**Structure/Implementation exploration**:
- "where implemented", "understand structure", "dependencies"
- "ability trace", "ability flow", "GA_ trigger"

**Keywords**: `BP flow`, `BP trace`, `BP execution`, `blueprint`, `BP 노드`, `BP 이벤트`, `GA_ trigger`, `BP 내부 흐름`, `Blueprint 구조`

---

## Workflow Overview

### Phase 0: Discovery Pre-Exploration (Issue #6289)

> Before calling `ue_manage_blueprint(get_structure)`, verify the Blueprint exists and identify its type.

```python
# 0a. Verify Blueprint exists and get basic info
ue_grep(query="<blueprint_name>", domain="assets", limit=5)
# Returns: BP type, variable count, function count (~50 tokens)

# 0b. If found, get structured detail
ue_read(identifier="<blueprint_name>")
# Returns: parent class, variables preview, functions preview (~100 tokens)
```

**Phase 0 Decision Logic**:
- BP found in metadata → Confirm name, proceed to Phase 1
- BP not found → Report "Blueprint not found", suggest similar names
- `ue_read` shows simple BP (few nodes) → Method B (workflow shortcut) recommended
- `ue_read` shows complex BP (many nodes/functions) → Method A (step-by-step) for flexibility

---

### 3-Phase Analysis Process

```text
Phase 1: Structure Analysis
│ Tool: ue_manage_blueprint(operation="get_structure")
│ Output: Graph nodes, connections, entry points
↓
Phase 2: Execution Tracing (Hybrid Method)
│ Method A: 4 individual MCP calls — for debugging/learning
│ Method B: ue_run_workflow shortcut (1 atomic call) — recommended default ⭐
│ Output: Execution path, function calls, animations
↓
Phase 3: Result Integration
│ Merge structure + trace data
│ Generate Mermaid visualization
│ Identify key execution paths
```

---

## Phase 2: Method Selection

### Method B: Workflow Shortcut (Default) ⭐

**Use when**: Standard flow tracing (Input → Animation), fast results needed

```python
result = ue_run_workflow(
    workflow="input-to-action",
    target="LeftMouseButton"
)
# → Complete flow in a single atomic workflow call
```

### Method A: Step-by-step (Baseline)

**Use when**: Cross-domain (C++↔BP), delegate tracking, step-by-step learning, debugging

**4 Separate MCP Calls**:

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

### Decision Algorithm

```python
def choose_method(user_request, flow_type):
    # Priority 1: Non-standard flows require Method A
    if flow_type in ["cross_domain", "delegate", "realtime", "widget"]:
        return "Method A"

    # Priority 2: User wants step-by-step
    if "step-by-step" in user_request or "단계별" in user_request:
        return "Method A"

    # Priority 3: Default to workflow for speed
    return "Method B ⭐"
```

---

## Tool Integration

### Primary Tools Used

| Tool | Operation | Purpose |
|------|-----------|---------|
| `ue_manage_blueprint` | `get_structure` | Get graph structure (Phase 1) |
| `ue_manage_blueprint` | `analyze_execution_depth` | Calculate complexity |
| `ue_trace_execution` | `trace_execution_flow` | Trace from entry point |
| `ue_trace_execution` | `trace_ability_flow` | GAS ability tracing |
| `ue_run_workflow` | `input-to-action` | Atomic flow trace (Method B) ⭐ |

### Routing Logic

```python
BLUEPRINT_ROUTING = {
    "structure": ("ue_manage_blueprint", "get_structure"),
    "depth": ("ue_manage_blueprint", "analyze_execution_depth"),
    "flow": ("ue_trace_execution", "trace_execution_flow"),
    "ability": ("ue_trace_execution", "trace_ability_flow"),
    "input": ("ue_trace_execution", "trace_from_input"),
}
```

**GAS decision rule** (Issue #3876): Ability queries (`ability_activation`, `ability_trigger`, `fire_rate`, `cooldown` — 발사 간격/쿨다운) route to `ue_manage_gameplay(trace_abilities)` FIRST (returns InputTag chain for Lyra-style projects); tag lookups → `ue_manage_gameplay(search_tags)`. Full dict: [REFERENCE.md § 9 GAS Routing Dictionary (Detail)](REFERENCE.md#9-gas-routing-dictionary-detail).

---

## Examples & Output

- 3-phase walkthrough ("BP_ShooterCharacter 실행 흐름"): [REFERENCE.md § 11 Quick Example Walkthrough](REFERENCE.md#11-quick-example-walkthrough).
- Method A 4-call step-by-step: [REFERENCE.md § 10 Phase 2 Method A — Step-by-step Example](REFERENCE.md#10-phase-2-method-a--step-by-step-example).
- Output template: [REFERENCE.md § 14 Output Format Template](REFERENCE.md#14-output-format-template).
- Method A vs B structure (4 MCP calls vs 1; no benchmarked figures): [REFERENCE.md § 12](REFERENCE.md#12-performance-comparison-method-a-vs-b).

---

## Failure Recovery

Quick rules: `get_structure` empty → verify name via `ue_search_assets`; `trace_execution_flow` fails → confirm valid EventGraph/InputAction entry point; `ue_run_workflow` timeout → Method A (lower `max_depth`); Ability mis-routed → `ue_manage_gameplay(trace_abilities)` per #3876; large BP (500+ nodes) → cap `max_depth` / subgraphs.

Full tables: [REFERENCE.md § 13 Common Failures](REFERENCE.md#13-common-failures) (KO), [REFERENCE.md § 15 Error Recovery (English)](REFERENCE.md#15-error-recovery-english). Companion docs + issue index: [REFERENCE.md § 16 Related Files](REFERENCE.md#16-related-files).

**Status**: Production (Merged v1.0.0 + v2.0.0)
**Last Updated**: 2026-02-13
