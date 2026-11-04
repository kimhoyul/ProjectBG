---
name: gameplay-framework
model: opus
description: "Purpose: UE5 gameplay framework reference (AGameModeBase/AGameMode + AGameStateBase/AGameState + APlayerController + APlayerState + APawn lifecycle + ENetRole + GetWorld()/UWorld access + level streaming). Positive-only API tables verified against UE 5.7 source. Built from #13003 baseline (gameplay-framework 5% halluc, dominated by GameState property naming in hard-difficulty probes). Triggers: 'AGameMode', 'AGameModeBase', 'AGameState', 'AGameStateBase', 'APlayerController', 'APlayerState', 'APawn lifecycle', 'BeginPlay', 'EndPlay', 'PossessedBy', 'ROLE_Authority', 'ROLE_AutonomousProxy', 'ROLE_SimulatedProxy', 'GetWorld', 'ServerTravel', '게임 모드', '플레이어 컨트롤러', '폰 라이프사이클', '네트워크 역할', '레벨 스트리밍'."
compatibility: "Requires UE 5.x C++ project. Reference tables verified against UE 5.7 Engine/Source/Runtime/Engine/Classes/GameFramework/{GameModeBase,GameMode,GameStateBase,GameState,PlayerController,PlayerState,Pawn,Controller,Actor}.h and Engine/{EngineTypes,World,LevelStreaming}.h."
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  origin_issue: "#13002 Phase 1 priority #5 (#13116)"
  baseline_data: "#13003 100-probe baseline (gameplay-framework 5% halluc, 1 partial / 20 — Easy 0% / Medium 0% / Hard 20%, single GameState property-naming failure)"
  design_rule: "v1.1.0 cpp-foundations lesson — positive tables only, no wrong-name enumerations"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE5 Gameplay Framework Reference

**Priority**: High
**Goal**: Authoritative API/enum tables for AGameModeBase/AGameMode, AGameStateBase/AGameState, APlayerController, APlayerState, APawn lifecycle, ENetRole network split, GetWorld()/UWorld access patterns, and level streaming. Positive-only: every name in this skill is verified against UE 5.7 source at the cited file:line.

> **Why this skill exists**: Issue #13003's 100-probe baseline measured 5% hallucination rate on `gameplay-framework` (1/20 partial) — the lowest of the 5 Phase 1 subsystems. The single failure was GameState property naming in a hard-difficulty probe. This is near-noise; the skill exists primarily as paradigm coverage and to lock the boundary against sibling skills (character-movement, animation-workflow, input-system, ai-development, gas-development, UMG). Detection power is limited — value is paradigm consistency, not raw rate improvement.

> **Naming discipline**: When a question asks for the name of a UE symbol (UPROPERTY, UFUNCTION, enum value, class), return the exact UE identifier verbatim — write `PlayerArray`, not "the players array"; write `GetController`, not "the controller getter"; write `ROLE_Authority`, not "the authority role". The identifiers below are the canonical names; use them as written when answering "what is the name of X".

---

## When to load this skill

The router auto-loads when the user mentions any of: `AGameMode`, `AGameModeBase`, `AGameState`, `AGameStateBase`, `APlayerController`, `APlayerState`, `APawn`, `BeginPlay`, `EndPlay`, `PossessedBy`, `OnPossess`, `MatchState`, `ROLE_Authority`, `ROLE_AutonomousProxy`, `ROLE_SimulatedProxy`, `ENetRole`, `GetWorld`, `UWorld`, `ServerTravel`, `seamless travel`, `level streaming`, `ULevelStreaming`, or Korean equivalents (`게임 모드`, `플레이어 컨트롤러`, `폰 라이프사이클`, `네트워크 역할`, `레벨 스트리밍`).

**When NOT to load**:
- `UCharacterMovementComponent` / `EMovementMode` / `ERootMotionMode` / `MaxWalkSpeed` → character-movement skill
- `AnimMontage` / `AnimInstance` / `PlayMontage` → animation-workflow skill
- `Enhanced Input` / `InputMappingContext` / `UInputAction` → input-system skill
- `AAIController` / `BehaviorTree` / `EQS` / AI MoveTo → ai-development skill
- `GAS` / `UGameplayAbility` / `GameplayEffect` → gas-development skill
- `UMG` / `UUserWidget` / Slate → umg-workflow skill (Phase 2, not yet shipped)
- `UPROPERTY` / `UFUNCTION` specifier semantics → cpp-foundations skill
- `UAssetManager` / `UDataAsset` / `UDataTable` → data-assets-tables skill

