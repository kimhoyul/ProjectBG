# Performance Health Check - Usage Examples

Complete examples and conversation scenarios for Performance Health Check.

---

## Example 1: Full Project Health Check

**User**: "프로젝트 건강도 전체 분석해줘"

**Health Check Response**:

```
🔍 Starting comprehensive project health check...

📊 Scanning project...
  - Blueprints: 1,515
  - C++ classes: 487
  - Assets: 3,201

🔵 Analyzing Blueprint Health... (3 min)
  → 15 issues found (5 high, 7 medium, 3 low)

⚙️ Analyzing C++ Architecture... (4 min)
  → 23 issues found (8 high, 12 medium, 3 low)

🚀 Analyzing Memory/Performance... (2 min)
  → 12 issues found (4 high, 6 medium, 2 low)

⏱️ Analyzing Build Time... (2 min)
  → 7 issues found (3 high, 3 medium, 1 low)

✅ Analyzing Epic Standards... (1 min)
  → 35 issues found (12 high, 18 medium, 5 low)

✅ Analysis complete! (12 minutes total)

================================================================================
Performance Health Check Report
================================================================================

Project: LastContact
Overall Health Score: 72/100 (Good)

Total Issues: 92
  - High Priority: 32 (Fix immediately)
  - Medium Priority: 43 (Address soon)
  - Low Priority: 17 (Consider for future)

Top 10 Recommendations:
1. [HIGH] Fix cyclic dependency: GameplayCore ↔ UISystem
2. [HIGH] Optimize BP_EnemyManager (8.3MB, Tick every frame)
3. [HIGH] Fix mega-function: BP_GameMode::BeginPlay (152 nodes)
4. [HIGH] Add Category to 23 UPROPERTY declarations
5. [HIGH] Fix EditAnywhere on Components (12 instances)
... (see detailed report)

Would you like:
- Detailed breakdown of any category?
- Auto-fix suggestions for specific issues?
- Export report to HTML/Markdown?
```

---

## Example 2: Blueprint-Specific Analysis

**User**: "Blueprint 복잡도만 분석해줘"

**Response**:

```
🔵 Blueprint Health Analysis

Scanning 1,515 Blueprints...

================================================================================
Mega-functions (>50 nodes)
================================================================================

1. ⚠️ BP_GameMode::BeginPlay - 152 nodes
   Impact: Hard to debug, maintain, understand
   Recommendation: Extract to helper functions
   - CreatePlayerState (20 nodes)
   - InitializeGameRules (35 nodes)
   - SetupMatchTimer (18 nodes)
   - LoadGameConfiguration (45 nodes)

2. ⚠️ BP_PlayerController::Tick - 87 nodes
   Impact: Performance hit every frame
   Recommendation: Move to event-driven or reduce frequency

3. BP_InventorySystem::UpdateUI - 73 nodes
   Recommendation: Extract UI update logic to widget

================================================================================
High Complexity (execution wire density)
================================================================================

1. BP_CombatSystem::ProcessAttack - 124 execution wires
   Recommendation: Extract damage calculation, hit detection

2. BP_AIController::EvaluateTargets - 98 execution wires
   Recommendation: Use Behavior Tree instead

================================================================================
Unused Variables/Functions
================================================================================

BP_Enemy:
  - Variables: TempDamage, OldHealth, DebugCounter (3)
  - Functions: CalculateLegacyDamage, OldAILogic (2)

BP_Weapon:
  - Functions: CalculateSpread, GetRecoilPattern, UpdateAmmoUI (3)

================================================================================
Naming Violations
================================================================================

❌ MyGameMode → BP_MyGameMode (missing BP_ prefix)
❌ player_controller → BP_PlayerController (snake_case)
❌ WEAPON → BP_Weapon (all caps)

Auto-fix available for 8/15 issues via Error Doctor Skill.

Would you like me to:
1. Show detailed mega-function breakdown?
2. Auto-fix naming violations?
3. Generate refactoring plan for complex functions?
```

---

## Example 3: Epic Standards Check

**User**: "Epic 표준 위반만 찾아줘"

**Response**:

```
✅ Epic Standards Compliance Check

Scanning C++ and Blueprint code...

================================================================================
Naming Conventions (32 violations)
================================================================================

Classes/Structs:
  ❌ EMyEnum → ELC_MyEnum (add project prefix ELC_)
  ❌ MyGameMode → ALC_GameMode (missing A prefix + project ELC_)
  ❌ player_data → FLCPlayerData (struct naming F + project)

Blueprints:
  ❌ MyPlayerController → BP_MyPlayerController (missing BP_)
  ❌ weapon_base → BP_WeaponBase (snake_case)

================================================================================
UPROPERTY Best Practices (47 violations)
================================================================================

Missing Category (23):
  - AMyCharacter::Health (no category)
  - UMyComponent::MaxSpeed (no category)
  → Recommendation: Add UPROPERTY(Category="Stats|Health")

EditAnywhere on Components (12):
  ❌ UPROPERTY(EditAnywhere) UMeshComponent* MeshComp;
  ✅ UPROPERTY(EditDefaultsOnly) UMeshComponent* MeshComp;

  Epic Standard: Components should use EditDefaultsOnly
  Reason: Prevents per-instance modification, designer confusion

BlueprintReadWrite on Private Members (5):
  ❌ private: UPROPERTY(BlueprintReadWrite) float Health;
  ✅ protected: UPROPERTY(BlueprintReadOnly) float Health;

  Reason: Private members shouldn't be Blueprint-writable

================================================================================
Component Exposure (8 violations)
================================================================================

❌ UPROPERTY(EditAnywhere) UMyMeshComponent* Mesh;
   Missing BlueprintReadOnly for designer access

✅ UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components")
   UMyMeshComponent* Mesh;

================================================================================
Category Organization (38 issues)
================================================================================

Poor Organization:
  - 38 properties in "Default" category
  - 12 properties with no category

Recommendation:
  - Use module-based categories: "Combat|Damage", "Movement|Speed"
  - Group related properties together

================================================================================
Auto-Fix Summary
================================================================================

Auto-fixable: 47/92 issues (51%)

Categories:
  - Naming: 32 auto-fixable (regex + Epic conventions)
  - UPROPERTY Category: 23 auto-fixable (add Category="...")
  - EditAnywhere → EditDefaultsOnly: 12 auto-fixable

Would you like me to:
1. Auto-fix all issues (preview mode first)?
2. Show Epic Games documentation links?
3. Generate before/after diff for specific issues?
```

---

## Example 4: C++ Architecture Analysis

**User**: "C++ 의존성 문제만 찾아줘"

**Response**:

```
⚙️ C++ Architecture Analysis

Scanning module dependencies and class structure...

================================================================================
Cyclic Dependencies (3 critical)
================================================================================

1. ⚠️ CRITICAL: GameplayCore ↔ UISystem
   Chain: GameplayCore → UISystem → GameplayCore

   Impact:
   - Circular build dependency
   - Increased build time
   - Tight coupling

   Fix Options:
   a) Extract shared interfaces to "GameplayInterfaces" module
   b) Use event system instead of direct calls
   c) Reverse dependency (UI depends on Gameplay only)

2. ⚠️ CRITICAL: NetworkingCore ↔ InventorySystem
   Chain: NetworkingCore → InventorySystem → NetworkingCore

   Fix: Extract INetworkSerializable interface

================================================================================
Include Bloat (12 issues)
================================================================================

High Priority:
  - MyPlayerController.h: 34 includes (recommend: PCH or reduce)
  - UMyLargeClass.h: 28 includes

Recommendations:
  - Move includes to .cpp (forward declarations in .h)
  - Use precompiled headers for common includes
  - Remove redundant CoreMinimal.h if Engine.h present

================================================================================
Large Classes (5 issues)
================================================================================

1. UMyLargeClass.cpp - 1,847 lines
   Recommendation: Split into:
   - UMyLargeClass_Core (base functionality)
   - UMyLargeClass_Combat (combat logic)
   - UMyLargeClass_UI (UI interaction)

2. AMyPlayerController.cpp - 1,234 lines
   Recommendation: Extract to components

================================================================================
UPROPERTY Violations (23 issues)
================================================================================

Missing Category: 23 properties
Example:
  ❌ UPROPERTY(EditAnywhere) float Health;
  ✅ UPROPERTY(EditAnywhere, Category="Stats|Health") float Health;

Would you like:
1. Detailed refactoring plan for cyclic dependencies?
2. Auto-fix for UPROPERTY violations?
3. Include bloat analysis with specific suggestions?
```

---

## Example 5: Build Time Optimization

**User**: "빌드 시간 느린 모듈 찾아줘"

**Response**:

