# visual-regression-test — Reference

**Version**: 1.0.0
**Issue**: #7927
**See also**: [SKILL.md](SKILL.md) | [EXAMPLES.md](EXAMPLES.md)

---

## Directory layout

```
<project-root>/
└── Saved/
    └── VisualBaselines/
        ├── reports/                                # transient, gitignored
        │   └── 2026-04-18T143201_LEVEL_Day1_MainHall_default.md
        ├── LEVEL_Day1_MainHall/
        │   ├── manifest.json
        │   ├── default.png
        │   ├── default.png.bak                     # one-generation backup
        │   ├── night.png
        │   └── .current_default.png                # temp, kept only on non-match
        └── UI_MainMenu/
            ├── manifest.json
            └── default.png
```

- One directory per `--name`; one PNG per `--tag` + one manifest entry per `--tag`.
- `reports/` is gitignored by default. Golden `*.png` and `manifest.json` are **not** gitignored — users decide whether to commit them (or use Git LFS).

---

## Manifest schema

### Version 1

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
      "purpose": "Main hall baseline with default lighting"
    },
    "night": {
      "image": "night.png",
      "captured_at": "2026-04-18T14:40:10Z",
      "commit_sha": "ca921dd9d",
      "tolerance": "allow lightmap noise; strict on UI overlay position",
      "resolution": "1920x1080",
      "format": "png",
      "capture_target": "viewport",
      "purpose": "Main hall with night-lighting preset"
    }
  }
}
```

### Field reference

| Field | Type | Required | Notes |
|---|---|---|---|
| `version` | integer | yes | Schema version. `1` for this release. Future migrations detect on this. |
| `name` | string | yes | Must match the enclosing directory name. |
| `variants` | object | yes | Map from `tag` → variant spec. |
| `variants.{tag}.image` | string | yes | Filename relative to the name directory. |
| `variants.{tag}.captured_at` | string | yes | ISO-8601 UTC. |
| `variants.{tag}.commit_sha` | string | yes | Short SHA from `git rev-parse --short HEAD`, or `"unknown"` if unavailable. |
| `variants.{tag}.tolerance` | string | yes | Free-form English. Passed to Claude Vision as judgment guideline. |
| `variants.{tag}.resolution` | string | yes | `WIDTHxHEIGHT` — used to re-capture on verify. |
| `variants.{tag}.format` | string | yes | `png` (default) or `jpeg`. |
| `variants.{tag}.capture_target` | string | yes | `viewport` (default) or `pie`. |
| `variants.{tag}.purpose` | string | no | Human-readable description of what the baseline represents. |

### Slug constraints

`name` (directory key) and `tag` (variants key) MUST satisfy:

```
^[A-Za-z0-9_-]{1,64}$
```

Any path-unsafe input (`..`, `/`, `\`, whitespace, length > 64) is rejected at argument parsing time.

---

## CLI reference

### `capture`

Save a new golden image and upsert the manifest entry for `{name}/{tag}`.

```
/visual-regression-test capture --name <id> --tag <variant> [--purpose "..."] [--tolerance "..."]
```

| Arg | Required | Default | Description |
|---|---|---|---|
| `--name` | yes | — | Baseline identifier (slug). |
| `--tag` | yes | — | Variant identifier (slug). Use `default` for single-variant baselines. |
| `--purpose` | no | empty | Free-form description, stored in manifest. |
| `--tolerance` | no | `"strict: no visible differences"` | Natural-language judgment guideline for `verify`. |

**Destructive behaviour**: if `{name}/{tag}` already exists, `AskUserQuestion` confirmation is required before overwriting. The previous PNG is moved to `{tag}.png.bak` (one-generation retention).

### `verify`

Capture the current render and compare against the golden image.

```
/visual-regression-test verify --name <id> --tag <variant> [--fail-on regression|change]
```

| Arg | Required | Default | Description |
|---|---|---|---|
| `--name` | yes | — | Baseline identifier. Must exist. |
| `--tag` | yes | — | Variant identifier. Must exist in manifest. |
| `--fail-on` | no | `regression` | `regression` = exit 1 only on true regression (default); `change` = exit 1 on any non-`match` verdict. |

**Exit codes**:
- `0` — verdict is `match` or `match_within_tolerance` (with `--fail-on=regression`).
- `1` — verdict is `regression` (any `--fail-on` value), or any non-`match` verdict with `--fail-on=change`.

**Report output** on non-match: `Saved/VisualBaselines/reports/{YYYY-MM-DDTHHMMSS}_{name}_{tag}.md`.

### `list`

Enumerate all baselines, or drill into a single one.

```
/visual-regression-test list [--name <id>]
```

- Without `--name`: tabular summary of every `{name}/` (name, variant count, last captured, resolution).
- With `--name`: per-variant detail rows (tag, captured_at, commit_sha, resolution, tolerance, purpose).

### `delete`

Remove a single variant, or an entire baseline.

```
/visual-regression-test delete --name <id> [--tag <variant>]
```

- With `--tag`: removes `{tag}.png`, `{tag}.png.bak`, and the `variants[tag]` manifest entry. If the manifest ends up with zero variants, the whole `{name}/` directory is removed.
- Without `--tag`: removes the entire `{name}/` directory after confirmation.

**Always confirms via `AskUserQuestion` before deleting. No undo.**

---

## Comparison verdicts

The comparison is executed by Claude Vision after `Read()`ing both images.

| Verdict | Severity | Meaning | Typical cause |
|---|---|---|---|
| `match` | `none` | Visually identical. | No change. |
| `match_within_tolerance` | `minor` | Differences present but fall inside the tolerance guideline. | AA jitter, minor lighting drift, dynamic shadows. |
| `regression` | `major` | Unambiguous visual breakage. | Broken material, missing actor, wrong color, layout shift. |

The verdict and findings list are written into the report on non-match. Findings are a list of `{element, location, change}` entries — element = what visual thing changed (e.g., "floor material"), location = where in the frame (e.g., "bottom-right quadrant"), change = how it changed (e.g., "diffuse color shifted from grey to purple").

---

## Tolerance string patterns

Tolerance is free-form English but concrete patterns work best. Mix and match:

- **Dynamic lighting**: `"allow up to 10% brightness variation from dynamic lighting"`
- **AA/temporal**: `"allow AA jitter on mesh edges"`, `"ignore 1-pixel sub-pixel differences"`
- **Post-processing**: `"allow bloom intensity variance"`, `"ignore auto-exposure differences"`
- **Strict regions**: `"strict on UI overlay position; permissive on background actors"`
- **Character pose**: `"allow idle animation frame drift"`
- **Strict baseline**: `"strict: no visible differences"` — for deterministic render tests

See [EXAMPLES.md](EXAMPLES.md) for end-to-end walkthroughs using these patterns.

---

## Environment preconditions

- Unreal Editor running (for `capture`/`verify`).
- NarshaMCP daemon connected — verify with `ue_check_health(compact=true)`.
- Target level loaded; for PIE-mode baselines, PIE is either active or will be started by `auto_capture_analyze` (see the `start_pie` parameter documented in [docs/reference/tools/ue_editor_debug.md](../../../docs/reference/tools/ue_editor_debug.md)).
- Write permission on `Saved/VisualBaselines/`.
- `git` on `PATH` for `commit_sha` provenance (falls back to `"unknown"` if not a git repo).

---

## Interaction with other skills

| Skill | Relationship |
|---|---|
| [screenshot-vision-analyzer](../screenshot-vision-analyzer/SKILL.md) | Sibling; ad-hoc screenshot + analysis without a baseline. `visual-regression-test` adds the baseline + compare cycle. |
| [ue-test-gen](../ue-test-gen/SKILL.md) | Complementary; logic regression tests. Use `visual-regression-test` when the signal is visual (UI, rendering, materials) and `ue-test-gen` when the signal is functional. |
| [ue-pre-commit](../ue-pre-commit/SKILL.md) | Compose: run `verify` from a pre-commit hook on known-sensitive baselines. |
| [ci-verify](../ci-verify/SKILL.md) | CI integration path; use exit code from `verify --fail-on=regression` as a gate. |

---

## Failure modes & remedies

| Symptom | Likely cause | Remedy |
|---|---|---|
| `ERROR: --name/--tag must match ...` | Slug violation | Use only `[A-Za-z0-9_-]`, ≤64 chars. |
| `ERROR: no baseline for {name}/{tag}` | `verify` before `capture` | Run `capture` first. |
| Resolution mismatch warning | Editor viewport resized since baseline | Resize editor to match, or re-capture baseline. |
| `No active editor viewport` | Editor not running | Launch Editor, open target level. |
| Manifest JSON parse error | Hand-edited JSON broken | Restore from backup, fix syntax, or re-capture. |
| False-positive regression on AA | Tolerance too strict | Add `"allow AA jitter on mesh edges"` to tolerance. |
| False-negative (missed regression) | Tolerance too permissive, or Claude Vision blind spot | Tighten tolerance; capture additional angles as extra tags; document in EXAMPLES.md. |
| Report directory missing | `reports/` not created | Skill creates on first non-match verify; ensure write permission. |

---

## Known limitations

- **Cross-machine variance**: Different GPU / driver / sample count can produce different frames. Always capture and verify on the same machine configuration, or accept higher tolerance.
- **Non-deterministic frames**: Games with random systems (particles, wind, crowds) benefit from multiple variants or a "dynamic scene" tolerance note.
- **Claude Vision blind spots**: Very small UI regressions (single-pixel icon shifts) may be missed. Cover those with targeted high-zoom tags.
- **Single-user assumption**: Manifest is not concurrency-safe. Avoid two sessions writing to the same baseline simultaneously.