---

## 1. Core class hierarchy

```text
AActor                                       (GameFramework/Actor.h)
├── AInfo                                    (GameFramework/Info.h)
│   ├── AGameModeBase                        (GameFramework/GameModeBase.h:47)
│   │   └── AGameMode                        (GameFramework/GameMode.h:35)
│   ├── AGameStateBase                       (GameFramework/GameStateBase.h:32)
│   │   └── AGameState                       (GameFramework/GameState.h:16)
│   └── APlayerState                         (GameFramework/PlayerState.h:41)
├── APawn                                    (GameFramework/Pawn.h:42)
│   ├── ADefaultPawn                         (GameFramework/DefaultPawn.h)
│   └── ACharacter                           (GameFramework/Character.h) — see character-movement skill
└── AController                              (GameFramework/Controller.h)
    ├── APlayerController                    (GameFramework/PlayerController.h:260)
    └── AAIController                        (AIModule, see ai-development skill)
```

Server/client lifetime split: `AGameModeBase` exists **only on the server**. `AGameStateBase` is replicated to all clients. `APlayerState` exists once per connected player on every machine (replicated). `APlayerController` exists for the owning client + on the server (one server-side instance per connected player); on simulated proxies, the player controller is null. `APawn` exists on all machines that see the pawn (replicated).

---

## 2. AGameModeBase / AGameMode

**AGameModeBase** (`GameFramework/GameModeBase.h:47`) — base class for all game modes. Use when you don't need match-state machinery (single-player, lobby, menu, etc.). Server-only existence: `World->GetAuthGameMode()` returns nullptr on clients.

**AGameMode** (`GameFramework/GameMode.h:35`) — `AGameModeBase` subclass with multiplayer match state machine. Use when you need `MatchState` transitions (waiting → in-progress → post-match).

### AGameModeBase — login / spawn lifecycle (`GameModeBase.h`)

| Function | Signature | Line |
|---|---|---|
| `InitGame` | `virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)` — called before `PreInitializeComponents` of any actor | 62 |
| `InitGameState` | `virtual void InitGameState()` — called during `PreInitializeComponents` of GameMode | 69 |
| `PreLogin` | `virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)` — set `ErrorMessage` to reject | 293 |
| `Login` | `virtual APlayerController* Login(UPlayer* NewPlayer, ENetRole InRemoteRole, const FString& Portal, const FString& Options, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)` — returns the spawned PC | 323 |
| `PostLogin` | `virtual void PostLogin(APlayerController* NewPlayer)` — game-logic hook after Login succeeded | 326 |
| `OnPostLogin` | `virtual void OnPostLogin(AController* NewPlayer)` — terminal step before pawn spawn (UE 5.6+ replaces deprecated `DispatchPostLogin`) | 334 |
| `Logout` | `virtual void Logout(AController* Exiting)` | 342 |
| `HandleStartingNewPlayer` | `void HandleStartingNewPlayer(APlayerController* NewPlayer)` — default: calls `RestartPlayer` | 367 |
| `ChoosePlayerStart` | `AActor* ChoosePlayerStart(AController* Player)` — override to pick a spawn point | 411 |
| `FindPlayerStart` | `AActor* FindPlayerStart(AController* Player, const FString& IncomingName = TEXT(""))` | 422 |
| `RestartPlayer` | `virtual void RestartPlayer(AController* NewPlayer)` — spawns the pawn at the chosen start | 441 |
| `StartPlay` | `virtual void StartPlay()` — transitions to BeginPlay on all actors | 159 |

### AGameModeBase — class defaults (CDO-editable UPROPERTYs)

