---
name: code-generation
model: opus
description: "Purpose: single class generation. Orchestrates ue_generate_code + ue_fix_errors + ue_analyze_symbols with validation pipeline and interactive feature selection (21 features). For multi-class system generation (3-10 classes, weapon systems), use code-generation-power skill. Triggers: 'C++ 클래스 생성', 'generate class', 'create class', 'Lyra 캐릭터 만들어', '컴포넌트 생성'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Code Generation Workflow

**Priority**: High
**Goal**: Automate 3-tool workflow for validated code generation with optional interactive UI

---

## Purpose

Orchestrates code generation for **production-ready C++ classes** with two execution modes:

1. **Pipeline Mode** (Default) - Automated validation pipeline: Preflight → PDB → Generate
2. **Interactive Mode** - Step-by-step UI with feature selection checkboxes

**Key Innovation**: Hybrid approach from merging two complementary skills:
- **Pipeline Mode** (from code-generation v1.0.0): 3-tool orchestration with preflight validation + PDB verification
- **Interactive Mode** (from ue-class-generator v1.0.0): 21 composable features with preview and interactive selection

---

## Trigger Phrases

### Korean

- "C++ 클래스 생성", "클래스 템플릿"
- "캐릭터 생성", "컴포넌트 생성"
- "Lyra 스타일 캐릭터", "Lyra 캐릭터 만들어"
- "보일러플레이트 생성"
- "코드 생성 검증"
- "GAS 캐릭터", "클래스 만들어"

### English

- "generate class", "create class"
- "generate character", "generate component"
- "Lyra-style character"
- "boilerplate generation"
- "scaffold code"

---

## Mode Selection

### Decision Algorithm

```python
def choose_mode(user_request):
    # Priority 1: User wants interactive step-by-step
    if "대화형" in user_request or "interactive" in user_request:
        return "Interactive Mode"
    if "feature 선택" in user_request or "select features" in user_request:
        return "Interactive Mode"

    # Priority 2: User wants quick generation with validation
    if "검증" in user_request or "validate" in user_request:
        return "Pipeline Mode"
    if "preflight" in user_request or "PDB" in user_request:
        return "Pipeline Mode"

    # Priority 3: Default — Pipeline Mode for known organisms, Interactive for custom
    if matches_organism(user_request):
        return "Pipeline Mode"
    return "Interactive Mode"
```

### Comparison

| Metric | Pipeline Mode | Interactive Mode |
|--------|--------------|-----------------|
| **Speed** | Fast (automated) | Slower (user interaction) |
| **Validation** | Preflight + PDB | Preview only |
| **Flexibility** | Organisms + Features | Features only (21) |
| **Best For** | Known patterns, CI | Custom combinations, learning |

---

## Pipeline Mode (Default)

### 3-Phase Generation Process

```text
Phase 1: Preflight Validation
│ Tool: ue_fix_errors(mode="preflight")
│ Output: Potential errors, warnings, validation status
↓
Phase 2: Symbol Verification (Optional)
│ Tool: ue_analyze_symbols(operation="search_symbols")
│ Input: Base class and include dependencies
│ Output: Verified include paths from PDB
↓
Phase 3: Code Generation
│ Tool: ue_generate_code(operation="generate_class")
│ Input: Organism template + validated config
│ Output: Production-ready .h + .cpp files
```

