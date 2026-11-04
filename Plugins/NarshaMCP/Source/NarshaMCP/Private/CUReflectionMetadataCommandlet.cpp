// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUReflectionMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"

// Reflection system includes
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"

// Blueprint graph includes (for FEdGraphPin, FEdGraphPinType)
#include "EdGraph/EdGraphPin.h"

// FText includes (for FTextHistory types - Phase 1 only uses FEdGraphPinType)
#include "Internationalization/Text.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPReflection, Log, All);

UCUReflectionMetadataCommandlet::UCUReflectionMetadataCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

// Override CommandletBase::RunExtraction() - called by CommandletBase::Main()
int32 UCUReflectionMetadataCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
    return RunExtractionStatic(OutputDir, Registry);
}

// Static method for batch commandlet (backward compatibility)
int32 UCUReflectionMetadataCommandlet::RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
    UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata] Starting reflection metadata extraction..."));
    UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata] UE Version: %s"), *GetUEVersionString());

    // Get target structs to extract
    TArray<UScriptStruct*> TargetStructs = GetTargetStructs();
    UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata] Found %d target structs"), TargetStructs.Num());

    // Extract metadata from each struct
    TArray<TSharedPtr<FJsonValue>> StructsArray;
    int32 SuccessCount = 0;
    int32 ErrorCount = 0;

    for (UScriptStruct* Struct : TargetStructs)
    {
        if (!ValidateStruct(Struct))
        {
            UE_LOG(LogNarshaMCPReflection, Warning, TEXT("[ReflectionMetadata] Skipping invalid struct"));
            ErrorCount++;
            continue;
        }

        FString StructName = Struct->GetName();
        UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata] Extracting: %s"), *StructName);

        TSharedPtr<FJsonObject> StructJson = ExtractStructMetadata(Struct);
        if (StructJson.IsValid())
        {
            StructsArray.Add(MakeShareable(new FJsonValueObject(StructJson)));
            SuccessCount++;
            UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata]   ✓ Success: %d fields"), StructJson->GetArrayField(TEXT("fields")).Num());
        }
        else
        {
            UE_LOG(LogNarshaMCPReflection, Error, TEXT("[ReflectionMetadata]   ✗ Failed to extract metadata"));
            ErrorCount++;
        }
    }

    // Build final JSON object
    TSharedPtr<FJsonObject> RootJson = BuildFinalJsonObject(StructsArray);

    // Save JSON to file
    FString JsonPath = OutputDir / TEXT("ue_struct_metadata.json");
    FString JsonString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
    if (FJsonSerializer::Serialize(RootJson.ToSharedRef(), Writer))
    {
        if (FFileHelper::SaveStringToFile(JsonString, *JsonPath))
        {
            UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata] ✓ Saved metadata to: %s"), *JsonPath);
            UE_LOG(LogNarshaMCPReflection, Display, TEXT("[ReflectionMetadata] ✓ Success: %d structs, Error: %d structs"), SuccessCount, ErrorCount);
            return 0;  // Success
        }
        else
        {
            UE_LOG(LogNarshaMCPReflection, Error, TEXT("[ReflectionMetadata] ✗ Failed to save file: %s"), *JsonPath);
            return 1;  // File save error
        }
    }
    else
    {
        UE_LOG(LogNarshaMCPReflection, Error, TEXT("[ReflectionMetadata] ✗ Failed to serialize JSON"));
        return 1;  // JSON serialization error
    }
}