| UPROPERTY | Type | Line |
|---|---|---|
| `GameSessionClass` | `TSubclassOf<AGameSession>` | 88 |
| `GameStateClass` | `TSubclassOf<AGameStateBase>` | 92 |
| `PlayerControllerClass` | `TSubclassOf<APlayerController>` | 96 |
| `PlayerStateClass` | `TSubclassOf<APlayerState>` | 100 |
| `HUDClass` | `TSubclassOf<AHUD>` | 104 |
| `DefaultPawnClass` | `TSubclassOf<APawn>` | 108 |
| `SpectatorClass` | `TSubclassOf<ASpectatorPawn>` | 112 |
| `ReplaySpectatorPlayerControllerClass` | `TSubclassOf<APlayerController>` | 116 |

Runtime accessors: `GameSession` (Transient, L129), `GameState` (Transient `TObjectPtr<AGameStateBase>`, L133). Template helper `T* GetGameState() const` (L137) for typed cast.

### AGameMode — `MatchState` namespace constants (`GameMode.h:16-27`)

```cpp
namespace MatchState
{
    extern ENGINE_API const FName EnteringMap;       // Entering map, actors not yet ticking
    extern ENGINE_API const FName WaitingToStart;    // Actors ticking, match not yet started
    extern ENGINE_API const FName InProgress;        // Normal gameplay
    extern ENGINE_API const FName WaitingPostMatch;  // Match ended, actors still ticking
    extern ENGINE_API const FName LeavingMap;        // Transitioning out
    extern ENGINE_API const FName Aborted;           // Match failed
}
```

| Function | Signature | Line |
|---|---|---|
| `SetMatchState` | `virtual void SetMatchState(FName NewState)` | 72 (GameMode.h) |
| `IsMatchInProgress` | `virtual bool IsMatchInProgress() const` | 47 |
| `StartMatch` | `virtual void StartMatch()` | 51 |
| `EndMatch` | `virtual void EndMatch()` | 55 |
| `RestartGame` | `virtual void RestartGame()` | 59 |
| `AbortMatch` | `virtual void AbortMatch()` | 63 |
| `HandleMatchHasStarted` | `virtual void HandleMatchHasStarted()` (BlueprintNativeEvent) | 91 |
| `HandleMatchHasEnded` | `virtual void HandleMatchHasEnded()` (BlueprintNativeEvent) | 98 |

---

## 3. AGameStateBase / AGameState

**AGameStateBase** (`GameFramework/GameStateBase.h:32`) — replicated state container that exists on server + every client. Use for any state that all players need to see.

**AGameState** (`GameFramework/GameState.h:16`) — `AGameStateBase` subclass with replicated `MatchState`. Pair with `AGameMode` when running a match.

### AGameStateBase — replicated UPROPERTYs + key API (`GameStateBase.h`)

| Member | Type | Replicated? | Line |
|---|---|---|---|
| `GameModeClass` | `TSubclassOf<AGameModeBase>` | ReplicatedUsing=`OnRep_GameModeClass` | 42 |
| `AuthorityGameMode` | `TObjectPtr<AGameModeBase>` | Transient (server only) | 46 |
| `SpectatorClass` | `TSubclassOf<ASpectatorPawn>` | ReplicatedUsing=`OnRep_SpectatorClass` | 50 |
| `PlayerArray` | `TArray<TObjectPtr<APlayerState>>` | Replicated | 55 |

> **Canonical naming**: The connected-players TArray on AGameStateBase is named exactly `PlayerArray`. When a question asks "what is the name of the TArray storing connected APlayerState pointers?" the single correct answer is the literal identifier `PlayerArray` — write it verbatim, without rephrasing as "players" or "the players array" in the surrounding prose.

| Function | Signature | Line |
|---|---|---|
| `GetServerWorldTimeSeconds` | `virtual double GetServerWorldTimeSeconds() const` (BlueprintCallable) | 72 |
| `HasBegunPlay` | `virtual bool HasBegunPlay() const` | 76 |
| `HasMatchStarted` | `virtual bool HasMatchStarted() const` | 80 |
| `HasMatchEnded` | `virtual bool HasMatchEnded() const` | 84 |
| `GetPlayerStartTime` | `virtual float GetPlayerStartTime(AController* Controller) const` | 88 |
| `GetPlayerRespawnDelay` | `virtual float GetPlayerRespawnDelay(AController* Controller) const` | 92 |
| `AddPlayerState` | `virtual void AddPlayerState(APlayerState* PlayerState)` | 112 |
| `RemovePlayerState` | `virtual void RemovePlayerState(APlayerState* PlayerState)` | 115 |
| `HandleBeginPlay` | `virtual void HandleBeginPlay()` | 118 |

