---
name: visual-regression-test
model: opus
description: "Screenshot-based visual regression detection via Claude Vision. Capture golden images, verify against current, report regressions. Triggers: '비주얼 회귀', '시각 회귀 테스트', '골든 이미지', 'visual regression', 'golden image', 'screenshot diff'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Editor running for capture/verify modes. list/delete work offline."
allowed-tools:
  - Read
  - Write
  - Edit
  - Bash
  - Glob
  - AskUserQuestion
  - mcp__narshamcp__ue_editor_debug
  - mcp__narshamcp__ue_check_health
argument-hint: capture|verify|list|delete [--name <id>] [--tag <variant>] [--purpose "..."] [--tolerance "..."] [--fail-on regression|change]
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  issue: "#7927"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Visual Regression Test

**Status**: Beta
**Issue**: #7927

---

## Purpose

Detect visual regressions between a **known good** screenshot (the golden image) and the **current** render.
Uses Issue #7911's `auto_capture_analyze` for capture and Claude's native vision (via `Read()` on the image file) for comparison — no external image-diff library.

**Core workflow**:
1. Once: `capture` the golden image for each test point (level, variant).
2. After changes: `verify` re-captures and compares against the golden using the manifest's natural-language `tolerance` hint.
3. On regression: a markdown report lands in `Saved/VisualBaselines/reports/` and exit code is non-zero (if `--fail-on=regression`).

**Key insight**: Tolerance is expressed as plain English in the manifest (e.g., "allow AA jitter; allow ≤5% brightness variation"). Claude Vision reads both images and applies that guideline — no pixel threshold tuning.

---

## Subcommands

| Subcommand | Purpose |
|---|---|
| `capture` | Save a new golden image + upsert manifest entry |
| `verify` | Capture current → compare against golden → emit verdict + optional report |
| `list` | Enumerate all baselines and their variants |
| `delete` | Remove a variant (or all variants for a name) |

---

## Activation Triggers

### Auto-Trigger Keywords

**Korean**: "비주얼 회귀", "시각 회귀", "골든 이미지", "시각 회귀 테스트", "스크린샷 비교", "렌더링 회귀", "이미지 회귀 테스트", "기준 이미지 저장"
**English**: "visual regression", "visual regression test", "golden image", "screenshot diff", "image regression", "pixel regression", "baseline image", "snapshot visual"

### Should NOT Activate

- "화면 분석해줘" / "capture and analyze" → `/screenshot-vision-analyzer` (ad-hoc capture, no baseline)
- "BP 회귀 테스트" / "test regression for X()" → `/ue-test-gen` (logic regression)
- "테스트 시나리오" → `/ue-test-scenario`

---

## Argument Conventions

### Slug validation

`--name` and `--tag` MUST match the regex `^[A-Za-z0-9_-]{1,64}$` before being used in any path operation.
Reject (with a clear error) anything containing `/`, `\`, `..`, whitespace, or exceeding 64 chars.

### File layout

```
Saved/VisualBaselines/
├── {name}/
│   ├── manifest.json
│   ├── {tag}.png
│   └── {tag}.png.bak        # one-generation backup, only present after overwrite
└── reports/
    └── {yyyy-mm-ddTHHMMSS}_{name}_{tag}.md
