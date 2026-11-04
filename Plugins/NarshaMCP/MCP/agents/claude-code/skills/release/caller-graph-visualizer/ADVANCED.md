# Caller Graph Visualizer - Advanced Features

Advanced usage, recursive analysis, and refactoring strategies.

---

## 🔁 Recursive Analysis Deep Dive

### How Recursive Depth Works

**Depth 1 (Direct Callers)**:

```
Target: AMyCharacter::ApplyDamage

Direct calls:
- AMyWeapon::FireWeapon → ApplyDamage
- BP_EnemyAI::Attack → ApplyDamage

Result: 2 direct callers
```

**Depth 2 (Indirect Callers)**:

```
Target: AMyCharacter::ApplyDamage

Level 1:
- AMyWeapon::FireWeapon → ApplyDamage
- BP_EnemyAI::Attack → ApplyDamage

Level 2 (who calls Level 1):
- APlayerController::HandleInput → FireWeapon
- BP_AIController::Tick → BP_EnemyAI::Attack

Result: 4 total callers (2 direct + 2 indirect)
```

**Depth 3+ (Deep Call Chains)**:

```
Target: AMyCharacter::ApplyDamage

Level 1: Direct callers (2)
Level 2: Callers of Level 1 (2)
Level 3: Callers of Level 2 (3)

Result: 7 total callers across 3 levels
```

---

### Performance vs Coverage Trade-off

**Performance Profile** (last-contact-prod, 487 C++ classes + 1,515 Blueprints):

| Depth | Avg Time | Max Time | Typical Callers Found |
|-------|----------|----------|----------------------|
| 1 | 3s | 8s | 1-5 |
| 2 | 15s | 30s | 5-15 |
| 3 | 35s | 90s | 15-50 |
| 4 | 75s | 180s | 50-200 |
| 5 | 150s | 300s | 200+ |

**Recommendation**: Use depth=2 for 90% of cases, increase only if needed.

---

## 🛠️ Refactoring Strategies

### Strategy 1: Low Impact Refactoring (<5 callers)

**Scenario**: Function has 1-5 callers, all in same module

**Approach**: Direct refactoring with auto-fix

**Steps**:

```
1. Run Caller Graph Visualizer (depth=2)
2. Verify <5 total callers
3. Rename via IDE/manual edit using the caller list as the checklist
4. Verify with ue_analyze_symbols(operation="find_callers", function_name="OldName") → expect 0 callers
5. Manually update Blueprint nodes (if any)
6. Rebuild + test
```

**Timeline**: Same day

**Risk**: Low

---

### Strategy 2: Medium Impact Refactoring (5-15 callers)

**Scenario**: Function has 5-15 callers across multiple modules

**Approach**: Deprecation with migration period

**Steps**:

```cpp
// Step 1: Add new function
UFUNCTION(BlueprintCallable, Category="Combat|Damage")
float ComputeDamage(float BaseDamage, AActor* Attacker, AActor* Victim);

// Step 2: Mark old function deprecated
UE_DEPRECATED(5.6, "Use ComputeDamage instead")
UFUNCTION(BlueprintCallable, meta=(DeprecatedFunction, DeprecationMessage="Use ComputeDamage instead"))
float CalculateDamage(float BaseDamage, AActor* Attacker, AActor* Victim) {
    return ComputeDamage(BaseDamage, Attacker, Victim);  // Forward to new function
}

// Step 3: Gradually migrate callers (sprint 1-2)
// Step 4: Remove deprecated function (sprint 3)
```

**Timeline**: 1-2 sprints

**Risk**: Medium (requires coordination)

---

### Strategy 3: High Impact Refactoring (>15 callers)

**Scenario**: Function has >15 callers across entire project

**Approach**: Major refactoring initiative with staged migration

**Steps**:

```
Sprint 1:
1. Run Caller Graph Visualizer (depth=3-4 for complete picture)
2. Document all callers (23 locations)
3. Create migration plan with priorities
4. Add deprecation warnings
5. Communicate to team

Sprint 2:
6. Migrate high-priority callers (gameplay code)
7. Update documentation

Sprint 3:
8. Migrate medium-priority callers (UI, tools)
9. Performance Health Check (verify no regressions)

Sprint 4:
10. Migrate low-priority callers (editor utilities)
11. Remove deprecated function
12. Final testing + release
```

**Timeline**: Full quarter

**Risk**: High (requires project-wide coordination)

---

## 🔗 Cross-Domain Analysis

### C++ → Blueprint Call Flow

**Challenge**: C++ function called from multiple Blueprints

**Analysis**:

```
Target: UMyComponent::Initialize

Direct callers:
- C++ (2): AMyActor::BeginPlay, UMySubsystem::Startup
- Blueprint (5): BP_GameMode, BP_PlayerController, BP_Enemy, BP_Pickup, BP_Weapon

Impact: 7 total callers (2 C++, 5 Blueprint)
```

**Refactoring Consideration**:

- C++ callers: Auto-fix with ue_analyze_symbols
- Blueprint callers: Require Blueprint node redirects or manual update

**Blueprint Redirect Strategy**:

```ini
; DefaultEngine.ini
[CoreRedirects]
FunctionRedirects=(OldName="UMyComponent.Initialize", NewName="UMyComponent.Init")
```

---

### Delegate → Bound Listeners

**Challenge**: Delegate broadcast triggers multiple listeners

**Analysis**:

```
Target: FOnStateChanged (delegate)

Broadcast locations:
- UMyStateMachine::ChangeState (StateMachine.cpp:123)
- BP_GameMode::ForceStateChange

Bound listeners:
- C++ (1): UStateObserver::OnStateChanged
- Blueprint (4): BP_UI, BP_Audio, BP_Analytics, BP_Achievements

Impact: 5 listeners (1 C++ + 4 Blueprint)
```

**Refactoring Consideration**:

- Changing delegate signature: Breaks all 5 listeners
- Safe changes: Adding optional parameters (Blueprint auto-upgrades)
- Breaking changes: Require migration guide + Blueprint redirect

---

## 📊 Accuracy & Coverage Details

### C++ Analysis Accuracy

**Method**: tree-sitter AST parsing + PDB fallback

**Coverage**:

- **70-80% accurate** via tree-sitter (AST parsing)
- **+10-15% fallback** via PDB caller hints
- **Total**: ~85% C++ coverage

**Limitations**:

- Function pointers: Not tracked (dynamic dispatch)
- Virtual functions: Tracks base function only (not overrides)
- Macro expansions: May miss macro-generated calls

---

### Blueprint Analysis Accuracy

**Method**: Kismet bytecode parser + Blueprint metadata

**Coverage**:

- **100% accurate** for Blueprint → C++ calls
- **100% accurate** for Blueprint → Blueprint calls

**No Limitations**: Complete Blueprint metadata available

---

### Delegate Analysis Accuracy

**Method**: Full delegate tracking (Issue #107)

**Coverage**:

- **100% accurate** for delegate declarations
- **100% accurate** for delegate bindings
- **100% accurate** for delegate broadcasts

**No Limitations**: Complete delegate lifecycle tracked

---

## 🔗 Integration with Other Skills

### Error Doctor Integration

**Workflow**:

```
1. Caller Graph Visualizer identifies 15 callers
2. User decides to rename function
3. Error Doctor auto-fixes C++ callers (12/15)
4. Manual update required for Blueprint callers (3/15)
```

**Example**:

```
User: "CalculateDamage를 ComputeDamage로 바꾸고 싶어"

Caller Graph: 15 callers found (12 C++, 3 Blueprint)

Error Doctor: Applying auto-fix to 12 C++ callers...
[Shows unified diff for each C++ file]

Result: 12/15 auto-fixed, 3 Blueprint nodes require manual update
```

---

### Blueprint Flow Tracer Integration

**Workflow**:

```
1. Caller Graph shows BP_EnemyAI calls TakeDamage
2. User asks: "How does BP_EnemyAI reach TakeDamage?"
3. Blueprint Flow Tracer shows complete execution path
```

**Example**:

```
User: "BP_EnemyAI가 TakeDamage 호출하는 전체 흐름 보여줘"

Caller Graph: BP_EnemyAI::Attack calls TakeDamage

Blueprint Flow Tracer: Tracing execution flow...
→ InputAction_Attack
→ BP_EnemyAI::DecideAttack
→ BP_EnemyAI::Attack
→ TakeDamage (C++ function)

[Mermaid diagram showing complete flow]
```

---

### Performance Health Check Integration

**Workflow**:

```
1. Caller Graph identifies performance-critical call path
2. Performance Health Check analyzes impact
3. Optimization suggestions provided
```

**Example**:

```
User: "TakeDamage 함수가 성능에 영향 있는지 분석해줘"

Caller Graph: TakeDamage has 23 callers

Performance Health Check: Analyzing call frequency...
- Called from Tick: 3 locations (high frequency!)
- Called from Events: 20 locations (normal)

Recommendation: Remove Tick-based damage, use event-driven approach
```

---

## 🎯 Advanced Use Cases

### Use Case 1: API Deprecation Planning

**Goal**: Deprecate old API, introduce new API

**Steps**:

1. Run Caller Graph (depth=4) for complete coverage
2. Document all callers (categorize by priority)
3. Create migration timeline
4. Add deprecation warnings
5. Provide migration guide
6. Track adoption (Performance Health Check)

---

### Use Case 2: Performance Hot Path Analysis

**Goal**: Identify performance-critical call chains

**Steps**:

1. Profile game (identify slow functions)
2. Run Caller Graph (depth=3) for each slow function
3. Identify Tick-based call chains
4. Refactor to event-driven
5. Verify performance improvement

---

### Use Case 3: Circular Dependency Detection

**Goal**: Find circular function call chains

**Steps**:

1. Run Caller Graph (depth=5) with cycle detection
2. Identify circular paths
3. Refactor to break cycles (extract interfaces)
4. Verify with Module Mapper

---

## 📚 Related Documentation

**MCP Tools**:

- [POLICY_TOOLS_OVERVIEW.md](../../../docs/reference/POLICY_TOOLS_OVERVIEW.md)
- Unified Caller Tools

**Related Skills**:

- [Error Doctor](../unreal-error-doctor/SKILL.md) - Auto-fix integration
- [Blueprint Flow](../blueprint-flow/SKILL.md) - Execution flow
- [Performance Health Check](../performance-health-check/SKILL.md) - Impact analysis

**Related Issues**:

- #151 (Caller Graph Visualizer)
- #107 (Delegate Tracking - 100% coverage)
- #11 (Source-based Call Graph - tree-sitter)

---

**Version**: 1.0.0
**Status**: ✅ Production Ready
**Date**: 2025-10-22
