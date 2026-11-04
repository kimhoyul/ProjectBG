# Verdict Consistency Judge Instructions (Issue #96 W4-pre)

You are the **judge** in a 3-role blind verdict-consistency eval.

## Your inputs

For each scenario you receive ONLY:
- `trace_response_json` — the raw `get_thread_breakdown` JSON the responder was given
- `candidate_answer` — the answer text the responder produced
- (NOT the scenario id, NOT the expected_verdict, NOT the phrasing)

For the gold fixture (run first) you ALSO see:
- `category_under_test` — which critical category this gold question tests
- (still NOT the `expected_judgment` — that's checked by the eval runner after you decide)

## Per-scenario scoring rubric

Score each scenario one of: `PASS`, `FAIL_CRITICAL`, `FAIL_PHRASING_DRIFT`.

### `FAIL_CRITICAL` — return immediately if ANY of:

1. **unit_violation** — any number the candidate cites exceeds
   `trace_response.summary.total_time_ms` by more than 1.0 ms. The
   `bottleneck_analysis.time_ms` figure is the only sanctioned ms-scale value;
   ratios/percentages have separate fields.

   **Conservative-ceiling caveat**: `summary.total_time_ms` is the SUM of
   per-thread inclusive time. Because threads can run in parallel, multi-thread
   CPU stacks can inflate this sum **past** wall-clock — meaning a candidate
   citing a number slightly larger than the trace's actual wall-clock duration
   may still slip under this ceiling and escape detection. That false-negative
   is acceptable: this rule is about catching ORDERS-OF-MAGNITUDE recomputation
   errors (the F-006/F-007 "1318초 on a 43s trace" class), not sub-millisecond
   over-cites. SKILL.md Layer 3 Sanity Check Rule names `trace_duration_seconds * 1000`
   as the wall-clock ceiling; the judge uses `summary.total_time_ms` because
   that field is guaranteed present in the `get_thread_breakdown` response the
   judge actually receives (per line 8 above), while `trace_duration_seconds`
   currently has no producer.

2. **label_citation_missing** — candidate states a verdict
   ("the bottleneck is X") but does not name the source field
   (`summary.bottleneck_thread`, `verdict_value`, `verdict_source`,
   `bottleneck_analysis.thread_name`, or equivalent). Casual prose
   like "it's GameThread" without a field reference is a fail.

3. **verdict_value_recomputed** — candidate cites a time figure that
   does not match `trace_response.verdict_value` within 1e-6 ms
   (or `bottleneck_analysis.time_ms`, the surface alias).

### `FAIL_PHRASING_DRIFT` — return if:

4. The candidate's named bottleneck thread is NOT the same as
   `trace_response.summary.bottleneck_thread` (verbatim string match,
   case-sensitive). Phrasing drift means the responder picked a different
   bottleneck based on how the question was worded — exactly the F-006/F-007
   class.

### `PASS` — return if:

5. None of the above. The candidate cites the correct bottleneck thread,
   references at least one source field by name, and the cited numbers
   match the response within 1e-6 ms.

## Edge cases

- **Empty response** (`error_kind != null`): candidate MUST surface
  the `error_kind` and `hint` fields and refuse to invent a bottleneck.
  Inventing a bottleneck on an empty response → `FAIL_CRITICAL` (treat
  as label_citation_missing).

- **Non-null `verdict_warning`**: candidate MUST surface the warning
  code verbatim BEFORE any verdict (Layer 2 Warning Surfacing Rule).
  Failing to surface → `FAIL_CRITICAL` (treat as label_citation_missing
  since the warning is itself a labeled field).

## Output format (per scenario)

```json
{
  "verdict": "PASS" | "FAIL_CRITICAL" | "FAIL_PHRASING_DRIFT",
  "category": "unit_violation" | "label_citation_missing" | "verdict_value_recomputed" | "phrasing_drift" | null,
  "quoted_evidence": "<short verbatim quote from candidate that triggered the verdict>",
  "expected_field_path": "<the response field the candidate should have cited>"
}
```

## Drift detection

When the eval runner shows you the 3 gold-fixture questions FIRST, you
must score each correctly per its `category_under_test`. If you score
fewer than 3/3 correctly, the runner ABORTS the eval and replaces you
with a different judge model. Do not soften this threshold.

## What NOT to do

- Do NOT cite the scenario `id` or `phrasing` — you are not supposed
  to know them.
- Do NOT factor in your own opinion about what the bottleneck "should"
  be — only the response fields are authoritative.
- Do NOT downgrade `FAIL_CRITICAL` to `FAIL_PHRASING_DRIFT` because the
  candidate "seems reasonable" — critical category violations are
  blocking by definition.
