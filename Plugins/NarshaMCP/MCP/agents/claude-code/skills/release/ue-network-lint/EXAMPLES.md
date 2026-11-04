# UE Network Lint — Examples

**Version**: 1.0.0
**Parent**: [SKILL.md](SKILL.md)

---

## Scenario 1: Full Project Scan (no arguments)

### User Request
```text
네트워크 린트 실행
```

### Prerequisites
- NarshaMCP server running and connected
- UE project with C++ multiplayer code

### Step-by-Step

**Phase 1: Discovery**
```python
# Find RPC declarations
ue_grep(params={"query": "UFUNCTION.*Server|UFUNCTION.*Client|UFUNCTION.*NetMulticast",
    "domain": "source", "regex": true, "path_pattern": "**/*.h", "limit": 50})

# Find replication patterns
ue_grep(params={"query": "Replicated|DOREPLIFETIME|ReplicatedUsing",
    "domain": "source", "path_pattern": "**/*.h", "limit": 50})

# Check specific anti-patterns
ue_grep(params={"query": "GetPlayerController(0)", "domain": "source", "limit": 50})
```

**Phase 2: Analysis**
```python
# Phase 2A: Replication-aware rules via ue_analyze_source
ue_analyze_source(operation="cross_check", class_name="AMyCharacter")
ue_analyze_source(operation="extract_pattern", specifier_type="UPROPERTY",
    class_filter="AMyPlayerState")

# Phase 2B: RPC pattern rules via ue_read
ue_read(identifier="AMyCharacter")
# Manual pattern matching for NET-001~004, NET-006, NET-009
```

**Phase 3: Report**

### Expected Output

```text
════════════════════════════════════════════
 Network Lint Report
════════════════════════════════════════════
 Scanned:    42 files
 Violations: 7 total (1 critical, 1 error, 4 warning, 1 info)
 Strict:     off
 Rules:      NET-001~010

 [Critical] AMyChar.cpp:78   NET-009  Replicated Health assigned in Tick unconditionally
 [Error]    AMyChar.cpp:102  NET-003  GetPlayerController(0) without dedicated server guard
 [Warning]  AMyChar.cpp:45   NET-001  Server RPC ServerFire called without ownership check
 [Warning]  AMyChar.cpp:120  NET-002  Multicast MulticastSetDoorState modifies bDoorOpen
 [Warning]  AMyChar.h:67     NET-006  Client RPC ClientInit in BeginPlay without Replicated fallback
 [Warning]  AMyPlayerState.h NET-010  AMyPlayerState has 18 replicated properties (threshold: 15)
 [Info]     AMyChar.h:34     NET-008  Replicated Score modified in Tick without MARK_PROPERTY_DIRTY

 Suppressed: 0 items
════════════════════════════════════════════
```

### Notes
- Full scan covers all project + plugin source files
- `.gen.cpp` files are auto-generated and filtered out from results
- Plugin code (e.g., SM StateMachine, Ninja Inventory) is included in scan

---

## Scenario 2: Class-Scoped Scan

### User Request
```text
/ue-network-lint --class ALyraPlayerState
```

### Prerequisites
- Same as Scenario 1

### Step-by-Step

**Phase 1: Narrowed Discovery**
```python
# Find only the target class
ue_grep(params={"query": "class ALyraPlayerState", "domain": "source"})
# Collect ALyraPlayerState.h and ALyraPlayerState.cpp
```

**Phase 2: Targeted Analysis**
```python
# Run replication cross-check for this class only
ue_analyze_source(operation="cross_check", class_name="ALyraPlayerState")

# Extract UPROPERTY specifiers for property counting
ue_analyze_source(operation="extract_pattern", specifier_type="UPROPERTY",
    class_filter="ALyraPlayerState")

# Read source for RPC pattern matching
ue_read(identifier="ALyraPlayerState")
```

### Expected Output

```text
════════════════════════════════════════════
 Network Lint Report
════════════════════════════════════════════
 Scanned:    2 files (ALyraPlayerState.h, ALyraPlayerState.cpp)
 Violations: 2 total (0 critical, 0 error, 1 warning, 1 info)
 Strict:     off
 Rules:      NET-001~010
 Class:      ALyraPlayerState

 [Warning]  ALyraPlayerState.h:89  NET-010  22 replicated properties (threshold: 15)
 [Info]     ALyraPlayerState.h:45  NET-008  StatTags replicated without Push Model

 Suppressed: 0 items
════════════════════════════════════════════
```

