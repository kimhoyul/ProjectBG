---
name: ue-impact
model: opus
description: "Purpose: Change impact analysis for UE entities. Answers 'What breaks if I change X?' by tracing callers, subclasses, Blueprint references, and config dependencies. Auto-detects target type and runs multi-tool dependency analysis. Triggers: 'impact', 'what breaks', 'change X', 'dependency', '영향 분석', '뭐가 깨져', '변경 영향', '의존성', 'who uses', 'references to', '크로스 레퍼런스', '어떤 BP가 사용', 'cross reference', 'xref', 'references to'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "TargetName [--xref] (e.g., 'AActor::BeginPlay', 'APawn --xref', 'r.Shadow.MaxResolution', 'BP_Player')"
metadata:
  version: "1.4.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Impact Analysis — Change Impact Assessment

**Issue**: #4800, #5292, #6352, #7006, #7115
**Purpose**: Answer "What breaks if I change X?" with comprehensive dependency analysis

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the 3-phase analysis automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Step 0: Discovery Pre-Exploration** (lightweight context gathering)
2. **Target**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what UE entity to analyze for change impact.
3. **Identify target type** (function, class, config, asset)
4. **Execute analysis with ONE primary MCP call** (`impact_analysis`)
5. **Generate impact report** with risk level assessment

---

## CRITICAL RULES — Call Count Minimization (Issue #7115)

> **MANDATORY**: This skill MUST complete in **3-6 MCP/tool calls** (Bash excluded).
> Optimal: 3 calls. Maximum: 6 calls. Exceeding 6 is a **skill execution error**.
>
> **MANDATORY — `ue_analyze_symbols` is REQUIRED and non-skippable** — via its `impact_analysis` op
> (code targets) or `find_cross_bp_dependencies` / `find_bp_references` (asset-only targets). It is the
> **sole authoritative source of the impact verdict**. Step 0's `ue_grep`/`ue_read` are **discovery-only**
> (confirm the target exists + pick its type) — they are **NOT** the impact answer. Producing the impact
> report from `ue_grep`/`ue_read` alone, skipping `impact_analysis`, is a **skill execution error** —
> **even when Step 0 looks sufficient, and especially when `impact_analysis` returns few/zero rows on a
> small project** (sparse ≠ skip; an empty result is still the authoritative verdict). `ue_grep` is a text
> search that misses vtable / Blueprint / reflected references; only `ue_analyze_symbols` (the PDB
> caller/dependency graph) is authoritative for impact. The valid early-exits before `ue_analyze_symbols`
> are **(a)** a **config target** (`r.*` / `gc.*` / `net.*`) → route to `ue_analyze_config` instead, and
> **(b)** Step 0 proving the target **does not exist anywhere** → report "not found".

| # | Rule | Violation Example |
|---|------|-------------------|
| 1 | **ToolSearch 1x only** — call once at session start, never repeat per phase | Calling ToolSearch before Phase 2 AND again for config |
| 2 | **impact_analysis is the ONE primary call** — covers callers + cross-BP + delegates + hierarchy | Calling find_callers after impact_analysis |
| 3 | **No redundant calls after impact_analysis** — find_callers, search_symbols, find_cross_bp_dependencies, find_delegate_refs are ALL subsets | Calling find_cross_bp_dependencies separately |
| 4 | **Grep only as complement** — only when impact_analysis result is insufficient AND you have evidence Grep will add value | Grepping target name right after impact_analysis returned results |
| 5 | **No same-file re-reads** — if you Read a file, do not Read it again | Reading LyraTeamSubsystem.cpp twice |

### Target Call Sequences

```text
Optimal (3 calls):                    Standard (5 calls):
1. ToolSearch (1x)                    1. ToolSearch (1x)
2. ue_grep (Step 0)                   2. ue_grep (Step 0)
3. impact_analysis                    3. ue_read (Step 0)
                                      4. impact_analysis
                                      5. Grep (complementary, only if needed)
```

### What impact_analysis Already Returns (do NOT re-call individually)