```

---

## Workflow

### STEP 0: Parse & Validate

1. Parse `$ARGUMENTS` into `{subcommand, --name, --tag, --purpose, --tolerance, --fail-on}`.
2. If `$ARGUMENTS` is `--self-test` → run **Self-Test Mode** and exit.
3. Subcommand must be one of `capture | verify | list | delete`.
4. For `capture`/`verify`/`delete`: `--name` required (list accepts optional `--name` filter).
5. For `capture`/`verify`: `--tag` required (default: `default` is acceptable if user omits).
6. Validate `--name` and `--tag` against `^[A-Za-z0-9_-]{1,64}$`. On fail → exit with:
   ```
   ERROR: --name/--tag must match ^[A-Za-z0-9_-]{1,64}$. Got: <value>.
   ```

### STEP 1: Dispatch by Subcommand

---

### `capture` — Save Golden Image

1. Compute `dir = Saved/VisualBaselines/{name}` (create if missing via `Bash("mkdir -p ...")`).
2. Read or initialise `dir/manifest.json` (Tier 1 — see **Manifest Schema** in REFERENCE.md).
3. If `variants[tag]` already exists:
   - Use `AskUserQuestion` to confirm overwrite (auto mode still requires this per CLAUDE.md destructive-action rule).
   - On confirm, move current `{tag}.png` → `{tag}.png.bak` before proceeding.
4. Health check: call `mcp__narshamcp__ue_check_health(compact=true)` (short name: `ue_check_health`). If editor unavailable → exit with a remediation hint.
5. Capture via `ue_editor_debug` (short name of `mcp__narshamcp__ue_editor_debug`):
   ```python
   ue_editor_debug(
       operation="auto_capture_analyze",
       capture_count=1,
       return_mode="file",
       resolution="1920x1080",
       format="png",
       purpose=f"visual-regression baseline for {name}/{tag}",
   )
   ```
6. From the response, take `captures[0].file_path` and `Bash("mv <returned_path> <dir>/{tag}.png")`.
7. Upsert `manifest.variants[tag]`:
   ```json
   {
     "image": "{tag}.png",
     "captured_at": "<ISO-8601 UTC now>",
     "commit_sha": "<short SHA from `git rev-parse --short HEAD` — fallback 'unknown'>",
     "tolerance": "<--tolerance arg or 'strict: no visible differences'>",
     "resolution": "1920x1080",
     "format": "png",
     "capture_target": "viewport",
     "purpose": "<--purpose arg>"
   }
   ```
8. Write manifest back (preserve other variants).
9. Report:
   ```
   [OK] Golden captured: Saved/VisualBaselines/{name}/{tag}.png
   Next: /visual-regression-test verify --name {name} --tag {tag}
   ```

---

### `verify` — Compare Current Against Golden

1. Read `Saved/VisualBaselines/{name}/manifest.json`. If missing or `variants[tag]` absent → exit with:
   ```
   ERROR: no baseline for {name}/{tag}. Run: /visual-regression-test capture --name {name} --tag {tag}
   ```
2. Health check (same as `capture` STEP 4).
3. Capture current using the manifest's stored `resolution`, `format`, `capture_target`:
   ```python
   ue_editor_debug(
       operation="auto_capture_analyze",
       capture_count=1,
       return_mode="file",
       resolution=manifest["variants"][tag]["resolution"],
       format=manifest["variants"][tag]["format"],
       purpose=f"visual-regression verify for {name}/{tag}",
   )
   ```
4. Move the returned file to `Saved/VisualBaselines/{name}/.current_{tag}.png` (hidden temp).
5. `Read(golden_path)` — Claude sees the baseline.
6. `Read(current_path)` — Claude sees the current render.
7. Apply the **comparison protocol** below with the manifest's `tolerance` text as the judgment guideline.
8. Emit:
   ```
   verdict: match | match_within_tolerance | regression
   severity: none | minor | major
   findings: [ { element, location, change }, ... ]
   ```
9. If verdict is `regression` (or `change` when `--fail-on=change`):
   - Write `Saved/VisualBaselines/reports/{YYYY-MM-DDTHHMMSS}_{name}_{tag}.md` with:
     - Header: `# Visual Regression — {name}/{tag}`
     - Metadata table: golden captured_at, golden commit_sha, current captured_at, current commit_sha (from `git rev-parse --short HEAD`), resolution, tolerance
     - Section: **Findings** (table of element/location/change/severity)
     - Section: **Recommended next steps** (view images, re-accept as new golden, fix & re-run)
10. Clean up temp: keep `.current_{tag}.png` if verdict != match (user inspects it); delete on match.
11. Exit code:
    - 0 if verdict ∈ {`match`, `match_within_tolerance`}
    - 1 if verdict == `regression` and `--fail-on=regression` (default)
    - 1 if verdict != `match` and `--fail-on=change`

#### Comparison protocol (executed by Claude after both `Read()`s)

> You are comparing the golden image (first Read) against the current image (second Read) for visual regression detection.
>
> Tolerance guideline from manifest: `{manifest.variants[tag].tolerance}`
>
> Report:
> 1. **verdict** — one of `match`, `match_within_tolerance`, `regression`. `match` = visually identical; `match_within_tolerance` = differences fall inside the tolerance guideline; `regression` = unambiguous visual breakage (missing elements, broken materials, layout shifts, wrong colors beyond tolerance).
> 2. **severity** — `none` (match), `minor` (within tolerance), `major` (regression).
> 3. **findings** — list of concrete differences with element (what), location (where), change (how).
> 4. Stay concrete. Avoid speculation about causes.

