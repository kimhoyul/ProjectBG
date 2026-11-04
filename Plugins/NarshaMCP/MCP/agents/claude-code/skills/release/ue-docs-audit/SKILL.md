---
name: ue-docs-audit
model: opus
description: "Purpose: Engine-docs bundle health + coverage audit. Rolls up the ue_engine_docs validate op (load / skeleton-vs-hydrated / versions / doc_type coverage / decrypt sample) plus the coverage-manifest op into one health scorecard. Answers 'Is my engine-doc bundle healthy and complete?'. Triggers: 'docs audit', 'engine docs audit', 'bundle health', 'docs coverage', '엔진 문서 감사', '문서 번들 헬스', '문서 커버리지 감사'."
compatibility: "Requires NarshaMCP MCP server (v7.0.0+) with an engine-doc bundle loaded (narshamcp init-engine-docs)"
argument-hint: "empty for a full bundle audit; optional --engine-version 5.8 to scope the coverage summary narration"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # ships in end-user bundle: a product-facing bundle-health diagnostic
---

# UE Docs Audit — Engine-Docs Bundle Health + Coverage Rollup

**Issue**: #20022 (Epic #20009)
**Purpose**: Two-tier engine-docs health diagnostic. The `validate` op (Issue #20022) is the
substrate — a fully-offline one-call verdict over the encrypted `.ndb` bundle; this skill is
the second tier that rolls its five checks (plus the coverage-manifest op when available) into
a single "is my engine-doc bundle healthy and complete?" scorecard with prioritized fixes.

---

## Auto-Execution Instructions

> **CRITICAL**: When loaded, Claude **MUST execute the rollup automatically** — run the MCP
> tools and score the result, do NOT just print documentation.

1. **Target**: `$ARGUMENTS`. Empty → full bundle audit; `--engine-version <ver>` → scope the
   coverage narration to one version (the underlying ops are corpus-wide).
2. **Run the 2-tier rollup** (Tier 1 = substrate op calls, Tier 2 = score + suggest).
3. **Emit a scorecard** across the five checks + coverage with ranked fix suggestions.

**Call-count minimization** (MANDATORY): complete in **2-3 MCP calls** (Bash excluded).
Optimal 2 = `validate` + `stats`. `validate` is the primary call (all five health checks in
one offline pass); `stats` supplies cache/doc-count context. `manifest` (Issue #20018 / S9,
shipped) is a THIRD, OPTIONAL call — add it when the audit needs the per-module or
per-category breakdown rather than the aggregate. Do NOT `read` each doc.

---

## Auto-Trigger Phrases

KO: "엔진 문서 감사", "문서 번들 헬스", "문서 커버리지 감사", "번들 건강한가", "엔진 문서 검증".
EN: "docs audit", "engine docs audit", "bundle health", "docs coverage audit", "is my doc bundle healthy".
Full KO/EN lists + disambiguation table: [REFERENCE.md § Triggers & Disambiguation](REFERENCE.md#triggers--disambiguation).

**Disambiguation (one-liner)**: whole-*project* 6-point health → `/ue-audit`; config `.ini`
health → `/ue-config-audit`; a single doc lookup/search → `ue_engine_docs(read|search)`. This
skill is the engine-docs **bundle-health** lane.

---

## 2-Tier Rollup Workflow

```text
Tier 1 (op call): validate -> 5 checks | stats -> doc/cache counts | manifest -> coverage matrix (opt)
Tier 2 (skill):   map checks -> dimensions, grade each, rank fixes -> scorecard
```

### Step 1 — Tier 1 substrate op calls

```python
# PRIMARY call: all five health checks in one offline pass.
health = ue_engine_docs(operation="validate")
# -> pass, total_docs, checks{bundle_load, hydration, versions, coverage, decrypt_sample},
#    failed_checks[]

# CONTEXT call: cache/doc-count context for the scorecard header.
stats = ue_engine_docs(operation="stats")
# -> total_docs, cached_docs, cache_capacity, cache_usage_pct

# OPTIONAL (Issue #20018 / S9): the modules x doc_types coverage matrix. Summary
# mode is the aggregate + per-category rollup; add summary=False, missing_only=True
# for the specific modules to fix. Same expected-doc-type SSOT as `validate`, so
# total_modules / coverage_pct always agree with health["checks"]["coverage"].
matrix = ue_engine_docs(operation="manifest")
# -> total_modules, fully_documented_modules, coverage_pct, missing_by_doc_type{},
#    optional_present_by_doc_type{}, by_category[]
```

> **Honesty gate (MCP Empty-Result Fallback)**: read `health["checks"]`. A `hydration.skeleton
> == true` bundle is **valid but incomplete** — grade coverage **WARN (skeleton: code blocks
> pending)**, never PASS-with-full-code. If you skip the `manifest` call, grade coverage from
> `validate`'s `coverage` summary and say "aggregate only — per-module matrix not fetched".
> `manifest` reports `USAGE` under `optional_*` and EXCLUDES it from `coverage_pct`; never
> grade a module down for a missing optional type. A skipped/absent check is NOT a passing check.

### Step 2 — Tier 2 dimension scoring

Map each `validate` check to a graded dimension (see REFERENCE.md for the full rubric):

| Dimension | Source |
|-----------|--------|
| Bundle load | `checks.bundle_load.pass` + `total_docs` |
| Hydration | `checks.hydration.skeleton` (skeleton → WARN) |
| Versions | `checks.versions.pass` / `engine_versions` / `unrecognized` **and** `newest_version_present` + `expected_newest` (Issue #24712 / #21516) |
| Coverage | `checks.coverage.missing_by_doc_type` + `coverage_pct` |
| Decrypt integrity | `checks.decrypt_sample.pass` + `failures[]` |

**Versions grade (WARN-only newest tier — do not fold into MCP `pass`/`failed_checks`)**:

1. `checks.versions.pass:false` or `unrecognized` non-empty → **FAIL** (empty / non-`ue5_*` corpus).
2. Else if `checks.versions.newest_version_present:false` → **WARN**. Surface
   `expected_newest`, actual `engine_versions`, and a re-init / republish hint
   (`narshamcp init-engine-docs --engine-dir <UE_ENGINE_PATH>`). Never upgrade this
   alone to FAIL: validate deliberately keeps newest-missing out of `pass` /
   `failed_checks` so a legitimately not-yet-hydrated newest tier (or CI skeleton
   corpus) does not turn the op red.
3. Else → **PASS**.

Grade rubric (other dimensions): check `pass:true` → PASS · skeleton or
coverage_pct < 100 → WARN · any `failed_checks` entry → FAIL. **Overall** = worst
dimension (any FAIL→FAIL, else any WARN→WARN, else PASS). A stale-newest corpus
with every other check green is therefore **Overall WARN** while MCP
`validate.pass` may still be `true`.

### Step 3 — Rank fix suggestions

Rank by severity: a decrypt failure (corrupt/tampered `.ndb`) → **re-init** the bundle; a
failed `bundle_load`/`versions` → the bundle is empty or wrong-version → re-run
`narshamcp init-engine-docs --engine-dir <UE_ENGINE_PATH>`; `newest_version_present:false`
→ **WARN** re-init / republish so `expected_newest` lands in the corpus (do not treat as
integrity FAIL); a low `coverage_pct` / high `missing_by_doc_type` → the source corpus is
thin for those modules (cite the `sample_modules_missing_overview` paths, or
`manifest(summary=False, missing_only=True)` for the exact list). Skeleton → wait for
background hydration or run it.

---

## Output, Errors, Evaluation

Scorecard output template, the full error-recovery table, and activation/quantitative eval
criteria live in [REFERENCE.md](REFERENCE.md) (progressive disclosure) — sections
[§ Output Format](REFERENCE.md#output-format), [§ Error Recovery](REFERENCE.md#error-recovery),
[§ Evaluation Criteria](REFERENCE.md#evaluation-criteria).
