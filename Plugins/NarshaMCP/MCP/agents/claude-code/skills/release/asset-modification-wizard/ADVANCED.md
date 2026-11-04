# Asset Modification Wizard - Advanced Features

Advanced usage of the real **`ue_modify_asset`** write pipeline: value encoding, batch
transactions, backup control, SpliceZone handling, and skill integration.

> All property writes go through `ue_modify_asset` (singular, flat kwargs, `dry_run` default
> `true`). See [docs/reference/tools/ue_modify_asset.md](../../../docs/reference/tools/ue_modify_asset.md)
> for the authoritative contract.

---

## ⚙️ Write Surface & Reliability

`ue_modify_asset` is a **thin MCP wrapper over the Issue #8394 `write_pipeline`**. Every call:

1. Resolves `asset_path` (`/Game/...` → `Content/...`; cooked packages refused with a `Parse` error).
2. Parses the `.uasset` and classifies each mutation as **Reliable** or **SpliceZone (HIGH)**.
3. On `dry_run=true` (default) returns the classification only — no bytes touched.
4. On `dry_run=false` runs **backup `.bak` → atomic patch → durable swap**.

**Reliability matrix** (what round-trips safely today):

| Target | Reliability | Notes |
|--------|-------------|-------|
| Fixed-width primitive on flat path `exports[0].properties.MaxHealth` | ✅ Reliable | Int/Int64/Float/Double/UInt32/UInt64/Byte/Object |
| Fixed-width primitive on nested struct field `exports[0].properties.Stats.MaxHealth` | ✅ Reliable | #10231 Phase 1, ≤8 deep |
| Fixed-width primitive field inside a struct/container element | ✅ Reliable | #16962 (count-stable) |
| Bool / FString / FName / FText / primitive Array·Set·Map | ✅ Reliable | #10230 / #10232 / #16576 |
| Array index `Items[2]` / map key `M["k"]` | 🟡 SpliceZone HIGH | generic walker deferred #10532 → refuse unless `allow_splice_zone=True` |
| Count-change / Map-of-struct / variable-width struct field | 🟡 SpliceZone HIGH | evolves with Epic #9920 |

There is **no** Remote Control / WebSocket path and **no** `.uasset` write pseudo-code to hand-roll
— the pipeline is the implementation.

---

## 🔢 Value Encoding (`new_value`)

Bare JSON numbers always produce **signed** variants (`Int` / `Int64` / `Double`). Use the explicit
discriminator object for anything else:

```python
# 32-bit float (bare 600.0 would be a 64-bit Double)
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Boss",
    property_path="exports[0].properties.Stats.MoveSpeed",
    new_value={"type": "float", "value": 600.0},
    dry_run=False,
)
```

