# UE Engine Content — Reference

## MCP Tool Mapping

| Step | MCP Call | Purpose |
|------|----------|---------|
| 1 | `ue_cache_control(operation="get_stats")` | Current PDB/metadata status |
| 2 | `ue_cache_control(operation="load_engine_runtime")` | ~100 gameplay modules from `Engine/Binaries/Win64/` |
| 2.5 | `ue_cache_control(operation="load_engine_plugins")` | ~1,598 plugin modules from `Engine/Plugins/**/Binaries/Win64/` |
| 3 | `ue_cache_control(operation="regenerate_metadata", include_engine=true)` | Rebuild metadata with engine content |
| 4 | `ue_cache_control(operation="load_engine_editor")` | ~400 editor modules (optional, high memory) |
| 5 | `ue_cache_control(operation="get_stats")` | Final before/after comparison |

## Engine PDB Tiers

| Tier | Path | Count | Default |
|------|------|-------|---------|
| Core | `Engine/Binaries/Win64/UnrealEditor-{Core,Engine,...}.pdb` | ~8 | Loaded at startup |
| Runtime | `Engine/Binaries/Win64/UnrealEditor-*.pdb` (non-editor) | ~100 | On-demand |
| EnginePlugin | `Engine/Plugins/**/Binaries/Win64/UnrealEditor-*.pdb` | ~1,598 | On-demand (Issue #6896) |
| Editor | `Engine/Binaries/Win64/UnrealEditor-*.pdb` (editor-only) | ~400 | On-demand |

## Key Environment Variables

| Variable | Default | Purpose |
|----------|---------|---------|
| `UECODEGEN_LOAD_ENGINE_PLUGINS` | `false` | Auto-load Engine Plugin PDBs at startup |
| `UECODEGEN_INCLUDE_ENGINE_METADATA` | `false` | Include engine UAssets in metadata generation |
| `UECODEGEN_ENGINE_DIR` | Auto-detected | Engine installation path |

## Module Groups (for `load_modules`)

| Group | Modules |
|-------|---------|
| GAS | GameplayAbilities, GameplayTags, GameplayTasks |
| Niagara | NiagaraCore, Niagara, NiagaraShader |
| AI | AIModule, BehaviorTree, StateTree, NavigationSystem |
| MetaSound | MetasoundEngine, MetasoundFrontend, MetasoundGenerator, MetasoundGraphCore |
| PCG | PCG |
| Animation | AnimGraph, AnimationCore |
| Input | EnhancedInput, InputCore |

## Typical Symbol Counts

| Scope | Symbols | Memory |
|-------|---------|--------|
| Project only | ~6M | ~3 GB |
| + Runtime | ~6.5M | ~3.5 GB |
| + EnginePlugin | ~28M | ~8 GB |
| + Editor | ~30M | ~12 GB |
