// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #28: Blueprint Modification Support - Phase 3

#include "CUBlueprintManipulatorCommandlet.h"
#include "CUBlueprintManipulator.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "CULog.h"

UCUBlueprintManipulatorCommandlet::UCUBlueprintManipulatorCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UCUBlueprintManipulatorCommandlet::Main(const FString& Params)
{
    UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("=== Blueprint Manipulator Commandlet v3.9.0 ==="));

    // Parse BlueprintPath parameter
    FString BlueprintPath;
    if (!GetStringParam(Params, TEXT("BlueprintPath"), BlueprintPath))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing required parameter: -BlueprintPath"));
        return 1;
    }

    // Load Blueprint
    UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
    if (!Blueprint)
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Failed to load Blueprint: %s"), *BlueprintPath);
        return 1;
    }

    // Parse Operation parameter
    FString Operation;
    if (!GetStringParam(Params, TEXT("Operation"), Operation))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing required parameter: -Operation"));
        return 1;
    }

    UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("Operation: %s"), *Operation);

    // Execute operation
    int32 Result = 1;

    if (Operation == TEXT("SetPinDefault"))
    {
        Result = HandleSetPinDefault(Blueprint, Params);
    }
    else if (Operation == TEXT("ConnectPins"))
    {
        Result = HandleConnectPins(Blueprint, Params);
    }
    else if (Operation == TEXT("DisconnectPin"))
    {
        Result = HandleDisconnectPin(Blueprint, Params);
    }
    else
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Unknown operation: %s"), *Operation);
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("Valid operations: SetPinDefault, ConnectPins, DisconnectPin"));
        return 1;
    }

    // Save Blueprint if operation succeeded
    if (Result == 0)
    {
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("Saving Blueprint..."));
        if (FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
        {
            UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Blueprint saved successfully"));
        }
        else
        {
            UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("✗ Failed to save Blueprint"));
            return 1;
        }
    }

    return Result;
}

// Handle SetPinDefault operation
int32 UCUBlueprintManipulatorCommandlet::HandleSetPinDefault(UBlueprint* Blueprint, const FString& Params)
{
    // Parse NodeGuid
    FGuid NodeGuid;
    if (!ParseGuid(Params, TEXT("NodeGuid"), NodeGuid))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing or invalid parameter: -NodeGuid"));
        return 1;
    }

    // Parse PinId
    FGuid PinId;
    if (!ParseGuid(Params, TEXT("PinId"), PinId))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing or invalid parameter: -PinId"));
        return 1;
    }

    // Parse Value
    FString Value;
    if (!GetStringParam(Params, TEXT("Value"), Value))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing parameter: -Value"));
        return 1;
    }

    // Find pin
    UEdGraphNode* Node = nullptr;
    UEdGraphPin* Pin = FCUBlueprintManipulator::FindPinById(Blueprint, PinId, Node);

    if (!Pin)
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Pin not found: %s"), *PinId.ToString());
        return 1;
    }

    // Set default value
    if (FCUBlueprintManipulator::SetPinDefaultValue(Pin, Value))
    {
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Set pin default: %s = '%s'"), *Pin->PinName.ToString(), *Value);
        return 0;
    }
    else
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("✗ Failed to set pin default value"));
        return 1;
    }
}

// Handle ConnectPins operation
int32 UCUBlueprintManipulatorCommandlet::HandleConnectPins(UBlueprint* Blueprint, const FString& Params)
{
    // Parse SourceNodeGuid (optional - we only need PinId)
    FGuid SourceNodeGuid;
    ParseGuid(Params, TEXT("SourceNodeGuid"), SourceNodeGuid);

    // Parse SourcePinId
    FGuid SourcePinId;
    if (!ParseGuid(Params, TEXT("SourcePinId"), SourcePinId))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing or invalid parameter: -SourcePinId"));
        return 1;
    }

    // Parse TargetNodeGuid (optional)
    FGuid TargetNodeGuid;
    ParseGuid(Params, TEXT("TargetNodeGuid"), TargetNodeGuid);

    // Parse TargetPinId
    FGuid TargetPinId;
    if (!ParseGuid(Params, TEXT("TargetPinId"), TargetPinId))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing or invalid parameter: -TargetPinId"));
        return 1;
    }

    // Find pins
    UEdGraphNode* SourceNode = nullptr;
    UEdGraphPin* SourcePin = FCUBlueprintManipulator::FindPinById(Blueprint, SourcePinId, SourceNode);

    if (!SourcePin)
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Source pin not found: %s"), *SourcePinId.ToString());
        return 1;
    }

    UEdGraphNode* TargetNode = nullptr;
    UEdGraphPin* TargetPin = FCUBlueprintManipulator::FindPinById(Blueprint, TargetPinId, TargetNode);

    if (!TargetPin)
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Target pin not found: %s"), *TargetPinId.ToString());
        return 1;
    }

    // Connect pins
    if (FCUBlueprintManipulator::ConnectPins(SourcePin, TargetPin))
    {
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Connected pins: %s -> %s"),
            *SourcePin->PinName.ToString(), *TargetPin->PinName.ToString());
        return 0;
    }
    else
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("✗ Failed to connect pins"));
        return 1;
    }
}

// Handle DisconnectPin operation
int32 UCUBlueprintManipulatorCommandlet::HandleDisconnectPin(UBlueprint* Blueprint, const FString& Params)
{
    // Parse NodeGuid (optional)
    FGuid NodeGuid;
    ParseGuid(Params, TEXT("NodeGuid"), NodeGuid);

    // Parse PinId
    FGuid PinId;
    if (!ParseGuid(Params, TEXT("PinId"), PinId))
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Missing or invalid parameter: -PinId"));
        return 1;
    }

    // Find pin
    UEdGraphNode* Node = nullptr;
    UEdGraphPin* Pin = FCUBlueprintManipulator::FindPinById(Blueprint, PinId, Node);

    if (!Pin)
    {
        UE_LOG(LogNarshaMCPBlueprint, Error, TEXT("Pin not found: %s"), *PinId.ToString());
        return 1;
    }

    // Disconnect pin
    int32 DisconnectedCount = FCUBlueprintManipulator::DisconnectPin(Pin);

    if (DisconnectedCount > 0)
    {
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("✓ Disconnected pin from %d links"), DisconnectedCount);
        return 0;
    }
    else
    {
        UE_LOG(LogNarshaMCPBlueprint, Display, TEXT("Pin has no connections"));
        return 0;
    }
}

// Helper: Parse GUID from params
bool UCUBlueprintManipulatorCommandlet::ParseGuid(const FString& Params, const FString& ParamName, FGuid& OutGuid)
{
    FString GuidString;
    if (!FParse::Value(*Params, *(ParamName + TEXT("=")), GuidString))
    {
        return false;
    }

    return FGuid::Parse(GuidString, OutGuid);
}

// Helper: Get string parameter
bool UCUBlueprintManipulatorCommandlet::GetStringParam(const FString& Params, const FString& ParamName, FString& OutValue)
{
    return FParse::Value(*Params, *(ParamName + TEXT("=")), OutValue);
}
