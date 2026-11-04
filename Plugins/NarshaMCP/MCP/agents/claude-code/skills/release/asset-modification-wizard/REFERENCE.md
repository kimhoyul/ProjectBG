# Asset Modification Wizard - Complete Reference

Complete workflow details, tool parameters, and safety guidelines.

> **Real tool**: property writes go through **`ue_modify_asset`** (singular) — the thin MCP
> wrapper over the Issue #8394 `write_pipeline` (dry-run classification → backup `.bak` →
> atomic patch). It is **Editor-less** (parses/patches the `.uasset` binary directly) and
> **defaults to `dry_run=true`**. Full tool doc: [docs/reference/tools/ue_modify_asset.md](../../../docs/reference/tools/ue_modify_asset.md).
>
> There is **no** `ue_modify_assets` (plural) tool, no `/Game/.backup/<timestamp>/` directory,
> and no `create_backup` / `rollback` operation — those were a pre-v7.0.0 Python-MCP surface
> that no longer exists. Recovery is a manual restore from the adjacent `<asset>.uasset.bak`.

---

## Complete Workflow Details

### Stage 1: Target Discovery (Asset Search)

**Purpose**: Find all assets matching the modification criteria.

**Tool Call**:

```python
ue_search_assets(
    asset_type="blueprint",  # or "pcg", "animation", etc.
    query="BP_Enemy*",       # Wildcard pattern
)
```

For a quick existence/scope check first, `ue_glob(pattern="BP_Enemy*", domain="assets")` is
cheaper (~50 tokens).

**Expected Output**:

```json
{
    "results": [
        {
            "name": "BP_Enemy_Melee",
            "path": "/Game/Blueprints/Enemies/BP_Enemy_Melee.uasset",
            "size_mb": 2.3
        }
    ],
    "total_count": 3
}
```

**Response Template**:

```text
🔍 Stage 1: Target Discovery

Found **[N] [Type]** matching "[Pattern]":
- [Asset1] ([Size] MB)
- [Asset2] ([Size] MB)

Proceeding to impact analysis...
```

---

### Stage 2: Impact Analysis (Dependency Check)

**Purpose**: Assess modification impact, identify risks, and check dependencies.

Impact analysis is a **read-only** step using the real symbol/reference tools — there is no
internal `analyze_impact()` helper. Prefer the `/ue-impact` skill (it routes callers, subclasses,
Blueprint references and config deps for you). To query callers directly:

```python
ue_analyze_symbols(
    operation="find_callers",
    function_name="AMyEnemy::TakeDamage",
)
```

**Response Template**:

```text
📊 Stage 2: Impact Analysis

Modification Impact:
- Direct callers: [N] Blueprints reference these assets
- Child Blueprints: [M] inherit from these classes
- Risk Level: **[Low/Medium/High]** [✅/⚠️/❌]

Recommendation: [Safe to proceed / Proceed with caution / High risk]

Proceeding to strategy selection...
```

---

### Stage 3: Strategy Selection

**Purpose**: Pick the correct execution surface for the requested change.

There is **one** primary write surface for persisting property/default-value changes to assets
on disk:

| Surface | Tool | When |
|---------|------|------|
| **Offline `.uasset` patch (primary)** | `ue_modify_asset` | Persist a property / default value into the asset file. Works with the Editor **closed**. Fixed-width primitive fields on flat or nested-struct paths are Reliable. |
| **Live Editor batch (optional)** | `ue_batch_editor_operations` / `ue_editor_automation` | Drive a **running** Editor / PIE session (live actors, transient state). A different surface — it does not replace on-disk asset patching. |

> The offline path is fast because it patches bytes directly (no Editor round-trip). Do **not**
> claim a "Remote Control API 50-200x" speedup — that was fabricated. Pick `ue_modify_asset`
> for asset/default-value edits; reach for the live-Editor tools only when you specifically need
> to act on a running session.

**Decision**:

```text
Do you need to change a saved asset's property/default value?
├─ YES → ue_modify_asset (offline, dry_run → apply)
└─ NO, act on a live/PIE actor → ue_batch_editor_operations / ue_editor_automation
```

---

### Stage 4: Dry-Run Preview (User Approval Required)

**Purpose**: Show exactly what will change before applying — this is a **real** `ue_modify_asset`
call with the default `dry_run=true` (the file is never touched).

**Tool Call** (single mutation, smart auto-routing):

```python
ue_modify_asset(
    asset_path="/Game/Blueprints/Enemies/BP_Enemy_Melee",
    property_path="exports[0].properties.MaxHealth",
    new_value=100,
)
```

**DryRunReport** (returned when `dry_run` is left at its default):

