// Copyright 2026 Next Stage Inc. All rights reserved.

#include "BlueprintMetadata/InputExtractors.h"
#include "BlueprintMetadata/BlueprintMetadataHelpers.h"
#if WITH_NARSHAMCP_ENHANCEDINPUT
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "AssetRegistry/AssetData.h"

#include "CULog.h"

namespace InputExtractors
{
    using namespace BlueprintMetadataHelpers;

    static TSharedPtr<FJsonObject> MakeEnhancedInputUnavailable()
    {
        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetBoolField(TEXT("success"), false);
        Result->SetStringField(TEXT("error"), TEXT("EnhancedInput plugin unavailable"));
        Result->SetStringField(TEXT("error_code"), TEXT("enhanced_input_plugin_unavailable"));
        Result->SetStringField(TEXT("hint"), TEXT("Enable EnhancedInput in your .uproject, then rebuild."));
        return Result;
    }

    // ============================================================================
    // Enhanced Input Support (Issue #20 Phase 7 - v3.4.0)
    // ============================================================================

    TSharedPtr<FJsonObject> ExtractInputActionMetadata(UInputAction* InputAction, const FAssetData& AssetData)
    {
#if !WITH_NARSHAMCP_ENHANCEDINPUT
        return MakeEnhancedInputUnavailable();
#else
        if (!InputAction)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][EnhancedInput] InputAction is null"));
            return nullptr;
        }

        // Basic info (using helper to reduce duplication)
        TSharedPtr<FJsonObject> IAJson = CreateBasicAssetJson(InputAction->GetName(), AssetData);

        // Value type (enum to string)
        FString ValueType;
        switch (InputAction->ValueType)
        {
            case EInputActionValueType::Boolean:
                ValueType = TEXT("Boolean");
                break;
            case EInputActionValueType::Axis1D:
                ValueType = TEXT("Axis1D");
                break;
            case EInputActionValueType::Axis2D:
                ValueType = TEXT("Axis2D");
                break;
            case EInputActionValueType::Axis3D:
                ValueType = TEXT("Axis3D");
                break;
            default:
                ValueType = TEXT("Unknown");
                break;
        }
        IAJson->SetStringField(TEXT("value_type"), ValueType);

        // Triggers (e.g., Press, Release, Hold)
        TArray<TSharedPtr<FJsonValue>> TriggersArray;
        for (UInputTrigger* Trigger : InputAction->Triggers)
        {
            if (Trigger)
            {
                TSharedPtr<FJsonObject> TriggerJson = MakeShareable(new FJsonObject);
                TriggerJson->SetStringField(TEXT("trigger_class"), Trigger->GetClass()->GetName());

                TriggersArray.Add(MakeShareable(new FJsonValueObject(TriggerJson)));
            }
        }
        IAJson->SetArrayField(TEXT("triggers"), TriggersArray);
        IAJson->SetNumberField(TEXT("trigger_count"), TriggersArray.Num());

        // Modifiers (e.g., Negate, Scale)
        TArray<TSharedPtr<FJsonValue>> ModifiersArray;
        for (UInputModifier* Modifier : InputAction->Modifiers)
        {
            if (Modifier)
            {
                TSharedPtr<FJsonObject> ModifierJson = MakeShareable(new FJsonObject);
                ModifierJson->SetStringField(TEXT("modifier_class"), Modifier->GetClass()->GetName());

                ModifiersArray.Add(MakeShareable(new FJsonValueObject(ModifierJson)));
            }
        }
        IAJson->SetArrayField(TEXT("modifiers"), ModifiersArray);
        IAJson->SetNumberField(TEXT("modifier_count"), ModifiersArray.Num());

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][EnhancedInput] InputAction: %s (ValueType: %s, Triggers: %d, Modifiers: %d)"),
               *InputAction->GetName(), *ValueType, TriggersArray.Num(), ModifiersArray.Num());

        return IAJson;
#endif
    }

    TSharedPtr<FJsonObject> ExtractInputMappingContextMetadata(UInputMappingContext* InputMappingContext, const FAssetData& AssetData)
    {
#if !WITH_NARSHAMCP_ENHANCEDINPUT
        return MakeEnhancedInputUnavailable();
#else
        if (!InputMappingContext)
        {
            UE_LOG(LogNarshaMCPMetadata, Warning, TEXT("[NarshaMCP][EnhancedInput] InputMappingContext is null"));
            return nullptr;
        }

        // Basic info (using helper to reduce duplication)
        TSharedPtr<FJsonObject> IMCJson = CreateBasicAssetJson(InputMappingContext->GetName(), AssetData);

        // Extract key mappings (the core data structure!)
        TArray<TSharedPtr<FJsonValue>> MappingsArray;
        for (const FEnhancedActionKeyMapping& Mapping : InputMappingContext->GetMappings())
        {
            TSharedPtr<FJsonObject> MappingJson = MakeShareable(new FJsonObject);

            // InputAction reference
            if (Mapping.Action)
            {
                MappingJson->SetStringField(TEXT("action_name"), Mapping.Action->GetName());
                MappingJson->SetStringField(TEXT("action_path"), Mapping.Action->GetPathName());
            }

            // Key (e.g., "LeftMouseButton", "W", "Gamepad_FaceButton_Bottom")
            MappingJson->SetStringField(TEXT("key"), Mapping.Key.ToString());

            // Triggers (mapping-specific overrides)
            TArray<TSharedPtr<FJsonValue>> TriggersArray;
            for (UInputTrigger* Trigger : Mapping.Triggers)
            {
                if (Trigger)
                {
                    TSharedPtr<FJsonObject> TriggerJson = MakeShareable(new FJsonObject);
                    TriggerJson->SetStringField(TEXT("trigger_class"), Trigger->GetClass()->GetName());

                    TriggersArray.Add(MakeShareable(new FJsonValueObject(TriggerJson)));
                }
            }
            if (TriggersArray.Num() > 0)
            {
                MappingJson->SetArrayField(TEXT("triggers"), TriggersArray);
            }

            // Modifiers (mapping-specific overrides)
            TArray<TSharedPtr<FJsonValue>> ModifiersArray;
            for (UInputModifier* Modifier : Mapping.Modifiers)
            {
                if (Modifier)
                {
                    TSharedPtr<FJsonObject> ModifierJson = MakeShareable(new FJsonObject);
                    ModifierJson->SetStringField(TEXT("modifier_class"), Modifier->GetClass()->GetName());

                    ModifiersArray.Add(MakeShareable(new FJsonValueObject(ModifierJson)));
                }
            }
            if (ModifiersArray.Num() > 0)
            {
                MappingJson->SetArrayField(TEXT("modifiers"), ModifiersArray);
            }

            MappingsArray.Add(MakeShareable(new FJsonValueObject(MappingJson)));

            UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][EnhancedInput]   Mapping: %s → %s"),
                   *Mapping.Key.ToString(), Mapping.Action ? *Mapping.Action->GetName() : TEXT("None"));
        }

        IMCJson->SetArrayField(TEXT("mappings"), MappingsArray);
        IMCJson->SetNumberField(TEXT("mapping_count"), MappingsArray.Num());

        UE_LOG(LogNarshaMCPMetadata, Display, TEXT("[NarshaMCP][EnhancedInput] InputMappingContext: %s (Mappings: %d)"),
               *InputMappingContext->GetName(), MappingsArray.Num());

        return IMCJson;
#endif
    }
}
