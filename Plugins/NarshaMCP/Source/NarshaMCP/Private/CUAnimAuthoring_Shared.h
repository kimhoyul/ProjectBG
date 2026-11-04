// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #9489 Phase 2 — shared helpers for `ue_manage_animation` authoring
// ops. Split across two impl files (`CUAnimAuthoring_BlendSpace.cpp` /
// `CUAnimAuthoring_Montage.cpp`) to stay under Rule 8's 1000-line cap; this
// header hosts the common file-local helpers each impl needs.
//
// All helpers are `static inline` so the `internal linkage + included by
// multiple TUs` rule applies cleanly — no ODR violation, no linker conflict.

#pragma once

#include "CURemoteLibrary.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/BlendSpace.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#endif

namespace NarshaAnimAuthoring
{
	static inline FString SerializeJsonResponse(const TSharedRef<FJsonObject>& Obj)
	{
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}

	static inline TSharedRef<FJsonObject> MakeErrorObject(
		const FString& AssetPath,
		const FString& ErrorMsg,
		const FString& Hint = FString())
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("asset_path"), AssetPath);
		Obj->SetStringField(TEXT("error"), ErrorMsg);
		if (!Hint.IsEmpty())
		{
			Obj->SetStringField(TEXT("hint"), Hint);
		}
		return Obj;
	}

	/**
	 * Resolve a caller track string to an AnimNotifyTracks index (Issue #23577).
	 *
	 * Lock-step with Rust `notify_track_alias::resolve_notify_track_alias`:
	 *  1) exact TrackName match (case-sensitive, then case-insensitive)
	 *  2) synthetic "Track N" / "Track_N" → 0-based index N
	 *  3) pure integer string → 0-based index (only after exact-name miss)
	 *
	 * Returns INDEX_NONE when unresolved. When OutResolvedName is non-null and
	 * resolution succeeds, writes the serialized TrackName (or index string if
	 * the slot has a blank name).
	 */
	static inline int32 ResolveMontageNotifyTrackIndex(
		const UAnimMontage* Montage,
		const FString& Requested,
		FString* OutResolvedName = nullptr)
	{
		if (!Montage)
		{
			return INDEX_NONE;
		}
		const FString Req = Requested.TrimStartAndEnd();
		if (Req.IsEmpty() || Montage->AnimNotifyTracks.Num() == 0)
		{
			return INDEX_NONE;
		}

		// 1a) Exact (case-sensitive).
		for (int32 i = 0; i < Montage->AnimNotifyTracks.Num(); ++i)
		{
			const FString Name = Montage->AnimNotifyTracks[i].TrackName.ToString();
			if (Name.IsEmpty())
			{
				continue;
			}
			if (Name.Equals(Req, ESearchCase::CaseSensitive))
			{
				if (OutResolvedName)
				{
					*OutResolvedName = Name;
				}
				return i;
			}
		}
		// 1b) Case-insensitive exact.
		for (int32 i = 0; i < Montage->AnimNotifyTracks.Num(); ++i)
		{
			const FString Name = Montage->AnimNotifyTracks[i].TrackName.ToString();
			if (Name.IsEmpty())
			{
				continue;
			}
			if (Name.Equals(Req, ESearchCase::IgnoreCase))
			{
				if (OutResolvedName)
				{
					*OutResolvedName = Name;
				}
				return i;
			}
		}

		// 2) Synthetic "Track N" / "Track_N" / "trackN".
		{
			FString Lower = Req.ToLower();
			if (Lower.StartsWith(TEXT("track")))
			{
				FString Rest = Lower.RightChop(5); // after "track"
				while (Rest.StartsWith(TEXT(" ")) || Rest.StartsWith(TEXT("_")) || Rest.StartsWith(TEXT("-")))
				{
					Rest = Rest.RightChop(1);
				}
				bool bAllDigits = !Rest.IsEmpty();
				for (int32 c = 0; c < Rest.Len(); ++c)
				{
					if (!FChar::IsDigit(Rest[c]))
					{
						bAllDigits = false;
						break;
					}
				}
				if (bAllDigits)
				{
					const int32 Idx = FCString::Atoi(*Rest);
					if (Idx >= 0 && Idx < Montage->AnimNotifyTracks.Num())
					{
						const FString Name = Montage->AnimNotifyTracks[Idx].TrackName.ToString();
						if (OutResolvedName)
						{
							*OutResolvedName = Name.IsEmpty() ? FString::FromInt(Idx) : Name;
						}
						return Idx;
					}
				}
			}
		}

		// 3) Pure integer as 0-based index.
		{
			bool bAllDigits = !Req.IsEmpty();
			for (int32 c = 0; c < Req.Len(); ++c)
			{
				if (!FChar::IsDigit(Req[c]))
				{
					bAllDigits = false;
					break;
				}
			}
			if (bAllDigits)
			{
				const int32 Idx = FCString::Atoi(*Req);
				if (Idx >= 0 && Idx < Montage->AnimNotifyTracks.Num())
				{
					const FString Name = Montage->AnimNotifyTracks[Idx].TrackName.ToString();
					if (OutResolvedName)
					{
						*OutResolvedName = Name.IsEmpty() ? FString::FromInt(Idx) : Name;
					}
					return Idx;
				}
			}
		}

		return INDEX_NONE;
	}

	/** Build the standardized "track not found" error for montage notify write
	 *  ops (add_notify / add_notify_state / remove_notify).
	 *
	 *  Issue #14776: the previous hint ("Inspect tracks[] via
	 *  get_montage_notifies") was a circular dead-end — the read returned only
	 *  synthetic "Track N" labels, never the real serialized FName these ops
	 *  match against. Enumerate the actually-available
	 *  `AnimNotifyTracks[i].TrackName` (the live, authoritative values) so the
	 *  caller can immediately retry with a real track, no second call needed.
	 *
	 *  Issue #23577: also advertise synthetic Track N / numeric index aliases
	 *  now accepted by ResolveMontageNotifyTrackIndex. */
	static inline TSharedRef<FJsonObject> MakeTrackNotFoundError(
		const FString& AssetPath,
		const FString& Track,
		const UAnimMontage* Montage)
	{
		TArray<FString> Available;
		if (Montage)
		{
			Available.Reserve(Montage->AnimNotifyTracks.Num());
			for (int32 i = 0; i < Montage->AnimNotifyTracks.Num(); ++i)
			{
				Available.Add(Montage->AnimNotifyTracks[i].TrackName.ToString());
			}
		}
		const FString Hint = Available.Num() > 0
			? FString::Printf(
				TEXT("Available tracks: %s. Also accepts synthetic labels Track N / Track_N (0-based index) and numeric indices."),
				*FString::Join(Available, TEXT(", ")))
			: TEXT("Montage has no notify tracks");
		return MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Track '%s' not found on montage"), *Track),
			Hint);
	}

	static inline UBlendSpace* LoadBlendSpace(const FString& AssetPath)
	{
		UObject* Loaded = StaticLoadObject(UBlendSpace::StaticClass(), nullptr, *AssetPath);
		return Cast<UBlendSpace>(Loaded);
	}

	static inline UAnimMontage* LoadMontage(const FString& AssetPath)
	{
		UObject* Loaded = StaticLoadObject(UAnimMontage::StaticClass(), nullptr, *AssetPath);
		return Cast<UAnimMontage>(Loaded);
	}

	/** Save the asset's owning package via UPackage::SavePackage (codebase-standard
	 *  pattern; see e.g. line 1144 / 2219 / 4498 of CURemoteLibrary.cpp). Writes
	 *  `modified` and `saved` boolean fields onto `Obj`, plus a `save_hint` string
	 *  on failure. */
	static inline void MarkDirtyAndSave(UObject* Asset, const TSharedRef<FJsonObject>& Obj)
	{
		if (!Asset)
		{
			Obj->SetBoolField(TEXT("modified"), false);
			Obj->SetBoolField(TEXT("saved"), false);
			return;
		}
		Asset->MarkPackageDirty();
		Obj->SetBoolField(TEXT("modified"), true);

		UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			Obj->SetBoolField(TEXT("saved"), false);
			Obj->SetStringField(TEXT("save_hint"), TEXT("Asset has no Outermost package"));
			return;
		}
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		SaveArgs.Error = GError;

		const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
		Obj->SetBoolField(TEXT("saved"), bSaved);
		if (!bSaved)
		{
			Obj->SetStringField(TEXT("save_hint"),
				FString::Printf(TEXT("SavePackage returned false for '%s'"), *PackageFileName));
		}
	}

	/** Try to load a UClass* (AnimNotify / AnimNotifyState subclass) by path. */
	static inline UClass* TryLoadNotifyClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}
		UClass* Cls = LoadClass<UObject>(nullptr, *ClassPath);
		if (!Cls)
		{
			Cls = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *ClassPath));
		}
		return Cls;
	}

	/** axis_values arity check (1 = BS1D/AO1D, 2 = BS/AO). */
	static inline bool ValidateAxisArity(
		const TArray<float>& AxisValues,
		const FString& Op,
		const FString& /*AssetPath*/,
		FString& OutError)
	{
		if (AxisValues.Num() < 1 || AxisValues.Num() > 2)
		{
			OutError = FString::Printf(
				TEXT("%s: AxisValues must have length 1 (BS1D/AO1D) or 2 (BS/AO); got %d"),
				*Op, AxisValues.Num());
			return false;
		}
		return true;
	}

	/** Find sample index whose SampleValue matches `Target` within `Eps`. INDEX_NONE if absent.
	 *  Switched from TArray<float> to FVector in commit 2.6 — UE 5.8 RC has a known
	 *  TArray<float> marshalling defect (N-element input gets mangled to N+1 with
	 *  leading zero). FVector marshals correctly via RC's struct serializer.
	 *  Y is ignored for BS1D/AO1D callers (just send X with Y=Z=0). */
	static inline int32 FindSampleByVector(
		const UBlendSpace* BS,
		const FVector& Target,
		float Eps = 1e-4f)
	{
		if (!BS)
		{
			return INDEX_NONE;
		}
		const TArray<FBlendSample>& Samples = BS->GetBlendSamples();
		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			const FVector& V = Samples[i].SampleValue;
			if (FMath::IsNearlyEqual(static_cast<float>(V.X), static_cast<float>(Target.X), Eps)
				&& FMath::IsNearlyEqual(static_cast<float>(V.Y), static_cast<float>(Target.Y), Eps))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}
}
