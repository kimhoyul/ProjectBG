# Code Generation - Complete Reference

Complete API reference for code generation operations.

---

## ue_generate_code Operations

### generate_class

Generate from organism template.

```python
ue_generate_code(operation="generate_class", params={
    "organism": "character",
    "class_name": "AMyCharacter",
    "output_dir": "Source/MyGame/Characters"
})
```

### derive_class

Generate from ANY UE base class.

```python
ue_generate_code(operation="derive_class", params={
    "base_class": "ACharacter",
    "class_name": "AMyHero",
    "output_dir": "Source/MyGame/Characters",
    "overrides": ["BeginPlay", "Tick"],
    "features": ["gas_setup"]
})
```

### suggest_class

Get recommendations without generating code.

```python
ue_generate_code(operation="suggest_class", params={
    "base_class": "ACharacter"
})
# Returns: recommended_overrides, common_properties, common_components
```

### scaffold_class

Generate minimal skeleton for Edit tool workflow.

```python
ue_generate_code(operation="scaffold_class", params={
    "base_class": "ACharacter",
    "class_name": "AMyHero",
    "output_dir": "Source/MyGame/Characters"
})
```

### batch_class

Generate multiple classes at once.

```python
ue_generate_code(operation="batch_class", params={
    "classes": [
        {"base_class": "ACharacter", "class_name": "AHero"},
        {"base_class": "UActorComponent", "class_name": "UHealthComponent"}
    ],
    "output_dir": "Source/MyGame"
})
```

---

## 12 Organism Templates

| Organism | Base Class | Use Case |
|----------|------------|----------|
| `character` | ACharacter | Basic character |
| `component` | UActorComponent | Actor component |
| `gameplay_ability` | UGameplayAbility | GAS ability |
| `attribute_set` | UAttributeSet | GAS attributes |
| `data_asset` | UPrimaryDataAsset | Data management |
| `lyra_character` | AModularCharacter | Lyra-style |
| `lyra_player_controller` | AModularPlayerController | Multiplayer |
| `lyra_player_state` | AModularPlayerState | Player state |
| `lyra_game_mode` | AModularGameModeBase | Experience system |
| `lyra_game_state` | AModularGameStateBase | Server metrics |
| `lyra_gameplay_ability` | ULyraGameplayAbility | Lyra ability |
| `input_config` | UDataAsset | Enhanced Input |

---

## 21 Features

| Feature | Description |
|---------|-------------|
| `gas_setup` | GameplayAbilitySystem setup |
| `replication` | Network replication |
| `team_system` | Team interface |
| `save_game` | Save/Load support |
| ... | See CODE_GENERATION_GUIDE.md |

---

## Validation Error Codes

| Code | Description | Solution |
|------|-------------|----------|
| `INVALID_ORGANISM` | Unknown organism name | Use valid organism |
| `INVALID_CLASS_PREFIX` | Missing A/U/F prefix | Add correct prefix |
| `MODULE_NOT_FOUND` | Output dir not in module | Use correct Source path |

---

## Related Documentation

- [Code Generation Guide](../../../docs/user-guides/tools/CODE_GENERATION_GUIDE.md)
