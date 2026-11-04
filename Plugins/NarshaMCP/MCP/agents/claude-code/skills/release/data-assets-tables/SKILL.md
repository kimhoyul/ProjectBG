---
name: data-assets-tables
model: opus
description: "Purpose: UE5 data-driven asset reference (UDataAsset / UDataTable / UPrimaryDataAsset / UAssetManager / FStreamableManager). Authoritative API tables + config conventions + sibling-API disambiguation. Built from Phase 1 hallucination probe baseline (#13003) which identified hard-probe knowledge gaps in AssetManager-internals. Triggers: 'UAssetManager', 'PrimaryDataAsset', 'DataTable', 'DataAsset', 'AssetBundles', 'StreamableManager', 'TSoftObjectPtr', 'TSoftClassPtr', 'FPrimaryAssetId', 'FPrimaryAssetType', 'AssetManagerSettings', 'asset bundle', '에셋 매니저', '데이터 에셋', '데이터 테이블'."
compatibility: "Requires UE 5.x C++ project. Authoritative tables verified against UE 5.7/5.8 Engine source."
metadata:
  version: "1.0.0"
  author: "Next-Stage-Inc"
  license: "MIT"
  origin_issue: "#13002 Phase 2"
  baseline_data: "#13003 100-probe baseline (data-assets-tables hard 60% rate before this skill)"
audience: end-user  # Issue #14105: ships in end-user bundle (shipped-closure root)
---

# Data Assets, Tables, and the Asset Manager (UE5)

**Priority**: High
**Goal**: Provide authoritative reference tables so Claude emits correct UE5 API names + config conventions for data-driven asset workflows.

> **Why this skill exists**: Issue #13003's 100-probe baseline measured 60% hallucination rate on `data-assets-tables` hard probes (AssetManager internals: config sections, diagnostic functions, bundle conventions). This skill provides preloaded authoritative references that should reduce that rate.

---

## When to load this skill

The router auto-loads when the user mentions any of: `UDataAsset`, `UDataTable`, `UPrimaryDataAsset`, `UAssetManager`, `FStreamableManager`, `TSoftObjectPtr`, `TSoftClassPtr`, `FPrimaryAssetId`, `FPrimaryAssetType`, `AssetManagerSettings`, asset bundles, or Korean equivalents (`에셋 매니저`, `데이터 에셋`, `데이터 테이블`).

