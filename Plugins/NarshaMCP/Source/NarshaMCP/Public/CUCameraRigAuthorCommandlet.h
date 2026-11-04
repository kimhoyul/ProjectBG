// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16577: Domain C++ commandlet for reflection-blocked UPROPERTY areas (spike).

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUCameraRigAuthorCommandlet.generated.h"

/**
 * Headless authoring commandlet for a GameplayCamera rig.
 *
 * Demonstrates the "domain C++ toolset" pattern for areas that reflection cannot reach:
 * UCameraRigAsset::RootNode is `UPROPERTY(Instanced)` with no EditAnywhere/BlueprintReadWrite flag, so it
 * is invisible to set_editor_property / ObjectTools / execute_python / Remote Control. This
 * commandlet attaches a node to the rig from C++ (via FCUCameraRigAuthor), which is the only
 * full-automation path. See docs/technical/spikes/16577-camera-rig-author-eval.md.
 *
 * Modeled on UCUBlueprintManipulatorCommandlet (a write commandlet): derives directly from
 * UCommandlet rather than the extraction-oriented UCUCommandletBase, because no AssetRegistry
 * full-scan wait is needed.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUCameraRigAuthor
 *     -RigPath=/Game/NarshaMCP/Spike/CR_Authored
 *     [-OffsetX=100 -OffsetY=0 -OffsetZ=0]
 *     [-OutputDir="D:/Custom/Report/Dir"]
 *     -unattended -nosplash
 *
 * Writes a JSON report (gameplaycamera_rig_author.json) to OutputDir (default:
 * <Project>/Intermediate/NarshaMCP) for machine verification. Returns 0 on success, 1 on error.
 */
UCLASS()
class UCUCameraRigAuthorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUCameraRigAuthorCommandlet();

	virtual int32 Main(const FString& Params) override;
};
