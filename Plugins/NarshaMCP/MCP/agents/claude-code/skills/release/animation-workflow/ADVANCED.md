# Animation Workflow Advanced

**Version**: 1.3.0
**Parent**: [SKILL.md](SKILL.md)

---

## State Machine Analysis

### Overview

Animation Blueprints often contain multiple state machines for different animation layers (locomotion, upper body, full body). Analyzing these programmatically helps debug complex animation behaviors.

### Implementation

```python
import unreal
from typing import Dict, Any, List

def get_state_machine_info(animgraph_path: str) -> Dict[str, Any]:
    """Get state machine information from AnimGraph."""
    try:
        anim_bp = unreal.load_asset(animgraph_path)
        if not anim_bp:
            return {"success": False, "error": f"AnimBP not found: {animgraph_path}"}

        # Access the generated class to get state machine data
        anim_class = anim_bp.generated_class()
        if not anim_class:
            return {"success": False, "error": "No generated class found"}

        state_machines: List[Dict[str, Any]] = []

        # Note: State machine introspection requires CDO access
        cdo = unreal.get_default_object(anim_class)
        if cdo:
            # Get state machine names from the blueprint
            state_machines.append({
                "name": "MainStateMachine",
                "states": ["Idle", "Walk", "Run", "Jump"],  # Example
                "current_state": "Unknown"  # Runtime only
            })

        return {
            "success": True,
            "anim_bp": anim_bp.get_name(),
            "state_machines": state_machines,
            "machine_count": len(state_machines)
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to analyze state machines: {str(e)}"}
```markdown

### State Transition Debugging

```python
def trace_state_transitions(anim_instance) -> Dict[str, Any]:
    """Trace state machine transitions (runtime only)."""
    try:
        # This requires PIE mode with active anim instance
        if not anim_instance:
            return {"success": False, "error": "No AnimInstance provided"}

        # Get current state for each state machine
        transitions: List[Dict[str, Any]] = []

        # Note: State machine index starts at 0
        machine_index = 0
        current_state = anim_instance.get_current_state_name(machine_index)

        transitions.append({
            "machine_index": machine_index,
            "current_state": str(current_state),
            "timestamp": unreal.GameplayStatics.get_real_time_seconds(None)
        })

        return {"success": True, "transitions": transitions}
    except Exception as e:
        return {"success": False, "error": f"Failed to trace transitions: {str(e)}"}
```markdown

### Best Practices

- Use `ShowDebug Animation` console command for visual debugging
- State machine names should be descriptive (e.g., "Locomotion", "UpperBody")
- Limit state count per machine to maintain readability (< 10 states)

---

## Animation Retargeting

### Overview

Animation retargeting allows animations created for one skeleton to be used on another. This is useful for sharing animations across different character types.

### IK Retargeter Setup

```python
import unreal
from typing import Dict, Any

def create_ik_retargeter(
    source_skeleton_path: str,
    target_skeleton_path: str,
    retargeter_name: str
) -> Dict[str, Any]:
    """Create an IK Retargeter asset for skeleton mapping."""
    try:
        source_skeleton = unreal.load_asset(source_skeleton_path)
        target_skeleton = unreal.load_asset(target_skeleton_path)

        if not source_skeleton:
            return {"success": False, "error": f"Source skeleton not found: {source_skeleton_path}"}
        if not target_skeleton:
            return {"success": False, "error": f"Target skeleton not found: {target_skeleton_path}"}

        # Note: IK Retargeter creation requires Editor scripting utilities
        # This is a conceptual example - actual implementation varies by UE version

        return {
            "success": True,
            "retargeter_name": retargeter_name,
            "source": source_skeleton.get_name(),
            "target": target_skeleton.get_name(),
            "status": "Created - requires manual chain mapping"
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to create retargeter: {str(e)}"}
```markdown

### Bone Mapping Validation

```python
def validate_bone_mapping(
    source_skeleton_path: str,
    target_skeleton_path: str
) -> Dict[str, Any]:
    """Validate bone name compatibility between skeletons."""
    try:
        source = unreal.load_asset(source_skeleton_path)
        target = unreal.load_asset(target_skeleton_path)

        if not source or not target:
            return {"success": False, "error": "Skeleton(s) not found"}

        source_ref = source.get_reference_skeleton()
        target_ref = target.get_reference_skeleton()

        source_bones = set()
        target_bones = set()

        for i in range(source_ref.get_num_bones()):
            source_bones.add(str(source_ref.get_bone_name(i)))

        for i in range(target_ref.get_num_bones()):
            target_bones.add(str(target_ref.get_bone_name(i)))

        matching = source_bones.intersection(target_bones)
        source_only = source_bones - target_bones
        target_only = target_bones - source_bones

        return {
            "success": True,
            "matching_bones": list(matching),
            "source_only": list(source_only),
            "target_only": list(target_only),
            "compatibility_score": len(matching) / max(len(source_bones), len(target_bones))
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to validate mapping: {str(e)}"}
```markdown

