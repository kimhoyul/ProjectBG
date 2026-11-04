// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUMaterialMetadataCommandlet.h"
#include "CUCommandletHelpers.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionPower.h"
#include "Materials/MaterialExpressionSquareRoot.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Engine/Texture.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Compression.h"  // GZIP compression support
#include "Async/ParallelFor.h"

// Hash support for incremental build
#include "IO/IoHash.h"
#include "AssetRegistry/AssetRegistryState.h"

// Material resource for HLSL code extraction
#include "MaterialShared.h"
#include "Materials/MaterialRenderProxy.h"

// Issue #4272: FMaterialInsights for direct HLSL extraction (UE 5.7+)
#include "Materials/MaterialInsights.h"

// Shader stats extraction (Issue #446)
#include "MaterialShader.h"
#include "Shader.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPMaterial, Log, All);

UCUMaterialMetadataCommandlet::UCUMaterialMetadataCommandlet(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
}

// Override CommandletBase::RunExtraction() - called by CommandletBase::Main()
int32 UCUMaterialMetadataCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
    return RunExtractionStatic(OutputDir, Registry);
}

// Static method for batch commandlet (backward compatibility)
int32 UCUMaterialMetadataCommandlet::RunExtractionStatic(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Running extraction..."));

    // Find all Material assets (Material + MaterialInstance + subtypes)
    FARFilter MaterialFilter;
    MaterialFilter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
    MaterialFilter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
    MaterialFilter.bRecursiveClasses = true;

    TArray<FAssetData> MaterialAssetList;
    AssetRegistry.GetAssets(MaterialFilter, MaterialAssetList);

    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Found %d Material assets"), MaterialAssetList.Num());

    // ============================================================================
    // Issue #4272: Support filtering by material name or path
    // -MaterialFilter=M_Ivy,M_Kite → filter by asset name (comma-separated)
    // -MaterialPath=/Game/Materials/ → filter by package path prefix
    // ============================================================================
    {
        FString FilterParam;
        if (FParse::Value(FCommandLine::Get(), TEXT("-MaterialFilter="), FilterParam))
        {
            TArray<FString> FilterNames;
            FilterParam.ParseIntoArray(FilterNames, TEXT(","), true);

            if (FilterNames.Num() > 0)
            {
                TArray<FAssetData> FilteredList;
                for (const FAssetData& Asset : MaterialAssetList)
                {
                    FString AssetName = Asset.AssetName.ToString();
                    for (const FString& Name : FilterNames)
                    {
                        if (AssetName.Equals(Name.TrimStartAndEnd(), ESearchCase::IgnoreCase))
                        {
                            FilteredList.Add(Asset);
                            break;
                        }
                    }
                }
                UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] MaterialFilter: %d / %d matched '%s'"),
                    FilteredList.Num(), MaterialAssetList.Num(), *FilterParam);
                MaterialAssetList = MoveTemp(FilteredList);
            }
        }

        FString PathParam;
        if (FParse::Value(FCommandLine::Get(), TEXT("-MaterialPath="), PathParam))
        {
            TArray<FAssetData> FilteredList;
            for (const FAssetData& Asset : MaterialAssetList)
            {
                FString PackagePath = Asset.PackageName.ToString();
                if (PackagePath.StartsWith(PathParam))
                {
                    FilteredList.Add(Asset);
                }
            }
            UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] MaterialPath: %d / %d matched '%s'"),
                FilteredList.Num(), MaterialAssetList.Num(), *PathParam);
            MaterialAssetList = MoveTemp(FilteredList);
        }
    }

    // ============================================================================
    // Issue #224 Phase 1: Incremental build support (Blueprint pattern reuse)
    // ============================================================================
    FString JsonPath = OutputDir / TEXT("material_metadata.json");
    TSharedPtr<FJsonObject> ExistingJson = LoadExistingJson(JsonPath);
    bool bIncrementalMode = ExistingJson.IsValid();

    TArray<FAssetData> AssetsToProcess;
    if (bIncrementalMode)
    {
        // Incremental build: Only process changed Materials
        AssetsToProcess = FindChangedMaterials(MaterialAssetList, ExistingJson, AssetRegistry);
        UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Incremental mode: Processing %d / %d changed Materials"),
               AssetsToProcess.Num(), MaterialAssetList.Num());
    }
    else
    {
        // Full build: Process all Materials
        AssetsToProcess = MaterialAssetList;
        UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Full build mode: Processing all %d Materials"),
               AssetsToProcess.Num());
    }

    // Root JSON object
    TSharedPtr<FJsonObject> RootJson = ExistingJson.IsValid() ? ExistingJson : MakeShareable(new FJsonObject);
    TArray<TSharedPtr<FJsonValue>> MaterialsArray;
    TArray<TSharedPtr<FJsonValue>> MaterialInstancesArray;

    // Thread-safe counters
    TAtomic<int32> ProcessedCount(0);
    TAtomic<int32> ErrorCount(0);
    TAtomic<int32> InstanceProcessedCount(0);
    TAtomic<int32> InstanceErrorCount(0);

    // Thread-safe JSON arrays (protected by FCriticalSection)
    FCriticalSection MaterialsArrayLock;
    FCriticalSection MaterialInstancesArrayLock;

    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Processing %d Material assets..."), AssetsToProcess.Num());

    FDateTime StartTime = FDateTime::UtcNow();
    int32 TotalCount = AssetsToProcess.Num();
    int32 ProgressStep = FMath::Max(1, TotalCount / 10);  // 10% intervals

    // Sequential processing (Material compilation requires GameThread)
    for (int32 Index = 0; Index < AssetsToProcess.Num(); ++Index)
    {
        const FAssetData& AssetData = AssetsToProcess[Index];

        // Progress reporting
        if ((Index + 1) % ProgressStep == 0 || Index == TotalCount - 1)
        {
            float Progress = (Index + 1) * 100.0f / TotalCount;
            UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Progress: %d/%d (%.1f%%)"),
                   Index + 1, TotalCount, Progress);
        }

        // Load Material asset
        UObject* Asset = AssetData.GetAsset();
        if (!Asset)
        {
            UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Failed to load asset: %s"), *AssetData.GetObjectPathString());
            ErrorCount++;
            continue;
        }

        // Check asset type and route to appropriate extraction function
        UMaterial* Material = Cast<UMaterial>(Asset);
        UMaterialInstance* MaterialInstance = Cast<UMaterialInstance>(Asset);

        TSharedPtr<FJsonObject> AssetJson;

        if (Material)
        {
            // Extract Material metadata
            AssetJson = ExtractMaterialMetadata(Material, AssetData, AssetRegistry);
            if (AssetJson.IsValid())
            {
                FScopeLock Lock(&MaterialsArrayLock);
                MaterialsArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
                ProcessedCount++;
            }
            else
            {
                UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Failed to extract Material metadata: %s"), *AssetData.AssetName.ToString());
                ErrorCount++;
            }
        }
        else if (MaterialInstance)
        {
            // Extract Material Instance metadata
            AssetJson = ExtractMaterialInstanceMetadata(MaterialInstance, AssetData, AssetRegistry);
            if (AssetJson.IsValid())
            {
                FScopeLock Lock(&MaterialInstancesArrayLock);
                MaterialInstancesArray.Add(MakeShareable(new FJsonValueObject(AssetJson)));
                InstanceProcessedCount++;
            }
            else
            {
                UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Failed to extract MaterialInstance metadata: %s"), *AssetData.AssetName.ToString());
                InstanceErrorCount++;
            }
        }
        else
        {
            UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Unknown asset type: %s"), *AssetData.AssetClassPath.ToString());
            ErrorCount++;
        }
    }

    FTimespan Duration = FDateTime::UtcNow() - StartTime;
    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Processing complete in %.2fs"), Duration.GetTotalSeconds());
    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Materials: %d processed, %d errors"), ProcessedCount.Load(), ErrorCount.Load());
    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Instances: %d processed, %d errors"), InstanceProcessedCount.Load(), InstanceErrorCount.Load());

    // ============================================================================
    // Build final JSON and save
    // ============================================================================
    TSharedPtr<FJsonObject> FinalJson;

    if (bIncrementalMode)
    {
        // Merge with existing JSON
        MergeJsonData(RootJson, MaterialsArray, MaterialInstancesArray);
        FinalJson = RootJson;
    }
    else
    {
        // Build new JSON from scratch
        FinalJson = BuildFinalJsonObject(
            MaterialsArray,
            MaterialInstancesArray,
            ProcessedCount.Load(),
            ErrorCount.Load(),
            InstanceProcessedCount.Load(),
            InstanceErrorCount.Load()
        );
    }

    // Save JSON with GZIP compression
    bool bSaved = SaveJsonWithMetadata(JsonPath, FinalJson, bIncrementalMode);

    if (bSaved)
    {
        UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Successfully saved to: %s"), *JsonPath);
        return 0;
    }
    else
    {
        UE_LOG(LogNarshaMCPMaterial, Error, TEXT("[MaterialMetadata] Failed to save JSON file"));
        return 1;
    }
}

