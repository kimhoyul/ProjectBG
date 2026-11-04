---
name: ue-diff
model: opus
description: "Purpose: Cross-check and mismatch detection for UE projects. Finds gaps between declarations and registrations, class hierarchy deltas, config override cascades, and binary asset diffs. Triggers: 'replication audit', 'config diff', 'config chain', 'diff hierarchy', 'class delta', 'compare configs', 'override cascade', 'compare assets', 'asset diff', 'uasset diff', '리플리케이션 감사', '설정 비교', '계층 비교', '클래스 차이', '에셋 비교', '에셋 차이'."
compatibility: "Requires NarshaMCP MCP server (v0.9.7+) with Unreal Engine 5.x C++ project"
argument-hint: "TargetName, ConfigFiles, or AssetPaths (e.g., 'ALyraPlayerState', 'DefaultEngine.ini vs WindowsEngine.ini', 'ALyraCharacter vs ACharacter', 'BP_Player_v1.uasset vs BP_Player_v2.uasset')"
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE Diff — Cross-Check & Mismatch Detection

**Issue**: #7315, #7422, #28549
**Purpose**: Find what's MISSING — gaps between declarations and registrations, hierarchy deltas, config override cascades, binary asset diffs

## Supported Operations (23)

`smart`, `auto`, `replication_audit`, `diff_hierarchy`, `compare_configs`, `compare_config_chain`, `compare_assets`, `extract_asset`, `render_blueprintue`, `compare_revision`, `verify_identical`, `merge_preview`, `bisect_asset`, `diff_asset_revision`, `snapshot`, `compare_to_snapshot`, `compare_folders`, `diff_level`, `diff_datatable`, `compare_cooked_builds`, `export_changeset`, `compare_collections`, `open_visual_diff`

`auto` is an exact alias of `smart`.

---

## Auto-Execution Instructions

> **CRITICAL**: When this skill is loaded, Claude **MUST execute the analysis automatically**.
> Do NOT just display documentation - actually run the MCP tools!

### Execution Requirements

1. **Parse `$ARGUMENTS`** to determine operation and target
2. **Auto-route** based on input pattern (see routing table below)
3. **Execute** the appropriate `ue_diff` operation
4. **Generate report** with findings

> If `$ARGUMENTS` is empty, ask the user what they want to diff/audit.

---

## CRITICAL RULES — Call Count Minimization

> **MANDATORY**: This skill MUST complete in **2-4 MCP/tool calls** (Bash excluded).
> Optimal: 2 calls. Maximum: 4 calls.

| # | Rule |
|---|------|
| 1 | **ToolSearch 1x only** — fetch `ue_diff` schema once at start |
| 2 | **smart mode first** — let `ue_diff(smart)` auto-route when possible |
| 3 | **No redundant exploration** — `ue_diff` already handles file discovery internally |
| 4 | **compare_config_chain for 3+ files** — don't call compare_configs repeatedly |

### Target Call Sequences

```text
Optimal (2 calls):                    Standard (3 calls):
1. ToolSearch (1x)                    1. ToolSearch (1x)
2. ue_diff(smart, ...)               2. ue_grep (context, optional)
                                      3. ue_diff(operation, ...)
```

---

## Input Pattern Routing

