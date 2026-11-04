# Audio Workflow Reference

**Version**: 1.2.0 (Issue #4442 - 2-tool upgrade)
**Status**: Complete
**Parent**: [SKILL.md](SKILL.md)

---

## Overview

Complete Python code patterns for Unreal Engine audio workflows via Remote Control API.
All functions return `Dict[str, Any]` with `{"success": bool, ...}` for MCP protocol compliance.

---

## Function Reference

### play_2d_sound

Play a 2D (non-spatial) sound for UI or system audio.

```python
import unreal
from typing import Dict, Any, Optional

def play_2d_sound(
    sound_path: str,
    volume: float = 1.0,
    pitch: float = 1.0,
    start_time: float = 0.0,
    concurrency_settings: Optional[str] = None
) -> Dict[str, Any]:
    """Play a 2D sound (non-spatial, UI sounds).

    Args:
        sound_path: Content path to sound asset (e.g., "/Game/Audio/UI/Click")
        volume: Volume multiplier (0.0-2.0, default: 1.0)
        pitch: Pitch multiplier (0.5-2.0, default: 1.0)
        start_time: Start playback offset in seconds
        concurrency_settings: Optional concurrency settings asset path

    Returns:
        dict: Playback result with sound info
            - success: bool
            - sound: str (asset name)
            - volume: float
            - pitch: float
            - type: "2D"

    Error Codes:
        AUDIO_001: Sound asset not found
        AUDIO_002: Invalid volume range
        AUDIO_003: Invalid pitch range
    """
    try:
        # Validate parameters
        if not 0.0 <= volume <= 2.0:
            return {"success": False, "error": "Volume must be 0.0-2.0",
                    "code": "AUDIO_002"}
        if not 0.5 <= pitch <= 2.0:
            return {"success": False, "error": "Pitch must be 0.5-2.0",
                    "code": "AUDIO_003"}

        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}",
                    "code": "AUDIO_001"}

        world = unreal.EditorLevelLibrary.get_editor_world()

        # Load concurrency if specified
        concurrency = None
        if concurrency_settings:
            concurrency = unreal.load_asset(concurrency_settings)

        unreal.GameplayStatics.play_sound_2d(
            world,
            sound,
            volume_multiplier=volume,
            pitch_multiplier=pitch,
            start_time=start_time,
            concurrency_settings=concurrency
        )

        return {
            "success": True,
            "sound": sound.get_name(),
            "path": sound_path,
            "volume": volume,
            "pitch": pitch,
            "start_time": start_time,
            "type": "2D"
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to play sound: {str(e)}"}
```markdown

**Parameters:**

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `sound_path` | str | - | Required | Content path to sound asset |
| `volume` | float | 0.0-2.0 | 1.0 | Volume multiplier |
| `pitch` | float | 0.5-2.0 | 1.0 | Pitch multiplier |
| `start_time` | float | 0.0+ | 0.0 | Playback start offset |
| `concurrency_settings` | str | - | None | Concurrency asset path |

---

### play_3d_sound

Play a 3D spatial sound at a specific world location.

```python
import unreal
from typing import Dict, Any, Tuple, Optional

def play_3d_sound(
    sound_path: str,
    location: Tuple[float, float, float],
    volume: float = 1.0,
    pitch: float = 1.0,
    attenuation_settings: Optional[str] = None,
    start_time: float = 0.0
) -> Dict[str, Any]:
    """Play a 3D spatial sound at a specific location.

    Args:
        sound_path: Content path to sound asset
        location: World location (X, Y, Z) in Unreal units
        volume: Volume multiplier (0.0-2.0)
        pitch: Pitch multiplier (0.5-2.0)
        attenuation_settings: Optional attenuation asset path
        start_time: Start playback offset in seconds

    Returns:
        dict: Playback result with location info
            - success: bool
            - sound: str
            - location: tuple
            - type: "3D"

    Error Codes:
        AUDIO_001: Sound asset not found
        AUDIO_004: Invalid location format
        AUDIO_005: Attenuation asset not found
    """
    try:
        if len(location) != 3:
            return {"success": False, "error": "Location must be (X, Y, Z)",
                    "code": "AUDIO_004"}

        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}",
                    "code": "AUDIO_001"}

        world = unreal.EditorLevelLibrary.get_editor_world()
        loc = unreal.Vector(location[0], location[1], location[2])
        rotation = unreal.Rotator(0, 0, 0)

        # Load attenuation if specified
        attenuation = None
        if attenuation_settings:
            attenuation = unreal.load_asset(attenuation_settings)
            if not attenuation:
                return {"success": False,
                        "error": f"Attenuation not found: {attenuation_settings}",
                        "code": "AUDIO_005"}

        unreal.GameplayStatics.play_sound_at_location(
            world,
            sound,
            loc,
            rotation,
            volume_multiplier=volume,
            pitch_multiplier=pitch,
            start_time=start_time,
            attenuation_settings=attenuation
        )

        return {
            "success": True,
            "sound": sound.get_name(),
            "path": sound_path,
            "location": location,
            "volume": volume,
            "pitch": pitch,
            "type": "3D"
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to play 3D sound: {str(e)}"}
```markdown

**Parameters:**

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `sound_path` | str | - | Required | Content path to sound asset |
| `location` | tuple | (X, Y, Z) | Required | World location |
| `volume` | float | 0.0-2.0 | 1.0 | Volume multiplier |
| `pitch` | float | 0.5-2.0 | 1.0 | Pitch multiplier |
| `attenuation_settings` | str | - | None | Attenuation asset path |
| `start_time` | float | 0.0+ | 0.0 | Playback start offset |

---

### add_audio_component

Add an AudioComponent to an actor for persistent or looping sounds.

```python
import unreal
from typing import Dict, Any, Optional

def add_audio_component(
    actor_name: str,
    sound_path: str,
    auto_play: bool = True,
    is_looping: bool = False,
    volume: float = 1.0,
    attenuation_settings: Optional[str] = None
) -> Dict[str, Any]:
    """Add an AudioComponent to an actor for persistent sounds.

    Args:
        actor_name: Target actor label in level
        sound_path: Content path to sound asset
        auto_play: Auto-activate on spawn (default: True)
        is_looping: Enable sound looping
        volume: Initial volume multiplier
        attenuation_settings: Optional attenuation asset path

    Returns:
        dict: Component creation result
            - success: bool
            - actor: str
            - sound: str
            - component_name: str
            - auto_play: bool
            - is_looping: bool

    Error Codes:
        AUDIO_001: Sound asset not found
        AUDIO_006: Actor not found
        AUDIO_007: Component creation failed
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}",
                    "code": "AUDIO_006"}

        sound = unreal.load_asset(sound_path)
        if not sound:
            return {"success": False, "error": f"Sound not found: {sound_path}",
                    "code": "AUDIO_001"}

        # Add AudioComponent
        audio_comp = target_actor.add_component_by_class(
            unreal.AudioComponent,
            manual_attachment=False,
            relative_transform=unreal.Transform(),
            transform_is_relative=True
        )

        if not audio_comp:
            return {"success": False, "error": "Failed to create AudioComponent",
                    "code": "AUDIO_007"}

        # Configure component
        audio_comp.set_editor_property("sound", sound)
        audio_comp.set_editor_property("auto_activate", auto_play)
        audio_comp.set_editor_property("is_ui_sound", False)
        audio_comp.set_editor_property("volume_multiplier", volume)

        # Set looping via sound wave property or component override
        if is_looping:
            audio_comp.set_editor_property("override_attenuation", True)

        # Load attenuation if specified
        if attenuation_settings:
            attenuation = unreal.load_asset(attenuation_settings)
            if attenuation:
                audio_comp.set_editor_property("attenuation_settings", attenuation)

        return {
            "success": True,
            "actor": actor_name,
            "sound": sound.get_name(),
            "component_name": audio_comp.get_name(),
            "auto_play": auto_play,
            "is_looping": is_looping,
            "volume": volume
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to add audio component: {str(e)}"}
```markdown

---

### stop_audio_component

Stop playback on an actor's AudioComponent.

```python
import unreal
from typing import Dict, Any

def stop_audio_component(
    actor_name: str,
    fade_out_duration: float = 0.0
) -> Dict[str, Any]:
    """Stop audio playback on an actor's AudioComponent.

    Args:
        actor_name: Target actor label
        fade_out_duration: Fade out time in seconds (0 = immediate)

    Returns:
        dict: Stop result
            - success: bool
            - actor: str
            - fade_out: float

    Error Codes:
        AUDIO_006: Actor not found
        AUDIO_008: No AudioComponent found
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}",
                    "code": "AUDIO_006"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": "No AudioComponent on actor",
                    "code": "AUDIO_008"}

        if fade_out_duration > 0:
            audio_comp.fade_out(fade_out_duration, 0.0)
        else:
            audio_comp.stop()

        return {
            "success": True,
            "actor": actor_name,
            "fade_out_duration": fade_out_duration,
            "stopped": True
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to stop audio: {str(e)}"}
```markdown

---

### set_audio_volume

Adjust volume on an actor's AudioComponent.

```python
import unreal
from typing import Dict, Any

def set_audio_volume(
    actor_name: str,
    volume: float,
    fade_duration: float = 0.0
) -> Dict[str, Any]:
    """Set volume on an actor's AudioComponent.

    Args:
        actor_name: Target actor label
        volume: New volume multiplier (0.0-2.0)
        fade_duration: Transition time in seconds

    Returns:
        dict: Volume change result
            - success: bool
            - actor: str
            - volume: float
            - fade_duration: float

    Error Codes:
        AUDIO_002: Invalid volume range
        AUDIO_006: Actor not found
        AUDIO_008: No AudioComponent found
    """
    try:
        if not 0.0 <= volume <= 2.0:
            return {"success": False, "error": "Volume must be 0.0-2.0",
                    "code": "AUDIO_002"}

        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}",
                    "code": "AUDIO_006"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": "No AudioComponent on actor",
                    "code": "AUDIO_008"}

        if fade_duration > 0:
            audio_comp.adjust_volume(fade_duration, volume)
        else:
            audio_comp.set_editor_property("volume_multiplier", volume)

        return {
            "success": True,
            "actor": actor_name,
            "volume": volume,
            "fade_duration": fade_duration
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to set volume: {str(e)}"}
```markdown

---

### get_audio_info

Get information about an actor's AudioComponent.

```python
import unreal
from typing import Dict, Any

def get_audio_info(actor_name: str) -> Dict[str, Any]:
    """Get AudioComponent information for an actor.

    Args:
        actor_name: Target actor label

    Returns:
        dict: AudioComponent info
            - success: bool
            - actor: str
            - sound: str (if assigned)
            - is_playing: bool
            - volume: float
            - pitch: float
            - is_looping: bool
            - auto_activate: bool

    Error Codes:
        AUDIO_006: Actor not found
        AUDIO_008: No AudioComponent found
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}",
                    "code": "AUDIO_006"}

        audio_comp = target_actor.get_component_by_class(unreal.AudioComponent)
        if not audio_comp:
            return {"success": False, "error": "No AudioComponent on actor",
                    "code": "AUDIO_008"}

        sound = audio_comp.get_editor_property("sound")
        sound_name = sound.get_name() if sound else None

        return {
            "success": True,
            "actor": actor_name,
            "sound": sound_name,
            "is_playing": audio_comp.is_playing(),
            "volume": audio_comp.get_editor_property("volume_multiplier"),
            "pitch": audio_comp.get_editor_property("pitch_multiplier"),
            "auto_activate": audio_comp.get_editor_property("auto_activate"),
            "is_ui_sound": audio_comp.get_editor_property("is_ui_sound")
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to get audio info: {str(e)}"}
```markdown

---

### list_audio_actors

List all actors with AudioComponents in the level.

```python
import unreal
from typing import Dict, Any, List

def list_audio_actors() -> Dict[str, Any]:
    """List all actors with AudioComponents in the current level.

    Returns:
        dict: List of audio actors
            - success: bool
            - count: int
            - actors: list of audio actor info

    Error Codes:
        AUDIO_009: No actors in level
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()

        if not actors:
            return {"success": False, "error": "No actors in level",
                    "code": "AUDIO_009"}

        audio_actors: List[Dict[str, Any]] = []

        for actor in actors:
            audio_comp = actor.get_component_by_class(unreal.AudioComponent)
            if audio_comp:
                sound = audio_comp.get_editor_property("sound")
                audio_actors.append({
                    "name": actor.get_actor_label(),
                    "class": actor.get_class().get_name(),
                    "sound": sound.get_name() if sound else None,
                    "is_playing": audio_comp.is_playing(),
                    "auto_activate": audio_comp.get_editor_property("auto_activate")
                })

        return {
            "success": True,
            "count": len(audio_actors),
            "actors": audio_actors
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to list audio actors: {str(e)}"}
```markdown

---

## Type Definitions

### AudioInfo

```python
from typing import TypedDict, Optional

class AudioInfo(TypedDict):
    """Information about an audio component."""
    actor: str
    sound: Optional[str]
    is_playing: bool
    volume: float
    pitch: float
    auto_activate: bool
    is_ui_sound: bool
```markdown

### SoundPlaybackResult

```python
class SoundPlaybackResult(TypedDict):
    """Result of sound playback operation."""
    success: bool
    sound: str
    path: str
    volume: float
    pitch: float
    type: str  # "2D" or "3D"
    location: Optional[tuple]  # Only for 3D
```markdown

---

## Error Codes

| Code | Name | Description | Recovery |
|------|------|-------------|----------|
| AUDIO_001 | SOUND_NOT_FOUND | Sound asset path invalid | Check path in Content Browser |
| AUDIO_002 | INVALID_VOLUME | Volume outside 0.0-2.0 | Use valid range |
| AUDIO_003 | INVALID_PITCH | Pitch outside 0.5-2.0 | Use valid range |
| AUDIO_004 | INVALID_LOCATION | Location not (X, Y, Z) | Provide 3 coordinates |
| AUDIO_005 | ATTENUATION_NOT_FOUND | Attenuation asset invalid | Check attenuation path |
| AUDIO_006 | ACTOR_NOT_FOUND | Actor label doesn't exist | Verify actor label |
| AUDIO_007 | COMPONENT_FAILED | AudioComponent creation error | Check actor type |
| AUDIO_008 | NO_AUDIO_COMPONENT | Actor has no AudioComponent | Add component first |
| AUDIO_009 | NO_ACTORS | Level has no actors | Load level with actors |

---

## Console Commands

Audio-related console commands for debugging and configuration.

| Command | Description | Example |
|---------|-------------|---------|
| `au.Debug.Sounds` | Show active sound debug | `au.Debug.Sounds 1` |
| `au.Debug.SoundCues` | Debug SoundCue playback | `au.Debug.SoundCues 1` |
| `au.Debug.SoundWaves` | Debug SoundWave playback | `au.Debug.SoundWaves 1` |
| `au.MaxChannels` | Max concurrent sounds | `au.MaxChannels 128` |
| `au.FlushAudioRenderThread` | Flush audio thread | For debugging |
| `au.ForceStereo` | Force stereo output | `au.ForceStereo 1` |
| `au.DisableStereoSpread` | Disable stereo spread | For mono testing |
| `au.VolumeMultiplier` | Global volume scale | `au.VolumeMultiplier 0.5` |

### Executing Console Commands

```python
import unreal

def execute_audio_command(command: str) -> Dict[str, Any]:
    """Execute an audio console command.

    Args:
        command: Console command string

    Returns:
        dict: Execution result
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, command)

        return {
            "success": True,
            "command": command,
            "message": "Command executed. Check Output Log for result."
        }
    except Exception as e:
        return {"success": False, "error": f"Command failed: {str(e)}"}
```

---

## Sound Asset Types

| Type | Class | Description |
|------|-------|-------------|
| Sound Wave | `USoundWave` | Raw audio files (.wav, .ogg) |
| Sound Cue | `USoundCue` | Node-based audio with modulation |
| MetaSound | `UMetaSoundSource` | Procedural audio (UE5.1+) |
| Sound Class | `USoundClass` | Mixing groups |
| Sound Mix | `USoundMix` | Global mix presets |
| Sound Attenuation | `USoundAttenuation` | Spatial falloff settings |
| Sound Concurrency | `USoundConcurrency` | Instance limit settings |

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios and examples |
| [ADVANCED.md](ADVANCED.md) | Advanced topics |

---

**Tracking**: Issue #3995 - Engine Systems Skills Enhancement
**Last Updated**: 2026-01-28
