# UE Scaffold - Reference

> Technical reference for feature scaffolding with architecture design and code generation.

## Feature Type Detection Rules

| Feature Type | Detection Keywords | Base Class | Default Features |
|---|---|---|---|
| `character` | character, hero, player, pawn | ACharacter | enhanced_input |
| `ability_system` | ability, GAS, gameplay ability | UGameplayAbility | gas_ability_base |
| `weapon_item` | weapon, gun, sword, projectile | AActor | weapon_fire_state |
| `ui_widget` | widget, HUD, menu, UI | UUserWidget | — |
| `ai_npc` | AI, NPC, enemy, bot, patrol | ACharacter | ai_controller_base |
| `component` | component, subsystem | UActorComponent | actor_component_base |
| `game_mode` | game mode, game state, match | AGameModeBase | — |

## MCP Call Budget (current, #7131)

> **Target: 4-5 core MCP calls per invocation** (Step-0 grep + suggest + scaffold + dep check). ECA-asset step removed in #11231 / Epic #11230.

| Tool | Max Calls | Notes |
|------|-----------|-------|
| `ToolSearch` | **1** | Use full name: `select:mcp__narshamcp__ue_generate_code` |
| `ue_generate_code` | **2** | Call 1: `suggest_class` (dry-run) + Call 2: `derive_class` or `scaffold_class` |
| `ue_fix_errors` | **1** | `dependency_check` for post-generation validation |
| `ue_grep` / `ue_read` (MCP) | **1-2** | Step 0 lightweight discovery (replaces expensive `trace_hierarchy`) |
| `ue_analyze_symbols` | **0 — DO NOT USE** | Step 0 `ue_read`/`ue_grep` replaces `trace_hierarchy` |