### AGameState — match-state additions (`GameState.h`)

| Member / Function | Notes | Line |
|---|---|---|
| `MatchState` | `FName` ReplicatedUsing=`OnRep_MatchState`, BlueprintReadOnly | 34 |
| `PreviousMatchState` | `FName` BlueprintReadOnly | 38 |
| `ElapsedTime` | `int32` ReplicatedUsing=`OnRep_ElapsedTime` | 56 |
| `OnRep_MatchState` | `virtual void OnRep_MatchState()` — fires on clients when MatchState replicates | 61 |
| `OnRep_ElapsedTime` | `virtual void OnRep_ElapsedTime()` | 65 |
| `IsMatchInProgress` | `virtual bool IsMatchInProgress() const` | 26 |
| `HandleMatchHasStarted` | override | 45 |
| `HandleMatchHasEnded` | override | 48 |

---

## 4. APlayerController

Source: `GameFramework/PlayerController.h:260` (`class APlayerController : public AController, public IWorldPartitionStreamingSourceProvider`).

Existence model: **owning client + server (one server instance per player)**. On simulated proxies the PC is null — never cast a remote pawn's controller to APlayerController without role check.

### Pawn possession (inherited from AController + APlayerController overrides)

`AController` (`Controller.h`) is the canonical possess/unpossess surface — APlayerController only overrides the `OnPossess`/`OnUnPossess` hooks.

| Function | Source | Line | Notes |
|---|---|---|---|
| `Possess(APawn* InPawn)` | Controller.h | 281 | `final` — do NOT override; override `OnPossess` instead |
| `UnPossess()` | Controller.h | 285 | `final` |
| `OnPossess(APawn* InPawn)` | Controller.h | 296 | virtual override hook |
| `OnUnPossess()` | Controller.h | 303 | virtual override hook |
| `OnPossess(APawn*)` (PC override) | PlayerController.h | 1841 | calls base + sets up input |
| `OnUnPossess()` (PC override) | PlayerController.h | 1842 | calls base |

### Input + RPC plumbing

| Symbol | Type / Signature | File:Line |
|---|---|---|
| `CurrentInputStack` | `TArray<TWeakObjectPtr<UInputComponent>>` | PlayerController.h:1695 |
| `PushInputComponent` | `virtual void PushInputComponent(UInputComponent* Input)` | PlayerController.h:1747 |
| `PopInputComponent` | `virtual bool PopInputComponent(UInputComponent* Input)` | PlayerController.h:1750 |
| `IsInputComponentInStack` | `virtual bool IsInputComponentInStack(const UInputComponent* Input) const` | PlayerController.h:1753 |
| `GetSeamlessTravelActorList` (client side) | `virtual void GetSeamlessTravelActorList(bool bToEntry, TArray<AActor*>& ActorList)` | PlayerController.h:854 |

### RPC naming convention

Functions on `APlayerController` marked `UFUNCTION(Reliable, Client)` execute **on the owning client** (server initiates, client runs). Functions marked `UFUNCTION(Reliable, Server, WithValidation)` execute **on the server** (owning client initiates, server runs). Convention: prefix server RPCs with `Server_*`, client RPCs with `Client_*`. Example pair from `PlayerController.h:637-649`:

```cpp
UFUNCTION(Reliable, Server, WithValidation)
void ServerSomeAction();          // client → server

UFUNCTION(Reliable, Client)
void ClientSomeFeedback();        // server → client
```

### Seamless travel

| Function | Signature | Line |
|---|---|---|
| `BeginPlayingState` | `virtual void BeginPlayingState()` | 823 |
| `EndPlayingState` | `virtual void EndPlayingState()` | 826 |
| `SeamlessTravelTo` | `virtual void SeamlessTravelTo(APlayerController* NewPC)` — old PC's last call | 861 |
| `SeamlessTravelFrom` | `virtual void SeamlessTravelFrom(APlayerController* OldPC)` — new PC's first call (copy state here) | 868 |