// ============================================================================
// Struct Extraction Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUReflectionMetadataCommandlet::ExtractStructMetadata(UScriptStruct* Struct)
{
    if (!Struct)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> StructJson = MakeShareable(new FJsonObject);

    // Basic struct information
    FString StructName = Struct->GetName();
    int32 StructSize = Struct->GetStructureSize();

    StructJson->SetStringField(TEXT("name"), StructName);
    StructJson->SetNumberField(TEXT("size"), StructSize);
    StructJson->SetStringField(TEXT("ue_version"), GetUEVersionString());

    // Extract all fields
    TArray<TSharedPtr<FJsonValue>> FieldsArray;
    for (TFieldIterator<FProperty> It(Struct); It; ++It)
    {
        FProperty* Property = *It;
        TSharedPtr<FJsonObject> PropertyJson = ExtractPropertyMetadata(Property);
        if (PropertyJson.IsValid())
        {
            // Add struct name for conditional field detection
            bool bConditional = IsPropertyConditional(Property, StructName);
            PropertyJson->SetBoolField(TEXT("conditional"), bConditional);

            FieldsArray.Add(MakeShareable(new FJsonValueObject(PropertyJson)));
        }
    }

    StructJson->SetArrayField(TEXT("fields"), FieldsArray);

    UE_LOG(LogNarshaMCPReflection, Verbose, TEXT("[ReflectionMetadata]   Struct: %s, Size: %d bytes, Fields: %d"),
           *StructName, StructSize, FieldsArray.Num());

    return StructJson;
}

TSharedPtr<FJsonObject> UCUReflectionMetadataCommandlet::ExtractPropertyMetadata(FProperty* Property)
{
    if (!Property)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> PropertyJson = MakeShareable(new FJsonObject);

    // Field name
    FString PropertyName = Property->GetName();
    PropertyJson->SetStringField(TEXT("name"), PropertyName);

    // Field type
    FString PropertyType = GetPropertyTypeName(Property);
    PropertyJson->SetStringField(TEXT("type"), PropertyType);

    // Field offset (byte offset from struct start)
    int32 PropertyOffset = Property->GetOffset_ForInternal();
    PropertyJson->SetNumberField(TEXT("offset"), PropertyOffset);

    // Field size (bytes)
    int32 PropertySize = Property->GetSize();
    PropertyJson->SetNumberField(TEXT("size"), PropertySize);

    UE_LOG(LogNarshaMCPReflection, VeryVerbose, TEXT("[ReflectionMetadata]     Field: %s, Type: %s, Offset: %d, Size: %d"),
           *PropertyName, *PropertyType, PropertyOffset, PropertySize);

    return PropertyJson;
}