> **Blueprint graphs (Issue #17059 §P4)**: this skill emits C++ only. If the task also needs a
> Blueprint graph, delegate to `/blueprint-generator` — generated graphs are laid out
> automatically (`build_graph` runs `auto_layout`, `LayoutStrategy` auto-detected: DAG left→right
> or AnimStateMachine 2D). No manual node placement.

### Quick Example (Pipeline Mode)

**User**: "AShooterCharacter Lyra 스타일로 생성해줘"

```text
Phase 1: Preflight Validation
├─ Tool: ue_fix_errors(mode="preflight")
├─ Target: lyra_character organism
└─ Result:
   ├─ Base class: AModularCharacter ✅
   ├─ GAS components: Required ✅
   └─ Validation: PASS

Phase 2: Symbol Verification
├─ Tool: ue_analyze_symbols(operation="search_symbols")
├─ Query: AModularCharacter, UAbilitySystemComponent
└─ Result:
   ├─ AModularCharacter → ModularCharacter.h ✅
   ├─ UAbilitySystemComponent → AbilitySystemComponent.h ✅
   └─ All includes verified

Phase 3: Code Generation
├─ Tool: ue_generate_code(operation="generate_class")
├─ Organism: lyra_character
├─ Class: AShooterCharacter
└─ Output:
   ├─ ShooterCharacter.h (generated)
   ├─ ShooterCharacter.cpp (generated)
   └─ Module: Auto-detected from output path
```

---

## Interactive Mode (from ue-class-generator)

### 5-Step Interactive Workflow

**Step 1: Base Class Selection**

```text
어떤 종류의 클래스를 생성할까요?

1. ACharacter - 캐릭터 클래스
2. AActor - 기본 액터
3. UActorComponent - 액터 컴포넌트
4. UPawnComponent - 폰 컴포넌트
5. UGameplayAbility - GAS 어빌리티
6. UAttributeSet - GAS 어트리뷰트
7. UDataAsset - 데이터 에셋
8. (직접 입력) - 다른 베이스 클래스

선택:
```

**Step 2: Feature Selection**

```text
Available Features for ACharacter:

[GAS - Gameplay Ability System]
[ ] gas_setup - ASC + IAbilitySystemInterface 통합
[ ] gas_ability_base - GameplayAbility 기본 구조
[ ] gas_ability_cost - Cost/Cooldown 시스템

[Team & AI]
[ ] team_system - IGenericTeamAgentInterface + 팀 변경 델리게이트

[Combat]
[ ] damage_system - Health/MaxHealth + TakeDamage
[ ] ranged_weapon_base - 무기 속성 (데미지, 스프레드, 발사속도)

[Input]
[ ] enhanced_input - Enhanced Input System 설정

선택할 features (쉼표로 구분): gas_setup, team_system
```

**Step 3: Class Configuration**

```text
Class Configuration:

클래스 이름: AMyCharacter
출력 경로: Source/MyGame/Characters
모듈 이름: (자동 감지: MyGame)

생성할까요? [Y/n]
```

**Step 4: Preview & Validation**

```python
ue_generate_code(
    operation="derive_class",
    base_class="ACharacter",
    class_name="AMyCharacter",
    output_dir="Source/MyGame/Characters",
    features=["gas_setup", "team_system"],
    preview=True,
)
```

**Step 5: Generation Complete**

```text
Generated Successfully!

Files Created:
  - Source/MyGame/Characters/AMyCharacter.h
  - Source/MyGame/Characters/AMyCharacter.cpp

Build.cs Updates Required:
  - PublicDependencyModuleNames: GameplayAbilities, AIModule

Next Steps:
  1. Build project (Ctrl+Shift+B)
  2. Check for compilation errors
  3. Create Blueprint subclass (optional)
```

---

## Tool Integration

### Primary Tools Used

| Tool | Operation | Purpose |
|------|-----------|---------|
| `ue_fix_errors` | `preflight` | Pre-generation validation (Pipeline Mode) |
| `ue_analyze_symbols` | `search_symbols` | PDB-based include verification (Pipeline Mode) |
| `ue_generate_code` | `generate_class` | Class generation (organism templates) |
| `ue_generate_code` | `derive_class` | Class generation (feature-based) |
| `ue_generate_code` | `suggest_class` | Get recommendations for base class |
| `ue_generate_code` | `generate_module` | Module scaffolding |
| `ue_generate_code` | `generate_plugin` | Plugin creation |

### Routing Logic

```python
GENERATION_ROUTING = {
    "class": {
        "preflight": ("ue_fix_errors", "preflight"),
        "symbols": ("ue_analyze_symbols", "search_symbols"),
        "generate": ("ue_generate_code", "generate_class"),  # Organism-based
    },
    "derive": {
        # Feature-based generation (Issue #1863)
        "suggest": ("ue_generate_code", "suggest_class"),
        "preview": ("ue_generate_code", "derive_class", {"preview": True}),
        "generate": ("ue_generate_code", "derive_class"),
    },
    "module": {
        "generate": ("ue_generate_code", "generate_module"),
    },
    "plugin": {
        "generate": ("ue_generate_code", "generate_plugin"),
    },
}
```

### Two Generation Approaches

**1. Organism-Based** (`generate_class`):
- Uses 12 pre-defined templates
- Fast for common patterns
- Example: "Lyra 캐릭터 생성" → `lyra_character` organism

**2. Feature-Based** (`derive_class`):
- Uses 21 composable features
- Flexible combinations
- Example: "GAS + Team 기능 있는 캐릭터" → `gas_setup` + `team_system` features

**When to Use Which**:
- Organism: Standard UE/Lyra patterns (Pipeline Mode recommended)
- Features: Custom combinations, non-standard base classes (Interactive Mode recommended)

---

## 12 Available Organisms

### Basic Classes

| Organism | Base Class | Use Case |
|----------|------------|----------|
| `character` | ACharacter | Basic character |
| `component` | UActorComponent | Actor component |
| `gameplay_ability` | UGameplayAbility | GAS ability |
| `attribute_set` | UAttributeSet | GAS attributes |
| `data_asset` | UPrimaryDataAsset | Data management |

### Lyra Framework Classes

| Organism | Base Class | Use Case |
|----------|------------|----------|
| `lyra_character` | AModularCharacter | Lyra-style character with GAS |
| `lyra_player_controller` | AModularPlayerController | Lyra player controller |
| `lyra_player_state` | AModularPlayerState | Lyra player state with ASC |
| `lyra_game_mode` | AModularGameModeBase | Lyra game mode |
| `lyra_game_state` | AModularGameStateBase | Lyra game state |
| `lyra_gameplay_ability` | ULyraGameplayAbility | Lyra-specific ability |
| `input_config` | ULyraInputConfig | Enhanced Input config |

---

## 21 Available Features (Issue #1863)

| Category | Features | Description |
|----------|----------|-------------|
| **GAS** | `gas_setup` | ASC + IAbilitySystemInterface 통합 |
| | `gas_ability_base` | GameplayAbility 기본 구조 (ActivateAbility, CommitAbility) |
| | `gas_ability_cost` | Cost/Cooldown 시스템 (CheckCost, ApplyCost) |
| | `gas_asc_base` | AbilitySystemComponent 기본 설정 |
| | `gas_attributeset_base` | AttributeSet 기본 구조 |
| **Team & AI** | `team_system` | IGenericTeamAgentInterface + 팀 변경 델리게이트 |
| **Combat** | `damage_system` | Health/MaxHealth + TakeDamage |
| | `ranged_weapon_base` | 무기 속성 (MaxDamageRange, SpreadAngle, TimeBetweenShots) |
| **Input** | `enhanced_input` | Enhanced Input System (SetupPlayerInputComponent) |
| **Networking** | `character_replication` | 이동 복제 (ReplicatedAcceleration, RepTimeStamp) |
| | `character_movement_state` | 이동 상태 (RepMovement, bIsCrouched) |
| **Camera** | `camera_mode` | 카메라 설정 (FOV, BlendTime, ViewPitch) |
| **Component** | `pawn_component_base` | 폰 컴포넌트 (GameFramework 인터페이스) |
| | `actor_component_base` | 액터 컴포넌트 (BeginPlay, EndPlay) |
| **Other** | `data_asset_base` | 데이터 에셋 기본 구조 |
| | `gameplay_cue_interface` | GameplayCue 인터페이스 |
| | `gameplay_tag_interface` | GameplayTag 인터페이스 |

*17 base features listed above + 4 conditional features for specific base classes (UPawnComponent, UActorComponent variants) = 21 total*

---

## Generation Types

### Class Generation
- **Target**: Single C++ class (.h + .cpp)
- **Validation**: Preflight + PDB verification (Pipeline Mode)
- **Output**: Production-ready source files

### Module Generation
- **Target**: Module structure (Build.cs + directories)
- **Types**: Runtime, Editor, Developer
- **Output**: Complete module scaffold

### Plugin Generation
- **Target**: Complete plugin (.uplugin + modules)
- **Output**: Ready-to-use plugin structure

---

## ⚠️ Common Failures

| 에러 유형 | 원인 | 해결 방법 |
|-----------|------|-----------|
| Preflight validation 실패 | 잘못된 base class 이름 또는 존재하지 않는 organism | `suggest_class`로 사용 가능한 base class 확인, organism 목록 참조 |
| PDB symbol 미발견 | PDB 인덱스 미생성 또는 엔진 경로 미설정 | `ue_check_health()`로 PDB 상태 확인, 인덱스 재빌드 |
| Include 경로 오류 | `ue_analyze_symbols` 결과와 실제 경로 불일치 | PDB 기반 결과 우선, 수동 헤더 경로 지정으로 오버라이드 |
| Feature 조합 충돌 | 호환되지 않는 feature 동시 선택 (예: 중복 ASC 설정) | `derive_class` preview 모드로 사전 검증 |
| Feature 의존성 누락 | `gas_ability_cost` 선택 시 `gas_ability_base` 미포함 | 필요한 의존 Feature 명시적 추가 (Interactive Mode에서 자동 제안) |
| 생성된 코드 컴파일 실패 | 모듈 의존성 누락 (Build.cs 미업데이트) | 생성 후 `ue_fix_errors(mode="auto")`로 빌드 에러 자동 진단 |
| `generate_module` 경로 오류 | output_dir이 프로젝트 Source 외부 | 프로젝트 Source 디렉토리 내 경로 지정 확인 |
| 모듈 이름 자동 감지 실패 | 비표준 프로젝트 구조 또는 다중 모듈 프로젝트 | `module_name` 파라미터로 명시적 모듈 이름 지정 |

---

## Related Files

For detailed information, see:

- **EXAMPLES.md** - Complete usage examples and scenarios
- **Parent Issue**: [#1715](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1715)
- **Feature Issue**: [#1863](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1863) (Lyra Recreation)
- **Code Generation Guide**: [docs/user-guides/tools/CODE_GENERATION_GUIDE.md](../../../docs/user-guides/tools/CODE_GENERATION_GUIDE.md)

---

## Output Format

```
Code Generation Report
──────────────────────
Mode: Pipeline | Interactive
Class: <class name>
Base: <base class>
Organism: <organism name> (or Features: <list>)

[Phase 1: Preflight]
  Validation: PASS | FAIL
  Base class: <verified>
  GAS components: <required/optional>

[Phase 2: Symbol Verification]
  Includes verified: <N>/<N>
  PDB status: OK | NOT INDEXED

[Phase 3: Generation]
  Files:
    - <Class>.h (generated)
    - <Class>.cpp (generated)
  Module: <auto-detected>
  Build.cs deps: <module list>

Status: SUCCESS | PARTIAL | FAILED
Next: Build project (Ctrl+Shift+B)
```

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Preflight validation failed | Invalid base class or unknown organism | Use `ue_generate_code(suggest_class)` to find available base classes |
| PDB symbol not found | PDB index not built or engine path not set | Run `ue_check_health()` to verify PDB status; rebuild index if needed |
| Feature combination conflict | Incompatible features selected (e.g., duplicate ASC setup) | Use `derive_class` with `preview: true` to validate before generation |
| Generated code compile failure | Missing module dependencies in Build.cs | Run `ue_fix_errors(mode="auto")` to auto-diagnose build errors |

**Status**: Production (v2.0.0 - Merged code-generation + ue-class-generator)
**Last Updated**: 2026-02-13