**When NOT to load**: questions about uassets in general (`ue_search_assets`), Blueprint asset structure (`ue_manage_blueprint`), Material assets (`ue_manage_material`), or asset references in shipping packaging (those have their own surface). Table **health audits** ("데이터 테이블 감사", batch validate) and the **MCP table read/write workflow** (`ue_modify_asset` `rows.`/`curves.` syntax) → [`/ue-data-audit`](../ue-data-audit/SKILL.md) (Issue #20623) — this skill is the static C++ API reference, that one is the tool-orchestration workflow.

---

## 1. Core class hierarchy (verbatim from UE 5.7/5.8 source)

```text
UObject
├── UDataAsset                     (Engine/Public/Engine/DataAsset.h)
│   ├── UPrimaryDataAsset          (Engine/Public/Engine/DataAsset.h)
│   └── UCurveLinearColorAtlas, USoundConcurrency, etc. (engine subclasses)
└── UDataTable                     (Engine/Public/Engine/DataTable.h)
    └── UCompositeDataTable        (Engine/Public/Engine/CompositeDataTable.h)

USTRUCT bases:
├── FTableRowBase                  (Engine/Public/Engine/DataTable.h)  ← ALL DataTable row structs MUST inherit
└── (your row struct here)
```

**Key gotcha**: `UDataAsset` is the simpler base; `UPrimaryDataAsset` is what AssetManager uses for streaming/bundles. If you need bundles, you MUST use `UPrimaryDataAsset` (not bare `UDataAsset`).

---

## 2. Soft pointer family — semantic distinctions (CRITICAL — common confusion zone)

| Type | What it stores | What it does NOT do |
|---|---|---|
| `TSoftObjectPtr<T>` | A path to an asset **instance** | Does NOT keep the asset loaded; does NOT prevent GC |
| `TSoftClassPtr<T>` | A path to a **UClass** (asset of class type) | Does NOT keep the class loaded; does NOT instantiate |
| `TWeakObjectPtr<T>` | A weak ref to a **live UObject** | Does NOT prevent GC; goes null when the object dies |
| `TObjectPtr<T>` | UE5-aware replacement for raw `T*` member pointers | Auto-handles GC anchoring when in a UPROPERTY |

**Common hallucinations to avoid**:
- ❌ "TWeakObjectPtr is for asset paths" — wrong, it's for live objects
- ❌ "TSoftObjectPtr keeps the asset loaded" — wrong, it's just a path
- ❌ "TSoftClassPtr<T> resolves to T*" — wrong, it resolves to `UClass*` (the class, not an instance)
- ✅ For an asset-path-to-an-instance: `TSoftObjectPtr<UMyAsset> AssetRef;`
- ✅ For an asset-path-to-a-class: `TSoftClassPtr<UMyClass> ClassRef;` (resolves to `UClass*`)
- ✅ For a runtime weak ref to a living UObject: `TWeakObjectPtr<AActor> WeakActor;`

---

## 3. UAssetManager — singleton + key APIs

### Singleton access
```cpp
UAssetManager& Manager = UAssetManager::Get();  // canonical accessor
// NOT: UAssetManager::GetInstance(), GetAssetManager(), FAssetManager::Get()
```

### Primary asset identification
```cpp
FPrimaryAssetType TypeKey = FPrimaryAssetType(TEXT("Weapon"));   // category
FPrimaryAssetId  AssetId = FPrimaryAssetId(TypeKey, FName("Sword"));  // {type, name}
```

| Struct | Purpose |
|---|---|
| `FPrimaryAssetType` | Category like "Weapon", "Map", "Item" |
| `FPrimaryAssetId` | `{type, name}` — uniquely identifies one primary asset |

**Don't confuse**: `FPrimaryAssetType` (category) vs `FPrimaryAssetId` (specific asset). They are NOT interchangeable; the registry uses both as separate keys.

### Loading / streaming
```cpp
// Async load via the bundle system (recommended for primary assets)
TArray<FName> Bundles = { FName("UI"), FName("Gameplay") };
TArray<FPrimaryAssetId> Assets = { AssetId };
FStreamableDelegate OnDone = FStreamableDelegate::CreateLambda([](){ /* ... */ });
TSharedPtr<FStreamableHandle> Handle = Manager.ChangeBundleStateForPrimaryAssets(
    Assets, Bundles, /*RemoveBundles=*/{}, /*bRemoveAllBundles=*/false,
    FStreamableManagerDownloadPriority::Game, OnDone);

// Minimal load (no bundle state change — just access the asset itself)
TSharedPtr<FStreamableHandle> Handle2 = Manager.LoadPrimaryAssets(
    Assets, /*LoadBundles=*/{}, OnDone);

// Underlying streamable manager (for non-primary-asset soft refs)
FStreamableManager& Streamer = Manager.GetStreamableManager();
TSharedPtr<FStreamableHandle> Handle3 = Streamer.RequestAsyncLoad(
    SomeSoftObjectPath, OnDone);
```

### Diagnostic dump functions (5 of them — common confusion)

| Function | What it dumps |
|---|---|
| `UAssetManager::DumpAssetTypeSummary` | Per-PrimaryAssetType counts + paths |
| `UAssetManager::DumpLoadedAssetState` | Currently loaded asset states |
| `UAssetManager::DumpBundlesForAsset` | Bundle membership of a specific asset |
| `UAssetManager::DumpAssetRegistryInfo` | AssetRegistry view of registered primary assets |
| `UAssetManager::DumpReferencersForPackage` | Reverse-reference list for a package |

All are typically exposed via console commands like `AssetManager.DumpTypeSummary`. **None of these are named `DumpPrimaryAssets`, `LogPrimaryAssets`, `PrintAssetRegistry`, or `ShowPrimaryAssets`** (those are common hallucinations).

---

## 4. UPrimaryDataAsset virtuals (you'll override these)

```cpp
class UMyPrimaryAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    // Required: tell AssetManager what FPrimaryAssetId this asset has
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;

    // Required if you have soft refs in bundles: enumerate bundle membership
    virtual void UpdateAssetBundleData() override;

    // Often overridden: declare bundles you want pre-loaded together
    // (called by UpdateAssetBundleData implementation in AssetManager)

    UPROPERTY(EditAnywhere, meta=(AssetBundles="UI"))
    TSoftObjectPtr<UTexture2D> Icon;

    UPROPERTY(EditAnywhere, meta=(AssetBundles="Gameplay"))
    TSoftObjectPtr<UStaticMesh> Mesh;
};
```

**The `meta=(AssetBundles="...")` UPROPERTY meta-tag is critical** — it tells AssetManager which TSoftObjectPtr/TSoftClassPtr fields belong to which bundle. Common hallucinations: `meta=(Bundles=...)`, `meta=(BundleMeta=...)`, `meta=(BundleName=...)`, `meta=(AssetBundle=...)`  ❌  — none of those exist. **Only `AssetBundles` (plural) works.**

---

## 5. Config: registering AssetManager scan rules

In `Config/DefaultGame.ini`:

```ini
[/Script/Engine.AssetManagerSettings]
+PrimaryAssetTypesToScan=(PrimaryAssetType="Weapon",AssetBaseClass=/Script/MyGame.WeaponAsset,bHasBlueprintClasses=false,bIsEditorOnly=false,Directories=((Path="/Game/Weapons")),SpecificAssets=,Rules=(Priority=-1,bApplyRecursively=true,ChunkId=-1,CookRule=Unknown))

+PrimaryAssetTypesToScan=(PrimaryAssetType="Map",AssetBaseClass=/Script/Engine.World,bHasBlueprintClasses=false,bIsEditorOnly=false,Directories=((Path="/Game/Maps")),Rules=(...))
```

**Section header MUST be `[/Script/Engine.AssetManagerSettings]`** — derived from `UAssetManagerSettings.h`'s `UCLASS(config = Game, defaultconfig, ...)`. Common hallucinations: `[AssetManager]`, `[AssetManagerSettings]` (no /Script prefix), `[/Script/Engine.AssetManager]` (missing Settings suffix). **None of those work.**

---

## 6. UDataTable — row lookup patterns

```cpp
USTRUCT(BlueprintType)
struct FMyRow : public FTableRowBase  // ← MUST inherit FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere) int32 Damage = 10;
    UPROPERTY(EditAnywhere) FName DisplayName;
};

// Lookup by row name (returns pointer to struct or nullptr)
const FMyRow* Row = DataTable->FindRow<FMyRow>(FName("Sword"), TEXT("Context"));
// NOT: LookupRow, GetRow, QueryRow, FetchRow — those don't exist on UDataTable

// FDataTableRowHandle for designer-exposed UPROPERTY
USTRUCT()
struct FMyConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, meta=(RowType="MyRow"))
    FDataTableRowHandle WeaponRow;  // ← designer can pick the table + row in details panel
};

// Resolving a row handle at runtime
FMyRow* Resolved = WeaponRow.GetRow<FMyRow>(TEXT("WeaponRow.GetRow"));
```

---

## 7. FStreamableManager — handle lifecycle

```cpp
// Request load
TSharedPtr<FStreamableHandle> Handle = StreamableManager.RequestAsyncLoad(
    SoftObjectPath, OnLoadedDelegate);

// Cancel before completion (rare)
Handle->CancelHandle();

// Release (decrement refcount; asset stays loaded if other handles still hold it)
Handle->ReleaseHandle();

// Forcibly unload (only when no other handles reference it)
StreamableManager.Unload(SoftObjectPath);
```

**Common hallucination**: `StreamableManager.ReleaseAsset(...)`, `StreamableManager.DestroyAsset(...)`, `StreamableManager.FreeAsset(...)` ❌ — none exist. The canonical API is **`Unload`** on the manager + **`ReleaseHandle`** / **`CancelHandle`** on the handle.

---

## 8. Common task → API recipes

### "Find all assets of a primary type"
```cpp
TArray<FAssetData> Assets;
UAssetManager::Get().GetPrimaryAssetDataList(FPrimaryAssetType("Weapon"), Assets);
```

### "Synchronously get an already-loaded primary asset"
```cpp
UMyPrimaryAsset* Asset = Cast<UMyPrimaryAsset>(
    UAssetManager::Get().GetPrimaryAssetObject(AssetId));
// nullptr if not loaded — call LoadPrimaryAssets first
```

### "Asynchronously load and call back when ready"
```cpp
TArray<FPrimaryAssetId> ToLoad = { AssetId };
FStreamableDelegate Cb = FStreamableDelegate::CreateUObject(this, &AMyActor::OnAssetReady);
UAssetManager::Get().LoadPrimaryAssets(ToLoad, /*LoadBundles=*/{}, Cb);
```

### "Query asset metadata without loading"
```cpp
FAssetData Data;
UAssetManager::Get().GetPrimaryAssetData(AssetId, Data);
// Inspect Data.AssetClassPath, Data.PackagePath, etc. — no full load.
```

---

## 9. Authoritative source locations (UE 5.7/5.8)

If you're uncertain about an API name, check these files:

| File | What's there |
|---|---|
| `Engine/Source/Runtime/Engine/Classes/Engine/AssetManager.h` | All `UAssetManager` public API |
| `Engine/Source/Runtime/Engine/Classes/Engine/AssetManagerSettings.h` | Config-driven scan rules (UCLASS decl shows the config section name) |
| `Engine/Source/Runtime/Engine/Classes/Engine/DataAsset.h` | `UDataAsset` + `UPrimaryDataAsset` |
| `Engine/Source/Runtime/Engine/Classes/Engine/DataTable.h` | `UDataTable` + `FTableRowBase` + `FDataTableRowHandle` |
| `Engine/Source/Runtime/Engine/Private/AssetManager.cpp` | Where the 5 `Dump*` functions are implemented (search `void UAssetManager::Dump`) |
| `Engine/Source/Runtime/Engine/Public/Engine/StreamableManager.h` | `FStreamableManager` + `FStreamableHandle` |

**Verification commands** (from a NarshaMCP-equipped session):
```python
# Locate the dump function in engine source (authoritative source line)
ue_grep(query="DumpAssetRegistryInfo", domain="source", scope="engine")

# Verify a UAssetManager method name exists (avoid hallucinated APIs like DumpPrimaryAssets)
ue_analyze_symbols(operation="search_symbols", query="UAssetManager")

# Find concrete UPrimaryDataAsset subclasses in the project (real bundle-meta usage)
ue_search_assets(asset_type="DataAsset")

# Pull the official engine docs for AssetManager API + AssetBundles meta-tag semantics
ue_engine_docs(operation="search", query="AssetManager AssetBundles")
```

---

## 10. Anti-hallucination checklist (when answering data-assets questions)

Before naming an API, mentally verify:

- [ ] Is the function name on `UAssetManager` or `FStreamableManager`? (different singletons)
- [ ] Am I confusing `FPrimaryAssetType` (category) with `FPrimaryAssetId` (specific)?
- [ ] Did I use `TSoftClassPtr<T>` when the question asked about a class reference (not instance)?
- [ ] Is the config section header in `[/Script/Engine.<ClassName>]` form?
- [ ] Did I use `meta=(AssetBundles="...")` (plural, with `=`) for UPROPERTY bundle membership?
- [ ] Does the row struct inherit `FTableRowBase` (not `FDataTableRow`, `FRowBase`, or `FDataTableRowBase`)?
- [ ] Did I name a real `Dump*` function (not `DumpPrimaryAssets` / `LogPrimaryAssets` / etc.)?

If you cannot answer confidently from this reference, verify against UE 5.7/5.8 source:

```python
# Verify a config section name by reading the UCLASS decl
ue_grep(query="UCLASS.*config", domain="source", scope="engine", path_pattern="Runtime/Engine/Classes/Engine/AssetManagerSettings.h")

# Verify a UPROPERTY meta-tag exists in real engine code
ue_grep(query="AssetBundles", domain="source", scope="engine")

# Resolve a class hierarchy (e.g., confirm FTableRowBase is the canonical row base)
ue_analyze_symbols(operation="trace_hierarchy", query="FTableRowBase")
```

---

## Trigger phrases (for reference / human review of routing rules)

Korean: `에셋 매니저`, `데이터 에셋`, `데이터 테이블`, `에셋 번들`, `소프트 포인터 에셋`, `프라이머리 에셋`
English: `UAssetManager`, `UDataAsset`, `UDataTable`, `UPrimaryDataAsset`, `FStreamableManager`, `TSoftObjectPtr`, `TSoftClassPtr`, `FPrimaryAssetId`, `FPrimaryAssetType`, `AssetManagerSettings`, `asset bundle`, `primary asset`, `data table row`

Negative patterns (do NOT activate this skill):
- `Blueprint asset` (use ue_manage_blueprint instead)
- `Material asset` (use ue_manage_material instead)
- `asset cooking` / `package` / `shipping` (build-system territory)

---

*This skill's content is authored from UE 5.7/5.8 source verification + Issue #13003's 100-probe baseline (Issue #20615 (6) 5.8 reconfirm). When UE versions change, audit against the new `Engine/Source/Runtime/Engine/Classes/Engine/AssetManager.h` and update accordingly.*