### Best Practices

- Ensure source and target have similar bone hierarchies
- Map IK chains (spine, arms, legs) before individual bones
- Test retargeting with locomotion animations first
- Check for foot sliding and hand positioning issues

---

## Blend Space Configuration

### Overview

Blend Spaces allow smooth blending between multiple animations based on input parameters (speed, direction, etc.).

### Analyzing Blend Space Structure

```python
import unreal
from typing import Dict, Any, List

def analyze_blend_space(blend_space_path: str) -> Dict[str, Any]:
    """Analyze Blend Space sample points and axes."""
    try:
        blend_space = unreal.load_asset(blend_space_path)
        if not blend_space:
            return {"success": False, "error": f"BlendSpace not found: {blend_space_path}"}

        # Get axis information
        axis_info = {
            "horizontal_axis": {
                "name": blend_space.get_editor_property("axis_to_scale_animation") or "Speed",
                "min": blend_space.get_editor_property("blend_parameters")[0].min if hasattr(blend_space, "blend_parameters") else 0,
                "max": blend_space.get_editor_property("blend_parameters")[0].max if hasattr(blend_space, "blend_parameters") else 1
            }
        }

        # Check if 2D blend space
        is_2d = "BlendSpace" in blend_space.get_class().get_name() and "1D" not in blend_space.get_class().get_name()

        return {
            "success": True,
            "name": blend_space.get_name(),
            "type": "2D" if is_2d else "1D",
            "axis_info": axis_info,
            "skeleton": str(blend_space.get_skeleton()) if blend_space.get_skeleton() else None
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to analyze blend space: {str(e)}"}
```markdown

### Sample Point Management

```python
def get_blend_space_samples(blend_space_path: str) -> Dict[str, Any]:
    """Get sample animation positions in blend space."""
    try:
        blend_space = unreal.load_asset(blend_space_path)
        if not blend_space:
            return {"success": False, "error": f"BlendSpace not found: {blend_space_path}"}

        samples: List[Dict[str, Any]] = []

        # Note: Sample access varies by UE version
        # This is a conceptual structure
        sample_data = blend_space.get_editor_property("sample_data") if hasattr(blend_space, "sample_data") else []

        for sample in sample_data:
            samples.append({
                "animation": str(sample.animation) if sample.animation else None,
                "position_x": sample.sample_value.x if hasattr(sample, "sample_value") else 0,
                "position_y": sample.sample_value.y if hasattr(sample, "sample_value") else 0
            })

        return {
            "success": True,
            "blend_space": blend_space.get_name(),
            "samples": samples,
            "sample_count": len(samples)
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to get samples: {str(e)}"}
```markdown

### Best Practices

- Use consistent axis ranges across blend spaces
- Place samples at grid intersections for predictable blending
- Test blend space in Persona before runtime use
- Consider using Aim Offset for additive directional blending

---

## Animation Notify Management

### Custom Notify Creation Pattern

```python
import unreal
from typing import Dict, Any

def add_notify_to_montage(
    montage_path: str,
    notify_class: str,
    trigger_time: float,
    notify_name: str = ""
) -> Dict[str, Any]:
    """Add an animation notify to a montage at specified time."""
    try:
        montage = unreal.load_asset(montage_path)
        if not montage:
            return {"success": False, "error": f"Montage not found: {montage_path}"}

        # Validate trigger time
        duration = montage.get_play_length()
        if trigger_time < 0 or trigger_time > duration:
            return {
                "success": False,
                "error": f"Trigger time {trigger_time} out of range (0-{duration})"
            }

        # Note: Adding notifies requires Editor Scripting Utilities
        # This is conceptual - actual API may vary
        return {
            "success": True,
            "montage": montage.get_name(),
            "notify_class": notify_class,
            "trigger_time": trigger_time,
            "notify_name": notify_name,
            "status": "Notify placement prepared - save asset to apply"
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to add notify: {str(e)}"}
```markdown

### Notify State Duration Handling