// ============================================================================
// Material Extraction Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::ExtractMaterialMetadata(
    UMaterial* Material,
    const FAssetData& AssetData,
    IAssetRegistry& AssetRegistry)
{
    if (!Material)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> MaterialJson = CreateBasicAssetJson(AssetData.AssetName.ToString(), AssetData, AssetRegistry);

    // Set Material-specific fields
    MaterialJson->SetStringField(TEXT("type"), TEXT("Material"));
    MaterialJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());

    // Extract MaterialExpression nodes
    TArray<TSharedPtr<FJsonValue>> NodesArray = ExtractMaterialNodes(Material);
    MaterialJson->SetArrayField(TEXT("nodes"), NodesArray);
    MaterialJson->SetNumberField(TEXT("node_count"), NodesArray.Num());

    // Extract pin connections
    TArray<TSharedPtr<FJsonValue>> ConnectionsArray = ExtractMaterialConnections(Material);
    MaterialJson->SetArrayField(TEXT("connections"), ConnectionsArray);

    // Extract Material inputs (BaseColor, Roughness, etc.)
    TArray<TSharedPtr<FJsonValue>> InputsArray = ExtractMaterialInputs(Material);
    MaterialJson->SetArrayField(TEXT("material_inputs"), InputsArray);

    // Build Material Instance hierarchy
    FString MaterialPath = AssetData.GetObjectPathString();
    TArray<TSharedPtr<FJsonValue>> InstancesArray = BuildMaterialInstanceHierarchy(MaterialPath, AssetRegistry);
    MaterialJson->SetArrayField(TEXT("instances"), InstancesArray);

    // Extract HLSL code (may be empty if Material not compiled)
    FString HLSLCode = ExtractHLSLCode(Material);
    if (!HLSLCode.IsEmpty())
    {
        MaterialJson->SetStringField(TEXT("hlsl_code"), HLSLCode);
    }

    // Analyze performance
    TSharedPtr<FJsonObject> PerformanceJson = AnalyzeMaterialPerformance(Material);
    if (PerformanceJson.IsValid())
    {
        MaterialJson->SetObjectField(TEXT("performance"), PerformanceJson);
    }

    // Generate optimization suggestions
    TArray<TSharedPtr<FJsonValue>> SuggestionsArray = GenerateOptimizationSuggestions(Material);
    if (SuggestionsArray.Num() > 0)
    {
        MaterialJson->SetArrayField(TEXT("optimization_suggestions"), SuggestionsArray);
    }

    return MaterialJson;
}

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::ExtractMaterialInstanceMetadata(
    UMaterialInstance* MaterialInstance,
    const FAssetData& AssetData,
    IAssetRegistry& AssetRegistry)
{
    if (!MaterialInstance)
    {
        return nullptr;
    }

    TSharedPtr<FJsonObject> InstanceJson = CreateBasicAssetJson(AssetData.AssetName.ToString(), AssetData, AssetRegistry);

    // Set Material Instance-specific fields
    InstanceJson->SetStringField(TEXT("type"), TEXT("MaterialInstance"));
    InstanceJson->SetStringField(TEXT("class"), AssetData.AssetClassPath.ToString());

    // Get parent Material
    UMaterialInterface* Parent = MaterialInstance->Parent;
    if (Parent)
    {
        InstanceJson->SetStringField(TEXT("parent"), Parent->GetPathName());
    }
    else
    {
        InstanceJson->SetStringField(TEXT("parent"), TEXT(""));
    }

    // Extract parameter overrides
    TSharedPtr<FJsonObject> ParametersJson = ExtractMaterialInstanceParameters(MaterialInstance);
    if (ParametersJson.IsValid())
    {
        InstanceJson->SetObjectField(TEXT("overrides"), ParametersJson);
    }

    // Build child hierarchy
    FString InstancePath = AssetData.GetObjectPathString();
    TArray<TSharedPtr<FJsonValue>> ChildrenArray = BuildMaterialInstanceHierarchy(InstancePath, AssetRegistry);
    InstanceJson->SetArrayField(TEXT("children"), ChildrenArray);

    return InstanceJson;
}

/**
 * Extracts all MaterialExpression nodes from a Material as JSON.
 *
 * @param Material The Material to extract nodes from
 * @return Array of JSON objects, each representing a MaterialExpression node
 */
TArray<TSharedPtr<FJsonValue>> UCUMaterialMetadataCommandlet::ExtractMaterialNodes(UMaterial* Material)
{
    TArray<TSharedPtr<FJsonValue>> NodesArray;

    if (!Material)
    {
        return NodesArray;
    }

    // Get all MaterialExpression nodes
    // Issue #224 UE 5.6 Compat: GetExpressions() returns TArrayView in UE 5.6, TArray& in UE 5.5
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
    TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();
#else
    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();
#endif

    for (int32 Index = 0; Index < Expressions.Num(); ++Index)
    {
        UMaterialExpression* Expression = Expressions[Index];
        if (!Expression)
        {
            continue;
        }

        TSharedPtr<FJsonObject> NodeJson = MakeShareable(new FJsonObject);

        // Generate deterministic GUID
        FString NodeGUID = GetMaterialExpressionGUID(Expression, Material);
        NodeJson->SetStringField(TEXT("guid"), NodeGUID);

        // Node type
        FString NodeType = GetMaterialExpressionTypeName(Expression);
        NodeJson->SetStringField(TEXT("type"), NodeType);

        // Node description (if available)
        FString Description = Expression->Desc;
        if (!Description.IsEmpty())
        {
            NodeJson->SetStringField(TEXT("description"), Description);
        }

        // Node position (editor-only data)
#if WITH_EDITORONLY_DATA
        TSharedPtr<FJsonObject> PositionJson = MakeShareable(new FJsonObject);
        PositionJson->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
        PositionJson->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
        NodeJson->SetObjectField(TEXT("position"), PositionJson);
#endif

        // Extract type-specific parameters
        TSharedPtr<FJsonObject> ParametersJson = ExtractMaterialExpressionParameters(Expression);
        if (ParametersJson.IsValid() && ParametersJson->Values.Num() > 0)
        {
            NodeJson->SetObjectField(TEXT("parameters"), ParametersJson);
        }

        // Extract output pins (most MaterialExpressions have at least one output)
        TArray<TSharedPtr<FJsonValue>> OutputsArray;
        TArray<FExpressionOutput>& Outputs = Expression->GetOutputs();
        for (const FExpressionOutput& Output : Outputs)
        {
            TSharedPtr<FJsonObject> OutputJson = MakeShareable(new FJsonObject);
            OutputJson->SetStringField(TEXT("name"), Output.OutputName.ToString());
            // Note: Output type information is not readily available in FExpressionOutput
            // We could infer from node type, but that's complex - leaving as "unknown" for now
            OutputsArray.Add(MakeShareable(new FJsonValueObject(OutputJson)));
        }
        if (OutputsArray.Num() > 0)
        {
            NodeJson->SetArrayField(TEXT("outputs"), OutputsArray);
        }

        NodesArray.Add(MakeShareable(new FJsonValueObject(NodeJson)));
    }

    return NodesArray;
}

