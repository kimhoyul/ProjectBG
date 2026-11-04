# UE Docs Audit — Reference (Cold Content)

Progressive-disclosure companion to [SKILL.md](SKILL.md). The hot path (auto-execution,
2-tier workflow, grading) lives in SKILL.md; this file holds the full trigger lists,
disambiguation table, output template, error-recovery table, and evaluation criteria.

---

## Triggers & Disambiguation

### Auto-Trigger Phrases

**Korean**: "엔진 문서 감사", "엔진 문서 감사해줘", "문서 번들 헬스", "문서 번들 건강한가",
"문서 커버리지 감사", "엔진 문서 검증", "문서 번들 상태 확인".

**English**: "docs audit", "engine docs audit", "bundle health", "doc bundle health",
"docs coverage audit", "is my doc bundle healthy", "validate engine docs bundle".

### Should NOT Activate — Disambiguation

| If the user wants... | Route to | Not this skill because |
|----------------------|----------|------------------------|
| A single engine doc read/search | `ue_engine_docs(read/search)` | this is a whole-bundle *health* rollup, not a lookup |
| Whole-*project* 6-point health | `/ue-audit` | that audits BP/C++/build/config; this is engine-docs-only |
| Config `.ini` health | `/ue-config-audit` | that lints project config; this checks the doc `.ndb` bundle |
| A single CVar deep dive | `/ue-cvar-explorer` | unrelated (CVar metadata, not doc coverage) |
| Cross-version doc diff | `ue_engine_docs(diff)` | diff compares two versions; audit scores one bundle's health |

This skill is the engine-docs **bundle-health** lane: one `validate` verdict + coverage
rollup, answering "is my `.ndb` healthy and complete?".

---

## Output Format

```text
=== Engine-Docs Bundle Audit ===
Overall: WARN (skeleton — code blocks pending hydration)
Corpus:  12,105 docs | cache 200/200 (100%) | versions: ue5_6, ue5_7, ue5_8

Dimension            Grade   Detail
-------------------  ------  ------------------------------------------------
Bundle load          PASS    12,105 docs indexed
Hydration            WARN    skeleton — C++ code blocks hydrate in background
Versions             PASS    engine_versions=[ue5_6,ue5_7,ue5_8], unrecognized=[],
                             newest_version_present=true (expected_newest=ue5_8)
Coverage             WARN    82% (missing: EXAMPLES x14, API_CHANGES x31)
Decrypt integrity    PASS    16/16 sampled OK, 0 failures

Top fixes:
  1. [WARN] Hydration pending — run/await background hydration for full code.
  2. [WARN] 31 modules missing API_CHANGES.md — thin source corpus for those
     modules (e.g. ue5_8/modules/Plugins/Foo). Re-generate docs for them.

Manifest: <total_modules> modules, <fully_documented_modules> fully documented,
          per-category rollup (Runtime <n> @ <pct>%, Plugins <n> @ <pct>%, ...);
          USAGE is optional and excluded from coverage_pct.
```

**Stale-newest example** (`newest_version_present:false`, MCP `pass` still true):

```text
Versions             WARN    newest_version_present=false; expected_newest=ue5_8;
                             present engine_versions=[ue5_6,ue5_7]; unrecognized=[]
                             → re-init / republish so the newest tier lands
Overall: WARN  (not FAIL — validate.pass may remain true; Issue #24712 / #21516)
```

Rules: report N/A / "unavailable" dimensions separately so an absent check never inflates
the grade. Always cite concrete `sample_modules_missing_overview` paths and
`decrypt_sample.failures[].path` when present. Always surface `expected_newest` + actual
`engine_versions` when `newest_version_present` is false — never hide that WARN behind a
green Overall just because `versions.pass` is true.

---

## Error Recovery

| Symptom | Cause | Recovery |
|---------|-------|----------|
| `Engine doc bundle not loaded` | no `.ndb` loaded | run `narshamcp init-engine-docs --engine-dir <UE_ENGINE_PATH>` or set `UECODEGEN_ENGINE_DOC_BUNDLE`; then re-audit |
| `validate` `pass:false`, `failed_checks:["decrypt_sample"]` | corrupt / tampered `.ndb` | re-init the bundle (decrypt failure = integrity breach, never ignore) |
| `failed_checks:["versions"]` | empty or non-`ue5_*` corpus | verify the engine-dir points at a real UE tree; re-init |
| `newest_version_present:false` (MCP `pass` still true; not in `failed_checks`) | stale corpus missing NEWEST tier (Issue #21516 / #24712 WARN-only) | grade Versions **WARN** on the scorecard; cite `expected_newest` + actual `engine_versions`; re-run `narshamcp init-engine-docs --engine-dir <UE_ENGINE_PATH>` / republish so the newest tier lands. Do **not** treat as FAIL or claim Overall PASS |
| `hydration.skeleton:true` | un-hydrated skeleton bundle | text search works now; await background hydration (or run it) for code blocks; re-audit after |
| `manifest` `total_modules:0` | prefix/`engine_version` filtered everything out | widen the scope (omit `engine_version`) before concluding the corpus is empty |
| `manifest` vs `validate` coverage disagree | should be impossible — both read one SSOT (Issue #20018) | treat as a real defect and file it; do NOT paper over by picking one |
| coverage_pct low but pass:true | thin source corpus for some modules | not a bundle defect — the source docs are sparse; cite the missing modules |

---

## Evaluation Criteria

### Activation Test Cases

| Prompt | Should activate? |
|--------|------------------|
| "엔진 문서 번들 감사해줘" | YES |
| "is my engine docs bundle healthy?" | YES |
| "audit engine docs coverage" | YES |
| "read the Niagara OVERVIEW doc" | NO → `ue_engine_docs(read)` |
| "audit my project config" | NO → `/ue-config-audit` |
| "is my whole project healthy?" | NO → `/ue-audit` |

### Quantitative Metrics

- **Call-count**: 2-3 MCP calls (`validate` + `stats` [+ optional `manifest`]). More than 3
  (e.g. per-module `read`) is a regression — the rollup scores from the batch responses.
- **Honesty**: a `skeleton:true` bundle is graded WARN, never PASS-with-full-code; an absent
  manifest op yields "coverage summary only", never a fabricated full matrix;
  `newest_version_present:false` is scorecard **WARN** (with `expected_newest` cited), never
  silent Overall PASS and never alone a FAIL / `failed_checks` entry (Rust WARN-only contract).
- **Actionability**: every WARN/FAIL dimension carries a concrete fix (re-init command,
  cited module path, hydration note, or newest-tier republish) — no bare grades.
