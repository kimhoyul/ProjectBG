---
name: character-movement
model: opus
description: "Purpose: UE5 character movement reference (UCharacterMovementComponent + ACharacter + ERootMotionMode/EMovementMode + network prediction + PhysCustom). Positive-only API tables verified against UE 5.7 source. Built from #13003 baseline (character-movement 10% halluc, dominated by ERootMotionMode 4-variant ambiguity). Triggers: 'UCharacterMovementComponent', 'ACharacter', 'EMovementMode', 'ERootMotionMode', 'MaxWalkSpeed', 'PhysFalling', 'PhysCustom', 'ServerMove', 'FSavedMove_Character', '캐릭터 이동', '루트 모션', '커스텀 무브먼트'."
compatibility: "Requires UE 5.x C++ project. Reference tables verified against UE 5.7 Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h and Animation/AnimEnums.h."
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  origin_issue: "#13002 Phase 1 priority #3 (#13114)"
  baseline_data: "#13003 100-probe baseline (character-movement 10% halluc, Easy 0% / Medium 20% / Hard 0%)"
  design_rule: "v1.1.0 cpp-foundations lesson — positive tables only, no wrong-name enumerations"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE5 Character Movement Reference

**Priority**: High
**Goal**: Authoritative API/enum tables for UCharacterMovementComponent (CMC), ACharacter, root motion, network prediction, and custom movement modes. Positive-only: every name in this skill is verified against UE 5.7 source at the cited file:line.

> **Why this skill exists**: Issue #13003's 100-probe baseline measured 10% hallucination rate on `character-movement` (2/20 partial). The dominant failure mode was `ERootMotionMode`'s 4-variant ambiguity (`NoRootMotionExtraction` / `IgnoreRootMotion` / `RootMotionFromEverything` / `RootMotionFromMontagesOnly`) — Claude reliably names *an* enumerator from the family but not the correct one for the use case. Secondary failures: `EMovementMode` → physics function mapping. This skill preloads the verified tables so a single-enumerator answer cites THE correct value.

---

## When to load this skill

The router auto-loads when the user mentions any of: `UCharacterMovementComponent`, `ACharacter`, `EMovementMode`, `ERootMotionMode`, `MaxWalkSpeed`, `JumpZVelocity`, `PhysWalking`/`PhysFalling`/`PhysCustom`, `ServerMove`, `FSavedMove_Character`, `FNetworkPredictionData_Client_Character`, or Korean equivalents (`캐릭터 이동`, `루트 모션`, `커스텀 무브먼트`).

**When NOT to load**: AnimInstance / AnimMontage interactions (animation-workflow skill), Enhanced Input mapping contexts (input-system skill), GameMode/PlayerController/Pawn lifecycle (gameplay-framework skill), or wheeled-vehicle / hovercraft physics (separate physics-vehicles skill).

---

## 1. Core class hierarchy

```text
UActorComponent
└── USceneComponent
    └── UMovementComponent                  (Engine/Public/GameFramework/MovementComponent.h)
        └── UNavMovementComponent           (Engine/Classes/GameFramework/NavMovementComponent.h)
            └── UPawnMovementComponent      (Engine/Classes/GameFramework/PawnMovementComponent.h)
                └── UCharacterMovementComponent  (Engine/Classes/GameFramework/CharacterMovementComponent.h)
```

`UCharacterMovementComponent` (CMC) is owned by `ACharacter`. Access via `ACharacter::GetCharacterMovement()` (`GameFramework/Character.h:401`).

---

## 2. `EMovementMode` enum

