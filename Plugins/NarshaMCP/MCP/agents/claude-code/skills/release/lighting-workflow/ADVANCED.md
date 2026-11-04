# Lighting Workflow Advanced

**Version**: 1.1.0
**Status**: Complete
**Parent**: [SKILL.md](SKILL.md)

---

## Overview

Advanced lighting techniques for Unreal Engine 5, including Lumen Global Illumination,
Ray Tracing configuration, light baking workflows, IES profiles, and volumetric effects.

---

## Lumen Global Illumination

### Overview

Lumen is UE5's fully dynamic global illumination and reflections system.
Configure via console commands for real-time GI without baking.

### Console Command Configuration

```python
from typing import Any

import unreal
from typing import Dict, Optional

def configure_lumen_gi(
    quality: str = "high",
    scene_detail: float = 1.0,
    final_gather_quality: float = 1.0
) -> dict[str, Any]:
    """Configure Lumen Global Illumination settings.

    Args:
        quality: Quality preset ("low", "medium", "high", "epic")
        scene_detail: Scene detail level (0.25-4.0)
        final_gather_quality: Final gather quality (0.25-4.0)

    Returns:
        dict: Configuration result with applied settings
    """
    try:
        quality_settings = {
            "low": {"lumen_quality": 0.5, "ray_lighting": 0},
            "medium": {"lumen_quality": 1.0, "ray_lighting": 0},
            "high": {"lumen_quality": 1.5, "ray_lighting": 1},
            "epic": {"lumen_quality": 2.0, "ray_lighting": 1}
        }

        if quality.lower() not in quality_settings:
            return {"success": False, "error": f"Invalid quality: {quality}",
                    "valid_options": list(quality_settings.keys())}

        settings = quality_settings[quality.lower()]
        world = unreal.EditorLevelLibrary.get_editor_world()

        commands = [
            f"r.Lumen.DiffuseIndirect.Allow 1",
            f"r.Lumen.Reflections.Allow 1",
            f"r.Lumen.TraceMeshSDFs 1",
            f"r.Lumen.ScreenProbeGather.ScreenTraces 1",
            f"r.LumenScene.Detail {scene_detail}",
            f"r.Lumen.FinalGather.Quality {final_gather_quality}",
            f"r.Lumen.DiffuseIndirect.HardwareRayTracing {settings['ray_lighting']}"
        ]

        for cmd in commands:
            unreal.SystemLibrary.execute_console_command(world, cmd)

        return {
            "success": True,
            "quality": quality,
            "scene_detail": scene_detail,
            "final_gather_quality": final_gather_quality,
            "hardware_raytracing": bool(settings["ray_lighting"]),
            "commands_executed": len(commands)
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to configure Lumen: {str(e)}"}
```

### Lumen Settings Reference

| Setting | Console Command | Range | Description |
|---------|----------------|-------|-------------|
| Scene Detail | `r.LumenScene.Detail` | 0.25-4.0 | Higher = more mesh cards |
| Final Gather Quality | `r.Lumen.FinalGather.Quality` | 0.25-4.0 | Noise vs performance |
| Screen Traces | `r.Lumen.ScreenProbeGather.ScreenTraces` | 0/1 | Enable screen-space traces |
| Hardware RT | `r.Lumen.DiffuseIndirect.HardwareRayTracing` | 0/1 | Use RTX hardware |
| Reflection Quality | `r.Lumen.Reflections.Quality` | 0.25-4.0 | Reflection clarity |

### Best Practices

1. **Performance Scaling**: Start with `r.LumenScene.Detail 0.5` for testing
2. **Hardware RT**: Enable only with RTX 3060+ for smooth performance
3. **Interior Scenes**: Increase `r.Lumen.FinalGather.Quality` for less noise
4. **Exterior Scenes**: Lower quality acceptable due to sky light dominance

---

## Ray Tracing Configuration

### Overview

Configure hardware ray tracing for shadows, reflections, and global illumination
when Lumen's software tracing is insufficient.

### Ray Tracing Setup

