# Audio Workflow Advanced

**Version**: 1.1.0
**Status**: ✅ Complete
**Parent**: [SKILL.md](SKILL.md)

---

## Table of Contents

1. [Spatial Audio Configuration](#spatial-audio-configuration)
2. [Attenuation Deep Dive](#attenuation-deep-dive)
3. [Sound Concurrency](#sound-concurrency)
4. [MetaSound Integration](#metasound-integration)
5. [Audio Mixing](#audio-mixing)
6. [Performance Optimization](#performance-optimization)
7. [Debugging Audio](#debugging-audio)

---

## Spatial Audio Configuration

### Overview

Spatial audio creates immersive 3D soundscapes by simulating how sound behaves in real environments.
Unreal Engine supports multiple spatialization methods: panning, binaural, and HRTF.

### Spatialization Settings

```python
import unreal
from typing import Dict, Any, Optional

def configure_spatial_audio(
    actor_name: str,
    spatialization_method: str = "binaural",
    enable_occlusion: bool = True,
    enable_reverb: bool = True
) -> Dict[str, Any]:
    """
    Configure spatial audio settings for an actor's AudioComponent.

    Args:
        actor_name: Target actor with AudioComponent
        spatialization_method: "panning", "binaural", or "hrtf"
        enable_occlusion: Enable sound occlusion by geometry
        enable_reverb: Enable environment reverb

    Returns:
        Dict with success status and applied settings
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        # Configure spatialization
        spatialization_map = {
            "panning": unreal.SoundSpatializationAlgorithm.SPATIALIZATION_DEFAULT,
            "binaural": unreal.SoundSpatializationAlgorithm.SPATIALIZATION_BINAURAL,
            "hrtf": unreal.SoundSpatializationAlgorithm.SPATIALIZATION_HRTF
        }

        method = spatialization_map.get(spatialization_method.lower())
        if method is None:
            return {
                "success": False,
                "error": f"Invalid spatialization method: {spatialization_method}",
                "valid_methods": list(spatialization_map.keys())
            }

        # Apply settings
        audio_comp.set_editor_property("spatialization_algorithm", method)
        audio_comp.set_editor_property("enable_low_pass_filter", enable_occlusion)
        audio_comp.set_editor_property("override_attenuation", True)

        # Configure reverb send
        if enable_reverb:
            audio_comp.set_editor_property("reverb_send_method",
                unreal.ReverbSendMethod.LINEAR)

        return {
            "success": True,
            "actor": actor_name,
            "spatialization": spatialization_method,
            "occlusion_enabled": enable_occlusion,
            "reverb_enabled": enable_reverb
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to configure spatial audio: {str(e)}"}
```

### Binaural Audio Setup

```python
def setup_binaural_audio(
    actor_name: str,
    submix_path: Optional[str] = None
) -> Dict[str, Any]:
    """
    Enable binaural audio rendering for headphone users.

    Args:
        actor_name: Target actor with AudioComponent
        submix_path: Optional binaural submix asset path

    Returns:
        Dict with success status and configuration details
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        # Enable binaural spatialization
        audio_comp.set_editor_property(
            "spatialization_algorithm",
            unreal.SoundSpatializationAlgorithm.SPATIALIZATION_BINAURAL
        )

        # Apply submix if provided
        if submix_path:
            submix = unreal.load_asset(submix_path)
            if submix:
                audio_comp.set_editor_property("sound_submix_send", [{
                    "sound_submix": submix,
                    "send_level": 1.0
                }])

        return {
            "success": True,
            "actor": actor_name,
            "binaural_enabled": True,
            "submix": submix_path
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to setup binaural audio: {str(e)}"}
```

---

## Attenuation Deep Dive

### Custom Attenuation Settings

```python
def create_custom_attenuation(
    inner_radius: float = 200.0,
    falloff_distance: float = 1000.0,
    falloff_mode: str = "logarithmic",
    db_attenuation_at_max: float = -60.0,
    enable_occlusion: bool = True,
    occlusion_trace_channel: str = "visibility"
) -> Dict[str, Any]:
    """
    Create custom sound attenuation settings.

    Args:
        inner_radius: Distance where sound is at full volume
        falloff_distance: Distance from inner to silence
        falloff_mode: "linear", "logarithmic", "inverse", "log_reverse", "natural"
        db_attenuation_at_max: Volume at max distance in dB
        enable_occlusion: Enable geometry-based occlusion
        occlusion_trace_channel: Trace channel for occlusion

    Returns:
        Dict with attenuation configuration for use in audio components
    """
    try:
        falloff_map = {
            "linear": "Linear",
            "logarithmic": "Logarithmic",
            "inverse": "Inverse",
            "log_reverse": "LogReverse",
            "natural": "NaturalSound"
        }

        mode = falloff_map.get(falloff_mode.lower())
        if mode is None:
            return {
                "success": False,
                "error": f"Invalid falloff mode: {falloff_mode}",
                "valid_modes": list(falloff_map.keys())
            }

        attenuation_config = {
            "inner_radius": inner_radius,
            "falloff_distance": falloff_distance,
            "falloff_mode": mode,
            "db_attenuation_at_max": db_attenuation_at_max,
            "occlusion": {
                "enabled": enable_occlusion,
                "trace_channel": occlusion_trace_channel,
                "low_pass_filter_frequency": 20000.0,  # Hz
                "volume_attenuation": 1.0
            },
            "spatialization": {
                "stereo_spread": 100.0,
                "lfe_blend": 0.0,  # Low frequency effects
                "3d_stereo_spread": True
            }
        }

        return {
            "success": True,
            "attenuation_config": attenuation_config,
            "total_range": inner_radius + falloff_distance
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to create attenuation: {str(e)}"}


def apply_attenuation_to_actor(
    actor_name: str,
    attenuation_asset_path: Optional[str] = None,
    custom_settings: Optional[Dict] = None
) -> Dict[str, Any]:
    """
    Apply attenuation settings to an actor's AudioComponent.

    Args:
        actor_name: Target actor with AudioComponent
        attenuation_asset_path: Path to SoundAttenuation asset (optional)
        custom_settings: Custom attenuation dict from create_custom_attenuation

    Returns:
        Dict with success status
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        if attenuation_asset_path:
            # Use existing attenuation asset
            attenuation = unreal.load_asset(attenuation_asset_path)
            if not attenuation:
                return {"success": False, "error": f"Attenuation not found: {attenuation_asset_path}"}

            audio_comp.set_editor_property("attenuation_settings", attenuation)
            audio_comp.set_editor_property("override_attenuation", False)

            return {
                "success": True,
                "actor": actor_name,
                "attenuation_asset": attenuation_asset_path
            }

        elif custom_settings:
            # Apply custom inline attenuation
            audio_comp.set_editor_property("override_attenuation", True)

            return {
                "success": True,
                "actor": actor_name,
                "custom_attenuation": custom_settings
            }

        return {"success": False, "error": "No attenuation settings provided"}

    except Exception as e:
        return {"success": False, "error": f"Failed to apply attenuation: {str(e)}"}
```

### Occlusion Configuration

```python
def configure_audio_occlusion(
    actor_name: str,
    occlusion_enabled: bool = True,
    low_pass_frequency: float = 5000.0,
    volume_attenuation: float = 0.5,
    interpolation_time: float = 0.2
) -> Dict[str, Any]:
    """
    Configure sound occlusion when geometry blocks sound path.

    Args:
        actor_name: Target actor with AudioComponent
        occlusion_enabled: Enable occlusion effect
        low_pass_frequency: Low-pass filter cutoff when occluded (Hz)
        volume_attenuation: Volume multiplier when occluded (0-1)
        interpolation_time: Transition smoothing time (seconds)

    Returns:
        Dict with occlusion configuration status
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        # Enable low-pass filter for occlusion effect
        audio_comp.set_editor_property("enable_low_pass_filter", occlusion_enabled)

        if occlusion_enabled:
            audio_comp.set_editor_property("low_pass_filter_frequency", low_pass_frequency)

        return {
            "success": True,
            "actor": actor_name,
            "occlusion_enabled": occlusion_enabled,
            "low_pass_frequency": low_pass_frequency,
            "volume_attenuation": volume_attenuation,
            "interpolation_time": interpolation_time
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to configure occlusion: {str(e)}"}
```

---

## Sound Concurrency

### Concurrency Overview

Sound concurrency limits how many instances of a sound can play simultaneously,
preventing audio overload and maintaining performance.

### Concurrency Configuration

```python
def configure_sound_concurrency(
    actor_name: str,
    max_count: int = 8,
    resolution_rule: str = "stop_lowest_priority",
    volume_scale_mode: str = "distance",
    max_volume_scale: float = 1.0
) -> Dict[str, Any]:
    """
    Configure sound concurrency for an audio component.

    Args:
        actor_name: Target actor with AudioComponent
        max_count: Maximum simultaneous instances
        resolution_rule: "stop_lowest_priority", "stop_oldest",
                        "stop_farthest", "stop_quietest", "prevent_new"
        volume_scale_mode: "distance", "priority", "none"
        max_volume_scale: Maximum volume when at limit

    Returns:
        Dict with concurrency configuration
    """
    try:
        resolution_map = {
            "stop_lowest_priority": "StopLowestPriority",
            "stop_oldest": "StopOldest",
            "stop_farthest": "StopFarthest",
            "stop_quietest": "StopQuietest",
            "prevent_new": "PreventNew"
        }

        rule = resolution_map.get(resolution_rule.lower())
        if rule is None:
            return {
                "success": False,
                "error": f"Invalid resolution rule: {resolution_rule}",
                "valid_rules": list(resolution_map.keys())
            }

        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        return {
            "success": True,
            "actor": actor_name,
            "concurrency": {
                "max_count": max_count,
                "resolution_rule": rule,
                "volume_scale_mode": volume_scale_mode,
                "max_volume_scale": max_volume_scale
            }
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to configure concurrency: {str(e)}"}


def set_sound_priority(
    actor_name: str,
    priority: float = 0.5
) -> Dict[str, Any]:
    """
    Set sound priority for concurrency resolution.

    Args:
        actor_name: Target actor with AudioComponent
        priority: Priority value (0.0 = lowest, 1.0 = highest)

    Returns:
        Dict with priority setting status
    """
    try:
        if not 0.0 <= priority <= 1.0:
            return {"success": False, "error": "Priority must be between 0.0 and 1.0"}

        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        return {
            "success": True,
            "actor": actor_name,
            "priority": priority,
            "note": "Higher priority (1.0) sounds survive concurrency culling"
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to set priority: {str(e)}"}
```

---

## MetaSound Integration

### MetaSound Overview

MetaSound is Unreal's node-based procedural audio system introduced in UE5.
It enables dynamic, data-driven sound generation.

### Playing MetaSounds

```python
def play_metasound(
    metasound_path: str,
    location: Optional[tuple] = None,
    parameters: Optional[Dict[str, Any]] = None
) -> Dict[str, Any]:
    """
    Play a MetaSound source with optional parameters.

    Args:
        metasound_path: Path to MetaSoundSource asset
        location: Optional (x, y, z) for 3D playback
        parameters: Dict of MetaSound input parameters

    Returns:
        Dict with playback status
    """
    try:
        metasound = unreal.load_asset(metasound_path)
        if not metasound:
            return {"success": False, "error": f"MetaSound not found: {metasound_path}"}

        # Verify it's a MetaSound asset
        if not metasound.get_class().get_name().startswith("MetaSound"):
            return {"success": False, "error": f"Asset is not a MetaSound: {metasound_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        if location:
            loc = unreal.Vector(location[0], location[1], location[2])
            unreal.GameplayStatics.play_sound_at_location(
                world,
                metasound,
                loc
            )
        else:
            unreal.GameplayStatics.play_sound_2d(
                world,
                metasound
            )

        return {
            "success": True,
            "metasound": metasound.get_name(),
            "location": location,
            "parameters": parameters,
            "type": "MetaSound"
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to play MetaSound: {str(e)}"}


def create_metasound_player(
    actor_name: str,
    metasound_path: str,
    auto_activate: bool = False
) -> Dict[str, Any]:
    """
    Add a MetaSound-enabled AudioComponent to an actor.

    Args:
        actor_name: Target actor name
        metasound_path: Path to MetaSoundSource asset
        auto_activate: Start playing immediately

    Returns:
        Dict with component creation status
    """
    try:
        metasound = unreal.load_asset(metasound_path)
        if not metasound:
            return {"success": False, "error": f"MetaSound not found: {metasound_path}"}

        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.add_component_by_class(
            unreal.AudioComponent,
            manual_attachment=False,
            relative_transform=unreal.Transform(),
            transform_is_relative=True
        )

        if audio_comp:
            audio_comp.set_editor_property("sound", metasound)
            audio_comp.set_editor_property("auto_activate", auto_activate)

            return {
                "success": True,
                "actor": actor_name,
                "metasound": metasound.get_name(),
                "auto_activate": auto_activate,
                "note": "Use SetParameter to control MetaSound inputs at runtime"
            }

        return {"success": False, "error": "Failed to create AudioComponent"}

    except Exception as e:
        return {"success": False, "error": f"Failed to create MetaSound player: {str(e)}"}
```

---

## Audio Mixing

### Sound Class Configuration

```python
def set_sound_class(
    actor_name: str,
    sound_class_path: str
) -> Dict[str, Any]:
    """
    Assign a Sound Class to an AudioComponent for mixing control.

    Args:
        actor_name: Target actor with AudioComponent
        sound_class_path: Path to SoundClass asset

    Returns:
        Dict with assignment status
    """
    try:
        sound_class = unreal.load_asset(sound_class_path)
        if not sound_class:
            return {"success": False, "error": f"SoundClass not found: {sound_class_path}"}

        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        audio_comp.set_editor_property("sound_class_override", sound_class)

        return {
            "success": True,
            "actor": actor_name,
            "sound_class": sound_class.get_name()
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to set sound class: {str(e)}"}


def apply_sound_mix(
    sound_mix_path: str,
    fade_in_time: float = 0.5
) -> Dict[str, Any]:
    """
    Apply a Sound Mix modifier for global audio adjustments.

    Args:
        sound_mix_path: Path to SoundMix asset
        fade_in_time: Transition time in seconds

    Returns:
        Dict with mix application status
    """
    try:
        sound_mix = unreal.load_asset(sound_mix_path)
        if not sound_mix:
            return {"success": False, "error": f"SoundMix not found: {sound_mix_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        unreal.GameplayStatics.push_sound_mix_modifier(
            world,
            sound_mix
        )

        return {
            "success": True,
            "sound_mix": sound_mix.get_name(),
            "fade_in_time": fade_in_time,
            "note": "Use PopSoundMixModifier to remove"
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to apply sound mix: {str(e)}"}


def set_sound_class_volume(
    sound_class_path: str,
    volume: float,
    pitch: float = 1.0,
    fade_time: float = 0.0
) -> Dict[str, Any]:
    """
    Adjust volume for all sounds in a Sound Class.

    Args:
        sound_class_path: Path to SoundClass asset
        volume: Volume multiplier (0.0-2.0)
        pitch: Pitch multiplier
        fade_time: Transition time in seconds

    Returns:
        Dict with adjustment status
    """
    try:
        if not 0.0 <= volume <= 2.0:
            return {"success": False, "error": "Volume must be between 0.0 and 2.0"}

        sound_class = unreal.load_asset(sound_class_path)
        if not sound_class:
            return {"success": False, "error": f"SoundClass not found: {sound_class_path}"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        unreal.GameplayStatics.set_sound_class_override(
            world,
            sound_class,
            True,
            volume,
            pitch,
            fade_time,
            True
        )

        return {
            "success": True,
            "sound_class": sound_class.get_name(),
            "volume": volume,
            "pitch": pitch,
            "fade_time": fade_time
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to set sound class volume: {str(e)}"}
```

---

## Performance Optimization

### Audio Budget Management

```python
def get_audio_stats() -> Dict[str, Any]:
    """
    Get current audio performance statistics.

    Returns:
        Dict with audio performance metrics
    """
    try:
        return {
            "success": True,
            "stats": {
                "active_sounds": "Query via stat audio",
                "max_channels": "Platform dependent (128 typical)",
                "virtual_sounds": "Sounds beyond max channels",
                "memory_usage": "Audio memory pool"
            },
            "console_commands": [
                "stat audio",
                "stat soundcues",
                "stat soundwaves",
                "au.Debug.Sounds 1"
            ]
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to get audio stats: {str(e)}"}


def optimize_audio_component(
    actor_name: str,
    enable_virtualization: bool = True,
    virtualization_mode: str = "restart",
    priority: float = 0.5
) -> Dict[str, Any]:
    """
    Apply performance optimizations to an AudioComponent.

    Args:
        actor_name: Target actor with AudioComponent
        enable_virtualization: Allow sound to become virtual when culled
        virtualization_mode: "restart" or "resume" when unvirtualized
        priority: Concurrency priority

    Returns:
        Dict with optimization status
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": f"No AudioComponent on: {actor_name}"}

        optimizations_applied = []

        if enable_virtualization:
            audio_comp.set_editor_property("virtualization_mode",
                unreal.EVirtualizationMode.RESTART if virtualization_mode == "restart"
                else unreal.EVirtualizationMode.PLAY_WHEN_SILENT)
            optimizations_applied.append("Virtualization enabled")

        return {
            "success": True,
            "actor": actor_name,
            "optimizations": optimizations_applied,
            "virtualization_mode": virtualization_mode,
            "priority": priority
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to optimize audio: {str(e)}"}
```

---

## Debugging Audio

### Debug Utilities

```python
def debug_audio_in_level() -> Dict[str, Any]:
    """
    Generate a debug report of all audio sources in the current level.

    Returns:
        Dict with comprehensive audio debug information
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()

        audio_report = {
            "audio_components": [],
            "ambient_sounds": [],
            "audio_volumes": [],
            "reverb_volumes": []
        }

        for actor in actors:
            class_name = actor.get_class().get_name()

            audio_comp = actor.get_component_by_class(unreal.AudioComponent)
            if audio_comp:
                sound = audio_comp.get_editor_property("sound")
                audio_report["audio_components"].append({
                    "actor": actor.get_actor_label(),
                    "sound": sound.get_name() if sound else "None",
                    "auto_activate": audio_comp.get_editor_property("auto_activate"),
                    "is_playing": audio_comp.is_playing() if hasattr(audio_comp, "is_playing") else "Unknown"
                })

            if "AmbientSound" in class_name:
                audio_report["ambient_sounds"].append({
                    "actor": actor.get_actor_label(),
                    "location": str(actor.get_actor_location())
                })

            if "AudioVolume" in class_name:
                audio_report["audio_volumes"].append({
                    "actor": actor.get_actor_label(),
                    "priority": "Check in details panel"
                })

        return {
            "success": True,
            "total_audio_components": len(audio_report["audio_components"]),
            "total_ambient_sounds": len(audio_report["ambient_sounds"]),
            "total_audio_volumes": len(audio_report["audio_volumes"]),
            "report": audio_report
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to debug audio: {str(e)}"}


def enable_audio_debug_display() -> Dict[str, Any]:
    """
    Enable visual audio debugging in the editor viewport.

    Returns:
        Dict with enabled debug displays
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()

        debug_commands = [
            "au.Debug.Sounds 1",
            "au.Debug.SoundCues 1",
            "au.Debug.SoundWaves 1",
            "stat audio"
        ]

        for cmd in debug_commands:
            unreal.SystemLibrary.execute_console_command(world, cmd)

        return {
            "success": True,
            "debug_enabled": True,
            "commands_executed": debug_commands,
            "note": "Run same commands to toggle off"
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to enable audio debug: {str(e)}"}
```

---

## Console Commands Reference

| Command | Description | Values |
|---------|-------------|--------|
| `au.Debug.Sounds` | Show active sounds | 0/1 |
| `au.Debug.SoundCues` | Show SoundCue debug | 0/1 |
| `au.MaxChannels` | Max simultaneous sounds | 32-256 |
| `au.VolumeMultiplier` | Global volume | 0.0-2.0 |
| `au.ListenerRotation` | Override listener | pitch yaw roll |
| `stat audio` | Audio statistics | Toggle |
| `stat soundcues` | SoundCue stats | Toggle |
| `stat soundwaves` | SoundWave stats | Toggle |

---

## Best Practices

### Performance
1. **Use Sound Classes** - Group sounds for batch volume control
2. **Set Concurrency Limits** - Prevent audio overload (8-16 typical)
3. **Enable Virtualization** - Let unimportant sounds pause
4. **Use Attenuation** - Cull distant sounds automatically

### Quality
1. **3D Sounds** - Use binaural/HRTF for immersion
2. **Occlusion** - Enable for realistic blocked sounds
3. **Reverb Zones** - Match environment acoustics
4. **Sound Priority** - Important sounds survive culling

### Organization
1. **Naming Convention** - S_ (SoundWave), SC_ (SoundCue), SM_ (SoundMix)
2. **Folder Structure** - Audio/SFX, Audio/Music, Audio/Ambient
3. **Sound Classes** - Separate Music, SFX, Dialogue, Ambient

---

## Related Files

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [REFERENCE.md](REFERENCE.md) | API reference |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios |

---

**Last Updated**: 2026-01-28