Source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:1005-1034`

```cpp
UENUM(BlueprintType)
enum EMovementMode : int
{
    MOVE_None        UMETA(DisplayName="None"),            // Movement is disabled.
    MOVE_Walking     UMETA(DisplayName="Walking"),         // Walking on a surface.
    MOVE_NavWalking  UMETA(DisplayName="Navmesh Walking"), // Simplified walking on navigation data.
    MOVE_Falling     UMETA(DisplayName="Falling"),         // Under gravity (post-jump, walked off edge).
    MOVE_Swimming    UMETA(DisplayName="Swimming"),        // In a fluid volume, gravity + buoyancy.
    MOVE_Flying      UMETA(DisplayName="Flying"),          // Ignores gravity, affected by fluid friction.
    MOVE_Custom      UMETA(DisplayName="Custom"),          // User-defined sub-mode (see § 6).
    MOVE_MAX         UMETA(Hidden),
};
```

Storage on CMC: `TEnumAsByte<enum EMovementMode> MovementMode;` (CharacterMovementComponent.h:229) + `uint8 CustomMovementMode;` (CharacterMovementComponent.h:237).

### Mode → physics function mapping

| `EMovementMode` value | Physics tick function called by CMC each frame |
|---|---|
| `MOVE_Walking` | `PhysWalking(float deltaTime, int32 Iterations)` |
| `MOVE_NavWalking` | `PhysNavWalking(float deltaTime, int32 Iterations)` |
| `MOVE_Falling` | `PhysFalling(float deltaTime, int32 Iterations)` |
| `MOVE_Swimming` | `PhysSwimming(float deltaTime, int32 Iterations)` |
| `MOVE_Flying` | `PhysFlying(float deltaTime, int32 Iterations)` |
| `MOVE_Custom` | `PhysCustom(float deltaTime, int32 Iterations)` — override in your subclass |
| `MOVE_None` / `MOVE_MAX` | (no physics tick) |

Switch modes at runtime via `SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode = 0)` (CharacterMovementComponent.h:1257).

---

## 3. `ERootMotionMode` enum

Source: `Engine/Source/Runtime/Engine/Classes/Animation/AnimEnums.h:26-43`

```cpp
UENUM()
namespace ERootMotionMode
{
    enum Type : int
    {
        NoRootMotionExtraction,        // Leave root motion in animation.
        IgnoreRootMotion,              // Extract root motion but do not apply it.
        RootMotionFromEverything,      // Root motion from all animations contributing to the final pose.
                                       //   NOT suitable for network multiplayer setups.
        RootMotionFromMontagesOnly,    // Root motion only from montages.
                                       //   Suitable for network multiplayer setups.
    };
}
```

### Picking the right value

| Goal | Use this value |
|---|---|
| Animation moves the bone but the capsule stays put | `NoRootMotionExtraction` |
| Read the root delta but don't move the capsule | `IgnoreRootMotion` |
| Single-player game, all animations drive locomotion | `RootMotionFromEverything` |
| Multiplayer game, only montages (e.g. attacks) drive root motion | `RootMotionFromMontagesOnly` |

Set via the SkeletalMeshComponent's anim instance settings or `USkeletalMeshComponent::SetAnimationMode` + project-level config; for replicated montages CMC consumes the result through the `FRootMotionSourceGroup CurrentRootMotion` member.

---

## 4. UCharacterMovementComponent — core public API

All entries verified against `Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h` at the cited line.

### Tunable UPROPERTYs (CDO-editable, blueprint read/write unless noted)

| Property | Type | Default | Line |
|---|---|---|---|
| `GravityScale` | `float` | `1.f` (initialized in `.cpp:667`; equivalent to `1.0f`) | 155 |
| `MaxStepHeight` | `float` | — | 159 |
| `JumpZVelocity` | `float` (ForceUnits cm/s) | — | 163 |
| `WalkableFloorAngle` | `float` (degrees, 0-90) | — | 183 |
| `MovementMode` | `TEnumAsByte<EMovementMode>` (read-only in BP) | — | 229 |
| `CustomMovementMode` | `uint8` (read-only in BP) | — | 237 |
| `GroundFriction` | `float` | — | 254 |
| `MaxWalkSpeed` | `float` (cm/s) | — | 274 |
| `MaxWalkSpeedCrouched` | `float` (cm/s) | — | 278 |
| `MaxSwimSpeed` | `float` (cm/s) | — | 282 |
| `MaxFlySpeed` | `float` (cm/s) | — | 286 |
| `MaxCustomMovementSpeed` | `float` (cm/s) | — | 290 |
| `MaxAcceleration` | `float` | — | 294 |
| `MinAnalogWalkSpeed` | `float` (cm/s) | — | 298 |
| `BrakingFrictionFactor` | `float` | `2` (historical default) | 307 |
| `BrakingFriction` | `float` (used when `bUseSeparateBrakingFriction=true`) | — | 318 |
| `BrakingDecelerationWalking` | `float` | — | 331 |
| `BrakingDecelerationFalling` | `float` | — | 338 |
| `AirControl` | `float` | — | 359 |

### Mode + state query functions

| Function | Signature | Line |
|---|---|---|
| `SetMovementMode` | `virtual void SetMovementMode(EMovementMode NewMovementMode, uint8 NewCustomMode = 0)` | 1257 |
| `IsFalling` | `virtual bool IsFalling() const override` | 1307 |
| `IsMovingOnGround` | `virtual bool IsMovingOnGround() const override` | 1308 |
| `StopActiveMovement` | `virtual void StopActiveMovement() override` | 1305 |

### Crouch + impulse + launch

| Function | Signature | Line |
|---|---|---|
| `Crouch` | `virtual void Crouch(bool bClientSimulation = false)` | 1787 |
| `UnCrouch` | `virtual void UnCrouch(bool bClientSimulation = false)` | 1793 |
| `Launch` | `virtual void Launch(FVector const& LaunchVel)` | 1445 |
| `AddImpulse` | `virtual void AddImpulse(FVector Impulse, bool bVelocityChange = false)` | 1868 |
| `AddForce` | `virtual void AddForce(FVector Force)` | 1881 |

### Per-mode physics tick (override in subclass when extending)

| Function | Signature | Line |
|---|---|---|
| `PhysWalking` | `virtual void PhysWalking(float deltaTime, int32 Iterations)` | 1956 |
| `PhysFalling` | `virtual void PhysFalling(float deltaTime, int32 Iterations)` | 1627 |
| `PhysFlying` | `virtual void PhysFlying(float deltaTime, int32 Iterations)` | 1962 |
| `PhysSwimming` | `virtual void PhysSwimming(float deltaTime, int32 Iterations)` | 1965 |
| `PhysCustom` | `virtual void PhysCustom(float deltaTime, int32 Iterations)` | 1968 |

---

## 5. Network prediction structs

### `FNetworkPredictionData_Client_Character`

Source: `CharacterMovementComponent.h:3080-3201`. Owned by CMC on autonomous-proxy clients; stores the move history used for client-side prediction + server reconciliation.

| Field | Type | Purpose | Line |
|---|---|---|---|
| `ClientUpdateRealTime` | `float` | Real-time stamp of the last sent `ServerMove` RPC | 3093 |
| `CurrentTimeStamp` | `float` | TimeStamp for the next move; resets at `MinTimeBetweenTimeStampResets` cadence | 3095 |
| `LastReceivedAckRealTime` | `float` | World real-time of the last server ack | 3098 |
| `SavedMoves` | `TArray<FSavedMovePtr>` | Pending moves, oldest → newest. Acked moves are removed | 3101 |
| `FreeMoves` | `TArray<FSavedMovePtr>` | Freelist for move object reuse | 3102 |
| `PendingMove` | `FSavedMovePtr` | Move processed locally, awaiting combine-with-next | 3103 |
| `LastAckedMove` | `FSavedMovePtr` | Last server-acknowledged move | 3104 |
| `MaxFreeMoveCount` | `int32` | Cap on freelist size | 3106 |
| `MaxSavedMoveCount` | `int32` | Cap on saved-move buffer | 3107 |
| `MaxClientSmoothingDeltaTime` | `float` | Max delta over which smoothing interpolation runs | 3136 |
| `SmoothingServerTimeStamp` | `double` | Tracks last server-move timestamp for smoothing | 3139 |
| `SmoothingClientTimeStamp` | `double` | Tracks client time as it converges to server | 3142 |
| `MaxMoveDeltaTime` | `float` | Largest allowed single-move delta time | 3171 |

### `FSavedMove_Character`

Source: `CharacterMovementComponent.h:2912-3049`. One entry per buffered move; override its virtuals when adding custom replicated state.

| Virtual | Purpose | Line |
|---|---|---|
| `SetMoveFor(ACharacter*, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character&)` | Capture pre-move state for replay | 3015 |
| `Clear()` | Reset move data for reuse from freelist | 3012 |
| `CanCombineWith(const FSavedMovePtr& NewMove, ACharacter*, float MaxDelta) const` | Allow merging adjacent moves to reduce bandwidth | 3036 |
| `PrepMoveFor(ACharacter*)` | Restore character state before replay during correction | 3042 |
| `GetCompressedFlags() const` | Pack boolean state into 1 byte for RPC (see § 6 flag table) | 3045 |

Typical key fields on `FSavedMove_Character`: `CharacterOwner`, `bPressedJump:1`, `bWantsToCrouch:1`, `bForceMaxAccel:1`, `bForceNoCombine:1`, `TimeStamp`, `DeltaTime`, `StartLocation`, `SavedLocation`, `SavedVelocity`, `SavedRootMotion` (FRootMotionSourceGroup).

---

## 6. Custom movement modes (`PhysCustom`) + compressed flags

### Registering a custom mode

```cpp
// 1. Pick a sub-mode value (0-255). Convention: define an enum class.
enum class ECustomMoveMode : uint8 { None = 0, Climbing = 1, WallRunning = 2, };

