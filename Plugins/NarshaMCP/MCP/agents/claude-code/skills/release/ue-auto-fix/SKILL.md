---
name: ue-auto-fix
model: sonnet
context: fork
agent: general-purpose
description: "Umbrella entry point for the Epic #8190 auto-fix family. Routes to one of 5 engines via ue_auto_fix(subtool=...): rename (CoreRedirects-driven test rewrites), module_move (Build.cs auto-repair), deletion (function deletion test retargeting + @deprecated_test markers), enum (switch-coverage test auto-generation), harness (apply-then-verify wrapper). subtool=smart inspects classifier batch (changes[]) or subtool-specific keys to auto-route. Activates on: '자동 수정 적용', 'auto fix family', 'apply classifier output', 'test auto-maintenance', 'CoreRedirects 적용'. NOT for compile/runtime test failure recovery (use /ue-test-autofix), NOT for general build error fixing (use /ue-debug). Issue #9571 (Epic #8190 Phase 5)."
compatibility: "Requires NarshaMCP v0.9.13+ with `ue_auto_fix` umbrella registered. Internally dispatches to one of five engines (`tool_auto_fix_{rename,module_move,deletion,enum,harness}`) via the `subtool` parameter — same dry-run semantics, same response shape (wrapped under subtool_results when subtool=smart with heterogeneous changes[]). Issue #9679 (2026-04-26): the legacy top-level aliases (`ue_auto_fix_rename` etc.) were removed from MCP — the umbrella is now the only entry point."
allowed-tools: [Bash, Read]
argument-hint: "[--subtool rename|module_move|deletion|enum|harness] [--operation smart|preview|apply] [--dry-run] [--from-classifier <classify_api_changes JSON path>]"
metadata:
  version: "1.1.0"
  issue: "#9571"
  epic: "#8190"
  phase: "5"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# ue-auto-fix — Auto-Fix Umbrella Entry Point

**Issue**: #9571 (Epic #8190 Phase 5)
**Purpose**: Single, discoverable entry for the auto-fix family. Forwards through `ue_auto_fix` to whichever engine matches the user's intent or the classifier's `change.type`.

---

## When to invoke

Use this skill when the user wants to **apply a classifier-driven test fix** without remembering which sub-tool to call. Typical phrasings:

- "이 분류기 결과를 적용해줘"
- "rename 자동 수정 dry-run 보여줘"
- "enum 변경에 대한 switch coverage 테스트 자동 생성해줘"
- "Build.cs 모듈 이동 자동 수정"
- "함수 삭제 후 테스트 자동 마킹"
- "apply classifier output to tests"

**Do NOT invoke** when:
- The user wants to debug a failing UE Automation test (→ `/ue-test-autofix`).
- The user wants to fix a compile error (→ `/ue-debug` or call `ue_fix_errors` directly).
- The user wants to write new C++ classes (→ `/ue-scaffold`, `/code-generation-power`).

---

## Workflow

The umbrella dispatches inside the MCP server — the skill is a thin guidance + invocation wrapper.

### Step 1: Identify the subtool

User intent → subtool mapping. Engine input keys are listed for reference; **callers must always set `subtool` explicitly when passing `symbols`** (enum/module_move/deletion all consume it and the smart heuristic cannot disambiguate from `symbols` alone).

| Hint from user / args | subtool | Engine input keys |
|---|---|---|
| Mentions "rename", CoreRedirects, `[CoreRedirects]` rewrites | `rename` | `redirect_file` (top-level, **uniquely auto-routed**) |
| Mentions "module move", "Build.cs dependency" | `module_move` | `symbols` (set `subtool="module_move"` explicitly) |
| Mentions "function deletion", "test retarget" | `deletion` | `symbols` (set `subtool="deletion"` explicitly) |
| Mentions "enum", "switch coverage" | `enum` | `symbols` (set `subtool="enum"` explicitly) |
| Mentions "verify", "build harness", apply-then-verify | `harness` | `fix` + `build` (object-shaped, **uniquely auto-routed**) |
| Has classifier batch `changes[]` with multiple types | `smart` (per-item dispatch) | each item carries `type` field |
| Anything ambiguous | Ask the user to pick one | umbrella returns `status: "ambiguous_subtool"` |

### Step 2: Always dry-run first

```python
ue_auto_fix(
    subtool="<chosen>",
    operation="preview",   # or "smart" — both engines treat it as non-destructive
    project_root="<absolute path>",
    dry_run=True,
    # forward subtool-specific args verbatim
)
```

Report the planned edits (file path, line range, snippet) to the user.

### Step 3: Apply only with explicit user confirmation

```python
ue_auto_fix(
    subtool="<chosen>",
    operation="apply",
    project_root="<absolute path>",
    dry_run=False,         # required to write
    confirmed=True,        # required for `deletion` subtool's hard gate
)
```

If the harness subtool is selected, the engine snapshots source, runs UBT, and rolls back on failure automatically — but still surface the build verdict to the user.

### Step 4: Verify

After apply, run the engine's verification op (when supported):
- `harness` → `get_metrics` to confirm the run was recorded
- `rename`/`module_move`/`enum` → optional `cargo build` + targeted test run for the affected module

---

## Subtool quick reference

| subtool | Engine | Ops | Writes |
|---|---|---|---|
| `rename` | `tools/auto_fix_rename` | `smart`, `parse_redirects`, `preview`, `apply` | `.cpp`/`.h` |
| `module_move` | `tools/auto_fix_module_move` | `smart`, `detect`, `preview`, `apply` | `*.Build.cs` |
| `deletion` | `tools/auto_fix_deletion` | `smart`, `detect`, `preview`, `apply` | `@deprecated_test` markers only |
| `enum` | `tools/auto_fix_enum` | `smart`, `detect`, `preview`, `apply` | `<Module>/Tests/<Enum>SwitchCoverageTests.cpp` |
| `harness` | `tools/auto_fix_harness` | `smart`, `run_*_with_verify`, `get_metrics` | source + UBT artifacts (snapshot/rollback) |

`subtool="smart"` reads the umbrella's `changes[]` batch (when present) or hints from **engine-unique** top-level keys: `redirect_file` → rename, `fix`/`build` (object-shaped) → harness. enum, module_move, and deletion all consume `symbols` so they cannot be auto-disambiguated from that key alone — the umbrella returns `status: "ambiguous_subtool"` so the caller knows to set `subtool` explicitly.

---

## Migration note (Issue #9571 → #9679)

The five legacy top-level tools (`ue_auto_fix_rename`, `ue_auto_fix_module_move`, `ue_auto_fix_deletion`, `ue_auto_fix_enum`, `ue_auto_fix_harness`) were demoted to Hidden + whitelisted in #9571, then **removed entirely on 2026-04-26 (#9679)**. Calling them now returns `Unknown tool: ue_auto_fix_<X>`. All callers must use `ue_auto_fix(subtool=...)`.

The `DEPRECATED_HIDDEN_ALIASES` constant in `tools/dispatcher.rs` is preserved (empty) so future umbrella consolidations can reuse the same soak pattern without re-litigating the design.

See [docs/migration/MIGRATION_AUTO_FIX_UMBRELLA_ISSUE_9571.md](../../../docs/migration/MIGRATION_AUTO_FIX_UMBRELLA_ISSUE_9571.md) for the full before/after mapping and the post-removal verification checklist.