---

### `list` — Enumerate Baselines

1. `Glob("Saved/VisualBaselines/*/manifest.json")` (non-recursive on name dirs).
2. For each manifest, parse and render a table:
   ```
   | Name | Variants | Captured | Resolution |
   |------|----------|----------|------------|
   | ...  | default, night | 2026-04-18 | 1920x1080 |
   ```
3. If `--name` filter supplied, restrict to that name and show per-variant detail (tolerance, purpose).

---

### `delete` — Remove Baseline

1. If `--tag` provided:
   - `AskUserQuestion`: "Delete `{name}/{tag}` (image + manifest entry)? No undo."
   - On confirm: `Bash("rm -f Saved/VisualBaselines/{name}/{tag}.png Saved/VisualBaselines/{name}/{tag}.png.bak")` and remove `variants[tag]` from manifest. If manifest becomes empty, delete the whole `{name}/` directory.
2. If `--tag` NOT provided:
   - `AskUserQuestion`: "Delete ENTIRE baseline `{name}` (all variants)? No undo."
   - On confirm: `Bash("rm -rf Saved/VisualBaselines/{name}")`.

---

## Manifest Schema

See [REFERENCE.md](REFERENCE.md#manifest-schema) for the full schema. Version 1 fields:

```json
{
  "version": 1,
  "name": "LEVEL_Day1_MainHall",
  "variants": {
    "{tag}": {
      "image": "{tag}.png",
      "captured_at": "ISO-8601 UTC",
      "commit_sha": "short SHA or 'unknown'",
      "tolerance": "free-form English guidance",
      "resolution": "WIDTHxHEIGHT",
      "format": "png|jpeg",
      "capture_target": "viewport|pie",
      "purpose": "free-form description"
    }
  }
}
```

`tolerance` is **Claude instruction text**, not executable code. Do not embed markup that is interpreted as commands.

---

## Error Handling

| Error | Cause | Recovery |
|-------|-------|----------|
| `ERROR: --name/--tag must match ...` | Slug violation | Use only `[A-Za-z0-9_-]`, ≤64 chars |
| `ERROR: no baseline for {name}/{tag}` | Verify before capture | Run `capture` first |
| `Resolution mismatch (current=X, manifest=Y)` | Editor settings changed | Re-capture golden OR force matching resolution |
| `No active editor viewport` | Editor not running | Launch Editor, open target level, retry |
| `Manifest parse error` | Hand-edited JSON broken | Restore from backup or re-capture |
| `AskUserQuestion timeout on overwrite` | No confirmation received | Re-run with fresh session |

---

## Self-Test Mode

`/visual-regression-test --self-test` runs 5 checks (no Editor needed):

1. **Structure**: `SKILL.md`, `REFERENCE.md`, `EXAMPLES.md` all exist under `.claude/skills/visual-regression-test/`.
2. **Frontmatter**: YAML parses; required fields (`name`, `description`, `allowed-tools`) present.
3. **Routing**: `.claude/skills/routing.json` has entry with `skill == "visual-regression-test"` and at least one trigger pattern in `trigger_patterns.ko` and one in `trigger_patterns.en`.
4. **Version consistency**: `metadata.version` in SKILL.md frontmatter matches the version claim in REFERENCE.md header.
5. **Subcommand coverage**: `EXAMPLES.md` contains invocations for all four subcommands (`capture`, `verify`, `list`, `delete`).

**Output**:
```
visual-regression-test --self-test
  [PASS] Structure
  [PASS] Frontmatter
  [PASS] Routing
  [PASS] Version consistency
  [PASS] Subcommand coverage
Self-test: 5/5 PASS
```

Failures print the specific check and remedy. Does not touch the file system beyond reads.

---

## Related

- [REFERENCE.md](REFERENCE.md) — full manifest schema, CLI reference, directory layout
- [EXAMPLES.md](EXAMPLES.md) — 4 walkthroughs (capture, verify-pass, verify-tolerance, verify-regression)
- [VISUAL_REGRESSION_GUIDE.md](../../../docs/user-guides/tools/VISUAL_REGRESSION_GUIDE.md) — user-facing usage guide
- [screenshot-vision-analyzer](../screenshot-vision-analyzer/SKILL.md) — sibling skill for ad-hoc capture + analysis (no baseline)
- Issue #7927 (this skill), Issue #7911 (underlying `auto_capture_analyze` operation)
