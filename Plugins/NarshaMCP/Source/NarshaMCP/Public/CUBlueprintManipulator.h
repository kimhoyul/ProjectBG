// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #28: Blueprint Modification Support - Phase 3

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"

class UBlueprint;
class UEdGraphNode;
class UEdGraphPin;

/**
 * Blueprint Manipulator - Helper functions for modifying Blueprints programmatically
 *
 * Provides 6 core functions for Blueprint modification:
 * 1. FindNodeByGuid() - Find node by permanent GUID
 * 2. FindPinById() - Find pin by permanent ID
 * 3. SetPinDefaultValue() - Set pin default value
 * 4. ConnectPins() - Connect two pins
 * 5. DisconnectPin() - Disconnect pin
 * 6. SaveBlueprint() - Save Blueprint to disk
 *
 * Usage from Python via subprocess:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUBlueprintManipulator
 *     -BlueprintPath=/Game/MyBlueprint
 *     -Operation=SetPinDefault
 *     -NodeGuid=12345678-1234-1234-1234-123456789012
 *     -PinId=87654321-4321-4321-4321-210987654321
 *     -Value="NewValue"
 *
 * Author: NarshaMCP v3.9.0
 * Date: 2025-10-17
 */
class NARSHAMCP_API FCUBlueprintManipulator
{
public:
    /**
     * Find a node in Blueprint by its permanent GUID
     *
     * @param Blueprint - Blueprint to search
     * @param NodeGuid - Node GUID to find
     * @return Found node or nullptr
     */
    static UEdGraphNode* FindNodeByGuid(UBlueprint* Blueprint, const FGuid& NodeGuid);

    /**
     * Find a pin in Blueprint by its permanent ID
     *
     * @param Blueprint - Blueprint to search
     * @param PinId - Pin ID to find
     * @param OutNode - [OUT] Node that owns the pin
     * @return Found pin or nullptr
     */
    static UEdGraphPin* FindPinById(UBlueprint* Blueprint, const FGuid& PinId, UEdGraphNode*& OutNode);

    /**
     * Set default value for a pin
     *
     * @param Pin - Pin to modify
     * @param NewValue - New default value as string
     * @return true if successful
     */
    static bool SetPinDefaultValue(UEdGraphPin* Pin, const FString& NewValue);

    /**
     * Connect two pins via the K2 schema (UEdGraphSchema_K2::TryCreateConnection).
     *
     * Issue #15283 / #15336: replaces the legacy hand-rolled compatibility gate +
     * raw UEdGraphPin::MakeLinkTo. Truly-incompatible pins now fail honestly
     * (CONNECT_RESPONSE_DISALLOW) instead of returning a false success, and UE may
     * auto-insert a conversion (cast) node for convertible pin pairs. Any such
     * auto-inserted node is reported back via OutAutoInsertedNodes.
     *
     * @param SourcePin - Output pin
     * @param TargetPin - Input pin
     * @param OutAutoInsertedNodes - Optional; receives any conversion node(s) the
     *        schema auto-inserted into the owning graph during this connection.
     * @param OutReplacedExistingLink - Optional; set true when the schema broke a
     *        pre-existing link on a single-input pin to make this connection
     *        (CONNECT_RESPONSE_BREAK_OTHERS_*), unlike the legacy additive link.
     * @return true if the connection was made (possibly via a conversion node)
     */
    static bool ConnectPins(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin,
        TArray<UEdGraphNode*>* OutAutoInsertedNodes = nullptr,
        bool* OutReplacedExistingLink = nullptr);

    /**
     * Disconnect a pin from all connections
     *
     * @param Pin - Pin to disconnect
     * @return Number of disconnected links
     */
    static int32 DisconnectPin(UEdGraphPin* Pin);

    /**
     * Save Blueprint to disk
     *
     * @param Blueprint - Blueprint to save
     * @param bRecompile - Whether to recompile after saving
     * @return true if successful
     */
    static bool SaveBlueprint(UBlueprint* Blueprint, bool bRecompile = true);

    /**
     * Load Blueprint from asset path
     *
     * @param BlueprintPath - Asset path (e.g., "/Game/Blueprints/MyBlueprint")
     * @return Loaded Blueprint or nullptr
     */
    static UBlueprint* LoadBlueprint(const FString& BlueprintPath);

private:
    /**
     * Helper: Log error message to stderr
     */
    static void LogError(const FString& Message);

    /**
     * Helper: Log info message to stderr
     */
    static void LogInfo(const FString& Message);
};
