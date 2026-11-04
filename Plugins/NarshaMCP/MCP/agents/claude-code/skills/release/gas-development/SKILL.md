---
name: gas-development
model: opus
description: "Purpose: single GAS ability development. Tag analysis, ability flow tracing, reverse lookup (Ability→Key), symbol resolution via ue_manage_gameplay + ue_trace_execution + ue_analyze_symbols. Not for full Input→Animation deep chain (5-stage), use cross-domain-flow. Triggers: 'GAS 분석', 'Ability 개발', 'GameplayTag 찾기', 'Attribute 분석', 'GAS setup', 'ability trace'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# GAS Development Workflow

**Priority**: High
**Goal**: Automate 3-tool workflow for GAS development

---

## Purpose

Orchestrates Gameplay Ability System analysis for **complete GAS understanding**:

1. **Gameplay Analysis** - Tag hierarchy, ability configuration
2. **Execution Tracing** - Ability activation and effect flow
3. **Symbol Resolution** - C++ class and function lookup

**Key Difference from Individual Tools**:
- **Individual Tools**: Manual tool selection and parameter passing
- **GAS Development Workflow**: Automatic analysis→trace→resolve pipeline with context sharing

---

## Trigger Phrases

### Korean

- "GAS 분석", "Ability 개발"
- "GameplayTag 검색", "태그 계층 분석"
- "Attribute 분석", "AttributeSet 구조"
- "GameplayEffect 분석", "이펙트 흐름"
- "Ability 실행 흐름", "GA_ 분석"

### English

- "GAS analysis", "ability development"
- "GameplayTag search", "tag hierarchy"
- "Attribute analysis", "AttributeSet structure"
- "GameplayEffect analysis", "effect flow"
- "ability execution flow", "GA_ analysis"

---

## Workflow Overview

### 3-Phase Analysis Process

```text
Phase 1: Gameplay Analysis
│ Tool: ue_manage_gameplay(operation="search_tags")
│ Output: Tag hierarchy, ability configs, effect definitions
↓
Phase 2: Execution Tracing
│ Tool: ue_trace_execution(operation="trace_ability_flow")
│ Input: Abilities from Phase 1
│ Output: Activation path, effect application, attribute modification
↓
Phase 3: Symbol Resolution
│ Tool: ue_analyze_symbols(operation="search_symbols")
│ Input: Classes/functions from Phase 2
│ Output: C++ definitions, headers, implementation details
↓
Phase 3.5: Macro Validation (auto-trigger when AttributeSet detected)
│ Trigger: class name contains "AttributeSet" OR parent is UAttributeSet
│
│ Step 3.5a: Cross-check replication completeness
│ │ Tool: ue_analyze_source(operation="cross_check", class_name="<AttributeSet>")
│ │ Output: missing_registration (UPROPERTY(Replicated) without DOREPLIFETIME)
│ │         matched (correctly registered properties)
│ │         orphan_registration (DOREPLIFETIME without UPROPERTY)
│ │ Action: Warn user if missing_registration > 0
│ │
│ Step 3.5b: Validate ATTRIBUTE_ACCESSORS completeness
│ │ Tool: ue_analyze_source(operation="find_macro_invocations", macro_name="ATTRIBUTE_ACCESSORS")
│ │ Output: List of (class, attribute) pairs
│ │ Action: Cross-reference with Phase 1 attributes — warn if any attribute missing
│ │
│ Step 3.5c: Check GAMEPLAYATTRIBUTE_REPNOTIFY consistency
│   Tool: ue_analyze_source(operation="find_macro_invocations", macro_name="GAMEPLAYATTRIBUTE_REPNOTIFY")
│   Output: List of repnotify macro calls
│   Action: Cross-reference with cross_check matched[].on_rep_function
```

---

## Tool Integration

### Primary Tools Used

| Tool | Operation | Purpose |
|------|-----------|---------|
| `ue_manage_gameplay` | `search_tags` | Find GameplayTags |
| `ue_manage_gameplay` | `trace_abilities` | Trace ability relationships |
| `ue_trace_execution` | `trace_ability_flow` | Trace execution from ability |
| `ue_analyze_symbols` | `search_symbols` | Find C++ symbol definitions |
| `ue_analyze_symbols` | `trace_hierarchy` | Get class inheritance |
| `ue_analyze_source` | `cross_check` | Validate ATTRIBUTE_ACCESSORS + DOREPLIFETIME completeness |
| `ue_analyze_source` | `find_macro_invocations` | Find GAMEPLAYATTRIBUTE_REPNOTIFY calls |

### Routing Logic

```python
GAS_ROUTING = {
    "tag": ("ue_manage_gameplay", "search_tags"),
    "ability": ("ue_manage_gameplay", "trace_abilities"),
    "effect": ("ue_trace_execution", "trace_ability_flow"),
    "attribute": ("ue_analyze_symbols", "search_symbols"),
    "class": ("ue_analyze_symbols", "trace_hierarchy"),
    "attribute_validation": ("ue_analyze_source", "cross_check"),
}
```

---

## Quick Example

**User**: "GA_Attack의 실행 흐름과 적용되는 이펙트 분석해줘"

**GAS Development Workflow**:

```text
Phase 1: Gameplay Analysis
├─ Tool: ue_manage_gameplay(operation="trace_abilities")
├─ Target: GA_Attack
└─ Result:
   ├─ Tags: Ability.Attack, Ability.Melee
   ├─ Effects: GE_ApplyDamage, GE_AddCombo
   └─ Attributes: Health, Stamina (cost)

Phase 2: Execution Tracing
├─ Tool: ue_trace_execution(operation="trace_ability_flow")
├─ Ability: GA_Attack
└─ Result:
   ├─ Activation: CanActivate → CommitAbility → EndAbility
   ├─ Effect Application: GE_ApplyDamage → Target.Health
   └─ Cues: GC_Attack_Hit, GC_Attack_Miss

Phase 3: Symbol Resolution
├─ Tool: ue_analyze_symbols(operation="search_symbols")
├─ Query: UGA_Attack
└─ Result:
   ├─ Header: Source/MyGame/Abilities/GA_Attack.h
   ├─ Implementation: GA_Attack.cpp
   └─ Base Class: UGameplayAbility
```

---

## Analysis Types

### Tag Analysis
- **Input**: Tag pattern (e.g., "Ability.*")
- **Output**: Matching tags, hierarchy, usage locations

### Ability Analysis
- **Input**: Ability class name (e.g., "GA_Attack")
- **Output**: Configuration, granted tags, blocked tags, effects

### Effect Analysis
- **Input**: Effect class or ability
- **Output**: Modifiers, duration, period, stacking rules

### Attribute Analysis
- **Input**: AttributeSet or specific attribute
- **Output**: Base value, current value, clamping, replication

---

## Evaluation Criteria

### Quantitative Metrics

| Metric | Target | Measurement Method |
|--------|--------|-------------------|
| GAS analysis time | **<30s** | From query to complete ability flow |
| Tag resolution accuracy | **>95%** | Correctly identifies tag hierarchy |
| Ability flow trace accuracy | **>90%** | Complete activation→effect→cue chain |
| Multi-tool workflow completion | **>85%** | Successfully completes 3-stage workflow |

### Activation Test Cases

**Positive (5)** - Should activate:
1. "GA_Attack의 실행 흐름 분석해줘" → ✅ Activate (ability flow)
2. "Ability.Melee 태그 사용하는 Ability 찾아줘" → ✅ Activate (tag search)
3. "GE_ApplyDamage 이펙트 어떻게 적용돼?" → ✅ Activate (effect analysis)
4. "AttributeSet 구조 보여줘" → ✅ Activate (attribute analysis)
5. "이 Ability가 어떤 Cue를 트리거해?" → ✅ Activate (cue trace)

**Negative (7)** - Should NOT activate:
1. "NS_Fire 파티클 분석해줘" → ❌ DO NOT activate (ue_manage_niagara directly)
2. "ST_EnemyAI StateTree 분석해줘" → ❌ DO NOT activate (ue_manage_ai directly)
3. "M_Character 머티리얼 분석해줘" → ❌ DO NOT activate (material-analysis)
4. "GAS 설명해줘" → ❌ DO NOT activate (ue-explain — explanation, not development workflow)
5. "GAS 캐릭터 만들어줘" → ❌ DO NOT activate (ue-scaffold — character scaffolding)
6. "GA_Attack에서 입력받아 Animation까지 전체 흐름" → ❌ DO NOT activate (cross-domain-flow)
7. "GAS ability flow trace" → ❌ DO NOT activate (ue-execution-trace)

### Before/After Comparison

| Scenario | Without Skill | With Skill |
|----------|--------------|------------|
| Ability flow analysis | ~15 min (manual trace) | **<2 min** (automated) |
| Find abilities by tag | ~10 min (manual search) | **<1 min** (tag search) |
| Effect chain analysis | ~20 min (manual inspection) | **<3 min** (3-tool workflow) |

---

## Output Format

```
=== GAS Analysis: {AbilityName} ===

Phase 1: Gameplay Analysis
  Tags: {Tag1}, {Tag2}
  Effects: {GE_1}, {GE_2}
  Attributes: {Attr1} (cost), {Attr2} (modified)

Phase 2: Execution Trace
  Activation: CanActivate -> CommitAbility -> EndAbility
  Effect Chain: {GE_1} -> {Target}.{Attribute}
  Cues: {GC_1}, {GC_2}

Phase 3: Symbol Resolution
  Header: {SourcePath}/{ClassName}.h
  Base Class: {BaseClassName}
  Key Methods: {Method1}, {Method2}
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `search_tags` returns 0 results | Tag pattern mismatch or GameplayTags not registered in project | Broaden tag pattern (e.g., `Ability.*` instead of exact match); verify tags in GameplayTags.ini |
| `trace_ability_flow` incomplete chain | Editor not running for Blueprint trace portion | Show C++ symbol data only (Phase 3); note Blueprint trace requires Editor |
| Ability class not found in Phase 3 | Class uses Blueprint-only implementation without C++ base | Use `ue_manage_blueprint(get_structure)` as fallback to inspect BP-only ability |

## Success Criteria

- [x] Automatic gameplay→trace→symbols pipeline
- [x] 3-tool integration (ue_manage_gameplay + ue_trace_execution + ue_analyze_symbols)
- [x] Context passed between phases
- [x] Unified result with tag, flow, and symbol data
- [ ] Unit test coverage

---

## Related Files

For detailed information, see:

- **EXAMPLES.md** - Complete usage examples and scenarios
- **Workflow Implementation**: Rust dispatcher `MCP/native/mcp_server/src/tools/gameplay_gas.rs` (ue_manage_gameplay GAS ops) + `MCP/native/mcp_server/src/tools/analysis_trace/ability_flow.rs` (ue_trace_execution `trace_ability_flow`) — the Python MCP was retired in v7.0.0
- **Parent Issue**: [#1715](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1715)

---

**Status**: Phase 3 Implementation
**Last Updated**: 2025-12-01
