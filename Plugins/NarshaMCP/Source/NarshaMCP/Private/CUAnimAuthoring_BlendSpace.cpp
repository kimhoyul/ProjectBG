// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #9489 Phase 2 — BlendSpace authoring ops (7 UFUNCTIONs on
// UCURemoteLibrary) + Issue #16573 `AnimBS_CreateBlendSpace` (asset creation).
// Declarations in `Public/CURemoteLibrary.h`. Shared helpers in
// `CUAnimAuthoring_Shared.h`. Sibling file `CUAnimAuthoring_Montage.cpp`
// hosts the 8 AnimMontage handlers + `AnimAM_CreateMontage`.
//
// Pattern per function:
//   1. Validate input (early return on failure).
//   2. Load asset via `LoadBlendSpace` helper.
//   3. Wrap mutation in `FScopedTransaction` (tagged "NarshaMCP: <op> on <asset>").
//   4. Asset->Modify() + mutate.
//   5. `MarkDirtyAndSave` (UPackage::SavePackage under the hood).
//   6. Build JSON response with lifted fields (sample_index / modified / saved).

#include "CUAnimAuthoring_Shared.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/Skeleton.h"

// Issue #16573 — BlendSpace asset creation (UnrealEd factories are editor-only).
#if WITH_EDITOR
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

using namespace NarshaAnimAuthoring;

DEFINE_LOG_CATEGORY_STATIC(LogNarshaAnimBS, Log, All);

FString UCURemoteLibrary::AnimBS_AddSample(
	const FString& AssetPath,
	const FString& AnimRef,
	const FVector& SampleValue)
{
	UE_LOG(LogNarshaAnimBS, Log, TEXT("[AnimBS_AddSample] %s sample=(%.3f, %.3f, %.3f) anim=%s"),
		*AssetPath, SampleValue.X, SampleValue.Y, SampleValue.Z, *AnimRef);

	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath),
			TEXT("Verify the asset path includes the package suffix (e.g. /Game/Anim/BS_Foo.BS_Foo)")));
	}

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *AnimRef));
	if (!AnimSeq)
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AnimRef),
			TEXT("anim_ref must be a /Game/... AnimSequence path")));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: add_sample on %s"), *AssetPath)));
	BS->Modify();
#endif

	// UBlendSpace::AddSample (UE 5.8) returns the new sample's index, or
	// INDEX_NONE on failure (out-of-range axis values, duplicate position, etc).
	const int32 NewIndex = BS->AddSample(AnimSeq, SampleValue);
	if (NewIndex == INDEX_NONE)
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			FString::Printf(TEXT("BlendSpace::AddSample rejected sample at (%.3f, %.3f) — possibly out of axis range or duplicate position"),
				SampleValue.X, SampleValue.Y),
			TEXT("Inspect axes[].min/max via get_blendspace_structure; use move_sample or set_sample_animation if the position is occupied")));
	}

