# Animation Workflow Examples

**Version**: 1.3.0
**Parent**: [SKILL.md](SKILL.md)

---

## Scenario 1: Character AnimBP Analysis

### User Request

"BP_PlayerCharacter의 AnimBP 구조 보여줘"

### Prerequisites

- Unreal Editor running with Remote Control enabled
- Character Blueprint with AnimBP assigned
- Asset path known or discoverable

### Step-by-Step

**Step 1**: Find the AnimBP asset path

```python
# If asset path unknown, search first
ue_search_assets(asset_type="animation", query="*Player*AnimBP*")
```text

**Step 2**: Analyze AnimBP structure

```python
import unreal
from typing import Dict, Any

def get_animbp_structure(asset_path: str) -> Dict[str, Any]:
    """Get AnimBP structure including state machines and graphs."""
    try:
        anim_bp = unreal.load_asset(asset_path)
        if not anim_bp:
            return {"success": False, "error": f"AnimBP not found: {asset_path}"}

        return {
            "success": True,
            "name": anim_bp.get_name(),
            "skeleton": str(anim_bp.target_skeleton) if anim_bp.target_skeleton else None,
            "parent_class": anim_bp.get_class().get_name(),
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to load AnimBP: {str(e)}"}

# Execute
result = get_animbp_structure("/Game/Characters/Player/ABP_PlayerCharacter")
```markdown

**Step 3**: Report results

### Expected Output

```json
{
    "success": true,
    "name": "ABP_PlayerCharacter",
    "skeleton": "/Game/Characters/SK_Mannequin",
    "parent_class": "AnimInstance"
}
```markdown

### Notes

- AnimBP must be saved and compiled
- Skeleton reference helps identify compatible animations
- Parent class indicates AnimInstance type (standard vs linked)

---

## Scenario 2: Combat Montage Playback

### User Request

"캐릭터가 공격 몽타주를 재생하도록 해줘"

### Prerequisites

- Character placed in level with SkeletalMesh
- AnimBP assigned to character
- AnimMontage asset exists

### Step-by-Step

**Step 1**: Identify character and montage

```python
# List characters in level
actors = unreal.EditorLevelLibrary.get_all_level_actors()
characters = [a for a in actors if "Character" in a.get_class().get_name()]
```text

**Step 2**: Play the montage

```python
import unreal
from typing import Dict, Any, Optional

def play_montage(
    character_name: str,
    montage_path: str,
    play_rate: float = 1.0,
    start_section: Optional[str] = None
) -> Dict[str, Any]:
    """Play animation montage on character."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        character = None
        for actor in actors:
            if actor.get_actor_label() == character_name:
                character = actor
                break

        if not character:
            return {"success": False, "error": f"Character not found: {character_name}"}

        montage = unreal.load_asset(montage_path)
        if not montage:
            return {"success": False, "error": f"Montage not found: {montage_path}"}

        mesh_comp = character.get_component_by_class(unreal.SkeletalMeshComponent)
        if not mesh_comp:
            return {"success": False, "error": "No SkeletalMeshComponent found"}

        anim_instance = mesh_comp.get_anim_instance()
        if not anim_instance:
            return {"success": False, "error": "No AnimInstance found"}

        duration = anim_instance.montage_play(montage, play_rate)
        if start_section:
            anim_instance.montage_jump_to_section(start_section, montage)

        return {
            "success": True,
            "montage": montage.get_name(),
            "duration": duration,
            "play_rate": play_rate
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to play montage: {str(e)}"}

# Execute with fast playback
result = play_montage(
    character_name="BP_PlayerCharacter_C_0",
    montage_path="/Game/Animations/Combat/AM_Sword_Attack",
    play_rate=1.2
)
```text

**Step 3**: Jump to specific section (optional)

```python
# Jump to "Impact" section for hit timing testing
result = play_montage(
    character_name="BP_PlayerCharacter_C_0",
    montage_path="/Game/Animations/Combat/AM_Sword_Attack",
    start_section="Impact"
)
```markdown

