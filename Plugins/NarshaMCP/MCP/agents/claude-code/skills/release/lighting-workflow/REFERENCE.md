# Lighting Workflow Reference

**Status**: ✅ Complete
**Parent**: [SKILL.md](SKILL.md)
**Version**: 1.2.0

---

## Overview

Complete API reference for lighting workflow automation functions.
All functions follow MCP protocol compliance (return `Dict[str, Any]`, no `print()`).

---

## Function Reference

### spawn_light

Spawns a light actor in the current level.

```python
import unreal
from typing import Dict, Any, Optional, Tuple

def spawn_light(
    light_type: str,
    location: Tuple[float, float, float],
    intensity: float = 5000.0,
    color: Optional[Tuple[float, float, float]] = None,
    rotation: Optional[Tuple[float, float, float]] = None
) -> Dict[str, Any]:
    """
    Spawn a light actor at the specified location.

    Args:
        light_type: Light type ("point", "spot", "directional", "rect", "sky")
        location: World location (X, Y, Z)
        intensity: Light intensity in candelas/lux (default: 5000)
        color: RGB color tuple 0-1 range (default: white)
        rotation: Pitch, Yaw, Roll in degrees (default: 0, 0, 0)

    Returns:
        Dict with success status and light information
    """
    try:
        light_classes = {
            "point": unreal.PointLight,
            "spot": unreal.SpotLight,
            "directional": unreal.DirectionalLight,
            "rect": unreal.RectLight,
            "sky": unreal.SkyLight
        }

        light_class = light_classes.get(light_type.lower())
        if not light_class:
            return {
                "success": False,
                "error": f"Unknown light type: {light_type}",
                "valid_types": list(light_classes.keys())
            }

        # Create location vector
        loc = unreal.Vector(location[0], location[1], location[2])

        # Create rotation if specified
        rot = unreal.Rotator(0, 0, 0)
        if rotation:
            rot = unreal.Rotator(rotation[0], rotation[1], rotation[2])

        # Spawn the light actor
        light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            light_class, loc, rot
        )

        if not light:
            return {"success": False, "error": "Failed to spawn light actor"}

        # Get light component and set properties
        light_comp = light.get_component_by_class(unreal.LightComponent)
        if light_comp:
            light_comp.set_editor_property("intensity", intensity)

            if color:
                linear_color = unreal.LinearColor(color[0], color[1], color[2], 1.0)
                light_comp.set_editor_property("light_color", linear_color)

        return {
            "success": True,
            "light_type": light_type,
            "name": light.get_actor_label(),
            "location": location,
            "intensity": intensity,
            "color": color or (1.0, 1.0, 1.0)
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to spawn light: {str(e)}"}
```

**Parameters**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `light_type` | str | Yes | - | "point", "spot", "directional", "rect", "sky" |
| `location` | Tuple[float, float, float] | Yes | - | World coordinates (X, Y, Z) |
| `intensity` | float | No | 5000.0 | Brightness in cd (Point/Spot) or lux (Directional) |
| `color` | Tuple[float, float, float] | No | (1, 1, 1) | RGB values 0-1 range |
| `rotation` | Tuple[float, float, float] | No | (0, 0, 0) | Pitch, Yaw, Roll degrees |

**Returns**:

```python
# Success
{
    "success": True,
    "light_type": "spot",
    "name": "SpotLight_1",
    "location": (300, -300, 400),
    "intensity": 10000.0,
    "color": (1.0, 0.9, 0.8)
}

# Error
{
    "success": False,
    "error": "Unknown light type: invalid",
    "valid_types": ["point", "spot", "directional", "rect", "sky"]
}
```

---

### set_light_properties

Configure properties of an existing light by name.

```python
import unreal
from typing import Dict, Any

def set_light_properties(light_name: str, **properties) -> Dict[str, Any]:
    """
    Set properties on an existing light actor.

    Args:
        light_name: Actor label of the light
        **properties: Property key-value pairs to set

    Returns:
        Dict with success status and updated properties
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
            return {"success": False, "error": f"No LightComponent found on: {light_name}"}

        updated = {}
        errors = []

        for prop, value in properties.items():
            try:
                # Handle special cases
                if prop == "light_color" and isinstance(value, (tuple, list)):
                    value = unreal.LinearColor(value[0], value[1], value[2], 1.0)
                elif prop == "temperature" and isinstance(value, (int, float)):
                    # Enable temperature mode first
                    light_comp.set_editor_property("use_temperature", True)

                light_comp.set_editor_property(prop, value)
                updated[prop] = properties[prop]  # Store original value
            except Exception as prop_error:
                errors.append(f"{prop}: {str(prop_error)}")

        result = {
            "success": len(updated) > 0,
            "light": light_name,
            "updated_properties": updated
        }

        if errors:
            result["property_errors"] = errors

        return result

    except Exception as e:
        return {"success": False, "error": f"Failed to set properties: {str(e)}"}
```

