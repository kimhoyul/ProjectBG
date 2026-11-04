# UE Create System — Examples

**Skill**: `/ue-create-system`
**Version**: 1.0.0
**Issue**: #7408
**Parent**: [SKILL.md](SKILL.md) | [REFERENCE.md](REFERENCE.md)

> These examples show complete `/ue-create-system` invocations across 4 scenarios.

> **Path A (ECABridge) deprecation notice — Epic #11230 / PR #11295 (2026-05-09)**: Examples 1, 3, and 5 below reference the historical ECA Full-Stack creation path (`ue_eca_proxy(call_tool, ...)` → BP/Niagara/EnhancedInput from LISP). The ECABridge plugin was confirmed not present in Epic's public UE 5.8 mirror, so the entire ECA write-back channel was deleted. **The skill now always routes to Path B** (Example 2 — Editor RC `ue_manage_blueprint(create_blueprint)`) regardless of engine version. ECA examples are retained as historical reference; see [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md).

---

## Example 1: Damage System (UE 5.8, ECA Available — Full Path A) [REMOVED post-Epic #11230]

**User prompt**: "데미지 시스템 만들어줘"

### Phase 0: Discovery

```python
ue_grep(params={"query": "DamageType", "domain": "source", "limit": 10})
# → Found: UDamageType (engine), no project-specific damage system
# → Decision: No similar system exists, proceed

ue_read(params={"identifier": "UDamageType"})
# → Read engine DamageType to understand project conventions
```

### Phase 1: Analysis

```python
ue_analyze_symbols(operation="smart", params={"query": "UDamageType"})
# → Inheritance: UDamageType → UObject
# → Available overrides: GetDeathEvent(), GetDamageDescription()

ue_generate_code(operation="suggest_class", params={
    "base_class": "UActorComponent",
    "class_name": "CUDamageComponent",
    "features": ["gas_setup", "replication"]
})
# → Architecture plan returned
```

**Plan presented to user**:
```text
=== System Architecture Plan ===

System Type: Damage System
Engine: UE 5.8 (ECABridge available ✅)

C++ Classes (Phase 2):
  1. UCUDamageComponent : UActorComponent [gas_setup, replication]
  2. UCUDamageType : UDamageType
  3. UGA_ApplyDamage : UGameplayAbility [gas_setup]

Visual Assets (Phase 3 via ECABridge):
  4. BP_DamageComponent (Blueprint graph)
  5. NS_DamageImpact (Niagara VFX)

Proceed? [Y/n]
```

User confirms → Phase 2.

### Phase 2: C++ Generation

```python
# Class 1: DamageType (no dependencies)
ue_generate_code(operation="derive_class", params={
    "base_class": "UDamageType",
    "class_name": "CUDamageType",
    "output_dir": "Source/MyGame/Damage",
    "features": []
})
# → UCUDamageType.h + UCUDamageType.cpp

# Class 2: Ability (depends on DamageType)
ue_generate_code(operation="derive_class", params={
    "base_class": "UGameplayAbility",
    "class_name": "GA_ApplyDamage",
    "output_dir": "Source/MyGame/Abilities",
    "features": ["gas_setup"]
})
# → UGA_ApplyDamage.h + UGA_ApplyDamage.cpp

# Class 3: Component (depends on Ability + Type)
ue_generate_code(operation="derive_class", params={
    "base_class": "UActorComponent",
    "class_name": "CUDamageComponent",
    "output_dir": "Source/MyGame/Components",
    "features": ["gas_setup", "replication"]
})
# → UCUDamageComponent.h + UCUDamageComponent.cpp
```

### Phase 3A: ECA Asset Creation

```python
ue_check_health()
# → engine_version.version: "5.8.0"

ue_eca_proxy(operation="list_tools", params={"query": "blueprint"})
# → Found: lisp_to_blueprint, create_niagara_system, ...

ue_eca_proxy(operation="call_tool", params={
    "tool_name": "lisp_to_blueprint",
    "arguments": {
        "blueprint_name": "BP_DamageComponent",
        "lisp": "(begin-object-with-defaults :class UCUDamageComponent)"
    }
})
# → ✅ BP_DamageComponent created

ue_eca_proxy(operation="call_tool", params={
    "tool_name": "create_niagara_system",
    "arguments": {
        "system_name": "NS_DamageImpact",
        "template": "Simple Sprite Burst"
    }
})
# → ✅ NS_DamageImpact created
```