```python
import unreal

def configure_ray_tracing(
    enable_rtgi: bool = True,
    enable_rt_shadows: bool = True,
    enable_rt_reflections: bool = True,
    max_bounces: int = 2,
    samples_per_pixel: int = 1
) -> dict:
    """Configure hardware ray tracing settings.

    Args:
        enable_rtgi: Enable ray traced global illumination
        enable_rt_shadows: Enable ray traced shadows
        enable_rt_reflections: Enable ray traced reflections
        max_bounces: Maximum light bounces (1-4)
        samples_per_pixel: Samples per pixel (1-4)

    Returns:
        dict: Configuration result
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()

        commands = [
            f"r.RayTracing 1",
            f"r.RayTracing.GlobalIllumination {1 if enable_rtgi else 0}",
            f"r.RayTracing.Shadows {1 if enable_rt_shadows else 0}",
            f"r.RayTracing.Reflections {1 if enable_rt_reflections else 0}",
            f"r.RayTracing.GlobalIllumination.MaxBounces {max_bounces}",
            f"r.RayTracing.GlobalIllumination.SamplesPerPixel {samples_per_pixel}",
            f"r.RayTracing.Reflections.MaxBounces {max_bounces}",
            f"r.RayTracing.Shadows.MaxBounces 1"
        ]

        for cmd in commands:
            unreal.SystemLibrary.execute_console_command(world, cmd)

        return {
            "success": True,
            "rtgi_enabled": enable_rtgi,
            "rt_shadows_enabled": enable_rt_shadows,
            "rt_reflections_enabled": enable_rt_reflections,
            "max_bounces": max_bounces,
            "samples_per_pixel": samples_per_pixel
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to configure RT: {str(e)}"}


def check_ray_tracing_support() -> dict:
    """Check if hardware ray tracing is supported and enabled.

    Returns:
        dict: Ray tracing support status
    """
    try:
        # Check via rendering settings
        world = unreal.EditorLevelLibrary.get_editor_world()

        # These commands return values to output log
        unreal.SystemLibrary.execute_console_command(world, "r.RayTracing")

        return {
            "success": True,
            "message": "Check Output Log for r.RayTracing value",
            "requirements": [
                "Windows 10/11 with DXR support",
                "NVIDIA RTX 2000+ or AMD RX 6000+",
                "Project Settings > Rendering > Ray Tracing enabled"
            ]
        }
    except Exception as e:
        return {"success": False, "error": str(e)}
```

### RT Performance Guidelines

| Feature | GPU Cost | When to Use |
|---------|----------|-------------|
| RT Shadows | Medium | Contact shadows, area light softness |
| RT Reflections | High | Mirror surfaces, wet floors |
| RT GI | Very High | Architectural visualization |
| RT AO | Low | Subtle contact darkening |

---

## Light Baking Workflow

### Overview

For projects requiring baked lighting (mobile, older hardware), use Lightmass
for pre-computed global illumination.

### Lightmass Configuration

```python
import unreal

def configure_lightmass(
    quality: str = "production",
    indirect_bounces: int = 3,
    indirect_quality: float = 1.0,
    static_lighting_level_scale: float = 1.0
) -> dict:
    """Configure Lightmass baking settings.

    Args:
        quality: Build quality ("preview", "medium", "high", "production")
        indirect_bounces: Number of indirect light bounces (1-10)
        indirect_quality: Indirect lighting quality multiplier (0.5-4.0)
        static_lighting_level_scale: Scale for all static lighting (0.5-2.0)

    Returns:
        dict: Configuration result
    """
    try:
        quality_presets = {
            "preview": {"bounces": 1, "quality": 0.5, "scale": 2.0},
            "medium": {"bounces": 2, "quality": 1.0, "scale": 1.0},
            "high": {"bounces": 3, "quality": 2.0, "scale": 0.5},
            "production": {"bounces": 4, "quality": 4.0, "scale": 0.25}
        }

        if quality.lower() not in quality_presets:
            return {"success": False, "error": f"Invalid quality: {quality}",
                    "valid_options": list(quality_presets.keys())}

        world = unreal.EditorLevelLibrary.get_editor_world()

        # Access World Settings
        world_settings = unreal.GameplayStatics.get_game_mode(world)

        commands = [
            f"r.LightmassNumIndirectLightingBounces {indirect_bounces}",
            f"r.LightmassIndirectQuality {indirect_quality}",
            f"r.StaticLightingLevelScale {static_lighting_level_scale}"
        ]

        for cmd in commands:
            unreal.SystemLibrary.execute_console_command(world, cmd)

        return {
            "success": True,
            "quality": quality,
            "indirect_bounces": indirect_bounces,
            "indirect_quality": indirect_quality,
            "level_scale": static_lighting_level_scale,
            "note": "Run 'Build Lighting Only' from Build menu"
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to configure Lightmass: {str(e)}"}


def build_lighting(quality: str = "production") -> dict:
    """Trigger lighting build via console command.

    Args:
        quality: Build quality level

    Returns:
        dict: Build initiation result
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()

        quality_map = {
            "preview": "BuildLightingOnly Preview",
            "medium": "BuildLightingOnly Medium",
            "high": "BuildLightingOnly High",
            "production": "BuildLightingOnly Production"
        }

        command = quality_map.get(quality.lower(), "BuildLightingOnly Production")
        unreal.SystemLibrary.execute_console_command(world, command)

        return {
            "success": True,
            "command": command,
            "message": "Lighting build initiated. Check Build Progress window."
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to build lighting: {str(e)}"}
```