// 2. Switch into MOVE_Custom with your sub-mode.
GetCharacterMovement()->SetMovementMode(MOVE_Custom, (uint8)ECustomMoveMode::Climbing);

// 3. In your CMC subclass, override PhysCustom and dispatch on CustomMovementMode.
void UMyCharacterMovementComponent::PhysCustom(float deltaTime, int32 Iterations)
{
    Super::PhysCustom(deltaTime, Iterations);

    switch ((ECustomMoveMode)CustomMovementMode)
    {
        case ECustomMoveMode::Climbing:    PhysClimbing(deltaTime, Iterations); break;
        case ECustomMoveMode::WallRunning: PhysWallRunning(deltaTime, Iterations); break;
        default: break;
    }
}
```

### Replicating a custom mode boolean (e.g. "wants to climb")

CMC packs 8 boolean bits per move into the RPC via `GetCompressedFlags()`. 4 bits are reserved for engine use; 4 are yours.

Source: `CharacterMovementComponent.h:3057-3068`

```cpp
enum CompressedFlags
{
    FLAG_JumpPressed    = 0x01,   // Jump pressed
    FLAG_WantsToCrouch  = 0x02,   // Wants to crouch
    FLAG_Reserved_1     = 0x04,   // Reserved for future engine use
    FLAG_Reserved_2     = 0x08,   // Reserved for future engine use
    FLAG_Custom_0       = 0x10,   // Available for game code
    FLAG_Custom_1       = 0x20,   // Available for game code
    FLAG_Custom_2       = 0x40,   // Available for game code
    FLAG_Custom_3       = 0x80,   // Available for game code
};
```

To replicate a custom boolean: (1) add it to your `FSavedMove_Character` subclass, (2) OR its bit into `GetCompressedFlags()`, (3) read it back in `UpdateFromCompressedFlags(uint8 Flags)` on the server-side CMC, (4) trigger the matching state change.

---

## 7. ACharacter integration points

Source: `Engine/Source/Runtime/Engine/Classes/GameFramework/Character.h`

| Function | Signature | Line |
|---|---|---|
| `GetCharacterMovement` | `inline UCharacterMovementComponent* GetCharacterMovement() const` | 401 |
| `Jump` | `virtual void Jump()` | 711 |
| `StopJumping` | `virtual void StopJumping()` | 720 |
| `LaunchCharacter` | `virtual void LaunchCharacter(FVector LaunchVelocity, bool bXYOverride, bool bZOverride)` | 791 |
| `Landed` | `virtual void Landed(const FHitResult& Hit)` | 820 |
| `Crouch` | `virtual void Crouch(bool bClientSimulation = false)` | 871 |
| `UnCrouch` | `virtual void UnCrouch(bool bClientSimulation = false)` | 880 |
| `OnMovementModeChanged` | `virtual void OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode = 0)` | 924 |

`OnMovementModeChanged` fires after the mode actually changes — override on `ACharacter` to react in the owning actor (e.g. broadcast events, switch animations).

---

## 8. Verification + anti-hallucination workflow

Before naming an API, mentally confirm:

- The function belongs to `UCharacterMovementComponent` (not `ACharacter` directly) — `ACharacter::GetCharacterMovement()` is the bridge.
- The `EMovementMode` value name starts with `MOVE_` and matches one of the 8 in § 2.
- The `ERootMotionMode` value matches one of the 4 in § 3, and the network-suitability comment matches the use case.
- For custom movement, `PhysCustom` is the override point and `CustomMovementMode` is a `uint8` field — not a separate enum-typed UPROPERTY.
- Network compressed flags: 4 reserved bits (`0x04`, `0x08` are the two `Reserved_*`) + 4 custom bits (`0x10`-`0x80`).

When you cannot answer confidently from this reference, verify against UE 5.7 source:

```python
# Confirm a UCharacterMovementComponent member exists at the cited line
ue_grep(query="SetMovementMode", domain="source", scope="engine",
        path_pattern="Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h")

