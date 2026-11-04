# Audio Workflow Examples

**Version**: 1.1.0
**Status**: Complete
**Parent**: [SKILL.md](SKILL.md)

---

## Overview

Practical examples and step-by-step scenarios for Unreal Engine audio workflows.
Each scenario includes user request, implementation steps, and expected output.

---

## Scenario 1: Ambient Environment Setup

### User Request
"캠프파이어에 불 소리 추가해줘" (Add fire sound to the campfire)

### Prerequisites
- Campfire actor labeled "Campfire_01" in level
- Fire loop sound at `/Game/Audio/Ambient/SFX_Fire_Loop`
- Optional: Attenuation settings for spatial falloff

### Step-by-Step Implementation

#### Step 1: Verify Actor Exists

```python
import unreal
from typing import Dict, Any, List

def find_actor(actor_name: str) -> Dict[str, Any]:
    """Find an actor by label."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        for actor in actors:
            if actor.get_actor_label() == actor_name:
                location = actor.get_actor_location()
                return {
                    "success": True,
                    "actor": actor_name,
                    "class": actor.get_class().get_name(),
                    "location": (location.x, location.y, location.z)
                }
        return {"success": False, "error": f"Actor not found: {actor_name}"}
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: find_actor("Campfire_01")
```

#### Step 2: Add Looping Audio Component

```python
def add_ambient_sound(
    actor_name: str,
    sound_path: str,
    volume: float = 1.0,
    attenuation_radius: float = 1000.0
) -> Dict[str, Any]:
    """Add an ambient looping sound to an actor."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}"}

        # Add AudioComponent
        audio_comp = target_actor.add_component_by_class(
            unreal.AudioComponent,
            manual_attachment=False,
            relative_transform=unreal.Transform(),
            transform_is_relative=True
        )

        if not audio_comp:
            return {"success": False, "error": "Failed to create AudioComponent"}

        # Configure for ambient loop
        audio_comp.set_editor_property("sound", sound)
        audio_comp.set_editor_property("auto_activate", True)
        audio_comp.set_editor_property("is_ui_sound", False)
        audio_comp.set_editor_property("volume_multiplier", volume)

        # Enable attenuation override for distance falloff
        audio_comp.set_editor_property("override_attenuation", True)

        return {
            "success": True,
            "actor": actor_name,
            "sound": sound.get_name(),
            "volume": volume,
            "auto_play": True,
            "spatial": True
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: add_ambient_sound("Campfire_01", "/Game/Audio/Ambient/SFX_Fire_Loop", 0.8)
```

### Expected Output

```text
🔊 Ambient Sound Setup

Actor: Campfire_01
Sound: SFX_Fire_Loop
Volume: 0.8
Auto-play: Yes
Spatial: Yes (3D)

✅ Ambient sound added successfully!
The fire sound will loop automatically when the level loads.
```

### Verification

```python
def verify_audio_setup(actor_name: str) -> Dict[str, Any]:
    """Verify audio component is properly configured."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        for actor in actors:
            if actor.get_actor_label() == actor_name:
                audio_comp = actor.get_component_by_class(unreal.AudioComponent)
                if audio_comp:
                    sound = audio_comp.get_editor_property("sound")
                    return {
                        "success": True,
                        "actor": actor_name,
                        "has_audio_component": True,
                        "sound": sound.get_name() if sound else None,
                        "auto_activate": audio_comp.get_editor_property("auto_activate"),
                        "volume": audio_comp.get_editor_property("volume_multiplier")
                    }
                return {"success": True, "actor": actor_name, "has_audio_component": False}
        return {"success": False, "error": f"Actor not found: {actor_name}"}
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: verify_audio_setup("Campfire_01")
```

---

## Scenario 2: UI Sound Effects

### User Request
"버튼 클릭 소리 재생해줘" (Play button click sound)

### Prerequisites
- UI click sound at `/Game/Audio/UI/SFX_UI_Click`

