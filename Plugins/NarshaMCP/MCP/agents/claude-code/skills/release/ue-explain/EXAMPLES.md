# UE Explain — Examples

**Version**: 2.2.0
**Status**: Complete
**Parent**: [SKILL.md](SKILL.md)

This file holds per-domain walkthroughs for `/ue-explain`. SKILL.md § Step 2
points here for the per-call invocation templates that previously lived inline
(Issue #10510 — Epic #10451 Tier 3-B modularization).

---

## Scenario 1: C++ Class — ACharacter

### User Request
```
"ACharacter 어떻게 동작해?"
```

### Invocation
```bash
/ue-explain ACharacter
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="ACharacter", limit=10)
# Discovers: Engine symbol, multiple project subclasses

ue_read(identifier="ACharacter")
# Returns: class hierarchy, key methods, member variables
```

#### Step 1: Domain Detection
- Name prefix: `A` → Actor subclass → **cpp_class** domain
- Confirmed by Step 0: found in symbols domain

#### Step 2: Domain-Specific Analysis
```python
# 1. Search for the symbol — ALWAYS CALL (confirms PDB indexing, fast)
ue_analyze_symbols(operation="search_symbols", params={"query": "ACharacter", "symbol_type": "class"})

# 2. Get methods/members
# > Step 0 Skip: ue_grep returned method-level matches → skip. Call only if full method list is needed.
ue_analyze_symbols(operation="get_methods", params={"class_name": "ACharacter"})

# 3. Get inheritance hierarchy
# > Step 0 Skip: ue_read returned parent_class="APawn", child_count=12 → skip. Call only on explicit "뭘 상속해?" questions.
ue_analyze_symbols(operation="trace_hierarchy", params={"class_name": "ACharacter"})

# 4. Get virtual method overrides (always skip unless user asked)
# > Step 0 Skip: most expensive call. Only call when user mentions "override"/"재정의".
ue_analyze_symbols(operation="find_virtual_overrides", params={"class_name": "ACharacter"})
```

#### Step 3: Multi-Query Search Planning
```text
Query 1 (EXACT):   "ACharacter" → ue_analyze_symbols
Query 2 (RELATED): "CharacterMovementComponent" → ue_analyze_symbols
Query 3 (CONTEXT): "ACharacter::BeginPlay" → ue_grep in engine source
```

### Expected Output
```text
=== ACharacter Explanation ===

1. Identity
   Type: C++ Class (Engine)
   Hierarchy: AActor → APawn → ACharacter
   Module: Engine

2. Architecture
   - Inherits movement from UCharacterMovementComponent
   - Capsule component for collision (UCapsuleComponent)
   - Skeletal mesh for visual representation

3. Key Interfaces
   - Jump(): Initiates jump with JumpZVelocity
   - LaunchCharacter(): Apply impulse velocity
   - GetCharacterMovement(): Access movement component

4. Usage Patterns
   - Subclass for player characters (APlayerCharacter)
   - Override SetupPlayerInputComponent for input binding
   - Use CharacterMovementComponent for walking/falling/swimming

5. Related Entities
   - UCharacterMovementComponent (movement logic)
   - APlayerController (input processing)
   - UAnimInstance (animation state machine)
```

---

## Scenario 2: Blueprint — BP_PlayerController

### User Request
```
"Explain BP_PlayerController"
```

### Invocation
```bash
/ue-explain BP_PlayerController
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="BP_PlayerController", limit=10)
# Discovers: Content/Blueprints/BP_PlayerController.uasset

ue_read(identifier="BP_PlayerController")
# Returns: Blueprint structure summary with parent class, variables, event graph
```

#### Step 1: Domain Detection
- Name prefix: `BP_` → **blueprint** domain
- Confirmed by Step 0: found in assets domain

#### Step 2: Domain-Specific Analysis
```python
# 1. Get structure — ALWAYS CALL (binary format, Step 0 only provides summary)
ue_manage_blueprint(operation="get_structure", params={"blueprint_name": "BP_PlayerController"})

# 2. Get raw nodes
# > Step 0 Skip: get_structure already returned node details → skip.
# > Call only when get_structure returned just node_count without details.
ue_manage_blueprint(operation="get_raw_nodes", params={"blueprint_name": "BP_PlayerController", "limit": 20})

# 3. Search variables
# > Step 0 Skip: ue_read returned variables_preview (e.g. ["Health: float", "MaxHealth: float"]) → skip.
# > Call only when full variable list is needed.
ue_manage_blueprint(operation="search_variables", params={"blueprint_name": "BP_PlayerController", "pattern": "*"})

# 4. Trace execution
# > Step 0 Skip: only call when user explicitly asks about "실행 흐름", "어떻게 실행돼", "execution flow".
# > Skip on structure/explanation questions.
ue_trace_execution(operation="trace_execution_flow", params={
    "blueprint_name": "BP_PlayerController",
    "output_format": "mermaid"
})
```

### Expected Output
```text
=== BP_PlayerController Explanation ===

1. Identity
   Type: Blueprint (Asset)
   Parent: APlayerController
   Path: /Game/Blueprints/BP_PlayerController

2. Architecture
   - Event Graph: BeginPlay → InitializeHUD, SetupInput
   - 3 custom functions: InitializeHUD, HandlePause, SwitchCamera
   - 5 variables: HUDWidget, bIsPaused, CurrentCamera, ...

3. Key Interfaces
   - Event BeginPlay: Sets up HUD widget and input bindings
   - HandlePause(): Toggles game pause state
   - SwitchCamera(): Cycles between camera modes

4. Usage Patterns
   - Assigned in GameMode as default PlayerController class
   - Communicates with BP_HUD via direct reference

5. Related Entities
   - BP_HUD (UI management)
   - BP_PlayerCharacter (possessed pawn)
   - GM_Main (GameMode assigning this controller)
```

---

## Scenario 3: Material — M_BasicRock

### User Request
```
"M_BasicRock 설명해줘"
```

### Invocation
```bash
/ue-explain M_BasicRock
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="M_BasicRock", limit=10)
# Discovers: Content/Materials/M_BasicRock.uasset

ue_read(identifier="M_BasicRock")
# Returns: Material summary with domain, parameters, textures
```

#### Step 1: Domain Detection
- Name prefix: `M_` → **material** domain
- Confirmed by Step 0: found in assets as Material type

#### Step 2: Domain-Specific Analysis
```python
# Primary: Material structure (binary asset — MCP required)
ue_manage_material(operation="search_materials", query="M_BasicRock")
# Returns: material list with blend modes, shading models

# Secondary: Parameter details
ue_manage_material(operation="get_hlsl", material_name="M_BasicRock")
# Returns: generated HLSL code showing node graph logic
```

### Expected Output
```text
=== M_BasicRock Explanation ===

1. Identity
   Type: Material (Asset)
   Path: /Game/Materials/M_BasicRock
   Shading Model: Default Lit
   Blend Mode: Opaque

2. Architecture
   - Base Color: Texture sample (T_Rock_D) × Tint parameter
   - Normal: Texture sample (T_Rock_N) with intensity parameter
   - Roughness: Scalar parameter (0.7 default)
   - 2 Material Instances: MI_Rock_Mossy, MI_Rock_Wet

3. Key Parameters
   - Tint (Vector4): Base color multiplier
   - RoughnessScale (Scalar): Surface roughness [0-1]
   - NormalIntensity (Scalar): Normal map strength

4. Usage Patterns
   - Used by SM_Rock_Large, SM_Rock_Small static meshes
   - 2 material instances for environment variation

5. Related Entities
   - MI_Rock_Mossy (instance with green tint)
   - MI_Rock_Wet (instance with low roughness)
   - T_Rock_D, T_Rock_N (texture assets)
```

---

## Scenario 4: Niagara — NS_Fire

### User Request
```
"How does NS_Fire work?"
```

### Invocation
```bash
/ue-explain NS_Fire
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="NS_Fire", limit=10)
# Discovers: Content/VFX/NS_Fire.uasset

ue_read(identifier="NS_Fire")
# Returns: emitter count, parameter count summary
```

#### Step 1: Domain Detection
- Name prefix: `NS_` → **niagara** domain
- Confirmed by Step 0: found in assets as NiagaraSystem type

#### Step 2: Domain-Specific Analysis
```python
# Primary: System structure (binary asset — MCP required)
ue_manage_niagara(operation="get_structure", params={"system_name": "NS_Fire"})
# Returns: emitters, modules, render settings

# Secondary: System-level parameters
# > Step 0 Skip: skip when ue_read already returned parameter_count and the question is overview-only.
ue_manage_niagara(operation="get_parameters", params={"system_name": "NS_Fire"})
# Returns: User parameters, exposed to Blueprint
```

### Expected Output
```text
=== NS_Fire Explanation ===

1. Identity
   Type: NiagaraSystem (Asset)
   Path: /Game/VFX/NS_Fire
   Emitters: 3 (Flame, Smoke, Embers)

2. Architecture
   - Flame emitter: GPU sim, 500 particles, sprite renderer with M_Flame
   - Smoke emitter: CPU sim, 100 particles, mesh renderer with SM_SmokeQuad
   - Embers emitter: GPU sim, 200 particles, ribbon renderer

3. Key Parameters
   - User.Intensity (float): scales Spawn Rate across all emitters
   - User.Color (LinearColor): tints Flame emitter material parameter
   - User.Lifetime (float): overrides Particle.Lifetime in Spawn module

4. Usage Patterns
   - Spawn via UNiagaraComponent::Activate from BP_Torch / BP_Campfire
   - Set User parameters via SetVariable* nodes before Activate
   - Auto-deactivates after Embers emitter completes (no looping)

5. Related Entities
   - M_Flame (material referenced by Flame emitter sprite renderer)
   - SM_SmokeQuad (static mesh used by Smoke mesh renderer)
   - BP_Torch, BP_Campfire (spawners)
```

---

## Scenario 5: GAS Ability — GA_Attack

### User Request
```
"GA_Attack 설명해줘"
```

### Invocation
```bash
/ue-explain GA_Attack
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="GA_Attack", limit=10)
# Discovers: Content/Abilities/GA_Attack.uasset

ue_read(identifier="GA_Attack")
# Returns: parent class (UGameplayAbility), tag summary
```

#### Step 1: Domain Detection
- Name prefix: `GA_` → **ability** domain
- Confirmed by Step 0: found in assets as GameplayAbility type

#### Step 2: Domain-Specific Analysis
```python
# Primary: Ability flow tracing (Step 0 cannot infer this)
ue_trace_execution(operation="trace_ability_flow", params={"ability_name": "GA_Attack"})
# Returns: activation tags, blocking tags, ability tasks, applied effects

# Secondary: Tag → Ability binding lookup
ue_manage_gameplay(operation="trace_abilities", params={"ability_name": "GA_Attack"})
# Returns: granting AbilitySystemComponent, input bindings, cost / cooldown effects
```

### Expected Output
```text
=== GA_Attack Explanation ===

1. Identity
   Type: GameplayAbility (Blueprint)
   Parent: UGameplayAbility
   Path: /Game/Abilities/GA_Attack

2. Architecture
   - Activation: Ability.Combat.Attack (Gameplay Tag)
   - Blocked By: Ability.Combat.Stunned, Ability.Movement.Dashing
   - Tasks: PlayMontageAndWait → WaitGameplayEvent → ApplyDamage
   - Costs: GE_AttackCost (drains 10 Stamina via AttributeSet)
   - Cooldown: GE_AttackCooldown (1.5s)

3. Key Interfaces
   - ActivateAbility: triggered by IA_Attack input action
   - K2_OnEndAbility: cleanup hook for animation state restoration
   - K2_CanActivateAbility: gates activation on Stamina ≥ 10

4. Usage Patterns
   - Granted to player character on BeginPlay via GiveAbility
   - Bound to LeftMouseButton via Input → InputTag → Ability chain
   - Triggers PlayMontage → AnimNotify → ApplyDamage on hit frame

5. Related Entities
   - AM_Attack (animation montage played by PlayMontageAndWait)
   - GE_Damage (effect applied on successful hit)
   - AS_Combat (AttributeSet holding Stamina/Health attributes)
```

---

## Scenario 6: Config / CVar — r.Shadow.MaxResolution

### User Request
```
"r.Shadow.MaxResolution 뭐야?"
```

### Invocation
```bash
/ue-explain r.Shadow.MaxResolution
```

### Step-by-Step Workflow

#### Step 0: Discovery Pre-Exploration
```python
ue_grep(query="r.Shadow.MaxResolution", limit=10)
# Discovers: Engine/Config/BaseEngine.ini, Renderer/ShadowRendering.cpp

ue_read(identifier="r.Shadow.MaxResolution")
# Returns: cvar_type=int32, default_value=2048, help_text, ECVF flags, source_file
```

#### Step 1: Domain Detection
- Pattern: `r.` prefix → **config** domain
- Confirmed by Step 0: found in BaseEngine.ini

#### Step 2: Domain-Specific Analysis (Step 0 Full Bypass)

> `ue_read` already returned `cvar_type`, `default_value`, `help_text`, `flags`, `source_file`.
> Sufficient for "설명해줘" queries — `ue_analyze_config` is **NOT needed**.
>
> Call `ue_analyze_config` only when the user asks about:
> - Override layer ("어디서 덮어쓰는지", "어떤 ini에서 설정되는지")
> - Modifying the value
> - `ue_read` returned an empty `help_text` (rare)

```python
# Only for override-layer questions:
ue_analyze_config(operation="search_config", params={"key": "r.Shadow.MaxResolution"})
# Returns: full INI cascade across BaseEngine.ini → DefaultEngine.ini → WindowsEngine.ini → user override
```

> **CVar Exact Name Search Fallback** (Issue #6681): if exact name returns 0 results,
> try partial (`Shadow.MaxResolution` then `MaxResolution`) and finally
> `ue_grep("MaxResolution")` to locate the `TAutoConsoleVariable` declaration.

### Expected Output
```text
=== r.Shadow.MaxResolution Explanation ===

1. Identity
   Type: Console Variable (CVar)
   Default: 2048
   Type: int32
   Source: Engine/Source/Runtime/Renderer/Private/ShadowRendering.cpp

2. Architecture
   - ECVF Flags: ECVF_Scalability, ECVF_RenderThreadSafe
   - Scalability Group: sg.ShadowQuality
   - Read by: FProjectedShadowInitializer::InitDynamicProjectedShadow

3. Behavior
   - Caps the resolution (in pixels per dimension) of dynamic shadow maps
   - Applied per-shadow at projection setup; higher = sharper but more VRAM
   - Lyra/scalability presets override this via DeviceProfiles

4. Override Layers (lowest to highest precedence)
   - BaseEngine.ini [SystemSettings]: 2048 (default)
   - DefaultEngine.ini [SystemSettings]: project-level override (if any)
   - DeviceProfile (Windows/Mobile/etc.) per-platform tuning
   - User console / -ExecCmds runtime override

5. Related CVars
   - r.Shadow.MaxCSMResolution (cascade-shadow-map cap)
   - r.Shadow.RadiusThreshold (LOD culling)
   - sg.ShadowQuality (scalability group governing this CVar)
```

---

## Notes

- **Domain Auto-Detection**: Prefix-based routing (A/U→C++, BP_→Blueprint, M_/MI_→Material, NS_→Niagara, GA_→GAS, r./sg.→Config) — full table in [SKILL.md § Step 1](SKILL.md#step-1-identify-target-type-domain-auto-routing)
- **Multi-Query Planning**: Plan 3 queries per domain before executing — execute ALL even if first returns results (anti first-hit bias). Detail in [REFERENCE.md § Multi-Query Search Planning](REFERENCE.md#multi-query-search-planning).
- **Step 0 Bypass**: Config/CVar targets (`r.*`, `sg.*`) skip Step 2 entirely when `ue_read` returned `help_text` (see Scenario 6).
- **Binary Assets**: Blueprint, Material, Niagara, GAS assets require MCP tools — built-in `Read` returns unreadable binary data.
- **Engine source verification**: After MCP tools return, grep exact names in `UECODEGEN_ENGINE_DIR` to discover CVar exact strings, CL# refs in comments. Only cite line numbers actually `Read()`. Domain paths in [REFERENCE.md § Engine Source Verification](REFERENCE.md#engine-source-verification).
