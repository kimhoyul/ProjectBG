---
name: ue-pr-asset-review
model: opus
description: "Purpose: PR review of .uasset/.umap changes between two VCS revisions. Produces a risk-ranked, PR-comment-ready summary of which assets changed, how heavily each is referenced, and what may break. Wraps ue_diff(compare_revision) with summary_mode + with_impact. C++ source changes are handed off to ue_analyze_symbols(detect_changes). Triggers: 'PR asset review', 'review asset changes', 'what assets changed', 'uasset diff review', 'asset PR summary', 'PR 에셋 리뷰', '에셋 변경 리뷰', '에셋 diff 요약', '리비전 에셋 비교'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with a UE 5.x project under Git/SVN/Perforce"
argument-hint: "revision_a [revision_b] [--type <asset_type>] (e.g., 'HEAD~1 HEAD', '5948 5950 --type material', 'origin/main')"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "20728"
audience: end-user
---

# UE PR Asset Review — Risk-Ranked .uasset Change Summary

**Issue**: #20728 (Epic #20709 — Diff part audit follow-up)
**Purpose**: Turn a raw revision diff into a PR-comment-ready, risk-ranked view of asset changes.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill loads, Claude **MUST run the analysis** — do not just print docs.

1. **Parse `$ARGUMENTS`** into `revision_a`, optional `revision_b`, optional `--type <asset_type>`.
   > If `$ARGUMENTS` is empty, ask for the base revision (and optionally the target). Default `revision_b` = working copy.
2. **Run ONE primary MCP call** — `ue_diff(compare_revision)` with `summary_mode: true` and `with_impact: true`.
3. **Render** the returned `markdown` field as the review, then add a short **Reviewer Verdict** (below).
4. **Do NOT** analyze C++ source impact here — the tool already points those at `detect_changes` (the boundary).

---

## The ONE Primary Call

```jsonc
// ue_diff — compare_revision with the PR-review layers
{
  "operation": "compare_revision",
  "revision_a": "<base>",         // required, e.g. "HEAD~1", "5948", "origin/main"
  "revision_b": "<target>",       // optional; omit for working copy / HEAD
  "summary_mode": true,           // adds the ranked `markdown` digest
  "with_impact": true,            // joins referenced_by_count + top_referencers per asset
  "asset_type": "<type>"          // optional; only when --type given (e.g. "material")
  // "vcs_type": "git|svn|p4"     // only if auto-detect fails
  // "project_root": "<path>"     // only for a monorepo sub-dir / non-default working tree
}
```

**Why this one call is sufficient** (Issue #20728):
- `summary_mode` buckets the raw per-file entries into **assets / C++ source / config** and returns a
  `markdown` field ready to paste into a PR comment.
- `with_impact` joins each `.uasset`/`.umap` entry with `referenced_by_count` (blast radius) and
  `top_referencers`, reusing the same asset-reference index as `ue_search_assets(find_references)`.
- The asset table is **already ranked by risk** (status weight x referencer count). You do not need a
  second call to rank it.

---

## Risk Ranking (how the tool scores)

Each changed asset gets a `impact_risk.score` (0-100) and `level`:

| Factor | Weight |
|---|---|
| Status: `deleted` | base 40 |
| Status: `added` | base 15 |
| Status: `modified` | base 10 |
| Each referencing asset | +3 (capped +55) |

`level`: `critical` (>=70), `high` (45-69), `medium` (20-44), `low` (<20).

**Read the ranking like a reviewer:**
- **critical / high + deleted** — a heavily-referenced asset is being removed. Confirm every
  `top_referencer` is updated or the reference is intentionally dropped, else expect broken refs.
- **modified + many referencers** — a change with wide blast radius; call out that downstream assets
  should be re-validated / recooked.
- **low + few referencers** — safe to skim.

> **Honesty contract**: when the reference index is still building / unavailable, an asset's
> `referenced_by_count` is `null` and `impact_index_unavailable: true` — **not** `0`. Do NOT report
> "nothing references this" from a `null`; say the impact index was unavailable and suggest
> `ue_cache_control(regenerate_metadata)` then a re-run.

---

## C++ Source Boundary

`compare_revision` deliberately does **not** analyze C++ symbol impact. The `markdown` lists changed
`.h`/`.cpp` files and points at:

```jsonc
{ "tool": "ue_analyze_symbols", "operation": "detect_changes", "scope": "all" }
```

If the reviewer wants C++ -> Blueprint/GAS impact, run that (or the `/ue-pre-commit` skill) as a
**separate** step. Do not try to derive C++ impact inside this skill.

---

## Reviewer Verdict (append after the markdown)

After rendering the tool's `markdown`, add a 2-4 line verdict:

```text
### Reviewer Verdict
- <N> asset(s) changed; highest risk: <asset> (<level>, <ref_count> referencers, <status>).
- Action: <what to double-check> (e.g. "verify BP_Game / BP_HUD still resolve BP_Hero before merge").
- C++: <count> source file(s) changed — run detect_changes for symbol impact (not covered here).
```

Keep it ASCII, concrete, and tied to the `top_referencers` the tool returned.

---

## Examples

| User says | Call |
|---|---|
| "review asset changes since main" | `compare_revision(revision_a="origin/main", summary_mode, with_impact)` |
| "what materials changed between 5948 and 5950" | `compare_revision(revision_a="5948", revision_b="5950", asset_type="material", summary_mode, with_impact)` |
| "PR asset review for this branch" | `compare_revision(revision_a="HEAD~1", summary_mode, with_impact)` (working-copy target) |

---

## Call-Count Budget

- **1 MCP call** (the `compare_revision` above) covers the whole review.
- Optional **1 more** only if the reviewer explicitly asks for C++ symbol impact
  (`ue_analyze_symbols(detect_changes)`).
- Do **not** call `find_references` per asset — `with_impact` already joined it in one pass.