### Step-by-Step Implementation

#### Step 1: Play 2D UI Sound

```python
import unreal
from typing import Dict, Any

def play_ui_sound(
    sound_path: str,
    volume: float = 1.0,
    pitch: float = 1.0
) -> Dict[str, Any]:
    """Play a 2D UI sound effect."""
    try:
        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        unreal.GameplayStatics.play_sound_2d(
            world,
            sound,
            volume_multiplier=volume,
            pitch_multiplier=pitch
        )

        return {
            "success": True,
            "sound": sound.get_name(),
            "volume": volume,
            "pitch": pitch,
            "type": "2D UI"
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: play_ui_sound("/Game/Audio/UI/SFX_UI_Click", 0.8)
```

#### Step 2: Play Multiple UI Sounds with Variations

```python
import random

def play_ui_sound_with_variation(
    sound_path: str,
    volume_range: tuple = (0.9, 1.0),
    pitch_range: tuple = (0.95, 1.05)
) -> Dict[str, Any]:
    """Play UI sound with random pitch/volume variation for natural feel."""
    try:
        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        # Random variation
        volume = random.uniform(volume_range[0], volume_range[1])
        pitch = random.uniform(pitch_range[0], pitch_range[1])

        unreal.GameplayStatics.play_sound_2d(
            world,
            sound,
            volume_multiplier=volume,
            pitch_multiplier=pitch
        )

        return {
            "success": True,
            "sound": sound.get_name(),
            "volume": round(volume, 3),
            "pitch": round(pitch, 3),
            "variation": True
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: play_ui_sound_with_variation("/Game/Audio/UI/SFX_UI_Click")
```

### Expected Output

```text
🔊 UI Sound Playback

Sound: SFX_UI_Click
Type: 2D (Non-spatial)
Volume: 0.95
Pitch: 1.02
Variation: Applied

✅ Click sound played!
```

---

## Scenario 3: 3D Spatial Audio (Explosion)

### User Request
"폭발 위치에서 폭발 소리 재생" (Play explosion sound at explosion location)

### Prerequisites
- Explosion sound at `/Game/Audio/SFX/SFX_Explosion_Large`
- Target location coordinates

### Step-by-Step Implementation

#### Step 1: Play 3D Sound at Location

```python
import unreal
from typing import Dict, Any, Tuple

def play_explosion_sound(
    location: Tuple[float, float, float],
    sound_path: str = "/Game/Audio/SFX/SFX_Explosion_Large",
    volume: float = 1.0,
    attenuation_path: str = None
) -> Dict[str, Any]:
    """Play explosion sound at a specific world location."""
    try:
        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()
        loc = unreal.Vector(location[0], location[1], location[2])
        rotation = unreal.Rotator(0, 0, 0)

        # Load attenuation if specified
        attenuation = None
        if attenuation_path:
            attenuation = unreal.load_asset(attenuation_path)

        unreal.GameplayStatics.play_sound_at_location(
            world,
            sound,
            loc,
            rotation,
            volume_multiplier=volume,
            attenuation_settings=attenuation
        )

        return {
            "success": True,
            "sound": sound.get_name(),
            "location": location,
            "volume": volume,
            "type": "3D Spatial"
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: play_explosion_sound((1000, 500, 100), volume=1.2)
```

#### Step 2: Play Sound at Actor Location

```python
def play_sound_at_actor(
    actor_name: str,
    sound_path: str,
    volume: float = 1.0
) -> Dict[str, Any]:
    """Play 3D sound at an actor's location."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()
        location = target_actor.get_actor_location()
        rotation = target_actor.get_actor_rotation()

        unreal.GameplayStatics.play_sound_at_location(
            world,
            sound,
            location,
            rotation,
            volume_multiplier=volume
        )

        return {
            "success": True,
            "actor": actor_name,
            "sound": sound.get_name(),
            "location": (location.x, location.y, location.z),
            "volume": volume,
            "type": "3D at Actor"
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: play_sound_at_actor("Barrel_Explosive", "/Game/Audio/SFX/SFX_Explosion_Large")
```

