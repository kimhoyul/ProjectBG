# BP-to-C++ Node Mapping Reference

## Node Mapping Table (v1.0 — 30 nodes)

### Flow Control

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Branch | `if (Condition) { ... } else { ... }` | LOW | Direct mapping |
| Sequence | Sequential statements | LOW | Nodes execute in order |
| ForEachLoop | `for (auto& Item : Array) { ... }` | LOW | Range-based for |
| ForLoop | `for (int32 i = First; i <= Last; ++i) { ... }` | LOW | Standard for loop |
| WhileLoop | `while (Condition) { ... }` | LOW | Direct mapping |
| FlipFlop | `bFlipFlopState = !bFlipFlopState; if (bFlipFlopState) { A } else { B }` | LOW | Requires bool member |
| Gate | `if (bGateOpen) { ... }` | LOW | Requires bool member |
| DoOnce | `if (!bHasDoneOnce) { bHasDoneOnce = true; ... }` | LOW | Requires bool member |
| Select | Ternary or switch | LOW | Depends on type |
| Switch on Int/String/Enum | `switch (Value) { case X: ... }` | LOW | Direct mapping |

### Events

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Event BeginPlay | `virtual void BeginPlay() override` | LOW | |
| Event Tick | `virtual void Tick(float DeltaTime) override` | LOW | |
| Event EndPlay | `virtual void EndPlay(const EEndPlayReason::Type Reason) override` | LOW | |
| Custom Event | `UFUNCTION() void EventName()` | LOW | |
| Event Dispatcher | `DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEventName)` | MEDIUM | Delegate + broadcast |
| Input Action (Enhanced) | `void OnInputAction(const FInputActionValue& Value)` | MEDIUM | Needs Enhanced Input setup |
| OnComponentBeginOverlap | `void OnOverlap(UPrimitiveComponent*, AActor*, ...)` | MEDIUM | Delegate binding in constructor |

### Variables & Access

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Get Variable | `this->VarName` | LOW | |
| Set Variable | `this->VarName = Value` | LOW | |
| Get (other actor) | `OtherActor->GetPropertyName()` or `Cast<T>(OtherActor)->VarName` | LOW | May need cast |
| Cast To | `Cast<T>(Object)` | LOW | Returns nullptr on fail |
| IsValid | `IsValid(Object)` or `Object != nullptr` | LOW | |

### Actor Operations

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Spawn Actor | `GetWorld()->SpawnActor<T>(Class, Transform)` | MEDIUM | Template parameter |
| Destroy Actor | `Destroy()` or `Actor->Destroy()` | LOW | |
| Set Actor Location | `SetActorLocation(FVector)` | LOW | |
| Set Actor Rotation | `SetActorRotation(FRotator)` | LOW | |
| Get Actor Location | `GetActorLocation()` | LOW | |
| Add Actor Component | `NewObject<T>(this); AddOwnedComponent(Comp)` | MEDIUM | Runtime vs constructor differs |

### Component Operations

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Get Component by Class | `FindComponentByClass<T>()` | LOW | Template |
| Add Component (Construction) | `CreateDefaultSubobject<T>(TEXT("Name"))` | MEDIUM | Constructor only |
| Set Component property | `Component->SetProperty(Value)` | LOW | |

### Timers

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Set Timer by Event | `GetWorldTimerManager().SetTimer(Handle, this, &AClass::Func, Rate, bLoop)` | MEDIUM | Needs FTimerHandle member |
| Set Timer by Function Name | `GetWorldTimerManager().SetTimer(Handle, this, &AClass::Func, Rate, bLoop)` | MEDIUM | Prefer delegate version |
| Clear Timer | `GetWorldTimerManager().ClearTimer(Handle)` | LOW | |
| Clear All Timers | `GetWorldTimerManager().ClearAllTimersForObject(this)` | LOW | |

### Animation

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Play Montage | `PlayAnimMontage(Montage, Rate, Section)` | MEDIUM | Needs UAnimMontage* UPROPERTY |
| Stop Montage | `StopAnimMontage(Montage)` | LOW | |
| Get Anim Instance | `GetMesh()->GetAnimInstance()` | LOW | |

### Gameplay

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Apply Damage | `UGameplayStatics::ApplyDamage(Target, Damage, Controller, Causer, DamageType)` | MEDIUM | 5 params |
| Line Trace | `UKismetSystemLibrary::LineTraceSingle(World, Start, End, ...)` | MEDIUM | Many params |
| Print String | `UE_LOG(LogTemp, Warning, TEXT("%s"), *Message)` | LOW | Or GEngine->AddOnScreenDebugMessage |
| Get Player Controller | `UGameplayStatics::GetPlayerController(this, 0)` | LOW | |
| Get Game Mode | `UGameplayStatics::GetGameMode(this)` | LOW | Cast may be needed |

