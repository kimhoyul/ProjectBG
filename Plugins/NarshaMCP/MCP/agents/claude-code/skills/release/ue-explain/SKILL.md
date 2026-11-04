---
name: ue-explain
model: opus
description: "Purpose: Universal UE system explainer with auto-domain routing. Answers 'How does X work?' for any UE entity (C++ class, Blueprint, Material, Niagara, GAS, Config, AI, PCG, Sequencer). Auto-detects target type from name prefix and routes to optimal MCP tools. Triggers: 'explain', 'how does', 'what is', '설명해', '어떻게 동작', '구조 분석', 'show me how', 'tell me about'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
argument-hint: "TargetName (e.g., ACharacter, BP_Player, M_Rock, NS_Fire, GA_Attack, r.Shadow.MaxResolution) | --self-test"
metadata:
  version: "2.2.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  changelog: "2.2.0 — modularized into REFERENCE.md / EXAMPLES.md siblings (Issue #10510, Epic #10451). SKILL.md trimmed 35021 → 12716 bytes (-63.7%, target was ≤19261 / -45%). Per-domain MCP templates → EXAMPLES.md scenarios 4-6 (Niagara, GAS, Config). Verbose verification gates / Step 0 8-type matrix / Step 4 engine source paths / 10 quality rules → REFERENCE.md. Hot-path preserved: auto-exec / verification gate one-liners / Step 0 decision flow / Step 1 routing dict / Step 2 routing table / output format / error recovery. No behaviour change; routing.json untouched. 2.1.0 — initial skill (Issue #4800)."
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Explain — Universal System Explainer

**Issue**: #4800, #6002, #6352, #10510 | **Purpose**: Answer "How does X work?" for **any** UE entity, auto-routed by domain
**See also**: [REFERENCE.md](REFERENCE.md) (discovery / verification / quality) | [EXAMPLES.md](EXAMPLES.md) (per-domain walkthroughs)

---

## Auto-Execution Instructions

> **CRITICAL**: When loaded, Claude **MUST run the workflow** — actually call the MCP tools, not just display docs.

1. **Step 0-PRE**: `ue_engine_docs(read, "TOPIC_INDEX.md", level=2)` first ([algorithm](REFERENCE.md#step-0-pre-topic_index-navigation))
2. **Target**: `$ARGUMENTS` (empty → ask which UE entity)
3. **Auto-detect domain** via Step 1 routing dict
4. **Execute MCP tools** for that domain (templates: [EXAMPLES.md](EXAMPLES.md))
5. **Generate explanation** (output format below)

---

## MANDATORY VERIFICATION GATES (check BEFORE answering)

> One-liner contracts. Rationale: [REFERENCE.md § Verification Gates Detail](REFERENCE.md#verification-gates-detail).

- **Gate 1 NAME AUDIT** — every name comes from a tool result, `Read`/`Grep`, or the question. ≥3 unverified → re-search first.
- **Gate 2 ABSENCE CHECK** — `engine_docs`+`grep` both empty ⇒ "Not confirmed in engine source" IS valid. No fabricated workarounds.
- **Gate 3 ALTERNATIVE CHECK** — grep "deprecated"/"legacy"/"prefer X over Y" nearby. Report current-gen, not first-found.
- **Gate 4 engine_docs CROSS-CHECK** — `ue_engine_docs(search, "<topic>")` MANDATORY. Disagrees with grep → report BOTH, prefer designed > workaround > training.

**Tool Call Budget** — reserve LAST 2 calls: V1 `ue_engine_docs(operation="search", query="<topic>")`; V2 `ue_grep` for the EXACT cited name. [Patterns](REFERENCE.md#common-failure-patterns).

---

## Step 0-PRE: TOPIC_INDEX Navigation

`ue_engine_docs(operation="read", path="TOPIC_INDEX.md", level=2)` → identify primary engine module, then read its `TROUBLESHOOT.md`. Skip if "not found"; unclear → read the 2 most likely. [Full algorithm](REFERENCE.md#step-0-pre-topic_index-navigation).

---

## Step 0: Discovery — Tool Selection by Question Type

Pick the FIRST tool from the question's signal (priority order; keywords in linked matrix):

```text
1. CVar/설정 이름 (r., sg., au.)        → ue_analyze_config
2. 에러/버그 증상                        → ue_grep(증상 키워드)
3. 빌드/자동화 (BuildGraph/cook/UAT)     → ue_grep(scope=engine, path_pattern="*.cs,*.xml")
4. 특정 플러그인 (Mutable/nDisplay/Zen)  → ue_grep(scope=engine, path_pattern=plugin)
5. 성능 (bottleneck/delayed)            → ue_grep(symptom) + ue_analyze_config
6. 워크플로우/설정 방법                   → ue_engine_docs(search) + ue_analyze_config
7. 소스 위치 (구현 어디)                  → ue_grep(클래스/함수명)
8. 개념/API/방법 (default)              → ue_engine_docs(search)
```

**Decision**: TOPIC_INDEX module → primary target | engine_docs hit → doc base + grep code | grep symbol → cpp_class | grep asset → asset workflow | config hit → bypass Step 2 | nothing → "not found".

Full 8-type matrix (keywords+reasoning) + engine path hints + workflow code: [REFERENCE.md § Step 0 Discovery Detail](REFERENCE.md#step-0-discovery-detail). Auto-trigger phrases: [REFERENCE.md § Auto-Trigger Phrases](REFERENCE.md#auto-trigger-phrases).

---

## Step 1: Identify Target Type (Domain Auto-Routing)

Match name prefix → domain (asset prefixes by specificity, then C++ type prefix):

```python
ROUTING_RULES = {
    "BP_/WBP_/ABP_": "blueprint", "GA_/GE_": "ability", "NS_": "niagara",
    "M_/MI_": "material", "ST_/BT_": "ai", "PCG_": "pcg", "LS_": "sequencer",
    "DT_": "datatable", "MS_/Sound": "audio", "CR_": "control_rig", "IK_": "ik_rig",
    "r./gc./net./fx.": "config",          # config CVar prefixes
    "A/U/F/E/I": "cpp_class",              # Actor/UObject/struct/Enum/Interface
}
# No prefix match → KEYWORD_ROUTING (control rig, ik rig, media/video/electra)
# Still none → ue_search_assets to discover type
```

Full prefix comments + KEYWORD_ROUTING: [REFERENCE.md § Step 1 Routing Dict Full](REFERENCE.md#step-1-routing-dict-full).

---

## Step 2: Route to MCP Tools (by Domain)

| Domain | Primary Tool + key ops / caveat |
|--------|---------------------------------|
| `cpp_class` | `ue_analyze_symbols`: `search_symbols`+`get_methods`+`trace_hierarchy` (+`find_virtual_overrides` only if "override"/"재정의") |
| `blueprint` | `ue_manage_blueprint(get_structure)` (+`get_raw_nodes`/`search_variables` per bypass) |
| `material` | `ue_manage_material`: `get_nodes`+`get_hierarchy`+`analyze_performance` |
| `niagara` | `ue_manage_niagara`: `get_structure`+`get_parameters` |
| `ability` | `ue_manage_gameplay(trace_abilities)` |
| `config` | `ue_analyze_config(search_config)` — full bypass if `ue_read` gave cvar_type/default/help_text. `search_config` **rejects** wildcard-only/empty ("*"); use exact key or partial (e.g. `r.Shadow`) |
| `ai` | `ue_manage_ai`: `get_structure`+`find_transitions` |
| `pcg` | `ue_manage_pcg`: `get_structure`+`trace_flow` |
| `sequencer` | `ue_sequencer_structure`: `get_structure`+`list_tracks` |
| `audio` | `ue_analyze_symbols(search_symbols)` `FMetaSoundSource`/`FAudioDevice` + `ue_engine_docs` + `ue_analyze_config(au.*)` |
| `control_rig` / `ik_rig` | `ue_manage_rigging(get_structure, rig_type=...)` (+`FAnimNode_RigidBody` for physics) |
| `media` | `ue_analyze_symbols(IVideoRecordingSystem, IMediaPlayer, FElectraPlayer)` + `ue_grep(scope=engine, path_pattern="Engine/Plugins/Media/")` |
| `datatable` / `fallback` | `ue_search_assets` (datatable skippable if `ue_read` gave asset info) |

Bypass conditions, non-C++ search (`*.usf`/`*.xml`/`*.ini`), CVar fallback, templates: [REFERENCE.md § Step 2 Routing Detail](REFERENCE.md#step-2-routing-detail), [EXAMPLES.md](EXAMPLES.md).

---

## Step 3: Multi-Query Search Planning + engine_docs Cross-Check

Plan **3 diverse queries** per target (EXACT name | RELATED helper/manager | SYMPTOM keyword) and run ALL even if the first hits — first-hit bias = 50% of bimodal failures. Step 0 skipped `ue_engine_docs` → call now (MANDATORY); contradictions → report BOTH, prefer designed. 6-query expansion: [REFERENCE.md § Multi-Query Search Planning](REFERENCE.md#multi-query-search-planning).

---

## Step 4: Engine Source Verification

After MCP tools return, grep EXACT result names in engine source (`UECODEGEN_ENGINE_DIR`) for missing details (exact CVar strings, CL# refs). ⚠️ Cite only line numbers actually `Read()`. Per-domain paths + secondary discovery (animation legacy+IK Retargeter, MetaSound, Mutable, audio spatialization): [REFERENCE.md § Engine Source Verification](REFERENCE.md#engine-source-verification).


---

## Answer Quality Rules (5 hot-path checks)

1. **CONFIDENCE** — `Read()` the file with the SPECIFIC mechanism, not a wrapper. Wrapper → grep the function it calls.
2. **No fabrication** — Cite only names from MCP results, `Read`/`Grep`, or the question. No invented names.
3. **Absence is an answer** — Related-but-not-exact ⇒ may not exist. State "not currently supported" with queries tried, not a substitute.
4. **Synthesized output** — Coherent synthesis, not narration ("Now let me search..."). Out of calls → synthesize so far.
5. **Complete answer** — Always include What it is / How it works / When+Why. Never end on bare tool calls or empty.

Full 10-rule list (architecture validation, name-existence, exhaustive absence, version/CL): [REFERENCE.md § Answer Quality Rules Full](REFERENCE.md#answer-quality-rules-full).

---

## Output Format

```text
=== [Target Name] — [Type] Explained ===
--- Overview ---     [1-2 sentence purpose]
--- Structure ---    [domain tree; templates in REFERENCE]
--- How It Works --- [execution / data flow]
--- Key Files ---    [Source/.../X.h:42 | /Game/.../Asset.uasset]
--- Related ---      [parent/child classes, assets, deps]
```

Per-domain trees: [REFERENCE.md § Output Format Templates](REFERENCE.md#output-format-templates).

---

## Error Recovery

Dispatch on failure (full causes/table: [REFERENCE.md § Error Recovery](REFERENCE.md#error-recovery)):
- Symbol not found → `ue_search_assets` fuzzy name
- Blueprint not found → `ue_search_assets(asset_type="blueprint", query="*PartialName*")`
- Config key not found → `ue_analyze_config(search_config, key="<partial>")`
- MCP tool unavailable → `ue_check_health()` to verify connection

**Step 5 (optional)**: version/CL history → `/ue-cl-tracker` (not auto-executed; preserves tool-call budget).

---

**Status**: Phase 1 MVP | **Related**: Issue #4800 (MCP-First Tool Selection)