FString UCUReflectionMetadataCommandlet::GetPropertyTypeName(FProperty* Property)
{
    if (!Property)
    {
        return TEXT("Unknown");
    }

    // Basic types
    if (Property->IsA<FBoolProperty>())
    {
        return TEXT("bool");
    }
    if (Property->IsA<FInt8Property>())
    {
        return TEXT("int8");
    }
    if (Property->IsA<FInt16Property>())
    {
        return TEXT("int16");
    }
    if (Property->IsA<FIntProperty>())
    {
        return TEXT("int32");
    }
    if (Property->IsA<FInt64Property>())
    {
        return TEXT("int64");
    }
    if (Property->IsA<FByteProperty>())
    {
        return TEXT("uint8");
    }
    if (Property->IsA<FUInt16Property>())
    {
        return TEXT("uint16");
    }
    if (Property->IsA<FUInt32Property>())
    {
        return TEXT("uint32");
    }
    if (Property->IsA<FUInt64Property>())
    {
        return TEXT("uint64");
    }
    if (Property->IsA<FFloatProperty>())
    {
        return TEXT("float");
    }
    if (Property->IsA<FDoubleProperty>())
    {
        return TEXT("double");
    }

    // Name and String types
    if (Property->IsA<FNameProperty>())
    {
        return TEXT("FName");
    }
    if (Property->IsA<FStrProperty>())
    {
        return TEXT("FString");
    }
    if (Property->IsA<FTextProperty>())
    {
        return TEXT("FText");
    }

    // Enum types
    if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
    {
        UEnum* Enum = EnumProp->GetEnum();
        FString EnumName = Enum ? Enum->GetName() : TEXT("UnknownEnum");
        return FString::Printf(TEXT("Enum (%s)"), *EnumName);
    }
    if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
    {
        // Old-style enum (UE4 legacy)
        if (ByteProp->Enum)
        {
            return FString::Printf(TEXT("Enum (%s)"), *ByteProp->Enum->GetName());
        }
    }

    // Struct types
    if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
    {
        UScriptStruct* Struct = StructProp->Struct;
        FString StructName = Struct ? Struct->GetName() : TEXT("UnknownStruct");
        return FString::Printf(TEXT("Struct (%s)"), *StructName);
    }

    // Array types
    if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
    {
        FProperty* InnerProp = ArrayProp->Inner;
        FString InnerType = GetPropertyTypeName(InnerProp);
        return FString::Printf(TEXT("TArray<%s>"), *InnerType);
    }

    // Set types
    if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
    {
        FProperty* ElementProp = SetProp->ElementProp;
        FString ElementType = GetPropertyTypeName(ElementProp);
        return FString::Printf(TEXT("TSet<%s>"), *ElementType);
    }

    // Map types
    if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
    {
        FProperty* KeyProp = MapProp->KeyProp;
        FProperty* ValueProp = MapProp->ValueProp;
        FString KeyType = GetPropertyTypeName(KeyProp);
        FString ValueType = GetPropertyTypeName(ValueProp);
        return FString::Printf(TEXT("TMap<%s, %s>"), *KeyType, *ValueType);
    }

    // Object types
    if (FObjectProperty* ObjectProp = CastField<FObjectProperty>(Property))
    {
        UClass* Class = ObjectProp->PropertyClass;
        FString ClassName = Class ? Class->GetName() : TEXT("UnknownClass");
        return FString::Printf(TEXT("UObject* (%s)"), *ClassName);
    }
    if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
    {
        UClass* MetaClass = ClassProp->MetaClass;
        FString ClassName = MetaClass ? MetaClass->GetName() : TEXT("UnknownClass");
        return FString::Printf(TEXT("TSubclassOf<%s>"), *ClassName);
    }

    // Delegate types
    if (Property->IsA<FDelegateProperty>())
    {
        return TEXT("FDelegate");
    }
    if (Property->IsA<FMulticastDelegateProperty>())
    {
        return TEXT("FMulticastDelegate");
    }

    // Interface types
    if (FInterfaceProperty* InterfaceProp = CastField<FInterfaceProperty>(Property))
    {
        UClass* InterfaceClass = InterfaceProp->InterfaceClass;
        FString InterfaceName = InterfaceClass ? InterfaceClass->GetName() : TEXT("UnknownInterface");
        return FString::Printf(TEXT("TScriptInterface<%s>"), *InterfaceName);
    }

    // Fallback: Return property class name
    return Property->GetClass()->GetName();
}

bool UCUReflectionMetadataCommandlet::IsPropertyConditional(FProperty* Property, const FString& StructName)
{
    if (!Property)
    {
        return false;
    }

    FString PropertyName = Property->GetName();

    // Known conditional fields from Engine source analysis (Issue #722)
    // These fields have version checks in their serialization code
    if (StructName == TEXT("FEdGraphPinType"))
    {
        // Phase 34.9: PinSubCategoryMemberReference added in VER_UE4_MEMBERREFERENCE_IN_PINTYPE (355)
        if (PropertyName == TEXT("PinSubCategoryMemberReference"))
        {
            return true;
        }
    }

    if (StructName == TEXT("FEdGraphPin") || StructName == TEXT("UEdGraphPin"))
    {
        // Phase 37: PinName changed serialization in EdGraphPinSourceIndex (5037)
        if (PropertyName == TEXT("PinName"))
        {
            return true;
        }
        // Phase 45: SourceIndex added in EdGraphPinSourceIndex (5037)
        if (PropertyName == TEXT("SourceIndex"))
        {
            return true;
        }
        // Phase 38: DefaultTextValue added in UE5 (TextHistoryTextFormat removed)
        if (PropertyName == TEXT("DefaultTextValue"))
        {
            return true;
        }
    }

    // Heuristic: Check metadata for version-related tags
    const FString* VersionMeta = Property->FindMetaData(TEXT("Version"));
    if (VersionMeta)
    {
        return true;
    }

    const FString* ConditionalMeta = Property->FindMetaData(TEXT("Conditional"));
    if (ConditionalMeta)
    {
        return true;
    }

    // Default: Not conditional
    return false;
}

