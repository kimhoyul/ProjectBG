# Lighting Workflow Examples

**Status**: ✅ Complete
**Parent**: [SKILL.md](SKILL.md)
**Version**: 1.1.0

---

## Overview

Practical examples for common lighting scenarios in Unreal Engine.
Each example includes step-by-step guidance and expected outputs.

---

## Scenario 1: Three-Point Portrait Lighting

Professional character lighting for portraits and cinematics.

### User Request

"캐릭터에 3점 조명 세팅해줘" / "Set up three-point lighting for my character"

### Prerequisites

- Character actor in level (e.g., "BP_PlayerCharacter")
- Editor with Remote Control enabled

### Step-by-Step

**Step 1: Get character location**

```python
import unreal
from typing import Dict, Any

def get_actor_location(actor_name: str) -> Dict[str, Any]:
    """Get the world location of an actor."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        for actor in actors:
            if actor.get_actor_label() == actor_name:
                loc = actor.get_actor_location()
                return {
                    "success": True,
                    "actor": actor_name,
                    "location": (loc.x, loc.y, loc.z)
                }
        return {"success": False, "error": f"Actor not found: {actor_name}"}
    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute
result = get_actor_location("BP_PlayerCharacter")
# Output: {"success": True, "actor": "BP_PlayerCharacter", "location": (0, 0, 100)}
```

**Step 2: Create three-point lighting**

```python
import unreal
import math
from typing import Dict, Any, Tuple

def create_three_point_lighting(
    target_location: Tuple[float, float, float],
    key_intensity: float = 10000.0
) -> Dict[str, Any]:
    """Create professional three-point lighting setup."""
    try:
        tx, ty, tz = target_location
        distance = 500.0
        created_lights = []

        # Key Light: 45° front-left, elevated
        key_loc = (
            tx + distance * math.cos(math.radians(45)),
            ty - distance * math.sin(math.radians(45)),
            tz + distance * 0.6
        )
        key_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SpotLight,
            unreal.Vector(*key_loc)
        )
        if key_light:
            key_light.set_actor_label("ThreePoint_Key")
            comp = key_light.get_component_by_class(unreal.SpotLightComponent)
            comp.set_editor_property("intensity", key_intensity)
            comp.set_editor_property("outer_cone_angle", 44.0)
            comp.set_editor_property("inner_cone_angle", 30.0)
            comp.set_editor_property("use_temperature", True)
            comp.set_editor_property("temperature", 5600)  # Daylight
            created_lights.append({"role": "key", "name": "ThreePoint_Key"})

        # Fill Light: Opposite side, softer (30% of key)
        fill_loc = (tx - distance * 0.8, ty + distance * 0.6, tz + distance * 0.3)
        fill_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.RectLight,
            unreal.Vector(*fill_loc)
        )
        if fill_light:
            fill_light.set_actor_label("ThreePoint_Fill")
            comp = fill_light.get_component_by_class(unreal.RectLightComponent)
            comp.set_editor_property("intensity", key_intensity * 0.3)
            comp.set_editor_property("source_width", 200.0)
            comp.set_editor_property("source_height", 200.0)
            created_lights.append({"role": "fill", "name": "ThreePoint_Fill"})

        # Rim Light: Behind and above (80% of key)
        rim_loc = (tx, ty + distance * 0.8, tz + distance * 0.8)
        rim_light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SpotLight,
            unreal.Vector(*rim_loc)
        )
        if rim_light:
            rim_light.set_actor_label("ThreePoint_Rim")
            comp = rim_light.get_component_by_class(unreal.SpotLightComponent)
            comp.set_editor_property("intensity", key_intensity * 0.8)
            comp.set_editor_property("outer_cone_angle", 35.0)
            created_lights.append({"role": "rim", "name": "ThreePoint_Rim"})

        return {
            "success": len(created_lights) == 3,
            "lights_created": len(created_lights),
            "lights": created_lights,
            "target": target_location
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

# Execute with character location
result = create_three_point_lighting((0, 0, 100), key_intensity=10000)
```

### Expected Output

```text
🎬 3-Point Lighting Setup Complete

1. Key Light (ThreePoint_Key)
   - Type: SpotLight
   - Position: (353, -353, 400)
   - Intensity: 10000 cd
   - Temperature: 5600K

2. Fill Light (ThreePoint_Fill)
   - Type: RectLight
   - Position: (-400, 300, 250)
   - Intensity: 3000 cd
   - Size: 200x200

3. Rim Light (ThreePoint_Rim)
   - Type: SpotLight
   - Position: (0, 400, 500)
   - Intensity: 8000 cd

✅ Setup complete in ~1.5 seconds
```

