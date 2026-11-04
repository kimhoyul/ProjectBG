# UE Engine Content — Examples

## Example 1: Full Guided Workflow (default)

**User**: "엔진 메타데이터 포함해줘"

**Invocation**: `/ue-engine-content`

**Steps executed**:
```python
# Step 1: Check current status
ue_cache_control(operation="get_stats")
# → stats.loaded: true, stats.symbols_cached: 6417624

# Step 2: Load runtime PDBs
ue_cache_control(operation="load_engine_runtime")
# → success: true, pdbs_parsed: 33, new_symbols: 145230

# Step 2.5: Load engine plugin PDBs (Issue #6896)
ue_cache_control(operation="load_engine_plugins")
# → success: true, pdbs_parsed: ~1530, new_symbols: ~22M

# Step 3: Regenerate metadata with engine content
ue_cache_control(operation="regenerate_metadata", include_engine=true)
# → success: true (takes ~2 minutes)

# Step 4: Ask user about editor PDBs → user declines

# Step 5: Final status
ue_cache_control(operation="get_stats")
# → stats.symbols_cached: 28660950
```

**Output**:
```
## Engine Content Loading Complete

| Component | Before | After |
|-----------|--------|-------|
| PDBs loaded | 172 | 1735 |
| Symbols indexed | 6.4M | 28.7M |
| Engine included | No | Yes |
```

---

## Example 2: Plugin PDBs Only

**User**: "엔진 플러그인 PDB만 로드해줘"

**Invocation**: `/ue-engine-content plugins`

**Steps executed**:
```python
# Step 1: Check status
ue_cache_control(operation="get_stats")

# Step 2.5 only: Load engine plugin PDBs
ue_cache_control(operation="load_engine_plugins")
# → success: true, pdbs_parsed: ~1530, new_symbols: ~22M
```

**Use case**: MetaSound, Niagara, PCG 등 Engine Plugin 클래스만 검색 가능하게 하고 싶을 때. 메타데이터 재생성이나 Editor PDB 없이 빠르게 심볼만 추가.

---

## Example 3: Runtime PDBs Only

**User**: "Load engine runtime"

**Invocation**: `/ue-engine-content runtime`

**Steps executed**:
```python
# Step 1 + Step 2 only
ue_cache_control(operation="get_stats")
ue_cache_control(operation="load_engine_runtime")
# → success: true, pdbs_parsed: 33
```

---

## Example 4: Full Load (Including Editor)

**User**: "엔진 전체 로드해줘"

**Invocation**: `/ue-engine-content full`

**Steps executed**:
```python
# Steps 1-4 without user confirmation for Step 4
ue_cache_control(operation="get_stats")
ue_cache_control(operation="load_engine_runtime")
ue_cache_control(operation="load_engine_plugins")
ue_cache_control(operation="regenerate_metadata", include_engine=true)
ue_cache_control(operation="load_engine_editor")  # No confirmation needed in full mode
ue_cache_control(operation="get_stats")
```

**Warning**: Requires ~12 GB free RAM for Editor PDBs.

---

## Example 5: Already Loaded (Idempotent)

**User**: "엔진 PDB 로드해줘" (already loaded in this session)

**Invocation**: `/ue-engine-content`

**Steps executed**:
```python
# Step 1: Check status
ue_cache_control(operation="get_stats")
# → stats.symbols_cached: 28660950

# Step 2: Already loaded
ue_cache_control(operation="load_engine_runtime")
# → message: "Engine Runtime PDBs already loaded", new_symbols: 0

# Step 2.5: Already loaded
ue_cache_control(operation="load_engine_plugins")
# → message: "Engine EnginePlugin PDBs already loaded", new_symbols: 0

# Skip remaining steps, report "Already loaded"
```
