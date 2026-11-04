// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16577: Domain C++ authoring for reflection-blocked UPROPERTY areas (spike).

#include "CUCameraRigAuthor.h"
#include "CULog.h"

// GameplayCameras (experimental engine plugin, 5.7-pinned — see Build.cs note + spike eval doc).
#if WITH_NARSHAMCP_GAMEPLAYCAMERAS
#include "Core/CameraRigAsset.h"
#include "Nodes/Common/OffsetCameraNode.h"
#include "Build/CameraBuildStatus.h"
#endif

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
#if WITH_NARSHAMCP_GAMEPLAYCAMERAS
	FString BuildStatusToString(ECameraBuildStatus Status)
	{
		switch (Status)
		{
		case ECameraBuildStatus::Clean:             return TEXT("Clean");
		case ECameraBuildStatus::CleanWithWarnings: return TEXT("CleanWithWarnings");
		case ECameraBuildStatus::WithErrors:        return TEXT("WithErrors");
		case ECameraBuildStatus::Dirty:             return TEXT("Dirty");
		default:                                    return TEXT("Unknown");
		}
	}
#endif
}

bool FCUCameraRigAuthor::Execute(const FCUCameraRigAuthorParams& In, FCUCameraRigAuthorResult& Out)
{
	Out = FCUCameraRigAuthorResult{};

#if !WITH_NARSHAMCP_GAMEPLAYCAMERAS
	Out.Error = TEXT("GameplayCameras plugin unavailable");
	Out.ErrorCode = TEXT("gameplay_cameras_plugin_unavailable");
	Out.Hint = TEXT("Enable GameplayCameras in your .uproject, then rebuild.");
	return false;
#else

	if (In.RigPackagePath.IsEmpty())
	{
		Out.Error = TEXT("RigPackagePath is empty");
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}

	if (!FPackageName::IsValidLongPackageName(In.RigPackagePath))
	{
		Out.Error = FString::Printf(TEXT("Invalid long package name: %s"), *In.RigPackagePath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}

	const FString PackageName = In.RigPackagePath;
	const FString AssetName = FPackageName::GetShortName(PackageName);

	// Overwrite is intentional for a re-runnable spike, but make it loud rather than silent:
	// CreatePackage + same-name NewObject below replaces any existing asset at this path.
	if (FPackageName::DoesPackageExist(PackageName))
	{
		UE_LOG(LogNarshaMCP, Warning,
			TEXT("[CameraRigAuthor] Overwriting existing asset at %s"), *PackageName);
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Out.Error = FString::Printf(TEXT("CreatePackage failed for %s"), *PackageName);
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}
	Package->FullyLoad();

	// RF_Transactional matches the engine's own UCameraRigAssetFactory so the asset
	// participates in undo/redo when this same helper later backs a live-editor UFUNCTION.
	UCameraRigAsset* Rig =
		NewObject<UCameraRigAsset>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Rig)
	{
		Out.Error = TEXT("NewObject<UCameraRigAsset> returned null");
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}

	// Author a single OffsetCameraNode as the minimal root graph. The node is outered to
	// the rig because RootNode is an Instanced subobject.
	UOffsetCameraNode* OffsetNode = NewObject<UOffsetCameraNode>(Rig);
	if (!OffsetNode)
	{
		Out.Error = TEXT("NewObject<UOffsetCameraNode> returned null");
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}
	// TranslationOffset is an EditAnywhere node param — set its underlying value directly.
	OffsetNode->TranslationOffset.Value = In.TranslationOffset;

	// ── THE reflection-blocked write ──
	// RootNode is `UPROPERTY(Instanced)` with no EditAnywhere/BlueprintReadWrite flag, so this assignment
	// is unreachable through any reflection automation path. This single line is the spike.
	Rig->RootNode = OffsetNode;

	// Build the rig graph via the public engine API.
	Rig->BuildCameraRig();

	const ECameraBuildStatus Status = Rig->GetBuildStatus();
	Out.BuildStatus = BuildStatusToString(Status);
	Out.RootNodeClass = Rig->GetRootNode() ? Rig->GetRootNode()->GetClass()->GetName() : TEXT("None");

	if (Rig->GetRootNode() == nullptr)
	{
		Out.Error = TEXT("RootNode is null after assignment (reflection-bypass write did not persist)");
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}
	if (Status != ECameraBuildStatus::Clean && Status != ECameraBuildStatus::CleanWithWarnings)
	{
		Out.Error = FString::Printf(TEXT("BuildCameraRig did not converge (status=%s)"), *Out.BuildStatus);
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}

	// Persist the authored asset to disk.
	FAssetRegistryModule::AssetCreated(Rig);
	Rig->MarkPackageDirty();

	const FString FileName =
		FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());

	// Ensure the target Content subtree exists (a fresh /Game/.../Spike/ path may have no
	// on-disk dir yet); SavePackage usually creates it, but not guaranteed across sandboxes.
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), /*Tree=*/true);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError; // route SavePackage failure details to the log (W3)
	const bool bSaved = UPackage::SavePackage(Package, Rig, *FileName, SaveArgs);
	if (!bSaved)
	{
		Out.Error = FString::Printf(TEXT("SavePackage failed for %s"), *FileName);
		UE_LOG(LogNarshaMCP, Error, TEXT("[CameraRigAuthor] %s"), *Out.Error);
		return false;
	}

	Out.SavedFilePath = FileName;
	Out.bSuccess = true;
	UE_LOG(LogNarshaMCP, Display,
		TEXT("[CameraRigAuthor] Authored %s (root=%s, build=%s) -> %s"),
		*PackageName, *Out.RootNodeClass, *Out.BuildStatus, *FileName);
	return true;
#endif
}