### Notes

- Key light provides main illumination (45° angle is standard)
- Fill light reduces harsh shadows (typically 20-40% of key)
- Rim light creates separation from background
- Use temperature for natural color variation

---

## Scenario 2: Outdoor Scene Lighting

Sun, sky, and atmospheric lighting for exterior environments.

### User Request

"야외 씬 조명 세팅해줘" / "Set up outdoor scene lighting"

### Prerequisites

- Outdoor level environment
- Sky sphere or sky atmosphere (optional)

### Step-by-Step

**Step 1: Create directional light (sun)**

```python
import unreal
from typing import Dict, Any

def setup_outdoor_sun(
    sun_angle: float = 45.0,
    intensity: float = 10.0,
    temperature: float = 5500.0
) -> Dict[str, Any]:
    """Create directional light simulating the sun."""
    try:
        # Spawn directional light
        sun = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.DirectionalLight,
            unreal.Vector(0, 0, 0)
        )

        if not sun:
            return {"success": False, "error": "Failed to spawn DirectionalLight"}

        sun.set_actor_label("Sun_DirectionalLight")

        # Set rotation for sun angle
        sun.set_actor_rotation(
            unreal.Rotator(-sun_angle, 0, 0),
            teleport_physics=False
        )

        # Configure light properties
        comp = sun.get_component_by_class(unreal.DirectionalLightComponent)
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("use_temperature", True)
        comp.set_editor_property("temperature", temperature)
        comp.set_editor_property("cast_shadows", True)
        comp.set_editor_property("dynamic_shadow_cascades", 4)

        # Atmospheric scattering
        comp.set_editor_property("atmosphere_sun_light", True)

        return {
            "success": True,
            "name": "Sun_DirectionalLight",
            "sun_angle": sun_angle,
            "intensity": intensity,
            "temperature": temperature,
            "cascades": 4
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = setup_outdoor_sun(sun_angle=45.0, intensity=10.0, temperature=5500.0)
```

**Step 2: Add sky light for ambient**

```python
import unreal
from typing import Dict, Any

def setup_sky_light(
    intensity: float = 1.0,
    use_cubemap: bool = False,
    cubemap_path: str = None
) -> Dict[str, Any]:
    """Create sky light for ambient illumination."""
    try:
        sky = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SkyLight,
            unreal.Vector(0, 0, 500)
        )

        if not sky:
            return {"success": False, "error": "Failed to spawn SkyLight"}

        sky.set_actor_label("Ambient_SkyLight")

        comp = sky.get_component_by_class(unreal.SkyLightComponent)
        comp.set_editor_property("intensity", intensity)

        if use_cubemap and cubemap_path:
            cubemap = unreal.load_asset(cubemap_path)
            if cubemap:
                comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
                comp.set_editor_property("cubemap", cubemap)
        else:
            comp.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_CAPTURED_SCENE)

        # Recapture sky
        comp.recapture_sky()

        return {
            "success": True,
            "name": "Ambient_SkyLight",
            "intensity": intensity,
            "source_type": "captured" if not use_cubemap else "cubemap"
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = setup_sky_light(intensity=1.0)
```

**Step 3: Add exponential height fog**

```python
import unreal
from typing import Dict, Any

def setup_atmospheric_fog(
    fog_density: float = 0.02,
    fog_height: float = 0.0,
    fog_color: tuple = (0.5, 0.6, 0.7)
) -> Dict[str, Any]:
    """Create exponential height fog for atmosphere."""
    try:
        fog = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.ExponentialHeightFog,
            unreal.Vector(0, 0, 0)
        )

        if not fog:
            return {"success": False, "error": "Failed to spawn ExponentialHeightFog"}

        fog.set_actor_label("Atmosphere_HeightFog")

        comp = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        comp.set_editor_property("fog_density", fog_density)
        comp.set_editor_property("fog_height_falloff", 0.2)
        comp.set_editor_property("fog_inscattering_color",
            unreal.LinearColor(fog_color[0], fog_color[1], fog_color[2], 1.0))

        # Volumetric fog for Lumen
        comp.set_editor_property("volumetric_fog", True)

        return {
            "success": True,
            "name": "Atmosphere_HeightFog",
            "density": fog_density,
            "volumetric": True
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = setup_atmospheric_fog(fog_density=0.02)
```