| Variant | Encoding |
|---------|----------|
| `Int` / `Int64` | bare integer (in / out of `i32`) |
| `Double` | bare number with a fractional part |
| `Float` | `{"type": "float", "value": N}` |
| `UInt32` / `UInt64` | `{"type": "uint32", "value": N}` / `{"type": "uint64", ...}` |
| `Byte` | `{"type": "byte", "value": 255}` |
| `Object` | `{"type": "object", "index": N}` (FPackageIndex) |
| `Name` | `{"type": "name", "value": "X"}` (opt `"number": N`) — new names grow the NameMap (#17193) |
| `Struct` | `{"type": "struct", "fields": [{"name": "F", "value": {...}}]}` (omit unchanged fields) |
| `Array` / `Set` / `Map` | `{"type": "array", "items": [...]}` / `{"type": "map", "entries": [...]}` (bare arrays rejected) |

---

## 📦 Batch Transactions (`modify_properties`)

`mutations[]` applies many changes to **one** asset as an all-or-nothing transaction. Any
HIGH-severity SpliceZone refuses the **whole** batch unless `allow_splice_zone=True`.

```python
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Boss",
    dry_run=False,
    mutations=[
        {"property_path": "exports[0].properties.MaxHealth", "new_value": 800},
        {"property_path": "exports[0].properties.Armor", "new_value": 30},
        {"property_path": "exports[0].properties.Stats.MoveSpeed", "new_value": {"type": "float", "value": 450.0}},
    ],
)
```

Bulk-across-**assets** is a caller-side loop (one `ue_modify_asset` per `asset_path`); `mutations`
is only for multiple properties within a single asset.

---

## 🛡️ Backup, Locks & SpliceZone Control

### Automatic backup (`.bak`)

Every non-empty apply writes an adjacent `<asset>.uasset.bak` **before** any write, fsync'd for
durability. If `.bak` already exists, the new backup is written to the next free suffix
(`.bak.1`, `.bak.2`, … up to `.bak.99`), leaving the older `.bak` in place as the oldest copy.
The apply's `ApplyReport.backup_path` is the exact file to restore from. An **empty** `mutations` apply
is a no-op and writes **no** backup.

Centralize backups with `backup_dir`:

```python
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Melee",
    property_path="exports[0].properties.MaxHealth",
    new_value=100,
    dry_run=False,
    backup_dir="D:/AssetBackups/2026-07-15",
)
```

### Perforce locks (`allow_locked`)

A Perforce-locked file refuses the write with `error_kind: "LockedByOther"`. After coordinating the
lock, pass `allow_locked=True` to proceed (no effect on Git/SVN).

### SpliceZone opt-in (`allow_splice_zone`)

HIGH-severity SpliceZone mutations (array index, map key, variable-width struct field) refuse the
apply by default. `allow_splice_zone=True` does **not** make them Reliable — it **silently drops**
them and lists each in `dropped_mutations`. Prefer the type-specific DataTable/CurveTable handlers
(#10458 / #10459) or UE Editor scripting for those paths.

---

## 📋 Report Shapes

**DryRunReport** (`dry_run=true`, default):

```json
{ "success": true, "operation": "modify_property", "dry_run": true,
  "would_succeed": true,
  "changes": [ { "path": "exports[0].properties.MaxHealth", "old_value": 80, "new_value": 100 } ],
  "splice_warnings": [], "lock_status": { "kind": "unlocked" }, "method": "write_pipeline.dry_run" }
```

**ApplyReport** (`dry_run=false`):

```json
{ "success": true, "operation": "modify_property", "dry_run": false,
  "bytes_written": 2408192, "backup_path": ".../Asset.uasset.bak", "dropped_mutations": [] }
```

Errors carry `error_kind` ∈ `Parse | Io | PathResolve | LockedByOther | SpliceZoneRefused | Refused | ChainShapeChange`
plus a contextual `hint`. (Asset-resolution failures — bare name not found / ambiguous — are
returned *before* the write pipeline and carry no `error_kind` field.)

---

## 🔗 Integration with Other Skills

### Impact analysis (before modifying)

Run the read-only [Caller Graph Visualizer](../caller-graph-visualizer/SKILL.md) or `/ue-impact`
to see callers / subclasses / config deps before authorizing the apply. This is Stage 2 of the
workflow — modification never happens in the impact step.

### Post-edit compile errors

If a changed default value breaks callers (type mismatch, removed property), route the compile
error to the [Error Doctor](../unreal-error-doctor/SKILL.md) skill. To undo the change entirely,
restore from the `backup_path` the apply returned.

### Post-bulk health

After a large bulk pass, [Performance Health Check](../performance-health-check/SKILL.md) can flag
regressions (e.g. Blueprint bloat from added properties).

---

## 🎯 Advanced Use Cases

### Use Case 1: Mass Default-Value Rebalance

**Goal**: Rebalance `MaxHealth` across 200 enemy Blueprints.

1. Discover with `ue_search_assets(asset_type="blueprint", query="BP_Enemy*")`.
2. Dry-run one `ue_modify_asset` per asset (default `dry_run=true`); collect `splice_warnings`.
3. Present the preview table; get user approval.
4. Loop the same calls with `dry_run=False`; each writes its own `.bak`.

### Use Case 2: PCG Parameter Sweep

**Goal**: Try `Seed`/`Density` sets across PCG graphs.

1. Baseline is implicit — each apply leaves a `<graph>.uasset.bak` you can restore from.
2. Apply parameter set A (per graph), evaluate.
3. To revert, copy each `.bak` back over its `.uasset` (there is no rollback op).
4. Apply parameter set B, compare, keep the best.

### Use Case 3: Struct-Field Tuning

**Goal**: Update a nested struct field (e.g. `Stats.CritChance`) on many Blueprints.

Nested struct primitive fields are Reliable (#10231). Use the float discriminator for 32-bit
fields. Array-of-struct element fields (`Loot[0].Quantity`) are SpliceZone — use #10458/#10459 or
Editor scripting instead.

---

## 📚 Related Documentation

- [ue_modify_asset tool reference](../../../docs/reference/tools/ue_modify_asset.md)
- [Caller Graph Visualizer](../caller-graph-visualizer/SKILL.md) — impact analysis
- [Error Doctor](../unreal-error-doctor/SKILL.md) — auto-fix integration
- [Performance Health Check](../performance-health-check/SKILL.md) — post-modification validation

**Related Issues**: #8394 (write_pipeline) · #8395 (ue_modify_asset) · #10231 (nested paths) · #10532 (array/map walker deferral) · #9920 (reliability roadmap)
