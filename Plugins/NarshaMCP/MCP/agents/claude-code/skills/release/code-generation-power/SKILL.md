---
name: code-generation-power
model: opus
description: "Purpose: multi-class system generation (3-10 classes). 17 built-in scenarios (GAS Character, Weapon, AI Enemy, Multiplayer). 7-phase E2E: Scenario → Skeleton → Enrichment → Validation → BP Creation → Input Setup → Final Verification. Editor-dependent phases degrade gracefully via Editor Remote Control. For single class, use ue_generate_code(smart) directly. Triggers: '무기 시스템 만들어', 'GAS 캐릭터 시스템', 'generate weapon system', '멀티클래스 생성', 'full system generation'."
argument-hint: "system description (e.g., 'GAS character system', 'weapon system') or --self-test"
allowed-tools:
  - Read
  - Edit
  - Write
  - Glob
  - Grep
  - Bash
  - Task
  - TodoWrite
  - mcp__narshamcp__ue_generate_code
  - mcp__narshamcp__ue_check_health
  - mcp__narshamcp__ue_fix_errors
  - mcp__narshamcp__ue_manage_blueprint
  - mcp__narshamcp__ue_manage_gameplay
  - mcp__narshamcp__ue_analyze_symbols
  - mcp__narshamcp__ue_build_pipeline
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.2.0"
  author: "Next-Stage-Inc"
  license: "Proprietary"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Power Code Generation

**Priority**: High
**Goal**: One-command multi-class system generation with E2E Blueprint/Input integration and validation

> Cold-path detail (per-scenario enrichment templates, phase reference, evaluator loop, ECA Phase 4A, scenario registry):
> [SCENARIOS.md](SCENARIOS.md) (per-scenario detail) | [REFERENCE.md](REFERENCE.md) (phase/atom/loop reference).
> SKILL.md keeps only the dispatch/routing logic.

---

## Purpose

Orchestrates **multi-class system generation** for complete UE gameplay systems:

- **17 built-in scenarios** (GAS Character, Weapon, AI Enemy, Multiplayer, etc.)
- **7-Phase E2E workflow**: Scenario Analysis → Skeleton → Enrichment → Validation → BP Creation → Input Setup → Final Verification
- **MCP + Claude hybrid**: `ue_generate_code` for skeleton + Claude for production-ready enrichment

**Key Difference vs `code-generation` skill**: code-generation = single class skeleton + preflight 1회. Power = multi-class scenario + enrichment + validation loop with auto-fix.

---

## Trigger Phrases

### Korean
- "파워 코드 생성", "시나리오 생성", "멀티클래스 생성"
- "GAS 캐릭터 시스템 만들어", "무기 시스템 만들어"
- "AI 적 시스템", "멀티플레이어 캐릭터 시스템"
- "완전한 시스템 생성", "시스템 코드 생성"
- "체력 컴포넌트 시스템", "투사체 시스템"

### English
- "power codegen", "scenario generation", "multi-class generation"
- "generate GAS character system", "generate weapon system"
- "generate AI enemy system", "generate multiplayer character"
- "full system generation", "system codegen"
- "generate health component system", "generate projectile system"

---

## 7-Phase E2E Workflow