| impact_analysis field | Equivalent individual operation | Status |
|---|---|---|
| `callers` | `find_callers` | REDUNDANT — skip |
| `subclasses` | `trace_hierarchy` | REDUNDANT — skip |
| `blueprint_references` / `blueprint_impact.direct_bp_callers` | `find_cross_bp_dependencies` / `find_cpp_to_bp` | REDUNDANT — skip |
| `delegate_refs` | `find_delegate_refs` | REDUNDANT — skip |
| `interface_implementors` | `find_interface_implementors` | REDUNDANT — skip |
| `affected_communities` | (no equivalent) | Unique to impact_analysis with `include_communities: true` |
| `efg_fallback` | `find_nodes` | Auto-included when cross-ref returns 0 |

---

## Step 0: Discovery Pre-Exploration (Issue #6352)

> **Purpose**: Before calling heavy analysis tools (`impact_analysis`),
> use lightweight Discovery Tools to confirm target existence and gather initial context.
> Reduces wasted calls when target doesn't exist or is misidentified.

### When to Run Step 0

- **ALWAYS** run Step 0 before Phase 2 (Analysis)
- Skip Step 0 ONLY for config targets (r.*, gc.*, net.*) — go directly to `ue_analyze_config`

### Step 0 Workflow

```python
# 0a. Quick grep to discover target presence across all domains
ue_grep(query="<TargetName>", limit=15)
# Returns: matches across symbols/assets/metadata/configs/source (~50 tokens)
# Key info: which domains contain the target, approximate match count

# 0b. Read primary match for structured detail
ue_read(identifier="<TargetName>")
# Returns: class info, asset structure, or source code (~100 tokens)

# 0c. Use Step 0 results ONLY for discovery — NOT as the impact answer:
#   - Confirm target exists, then ALWAYS proceed to impact_analysis (the verdict source)
#   - Identify correct target type (class vs function vs asset)
#   - Discover related assets/files that impact_analysis might miss
#   - Pre-populate context for better Phase 3 risk assessment
#   The ONLY early-exit is "not found anywhere" -> report not-found (skip impact_analysis).
#   Otherwise impact_analysis is MANDATORY even if grep already looks sufficient.
```

### Step 0 → Phase 1 Integration

```text
Step 0 result → Phase 1 routing (impact_analysis / ue_analyze_symbols is the destination in EVERY found-case):
  Found in symbols (PDB)  → Confirmed C++ entity → impact_analysis
  Found in assets only    → Asset target → find_cross_bp_dependencies / find_bp_references (ue_analyze_symbols), NOT grep alone
  Found in source only    → grep #include/call patterns for context, then STILL run impact_analysis
  Found in multiple       → Cross-domain target → impact_analysis (full)
  Not found anywhere      → ONLY here: report "target not found" (no impact_analysis), suggest similar names from grep
```

### Step 0 Result Variables (downstream 참조용)

Phase 2에서 아래 변수명으로 Step 0 결과를 참조하여 불필요한 호출을 스킵합니다:

- `step0_domains` — 매치된 도메인 목록 (symbols, assets, metadata, configs, source)
- `step0_read_detail` — `ue_read` 구조화 응답 (child_count, caller_count, ref_count 등)
- `step0_target_exists` — 타겟 존재 여부 (false면 즉시 종료)
- `step0_target_type` — Step 0으로 확인된 타겟 타입 (class, method, asset, config)

---

## Auto-Trigger Phrases

### Korean
- "X를 바꾸면 뭐가 깨져?", "X 변경 영향 분석"
- "X 의존성 분석", "X 사용하는 곳 찾아줘"
- "X 리팩토링 전 영향 분석", "X 삭제해도 돼?"
- "X 변경하면 어디 영향?"
- "어떤 BP가 X를 사용해?", "X 크로스 레퍼런스", "X 참조 관계"

### English
- "What breaks if I change X?", "Impact analysis for X"
- "Who uses X?", "Dependencies of X"
- "Is it safe to modify X?", "References to X"
- "What depends on X?", "Refactoring impact of X"
- "Cross reference X", "xref X", "Which BPs call X?"