```python
def validate_notify_states(montage_path: str) -> Dict[str, Any]:
    """Validate AnimNotifyState durations don't exceed montage length."""
    try:
        montage = unreal.load_asset(montage_path)
        if not montage:
            return {"success": False, "error": f"Montage not found: {montage_path}"}

        duration = montage.get_play_length()
        issues: List[Dict[str, Any]] = []

        for notify in montage.notifies:
            if notify.get_duration() > 0:  # NotifyState
                end_time = notify.get_trigger_time() + notify.get_duration()
                if end_time > duration:
                    issues.append({
                        "notify_name": str(notify.notify_name),
                        "end_time": end_time,
                        "montage_duration": duration,
                        "overflow": end_time - duration
                    })

        return {
            "success": True,
            "montage": montage.get_name(),
            "issues": issues,
            "valid": len(issues) == 0
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to validate notifies: {str(e)}"}
```markdown

### Best Practices

- Name notifies descriptively (e.g., "FootstepL", "WeaponSwing")
- Use NotifyState for continuous effects (trails, sounds)
- Keep notify logic minimal - delegate to gameplay systems
- Document notify dependencies in asset metadata

---

## Animation LOD System

### Overview

Animation LOD reduces skeletal mesh update costs by simplifying animations at distance.

### LOD Analysis

```python
import unreal
from typing import Dict, Any, List

def analyze_skeletal_mesh_lods(mesh_path: str) -> Dict[str, Any]:
    """Analyze SkeletalMesh LOD settings for animation optimization."""
    try:
        skeletal_mesh = unreal.load_asset(mesh_path)
        if not skeletal_mesh:
            return {"success": False, "error": f"SkeletalMesh not found: {mesh_path}"}

        lod_info: List[Dict[str, Any]] = []

        # Get LOD count
        lod_count = skeletal_mesh.get_lod_num()

        for i in range(lod_count):
            lod_data = {
                "lod_index": i,
                "screen_size": skeletal_mesh.get_lod_screen_size_at_quality(i, 0),
            }
            lod_info.append(lod_data)

        return {
            "success": True,
            "mesh": skeletal_mesh.get_name(),
            "lod_count": lod_count,
            "lods": lod_info
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to analyze LODs: {str(e)}"}
```markdown

### Animation Budget Settings

```python
def configure_animation_budget(
    actor_name: str,
    significance_threshold: float = 0.5,
    update_rate_optimizations: bool = True
) -> Dict[str, Any]:
    """Configure animation budget settings for a character."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        target_actor = None

        for actor in actors:
            if actor.get_actor_label() == actor_name:
                target_actor = actor
                break

        if not target_actor:
            return {"success": False, "error": f"Actor not found: {actor_name}"}

        mesh_comp = target_actor.get_component_by_class(unreal.SkeletalMeshComponent)
        if not mesh_comp:
            return {"success": False, "error": "No SkeletalMeshComponent found"}

        # Configure URO (Update Rate Optimizations)
        if update_rate_optimizations:
            mesh_comp.set_editor_property("enable_update_rate_optimizations", True)

        return {
            "success": True,
            "actor": actor_name,
            "update_rate_optimizations": update_rate_optimizations,
            "significance_threshold": significance_threshold
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to configure budget: {str(e)}"}
```markdown

### Best Practices

- Enable URO for background characters
- Use significance manager for large crowds
- Test animation quality at each LOD distance
- Consider animation sharing for identical characters

---

## Performance Optimization

### Animation Thread Analysis

```python
def get_animation_tick_stats() -> Dict[str, Any]:
    """Get animation system tick statistics."""
    try:
        # Console command to enable animation stats
        world = unreal.EditorLevelLibrary.get_editor_world()
        unreal.SystemLibrary.execute_console_command(world, "stat anim")

        return {
            "success": True,
            "status": "Animation stats enabled in viewport",
            "metrics": [
                "Tick Time",
                "Bone Transform Update",
                "Notify Triggers",
                "Blend Evaluations"
            ]
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to enable stats: {str(e)}"}
```markdown

### Optimization Recommendations

| Issue | Detection | Solution |
|-------|-----------|----------|
| High bone count | > 100 bones | Use LOD with bone reduction |
| Many active montages | > 5 concurrent | Use montage priorities |
| Complex blend trees | > 20 nodes | Simplify or use cached poses |
| Frequent notifies | > 10/second | Batch notify processing |

---

## Integration with Gameplay

### GAS Integration Example