### Lightmap Resolution Guidelines

| Object Type | Resolution | Memory |
|-------------|------------|--------|
| Hero Props | 128-256 | High |
| Standard Props | 32-64 | Medium |
| Walls/Floors | 16-32 | Low |
| Background | 4-16 | Minimal |

---

## IES Light Profiles

### Overview

IES (Illuminating Engineering Society) profiles define real-world light distribution
patterns from manufacturer data.

### Loading IES Profiles

```python
import unreal

def apply_ies_profile(
    light_name: str,
    ies_path: str,
    brightness_scale: float = 1.0
) -> dict:
    """Apply an IES light profile to a light actor.

    Args:
        light_name: Target light actor name
        ies_path: Content path to IES texture (e.g., "/Game/Lighting/IES/Spotlight_01")
        brightness_scale: Scale factor for IES brightness

    Returns:
        dict: Application result
    """
    try:
        # Find the light actor
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_light = None

        for actor in actors:
            if actor.get_actor_label() == light_name:
                target_light = actor
                break

        if not target_light:
            return {"success": False, "error": f"Light not found: {light_name}"}

        # Load IES texture
        ies_texture = unreal.load_asset(ies_path)
        if not ies_texture:
            return {"success": False, "error": f"IES profile not found: {ies_path}"}

        # Get light component
        light_comp = target_light.get_component_by_class(unreal.LocalLightComponent)
        if not light_comp:
            return {"success": False, "error": "Actor has no LocalLightComponent"}

        # Apply IES profile
        light_comp.set_editor_property("ies_texture", ies_texture)
        light_comp.set_editor_property("use_ies_brightness", True)
        light_comp.set_editor_property("ies_brightness_scale", brightness_scale)

        return {
            "success": True,
            "light": light_name,
            "ies_profile": ies_path,
            "brightness_scale": brightness_scale
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to apply IES: {str(e)}"}


def list_ies_profiles(content_path: str = "/Game/Lighting/IES") -> dict:
    """List available IES profiles in a content directory.

    Args:
        content_path: Content Browser path to search

    Returns:
        dict: List of IES textures found
    """
    try:
        asset_registry = unreal.AssetRegistryHelpers.get_asset_registry()
        filter = unreal.ARFilter()
        filter.class_names = ["TextureLightProfile"]
        filter.package_paths = [content_path]
        filter.recursive_paths = True

        assets = asset_registry.get_assets(filter)

        profiles = []
        for asset in assets:
            profiles.append({
                "name": str(asset.asset_name),
                "path": str(asset.package_name)
            })

        return {
            "success": True,
            "search_path": content_path,
            "count": len(profiles),
            "profiles": profiles
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to list IES profiles: {str(e)}"}
```

### Common IES Types

| Type | Use Case | Pattern |
|------|----------|---------|
| Spot/PAR | Stage lighting | Narrow beam |
| Flood | Area lighting | Wide coverage |
| Wall Wash | Architectural | Asymmetric |
| Pendant | Interior decorative | Downward focus |

---

## Volumetric Lighting

### Overview

Volumetric fog and lighting create atmospheric effects with visible light rays
(god rays, fog beams).

### Volumetric Fog Setup