| Input Pattern | Operation | Example |
|---|---|---|
| Two .uasset file paths or asset names | `compare_assets` | "BP_Player_v1.uasset vs BP_Player_v2.uasset" |
| "asset diff" / "에셋 비교" keyword | `compare_assets` | "NS_Confetti vs NS_Explosion 에셋 비교" |
| Class name only (Actor/Character/PlayerState/GameState/Controller/Pawn/GameMode/PlayerController/ReplicatedComponent) | `replication_audit` | "ALyraPlayerState" |
| Class name + "vs" + base class | `diff_hierarchy` | "ALyraCharacter vs ACharacter" |
| Class name + base_class keyword | `diff_hierarchy` | "ALyraCharacter 계층 비교" |
| Two config file names | `compare_configs` | "DefaultEngine.ini vs WindowsEngine.ini" |
| Three+ config file names or "chain"/"cascade" keyword | `compare_config_chain` | "DefaultEngine → Windows → Editor 설정 체인" |
| One asset + "extract"/"metadata" keyword | `extract_asset` (explicit) | "BP_Player typed metadata 추출" |
| Absolute `.uasset` + "BlueprintUE render" keyword | `render_blueprintue` (explicit) | "이 BP 파일을 deterministic BlueprintUE HTML로 렌더" |
| "identical"/"same" or identity-check keyword | `verify_identical` | "이 두 에셋이 byte/exports까지 동일한가?" |
| Base + ours + theirs / "merge preview" | `merge_preview` | "BP_Player 3-way merge preview" |
| Asset + known-good/known-bad revisions | `bisect_asset` | "이 에셋 회귀를 도입한 Git commit 찾기" |
| One working-tree asset + revision | `diff_asset_revision` | "BP_Player를 HEAD와 비교" |
| Snapshot/baseline + scope | `snapshot` | "Content/Characters 기준선 저장" |
| Snapshot label without a scope | `compare_to_snapshot` | "pre_session 이후 변경 비교" |
| Two directories/folders | `compare_folders` | "두 빌드의 Content 폴더 비교" |
| Two `.umap` / level references | `diff_level` | "L_Expanse old vs new" |
| Ambiguous class name (no replication hint) | `diff_hierarchy` (auto-detect base) | "ULyraExperienceManagerComponent" |

### Parsing Rules

1. **Extract target names** from `$ARGUMENTS`:
   - Split on "vs", "와", "비교", "compared to"
   - Config files: match `*.ini` patterns
   - Classes: match `A`, `U`, `F` prefixed names
2. **Detect operation keywords**:
   - "asset", "uasset", "에셋 비교", "에셋 차이", ".uasset" → `compare_assets`
   - "replication", "replicated", "DOREPLIFETIME", "push model" → `replication_audit`
   - "hierarchy", "delta", "override", "계층", "차이" → `diff_hierarchy`
   - "config", "ini", "설정" → `compare_configs` or `compare_config_chain`
   - "chain", "cascade", "체인", "3개 이상 파일" → `compare_config_chain`
   - "identity", "identical", "same", "동일" → `verify_identical`
   - "merge", "ours", "theirs", "병합" → `merge_preview`
   - "bisect", "known-good", "known-bad", "회귀 커밋" → `bisect_asset`
   - "snapshot", "baseline", "기준선" → `snapshot` or `compare_to_snapshot`
   - "folder", "directory", "폴더", "디렉터리" → `compare_folders`
   - "level", "umap", "World Partition", "레벨" → `diff_level`
   - "BlueprintUE", "exact-file render", "deterministic HTML" → `render_blueprintue` (explicit only)

---

## Operation Details

### 1. replication_audit

**Purpose**: Find declared-but-not-registered UPROPERTY(Replicated) and push model coverage gaps.

```python
ue_diff(operation="replication_audit", class_name="ALyraPlayerState")
```

**Fail-closed result handling**: Before rendering the report, require both
`success == true` and `validation_complete == true`. If either field is not
true, report the audit as incomplete/error (including the returned error and
resolved file paths when available); never interpret empty mismatch arrays or
zero totals as a clean audit.

**Report Format**:
```text
## Replication Audit: ALyraPlayerState

**Declared**: 6 replicated properties
**Registered**: 6 DOREPLIFETIME entries
**Mismatches**: 0

### Push Model Coverage: 5/6 (83%)
| Property | Push Model |
|----------|-----------|
| MyTeamID | Applied |
| PawnData | Applied |
| ...      | ...     |
| StatTags | MISSING  |

**Recommendation**: Add MARK_PROPERTY_DIRTY for StatTags to enable push model replication.
```

### 2. diff_hierarchy

**Purpose**: Show what a project class adds/changes vs its engine base class.

