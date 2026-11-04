# GAS Development — Reference

**Version**: 1.0.0
**Skill**: gas-development

---

## MCP Tools Used

### Primary Tools

| Tool | Purpose | Key Operations |
|------|---------|----------------|
| `ue_manage_gameplay` | GAS data queries | `smart`, `search_tags`, `trace_input_flow`, `validate_all_flows` |
| `ue_trace_execution` | Ability flow tracing | `trace_ability_flow`, `trace_gameplay_tag`, `trace_from_input` |
| `ue_analyze_symbols` | C++ class/function resolution | `search_symbols`, `find_callers`, `trace_hierarchy` |

### Supplementary Tools

| Tool | When Used |
|------|-----------|
| `ue_analyze_source` | Cross-check replication (UPROPERTY↔DOREPLIFETIME) |
| `ue_grep` | Step 0 discovery for GAS-related source code |
| `ue_manage_blueprint` | When GAS logic lives in Blueprint (GA_ nodes) |

---

## Tool Parameters

### ue_manage_gameplay

```python
# Tag search
ue_manage_gameplay(operation="smart", tag="Ability.Attack")
# → Returns: tag hierarchy, registered abilities, input bindings

# Input flow trace
ue_manage_gameplay(operation="trace_input_flow", params={"input_action": "IA_Attack"})
# → Returns: InputAction → InputTag → AbilitySpec → GA_ chain

# Full validation
ue_manage_gameplay(operation="validate_all_flows")
# → Returns: orphan tags, unbound abilities, missing input mappings
```

### ue_trace_execution

```python
# Ability flow
ue_trace_execution(operation="trace_ability_flow", params={"ability_name": "GA_Melee"})
# → Returns: ActivateAbility → CommitAbility → ApplyGameplayEffect → EndAbility

# Tag propagation
ue_trace_execution(operation="trace_gameplay_tag", params={"tag": "Ability.Attack"})
# → Returns: all abilities listening to or granting this tag

# Input to ability
ue_trace_execution(operation="trace_from_input", params={"key": "LeftMouseButton"})
# → Returns: InputAction → InputTag → GA_ activation chain
```

### ue_analyze_symbols

```python
# Find GAS class
ue_analyze_symbols(operation="smart", query="UAbilitySystemComponent")
# → Returns: class methods, hierarchy, callers

# Find ability implementation
ue_analyze_symbols(operation="find_callers", params={"function_name": "ActivateAbility"})
# → Returns: all classes that override ActivateAbility
```

---

## 3-Phase Workflow

### Phase 1: Gameplay Analysis
Query tag hierarchies, ability configurations, and input bindings.
```
ue_manage_gameplay(smart, tag=...) → tag tree + ability list
```

### Phase 2: Execution Tracing
Trace ability activation flows and effect chains.
```
ue_trace_execution(trace_ability_flow, ability_name=...) → execution graph
```

### Phase 3: Symbol Resolution
Resolve C++ implementations for deeper understanding.
```
ue_analyze_symbols(smart, query=...) → class/function details
```

---

## Return Value Structures

### Tag Search Result
```json
{
  "tag": "Ability.Attack.Melee",
  "parent": "Ability.Attack",
  "children": ["Ability.Attack.Melee.Light", "Ability.Attack.Melee.Heavy"],
  "abilities": ["GA_MeleeAttack", "GA_HeavyAttack"],
  "input_binding": "IA_Attack → InputTag.Attack"
}
```

### Ability Flow Result
```json
{
  "ability": "GA_MeleeAttack",
  "activation": "ActivateAbility()",
  "steps": [
    "CommitAbility()",
    "PlayMontageAndWait(AM_Attack)",
    "ApplyGameplayEffectToTarget(GE_Damage)",
    "EndAbility()"
  ],
  "effects_applied": ["GE_Damage", "GE_Cooldown"]
}
```

---

## Error Conditions

| Error | Cause | Recovery |
|-------|-------|----------|
| Tag not found | Tag name mismatch or not registered | Try broader search with `ue_grep` |
| No ability flow | GA_ not connected to input | Check `validate_all_flows` for orphan abilities |
| Symbol not indexed | PDB not loaded | Suggest `/ue-engine-content` for engine symbols |
