# BP → C++ Migration Validator - Examples

## Example 1: GameplayAbility Migration (MCP Live Test)

### Scenario
User wants to validate a Blueprint GameplayAbility migration to C++.

### User Input
```text
"GA_BackDoor_Attack01 마이그레이션 검증해줘"
```

### Workflow Execution (Core 5-tool)

**Stage 1: BP Structure Extraction**
```python
ue_manage_blueprint(
    operation="get_structure",
    params={"blueprint_name": "GA_BackDoor_Attack01", "compress": true}
)
```
Result:
```json
{
  "blueprint_name": "GA_BackDoor_Attack01",
  "node_count": 4,
  "source": "execution_graph",
  "success": true,
  "total_time_ms": 0
}
```

**Stage 2: C++ Class Verification**
```python
ue_analyze_symbols(
    operation="search_symbols",
    params={"query": "*EnemyAttack*", "limit": 10}
)
```
Result (Direct match not found):
```json
{
  "results": [],
  "total_results": 0
}
```

**Stage 2 Fallback: Search Base Class**
```python
ue_analyze_symbols(
    operation="get_methods",
    params={"class_name": "AAGameplayAbilityBase", "limit": 10}
)
```
Result:
```json
{
  "class_name": "AAGameplayAbilityBase",
  "methods": [
    {"name": "AddGameplayCueWithParams", "full_name": "UAAGameplayAbilityBase::AddGameplayCueWithParams"},
    {"name": "ApplyCooldown", "full_name": "UAAGameplayAbilityBase::ApplyCooldown"},
    {"name": "ApplyCost", "full_name": "UAAGameplayAbilityBase::ApplyCost"},
    {"name": "CanActivateAbility", "full_name": "UAAGameplayAbilityBase::CanActivateAbility"}
  ],
  "methods_count": 10
}
```

**Stage 3: Reference Search**
```python
ue_search_assets(
    asset_type="Blueprint",
    params={"query": "*BackDoor*", "limit": 5}
)
```
Result:
```json
{
  "assets": [
    {"name": "BP_BackDoor", "path": "/Game/Core/EnemyCharacter/BackDoor/"},
    {"name": "BP_BackDoor_Projectile", "path": "/Game/Core/EnemyCharacter/BackDoor/Projectile/"},
    {"name": "BP_CR_BackDoor", "path": "/Game/Character/Monster/BackDoor/Mesh/"}
  ],
  "total_found": 4
}
```

**Stage 4: Build Check (Skipped)**
```python
ue_fix_errors(mode="preflight")
```
Result:
```json
{
  "error": "preflight mode requires 'build_log_path' parameter"
}
```
→ **Stage 4 Skipped**: No build log available

**Stage 5: Migration Report**

```text
================================================================================
📋 MIGRATION VALIDATION REPORT: GA_BackDoor_Attack01
================================================================================

📍 VALIDATION SUMMARY (Core Workflow)
┌─────────────────────┬──────────┬─────────────────────────────┐
│ Stage               │ Status   │ Details                     │
├─────────────────────┼──────────┼─────────────────────────────┤
│ 1. BP Structure     │ ✅ Pass  │ 4 nodes found               │
│ 2. C++ Class Found  │ ✅ Pass  │ UAAGameplayAbilityBase      │
│                     │          │ (10 methods verified)       │
│ 3. References       │ ✅ Pass  │ 4 related assets found      │
│ 4. Build Check      │ ⏭️ Skip  │ No build log available      │
└─────────────────────┴──────────┴─────────────────────────────┘

📍 COMPLETENESS SCORE: 80% (Core 4/5 Passed)

📍 C++ BASE CLASS ANALYSIS

UAAGameplayAbilityBase (Project-specific C++ class):
├─ AddGameplayCueWithParams
├─ ApplyCooldown / ApplyCooldownNative / ApplyCooldownFragments
├─ ApplyCost / ApplyCostNative / ApplyCostFragments
├─ CanActivateAbility / CanActivateAbilityNative
└─ (10+ overridable methods)

📍 RELATED ASSETS

Assets referencing BackDoor:
- BP_BackDoor (Main character)
- BP_BackDoor_Projectile (Projectile BP)
- BP_CR_BackDoor (Control Rig)
- BP_BackDoor_TargetingTest (Dev test)

📍 MIGRATION STATUS

Blueprint: GA_BackDoor_Attack01 (4 nodes)
├─ Parent: GA_EnemyAttackBase (Blueprint)
└─ Ultimate Base: UAAGameplayAbilityBase (C++)

Migration Type: PARTIAL
- BP-only logic in GA_BackDoor_Attack01
- C++ base provides core functionality
- No direct C++ equivalent for this specific ability

📍 ACTION ITEMS

[NOT REQUIRED - Hybrid Architecture Detected]
This appears to be an intentional BP/C++ hybrid where:
- C++ (UAAGameplayAbilityBase): Core ability functionality
- BP (GA_BackDoor_Attack01): Instance-specific logic

If full C++ migration is desired:
1. ⬜ Create C++ class UGA_BackDoor_Attack01
2. ⬜ Override ActivateAbility with montage logic
3. ⬜ Update spawning code to use C++ class
4. ⬜ Delete BP after verification

================================================================================
```

