# Animation Workflow Reference

**Version**: 1.3.0
**Parent**: [SKILL.md](SKILL.md)

---

## Function Reference

### get_animbp_structure(asset_path: str) -> Dict[str, Any]

Retrieves the structure of an Animation Blueprint including state machines and skeleton info.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `asset_path` | str | Yes | Full asset path (e.g., `/Game/Characters/BP_Player_AnimBP`) |

**Returns**:

```python
{
    "success": bool,           # True if operation succeeded
    "name": str,               # AnimBP name
    "skeleton": str | None,    # Target skeleton path
    "parent_class": str,       # Parent class name
    "error": str               # Error message (only if success=False)
}
```markdown

**Error Codes**:

| Code | Message | Cause |
|------|---------|-------|
| ANIM_001 | `AnimBP not found: {path}` | Invalid asset path or asset doesn't exist |
| ANIM_002 | `Failed to load AnimBP: {error}` | Asset loading error |

**Example**:

```python
result = get_animbp_structure("/Game/Characters/Player/ABP_Player")
if result["success"]:
    skeleton = result["skeleton"]
# Output: {"success": True, "name": "ABP_Player", "skeleton": "/Game/Characters/SK_Player", "parent_class": "AnimInstance"}
```markdown

---

### play_montage(character_name, montage_path, play_rate, start_section) -> Dict[str, Any]

Plays an Animation Montage on a character in the level.

**Parameters**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `character_name` | str | Yes | - | Actor label in the level |
| `montage_path` | str | Yes | - | Full asset path to AnimMontage |
| `play_rate` | float | No | 1.0 | Playback speed multiplier |
| `start_section` | str | No | None | Section name to start from |

**Returns**:

```python
{
    "success": bool,
    "montage": str,            # Montage name
    "duration": float,         # Playback duration in seconds
    "play_rate": float,        # Applied play rate
    "error": str               # Error message (only if success=False)
}
```markdown

**Error Codes**:

| Code | Message | Cause |
|------|---------|-------|
| ANIM_010 | `Character not found: {name}` | No actor with matching label |
| ANIM_011 | `Montage not found: {path}` | Invalid montage asset path |
| ANIM_012 | `No SkeletalMeshComponent found` | Character lacks mesh component |
| ANIM_013 | `No AnimInstance found` | Character has no AnimBP assigned |

**Example**:

```python
result = play_montage(
    character_name="BP_PlayerCharacter",
    montage_path="/Game/Animations/AM_Attack_Melee",
    play_rate=1.5,
    start_section="Impact"
)
# Output: {"success": True, "montage": "AM_Attack_Melee", "duration": 1.2, "play_rate": 1.5}
```markdown

---

### stop_montage(character_name: str, montage_path: str, blend_out: float) -> Dict[str, Any]

Stops a playing montage on a character with optional blend out.

**Parameters**:

| Parameter | Type | Required | Default | Description |
|-----------|------|----------|---------|-------------|
| `character_name` | str | Yes | - | Actor label in the level |
| `montage_path` | str | No | None | Specific montage to stop (None = all) |
| `blend_out` | float | No | 0.25 | Blend out duration in seconds |

**Returns**:

```python
{
    "success": bool,
    "stopped": bool,           # True if montage was playing
    "blend_out": float,        # Applied blend out time
    "error": str
}
```text

**Example**:

```python
import unreal
from typing import Dict, Any, Optional

def stop_montage(
    character_name: str,
    montage_path: Optional[str] = None,
    blend_out: float = 0.25
) -> Dict[str, Any]:
    """Stop animation montage on character."""
    try:
        actors = unreal.EditorLevelLibrary.get_all_level_actors()
        character = None
        for actor in actors:
            if actor.get_actor_label() == character_name:
                character = actor
                break

        if not character:
            return {"success": False, "error": f"Character not found: {character_name}"}

        mesh_comp = character.get_component_by_class(unreal.SkeletalMeshComponent)
        if not mesh_comp:
            return {"success": False, "error": "No SkeletalMeshComponent found"}

        anim_instance = mesh_comp.get_anim_instance()
        if not anim_instance:
            return {"success": False, "error": "No AnimInstance found"}

        if montage_path:
            montage = unreal.load_asset(montage_path)
            if montage:
                anim_instance.montage_stop(blend_out, montage)
        else:
            anim_instance.stop_all_montages(blend_out)

        return {"success": True, "stopped": True, "blend_out": blend_out}
    except Exception as e:
        return {"success": False, "error": f"Failed to stop montage: {str(e)}"}
```markdown