**Common Properties**:

| Property | Type | Range | Description |
|----------|------|-------|-------------|
| `intensity` | float | 0-100000 | Brightness (cd for Point/Spot, lux for Directional) |
| `light_color` | tuple | (0-1, 0-1, 0-1) | RGB color values |
| `temperature` | float | 1500-15000 | Color temperature in Kelvin |
| `use_temperature` | bool | True/False | Enable temperature-based color |
| `attenuation_radius` | float | 0-100000 | Light falloff distance |
| `source_radius` | float | 0-1000 | Soft shadow source size |
| `cast_shadows` | bool | True/False | Enable shadow casting |
| `inner_cone_angle` | float | 0-80 | Spot light inner cone (degrees) |
| `outer_cone_angle` | float | 0-80 | Spot light outer cone (degrees) |
| `source_width` | float | 0-10000 | Rect light width |
| `source_height` | float | 0-10000 | Rect light height |
| `indirect_lighting_intensity` | float | 0-10 | GI contribution multiplier |

---

### delete_light

Remove a light actor from the level.

```python
import unreal
from typing import Dict, Any

def delete_light(light_name: str) -> Dict[str, Any]:
    """
    Delete a light actor by name.

    Args:
        light_name: Actor label of the light to delete

    Returns:
        Dict with success status
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()

        for actor in actors:
            if actor.get_actor_label() == light_name:
                # Verify it's a light
                if not actor.get_component_by_class(unreal.LightComponent):
                    return {
                        "success": False,
                        "error": f"Actor '{light_name}' is not a light"
                    }

                # Delete the actor
                unreal.EditorLevelLibrary.destroy_actor(actor)
                return {
                    "success": True,
                    "deleted": light_name,
                    "message": f"Light '{light_name}' deleted successfully"
                }

        return {"success": False, "error": f"Light not found: {light_name}"}

    except Exception as e:
        return {"success": False, "error": f"Failed to delete light: {str(e)}"}
```

---

### get_light_info

Query properties of a light actor.

```python
import unreal
from typing import Dict, Any, List

def get_light_info(light_name: str) -> Dict[str, Any]:
    """
    Get detailed information about a light actor.

    Args:
        light_name: Actor label of the light

    Returns:
        Dict with light properties and configuration
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()

        for actor in actors:
            if actor.get_actor_label() == light_name:
                light_comp = actor.get_component_by_class(unreal.LightComponent)
                if not light_comp:
                    return {
                        "success": False,
                        "error": f"Actor '{light_name}' has no LightComponent"
                    }

                # Get location and rotation
                transform = actor.get_actor_transform()
                location = transform.translation
                rotation = transform.rotation.rotator()

                # Determine light type
                light_type = "unknown"
                class_name = actor.get_class().get_name()
                if "Point" in class_name:
                    light_type = "point"
                elif "Spot" in class_name:
                    light_type = "spot"
                elif "Directional" in class_name:
                    light_type = "directional"
                elif "Rect" in class_name:
                    light_type = "rect"
                elif "Sky" in class_name:
                    light_type = "sky"

                # Build info dict
                info = {
                    "success": True,
                    "name": light_name,
                    "light_type": light_type,
                    "class": class_name,
                    "location": (location.x, location.y, location.z),
                    "rotation": (rotation.pitch, rotation.yaw, rotation.roll),
                    "intensity": light_comp.get_editor_property("intensity"),
                    "cast_shadows": light_comp.get_editor_property("cast_shadows"),
                    "visible": light_comp.is_visible()
                }

                # Add type-specific properties
                if light_type == "spot":
                    info["inner_cone_angle"] = light_comp.get_editor_property("inner_cone_angle")
                    info["outer_cone_angle"] = light_comp.get_editor_property("outer_cone_angle")
                elif light_type == "rect":
                    info["source_width"] = light_comp.get_editor_property("source_width")
                    info["source_height"] = light_comp.get_editor_property("source_height")
                elif light_type in ["point", "spot"]:
                    info["attenuation_radius"] = light_comp.get_editor_property("attenuation_radius")

                # Color info
                try:
                    color = light_comp.get_editor_property("light_color")
                    info["color"] = (color.r, color.g, color.b)
                    info["use_temperature"] = light_comp.get_editor_property("use_temperature")
                    if info["use_temperature"]:
                        info["temperature"] = light_comp.get_editor_property("temperature")
                except:
                    pass

                return info

        return {"success": False, "error": f"Light not found: {light_name}"}

    except Exception as e:
        return {"success": False, "error": f"Failed to get light info: {str(e)}"}
```

