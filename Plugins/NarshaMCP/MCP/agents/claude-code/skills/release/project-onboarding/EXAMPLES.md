# Project Onboarding — Examples

Usage scenarios for `/project-onboarding` across different project types.

---

## Example 1: Full Onboarding — Lyra-like Action RPG (Korean)

**Scenario**: First session on a GAS-heavy project with Niagara VFX and C++ gameplay code.

**User Input**: "프로젝트 분석해줘"

### Phase 1: Server & Environment Check

```python
ue_check_health()
```
```json
{
  "status": "healthy",
  "version": "0.9.6",
  "project_path": "C:/LCP_A",
  "path_validation": { "status": "valid" }
}
```

```python
ue_cache_control(operation="get_stats")
```
```json
{
  "symbol_index": { "loaded": true, "symbol_count": 185432 },
  "metadata": { "status": "loaded" },
  "four_phase": { "complete": true, "running": false }
}
```

### Phase 2: System Detection (13 parallel calls)

```python
# All 13 executed in parallel
ue_search_assets(asset_type="gameplayability", query="*")  # → 42 abilities
ue_search_assets(asset_type="gameplayeffect", query="*")   # → 87 effects
ue_search_assets(asset_type="NiagaraSystem", query="*")    # → 678 systems
ue_search_assets(asset_type="blueprint", query="*")        # → 1,204 blueprints
ue_search_assets(asset_type="material", query="*")         # → 892 materials
ue_search_assets(asset_type="inputaction", query="*")      # → 24 input actions
ue_search_assets(asset_type="animation", query="*")        # → 356 animations
ue_search_assets(asset_type="pcg", query="*")              # → 0
ue_search_assets(asset_type="statetree", query="*")        # → 5 state trees
ue_search_assets(asset_type="behaviortree", query="*")     # → 0
ue_search_assets(asset_type="levelsequence", query="*")    # → 12 sequences
ue_search_assets(asset_type="controlrig", query="*")       # → 3 rigs
ue_search_assets(asset_type="ikrig", query="*")            # → 2 rigs
```

### Phase 3: C++ Architecture Sample

```python
ue_analyze_symbols(operation="search_symbols", params={"query": "A*", "limit": 20})
```
Result: 18 Actor classes found → **Heavy C++**

### Phase 4: Recommendations

```text
=== Project Onboarding Report ===

--- 1. Environment ---
  Server:   v0.9.6 (healthy)
  Project:  C:/LCP_A (UE 5.5)
  PDB:      185,432 symbols loaded (complete)
  Metadata: loaded

--- 2. Detected Systems (11/13) ---
  [✅] GAS (GameplayAbility)    — 42 abilities, 87 effects
  [✅] Enhanced Input            — 24 input actions
  [✅] Niagara                   — 678 systems
  [✅] Blueprints                — 1,204 blueprints
  [✅] Materials                 — 892 materials
  [✅] Animation                 — 356 animations
  [✅] AI (StateTree)            — 5 state trees
  [✅] Cinematics                — 12 level sequences
  [✅] Control Rig               — 3 rigs
  [✅] IK Rig                    — 2 rigs
  [❌] PCG
  [❌] AI (BehaviorTree)

--- 3. C++ Architecture ---
  Actor classes found: 18
  Key classes: AMyCharacter, AMyEnemy, AMyWeapon, ...
  Complexity: Heavy C++

--- 4. Recommended Tools & Skills ---

  Priority 1 — Detected Systems:
    /gas-development          — GAS ability workflow
    /material-analysis        — Material structure analysis
    /ue-sequencer-cinematics  — Cinematic workflow

  Priority 2 — Always Useful:
    /ue-debug                 — Error resolution
    /ue-audit                 — Full project health check

  Priority 3 — C++ Tools:
    ue_analyze_symbols(trace_hierarchy) — Class inheritance
    /ue-cpp-refactor          — Safe refactoring
    /ue-test-gen              — Test generation

--- Try It Now ---
  1. ue_manage_gameplay(operation="smart", tag="Ability.*")
  2. ue_manage_material(operation="smart", material_name="M_Character")
  3. ue_analyze_symbols(operation="smart", class_name="AMyCharacter")
```

---

## Example 2: Quick Mode — English Input

**Scenario**: User wants a fast overview without C++ sampling.

**User Input**: "Get started --quick"

### Phase 1-2 Only

```python
ue_check_health()
ue_cache_control(operation="get_stats")
# → healthy, 52,000 symbols, indexing complete
```

```python
# 13 parallel asset searches
ue_search_assets(asset_type="blueprint", query="*")   # → 340 blueprints
ue_search_assets(asset_type="material", query="*")    # → 120 materials
# ... remaining 11 types → all 0
```

### Output (--quick)

```text
=== Project Onboarding Report ===

--- 1. Environment ---
  Server:   v0.9.6 (healthy)
  Project:  D:/MyGame (UE 5.4)
  PDB:      52,000 symbols loaded (complete)

--- 2. Detected Systems (2/13) ---
  [✅] Blueprints — 340 blueprints
  [✅] Materials  — 120 materials
  [❌] GAS, PCG, Niagara, Input, Animation, AI, Cinematics, Rigging

--- 3. C++ Architecture ---
  Skipped (--quick mode)

--- 4. Recommended Tools & Skills ---

  Priority 1:
    /blueprint-flow           — BP execution flow analysis
    /material-analysis        — Material structure

  Priority 2:
    /ue-debug                 — Error resolution
    /ue-scaffold              — Build new features

--- Try It Now ---
  1. ue_manage_blueprint(operation="smart", blueprint_name="BP_Player")
  2. ue_manage_material(operation="smart", material_name="M_Default")
  3. ue_fix_errors(operation="smart", project_root="D:/MyGame")
```

---

## Example 3: Blueprint-Only Project — No C++ Classes

**Scenario**: A new project with only Blueprints, no GAS/Niagara/AI systems.

**User Input**: "뭘 할 수 있어?"

### Phase 1: Server & Environment Check

```python
ue_check_health()
# → healthy, v0.9.6, project: D:/MyGame
ue_cache_control(operation="get_stats")
# → 52,000 symbols, indexing complete
```

### Phase 2: System Detection

```text
Detected: Blueprints (45), Materials (18)
Not detected: GAS, PCG, Niagara, Input, Animation, AI, Cinematics, Rigging
```

### Phase 3: C++ Architecture

```python
ue_analyze_symbols(operation="search_symbols", params={"query": "A*", "limit": 20})
```
Result: 0 Actor classes → **Blueprint-only project**

### Output

```text
--- 3. C++ Architecture ---
  Actor classes found: 0
  Complexity: Blueprint-only

--- 4. Recommended Tools & Skills ---

  Priority 1:
    /blueprint-flow           — BP execution flow analysis
    /material-analysis        — Material structure

  Priority 2:
    /ue-debug                 — Error resolution
    /ue-audit                 — Project health check

  Priority 3 — Migration Option:
    /bp-cpp-project-init      — Convert BP project to C++ for NarshaMCP plugin

--- Try It Now ---
  1. ue_manage_blueprint(operation="smart", blueprint_name="BP_ThirdPersonCharacter")
  2. ue_manage_material(operation="smart", material_name="M_Default")
  3. ue_fix_errors(operation="smart", project_root="D:/MyGame")
```