#if WITH_EDITOR
	// Issue #12592 — UBlendSpace::AddSample mutates SampleData but does NOT rebuild
	// the serialized BlendSpaceData UPROPERTY (the runtime blend grid). Without
	// this call, SavePackage below persists a stale grid → reload/PIE shows
	// "sample out of bounds". ResampleData() is the engine's grid-rebuild entry
	// point (BlendSpace.cpp:2650), declared public+ENGINE_API at BlendSpace.h:663.
	BS->ResampleData();
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("sample_index"), NewIndex);
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimBS_RemoveSample(
	const FString& AssetPath,
	int32 Index,
	const FVector& SampleValue,
	bool bUseIndex)
{
	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath)));
	}

	int32 ResolvedIndex = INDEX_NONE;
	if (bUseIndex)
	{
		ResolvedIndex = Index;
	}
	else
	{
		ResolvedIndex = FindSampleByVector(BS, SampleValue);
		if (ResolvedIndex == INDEX_NONE)
		{
			return SerializeJsonResponse(MakeErrorObject(
				AssetPath,
				FString::Printf(TEXT("No sample matches sample_value=(%.3f, %.3f)"),
					SampleValue.X, SampleValue.Y),
				TEXT("Inspect samples[] via get_blendspace_structure; tolerance is 1e-4")));
		}
	}

	const int32 SampleCount = BS->GetBlendSamples().Num();
	if (ResolvedIndex < 0 || ResolvedIndex >= SampleCount)
	{
		return SerializeJsonResponse(MakeErrorObject(
			AssetPath,
			FString::Printf(TEXT("Index %d out of range (size=%d)"), ResolvedIndex, SampleCount)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: remove_sample on %s"), *AssetPath)));
	BS->Modify();
#endif

	const bool bRemoved = BS->DeleteSample(ResolvedIndex);

#if WITH_EDITOR
	// Issue #12592 — rebuild blend grid only on a real mutation. A failed
	// DeleteSample leaves SampleData untouched, so no rebuild is needed.
	if (bRemoved)
	{
		BS->ResampleData();
	}
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bRemoved);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("removed_index"), ResolvedIndex);
	Result->SetBoolField(TEXT("removed"), bRemoved);
	if (!bRemoved)
	{
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("DeleteSample(%d) returned false"), ResolvedIndex));
	}
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimBS_MoveSample(
	const FString& AssetPath,
	int32 Index,
	const FVector& NewSampleValue)
{
	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath)));
	}
	const int32 SampleCount = BS->GetBlendSamples().Num();
	if (Index < 0 || Index >= SampleCount)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Index %d out of range (size=%d)"), Index, SampleCount)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: move_sample on %s"), *AssetPath)));
	BS->Modify();
#endif

	const bool bMoved = BS->EditSampleValue(Index, NewSampleValue);

#if WITH_EDITOR
	// Issue #12592 — rebuild blend grid only on a real mutation. EditSampleValue
	// returns false on duplicate-position / out-of-axis-range; in that case
	// SampleData is unchanged so no grid rebuild is needed.
	if (bMoved)
	{
		BS->ResampleData();
	}
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bMoved);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("sample_index"), Index);
	if (!bMoved)
	{
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("EditSampleValue(%d) returned false — likely out of axis range or duplicate position"), Index));
	}
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimBS_SetSampleAnimation(
	const FString& AssetPath,
	int32 Index,
	const FString& NewAnimRef)
{
	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath)));
	}
	const int32 SampleCount = BS->GetBlendSamples().Num();
	if (Index < 0 || Index >= SampleCount)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Index %d out of range (size=%d)"), Index, SampleCount)));
	}

	UAnimSequence* AnimSeq = Cast<UAnimSequence>(StaticLoadObject(UAnimSequence::StaticClass(), nullptr, *NewAnimRef));
	if (!AnimSeq)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load AnimSequence: %s"), *NewAnimRef),
			TEXT("anim_ref must be a /Game/... AnimSequence path")));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: set_sample_animation on %s"), *AssetPath)));
	BS->Modify();
#endif

	const bool bSet = BS->ReplaceSampleAnimation(Index, AnimSeq);

#if WITH_EDITOR
	// Issue #12592 — swapping the AnimSequence on a sample can change additive
	// type / animation length / skeleton compatibility, all of which feed into
	// the grid weight calculation. Rebuild on success.
	if (bSet)
	{
		BS->ResampleData();
	}
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bSet);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetNumberField(TEXT("sample_index"), Index);
	if (!bSet)
	{
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("ReplaceSampleAnimation(%d) returned false"), Index));
	}
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimBS_SetAxisRange(
	const FString& AssetPath,
	int32 AxisIndex,
	float MinVal,
	float MaxVal)
{
	if (AxisIndex < 0 || AxisIndex > 1)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("AxisIndex must be 0 or 1, got %d"), AxisIndex)));
	}
	if (MinVal >= MaxVal)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Min (%.3f) must be < Max (%.3f)"), MinVal, MaxVal)));
	}

	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath)));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: set_axis_range on %s"), *AssetPath)));
	BS->Modify();
#endif

	// UBlendSpace::GetBlendParameter returns `const FBlendParameter&` because
	// the underlying `BlendParameters[3]` is `protected`. The defensive const
	// contract on the getter doesn't reflect the underlying mutability — the
	// field is a non-const member of a non-const UBlendSpace and is intended
	// to be edited externally (it's UPROPERTY EditAnywhere). The Editor itself
	// mutates this via the property system; the const_cast here is the
	// idiomatic plugin-author workaround when the property system isn't
	// available (we're inside a UFUNCTION running in the GameThread).
	const_cast<FBlendParameter&>(BS->GetBlendParameter(AxisIndex)).Min = MinVal;
	const_cast<FBlendParameter&>(BS->GetBlendParameter(AxisIndex)).Max = MaxVal;

