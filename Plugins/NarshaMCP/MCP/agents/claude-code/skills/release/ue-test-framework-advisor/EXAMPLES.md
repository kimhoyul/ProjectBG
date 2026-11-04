# UE Test Framework Advisor — Examples

## Example 1: Pure Utility Class → Simple Test

**User**: "FDamageCalculator에 어떤 테스트가 좋을까?"

**Phase 1 Analysis**:
```
Target: FDamageCalculator
Header: Source/MyGame/Combat/DamageCalculator.h
  - Inheritance: plain struct (no UObject, no AActor)
  - No UFUNCTION, no UPROPERTY
  - Methods: CalculateDamage(), ApplyResistance(), GetCritMultiplier()
  - No world dependency, no async patterns
```

**Phase 2 Decision**:
- Rule 7 matches: Pure computation, no world/actors → **simple**

**Phase 3 Output**:
```
=== Test Framework Recommendation ===

Target: FDamageCalculator
Detected: Plain struct, 3 public methods, no world dependency

Recommended: Simple Test (simple)
  Reason: Pure synchronous computation with no UE subsystem dependencies.
    Each function can be tested in isolation with direct input/output.
  Confidence: HIGH

Alternatives:
  1. BDD Spec — if you want Describe/It grouping for readability
     (overkill for stateless utility functions)

Generation options:
  A) Quick one-shot → /ue-test-gen (recommended for simple targets)
  B) Iterative → /ue-automation-test-builder (unnecessary here)

Proceed with [A]?
```

**Phase 4 Route**: `/ue-test-gen` with `test_type=simple`, `test_target=FDamageCalculator`

---

## Example 2: GAS Character → BDD Spec

**User**: "What's the best test framework for ALyraCharacter::TakeDamage?"

**Phase 1 Analysis**:
```
Target: ALyraCharacter::TakeDamage
Header: Source/LyraGame/Character/LyraCharacter.h
  - Inheritance: ACharacter → APawn → AActor
  - UFUNCTION: TakeDamage (BlueprintCallable)
  - Subsystems: UAbilitySystemComponent, ULyraHealthComponent
  - UPROPERTY(Replicated): Health, bIsDead
Cpp: Source/LyraGame/Character/LyraCharacter.cpp
  - GAS: GameplayAbility, GameplayEffect for damage application
  - Delegates: OnHealthChanged, OnDeathStarted
  - No timers or latent actions in TakeDamage itself
Project patterns:
  - Existing tests: IMPLEMENT_SIMPLE_AUTOMATION_TEST (plugin tests)
  - Build.cs modules: GameplayAbilities, EnhancedInput
```

**Phase 2 Decision**:
- Rule 4 (async): No — TakeDamage is synchronous (delegates fire immediately)
- Rule 6 (3+ methods): Yes — TakeDamage, GetHealth, IsDead, HandleDamage, etc.
- → **bdd_spec**

**Phase 3 Output**:
```
=== Test Framework Recommendation ===

Target: ALyraCharacter::TakeDamage
Detected: ACharacter subclass, GAS-enabled, Replicated health, 5+ related methods

Recommended: BDD Spec (bdd_spec)
  Reason: Multiple related damage behaviors (take damage, reduce health,
    trigger death, apply effects) fit Describe/It organization.
    TakeDamage itself is synchronous — no latent commands needed.
  Confidence: HIGH

Alternatives:
  1. Complex+Latent — if testing damage-over-time effects (GameplayEffect with duration)
  2. Simple — if testing only TakeDamage in isolation (fewer assertions)

Generation options:
  A) Quick one-shot → /ue-test-gen (seconds, generates BDD Spec skeleton)
  B) Iterative → /ue-automation-test-builder (detects GAS patterns, adds AbilitySystem setup)

Proceed with [A/B]?
```

**Phase 4 Route**: User chooses B → `/ue-automation-test-builder` with GAS context handoff

---

## Example 3: Movement Scenario (NL) → /ue-test-scenario

**User**: "캐릭터가 10m 전진하고 점프해서 착지하는 테스트 프레임워크 추천해줘"

**Phase 1 Analysis**:
```
Target: Natural language scenario (not a class/function name)
  "캐릭터가 10m 전진하고 점프해서 착지"
  = Character moves forward 10m, jumps, and lands
```

