// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #28: Blueprint Modification Support - Phase 3

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUBlueprintManipulatorCommandlet.generated.h"

/**
 * Commandlet for Blueprint modification operations
 *
 * Exposes BlueprintManipulator functions via command-line for Python integration.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUBlueprintManipulator
 *     -BlueprintPath=/Game/MyBlueprint
 *     -Operation=<operation>
 *     [operation-specific parameters]
 *
 * Operations:
 *   SetPinDefault:
 *     -NodeGuid=<guid> -PinId=<guid> -Value=<string>
 *
 *   ConnectPins:
 *     -SourceNodeGuid=<guid> -SourcePinId=<guid>
 *     -TargetNodeGuid=<guid> -TargetPinId=<guid>
 *
 *   DisconnectPin:
 *     -NodeGuid=<guid> -PinId=<guid>
 *
 * Example:
 *   UnrealEditor-Cmd.exe MyProject.uproject -run=NarshaMCP.CUBlueprintManipulator
 *     -BlueprintPath=/Game/Blueprints/BP_Test
 *     -Operation=SetPinDefault
 *     -NodeGuid=12345678-1234-1234-1234-123456789012
 *     -PinId=87654321-4321-4321-4321-210987654321
 *     -Value="NewValue"
 *
 * Author: NarshaMCP v3.9.0
 * Date: 2025-10-17
 */
UCLASS()
class UCUBlueprintManipulatorCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UCUBlueprintManipulatorCommandlet();

    virtual int32 Main(const FString& Params) override;

private:
    // Operation handlers
    int32 HandleSetPinDefault(class UBlueprint* Blueprint, const FString& Params);
    int32 HandleConnectPins(class UBlueprint* Blueprint, const FString& Params);
    int32 HandleDisconnectPin(class UBlueprint* Blueprint, const FString& Params);

    // Helper: Parse GUID from params
    bool ParseGuid(const FString& Params, const FString& ParamName, FGuid& OutGuid);

    // Helper: Get string parameter
    bool GetStringParam(const FString& Params, const FString& ParamName, FString& OutValue);
};