---

## 3-Phase Analysis Workflow

```text
Phase 1: Identify Target
   │ Detect: function, class, config, or asset
   ▼
Phase 2: Analysis (ONE impact_analysis call)
   │ Callers, subclasses, BP refs, delegates, communities
   │ Fallback: EFG title search (Issue #7068)
   ▼
Phase 3: Impact Report
   │ Risk level, recommendations
   ▼
[Complete Impact Report]
```

### Phase 1: Identify Target Type

```python
TARGET_TYPES = {
    # Contains :: -> function/method
    "::":           "function",     # AActor::BeginPlay, UObject::PostLoad

    # UE class prefixes
    "A|U|F|E|I":   "class",        # APawn, UGameplayAbility

    # Config prefixes
    "r.|gc.|net.":  "config",      # r.Shadow.MaxResolution

    # Asset prefixes
    "BP_|GA_|GE_|M_|NS_|ST_|BT_": "asset",  # BP_Player, GA_Attack
}
```

### Phase 2: Analysis (Step 0 Conditional)

> **ONE CALL**: `impact_analysis` replaces ALL of: find_callers, trace_hierarchy,
> find_cross_bp_dependencies, find_delegate_refs, find_interface_implementors.
> Do NOT call these individually after impact_analysis.

#### Step 0 Gates

| Step 0 Result | Optimization |
|---------------|-------------|
| `step0_target_exists = false` (grep 매치 0건) | **즉시 종료**: "target not found" 보고, grep 부분 매치로 대안 제안 |
| `step0_read_detail.child_count = 0` | Subclass section will be empty — noted in report |
| `step0_domains`에 symbols 없음 (assets만 있음) | PDB 기반 `impact_analysis` 스킵 → Phase 2b `find_bp_references`로 직행 |
| `step0_target_type = config` | Phase 2 전체 스킵 → `ue_analyze_config(find_overrides)`만 실행 |
| `step0_read_detail.type = method` with parent class | `impact_analysis`를 parent class 대상으로 호출 (1콜로 통합) |

#### Primary Call

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")

