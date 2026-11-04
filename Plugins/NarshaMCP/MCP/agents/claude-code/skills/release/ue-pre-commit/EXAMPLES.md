# UE Pre-Commit Impact Analysis -- Examples

## Example 1: Simple Function Body Change (LOW Risk)

**User**: "커밋해도 돼?"

**Staged files** (1 .cpp):
```text
Source/MyGame/Characters/MyCharacter.cpp
```

### Phase 1: VCS Scan
```bash
$ git diff --cached --name-only
Source/MyGame/Characters/MyCharacter.cpp
```
Result: 1 C++ file (.cpp only, no headers)

### Phase 2: Function Extraction
```bash
$ git diff --cached -U0
@@ -142,3 +142,5 @@ void AMyCharacter::Tick(float DeltaTime)
+    // Added velocity logging
+    UE_LOG(LogTemp, Log, TEXT("Velocity: %s"), *GetVelocity().ToString());
```
Extracted: `AMyCharacter::Tick` — function_body change (MEDIUM)

### Phase 3: Impact Analysis
```python
ue_analyze_symbols(operation="impact_analysis", params={"target": "AMyCharacter", "depth": 2})
# Result: 2 affected BPs (BP_PlayerCharacter, BP_EnemyBase), risk_level: "low"

ue_fix_errors(operation="hotreload_check", params={"auto_detect_git": true, "staged": true})
# Result: hot_reload_safe: true (no header changes)
```

### Phase 4: Skipped (LOW risk, no HIGH targets)

### Phase 5: Report
```text
=== Pre-Commit Impact Report ===

--- Scope ---
Mode: Staged files (git diff --cached)
Changed C++ files: 1 (.h: 0, .cpp: 1)
Targets analyzed: 1 (classes: 1, functions: 0)

--- Risk Summary ---
Overall Risk: LOW
Hot Reload Safe: YES

--- Changed Files ---
  [M] Source/MyGame/Characters/MyCharacter.cpp (function_body)

--- Impact by Target ---

1. AMyCharacter (class, MEDIUM - function body only)
   Affected BPs: 2
   - BP_PlayerCharacter (hop 1)
   - BP_EnemyBase (hop 1)

--- Hot Reload Safety ---
Status: SAFE (no header changes)

--- Verdict ---
SAFE TO COMMIT: 2 BPs affected (body-only change), hot reload safe.
```

---

## Example 2: Header Signature Change (HIGH Risk)

**User**: "Pre-commit check"

**Staged files** (2 files):
```text
Source/MyGame/Combat/DamageSystem.h
Source/MyGame/Combat/DamageSystem.cpp
```

### Phase 1: VCS Scan
```bash
$ git diff --cached --name-only
Source/MyGame/Combat/DamageSystem.h
Source/MyGame/Combat/DamageSystem.cpp
```
Result: 2 C++ files (1 header + 1 source)

### Phase 2: Function Extraction
```bash
$ git diff --cached -U0
diff --git a/Source/MyGame/Combat/DamageSystem.h b/Source/MyGame/Combat/DamageSystem.h
@@ -45,1 +45,1 @@ class MYGAME_API UDamageSystem : public UActorComponent
-    UFUNCTION(BlueprintCallable)
-    float CalculateDamage(AActor* Target, float BaseDamage);
+    UFUNCTION(BlueprintCallable)
+    float CalculateDamage(AActor* Target, float BaseDamage, EDamageType DamageType);

@@ -52,0 +53,3 @@
+    UPROPERTY(EditAnywhere, BlueprintReadWrite)
+    float DamageMultiplier = 1.0f;
```
Extracted:
- `UDamageSystem::CalculateDamage` — signature_change (HIGH): added parameter
- `UDamageSystem.DamageMultiplier` — property_change (HIGH): new UPROPERTY

### Phase 3: Impact Analysis
```python
ue_analyze_symbols(operation="impact_analysis", params={"target": "UDamageSystem", "depth": 2})
# Result: 8 affected BPs, risk_level: "medium"
# affected: BP_MeleeWeapon, BP_RangedWeapon, BP_ExplosiveBarrel, BP_Turret,
#           BP_EnemyBase, BP_BossEnemy, BP_DamageZone, BP_PlayerController

ue_fix_errors(operation="hotreload_check", params={"auto_detect_git": true, "staged": true})
# Result: hot_reload_safe: false
# issues: ["DamageSystem.h: function signature changed", "DamageSystem.h: UPROPERTY added"]
```

