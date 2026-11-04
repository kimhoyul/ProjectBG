# /ue-perf-audit — Reference (cold content)

Companion to [SKILL.md](SKILL.md) (Issue #20596). Loaded on demand: scoring rubric,
report template, capture chain, citation rules.

---

## Scoring Rubric (Audit Score 0-100)

**Audit Score** is a weighted composite. A section whose data source is absent is
**UNSCORED** and its weight is renormalized over the scored sections — never scored 0
for missing data (missing ≠ bad, per the honest-signal rule).

| Section | Weight | Source | Rubric |
|---|---|---|---|
| Trace Quality | 40% | `quality.score` (0-100) | use as-is |
| Frame Health | 30% | `summary.drop_rate_percent` (P), `summary.worst_drop_ms` (W) **only when** `detect_frame_drops.success==true` and `quality.frame_boundaries.frame_health_scorable==true` (Issue #23856) | `100 - 2×P - min(20, W/10)`, floor 0; else **UNSCORED** (never score zeros from `NoFrameData`) |
| Runtime Balance | 30% | `frame_summary.p99_frame_ms` ÷ `frame_summary.median_frame_ms` (R) | R ≤ 2 → 100 · R ≤ 4 → 66 · R > 4 → 33 · missing → UNSCORED |

`quality.score`'s own weights are emitted in the response (`quality.score_weights`,
SSOT: `csv_backend/validate.rs` consts — cpu 30 / frames 25 or 15 inferred / gpu 15 /
clock 10 / session 10 / supplementary 10).

---

## Capture Chain (Step C) — 캡처→검증→분석→리포트

When the Editor is live and the user asked this skill to capture first, drive the
**MCP-owned** capture ops on `ue_analyze_insights` (Issue #20592 / #23848).
Do **not** primary-path raw `Trace.Start`/`Trace.Stop` via console, and do **not**
pick “the newest `.utrace` under Saved/Profiling” — that can attach an unrelated capture.

```python
ToolSearch("select:mcp__narshamcp__ue_analyze_insights")

start = ue_analyze_insights(
    operation="capture_start",
    channels="cpu,gpu,frame,bookmark,loadtime",  # intent → channel list (see INTENT_CHANNEL)
)
# ... user plays the scenario in PIE (ask them, or wait the requested duration) ...
ue_analyze_insights(operation="capture_status")
stop = ue_analyze_insights(operation="capture_stop")
trace = stop.get("trace_path") or (stop.get("session") or {}).get("owned_trace_path")
if not (stop.get("success") and trace):
    # Surface stop.error / stop.hint (editor offline, flush incomplete, not_mcp_owned).
    raise RuntimeError(stop)
# Step 1: validate_trace + smart_analyze on the owned path only.
quality = ue_analyze_insights(operation="validate_trace", trace_path=trace)
overview = ue_analyze_insights(operation="smart_analyze", trace_path=trace, top_n=20)
```

If the requested intent needs extra channels (memory, net, file, task, stats), pass
them on `capture_start` (`channels=` / `preset=`) — the intent→channel map is the same
one `validate_trace` uses for recommended-channel warnings (SSOT: `INTENT_CHANNEL` in
`csv_backend/validate.rs`).

Manual capture guide (no live Editor / owned capture unavailable):

- **In-Editor**: Tools → Trace → Start Trace (channels: `cpu,gpu,frame,bookmark`) →
  play scenario in PIE → stop. Saved to `Saved/Profiling/`. Then pass that **explicit**
  path to `validate_trace` / `smart_analyze` (still no newest-file guess).
- **CLI**: `-trace=cpu,gpu,frame,bookmark` on the game/editor command line.

---

## Report Template

```markdown
# 🔎 UE Performance Audit — <trace file name>
**Verdict**: <quality.verdict> · **Audit Score**: NN/100
(Trace Quality NN·40% / Frame Health NN·30% / Runtime Balance NN·30%)

## 1. Trace Quality (validate_trace) — NN/100
- verdict, warnings[] verbatim, notes[] verbatim
- capture: duration_seconds (duration_source), total_events, events_per_second
- clock: status, anomalous_frame_count, quantized_spans
- frame_boundaries: source (+ synthetic_bucketing_suspected)
- session: platform / build_version / compatibility
- intent: requested → satisfied? (+ recapture_hint when false)

## 2. Frame Health (detect_frame_drops) — NN/100 or UNSCORED
- drop_rate_percent, drop_count / total_frames_analyzed, worst_drop_ms
- top offender frames: frame_index · duration_ms · top_contributor

## 3. Runtime Overview (smart_analyze)
- frame_summary: avg / median / p95 / p99 (cite field names)
- top 5 hot_functions (name · exclusive_time_ms · thread)
- gpu_summary / memory_summary / counters_summary when non-null

## 4. Channel Rollup
| channel | present | headline |  ← quality.channels × overview.channels
(9 rollup channels + cpu/gpu/frames/memory/counters first-class rows)

## 5. Recommendations
- quality.capabilities.unavailable[] → recapture_hint per missing analysis
- quality.capabilities.unsupported[] → state the op is permanently unavailable
  and cite its `blocking_artifact` + `reason`; never offer a recapture for it
- worst hitch offender → suggest /ue-insights-profiler deep dive
  (find_callers on top_contributor)
- unsatisfied intent → exact -trace= flag from quality.intent.recapture_hint
```

---

## Verdict & Citation Rules

Inherited verbatim from ue-insights-profiler (Issue #96 F-006/F-007 three-layer
contract — see [../ue-insights-profiler/SKILL.md](../ue-insights-profiler/SKILL.md)
§ Verdict Citation Rules):

1. **Citation Rule** — every number in the report MUST be cited by raw response
   field name and value (e.g. `quality.score`, `summary.drop_rate_percent`,
   `frame_summary.p99_frame_ms`). No recomputation beyond the documented rubric.
2. **Warning Surfacing Rule** — `quality.warnings[]` are surfaced verbatim BEFORE
   any conclusion.
3. **Sanity Check Rule** — reject any ms figure exceeding
   `quality.capture.duration_seconds * 1000` as a recomputation error. When
   `duration_seconds` is null (`duration_source: "unknown"`), skip the ceiling
   check and say so.

---

## validate_trace `quality` Response Schema (Issue #20596)

```text
quality.schema_version        "1.1"   # 1.0 -> 1.1: additive capabilities.unsupported (#24725)
quality.verdict               ok | usable_with_gaps | unusable
quality.score / score_max     0-100 composite (weights in score_weights)
quality.channels              { present: [...], missing: [{channel, recapture_hint}] }
quality.capture               { duration_seconds, duration_source, total_events, events_per_second }
quality.clock                 { status, anomalous_frame_count, quantized_spans, measured_frame_count }
quality.frame_boundaries      { present, source: channel|inference_fallback|none,
                                cpu_frame_buckets, rendering_frame_count, gpu_frame_count,
                                cpu_frames_present, gpu_frames_present,
                                frame_health_scorable,   # Issue #23856 — hitch ops need CPU frames
                                synthetic_bucketing_suspected }
quality.session               { present, platform, app_name, project_name, build_version,
                                branch, compatibility,   # ok|unknown|invalid|incompatible (#23856)
                                compatibility_detail, supported_engine_range }
quality.intent                null | { requested, normalized, required_channel, satisfied,
                                recapture_hint } | { requested, normalized, satisfied: null,
                                supported_intents }
quality.capabilities          { analyzable: [{analysis, ops}],
                                unavailable: [{analysis, missing_channel, recapture_hint}],
                                unsupported: [{op, blocking_artifact, reason,
                                               recapture_hint: null}] }   # Issue #24725
quality.warnings / notes      hard problems vs best-effort ambiguity (honest-signal split)
```

Supported `intent` aliases: cpu · gpu · frame/frames/hitch/fps · memory/alloc/allocations ·
loading/asset_loading/streaming · network/net · file/file_io/io · task/tasks ·
counters/stats · log/logs/bookmarks/markers · callstack/callstacks.

---

## Disambiguation (full table)

| Query flavor | Right skill |
|---|---|
| "이 트레이스 믿을 수 있어? / 성능 감사 리포트" (scored breadth-first audit of a capture) | **ue-perf-audit** |
| "왜 느려? / 병목 원인 찾아줘" (depth-first cause hunt on one symptom) | /ue-insights-profiler |
| "프로젝트 건강도 / Epic 표준 / 빌드 시간" (static project-wide audit, no trace) | /performance-health-check |
| "프로젝트 진단 / 경고 요약 / 메시지 로그" (editor+config+asset health) | /ue-audit |
| "데몬 메모리 / RSS" (NarshaMCP daemon memory) | /mem-report |
| "에셋 검증 / data validation" (asset quality) | /ue-validate |

**Rule**: this skill is the breadth-first scored snapshot ("is this capture trustworthy
and is runtime healthy?"). The moment the user wants the *cause* of one specific
bottleneck, hand off to /ue-insights-profiler with the trace path and the offending
function name.
