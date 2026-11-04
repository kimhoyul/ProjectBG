// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

class UInputAction;
class UInputMappingContext;
class FJsonObject;
struct FAssetData;

/**
 * Enhanced Input system extraction functions
 *
 * Issue #108 Phase 0: Extracted from CUBlueprintMetadataCommandlet.cpp (1938 lines → modular)
 */
namespace InputExtractors
{
    /**
     * Extract InputAction metadata (Issue #20 Phase 7 - v3.4.0)
     *
     * Extracts InputAction asset information including:
     * - Action name and path
     * - Value type (Digital/Axis1D/Axis2D/Axis3D)
     * - Triggers and Modifiers
     * - Consume Input flag
     *
     * @param InputAction - InputAction asset to extract from
     * @param AssetData - Asset registry data for the InputAction
     * @return JSON object containing InputAction metadata, or nullptr on error
     */
    TSharedPtr<FJsonObject> ExtractInputActionMetadata(UInputAction* InputAction, const FAssetData& AssetData);

    /**
     * Extract InputMappingContext metadata (Issue #20 Phase 7 - v3.4.0)
     *
     * Extracts InputMappingContext asset information including:
     * - Context name and path
     * - Key mappings (InputAction → Key bindings)
     * - Triggers and Modifiers per mapping
     *
     * @param InputMappingContext - InputMappingContext asset to extract from
     * @param AssetData - Asset registry data for the InputMappingContext
     * @return JSON object containing InputMappingContext metadata, or nullptr on error
     */
    TSharedPtr<FJsonObject> ExtractInputMappingContextMetadata(UInputMappingContext* InputMappingContext, const FAssetData& AssetData);
}