### Expected Output

```text
🌅 Outdoor Lighting Setup Complete

1. Sun (Sun_DirectionalLight)
   - Angle: 45° (afternoon sun)
   - Intensity: 10 lux
   - Temperature: 5500K
   - Cascaded Shadows: 4

2. Sky Light (Ambient_SkyLight)
   - Intensity: 1.0
   - Source: Scene Capture
   - Auto-recaptured: Yes

3. Height Fog (Atmosphere_HeightFog)
   - Density: 0.02
   - Volumetric: Enabled

✅ Outdoor lighting ready
```

---

## Scenario 3: Interior Lighting

Point lights, rect lights, and practical lighting for indoor scenes.

### User Request

"실내 조명 설정해줘" / "Set up interior lighting"

### Step-by-Step

**Step 1: Create ceiling light array**

```python
import unreal
from typing import Dict, Any, List, Tuple

def create_ceiling_lights(
    room_bounds: Tuple[Tuple[float, float], Tuple[float, float]],
    ceiling_height: float = 300.0,
    grid_spacing: float = 400.0,
    intensity: float = 2000.0
) -> Dict[str, Any]:
    """Create a grid of ceiling lights for a room."""
    try:
        (min_x, min_y), (max_x, max_y) = room_bounds
        lights = []

        x = min_x + grid_spacing / 2
        index = 0
        while x < max_x:
            y = min_y + grid_spacing / 2
            while y < max_y:
                light = unreal.EditorLevelLibrary.spawn_actor_from_class(
                    unreal.PointLight,
                    unreal.Vector(x, y, ceiling_height - 20)
                )

                if light:
                    name = f"CeilingLight_{index:02d}"
                    light.set_actor_label(name)

                    comp = light.get_component_by_class(unreal.PointLightComponent)
                    comp.set_editor_property("intensity", intensity)
                    comp.set_editor_property("attenuation_radius", grid_spacing * 1.2)
                    comp.set_editor_property("source_radius", 10.0)
                    comp.set_editor_property("use_temperature", True)
                    comp.set_editor_property("temperature", 4000)  # Warm indoor

                    lights.append({
                        "name": name,
                        "location": (x, y, ceiling_height - 20)
                    })
                    index += 1

                y += grid_spacing
            x += grid_spacing

        return {
            "success": True,
            "lights_created": len(lights),
            "lights": lights,
            "grid_spacing": grid_spacing,
            "temperature": 4000
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

# Room from (0,0) to (1000, 800) with 300 unit ceiling
result = create_ceiling_lights(
    room_bounds=((0, 0), (1000, 800)),
    ceiling_height=300.0,
    grid_spacing=400.0
)
```

**Step 2: Add window light (rect light)**

```python
import unreal
from typing import Dict, Any, Tuple

def create_window_light(
    window_location: Tuple[float, float, float],
    window_size: Tuple[float, float] = (200.0, 300.0),
    direction: str = "north",
    intensity: float = 5000.0,
    is_daytime: bool = True
) -> Dict[str, Any]:
    """Create a rect light simulating light through a window."""
    try:
        # Direction determines rotation
        rotations = {
            "north": (0, 0, 0),
            "south": (0, 180, 0),
            "east": (0, 90, 0),
            "west": (0, -90, 0)
        }

        rot = rotations.get(direction.lower(), (0, 0, 0))

        light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.RectLight,
            unreal.Vector(*window_location),
            unreal.Rotator(*rot)
        )

        if not light:
            return {"success": False, "error": "Failed to spawn RectLight"}

        light.set_actor_label(f"Window_{direction.capitalize()}")

        comp = light.get_component_by_class(unreal.RectLightComponent)
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("source_width", window_size[0])
        comp.set_editor_property("source_height", window_size[1])
        comp.set_editor_property("use_temperature", True)

        # Daylight vs artificial light outside
        temp = 6500 if is_daytime else 3200
        comp.set_editor_property("temperature", temp)

        return {
            "success": True,
            "name": f"Window_{direction.capitalize()}",
            "location": window_location,
            "size": window_size,
            "direction": direction,
            "temperature": temp
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = create_window_light(
    window_location=(0, 400, 200),
    window_size=(200, 250),
    direction="north",
    is_daytime=True
)
```