TArray<TSharedPtr<FJsonValue>> UCUMaterialMetadataCommandlet::ExtractMaterialConnections(UMaterial* Material)
{
    TArray<TSharedPtr<FJsonValue>> ConnectionsArray;

    if (!Material)
    {
        return ConnectionsArray;
    }

    // Build a map of Expression pointer → GUID for quick lookup
    TMap<UMaterialExpression*, FString> ExpressionToGUID;
    // Issue #224 UE 5.6 Compat: GetExpressions() returns TArrayView in UE 5.6, TArray& in UE 5.5
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
    TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();
#else
    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();
#endif

    for (UMaterialExpression* Expression : Expressions)
    {
        if (Expression)
        {
            FString GUID = GetMaterialExpressionGUID(Expression, Material);
            ExpressionToGUID.Add(Expression, GUID);
        }
    }

    // Iterate through all expressions and extract their input connections
    for (UMaterialExpression* Expression : Expressions)
    {
        if (!Expression)
        {
            continue;
        }

        FString ToNodeGUID = ExpressionToGUID.FindRef(Expression);

        // Get all FExpressionInput properties via reflection
        // Note: MaterialExpressions have various typed inputs (e.g., A, B for Multiply)
        // We need to use reflection to find all FExpressionInput members

        for (TFieldIterator<FProperty> PropIt(Expression->GetClass()); PropIt; ++PropIt)
        {
            FProperty* Property = *PropIt;

            // Check if property is FExpressionInput (or FColorMaterialInput, FScalarMaterialInput, etc.)
            if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
            {
                // Check if struct is FExpressionInput or derived
                // Issue #224 UE 5.6 Compat: FExpressionInput::StaticStruct() removed in UE 5.6
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
                // UE 5.6: Use name-based check
                static const FName ExpressionInputName("ExpressionInput");
                FName StructName = StructProp->Struct->GetFName();
                bool bIsExpressionInput = (StructName == ExpressionInputName ||
                                          StructProp->Struct->GetName().Contains(TEXT("MaterialInput")));
                if (bIsExpressionInput)
#else
                // UE 5.5: Use StaticStruct() reflection
                if (StructProp->Struct->IsChildOf(FExpressionInput::StaticStruct()))
#endif
                {
                    FExpressionInput* Input = StructProp->ContainerPtrToValuePtr<FExpressionInput>(Expression);

                    if (Input && Input->Expression)
                    {
                        // Found a connection!
                        FString FromNodeGUID = ExpressionToGUID.FindRef(Input->Expression);

                        if (!FromNodeGUID.IsEmpty())
                        {
                            TSharedPtr<FJsonObject> ConnectionJson = MakeShareable(new FJsonObject);
                            ConnectionJson->SetStringField(TEXT("from_node"), FromNodeGUID);
                            ConnectionJson->SetStringField(TEXT("from_pin"), FString::Printf(TEXT("Output%d"), Input->OutputIndex));
                            ConnectionJson->SetStringField(TEXT("to_node"), ToNodeGUID);
                            ConnectionJson->SetStringField(TEXT("to_pin"), Property->GetName());

                            ConnectionsArray.Add(MakeShareable(new FJsonValueObject(ConnectionJson)));
                        }
                    }
                }
            }
        }
    }

    return ConnectionsArray;
}

TArray<TSharedPtr<FJsonValue>> UCUMaterialMetadataCommandlet::ExtractMaterialInputs(UMaterial* Material)
{
    TArray<TSharedPtr<FJsonValue>> InputsArray;

    if (!Material)
    {
        return InputsArray;
    }

    // Build Expression → GUID map
    TMap<UMaterialExpression*, FString> ExpressionToGUID;
    // Issue #224 UE 5.6 Compat: GetExpressions() returns TArrayView in UE 5.6, TArray& in UE 5.5
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
    TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();
#else
    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();
#endif

    for (UMaterialExpression* Expression : Expressions)
    {
        if (Expression)
        {
            FString GUID = GetMaterialExpressionGUID(Expression, Material);
            ExpressionToGUID.Add(Expression, GUID);
        }
    }

    // Extract Material input connections (BaseColor, Roughness, etc.)
    // Material properties are defined in EMaterialProperty enum

    auto ExtractMaterialInput = [&](const TCHAR* InputName, FExpressionInput* Input)
    {
        if (Input && Input->Expression)
        {
            FString NodeGUID = ExpressionToGUID.FindRef(Input->Expression);
            if (!NodeGUID.IsEmpty())
            {
                TSharedPtr<FJsonObject> InputJson = MakeShareable(new FJsonObject);
                InputJson->SetStringField(TEXT("input_name"), InputName);

                TSharedPtr<FJsonObject> ConnectedToJson = MakeShareable(new FJsonObject);
                ConnectedToJson->SetStringField(TEXT("node_guid"), NodeGUID);
                ConnectedToJson->SetStringField(TEXT("output_name"), FString::Printf(TEXT("Output%d"), Input->OutputIndex));

                InputJson->SetObjectField(TEXT("connected_to"), ConnectedToJson);
                InputsArray.Add(MakeShareable(new FJsonValueObject(InputJson)));
            }
        }
    };

    // Extract all major Material inputs
    ExtractMaterialInput(TEXT("BaseColor"), Material->GetExpressionInputForProperty(MP_BaseColor));
    ExtractMaterialInput(TEXT("Metallic"), Material->GetExpressionInputForProperty(MP_Metallic));
    ExtractMaterialInput(TEXT("Specular"), Material->GetExpressionInputForProperty(MP_Specular));
    ExtractMaterialInput(TEXT("Roughness"), Material->GetExpressionInputForProperty(MP_Roughness));
    ExtractMaterialInput(TEXT("Anisotropy"), Material->GetExpressionInputForProperty(MP_Anisotropy));
    ExtractMaterialInput(TEXT("EmissiveColor"), Material->GetExpressionInputForProperty(MP_EmissiveColor));
    ExtractMaterialInput(TEXT("Opacity"), Material->GetExpressionInputForProperty(MP_Opacity));
    ExtractMaterialInput(TEXT("OpacityMask"), Material->GetExpressionInputForProperty(MP_OpacityMask));
    ExtractMaterialInput(TEXT("Normal"), Material->GetExpressionInputForProperty(MP_Normal));
    ExtractMaterialInput(TEXT("Tangent"), Material->GetExpressionInputForProperty(MP_Tangent));
    ExtractMaterialInput(TEXT("WorldPositionOffset"), Material->GetExpressionInputForProperty(MP_WorldPositionOffset));
    ExtractMaterialInput(TEXT("SubsurfaceColor"), Material->GetExpressionInputForProperty(MP_SubsurfaceColor));
    ExtractMaterialInput(TEXT("AmbientOcclusion"), Material->GetExpressionInputForProperty(MP_AmbientOcclusion));
    ExtractMaterialInput(TEXT("Refraction"), Material->GetExpressionInputForProperty(MP_Refraction));

    return InputsArray;
}

TArray<TSharedPtr<FJsonValue>> UCUMaterialMetadataCommandlet::BuildMaterialInstanceHierarchy(
    const FString& MaterialPath,
    IAssetRegistry& AssetRegistry)
{
    TArray<TSharedPtr<FJsonValue>> InstancesArray;

    // Find all Material Instances that reference this Material as parent
    FARFilter Filter;
    Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
    Filter.bRecursiveClasses = true;

    TArray<FAssetData> AllInstances;
    AssetRegistry.GetAssets(Filter, AllInstances);

    // Filter instances that have this Material as parent
    for (const FAssetData& InstanceData : AllInstances)
    {
        // Get Parent tag value
        FAssetTagValueRef ParentTagValue = InstanceData.TagsAndValues.FindTag(FName("Parent"));
        if (ParentTagValue.IsSet())
        {
            FString ParentPath = ParentTagValue.GetValue();

            // Check if this instance's parent matches our Material
            if (ParentPath.Equals(MaterialPath, ESearchCase::IgnoreCase))
            {
                // Add to instances array
                InstancesArray.Add(MakeShareable(new FJsonValueString(InstanceData.GetObjectPathString())));
            }
        }
    }

    return InstancesArray;
}

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::ExtractMaterialInstanceParameters(UMaterialInstance* MaterialInstance)
{
    TSharedPtr<FJsonObject> ParametersJson = MakeShareable(new FJsonObject);

    if (!MaterialInstance)
    {
        return ParametersJson;
    }

    // Extract Scalar Parameters
    UMaterialInstanceConstant* ConstantInstance = Cast<UMaterialInstanceConstant>(MaterialInstance);
    if (ConstantInstance)
    {
        // Scalar parameters
        for (const FScalarParameterValue& ScalarParam : ConstantInstance->ScalarParameterValues)
        {
            TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
            ParamJson->SetNumberField(TEXT("value"), ScalarParam.ParameterValue);
            ParamJson->SetStringField(TEXT("type"), TEXT("Scalar"));
            ParametersJson->SetObjectField(ScalarParam.ParameterInfo.Name.ToString(), ParamJson);
        }

        // Vector parameters
        for (const FVectorParameterValue& VectorParam : ConstantInstance->VectorParameterValues)
        {
            TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
            FLinearColor Value = VectorParam.ParameterValue;
            FString ValueStr = FString::Printf(TEXT("(R=%.3f,G=%.3f,B=%.3f,A=%.3f)"),
                                              Value.R, Value.G, Value.B, Value.A);
            ParamJson->SetStringField(TEXT("value"), ValueStr);
            ParamJson->SetStringField(TEXT("type"), TEXT("Vector"));
            ParametersJson->SetObjectField(VectorParam.ParameterInfo.Name.ToString(), ParamJson);
        }

        // Texture parameters
        for (const FTextureParameterValue& TextureParam : ConstantInstance->TextureParameterValues)
        {
            TSharedPtr<FJsonObject> ParamJson = MakeShareable(new FJsonObject);
            if (TextureParam.ParameterValue)
            {
                ParamJson->SetStringField(TEXT("value"), TextureParam.ParameterValue->GetPathName());
            }
            else
            {
                ParamJson->SetStringField(TEXT("value"), TEXT(""));
            }
            ParamJson->SetStringField(TEXT("type"), TEXT("Texture"));
            ParametersJson->SetObjectField(TextureParam.ParameterInfo.Name.ToString(), ParamJson);
        }
    }

    return ParametersJson;
}

