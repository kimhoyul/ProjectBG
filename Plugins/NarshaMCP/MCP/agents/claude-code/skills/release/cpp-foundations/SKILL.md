---
name: cpp-foundations
model: opus
description: "Purpose: UE5 C++ language-level reference (UPROPERTY/UFUNCTION/UCLASS specifiers, reflection macros, delegate macros, container API, smart pointers, log categories). Authoritative positive-only tables — name THE specifier without contrasting against siblings. Triggers: 'UPROPERTY', 'UFUNCTION', 'UCLASS', 'GENERATED_BODY', 'DECLARE_DYNAMIC_MULTICAST_DELEGATE', 'TArray', 'TMap', 'TWeakObjectPtr', 'TObjectPtr', 'DECLARE_LOG_CATEGORY_EXTERN', 'BlueprintAssignable', 'EditDefaultsOnly', 'specifier', '리플렉션 매크로', '델리게이트 매크로'."
compatibility: "Requires UE 5.x C++ project. Reference tables verified against UE 5.7 Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h."
metadata:
  version: "1.1.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  origin_issue: "#13002 Phase 2"
  baseline_data: "#13003 100-probe baseline (cpp-foundations 15% partial rate driven by pedagogical-context — easy 40%)"
  redesign_note: "v1.1.0 removes all explicit wrong-name enumerations after v1.0.0 A/B showed easy 60%→0% but medium 10%→30% backfire — explicit ❌ lists became parroting prompts (Don't think of an elephant effect). v1.1.0 keeps only positive tables."
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# UE5 C++ Foundations Reference

**Priority**: High
**Goal**: Authoritative specifier/macro tables for UE5 reflection, delegates, containers, smart pointers. **Positive-only**: tables enumerate correct names with their effects, never list wrong-answer alternatives.