### Notes
- Class-scoped scan is faster (2 files vs 42+)
- Only rules applicable to the class are checked (e.g., NET-003 only if GetPlayerController found)

---

## Scenario 3: Rule-Filtered Scan

### User Request
```text
Run network lint --rules NET-001,NET-003
```

### Prerequisites
- Same as Scenario 1

### Step-by-Step

**Phase 1: Rule-Specific Discovery**
```python
# NET-001: Server RPC declarations only
ue_grep(params={"query": "UFUNCTION.*Server", "domain": "source",
    "regex": true, "path_pattern": "**/*.h", "limit": 50})

# NET-003: GetPlayerController(0) only
ue_grep(params={"query": "GetPlayerController(0)", "domain": "source", "limit": 50})
```

**Phase 2: Filtered Analysis**
```python
# Only check NET-001 and NET-003 patterns
# Skip all replication rules (NET-005~010)
ue_read(identifier="AMyCharacter")  # Check Server RPC ownership + GetPlayerController guards
```

### Expected Output

```text
════════════════════════════════════════════
 Network Lint Report
════════════════════════════════════════════
 Scanned:    15 files
 Violations: 3 total (0 critical, 1 error, 2 warning, 0 info)
 Strict:     off
 Rules:      NET-001, NET-003 (filtered)

 [Error]    AMyHero.cpp:102     NET-003  GetPlayerController(0) without dedicated server guard
 [Warning]  AMyHero.cpp:45      NET-001  Server RPC ServerFire without ownership check
 [Warning]  AMyVehicle.cpp:78   NET-001  Server RPC ServerEnterVehicle without ownership check

 Suppressed: 0 items
════════════════════════════════════════════
```

### Notes
- Fewer files scanned because only rule-relevant patterns are searched
- Useful for focused audits (e.g., "just check RPC ownership across the project")

---

## Scenario 4: Strict Mode (CI Gate)

### User Request
```text
/ue-network-lint --strict
```

### Prerequisites
- Same as Scenario 1
- Typically used in pre-commit or CI pipeline

### Step-by-Step

Same as Scenario 1, but with `net_strict=true`:
- Any Warning or above is treated as a blocking issue
- Info remains non-blocking

### Expected Output (with violations)

```text
════════════════════════════════════════════
 Network Lint Report
════════════════════════════════════════════
 Scanned:    42 files
 Violations: 7 total (1 critical, 1 error, 4 warning, 1 info)
 Strict:     ON — 6 BLOCKING violations
 Rules:      NET-001~010

 [BLOCK] AMyChar.cpp:78   NET-009  Replicated Health assigned in Tick unconditionally
 [BLOCK] AMyChar.cpp:102  NET-003  GetPlayerController(0) without dedicated server guard
 [BLOCK] AMyChar.cpp:45   NET-001  Server RPC ServerFire called without ownership check
 [BLOCK] AMyChar.cpp:120  NET-002  Multicast MulticastSetDoorState modifies bDoorOpen
 [BLOCK] AMyChar.h:67     NET-006  Client RPC ClientInit without Replicated fallback
 [BLOCK] AMyPlayerState.h NET-010  18 replicated properties (threshold: 15)
 [Info]  AMyChar.h:34     NET-008  Replicated Score without MARK_PROPERTY_DIRTY

 Suppressed: 0 items

 ❌ STRICT MODE: 6 blocking violations found. Fix or suppress with NOLINT.
════════════════════════════════════════════
```

### Expected Output (clean)

```text
════════════════════════════════════════════
 Network Lint Report
════════════════════════════════════════════
 Scanned:    42 files
 Violations: 1 total (0 critical, 0 error, 0 warning, 1 info)
 Strict:     ON — 0 blocking violations
 Rules:      NET-001~010

 [Info]  AMyChar.h:34  NET-008  Replicated Score without MARK_PROPERTY_DIRTY

 Suppressed: 2 items (NOLINT)

 ✅ STRICT MODE: All clear. Safe to commit.
════════════════════════════════════════════
```

### Notes
- Strict mode is designed for CI/pre-commit gates
- Use `// NOLINT(NET-XXX)` to suppress intentional patterns
- Info-level violations (NET-008) are never blocking even in strict mode
- Combine with `--class` for targeted strict checks: `--strict --class AMyCharacter`

---

**Issue**: #7598
**Last Updated**: 2026-03-29
