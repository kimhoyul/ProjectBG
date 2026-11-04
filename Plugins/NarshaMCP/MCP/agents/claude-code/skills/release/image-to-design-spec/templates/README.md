# /image-to-design-spec Stage 2 Templates

> Built 2026-04-30 to mitigate F-006 (PHASE2 E2E pilot defect — empty templates/ caused inline brief generation in `wf_20260429_d2e9a7`). Validated structure: pilot brief reached 9.4/10 with the layout reproduced here in `hybrid_brief.md.template`.

## 4 templates

| Template | Used when | Sections |
|---|---|---|
| `hybrid_brief.md.template` | `domain_distribution` is mixed (no domain ≥60%) — the PHASE2 pilot path | 16 |
| `ui_brief.md.template` | `domain_distribution.ui_mockup ≥ 0.6` | 11 |
| `system_brief.md.template` | `domain_distribution.system_diagram ≥ 0.6` | 12 |
| `concept_brief.md.template` | `domain_distribution.concept_art ≥ 0.6` | 11 |

## Loader contract (Skill responsibility)

The Skill (`/image-to-design-spec --stage 2` Phase 3) reads `stage1_analysis.json` and decides:

```python
dist = stage1["domain_distribution"]
total = sum(dist.values()) or 1
shares = {k: v/total for k, v in dist.items()}

if max(shares.values()) >= 0.6:
    dominant = max(shares, key=shares.get)
    template = f"{dominant.replace('_mockup','').replace('_diagram','').replace('_art','')}_brief.md.template"
    # ui_mockup → ui_brief, system_diagram → system_brief, concept_art → concept_brief
else:
    template = "hybrid_brief.md.template"
```

## Placeholder substitution

Templates use `{{var_name}}` — Jinja-like syntax. The Skill's renderer is responsible for substitution from `stage1_analysis.json` fields. Common placeholders:

| Placeholder | Source |
|---|---|
| `{{workflow_id}}` | manifest.workflow_id |
| `{{generated_at}}` | renderer-set ISO timestamp |
| `{{title_from_vision}}` | stage1.images[0].vision_extract.title_guess |
| `{{intended_experience}}` | stage1.images[0].vision_extract.design_intent.intended_experience |
| `{{intended_experience_summary_one_sentence}}` | renderer-derived (first sentence + key clause) |
| `{{global_summary}}` | stage1.global_summary |
| `{{components_count}}` | len(stage1.images[0].vision_extract.components) |
| `{{components_table_with_bbox_zone_interactive_intent_contribution}}` | renderer-formatted markdown table |
| `{{diegetic_field_count}}` | len(stage1.images[0].vision_extract.art_attributes.diegetic_layer.fields) |
| `{{diegetic_fields_table_placeholder_owner_attribute}}` | renderer-formatted markdown table |
| `{{emotion_spectrum_table}}` | stage1.images[0].vision_extract.design_intent.emotion_spectrum (Plutchik 5-axis table) |
| `{{schell_lenses_list_with_implications}}` | stage1.images[0].vision_extract.design_intent.schell_lens_applicable[] (rendered with implications) |
| `{{norman_dominant}}` | stage1.images[0].vision_extract.design_intent.norman_dominant_layer |
| `{{mda_dominant}}` | stage1.images[0].vision_extract.design_intent.mda_layer_dominant |
| `{{rld_indicators_table}}` | stage1.images[0].vision_extract.rld_indicators (visibility/loss/delay) |
| `{{loop_signals_table}}` | stage1.images[0].vision_extract.loop_signals (moment/session/meta) |
| `{{art_attributes_table}}` | stage1.images[0].vision_extract.art_attributes (lighting/composition/mood/fidelity) |
| `{{warnings_list_numbered}}` | stage1.warnings[] |
| `{{interactive_widget_names}}` | filter components where interactive==true |
| `{{always_visible_widgets}}` | renderer-derived from RLD analysis (initial draft — Skill should classify) |
| `{{material_recommendations_list}}` | renderer-derived from art_attributes (Vision-protected — derive, never copy) |

Placeholders not in stage1_analysis.json (e.g. `{{audio_axes_table_combat_ambient_magic}}`, `{{a11y_axes_table_*}}`, `{{failure_loop_steps_list_or_n_a}}`) — renderer should leave as TODO with a warning, OR apply per-section MANUAL_FIX in the iter1 of `/design-doc-iterate`.

## §8 sub-section numbering convention (F-007 mitigation)

`hybrid_brief.md.template` introduces 5 sub-sections under §8 (game-play implications). The pilot wf_20260429_d2e9a7 surfaced cosmetic disorder because iter2 fixes (FIX-007/008/009) were Edit-inserted ABOVE iter1 fixes (FIX-004/005/006) — final order was D/E/A/B/C instead of A→E.

**Canonical alphabetical order** (templates ARE correct — pilot was a runtime artefact only):

| ID | Section | Source FIX |
|---|---|---|
| `SEC-AUDIO-001`     | §8.A 음향 디렉션              | FIX-004 (audio = ~50% of visceral budget for action genres) |
| `SEC-A11Y-001`      | §8.B 접근성                  | FIX-005 (4-axis: colorblind / motion / controller / scaling) |
| `SEC-FAILURE-001`   | §8.C 실패 상태 / 죽음 루프   | FIX-006 (death-loop / souls drop) |
| `SEC-NETWORK-001`   | §8.D 네트워크 범위           | FIX-008 (single-player vs co-op vs MP) |
| `SEC-ONBOARDING-001`| §8.E 온보딩 패턴             | FIX-009 (progressive disclosure schedule) |

**Renderer responsibility**: when `/design-doc-iterate` Edit-inserts new §8.x sections after iter1, prefer **append at the end** of the §8 block (before §9 Stage 3 handoff guide), NOT insert above existing sub-sections. Templates here have the canonical order baked in.

**Lint rule (future)**: `/design-doc-finalize` Stage 4 9-point lint could optionally check sub-section letter ordering monotonicity (A < B < C < D < E) — currently omitted because it's cosmetic, not correctness-affecting.

---

## IP-safety preservation

All templates inherit `stage1_analysis.json`'s IP-safety contract:
- Placeholders only (e.g. `<boss_name>`, `<region_name>`) — never literal proper nouns
- `color_palette: []` — derive new palette from intended_experience, never copy hex
- `warnings_list_numbered` carries forward the IP-lookalike warning if present

## Validation note

The pilot wf_20260429_d2e9a7 used `hybrid_brief.md.template`'s structure (validated path). The other 3 templates are derived by reduction — same anchor citation rule, same Round 12 vocab, same IP-safety contract, but narrower section set per dominant domain.

Future improvement (not in this round): rendering script that auto-substitutes placeholders from stage1_analysis.json. Current renderer is the Skill's main-context Edit calls.