### Phase 4: Validation

```python
ue_fix_errors(operation="smart", params={"project_root": "D:/MyProject"})
# → 0 errors, 2 warnings (missing Build.cs deps)
```

### Final Report

```text
=== System Generation Report ===

System: DamageSystem (Damage)
Engine: UE 5.8 | ECABridge: available

--- Phase 2: C++ Classes ---
✅ UCUDamageType.h/cpp (Source/MyGame/Damage/)
✅ UGA_ApplyDamage.h/cpp (Source/MyGame/Abilities/)
✅ UCUDamageComponent.h/cpp (Source/MyGame/Components/)

--- Phase 3: Visual Assets ---
✅ BP_DamageComponent (Blueprint via ECABridge)
✅ NS_DamageImpact (Niagara VFX via ECABridge)
⏭️ Material: Not needed for this system type
⏭️ UI Widget: Not needed for this system type

--- Phase 4: Validation ---
✅ Build: PASSED (0 errors, 2 warnings)

--- Build.cs Dependencies ---
Add to MyGame.Build.cs PublicDependencyModuleNames:
  "GameplayAbilities", "GameplayTags", "Niagara"

--- Next Steps ---
1. Open BP_DamageComponent in Blueprint Editor → verify event graph
2. Assign NS_DamageImpact to UCUDamageComponent::ImpactVFX property
3. Add GA_ApplyDamage to your AbilitySystemComponent
4. Test in PIE: apply damage and verify VFX plays
```

---

## Example 2: Inventory System with UI (UE 5.7, No ECA — Path B)

**User prompt**: "인벤토리 시스템 UI 포함해서 만들어"

### Phase 0: Discovery

```python
ue_grep(params={"query": "Inventory", "domain": "source", "limit": 10})
# → No existing inventory system found → proceed
```

### Phase 1: Analysis

```python
ue_analyze_symbols(operation="smart", params={"query": "UActorComponent"})

ue_generate_code(operation="suggest_class", params={
    "base_class": "UActorComponent",
    "class_name": "CUInventoryComponent",
    "features": ["replication"]
})
```

**Plan**: 3 C++ classes + BP + UI widget

### Phase 2: C++ Generation

```python
# 1. Item data
ue_generate_code(operation="derive_class", params={
    "base_class": "UObject",
    "class_name": "CUInventoryItem",
    "output_dir": "Source/MyGame/Inventory",
    "features": []
})

# 2. Component
ue_generate_code(operation="derive_class", params={
    "base_class": "UActorComponent",
    "class_name": "CUInventoryComponent",
    "output_dir": "Source/MyGame/Inventory",
    "features": ["replication"]
})

# 3. UI Controller
ue_generate_code(operation="derive_class", params={
    "base_class": "UUserWidget",
    "class_name": "CUInventoryWidget",
    "output_dir": "Source/MyGame/UI",
    "features": []
})
```

### Phase 3B: Fallback (No ECA)

```python
ue_check_health()
# → engine_version.version: "5.7.1"

# Basic BP via NarshaMCP
ue_manage_blueprint(operation="add_node", params={
    "blueprint_name": "BP_InventoryComponent",
    "node_class": "K2Node_Event",
    "function_name": "OnInventoryChanged"
})
```

**Manual guidance output**:
```text
=== Manual Asset Creation Guide ===

UI: Content Browser → Right-click → Widget Blueprint
  Name: WBP_Inventory
  Root: Canvas Panel → Vertical Box
  Add: ListView (for item list) + TextBlock (for item count)
  Bind: UCUInventoryComponent::OnInventoryChanged → WBP_Inventory::UpdateList
```

### Final Report