#if WITH_EDITOR
	// Issue #12592 — axis range changes the grid extent; existing samples may
	// fall outside the new bounds. ResampleData re-triangulates and snaps
	// out-of-range samples. Without this, persisted BlendSpaceData references
	// the old extent → "sample out of bounds" on reload.
	BS->ResampleData();
#endif

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	TSharedRef<FJsonObject> Updated = MakeShared<FJsonObject>();
	Updated->SetNumberField(TEXT("axis_index"), AxisIndex);
	Updated->SetNumberField(TEXT("min"), MinVal);
	Updated->SetNumberField(TEXT("max"), MaxVal);
	Result->SetObjectField(TEXT("updated"), Updated);
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimBS_SetInterpolation(
	const FString& AssetPath,
	const FString& Kind)
{
	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath)));
	}

	EFilterInterpolationType InterpType;
	if (Kind == TEXT("Linear")) { InterpType = EFilterInterpolationType::BSIT_Average; }
	else if (Kind == TEXT("Cubic")) { InterpType = EFilterInterpolationType::BSIT_Cubic; }
	else if (Kind == TEXT("LinearStep") || Kind == TEXT("Step")) { InterpType = EFilterInterpolationType::BSIT_Linear; }
	else if (Kind == TEXT("CubicStep")) { InterpType = EFilterInterpolationType::BSIT_EaseInOut; }
	else
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Unknown interpolation kind '%s'"), *Kind),
			TEXT("Allowed: Linear / Cubic / Step / LinearStep / CubicStep")));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: set_interpolation on %s"), *AssetPath)));
	BS->Modify();
#endif

	BS->InterpolationParam[0].InterpolationType = InterpType;
	BS->InterpolationParam[1].InterpolationType = InterpType;

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	TSharedRef<FJsonObject> Updated = MakeShared<FJsonObject>();
	Updated->SetStringField(TEXT("kind"), Kind);
	Result->SetObjectField(TEXT("updated"), Updated);
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimBS_SetParameter(
	const FString& AssetPath,
	const FString& Key,
	const FString& JsonValue)
{
	UBlendSpace* BS = LoadBlendSpace(AssetPath);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to load BlendSpace: %s"), *AssetPath)));
	}

	// Issue #9489 commit 2.7 — UE's FJsonSerializer::Deserialize is strict and
	// only accepts JSON objects/arrays at the root; bare values like `"29.5"`
	// fail to parse. Live test 2026-05-12 hit this with sample_rate=29.5
	// returning "Failed to parse JsonValue '29.5'". Wrap input in array
	// `[<JsonValue>]` and extract first element.
	TSharedPtr<FJsonValue> ParsedValue;
	const FString WrappedJson = FString::Printf(TEXT("[%s]"), *JsonValue);
	TArray<TSharedPtr<FJsonValue>> ParsedArray;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(WrappedJson);
	if (!FJsonSerializer::Deserialize(Reader, ParsedArray) || ParsedArray.Num() != 1
	    || !ParsedArray[0].IsValid())
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Failed to parse JsonValue '%s' (must be valid JSON literal: number, true/false, \"string\", or [array]/{object})"), *JsonValue)));
	}
	ParsedValue = ParsedArray[0];

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("NarshaMCP: set_parameter on %s"), *AssetPath)));
	BS->Modify();