### Phase 4: Cross-Reference Extension (HIGH risk triggers)
```python
# PRIMARY: Use blueprint_impact from Phase 3's impact_analysis response
# Phase 3 already returned blueprint_impact.direct_bp_callers[] for UDamageSystem
# Result: 5 BP callers from impact_analysis

# FALLBACK: If blueprint_impact not available, use find_cpp_to_bp directly
ue_analyze_symbols(operation="find_cpp_to_bp", params={
    "function_name": "CalculateDamage", "limit": 30
})
# Result: 5 BP callers
# - BP_MeleeWeapon -> EventGraph::CallFunction_12
# - BP_RangedWeapon -> EventGraph::CallFunction_8
# - BP_ExplosiveBarrel -> OnOverlap::CallFunction_3
# - BP_Turret -> FireWeapon::CallFunction_22
# - BP_DamageZone -> ApplyDamage::CallFunction_7
```

### Phase 5: Report
```text
=== Pre-Commit Impact Report ===

--- Scope ---
Mode: Staged files (git diff --cached)
Changed C++ files: 2 (.h: 1, .cpp: 1)
Targets analyzed: 2 (classes: 1, functions: 1)

--- Risk Summary ---
Overall Risk: HIGH
Hot Reload Safe: NO (2 issues)

--- Changed Files ---
  [H] Source/MyGame/Combat/DamageSystem.h (signature_change + property_change)
  [M] Source/MyGame/Combat/DamageSystem.cpp (function_body)

--- Impact by Target ---

1. UDamageSystem::CalculateDamage (function, HIGH - signature changed)
   Added parameter: EDamageType DamageType
   BP Callers: 5 (ALL WILL BREAK)
   - BP_MeleeWeapon -> EventGraph::CallFunction_12
   - BP_RangedWeapon -> EventGraph::CallFunction_8
   - BP_ExplosiveBarrel -> OnOverlap::CallFunction_3
   - BP_Turret -> FireWeapon::CallFunction_22
   - BP_DamageZone -> ApplyDamage::CallFunction_7

2. UDamageSystem.DamageMultiplier (property, HIGH - new UPROPERTY)
   Affected BPs: 8 (all UDamageSystem users)

--- Hot Reload Safety ---
Status: NOT SAFE (2 issues)
  1. [DamageSystem.h] Function signature changed -> BP nodes will have missing pin
  2. [DamageSystem.h] UPROPERTY added -> serialized layout changed

--- Recommendations ---
  1. [CRITICAL] All 5 BP callers of CalculateDamage will break (missing parameter pin)
  2. [CRITICAL] Full rebuild required (hot reload not safe)
  3. [HIGH] After rebuild, open and resave all 5 BP callers to fix broken nodes
  4. [MEDIUM] Consider adding default value for DamageType to preserve BP compatibility

--- Verdict ---
BLOCKED: 8 BPs affected, 5 BP nodes will break, hot reload not safe.
Recommend: Add default parameter value, full rebuild, resave affected BPs.
```

---

## Example 3: Branch Refactor (`--branch` mode)

**User**: "/ue-pre-commit --branch"

**Branch changes** (12 C++ files on `feat/weapon-refactor`):
```text
Source/MyGame/Weapons/WeaponBase.h
Source/MyGame/Weapons/WeaponBase.cpp
Source/MyGame/Weapons/MeleeWeapon.h
Source/MyGame/Weapons/MeleeWeapon.cpp
Source/MyGame/Weapons/RangedWeapon.h
Source/MyGame/Weapons/RangedWeapon.cpp
Source/MyGame/Weapons/ProjectileBase.h
Source/MyGame/Weapons/ProjectileBase.cpp
Source/MyGame/Weapons/DamageCalculator.h
Source/MyGame/Weapons/DamageCalculator.cpp
Source/MyGame/Inventory/InventoryComponent.h
Source/MyGame/Inventory/InventoryComponent.cpp
```

### Phase 2: Batching (>10 targets -> class-level only)
Deduplicated classes: `AWeaponBase`, `AMeleeWeapon`, `ARangedWeapon`, `AProjectileBase`, `UDamageCalculator`, `UInventoryComponent` (6 classes)

### Phase 3: Impact Analysis (6 MCP calls)
Run `impact_analysis` per class. Results aggregated:
- Total affected BPs: 18 (deduplicated)
- Hot reload: NOT SAFE (header changes)

### Phase 5: Report (abbreviated)
```text
=== Pre-Commit Impact Report ===

--- Scope ---
Mode: Branch changes (git diff origin/main...HEAD)
Changed C++ files: 12 (.h: 6, .cpp: 6)
Targets analyzed: 6 classes (batched from 12 files)

--- Risk Summary ---
Overall Risk: HIGH
Hot Reload Safe: NO
Total Affected BPs: 18

--- Impact by Target ---
1. AWeaponBase (6 BPs affected)
2. AMeleeWeapon (3 BPs affected)
3. ARangedWeapon (4 BPs affected)
4. AProjectileBase (2 BPs affected)
5. UDamageCalculator (5 BPs affected)
6. UInventoryComponent (3 BPs affected)

--- Verdict ---
PROCEED WITH CAUTION: Large refactor affecting 18 BPs across 6 classes.
Recommend: Full rebuild + comprehensive BP testing before merge.
```

