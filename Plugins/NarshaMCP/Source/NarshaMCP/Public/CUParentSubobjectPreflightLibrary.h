// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CUParentSubobjectPreflightLibrary.generated.h"

/**
 * NarshaMCP parent-CDO subobject pre-flight (Issue #28225, Epic #28173).
 *
 * WHY THIS EXISTS
 * ---------------
 * When a parent Blueprint's CDO holds an Instanced subobject that lacks
 * `RF_Public`, a derived child Blueprint is created and compiled successfully
 * but `SavePackage` REFUSES to write it:
 *
 *     Can't save 'GE_ZZChild.uasset': Illegal reference to private object:
 *       'AssetTagsGameplayEffectComponent .../GE_ZZParent.Default__GE_ZZParent_C:
 *        AssetTagsGameplayEffectComponent_0'
 *       referenced by 'AssetTagsGameplayEffectComponent_0' ... in its
 *       'Unknown property' property.
 *
 * Re-instancing is NOT broken — the child does get its own copy. What fails is
 * the copy's ARCHETYPE LINK, which points back at the parent package's
 * non-public object. `Unknown property` is the giveaway that the culprit is an
 * archetype link rather than a UPROPERTY (`SavePackageUtilities.cpp`,
 * `FindMostLikelyCulprit`). The engine classifies this as a code defect:
 * "All archetype objects must be public."
 *
 * This is an AUTHORING-PATH difference, not an engine bug:
 *
 *   Editor Details panel          → RF_Public  (PropertyHandleImpl.cpp:5887,
 *                                   GetMaskedFlags(RF_PropagateToSubObjects))
 *   C++ UGameplayEffect::AddComponent<T>() → RF_Public (GameplayEffect.h:2497)
 *   Python unreal.new_object(cls, outer=cdo) → NOT public (no flags argument)
 *
 * `RF_PropagateToSubObjects` = RF_Public | RF_ArchetypeObject | RF_Transactional
 * | RF_Transient (ObjectMacros.h:646).
 *
 * So only parents whose CDO subobjects were injected by a script are affected.
 * NarshaMCP itself never authors such a parent, but external tools and user
 * scripts do — and the caller then receives success:true and stacks further
 * work on an asset that will never reach disk.
 *
 * WHY C++ AND NOT PYTHON
 * ----------------------
 * UE Python cannot read object flags — the issue's own live investigation
 * needed a temporary C++ probe just to observe the flag strings. Detection and
 * repair therefore have to live here.
 *
 * WHY REFLECTION AND NOT A GameplayEffect SPECIAL CASE
 * ----------------------------------------------------
 * The walk is driven by `CPF_InstancedReference`, so it covers any Instanced
 * subobject on any CDO — GameplayEffect's `GEComponents` is merely where the
 * defect was first observed. Same mechanism as the property walk in
 * `CUBehaviorTreeWriteFinalize.cpp`.
 *
 * Invoked via Remote Control:
 *   PUT http://localhost:30010/remote/object/call
 *   {
 *     "objectPath": "/Script/NarshaMCP.Default__CUParentSubobjectPreflightLibrary",
 *     "functionName": "InspectParentSubobjectPublicFlags",
 *     "parameters": { "ParentPath": "/Game/GE_Parent", "bRepair": false }
 *   }
 */
/**
 * Editor-free core of the pre-flight, split out of the UFUNCTION so the
 * reflection walk is unit-testable on any in-memory object. The UFUNCTION below
 * only adds path resolution, the repair write, and JSON shaping — none of which
 * change what counts as an offender.
 */
namespace CUParentSubobjectPreflight
{
	/** One CDO subobject that lacks RF_Public. */
	struct FNonPublicSubobject
	{
		UObject* Object = nullptr;
		FString Name;
		FString PathName;
		/** The CPF_InstancedReference property pointing at it, or `<unreferenced>`. */
		FString PropertyName;
	};

	/**
	 * Walk `DefaultObject`'s subobjects and collect the ones without RF_Public.
	 *
	 * Membership is decided by `GetObjectsWithOuter(DefaultObject, …,
	 * IncludeNestedObjects)` — every object whose outer chain reaches the CDO,
	 * which is exactly the archetype set SavePackage validates. A separate
	 * `CPF_InstancedReference` property walk supplies the attribution so a
	 * refusal can name the part rather than only the generated object name;
	 * an offender no property references is still reported, tagged
	 * `<unreferenced>`.
	 *
	 * Performs NO writes and loads nothing.
	 */
	NARSHAMCP_API void CollectNonPublicSubobjects(
		UObject* DefaultObject,
		TArray<FNonPublicSubobject>& OutOffenders,
		int32& OutInspected,
		int32& OutInstancedProperties);

	/**
	 * Set RF_Public on each offender, returning how many were flagged.
	 *
	 * RF_Public ONLY: the live investigation confirmed a single
	 * `SetFlags(RF_Public)` clears the save refusal, so this deliberately does
	 * not widen to the whole RF_PropagateToSubObjects mask — the minimum change
	 * that restores the invariant the engine already requires.
	 *
	 * Marking the package dirty and saving is the CALLER's job; flagging alone
	 * does not persist anything.
	 */
	NARSHAMCP_API int32 ApplyPublicFlag(const TArray<FNonPublicSubobject>& Offenders);
}

UCLASS()
class NARSHAMCP_API UCUParentSubobjectPreflightLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Inspect (and optionally repair) the Instanced subobjects on a parent's CDO.
	 *
	 * `ParentPath` accepts a Blueprint asset path ("/Game/Fx/GE_Parent"), in which
	 * case the CDO of its GeneratedClass is walked, or a native class path
	 * ("/Script/GameplayAbilities.GameplayEffect").
	 *
	 * `bRepair=false` (the DEFAULT the caller should use) performs NO writes: it
	 * reports what it found and leaves the asset untouched. Silently mutating
	 * somebody else's asset is too much for a default. The caller decides whether
	 * to reject or to ask for a repair.
	 *
	 * `bRepair=true` applies `SetFlags(RF_Public)` to each offender and re-saves
	 * the parent package. This restores the invariant the engine already requires
	 * ("All archetype objects must be public"), so it is not a semantic change to
	 * the asset. The save verdict is reported honestly in `saved` — a repair that
	 * could not be persisted (read-only file, VCS checkout required) returns
	 * success:false so the caller falls back to rejecting rather than deriving
	 * from a parent that is still defective on disk.
	 *
	 * Returns JSON:
	 * {
	 *   "success": bool,            // inspection completed (bRepair=false), or
	 *                               // repair applied AND persisted (bRepair=true)
	 *   "error": "...",             // present only on failure
	 *   "parent_path": "...",
	 *   "resolved_class": "...",
	 *   "inspected": N,             // instanced subobjects visited
	 *   "clean": bool,              // true when there are no offenders (no-op)
	 *   "offenders": ["Name", ...],
	 *   "offender_paths": ["/Game/...:Sub", ...],
	 *   "repair_requested": bool,
	 *   "parent_repaired": ["Name", ...],   // actually flagged this call
	 *   "saved": bool               // parent package re-save verdict (repair only)
	 * }
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Blueprint")
	static FString InspectParentSubobjectPublicFlags(const FString& ParentPath, bool bRepair);
};