### GAS (Gameplay Ability System)

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Activate Ability | `AbilitySystemComponent->TryActivateAbility(Handle)` | MEDIUM | Needs ASC reference |
| Apply Gameplay Effect | `AbilitySystemComponent->ApplyGameplayEffectToSelf(GE, Level, Context)` | MEDIUM | |
| Remove Active GE by Source | `AbilitySystemComponent->RemoveActiveGameplayEffect(Handle)` | MEDIUM | Or `RemoveActiveEffectsWithSourceEffect()` for class-based removal |
| Get Float Attribute | `AbilitySystemComponent->GetNumericAttribute(FGameplayAttribute(...))` | LOW | Use `UAbilitySystemBlueprintLibrary::GetFloatAttribute()` shorthand |
| Has Matching Tag | `AbilitySystemComponent->HasMatchingGameplayTag(Tag)` | LOW | |
| Send Gameplay Event | `UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Actor, Tag, Payload)` | MEDIUM | |

### Utility & Type Conversion

| BP Node | C++ Code | Complexity | Notes |
|---|---|---|---|
| Build String | `FString::Printf(TEXT("%s%s"), *A, *B)` | LOW | String concatenation |
| Build String_Double | `FString::Printf(TEXT("%.2f"), Value)` | LOW | Float to string |
| Conv_String To Text | `FText::FromString(String)` | LOW | FString → FText |
| Make Literal Text | `FText::FromString(TEXT("..."))` | LOW | Direct FText construction |
| Create Dynamic Material Instance | `UMaterialInstanceDynamic::Create(ParentMat, Outer)` | MEDIUM | Runtime material instance |
| Equal Equal_Class Class | `ClassA == ClassB` | LOW | TSubclassOf comparison |
| Set Text (TextRender) | `TextComponent->SetText(FText)` | LOW | UTextRenderComponent specific |
| Set World Size (TextRender) | `TextComponent->SetWorldSize(float)` | LOW | UTextRenderComponent specific |

---

## Unsupported Nodes (v1.0)

| Node | Reason | Suggested C++ Pattern |
|---|---|---|
| **Timeline** | Complex: FTimeline + FOnTimelineFloat/Vector + UCurveFloat assets | Generate FTimeline member + TickTimeline() in Tick. Manual: bind curves, set delegates |
| **Delay** | Latent action, requires async | `FTimerHandle` + `GetWorldTimerManager().SetTimer(Handle, Lambda, Delay, false)` |
| **AI MoveTo** | Latent, navigation-dependent | `UAIBlueprintHelperLibrary::SimpleMoveToLocation(Controller, Target)` or `MoveToLocation()` on AIController |
| **Async Load** | Latent, streaming-dependent | `FStreamableManager::RequestAsyncLoad()` with delegate |
| **Macro Nodes** | Must be inlined first | Expand macro, then map individual nodes |
| **Widget Create** | UMG construction differs in C++ | `CreateWidget<T>(GetWorld(), WidgetClass)` — but full UMG migration is separate |

---

## Class Name Convention

| BP Pattern | C++ Pattern | Example |
|---|---|---|
| `BP_<Name>` (Actor) | `A<Name>` | `BP_PlayerCharacter` → `APlayerCharacter` |
| `BP_<Name>` (Component) | `U<Name>` | `BP_HealthComponent` → `UHealthComponent` |
| `BP_<Name>` (GameMode) | `A<Name>` | `BP_MyGameMode` → `AMyGameMode` |
| `BP_<Name>` (PlayerController) | `A<Name>` | `BP_MyPlayerController` → `AMyPlayerController` |
| `BP_<Name>` (HUD) | `A<Name>` | `BP_MyHUD` → `AMyHUD` |
| `BP_<Name>` (Pawn) | `A<Name>` | `BP_MyPawn` → `AMyPawn` |

**Conflict resolution**: If the generated name conflicts with an engine class (e.g., `AGameMode`), append project prefix: `AMyProject_GameMode` or `AGameMode_Custom`.

---

## Variable Type Mapping

| BP Type | C++ Type | UPROPERTY Specifiers |
|---|---|---|
| Boolean | `bool` | |
| Integer | `int32` | |
| Float | `float` | |
| String | `FString` | |
| Name | `FName` | |
| Text | `FText` | |
| Vector | `FVector` | |
| Rotator | `FRotator` | |
| Transform | `FTransform` | |
| Actor Reference | `AActor*` (or specific type) | |
| Class Reference | `TSubclassOf<AActor>` | |
| Soft Object Reference | `TSoftObjectPtr<UObject>` | |
| Array | `TArray<T>` | |
| Set | `TSet<T>` | |
| Map | `TMap<K,V>` | |
| Struct | `F<StructName>` | |
| Enum | `E<EnumName>` | |

### UPROPERTY Specifier Mapping

| BP Property Flag | UPROPERTY Specifier |
|---|---|
| Editable (Instance) | `EditAnywhere` |
| Editable (Default) | `EditDefaultsOnly` |
| Read Only | `VisibleAnywhere` or `BlueprintReadOnly` |
| Exposed on Spawn | `ExposeOnSpawn` |
| Replicated | `Replicated` (+ add to `GetLifetimeReplicatedProps`) |
| Net Multicast | `ReplicatedUsing=OnRep_VarName` |
| Transient | `Transient` |
| SaveGame | `SaveGame` |
| Category | `Category = "CategoryName"` |
