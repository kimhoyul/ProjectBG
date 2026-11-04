// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #28225 (Epic #28173): parent-CDO subobject RF_Public pre-flight.

#include "CUParentSubobjectPreflightLibrary.h"

#include "CUJsonUtils.h"
#include "CULog.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
// GetObjectsWithOuter lives in UObjectHash.h. CURemoteLibrary.cpp reaches it
// transitively through UObjectIterator.h under Unity builds; include it directly
// so this file also compiles with Unity disabled.
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr const TCHAR* JsonSuccess = TEXT("success");
	constexpr const TCHAR* UnreferencedProperty = TEXT("<unreferenced>");

	/**
	 * Resolve the CDO to inspect from a caller-supplied path.
	 *
	 * Accepts a Blueprint asset path (walk its GeneratedClass CDO) or a native
	 * class path. Returns nullptr and fills OutError when the path names nothing
	 * usable — an unresolvable parent must fail closed, never quietly report
	 * "clean". A false all-clear here would hand the caller exactly the
	 * success-that-is-not-success this issue exists to remove.
	 */
	UObject* ResolveParentDefaultObject(const FString& ParentPath, UClass*& OutClass, FString& OutError)
	{
		OutClass = nullptr;

		if (ParentPath.IsEmpty())
		{
			OutError = TEXT("ParentPath is required");
			return nullptr;
		}

		UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *ParentPath);
		if (!Loaded)
		{
			// A bare class path ("/Script/Module.Class") loads as a UClass.
			Loaded = StaticLoadObject(UClass::StaticClass(), nullptr, *ParentPath);
		}

		if (!Loaded)
		{
			OutError = FString::Printf(TEXT("could not load parent '%s'"), *ParentPath);
			return nullptr;
		}

		if (const UBlueprint* AsBlueprint = Cast<UBlueprint>(Loaded))
		{
			OutClass = AsBlueprint->GeneratedClass;
			if (!OutClass)
			{
				OutError = FString::Printf(
					TEXT("parent Blueprint '%s' has no GeneratedClass (compile it first)"), *ParentPath);
				return nullptr;
			}
		}
		else if (UClass* AsClass = Cast<UClass>(Loaded))
		{
			OutClass = AsClass;
		}
		else
		{
			// Out of contract: the path named an object instance, not a Blueprint
			// asset or a class. Silently substituting `Loaded->GetClass()` would
			// inspect a DIFFERENT CDO than the caller asked about and report a
			// verdict about it — the same shape of quiet substitution this issue
			// exists to remove. Refuse instead.
			OutError = FString::Printf(
				TEXT("parent '%s' resolved to a %s instance, not a Blueprint asset or a class"),
				*ParentPath, *Loaded->GetClass()->GetName());
			return nullptr;
		}

		UObject* DefaultObject = OutClass->GetDefaultObject();
		if (!DefaultObject)
		{
			OutError = FString::Printf(TEXT("class '%s' has no CDO"), *OutClass->GetName());
		}
		return DefaultObject;
	}

	/**
	 * Map each referenced object to the CPF_InstancedReference property that points
	 * at it, so a rejection can name the part the user has to re-author.
	 *
	 * TPropertyValueIterator descends into structs and containers, so an instanced
	 * reference held in a TArray/TMap (GameplayEffect's GEComponents is an array)
	 * is attributed the same as a plain object property. This is the
	 * CPF_InstancedReference walk AC #7 asks for; membership itself is still
	 * decided by the outer chain, which also catches subobjects no property
	 * happens to reference.
	 */
	void BuildInstancedPropertyAttribution(UObject* DefaultObject, TMap<const UObject*, FString>& OutAttribution)
	{
		if (!DefaultObject)
		{
			return;
		}

		for (TPropertyValueIterator<FObjectPropertyBase> It(DefaultObject->GetClass(), DefaultObject); It; ++It)
		{
			const FObjectPropertyBase* Property = It.Key();
			const void* ValuePtr = It.Value();
			if (!Property || !ValuePtr)
			{
				continue;
			}

			if (!Property->HasAnyPropertyFlags(CPF_InstancedReference))
			{
				continue;
			}

			const UObject* Referenced = Property->GetObjectPropertyValue(ValuePtr);
			if (Referenced && !OutAttribution.Contains(Referenced))
			{
				OutAttribution.Add(Referenced, Property->GetName());
			}
		}
	}
}

namespace CUParentSubobjectPreflight
{
	void CollectNonPublicSubobjects(
		UObject* DefaultObject,
		TArray<FNonPublicSubobject>& OutOffenders,
		int32& OutInspected,
		int32& OutInstancedProperties)
	{
		OutOffenders.Reset();
		OutInspected = 0;
		OutInstancedProperties = 0;

		if (!DefaultObject)
		{
			return;
		}

		TMap<const UObject*, FString> Attribution;
		BuildInstancedPropertyAttribution(DefaultObject, Attribution);
		OutInstancedProperties = Attribution.Num();

		TArray<UObject*> Subobjects;
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		GetObjectsWithOuter(DefaultObject, Subobjects, EGetObjectsFlags::IncludeNestedObjects);
#else
		GetObjectsWithOuter(DefaultObject, Subobjects, /*bIncludeNestedObjects=*/true);
#endif

		for (UObject* Subobject : Subobjects)
		{
			if (!IsValid(Subobject))
			{
				continue;
			}

			++OutInspected;

			if (Subobject->HasAnyFlags(RF_Public))
			{
				continue;
			}

			FNonPublicSubobject Offender;
			Offender.Object = Subobject;
			Offender.Name = Subobject->GetName();
			Offender.PathName = Subobject->GetPathName();
			if (const FString* PropertyName = Attribution.Find(Subobject))
			{
				Offender.PropertyName = *PropertyName;
			}
			else
			{
				Offender.PropertyName = UnreferencedProperty;
			}
			OutOffenders.Add(MoveTemp(Offender));
		}
	}

