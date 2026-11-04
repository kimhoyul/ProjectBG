---
name: ue-widget-authoring
model: opus
description: "Purpose: Offline Widget Blueprint authoring/validation round-trip. Chains ue_manage_widget export_dsl (deterministic JSON baseline) → validate_dsl (13-rule lint) + validate_widget_blueprint (4-rule asset health) → import_dsl/apply_dsl (#19998, dry_run default) or Phase 3a authoring ops → re-export diff verification. UMG animation authoring is a parallel Editor path: ue_manage_blueprint(create_widget_animation) with required RenderTransform component (#26388/#27237) — import_dsl still fail-closes non-empty animations (#27238). Triggers: '위젯 DSL', '위젯 오서링', '위젯 애니메이션 만들어', 'create widget animation', 'widget dsl', 'widget authoring'. Runtime widget symptoms (focus/hit-test/visibility/PIE) → ue-slate-debug. BP graph logic → blueprint-generator."
compatibility: "Requires NarshaMCP MCP server (v0.9.13+). Export/validate legs are fully offline (binary metadata cache, Rule 20 — no Editor). import_dsl/apply_dsl execute legs and Phase 3a apply/preview require a live Editor (HTTP RC → CURemoteLibrary); import_dsl dry_run=true is offline-safe."
argument-hint: "WBP name or /Game path + intended edits (e.g. 'WBP_HUD 타이틀 텍스트 바꿔'), or empty to prompt"
metadata:
  version: "1.0.1"
  author: "Next-Stage-Inc"
  issue: "20001"
audience: end-user  # offline authoring/validate surface (Epic #19989 "A4")
---

# UE Widget Authoring — Offline WBP Authoring / Validation Round-Trip