---

## Example 2: Character Blueprint Migration

### Scenario
User migrated BP_Character to C++ and wants validation.

### User Input
```text
"BP_Character를 AMyCharacter로 변환했는데 제대로 됐어?"
```

### Workflow Execution (Full 7-tool)

**Stage 1: BP Structure**
```python
ue_manage_blueprint(
    operation="get_structure",
    params={
        "blueprint_name": "BP_Character",
        "include_variables": true
    }
)
```
Result:
```json
{
  "functions": 12,
  "variables": 18,
  "events": 5,
  "custom_events": 3,
  "components": 8
}
```

**Stage 2: C++ Class Search**
```python
ue_analyze_symbols(
    operation="search_symbols",
    params={"query": "*MyCharacter*"}
)
```
Result:
```json
{
  "results": [
    {"name": "AMyCharacter", "type": "class", "pdb_source": "MyProject.pdb"},
    {"name": "AMyCharacterBase", "type": "class", "pdb_source": "MyProject.pdb"}
  ]
}
```

**Stage 3: Reference Search**
```python
ue_search_assets(
    asset_type="Blueprint",
    params={"query": "*Character*"}
)
```

**Stage 3A: Execution Flow Comparison** (Extended)
```python
ue_trace_execution(
    operation="trace_execution_flow",
    params={
        "blueprint_name": "BP_Character",
        "start_node": "BeginPlay"
    }
)
```

**Stage 4: Build Check**
```python
ue_fix_errors(
    mode="preflight",
    params={"build_log_path": "Saved/Logs/UnrealEditor-2026.02.06.log"}
)
```

**Stage 5A: Compile Test** (Extended)
```python
ue_editor_automation(
    operation="compile_blueprints",
    params={"target": "AMyCharacter"}
)
```

### Full Migration Report

