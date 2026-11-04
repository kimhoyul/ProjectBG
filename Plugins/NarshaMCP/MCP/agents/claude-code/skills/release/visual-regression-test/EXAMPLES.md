# visual-regression-test — Examples

**Version**: 1.0.0
**See also**: [SKILL.md](SKILL.md) | [REFERENCE.md](REFERENCE.md)

Four end-to-end walkthroughs covering the full lifecycle: capture baseline → verify pass → verify within tolerance → verify regression.

---

## Example 1: Capture Golden Image (first run)

**Scenario**: You've just finished polishing the main hall lighting for `Day1_MainHall.umap` and want to lock the current look as the reference.

### Input

```bash
/visual-regression-test capture \
  --name LEVEL_Day1_MainHall \
  --tag default \
  --purpose "Main hall after lighting bake v3 — approved by art director" \
  --tolerance "allow AA jitter on mesh edges; allow up to 5% brightness variation from dynamic lighting"
```

### What happens

1. **Slug validation**: `LEVEL_Day1_MainHall` and `default` both match `^[A-Za-z0-9_-]{1,64}$` — accepted.
2. **Pre-check**: `ue_check_health(compact=true)` confirms Editor connected, `Day1_MainHall.umap` is the active level.
3. **Directory resolution**: `{project_root}/Saved/VisualBaselines/LEVEL_Day1_MainHall/` does not exist yet — skill creates it.
4. **No overwrite confirmation needed** (fresh baseline).
5. **Capture**:
   ```
   ue_editor_debug(
     operation="auto_capture_analyze",
     capture_count=1,
     capture_target="viewport",
     resolution="1920x1080",
     format="png",
     return_mode="file",
     purpose="visual-regression baseline: LEVEL_Day1_MainHall/default"
   )
   ```
   Returns `{"captures": [{"file_path": "C:/Project/Saved/Screenshots/MCP/capture_20260418_143201.png", "width": 1920, "height": 1080}]}`.

   > `return_mode="file"` is the only supported mode for this skill (the server also forces it to `file`); it is passed explicitly here to document the contract at the call site.
6. **Move into baseline dir**: the captured PNG is moved (not copied) to `Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png`.
7. **Write manifest**:
   ```json
   {
     "version": 1,
     "name": "LEVEL_Day1_MainHall",
     "variants": {
       "default": {
         "image": "default.png",
         "captured_at": "2026-04-18T14:32:01Z",
         "commit_sha": "ca921dd9d",
         "tolerance": "allow AA jitter on mesh edges; allow up to 5% brightness variation from dynamic lighting",
         "resolution": "1920x1080",
         "format": "png",
         "capture_target": "viewport",
         "purpose": "Main hall after lighting bake v3 — approved by art director"
       }
     }
   }
   ```

### Output

```text
CAPTURED: LEVEL_Day1_MainHall/default
  File:        Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png (4.2 MB, 1920x1080)
  Commit:      ca921dd9d
  Tolerance:   allow AA jitter on mesh edges; allow up to 5% brightness variation from dynamic lighting
  Purpose:     Main hall after lighting bake v3 — approved by art director

Next step: commit the baseline (or add to Git LFS for large images):
  git add Saved/VisualBaselines/LEVEL_Day1_MainHall/
  git commit -m "test(visual): add baseline for Day1 Main Hall"
```

---

## Example 2: Verify — exact match (CI green path)

**Scenario**: CI re-opens the same level after a no-op change (e.g., a README edit) and runs `verify` to guarantee no visual side effects leaked in.

### Input

```bash
/visual-regression-test verify \
  --name LEVEL_Day1_MainHall \
  --tag default
```

### What happens

1. **Manifest load**: reads `Saved/VisualBaselines/LEVEL_Day1_MainHall/manifest.json`, finds `variants.default`.
2. **Resolution check**: editor viewport = 1920x1080, matches `variants.default.resolution`. No warning.
3. **Re-capture**: same `ue_editor_debug` call as capture, but writes to a temp file `Saved/VisualBaselines/LEVEL_Day1_MainHall/.current_default.png`.
4. **Dual Read**:
   - `Read("Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png")` — golden
   - `Read("Saved/VisualBaselines/LEVEL_Day1_MainHall/.current_default.png")` — current
   Claude sees both images side-by-side.
5. **Claude Vision judgment** (internal reasoning, not part of output):
   > "Both frames show the same camera angle, same lighting, same actors in identical poses. Pixel-level AA jitter is imperceptible. No differences detected."
6. **Verdict**: `match`. No report is written (reports only fire on non-match).
7. **Cleanup**: `.current_default.png` is deleted.

### Output

```text
VERIFIED: LEVEL_Day1_MainHall/default — MATCH
  Baseline:    Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png (captured 2026-04-18T14:32:01Z)
  Current:     (temp — deleted)
  Verdict:     match
  Severity:    none

Exit code: 0
```