### Expected Output

```json
{
    "success": true,
    "montage": "AM_Sword_Attack",
    "duration": 1.8,
    "play_rate": 1.2
}
```markdown

### Notes

- Montage playback requires PIE or Simulate mode for proper animation
- Section names are case-sensitive
- Play rate affects both animation and notify timing

---

## Scenario 3: Animation Notify Analysis

### User Request

"AM_Attack_Combo 몽타주의 노티파이 타이밍 확인해줘"

### Prerequisites

- AnimMontage with notifies
- Asset path known

### Step-by-Step

**Step 1**: List all notifies in the montage

```python
import unreal
from typing import Dict, Any, List

def list_montage_notifies(montage_path: str) -> Dict[str, Any]:
    """List all animation notifies in a montage."""
    try:
        montage = unreal.load_asset(montage_path)
        if not montage:
            return {"success": False, "error": f"Montage not found: {montage_path}", "notifies": []}

        notifies: List[Dict[str, Any]] = []
        for notify in montage.notifies:
            notify_info = {
                "name": str(notify.notify_name) if notify.notify_name else "Unnamed",
                "time": notify.get_trigger_time(),
                "duration": notify.get_duration(),
                "notify_class": notify.notify.get_class().get_name() if notify.notify else "None"
            }
            notifies.append(notify_info)

        return {
            "success": True,
            "montage": montage.get_name(),
            "notifies": notifies,
            "count": len(notifies)
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to list notifies: {str(e)}", "notifies": []}

# Execute
result = list_montage_notifies("/Game/Animations/Combat/AM_Attack_Combo")
```text

**Step 2**: Analyze timing distribution

```python
# Sort notifies by time for sequence analysis
sorted_notifies = sorted(result["notifies"], key=lambda x: x["time"])
for i, notify in enumerate(sorted_notifies):
    time_str = f"{notify['time']:.2f}s"
    duration_str = f"({notify['duration']:.2f}s)" if notify['duration'] > 0 else "(instant)"
```markdown

### Expected Output

```json
{
    "success": true,
    "montage": "AM_Attack_Combo",
    "notifies": [
        {"name": "ComboInputWindow", "time": 0.15, "duration": 0.4, "notify_class": "AnimNotifyState_TimedNiagaraEffect"},
        {"name": "AttackHit_01", "time": 0.35, "duration": 0.0, "notify_class": "AnimNotify_PlaySound"},
        {"name": "AttackHit_02", "time": 0.75, "duration": 0.0, "notify_class": "AnimNotify_PlaySound"},
        {"name": "ComboEnd", "time": 1.2, "duration": 0.0, "notify_class": "AnimNotify"}
    ],
    "count": 4
}
```markdown

### Notes

- Duration > 0 indicates AnimNotifyState (has Begin/End events)
- Duration = 0 indicates instant AnimNotify
- Notify timing is relative to montage start, not section

---

## Scenario 4: Skeleton Bone Debugging

### User Request

"캐릭터 스켈레톤의 본 이름 확인해줘"

### Prerequisites

- Skeleton asset exists
- Asset path known or discoverable

### Step-by-Step

**Step 1**: Get skeleton from character or directly

```python
import unreal
from typing import Dict, Any, List

def get_skeleton_bones(skeleton_path: str) -> Dict[str, Any]:
    """Get all bone names from a skeleton."""
    try:
        skeleton = unreal.load_asset(skeleton_path)
        if not skeleton:
            return {"success": False, "error": f"Skeleton not found: {skeleton_path}"}

        bones: List[str] = []
        ref_skeleton = skeleton.get_reference_skeleton()
        bone_count = ref_skeleton.get_num_bones()

        for i in range(bone_count):
            bone_name = ref_skeleton.get_bone_name(i)
            bones.append(str(bone_name))

        return {
            "success": True,
            "skeleton": skeleton.get_name(),
            "bones": bones,
            "bone_count": len(bones)
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to get skeleton bones: {str(e)}"}

# Execute
result = get_skeleton_bones("/Game/Characters/Mannequin/SK_Mannequin")
```text