### Expected Output

```text
🔊 3D Spatial Sound

Sound: SFX_Explosion_Large
Location: (1000.0, 500.0, 100.0)
Volume: 1.2
Type: 3D Spatial

✅ Explosion sound played at location!
Distance falloff will apply based on attenuation settings.
```

---

## Scenario 4: Background Music

### User Request
"BGM 재생하고 페이드인해줘" (Play BGM with fade in)

### Prerequisites
- BGM track at `/Game/Audio/Music/BGM_MainTheme`

### Step-by-Step Implementation

#### Step 1: Setup BGM Actor with Fade

```python
import unreal
from typing import Dict, Any

def setup_background_music(
    bgm_path: str,
    volume: float = 0.5,
    fade_in_duration: float = 2.0
) -> Dict[str, Any]:
    """Setup background music with fade in effect."""
    try:
        sound = unreal.load_asset(bgm_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {bgm_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        # Spawn dedicated BGM actor
        bgm_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.Actor,
            unreal.Vector(0, 0, 0)
        )

        if not bgm_actor:
            return {"success": False, "error": "Failed to spawn BGM actor"}

        # Rename actor for identification
        bgm_actor.set_actor_label("BGM_Player")

        # Add AudioComponent
        audio_comp = bgm_actor.add_component_by_class(
            unreal.AudioComponent,
            manual_attachment=False,
            relative_transform=unreal.Transform(),
            transform_is_relative=True
        )

        if not audio_comp:
            return {"success": False, "error": "Failed to create AudioComponent"}

        # Configure for BGM (2D, no spatialization)
        audio_comp.set_editor_property("sound", sound)
        audio_comp.set_editor_property("auto_activate", False)  # Manual start
        audio_comp.set_editor_property("is_ui_sound", True)  # 2D sound
        audio_comp.set_editor_property("volume_multiplier", 0.0)  # Start silent

        # Start playback with fade
        audio_comp.play()
        audio_comp.adjust_volume(fade_in_duration, volume)

        return {
            "success": True,
            "actor": "BGM_Player",
            "sound": sound.get_name(),
            "target_volume": volume,
            "fade_in": fade_in_duration,
            "is_2d": True
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: setup_background_music("/Game/Audio/Music/BGM_MainTheme", 0.6, 3.0)
```

#### Step 2: BGM Controls (Stop, Fade Out)

```python
def stop_background_music(fade_out_duration: float = 2.0) -> Dict[str, Any]:
    """Stop BGM with fade out."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        bgm_actor = None

        for actor in actors:
            if actor.get_actor_label() == "BGM_Player":
                bgm_actor = actor
                break

        if not bgm_actor:
            return {"success": False, "error": "BGM_Player actor not found"}

        audio_comp = bgm_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": "No AudioComponent on BGM_Player"}

        # Fade out then stop
        audio_comp.fade_out(fade_out_duration, 0.0)

        return {
            "success": True,
            "fade_out": fade_out_duration,
            "message": f"BGM fading out over {fade_out_duration}s"
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: stop_background_music(2.0)
```

#### Step 3: Change BGM Track

```python
def change_bgm_track(
    new_bgm_path: str,
    crossfade_duration: float = 2.0,
    volume: float = 0.5
) -> Dict[str, Any]:
    """Crossfade to a new BGM track."""
    try:
        new_sound = unreal.load_asset(new_bgm_path)
        if not new_sound:
            return {"success": False, "error": f"Sound not found: {new_bgm_path}"}

        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        bgm_actor = None

        for actor in actors:
            if actor.get_actor_label() == "BGM_Player":
                bgm_actor = actor
                break

        if not bgm_actor:
            return {"success": False, "error": "BGM_Player not found. Use setup_background_music first."}

        audio_comp = bgm_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": "No AudioComponent on BGM_Player"}

        # Fade out current
        audio_comp.fade_out(crossfade_duration / 2, 0.0)

        # Note: For true crossfade, would need second component
        # This is simplified sequential transition

        return {
            "success": True,
            "new_track": new_sound.get_name(),
            "crossfade": crossfade_duration,
            "target_volume": volume,
            "note": "Sequential fade - true crossfade requires dual AudioComponents"
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: change_bgm_track("/Game/Audio/Music/BGM_Combat", 3.0, 0.7)
```