---

## Example 4: No C++ Changes (Short-Circuit)

**User**: "Is it safe to commit?"

**Staged files**:
```text
Config/DefaultGame.ini
Content/Maps/MainMenu.umap
```

### Phase 1: VCS Scan
```bash
$ git diff --cached --name-only
Config/DefaultGame.ini
Content/Maps/MainMenu.umap
```
Result: 0 C++ files -> SHORT-CIRCUIT

### Report
```text
=== Pre-Commit Impact Report ===

--- Scope ---
Mode: Staged files (git diff --cached)
Changed files: 2 (C++: 0)

--- Verdict ---
SAFE TO COMMIT: No C++ files changed. No BP/asset impact from code changes.
Note: Config and asset changes may still have runtime effects.
```

---

## Example 5: Phase 6 HIT path (existing tests cover the change)

**User**: "커밋 전 분석해줘"

**Staged files**:
```text
Source/MyGame/Characters/MyCharacter.cpp
```

**Phase 1-2** detects:
- `changed_symbols`: `["AMyCharacter::TakeDamage"]`
- `changed_files`: `["Source/MyGame/Characters/MyCharacter.cpp"]`

**Phases 3-5** produce a normal MEDIUM-risk report (function-body change, 2 affected BPs, hot-reload safe — same shape as Example 1).

### Phase 6: Test Intent Suggestion

```python
# 6a. find_test_impact returns existing tests
ue_analyze_symbols(operation="find_test_impact", params={
    "symbols": ["AMyCharacter::TakeDamage"],
    "max_depth": 2
})
# Result:
# {
#   "success": true,
#   "affected_tests": [
#     {
#       "test_file": "Source/MyGame/Tests/MyCharacterTakeDamageSpec.cpp",
#       "test_symbol": "FMyCharacterTakeDamageSpec::Define",
#       "changed_symbol": "AMyCharacter::TakeDamage",
#       "impact_depth": 1,
#       "test_kind": "Spec"
#     },
#     {
#       "test_file": "Source/MyGame/Tests/DamageSystemTest.cpp",
#       "test_symbol": "FDamageSystemTest::RunTest",
#       "changed_symbol": "AMyCharacter::TakeDamage",
#       "impact_depth": 2,
#       "test_kind": "SimpleAutomation"
#     }
#   ],
#   "summary": {"input_symbol_count": 1, "affected_test_count": 2,
#                "total_callers_traversed": 4, "truncated": false}
# }

# 6b. SKIPPED — all changed_symbols had at least one HIT, no MISS path triggered.
# False-positive prevention (issue acceptance criterion #2): existing tests
# covering the change → no new intents derived.
```

### Phase 6 Markdown Output (appended to PR description)

```markdown
### Test Coverage (Phase 6)

🔁 **Rerun 2 existing tests** (HIT path):
- `MyGame.MyCharacter.TakeDamage_Spec` exercises `AMyCharacter::TakeDamage` (depth=1, Spec)
- `MyGame.DamageSystem.Test` exercises `AMyCharacter::TakeDamage` (depth=2, SimpleAutomation)
```

> Note: no MISS block — coverage is complete. If a reviewer wants additional
> edge-case coverage, they can manually invoke `/ue-test-gen --intents-from
> AMyCharacter::TakeDamage` to generate suggestions on top of existing tests.

---

## Example 6: Phase 6 MISS path (new code, no existing tests → intent suggestions)

**User**: "Pre-commit check"

**Staged files**:
```text
Source/MyGame/Combat/DamageSystem.h
Source/MyGame/Combat/DamageSystem.cpp
```

**Phase 1-2** detects:
- `changed_symbols`: `["UDamageSystem::ApplyArmor"]` (NEW method)
- `changed_files`: 2 files

**Phases 3-5** produce a normal CAUTION-risk report (new public method, 4 affected BPs that subclass `UDamageSystem`, hot-reload not safe due to header change).

### Phase 6: Test Intent Suggestion