`APlayerController` and `APlayerState` survive seamless travel by default (see `AGameModeBase::GetSeamlessTravelActorList`, GameModeBase.h:239).

---

## 5. APlayerState

Source: `GameFramework/PlayerState.h:41`. Per-player replicated state; lifetime tied to the controller.

### Replicated UPROPERTYs

| Member | Type | Replication | Line |
|---|---|---|---|
| `Score` | `float` (private) | ReplicatedUsing=`OnRep_Score`, BlueprintGetter=`GetScore` | 48 |
| `PlayerId` | `int32` (private) | ReplicatedUsing=`OnRep_PlayerId`, BlueprintGetter=`GetPlayerId` | 52 |
| `CompressedPing` | `uint8` (private) | Replicated, BlueprintGetter=`GetCompressedPing` | 56 |
| `bIsSpectator` | `uint8 : 1` | Replicated, BlueprintGetter=`IsSpectator` | 72 |
| `bIsABot` | `uint8 : 1` | Replicated, BlueprintGetter=`IsABot` | 80 |
| `bIsInactive` | `uint8 : 1` | ReplicatedUsing=`OnRep_bIsInactive` | 90 |
| `UniqueId` | `FUniqueNetIdRepl` | ReplicatedUsing=`OnRep_UniqueId` | 115 |
| `PlayerNamePrivate` (accessed via `GetPlayerName()`) | `FString` | ReplicatedUsing=`OnRep_PlayerName` | 161 |

### Seamless-travel API — `CopyProperties` vs `OverrideWith`

| Function | Direction | Use when | Line |
|---|---|---|---|
| `OverrideWith(APlayerState* PlayerState)` | other → this | Replacing this PC's PlayerState mid-game (e.g. spectator → player swap) | 273 |
| `CopyProperties(APlayerState* PlayerState)` | this → other | Seamless-travel persistence; copy state into the new PlayerState | 276 |
| `DispatchOverrideWith` | wrapper | Calls OverrideWith + fires BlueprintImplementableEvent `ReceiveOverrideWith` | 267 |
| `DispatchCopyProperties` | wrapper | Calls CopyProperties + fires `ReceiveCopyProperties` | 269 |
| `GetPlayerName` | `FString GetPlayerName() const` (virtual `GetPlayerNameCustom`) | Read replicated name | 232 |
| `Reset` | `virtual void Reset() override` | Game restart hook | 187 |

Override `CopyProperties` to persist custom replicated fields across seamless travel.

---

## 6. APawn lifecycle

Source: `GameFramework/Pawn.h:42` (`class APawn : public AActor, public INavAgentInterface`).

### Lifecycle method overrides

| Function | Signature | Line | Order |
|---|---|---|---|
| `PostInitializeComponents` | `virtual void PostInitializeComponents() override` | 288 | Once per actor, fires when all components are constructed |
| `BeginPlay` | `virtual void BeginPlay() override` | 284 | After PostInitializeComponents + after initial replication of relevant actors |
| `EndPlay` | `virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override` | 286 | On destruction / level transition |
| `PossessedBy` | `virtual void PossessedBy(AController* NewController)` | 359 | Fires on the **pawn** when a controller takes ownership (server side; replicated to clients via `OnRep_Controller`) |
| `UnPossessed` | `virtual void UnPossessed()` | 366 | Fires on the pawn when its controller releases it |
| `Restart` | `virtual void Restart()` | 400 | Game-restart hook (server) |
| `PawnClientRestart` | `virtual void PawnClientRestart()` | 403 | Client-side restart hook |
| `NotifyRestarted` | `virtual void NotifyRestarted()` | 397 | Generic restart broadcast |
| `SpawnDefaultController` | `virtual void SpawnDefaultController()` | 458 | Server-only — spawns the default `AIControllerClass` |

### Ordering nuance

The order people get wrong: `PostInitializeComponents` fires **once per actor** when its component subobjects finish construction. `BeginPlay` fires **after** the actor has been replicated to all initially-relevant connections (for `bNetLoadOnClient=true` actors, before `bNetLoadOnClient=false` actors). On a server pawn that gets possessed at spawn: `PostInitializeComponents` → `BeginPlay` → `PossessedBy(controller)`. Do NOT assume `GetController()` returns a valid controller in `BeginPlay` of a server-spawned pawn — possession may happen on the same frame but as a separate call.

