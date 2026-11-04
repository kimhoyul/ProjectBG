# Unreal Error Doctor - Usage Examples

Complete examples for both Path A (Read-first) and Path B (MCP) workflows.

---

## Example 0: Path A — Read-First (Compile Error, 3 Calls)

**User**: "CanCauseDamage 선언 없다고 빌드 에러"

**Step 0**: Error type = Compile error (undeclared identifier) → **Path A (Read-first)**

**Agent**:

```text
=== Error Doctor Report (Read-First) ===

Step 0: Classification
  Error Type: Compile error (C2065: undeclared identifier)
  Path: A (Read-first) — no MCP needed

Call 1: Read(Source/LyraGame/Teams/LyraTeamSubsystem.h)
  Found: Header file, 89 lines
  Missing: No CanCauseDamage declaration in public/protected section

Call 2: Read(Source/LyraGame/Teams/LyraTeamSubsystem.cpp)
  Found: bool ALyraTeamSubsystem::CanCauseDamage(const UObject* Instigator, const UObject* Target) const
  Line 128-135: Implementation exists, declaration missing from header

Call 3: Edit(Source/LyraGame/Teams/LyraTeamSubsystem.h:45)
  Added: bool CanCauseDamage(const UObject* Instigator, const UObject* Target) const;

Resolution: COMPLETE
Total Calls: 3 (target: 3-4)
```

---

## Example 0B: Path A — User-Pasted Error (2 Calls)

**User**: "이 에러 수정해줘: `LyraTeamSubsystem.cpp(128): error C2065: 'CanCauseDamage': undeclared identifier`"

**Step 0**: Error message pasted directly with file:line → skip log read → **Path A, start at Call 2**

**Agent**:

```text
=== Error Doctor Report (Read-First) ===

Step 0: User pasted error → Path A (skip log read)

Call 1: Read(Source/LyraGame/Teams/LyraTeamSubsystem.h)
  → No CanCauseDamage declaration found

Call 2: Edit(Source/LyraGame/Teams/LyraTeamSubsystem.h:45)
  → Added: bool CanCauseDamage(const UObject* Instigator, const UObject* Target) const;

Resolution: COMPLETE
Total Calls: 2 (target: 2-3)
```

---

## Example 0C: Path A — Generic "빌드 에러" (No Details, 4 Calls)

**User**: "빌드 에러 났어"

**Step 0**: No error details provided, no file path → **Path A** (compile errors are default)

**Agent**:

```text
=== Error Doctor Report (Read-First) ===

Step 0: No details → Path A default → check UBT log first

Call 1: Read(C:\Users\{whoami}\AppData\Local\UnrealBuildTool\Log.txt)
  → Log contains "Result: Succeeded" or different project errors (STALE LOG)
  → No relevant errors found

Call 2: Bash("C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles/Build.bat" LyraEditor Win64 Development -Project="C:/Lyra/Lyra571/LyraStarterGame/LyraStarterGame.uproject" 2>&1 | tail -50)
  → Captured: error C2039: 'CanCauseDamage': is not a member of 'ULyraTeamSubsystem'
  → Files: LyraWeaponStateComponent.cpp(52), LyraDamageExecution.cpp(96)
  → Header: LyraTeamSubsystem.h(62)

Call 3: Read(Source/LyraGame/Teams/LyraTeamSubsystem.h)
  → No CanCauseDamage declaration in public section

Call 4: Edit(Source/LyraGame/Teams/LyraTeamSubsystem.h:85)
  → Added: bool CanCauseDamage(const UObject* Instigator, const UObject* Target) const;

Resolution: COMPLETE
Total Calls: 4 (target: ≤4)
```

**Key**: When UBT log is stale, run build via Bash to capture errors directly. Do NOT search with Glob/find.

---

## Example 1: Path B — Complete 6-Step Cycle (Single Iteration)

**User**: "빌드 에러 났는데 도와줘" (ambiguous, but Step 0 finds build log → Path A first)

If no build log found, falls through to Path B:

**Agent**:

```text
=== Error Doctor Report [Iteration 1/1] ===

Step 0: No build log found → Path B (MCP)

Step 1: Diagnosis
  Call 1: ue_fix_errors(operation="smart")
  Error: Missing Semicolon after UCLASS declaration
  File: Source/MyGame/MyPlayerController.h:42
  Confidence: 95%
  Errors Before: 1

Step 2: Analysis
  UCLASS declarations require semicolon after closing brace.
  Epic Best Practice: GENERATED_BODY() present ✅

Step 3: Preview
  - }
  + };

Step 4: Apply
  Call 2: Edit(Source/MyGame/MyPlayerController.h:42)
  Fix Applied: YES
  Backup: MyPlayerController.h.backup

Step 5: Build
  Call 3: ue_build_pipeline(build_editor)
  Configuration: Development | Platform: Win64
  Duration: 38s
  Result: SUCCESS (exit_code=0)

Step 6: Verify
  Call 4: ue_fix_errors(scan_build_log)
  Errors: 1 → 0
  Resolution: COMPLETE — all errors resolved

Total Calls: 4 (target: 3-5)
NOTE: ue_fix_errors called exactly 1 time (smart). No ue_check_health. No ToolSearch.
```

