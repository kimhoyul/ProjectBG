# UE Asset Audit — Reference

Companion to [SKILL.md](SKILL.md). Rule catalog, scoring, and MCP call shapes for the mesh/texture health rollup. Issue #20648 (depends on validate_assets #20647).

---

## validate_assets rule catalog (#20647)

`ue_search_assets(operation="validate_assets")` evaluates fixed rules over **already-extracted** texture/mesh metadata (warm cache or inline `assets[]` fixtures). **No new binary parsing.**

| Family | rule_id | severity | catches |
|--------|---------|----------|---------|
| Texture | `texture_power_of_two` | warning | non-power-of-two size_x/size_y |
| Texture | `texture_oversized_4k` | warning | max dim > 4096 |
| Texture | `texture_masks_srgb` | error | Masks/Normalmap compression with sRGB=true |
| Texture | `texture_neverstream_large` | warning | NeverStream on ≥1024px texture |
| Texture | `texture_single_mip_large` | info | ≥512px texture with only 1 mip |
| Mesh | `mesh_no_collision` | warning | StaticMesh has_collision=false |
| Mesh | `mesh_single_lod_large` | warning | large mesh (tri≥50k) with lod_count≤1 |
| Mesh | `mesh_zero_materials` | error | material_slots=0 |
| Mesh | `mesh_missing_lightmap` | info | authoritative StaticMesh lightmap_resolution=0 |
| Mesh | `mesh_nanite_candidate` | info | large StaticMesh without Nanite |

`is_valid` is false iff an evaluated finding severity is `error`. It is `null`
when no authoritative rule ran. Response includes `indeterminate`, `reason`,
`evaluated_rule_count`, `skipped_rule_count`, `checks_performed`, and
`checks_skipped` in addition to `summary`, `findings`, and `source`.

Missing fields and fields listed in `_defaults_applied` skip every rule that
depends on them. Counts are asset-rule pairs; check-name arrays are de-duplicated.
Skipped rules and indeterminate assets never contribute to the health score.

### Canonical call (copy-paste safe)

```python
ue_search_assets(
    operation="validate_assets",
    asset_type="all",
    path="/Game",
    limit=500,
)
```

**Do not** wrap tool kwargs in an empty params dict — that is the MT1 anti-pattern this skill's smoke tests guard against.

Inline fixture form (tests / CI without a warm project cache):

```python
ue_search_assets(
    operation="validate_assets",
    assets=[
        {
            "asset_path": "/Game/T_Example",
            "asset_type": "Texture2D",
            "meta": {
                "size_x": 100,
                "size_y": 128,
                "srgb": True,
                "compression_settings": "TC_Normalmap",
                "mip_count": 1,
            },
        }
    ],
)
```

---

## Inventory search shapes

```python
ue_search_assets(operation="search", asset_type="Texture", path="/Game", limit=200)
ue_search_assets(operation="search", asset_type="StaticMesh", path="/Game", limit=200)
ue_search_assets(operation="search", asset_type="SkeletalMesh", path="/Game", limit=200)
```

When `total_found` > returned count, re-call with a higher `limit`. Never present a truncated inventory as complete.

Budget fields (when present on cache rows / extractor meta):

| field | family | use |
|-------|--------|-----|
| size_x / size_y | Texture | max-dim ranking, PoT |
| compression_settings / srgb / never_stream / mip_count | Texture | rule corroboration |
| triangle_count / vertex_count | Mesh | heavy-mesh ranking |
| lod_count / material_slots / has_collision / nanite_enabled | Mesh | rule corroboration |

---

## Scoring & prioritization

The scoring gate is `indeterminate=false` and a non-null `is_valid`. Any
indeterminate reason (`cache_cold`, `no_matching_assets`, or
`no_evaluated_rules`) makes the health score N/A; skipped/defaulted rules do not
enter either the numerator or denominator.

- **Health score** = fraction of evaluated assets with zero **error** findings (from `validate_assets`). If `source=cache_cold` or validator unavailable → score **N/A** (never invent 100%).
- **Worst offenders**: group findings by `asset_path`, sort by (error count desc, warning count desc), take `--top N` (default 10).
- **Per-rule histogram**: count findings and distinct assets per `rule_id`.
- **Same-name collisions**: basename collision across Texture/StaticMesh/SkeletalMesh inventories (info; not a validate_assets rule).
- **Unreferenced candidates**: `get_ref_counts` incoming == 0 (info; cap sample size).

---

## Composition

| Skill | Role |
|-------|------|
| `/ue-asset-audit` (this) | project mesh/texture health rollup |
| `/ue-asset-discovery` | single-asset refs/deps/circular |
| `/ue-data-audit` | DataTable/CurveTable/ChooserTable |
| `/ue-physics-audit` | PhysicsAsset batch |
| `/ue-audit` | multi-domain project health |

---

## Degrade matrix

| binary / cache state | Phase 1 | Phase 2–3 | report |
|----------------------|---------|-----------|--------|
| post-#20647 + warm cache | full rules | full | health % |
| post-#20647 + indeterminate | explicit reason + nullable validity | inventory may be empty or provenance-skipped | N/A health; follow reason-specific recovery |
| post-#20647 + cold cache | source=cache_cold | inventory may be empty | N/A health, warm instruction |
| pre-#20647 | Unknown operation | inventory + refs | validator N/A |