#endif

	bool bApplied = true;
	TSharedRef<FJsonObject> Updated = MakeShared<FJsonObject>();
	if (Key == TEXT("sample_rate"))
	{
		const float V = static_cast<float>(ParsedValue->AsNumber());
		BS->TargetWeightInterpolationSpeedPerSec = V;
		Updated->SetNumberField(TEXT("sample_rate"), V);
#if WITH_EDITOR
		// Issue #12592 — sample_rate writes need to trigger per-bone-blend
		// re-init, which the engine gates behind PostEditChangeProperty
		// (BlendSpace.cpp:164-166 dispatches InitializePerBoneBlend on
		// TargetWeightInterpolationSpeedPerSec changes). The issue body
		// suggested calling InitializePerBoneBlend() directly but that symbol
		// is protected + non-ENGINE_API (BlendSpace.h:708) so the plugin
		// can't reach it. Route through PostEditChangeProperty exactly the
		// way the Editor Details panel does — precedent at
		// CURemoteLibrary.cpp:11042-11049 (BP variable default writes).
		if (FProperty* RateProp = UBlendSpace::StaticClass()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(UBlendSpace, TargetWeightInterpolationSpeedPerSec)))
		{
			FPropertyChangedEvent ChangeEvent(RateProp, EPropertyChangeType::ValueSet);
			BS->PostEditChangeProperty(ChangeEvent);
		}
#endif
	}
	else if (Key == TEXT("notify_trigger_mode"))
	{
		const FString S = ParsedValue->AsString();
		if (S == TEXT("AllAnimations")) { BS->NotifyTriggerMode = ENotifyTriggerMode::AllAnimations; }
		else if (S == TEXT("HighestWeightedAnimation")) { BS->NotifyTriggerMode = ENotifyTriggerMode::HighestWeightedAnimation; }
		else if (S == TEXT("None")) { BS->NotifyTriggerMode = ENotifyTriggerMode::None; }
		else { bApplied = false; }
		Updated->SetStringField(TEXT("notify_trigger_mode"), S);
	}
	else if (Key == TEXT("wrap_input"))
	{
		// Same const_cast workaround as AnimBS_SetAxisRange — see comment there.
		// Per-axis bWrapInput; we set both axes for consistency.
		const bool V = ParsedValue->AsBool();
		const_cast<FBlendParameter&>(BS->GetBlendParameter(0)).bWrapInput = V;
		const_cast<FBlendParameter&>(BS->GetBlendParameter(1)).bWrapInput = V;
		Updated->SetBoolField(TEXT("wrap_input"), V);
#if WITH_EDITOR
		// Issue #12592 — wrap_input changes how axis values clamp into the
		// grid (wrap vs clamp at boundaries). ResampleData re-derives the
		// segmentation so reload reflects the new wrap behavior.
		BS->ResampleData();
#endif
	}
	else
	{
		return SerializeJsonResponse(MakeErrorObject(AssetPath,
			FString::Printf(TEXT("Unknown set_parameter key '%s'"), *Key)));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), bApplied);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	Result->SetObjectField(TEXT("updated"), Updated);
	if (!bApplied)
	{
		Result->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Key '%s' could not be applied (engine version may not expose this UPROPERTY)"), *Key));
	}
	MarkDirtyAndSave(BS, Result);
	return SerializeJsonResponse(Result);
}