---

### list_level_lights

List all lights in the current level.

```python
import unreal
from typing import Dict, Any, List

def list_level_lights(filter_type: str = None) -> Dict[str, Any]:
    """
    List all light actors in the current level.

    Args:
        filter_type: Optional filter by light type

    Returns:
        Dict with list of lights and summary
    """
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        lights = []

        for actor in actors:
            light_comp = actor.get_component_by_class(unreal.LightComponent)
            if not light_comp:
                continue

            # Determine light type
            class_name = actor.get_class().get_name()
            light_type = "unknown"
            if "Point" in class_name:
                light_type = "point"
            elif "Spot" in class_name:
                light_type = "spot"
            elif "Directional" in class_name:
                light_type = "directional"
            elif "Rect" in class_name:
                light_type = "rect"
            elif "Sky" in class_name:
                light_type = "sky"

            # Apply filter if specified
            if filter_type and light_type != filter_type.lower():
                continue

            transform = actor.get_actor_transform()
            location = transform.translation

            lights.append({
                "name": actor.get_actor_label(),
                "type": light_type,
                "intensity": light_comp.get_editor_property("intensity"),
                "location": (location.x, location.y, location.z),
                "visible": light_comp.is_visible()
            })

        # Summary by type
        type_counts = {}
        for light in lights:
            lt = light["type"]
            type_counts[lt] = type_counts.get(lt, 0) + 1

        return {
            "success": True,
            "lights": lights,
            "count": len(lights),
            "by_type": type_counts,
            "filter_applied": filter_type
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to list lights: {str(e)}"}
```

---

### setup_three_point_lighting

Create a professional three-point lighting setup.

```python
import unreal
from typing import Dict, Any, Optional, Tuple

def setup_three_point_lighting(
    target_location: Tuple[float, float, float],
    key_intensity: float = 10000.0,
    fill_ratio: float = 0.3,
    rim_ratio: float = 0.8,
    distance: float = 500.0
) -> Dict[str, Any]:
    """
    Create a three-point lighting setup around a target location.

    Args:
        target_location: Center point for the lighting setup
        key_intensity: Key light intensity (default: 10000 cd)
        fill_ratio: Fill light intensity as ratio of key (default: 0.3)
        rim_ratio: Rim light intensity as ratio of key (default: 0.8)
        distance: Distance of lights from target (default: 500 units)

    Returns:
        Dict with created light information
    """
    try:
        import math
        tx, ty, tz = target_location

        created_lights = []

        # Key Light: 45° front-left, elevated
        key_x = tx + distance * math.cos(math.radians(45))
        key_y = ty - distance * math.sin(math.radians(45))
        key_z = tz + distance * 0.6

        key_result = spawn_light(
            "spot",
            (key_x, key_y, key_z),
            intensity=key_intensity,
            color=(1.0, 0.95, 0.9)  # Warm daylight
        )
        if key_result["success"]:
            created_lights.append({
                "role": "key",
                "name": key_result["name"],
                "intensity": key_intensity
            })

        # Fill Light: Opposite side, softer
        fill_x = tx - distance * 0.8
        fill_y = ty + distance * 0.6
        fill_z = tz + distance * 0.3

        fill_result = spawn_light(
            "rect",
            (fill_x, fill_y, fill_z),
            intensity=key_intensity * fill_ratio,
            color=(0.9, 0.95, 1.0)  # Cool fill
        )
        if fill_result["success"]:
            created_lights.append({
                "role": "fill",
                "name": fill_result["name"],
                "intensity": key_intensity * fill_ratio
            })

        # Rim Light: Behind and above
        rim_x = tx
        rim_y = ty + distance * 0.8
        rim_z = tz + distance * 0.8

        rim_result = spawn_light(
            "spot",
            (rim_x, rim_y, rim_z),
            intensity=key_intensity * rim_ratio,
            color=(1.0, 1.0, 1.0)  # Neutral
        )
        if rim_result["success"]:
            created_lights.append({
                "role": "rim",
                "name": rim_result["name"],
                "intensity": key_intensity * rim_ratio
            })

        return {
            "success": len(created_lights) == 3,
            "lights_created": len(created_lights),
            "lights": created_lights,
            "target_location": target_location,
            "setup_type": "three_point"
        }

    except Exception as e:
        return {"success": False, "error": f"Failed to setup lighting: {str(e)}"}
```