FString UCUMaterialMetadataCommandlet::ExtractHLSLCode(UMaterial* Material)
{
    if (!Material)
    {
        return TEXT("");
    }

#if WITH_EDITOR
    // Issue #4272: Engine API-Based HLSL Extraction (UE 5.7+)
    // Multi-method approach with commandlet-safe platform fallbacks

    FString MaterialName = Material->GetName();

    // Method 1: Try FMaterialInsights first (independent of FMaterialResource)
    // FMaterialInsights is populated during material translation in Editor
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
    if (UMaterialInterface* MaterialInterface = Cast<UMaterialInterface>(Material))
    {
        if (const FMaterialInsights* Insights = MaterialInterface->MaterialInsight.Get())
        {
            if (!Insights->NewHLSLCode.IsEmpty())
            {
                UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Extracted HLSL via FMaterialInsights::NewHLSLCode for %s (%d chars)"),
                    *MaterialName, Insights->NewHLSLCode.Len());
                return Insights->NewHLSLCode;
            }
            if (!Insights->LegacyHLSLCode.IsEmpty())
            {
                UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Extracted HLSL via FMaterialInsights::LegacyHLSLCode for %s (%d chars)"),
                    *MaterialName, Insights->LegacyHLSLCode.Len());
                return Insights->LegacyHLSLCode;
            }

            // Try template parameters (intermediate HLSL snippets)
            const TMap<FString, FString>& TemplateParams = Insights->New_ShaderStringParameters;
            if (TemplateParams.Num() > 0)
            {
                FString HLSLCode;
                for (const TPair<FString, FString>& Pair : TemplateParams)
                {
                    HLSLCode += FString::Printf(TEXT("// === %s ===\n"), *Pair.Key);
                    HLSLCode += Pair.Value;
                    HLSLCode += TEXT("\n\n");
                }
                if (!HLSLCode.IsEmpty())
                {
                    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Extracted HLSL via TemplateParams for %s (%d chars)"),
                        *MaterialName, HLSLCode.Len());
                    return HLSLCode;
                }
            }
        }
    }
#endif

    // Method 2: Try GetMaterialResource with multiple platform fallbacks
    // In commandlet mode, GMaxRHIFeatureLevel may be uninitialized
    FMaterialResource* MaterialResource = nullptr;

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
    // Try current platform first
    {
        EShaderPlatform CurrentPlatform = GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel);
        MaterialResource = Material->GetMaterialResource(CurrentPlatform);
    }

    // Fallback: try common shader platforms explicitly (commandlet-safe)
    if (!MaterialResource)
    {
        static const EShaderPlatform PlatformFallbacks[] = {
            SP_PCD3D_SM6,       // DX12 SM6
            SP_PCD3D_SM5,       // DX11 SM5
            SP_VULKAN_SM5,      // Vulkan SM5
        };

        for (EShaderPlatform FallbackPlatform : PlatformFallbacks)
        {
            MaterialResource = Material->GetMaterialResource(FallbackPlatform);
            if (MaterialResource)
            {
                UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Found resource via fallback platform %d for %s"),
                    (int32)FallbackPlatform, *MaterialName);
                break;
            }
        }
    }
#elif ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
    MaterialResource = Material->GetMaterialResource(GMaxRHIFeatureLevel);
#endif

    if (MaterialResource)
    {
        // Method 3: Try GetMaterialExpressionSource (direct from FMaterialResource)
        FString Source;
        if (MaterialResource->GetMaterialExpressionSource(Source) && !Source.IsEmpty())
        {
            UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Extracted HLSL via GetMaterialExpressionSource for %s (%d chars)"),
                *MaterialName, Source.Len());
            return Source;
        }

        // Method 4: If not compiled, trigger CacheShaders and retry
        if (!MaterialResource->IsCompilationFinished())
        {
            UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Triggering CacheShaders() for %s..."), *MaterialName);
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
            MaterialResource->CacheShaders();
#else
            MaterialResource->CacheShaders(GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel));
#endif

            // Wait for compilation (with timeout)
            int32 MaxWaitMs = 10000;
            int32 WaitedMs = 0;
            while (!MaterialResource->IsCompilationFinished() && WaitedMs < MaxWaitMs)
            {
                FPlatformProcess::Sleep(0.1f);
                WaitedMs += 100;
            }

            if (MaterialResource->IsCompilationFinished())
            {
                if (MaterialResource->GetMaterialExpressionSource(Source) && !Source.IsEmpty())
                {
                    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Extracted HLSL after CacheShaders for %s (%d chars)"),
                        *MaterialName, Source.Len());
                    return Source;
                }

                // Check FMaterialInsights again after compilation
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
                if (UMaterialInterface* MI = Cast<UMaterialInterface>(Material))
                {
                    if (const FMaterialInsights* Insights = MI->MaterialInsight.Get())
                    {
                        if (!Insights->NewHLSLCode.IsEmpty()) return Insights->NewHLSLCode;
                        if (!Insights->LegacyHLSLCode.IsEmpty()) return Insights->LegacyHLSLCode;
                    }
                }
#endif
            }
        }
    }

    // Note: In commandlet mode, materials typically don't have compiled resources
    // HLSL extraction works best when:
    // 1. Running with Editor open (FMaterialInsights populated)
    // 2. Material was previously compiled in Editor session
    return TEXT("");

#else
    return TEXT("HLSL extraction is only available in Editor builds.");