```python
ue_diff(operation="diff_hierarchy", class_name="ALyraCharacter", base_class="ACharacter")
```

**Report Format**:
```text
## Hierarchy Diff: ALyraCharacter vs ACharacter

**Added Methods** (50): DestroyDueToDeath, GetAbilitySystemComponent, ...
**Overridden Methods** (9): BeginPlay, PossessedBy, OnMovementModeChanged, ...
**Added Properties** (8): CameraComponent, HealthComponent, PawnExtComponent, ...
**Interface Changes**: +IAbilitySystemInterface, +IGameplayCueInterface, +IGameplayTagAssetInterface, +ILyraTeamAgentInterface
**Child Classes**: 1
```

### 3. compare_configs

**Purpose**: Diff two config files section by section.

```python
ue_diff(operation="compare_configs", config_a="DefaultEngine.ini", config_b="WindowsEngine.ini")
```

### 4. compare_assets

**Purpose**: Compare two .uasset binary files and return structured diff (names, imports, exports).

```python
ue_diff(operation="compare_assets", asset_a="NS_Confetti.uasset", asset_b="NS_Explosion.uasset")
```

`asset_type` is advisory: it overrides only the response label. The native class
of each asset still selects the typed extractor automatically.

**Report Format**:

```text
## Asset Diff: NS_Confetti vs NS_Explosion

**Asset Type**: NiagaraSystem (auto-detected)
**Asset A**: 245 names, 32 imports, 18 exports
**Asset B**: 251 names, 34 imports, 19 exports

### Changes (12 total: +5 added, -2 removed, ~5 modified)
| Path | Type | Old | New |
|------|------|-----|-----|
| names.ExplosionForce | added | — | ExplosionForce |
| imports.NiagaraModule/NewEmitter | added | — | NiagaraModule/NewEmitter |
| exports.NS_Explosion.serial_size | modified | 1024 | 2048 |
```

### 5. compare_revision

**Purpose**: Compare all changed files between two VCS revisions (SVN/Git/P4).

```python
ue_diff(operation="compare_revision", revision_a="5948", revision_b="5950")
```

**Report Format**:
```text
## Revision Diff: r5948 → r5950

**VCS**: SVN (auto-detected)
**Files Changed**: 15 (3 .uasset, 8 source, 2 config, 2 skipped)

### .uasset Files (typed+generic diff)
| File | Status | Changes |
|------|--------|---------|
| Content/Characters/BP_Hero.uasset | modified | 12 changes (8 typed, 4 generic) |

### Source Files (text diff)
| File | Status | +Lines | -Lines |
|------|--------|--------|--------|
| Source/MyGame/MyCharacter.cpp | modified | +45 | -12 |
```

### 6. compare_config_chain

**Purpose**: Trace override cascade across 3+ config files with final resolved values.

```python
ue_diff(operation="compare_config_chain", config_files="DefaultEngine.ini,WindowsEngine.ini,WindowsEditor.ini")
```

**Report Format**:
```text
## Config Chain: DefaultEngine → WindowsEngine → WindowsEditor

**Files**: 3 analyzed
**Override Steps**: N sections modified across chain
**Final Resolved Values**: [key sections with final values]
```

### Additional Operations

| Operation | Use |
|-----------|-----|
| `extract_asset` | Extract one asset's typed metadata; pass `operation="extract_asset"` and `asset` explicitly |
| `render_blueprintue` | Render an absolute, existing native Blueprint-family `.uasset` as deterministic BlueprintUE HTML. Exact-file only; no resolver/cache fallback and no smart/auto route. Do not pass `format`, `project_root`, or `asset_type`. |
| `verify_identical` | Fast size/hash/export identity for `asset_a` + `asset_b`, or a `pairs` batch |
| `merge_preview` | 3-way base/ours/theirs conflict classification in path or revision mode |
| `bisect_asset` | Git-only binary search from `revision_good` to `revision_bad` for `asset_path` |
| `diff_asset_revision` | Compare `asset_path` in the working tree with one revision |
| `snapshot` | Write a scoped baseline under `Saved/NarshaMCP/diff_snapshots/` |
| `compare_to_snapshot` | Compare the live tree with the stored `label` baseline |
| `compare_folders` | Hash-gated, no-VCS directory-tree diff with typed asset drilldown |
| `diff_level` | OFPA-aware actor-unit diff of two levels |