**Step 3: Add accent/practical lights**

```python
import unreal
from typing import Dict, Any, Tuple

def create_lamp_light(
    location: Tuple[float, float, float],
    lamp_name: str = "TableLamp",
    intensity: float = 800.0,
    color: Tuple[float, float, float] = (1.0, 0.9, 0.7)
) -> Dict[str, Any]:
    """Create a practical lamp light (table lamp, floor lamp)."""
    try:
        light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.PointLight,
            unreal.Vector(*location)
        )

        if not light:
            return {"success": False, "error": "Failed to spawn PointLight"}

        light.set_actor_label(lamp_name)

        comp = light.get_component_by_class(unreal.PointLightComponent)
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("attenuation_radius", 300.0)
        comp.set_editor_property("source_radius", 5.0)
        comp.set_editor_property("light_color",
            unreal.LinearColor(color[0], color[1], color[2], 1.0))
        comp.set_editor_property("cast_shadows", True)

        return {
            "success": True,
            "name": lamp_name,
            "location": location,
            "intensity": intensity,
            "color": color
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = create_lamp_light(
    location=(800, 100, 80),
    lamp_name="DeskLamp_01",
    intensity=800
)
```

### Expected Output

```text
🏠 Interior Lighting Setup Complete

1. Ceiling Lights (6 total)
   - Grid: 400 unit spacing
   - Intensity: 2000 cd each
   - Temperature: 4000K (warm white)

2. Window Light (Window_North)
   - Size: 200x250
   - Intensity: 5000 cd
   - Temperature: 6500K (daylight)

3. Practical Light (DeskLamp_01)
   - Intensity: 800 cd
   - Color: Warm yellow
   - Shadows: Enabled

✅ Interior lighting complete
```

---

## Scenario 4: Cinematic Lighting

Dramatic lighting for cutscenes and cinematics.

### User Request

"시네마틱 라이팅 설정해줘" / "Set up cinematic lighting"

### Step-by-Step

**Step 1: Create dramatic key light**

```python
import unreal
from typing import Dict, Any, Tuple

def create_cinematic_key(
    target_location: Tuple[float, float, float],
    angle: float = 45.0,
    intensity: float = 15000.0,
    shadow_sharpness: float = 0.5
) -> Dict[str, Any]:
    """Create dramatic key light for cinematics."""
    try:
        import math
        tx, ty, tz = target_location
        distance = 600.0

        # Calculate position based on angle
        x = tx + distance * math.cos(math.radians(angle))
        y = ty - distance * math.sin(math.radians(angle))
        z = tz + distance * 0.5

        light = unreal.EditorLevelLibrary.spawn_actor_from_class(
            unreal.SpotLight,
            unreal.Vector(x, y, z)
        )

        if not light:
            return {"success": False, "error": "Failed to spawn SpotLight"}

        light.set_actor_label("Cinematic_Key")

        comp = light.get_component_by_class(unreal.SpotLightComponent)
        comp.set_editor_property("intensity", intensity)
        comp.set_editor_property("outer_cone_angle", 35.0)
        comp.set_editor_property("inner_cone_angle", 25.0)

        # Shadow settings for cinematic quality
        comp.set_editor_property("cast_shadows", True)
        comp.set_editor_property("source_radius", 10.0 * shadow_sharpness)

        # High contrast setup
        comp.set_editor_property("use_temperature", True)
        comp.set_editor_property("temperature", 5200)

        return {
            "success": True,
            "name": "Cinematic_Key",
            "location": (x, y, z),
            "angle": angle,
            "intensity": intensity
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = create_cinematic_key(
    target_location=(0, 0, 100),
    angle=60.0,
    intensity=15000.0
)
```

**Step 2: Add colored accent lights**

