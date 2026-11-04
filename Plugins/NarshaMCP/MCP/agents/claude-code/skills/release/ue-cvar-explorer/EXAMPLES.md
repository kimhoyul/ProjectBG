# UE CVar Explorer — Examples

**Version**: 1.1.0
**Skill**: ue-cvar-explorer

---

## Example 1: Single CVar Query

**User**: "r.Shadow.MaxResolution 뭐야?"

### Execution

**Step 0**: Discovery
```
ue_grep(query="r.Shadow.MaxResolution", domain="source")
→ Found in: ShadowSetup.cpp, DefaultEngine.ini
```

**Step 1**: Structured search
```
ue_analyze_config(operation="smart", query="r.Shadow.MaxResolution")
→ {
    name: "r.Shadow.MaxResolution",
    value: 2048,
    default: 2048,
    flags: ["ECVF_Scalability", "ECVF_RenderThreadSafe"],
    config: "DefaultEngine.ini [/Script/Engine.RendererSettings]",
    scalability_group: "sg.ShadowQuality"
  }
```

**Output**:
```
## r.Shadow.MaxResolution

| Property | Value |
|----------|-------|
| Current | 2048 |
| Default | 2048 |
| Flags | ECVF_Scalability, ECVF_RenderThreadSafe |
| Config | DefaultEngine.ini |
| Scalability | sg.ShadowQuality (0=512, 1=1024, 2=2048, 3=2048, 4=4096) |
```

---

## Example 2: Scalability Group

**User**: "sg.ShadowQuality 관련 설정 전부 보여줘"

### Execution

**Step 0**: Discovery
```
ue_grep(query="sg.ShadowQuality", domain="source")
→ Found in: Scalability.ini, ShadowSetup.cpp
```

**Step 1**: Structured search
```
ue_analyze_config(operation="search_config", params={"query": "sg.ShadowQuality"})
→ Group members: r.Shadow.MaxResolution, r.Shadow.CSM.MaxCascades,
  r.Shadow.RadiusThreshold, r.Shadow.DistanceScale
```

**Output**:
```
## sg.ShadowQuality Controlled CVars

| CVar | Low(0) | Medium(1) | High(2) | Epic(3) | Cinematic(4) |
|------|--------|-----------|---------|---------|---------------|
| r.Shadow.MaxResolution | 512 | 1024 | 2048 | 2048 | 4096 |
| r.Shadow.CSM.MaxCascades | 1 | 2 | 4 | 6 | 10 |
| r.Shadow.RadiusThreshold | 0.06 | 0.04 | 0.02 | 0.01 | 0.005 |
```

---

## Example 3: Pattern Search

**User**: "r.Lumen.* 전부"

### Execution

**Step 0**: Discovery
```
ue_grep(query="r.Lumen", domain="source")
→ 47 matches across LumenScene.cpp, LumenReflections.cpp, etc.
```

**Step 1**: Structured search
```
ue_analyze_config(operation="search_config", params={"query": "r.Lumen"})
→ 23 CVars: r.Lumen.ScreenProbeGather.*, r.Lumen.Reflections.*, etc.
```

**Output**: Grouped list of all Lumen CVars with current values and brief descriptions.

---

## Example 4: Fallback Scenario (No Engine Source)

**User**: "r.GUITextureLODBias 뭐야?"

### Execution

**Step 0**: Discovery
```
ue_grep(query="r.GUITextureLODBias", domain="source")
→ 0 matches (engine CVar, not in project source)
```

**Step 1**: Structured search
```
ue_analyze_config(operation="smart", query="r.GUITextureLODBias")
→ No results (engine source not indexed)
```

**Step 2**: Fallback report
```
⚠ r.GUITextureLODBias not found in project scope.
This is an engine-level CVar. To get full metadata:
  /ue-engine-content (loads engine PDB/metadata)

From training knowledge (unverified):
  Controls texture LOD bias for UI elements. Default: 0.
  Higher values = lower resolution UI textures.
```