| Phase | Action | Tool | Output | Editor needed |
|-------|--------|------|--------|---------------|
| **1: Scenario Analysis** | Match user request → 1 of 17 built-in scenarios or custom plan | (Claude) | Class generation plan (order, organisms, features) | No |
| **2: Skeleton Generation** | Generate base C++ skeletons | `ue_generate_code(suggest_class \| derive_class \| generate_class)` | `.h`/`.cpp` files | No |
| **2.5: Enrichment** | Per-scenario enrichment (UPROPERTY, replication, lifecycle) | Claude (Write/Edit) | Production-ready C++ | No |
| **3: Validation** | Preflight + auto-fix | `ue_fix_errors(preflight \| smart)`, `ue_analyze_symbols` | Build-ready code, errors resolved | No |
| **3.7 (Editor Check)** | Detect Editor reachability | `ue_check_health` | `editor_control` | Light probe |
| **4 (Editor RC)** | BP subclass via Editor RC | `ue_manage_blueprint(create_blueprint)` | BP subclass | Yes (Editor) |
| **5: Input Setup** | Create IA + IMC mappings | `ue_manage_gameplay(create_input_action \| create_input_mapping)` | Enhanced Input config | Yes (Editor) |
| **6: GAS Configuration** | Tags + Effects | `ue_manage_gameplay` (gameplay tag/effect ops) | Tag tables, GE assets | Yes (Editor) |
| **7: Final Verification** | Project state check, summary | `ue_check_health` | Pass/fail report | No |