```json
{
    "success": true,
    "operation": "modify_property",
    "asset_path": "/Game/Blueprints/Enemies/BP_Enemy_Melee",
    "dry_run": true,
    "would_succeed": true,
    "changes": [
        { "path": "exports[0].properties.MaxHealth", "old_value": 50, "new_value": 100 }
    ],
    "splice_warnings": [],
    "lock_status": { "kind": "no_vcs" },
    "method": "write_pipeline.dry_run"
}
```

**Response Template**:

```text
🔍 Stage 4: Dry-Run Preview

Proposed Changes (Preview Only - Not Applied Yet):

| Asset | Property | Old Value | New Value | Class |
|-------|----------|-----------|-----------|-------|
| BP_Enemy_Melee | MaxHealth | 50 | 100 | ✅ Reliable |

⚠️ splice_warnings: []

**Do you want to proceed with these changes?**
- "Yes" / "예" → Proceed to Stage 5 (Apply with dry_run=False)
- "No" / "아니오" → Cancel
```

**IMPORTANT**: Wait for user confirmation before proceeding to Stage 5. If a mutation shows up
in `splice_warnings` (SpliceZone HIGH), it will refuse the apply unless the user explicitly
opts in with `allow_splice_zone=True`.

---

### Stage 5: Execution & Validation (After User Approval)

**Purpose**: Apply the reviewed mutation. This is the **same** call with `dry_run=False`, which
runs backup `.bak` → atomic write.

**Tool Call** (bulk = loop one `ue_modify_asset` per discovered asset):

```python
ue_modify_asset(
    asset_path="/Game/Blueprints/Enemies/BP_Enemy_Melee",
    property_path="exports[0].properties.MaxHealth",
    new_value=100,
    dry_run=False,
)
```

**ApplyReport** (returned when `dry_run=False`):

```json
{
    "success": true,
    "operation": "modify_property",
    "asset_path": "/Game/Blueprints/Enemies/BP_Enemy_Melee",
    "dry_run": false,
    "bytes_written": 2408192,
    "backup_path": "C:/Game/Content/Blueprints/Enemies/BP_Enemy_Melee.uasset.bak",
    "dropped_mutations": []
}
```

**Response Template**:

```text
✅ Stage 5: Execution Complete

| Asset | Property | New Value | Status |
|-------|----------|-----------|--------|
| BP_Enemy_Melee | MaxHealth | 100 | ✅ Success |

Execution Summary:
- Applied: [N]/[Total] assets
- Backup: <asset>.uasset.bak written next to each original
- backup_path returned per apply (e.g. .../BP_Enemy_Melee.uasset.bak)

Recovery: to undo, copy each `<asset>.uasset.bak` back over its `.uasset`.
```

---

## Tool Parameter Reference

### `ue_search_assets`