---

## Type Definitions

### LightInfo

```python
class LightInfo(TypedDict):
    """Light actor information structure."""
    name: str                    # Actor label
    light_type: str              # "point", "spot", "directional", "rect", "sky"
    class_name: str              # UE class name
    location: Tuple[float, float, float]
    rotation: Tuple[float, float, float]
    intensity: float
    color: Tuple[float, float, float]
    cast_shadows: bool
    visible: bool
    # Type-specific (optional)
    attenuation_radius: Optional[float]
    inner_cone_angle: Optional[float]   # Spot only
    outer_cone_angle: Optional[float]   # Spot only
    source_width: Optional[float]       # Rect only
    source_height: Optional[float]      # Rect only
    temperature: Optional[float]        # If use_temperature=True
```

### ThreePointSetup

```python
class ThreePointSetup(TypedDict):
    """Three-point lighting setup result."""
    success: bool
    lights_created: int
    lights: List[Dict[str, Any]]  # role, name, intensity
    target_location: Tuple[float, float, float]
    setup_type: str  # "three_point"
```

---

## Error Codes

| Code | Message | Cause | Solution |
|------|---------|-------|----------|
| LIGHT_001 | Unknown light type | Invalid `light_type` parameter | Use: point, spot, directional, rect, sky |
| LIGHT_002 | Light not found | Actor label doesn't exist | Check actor name with `list_level_lights()` |
| LIGHT_003 | No LightComponent | Actor is not a light | Verify actor is a light type |
| LIGHT_004 | Property set failed | Invalid property name/value | Check property reference table |
| LIGHT_005 | Spawn failed | Level not writable | Ensure level is checked out/editable |
| LIGHT_006 | Delete failed | Actor locked or referenced | Check actor dependencies |

---

## Console Commands Reference

### Quality Commands

| Command | Description | Values |
|---------|-------------|--------|
| `r.LightFunctionQuality` | Light function quality | 0-2 (Low/Medium/High) |
| `r.ShadowQuality` | Shadow quality level | 0-5 |
| `r.Shadow.MaxResolution` | Max shadow map resolution | 512-4096 |
| `r.Shadow.CSM.MaxCascades` | Cascaded shadow map cascades | 1-10 |
| `r.VolumetricFog` | Enable volumetric fog | 0/1 |
| `r.VolumetricFog.GridPixelSize` | Fog grid resolution | 4-32 |

### Debug Commands

| Command | Description |
|---------|-------------|
| `ShowFlag.Lighting 0/1` | Toggle lighting display |
| `ShowFlag.DirectionalLights 0/1` | Toggle directional lights |
| `ShowFlag.PointLights 0/1` | Toggle point lights |
| `ShowFlag.SpotLights 0/1` | Toggle spot lights |
| `ShowFlag.SkyLighting 0/1` | Toggle sky lighting |
| `stat LightRendering` | Light rendering stats |

### Lumen Commands

| Command | Description | Values |
|---------|-------------|--------|
| `r.Lumen.DiffuseIndirect.Allow` | Enable Lumen GI | 0/1 |
| `r.Lumen.Reflections.Allow` | Enable Lumen reflections | 0/1 |
| `r.Lumen.TraceMeshSDFs` | SDF tracing quality | 0/1 |
| `r.Lumen.ScreenProbeGather.ScreenSpaceRadianceCache` | Screen-space cache | 0/1 |

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios |
| [ADVANCED.md](ADVANCED.md) | Lumen, ray tracing, GI setup |

---

**Last Updated**: 2026-01-28