---

## Example 2: Path B — Multi-Iteration Loop-Back

**User**: "compile error 2개 나는데 해결해줘"

**Step 0**: Multiple compile errors mentioned + no pasted file:line → try Path A build log first, then Path B.

**Agent**:

```text
=== Error Doctor Report [Iteration 1/2] ===

Call 1: ue_fix_errors(operation="smart")
  Errors Before: 2
  Error 1: .generated.h not last include — MyCharacter.h:5
  Error 2: UPROPERTY missing Category — MyCharacter.h:28

Step 4: Apply (first error)
  Call 2: Edit(Source/MyGame/MyCharacter.h:5)
  Fix: Moved .generated.h to last include position

Step 5: Build
  Call 3: ue_build_pipeline(build_editor)
  Duration: 42s
  Result: COMPLETED (1 error remaining)

Step 6: Verify
  Call 4: ue_fix_errors(scan_build_log)
  Errors: 2 → 1 (PARTIAL — 1 resolved, 1 remaining)
  → Looping back to Step 4 (reusing Step 1 diagnosis)...

=== Error Doctor Report [Iteration 2/2] ===

Step 4: Apply (second error — from initial diagnosis)
  Call 5: Edit(Source/MyGame/MyCharacter.h:28)
  - UPROPERTY(EditAnywhere)
  + UPROPERTY(EditAnywhere, Category="Gameplay")

Step 5-6: Build + Verify omitted (user can rebuild manually)

Resolution: COMPLETE
Total Calls: 5 (target: 3-5)
Errors Resolved: 2/2

NOTE: ue_fix_errors called exactly 1 time (smart, Call 1).
Both fixes derived from single diagnosis — no repeated MCP calls.
```

---

## Example 3: Path A — Build Log Read (Missing Include)

**User**: "빌드 에러 났어"

**Agent**:

```text
=== Error Doctor Report (Read-First) ===

Step 0: Generic build error → Path A (check build log first)

Call 1: Read(Saved/Logs/LyraStarterGame.log, last 100 lines)
  Found: error C1083: Cannot open include file: 'AbilitySystemComponent.h'
  File: Source/LyraGame/AbilitySystem/LyraAbilitySystemComponent.cpp(3)

Call 2: Read(Source/LyraGame/AbilitySystem/LyraAbilitySystemComponent.cpp)
  Line 3: #include "AbilitySystemComponent.h"
  → Module dependency missing: GameplayAbilities not in Build.cs

Call 3: Edit(Source/LyraGame/LyraGame.Build.cs)
  Added: "GameplayAbilities" to PublicDependencyModuleNames

Resolution: COMPLETE
Total Calls: 3 (target: 3-4)
```

---

## Example 4: Path B — Blueprint Asset Error

**User**: "BP_Player Blueprint compile failed"

**Step 0**: Blueprint error → **Path B (MCP)**

**Agent**:

```text
=== Error Doctor Report [Iteration 1/1] ===

Step 0: Blueprint error → Path B

Call 1: ue_fix_errors(operation="smart")
  → BP_Player: Pin connection type mismatch at node "Set Health"
  → Expected: Float, Got: Integer
  → Root C++ function: SetHealth(float NewHealth)

Call 2: Read(Source/MyGame/Characters/MyPlayerCharacter.h)
  → Line 45: void SetHealth(float NewHealth);  ← correct, BP binding wrong

Call 3: [Report to user — Blueprint pin reconnection needed in Editor]
  → Recommendation: Open BP_Player, delete "Set Health" node, re-add it
  → C++ side is correct, no Edit needed

Resolution: COMPLETE (user action required in Editor)
Total Calls: 2
NOTE: Did NOT call crash_analysis, runtime_analysis, or ue_check_health.
```

---

## Common Scenarios

### Scenario 1: Missing Semicolon on UCLASS

- **Error**: `expected ';' after class definition`
- **Path**: A (Read-first) if user pastes error, B if generic complaint
- **Fix Time**: <1 second (+ 30-120s build)
- **Confidence**: 95%
- **Target Calls**: 2-3

### Scenario 2: .generated.h Not Last Include

- **Error**: Various compilation errors
- **Path**: A (Read-first)
- **Fix**: Move `.generated.h` to last include
- **Confidence**: 90%
- **Target Calls**: 3

### Scenario 3: UPROPERTY Missing Category

- **Warning**: `EditAnywhere property without Category`
- **Path**: A (Read-first)
- **Fix**: Add `Category="Gameplay"`
- **Confidence**: 85%
- **Target Calls**: 2-3

### Scenario 4: Missing Module Dependency (Linker Error)

- **Error**: `unresolved external symbol` or `cannot open include file`
- **Path**: A (Read build log → Read Build.cs → Edit Build.cs)
- **Fix**: Add module to `PublicDependencyModuleNames`
- **Target Calls**: 3