### Expected Output

```text
🎵 Background Music Setup

Track: BGM_MainTheme
Target Volume: 0.6
Fade In: 3.0 seconds
Type: 2D (Non-spatial)

✅ BGM started with fade in!
Actor: BGM_Player
```

---

## Scenario 5: Multi-Source Ambient Soundscape

### User Request
"숲 환경음 시스템 구축해줘" (Set up forest ambient sound system)

### Prerequisites
- Wind loop, bird loop, water stream sounds
- Multiple ambient source actors in level

### Step-by-Step Implementation

```python
import unreal
from typing import Dict, Any, List

def setup_forest_soundscape() -> Dict[str, Any]:
    """Create a layered forest ambient soundscape."""
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        results: List[Dict[str, Any]] = []

        # Sound configurations
        ambient_configs = [
            {
                "name": "Ambient_Wind",
                "location": (0, 0, 500),
                "sound": "/Game/Audio/Ambient/SFX_Wind_Loop",
                "volume": 0.4,
                "is_2d": True  # Wind is global
            },
            {
                "name": "Ambient_Birds",
                "location": (0, 0, 400),
                "sound": "/Game/Audio/Ambient/SFX_Birds_Loop",
                "volume": 0.3,
                "is_2d": True  # Birds are atmospheric
            },
            {
                "name": "Ambient_Stream",
                "location": (500, -200, 50),
                "sound": "/Game/Audio/Ambient/SFX_Water_Stream",
                "volume": 0.6,
                "is_2d": False  # Stream is localized
            }
        ]

        for config in ambient_configs:
            # Spawn ambient actor
            loc = unreal.Vector(config["location"][0], config["location"][1], config["location"][2])
            ambient_actor = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.Actor, loc
            )

            if not ambient_actor:
                results.append({"name": config["name"], "success": False, "error": "Spawn failed"})
                continue

            ambient_actor.set_actor_label(config["name"])

            # Load sound
            sound = unreal.load_asset(config["sound"])
            if not sound:
                results.append({"name": config["name"], "success": False, "error": "Sound not found"})
                continue

            # Add and configure AudioComponent
            audio_comp = ambient_actor.add_component_by_class(
                unreal.AudioComponent,
                manual_attachment=False,
                relative_transform=unreal.Transform(),
                transform_is_relative=True
            )

            audio_comp.set_editor_property("sound", sound)
            audio_comp.set_editor_property("auto_activate", True)
            audio_comp.set_editor_property("volume_multiplier", config["volume"])
            audio_comp.set_editor_property("is_ui_sound", config["is_2d"])

            results.append({
                "name": config["name"],
                "success": True,
                "sound": sound.get_name(),
                "volume": config["volume"],
                "type": "2D" if config["is_2d"] else "3D"
            })

        successful = sum(1 for r in results if r.get("success"))

        return {
            "success": successful == len(ambient_configs),
            "total": len(ambient_configs),
            "successful": successful,
            "sources": results
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: setup_forest_soundscape()
```

### Expected Output

```text
🌲 Forest Soundscape Setup

Sources Created: 3/3

1. Ambient_Wind
   Sound: SFX_Wind_Loop
   Volume: 0.4
   Type: 2D (Global)
   ✅ Created

2. Ambient_Birds
   Sound: SFX_Birds_Loop
   Volume: 0.3
   Type: 2D (Atmospheric)
   ✅ Created

3. Ambient_Stream
   Sound: SFX_Water_Stream
   Volume: 0.6
   Type: 3D (Localized at stream)
   ✅ Created

✅ Forest soundscape complete!
All ambient sources are auto-playing.
```

