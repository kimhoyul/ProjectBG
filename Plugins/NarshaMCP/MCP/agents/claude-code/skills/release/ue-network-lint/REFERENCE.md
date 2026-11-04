# UE Network Lint — Rule Reference

**Version**: 1.0.0
**Parent**: [SKILL.md](SKILL.md)

---

## Rule Detail Table (NET-001~010)

| Rule | Name | Severity | Grep Pattern | Analysis Method | FP Risk | Fix Suggestion |
|------|------|----------|-------------|-----------------|---------|----------------|
| NET-001 | RPC ownership mismatch | Warning | `UFUNCTION.*Server` in *.h | `ue_read` call sites → check `IsLocallyControlled()` or `HasAuthority()` within ~10 lines | Medium | Add `if (IsLocallyControlled())` guard before Server RPC call |
| NET-002 | Multicast RPC for persistent state | Warning | `UFUNCTION.*NetMulticast` in *.h | `ue_read` body → flag member variable assignment (not transient FX) | Medium | Replace with `UPROPERTY(ReplicatedUsing=OnRep_X)` for persistent state |
| NET-003 | GetPlayerController(0) usage | Error | `GetPlayerController(0)` or `GetPlayerController( 0)` | `ue_read` context → check `#if WITH_EDITOR` / `IsRunningDedicatedServer()` guards | Low | Use `GetOwningPlayerController()` or wrap in `if (!IsRunningDedicatedServer())` |
| NET-004 | BeginPlay RPC without Authority | Warning | `::BeginPlay(` in *.cpp | `ue_read` body → find RPC calls without `HasAuthority()` wrap | Low | Add `if (HasAuthority()) { ServerRpc(); }` guard |
| NET-005 | OnRep C++/BP difference | Warning | `ReplicatedUsing=OnRep_` in *.h | `ue_analyze_source(cross_check)` → check server-side manual `OnRep_X()` call | Low | Manually call `OnRep_X()` on server after direct property assignment |
| NET-006 | Late-joiner unsafe RPC | Warning | `UFUNCTION.*Client` in *.h + called in BeginPlay | `ue_read` → cross-ref with `UPROPERTY(Replicated)` fallback existence | Medium | Add `UPROPERTY(Replicated)` as initial state fallback for late joiners |
| NET-007 | Non-atomic compound state | Warning | `UPROPERTY.*Replicated` (multiple in class) | `ue_analyze_source(extract_pattern)` → group by name prefix, flag 3+ related primitives | Low | Wrap related properties in a single `USTRUCT` with `NetSerialize()` |
| NET-008 | Push Model unused | Info | Replicated property in `::Tick(` | `ue_grep` for `MARK_PROPERTY_DIRTY` → absence = violation (UE 5.1+) | High | Add `MARK_PROPERTY_DIRTY_FROM_NAME(ClassName, PropertyName)` after assignment |
| NET-009 | Bandwidth excess (Tick replication) | Critical | Replicated property assigned in `::Tick(` | `ue_read` Tick body → flag unconditional assignment (no dirty/delta guard) | Low | Add `if (NewValue != OldValue)` dirty check before assignment |
| NET-010 | GameState/PlayerState abuse | Warning | `*GameState` or `*PlayerState` class | `ue_analyze_source(extract_pattern)` → count replicated properties > 15 | Low | Extract non-essential data to a separate `UActorComponent` |

---

## Severity Levels

| Level | Meaning | Action Required |
|-------|---------|-----------------|
| **Critical** | Active bandwidth waste or data corruption risk | Must fix before shipping |
| **Error** | Will cause bugs on dedicated server or specific net modes | Should fix in current sprint |
| **Warning** | Potential issue depending on context; may be intentional | Review and decide; suppress with NOLINT if intentional |
| **Info** | Optimization opportunity, not a bug | Consider for performance-sensitive contexts |

---

## NOLINT Suppression Syntax

```cpp
// Suppress specific rule on this line
SetHealth(NewHealth); // NOLINT(NET-009)

// Suppress all NET rules on this line
GetPlayerController(0); // NOLINT

// Multi-rule suppression
ServerFireWeapon(); // NOLINT(NET-001,NET-004)
```

**Scope**: Line-level only. No file-level or block-level suppression.

---

## MCP Tool Parameter Reference

### ue_grep (Phase 1: Discovery)