**Issue**: #20001 (Epic #19989 "A4") · Runtime twin: `ue-slate-debug` (this skill is the offline half that twin explicitly defers to)
**Purpose**: Orchestrate the offline widget surface of `ue_manage_widget`: export a deterministic DSL baseline, validate two ways, apply via **`import_dsl` / `apply_dsl`** (#19998; `dry_run=true` offline-safe) **or** Phase 3a surgical ops, then re-export-diff. Animation authoring is a parallel Editor path — see below. Recipes: [REFERENCE.md](REFERENCE.md).

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, Claude **MUST execute the workflow automatically** — do NOT just print documentation.

1. **Target**: `$ARGUMENTS` (WBP name/path + intended edits). Empty → ask one question: "어떤 WBP를 어떻게 바꾸거나 검증할까요?"
2. Run the 6-step workflow below. Validation-only stops after Step 3. **Animation-authoring** (`create_widget_animation` / Translation keys) skips `import_dsl`/`apply_dsl` for `blueprint.animations`, keeps `include_animations=False`, and must verify the exact authored keys with the `get_animation_tracks` receipt in REFERENCE §7.
3. Emit the round-trip report (or the animation-authoring result).

---

## Auto-Trigger Phrases

Routing SSOT: `routing.json`.
- **KO**: 위젯 DSL · 위젯 오서링 · WBP 오서링 · 오프라인 위젯 편집 · 위젯 검증 · WBP 검증 · 위젯 텍스트 바꿔 · 위젯 애니메이션 만들어 · UMG 애니메이션 저작
- **EN**: widget dsl · widget authoring · export widget dsl · validate widget · offline widget edit · create widget animation · widget animation authoring

Runtime symptoms (안 보임/안 눌림/focus/hit-test/designer-vs-PIE) → **`ue-slate-debug`**. BP graph logic → **`blueprint-generator`**. Widget C++ class scaffolding phrasing ("위젯 만들어줘", "create a health bar widget") currently routes to **`ue-scaffold`** — invoke this skill explicitly (`/ue-widget-authoring`) for WBP-asset creation via `create_widget_blueprint`.

---

## Workflow

All ops pass `operation=` verbatim — `export_dsl` and `validate_widget_blueprint` are **smart-router-excluded** (explicit op required; bare `widget_name` would silently route to `get_structure`).

```python
# Step 1 — Probe (2-tier honesty: offline vs editor legs)
h = ue_check_health(compact=True)
# editor reachable -> full chain. offline -> Steps 2-4 + 6a still run
# (binary parse, Rule 20); Step 5 apply degrades to an op-plan printout.

# Step 2 — Baseline export (deterministic JSON: same WBP -> byte-identical)
# include_animations=false is the authorable import_dsl path (#27238 fail-close).
# Animation authoring is parallel: ue_manage_blueprint(create_widget_animation).
base = ue_manage_widget(operation="export_dsl", widget_name="/Game/UI/WBP_HUD",
                        include_animations=False)
# save to disk: the git-diffable baseline for Step 6's diff

# Step 3 — Two-tier validate (both offline)
vwb = ue_manage_widget(operation="validate_widget_blueprint",
                       widget_name="/Game/UI/WBP_HUD")     # 4-rule asset health
vd  = ue_manage_widget(operation="validate_dsl",
                       dsl_content=json.dumps(base["dsl"])) # 13-rule lint sanity
# pre-existing errors -> report FIRST; only proceed to edits on a clean (or
# acknowledged) baseline. Rule catalogs -> REFERENCE.md §2-3.

# Step 4 — Edit plan: map the intent (or a hand-edited DSL diff) onto
# Phase 3a ops via REFERENCE.md §5 mapping table. New-WBP request ->
# create_widget_blueprint branch. Validate any edited DSL text with
# validate_dsl BEFORE planning ops (no asset touched).

# Step 5 — Apply (prefer import_dsl/apply_dsl #19998; Phase 3a for surgical ops)
# Full-DSL path (plan first, then execute):
plan = ue_manage_widget(operation="import_dsl", dsl_content=json.dumps(edited_dsl),
                        dry_run=True)   # offline-safe plan
r = ue_manage_widget(operation="apply_dsl", dsl_content=json.dumps(edited_dsl),
                     dry_run=False)    # Editor required to execute
# Surgical path (single-field edits; persistence contract #16934/#16777):
# r = ue_manage_widget(operation="set_slot_property", widget_name=...,
#                      target_widget=..., property_name="z_order", property_value=3)
# read r["result"]["persisted"] / ["marked_dirty"] (NOT envelope top-level).

# Step 6 — Verify DSL/tree/property edits only. This export cannot prove animation authoring.
after = ue_manage_widget(operation="export_dsl", widget_name=...,
                         include_animations=False)  # 6a: match Step 2's authorable export mode
# diff(base, after) must show EXACTLY the intended change (determinism contract)
# 6b: validate_dsl(after) + validate_widget_blueprint -> no NEW errors
# 6c (optional, editor): get_preview for a visual designer-view confirmation
```

**Mark-dirty-only edits**: Step 6's disk-level diff shows them only after a save; report "applied (pending save)" honestly — never claim disk persistence without a changed re-export.

### Animation authoring (parallel path, #26388 / #27237)

`import_dsl` fail-closes non-empty `blueprint.animations` (`unsupported-import-section`, #27238). Keep `include_animations=False`. Author via `ue_manage_blueprint` (Editor required). RenderTransform keys need `component` (`Scale.X|Y|XY`, `Translation.X|Y|XY`) unless `legacy_default_scale_component=true`. Details: [REFERENCE.md](REFERENCE.md) §7.

```python
r = ue_manage_blueprint(operation="create_widget_animation",
    blueprint_name="WBP_MainMenu", animation_name="SlideIn", duration=1.0,
    tracks=[{"target_widget": "Img_Logo", "property_path": "RenderTransform",
             "keyframes": [{"time": 0.0, "value": -200.0, "component": "Translation.X"},
                           {"time": 1.0, "value": 0.0, "component": "Translation.X"}]}])
# Required: assert success + persisted, regenerate metadata, then match the
# exact target/property/time_seconds/value/component receipt from REFERENCE §7.
```

---

## Report Template

```markdown
## Widget Authoring Round-Trip — {WBP} ({mode: full|offline-degraded})
**Baseline**: {widget_count} widgets · validate_widget_blueprint {E} errors / {W} warnings · validate_dsl {ok|issues}
**Applied**: {op} {target}.{property} {old} → {new} (persisted: {true|false(pending save)})
**Re-export diff**: {clean — intended change only | UNEXPECTED drift: ...}
**Post-validate**: asset {E}/{W} · dsl {ok} · {회귀 없음|신규 이슈}
```

---

## Error Recovery

| Error | Cause | Action |
|---|---|---|
| export_dsl 0 widgets / asset not found | 이름 오타 / 캐시 미인덱싱 | `ue_search_assets`로 정확한 `/Game` 경로 확인 후 재시도 |
| validate_dsl `unknown-widget-class` warning | 커스텀 위젯 클래스 | did-you-mean 제안 확인; 오탈자 아니면 warning 수용 (`strict=false`) |
| validate_dsl/import_dsl `unsupported-import-section` | non-empty `blueprint.animations` | `include_animations=false` 재-export. 저작은 `ue_manage_blueprint(operation="create_widget_animation")` (#27238) |
| apply 후 re-export diff 무변화 | mark-dirty-only op (저장 전) 또는 stale cache | persisted:false면 정상(저장 대기 보고); persisted:true인데 무변화면 `ue_cache_control(operation="regenerate_metadata")` 후 재-export |
| apply가 editor_required | 에디터 미기동 | Step 5를 op-플랜 출력으로 degrade하고 오프라인 leg 결과만 보고 |
| `result.persist_error` 존재 | in-call save 실패 | 에러 문자열 보고, 재적용 1회; 반복 시 수동 저장 안내 |

---

## Composability & Role Split

- **`ue-slate-debug`** — 런타임 증상 진단 (focus/hit-test/visibility). 본 스킬은 그 스킬 Scope가 명시적으로 위임한 오프라인 절반.
- **`blueprint-generator`** — BP 그래프/노드 로직 생성. 위젯 트리·슬롯·속성은 본 스킬.
- `ue_manage_widget(get_structure|get_hierarchy)` 직접 호출 — 단건 구조 조회는 스킬 불요.
- **import_dsl / apply_dsl (#19998)**: Step 5 1차 경로. Phase 3a는 surgical (`ue_manage_widget.md`).
- **`ue_manage_blueprint(create_widget_animation)`** (#26388/#27237): animation authoring. Do not revert `include_animations=False` (#27455).
