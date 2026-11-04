# Code Generation Workflow Examples

**Version**: 1.0.0 (Issue #1715 Phase 5)

---

## Example 1: Lyra Character Generation with Full Validation

**User Request**: "AShooterCharacter Lyra 스타일로 생성해줘"

### Phase 1: Preflight Validation

```text
Tool: ue_fix_errors
Mode: preflight
Target: lyra_character organism

Result:
├─ Validation Status: PASS
├─ Base Class Check:
│   ├─ AModularCharacter → Available ✅
│   └─ Header: ModularCharacter.h
├─ GAS Requirements:
│   ├─ UAbilitySystemComponent → Required ✅
│   ├─ ULyraAbilitySet → Required ✅
│   └─ IAbilitySystemInterface → Required ✅
├─ Module Detection:
│   ├─ Output: Source/MyGame/Characters/
│   └─ Module: MyGame (auto-detected)
└─ Warnings: None
```

### Phase 2: Symbol Verification

```text
Tool: ue_analyze_symbols
Operation: search_symbols
Query: AModularCharacter UAbilitySystemComponent

Result:
├─ Symbol Resolution:
│   ├─ AModularCharacter
│   │   ├─ Header: ModularGameplayActors/ModularCharacter.h
│   │   ├─ Module: ModularGameplayActors
│   │   └─ Status: Verified ✅
│   ├─ UAbilitySystemComponent
│   │   ├─ Header: AbilitySystemComponent.h
│   │   ├─ Module: GameplayAbilities
│   │   └─ Status: Verified ✅
│   └─ ULyraAbilitySet
│       ├─ Header: LyraAbilitySet.h
│       ├─ Module: LyraGame
│       └─ Status: Verified ✅
└─ All Includes: 100% Verified via PDB
```

### Phase 3: Code Generation

```text
Tool: ue_generate_code
Operation: generate_class
Organism: lyra_character
Class: AShooterCharacter
Output: Source/MyGame/Characters/

Generated Files:
├─ ShooterCharacter.h
│   ├─ Copyright header
│   ├─ #pragma once
│   ├─ Verified includes
│   ├─ UCLASS with proper specifiers
│   ├─ GAS interface implementation
│   └─ Component declarations (TObjectPtr)
├─ ShooterCharacter.cpp
│   ├─ Copyright header
│   ├─ Constructor with component setup
│   ├─ GetAbilitySystemComponent()
│   └─ OnRep functions
└─ Total: 2 files, ~150 lines
```

### Unified Result

```text
Code Generation Complete:
├─ Class: AShooterCharacter
├─ Base: AModularCharacter (Lyra)
├─ Validation: PASS (preflight + PDB)
├─ Files Generated:
│   ├─ ShooterCharacter.h (78 lines)
│   └─ ShooterCharacter.cpp (72 lines)
├─ GAS Integration:
│   ├─ AbilitySystemComponent: Yes
│   ├─ AttributeSet: Placeholder
│   └─ Interface: IAbilitySystemInterface
└─ Next Steps:
    ├─ Add to MyGame.Build.cs dependencies
    ├─ Implement custom attributes
    └─ Build project to verify
```

---

## Example 2: Basic Character (No Lyra)

**User Request**: "기본 캐릭터 클래스 생성"

### Phase 1: Preflight Validation

```text
Tool: ue_fix_errors
Mode: preflight
Target: character organism

Result:
├─ Validation Status: PASS
├─ Base Class: ACharacter
├─ Engine Includes: CoreMinimal.h, GameFramework/Character.h
└─ No GAS dependencies
```

### Phase 2: Symbol Verification (Skipped)

```text
Status: SKIPPED
Reason: Engine base class (ACharacter) - no custom verification needed
Note: Engine symbols are pre-verified in template
```

### Phase 3: Code Generation

```text
Tool: ue_generate_code
Operation: generate_class
Organism: character
Class: AMyCharacter

Generated Files:
├─ MyCharacter.h
│   ├─ Standard includes
│   ├─ UCLASS(config=Game)
│   ├─ Tick function declaration
│   └─ Input setup declarations
└─ MyCharacter.cpp
    ├─ Constructor
    ├─ BeginPlay()
    ├─ Tick()
    └─ SetupPlayerInputComponent()
```

---

## Example 3: GameplayAbility with Validation Failure

**User Request**: "GA_FireWeapon 어빌리티 생성"

### Phase 1: Preflight Validation

```text
Tool: ue_fix_errors
Mode: preflight
Target: gameplay_ability organism

Result:
├─ Validation Status: WARNING
├─ Base Class: UGameplayAbility ✅
├─ Missing Dependencies:
│   ├─ GameplayAbilities module not in Build.cs ⚠️
│   └─ Suggestion: Add "GameplayAbilities" to PublicDependencyModuleNames
└─ Can Proceed: Yes (with warnings)
```

### Phase 2: Symbol Verification

```text
Tool: ue_analyze_symbols
Operation: search_symbols
Query: UGameplayAbility FGameplayAbilitySpecHandle

Result:
├─ UGameplayAbility
│   ├─ Header: Abilities/GameplayAbility.h ✅
│   └─ Module: GameplayAbilities
├─ FGameplayAbilitySpecHandle
│   ├─ Header: GameplayAbilitySpec.h ✅
│   └─ Module: GameplayAbilities
└─ All Symbols: Found
```

### Phase 3: Code Generation

```text
Tool: ue_generate_code
Operation: generate_class
Organism: gameplay_ability
Class: UGA_FireWeapon

Generated Files:
├─ GA_FireWeapon.h
│   ├─ #include "Abilities/GameplayAbility.h"
│   ├─ UCLASS with BlueprintType
│   ├─ ActivateAbility override
│   ├─ CanActivateAbility override
│   └─ InputPressed/Released
└─ GA_FireWeapon.cpp
    └─ Implementation stubs
```

### Unified Result with Warning

```text
Code Generation Complete (with warnings):
├─ Class: UGA_FireWeapon
├─ Base: UGameplayAbility
├─ Validation: WARNING
├─ Action Required:
│   └─ Add to MyGame.Build.cs:
│       PublicDependencyModuleNames.AddRange(new string[] {
│           "GameplayAbilities",
│           "GameplayTags",
│           "GameplayTasks"
│       });
└─ Files Generated: 2
```

---

## Example 4: Module Generation

**User Request**: "GameFeatures 모듈 생성"

### Single Phase: Module Scaffolding

```text
Tool: ue_generate_code
Operation: generate_module
Module: GameFeatures
Type: Runtime

Generated Structure:
Source/GameFeatures/
├─ GameFeatures.Build.cs
│   ├─ Module dependencies
│   ├─ PCH settings
│   └─ Include paths
├─ Public/
│   └─ GameFeatures.h (module header)
├─ Private/
│   └─ GameFeaturesModule.cpp (IModuleInterface)
└─ Total: 3 files, ready to compile
```

---

## Example 5: Plugin Generation

**User Request**: "InventorySystem 플러그인 생성"

### Single Phase: Plugin Scaffolding

```text
Tool: ue_generate_code
Operation: generate_plugin
Plugin: InventorySystem
Modules: [InventoryRuntime, InventoryEditor]

Generated Structure:
Plugins/InventorySystem/
├─ InventorySystem.uplugin
│   ├─ Module definitions
│   ├─ Category: Gameplay
│   └─ EnabledByDefault: true
├─ Source/
│   ├─ InventoryRuntime/
│   │   ├─ InventoryRuntime.Build.cs
│   │   ├─ Public/
│   │   └─ Private/
│   └─ InventoryEditor/
│       ├─ InventoryEditor.Build.cs
│       ├─ Public/
│       └─ Private/
└─ Total: 1 .uplugin + 2 modules
```

---

## Example 6: Organism Auto-Selection

**User Request**: "플레이어 상태 클래스 만들어줘" (Ambiguous request)

### Intelligent Routing

```text
Step 1: Intent Detection
├─ Keywords: "플레이어 상태"
├─ Possible Organisms:
│   ├─ player_state (basic) - APlayerState
│   └─ lyra_player_state - AModularPlayerState + ASC
└─ Selected: lyra_player_state (more complete)

Step 2: User Confirmation
├─ "Lyra-style PlayerState with ASC를 생성할까요?"
├─ "- AbilitySystemComponent 포함"
├─ "- AttributeSet 지원"
└─ User: "예"

Step 3: Full Workflow
└─ [Preflight → Symbol → Generate as shown above]
```

---

## Workflow Benefits

| Metric | Manual Approach | Code Generation Workflow |
|--------|-----------------|-------------------------|
| Tool Calls | 3+ separate | 3 orchestrated |
| Validation | Manual check | Automated preflight |
| Include Accuracy | 60% (guess) | 100% (PDB) |
| Time | 15-30 min | 2-3 min |
| Error Rate | 30%+ | <5% |

---

## Common Patterns

### Pattern 1: GAS Class Generation
```text
User: "GA_* 또는 GE_* 클래스 생성"
→ Workflow auto-detects GAS organism
→ Validates GameplayAbilities module dependency
→ Generates with proper includes
```

### Pattern 2: Lyra Project Detection
```text
User: "캐릭터 생성" (in Lyra project)
→ Workflow detects LyraGame module
→ Suggests lyra_character over character
→ Includes Modular framework headers
```

### Pattern 3: Batch Generation
```text
User: "Character, Ability, Component 3개 생성"
→ Workflow queues 3 generation requests
→ Validates all in single preflight
→ Generates sequentially with shared validation
```

---

**Related**: [SKILL.md](SKILL.md) | [Issue #1715](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/1715)
