# UE Scaffold - Examples

> **1.6.0** — ECA asset creation added (Issue #7409)

## Example 1: Melee Combat Character

**User**: "근접 전투 캐릭터 만들어줘"

### Step 1: Feature Classification

```text
Detected: character
Base class: ACharacter
Features: enhanced_input, damage_system
Module: MyGame
```

### Step 2: Architecture Suggestion

```text
Proposed Classes:
  1. AMyMeleeCharacter (ACharacter)
     Features: enhanced_input, damage_system
     Files: MyMeleeCharacter.h, MyMeleeCharacter.cpp

Dependencies: EnhancedInput, GameplayTags
```

### Step 3: Conflict Check

```python
ue_analyze_symbols(operation="search_symbols", params={"query": "AMyMeleeCharacter"})
# → 0 results — no conflict
```

### Step 4: Code Generation

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "ACharacter",
    "class_name": "AMyMeleeCharacter",
    "output_dir": "Source/MyGame/Characters",
    "module_name": "MyGame",
    "features": ["enhanced_input", "damage_system"]
})
# → MyMeleeCharacter.h (52 lines)
# → MyMeleeCharacter.cpp (68 lines)
```

### Step 4.5: Asset Creation Guidance

```text
ue_check_health()
→ NarshaMCP path: Output manual guidance (ECA channel removed in #11230/#11231):
  "Create BP_MeleeCharacter in Editor: Content Browser → Blueprint Class → Parent: AMyMeleeCharacter"
```

### Step 5: Validation

```python
ue_generate_code(operation="dependency_check", params={
    "class_name": "AMyMeleeCharacter",
    "module_name": "MyGame"
})
# → All modules resolved ✅
```

### Output

```text
=== Scaffold Report ===
Feature: Melee Combat Character (character)
Files: 2 (1 class × .h + .cpp)
Lines: ~120
ECA Assets: None (ECA unavailable)
Validation: PASS

Next Steps:
  - Build project to verify compilation
  - Create BP_MeleeCharacter in Editor
```

---

## Example 2: GAS Ability Component

**User**: "Build a GAS ability component with cooldown"

### Step 1: Feature Classification

```text
Detected: ability_system
Base class: UGameplayAbility
Features: gas_ability_base, gas_ability_cost
Module: MyGame
```

### Step 4: Code Generation

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "UGameplayAbility",
    "class_name": "UGA_MyAbility",
    "output_dir": "Source/MyGame/Abilities",
    "module_name": "MyGame",
    "features": ["gas_ability_base", "gas_ability_cost"]
})
# → GA_MyAbility.h (36 lines), GA_MyAbility.cpp (45 lines)
```

### Step 5: Validation

```text
Dependencies: GameplayAbilities, GameplayTags, GameplayTasks
→ All resolved ✅
```

---

## Example 3: ECA-Enhanced Scaffold (UE 5.8+) [REMOVED post-Epic #11230]

> **Path A (ECABridge) — REMOVED post-Epic #11230 / PR #11295 (2026-05-09)**
>
> The ECA-Enhanced Scaffold path (`ue_eca_proxy(call_tool, ...)` for `lisp_to_blueprint` / `create_niagara_system` / `setup_enhanced_input` / `add_material_node`) was deleted alongside the ECABridge stack. The ECABridge plugin was confirmed not present in Epic's public UE 5.8 mirror, so the LISP write-back channel is no longer reachable from any engine version. **Scaffolding now always uses Path B** (Examples 1 + 2: `ue_generate_code` + manual asset creation guidance) regardless of engine version. The Example 3 block below is retained for historical reference only — see [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md).


**User**: "무기 아이템 만들어줘" (on UE 5.8 project)

### Step 1: Feature Classification

```text
Detected: weapon_item
Base class: AActor
Features: weapon_fire_state, ability_projectile_spawn
```

### Step 4: Code Generation

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "AActor",
    "class_name": "AMyWeapon",
    "output_dir": "Source/MyGame/Weapons",
    "features": ["weapon_fire_state", "ability_projectile_spawn"]
})
# → AMyWeapon.h + .cpp generated
```

### Step 4.5: ECA Asset Creation (Path A)

```text
ue_check_health() → engine_version: 5.8
→ generation always uses the NarshaMCP path (ECA channel removed in #11230/#11231)
```

```python
# 4.5a-0: Discover tools
ue_eca_proxy(operation="list_tools", query="blueprint")

# 4.5a-1: BP subclass
ue_eca_proxy(operation="call_tool",
    tool_name="lisp_to_blueprint",
    arguments={
        "blueprint_name": "BP_MyWeapon",
        "parent_class": "AMyWeapon",
        "lisp": "(BeginPlay (PrintString \"Hello from MyWeapon\"))"
    })
# → BP_MyWeapon created ✅

# 4.5a-2: VFX (weapon_item → muzzle_flash)
ue_eca_proxy(operation="call_tool",
    tool_name="create_niagara_system",
    arguments={
        "system_name": "NS_MyWeapon",
        "template": "muzzle_flash"
    })
# → NS_MyWeapon created ✅

# 4.5a-3: Material (weapon_item only)
ue_eca_proxy(operation="call_tool",
    tool_name="add_material_node",
    arguments={
        "material_name": "M_MyWeapon",
        "node_type": "TextureSample",
        "connect_to": "BaseColor"
    })
# → M_MyWeapon created ✅
```

### Output

```text
=== Scaffold Report ===
Feature: Weapon Item (weapon_item)
Files: 2 (1 class × .h + .cpp)
Lines: ~95
ECA Assets: 3 (BP_MyWeapon, NS_MyWeapon, M_MyWeapon)
Validation: PASS

ECA Asset Summary:
  ✅ BP_MyWeapon — Blueprint subclass of AMyWeapon
  ✅ NS_MyWeapon — Niagara muzzle flash VFX
  ✅ M_MyWeapon — Base material with texture sample
```

> **Note**: ECA Path A requires Issue #7406 (ECA Client). Until implemented, Path B (manual guidance) always executes.