```text
=== System Generation Report ===

System: InventorySystem (Inventory)
Engine: UE 5.7 | ECABridge: unavailable

--- Phase 2: C++ Classes ---
✅ UCUInventoryItem.h/cpp (Source/MyGame/Inventory/)
✅ UCUInventoryComponent.h/cpp (Source/MyGame/Inventory/)
✅ UCUInventoryWidget.h/cpp (Source/MyGame/UI/)

--- Phase 3: Visual Assets ---
✅ BP_InventoryComponent (Blueprint via ue_manage_blueprint)
📋 WBP_Inventory (Manual creation needed — see guide above)

--- Phase 4: Validation ---
✅ Build: PASSED (0 errors, 1 warning)

--- Next Steps ---
1. Create WBP_Inventory following the Manual Asset Creation Guide above
2. Bind UCUInventoryComponent::OnInventoryChanged to WBP_Inventory
3. Add UCUInventoryComponent to your PlayerCharacter Blueprint
4. Test in PIE: pick up items and verify UI updates
```

---

## Example 3: Projectile System (UE 5.8, Partial ECA Failure)

**User prompt**: "Create projectile system with VFX and material"

### Phase 2: C++ Generation (3 classes)

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "AActor",
    "class_name": "CUProjectileBase",
    "output_dir": "Source/MyGame/Projectiles",
    "features": ["replication"]
})

ue_generate_code(operation="derive_class", params={
    "base_class": "UActorComponent",
    "class_name": "CUProjectileLauncher",
    "output_dir": "Source/MyGame/Projectiles",
    "features": []
})

ue_generate_code(operation="derive_class", params={
    "base_class": "UObject",
    "class_name": "CUProjectileData",
    "output_dir": "Source/MyGame/Projectiles",
    "features": []
})
```

### Phase 3A: Partial ECA Success

```python
ue_check_health()

# BP creation → success
ue_eca_proxy(operation="call_tool", params={
    "tool_name": "lisp_to_blueprint",
    "arguments": {"blueprint_name": "BP_ProjectileBase"}
})
# → ✅ Success

# Niagara creation → success
ue_eca_proxy(operation="call_tool", params={
    "tool_name": "create_niagara_system",
    "arguments": {"system_name": "NS_ProjectileTrail", "template": "Beam"}
})
# → ✅ Success

# Material creation → TIMEOUT
ue_eca_proxy(operation="call_tool", params={
    "tool_name": "add_material_node",
    "arguments": {"material_name": "M_ProjectileGlow"}
})
# → ❌ Error: "timeout after 30s"
# → Graceful degradation: skip material, output manual guidance
```

### Final Report (Partial Success)

```text
=== System Generation Report ===

System: ProjectileSystem (Projectile)
Engine: UE 5.8 | ECABridge: available

--- Phase 2: C++ Classes ---
✅ UCUProjectileBase.h/cpp (Source/MyGame/Projectiles/)
✅ UCUProjectileLauncher.h/cpp (Source/MyGame/Projectiles/)
✅ UCUProjectileData.h/cpp (Source/MyGame/Projectiles/)

--- Phase 3: Visual Assets ---
✅ BP_ProjectileBase (Blueprint via ECABridge)
✅ NS_ProjectileTrail (Niagara VFX via ECABridge)
❌ M_ProjectileGlow (ECA timeout — manual creation needed)
   → Content Browser → Right-click → Material
   → Name: M_ProjectileGlow
   → Key nodes: Emissive Color (Vector Parameter) + Fresnel + Opacity
   → Connect to: UCUProjectileBase::ProjectileMaterial property

--- Phase 4: Validation ---
✅ Build: PASSED (0 errors, 0 warnings)

--- Next Steps ---
1. Create M_ProjectileGlow following the guidance above
2. Assign NS_ProjectileTrail to UCUProjectileBase::TrailVFX
3. Assign M_ProjectileGlow to UCUProjectileBase::ProjectileMaterial
4. Test in PIE: fire projectile and verify trail + glow
```

---

## Example 4: Similar System Exists (Phase 0 Early Exit)

**User prompt**: "데미지 시스템 만들어줘"

### Phase 0: Discovery — Early Exit

```python
ue_grep(params={"query": "DamageSystem", "domain": "source", "limit": 10})
# → Found: UCUDamageComponent (Source/MyGame/Combat/CUDamageComponent.h)
# →        UGA_ApplyDamage (Source/MyGame/Abilities/GA_ApplyDamage.h)