```
⏱️ Build Time Analysis

Analyzing module compile times and dependencies...

================================================================================
Slow Modules (>30s)
================================================================================

1. GameplayCore: 47 seconds ⚠️
   Reasons:
   - 487 C++ files
   - Heavy template usage
   - Missing PCH optimization

   Recommendations:
   - Enable Unity Build for faster iteration
   - Add precompiled headers
   - Split into smaller modules

2. UISystem: 34 seconds
   Reasons:
   - 234 files with heavy Slate includes
   - No PCH for Slate headers

   Fix: Add Slate PCH (saves ~12s)

3. NetworkingCore: 28 seconds (borderline)
   Recommendation: Monitor, consider PCH

================================================================================
Missing PCH (Precompiled Headers)
================================================================================

Modules without PCH:
  - UISystem (would save ~12s)
  - InventorySystem (would save ~5s)
  - AICore (would save ~7s)

Total potential savings: ~24 seconds

Setup:
  1. Add MyModule.h with common includes
  2. Set PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs
  3. Rebuild

================================================================================
Unnecessary Includes (15 files)
================================================================================

MyPlayerController.cpp:
  - 15 unnecessary includes detected
  - Potential compile time savings: ~3s

Recommendations:
  - Remove redundant Engine.h (already in PCH)
  - Forward declare instead of include where possible

================================================================================
Estimated Total Savings
================================================================================

Current build time: 2m 15s
After optimizations: ~1m 35s (-30% improvement)

Breakdown:
  - PCH additions: -24s
  - Include cleanup: -10s
  - Unity Build: -6s (GameplayCore)

Would you like:
1. Step-by-step PCH setup guide?
2. Auto-generate include cleanup patches?
3. Unity Build configuration help?
```

---

## 🎓 Tips for Best Results

### Run Weekly

**Why**: Consistent tracking shows trends and catches regressions early.

**Best Practice**:

```
Monday morning: Run health check
Review high-priority issues
Plan fixes for the sprint
Track improvement week-over-week
```

---

### Prioritize High Issues

**Strategy**:

```
High Priority: Fix immediately (this sprint)
Medium Priority: Dedicated cleanup sprint
Low Priority: Long-term improvement goals
```

**Example Priority Matrix**:

| Issue Type | High | Medium | Low |
|------------|------|--------|-----|
| Cyclic dependencies | ✅ Fix now | - | - |
| Mega-functions | ✅ Refactor | Split gradually | - |
| Naming violations | - | ✅ Cleanup sprint | ✅ New code only |
| Missing categories | - | - | ✅ Ongoing |

---

### Combine with Other Skills

**Workflow**:

```
1. Performance Health Check → Identify 92 issues
2. Error Doctor → Auto-fix 47 issues (51%)
3. Blueprint Flow Tracer → Understand complex Blueprints
4. Module Mapper → Fix dependency issues
5. Re-run Health Check → Verify improvements
```

---

### Export Reports

**For Team Sharing**:

```
User: "리포트 HTML로 저장해줘"

Exported to: .narshamcp-mcp-mcp/health_reports/health_check_2025-10-20.html

Features:
- Interactive charts (Chart.js)
- Collapsible categories
- Color-coded priorities
- Clickable file paths
- Share with team via Confluence/SharePoint
```

---

### Focus on Trends

**Week-over-Week Comparison**:

```
Week 1: 112 issues (Score: 64/100)
Week 2: 98 issues (Score: 68/100) ↗️ +6%
Week 3: 92 issues (Score: 72/100) ↗️ +6%
Week 4: 78 issues (Score: 78/100) ↗️ +8%

Insight: Consistent 6-8% improvement → sustainable progress
Action: Maintain current cleanup velocity
```

---

## Common Scenarios

### Scenario 1: New Team Member Onboarding

**Use Case**: Show new developer project health status

**Command**: "프로젝트 건강도 분석해줘"

**Value**: New developer sees:

- What needs improvement
- Epic standards to follow
- Common mistakes to avoid

---

### Scenario 2: Pre-Release Checklist

**Use Case**: Validate project quality before release

**Command**: "Epic 표준 위반 찾아줘"

**Value**: Ensure compliance with Epic standards before shipping

---

### Scenario 3: Performance Regression Detection

**Use Case**: Something broke performance

**Command**: "Blueprint 복잡도 분석해줘"

**Value**: Find new mega-functions or Tick-heavy Blueprints introduced recently

---

**Note**: All examples use real-world data from last-contact-prod project testing.
