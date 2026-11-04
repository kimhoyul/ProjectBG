---
name: asset-modification-wizard
model: opus
description: "Purpose: safe bulk asset property modification. 5-stage safety workflow (Discovery → Impact → Strategy → Dry-Run → Execute) for Blueprint and PCG assets via the Editor-less ue_modify_asset binary patch tool (dry_run → apply, atomic .bak backup). Not for single-asset analysis (use blueprint-flow or material-analysis). Triggers: 'bulk modify', 'change all Blueprints', 'PCG parameter tuning', 'add property to Blueprints', '일괄 수정', 'BP 대량 변경'."
compatibility: "Requires NarshaMCP MCP server (v0.7.0+) with Unreal Engine 5.x C++ project"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Asset Modification Wizard

---

## Features

Safely modify Blueprint and PCG asset properties in bulk with the real `ue_modify_asset` tool:

- **Offline `.uasset` patch**: `ue_modify_asset` patches the asset binary directly — no Editor required
- **Dry-run by default**: `dry_run=true` previews every change before any write
- **Automatic backup**: an adjacent `<asset>.uasset.bak` is written before each apply
- **Bulk Operations**: loop the call over many discovered assets

**Key Benefits**:

- Editor-less: works with the Editor closed (binary write pipeline, Issue #8394)
- Dry-run preview + user approval before applying
- Atomic write with a mandatory `.bak` backup (manual restore to undo)
- Cross-domain support (Blueprint + PCG)

---

## 🔍 Auto-Load Trigger Phrases

**Bulk modification queries**:

- "모든 블루프린트에서 MaxHealth 기본값 100으로 바꿔줘" / "Change MaxHealth default to 100 in all Blueprints"
- "BP_Enemy*블루프린트에 MovementSpeed 속성 추가해줘" / "Add MovementSpeed property to BP_Enemy* Blueprints"
- "PCG 그래프에서 Seed 값 전부 1234로 변경" / "Change all Seed values to 1234 in PCG graphs"

**Safe modification queries**:

- "이 블루프린트 수정해도 안전해?" / "Is it safe to modify this Blueprint?"
- "영향 범위 확인하고 수정해줘" / "Check impact and modify"
- "미리보기로 먼저 보여줘" / "Show me preview first"

**Property modification queries**:

- "UPROPERTY 기본값 바꿔줘" / "Change UPROPERTY default value"
- "Component 설정 변경해줘" / "Change Component settings"
- "PCG 파라미터 튜닝해줘" / "Tune PCG parameters"

**Keywords**: `modify`, `change`, `update`, `set`, `add property`, `remove`, `bulk`, `all blueprints`, `PCG`, `수정`, `변경`, `추가`, `제거`, `일괄`, `모든 블루프린트`

---

## 🧭 5-Stage Workflow

### Complete Flow Path

```text
Stage 1: Target Discovery       → Find assets to modify
Stage 2: Impact Analysis        → Assess modification impact and risks
Stage 3: Strategy Selection     → Choose fastest execution method
Stage 4: Dry-Run Preview        → Show what will change (user approval)
Stage 5: Execution & Validation → Apply changes and verify success
```markdown

---

### Stage 0: Discovery Pre-Exploration (Issue #6289)

> Before running `ue_search_assets`, use `ue_glob` to quickly confirm target assets exist and estimate scope.

```python
# 0a. Quick asset existence check
ue_glob(pattern="<asset_pattern>", domain="assets", limit=50)
# Returns: matching asset names with types (~50 tokens)
# Example: ue_glob(pattern="BP_Enemy*", domain="assets")

# 0b. If modifying properties, verify property exists
ue_grep(query="<property_name>", domain="assets", limit=10)
# Returns: assets containing the property (~100 tokens)
```text

**Stage 0 Decision Logic**:
- Zero matches → Report "no assets match pattern", STOP
- 1-5 assets → Proceed directly to Stage 4 (Dry-Run), skip impact analysis
- 6+ assets → Full 5-stage workflow
- Property not found in metadata → Warn about potential add-new-property scenario

---

### Stage 1: Target Discovery

**Goal**: Find all assets matching the modification criteria

**Tool**: `ue_search_assets(asset_type="blueprint", query="BP_Enemy*")`

**Output**:

```text
🔍 Stage 1: Target Discovery

Found **3 Blueprints** matching "BP_Enemy*":
- BP_Enemy_Melee (2.3 MB)
- BP_Enemy_Ranged (1.8 MB)
- BP_Enemy_Boss (5.2 MB)

Total size: 9.3 MB
```markdown

---

### Stage 2: Impact Analysis

**Goal**: Assess modification impact, identify risks

**Output**:

```text
📊 Stage 2: Impact Analysis

Modification Impact:
- Direct callers: 12 Blueprints reference these enemies
- Child Blueprints: 5 inherit from these classes
- Risk Level: **Low** ✅

Recommendation: Safe to proceed with caution
```markdown

---

### Stage 3: Strategy Selection

**Goal**: Pick the correct write surface

**Decision Tree**:

```text
Persist a property / default value into the saved asset?
├─ YES → ue_modify_asset (offline binary patch, dry_run → apply) ✅
└─ NO, act on a live/PIE actor → ue_batch_editor_operations / ue_editor_automation
```

**Output**:

```text
⚡ Stage 3: Strategy Selection

Selected: ue_modify_asset (offline .uasset patch)
- No Editor required — patches the asset binary directly
- dry_run=true previews; dry_run=false applies with a .bak backup
```

---

### Stage 4: Dry-Run Preview

**Goal**: Show exactly what will change before applying

**Output**:

```markdown
🔍 Stage 4: Dry-Run Preview

Proposed Changes (Preview Only):

| Blueprint | Property | Old Value | New Value | Status |
|-----------|----------|-----------|-----------|--------|
| BP_Enemy_Melee | MaxHealth | 50 | 100 | ✅ Will change |
| BP_Enemy_Ranged | MaxHealth | 30 | 100 | ✅ Will change |
| BP_Enemy_Boss | MaxHealth | 500 | 100 | ⚠️ Will change |

⚠️ Warnings:
- BP_Enemy_Boss: Significant decrease (500 → 100) may affect gameplay

**Do you want to proceed with these changes?**
```markdown

**User Confirmation Required**: Wait for approval before Stage 5.

---

### Stage 5: Execution & Validation

**Goal**: Apply the reviewed mutation (re-issue the dry-run call with `dry_run=False`)

**Tool Call** (one call per discovered asset):

```python
ue_modify_asset(
    asset_path="/Game/Enemies/BP_Enemy_Melee",
    property_path="exports[0].properties.MaxHealth",
    new_value=100,
    dry_run=False,
)
```

**Output (after user confirms)**:

```text
✅ Stage 5: Execution Complete

Applied: 3/3 assets

ApplyReport (per asset):
- bytes_written: 2408192
- backup_path: .../BP_Enemy_Melee.uasset.bak
- dropped_mutations: []
```

---

## 📊 Performance Notes

`ue_modify_asset` patches the `.uasset` binary directly through the safe-write pipeline
(Issue #8394) — no Editor process and no Remote Control round-trip. Each call parses the target
asset, classifies the mutation (dry-run), and on apply writes a `.bak` + atomic patch. Bulk work
is a per-asset loop, so wall-clock scales linearly with the number of assets. (There is no
"Remote Control API 50-200x" fast path — that was a fabricated claim in the pre-v7.0.0 docs.)

---

## Output Format

```text
=== Asset Modification Report ===

Target: {asset_type} matching "{pattern}"
Discovered: {N} assets

--- Impact Analysis ---
| Asset | Direct Refs | BP Refs | Risk |
|-------|------------|---------|------|
| {name} | {count} | {count} | LOW/MED/HIGH |

--- Strategy ---
Selected: ue_modify_asset (offline .uasset patch)
Reason: {justification}

--- Dry-Run Results (dry_run=true) ---
{N} assets would be modified
splice_warnings: {list or empty}

--- Execution (dry_run=false) ---
Modified: {N}/{total} assets
backup_path: <asset>.uasset.bak per asset
Recovery: copy each .uasset.bak back over its .uasset
```

## Error Recovery

| Error | Cause | Recovery |
|-------|-------|----------|
| `asset not found by bare name '…'` / `ambiguous asset name '…'` (pre-pipeline resolver — **no `error_kind`**) | Bare-name lookup under `Content/` missed or matched multiple assets | Use a `/Game/...` virtual path or absolute path; find the exact name with `ue_search_assets` |
| `error_kind: "Parse"` (cooked package) | `asset_path` points at a cooked package | Use the editor (uncooked) source `.uasset` under `Content/` |
| `error_kind: "PathResolve"` | `property_path` grammar / depth invalid | Fix to `exports[N].properties.<Name>(.<Field>)*` (≤8 deep) |
| `error_kind: "SpliceZoneRefused"` | Array-index / map-key / variable-width mutation | Review `splice_warnings`; use #10458/#10459 handlers or pass `allow_splice_zone=True` after review |
| `error_kind: "LockedByOther"` | Perforce lock held by another user | Coordinate the lock, or pass `allow_locked=True` after review |
| Write failed / bad result | Modified asset breaks references | Restore from the returned `backup_path` (`<asset>.uasset.bak`); inspect refs with `ue_manage_blueprint(get_structure)` |

---

## 📚 Related Files

For detailed information, see:

- **EXAMPLES.md** - Complete usage examples and conversation scenarios
- **ADVANCED.md** - Smart strategy selection, safety features, integration
- **REFERENCE.md** - Complete tool parameters, execution strategies, safety guidelines

---

**Status**: ✅ Production Ready (Issue #151 Phase 4)
**Date**: 2025-10-22
## MCP Tool Examples

```python
# 1. Discover assets to modify
ue_search_assets(asset_type="blueprint", query="BP_*")
# 2. (Optional) inspect structure to find the property path
ue_manage_blueprint(operation="smart", blueprint_name="BP_Player")
# 3. Dry-run the property change (dry_run defaults to true — file untouched)
ue_modify_asset(asset_path="/Game/Player/BP_Player",
                property_path="exports[0].properties.MaxHealth", new_value=100)
# 4. Apply after review (writes BP_Player.uasset.bak + atomic patch)
ue_modify_asset(asset_path="/Game/Player/BP_Player",
                property_path="exports[0].properties.MaxHealth", new_value=100, dry_run=False)
```