```python
import unreal

def configure_volumetric_fog(
    enable: bool = True,
    scattering_distribution: float = 0.2,
    fog_density: float = 0.02,
    view_distance: float = 6000.0,
    start_distance: float = 0.0
) -> dict:
    """Configure volumetric fog settings.

    Args:
        enable: Enable/disable volumetric fog
        scattering_distribution: Light scattering (-1 to 1, 0 = isotropic)
        fog_density: Overall fog density (0.001-1.0)
        view_distance: Maximum fog rendering distance
        start_distance: Distance where fog begins

    Returns:
        dict: Configuration result
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()

        commands = [
            f"r.VolumetricFog {1 if enable else 0}",
            f"r.VolumetricFog.GridPixelSize 8",
            f"r.VolumetricFog.GridSizeZ 128",
            f"r.VolumetricFog.TemporalReprojection 1"
        ]

        for cmd in commands:
            unreal.SystemLibrary.execute_console_command(world, cmd)

        # Find or create ExponentialHeightFog
        fog_actors = unreal.GameplayStatics.get_all_actors_of_class(
            world, unreal.ExponentialHeightFog
        )

        if fog_actors:
            fog = fog_actors[0]
            fog_comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)

            if fog_comp:
                fog_comp.set_editor_property("volumetric_fog", enable)
                fog_comp.set_editor_property("volumetric_fog_scattering_distribution",
                                              scattering_distribution)
                fog_comp.set_editor_property("fog_density", fog_density)
                fog_comp.set_editor_property("fog_max_opacity", 1.0)

        return {
            "success": True,
            "enabled": enable,
            "scattering_distribution": scattering_distribution,
            "fog_density": fog_density,
            "view_distance": view_distance
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to configure fog: {str(e)}"}


def create_god_rays(
    light_name: str,
    volumetric_scattering_intensity: float = 1.0
) -> dict:
    """Enable god rays (volumetric scattering) for a directional light.

    Args:
        light_name: Directional light actor name
        volumetric_scattering_intensity: Intensity of light shafts

    Returns:
        dict: Configuration result
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_light = None

        for actor in actors:
            if actor.get_actor_label() == light_name:
                target_light = actor
                break

        if not target_light:
            return {"success": False, "error": f"Light not found: {light_name}"}

        light_comp = target_light.get_component_by_class(unreal.DirectionalLightComponent)
        if not light_comp:
            return {"success": False, "error": "Not a Directional Light"}

        # Enable volumetric scattering
        light_comp.set_editor_property("volumetric_scattering_intensity",
                                        volumetric_scattering_intensity)
        light_comp.set_editor_property("cast_volumetric_shadow", True)

        return {
            "success": True,
            "light": light_name,
            "volumetric_intensity": volumetric_scattering_intensity,
            "cast_volumetric_shadow": True
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to configure god rays: {str(e)}"}
```

### Volumetric Settings Reference

| Setting | Range | Performance Impact |
|---------|-------|-------------------|
| Grid Pixel Size | 4-16 | Lower = sharper, slower |
| Grid Size Z | 64-256 | Higher = more depth detail |
| Temporal Reprojection | 0/1 | Enable for stability |
| Scattering Distribution | -1 to 1 | 0 = uniform scatter |

---

## Light Channels and Masking

### Overview

Light channels allow lights to affect only specific actors, useful for
character-only lighting or gameplay-specific effects.

```python
import unreal

def set_light_channel(
    light_name: str,
    channel: int = 0,
    exclusive: bool = False
) -> dict:
    """Configure light channel for selective lighting.

    Args:
        light_name: Light actor name
        channel: Light channel (0, 1, or 2)
        exclusive: If True, only affects actors in same channel

    Returns:
        dict: Configuration result
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_light = None

        for actor in actors:
            if actor.get_actor_label() == light_name:
                target_light = actor
                break

        if not target_light:
            return {"success": False, "error": f"Light not found: {light_name}"}

        light_comp = target_light.get_component_by_class(unreal.LightComponent)
        if not light_comp:
            return {"success": False, "error": "No light component found"}

        # Configure light channels
        lighting_channels = unreal.LightingChannels()
        lighting_channels.channel0 = (channel == 0)
        lighting_channels.channel1 = (channel == 1)
        lighting_channels.channel2 = (channel == 2)

        light_comp.set_editor_property("lighting_channels", lighting_channels)

        return {
            "success": True,
            "light": light_name,
            "channel": channel,
            "exclusive": exclusive
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to set light channel: {str(e)}"}
```

### Light Channel Use Cases

| Channel | Common Use |
|---------|------------|
| 0 | Default environment lighting |
| 1 | Character-only key lights |
| 2 | Special effects, UI elements |

---

## Three-Point Lighting Setup (3점 조명)

### Overview

Three-point lighting is a standard technique for creating professional lighting setups.
Consists of Key Light, Fill Light, and Back Light (Rim Light).

### Setup Function