> **MCP calls must be sequential** (1 at a time). Parallel calls cause connection reset.
> If `editor_control == false`: skip Phases 4-6; print manual guidance.
> **Auto-layout (Issue #17059 §P4)**: any Blueprint graph built in Phase 4 is laid out
> automatically (`build_graph` runs `auto_layout`; `LayoutStrategy` auto-detected — DAG
> left→right, AnimStateMachine 2D). Never hand-place nodes; after `add_node` edits, end with
> one `ue_manage_blueprint(operation="auto_layout")`.
> Full per-phase tool parameters: [REFERENCE.md § Phase Reference (7 Phases)](REFERENCE.md#phase-reference-7-phases).

**Scenario decision tree (Phase 1)**:

```text
"GAS" + "character"     → Scenario 1 (GAS Character System)
"weapon" / "무기"       → Scenario 2 (Weapon System)
"AI" + "enemy" / "적"    → Scenario 3 (AI Enemy System)
"multiplayer" / "멀티"  → Scenario 4 (Multiplayer Character)
"health" / "체력"       → Scenario 5 (Health Component)
"ability" / "어빌리티"  → Scenario 6 (Gameplay Ability Set)
"widget" / "HUD"        → Scenario 7 (Widget/HUD System)
"projectile" / "투사체" → Scenario 8 (Projectile System)
"item" / "inventory"    → Scenario 9 (DataAsset Item System)
"input" / "입력"        → Scenario 10 (Enhanced Input Setup)
No match                → Ask user to pick or build custom scenario
```

---

## Feature Capability Matrix (v2.0.0 — E2E Character Creation)

50 features mapped to Phase 2 / 2.5 / 4 injection. Common GAS-related features:

| Feature | Phase 2 (skeleton) | Phase 2.5 (enrichment) | Phase 4 (BP/Editor) |
|---------|---------------------|------------------------|---------------------|
| `gas_attribute_set` | UCLASS skeleton | `FGameplayAttributeData`, `ATTRIBUTE_ACCESSORS`, `OnRep_*`, `GetLifetimeReplicatedProps` | — |
| `gas_ability` | UGameplayAbility skeleton | Tag list, cost/cooldown, `ActivateAbility` body | — |
| `gas_effect` | UGameplayEffect skeleton | Modifier list, GameplayEffectComponents | Asset creation in Editor |
| `replication_basic` | UPROPERTY skeleton | `Replicated` specifier, `GetLifetimeReplicatedProps`, `OnRep_*` | — |
| `enhanced_input_setup` | UInputComponent skeleton | `BindAction` for IA assets | IA + IMC asset creation (Phase 5) |
| `bp_subclass` | — | — | `ue_manage_blueprint(create_blueprint, parent=AMyCharacter)` |

> Full 50-feature list + injection decision rules:
> [REFERENCE.md § Feature Atom List](REFERENCE.md#feature-atom-list).

### Known Limitations
- Phase 4-6 require running Editor; without it, skill produces C++ only and prints manual instructions for BP/Input/GAS asset creation.
- ECA Phase 4A only available on UE 5.8+ (graceful degradation to Phase 4B otherwise).

---

## 17 Built-in Scenarios

### Tier 1 — Critical

| # | Name | Classes | Lines | Notes |
|---|------|---------|-------|-------|
| 1 | **GAS Character System** | 4 (Character + AttributeSet + Ability + HealthComponent) | 362 | **Verified** — 4/4 Production. [Detail](SCENARIOS.md#scenario-1-gas-character-system) |
| 2 | **Weapon System** | 4 (Weapon + WeaponComponent + FireAbility + Projectile) | 225 | **Verified** — 4/4 Production. [Detail](SCENARIOS.md#scenario-2-weapon-system) |
| 3 | **AI Enemy System** | 4 (Enemy + AIController + EnemyAbility + AttributeSet) | 309 | **Verified** — 4/4 Production. [Detail](SCENARIOS.md#scenario-3-ai-enemy-system) |
| 4 | **Multiplayer Character** | 4 (MPCharacter + PlayerState + Controller + GameMode) | 234 | **Verified** — 4/4 Production. [Detail](SCENARIOS.md#scenario-4-multiplayer-character) |

### Tier 2 — High

| # | Name | Classes | Lines | Notes |
|---|------|---------|-------|-------|
| 5 | **Health Component** | 3 (HealthComponent + AttributeSet + HealEffect) | 213 | **Verified** — 3/3 Production. [Detail](SCENARIOS.md#scenario-5-health-component) |
| 6 | **Gameplay Ability Set** | 3 (BaseAbility + CostAbility + CooldownEffect) | 156 | **Verified** — 3/3 Production. [Detail](SCENARIOS.md#scenario-6-gameplay-ability-set) |
| 7 | **Widget/HUD System** | 3 (HUDWidget + HealthBar + HUD) | 81 | **Verified** — 3/3 Clean. [Detail](SCENARIOS.md#scenario-7-widgethud-system) |
| 8 | **Projectile System** | 3 (Projectile + ProjectileMovement + FireAbility) | 168 | **Verified** — 3/3 Production. [Detail](SCENARIOS.md#scenario-8-projectile-system) |

### Tier 3 — Medium

| # | Name | Classes | Lines | Notes |
|---|------|---------|-------|-------|
| 9 | **DataAsset Item System** | 3 (ItemData + InventoryComponent + PickupActor) | 137 | **Verified** — 3/3 Production. [Detail](SCENARIOS.md#scenario-9-dataasset-item-system) |
| 10 | **Enhanced Input Setup** | 3 (InputConfig + InputComponent + Controller) | 123 | **Verified** — 3/3 Production. [Detail](SCENARIOS.md#scenario-10-enhanced-input-setup) |
| 11-17 | **Tier 3 Additional** | varies | varies | [Catalog](SCENARIOS.md#scenarios-11-17-tier-3-additional) |

> Public-facing scenario catalog (separate user-guide doc):
> [docs/user-guides/tools/CODE_GENERATION_SCENARIOS.md](../../../docs/user-guides/tools/CODE_GENERATION_SCENARIOS.md).

### Scenario-Phase Mapping (Phase 4-7)

| # | Scenario | Phase 4 (BP) | Phase 5 (Input) | Phase 6 (GAS) |
|---|----------|-------------|----------------|--------------|
| 1 | GAS Character System | `BP_MyCharacter` | `IA_Move`, `IA_Jump`, `IA_Look` | Tags + Effects |
| 2 | Weapon System | `BP_MyWeapon` | — | — |
| 3 | AI Enemy System | `BP_MyEnemy` | — | — |
| 4 | Multiplayer Character | `BP_MyMPCharacter` | `IA_Move`, `IA_Jump`, `IA_Look` | — |
| 5 | Health Component | — | — | — |
| 6 | Gameplay Ability Set | — | — | Tags + Effects |
| 7 | Widget/HUD System | — | — | — |
| 8 | Projectile System | — | — | — |
| 9 | DataAsset Item System | — | — | — |
| 10 | Enhanced Input Setup | — | `IA_Move`, `IA_Jump`, `IA_Look` | — |
| 11-17 | Tier 3 Scenarios | — | — | Varies |

**Legend**: `—` = phase skipped for this scenario. Phase 7 (Final Verification) always runs.

---

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `--scenario` | (auto-detect) | Scenario name (e.g., `gas-character`) |
| `--output-dir` | `Source/{Module}/` | Output base path |
| `--module` | (project main module) | Target module |
| `--skip-phase` | (none) | Skip phases (e.g., `4,5,6` for headless mode) |
| `--self-test` | — | Validate skill structure (no generation) |

**Env**: `UECODEGEN_EVALUATOR_LOOP=1` enables Phase 3.3 evaluator loop (Issue #8168). Loop config + stagnation detection + exit conditions: [REFERENCE.md § Evaluator Loop Reference](REFERENCE.md#evaluator-loop-reference-issue-8168-phase-33).

---

## Safety Mechanisms

- **Sequential MCP calls**: Skill enforces 1-at-a-time MCP tool usage. Parallel = connection reset.
- **Phase gating**: Phase 4-6 only run when `ue_check_health.capabilities.editor_control == true`. Otherwise: print manual steps and continue.
- **BP generation path**: Phase 4 always uses the NarshaMCP path (`ue_manage_blueprint` T3D/Editor RC). The ECA generation channel was removed (#11230/#11231), so there is no ECA-vs-fallback branch; `eca_bridge_available` is no longer emitted by `ue_check_health` (removed in #15171).
- **Backup before write**: Skill never overwrites existing files without explicit `--force`. Default: skip + warn if target file exists.
- **Build verification**: Phase 7 runs `ue_check_health` final probe; non-zero compile errors = report failure (does not auto-revert).

---

## Output Format

```text
═══════════════════════════════════════════
 Power Code Generation — Final Report
═══════════════════════════════════════════
 Scenario:    <name> (Tier <T>)
 Classes:     <N> generated
 Phases run:  1, 2, 2.5, 3, [3.7], [4A|4B], [5], [6], 7
 Status:      <success | partial | failed>

 Generated files:
   Source/<Module>/<Class1>.h  /  .cpp
   Source/<Module>/<Class2>.h  /  .cpp
   ...

 Editor-side artifacts (if Phases 4-6 ran):
   /Game/Blueprints/BP_<Name>
   /Game/Input/IA_<Name>
   /Game/GAS/Tags/<Tag>

 Build errors (if any):
   <file:line> <error message>     → ue_fix_errors auto-fix attempt: <result>

 Next steps:
   - Open <BP> in Editor to set defaults
   - Compile project (Build.cs auto-updated for new modules)
═══════════════════════════════════════════
```

---

## Self-Test

`/code-generation-power --self-test` validates skill structure (no generation). Full spec: [REFERENCE.md § Self-Test](REFERENCE.md#self-test).

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| `ue_generate_code` fails | PDB not loaded or invalid base class | Skip class, continue scenario; final report flags missing class |
| Editor not running (Phase 4-6) | `editor_control == false` | Skip Editor phases; print manual asset-creation steps |
| `ue_fix_errors` cannot resolve | Compile error not in known patterns | Continue with remaining phases; final report flags unresolved errors |
| Phase 7 reports failure | Build broken | Do NOT auto-revert; print rollback guidance (`git restore Source/<Module>/`) |

---

## Related

- `/code-generation` — Single class skeleton (use this for 1-class tasks)
- `/ue-scaffold` — Feature scaffolding (architecture design + 1-2 classes)
- `/ue-create-system` — Cross-domain (C++ + BP + VFX + Material + UI)
- `/ue-fix-errors` — Direct error resolution after manual code edits