# ONE-SHOT impact analysis — this is the ONLY MCP call needed for Phase 2
# Step 0 Gate: step0_domains에 'symbols'가 있을 때만 호출.
# Target goes in `query` (or typed: class_name= / function_name=) — `target=` is NOT read by the primary path.
impact = ue_analyze_symbols(operation="impact_analysis", query="<TargetName>", depth=2, include_communities=true)
# Returns ALL of:
#   callers, subclasses, blueprint_references, delegate_refs,
#   interface_implementors, affected_communities, efg_fallback
#
# DO NOT call find_callers, trace_hierarchy, find_cross_bp_dependencies,
# find_delegate_refs, or search_symbols after this — they are ALL subsets.
```

Before making a zero-impact or safe-to-modify claim, classify the response with
the shared `ue_analyze_symbols` result policy. Positive callers and references
remain useful when the response is partial, but zero callers/references are
authoritative only when the response is complete and unambiguous. If the result
is degraded, truncated, incomplete, or ambiguous, report the positive evidence,
label absence as unknown, and use the applicable source/asset fallback.

#### Config Targets Only

```python
# ToolSearch already done above — do NOT call again
overrides = ue_analyze_config(operation="find_overrides", option="r.Shadow.MaxResolution")
```

#### When Individual Operations Are Justified (RARE)

> Only use these if impact_analysis genuinely cannot serve the need.
> You MUST state the justification before calling.

| Scenario | Allowed Call | Justification |
|----------|-------------|---------------|
| Asset-only target (no PDB symbol) | `find_bp_references` | Step 0 showed assets-only domain |
| Interface detail needed but impact returned 0 | `find_interface_implementors` | I-prefix target, impact returned 0 implementors |
| User explicitly asks "what methods does X have?" | `get_methods` | Distinct question, not impact analysis |
| Asset-only target needs reference graph | `ue_search_assets` | No PDB data, need asset-level deps |

### Phase 2b: EFG Title Fallback (Issue #7068)

> This fallback runs as part of Phase 2 when `impact_analysis` returns 0 blueprint callers.

When `impact_analysis` returns 0 `blueprint_impact.direct_bp_callers`
AND `find_cpp_to_bp` also returns 0 (or was not called separately):

1. **Check response for `efg_fallback`**: The MCP tool (v0.9.7+) auto-includes EFG results
   when cross-ref returns 0. Look for `blueprint_impact.efg_fallback` or top-level `efg_fallback`.

2. If `efg_fallback.results` exists, include those BPs in the impact report with lower confidence:
   - Mark as "EFG-based detection (possible Statics wrapper pattern)"
   - These BPs have confidence 0.60 (lower than cross-ref 0.80-1.0)

3. If no `efg_fallback` in response (older MCP binary), manual fallback:

```python
# CamelCase → UE Display Name. Issue #26635: the old one-liner here was
#   re.sub(r'([A-Z])', r' \1', name)
# which splits before EVERY capital, so "AIController" became "A I Controller".
# UE's FName::NameToDisplayString does NOT split an uppercase run's tail, DOES
# lowercase 15 articles at a word start, DOES turn "_" into a space, and DOES
# open a word on a digit run:
#   "FindTeamFromObject" → "Find Team from Object"   (article lowercased)
#   "AIController"       → "AIController"            (run tail not split)
#   "Abs_Int"            → "Abs Int"                 (underscore)
#   "Text3D"             → "Text 3D"                 (digit run)
# Prefer the MCP binary's own efg_fallback over this approximation; when you must
# approximate, compare case-folded so the article axis cannot cost you a hit.
# Strip class qualifier first: "UMyClass::Func" → "Func"
function_name = target.split("::")[-1] if "::" in target else target
display_name = re.sub(r'(?<=[a-z0-9])(?=[A-Z])', ' ', function_name.replace('_', ' ')).strip()

efg = ue_analyze_symbols(operation="find_nodes", title_pattern=display_name, limit=30)