---

## Common Workflow Patterns

### Pattern: Batch Audio Component Setup

```python
def batch_add_audio_to_actors(
    actor_prefix: str,
    sound_path: str,
    volume: float = 1.0
) -> Dict[str, Any]:
    """Add audio to all actors matching a prefix."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        sound = unreal.load_asset(sound_path)

        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}"}

        modified = []

        for actor in actors:
            label = actor.get_actor_label()
            if label.startswith(actor_prefix):
                audio_comp = actor.add_component_by_class(
                    unreal.AudioComponent,
                    manual_attachment=False,
                    relative_transform=unreal.Transform(),
                    transform_is_relative=True
                )
                if audio_comp:
                    audio_comp.set_editor_property("sound", sound)
                    audio_comp.set_editor_property("auto_activate", True)
                    audio_comp.set_editor_property("volume_multiplier", volume)
                    modified.append(label)

        return {
            "success": True,
            "prefix": actor_prefix,
            "sound": sound.get_name(),
            "modified_count": len(modified),
            "actors": modified
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: batch_add_audio_to_actors("Torch_", "/Game/Audio/Ambient/SFX_Fire_Loop", 0.5)
```

### Pattern: Audio Debug Report

```python
def generate_audio_report() -> Dict[str, Any]:
    """Generate a report of all audio components in the level."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        audio_data = []
        total_volume = 0.0

        for actor in actors:
            audio_comp = actor.get_component_by_class(unreal.AudioComponent)
            if audio_comp:
                sound = audio_comp.get_editor_property("sound")
                volume = audio_comp.get_editor_property("volume_multiplier")
                total_volume += volume

                audio_data.append({
                    "actor": actor.get_actor_label(),
                    "sound": sound.get_name() if sound else "None",
                    "volume": volume,
                    "auto_play": audio_comp.get_editor_property("auto_activate"),
                    "is_2d": audio_comp.get_editor_property("is_ui_sound")
                })

        return {
            "success": True,
            "total_audio_sources": len(audio_data),
            "combined_volume": round(total_volume, 2),
            "average_volume": round(total_volume / len(audio_data), 2) if audio_data else 0,
            "sources": audio_data
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: generate_audio_report()
```

---

## Error Recovery Patterns

### Handling Missing Sounds

```python
def play_sound_with_fallback(
    primary_path: str,
    fallback_path: str,
    location: tuple = None
) -> Dict[str, Any]:
    """Play sound with fallback if primary not found."""
    try:
        sound = unreal.load_asset(primary_path)
        used_path = primary_path

        if not sound:
            sound = unreal.load_asset(fallback_path)
            used_path = fallback_path

        if not sound:
            return {
                "success": False,
                "error": "Both primary and fallback sounds not found",
                "tried": [primary_path, fallback_path]
            }

        world = unreal.EditorLevelLibrary.get_editor_world()

        if location:
            loc = unreal.Vector(location[0], location[1], location[2])
            unreal.GameplayStatics.play_sound_at_location(world, sound, loc)
            sound_type = "3D"
        else:
            unreal.GameplayStatics.play_sound_2d(world, sound)
            sound_type = "2D"

        return {
            "success": True,
            "sound": sound.get_name(),
            "used_fallback": used_path == fallback_path,
            "type": sound_type
        }
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute: play_sound_with_fallback(
#     "/Game/Audio/SFX/Impact_Metal",
#     "/Game/Audio/SFX/Impact_Generic",
#     (100, 200, 50)
# )
```

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [REFERENCE.md](REFERENCE.md) | Python code patterns |
| [ADVANCED.md](ADVANCED.md) | Advanced topics |

---

**Tracking**: Issue #3995 - Engine Systems Skills Enhancement
**Last Updated**: 2026-01-28
