---
name: ue-create-system
description: "Purpose: Cross-domain system generation (C++ + BP + VFX + Material + UI). NarshaMCP analysis -> C++ generation -> NarshaMCP asset creation (T3D/Editor RC) -> build validation. Triggers: '시스템 만들어', 'create system with', '전체 시스템', 'VFX 포함'."
compatibility: "NarshaMCP v0.9.7+ with Unreal Engine 5.x project."
argument-hint: "system description (e.g., 'damage system', 'inventory with UI', 'projectile system with VFX') | --self-test"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "Proprietary"
  issue: "#7408"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Create System — Cross-Domain System Generation

**Issue**: #7408
**Purpose**: "데미지 시스템 만들어줘" 한마디로 C++ + Blueprint + VFX + Material + UI를 오케스트레이션

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the 5-phase workflow automatically**.
> Do NOT just display documentation — actually run the MCP tools!

### Execution Requirements

1. **System request**: `$ARGUMENTS`
   > If `$ARGUMENTS` is empty, ask the user what system to create.
2. **Classify** the system type (Phase 0 — auto-detection)
3. **Analyze** existing patterns and plan architecture (Phase 1)
4. **Generate C++ classes** using `ue_generate_code(derive_class)` (Phase 2)
5. **Create assets** via NarshaMCP BP tools + manual guidance (Phase 3)
6. **Validate** build with `ue_fix_errors` (Phase 4)
7. **Present** unified report: generated files, asset status, next steps

---

## Auto-Trigger Phrases

### Korean
- "X 시스템 만들어줘", "전체 시스템 생성"
- "데미지 시스템 만들어", "무기 시스템 VFX 포함"
- "인벤토리 시스템 UI 포함해서 만들어"
- "완전한 시스템", "크로스 도메인 시스템"
- "이펙트 포함 시스템", "머티리얼 포함"

### English
- "Create X system with VFX", "Full system generation"
- "Build damage system", "Create system with effects"
- "Complete system with UI", "Cross-domain system"
- "System with material and VFX"
- "Create projectile system with effects"

### NOT Triggered By
- "시스템 설명해줘" → `/ue-explain`
- "시스템 분석해줘" → `/ue-explain`
- "캐릭터 만들어줘" → `/ue-scaffold` (no "시스템" qualifier)
- "파워 코드 생성" → `/code-generation-power`

---

## Skill Positioning

| Skill | Scope | Domains |
|-------|-------|---------|
| `/ue-scaffold` | Single feature (1-2 classes) | C++ only |
| `/code-generation-power` | Multi-class scenario (3-10) | C++ + BP (NarshaMCP) |
| **`/ue-create-system`** | Complete game system (3-6 classes) | **C++ + BP + VFX + Material + UI** |

**Key difference**: This skill creates **visual assets** (Blueprint graphs, Niagara VFX, Materials, UMG widgets) via NarshaMCP BP tools + manual application guidance, not just C++ code.

---

## MCP Call Budget

**Target: 8-12 calls per invocation** (varies by system scope).

| Phase | Tool | Max Calls | Notes |
|-------|------|-----------|-------|
| Phase 0 | `ue_grep` / `ue_read` | 2 | Lightweight discovery |
| Phase 1 | `ue_analyze_symbols` | 1 | Pattern analysis |
| Phase 1 | `ue_manage_gameplay` | 0-1 | Only for GAS systems |
| Phase 1 | `ue_generate_code(suggest_class)` | 1 | Dry-run architecture |
| Phase 2 | `ue_generate_code(derive_class)` | 3-6 | Sequential, 1 per class |
| Phase 3 | `ue_manage_blueprint` | 1-2 | BP node creation (Editor) |
| Phase 4 | `ue_fix_errors` | 1-2 | Validation + 1 auto-fix |

### No-Retry Policy

> If `ue_generate_code(derive_class)` fails on the FIRST call:
> - **DO NOT** retry with slightly different parameters
> - Instead: **Report the error clearly**, ask user for clarification
>
> **Rationale**: Retrying wastes tokens and usually produces the same failure.

---

## System Type Auto-Detection

Classify the user's request into a system type to determine which domains are needed:

| Type | Keywords (ko/en) | Domains |
|------|-------------------|---------|
| Damage | 데미지, damage, HP, health | C++ + BP + VFX |
| Weapon | 무기, weapon, fire, shoot | C++ + BP + VFX + Material |
| Inventory | 인벤토리, inventory, item | C++ + BP + UI |
| AI Enemy | AI, enemy, 적, NPC | C++ + BP |
| HUD/UI | HUD, UI, widget, 위젯 | C++ + BP + UI |
| Projectile | 투사체, projectile, bullet | C++ + BP + VFX + Material |
| Custom | (user-specified) | (ask user) |

If the system type is unclear, ask the user:
> "Which visual domains should this system include?"
> - [ ] Blueprint graph
> - [ ] VFX (Niagara)
> - [ ] Material
> - [ ] UI (UMG Widget)

---

## 5-Phase Workflow

### Phase 0: Discovery (2 MCP calls max)

```python
# Step 0a: Find existing similar systems
ue_grep(query="<system_keyword>", domain="source", limit=10)

# Step 0b: Read the most relevant existing class
ue_read(identifier="<most_relevant_existing_class>")

# Decision:
#   Similar system exists? → Warn user, suggest extending instead of creating new
#   No similar system? → Proceed to Phase 1
```

### Phase 1: Analysis (2-3 MCP calls)

```python
# Step 1a: Existing pattern analysis
ue_analyze_symbols(operation="smart", query="<base_class>")

# Step 1b (GAS systems only):
ue_manage_gameplay(operation="smart", tag="<relevant_tag>")

# Step 1c: Architecture dry-run
ue_generate_code(
    operation="suggest_class",
    base_class="<detected_base>",
    class_name="<SystemName>",
    features=["<auto_detected_features>"],
)
```

**Present Plan to User**:
```text
=== System Architecture Plan ===

System Type: Damage System
Engine: UE 5.x

C++ Classes (Phase 2):
  1. UCUDamageComponent : UActorComponent [gas_setup, replication]
  2. UCUDamageType : UDamageType
  3. UGA_ApplyDamage : UGameplayAbility [gas_setup]

Visual Assets (Phase 3 — manual creation guidance):
  4. BP_DamageComponent (Blueprint graph)
  5. NS_DamageImpact (Niagara VFX)

Proceed? [Y/n]
```

Wait for user confirmation before Phase 2.

### Phase 2: C++ Generation (3-6 MCP calls, sequential)

```python
# For each class in dependency order (independent classes first):
ue_generate_code(
    operation="derive_class",
    base_class="<base>",
    class_name="<name>",
    output_dir="Source/<Module>/<Subdirectory>",
    features=["<selected_features>"],
)
# → .h + .cpp file pair with production features
# IMPORTANT: Generate in dependency order (AttributeSet → Ability → Component → Character)
# Sequential calls only (no parallel ue_generate_code). Max 6 classes.
```

### Phase 3: Asset Creation (delegate to /blueprint-generator + manual guidance)

```text
Step 3-1 (BP): Delegate Blueprint graph creation to /blueprint-generator
  → That skill runs the T3D pipeline (build_graph_guide → build_graph
    → import_t3d → compile) and creates BP_<SystemName> directly.
  → For trivial BPs you can also use ue_manage_blueprint(operation="smart").
  → Node layout is AUTOMATIC (build_graph auto_layout; LayoutStrategy auto-
    detected — DAG left→right, AnimStateMachine 2D). After any add_node edits,
    finish with one ue_manage_blueprint(operation="auto_layout"). Issue #17059 §P4.

Step 3-2: Output manual guidance for non-BP visual assets:

  === Manual Asset Creation Guide ===

  VFX: Content Browser → Right-click → Niagara → Niagara System
    Name: NS_<SystemName>
    Template: <suggested_template>
    Connect to: <C++ class>::<VFX_function>

  Material: Content Browser → Right-click → Material
    Name: M_<SystemName>
    Key nodes: <suggested_nodes>

  UI: Content Browser → Right-click → Widget Blueprint
    Name: WBP_<SystemName>
    Root: Canvas Panel → <suggested_layout>
```

### Phase 4: Validation (1-2 MCP calls)

```python
# Step 4a: Build verification
ue_fix_errors(operation="smart", project_root="<project_root>")

# If errors found:
# Step 4b: Attempt 1 auto-fix cycle
ue_fix_errors(operation="auto", errors="<detected_errors>")

# If errors persist after auto-fix → Stop and report. Do NOT retry further.
```