```python
from typing import Any

import unreal

def create_three_point_lighting(
    target_name: str,
    key_intensity: float = 10000.0,
    fill_ratio: float = 0.5,
    rim_ratio: float = 0.8
) -> dict[str, Any]:
    """Create a three-point lighting setup around a target actor.

    Args:
        target_name: Name of the target actor to light
        key_intensity: Intensity of the key light in lumens
        fill_ratio: Fill light intensity as ratio of key (0.3-0.7 typical)
        rim_ratio: Rim light intensity as ratio of key (0.5-1.0 typical)

    Returns:
        dict: Created lights information
    """
    try:
        world = unreal.EditorLevelLibrary.get_editor_world()

        # Find target actor
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target = None
        for actor in actors:
            if actor.get_actor_label() == target_name:
                target = actor
                break

        if not target:
            return {"success": False, "error": f"Target not found: {target_name}"}

        target_loc = target.get_actor_location()

        # Key Light - 45 degrees front-left, elevated
        key_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PointLight,
            unreal.Vector(target_loc.x - 300, target_loc.y - 300, target_loc.z + 200)
        )
        key_light.set_actor_label("ThreePoint_Key")
        key_comp = key_light.get_component_by_class(unreal.PointLightComponent)
        key_comp.set_editor_property("intensity", key_intensity)

        # Fill Light - 45 degrees front-right, lower intensity
        fill_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PointLight,
            unreal.Vector(target_loc.x - 200, target_loc.y + 300, target_loc.z + 100)
        )
        fill_light.set_actor_label("ThreePoint_Fill")
        fill_comp = fill_light.get_component_by_class(unreal.PointLightComponent)
        fill_comp.set_editor_property("intensity", key_intensity * fill_ratio)

        # Rim/Back Light - Behind, elevated
        rim_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PointLight,
            unreal.Vector(target_loc.x + 300, target_loc.y, target_loc.z + 250)
        )
        rim_light.set_actor_label("ThreePoint_Rim")
        rim_comp = rim_light.get_component_by_class(unreal.PointLightComponent)
        rim_comp.set_editor_property("intensity", key_intensity * rim_ratio)

        return {
            "success": True,
            "target": target_name,
            "lights": {
                "key": {"name": "ThreePoint_Key", "intensity": key_intensity},
                "fill": {"name": "ThreePoint_Fill", "intensity": key_intensity * fill_ratio},
                "rim": {"name": "ThreePoint_Rim", "intensity": key_intensity * rim_ratio}
            }
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to create three-point setup: {str(e)}"}
```

### Three-Point Lighting Ratios

| Light | Typical Ratio | Purpose |
|-------|---------------|---------|
| Key Light | 1.0 (100%) | Main illumination, defines shadows |
| Fill Light | 0.3-0.5 (30-50%) | Softens shadows, reduces contrast |
| Rim Light | 0.5-1.0 (50-100%) | Separates subject from background |

---

## Console Command Reference

### Lighting Build Commands

| Command | Description |
|---------|-------------|
| `BuildLighting` | Build lighting for current level |
| `BuildLightingOnly Preview` | Quick preview quality build |
| `BuildLightingOnly Production` | Full production quality build |

### Lumen Commands

| Command | Range | Description |
|---------|-------|-------------|
| `r.LumenScene.Detail` | 0.25-4.0 | Scene detail level |
| `r.Lumen.FinalGather.Quality` | 0.25-4.0 | Final gather quality |
| `r.Lumen.DiffuseIndirect.Allow` | 0/1 | Enable diffuse GI |
| `r.Lumen.Reflections.Allow` | 0/1 | Enable Lumen reflections |
| `r.Lumen.DiffuseIndirect.HardwareRayTracing` | 0/1 | Use hardware RT |

### Ray Tracing Commands

| Command | Description |
|---------|-------------|
| `r.RayTracing` | Enable/disable ray tracing |
| `r.RayTracing.GlobalIllumination` | Enable RT global illumination |
| `r.RayTracing.Shadows` | Enable RT shadows |
| `r.RayTracing.Reflections` | Enable RT reflections |

### Volumetric Fog Commands

| Command | Description |
|---------|-------------|
| `r.VolumetricFog` | Enable/disable volumetric fog |
| `r.VolumetricFog.GridPixelSize` | Grid resolution (4-16) |
| `r.VolumetricFog.TemporalReprojection` | Enable temporal stability |

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [REFERENCE.md](REFERENCE.md) | Python code patterns |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios and examples |

---

**Tracking**: Issue #3995 - Engine Systems Skills Enhancement
**Last Updated**: 2026-01-28