```python
import time
phase6_start = time.monotonic()
PHASE6_BUDGET_S = 30.0

# 6a. find_test_impact returns empty — no existing tests touch ApplyArmor (it's new)
impact_resp = ue_analyze_symbols(operation="find_test_impact", params={
    "symbols": ["UDamageSystem::ApplyArmor"],
    "max_depth": 2
})
# Result: affected_tests: [], summary.truncated: false

hit_symbols = set()  # empty
miss_symbols = ["UDamageSystem::ApplyArmor"]  # all symbols MISS

# 6b. derive_test_intents per MISS symbol (1 call here)
if time.monotonic() - phase6_start <= PHASE6_BUDGET_S:
    intent_resp = ue_analyze_symbols(operation="derive_test_intents", params={
        "function": "UDamageSystem::ApplyArmor"
    })
# Result:
# {
#   "success": true,
#   "function": "UDamageSystem::ApplyArmor",
#   "intents": [
#     {
#       "description": "Armor reduction with NumericParam=int32 — boundary case",
#       "framework_hint": "SimpleAutomation",
#       "priority": "P1",
#       "source_pattern": {"kind": "pattern", "name": "NumericParam"},
#       "change_driven": false
#     },
#     {
#       "description": "Armor reduction non-negative invariant",
#       "framework_hint": "Spec",
#       "priority": "P1",
#       "source_pattern": {"kind": "pattern", "name": "NumericParam"},
#       "change_driven": false
#     },
#     {
#       "description": "Apply with null Target — pointer guard",
#       "framework_hint": "SimpleAutomation",
#       "priority": "P2",
#       "source_pattern": {"kind": "pattern", "name": "PointerParam"},
#       "change_driven": false
#     },
#     {
#       "description": "Apply when Armor=0 — early return path",
#       "framework_hint": "Spec",
#       "priority": "P2",
#       "source_pattern": {"kind": "branch", "name": "early_return"},
#       "change_driven": false
#     }
#     # Note: change_driven is always false in v1 — Issue #9144 will populate
#     # this flag once the change-classification layer ships. See
#     # MCP/native/mcp_server/src/tools/analysis_symbols_test_intents.rs:191-192
#     # which calls `derive_intents(analysis, None)` (no classification arg).
#   ],
#   "summary": {"intent_count": 4, "truncated": false,
#                "patterns_detected": 2, "code_paths": 1, "extraction_method": "pdb"}
# }
```

### Phase 6 Markdown Output (appended to PR description)

```markdown
### Test Coverage (Phase 6)

🆕 **4 new tests suggested** (MISS path — no existing test coverage):

- **`UDamageSystem::ApplyArmor`** (P1, SimpleAutomation): Armor reduction with NumericParam=int32 — boundary case
- **`UDamageSystem::ApplyArmor`** (P1, Spec): Armor reduction non-negative invariant
- **`UDamageSystem::ApplyArmor`** (P2, SimpleAutomation): Apply with null Target — pointer guard
- **`UDamageSystem::ApplyArmor`** (P2, Spec): Apply when Armor=0 — early return path

> Generate: `/ue-test-gen --intents-from UDamageSystem::ApplyArmor`
```

> The Markdown block above is the literal output: directly pasteable into
> a GitHub PR description (issue acceptance criterion #3). The reviewer can
> click the suggested command to actually generate the test files in a separate
> session — Phase 6 itself never writes `.cpp` files.

### Phase 6 Edge Case: budget exhausted

If `miss_symbols` had 100 entries and `derive_test_intents` calls averaged
500ms each (worst case), the 30s budget would exhaust around symbol #60. The
output would include the 60 derived intents plus:

```markdown
> ⚠️ 40 symbols not analyzed (30s budget exhausted) — rerun with `/ue-pre-commit --intents-only` for full coverage.
```

This satisfies issue acceptance criterion #5 (partial result + rerun
fallback) without sacrificing the pre-commit's fast UX on the common case.

---

## Appendix A: Activation Test Cases

### Positive (5) — should activate

1. "커밋 전에 영향 분석해줘" → Activate (Korean pre-commit)
2. "Is it safe to commit?" → Activate (English safety check)
3. "Pre-commit check" → Activate (direct trigger)
4. "What does my staged code affect?" → Activate (staged analysis)
5. "/ue-pre-commit --branch" → Activate (branch mode)

### Negative (4) — should NOT activate

1. "APawn 변경하면 뭐가 깨져?" → Use `/ue-impact` (specific entity, not VCS-driven)
2. "빌드 에러 해결해줘" → Use `/ue-debug` (error resolution)
3. "커밋 메시지 작성해줘" → Not this skill (commit message authoring)
4. "git diff 보여줘" → Not this skill (raw diff viewing)

---

## Appendix B: Quantitative Metrics

| Metric | Target |
|--------|--------|
| Changed function extraction accuracy | >85% |
| BP impact detection completeness | >90% (via `impact_analysis`) |
| Hot-reload safety check accuracy | >95% (via `ue_fix_errors`) |
| Total response time | <90s (5 targets), <180s (20 targets) |
| False positive rate (unnecessary warnings) | <10% |
