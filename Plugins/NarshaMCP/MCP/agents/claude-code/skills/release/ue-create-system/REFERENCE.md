# UE Create System — Reference

**Version**: 1.0.0
**Issue**: #7408
**Parent**: [SKILL.md](SKILL.md)

---

## System Type Classification

| Type | Keywords (ko) | Keywords (en) | Domains | ECA Tools (5.8+) |
|------|---------------|---------------|---------|-------------------|
| Damage | 데미지, HP, 체력 | damage, health, HP | C++ + BP + VFX | `lisp_to_blueprint`, `create_niagara_system` |
| Weapon | 무기, 발사, 총 | weapon, fire, shoot | C++ + BP + VFX + Material | + `add_material_node` |
| Inventory | 인벤토리, 아이템 | inventory, item | C++ + BP + UI | `lisp_to_blueprint`, `create_umg_widget_blueprint` |
| AI Enemy | AI, 적, NPC | AI, enemy, NPC | C++ + BP | `lisp_to_blueprint` |
| HUD/UI | HUD, UI, 위젯 | HUD, UI, widget | C++ + BP + UI | `lisp_to_blueprint`, `create_umg_widget_blueprint` |
| Projectile | 투사체, 총알 | projectile, bullet | C++ + BP + VFX + Material | all 4 tools |
| Custom | (user-specified) | (user-specified) | (ask user) | (Phase 1 decides) |

**Domain → ECA Tool Mapping**:

| Domain | ECA Tool Name | Fallback (5.7-) |
|--------|---------------|-----------------|
| Blueprint | `lisp_to_blueprint` | `ue_manage_blueprint(add_node)` |
| VFX (Niagara) | `create_niagara_system` | Manual guidance |
| Material | `add_material_node` | Manual guidance |
| UI (UMG Widget) | `create_umg_widget_blueprint` | Manual guidance |

---

## MCP Call Budget — Parameter Templates

### Phase 0: Discovery (max 2 calls)

```python
# Step 0a: Find existing similar systems
ue_grep(params={"query": "<system_keyword>", "domain": "source", "limit": 10})

# Step 0b: Read most relevant existing class
ue_read(params={"identifier": "<ClassName>"})
```

### Phase 1: Analysis (2-3 calls)

```python
# Step 1a: Symbol analysis
ue_analyze_symbols(operation="smart", params={"query": "<base_class>"})

# Step 1b (GAS only): Tag structure
ue_manage_gameplay(operation="smart", params={"tag": "<relevant_tag>"})

# Step 1c: Architecture dry-run
ue_generate_code(operation="suggest_class", params={
    "base_class": "<detected_base>",
    "class_name": "<SystemName>",
    "features": ["<auto_detected_features>"]
})
```

### Phase 2: C++ Generation (3-6 calls, sequential)

```python
# For each class in dependency order:
ue_generate_code(operation="derive_class", params={
    "base_class": "<base>",
    "class_name": "<name>",
    "output_dir": "Source/<Module>/<Subdirectory>",
    "features": ["<selected_features>"]
})
```

**Dependency order**: AttributeSet → Ability → Component → Character

### Phase 3: ECA Detection + Asset Creation [REMOVED post-Epic #11230]

> **Path A (ECABridge) — REMOVED post-Epic #11230 / PR #11295 (2026-05-09)**
>
> The previous Phase 3 ECABridge path (`ue_eca_proxy(call_tool, ...)` for `lisp_to_blueprint` / `create_niagara_system` / `add_material_node` / `create_umg_widget_blueprint`) was deleted alongside the ECABridge stack. The ECABridge plugin was confirmed not present in Epic's public UE 5.8 mirror, so the LISP write-back channel is no longer reachable from any engine version. **Phase 3 now always routes to Path B** (Editor RC: `ue_manage_blueprint(create_blueprint)` for BP, manual guidance for VFX/Material/UI). The historical block below is retained for historical reference only — see [`docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md`](../../../docs/archive/WIDGET_DSL_IMPORT_VIA_ECA.md).

```python
# [HISTORICAL — DO NOT EXECUTE]
# Step 3.0: (ECA channel removed in #11230/#11231; eca_bridge_available field removed from ue_check_health in #15171)
ue_check_health()

# Step 3a-0 (ECA available): Discover tools — REMOVED
# ue_eca_proxy(operation="list_tools", params={"query": "blueprint"})

# Step 3a-1: Blueprint graph — REMOVED
# ue_eca_proxy(operation="call_tool", params={
#     "tool_name": "lisp_to_blueprint", ... })

# Step 3a-2: Niagara VFX — REMOVED
# ue_eca_proxy(operation="call_tool", params={
#     "tool_name": "create_niagara_system", ... })

# Step 3a-3: Material — REMOVED
# ue_eca_proxy(operation="call_tool", params={
#     "tool_name": "add_material_node", ... })

# Step 3a-4: UMG Widget — REMOVED
# ue_eca_proxy(operation="call_tool", params={
#     "tool_name": "create_umg_widget_blueprint", ... })
```

### Phase 4: Validation (1-2 calls)

```python
# Step 4a: Build verification
ue_fix_errors(operation="smart", params={"project_root": "<project_root>"})

# Step 4b (if errors): Auto-fix attempt
ue_fix_errors(operation="auto", params={"errors": "<detected_errors>"})
```

