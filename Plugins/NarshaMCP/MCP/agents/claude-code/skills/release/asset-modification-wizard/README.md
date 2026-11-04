# Asset Modification Wizard

**Status**: ✅ Production Ready
**Author**: NarshaMCP Development Team

---

## Overview

Safe bulk modification of Blueprint and PCG asset **properties** via the real
**`ue_modify_asset`** tool — an Editor-less binary patch pipeline with dry-run preview, impact
analysis, and automatic `.bak` backup.

---

## Quick Start

**Example Query**:

```text
"모든 BP_Enemy 블루프린트에서 MaxHealth를 100으로 바꿔줘"
"Change MaxHealth to 100 in all BP_Enemy Blueprints"
```

**Auto-Activation**: Keywords like `modify`, `change`, `bulk`, `all blueprints`, `PCG`, `수정`, `변경`

---

## Key Features

- **Offline `.uasset` patch**: `ue_modify_asset` patches the asset binary directly — no Editor required
- **Dry-run by default**: `dry_run=true` previews before any write
- **Automatic `.bak` backup**: adjacent `<asset>.uasset.bak` written before each apply (manual restore to undo)
- **Cross-Domain**: Blueprint + PCG property modification

---

## Workflow

```text
Stage 1: Target Discovery       → Find assets to modify (ue_search_assets / ue_glob)
Stage 2: Impact Analysis        → Assess risks (/ue-impact, ue_analyze_symbols)
Stage 3: Strategy Selection     → ue_modify_asset (offline) vs live-Editor tools
Stage 4: Dry-Run Preview        → ue_modify_asset (dry_run=true, user approval)
Stage 5: Execution & Validation → ue_modify_asset (dry_run=false, .bak backup)
```

---

## Use Cases

1. **Bulk Property Changes**: "모든 블루프린트에서 MaxHealth 100으로"
2. **PCG Parameter Tuning**: "PCG 그래프 Density 0.5로 변경"
3. **Nested Struct Fields**: "Stats.MoveSpeed 값 일괄 변경"
4. **Safe Refactoring**: "영향 범위 확인하고 수정"

---

## Reliability at a Glance

| Path | Reliability |
|------|-------------|
| Flat primitive `exports[0].properties.MaxHealth` | ✅ Reliable |
| Nested struct field `exports[0].properties.Stats.MaxHealth` | ✅ Reliable |
| Array index / map key `Items[2]` / `M["k"]` | 🟡 SpliceZone (refused unless `allow_splice_zone=True`) |

`ue_modify_asset` patches bytes directly (no Editor, no Remote Control round-trip). Bulk work is a
per-asset loop, so wall-clock scales linearly with asset count.

---

## Safety Features

- ✅ `dry_run=true` by default (preview before write)
- ✅ Automatic `<asset>.uasset.bak` backup before every apply
- ✅ Atomic write with durable swap
- ✅ Impact analysis and risk assessment (Stage 2)
- ✅ SpliceZone classification refuses unsafe paths by default

---

## Integration

**Works with**:

- ✅ Caller Graph Visualizer — analyze impact before modification
- ✅ Error Doctor Skill — fix compile errors after modifications
- ✅ Performance Health Check — validate system health

---

## Documentation

- [SKILL.md](SKILL.md) — workflow and auto-activation
- [REFERENCE.md](REFERENCE.md) — full `ue_modify_asset` parameters, response shapes, safety
- [EXAMPLES.md](EXAMPLES.md) — conversation scenarios with real calls
- [ADVANCED.md](ADVANCED.md) — value encoding, batch transactions, backup control

---

## Related

- **Issue #8394**: `write_pipeline` (engine-layer safe-write pipeline)
- **Issue #8395**: `ue_modify_asset` (E8-3.6, property patch API)
- **Issue #9920**: mutation-reliability roadmap