# If results found, add to BP references with lower confidence
```

4. **Statics wrapper detection**: If `find_cpp_to_bp` returned 0 and `efg_fallback` found BPs,
   try searching for the Statics wrapper via PDB:
```python
statics = ue_analyze_symbols(operation="search_symbols", query=f"*Statics*::{function_name}")
# If found, retry: find_cpp_to_bp(function_name="<StaticsClass>::<Func>")
```

### Phase 2c: Networking Impact (Issue #7234)

> When Step 0 grep reveals that the target is a **Replicated property** or **RPC function**,
> add networking-specific impact data to the report.

**Detection** (from Step 0 grep results):
- grep result contains `UPROPERTY(Replicated)` or `ReplicatedUsing` → target is `replicated_property`
- grep result contains `UFUNCTION(Server,` or `Client,` or `NetMulticast` → target is `rpc_function`

**For Replicated properties** (0-1 additional MCP call):
```python
# Only call if Step 0 confirmed target is replicated — stays within 6-call budget
bandwidth = ue_analyze_symbols(operation="estimate_bandwidth", class_name="<OwnerClass>")
# Include in Phase 3 report:
# "This property adds ~X bytes/update (Y bytes/s at Z Hz)"
# If no COND_*, suggest adding one with estimated savings
```

**For RPC functions** (0 additional calls — annotation only):
- Note RPC type (Server/Client/Multicast) and reliability from Step 0 grep
- If `Reliable`: warn "Changing Reliable RPC signature may cause ordering issues"
- If `Server` + `WithValidation`: note "_Validate and _Implementation must be updated together"

**Phase 3 output addition**:
```text
--- Networking Impact ---
  Property: Health (AMyCharacter)
  Replication: UPROPERTY(Replicated), 4 bytes per update
  Bandwidth: ~120 bytes/s at 30Hz (per player)
  Condition: COND_None → Consider COND_OwnerOnly for ~75% savings
```

### Phase 3: Impact Report

Calculate risk level based on total dependency count (callers + subclasses + BP refs + delegates + interface implementors):

```python
RISK_LEVELS = {
    "LOW":      (0, 5),     # 0-5 dependents
    "MEDIUM":   (6, 15),    # 6-15 dependents
    "HIGH":     (16, 50),   # 16-50 dependents
    "CRITICAL": (51, None), # 51+ dependents
}
# Count ALL dependency types: callers + blueprint_callers + cross_bp + delegates + implementors
```

---

## ⚠️ Final Response Contract (MUST — eval-gated)

Before answering, make the impact decision auditable with all six fields:

1. **Target** — identify the symbol/asset/config and the proposed change being assessed.
2. **Dependents by reference type** — group concrete results as C++ caller, subclass/override,
   Blueprint reference, delegate, interface implementor, or config dependency.
3. **Scope** — report counts and coverage/completeness from the authoritative `impact_analysis`
   result, including partial/degraded/truncated status.
4. **Risk** — state `LOW`, `MEDIUM`, `HIGH`, or `CRITICAL` and explain the count/change-type basis.
5. **Recommendation** — give ordered compatibility, migration, and verification actions.
6. **Verdict** — say whether the change appears safe, unsafe, or requires more evidence. In
   `--xref` mode, explicitly state that risk scoring was skipped.

A complete, unambiguous empty result means `No known indexed/static dependents`; it does not prove
global safety because reflection, delegates, or runtime dispatch may escape the index. A partial or
ambiguous empty result means dependent scope and risk are `unknown`; run the documented fallback
and never convert missing coverage into a safe verdict.

---

## Output Format

```text
=== Impact Analysis: [Target] ===

--- Risk Level: [HIGH] ---
Total dependents: [N] (callers: X, subclasses: Y, BP refs: Z)

--- Direct Callers ([count]) ---
  1. UGameplayAbility::ActivateAbility (Source/GAS/GameplayAbility.cpp:234)
  2. APlayerController::BeginPlay (Source/Player/PlayerController.cpp:56)
  3. [+N more...]

--- Subclasses ([count]) ---
  APawn
  +-- ACharacter
  |   +-- AMyCharacter
  |   +-- AEnemyCharacter
  +-- AWheeledVehicle
  +-- ADefaultPawn

--- Blueprint References ([count]) ---
  1. BP_PlayerController — Uses in EventGraph (3 nodes)
  2. BP_EnemyBase — Casts to this class
  3. BP_GameMode — Spawns this class

--- Affected Communities (Issue #7006) ---
  [Only when `include_communities: true` is passed and `affected_communities` is present in response]
  Group affected BPs by community label. Show affected/total ratio per community.
  Combat (Weapons): 2 of 29 BPs affected
    - GA_Weapon_Fire
    - GA_Melee
  Actors + Pawns: 1 of 41 BPs affected
    - BP_DamageCalc

--- Config Dependencies ---
  [Only for config targets]
  - DefaultEngine.ini: r.Shadow.MaxResolution = 2048
  - ProjectSettings.ini: override = 4096

--- Recommendations ---
  1. [Priority 1]: Update 3 subclass overrides of BeginPlay
  2. [Priority 2]: Check BP_PlayerController cast compatibility
  3. [Priority 3]: Consider virtual method contract preservation
  4. Safe to modify: [YES/NO/WITH CAUTION]
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| Symbol not found | PDB not indexed | Report: "PDB index needed. Run ue_check_health()" |
| find_callers no matches | Query too short (<3 chars) | Use full method name (3+ chars); both "BeginPlay" and "AActor::BeginPlay" work |
| No callers found, complete and unambiguous indexed result | No known indexed/static caller | Report: "No known indexed/static callers" and still caveat virtual dispatch, delegates, and reflection; never claim globally safe |
| No callers found, partial/degraded/truncated/ambiguous result | Coverage is insufficient for an absence claim | Report caller status as unknown and use the source/asset fallback; never claim safe from the empty subset |
| find_cpp_to_bp returns 0 | Statics wrapper pattern (Issue #7068) | Check `efg_fallback` in response; if absent, convert CamelCase to display name and use `find_nodes(title_pattern=displayName)` |
| BP references fail | Editor not running | Skip BP phase, report partial results |
| Config not found | Invalid CVar name | Search with partial key match |

---

## Evaluation Criteria

### Activation Test Cases

**Positive (5)** - Should activate:
1. "AActor::BeginPlay 바꾸면 뭐가 깨져?" -> Activate (function impact)
2. "What depends on APawn?" -> Activate (class impact)
3. "r.Shadow.MaxResolution 변경 영향" -> Activate (config impact)
4. "Is it safe to modify BP_Player?" -> Activate (asset impact)
5. "GA_Attack 리팩토링 전 분석" -> Activate (ability impact)

**Negative (3)** - Should NOT activate:
1. "APawn 어떻게 동작해?" -> Use ue-explain skill
2. "APawn 성능 문제 디버그" -> Use ue-debug skill
3. "APawn 기반 캐릭터 만들어줘" -> Use ue-scaffold skill

### Quantitative Metrics

| Metric | Target |
|--------|--------|
| Caller detection completeness | >90% |
| Subclass chain accuracy | 100% (PDB-based) |
| Risk level accuracy | >85% |
| Response time | <60s for all phases |
| **Tool call count** | **3-6 (Bash excluded)** |

---

## v1.1.0: --xref Mode (Issue #5292)

> **Use when**: "어떤 BP가 X를 사용해?", "X xref", "cross reference X", "which BPs call X?"
> **Skips**: Phase 3 risk scoring (returns cross-reference graph only)

### --xref Auto-Detection

Activate `--xref` mode when the query contains ANY of:
- Korean: `크로스 레퍼런스`, `어떤 BP가 사용`, `참조 관계`, `xref`
- English: `cross reference`, `xref`, `who uses`, `which BPs call`, `references to`
- Or the `--xref` flag is explicitly passed

### --xref Workflow

> In --xref mode, `find_cpp_to_bp` + `impact_analysis` are both justified:
> `find_cpp_to_bp` provides detailed BP caller graph, `impact_analysis` adds community grouping.
> This is NOT redundant — each provides distinct data for cross-reference visualization.

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_symbols")

# Phase 1: Extract C++ function or class name from query
# Phase 2: C++→BP reverse lookup
callers = ue_analyze_symbols(operation="find_cpp_to_bp", function_name="ACharacter::Jump", limit=50)
# Returns: blueprint_callers list + total_callers count

# Phase 2b: Full impact with BP section (if class-level query)
impact = ue_analyze_symbols(operation="impact_analysis", query="ACharacter", depth=2, include_communities=true)
# Returns: affected_blueprints + blueprint_impact.direct_bp_callers + affected_communities

# Phase 3: Generate Mermaid cross-reference graph
```

### --xref Output Format

```mermaid
=== C++↔BP Cross-Reference: [Target] ===

--- Blueprint Callers ([count]) ---
  1. BP_Hero → EventGraph::K2Node_CallFunction_42
  2. BP_Enemy → BeginPlay::K2Node_CallFunction_17
  3. [+N more...]

--- Cross-Reference Graph (Mermaid) ---
graph LR
    Jump["ACharacter::Jump"] --> BP_Hero
    Jump --> BP_Enemy
    BP_Hero --> EventGraph
    BP_Enemy --> BeginPlay

Note: Risk scoring skipped (--xref mode). Use full /ue-impact for risk assessment.
```

---

**Status**: v1.4.0 (Phase 1 MVP + --xref mode + community grouping + call count optimization)
**Related**: Issue #4800 (MCP-First Tool Selection), Issue #5292 (C++↔BP Cross-Reference Bridge), Issue #7115 (Call Count Optimization)
## MCP Tool Examples

```python
# Blueprint reference check for impact analysis
ue_manage_blueprint(operation="smart", blueprint_name="BP_Player")
```