```python
import unreal
from typing import Dict, Any, Tuple, List

def create_color_accent_lights(
    locations: List[Tuple[float, float, float]],
    colors: List[Tuple[float, float, float]],
    intensity: float = 3000.0
) -> Dict[str, Any]:
    """Create colored accent lights for mood."""
    try:
        created = []

        for i, (loc, color) in enumerate(zip(locations, colors)):
            light = unreal.EditorLevelLibrary.spawn_actor_from_class(
                unreal.PointLight,
                unreal.Vector(*loc)
            )

            if light:
                name = f"Accent_Color_{i:02d}"
                light.set_actor_label(name)

                comp = light.get_component_by_class(unreal.PointLightComponent)
                comp.set_editor_property("intensity", intensity)
                comp.set_editor_property("attenuation_radius", 500.0)
                comp.set_editor_property("light_color",
                    unreal.LinearColor(color[0], color[1], color[2], 1.0))
                comp.set_editor_property("cast_shadows", False)

                created.append({
                    "name": name,
                    "color": color,
                    "location": loc
                })

        return {
            "success": True,
            "lights_created": len(created),
            "lights": created
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

# Create complementary color scheme (teal/orange)
result = create_color_accent_lights(
    locations=[(-400, 0, 50), (400, 200, 50)],
    colors=[(0.0, 0.5, 0.6), (1.0, 0.4, 0.1)],  # Teal and orange
    intensity=2000.0
)
```

### Expected Output

```text
🎬 Cinematic Lighting Setup Complete

1. Key Light (Cinematic_Key)
   - Angle: 60° (Rembrandt style)
   - Intensity: 15000 cd
   - Cone: 35° outer / 25° inner
   - High contrast shadows

2. Color Accents (2 lights)
   - Accent_Color_00: Teal (0, 0.5, 0.6)
   - Accent_Color_01: Orange (1.0, 0.4, 0.1)
   - Complementary color scheme

✅ Cinematic mood established
```

---

## Scenario 5: Performance-Optimized Lighting

Lighting setup optimized for real-time performance.

### User Request

"성능 최적화된 조명 설정" / "Set up performance-optimized lighting"

### Step-by-Step

**Step 1: Analyze current lighting**

```python
import unreal
from typing import Dict, Any

def analyze_lighting_performance() -> Dict[str, Any]:
    """Analyze current level lighting for performance issues."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()

        stats = {
            "total_lights": 0,
            "shadow_casting": 0,
            "movable": 0,
            "stationary": 0,
            "static": 0,
            "overlapping_lights": 0,
            "by_type": {}
        }

        light_positions = []

        for actor in actors:
            light_comp = actor.get_component_by_class(unreal.LightComponent)
            if not light_comp:
                continue

            stats["total_lights"] += 1

            # Mobility
            mobility = light_comp.get_editor_property("mobility")
            if mobility == unreal.ComponentMobility.MOVABLE:
                stats["movable"] += 1
            elif mobility == unreal.ComponentMobility.STATIONARY:
                stats["stationary"] += 1
            else:
                stats["static"] += 1

            # Shadows
            if light_comp.get_editor_property("cast_shadows"):
                stats["shadow_casting"] += 1

            # Type counting
            class_name = actor.get_class().get_name()
            light_type = "other"
            if "Point" in class_name:
                light_type = "point"
            elif "Spot" in class_name:
                light_type = "spot"
            elif "Directional" in class_name:
                light_type = "directional"
            elif "Rect" in class_name:
                light_type = "rect"

            stats["by_type"][light_type] = stats["by_type"].get(light_type, 0) + 1

            # Track positions for overlap detection
            loc = actor.get_actor_location()
            radius = 1000  # Default check radius
            try:
                radius = light_comp.get_editor_property("attenuation_radius")
            except:
                pass
            light_positions.append((loc, radius))

        # Simple overlap detection
        for i, (pos1, rad1) in enumerate(light_positions):
            for j, (pos2, rad2) in enumerate(light_positions[i+1:], i+1):
                dist = (pos1 - pos2).length()
                if dist < (rad1 + rad2) * 0.5:
                    stats["overlapping_lights"] += 1

        # Warnings
        warnings = []
        if stats["shadow_casting"] > 4:
            warnings.append(f"High shadow count: {stats['shadow_casting']} (recommend ≤4)")
        if stats["movable"] > 2:
            warnings.append(f"Many movable lights: {stats['movable']} (expensive)")
        if stats["overlapping_lights"] > 10:
            warnings.append(f"Overlapping lights: {stats['overlapping_lights']}")

        return {
            "success": True,
            "stats": stats,
            "warnings": warnings,
            "performance_score": max(0, 100 - len(warnings) * 20)
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = analyze_lighting_performance()
```

**Step 2: Optimize lights for performance**