---

### Phase 4.5: Live QA Verification (optional, Issue #9579)

> ## ✅ Live-verified 2026-04-24
>
> Evaluator end-to-end smoke test passed on LCP_D_571. Phase 4.5
> dispatches ns-ue-qa-evaluator successfully when its gates pass.
> See agent file top banner for the full verification table.
>
> **Auto-invoked for gameplay system types only** when the Editor is live.
> Opt-out: `UE_CREATE_SYSTEM_SKIP_LIVE_QA=1`.

**Gate conditions** (ALL must be true to run this phase):
1. Phase 0 classified `system_type` contains a gameplay domain — any of:
   `damage`, `ability`, `combat`, `movement`, `interaction`, `projectile`,
   `weapon`, `inventory_logic`, `health`, `stamina` (match by substring).
   Pure `ui` / `vfx` / `material` systems skip this phase.
2. `UE_CREATE_SYSTEM_SKIP_LIVE_QA` is NOT set to `1`.
3. `ue_check_health(compact=true)` returns `editor_hint: online` (compact mode
   lacks the full `editor` object — Issue #9241; use the hint field here).

> **Gate-signal divergence with `/full-cycle` / the `ns-review-pr` fan-out
> engine** (Stage 1.5/Phase 1 — [SKILL.md](../ns-review-pr/SKILL.md), #18292):
> those review flows use C++ anchor detection (`ACharacter`,
> `UGameplayAbility`, `UAbilitySystemComponent`, `UAttributeSet`,
> `UCharacterMovementComponent`) because they look at post-hoc diff. This
> skill uses generator `system_type` because classes may not be emitted yet
> when the phase runs. The two signals are deliberately overlapping but not
> identical — a `stamina` system_type may not touch a movement anchor, so
> create-system can invoke the evaluator while the review engine later skips
> it. This is intended: create-system does behavioral smoke test at
> generation time; the review engine does it again at review time only if it
> still looks gameplay-relevant to a static C++ reader.

If any gate fails → display one-line reason and skip to Output Format.

**Workflow** (when all gates pass):

```python
# Step 4.5a: Health gate
health = ue_check_health(compact=true)
if health.editor_hint != "online":
    display("Phase 4.5 SKIP: Editor offline — live-QA requires running Editor")
    goto_output_format()

# Step 4.5b: Derive expected behaviors from generated classes
# For each UGameplayAbility derived class → expected behavior: cooldown tag
#   applied + removed after ability activation
# For each AActor derived class with MovementComponent → expected: Velocity
#   changes on input
# For each UAttributeSet derived class → expected: attribute delta on apply
expected_behaviors = auto_derive_from_generated_classes(phase_2_output)

# Step 4.5c: Resolve input sequence
# Priority 1: Saved/TestRecordings/ heuristic (filename contains generated class name)
# Priority 2: Default minimal stability sequence (no input, check PIE starts)
input_sequence = resolve_input_sequence(generated_classes)

# Step 4.5d: Invoke evaluator (no reloops — single-shot for create-system path)
verdict = Agent(
    subagent_type="ns-ue-qa-evaluator",
    prompt=json.dumps({
        "sprint_contract": f"Generated {system_name} — classes: {class_list}",
        "commit_sha": None,  # not in a commit context yet
        "input_sequence": input_sequence,
        "expected_behaviors": expected_behaviors,
        "reloop_count": 0,
        "previous_findings": []
    }),
    timeout="15m"
)

# Step 4.5e: Report inline in Output Format "Live QA" section
# Do NOT block further work — create-system is generative, not gating.
# Findings are advisory: user decides whether to fix or ship.
```

**Opt-out scenarios**:
- `UE_CREATE_SYSTEM_SKIP_LIVE_QA=1` — explicit bypass (e.g. batch generation,
  non-interactive shell)
- Non-gameplay system type — UI-only, material-only, VFX-only generation
  has no behavioral predicates worth verifying
- Editor offline — no failure, just informational skip

---

## Output Format

```text
=== System Generation Report ===

System: <SystemName> (<SystemType>)
Engine: UE <version>

--- Phase 2: C++ Classes ---
✅ UCUDamageComponent.h/cpp (Source/MyGame/Components/)
✅ UCUDamageType.h/cpp (Source/MyGame/Damage/)
✅ UGA_ApplyDamage.h/cpp (Source/MyGame/Abilities/)

--- Phase 3: Visual Assets (manual creation guide emitted) ---
🛠️ BP_DamageComponent (Blueprint graph)
🛠️ NS_DamageImpact (Niagara VFX)
⏭️ Material: Not needed for this system type
⏭️ UI Widget: Not needed for this system type

--- Phase 4: Validation ---
✅ Build: PASSED (0 errors, 2 warnings)

--- Phase 4.5: Live QA (Issue #9579, gameplay systems only) ---
✅ ns-ue-qa-evaluator: passed (score 92/100, 0 findings)
   Predicates verified: cooldown tag applied + removed, damage delta applied
   Artifacts: Saved/Screenshots/MCP/qa_damage_system_*.png (2 files)

   # Alternative outcomes:
   # ⏭️  SKIPPED (editor offline) — re-run /full-cycle --live-qa to verify
   # ⏭️  SKIPPED (non-gameplay system) — no behavioral predicates apply
   # ⚠️  2 warnings (not blocker) — see ## Live QA Findings section below

--- Build.cs Dependencies ---
Add to <Module>.Build.cs PublicDependencyModuleNames:
  "GameplayAbilities", "GameplayTags", "Niagara"

--- Next Steps ---
1. Open BP_DamageComponent in Blueprint Editor → verify event graph
2. Assign NS_DamageImpact to UCUDamageComponent::ImpactVFX property
3. Add GA_ApplyDamage to your AbilitySystemComponent
4. Test in PIE: apply damage and verify VFX plays
```

**Manual creation example for non-BP visual assets**:
```text
--- Phase 3: Visual Assets ---
🛠️ NS_DamageImpact (manual)
   → Content Browser → Niagara → System → Template: "Simple Sprite Burst"
   → Name: NS_DamageImpact
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| System type unclear | Ambiguous description | Ask user which domains needed (C++/BP/VFX/Material/UI) |
| C++ generation fails | Missing base class or output dir conflict | Report error, ask user clarification (No-Retry) |
| `ue_fix_errors` finds issues | Missing Build.cs deps or include errors | List required modules, attempt 1 auto-fix cycle |
| Partial system generated | Some phases succeeded, others failed | Report partial success with clear status per domain |
| Similar system exists | Phase 0 discovers existing implementation | Warn user, suggest extending instead of creating new |
| Too many classes requested | >6 classes in one invocation | Split into 2 invocations or suggest code-generation-power |

---

## Evaluation Criteria

### Positive (SHOULD trigger this skill)
1. "데미지 시스템 만들어줘" → ✅ (시스템 만들어)
2. "Create projectile system with VFX" → ✅ (system with VFX)
3. "인벤토리 시스템 UI 포함해서 만들어" → ✅ (시스템 + UI 포함)
4. "전체 무기 시스템 생성" → ✅ (전체 시스템)
5. "Complete damage system with effects and UI" → ✅ (complete system + effects + UI)

### Negative (should NOT trigger)
1. "캐릭터 만들어줘" → `/ue-scaffold` (no "시스템" qualifier)
2. "시스템 어떻게 동작해?" → `/ue-explain` (explanation, not creation)
3. "파워 코드 생성" → `/code-generation-power` (explicit multi-class trigger)
4. "GAS 캐릭터 시스템 만들어" → `/code-generation-power` (scenario-based)
5. "시스템 진단해줘" → `/ue-audit` (diagnostic, not creation)

---

## Self-Test Mode

`/ue-create-system --self-test` runs a focused validation of this skill itself:

1. **Structure check**: Verify SKILL.md, REFERENCE.md, EXAMPLES.md exist
2. **Frontmatter check**: Parse YAML frontmatter for required fields (`name`, `description`, `argument-hint`)
3. **Routing check**: Verify routing.json entry exists and trigger patterns match activation triggers
4. **Trigger eval**: Run 20 eval cases from `evals/trigger-grading.json`, verify 100% pass rate

**Output**: PASS / FAIL with per-check results. Does not execute full 5-Phase workflow.

---

## References

- Predecessors: `/ue-scaffold` (v1.5.1), `/code-generation-power` (v2.0.0)
- Engine version: `ue_check_health()` → `engine_version.version`