```python
# RPC declarations (regex mode)
ue_grep(params={
    "query": "UFUNCTION.*Server|UFUNCTION.*Client|UFUNCTION.*NetMulticast",
    "domain": "source",
    "regex": true,
    "path_pattern": "**/*.h",
    "limit": 50
})

# Specific anti-pattern (literal mode)
ue_grep(params={
    "query": "GetPlayerController(0)",
    "domain": "source",
    "limit": 50
})

# Replication patterns
ue_grep(params={
    "query": "Replicated|DOREPLIFETIME|ReplicatedUsing",
    "domain": "source",
    "path_pattern": "**/*.h",
    "limit": 50
})
```

### ue_read (Phase 2B: Pattern Matching)

```python
# Read source file for manual pattern analysis
ue_read(identifier="AMyCharacter")
```

### ue_analyze_source (Phase 2A: Structured Analysis)

```python
# NET-005/007: Cross-check replication registration
ue_analyze_source(operation="cross_check", class_name="AMyCharacter")

# NET-008/010: Extract UPROPERTY specifiers
ue_analyze_source(operation="extract_pattern",
    specifier_type="UPROPERTY",
    class_filter="AMyPlayerState")
```

### ue_diff (Phase 2C: Optional Deep Audit)

```python
# Class-level replication audit
ue_diff(operation="replication_audit", class_name="ALyraPlayerState")
```

---

## Scoring & FP Rate Calculation

**False Positive Rate** = `FP / (TP + FP)` per rule

| FP Rate | Rating | Action |
|---------|--------|--------|
| < 5% | Excellent | No changes needed |
| 5-10% | Acceptable | Monitor; consider tightening heuristics |
| 10-20% | Needs improvement | Refine grep patterns or add context checks |
| > 20% | Unacceptable | Rule must be redesigned or demoted to Info |

**Target**: < 10% aggregate FP rate across all 10 rules.

---

## NET-009/NET-008 Deduplication

When both NET-009 (Critical) and NET-008 (Info) would fire for the same property:
- NET-009 takes priority (higher severity)
- NET-008 is suppressed automatically for that property
- Suppressed NET-008 does NOT count toward violation totals

---

## Hard Case Detection Patterns

Enhanced detection for edge cases that basic grep cannot catch:

### Indirect RPC Call Chain (NET-001)

```python
# Instead of just checking 10 lines around call site:
ue_analyze_symbols(operation="smart", function_name="ServerXxx")
# find_callers returns direct callers only
# Repeat unique frontier queries client-side before checking deeper ownership guards
```

### Pointer/Component State Mutation (NET-002)

Additional patterns beyond direct member assignment:
- `->Set*(` — component setter via pointer
- `->bXxx =` — pointer member direct write
- `Component->` + `=` — any assignment through component pointer
- Safe patterns (no flag): `Spawn*`, `PlaySound*`, `SpawnEmitter*`

### Lambda/Delegate RPC in BeginPlay (NET-004)

Scan entire BeginPlay body including:
- `[this]() { ServerRPC(); }` — lambda capture
- `[&]() { ClientRPC(); }` — reference capture
- `SetTimer(Handle, ...)` — timer delegate bodies
- `AddDynamic(this, &Class::OnSomething)` — delegate bindings that call RPCs

### Indirect OnRep Call (NET-005 FP Prevention)

```python
# If OnRep_X not found directly in setter:
ue_grep(params={"query": "OnRep_<Property>", "path_pattern": "*<ClassName>*"})
# If found anywhere in same class → suppress (helper function pattern)
# Only flag if OnRep_X never called anywhere in the class
```

### Default-True Condition (NET-009)

```text
# In header: bool bShouldUpdate = true;
# In Tick:  if (bShouldUpdate) { ReplicatedProp = value; }
# → Info warning: "Condition initialized to true — may be effectively unconditional"
```

### Inherited Property Counting (NET-010)

```python
# Step 1: Get ancestor chain
ue_analyze_symbols(operation="smart", class_name="AMyState", direction="up")
# Step 2: For each ancestor, count replicated properties
ue_analyze_source(operation="extract_pattern", class_filter="<parent>")
# Step 3: Sum > 15 → flag with inheritance breakdown
```

---

## Self-Test

`/ue-network-lint --self-test` validates:

1. **MCP connectivity**: `ue_check_health()` responds with status
2. **Source grep**: `ue_grep(query="UFUNCTION.*Server", domain="source", regex=true, limit=3)` returns results (project has networking code)
3. **Replication cross-check**: `ue_analyze_source(operation="cross_check", class_name="<any_replicated_class>")` returns matched/missing data
4. **NOLINT parsing**: Verify `// NOLINT(NET-XXX)` pattern recognition on sample line

**Expected**: Steps 1-2 always pass if NarshaMCP connected and project has C++ source. Step 3 passes if project has replicated classes. Step 4 is offline validation (always passes).

---

**Issue**: #7598
**Last Updated**: 2026-03-29
