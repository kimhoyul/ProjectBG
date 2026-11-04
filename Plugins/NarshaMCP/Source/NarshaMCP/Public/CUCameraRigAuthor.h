// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16577: Domain C++ authoring for reflection-blocked UPROPERTY areas (spike).

#pragma once

#include "CoreMinimal.h"

/**
 * Inputs for FCUCameraRigAuthor::Execute.
 *
 * Plain C++ (non-USTRUCT) on purpose: this is an intra-module helper contract shared
 * by the commandlet (headless) and a future Remote Control UFUNCTION (live editor),
 * neither of which needs reflection on the parameter bag itself.
 */
struct FCUCameraRigAuthorParams
{
	/** Long package name for the authored rig, e.g. "/Game/NarshaMCP/Spike/CR_Authored".
	 *  An existing asset at this path is overwritten (a warning is logged). */
	FString RigPackagePath;

	/** Translation offset written into the root UOffsetCameraNode (an EditAnywhere node param). */
	FVector TranslationOffset = FVector::ZeroVector;
};

/** Result of an authoring run; serialized to JSON by the commandlet for machine verification. */
struct FCUCameraRigAuthorResult
{
	bool bSuccess = false;
	/** Class name of the resolved root node after the build (expected "OffsetCameraNode"). */
	FString RootNodeClass;
	/** ECameraBuildStatus as a string: Clean / CleanWithWarnings / WithErrors / Dirty. */
	FString BuildStatus;
	/** On-disk path of the saved .uasset (empty on failure). */
	FString SavedFilePath;
	/** Human-readable failure reason (empty on success). */
	FString Error;
	/** Stable machine-readable decline code (empty for ordinary authoring failures). */
	FString ErrorCode;
	/** Actionable recovery hint (empty for ordinary authoring failures). */
	FString Hint;
};

/**
 * Authors a minimal GameplayCamera rig from C++.
 *
 * Why this exists (Issue #16577 / Epic #16551): UCameraRigAsset::RootNode is declared
 * `UPROPERTY(Instanced)` with NO EditAnywhere/BlueprintReadWrite specifier, so it is invisible to UE
 * reflection. No reflection-based automation path — set_editor_property,
 * ObjectTools.set_properties, execute_python, or Remote Control — can attach a node to
 * the rig. Only domain C++ that touches the member directly can. This helper is the
 * representative proof of that pattern.
 *
 * The reflection-bypassing logic lives here (not in the commandlet) so the same call
 * can later back a live-editor UCURemoteLibrary UFUNCTION without duplication.
 */
class NARSHAMCP_API FCUCameraRigAuthor
{
public:
	/**
	 * Create a UCameraRigAsset, attach a single UOffsetCameraNode as its RootNode
	 * (the reflection-blocked write), build the rig, and save the package to disk.
	 *
	 * @param In   Authoring parameters (package path + offset).
	 * @param Out  Populated with the build status, root node class, saved path, or error.
	 * @return true on success (root attached, build Clean/CleanWithWarnings, package saved).
	 */
	static bool Execute(const FCUCameraRigAuthorParams& In, FCUCameraRigAuthorResult& Out);
};