```python
def trigger_ability_from_notify(
    ability_tag: str,
    character_name: str
) -> Dict[str, Any]:
    """Pattern for triggering GameplayAbility from AnimNotify."""
    return {
        "success": True,
        "pattern": "AnimNotify_TriggerAbility",
        "usage": f"""
class UAnimNotify_TriggerAbility : public UAnimNotify
{{
    UPROPERTY(EditAnywhere)
    FGameplayTag AbilityTag;

    virtual void Notify(USkeletalMeshComponent* MeshComp, ...) override
    {{
        if (AActor* Owner = MeshComp->GetOwner())
        {{
            if (UAbilitySystemComponent* ASC = Owner->FindComponentByClass<UAbilitySystemComponent>())
            {{
                ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(AbilityTag));
            }}
        }}
    }}
}};
""",
        "ability_tag": ability_tag,
        "character": character_name
    }
```

---

## Engine Animation Nodes (GASP / FootPlacement / Motion Matching)

UE 5.4+ engine AnimNode plugins. These classes live in **engine PDBs**, so
`ue_cache_control(operation="load_engine_runtime")` must be called before searching.

### Key Classes

| Class | Module | Purpose |
|-------|--------|---------|
| `FAnimNode_FootPlacement` | AnimationWarpingRuntime | Foot IK + pelvis adjustment (curve-driven) |
| `FAnimNode_StrideWarping` | AnimationWarpingRuntime | Stride length warping |
| `FAnimNode_OrientationWarping` | AnimationWarpingRuntime | Orientation warping |
| `FAnimNode_OffsetRootBone` | AnimationWarpingRuntime | Root bone offset (distance matching) |
| `UPoseSearchLibrary` | PoseSearch | Motion Matching pose search |

### FootPlacement Curve System

`FAnimNode_FootPlacement` consumes animation curves to control IK behavior:

| Property | Default Curve | Effect |
|----------|--------------|--------|
| `DisablePelvisCurveName` | DisablePelvisAdjustment | Alpha 0-1: 1.0 = disable pelvis height adjustment |
| *(Left foot disable curve)* | DisableLeftFootIK | Alpha: 1.0 = disable left foot IK |
| *(Right foot disable curve)* | DisableRightFootIK | Alpha: 1.0 = disable right foot IK |

**Mechanism**: Curve value is read per-frame as an **Alpha blend factor**.
At jump apex (1.0), foot IK and pelvis adjustment are fully disabled to prevent ground-snapping artifacts.
On landing, curve blends from 1.0 back to 0.0, restoring IK correction.

**History**: Curve names were initially hardcoded, later refactored to configurable `FName` properties.

### Querying Engine Animation Nodes

```python
# 1. Load engine runtime PDBs (required for engine class search)
ue_cache_control(operation="load_engine_runtime")

# 2. Search engine symbols
ue_analyze_symbols(operation="search_symbols", params={"query": "FAnimNode_FootPlacement"})

# 3. Get class methods/properties
ue_analyze_symbols(operation="get_methods", params={"class_name": "FAnimNode_FootPlacement"})

# 4. Search engine docs for module documentation
ue_engine_docs(operation="search", query="FootPlacement AnimNode")
```

### GASP-Specific Patterns

GASP uses these curves in jump/fall animation sequences:
- Jump sequences set `DisablePelvisAdjustment = 1.0` during airborne phase
- Landing sequences blend the curve from 1.0 back to 0.0
- Left/right foot IK curves follow the same pattern

**Related**: AnimationWarping plugin, Motion Matching (PoseSearch plugin)

---

## Console Command Reference

### Animation Debug Commands

| Command | Description |
|---------|-------------|
| `showdebug animation` | Display animation debug info on screen |
| `showdebug bones` | Show bone hierarchy overlay |
| `showdebug montage` | Display montage playback debug |
| `a.animnode.statemachine.debug 1` | Enable state machine debug |

### Animation Node Commands

| Command | Description |
|---------|-------------|
| `a.animnode.warn 1` | Enable anim node warnings |
| `a.animnode.optimize 1` | Enable anim node optimization |
| `a.animnode.slotnode.printblendweights 1` | Print slot blend weights |

### Skeletal Mesh Commands

| Command | Description |
|---------|-------------|
| `skeletalmesh.lod.VisualizeAllLODs 1` | Visualize LOD levels |
| `p.clothphysics.visualize 1` | Visualize cloth physics |

### Performance Commands

| Command | Description |
|---------|-------------|
| `stat anim` | Show animation statistics |
| `stat AnimCharacterTotal` | Character animation totals |
| `stat AnimBudget` | Animation budget stats |

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [REFERENCE.md](REFERENCE.md) | API reference |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios |

---

**Tracking**: Issue #3995 - Engine Systems Skills Enhancement
**Last Updated**: 2026-01-28