**Step 2**: Filter bones by category

```python
# Filter for specific bone chains
hand_bones = [b for b in result["bones"] if "hand" in b.lower()]
spine_bones = [b for b in result["bones"] if "spine" in b.lower()]
```markdown

### Expected Output

```json
{
    "success": true,
    "skeleton": "SK_Mannequin",
    "bones": [
        "root",
        "pelvis",
        "spine_01",
        "spine_02",
        "spine_03",
        "clavicle_l",
        "upperarm_l",
        "lowerarm_l",
        "hand_l",
        "..."
    ],
    "bone_count": 67
}
```markdown

### Notes

- Bone names follow UE naming conventions
- Root bone is typically first in hierarchy
- Use bone names for IK setup and socket attachment

---

## Scenario 5: Animation Sequence Inspection

### User Request

"이 애니메이션의 프레임 정보를 확인해줘"

### Prerequisites

- AnimSequence asset exists
- Asset path known

### Step-by-Step

**Step 1**: Get animation sequence details

```python
import unreal
from typing import Dict, Any

def get_anim_sequence_info(sequence_path: str) -> Dict[str, Any]:
    """Get Animation Sequence details."""
    try:
        anim_seq = unreal.load_asset(sequence_path)
        if not anim_seq:
            return {"success": False, "error": f"AnimSequence not found: {sequence_path}"}

        return {
            "success": True,
            "name": anim_seq.get_name(),
            "duration": anim_seq.get_play_length(),
            "frame_count": anim_seq.get_number_of_frames(),
            "frame_rate": anim_seq.get_frame_rate().numerator,
            "is_looping": anim_seq.is_looping(),
            "skeleton": str(anim_seq.get_skeleton()) if anim_seq.get_skeleton() else None
        }
    except Exception as e:
        return {"success": False, "error": f"Failed to get sequence info: {str(e)}"}

# Execute
result = get_anim_sequence_info("/Game/Animations/Locomotion/Run_Fwd")
```markdown

### Expected Output

```json
{
    "success": true,
    "name": "Run_Fwd",
    "duration": 0.8333,
    "frame_count": 25,
    "frame_rate": 30,
    "is_looping": true,
    "skeleton": "/Game/Characters/SK_Mannequin"
}
```markdown

### Notes

- Frame rate is typically 30 or 60 FPS
- Duration = frame_count / frame_rate
- Looping animations should have matching start/end poses

---

## Common Workflow Patterns

### Error Recovery

```python
from typing import Callable, Dict, Any

def safe_animation_operation(
    operation_func: Callable[..., Dict[str, Any]],
    *args: Any,
    **kwargs: Any
) -> Dict[str, Any]:
    """Wrapper for safe animation operations with retry."""
    max_retries = 3
    result: Dict[str, Any] = {"success": False, "error": "Max retries exceeded"}
    for attempt in range(max_retries):
        result = operation_func(*args, **kwargs)
        if result.get("success"):
            return result
        if "not found" in result.get("error", "").lower():
            # Asset not found - no retry needed
            break
    return result
```markdown

### Batch Processing

```python
def analyze_multiple_montages(montage_paths: List[str]) -> Dict[str, Any]:
    """Analyze multiple montages in one operation."""
    results = {}
    for path in montage_paths:
        results[path] = list_montage_notifies(path)
    return {"success": True, "analyses": results, "count": len(results)}
```

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [REFERENCE.md](REFERENCE.md) | API reference |
| [ADVANCED.md](ADVANCED.md) | State machines, retargeting |

---

**Tracking**: Issue #3995 - Engine Systems Skills Enhancement
**Last Updated**: 2026-01-28