**Purpose**: Find assets matching pattern

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_type` | string | Yes | Asset type (`"blueprint"`, `"pcg"`, etc.) |
| `query` | string | Yes | Search pattern (supports `*` wildcard) |

---

### `ue_modify_asset`

**Purpose**: Property-level patch of an editor `.uasset` (dry-run → apply, atomic `.bak` backup).

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `operation` | string | No | `"smart"` | `smart` (auto-route), `auto` (alias), `modify_property` (single), `modify_properties` (batch) |
| `asset_path` | string | **Yes** | — | `/Game/...` virtual path or absolute path to an uncooked `.uasset` (cooked packages refused) |
| `property_path` | string | single/smart | — | `exports[N].properties.<Name>(.<Field>)*`, max depth 8 |
| `new_value` | any | single/smart | — | See **Value encoding** below |
| `mutations` | array | batch/smart | — | `[{"property_path": ..., "new_value": ...}, ...]` — all-or-nothing |
| `dry_run` | boolean | No | **`true`** | `true` = preview only; `false` = backup + atomic write |
| `allow_splice_zone` | boolean | No | `false` | apply only — silently drop HIGH SpliceZone mutations (reported in `dropped_mutations`) |
| `allow_locked` | boolean | No | `false` | apply only — write despite a Perforce lock |
| `backup_dir` | string | No | — | apply only — relocate the `.bak` (default: next to the asset) |

**Call shape is flat kwargs** — there is **no `params={...}` wrapper**; a wrapped call silently
drops every argument.

**Batch example** (single transaction, flat + nested-float in one call):

```python
ue_modify_asset(
    asset_path="/Game/Blueprints/Enemies/BP_Enemy_Melee",
    dry_run=False,
    mutations=[
        {"property_path": "exports[0].properties.MaxHealth", "new_value": 100},
        {"property_path": "exports[0].properties.Stats.MoveSpeed", "new_value": {"type": "float", "value": 600.0}},
    ],
)
```

---

## Value encoding (`new_value`)

Bare JSON numbers always produce **signed** variants. Use the explicit discriminator object for
everything else:

| FPropertyValue variant | JSON encoding |
|------------------------|---------------|
| `Int` | plain integer within `i32` (default for integer literals) |
| `Int64` | plain integer outside `i32` |
| `Double` | plain JSON number with a fractional part |
| `Float` (32-bit) | `{"type": "float", "value": 3.14}` |
| `UInt32` / `UInt64` | `{"type": "uint32", "value": 12345}` / `{"type": "uint64", ...}` |
| `Byte` | `{"type": "byte", "value": 255}` |
| `Object` | `{"type": "object", "index": <package_index>}` |
| `Name` / `FName` | `{"type": "name", "value": "X"}` (opt `"number": N`) |
| `Struct` | `{"type": "struct", "fields": [{"name": "F", "value": {...}}]}` |
| `Array` / `Set` / `Map` | `{"type": "array", "items": [...]}` / `{"type": "map", "entries": [...]}` (bare arrays rejected) |
| `Bool` / `String` | `true` / `false` / `"text"` (accepted directly) |

---

## Path grammar & SpliceZone

```text
property_path ::= "exports[" digits "].properties." segment+
segment       ::= "." identifier | "[" digits "]" | "[" '"' string '"' "]"
```

| Path | Reliability |
|------|-------------|
| `exports[0].properties.MaxHealth` | ✅ Reliable (flat top-level) |
| `exports[0].properties.Stats.MaxHealth` | ✅ Reliable (nested struct field, ≤8 deep) |
| `exports[0].properties.Items[2]` | 🟡 SpliceZone (HIGH) — generic array-index walker **deferred** (#10532) |
| `exports[0].properties.M["k1"]` | 🟡 SpliceZone (HIGH) — generic map-key walker **deferred** (#10532) |

SpliceZone mutations refuse the apply unless `allow_splice_zone=True` (which silently **drops**
them, reporting each in `dropped_mutations`). For tabular game-balance data prefer the
type-specific DataTable/CurveTable handlers (#10458 / #10459) or UE Editor scripting.

---

## Safety Guidelines

### Pre-Modification Safety Checklist

- [ ] **Dry-run first**: `dry_run` defaults to `true` — review `would_succeed` + `changes` + `splice_warnings` before applying.
- [ ] **Impact analyzed**: `ue_analyze_symbols(find_callers)` / `/ue-impact`.
- [ ] **User approval obtained**: wait for "Yes" before the `dry_run=False` apply.
- [ ] **Path reliability confirmed**: flat / nested-struct primitive = Reliable; array-index / map-key = SpliceZone.

### Execution Safety (built into `ue_modify_asset` apply)

- [ ] **Automatic backup**: `<asset>.uasset.bak` written before any write (`.bak.1`…`.bak.99` collision rotation, fsync durability).
- [ ] **Atomic write**: temp-file + atomic rename; on failure the original swap file is restored.
- [ ] **Batch = all-or-nothing**: any HIGH SpliceZone refuses the whole `mutations` batch unless `allow_splice_zone=True`.
- [ ] **VCS aware**: Perforce-locked files refuse the write unless `allow_locked=True`.

---

## Recovery from Backup (there is no rollback operation)

`ue_modify_asset` has **no** `rollback` operation. Every non-empty apply writes an adjacent
`<asset>.uasset.bak` sibling and returns its path as `backup_path` in the ApplyReport. To undo a
change, copy the `.bak` back over the original `.uasset`:

```text
# Windows (PowerShell) — restore from the backup returned in backup_path
Copy-Item "BP_Enemy_Melee.uasset.bak" "BP_Enemy_Melee.uasset" -Force
```

Backups are not auto-deleted. If `.bak` already exists, the new backup is written to the next
free suffix (`.bak.1`, `.bak.2`, … up to `.bak.99`), leaving the older `.bak` in place as the
oldest copy. Use `backup_dir` on apply to centralize backups in one directory.

---

## External Links

- [ue_modify_asset tool reference](../../../docs/reference/tools/ue_modify_asset.md)
- [Blueprint Scripting](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-visual-scripting-in-unreal-engine)
- [PCG (Procedural Content Generation)](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview)

**Related Issues**:

- [#8394 — write_pipeline (engine layer)](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/8394)
- [#8395 — ue_modify_asset (E8-3.6)](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/8395)
- [#9920 — mutation-reliability roadmap](https://github.com/Next-Stage-Inc/ue-code-mcp/issues/9920)