---

## Example 3: Verify — within tolerance (minor dynamic lighting drift)

**Scenario**: Between captures the sun has ticked slightly forward (dynamic time of day). Pixel-level diffs exist but they all fall inside the tolerance string the baseline was captured with.

### Input

```bash
/visual-regression-test verify \
  --name LEVEL_Day1_MainHall \
  --tag default
```

### What happens

1. Manifest load, re-capture, dual Read — same as Example 2.
2. **Claude Vision judgment**:
   > "The two frames are identical in composition and materials. The only differences are (a) a ~3% brightness lift on the floor near the south window, and (b) subtle AA jitter on the railing edges. Both fall within the declared tolerance: 'allow AA jitter on mesh edges; allow up to 5% brightness variation from dynamic lighting'."
3. **Verdict**: `match_within_tolerance`. Severity: `minor`.
4. **Report written** (non-match verdict always produces a report):
   `Saved/VisualBaselines/reports/2026-04-18T151022_LEVEL_Day1_MainHall_default.md`

### Report contents

```markdown
# Visual Regression Report

- **Baseline**: `LEVEL_Day1_MainHall/default`
- **Verdict**: `match_within_tolerance`
- **Severity**: `minor`
- **Timestamp**: 2026-04-18T15:10:22Z
- **Commit (baseline)**: ca921dd9d
- **Commit (current)**: 38bfc69d9

## Tolerance (declared at capture time)

> allow AA jitter on mesh edges; allow up to 5% brightness variation from dynamic lighting

## Findings

| Element | Location | Change |
|---|---|---|
| Floor diffuse | bottom-right quadrant, near south window | brightness +~3% (within tolerance) |
| Railing edges | mid-frame horizontal band | AA jitter (within tolerance) |

## Conclusion

All observed differences fall inside the declared tolerance. No action required.
If these drifts should be treated as regressions in the future, tighten the tolerance
string and re-capture the baseline.
```

### Output

```text
VERIFIED: LEVEL_Day1_MainHall/default — MATCH (within tolerance)
  Baseline:    Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png
  Verdict:     match_within_tolerance
  Severity:    minor
  Findings:    2 (all within tolerance)
  Report:      Saved/VisualBaselines/reports/2026-04-18T151022_LEVEL_Day1_MainHall_default.md

Exit code: 0 (--fail-on=regression default)
```

With `--fail-on=change` the exit code would be `1` (any non-`match` verdict fails the gate).

---

## Example 4: Verify — true regression (broken material)

**Scenario**: A merge accidentally replaced the floor material on `Day1_MainHall` — the diffuse color shifted from grey stone to a vibrant purple. CI catches this on the pre-merge gate.

### Input (CI pipeline)

```bash
/visual-regression-test verify \
  --name LEVEL_Day1_MainHall \
  --tag default \
  --fail-on regression
```

### What happens

1. Manifest load, re-capture, dual Read — same as Example 2.
2. **Claude Vision judgment**:
   > "The floor in the current frame has a fundamentally different diffuse color (purple instead of grey). This is not a lighting drift — the material itself has changed. This cannot be explained by any reasonable interpretation of the declared tolerance."
3. **Verdict**: `regression`. Severity: `major`.
4. **Report written**: `Saved/VisualBaselines/reports/2026-04-18T160015_LEVEL_Day1_MainHall_default.md`
5. **.current file retained** (not deleted) so a developer can open it locally and confirm.
6. **Exit code 1** — CI gate fails, merge blocked.

### Report contents

```markdown
# Visual Regression Report

- **Baseline**: `LEVEL_Day1_MainHall/default`
- **Verdict**: `regression`
- **Severity**: `major`
- **Timestamp**: 2026-04-18T16:00:15Z
- **Commit (baseline)**: ca921dd9d
- **Commit (current)**: f8f8b4d1c

## Tolerance (declared at capture time)

> allow AA jitter on mesh edges; allow up to 5% brightness variation from dynamic lighting

## Findings

| Element | Location | Change |
|---|---|---|
| Floor material diffuse | entire floor plane, frame-wide | color shifted from grey (#8a8a8a) to purple (#8a3aff) — outside tolerance |

## Conclusion

The floor material has been replaced. This is an unambiguous visual regression.
Likely cause: a recent merge replaced `M_Stone_Floor` or its master material.

## Suggested next steps

1. Compare the two images side by side:
   - Baseline: `Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png`
   - Current:  `Saved/VisualBaselines/LEVEL_Day1_MainHall/.current_default.png`
2. Bisect recent commits to find the material change.
3. Either revert or, if the new material is intentional, re-capture the baseline
   with `/visual-regression-test capture --name LEVEL_Day1_MainHall --tag default`.
```