---

## Phase Decision Trees

### Phase 0: Similar System Detection

```text
ue_grep results:
├─ Relevant class found?
│  ├─ YES → Warn user: "Similar system '<ClassName>' exists"
│  │        → Suggest: "Extend existing system instead?"
│  │        → Wait for user confirmation
│  └─ NO  → Proceed to Phase 1
```

### Phase 3: Generation Path (ECA removed)

```text
ue_check_health():
├─ ECA/ECABridge removed (Epic #11230/#11231; `eca_bridge_available` flag dropped in #15171)
│  └─ Path A (ECABridge) no longer available
└─ Path B: NarshaMCP (always)
   ├─ ue_manage_blueprint(add_node) for basic BP
   └─ Manual guidance for VFX/Material/UI
```

### Phase 4: Error Resolution

```text
ue_fix_errors(smart):
├─ 0 errors?
│  ├─ YES → ✅ Build PASSED, proceed to report
│  └─ NO  → Attempt 1 auto-fix cycle
│     ├─ ue_fix_errors(auto)
│     ├─ Errors resolved?
│     │  ├─ YES → ✅ Build PASSED after fix
│     │  └─ NO  → ❌ STOP. Report remaining errors. No further retry.
```

---

## Error Recovery Matrix

| Error | Typical Message | Cause | Recovery Action |
|-------|-----------------|-------|-----------------|
| System type unclear | N/A (ambiguous input) | User description doesn't match any type keywords | Ask: "Which domains? BP / VFX / Material / UI" |
| ECA not reachable | `"ECABridge not reachable on port 3000"` | UE <5.8, plugin disabled, Editor not running | Switch to Path B entirely |
| ECA tool not found | `"No tools matched the query filter"` | ECA server up but no tools registered | Switch to Path B entirely |
| ECA call_tool timeout | `"error": "timeout"` in call_tool response | ECA tool execution exceeded time limit | Skip domain, output manual guidance |
| ECA call_tool error | `"error": "<message>"` in call_tool response | Invalid arguments or tool-specific failure | Skip domain, output manual guidance |
| C++ gen fails | `derive_class` returns error | Missing base class, output dir conflict, invalid features | Report error, ask user. **No retry.** |
| Build error: missing module | `"Unresolved external symbol"` or `"Module not found"` | Build.cs missing dependency | List required modules in report |
| Build error: include | `"Cannot open include file"` | Missing #include or wrong path | Attempt 1 auto-fix cycle |
| Similar system exists | Phase 0 finds match | Project already has similar implementation | Warn user, suggest extending |
| Too many classes | >6 classes requested | System scope too large for one invocation | Split into 2 invocations or redirect to `/code-generation-power` |

---

## Graceful Degradation Spec

**Principle**: Each domain degrades independently. One domain's failure does NOT block others.

### Per-Domain Degradation

> The "Path A (ECA …)" columns below are **historical** — ECA was removed (Epic #11230/#11231); only the **Path B** column applies (generation always uses it).

| Domain | Path A (ECA success) | Path A (ECA fail) | Path B (no ECA) |
|--------|---------------------|-------------------|-----------------|
| Blueprint | `lisp_to_blueprint` creates full graph | `ue_manage_blueprint(add_node)` for basic nodes | `ue_manage_blueprint(add_node)` for basic nodes |
| VFX | `create_niagara_system` creates system | Manual: Content Browser → Niagara → System | Manual: Content Browser → Niagara → System |
| Material | `add_material_node` creates material | Manual: Content Browser → Material | Manual: Content Browser → Material |
| UI | `create_umg_widget_blueprint` creates widget | Manual: Content Browser → Widget Blueprint | Manual: Content Browser → Widget Blueprint |

### Manual Guidance Template

When a domain falls back to manual guidance, output:

```text
❌ <DomainName> (<reason> — manual creation needed)
   → Content Browser → Right-click → <AssetType>
   → Name: <Prefix>_<SystemName>
   → Template/Base: <suggested_template>
   → Connect to: <C++ class>::<relevant_property_or_function>
```

---

## Naming Conventions

| Domain | Prefix | Example |
|--------|--------|---------|
| C++ Component | `UCU` | `UCUDamageComponent` |
| C++ Type | `UCU` | `UCUDamageType` |
| C++ Ability | `UGA_` | `UGA_ApplyDamage` |
| C++ Attribute Set | `UCU` | `UCUDamageAttributeSet` |
| Blueprint | `BP_` | `BP_DamageComponent` |
| Niagara System | `NS_` | `NS_DamageImpact` |
| Material | `M_` | `M_DamageFlash` |
| UMG Widget | `WBP_` | `WBP_DamageHUD` |

**Note**: C++ classes follow the `CU` prefix rule (Critical Rule #17).

---

## Version Compatibility

| Engine Version | Path | Notes |
|----------------|------|-------|
| All versions (UE 5.7+) | Path B (NarshaMCP) | ECA/ECABridge removed (Epic #11230/#11231); `ue_manage_blueprint` + manual guidance. The former "Path A" (ECABridge, 407 tools) is no longer reachable from any engine version. |

**Detection**: ECA removed (Epic #11230/#11231; `eca_bridge_available` flag dropped in #15171) — generation always uses Path B (NarshaMCP).