#endif
}

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::AnalyzeMaterialPerformance(UMaterial* Material)
{
    TSharedPtr<FJsonObject> PerformanceJson = MakeShareable(new FJsonObject);

    if (!Material)
    {
        return PerformanceJson;
    }

#if WITH_EDITOR
    // Count texture samplers (already implemented helper)
    int32 TextureSamplerCount = CountTextureSamplers(Material);
    PerformanceJson->SetNumberField(TEXT("texture_sampler_count"), TextureSamplerCount);

    // Count total MaterialExpression nodes
    int32 TotalNodeCount = Material->GetExpressions().Num();
    PerformanceJson->SetNumberField(TEXT("total_node_count"), TotalNodeCount);

    // Count specific node types that impact performance
    int32 MathOperationCount = 0;
    int32 FunctionCallCount = 0;
    int32 CustomNodeCount = 0;

    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (!Expression)
            continue;

        // Count math operations (Add, Multiply, Power, etc.)
        if (Expression->IsA<UMaterialExpressionAdd>() ||
            Expression->IsA<UMaterialExpressionSubtract>() ||
            Expression->IsA<UMaterialExpressionMultiply>() ||
            Expression->IsA<UMaterialExpressionDivide>() ||
            Expression->IsA<UMaterialExpressionPower>() ||
            Expression->IsA<UMaterialExpressionSquareRoot>() ||
            Expression->IsA<UMaterialExpressionAbs>())
        {
            MathOperationCount++;
        }
        // Count material function calls
        else if (Expression->IsA<UMaterialExpressionMaterialFunctionCall>())
        {
            FunctionCallCount++;
        }
        // Count custom expression nodes (potential performance risk)
        else if (Expression->IsA<UMaterialExpressionCustom>())
        {
            CustomNodeCount++;
        }
    }

    PerformanceJson->SetNumberField(TEXT("math_operation_count"), MathOperationCount);
    PerformanceJson->SetNumberField(TEXT("function_call_count"), FunctionCallCount);
    PerformanceJson->SetNumberField(TEXT("custom_node_count"), CustomNodeCount);

    // Calculate complexity rating
    // Simple heuristic: texture samplers are expensive, custom nodes are risky
    FString ComplexityRating;
    int32 ComplexityScore = TextureSamplerCount * 10 + CustomNodeCount * 20 + MathOperationCount;

    if (ComplexityScore < 50)
        ComplexityRating = TEXT("Low");
    else if (ComplexityScore < 150)
        ComplexityRating = TEXT("Medium");
    else
        ComplexityRating = TEXT("High");

    PerformanceJson->SetStringField(TEXT("complexity_rating"), ComplexityRating);
    PerformanceJson->SetNumberField(TEXT("complexity_score"), ComplexityScore);

    // Estimate GPU cost (rough approximation)
    // Based on typical instruction costs: texture sample ~= 4 ALU ops
    int32 EstimatedInstructionCount = TextureSamplerCount * 4 + MathOperationCount + FunctionCallCount * 5 + CustomNodeCount * 10;
    PerformanceJson->SetNumberField(TEXT("estimated_instruction_count"), EstimatedInstructionCount);

    // Issue #446: Extract compiler-accurate instruction counts
    // Phase 1: Primary platform (GMaxRHIFeatureLevel)
    TSharedPtr<FJsonObject> CompilerStats = ExtractCompilerStats(Material, GMaxRHIFeatureLevel);
    if (CompilerStats.IsValid())
    {
        // Add compiler stats object
        PerformanceJson->SetObjectField(TEXT("compiler"), CompilerStats);

        // Calculate accuracy gap (Heuristic vs Compiler)
        int32 ActualInstructionCount = CompilerStats->GetIntegerField(TEXT("instruction_count"));
        if (ActualInstructionCount > 0)
        {
            float AccuracyGap = FMath::Abs(ActualInstructionCount - EstimatedInstructionCount) / (float)ActualInstructionCount * 100.0f;
            PerformanceJson->SetNumberField(TEXT("heuristic_accuracy_gap_percent"), AccuracyGap);

            // Log accuracy for debugging
            UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Heuristic vs Compiler: %d vs %d (%.1f%% gap) for %s"),
                   EstimatedInstructionCount, ActualInstructionCount, AccuracyGap, *Material->GetName());
        }
    }
    else
    {
        // Compiler stats unavailable (Material not compiled)
        UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Compiler stats unavailable for %s, using heuristic only"), *Material->GetName());
    }

    // Phase 2: Multi-Platform stats (SM5, SM6)
    TSharedPtr<FJsonObject> PlatformStats = MakeShareable(new FJsonObject);

    // SM5 (DX11/DX12)
    TSharedPtr<FJsonObject> SM5Stats = ExtractCompilerStats(Material, ERHIFeatureLevel::SM5);
    if (SM5Stats.IsValid())
    {
        PlatformStats->SetObjectField(TEXT("sm5"), SM5Stats);
    }

    // SM6 (DX12)
    TSharedPtr<FJsonObject> SM6Stats = ExtractCompilerStats(Material, ERHIFeatureLevel::SM6);
    if (SM6Stats.IsValid())
    {
        PlatformStats->SetObjectField(TEXT("sm6"), SM6Stats);
    }

    // Add platform stats if any platform succeeded
    if (PlatformStats->Values.Num() > 0)
    {
        PerformanceJson->SetObjectField(TEXT("platform_stats"), PlatformStats);
    }

    // Mobile compatibility warnings
    TArray<TSharedPtr<FJsonValue>> WarningsArray;

    if (TextureSamplerCount > 8)
    {
        TSharedPtr<FJsonObject> WarningJson = MakeShareable(new FJsonObject);
        WarningJson->SetStringField(TEXT("type"), TEXT("mobile_compatibility"));
        WarningJson->SetStringField(TEXT("message"), FString::Printf(TEXT("High texture sampler count (%d) may impact mobile performance. Consider reducing to <= 8."), TextureSamplerCount));
        WarningsArray.Add(MakeShareable(new FJsonValueObject(WarningJson)));
    }

    if (CustomNodeCount > 0)
    {
        TSharedPtr<FJsonObject> WarningJson = MakeShareable(new FJsonObject);
        WarningJson->SetStringField(TEXT("type"), TEXT("custom_node"));
        WarningJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Material contains %d custom expression nodes. Verify HLSL code for optimization."), CustomNodeCount));
        WarningsArray.Add(MakeShareable(new FJsonValueObject(WarningJson)));
    }

    if (ComplexityScore > 200)
    {
        TSharedPtr<FJsonObject> WarningJson = MakeShareable(new FJsonObject);
        WarningJson->SetStringField(TEXT("type"), TEXT("high_complexity"));
        WarningJson->SetStringField(TEXT("message"), TEXT("Material complexity is very high. Consider simplifying or using Material Functions."));
        WarningsArray.Add(MakeShareable(new FJsonValueObject(WarningJson)));
    }

    if (WarningsArray.Num() > 0)
    {
        PerformanceJson->SetArrayField(TEXT("warnings"), WarningsArray);
    }

#else
    PerformanceJson->SetStringField(TEXT("error"), TEXT("Performance analysis is only available in Editor builds."));
#endif

    return PerformanceJson;
}

/**
 * Extracts compiler-accurate shader instruction counts for a given feature level. (Issue #446)
 *
 * Retrieves the FMaterialResource for the target platform and reads shader
 * statistics (instruction count, texture sampler count, etc.) from the compiled ShaderMap.
 *
 * @param Material The Material to analyze
 * @param FeatureLevel The RHI feature level (SM5, SM6) to extract stats for
 * @return JSON object with shader compilation metrics, or nullptr if unavailable
 */
TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::ExtractCompilerStats(UMaterial* Material, ERHIFeatureLevel::Type FeatureLevel)
{
    if (!Material)
    {
        return nullptr;
    }

#if WITH_EDITOR
    // 1. Get Material Resource for target platform
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
    // UE 5.7+: GetMaterialResource requires EShaderPlatform instead of ERHIFeatureLevel
    FMaterialResource* MaterialResource = Material->GetMaterialResource(GetFeatureLevelShaderPlatform(FeatureLevel));
#else
    FMaterialResource* MaterialResource = Material->GetMaterialResource(FeatureLevel);
#endif
    if (!MaterialResource)
    {
        UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Material resource unavailable: %s"), *Material->GetName());
        return nullptr;
    }

    // 2. Check compilation status (Material Editor pattern)
    if (!MaterialResource->IsCompilationFinished())
    {
        UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Material not compiled: %s"), *Material->GetName());
        return nullptr;
    }

    // 3. Get Shader Map (Issue #782: UE 5.6 requires non-const for GetMaxNumInstructionsForShader)
    FMaterialShaderMap* ShaderMap = MaterialResource->GetGameThreadShaderMap();
    if (!ShaderMap)
    {
        UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] Shader map unavailable: %s"), *Material->GetName());
        return nullptr;
    }

    // 4. Extract shader list (Material Editor pattern - MaterialStats.cpp:134-135)
    TMap<FShaderId, TShaderRef<FShader>> Shaders;
    ShaderMap->GetShaderList(Shaders);

    if (Shaders.Num() == 0)
    {
        UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] No shaders found: %s"), *Material->GetName());
        return nullptr;
    }

    // 5. Extract instruction counts (Material Editor pattern - MaterialStatsCommon.cpp:483-525)
    int32 MaxInstructions = 0;
    int32 TotalInstructions = 0;
    int32 ShaderCount = 0;

    for (const auto& Entry : Shaders)
    {
        FShaderType* ShaderType = Entry.Value.GetType();
        if (!ShaderType)
        {
            continue;
        }

        // Use Material Editor's helper method (GetMaxNumInstructionsForShader)
        // This abstracts platform differences and shader type complexity
        // Issue #782 FIXED: UE 5.6 requires non-const FMaterialShaderMap* (line 965)
        int32 NumInstructions = 0;

        if (MaterialResource->IsUIMaterial())
        {
            // UI Material path (MaterialStatsCommon.cpp:483)
            // UE 5.6: GetMaxNumInstructionsForShader is available on FMaterialShaderMap
            NumInstructions = ShaderMap->GetMaxNumInstructionsForShader(ShaderType);
        }
        else
        {
            // Mesh Material path (MaterialStatsCommon.cpp:525)
            // UE 5.6: For mesh materials, ideally need FMeshMaterialShaderMap
            // However, our approach using FMaterialShaderMap->GetShaderList works for most cases
            // Fall back to FMaterialShaderMap query (covers most shader types)
            NumInstructions = ShaderMap->GetMaxNumInstructionsForShader(ShaderType);
        }

        if (NumInstructions > 0)
        {
            MaxInstructions = FMath::Max(MaxInstructions, NumInstructions);
            TotalInstructions += NumInstructions;
            ShaderCount++;
        }
    }

    // 6. Build JSON result
    TSharedPtr<FJsonObject> CompilerStats = MakeShareable(new FJsonObject);

    CompilerStats->SetNumberField(TEXT("instruction_count"), MaxInstructions);
    CompilerStats->SetNumberField(TEXT("avg_instruction_count"), ShaderCount > 0 ? TotalInstructions / ShaderCount : 0);
    CompilerStats->SetNumberField(TEXT("shader_count"), ShaderCount);

    // Feature level info
    FString FeatureLevelName;
    switch (FeatureLevel)
    {
        case ERHIFeatureLevel::SM5:
            FeatureLevelName = TEXT("SM5");
            break;
        case ERHIFeatureLevel::SM6:
            FeatureLevelName = TEXT("SM6");
            break;
        case ERHIFeatureLevel::ES3_1:
            FeatureLevelName = TEXT("ES3_1");
            break;
        default:
            FeatureLevelName = TEXT("Unknown");
            break;
    }
    CompilerStats->SetStringField(TEXT("feature_level"), FeatureLevelName);

    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Compiler stats for %s (%s): %d instructions (%d shaders)"),
           *Material->GetName(), *FeatureLevelName, MaxInstructions, ShaderCount);

    return CompilerStats;