> **Historical budget (pre-#11231, ECA era)**: the old table listed `ue_eca_proxy(list_tools/call_tool)` 0-4 and `ue_check_health` 0-1 for "Step 4.5 ECA detection/asset creation" (total 3-11 calls). All ECA rows are obsolete since Epic #11230/#11231; kept here only for historical context.

## Feature → Atom Mapping

| Feature Atom | Description | Adds |
|---|---|---|
| `enhanced_input` | Enhanced Input System setup | SetupPlayerInputComponent, IMC binding |
| `gas_setup` | GAS integration | IAbilitySystemInterface, ASC UPROPERTY |
| `gas_ability_base` | Ability base class | ActivateAbility, EndAbility |
| `gas_ability_cost` | Ability cost/cooldown | Cost GE, Cooldown GE |
| `gas_attributeset_base` | Attribute set | ATTRIBUTE_ACCESSORS macros |
| `damage_system` | Damage handling | ApplyDamage, OnHealthChanged delegate |
| `weapon_fire_state` | Weapon FSM | Fire/Reload states, Ammo count |
| `ability_projectile_spawn` | Projectile spawning | SpawnActor in ability |
| `replication` | Network replication | Replicated, DOREPLIFETIME |
| `tick` | Tick component | TickComponent override |
| `actor_component_base` | Component lifecycle | BeginPlay, EndPlay |
| `ai_controller_base` | AI controller | Possess, RunBehaviorTree |
| `inventory_slot` | Inventory slot/storage scaffold | Slot data and inventory operations |

## Domain Mapping (Step 4.5) — historical ECA tools

> **Historical (ECA removed).** The tool names below (`lisp_to_blueprint`, `create_niagara_system`, etc.) were the ECABridge mappings, removed in Epic #11230/#11231. Generation now uses the NarshaMCP path (see "Step 4.5 Generation Path" below). Retained for historical context.

| Feature Type | BP Subclass | VFX | Material | UI |
|---|---|---|---|---|
| character | `lisp_to_blueprint` | — | — | — |
| ability_system | `lisp_to_blueprint` | `create_niagara_system` (hit_impact) | — | — |
| weapon_item | `lisp_to_blueprint` | `create_niagara_system` (muzzle_flash) | `add_material_node` | — |
| ui_widget | — | — | — | `create_umg_widget_blueprint` |
| ai_npc | `lisp_to_blueprint` | — | — | — |
| component | — | — | — | — |
| game_mode | — | — | — | — |

## Step 4.5 Generation Path

ECA channel removed in #11230/#11231 (`eca_bridge_available` field removed from `ue_check_health` in #15171), so generation ALWAYS uses the NarshaMCP path — no health-gated branch.

```text
Generation (NarshaMCP path — always)
├─ Feature has BP domain? → ue_manage_blueprint T3D pipeline
├─ Feature has VFX domain? → ue_manage_niagara
├─ Feature has Material domain? → ue_manage_material
├─ Feature has UI domain? → ue_manage_widget
└─ Per-domain: success → ✅ | unsupported → skip + manual guidance
```

## Self-Test

`/ue-scaffold --self-test` validates:

1. **MCP connectivity**: `ue_check_health()` responds
2. **Symbol search**: `ue_analyze_symbols(search_symbols)` returns results for a known class
3. **Code generation**: `ue_generate_code(derive_class)` with test params produces valid output
4. **Dependency check**: `ue_generate_code(dependency_check)` resolves module dependencies
5. **ECA detection**: removed — the ECA/ECABridge channel was dropped (Epic #11230/#11231) and the `eca_bridge_available` flag was removed in #15171; generation always uses the NarshaMCP path.

**Expected**: Steps 1-4 always pass. Step 5: ECA is removed — the NarshaMCP path is always used.

---

## FEATURE_PATTERNS classification dict

Full Step 1 keyword → base_class → components/related/features mapping (moved from SKILL.md):

```python
FEATURE_PATTERNS = {
    "character": {
        "keywords": ["character", "player", "캐릭터", "플레이어", "hero", "pawn", "movement", "이동"],
        "base_class": "ACharacter",
        "components": ["UCharacterMovementComponent", "UCapsuleComponent", "USkeletalMeshComponent"],
        "features": ["enhanced_input", "replication"]
    },
    "ability_system": {
        "keywords": ["ability", "GAS", "combat", "attack", "어빌리티", "전투", "공격", "spell", "skill"],
        "base_class": "ACharacter",
        "related": ["UAbilitySystemComponent", "UAttributeSet", "UGameplayAbility"],
        "features": ["gas_setup", "replication"]
    },
    "inventory_system": {
        "keywords": ["inventory", "인벤토리", "inventory system", "item storage", "아이템 보관"],
        "base_class": "UActorComponent",
        "features": ["inventory_slot", "replication"]
    },
    "weapon_item": {
        "keywords": ["weapon", "item", "무기", "아이템", "pickup", "equipment", "장비"],
        "base_class": "AActor",
        "components": ["UStaticMeshComponent", "USphereComponent"],
        "features": ["replication"]
    },
    "ui_widget": {
        "keywords": ["UI", "widget", "HUD", "menu", "위젯", "메뉴", "화면", "인터페이스"],
        "base_class": "UUserWidget",
        "features": ["widget_setup"]
    },
    "ai_npc": {
        "keywords": ["AI", "NPC", "enemy", "적", "봇", "patrol", "behavior", "순찰", "state tree"],
        "base_class": "AAIController",
        "related": ["ACharacter (NPC pawn)", "UBehaviorTree/UStateTree"],
        "features": ["ai_setup"]
    },
    "component": {
        "keywords": ["component", "컴포넌트", "system component", "actor component"],
        "base_class": "UActorComponent",
        "features": ["replication", "tick"]
    },
    "game_mode": {
        "keywords": ["game mode", "게임 모드", "match", "round", "game state"],
        "base_class": "AGameModeBase",
        "related": ["AGameStateBase", "APlayerState"],
        "features": ["multiplayer"]
    }
}
```

## Exact Param Examples by Feature Type

(Moved from SKILL.md Step 4 to keep the always-loaded body under the 1000-word budget, Issue #15438.) Concrete per-type `ue_generate_code` calls — use these exact param sets to avoid parameter failures:

```python
# Character with GAS
ue_generate_code(operation="derive_class", class_name="AMyCharacter", base_class="ACharacter",
    output_dir="Source/MyGame/Characters", module_name="MyGame",
    features=["gas_setup", "enhanced_input", "replication"])
# AI Enemy NPC
ue_generate_code(operation="derive_class", base_class="AAIController", class_name="AMyEnemyAIController",
    output_dir="Source/MyGame/AI", module_name="MyGame", features=["ai_setup"])
# UI Widget
ue_generate_code(operation="derive_class", base_class="UUserWidget", class_name="UMyHealthBar",
    output_dir="Source/MyGame/UI", module_name="MyGame", features=["widget_setup"])
# Inventory Component
ue_generate_code(operation="derive_class", base_class="UActorComponent", class_name="UMyInventoryComponent",
    output_dir="Source/MyGame/Components", module_name="MyGame", features=["inventory_slot", "replication"])
```

## Step 0 Detail

(Moved from SKILL.md Step 0.)

**Result variables** (`step0_*`, referenced by Step 2/3 to skip redundant calls):
- `step0_similar_classes` — 프로젝트 내 유사 클래스 목록 (naming convention 참고)
- `step0_read_detail` — `ue_read` 클래스 구조 응답 (parent_class 포함 → `trace_hierarchy` 스킵 근거)
- `step0_naming_conflict` — 제안할 클래스명이 이미 존재하는지 여부
- `step0_project_context` — `class_name`/`base_class`/feature atom을 로컬에서 결정할 기존 프로젝트 패턴 정보 (`suggest_class`에 직접 전달하지 않음)

**Secondary discovery / conflict-check pass (Issue #6289)** — before code generation, check for naming conflicts and existing implementations:

```python
ue_glob(pattern="<class_name>*", domain="all", limit=10)  # 0a. existing files/symbols with similar names
ue_read(identifier="<base_class>")                         # 0b. if base class specified, verify it exists
```

Decision logic: File already exists → warn about overwrite, suggest different name · Base class not found → check for typo, suggest alternatives · Similar classes found → show existing, ask if user wants to extend · Base class `ue_read` shows features → pre-select relevant `features` for `derive_class`.

## Output Format

```text
=== Scaffold: [Feature Name] ===

--- Architecture ---
Feature Type: [Character / Ability / Weapon / UI / AI / Component]

Classes to generate:
  1. AMyHero : ACharacter
     +-- Components: MovementComponent, CapsuleComponent
     +-- Features: GAS setup, Enhanced Input
  2. UMyHeroAttributeSet : UAttributeSet
     +-- Attributes: Health, MaxHealth, Stamina
  3. UGA_MyHeroAttack : UGameplayAbility
     +-- Tags: Ability.Attack

--- Pre-Check ---
  File conflicts: None (0 existing files)
  Build.cs modules: 3 to add (GameplayAbilities, GameplayTags, GameplayTasks)

--- Generated Files ---
  [x] Source/MyGame/Characters/MyHero.h (42 lines)
  [x] Source/MyGame/Characters/MyHero.cpp (85 lines)
  [x] Source/MyGame/Attributes/MyHeroAttributeSet.h (28 lines)
  [x] Source/MyGame/Attributes/MyHeroAttributeSet.cpp (35 lines)

--- Generated Code (snippet) ---
```

Then echo a representative header snippet from the tool response
(`generated.header.content` / `skeleton.header`) so the macros are visible, e.g.:

```cpp
// Source/MyGame/Characters/MyHero.h
UCLASS()
class MYGAME_API AMyHero : public ACharacter
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Abilities")
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

    UFUNCTION(BlueprintCallable, Category="Combat")
    void ApplyDamage(float Amount);
};
```

```text
--- Post-Validation ---
  Dependency check: PASS (no missing modules after adding Build.cs deps)

--- Configuration ---
Build.cs dependencies to add:
  - "GameplayAbilities"
  - "GameplayTags"
  - "GameplayTasks"

--- Next Steps ---
  1. Add Build.cs module dependencies (shown above)
  2. Create Blueprint derived from AMyHero in Editor
     (BP/VFX/Material/UI authoring is manual since #11231 removed ECA assets;
      use blueprint-generator skill or Editor RC for graph creation)
  3. Set up Input Mapping Context in Project Settings
  4. Create GameplayEffects for damage/healing
  5. Wire up AnimBP for character animations
  6. (If GAS) Set up input action bindings with ue_manage_gameplay
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Feature type unclear | Ambiguous description | Ask user to clarify with feature type options |
| Base class not found | Invalid or engine-only class | Suggest closest available base class |
| File already exists | Target .h/.cpp exists | Warn user, offer: overwrite / rename (append suffix) / abort |
| Code gen fails | Missing output directory or permissions | Report error and suggest manual creation path. **Do NOT retry** (see No-Retry Policy) |
| dependency_check finds issues | Missing Build.cs modules | List required modules in Next Steps with exact `PublicDependencyModuleNames` line |

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** — Should activate:
1. "근접 전투 캐릭터 만들어줘" → Activate (character + GAS)
2. "Build an inventory system" → Activate (weapon/item)
3. "새 AI 적 NPC 구현해줘" → Activate (AI)
4. "Create a health bar widget" → Activate (UI)
5. "게임 모드 만들어줘" → Activate (game mode)

**Negative (3)** — Should NOT activate:
1. "ACharacter 어떻게 동작해?" → Use ue-explain skill
2. "빌드 에러 해결해줘" → Use ue-debug skill
3. "BP_Player 변경 영향 분석" → Use ue-impact skill

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Feature type detection | >90% |
| Generated code compilability | >85% |
| Architecture suggestion quality | >80% |
| Time to scaffold | <60s |
| ue_generate_code calls | ≤2 per invocation |

## MCP Tool Examples (deprecated)

> **Deprecated** — uses `ue_analyze_symbols`, which the SKILL's Step 0 forbids (`ue_grep`/`ue_read` replace `trace_hierarchy`). Retained for history only.

```python
ue_analyze_symbols(operation="smart", query="ACharacter")
```