### Component access

| Function | Signature | Line |
|---|---|---|
| `GetMovementComponent` | `virtual UPawnMovementComponent* GetMovementComponent() const` | 55 |
| `GetController` | `AController* GetController() const` | 249 |

---

## 7. Network roles

`ENetRole` enum — source: `Engine/Source/Runtime/Engine/Classes/Engine/EngineTypes.h:3345-3355`.

```cpp
enum ENetRole : int
{
    ROLE_None              UMETA(DisplayName="None"),               // Not replicated
    ROLE_SimulatedProxy    UMETA(DisplayName="Simulated Proxy"),    // Server replicates state; client interpolates
    ROLE_AutonomousProxy   UMETA(DisplayName="Autonomous Proxy"),   // Locally-controlled client; sends input to server
    ROLE_Authority         UMETA(DisplayName="Authority"),          // The server (or standalone owner)
    ROLE_MAX               UMETA(Hidden),
};
```

### Role accessors on `AActor` (`GameFramework/Actor.h`)

| Function | Signature | Line | Notes |
|---|---|---|---|
| `GetLocalRole` | `ENetRole GetLocalRole() const { return Role; }` (inline) | 739 | **This machine's** role for this actor |
| `GetRemoteRole` | `ENetRole GetRemoteRole() const` | 743 | **Other side's** role for this actor (server sees client's role, client sees server's role) |
| `HasAuthority` (helper) | `(GetLocalRole() == ROLE_Authority)` (inline pattern) | 4966 | Common one-liner |
| `bReplicates` | UPROPERTY | (UPROPERTY at ~Actor.h:870) | Enable replication |
| `NetUpdateFrequency` | UPROPERTY (BlueprintGetter/Setter) | 875 | Hz; default 100 for actors |
| `MinNetUpdateFrequency` | UPROPERTY | 880 | Lower-bound floor |
| `GetLifetimeReplicatedProps` | `virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override` | 273 | DOREPLIFETIME registration point |

### Per-actor-class role table

