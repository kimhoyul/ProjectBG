// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUReflectionMetadataCommandlet.generated.h"

class UScriptStruct;
class FProperty;
class FJsonObject;
class FJsonValue;
class IAssetRegistry;

/**
 * Commandlet for extracting UE Reflection System metadata
 *
 * Issue #722 Phase 1: Reflection Metadata Integration - Auxiliary Tool approach
 *
 * Purpose:
 *   Extract FProperty metadata (offset, size, type) from UScriptStruct definitions.
 *   This metadata is used to validate binary parsers, NOT to generate parsers automatically.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=ReflectionMetadata -OutputDir="Path/To/Output"
 *
 * Output:
 *   ue_struct_metadata.json - JSON file containing reflection metadata for key structs
 *
 * Extracted Metadata:
 *   - Struct name and size (GetStructureSize())
 *   - Field name, type, offset (GetOffset_ForInternal()), size
 *   - Conditional field detection (version-dependent fields)
 *   - UE version (for version comparison)
 *
 * Target Structures:
 *   - FEdGraphPinType (Blueprint Pin type information)
 *   - FEdGraphPin (Blueprint Pin complete structure)
 *   - FTextHistory_Base + subtypes (FText polymorphic serialization)
 *   - More can be added as needed
 *
 * Architecture:
 *   - Auxiliary Tool Pattern: Validation only, NOT parser generation
 *   - Top-Down Parsing Maintained: Engine source analysis → manual parser implementation
 *   - Validation Layer: Compare parser offsets vs reflection metadata
 *   - Documentation: Auto-generate struct reference docs
 *
 * Limitations:
 *   - Reflection provides struct layout, NOT serialization logic
 *   - Conditional fields (if (version >= X)) require manual Engine source analysis
 *   - Polymorphic serialization (FText History types) not handled by reflection
 *   - Variable-size fields (FString, FText) have unknown serialization size
 *
 * Integration:
 *   - Phase 1: Metadata extraction (this Commandlet)
 *   - Phase 2: Parser validation layer (MCP/tools/binary/validation/)
 *   - Phase 3: Documentation generation (scripts/generate_struct_docs.py)
 *
 * @since Issue #722 - Reflection Metadata Integration Research
 */
UCLASS()
class NARSHAMCP_API UCUReflectionMetadataCommandlet : public UCUCommandletBase
{
    GENERATED_BODY()

public:
    UCUReflectionMetadataCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
    /**
     * Run extraction logic - Overrides CommandletBase::RunExtraction()
     *
     * Performs reflection metadata extraction work.
     * Called automatically by CommandletBase::Main().
     *
     * @param OutputDir - Output directory for JSON file
     * @param Registry - Initialized AssetRegistry instance (unused, for interface compatibility)
     * @return 0 on success, non-zero on error
     */
    virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override;

public:
    /**
     * Static extraction method (for batch commandlet execution)
     *
     * Kept for backward compatibility with CUBatchCommandlet.
     *
     * @param OutputDir - Output directory for JSON file
     * @param AssetRegistry - Unused (reflection doesn't need AssetRegistry)
     * @return 0 on success, non-zero on error
     */
    static int32 RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry);

private:
    // ============================================================================
    // Struct Extraction Functions
    // ============================================================================

    /**
     * Extract metadata from a single UScriptStruct
     *
     * Iterates all FProperty fields and extracts:
     * - Field name (GetName())
     * - Field type (GetClass()->GetName() + special type handling)
     * - Field offset (GetOffset_ForInternal())
     * - Field size (GetSize())
     * - Conditional flag (heuristic: check for version-related metadata)
     *
     * @param Struct - UScriptStruct to extract (e.g., FEdGraphPinType::StaticStruct())
     * @return JSON object with struct metadata
     */
    static TSharedPtr<FJsonObject> ExtractStructMetadata(UScriptStruct* Struct);

    /**
     * Extract metadata from a single FProperty field
     *
     * Extracts detailed field information:
     * - name: Field name string
     * - type: Human-readable type name (FName, int32, bool, etc.)
     * - offset: Byte offset from struct start (GetOffset_ForInternal())
     * - size: Field size in bytes (GetSize())
     * - conditional: Whether field might be version-dependent (heuristic)
     *
     * @param Property - FProperty to extract
     * @return JSON object with field metadata
     */
    static TSharedPtr<FJsonObject> ExtractPropertyMetadata(FProperty* Property);

    /**
     * Get human-readable type name from FProperty
     *
     * Maps FProperty subclasses to readable type names:
     * - FBoolProperty → "bool"
     * - FIntProperty → "int32"
     * - FNameProperty → "FName"
     * - FStrProperty → "FString"
     * - FEnumProperty → "Enum (EnumName)"
     * - FStructProperty → "Struct (StructName)"
     * - FArrayProperty → "TArray<ElementType>"
     * - etc.
     *
     * @param Property - FProperty to analyze
     * @return Human-readable type name string
     */
    static FString GetPropertyTypeName(FProperty* Property);

    /**
     * Detect if field might be conditional (version-dependent)
     *
     * Heuristic detection based on:
     * - Metadata tags containing "Version", "Conditional", "Optional"
     * - Property name patterns (e.g., "SourceIndex" known to be version-dependent)
     * - Known conditional fields from Engine source analysis
     *
     * NOTE: This is best-effort heuristic. Engine source analysis is still required
     * for accurate conditional field detection (if (version >= X) checks).
     *
     * @param Property - FProperty to analyze
     * @param StructName - Parent struct name (for known conditional field lookup)
     * @return true if field might be conditional
     */
    static bool IsPropertyConditional(FProperty* Property, const FString& StructName);

    // ============================================================================
    // JSON Building Functions
    // ============================================================================

    /**
     * Build final JSON output with all metadata
     *
     * Creates root JSON object with:
     * - UE version (ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION)
     * - Generation timestamp
     * - Structs array with all extracted metadata
     *
     * @param StructsArray - Array of struct metadata JSON objects
     * @return Root JSON object
     */
    static TSharedPtr<FJsonObject> BuildFinalJsonObject(
        const TArray<TSharedPtr<FJsonValue>>& StructsArray
    );

    /**
     * Get current UE version string
     *
     * Formats version as "Major.Minor.Patch" (e.g., "5.6.0").
     *
     * @return UE version string
     */
    static FString GetUEVersionString();

    // ============================================================================
    // Helper Functions
    // ============================================================================

    /**
     * Get list of structs to extract metadata from
     *
     * Returns array of UScriptStruct pointers for:
     * - FEdGraphPinType (Blueprint Pin type)
     * - FEdGraphPin (Blueprint Pin complete structure)
     * - FTextHistory_Base (FText base type)
     * - FTextHistory_NamedFormat (FText History type 2)
     * - FTextHistory_OrderedFormat (FText History type 3)
     * - FTextHistory_ArgumentDataArray (FText History type 4)
     * - More can be added as needed
     *
     * @return Array of UScriptStruct pointers
     */
    static TArray<UScriptStruct*> GetTargetStructs();

    /**
     * Validate that struct is available and can be extracted
     *
     * Checks if UScriptStruct pointer is valid and has accessible fields.
     *
     * @param Struct - UScriptStruct to validate
     * @return true if struct is valid and extractable
     */
    static bool ValidateStruct(UScriptStruct* Struct);
};