**Phase 2 Decision**:
- Rule 1 matches: Input is natural-language scenario → Route to `/ue-test-scenario`

**Phase 3 Output**:
```
=== Test Framework Recommendation ===

Target: "캐릭터가 10m 전진하고 점프해서 착지"
Detected: Natural language gameplay scenario

Recommended: Complex+Latent via /ue-test-scenario
  Reason: This is a multi-frame gameplay scenario requiring:
    - Movement simulation (10m forward, needs tick progression)
    - Jump + landing (physics, multi-frame by nature)
    /ue-test-scenario specializes in converting natural language
    to Complex+Latent automation tests.
  Confidence: HIGH

Route: /ue-test-scenario (automatic — NL scenario detected)
```

**Phase 4 Route**: `/ue-test-scenario` with project_context (detected patterns)

---

## Example 4: Level-Based AI Test → FunctionalTest

**User**: "AI가 특정 맵에서 네비게이션 포인트를 순찰하는 테스트는 어떤 프레임워크?"

**Phase 1 Analysis**:
```
Target: AI patrol behavior in a specific map
  - Requires placed actors: AI Character, NavMesh, Patrol Points
  - Requires specific map/level geometry
  - Uses BehaviorTree + NavigationSystem
  - Multi-frame: AI movement over time
```

**Phase 2 Decision**:
- Rule 3 matches: Needs level with placed actors → **functional**

**Phase 3 Output**:
```
=== Test Framework Recommendation ===

Target: AI patrol navigation in specific map
Detected: Requires level geometry, placed actors (AI + NavPoints), BehaviorTree

Recommended: FunctionalTest (functional)
  Reason: This test requires:
    - Specific map with NavMesh baked
    - Placed patrol point actors
    - AI Character with BehaviorTree assigned
    FunctionalTest actors can be placed directly in the level editor
    and run with visual feedback in PIE.
  Confidence: HIGH

Alternatives:
  1. Complex+Latent — if you can spawn all actors programmatically
     (more portable but harder to set up NavMesh)
  2. Gauntlet — if this needs to run in cooked builds (CI/CD)

Generation options:
  A) Quick one-shot → /ue-test-gen (generates AFunctionalTest subclass)
  B) Iterative → /ue-automation-test-builder (recommended — level setup needs iteration)

Proceed with [B]?
```

**Phase 4 Route**: `/ue-automation-test-builder` with `test_type=functional`

---

## Example 5: CQTest-Detected Project

**User**: "AudioManager 테스트 프레임워크 추천"

**Phase 1 Analysis**:
```
Target: UAudioManager
Header: Source/MyGame/Audio/AudioManager.h
  - Inheritance: UObject (not Actor)
  - Methods: PlaySound(), StopAll(), SetVolume(), GetActiveCount()
Project patterns:
  - Build.cs includes "CQTest" module
  - Existing tests use TEST_CLASS pattern
```

**Phase 2 Decision**:
- Rule 5 matches: Project uses CQTest AND target fits JUnit style → **CQTest**

**Phase 3 Output**:
```
=== Test Framework Recommendation ===

Target: UAudioManager
Detected: UObject, 4 public methods, project already uses CQTest

Recommended: CQTest (TEST_CLASS/TEST_METHOD)
  Reason: Your project already includes the CQTest module and has
    existing tests using TEST_CLASS. UAudioManager fits the JUnit-style
    pattern with independent test methods per function.
  Confidence: HIGH

Note: CQTest is not supported by ue_generate_code — providing manual template:

  #include "CQTest.h"
  TEST_CLASS(AudioManagerTests, "Project.Audio.AudioManager")
  {
      TEST_METHOD(PlaySound_Should_Increase_ActiveCount) { /* ... */ }
      TEST_METHOD(StopAll_Should_Clear_ActiveSounds) { /* ... */ }
      TEST_METHOD(SetVolume_Should_Clamp_To_Range) { /* ... */ }
  };

Alternatives:
  1. BDD Spec — standard UE framework, supported by /ue-test-gen
  2. Simple — if you prefer one test per function

Generation: Manual (CQTest not in ue_generate_code). Template above is ready to use.
```
