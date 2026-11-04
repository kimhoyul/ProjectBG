# UE CVar Explorer — Reference

**Version**: 1.1.0
**Skill**: ue-cvar-explorer

---

## MCP Tools Used

### Primary: `ue_analyze_config`

| Operation | Purpose | Key Params |
|-----------|---------|------------|
| `search_config` | Search CVars by name pattern | `query` (string), `scope` (project/engine/all) |
| `smart` | Auto-route based on params | `query`, `cvar_name`, `config_file` |
| `modify_config` | Change config values | `config_file`, `section`, `key`, `value` |

### Supplementary

| Tool | When Used |
|------|-----------|
| `ue_grep` | Step 0 discovery — broad keyword search before structured analysis |
| `ue_read` | Read specific config files for context |
| `ue_analyze_symbols` | Resolve C++ CVar registration code |

---

## CVar Name Patterns

| Prefix | Domain | Examples |
|--------|--------|---------|
| `r.` | Rendering | `r.Shadow.MaxResolution`, `r.Lumen.ScreenProbeGather` |
| `sg.` | Scalability Groups | `sg.ShadowQuality`, `sg.ViewDistanceQuality` |
| `a.` | Animation | `a.URO.Enable` |
| `p.` | Physics | `p.Chaos.Solver.Iterations` |
| `fx.` | Effects/Niagara | `fx.Niagara.QualityLevel` |
| `au.` | Audio | `au.MaxChannels` |
| `net.` | Networking | `net.MaxClientRate` |

---

## Workflow Steps

### Step 0: Discovery (ue_grep)
Search source code for CVar mentions before structured analysis.
```
ue_grep(query="r.Shadow", domain="source")
```

### Step 1: Structured Search (ue_analyze_config)
```
ue_analyze_config(operation="search_config", params={"query": "r.Shadow.MaxResolution"})
```

This is discovery only: it returns matching config keys and locations, but metadata coverage is not
guaranteed. Call `explore_cvar` for the selected or 1–3 representative results before reporting
current/default values, ECVF flags, or scalability metadata.

### Step 2: Impact Analysis
For scalability CVars, map to quality levels:
- `sg.ShadowQuality` 0-4 → controls `r.Shadow.MaxResolution`, `r.Shadow.CSM.MaxCascades`, etc.

### Step 3: Formatted Report
Output includes: value, flags (ECVF_Scalability, ECVF_RenderThreadSafe, etc.), config source, related CVars.

---

## ECVF Flag Reference

| Flag | Meaning |
|------|---------|
| `ECVF_Scalability` | Controlled by scalability settings |
| `ECVF_RenderThreadSafe` | Safe to change on render thread |
| `ECVF_ReadOnly` | Cannot be changed at runtime |
| `ECVF_Cheat` | Requires cheat mode |
| `ECVF_SetByConsole` | Currently set via console command |

---

## Fallback Chain

When engine source is unavailable:
1. Try `ue_analyze_config(search_config)` — works with project configs
2. Fall back to `ue_grep` for source code references
3. If no results: report "CVar not found in project scope; engine source required for full metadata"

---

## Error Conditions

| Error | Cause | Recovery |
|-------|-------|----------|
| Empty results | CVar not in project scope | Try with `scope=all` or suggest engine content loading |
| Partial metadata | Engine source not indexed | Note missing fields, suggest `/ue-engine-content` |
| Multiple matches | Ambiguous pattern | Present all matches, ask user to narrow |

---

## Auto-Trigger Phrases

Exhaustive trigger phrase lists (the SKILL.md frontmatter `description` carries the canonical short set; these are the full KO/EN enumerations).

### Korean

- "CVar 찾아줘", "CVar X 뭐야", "설정 검색"
- "그래픽 설정 분석", "렌더링 옵션 설명"
- "퀄리티 설정 영향 분석", "스케일러빌리티 그룹"
- "r.Shadow 관련 설정 전부 보여줘"
- "성능 설정 탐색", "설정값 변경 영향"

### English

- "Explore CVar X", "What is r.Shadow.MaxResolution?"
- "CVar search Shadow", "Find config settings"
- "Graphics quality settings", "Rendering options"
- "config search", "graphics settings"
- "Scalability group mapping", "Quality level values"
- "What happens if I change r.Shadow.MaxResolution?"

---

## Disambiguation

- `r.Shadow 변경하면 영향 범위?` → use `ue-impact` despite the `r.` prefix.
- `Shadow 관련 모든 설정 찾아줘` → use `ue_grep` for a broad sweep.
- `렌더링 퀄리티 전부 낮추면 뭐가 바뀌어?` → use `ue-impact` or `ue-explain`.
- `config.ini 수정해줘` → use built-in file editing.

---

## CVar Name Normalization — Prefix Variant Table

User queries often omit the standard prefix. Always try variants before giving up:

1. For `G` followed by uppercase (for example `GUITexture...`), strip `G` and try `r.`.
2. Without a known dotted prefix (`r.`, `sg.`, `a.`, `p.`), try `r.{Name}`, then bare `{Name}`.
3. If `explore_cvar` returns no data, use `search_config` with the bare name as a pattern.