#else
    return nullptr;
#endif
}

TArray<TSharedPtr<FJsonValue>> UCUMaterialMetadataCommandlet::GenerateOptimizationSuggestions(UMaterial* Material)
{
    TArray<TSharedPtr<FJsonValue>> SuggestionsArray;

    if (!Material)
    {
        return SuggestionsArray;
    }

#if WITH_EDITOR
    // Analyze Material nodes for optimization opportunities

    // 1. Check for Power(x, 2.0) nodes that should be x*x
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (UMaterialExpressionPower* PowerNode = Cast<UMaterialExpressionPower>(Expression))
        {
            // Check if exponent is constant 2.0
            if (UMaterialExpressionConstant* ExponentConstant = Cast<UMaterialExpressionConstant>(PowerNode->Exponent.Expression))
            {
                if (FMath::IsNearlyEqual(ExponentConstant->R, 2.0f))
                {
                    TSharedPtr<FJsonObject> SuggestionJson = MakeShareable(new FJsonObject);
                    SuggestionJson->SetStringField(TEXT("type"), TEXT("math_optimization"));
                    SuggestionJson->SetStringField(TEXT("node_type"), TEXT("Power"));
                    SuggestionJson->SetStringField(TEXT("message"), TEXT("Replace Power(x, 2.0) with Multiply(x, x) for better performance."));
                    SuggestionJson->SetStringField(TEXT("severity"), TEXT("medium"));
                    SuggestionsArray.Add(MakeShareable(new FJsonValueObject(SuggestionJson)));
                }
            }
        }
    }

    // 2. Texture channel merging opportunities
    // Count separate texture samples that could be merged into channels
    TMap<UTexture*, int32> TextureUsageCount;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expression))
        {
            if (TexSample->Texture)
            {
                int32* CountPtr = TextureUsageCount.Find(TexSample->Texture);
                if (CountPtr)
                    (*CountPtr)++;
                else
                    TextureUsageCount.Add(TexSample->Texture, 1);
            }
        }
    }

    // If same texture sampled multiple times, suggest channel packing
    for (const auto& Pair : TextureUsageCount)
    {
        if (Pair.Value > 1)
        {
            TSharedPtr<FJsonObject> SuggestionJson = MakeShareable(new FJsonObject);
            SuggestionJson->SetStringField(TEXT("type"), TEXT("texture_optimization"));
            SuggestionJson->SetStringField(TEXT("texture"), Pair.Key->GetPathName());
            SuggestionJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Texture '%s' is sampled %d times. Consider using different channels to reduce sampler count."), *Pair.Key->GetName(), Pair.Value));
            SuggestionJson->SetStringField(TEXT("severity"), TEXT("low"));
            SuggestionsArray.Add(MakeShareable(new FJsonValueObject(SuggestionJson)));
        }
    }

    // 3. Mobile compatibility warnings
    int32 TextureSamplerCount = CountTextureSamplers(Material);
    if (TextureSamplerCount > 8)
    {
        TSharedPtr<FJsonObject> SuggestionJson = MakeShareable(new FJsonObject);
        SuggestionJson->SetStringField(TEXT("type"), TEXT("mobile_compatibility"));
        SuggestionJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Material uses %d texture samplers. Mobile platforms typically support 8-16 samplers. Reduce sampler count for better mobile performance."), TextureSamplerCount));
        SuggestionJson->SetStringField(TEXT("severity"), TEXT("high"));
        SuggestionsArray.Add(MakeShareable(new FJsonValueObject(SuggestionJson)));
    }

    // 4. Custom node warnings
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (UMaterialExpressionCustom* CustomNode = Cast<UMaterialExpressionCustom>(Expression))
        {
            TSharedPtr<FJsonObject> SuggestionJson = MakeShareable(new FJsonObject);
            SuggestionJson->SetStringField(TEXT("type"), TEXT("custom_node"));
            SuggestionJson->SetStringField(TEXT("message"), TEXT("Custom expression node detected. Verify HLSL code is optimized. Consider replacing with built-in nodes for better cross-platform compatibility."));
            SuggestionJson->SetStringField(TEXT("severity"), TEXT("medium"));
            SuggestionsArray.Add(MakeShareable(new FJsonValueObject(SuggestionJson)));
        }
    }

    // 5. Material Function usage recommendations
    // If Material is very complex, suggest breaking into functions
    int32 TotalNodeCount = Material->GetExpressions().Num();
    if (TotalNodeCount > 50)
    {
        TSharedPtr<FJsonObject> SuggestionJson = MakeShareable(new FJsonObject);
        SuggestionJson->SetStringField(TEXT("type"), TEXT("refactoring"));
        SuggestionJson->SetStringField(TEXT("message"), FString::Printf(TEXT("Material contains %d nodes. Consider breaking complex logic into Material Functions for reusability and maintainability."), TotalNodeCount));
        SuggestionJson->SetStringField(TEXT("severity"), TEXT("low"));
        SuggestionsArray.Add(MakeShareable(new FJsonValueObject(SuggestionJson)));
    }

    // 6. Check for unused nodes (no outputs connected)
    // This requires more complex graph analysis, defer to Phase 2

#else
    TSharedPtr<FJsonObject> ErrorJson = MakeShareable(new FJsonObject);
    ErrorJson->SetStringField(TEXT("type"), TEXT("error"));
    ErrorJson->SetStringField(TEXT("message"), TEXT("Optimization suggestions are only available in Editor builds."));
    SuggestionsArray.Add(MakeShareable(new FJsonValueObject(ErrorJson)));
#endif

    return SuggestionsArray;
}

// ============================================================================
// Helper Functions
// ============================================================================

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::CreateBasicAssetJson(
    const FString& AssetName,
    const FAssetData& AssetData,
    IAssetRegistry& AssetRegistry)
{
    TSharedPtr<FJsonObject> AssetJson = MakeShareable(new FJsonObject);
    AssetJson->SetStringField(TEXT("name"), AssetName);
    AssetJson->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
    AssetJson->SetStringField(TEXT("package"), AssetData.PackageName.ToString());

    // Issue #224 Hotfix: Add SavedHash for incremental builds (Blueprint pattern reuse)
    TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);
    if (PackageData.IsSet())
    {
        FIoHash SavedHash = PackageData->GetPackageSavedHash();
        FString SavedHashStr = LexToString(SavedHash);
        AssetJson->SetStringField(TEXT("saved_hash"), SavedHashStr);
    }

    return AssetJson;
}