```text
================================================================================
📋 MIGRATION VALIDATION REPORT: BP_Character → AMyCharacter
================================================================================

📍 VALIDATION SUMMARY (Core + Extended)
┌─────────────────────────┬──────────┬───────────────────────────────┐
│ Stage                   │ Status   │ Details                       │
├─────────────────────────┼──────────┼───────────────────────────────┤
│ 1. BP Structure         │ ✅ Pass  │ 12 functions, 18 variables    │
│ 2. C++ Class Found      │ ✅ Pass  │ AMyCharacter (PDB verified)   │
│ 3. References           │ ⚠️ Warn  │ 7 assets still reference BP   │
│ 4. Build Check          │ ✅ Pass  │ No errors in build log        │
│ 3A. Flow Comparison     │ ✅ Pass  │ 90% flow match                │
│ 5A. Compile Test        │ ✅ Pass  │ Compiled successfully         │
└─────────────────────────┴──────────┴───────────────────────────────┘

📍 COMPLETENESS SCORE: 85% (6/7 Passed, 1 Warning)

📍 FUNCTION MIGRATION STATUS

Migrated (10/12):
✅ BeginPlay → AMyCharacter::BeginPlay
✅ Tick → AMyCharacter::Tick
✅ TakeDamage → AMyCharacter::TakeDamage
✅ Jump → AMyCharacter::Jump
✅ Attack → AMyCharacter::Attack
✅ Dodge → AMyCharacter::Dodge
✅ Die → AMyCharacter::Die
✅ Respawn → AMyCharacter::Respawn
✅ SetupInput → AMyCharacter::SetupPlayerInputComponent
✅ InitAbilities → AMyCharacter::InitializeAbilitySystem

Not Migrated (2/12):
❌ BP_DebugDraw → Keep in BP (debug only)
❌ BP_TestFunction → Keep in BP (test only)

📍 VARIABLE MIGRATION STATUS

Migrated (15/18):
✅ Health → UPROPERTY float Health
✅ MaxHealth → UPROPERTY float MaxHealth
✅ Stamina → UPROPERTY float Stamina
... (12 more)

Not Migrated (3/18):
❌ EditorOnlyFlag → Keep in BP (editor-only)
❌ DebugColor → Keep in BP (debug)
❌ TestValue → Keep in BP (test)

📍 REMAINING REFERENCES (7 assets)

Blueprint references to update:
- BP_Character_Child (child BP)
- BP_CharacterSpawner (spawner)
- DT_CharacterStats (data table)
- L_Tutorial (level - placed actors)
- WBP_CharacterSelect (widget)
- GA_CharacterAbility1 (ability)
- GA_CharacterAbility2 (ability)

📍 ACTION ITEMS (Prioritized)

[CRITICAL] Update references (7):
1. ⬜ BP_Character_Child → Reparent to AMyCharacter
2. ⬜ BP_CharacterSpawner → Update spawn class
3. ⬜ DT_CharacterStats → Update class reference
4. ⬜ L_Tutorial → Replace placed actors

[OPTIONAL] Clean up:
5. ⬜ Delete BP_Character after all refs updated
6. ⬜ Archive BP_DebugDraw if needed

================================================================================
```

---

## Example 3: Partial Migration (Core Only)

### Scenario
User running validation without Editor connection.

### User Input
```text
"BP_Weapon 마이그레이션 검증 (오프라인)"
```

### Expected Output
- Stages 1-4 execute (Core)
- Stages 3A, 5A skipped (Extended - need Editor)
- Clear indication of what was validated vs skipped

---

## Error Recovery Examples

### Case 1: Blueprint Not Found

**Problem**: `BP_NotExist` doesn't exist
**Behavior**: Stage 1 returns error, workflow aborts
**Message**: "Blueprint 'BP_NotExist' not found. Use ue_search_assets to find available BPs."

### Case 2: No C++ Class

**Problem**: No C++ class exists yet
**Behavior**: Stage 2 returns warning, workflow continues
**Message**: "No C++ class found matching 'BP_Character'. Migration may not be complete."

### Case 3: No Build Log

**Problem**: Build log path not found
**Behavior**: Stage 4 skipped, uses preflight mode
**Message**: "No build log available. Run Build > Rebuild All to enable Stage 4."

### Case 4: Editor Not Connected

**Problem**: Extended stages need Editor
**Behavior**: Stages 3A, 5A skipped
**Message**: "Editor not connected. Extended validation skipped. Core validation: 5/5 passed."

---

## Parameter Auto-Detection

### Blueprint Name → C++ Class Name

| BP Name | Auto-detected C++ Candidates |
|---------|------------------------------|
| BP_Character | ACharacter, UCharacter, ACharacterBase, Character |
| BP_Weapon | AWeapon, UWeapon, AWeaponBase, Weapon |
| GA_Attack | UGA_Attack, AGA_Attack, GA_Attack |

### Start Node Detection

| BP Type | Priority Order |
|---------|----------------|
| Actor | BeginPlay → Tick → Construction Script |
| Ability | ActivateAbility → EndAbility → CanActivate |
| Widget | Construct → OnInitialized → PreConstruct |

---

## Integration with Other Skills

**Combined with code-generation**:
```text
bp-to-cpp-validator (find missing) → code-generation (generate C++ stubs)
```

**Combined with unreal-error-doctor**:
```text
bp-to-cpp-validator (build errors) → unreal-error-doctor (fix compile issues)
```

**Combined with cross-domain-flow**:
```text
bp-to-cpp-validator (verify flow) ← cross-domain-flow (trace execution)
```

---

**Last Updated**: 2026-02-06
**Related**: Issue #4442