---

## Smart Mode

When the input is ambiguous, use smart mode — it auto-routes based on parameters.
`operation="auto"` follows the exact same routes:

```python
# Class with replication hints → replication_audit
ue_diff(operation="smart", class_name="ALyraPlayerState")

# auto alias → the same replication_audit route
ue_diff(operation="auto", class_name="ALyraPlayerState")

# Class + base_class → diff_hierarchy
ue_diff(operation="smart", class_name="ALyraCharacter", base_class="ACharacter")

# config_a + config_b → compare_configs
ue_diff(operation="smart", config_a="DefaultEngine.ini", config_b="WindowsEngine.ini")

# config_files → compare_config_chain
ue_diff(operation="smart", config_files="DefaultEngine.ini,WindowsEngine.ini,WindowsEditor.ini")

# asset_a + asset_b → compare_assets
ue_diff(operation="smart", asset_a="NS_Confetti.uasset", asset_b="NS_Explosion.uasset")
```

### `project_root` Consumers

`project_root` is consumed by `replication_audit`, `compare_configs`,
`compare_config_chain`, `compare_revision`, revision-mode `merge_preview`,
`bisect_asset`, `diff_asset_revision`, `snapshot`, `compare_to_snapshot`,
`compare_folders`, and `diff_level`. `render_blueprintue` accepts an absolute
file path outside the project and intentionally does not consume `project_root`.
Other operations use resolved asset paths
or server defaults and do not consume this override.

---

## Error Handling

Error strings below are the **exact** messages emitted by the `ue_diff` handlers (cross-checked against `analysis_diff.rs` / `analysis_diff_assets.rs` / `analysis_diff_config.rs` in Issue #20718 — do not paraphrase).

| Error (exact) | Cause | Recovery |
|-------|-------|----------|
| `Header file for class '{name}' not found in Source/` | No matching `.h` under Source/ | Check class name spelling; a matching `.h` must exist in Source/. (No need to strip the prefix yourself — the handler already tries the name **with and without** its leading `A`/`U`/`F` and matches case-insensitively.) |
| `Cannot auto-detect base class for '{name}'. PDB data may not be loaded.` | PDB not loaded | Provide `base_class` explicitly |
| `Config file not found: '{path}'` | Wrong filename | Use `ue_glob("*.ini")`; platform configs live in `Config/Windows/`, `Config/Linux/`, etc. |
| `Failed to parse asset_a '{path}': {err}` / `Failed to parse asset_b '{path}': {err}` | Corrupt or unsupported asset | Verify the file is a valid `.uasset` (not `.umap` or a text asset) |
| `compare_assets requires 'asset_a'` / `compare_assets requires 'asset_b'` | Missing asset path | Provide both `asset_a` and `asset_b` |
| `Could not find asset_a: '{path}'` / `Could not find asset_b: '{path}'` | Path/name did not resolve to a file | Check the path or asset name; use `ue_glob` to locate it |

---

## Related

- Issue #7315 — ue_diff tool implementation
- Issue #7422 — compare_assets operation (binary .uasset diff)
- Issue #7423 — `/ue-vcs-diff` skill (VCS integration, depends on #7422)
- `/ue-pre-commit` — Uses replication_audit for pre-commit checks
- `/ue-audit` — Project-wide health audit (could integrate config diff)
- `/ue-impact` — Change impact analysis (complementary: impact shows what breaks, diff shows what's different)
## MCP Tool Examples

```python
# Search for patterns during diff analysis
ue_grep(query="UPROPERTY(Replicated)", domain="source")
```