# Resolve the class hierarchy up from UCharacterMovementComponent
ue_analyze_symbols(operation="trace_hierarchy", query="UCharacterMovementComponent")

# Verify an enum value name
ue_grep(query="RootMotionFromMontagesOnly", domain="source", scope="engine")

# Pull official engine docs for the subsystem
ue_engine_docs(operation="search", query="UCharacterMovementComponent root motion")
```

---

## Trigger phrases (for reference / human review of routing rules)

Korean: `UCharacterMovementComponent`, `ACharacter`, `EMovementMode`, `ERootMotionMode`, `MaxWalkSpeed`, `캐릭터 이동`, `루트 모션`, `커스텀 무브먼트`, `캐릭터 점프`, `FSavedMove_Character`

English: `UCharacterMovementComponent`, `ACharacter`, `EMovementMode`, `ERootMotionMode`, `MaxWalkSpeed`, `MaxAcceleration`, `JumpZVelocity`, `GravityScale`, `GroundFriction`, `AirControl`, `PhysWalking`, `PhysFalling`, `PhysFlying`, `PhysSwimming`, `PhysCustom`, `ServerMove`, `ClientAdjustPosition`, `FSavedMove_Character`, `FNetworkPredictionData_Client_Character`, `GetCompressedFlags`, `SetMovementMode`, `MOVE_Walking`, `MOVE_Falling`, `MOVE_Custom`, `RootMotionFromEverything`, `RootMotionFromMontagesOnly`, `LaunchCharacter`, `OnMovementModeChanged`

Negative patterns (do NOT activate this skill):
- `AnimInstance`, `AnimMontage`, `PlayMontage` → animation-workflow
- `Enhanced Input`, `InputMappingContext` → input-system (#13115)
- `GameMode`, `PlayerController` → gameplay-framework (#13116)
- `vehicle`, `wheeled`, `hovercraft` → vehicle-physics (separate Phase 2)
- Multi-class "create new system from scratch" wording → code-generation-power

---

*This skill's content is authored from UE 5.7 source verification + Issue #13003's 100-probe baseline (#13114, Epic #13002 Phase 1 priority #3). When UE versions change, audit against the new `Engine/Source/Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h` and `Animation/AnimEnums.h` and update accordingly. Positive-only design rule locked by #13033 v1.0 cpp-foundations backfire — do NOT add wrong-name enumeration tables.*