// ============================================================================
// JSON Building Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUReflectionMetadataCommandlet::BuildFinalJsonObject(
    const TArray<TSharedPtr<FJsonValue>>& StructsArray)
{
    TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

    // UE version
    RootJson->SetStringField(TEXT("ue_version"), GetUEVersionString());

    // Generation timestamp
    FDateTime Now = FDateTime::Now();
    FString Timestamp = Now.ToString(TEXT("%Y-%m-%d %H:%M:%S"));
    RootJson->SetStringField(TEXT("generation_timestamp"), Timestamp);

    // Structs array
    RootJson->SetArrayField(TEXT("structs"), StructsArray);

    // Statistics
    RootJson->SetNumberField(TEXT("struct_count"), StructsArray.Num());

    // Documentation
    RootJson->SetStringField(TEXT("purpose"), TEXT("Reflection metadata for binary parser validation"));
    RootJson->SetStringField(TEXT("note"), TEXT("This metadata is for validation only, NOT automatic parser generation"));

    return RootJson;
}

FString UCUReflectionMetadataCommandlet::GetUEVersionString()
{
    return FString::Printf(TEXT("%d.%d.%d"),
                          ENGINE_MAJOR_VERSION,
                          ENGINE_MINOR_VERSION,
                          ENGINE_PATCH_VERSION);
}

// ============================================================================
// Helper Functions
// ============================================================================

TArray<UScriptStruct*> UCUReflectionMetadataCommandlet::GetTargetStructs()
{
    TArray<UScriptStruct*> Structs;

    // FEdGraphPinType (Blueprint Pin type information)
    UScriptStruct* PinTypeStruct = FEdGraphPinType::StaticStruct();
    if (PinTypeStruct)
    {
        Structs.Add(PinTypeStruct);
    }

    // FEdGraphPin is NOT a UScriptStruct, it's a UObject class (UEdGraphPin)
    // We can still extract its property metadata, but need different approach
    // For now, skip it (UEdGraphPin serialization is complex, handled by Commandlet)

    // FText History types (polymorphic serialization)
    // NOTE: FTextHistory types are internal Engine classes without exposed StaticStruct()
    // We can extract them if needed, but requires Engine source access
    // For Phase 1, focus on FEdGraphPinType only

    // Future additions:
    // - FMemberReference (Phase 34.9 conditional field)
    // - FGuid (common in many structs)
    // - FVector, FRotator, FTransform (common math types)

    return Structs;
}

bool UCUReflectionMetadataCommandlet::ValidateStruct(UScriptStruct* Struct)
{
    if (!Struct)
    {
        UE_LOG(LogNarshaMCPReflection, Warning, TEXT("[ReflectionMetadata] Struct pointer is null"));
        return false;
    }

    if (Struct->GetStructureSize() <= 0)
    {
        UE_LOG(LogNarshaMCPReflection, Warning, TEXT("[ReflectionMetadata] Struct '%s' has invalid size: %d"),
               *Struct->GetName(), Struct->GetStructureSize());
        return false;
    }

    // Check if struct has at least one property.
    // Test the iterator directly instead of a single-iteration loop with an
    // unconditional break: the break makes ++It unreachable and MSVC flags the
    // inlined TFieldIterator::operator++ as C4702 (UE 5.8 bWarningsAsErrors).
    const bool bHasProperties = static_cast<bool>(TFieldIterator<FProperty>(Struct));

    if (!bHasProperties)
    {
        UE_LOG(LogNarshaMCPReflection, Warning, TEXT("[ReflectionMetadata] Struct '%s' has no extractable properties"),
               *Struct->GetName());
        return false;
    }

    return true;
}