> **v1.1.0 design lesson**: v1.0.0 enumerated wrong-name siblings (e.g. `❌ Remove(Index)`) as anti-hallucination warnings. The 100-probe A/B (#13003 cpp-foundations) showed those `❌` lists BACKFIRED on medium/hard probes — Claude parroted them back, treating them as positive examples. v1.1.0 removes all anti-pattern enumerations and relies on the positive table being the only API surface the model sees.

---

## 1. UPROPERTY specifiers — visibility & editability

| Specifier | Editable in Class Defaults? | Editable on Instance? | Visible (read-only) where? |
|---|---|---|---|
| `EditAnywhere` | yes | yes | — |
| `EditDefaultsOnly` | yes | no | — |
| `EditInstanceOnly` | no | yes | — |
| `VisibleAnywhere` | no | no | Both (read-only) |
| `VisibleDefaultsOnly` | no | no | Class Defaults only (read-only) |
| `VisibleInstanceOnly` | no | no | Instance only (read-only) |

## 2. UPROPERTY specifiers — Blueprint exposure

| Specifier | Where it applies | What it does |
|---|---|---|
| `BlueprintReadOnly` | non-delegate property | Blueprint can read, cannot write |
| `BlueprintReadWrite` | non-delegate property | Blueprint can read and write |
| `BlueprintAssignable` | Dynamic Multicast Delegate property (declared via `DECLARE_DYNAMIC_MULTICAST_DELEGATE*`) | Blueprint binds handlers via Bind Event / Assign Event |
| `BlueprintGetter=Fn` | non-delegate property | Routes Blueprint reads through a UFUNCTION named `Fn` |
| `BlueprintSetter=Fn` | non-delegate property | Routes Blueprint writes through a UFUNCTION named `Fn` |

`BlueprintCallable` is a **UFUNCTION** specifier (see section 4), not a UPROPERTY specifier.

## 3. UPROPERTY specifiers — networking & serialization

| Specifier | Effect |
|---|---|
| `Replicated` | Property is replicated; add `DOREPLIFETIME(ClassName, PropertyName)` in `GetLifetimeReplicatedProps` |
| `ReplicatedUsing=OnRep_FunctionName` | Replicated + calls `OnRep_FunctionName()` on clients when the value changes |
| `NotReplicated` | Forces no-replication for a property inside a USTRUCT that is itself replicated |
| `Transient` | Excluded from serialization (save/load) and from duplication (PIE) |
| `SaveGame` | Included in USaveGame serialization via `UGameplayStatics::SaveGameToSlot` |

## 4. UFUNCTION specifiers

| Specifier | Effect |
|---|---|
| `BlueprintCallable` | Function callable from Blueprint (exec pins shown by default) |
| `BlueprintPure` | Callable + pure (no exec pins; usually requires `const`) |
| `BlueprintImplementableEvent` | Declared in C++, implemented in Blueprint only (no C++ body) |
| `BlueprintNativeEvent` | Declared in C++, has C++ default impl, can be overridden in Blueprint (requires `_Implementation` suffix in C++) |
| `CallInEditor` | Shows "Call In Editor" button on the details panel |
| `Exec` | Runtime console command |
| `Server` | Server RPC — call on client, executes on server |
| `Client` | Client RPC — call on server, executes on owning client |
| `NetMulticast` | Multicast RPC — call on server, executes on all clients |
| `Reliable` | RPC delivery is reliable (TCP-like) |
| `Unreliable` | RPC delivery is unreliable (UDP-like); default if not specified |
| `WithValidation` | Requires `_Validate` companion returning bool (Server RPCs in shipping builds require this) |

RPC pattern example:
```cpp
UFUNCTION(Server, Reliable, WithValidation)
void ServerFireWeapon(FVector Target);

void ServerFireWeapon_Implementation(FVector Target) { /* server logic */ }
bool ServerFireWeapon_Validate(FVector Target) { return Target.SizeSquared() < 1e10; }
```

## 5. UCLASS specifiers

| Specifier | Effect |
|---|---|
| `Blueprintable` | Can be subclassed in Blueprint |
| `BlueprintType` | Can be used as a Blueprint variable type |
| `Abstract` | Cannot be instantiated directly; subclasses can |
| `NotPlaceable` | Cannot be placed in level via editor |
| `NotBlueprintable` | Explicitly disallow Blueprint subclassing |
| `MinimalAPI` | Export only the class itself, not all members (reduces DLL bloat) |
| `Config=Game` | UPROPERTYs with `config` specifier persist to `DefaultGame.ini` |
| `DefaultConfig` | Config writes go to `Default<Name>.ini` instead of `<Project>.ini` |
| `EditInlineNew` | Allow inline instance creation in details panel (for inner UObjects) |

## 6. Reflection macros — class body

| Macro | Where it goes |
|---|---|
| `GENERATED_BODY()` | First line inside the class body (after the opening brace). This is the modern form for both UCLASS and USTRUCT. |

## 7. Delegate macros — arity table

Naming pattern: `DECLARE_[DYNAMIC_]<KIND>_DELEGATE[_<NParams>][_RetVal][_<NParams>]`

| Kind | Static (no UFUNCTION binding) | Dynamic (Blueprint-bindable) |
|---|---|---|
| Single, no return | `DECLARE_DELEGATE` | `DECLARE_DYNAMIC_DELEGATE` |
| Single, with return | `DECLARE_DELEGATE_RetVal` | `DECLARE_DYNAMIC_DELEGATE_RetVal` |
| Multicast, no return | `DECLARE_MULTICAST_DELEGATE` | `DECLARE_DYNAMIC_MULTICAST_DELEGATE` |

Multicast delegates cannot have return values.

Arity suffix: append `_OneParam`, `_TwoParams`, `_ThreeParams`, ..., `_NineParams` to the base.

Examples:
- 2 params, dynamic, multicast: `DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, AActor*, Instigator, float, NewHealth)`
- 1 param + return, static, single: `DECLARE_DELEGATE_RetVal_OneParam(bool, FOnPredicate, AActor*)`
- 3 params, dynamic, single: `DECLARE_DYNAMIC_DELEGATE_ThreeParams(FOnHit, AActor*, A, AActor*, B, FHitResult, Hit)`

## 8. Container API — TArray / TMap / TSet

### TArray

| Operation | Method |
|---|---|
| Append one element | `Add(Item)` |
| Append another array | `Append(OtherArray)` |
| Insert at index | `Insert(Item, Index)` |
| Remove by index | `RemoveAt(Index)` |
| Remove all entries matching a value | `Remove(Item)` |
| Remove first entry matching a value | `RemoveSingle(Item)` |
| Remove matching a predicate | `RemoveAll([](auto& x){ return ...; })` |
| Find an element | `Find(Item)` — returns `INDEX_NONE` if missing |
| Find by predicate | `IndexOfByPredicate([](auto& x){ ... })` |
| Contains check | `Contains(Item)` |
| Number of elements | `Num()` |
| Clear (free capacity) | `Empty()` |
| Clear (keep capacity) | `Reset()` |

### TMap

| Operation | Method |
|---|---|
| Insert | `Add(Key, Value)` |
| Lookup, returns pointer | `Find(Key)` |
| Lookup, returns reference | `FindChecked(Key)` |
| Lookup or insert default | `FindOrAdd(Key)` |
| Remove | `Remove(Key)` |
| Contains check | `Contains(Key)` |
| Number of entries | `Num()` |
| Range-based for | `for (TPair<KeyType, ValueType>& Pair : MyMap)` |

### TSet

Same `Add` / `Remove` / `Contains` / `Num` semantics as TArray, but unordered and unique.

## 9. Smart pointer family

| Type | Holds | Use for |
|---|---|---|
| `TObjectPtr<T>` | `UObject*` | UE5 modern replacement for raw `T*` member pointers in UPROPERTY |
| `TWeakObjectPtr<T>` | `UObject*` (may be null) | Weak reference that survives the UObject's destruction (returns null on access after destroy) |
| `TSoftObjectPtr<T>` | Asset path | Path-only reference to an asset; does not load it |
| `TSoftClassPtr<T>` | Asset path to a `UClass` | Path-only reference to a class asset; resolves to `UClass*` |
| `TSharedPtr<T>` | Non-UObject `T*` | RAII shared ownership for plain C++ types |
| `TUniquePtr<T>` | Non-UObject `T*` | RAII sole ownership for plain C++ types |
| `TSharedRef<T>` | Non-UObject `T*` | RAII shared ownership; cannot be null |

For UObject member fields in a UPROPERTY, use `TObjectPtr<T>` (UE5) or raw `T*` (UE4-compat — auto-coerced).

## 10. Log categories

```cpp
// Header file (.h):
DECLARE_LOG_CATEGORY_EXTERN(LogMyGame, Log, All);

// Source file (.cpp):
DEFINE_LOG_CATEGORY(LogMyGame);

// Usage:
UE_LOG(LogMyGame, Warning, TEXT("Value: %d, Name: %s"), Value, *Name.ToString());
```

The `_EXTERN` declaration goes in the header, the definition goes in the cpp.

## 11. FString / FName / FText distinctions

| Type | Encoding | Mutable? | Use for | Convert to FString |
|---|---|---|---|---|
| `FString` | UTF-16 (TCHAR) | yes | Free-form text manipulation | `*Str` for `const TCHAR*`; `Str.Len()` for length |
| `FName` | Case-insensitive interned string | no (immutable, hash-based) | Asset names, identifiers, hash-keys | `Name.ToString()` |
| `FText` | Localizable text | no (immutable, may be a localization key) | User-visible UI text | `Text.ToString()` |

`FindRow<T>(FName Key, ...)` and similar lookup APIs take an `FName`, not an `FString`.

---

## When to load this skill

The router auto-loads this skill when the user mentions: `UPROPERTY`, `UFUNCTION`, `UCLASS`, `GENERATED_BODY`, `DECLARE_DYNAMIC_*_DELEGATE`, `TArray`, `TMap`, `TWeakObjectPtr`, `TObjectPtr`, `TSoftObjectPtr`, `DECLARE_LOG_CATEGORY_EXTERN`, `BlueprintAssignable`, `EditDefaultsOnly`, `EditAnywhere`, `VisibleAnywhere`, `specifier`, `리플렉션 매크로`, `델리게이트 매크로`, and similar identifiers.

For subsystem-specific questions, a dedicated skill takes precedence:
- Asset management (`UAssetManager`, `UDataAsset`) → `data-assets-tables` skill
- GAS (`UGameplayAbility`, `UAbilitySystemComponent`) → `gas-development` skill
- Materials (`UMaterial`) → `material-analysis` skill

## Verification

If you are uncertain about a specifier or macro name, verify against
`Engine/Source/Runtime/CoreUObject/Public/UObject/ObjectMacros.h`:

```python
# Confirm a UPROPERTY/UFUNCTION specifier exists in the canonical macro header
ue_grep(query="EditDefaultsOnly", domain="source", scope="engine", path_pattern="Runtime/CoreUObject/Public/UObject/ObjectMacros.h")

# Verify a delegate-macro arity name is real (e.g. distinguish _TwoParams vs _2Param)
ue_grep(query="DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams", domain="source", scope="engine")

# Resolve a smart-pointer / container class hierarchy
ue_analyze_symbols(operation="trace_hierarchy", query="TObjectPtr")

# Pull official engine docs for a specifier family (e.g. UPROPERTY, RPC specifiers)
ue_engine_docs(operation="search", query="UPROPERTY specifiers BlueprintAssignable")
```

---

*v1.1.0 redesign — positive tables only. v1.0.0 included explicit wrong-name enumerations which were measured to backfire (Don't think of an elephant). Pre-merge A/B per #13003 will validate whether v1.1.0 recovers medium/hard probes while preserving the easy-probe gain.*