// ---------------------------------------------------------------------------
// Issue #16573 — create_blendspace: new 1D (UBlendSpace1D) or 2D (UBlendSpace)
// BlendSpace from an explicit skeleton, with per-axis config applied at
// creation. Both UnrealEd factories hard-require TargetSkeleton (return nullptr
// otherwise); axis params are flattened scalars (Y* ignored when AxisCount==1)
// to dodge the UE 5.8 RC TArray<float> marshalling defect (see AnimBS_AddSample).
// ---------------------------------------------------------------------------
FString UCURemoteLibrary::AnimBS_CreateBlendSpace(
	const FString& DestinationPath,
	const FString& SkeletonPath,
	int32 AxisCount,
	const FString& XAxisName,
	float XAxisMin,
	float XAxisMax,
	int32 XGridDivisions,
	const FString& YAxisName,
	float YAxisMin,
	float YAxisMax,
	int32 YGridDivisions)
{
#if WITH_EDITOR
	if (AxisCount != 1 && AxisCount != 2)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("axis_count must be 1 (BlendSpace1D) or 2 (BlendSpace2D); got %d"), AxisCount)));
	}

	USkeleton* Skeleton = Cast<USkeleton>(
		StaticLoadObject(USkeleton::StaticClass(), nullptr, *SkeletonPath));
	if (!Skeleton)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("Failed to load Skeleton: %s"), *SkeletonPath),
			TEXT("skeleton_path must be a /Game/... USkeleton path (e.g. /Game/.../SK_Mannequin_Skeleton.SK_Mannequin_Skeleton)")));
	}

	// Normalize destination → long package name (drop any `.ObjectName` suffix).
	FString PackageName = DestinationPath.Contains(TEXT("."))
		? FPackageName::ObjectPathToPackageName(DestinationPath)
		: DestinationPath;
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("Invalid destination package name: %s"), *PackageName),
			TEXT("destination_path must be a long package name under /Game (e.g. /Game/Anim/BS_Locomotion)")));
	}
	if (FPackageName::DoesPackageExist(PackageName))
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("An asset already exists at %s"), *PackageName),
			TEXT("Choose a destination_path that does not yet exist, or delete the existing asset first")));
	}
	const FString AssetName = FPackageName::GetShortName(PackageName);
	// RC UFUNCTIONs bypass the Rust param gate (cf. #15755) — reject an empty leaf
	// so FactoryCreateNew(FName("")) can't auto-name a garbage asset.
	if (AssetName.TrimStartAndEnd().IsEmpty())
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("Could not derive an asset name from %s"), *PackageName),
			TEXT("destination_path must end in a non-empty asset name (e.g. /Game/Anim/BS_Locomotion)")));
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			FString::Printf(TEXT("CreatePackage failed for %s"), *PackageName)));
	}
	// Issue #1699: FullyLoad a freshly-created package before SavePackage to avoid
	// the 'partially loaded' save crash (matches HandleCreateAsset /
	// CUCameraRigAuthor creation paths; shared MarkDirtyAndSave skips it).
	Package->FullyLoad();

	UObject* NewAsset = nullptr;
	if (AxisCount == 1)
	{
		UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
		Factory->TargetSkeleton = Skeleton;
		NewAsset = Factory->FactoryCreateNew(
			UBlendSpace1D::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn);
	}
	else
	{
		UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
		Factory->TargetSkeleton = Skeleton;
		NewAsset = Factory->FactoryCreateNew(
			UBlendSpace::StaticClass(), Package, FName(*AssetName),
			RF_Public | RF_Standalone, nullptr, GWarn);
	}
	if (!NewAsset)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			TEXT("BlendSpace FactoryCreateNew returned nullptr")));
	}

	// UBlendSpace1D : public UBlendSpace, so axis config is uniform for both.
	UBlendSpace* BS = Cast<UBlendSpace>(NewAsset);
	if (!BS)
	{
		return SerializeJsonResponse(MakeErrorObject(
			DestinationPath,
			TEXT("Created asset is not a UBlendSpace")));
	}

	// Apply axis config at creation. FBlendParameter is directly assignable;
	// GetBlendParameter returns a const ref (see AnimBS_SetAxisRange rationale).
	{
		FBlendParameter& AxisX = const_cast<FBlendParameter&>(BS->GetBlendParameter(0));
		if (!XAxisName.IsEmpty()) { AxisX.DisplayName = XAxisName; }
		AxisX.Min = XAxisMin;
		AxisX.Max = XAxisMax;
		if (XGridDivisions > 0) { AxisX.GridNum = XGridDivisions; }
	}
	if (AxisCount == 2)
	{
		FBlendParameter& AxisY = const_cast<FBlendParameter&>(BS->GetBlendParameter(1));
		if (!YAxisName.IsEmpty()) { AxisY.DisplayName = YAxisName; }
		AxisY.Min = YAxisMin;
		AxisY.Max = YAxisMax;
		if (YGridDivisions > 0) { AxisY.GridNum = YGridDivisions; }
	}
	BS->ResampleData();

	FAssetRegistryModule::AssetCreated(NewAsset);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), PackageName);
	Result->SetNumberField(TEXT("axis_count"), AxisCount);
	Result->SetStringField(TEXT("blend_type"),
		AxisCount == 1 ? TEXT("BlendSpace1D") : TEXT("BlendSpace"));
	MarkDirtyAndSave(NewAsset, Result);
	return SerializeJsonResponse(Result);
#else
	return SerializeJsonResponse(MakeErrorObject(
		DestinationPath,
		TEXT("create_blendspace requires a WITH_EDITOR build (BlendSpace factories are editor-only)")));
#endif
}
