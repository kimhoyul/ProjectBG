# Asset Modification Wizard - Usage Examples

Complete examples and conversation scenarios for Asset Modification Wizard.

> Every property write below is a real **`ue_modify_asset`** (singular) call: flat kwargs,
> `dry_run` defaults to `true`, and each apply writes an adjacent `<asset>.uasset.bak`.
> Bulk = loop one `ue_modify_asset` call per discovered asset.

---

## Example 1: Simple Bulk Property Change

**User**: "모든 BP_Enemy 블루프린트에서 MaxHealth를 100으로 바꿔줘"

**Asset Modification Wizard**:

```text
🔍 Stage 1: Target Discovery
Found 3 Blueprints matching "BP_Enemy*"

📊 Stage 2: Impact Analysis
Risk Level: Low ✅ (Safe to modify)

🔍 Stage 4: Dry-Run Preview (dry_run defaults to true — file untouched)

| Blueprint | Property | Old | New | Class |
|-----------|----------|-----|-----|-------|
| BP_Enemy_Melee | MaxHealth | 50 | 100 | ✅ Reliable |
| BP_Enemy_Ranged | MaxHealth | 30 | 100 | ✅ Reliable |
| BP_Enemy_Boss | MaxHealth | 500 | 100 | ⚠️ Reliable (large decrease) |

Do you want to proceed? (Yes/No)

[User: Yes]
```

Apply — one call per asset, `dry_run=False`:

```python
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Melee",
    property_path="exports[0].properties.MaxHealth",
    new_value=100,
    dry_run=False,
)
```

```text
✅ Stage 5: Execution Complete
Applied: 3/3 assets
Backup: BP_Enemy_Melee.uasset.bak (written next to each original; path in backup_path)
```

---

## Example 2: PCG Parameter Tuning

**User**: "PCG 그래프에서 모든 Seed 값을 1234로 변경해줘"

**Response**:

```text
🔍 Stage 1: Target Discovery
Found 8 PCG graphs

📊 Stage 2: Impact Analysis
Risk Level: Low ✅ (no active instances in level)

🔍 Stage 4: Dry-Run Preview

| PCG Graph | Property | Old | New |
|-----------|----------|-----|-----|
| PCG_Forest | Seed | 42 | 1234 |
| PCG_Rocks | Seed | 100 | 1234 |
... (6 more)

[User: Yes]
```

Apply (per graph):

```python
ue_modify_asset(
    asset_path="/Game/PCG/PCG_Forest",
    property_path="exports[0].properties.Seed",
    new_value=1234,
    dry_run=False,
)
```

```text
✅ Stage 5: Execution Complete
Applied: 8/8 graphs — each with a .uasset.bak backup
```

---

## Example 3: Batch Multiple Properties in One Asset

**User**: "BP_Enemy_Boss의 MaxHealth를 800, 이동속도(Stats.MoveSpeed)를 450으로"

**Response**:

```text
📊 Impact: single asset, 2 fixed-width primitive fields → both Reliable.
🔍 Dry-Run: would_succeed=true, changes=[2 entries], splice_warnings=[]
[User: Yes]
```

Batch apply — both mutations as one all-or-nothing transaction:

```python
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Boss",
    dry_run=False,
    mutations=[
        {"property_path": "exports[0].properties.MaxHealth", "new_value": 800},
        {"property_path": "exports[0].properties.Stats.MoveSpeed", "new_value": {"type": "float", "value": 450.0}},
    ],
)
```

```text
✅ Stage 5: Execution Complete
Applied: BP_Enemy_Boss (2 mutations, 1 transaction)
Backup: BP_Enemy_Boss.uasset.bak
```

> Note the `{"type": "float", "value": 450.0}` discriminator — a bare `450.0` would encode as a
> 64-bit `Double`; use the discriminator to target a 32-bit float `UPROPERTY`.

---

## Example 4: Safe Modification with Impact Check

**User**: "BP_PlayerCharacter 수정해도 안전한지 확인하고 Health 값 바꿔줘"

**Response**:

```text
🔍 Stage 1: Found 1 Blueprint: BP_PlayerCharacter

📊 Stage 2: Impact Analysis (via /ue-impact)
- Referenced by: 23 Blueprints
- Child Blueprints: 3
- Referenced from C++: AMyPlayerController, UHealthComponent
- Risk Level: Medium ⚠️

Recommendation: preview carefully; verify HUD + damage systems after.

[User: Yes]

🔍 Stage 4: Dry-Run Preview
| Blueprint | Property | Old | New | Impact |
|-----------|----------|-----|-----|--------|
| BP_PlayerCharacter | Health | 100 | 150 | ⚠️ Referenced by 23 BPs |

[User: Yes]
```

Apply:

```python
ue_modify_asset(
    asset_path="/Game/Player/BP_PlayerCharacter",
    property_path="exports[0].properties.Health",
    new_value=150,
    dry_run=False,
)
```

```text
✅ Stage 5: Execution Complete
Modified: BP_PlayerCharacter
Backup: BP_PlayerCharacter.uasset.bak

📋 Next Steps:
1. Test player health in gameplay
2. Verify HUD displays correct values
```

---

## Example 5: SpliceZone Path (array index) — Refused Without Opt-In

**User**: "BP_Enemy_Boss의 LootTable[2].Quantity를 5로 바꿔줘"

**Response**:

```text
🔍 Stage 4: Dry-Run Preview
would_succeed: false
changes: [1 entry]
splice_warnings: [
  "exports[0].properties.LootTable[2].Quantity — SpliceZone HIGH (array index;
   generic walker deferred #10532)"
]

⚠️ This path classifies as SpliceZone HIGH. The apply will REFUSE it unless you pass
allow_splice_zone=True (which silently DROPS it, reporting it in dropped_mutations).

Recommended: use the DataTable/CurveTable handlers (#10458/#10459) or UE Editor scripting
for Array<Struct>[K] / Map[K] paths instead.
```

If the user insists after review, the opt-in form is:

```python
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Boss",
    property_path="exports[0].properties.LootTable[2].Quantity",
    new_value=5,
    dry_run=False,
    allow_splice_zone=True,
)
```

---

## 🎓 Tips for Best Results

### Always Dry-Run First

`dry_run` defaults to `true`, so the first call is always a safe preview. Review
`would_succeed`, `changes`, and `splice_warnings`, get user approval, then re-issue with `dry_run=False`.

### Confirm Path Reliability

- **Reliable**: flat primitive `exports[0].properties.MaxHealth` and nested struct fields `exports[0].properties.Stats.MaxHealth`.
- **SpliceZone (refused)**: array indices `Items[2]` and map keys `M["k"]` → use #10458/#10459 or Editor scripting.

### Bulk = Per-Asset Loop

`ue_modify_asset` targets one `asset_path` per call. For "all BP_Enemy" discover with
`ue_search_assets`/`ue_glob`, then loop the same call over each result.

---

## Recovery from Backup (no rollback operation)

There is **no** `rollback` operation. Each apply writes `<asset>.uasset.bak` next to the original
and returns its path as `backup_path`. To undo, copy the `.bak` back:

```text
# Restore from the backup path returned by the apply
Copy-Item "BP_Enemy_Melee.uasset.bak" "BP_Enemy_Melee.uasset" -Force
```

Backups are never auto-deleted; when `.bak` already exists a new backup is written to the next
free suffix (`.bak.1` … `.bak.99`), leaving the older `.bak` in place as the oldest copy.

---

**Note**: Example asset paths are illustrative. Reliability claims track the `ue_modify_asset`
tool contract (Issue #8395 / #10231); array-index and map-key paths remain SpliceZone (#10532).