	int32 ApplyPublicFlag(const TArray<FNonPublicSubobject>& Offenders)
	{
		int32 Flagged = 0;
		for (const FNonPublicSubobject& Offender : Offenders)
		{
			if (IsValid(Offender.Object))
			{
				Offender.Object->SetFlags(RF_Public);
				++Flagged;
			}
		}
		return Flagged;
	}
}

FString UCUParentSubobjectPreflightLibrary::InspectParentSubobjectPublicFlags(
	const FString& ParentPath,
	bool bRepair)
{
	using namespace CUParentSubobjectPreflight;

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("parent_path"), ParentPath);
	Result->SetBoolField(TEXT("repair_requested"), bRepair);

	UClass* ResolvedClass = nullptr;
	FString ResolveError;
	UObject* DefaultObject = ResolveParentDefaultObject(ParentPath, ResolvedClass, ResolveError);
	if (!DefaultObject)
	{
		Result->SetBoolField(JsonSuccess, false);
		Result->SetStringField(TEXT("error"), ResolveError);
		return CUJsonUtils::SerializeToString(Result);
	}

	Result->SetStringField(TEXT("resolved_class"), ResolvedClass ? ResolvedClass->GetPathName() : TEXT(""));

	TArray<FNonPublicSubobject> Offenders;
	int32 Inspected = 0;
	int32 InstancedProperties = 0;
	CollectNonPublicSubobjects(DefaultObject, Offenders, Inspected, InstancedProperties);

	TArray<TSharedPtr<FJsonValue>> OffenderNames;
	TArray<TSharedPtr<FJsonValue>> OffenderPaths;
	TArray<TSharedPtr<FJsonValue>> OffenderProperties;
	for (const FNonPublicSubobject& Offender : Offenders)
	{
		OffenderNames.Add(MakeShared<FJsonValueString>(Offender.Name));
		OffenderPaths.Add(MakeShared<FJsonValueString>(Offender.PathName));
		OffenderProperties.Add(MakeShared<FJsonValueString>(Offender.PropertyName));
	}

	Result->SetNumberField(TEXT("inspected"), Inspected);
	Result->SetNumberField(TEXT("instanced_properties"), InstancedProperties);
	Result->SetBoolField(TEXT("clean"), Offenders.Num() == 0);
	Result->SetArrayField(TEXT("offenders"), OffenderNames);
	Result->SetArrayField(TEXT("offender_paths"), OffenderPaths);
	Result->SetArrayField(TEXT("offender_properties"), OffenderProperties);

	const TArray<TSharedPtr<FJsonValue>> NoRepairs;

	if (Offenders.Num() == 0)
	{
		// Pre-flight no-op on a healthy parent (AC #6).
		Result->SetBoolField(JsonSuccess, true);
		Result->SetArrayField(TEXT("parent_repaired"), NoRepairs);
		return CUJsonUtils::SerializeToString(Result);
	}

	if (!bRepair)
	{
		// Detection succeeded; the CALLER decides the verdict. success:true here is
		// not a claim that the parent is usable — `clean` is false and `offenders`
		// is populated. The Rust tier turns this into the user-facing refusal.
		Result->SetBoolField(JsonSuccess, true);
		Result->SetArrayField(TEXT("parent_repaired"), NoRepairs);
		UE_LOG(LogNarshaMCPBlueprint, Warning,
			TEXT("[28225] parent '%s' has %d non-public CDO subobject(s); derived assets will not save"),
			*ParentPath, Offenders.Num());
		return CUJsonUtils::SerializeToString(Result);
	}

	const int32 Flagged = ApplyPublicFlag(Offenders);

	TArray<TSharedPtr<FJsonValue>> RepairedNames;
	for (const FNonPublicSubobject& Offender : Offenders)
	{
		if (IsValid(Offender.Object))
		{
			RepairedNames.Add(MakeShared<FJsonValueString>(Offender.Name));
		}
	}
	Result->SetArrayField(TEXT("parent_repaired"), RepairedNames);

	UPackage* Package = DefaultObject->GetOutermost();
	if (!Package)
	{
		Result->SetBoolField(JsonSuccess, false);
		Result->SetBoolField(TEXT("saved"), false);
		Result->SetStringField(TEXT("error"), TEXT("parent CDO has no outermost package to save"));
		return CUJsonUtils::SerializeToString(Result);
	}

	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	const bool bSaved = UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);

	Result->SetBoolField(TEXT("saved"), bSaved);
	// The repair only counts if it reached disk. A flag set in memory that never
	// persists would let the caller derive from a parent that is still defective
	// on disk and hit the identical save refusal one step later.
	Result->SetBoolField(JsonSuccess, bSaved);
	if (!bSaved)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("repaired %d subobject(s) in memory but could not save '%s' — check out the asset and retry"),
			Flagged, *PackageFileName));
	}

	UE_LOG(LogNarshaMCPBlueprint, Log,
		TEXT("[28225] repaired %d subobject(s) on '%s' | saved=%d"),
		Flagged, *ParentPath, bSaved ? 1 : 0);

	return CUJsonUtils::SerializeToString(Result);
}
