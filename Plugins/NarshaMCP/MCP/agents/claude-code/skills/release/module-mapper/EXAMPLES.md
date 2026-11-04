# Module Mapper - Usage Examples

Complete examples and common module mappings for Module Mapper.

---

## Example 1: FAssetData Linker Error

**User**: "FAssetData 클래스 찾을 수 없다는 링커 에러가 나. 어떤 모듈 추가해야 돼?"

**Module Mapper**:

```
🔍 Step 1: Module Query
FAssetData → AssetRegistry 모듈

📦 Step 2: Module Details
AssetRegistry (Runtime/AssetRegistry)
- Dependencies: CoreUObject, Core
- No circular dependencies

✏️ Step 3: .Build.cs Modification

```diff
--- a/Source/MyModule/MyModule.Build.cs
+++ b/Source/MyModule/MyModule.Build.cs
@@ -12,6 +12,7 @@
         "Core",
         "CoreUObject",
         "Engine",
+        "AssetRegistry",
     });
```

PublicDependencyModuleNames에 "AssetRegistry" 추가하시면 됩니다.
수정해드릴까요?

```

---

## Example 2: UGameplayAbility Module

**User**: "UGameplayAbility 클래스 사용하려면 어떤 모듈이 필요해?"

**Module Mapper**:
```

🔍 Step 1: Module Query
UGameplayAbility → GameplayAbilities 모듈

📦 Step 2: Module Details
GameplayAbilities (Runtime/GameplayAbilities)

- Dependencies: GameplayTags, GameplayTasks, Engine, CoreUObject, Core
- Transitive: +14 more modules

✏️ Step 3: .Build.cs Modification

PublicDependencyModuleNames에 추가 필요:

1. "GameplayAbilities" (primary)
2. "GameplayTags" (required dependency)
3. "GameplayTasks" (required dependency)

```diff
--- a/Source/MyModule/MyModule.Build.cs
+++ b/Source/MyModule/MyModule.Build.cs
@@ -12,6 +12,9 @@
         "Core",
         "CoreUObject",
         "Engine",
+        "GameplayAbilities",
+        "GameplayTags",
+        "GameplayTasks",
     });
```

GAS (Gameplay Ability System) 사용을 위한 표준 설정입니다.
수정해드릴까요?

```

---

## Example 3: Multiple Classes from Same Module

**User**: "FAssetData, UAssetRegistryHelpers, FARFilter 이 3개 다 못 찾는다는데?"

**Module Mapper**:
```

🔍 Step 1: Module Query

All 3 classes are in: AssetRegistry 모듈

- FAssetData
- UAssetRegistryHelpers
- FARFilter

📦 Step 2: Module Details
AssetRegistry (Runtime/AssetRegistry)
[Same as Example 1]

✏️ Step 3: .Build.cs Modification

AssetRegistry 모듈 하나만 추가하면 3개 클래스 모두 해결됩니다.

[Same diff as Example 1]

수정해드릴까요?

```

---

## Example 4: Enhanced Input Module

**User**: "UEnhancedInputComponent를 사용하려고 하는데 링커 에러가 나요"

**Module Mapper**:
```

🔍 Step 1: Module Query
UEnhancedInputComponent → EnhancedInput 모듈

📦 Step 2: Module Details
EnhancedInput (Runtime/EnhancedInput)

- Dependencies: InputCore, CoreUObject, Core
- Type: Runtime module

✏️ Step 3: .Build.cs Modification

```diff
--- a/Source/MyModule/MyModule.Build.cs
+++ b/Source/MyModule/MyModule.Build.cs
@@ -12,6 +12,7 @@
         "Core",
         "CoreUObject",
         "Engine",
+        "EnhancedInput",
     });
```

Enhanced Input System 사용을 위한 설정입니다.
수정해드릴까요?

```

---

## Example 5: UMG Widget Module

**User**: "UWidgetComponent 클래스가 정의되지 않았다고 나와요"

**Module Mapper**:
```

🔍 Step 1: Module Query
UWidgetComponent → UMG 모듈

📦 Step 2: Module Details
UMG (Runtime/UMG)

- Dependencies: Slate, SlateCore, Engine, CoreUObject, Core
- Type: Runtime module

⚠️ Note: UMG is a UI module, ensure your project uses widgets

✏️ Step 3: .Build.cs Modification

```diff
--- a/Source/MyModule/MyModule.Build.cs
+++ b/Source/MyModule/MyModule.Build.cs
@@ -12,6 +12,7 @@
         "Core",
         "CoreUObject",
         "Engine",
+        "UMG",
     });
```

UMG (Unreal Motion Graphics) 사용을 위한 설정입니다.
수정해드릴까요?

```

---

## 📋 Common Module Mappings

**Frequently Asked Modules**:

| Class/Struct | Module | Header |
|--------------|--------|--------|
| `FAssetData` | AssetRegistry | AssetRegistry/Public/AssetData.h |
| `UGameplayAbility` | GameplayAbilities | Abilities/GameplayAbility.h |
| `FGameplayTag` | GameplayTags | GameplayTagContainer.h |
| `UEnhancedInputComponent` | EnhancedInput | EnhancedInputComponent.h |
| `UWidgetComponent` | UMG | Components/WidgetComponent.h |
| `UAnimMontage` | Engine | Animation/AnimMontage.h |
| `APlayerController` | Engine | GameFramework/PlayerController.h |
| `UDataTable` | Engine | Engine/DataTable.h |
| `UStateTreeComponent` | StateTreeModule | StateTreeComponent.h |
| `UBehaviorTreeComponent` | AIModule | BehaviorTree/BehaviorTreeComponent.h |

---

## 🎓 Common Dependency Chains

**GAS (Gameplay Ability System)**:
```

GameplayAbilities
├── GameplayTags (required)
├── GameplayTasks (required)
├── Engine
├── CoreUObject
└── Core

```

**Enhanced Input**:
```

EnhancedInput
├── InputCore (required)
├── CoreUObject
└── Core

```

**UMG (Unreal Motion Graphics)**:
```

UMG
├── Slate (required)
├── SlateCore (required)
├── Engine
├── CoreUObject
└── Core

```

**StateTree (AI)**:
```

StateTreeModule
├── GameplayTags (required)
├── AIModule
├── Engine
├── CoreUObject
└── Core

```

**BehaviorTree (AI)**:
```

AIModule
├── GameplayTags (optional but recommended)
├── Engine
├── CoreUObject
└── Core

```

---

## 🎓 Tips for Best Results

### Be Specific with Class Names

✅ **Good**:
- "FAssetData 모듈 알려줘" (specific class)
- "UGameplayAbility는 어느 모듈이야?" (specific class)
- "FGameplayTag 클래스 모듈" (specific class)

❌ **Too Vague**:
- "Asset 관련 모듈" (which asset class?)
- "Ability 모듈" (Gameplay Ability? which class?)
- "UI 모듈" (UMG? Slate? which UI class?)

---

### Ask for Complete Dependency Chain

```

User: "GameplayAbilities 모듈 추가할 때 다른 모듈도 필요해?"

Module Mapper:
Yes! GameplayAbilities requires:

1. GameplayTags (direct dependency)
2. GameplayTasks (direct dependency)

Without these, you'll get additional linker errors.

```

---

### Request Diff Preview

```

User: ".Build.cs 수정 diff 보여줘"

Module Mapper:
[Shows unified diff format with exact line numbers and changes]

```

---

### Combine with Other Skills

```

User: "링커 에러 고치고, .Build.cs 수정해줘"
→ Error Doctor (parse error) + Module Mapper (suggest module) + Apply fix

User: "모듈 추가 후 성능 체크해줘"
→ Module Mapper (add module) + Performance Health Check (validate no bloat)

```

---

## Common Scenarios

### Scenario 1: First Time Using GAS

**Problem**: "Gameplay Ability System을 처음 사용하는데 어떤 모듈들이 필요한가요?"

**Module Mapper Approach**:
```

🔍 GAS Standard Setup:

Required Modules:

1. GameplayAbilities (core)
2. GameplayTags (required)
3. GameplayTasks (required)

Optional but Recommended:
4. GameplayDebugger (Editor debugging)

[Shows diff with all 3 required modules]

```

---

### Scenario 2: Multiple Linker Errors

**Problem**: "링커 에러가 20개나 나는데 다 같은 모듈 때문인가요?"

**Module Mapper Approach**:
```

🔍 Analyzing all 20 errors...

All errors are from: AssetRegistry 모듈

Missing classes:

- FAssetData (7 errors)
- FARFilter (5 errors)
- UAssetRegistryHelpers (8 errors)

Solution: Add AssetRegistry module once → Fixes all 20 errors

[Shows single module addition]

```

---

### Scenario 3: Circular Dependency Warning

**Problem**: "UnrealEd 모듈을 추가하려고 하는데 순환 의존성 경고가 나와요"

**Module Mapper Approach**:
```

⚠️ WARNING: Circular Dependency Risk

UnrealEd → Engine → MyModule (your module)

Adding UnrealEd to MyModule would create:
MyModule → UnrealEd → Engine → MyModule (circular!)

Recommendation:

- UnrealEd is an Editor-only module
- Runtime modules should NOT depend on Editor modules
- Refactor: Move editor code to separate Editor module

[Shows refactoring guidance]

```

---

**Note**: All examples use real-world UBT Manifest data from last-contact-prod project.
