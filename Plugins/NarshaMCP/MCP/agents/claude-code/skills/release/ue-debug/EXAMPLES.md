# UE Debug — Examples

**Version**: 2.0.0
**Status**: Complete
**Parent**: [SKILL.md](SKILL.md)

---

## Scenario 1: Compile Error — Unresolved External Symbol

### User Request
```
"error LNK2019: unresolved external symbol 해결해줘"
```

### Invocation
```bash
/ue-debug error LNK2019: unresolved external symbol "UMyComponent::Initialize"
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="UMyComponent", limit=10)
# Discovers: Source/MyGame/Components/MyComponent.h exists
# step0_source_path = "Source/MyGame/Components/MyComponent.h"

ue_read(identifier="UMyComponent")
# Returns: class structure, confirms Initialize() is declared but not defined
```

#### Step 1: Symptom Classification
- Input: "error LNK2019: unresolved external symbol"
- Classification: **compile_error** (specific error code LNK2019 present)

#### Step 2: Diagnostic Tool Execution
```python
# Path A: Compile Error — use ue_fix_errors
ue_fix_errors(operation="smart", project_root="D:/MyProject")
# Returns: parsed error with file/line, suggested fix

# Read the source file for context
Read("Source/MyGame/Components/MyComponent.cpp")
# Confirms: Initialize() body is missing from .cpp
```

#### Step 3: Root Cause + Fix
```python
# Root cause: Method declared in .h but not defined in .cpp
# Fix: Add method body to MyComponent.cpp
```

### Expected Output
```text
=== UE Debug Report ===

Symptom: LNK2019 unresolved external symbol
Classification: compile_error
Severity: ERROR (blocking compilation)

Root Cause:
  UMyComponent::Initialize() is declared in MyComponent.h (line 24)
  but has no implementation in MyComponent.cpp.

Fix:
  Add the method body to Source/MyGame/Components/MyComponent.cpp:
  void UMyComponent::Initialize() { /* implementation */ }

Confidence: 95% (symbol mismatch confirmed via PDB + source)
```

---

## Scenario 2: Runtime Crash — Null Pointer Access

### User Request
```
"PIE에서 BeginPlay 중 크래시 나는데 도와줘"
```

### Invocation
```bash
/ue-debug PIE에서 BeginPlay 중 크래시
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="BeginPlay", limit=10)
# Discovers: multiple BeginPlay overrides across project

ue_read(identifier="BeginPlay")
# Returns: list of classes overriding BeginPlay
```

#### Step 1: Symptom Classification
- Input: "PIE에서 BeginPlay 중 크래시"
- Classification: **runtime_crash** (keywords: PIE, 크래시)

#### Step 2: Diagnostic Tool Execution
```python
# Path B: Runtime Crash
# Method 1: Call Graph — find crash context
ue_analyze_symbols(operation="find_callers", function_name="BeginPlay")
# Returns direct callers; repeat unique frontiers client-side to build a crash path.

# Method 2: Check crash logs
ue_fix_errors(operation="scan_build_log")
# Returns: crash callstack from Saved/Logs/

# Method 3: Symbol search for null access pattern
ue_analyze_symbols(operation="search_symbols", query="BeginPlay")
# Returns: all BeginPlay implementations with source locations

# Source verification: Read the actual implementation
Read("Source/MyGame/Characters/MyCharacter.cpp")
# Check: Is the crash from accessing an uninitialized pointer?
```

#### Step 3: Root Cause + Fix
Deep crash analysis per [REFERENCE.md § Crash Doctor Reference](REFERENCE.md#crash-doctor-reference).

### Expected Output
```text
=== UE Debug Report ===

Symptom: PIE crash during BeginPlay
Classification: runtime_crash
Severity: CRITICAL (crash)

Root Cause:
  AMyCharacter::BeginPlay() accesses InventoryComponent (line 45)
  before it is initialized. The component is created in the constructor
  but the pointer is used before Super::BeginPlay() completes.

Fix:
  Move InventoryComponent access after Super::BeginPlay() call,
  or add a null check: if (InventoryComponent) { ... }

Confidence: 85% (callstack + source analysis)
```

---

## Scenario 3: Unexpected Behavior — Blueprint Event Not Firing

### User Request
```
"BP_Enemy의 OnHit 이벤트가 발동 안 돼"
```

### Invocation
```bash
/ue-debug BP_Enemy의 OnHit 이벤트가 발동 안 돼
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="BP_Enemy", limit=10)
# Discovers: Content/Blueprints/BP_Enemy.uasset exists

ue_read(identifier="BP_Enemy")
# Returns: Blueprint structure summary
```

#### Step 1: Symptom Classification
- Input: "OnHit 이벤트가 발동 안 돼"
- Classification: **unexpected_behavior** (no crash, no error — event simply not firing)

#### Step 2: Diagnostic Tool Execution
```python
# Path C: Unexpected Behavior
# Check Blueprint structure for OnHit binding
ue_manage_blueprint(operation="get_structure", params={"blueprint_name": "BP_Enemy"})
# Returns: event graph, nodes, variable bindings

# Trace execution flow to see if OnHit is connected
ue_trace_execution(operation="trace_execution_flow", blueprint_name="BP_Enemy")
# Returns: execution path analysis

# Check collision settings
ue_analyze_config(operation="search_config", params={"query": "CollisionProfile"})
# Returns: collision channel configuration
```

Blueprint debugging per [REFERENCE.md § BP Debugger Reference](REFERENCE.md#bp-debugger-reference).

### Expected Output
```text
=== UE Debug Report ===

Symptom: BP_Enemy OnHit event not firing
Classification: unexpected_behavior
Severity: WARNING (logic error)

Root Cause:
  BP_Enemy's root component has "Simulation Generates Hit Events" disabled.
  The OnComponentHit event requires both collision AND hit event generation
  to be enabled on the component.

Fix:
  In BP_Enemy → Root Component → Details panel:
  Enable "Simulation Generates Hit Events" checkbox.
  Also verify collision preset allows the relevant channel overlap.

Confidence: 90% (Blueprint structure + config analysis)
```

---

## Notes

- **Tool Selection by Symptom**: Visual bugs use `ue_grep`, crashes use `ue_analyze_symbols`, config issues use `ue_analyze_config`
- **Multi-Query Planning**: Always plan 3 queries (EXACT/RELATED/SYMPTOM) and execute ALL before concluding
- **Source Verification**: When a file is found, verify it's the implementation (not a wrapper) by checking file size and content
- **Anti First-Hit Bias**: Execute all planned queries even if the first returns results — compare findings for convergence