### Output

```text
VERIFIED: LEVEL_Day1_MainHall/default — REGRESSION
  Baseline:    Saved/VisualBaselines/LEVEL_Day1_MainHall/default.png (commit ca921dd9d)
  Current:     Saved/VisualBaselines/LEVEL_Day1_MainHall/.current_default.png (commit f8f8b4d1c) — RETAINED
  Verdict:     regression
  Severity:    major
  Findings:    1 (material color change)
  Report:      Saved/VisualBaselines/reports/2026-04-18T160015_LEVEL_Day1_MainHall_default.md

Exit code: 1
```

CI sees exit 1 and blocks the merge. The developer reads the report, inspects both PNGs locally, reverts the offending commit, and re-runs verify to confirm the green light.

---

## Bonus: multi-variant baselines

A single `--name` can hold many tagged variants. Useful when the same level has multiple lighting presets, UI states, or camera angles worth locking down:

```bash
# Default lighting (day)
/visual-regression-test capture --name LEVEL_Day1_MainHall --tag default --tolerance "..."

# Night lighting preset
/visual-regression-test capture --name LEVEL_Day1_MainHall --tag night --tolerance "allow lightmap noise; strict on UI overlay position"

# Fog-heavy weather preset
/visual-regression-test capture --name LEVEL_Day1_MainHall --tag foggy --tolerance "allow fog density ±10%; strict on geometry silhouettes"
```

`list` then shows all three:

```bash
/visual-regression-test list --name LEVEL_Day1_MainHall
```

```text
LEVEL_Day1_MainHall (3 variants)
  tag       captured_at           commit      resolution   tolerance
  default   2026-04-18T14:32:01Z  ca921dd9d   1920x1080    allow AA jitter on mesh edges; ...
  night     2026-04-18T14:40:10Z  ca921dd9d   1920x1080    allow lightmap noise; strict on UI overlay ...
  foggy     2026-04-18T14:45:33Z  ca921dd9d   1920x1080    allow fog density ±10%; strict on geometry ...
```

CI verifies each variant independently — any one failing blocks the merge.

---

## Example 5: Delete — single variant and full baseline

**Scenario**: The `night` variant of `LEVEL_Day1_MainHall` no longer reflects the intended lighting (the art team retired the night preset). Later, the entire `UI_OldMainMenu` baseline becomes obsolete and should be removed.

### Input — delete a single variant

```bash
/visual-regression-test delete \
  --name LEVEL_Day1_MainHall \
  --tag night
```

### What happens

1. **Slug validation**: both slugs match `^[A-Za-z0-9_-]{1,64}$`.
2. **Manifest load**: reads `Saved/VisualBaselines/LEVEL_Day1_MainHall/manifest.json`, confirms `variants.night` exists.
3. **AskUserQuestion confirmation** (required — destructive):
   > Delete `LEVEL_Day1_MainHall/night`? Removes `night.png`, `night.png.bak`, and the manifest entry. No undo. [yes / no]
4. User confirms `yes`.
5. **File removal**: `Saved/VisualBaselines/LEVEL_Day1_MainHall/night.png` and `night.png.bak` are unlinked.
6. **Manifest update**: `variants.night` entry is removed. Other variants (`default`, `foggy`) remain untouched.
7. **No entire-directory removal**: the name directory still holds 2 variants, so it stays.

### Output

```text
DELETED: LEVEL_Day1_MainHall/night
  Removed:     Saved/VisualBaselines/LEVEL_Day1_MainHall/night.png (4.1 MB)
               Saved/VisualBaselines/LEVEL_Day1_MainHall/night.png.bak (4.0 MB)
  Manifest:    variants.night entry removed
  Remaining:   LEVEL_Day1_MainHall still has 2 variants (default, foggy)
```

---

### Input — delete an entire baseline (no `--tag`)

```bash
/visual-regression-test delete --name UI_OldMainMenu
```

### What happens

1. **Manifest load**: reads `Saved/VisualBaselines/UI_OldMainMenu/manifest.json`.
2. **AskUserQuestion confirmation** (required — destructive, wider scope):
   > Delete the entire `UI_OldMainMenu` baseline? This removes the directory and all variants. No undo. [yes / no]
3. User confirms `yes`.
4. **Directory removal**: `Saved/VisualBaselines/UI_OldMainMenu/` is deleted recursively.

### Output

```text
DELETED: UI_OldMainMenu (entire baseline)
  Removed:     Saved/VisualBaselines/UI_OldMainMenu/ (directory with 1 variant, 4.3 MB total)
  Manifest:    (removed with directory)
```

> Reports in `Saved/VisualBaselines/reports/` are **not** deleted — they are historical artifacts of past verifies, orthogonal to baseline lifecycle. Clean them up manually or via a scheduled retention job.