ue_read(params={"identifier": "UCUDamageComponent"})
# → Existing damage component with 15 properties, 8 functions
```

**Warning presented to user**:
```text
⚠️ Similar system already exists:

Found existing damage system:
  - UCUDamageComponent (Source/MyGame/Combat/)
  - UGA_ApplyDamage (Source/MyGame/Abilities/)

Suggestions:
  1. Extend the existing system (add new damage types, effects)
  2. Create a parallel system with different scope (e.g., "Environmental Damage")
  3. Proceed anyway and create a new separate system

Which approach would you prefer? [1/2/3]
```

User selects option 1 → redirect to `/ue-scaffold` for extending existing classes.
User selects option 3 → proceed to Phase 1 with adjusted naming (e.g., "CUEnvDamage").

---

## Example 5: Dash Ability with Live QA (Phase 4.5, Issue #9579)

**Request**: `대시 어빌리티 시스템 만들어줘`

UE 5.8 Editor running, ECABridge available, gameplay system → Phase 4.5 auto-triggers.

```text
Phase 0 Discovery: type=gameplay (damage+ability match), classes=["GA_Dash"]
Phase 1 Analysis: ACharacter pattern found, GAS scaffolding selected
Phase 2 C++ Generation:
  ✅ UGA_Dash.h/cpp (Source/MyGame/Abilities/)
  ✅ UCUDashAttributeSet.h/cpp (Source/MyGame/Attributes/)
Phase 3 Asset Creation: BP_GA_Dash via ECABridge
Phase 4 Validation: Build PASSED (0 errors)

Phase 4.5 Live QA Verification (Issue #9579):
  ⚙️  Gameplay system detected → 3-gate check
     Gate 1 (system_type=ability): ✅ match
     Gate 2 (UE_CREATE_SYSTEM_SKIP_LIVE_QA): ✅ not set
     Gate 3 (ue_check_health editor): ✅ online
  → All gates pass, invoking ns-ue-qa-evaluator

  Expected behaviors auto-derived from generated classes:
    - UGA_Dash → cooldown tag 'Ability.Cooldown.Dash' applied + removed
    - UCUDashAttributeSet.Stamina → delta on activation

  Input sequence: Saved/TestRecordings/dash.urec (matched via class-name heuristic)

  Evaluator verdict: VERIFIED_LIVE_QA
  Overall Score: 95/100
  Findings: none
  Artifacts: Saved/Screenshots/MCP/qa_dash_*.png (3 files)

=== System Generation Report ===
System: DashAbility (gameplay) | Engine: UE 5.8 | ECABridge: available
[... normal report sections ...]

--- Phase 4.5: Live QA ---
✅ ns-ue-qa-evaluator: passed (score 95/100, 0 findings)
   Predicates verified: cooldown tag lifecycle, stamina delta applied
```

**Skip variants** (non-blocking informational):

```text
--- Phase 4.5: Live QA ---
⏭️  SKIPPED (non-gameplay system) — no behavioral predicates apply
   System classified as 'ui' via Phase 0 type detection.
```

```text
--- Phase 4.5: Live QA ---
⏭️  SKIPPED (editor offline) — re-run /full-cycle --live-qa to verify
   ue_check_health returned offline; create-system is generative, not gating.
```

```text
--- Phase 4.5: Live QA ---
⏭️  SKIPPED (UE_CREATE_SYSTEM_SKIP_LIVE_QA=1)
   Explicit opt-out via env var.
```

**Warning variant** (logic correct, visual-only miss, from UQ-008 pattern):

```text
--- Phase 4.5: Live QA ---
⚠️  ns-ue-qa-evaluator: passed with 1 warning (score 87/100)
   Predicates verified: cooldown applied, damage delta applied
   Warning: V4 (visual_correctness) — Niagara VFX reference null in BP_GA_Dash
   Gameplay logic is correct; fix VFX asset reference as follow-up.
```