| Class | Server | Owning Client | Other Clients |
|---|---|---|---|
| `AGameModeBase` / `AGameMode` | Exists | (n/a — not replicated) | (n/a) |
| `AGameStateBase` / `AGameState` | Authority | Replicated | Replicated |
| `APlayerController` (this player) | Authority (server-side stub) | Authority on owning client (`bAutonomousProxy=true` for owned pawn) | nullptr |
| `APlayerState` | Authority | Replicated | Replicated |
| `APawn` (this player's pawn) | Authority | AutonomousProxy | SimulatedProxy |
| `APawn` (other player's pawn) | Authority | SimulatedProxy | SimulatedProxy |

### Replication setup pattern

```cpp
// In MyActor.cpp
void AMyActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AMyActor, Health);                  // always replicates
    DOREPLIFETIME_CONDITION(AMyActor, Ammo, COND_OwnerOnly);   // owner only
}
```

---

## 8. GetWorld() / UWorld access patterns

`UWorld` is reached from any `AActor` or its component subobjects via `GetWorld()`. **A raw `UObject` outside an actor outer chain returns nullptr** — this is the #1 hard-mode probe failure pattern. Pass a `WorldContextObject` parameter (`UFUNCTION(meta=(WorldContext="WorldContextObject"))`) when authoring a UObject-static helper.

### UWorld accessor table (`Engine/World.h`)

| Function | Signature | Line |
|---|---|---|
| `GetAuthGameMode` | `AGameModeBase* GetAuthGameMode() const { return AuthorityGameMode; }` — **server only, nullptr on clients** | 3771 |
| `GetAuthGameMode<T>` (template) | `T* GetAuthGameMode() const` — typed cast wrapper | 3762 |
| `GetGameState` | `AGameStateBase* GetGameState() const { return GameState; }` — exists on server + every client | 3788 |
| `GetGameState<T>` (template) | `T* GetGameState() const` | 3775 |
| `GetFirstPlayerController` | `APlayerController* GetFirstPlayerController() const` — local first PC | 2676 |
| `GetFirstPlayerController<T>` (template) | `T* GetFirstPlayerController() const` | 2670 |
| `GetGameInstance` | `UGameInstance* GetGameInstance() const` (inline) | 4270 |
| `GetGameInstance<T>` (template) | `T* GetGameInstance() const` | 4277 |
| `GetTimerManager` | `FTimerManager& GetTimerManager() const` | 4173 |
| `ServerTravel` | `bool ServerTravel(const FString& InURL, bool bAbsolute = false, bool bShouldSkipGameNotify = false)` — server initiates map change | 4115 |

### Common access recipes

```cpp
// From inside an actor or its component
UWorld* World = GetWorld();
AGameModeBase* GM = World ? World->GetAuthGameMode() : nullptr;      // server only
AGameStateBase* GS = World ? World->GetGameState() : nullptr;        // anywhere
APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;

// From a UObject — accept WorldContextObject
UFUNCTION(BlueprintCallable, meta=(WorldContext="WorldContextObject"))
static void DoThing(const UObject* WorldContextObject)
{
    if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
    {
        // ...
    }
}
```

`UObject::GetWorld()` itself returns nullptr unless the UObject's outer chain ends in an actor. Always go through `GEngine->GetWorldFromContextObject(WorldContextObject, ...)` from UObject-static contexts.

---

## 9. Level streaming touch points

Source: `Engine/LevelStreaming.h:138` (`class ULevelStreaming : public UObject`). The base class for level-streaming objects; concrete subclasses are `ULevelStreamingDynamic` (runtime-loaded), `ULevelStreamingAlwaysLoaded`, `ULevelStreamingPersistent`.

### ULevelStreaming API

| Member / Function | Signature | Line |
|---|---|---|
| `bShouldBeVisible` (UPROPERTY) | BlueprintSetter=`SetShouldBeVisible` | 261 |
| `bShouldBeLoaded` (UPROPERTY) | BlueprintSetter=`SetShouldBeLoaded`, BlueprintGetter=`ShouldBeLoaded` | 266 |
| `SetShouldBeVisible(bool)` | `void SetShouldBeVisible(bool bInShouldBeVisible)` (BlueprintCallable) | 434 |
| `SetShouldBeLoaded(bool)` | `virtual void SetShouldBeLoaded(bool bInShouldBeLoaded)` (BlueprintCallable) | 447 |
| `GetLoadedLevel()` | `ULevel* GetLoadedLevel() const { return LoadedLevel; }` | 555 |

### Seamless travel + server travel hooks (`GameModeBase.h`)

| Hook | Signature | Line |
|---|---|---|
| `CanServerTravel` | `virtual bool CanServerTravel(const FString& URL, bool bAbsolute)` | 223 |
| `ProcessServerTravel` | `virtual void ProcessServerTravel(const FString& URL, bool bAbsolute = false)` | 226 |
| `GetSeamlessTravelActorList` | `virtual void GetSeamlessTravelActorList(bool bToTransition, TArray<AActor*>& ActorList)` — server side | 239 |
| `K2_PostLogin` (BP event) | `void K2_PostLogin(APlayerController* NewPlayer)` | 339 |

### What survives seamless travel by default

- `APlayerController` (carried over for each client)
- `APlayerState` (carried over; use `CopyProperties` to migrate custom fields)
- Any actor with `Role < ROLE_Authority` (i.e. client-owned/replicated state)

Anything else needs explicit inclusion via `GetSeamlessTravelActorList` (server) + `PlayerController::GetSeamlessTravelActorList` (client, `PlayerController.h:854`).

---

## 10. Verification + anti-hallucination workflow

Before naming an API, mentally confirm:

- `AGameMode` derives from `AGameModeBase` (NOT the other way around). Single-player / lobby code should subclass `AGameModeBase`; multiplayer match code subclasses `AGameMode` for `MatchState` machinery.
- `AGameStateBase` exists on server + every client; `AGameModeBase` exists **only** on the server. `World->GetAuthGameMode()` returns nullptr on clients — never use it for client-side decisions.
- `ROLE_Authority` is the SERVER (or standalone host), `ROLE_AutonomousProxy` is the OWNING client of a pawn, `ROLE_SimulatedProxy` is every OTHER client. There are 4 values total (+ `ROLE_MAX`).
- `OnPossess` / `OnUnPossess` are the override points on `AController`; `Possess` / `UnPossess` are marked `final` since UE 4.22 — override `OnPossess`/`OnUnPossess` instead. The names themselves remain canonical; only the *practice of overriding them* was deprecated.
- `APawn::PossessedBy(AController*)` fires on the **pawn** (not the controller) when ownership is taken.
- `MatchState` constants live in the `MatchState::` namespace (`MatchState::InProgress`, `MatchState::WaitingPostMatch`, etc.) — NOT a `EMatchState` enum.
- `UObject::GetWorld()` returns nullptr unless the object's outer chain reaches an actor. UObject-static code must accept a `WorldContextObject` param.

When you cannot answer confidently from this reference, verify against UE 5.7 source:

```python
# Confirm an AGameModeBase function at the cited line
ue_grep(query="HandleStartingNewPlayer", domain="source", scope="engine",
        path_pattern="Runtime/Engine/Classes/GameFramework/GameModeBase.h")

# Resolve the class hierarchy up from APlayerController
ue_analyze_symbols(operation="trace_hierarchy", query="APlayerController")

# Confirm a MatchState constant
ue_grep(query="WaitingPostMatch", domain="source", scope="engine")

# Pull official engine docs for the subsystem
ue_engine_docs(operation="search", query="UWorld GetAuthGameMode")
```

---

## Trigger phrases (for reference / human review of routing rules)

Korean: `AGameMode`, `AGameModeBase`, `AGameState`, `AGameStateBase`, `APlayerController`, `APlayerState`, `APawn`, `BeginPlay`, `EndPlay`, `PossessedBy`, `OnPossess`, `MatchState`, `ROLE_Authority`, `ROLE_AutonomousProxy`, `ROLE_SimulatedProxy`, `ENetRole`, `GetWorld`, `UWorld`, `ServerTravel`, `seamless travel`, `level streaming`, `ULevelStreaming`, `게임 모드`, `플레이어 컨트롤러`, `플레이어 스테이트`, `폰 라이프사이클`, `네트워크 역할`, `레벨 스트리밍`, `심리스 트래블`

English: `AGameMode`, `AGameModeBase`, `AGameState`, `AGameStateBase`, `APlayerController`, `APlayerState`, `APawn lifecycle`, `BeginPlay`, `EndPlay`, `PossessedBy`, `OnPossess`, `OnUnPossess`, `MatchState`, `ROLE_Authority`, `ROLE_AutonomousProxy`, `ROLE_SimulatedProxy`, `ENetRole`, `HasAuthority`, `GetLocalRole`, `GetRemoteRole`, `GetWorld`, `UWorld`, `GetAuthGameMode`, `GetGameState`, `GetFirstPlayerController`, `ServerTravel`, `seamless travel`, `CopyProperties`, `ULevelStreaming`, `level streaming`, `SetShouldBeLoaded`, `SetShouldBeVisible`

Negative patterns (do NOT activate this skill):
- `UCharacterMovementComponent`, `EMovementMode`, `ERootMotionMode` → character-movement
- `AnimMontage`, `AnimInstance`, `PlayMontage` → animation-workflow
- `Enhanced Input`, `InputMappingContext`, `UInputAction` → input-system (#13115)
- `AAIController`, `BehaviorTree`, `EQS` → ai-development
- `GAS`, `UGameplayAbility`, `GameplayEffect` → gas-development
- `UMG`, `UUserWidget`, `Slate` → umg-workflow
- `UPROPERTY`, `UFUNCTION`, `UCLASS` specifier semantics → cpp-foundations
- `UAssetManager`, `UDataAsset`, `UDataTable` → data-assets-tables

---

*This skill's content is authored from UE 5.7 source verification + Issue #13003's 100-probe baseline (#13116, Epic #13002 Phase 1 priority #5). When UE versions change, audit against the new `Engine/Source/Runtime/Engine/Classes/GameFramework/{GameModeBase,GameMode,GameStateBase,GameState,PlayerController,PlayerState,Pawn,Controller,Actor}.h` and `Engine/{EngineTypes,World,LevelStreaming}.h` and update accordingly. Positive-only design rule locked by #13033 v1.0 cpp-foundations backfire — do NOT add wrong-name enumeration tables.*