| User Input | Try These (in order) | Why |
|-----------|---------------------|-----|
| `GUITextureLODBias` | `r.UITextureLODBias`, `UITextureLODBias` | UE CVars use `r.` prefix; `G` is often a global variable prefix, not part of the CVar name |
| `ParticleLODBias` | `r.ParticleLODBias`, `ParticleLODBias` | Particle system CVar |
| `ShadowMaxResolution` | `r.Shadow.MaxResolution` | Dot-separated hierarchy |
| `ViewDistanceScale` | `r.ViewDistanceScale`, `sg.ViewDistanceScale` | Could be in scalability group |

```python
# Example: User asks about "GUITextureLODBias"
# Step 1: Try exact name
result = ue_analyze_config(operation="explore_cvar", option="GUITextureLODBias", explore=true)
# Step 2: If no data, try r. prefix with G stripped
result = ue_analyze_config(operation="explore_cvar", option="r.UITextureLODBias", explore=true)
# Step 3: If still no data, search broadly
result = ue_analyze_config(operation="search_config", query="*UITextureLODBias*")
```

---

## Fallback Chain (when `engine_source` is null)

When `explore_cvar` returns data but `engine_source: null` (CVar exists in config but scanner couldn't find source definition), use this fallback chain to gather additional context:

```python
# 1. Search PDB symbols for CVar variable name
ue_analyze_symbols(operation="search_symbols", query="ParticleLODBias", symbol_type="variable")

# 2. Find callers to understand usage context
ue_analyze_symbols(operation="find_callers", function_name="ParticleLODBias")

# 3. Search engine docs for any mention
ue_engine_docs(operation="grep", pattern="ParticleLODBias")
```

**Use the fallback results to enrich your answer**:

- Symbol search → confirms the CVar exists and shows its type (int32, float, etc.)
- Find callers → reveals which systems read/write this CVar (rendering, particles, etc.)
- Engine docs → may have usage examples or explanations

**IMPORTANT**: Even if fallback returns limited data, report what you found honestly. Do NOT guess or fabricate CVar behavior based on the name alone. State clearly: "Engine source not found for this CVar; information based on config data and symbol analysis."

---

## Multi-CVar Handling

For multiple CVars in one request (for example `GUITextureLODBias와 ParticleLODBias 뭐야?`), call `explore_cvar` once per canonical CVar (`r.UITextureLODBias`, `r.ParticleLODBias`), report separate sections, then optionally compare them.

## Engine Docs Cross-Check (conditional)

Run only when `engine_source == null` or the name can map to more than one subsystem (`*Particle*`, `*LOD*`, or `*Streaming*`). Skip it when source is populated and the name is unambiguous.

```python
ue_engine_docs(operation="search", query="<CVar name> usage scope limitation")
ue_engine_docs(operation="grep", pattern="<CVar bare name>")
```

Use this to verify system scope, directional behavior, and platform constraints. Never infer from a cross-system name: for example, `ParticleLODBias` controls Cascade particle LOD level, not textures or Niagara.

---

## Output Format

Present results in structured format:

```text
=== CVar Explorer: r.Shadow.MaxResolution ===

--- Identity ---
Name:     r.Shadow.MaxResolution
Type:     TAutoConsoleVariable<int32>
Default:  2048
Flags:    [RenderThreadSafe, Scalability]
Category: Rendering

--- Current Project Values ---
File                     Section                Value
DefaultEngine.ini        [ConsoleVariables]     2048

--- Engine Source ---
File: Source/Runtime/Renderer/Private/ShadowSetup.cpp:42
Help: "Max square dimensions (in texels) for shadow depth rendering"

--- Scalability Group ---
Controlled by: sg.ShadowQuality
  Low (0):         512
  Medium (1):      1024
  High (2):        1024
  Epic (3):        2048
  Cinematic (Cine): 4096

--- Change Impact ---
WARNING: Direct modification will be overridden when sg.ShadowQuality
preset changes.
Recommendation: Modify sg.ShadowQuality instead.
```

---

## Error Recovery

| Error | Cause | Fallback |
|-------|-------|----------|
| CVar not in registry | Engine path not set or CVar unknown | Show config file occurrences only |
| No scalability mapping | CVar not in any sg.* group | Report "Not controlled by scalability preset" |
| explore_cvar unavailable | Older MCP server version | Fall back to `explain_config` + `find_overrides` separately |
| No config files found | Project path incorrect | Suggest checking project_root configuration |

---

## Examples

### Example 1: Specific CVar

**User**: "r.Shadow.MaxResolution 뭐야?"
**Action**: `ue_analyze_config(operation="explore_cvar", option="r.Shadow.MaxResolution", explore=true)`

### Example 2: Scalability Group

**User**: "sg.ShadowQuality가 제어하는 설정들?"
**Action**: `ue_analyze_config(operation="explore_cvar", option="sg.ShadowQuality", explore=true)`

### Example 3: Pattern Search

**User**: "Shadow 관련 CVar 전부"
**Action**: `ue_analyze_config(operation="search_config", query="r.Shadow*")`
Then explore top results individually.

### Example 4: Impact Analysis

**User**: "r.ScreenPercentage 변경하면 영향?"
**Action**: `ue_analyze_config(operation="explore_cvar", option="r.ScreenPercentage", explore=true)`
Focus on `change_impact` and `relationships` in the response.
