# Power Code Generation - Reference

> Technical reference for multi-class system generation with 10 scenarios.

## Scenario Registry

The authoritative scenario list lives in [SKILL.md § Built-in Scenarios](SKILL.md#built-in-scenarios).
Do not duplicate the table here — it drifted previously (Issue #8168 doc audit) and any change
to the SKILL.md table without a parallel edit here would silently desync the A/B harness.

Quick summary (full details in SKILL.md):
- **CGP-001..004**: 4-class systems (GAS Character, Weapon, AI Enemy, Multiplayer Character)
- **CGP-005..010**: 3-class systems (Health Component, Ability Set, Widget/HUD, Projectile, DataAsset Item, Enhanced Input)

A/B harness consumes the same registry via
`tests/e2e/subagent_eval/scenarios/code_gen_power_scenarios.json`.

## Phase Reference (7 Phases)

| Phase | Name | Tool Calls | Editor? |
|-------|------|-----------|---------|
| 1 | Scenario Analysis | — (AI classification) | No |
| 2 | Skeleton Generation | `ue_generate_code(derive_class)` x N | No |
| 3 | Feature Enrichment | `ue_generate_code(add_feature)` | No |
| 3.3 | Evaluator Loop (DEFAULT ON, gated) | `Agent(ue-cpp-evaluator)` x 0-3 | No |
| 3.7 | Editor Reachability Check | `ue_check_health()` | No |
| 4 | BP Creation | `ue_manage_blueprint(build_graph + import_t3d)` | Yes |
| 5 | Input Setup | `ue_manage_gameplay(setup_input)` | Yes |
| 6 | Validation | `ue_fix_errors(smart)` | No |
| 7 | Final Verification | `ue_manage_blueprint(get_structure)` | No |

## Feature Atom List

| Atom | Description | Used By |
|------|-------------|---------|
| `gas_setup` | AbilitySystemComponent + IAbilitySystemInterface | Scenarios 1, 6 |
| `gas_attributeset_base` | ATTRIBUTE_ACCESSORS, DOREPLIFETIME | Scenario 1 |
| `gas_ability_base` | ActivateAbility, CommitAbility | Scenarios 1, 6 |
| `gas_ability_cost` | Cost/Cooldown GE classes | Scenarios 1, 6 |
| `enhanced_input` | EnhancedInput setup | Scenarios 1, 4, 11 |
| `damage_system` | ApplyDamage, OnHealthChanged | Scenarios 1, 2 |
| `replication` | Replicated properties + DOREPLIFETIME | Scenario 4 |
| `weapon_fire_state` | Fire/Reload FSM, Ammo, Timer | Scenario 2 |
| `ability_projectile_spawn` | SpawnActor in ActivateAbility | Scenario 2 |
| `actor_component_base` | BeginPlay/EndPlay, TickComponent | Multiple |
| `tick` | TickComponent override | Multiple |
| `ai_controller_base` | Possess, RunBehaviorTree | Scenario 3 |

## ECA Phase 4A — REMOVED post-Epic #11230

The previous "Phase 4A: ECA Full-Stack Creation" path (`ue_eca_proxy(list_tools / lisp_to_blueprint / create_niagara_system / setup_enhanced_input)`) was removed alongside the ECABridge stack in Epic #11230 / PR #11295. The ECABridge plugin is not present in Epic's public UE 5.8 mirror, so the LISP write-back channel is no longer reachable from any engine version.

Phase 4 now always uses the T3D pipeline (`ue_manage_blueprint(build_graph + import_t3d)`). Domain-specific creation (VFX, Materials, Input) routes through `ue_manage_material`, `ue_manage_gameplay`, etc., or manual guidance. There is no ECA capability flag any more — the `eca_bridge_available` flag and the `eca_bridge` diagnostic object were removed from `ue_check_health` in Issue #15171 (the ECABridge channel itself was removed in Epic #11230 / #11231), so the previous health check is now a plain Editor reachability check via `ue_check_health()`.

For the historical design rationale, see [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md).

## Evaluator Loop Reference (Issue #8168, Phase 3.3)

### Feature Flag

| Env Var | Default (since PR #9543) | Enabled Values | Disabled Values |
|---------|--------------------------|----------------|-----------------|
| `UECODEGEN_EVALUATOR_LOOP` | **on** (unset, `"1"`, or anything other than `"0"`) | unset, `"1"`, `"true"` | `"0"` |

**Default is ON** as of PR #9543 (2026-04-23) — the 60-run A/B documented in
[`docs/development/testing/EVALUATOR_LOOP_AB_RESULT.md`](../../../docs/development/testing/EVALUATOR_LOOP_AB_RESULT.md)
passed the gate with **Cohen's d = 2.002, p-value = 0.0005, win 9/10** (very large effect).

**Opt-out** for cost-sensitive workflows (prototyping, quick CI smokes):
```bash
UECODEGEN_EVALUATOR_LOOP=0   # explicit opt-out — Phase 3.3 becomes a no-op
```
Cost when ON: ~2.4× tokens, ~2× wall time per generation. Quality benefit: +28.8 raw points (+50%) on average.

### Loop Flow

```text
Phase 2.5 (enrichment) → Phase 3 (validation) → Phase 3.3 (Evaluator)
                                                    │
                             ┌──────────── score ≥ 80 → Phase 3.7
                             │
                             └─ score < 80 AND reloops < 2 AND not stagnant
                                     → feedback → Phase 2.5 (re-enrich)
                                     → Phase 3 (re-validate)
                                     → reloops += 1
                                     → back to Phase 3.3
                             │
                             └─ reloops == 2 OR stagnation_detected
                                     → warn, proceed to Phase 3.7
```

**Hard cap**: `reloops ≤ 2`. Total Phase 2.5 executions: 1 (initial) + up to 2 (reloops) = **≤ 3**.

### Stagnation Detection

Phase 3.3 sets `stagnation_detected = true` when both:
- `reloops ≥ 1` (second or later invocation)
- `|current_score - prev_score| < 3` (less than 3 point change across reloops)

Early exit prevents wasted compute when feedback is not improving the output.

### Evaluator Prompt Contract

The ue-cpp-evaluator subagent receives (structured prompt, not JSON):
- `scenario_name` — e.g., `"GAS Character"` (maps to Scenario 1)
- `scenario_description` — plain-text scenario goal
- `files` — list of absolute paths to enriched `.h`/`.cpp` files
- `feature_list` — features applied during Phase 2 (e.g., `["gas_setup","damage_system"]`)
- `reloop_index` — `0` (first pass), `1`, or `2`

### Evaluator Response Schema (JSON)

```json
{
  "overall_score": 0-100,
  "pass": true,
  "pass_threshold": 80,
  "per_category": {
    "correctness":   {"score": 0-22, "weight": 0.22, "findings": ["..."]},
    "ue_idioms":     {"score": 0-20, "weight": 0.20, "findings": ["..."]},
    "safety":        {"score": 0-18, "weight": 0.18, "findings": ["..."]},
    "architecture":  {"score": 0-15, "weight": 0.15, "findings": ["..."]},
    "performance":   {"score": 0-15, "weight": 0.15, "findings": ["..."]},
    "documentation": {"score": 0-10, "weight": 0.10, "findings": ["..."]}
  },
  "findings": [
    {
      "severity": "blocker|critical|warning",
      "category": "correctness|ue_idioms|safety|architecture|performance|documentation",
      "file": "Source/MyGame/AMyHero.h",
      "line": 42,
      "message": "UPROPERTY omitted for replicated member bIsAlive",
      "suggestion": "Add UPROPERTY(ReplicatedUsing=OnRep_IsAlive) above the declaration."
    }
  ],
  "feedback_for_enrichment": {
    "top_3_issues": ["...", "...", "..."],
    "suggested_fixes": [
      {"file": "Source/MyGame/AMyHero.cpp", "line": 88, "fix": "Replace FString(\"Hero\") with TEXT(\"Hero\")"}
    ],
    "must_preserve": ["ASC subobject creation", "DOREPLIFETIME for Health"]
  },
  "stagnation_detected": false
}
```

Severity scoring (Evaluator-internal, for transparency):
- `blocker` → −25 (cannot ship: compile break, UB, data race, security)
- `critical` → −15 (wrong UE idiom, missing UPROPERTY/replication, memory unsafety)
- `warning` → −5 (style, docs, minor perf)

### Feedback JSON → Phase 2.5 Integration

When `overall_score < 80` and not at reloop cap, the skill re-enters Phase 2.5 and
**must** include `feedback_for_enrichment` in the enrichment prompt as a delta spec:
do not rewrite `must_preserve` items, only address `top_3_issues` via `suggested_fixes`.
This prevents the Generator from discarding correct prior work while attempting to fix
Evaluator-flagged issues.

### Exit Conditions

| Condition | Outcome | Downstream |
|-----------|---------|------------|
| `UECODEGEN_EVALUATOR_LOOP == "0"` | SKIP Phase 3.3 (explicit opt-out) | Phase 3.7 runs normally |
| `overall_score >= 80` | PASS (exit loop) | Phase 3.7 runs normally |
| `reloops == 2` AND `score < 80` | WARN (do not block) | Phase 3.7 runs; findings in final summary |
| `stagnation_detected == true` AND `score < 80` | WARN (do not block) | Phase 3.7 runs; findings in final summary |

**Key invariant**: Phase 3.3 never blocks downstream phases. A failed evaluation emits
warnings to the session summary but does not abort generation — consistent with the
issue body's "cost-aware adoption" principle.

## Self-Test

`/code-generation-power --self-test` validates:

1. **MCP connectivity**: `ue_check_health()` responds
2. **Code generation**: `ue_generate_code(derive_class)` with test params produces valid output
3. **Scenario matching**: Verify "GAS Character" maps to Scenario 1 (4 classes)
4. **Generation path**: Confirm Phase 4 routes through the NarshaMCP path (T3D pipeline / domain tools / manual guidance). ECA detection is no longer applicable — the `eca_bridge_available` flag was removed from `ue_check_health` in Issue #15171 (channel removed in Epic #11230 / #11231).

**Expected**: Steps 1-3 always pass. Step 4 confirms the T3D / NarshaMCP generation path is the only route (no ECA branch).
