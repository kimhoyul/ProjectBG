# Blueprint Refactoring - Examples (`/ue-refactor-bp`)

> **1.0.0** — Initial examples (Issue #7412)
>
> **Path A (ECA LISP) deprecation notice — Epic #11230 / PR #11295 (2026-05-09)**: Example 1 below describes the historical "ECA LISP refactoring" workflow (`ue_eca_proxy(call_tool, "blueprint_to_lisp"/"lisp_to_blueprint")`). The ECABridge plugin was confirmed not present in Epic's public UE 5.8 mirror, so the LISP write-back channel was deleted alongside the ECABridge stack. **Path A no longer executes on any engine version** — the skill now always routes to Path B (Example 2). Example 1 is retained as historical reference; see [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md) for the design rationale.

## Example 1: Path A — ECA LISP Refactoring (UE 5.8+) [REMOVED post-Epic #11230]

**User**: "BP_EnemyAI 리팩터링해줘"

### Phase 0: Discovery + ECA Detection

```text
ue_grep(query="BP_EnemyAI", domain="assets") → Found: Blueprint, 12 variables, 8 functions
ue_read(identifier="BP_EnemyAI", domain="asset") → Parent: AEnemyBase, 45 nodes preview
ue_check_health() → engine_version: 5.8.0
→ NarshaMCP path (T3D pipeline / manual guide) — ECA channel removed in #11230/#11231
```

### Phase 1: Analysis

```python
ue_manage_blueprint(operation="get_structure", blueprint_name="BP_EnemyAI")
# → node_count: 45, functions: 3, events: 5

ue_manage_blueprint(operation="get_raw_nodes", blueprint_name="BP_EnemyAI")
# → 45 nodes with connections, entry points, pin data

ue_manage_blueprint(operation="find_bp_references", blueprint_name="BP_EnemyAI")
# → 8 references: BP_EnemySpawner (3), BP_CombatManager (2), BP_AIDirector (3)

ue_trace_execution(operation="trace_execution_flow",
    blueprint_name="BP_EnemyAI", max_depth=10)
# → BeginPlay → InitPatrol → SetupTimers → StartAI
```

### Analysis Report

```text
=== Blueprint Refactoring Analysis ===

Blueprint: BP_EnemyAI
Nodes: 45 | Functions: 3 | Events: 5
Callers: 8 references across 3 Blueprints

Identified Issues:
  1. [Complexity] EventGraph has 28 nodes — consider splitting into functions
  2. [Duplication] 3 identical Delay+SetActorLocation chains in patrol logic
  3. [Dead Code] CustomEvent "OnOldAbility" has 0 callers
  4. [Performance] ForEachLoop on AllEnemies array in Tick event

Refactoring Path: ECA LISP (UE 5.8+)
Proceed? [Y/n]
```

### Phase 2A: LISP Modification

```python
ue_eca_proxy(operation="call_tool", tool_name="blueprint_to_lisp",
    arguments={"blueprint_name": "BP_EnemyAI"})
# → LISP representation of current graph
```

```text
=== LISP Modification Preview ===

--- Original ---
(EventGraph
  (BeginPlay
    (Sequence
      (SetActorLocation Patrol1)
      (Delay 2.0 (SetActorLocation Patrol2))
      (Delay 2.0 (SetActorLocation Patrol3))
      (PrintString "Patrol Started")))
  (OnOldAbility
    (PrintString "Legacy")))

+++ Modified +++
(EventGraph
  (BeginPlay
    (CallFunction "StartPatrolSequence")))

(Function "StartPatrolSequence"
  (CallFunction "MoveToPatrolPoint" Patrol1 0.0)
  (CallFunction "MoveToPatrolPoint" Patrol2 2.0)
  (CallFunction "MoveToPatrolPoint" Patrol3 4.0))

(Function "MoveToPatrolPoint" (Location WaitTime)
  (Delay WaitTime (SetActorLocation Location)))

Changes: Extracted 2 functions, removed dead event, consolidated 3 duplicates
Apply? [Y/n]
```

```python
ue_eca_proxy(operation="call_tool", tool_name="lisp_to_blueprint",
    arguments={"blueprint_name": "BP_EnemyAI", "lisp": "<modified_lisp>"})
# → Applied successfully

ue_manage_blueprint(operation="auto_layout", params={"blueprint_name": "BP_EnemyAI"})
# → Layout applied
```

### Phase 3: Verification

```python
ue_fix_errors(operation="smart", project_root="D:/MyProject")
# → Build: PASS (0 errors)
```

### Output

```text
=== Blueprint Refactoring Report ===

Blueprint: BP_EnemyAI
Engine: UE 5.8 | ECABridge: Available
Refactoring Path: A (ECA LISP)

--- Phase 1: Analysis ---
Nodes: 45 | Functions: 3 | Events: 5
Callers: 8 references across 3 Blueprints
Issues Found: 4 (Complexity: 1, Duplication: 1, Dead Code: 1, Performance: 1)

--- Phase 2: Refactoring ---
  LISP Diff: -17 nodes, +2 functions extracted, 1 dead event removed
  Apply Status: SUCCESS

--- Phase 3: Verification ---
Build: PASS
Auto-fix: N/A

--- Next Steps ---
  - Open BP_EnemyAI in Editor to verify visual layout
  - Test AI patrol behavior unchanged
  - Review 8 callers across 3 Blueprints for compatibility
```

---

## Example 2: Path B — Manual Guide (UE 5.7-)

**User**: "Clean up BP_InventoryWidget Blueprint"

### Phase 0: Discovery + ECA Detection

```text
ue_grep(query="BP_InventoryWidget", domain="assets") → Found: Widget Blueprint, 6 variables
ue_read(identifier="BP_InventoryWidget", domain="asset") → Parent: UUserWidget, 32 nodes
ue_check_health() → engine_version: 5.4.2
→ path_a = false (Manual Guide)
```

### Phase 1: Analysis

```python
ue_manage_blueprint(operation="get_structure", blueprint_name="BP_InventoryWidget")
# → node_count: 32, functions: 2, events: 4

ue_manage_blueprint(operation="get_raw_nodes", blueprint_name="BP_InventoryWidget")
# → 32 nodes: 4 duplicate AddChild+SetVisibility chains, 1 unreachable event

ue_manage_blueprint(operation="find_bp_references", blueprint_name="BP_InventoryWidget")
# → 4 references: BP_HUD (2), BP_PlayerController (2)

ue_trace_execution(operation="trace_execution_flow",
    blueprint_name="BP_InventoryWidget", max_depth=10)
# → Construct → InitSlots → BindDelegates
```

### Phase 2B: Manual Refactoring Guide

```text
=== Manual Refactoring Guide ===

Blueprint: BP_InventoryWidget
Based on Phase 1 analysis:

Step 1: Extract Function "AddInventorySlot"
  - Select nodes: AddChild → SetVisibility → SetPadding (repeated 4x)
  - Right-click → Collapse to Function
  - Name: "AddInventorySlot"
  - Parameters: SlotWidget (UWidget*), bVisible (bool)

Step 2: Remove Dead Code
  - Delete CustomEvent "OnLegacyRefresh" (0 callers confirmed)
  - Delete 3 connected nodes (GetAllChildren, ForEach, RemoveChild)

Step 3: Replace Duplicate Chains
  - Replace 4 duplicate AddChild+SetVisibility chains with AddInventorySlot() calls

Estimated Impact:
  - Nodes reduced: 32 → 18
  - Functions added: +1
  - Maintainability: Improved (smaller EventGraph, reusable function)
```

### Output

```text
=== Blueprint Refactoring Report ===

Blueprint: BP_InventoryWidget
Engine: UE 5.4 | ECABridge: Unavailable
Refactoring Path: B (Manual Guide)

--- Phase 1: Analysis ---
Nodes: 32 | Functions: 2 | Events: 4
Callers: 4 references across 2 Blueprints
Issues Found: 3 (Duplication: 1, Dead Code: 1, Complexity: 1)

--- Phase 2: Manual Guide ---
  Steps: 3 actionable steps provided
  Estimated Node Reduction: 32 → 18

--- Phase 3: Verification ---
Build: N/A (Path B — user applies changes manually)

--- Next Steps ---
  - Apply steps 1-3 in UE Editor
  - Compile Blueprint after changes
  - Test inventory UI behavior unchanged
```

---

## Example 3: Error Recovery — Editor Offline

**User**: "Optimize BP_WeaponSystem graph"

### Phase 0: Discovery

```text
ue_grep(query="BP_WeaponSystem", domain="assets") → Found: Blueprint, 8 variables
ue_read(identifier="BP_WeaponSystem", domain="asset") → Parent: AActor, 51 nodes
ue_check_health() → editor_control: offline
→ Editor offline — find_bp_references will be skipped
```

### Phase 1: Analysis (Editor Offline)

```python
ue_manage_blueprint(operation="get_structure", blueprint_name="BP_WeaponSystem")
# → node_count: 51, source: "filesystem" (Editor offline)

ue_manage_blueprint(operation="get_raw_nodes", blueprint_name="BP_WeaponSystem")
# → 51 nodes with connections (works offline — reads .uasset directly)

# find_bp_references SKIPPED — Editor not running
# → Note: "Reference analysis unavailable without Editor"

ue_trace_execution(operation="trace_execution_flow",
    blueprint_name="BP_WeaponSystem", max_depth=10)
# → Fire → SpawnProjectile → ApplyDamage → PlayEffect
```

### Analysis Report

```text
=== Blueprint Refactoring Analysis ===

Blueprint: BP_WeaponSystem
Nodes: 51 | Functions: 4 | Events: 6
Callers: ⚠️ Reference analysis unavailable (Editor offline)

Identified Issues:
  1. [Complexity] EventGraph has 35 nodes — consider splitting
  2. [Duplication] 2 identical SpawnEmitter+PlaySound chains
  3. [Performance] LineTrace in Tick with no throttling

Refactoring Path: Manual Guide (ECA unavailable)
⚠️ Note: Reference data unavailable — verify callers manually before refactoring
Proceed? [Y/n]
```

### Output

```text
=== Blueprint Refactoring Report ===

Blueprint: BP_WeaponSystem
Engine: UE 5.4 | ECABridge: Unavailable | Editor: Offline

--- Phase 1: Analysis ---
Nodes: 51 | Functions: 4 | Events: 6
Callers: UNAVAILABLE (Editor offline)
Issues Found: 3 (Complexity: 1, Duplication: 1, Performance: 1)

--- Phase 2: Manual Guide ---
  Steps: 4 actionable steps provided
  Estimated Node Reduction: 51 → 35

--- Warnings ---
  ⚠️ Reference analysis skipped — verify callers before applying changes
  ⚠️ auto_layout unavailable — arrange nodes manually after refactoring
```