```python
import unreal
from typing import Dict, Any

def optimize_lights_for_performance(
    disable_shadows_below_intensity: float = 1000.0,
    max_shadow_casting: int = 4
) -> Dict[str, Any]:
    """Optimize lights for better performance."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        optimized = []
        shadow_lights = []

        for actor in actors:
            light_comp = actor.get_component_by_class(unreal.LightComponent)
            if not light_comp:
                continue

            name = actor.get_actor_label()
            intensity = light_comp.get_editor_property("intensity")
            changes = []

            # Collect shadow-casting lights
            if light_comp.get_editor_property("cast_shadows"):
                shadow_lights.append((name, intensity, light_comp))

            # Disable shadows on low-intensity lights
            if intensity < disable_shadows_below_intensity:
                if light_comp.get_editor_property("cast_shadows"):
                    light_comp.set_editor_property("cast_shadows", False)
                    changes.append("shadows_disabled")

            if changes:
                optimized.append({"name": name, "changes": changes})

        # Keep only top N shadow-casting lights by intensity
        shadow_lights.sort(key=lambda x: x[1], reverse=True)
        for name, intensity, comp in shadow_lights[max_shadow_casting:]:
            comp.set_editor_property("cast_shadows", False)
            optimized.append({
                "name": name,
                "changes": ["shadows_disabled_limit"]
            })

        return {
            "success": True,
            "lights_optimized": len(optimized),
            "details": optimized,
            "shadow_lights_kept": min(len(shadow_lights), max_shadow_casting)
        }

    except Exception as e:
        return {"success": False, "error": str(e)}

result = optimize_lights_for_performance(
    disable_shadows_below_intensity=1000.0,
    max_shadow_casting=4
)
```

### Expected Output

```yaml
⚡ Performance Optimization Results

Before:
- Total lights: 24
- Shadow casting: 12
- Overlapping: 8
- Performance score: 40/100

After Optimization:
- Shadows disabled on 8 lights (low intensity)
- Shadow casters limited to top 4
- Overlap warnings remain (manual review needed)

Changes Applied:
- CeilingLight_05: shadows_disabled
- CeilingLight_06: shadows_disabled
- Accent_01: shadows_disabled_limit
...

New Performance Score: 80/100
✅ Optimization complete
```

---

## Common Workflow Patterns

### Batch Light Updates

```python
import unreal
from typing import Dict, Any, List

def batch_update_lights(
    light_names: List[str],
    properties: Dict[str, Any]
) -> Dict[str, Any]:
    """Update multiple lights with same properties."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        updated = []
        failed = []

        # Build lookup
        light_map = {}
        for actor in actors:
            if actor.get_actor_label() in light_names:
                light_map[actor.get_actor_label()] = actor

        for name in light_names:
            actor = light_map.get(name)
            if not actor:
                failed.append({"name": name, "error": "not found"})
                continue

            comp = actor.get_component_by_class(unreal.LightComponent)
            if not comp:
                failed.append({"name": name, "error": "no light component"})
                continue

            for prop, value in properties.items():
                try:
                    if prop == "light_color" and isinstance(value, (tuple, list)):
                        value = unreal.LinearColor(*value, 1.0)
                    comp.set_editor_property(prop, value)
                except:
                    pass

            updated.append(name)

        return {
            "success": len(updated) > 0,
            "updated": updated,
            "failed": failed
        }

    except Exception as e:
        return {"success": False, "error": str(e)}
```

### Error Recovery

```python
import unreal
from typing import Dict, Any

def safe_delete_all_lights(prefix: str = None) -> Dict[str, Any]:
    """Safely delete lights with optional prefix filter."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        deleted = []
        skipped = []

        for actor in actors:
            if not actor.get_component_by_class(unreal.LightComponent):
                continue

            name = actor.get_actor_label()

            # Apply prefix filter
            if prefix and not name.startswith(prefix):
                continue

            # Skip protected lights
            if name.startswith("Sun_") or name.startswith("Sky"):
                skipped.append({"name": name, "reason": "protected"})
                continue

            try:
                unreal.EditorLevelLibrary.destroy_actor(actor)
                deleted.append(name)
            except Exception as e:
                skipped.append({"name": name, "reason": str(e)})

        return {
            "success": True,
            "deleted_count": len(deleted),
            "deleted": deleted,
            "skipped": skipped
        }

    except Exception as e:
        return {"success": False, "error": str(e)}
```

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [REFERENCE.md](REFERENCE.md) | API reference |
| [ADVANCED.md](ADVANCED.md) | Lumen, GI, ray tracing |

---

**Last Updated**: 2026-01-28