FString UCUMaterialMetadataCommandlet::GetMaterialExpressionGUID(UMaterialExpression* Expression, UMaterial* Material)
{
    if (!Expression || !Material)
    {
        return TEXT("");
    }

    // MaterialExpressions don't have persistent GUIDs by default
    // Generate deterministic GUID based on:
    // 1. Expression type
    // 2. Position (EditorX, EditorY)
    // 3. Material path
    // 4. Index in Material->Expressions array

    // Find index in Material's Expressions array
    // Issue #224 UE 5.6 Compat: GetExpressions() returns TArrayView in UE 5.6, TArray& in UE 5.5
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
    TArrayView<const TObjectPtr<UMaterialExpression>> Expressions = Material->GetExpressions();
    int32 ExpressionIndex = -1;
    for (int32 i = 0; i < Expressions.Num(); ++i)
    {
        if (Expressions[i] == Expression)
        {
            ExpressionIndex = i;
            break;
        }
    }
#else
    const TArray<TObjectPtr<UMaterialExpression>>& Expressions = Material->GetExpressions();
    int32 ExpressionIndex = Expressions.IndexOfByKey(Expression);
#endif

    // Build deterministic string
    FString GUIDSource = FString::Printf(TEXT("%s_%s_%d_%d_%d"),
        *Material->GetPathName(),
        *Expression->GetClass()->GetName(),
        Expression->MaterialExpressionEditorX,
        Expression->MaterialExpressionEditorY,
        ExpressionIndex
    );

    // Hash the string to create a shorter GUID-like identifier
    uint32 Hash = GetTypeHash(GUIDSource);

    // Format as GUID-style string
    return FString::Printf(TEXT("%08X-%04X-%04X"),
        Hash,
        (Hash >> 16) & 0xFFFF,
        Hash & 0xFFFF
    );
}

FString UCUMaterialMetadataCommandlet::GetMaterialExpressionTypeName(UMaterialExpression* Expression)
{
    if (!Expression)
    {
        return TEXT("Unknown");
    }

    return Expression->GetClass()->GetName();
}

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::ExtractMaterialExpressionParameters(UMaterialExpression* Expression)
{
    TSharedPtr<FJsonObject> ParametersJson = MakeShareable(new FJsonObject);

    if (!Expression)
    {
        return ParametersJson;
    }

    // Type-specific parameter extraction
    // Handle common MaterialExpression types

    // TextureSample
    if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expression))
    {
        if (TexSample->Texture)
        {
            ParametersJson->SetStringField(TEXT("Texture"), TexSample->Texture->GetPathName());
        }
        ParametersJson->SetStringField(TEXT("SamplerType"), UEnum::GetValueAsString(TexSample->SamplerType));
    }
    // MaterialFunctionCall (Issue #357 Phase 2.3)
    else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
    {
        if (FunctionCall->MaterialFunction)
        {
            ParametersJson->SetStringField(TEXT("MaterialFunction"), FunctionCall->MaterialFunction->GetPathName());
        }
    }
    // Constant
    else if (UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
    {
        ParametersJson->SetNumberField(TEXT("R"), Constant->R);
    }
    // Constant2Vector
    else if (UMaterialExpressionConstant2Vector* Constant2 = Cast<UMaterialExpressionConstant2Vector>(Expression))
    {
        ParametersJson->SetNumberField(TEXT("R"), Constant2->R);
        ParametersJson->SetNumberField(TEXT("G"), Constant2->G);
    }
    // Constant3Vector
    else if (UMaterialExpressionConstant3Vector* Constant3 = Cast<UMaterialExpressionConstant3Vector>(Expression))
    {
        FString ConstantStr = FString::Printf(TEXT("(R=%.3f,G=%.3f,B=%.3f)"),
                                             Constant3->Constant.R,
                                             Constant3->Constant.G,
                                             Constant3->Constant.B);
        ParametersJson->SetStringField(TEXT("Constant"), ConstantStr);
    }
    // Constant4Vector
    else if (UMaterialExpressionConstant4Vector* Constant4 = Cast<UMaterialExpressionConstant4Vector>(Expression))
    {
        FString ConstantStr = FString::Printf(TEXT("(R=%.3f,G=%.3f,B=%.3f,A=%.3f)"),
                                             Constant4->Constant.R,
                                             Constant4->Constant.G,
                                             Constant4->Constant.B,
                                             Constant4->Constant.A);
        ParametersJson->SetStringField(TEXT("Constant"), ConstantStr);
    }

    // Note: For other MaterialExpression types, we could use reflection to extract
    // UPROPERTY values, but that's complex and may not be necessary for Phase 1.
    // Current implementation covers the most common expression types.

    return ParametersJson;
}

int32 UCUMaterialMetadataCommandlet::CountTextureSamplers(UMaterial* Material)
{
    if (!Material)
    {
        return 0;
    }

    int32 Count = 0;
    for (UMaterialExpression* Expression : Material->GetExpressions())
    {
        if (Expression && Expression->IsA<UMaterialExpressionTextureSample>())
        {
            Count++;
        }
    }

    return Count;
}

// ============================================================================
// JSON Building Functions
// ============================================================================

/**
 * Builds the final JSON output containing all collected Material metadata.
 *
 * Assembles Materials and MaterialInstances arrays with processing statistics
 * and generation metadata into a single root JSON object ready for serialization.
 *
 * @param MaterialsArray Array of processed Material JSON objects
 * @param MaterialInstancesArray Array of processed MaterialInstance JSON objects
 * @param ProcessedCount Number of successfully processed Materials
 * @param ErrorCount Number of Material processing failures
 * @param InstanceProcessedCount Number of successfully processed MaterialInstances
 * @param InstanceErrorCount Number of MaterialInstance processing failures
 * @return Complete root JSON object for serialization
 */
TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::BuildFinalJsonObject(
    const TArray<TSharedPtr<FJsonValue>>& MaterialsArray,
    const TArray<TSharedPtr<FJsonValue>>& MaterialInstancesArray,
    int32 ProcessedCount,
    int32 ErrorCount,
    int32 InstanceProcessedCount,
    int32 InstanceErrorCount)
{
    TSharedPtr<FJsonObject> RootJson = MakeShareable(new FJsonObject);

    // Arrays
    RootJson->SetArrayField(TEXT("materials"), MaterialsArray);
    RootJson->SetArrayField(TEXT("material_instances"), MaterialInstancesArray);

    // Statistics
    RootJson->SetNumberField(TEXT("total_materials"), ProcessedCount);
    RootJson->SetNumberField(TEXT("material_errors"), ErrorCount);
    RootJson->SetNumberField(TEXT("total_instances"), InstanceProcessedCount);
    RootJson->SetNumberField(TEXT("instance_errors"), InstanceErrorCount);

    // Metadata
    RootJson->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToString());
    RootJson->SetStringField(TEXT("version"), TEXT("4.11.0"));  // Issue #224

    return RootJson;
}

// ============================================================================
// Incremental Build Functions (Blueprint pattern reuse)
// ============================================================================

TSharedPtr<FJsonObject> UCUMaterialMetadataCommandlet::LoadExistingJson(const FString& JsonPath)
{
    // Issue #14142: route through the canonical loader. Corrupted-file recovery, the GZIP footer
    // end-scan, the GZIP-bomb security layers, and the UE 5.6 FCompression API are all centralized
    // in FCUCommandletHelpers::LoadJsonFromFile, replacing this commandlet's local #if ladder and
    // fixed 10MB decompression buffer. Probe GZIP first (v4.6.0), then plain JSON (original order).
    const FString GzipPath = JsonPath + TEXT(".gz");
    if (FPaths::FileExists(GzipPath))
    {
        return FCUCommandletHelpers::LoadJsonFromFile(GzipPath);
    }
    if (FPaths::FileExists(JsonPath))
    {
        return FCUCommandletHelpers::LoadJsonFromFile(JsonPath);
    }

    // No existing file
    return nullptr;
}