---

### list_montage_notifies(montage_path: str) -> Dict[str, Any]

Lists all Animation Notifies in a montage.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `montage_path` | str | Yes | Full asset path to AnimMontage |

**Returns**:

```python
{
    "success": bool,
    "montage": str,
    "notifies": [
        {
            "name": str,           # Notify name
            "time": float,         # Trigger time in seconds
            "duration": float,     # Duration (0 for instant notifies)
            "notify_class": str    # Class name (e.g., "AnimNotify_PlaySound")
        }
    ],
    "count": int,
    "error": str
}
```text

**Example**:

```python
result = list_montage_notifies("/Game/Animations/AM_Attack_Combo")
# Output: {
#     "success": True,
#     "montage": "AM_Attack_Combo",
#     "notifies": [
#         {"name": "AttackHit", "time": 0.35, "duration": 0, "notify_class": "AnimNotify_PlaySound"},
#         {"name": "ComboWindow", "time": 0.5, "duration": 0.3, "notify_class": "AnimNotifyState_ComboWindow"}
#     ],
#     "count": 2
# }
```markdown

---

### get_skeleton_bones(skeleton_path: str) -> Dict[str, Any]

Retrieves all bone names from a skeleton asset.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `skeleton_path` | str | Yes | Full asset path to Skeleton |

**Returns**:

```python
{
    "success": bool,
    "skeleton": str,
    "bones": [str],            # List of bone names
    "bone_count": int,
    "error": str
}
```text

**Example**:

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

# Usage
result = get_skeleton_bones("/Game/Characters/SK_Mannequin")
# Output: {"success": True, "skeleton": "SK_Mannequin", "bones": ["root", "pelvis", "spine_01", ...], "bone_count": 67}
```markdown

---

### get_anim_sequence_info(sequence_path: str) -> Dict[str, Any]

Gets detailed information about an Animation Sequence.

**Parameters**:

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `sequence_path` | str | Yes | Full asset path to AnimSequence |

**Returns**:

```python
{
    "success": bool,
    "name": str,
    "duration": float,         # Length in seconds
    "frame_count": int,        # Total frames
    "frame_rate": float,       # FPS
    "is_looping": bool,
    "skeleton": str,           # Target skeleton path
    "error": str
}
```text

**Example**:

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
```markdown

---

## Type Definitions

### AnimBPInfo

```python
class AnimBPInfo(TypedDict):
    name: str
    skeleton: Optional[str]
    parent_class: str
    state_machines: List[str]
```markdown

### MontageInfo

```python
class MontageInfo(TypedDict):
    name: str
    duration: float
    play_rate: float
    sections: List[str]
```markdown

### NotifyInfo

```python
class NotifyInfo(TypedDict):
    name: str
    time: float
    duration: float
    notify_class: str
```markdown

### BoneInfo

```python
class BoneInfo(TypedDict):
    name: str
    index: int
    parent_index: int
```

---

## Error Code Reference

| Category | Code Range | Description |
|----------|------------|-------------|
| AnimBP | ANIM_001-009 | Animation Blueprint errors |
| Montage | ANIM_010-019 | Montage playback errors |
| Notify | ANIM_020-029 | Anim Notify errors |
| Skeleton | ANIM_030-039 | Skeleton/Bone errors |
| General | ANIM_090-099 | General animation errors |

---

## Console Commands

| Command | Description |
|---------|-------------|
| `ShowDebug Animation` | Toggle animation debug display |
| `a.AnimNode.DebugDrawPose` | Draw current pose |
| `a.AnimNode.ShowSkeletonInfo` | Display skeleton bone info |
| `a.AnimNode.DebugMontages` | Debug montage states |

---

## Related Documentation

| Document | Description |
|----------|-------------|
| [SKILL.md](SKILL.md) | Main skill documentation |
| [EXAMPLES.md](EXAMPLES.md) | Usage scenarios |
| [ADVANCED.md](ADVANCED.md) | State machines, retargeting |

---

**Tracking**: Issue #3995 - Engine Systems Skills Enhancement
**Last Updated**: 2026-01-28