TArray<FAssetData> UCUMaterialMetadataCommandlet::FindChangedMaterials(
    const TArray<FAssetData>& AllMaterials,
    const TSharedPtr<FJsonObject>& ExistingJson,
    IAssetRegistry& AssetRegistry)
{
    // Blueprint pattern reuse: Issue #108 Phase 1 Incremental
    // Hash-based change detection

    if (!ExistingJson.IsValid())
    {
        // No existing data, return all Materials
        return AllMaterials;
    }

    // Build hash map from existing JSON
    TMap<FString, FString> ExistingHashes;  // Path → Hash

    // Extract hashes from materials array
    const TArray<TSharedPtr<FJsonValue>>* MaterialsArrayPtr;
    if (ExistingJson->TryGetArrayField(TEXT("materials"), MaterialsArrayPtr))
    {
        for (const TSharedPtr<FJsonValue>& MaterialValue : *MaterialsArrayPtr)
        {
            const TSharedPtr<FJsonObject>* MaterialObjectPtr;
            if (MaterialValue->TryGetObject(MaterialObjectPtr))
            {
                FString Path, Hash;
                if ((*MaterialObjectPtr)->TryGetStringField(TEXT("path"), Path) &&
                    (*MaterialObjectPtr)->TryGetStringField(TEXT("saved_hash"), Hash))
                {
                    ExistingHashes.Add(Path, Hash);
                }
            }
        }
    }

    // Extract hashes from material_instances array
    const TArray<TSharedPtr<FJsonValue>>* InstancesArrayPtr;
    if (ExistingJson->TryGetArrayField(TEXT("material_instances"), InstancesArrayPtr))
    {
        for (const TSharedPtr<FJsonValue>& InstanceValue : *InstancesArrayPtr)
        {
            const TSharedPtr<FJsonObject>* InstanceObjectPtr;
            if (InstanceValue->TryGetObject(InstanceObjectPtr))
            {
                FString Path, Hash;
                if ((*InstanceObjectPtr)->TryGetStringField(TEXT("path"), Path) &&
                    (*InstanceObjectPtr)->TryGetStringField(TEXT("saved_hash"), Hash))
                {
                    ExistingHashes.Add(Path, Hash);
                }
            }
        }
    }

    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Incremental mode: %d existing hashes loaded"), ExistingHashes.Num());

    // Find changed Materials
    TArray<FAssetData> ChangedMaterials;

    for (const FAssetData& AssetData : AllMaterials)
    {
        FString AssetPath = AssetData.GetObjectPathString();

        // Issue #224 Hotfix: Get current hash from AssetRegistry (Blueprint pattern reuse)
        TOptional<FAssetPackageData> PackageData = AssetRegistry.GetAssetPackageDataCopy(AssetData.PackageName);
        FString CurrentHash;
        if (PackageData.IsSet())
        {
            FIoHash SavedHash = PackageData->GetPackageSavedHash();
            CurrentHash = LexToString(SavedHash);
        }

        // Compare with existing hash
        FString* ExistingHashPtr = ExistingHashes.Find(AssetPath);

        if (!ExistingHashPtr || *ExistingHashPtr != CurrentHash)
        {
            // Material changed or new Material
            ChangedMaterials.Add(AssetData);
        }
    }

    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Incremental mode: %d/%d Materials changed"),
           ChangedMaterials.Num(), AllMaterials.Num());

    return ChangedMaterials;
}

void UCUMaterialMetadataCommandlet::MergeJsonData(
    TSharedPtr<FJsonObject>& ExistingJson,
    const TArray<TSharedPtr<FJsonValue>>& NewMaterials,
    const TArray<TSharedPtr<FJsonValue>>& NewInstances)
{
    // Blueprint pattern reuse: Issue #108 Phase 1 Incremental
    // Merge new data with existing JSON, preserving unchanged Materials

    if (!ExistingJson.IsValid())
    {
        UE_LOG(LogNarshaMCPMaterial, Warning, TEXT("[MaterialMetadata] MergeJsonData called with invalid ExistingJson"));
        return;
    }

    // Build path → new data maps
    TMap<FString, TSharedPtr<FJsonValue>> NewMaterialsMap;
    for (const TSharedPtr<FJsonValue>& MaterialValue : NewMaterials)
    {
        const TSharedPtr<FJsonObject>* MaterialObjectPtr;
        if (MaterialValue->TryGetObject(MaterialObjectPtr))
        {
            FString Path;
            if ((*MaterialObjectPtr)->TryGetStringField(TEXT("path"), Path))
            {
                NewMaterialsMap.Add(Path, MaterialValue);
            }
        }
    }

    TMap<FString, TSharedPtr<FJsonValue>> NewInstancesMap;
    for (const TSharedPtr<FJsonValue>& InstanceValue : NewInstances)
    {
        const TSharedPtr<FJsonObject>* InstanceObjectPtr;
        if (InstanceValue->TryGetObject(InstanceObjectPtr))
        {
            FString Path;
            if ((*InstanceObjectPtr)->TryGetStringField(TEXT("path"), Path))
            {
                NewInstancesMap.Add(Path, InstanceValue);
            }
        }
    }

    // Merge materials array
    TArray<TSharedPtr<FJsonValue>> MergedMaterialsArray;
    const TArray<TSharedPtr<FJsonValue>>* ExistingMaterialsPtr;
    if (ExistingJson->TryGetArrayField(TEXT("materials"), ExistingMaterialsPtr))
    {
        for (const TSharedPtr<FJsonValue>& ExistingMaterial : *ExistingMaterialsPtr)
        {
            const TSharedPtr<FJsonObject>* MaterialObjectPtr;
            if (ExistingMaterial->TryGetObject(MaterialObjectPtr))
            {
                FString Path;
                if ((*MaterialObjectPtr)->TryGetStringField(TEXT("path"), Path))
                {
                    // Check if this Material was updated
                    TSharedPtr<FJsonValue>* NewMaterialPtr = NewMaterialsMap.Find(Path);
                    if (NewMaterialPtr)
                    {
                        // Use new data
                        MergedMaterialsArray.Add(*NewMaterialPtr);
                        NewMaterialsMap.Remove(Path);  // Mark as processed
                    }
                    else
                    {
                        // Keep existing data (unchanged)
                        MergedMaterialsArray.Add(ExistingMaterial);
                    }
                }
            }
        }
    }

    // Add new Materials that weren't in existing data
    for (const auto& Pair : NewMaterialsMap)
    {
        MergedMaterialsArray.Add(Pair.Value);
    }

    // Merge material_instances array
    TArray<TSharedPtr<FJsonValue>> MergedInstancesArray;
    const TArray<TSharedPtr<FJsonValue>>* ExistingInstancesPtr;
    if (ExistingJson->TryGetArrayField(TEXT("material_instances"), ExistingInstancesPtr))
    {
        for (const TSharedPtr<FJsonValue>& ExistingInstance : *ExistingInstancesPtr)
        {
            const TSharedPtr<FJsonObject>* InstanceObjectPtr;
            if (ExistingInstance->TryGetObject(InstanceObjectPtr))
            {
                FString Path;
                if ((*InstanceObjectPtr)->TryGetStringField(TEXT("path"), Path))
                {
                    // Check if this Instance was updated
                    TSharedPtr<FJsonValue>* NewInstancePtr = NewInstancesMap.Find(Path);
                    if (NewInstancePtr)
                    {
                        // Use new data
                        MergedInstancesArray.Add(*NewInstancePtr);
                        NewInstancesMap.Remove(Path);  // Mark as processed
                    }
                    else
                    {
                        // Keep existing data (unchanged)
                        MergedInstancesArray.Add(ExistingInstance);
                    }
                }
            }
        }
    }

    // Add new Instances that weren't in existing data
    for (const auto& Pair : NewInstancesMap)
    {
        MergedInstancesArray.Add(Pair.Value);
    }

    // Update ExistingJson with merged arrays
    ExistingJson->SetArrayField(TEXT("materials"), MergedMaterialsArray);
    ExistingJson->SetArrayField(TEXT("material_instances"), MergedInstancesArray);

    // Update statistics
    ExistingJson->SetNumberField(TEXT("total_materials"), MergedMaterialsArray.Num());
    ExistingJson->SetNumberField(TEXT("total_instances"), MergedInstancesArray.Num());

    // Update metadata
    ExistingJson->SetStringField(TEXT("generated_at"), FDateTime::UtcNow().ToString());
    ExistingJson->SetBoolField(TEXT("incremental_build"), true);

    UE_LOG(LogNarshaMCPMaterial, Display, TEXT("[MaterialMetadata] Merged data: %d materials, %d instances"),
           MergedMaterialsArray.Num(), MergedInstancesArray.Num());
}

bool UCUMaterialMetadataCommandlet::SaveJsonWithMetadata(
    const FString& JsonPath,
    const TSharedPtr<FJsonObject>& JsonData,
    bool bIncrementalMode)
{
    // Issue #14142: route through the canonical compressed saver. The atomic temp -> rename and the
    // UE 5.6 FCompression API now live in one place (FCUCommandletHelpers::SaveJsonWithCompression),
    // replacing this commandlet's local #if ladder. Output stays {JsonPath}.gz — byte-identical format.
    return FCUCommandletHelpers::SaveJsonWithCompression(JsonPath, JsonData);
}
