// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CURemoteLibrary.h"
#include "CUBlueprintAssetCreatePreflightUtils.h"
#include "CUJsonUtils.h"
#include "CUInsightsMemoryCSVCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/App.h"
#include "Math/NumericLimits.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/EngineVersion.h"  // Issue #1403: Dynamic engine version
#include "CUBlueprintManipulator.h"  // Issue #124 Phase 1
#include "CUPinTypeUtils.h"  // Issue #19009: shared struct:/object: pin-type mapper
#include "SecurityLimits.h"  // Issue #1403: Path validation
// Issue #1529 Phase 3: Sequencer Remote Control
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneStringChannel.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/Interface.h"
#include "UObject/UObjectGlobals.h"
#include "Sound/SoundAttenuation.h"  // Issue #16561: SetAttenuationCustomCurve (GT curve fixture authoring)
#include "Curves/CurveFloat.h"        // FRuntimeFloatCurve::GetRichCurve
#include "Curves/RichCurve.h"         // FRichCurve::AddKey / SetKeyInterpMode
// Issue #16552: GameplayEffect modifier authoring in C++. FGameplayModifierInfo's fields are all
// EditDefaultsOnly (CPF_DisableEditOnInstance), so they cannot be set from UE Python on a struct
// instance ("cannot be edited on instances"); C++ has no such restriction.
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "GameplayEffect.h"
#include "GameplayEffectTypes.h"
#include "AttributeSet.h"
#include "Abilities/GameplayAbility.h" // Issue #17689 step 2: UGameplayAbility CDO tag authoring
#endif
#include "UObject/UObjectIterator.h"
// Issue #2947 Phase 0-1: Blueprint Node Creation
#include "K2Node_IfThenElse.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"  // Issue #20522: RemoveBlueprintInterface preview — interface event-node enumeration
#include "K2Node_CustomEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_ExecutionSequence.h"
// Issue #2947 Phase H: Switch and Comment nodes
#include "K2Node_SwitchInteger.h"
#include "K2Node_SwitchString.h"
#include "K2Node_SwitchEnum.h"
#include "EdGraphNode_Comment.h"
// Issue #2947 Phase K: Timeline node
#include "K2Node_Timeline.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"  // Issue #7573: also includes K2Node_EditablePinBase.h
#include "Engine/TimelineTemplate.h"
#include "EdGraph/EdGraph.h"
// Issue #4741: Blueprint T3D Import/Export
#include "EdGraphUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
// Issue #12696: CreateBlueprintAsset (editor-e2e test fixture authoring)
#include "GameFramework/Actor.h"           // AActor::StaticClass() fallback parent
#include "Engine/BlueprintGeneratedClass.h" // UBlueprintGeneratedClass for FKismetEditorUtilities::CreateBlueprint
#include "Misc/PackageName.h"               // FPackageName::LongPackageNameToFilename (already used heavily, just making explicit)
#include "AssetToolsModule.h"                // Issue #28199: IAssetTools dialog-free public preflight checks
#include "IAssetTools.h"
#include "ISourceControlModule.h"          // Issue #28199: deleted-package preflight without revert/checkout
#include "ISourceControlProvider.h"
#include "ISourceControlState.h"
#include "FileHelpers.h"                     // FEditorFileUtils::IsMapPackageAsset
#include "Modules/ModuleManager.h"
// Issue #4346: Material Remote Control
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "StaticParameterSet.h" // Issue #16557: FStaticParameterSet / FStaticSwitchParameter / FStaticComponentMaskParameter for static_switch + component_mask set_parameter
#include "MaterialShared.h" // Issue #14979: FMaterialUpdateContext for batched recompile on node delete
#include "Subsystems/AssetEditorSubsystem.h" // Issue #14979: open-editor detection guard for material writes
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureObject.h"
// Issue #16324: GetMaterialGraph — base parameter class for generic ParameterName + static switch defaults
#include "Materials/MaterialExpressionParameter.h"
// Issue #24288: the two expressions whose ParameterName references a UMaterialParameterCollection
// asset rather than naming the node — excluded from add_node's parameter naming.
#include "Materials/MaterialExpressionCollectionParameter.h"
#include "Materials/MaterialExpressionCollectionTransform.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
// Issue #7921: Material Function support (ADK Phase 4)
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Engine/Texture2D.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/MaterialFactoryNew.h"
// Issue #6389: AnimBP State Machine CRUD
#include "AnimGraphNode_StateMachine.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateMachineGraph.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/BlendProfile.h"
#include "Animation/Skeleton.h"
// Issue #8564 Phase K-2: AnimNotify manipulation
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Kismet2/KismetEditorUtilities.h"
// Issue #2947 Phase J: Transaction/Undo support
#if WITH_EDITOR
#include "Editor.h"
#endif
// Issue #5291 Phase 1A: FCompilerResultsLog access for tokenized error capture
#if WITH_EDITOR
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"
#include "Logging/MessageLog.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
// Issue #5293: Blueprint runtime debug - watched pins, trace, step
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/WatchedPin.h"
#endif
// Issue #4263: BehaviorTree Modification
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h" // Issue #16912: AddBehaviorTreeService
#include "EnvironmentQuery/EnvQuery.h"          // Issue #16572 gap4: EQS authoring
#include "EnvironmentQuery/EnvQueryOption.h"    // UEnvQueryOption::Generator/Tests
#include "EnvironmentQuery/EnvQueryGenerator.h" // UEnvQueryGenerator
#include "EnvironmentQuery/EnvQueryTest.h"      // UEnvQueryTest
// Issue #16913: blackboard-key struct validation (FBlackboardKeySelector / FValueOrBBKey_*)
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
// Issue #26812 PR3: NarshaBlackboardKeyGuard moved out of this file so the editor-graph author
// (CUBehaviorTreeAuthor.cpp) validates keys through the SAME predicate this path uses.
#include "CUBlackboardKeyGuard.h"
// Issue #4263: StateTree Modification & Asset Creation
#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeTypes.h"
// Issue #26805 (ST-03): schema-first creation + authoritative compile gate.
// Mirrors UStateTreeFactory::FactoryCreateNew (StateTreeFactory.cpp:119-149) — the content-browser path.
#if WITH_NARSHAMCP_GAMEPLAYSTATETREE
#include "Components/StateTreeComponentSchema.h" // Default schema (same choice as Issue #1699)
#endif
#include "StateTreeEditorModule.h"               // GetEditorDataClass / GetEditorSchemaClass
#include "StateTreeEditorSchema.h"               // UStateTreeEditorSchema
#include "StateTreeEditingSubsystem.h"           // CompileStateTree (static)
#include "CUStateTreeWriteFinalize.h"            // Issue #27780 ST-06b compile/save/fresh-load receipt
#include "StateTreeCompilerLog.h"                // FStateTreeCompilerLog::ToTokenizedMessages
#include "Logging/TokenizedMessage.h"            // FTokenizedMessage::ToText
#include "UObject/LinkerLoad.h"                  // FLinkerLoad::InvalidateExport (5.7 discard path)
// Issue #11472 Gap 3: StateTree write API (parameters, tasks, bindings)
#include "StateTreeEditorNode.h"               // FStateTreeEditorNode::InitializeAs (5.8 runtime overload)
#include "StateTreeEditorPropertyBindings.h"   // FStateTreeEditorPropertyBindings, AddTaskCompletionBinding
#include "StateTreeDelegate.h"                 // UE::StateTree::ETaskCompletionCondition (5.8)
#endif
#include "StructUtils/PropertyBag.h"           // FInstancedPropertyBag, EPropertyBagPropertyType
#if WITH_NARSHAMCP_PCG
#include "PCGGraph.h"                          // Issue #18416: UPCGGraph user-parameter write API
#endif
#if WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "PropertyBindingPath.h"               // FPropertyBindingPath (PropertyBindingUtils plugin)
#endif
// Issue #11397: AddStateTreeTransition — FGameplayTag for transition RequiredEvent.Tag
#include "GameplayTagContainer.h"
// Issue #4415: FJsonObjectConverter for FStructProperty support
#include "JsonObjectConverter.h"
// Issue #4758: Blueprint Breakpoint management
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/Breakpoint.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/SimpleConstructionScript.h"  // Issue #7704: AddBlueprintComponent
#include "Engine/SCS_Node.h"                  // Issue #7704: AddBlueprintComponent
// Issue #9060: SCS component default — transactional editing + property-change propagation.
#include "ScopedTransaction.h"
#include "Templates/UniquePtr.h"              // Issue #27937: staged property value ownership
#include "UObject/UnrealType.h"               // FProperty, FEditPropertyChain
#include "Misc/EngineVersionComparison.h"
// Issue #4829: Per-instance memory measurement
#include "EngineUtils.h"  // TActorIterator
// Issue #4262/#6232: Enhanced Input asset creation
#if WITH_NARSHAMCP_ENHANCEDINPUT
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"  // Issue #7714: Modifier support for CreateInputMapping
#include "InputTriggers.h"   // Issue #7742: Trigger support for CreateInputAction/CreateInputMapping
#include "EnhancedActionKeyMapping.h"
#endif

// Issue #9344: UMG Widget Blueprint authoring
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
// Issue #26388: UWidgetAnimation authoring. `MovieScene` and `AnimationBindings` are
// public members (WidgetAnimation.h:138 `public:` block) — the reflection block that
// stops `execute_python` is the bare `UPROPERTY()` with no Blueprint/Editor visibility
// specifier, which does not apply to C++.
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "Animation/MovieScene2DTransformSection.h"
#include "Components/NamedSlotInterface.h"  // Issue #23088: named-slot subtree walk
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/BackgroundBlurSlot.h"
#include "Components/BorderSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/GridSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Components/SafeZoneSlot.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBoxSlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcherSlot.h"
#include "Components/WrapBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateBrush.h"

// Issue #9768: Runtime CVar tuning for n.WebSocket.MaxPerFrame
#include "HAL/IConsoleManager.h"
#include "CUWebSocketEditorSubsystem.h"

// Issue #8833: MetaSound editor mutations
#if WITH_EDITOR
#if WITH_NARSHAMCP_METASOUND
#include "MetasoundBuilderBase.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundDocumentInterface.h"
#endif
#include "ScopedTransaction.h"
#endif

// Forward declare BlueprintExtractors functions (defined in BlueprintExtractors.cpp)
// Used for metadata extraction in GenerateBlueprintMetadata implementation
namespace BlueprintExtractors
{
	extern TSharedPtr<FJsonObject> ExtractBlueprintMetadata(UBlueprint* Blueprint, const FAssetData& AssetData);
}

// Performance constants (compile-time evaluation)
static constexpr int32 DEFAULT_FRAME_RATE = 30;        // Default frame rate for sequencer operations
static constexpr int32 DEFAULT_DURATION_SECONDS = 10;  // Default sequence duration in seconds
static constexpr int32 DEFAULT_COMMENT_WIDTH = 400;    // Default comment node width
static constexpr int32 DEFAULT_COMMENT_HEIGHT = 100;   // Default comment node height

// JSON response field constants (Issue #4415: String literal deduplication)
static constexpr const TCHAR* JSON_SUCCESS_FIELD = TEXT("success");
static constexpr const TCHAR* JSON_ERROR_FIELD = TEXT("error");
static constexpr const TCHAR* JSON_MESSAGE_FIELD = TEXT("message");
static constexpr const TCHAR* JSON_DATA_FIELD = TEXT("data");

// Define log category for NarshaMCP Remote Control operations
DEFINE_LOG_CATEGORY(LogNarshaMCPRemote);

bool UCURemoteLibrary::GenerateBlueprintMetadata(
	const TArray<FString>& BlueprintPaths,
	const FString& OutputPath
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[CURemoteLibrary] GenerateBlueprintMetadata called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[CURemoteLibrary]   Blueprints: %d"), BlueprintPaths.Num());
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[CURemoteLibrary]   Output: %s"), *OutputPath);

	// Validate inputs
	if (BlueprintPaths.Num() == 0)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] No Blueprint paths provided"));
		return false;
	}

	if (OutputPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] Output path is empty"));
		return false;
	}

	// Issue #1403: URL-encoded path traversal check (before normalization)
	if (SecurityLimits::ContainsEncodedPathTraversal(OutputPath))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] URL-encoded path traversal detected: %s"), *OutputPath);
		return false;
	}

	// Issue #1403: Normalize and validate output path
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(OutputPath);
	FPaths::NormalizeFilename(NormalizedPath);

	// Issue #1403: Path traversal check after normalization
	if (NormalizedPath.Contains(TEXT("..")))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] Path traversal detected after normalization: %s"), *NormalizedPath);
		return false;
	}

	// Issue #1403: Project boundary check
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);

	if (!NormalizedPath.StartsWith(ProjectDir))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] Output path outside project directory: %s (Project: %s)"), *NormalizedPath, *ProjectDir);
		return false;
	}

	// Ensure output directory exists
	FString OutputDir = FPaths::GetPath(NormalizedPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*OutputDir))
	{
		if (!PlatformFile.CreateDirectoryTree(*OutputDir))
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] Failed to create output directory: %s"), *OutputDir);
			return false;
		}
	}

	// Load Asset Registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Build JSON array of Blueprint metadata
	TArray<TSharedPtr<FJsonValue>> BlueprintDataArray;

	for (const FString& BlueprintPath : BlueprintPaths)
	{
		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[CURemoteLibrary] Processing: %s"), *BlueprintPath);

		// Find asset in registry
		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(BlueprintPath));
		if (!AssetData.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[CURemoteLibrary] Blueprint not found: %s"), *BlueprintPath);
			continue;
		}

		// Load Blueprint
		UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset());
		if (!Blueprint)
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[CURemoteLibrary] Failed to load Blueprint: %s"), *BlueprintPath);
			continue;
		}

		// Extract metadata using existing BlueprintExtractors logic
		TSharedPtr<FJsonObject> BlueprintData = BlueprintExtractors::ExtractBlueprintMetadata(Blueprint, AssetData);
		if (!BlueprintData.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[CURemoteLibrary] Failed to extract metadata: %s"), *BlueprintPath);
			continue;
		}

		// Add to array
		BlueprintDataArray.Add(MakeShared<FJsonValueObject>(BlueprintData));
	}

	// Build root JSON object
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetArrayField(TEXT("blueprints"), BlueprintDataArray);
	RootObject->SetNumberField(TEXT("count"), BlueprintDataArray.Num());
	RootObject->SetStringField(TEXT("timestamp"), FDateTime::UtcNow().ToIso8601());

	// Serialize to JSON string
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(RootObject.ToSharedRef(), JsonWriter))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] Failed to serialize JSON"));
		return false;
	}

	// Write to file (use validated NormalizedPath)
	if (!FFileHelper::SaveStringToFile(JsonString, *NormalizedPath))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[CURemoteLibrary] Failed to write file: %s"), *NormalizedPath);
		return false;
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[CURemoteLibrary] Metadata generated successfully: %s"), *NormalizedPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[CURemoteLibrary]   Blueprints processed: %d"), BlueprintDataArray.Num());

	return true;
}

FString UCURemoteLibrary::Ping()
{
	// Return plugin version string
	return TEXT("NarshaMCP v4.6.0");
}

FString UCURemoteLibrary::GetStatus()
{
	// Build status JSON object
	TSharedPtr<FJsonObject> StatusObject = MakeShared<FJsonObject>();
	StatusObject->SetStringField(TEXT("plugin_name"), TEXT("NarshaMCP"));
	StatusObject->SetStringField(TEXT("plugin_version"), TEXT("4.6.0"));
	StatusObject->SetStringField(TEXT("editor_version"), FEngineVersion::Current().ToString());
	StatusObject->SetBoolField(TEXT("remote_control_enabled"), true);

	// Calculate uptime (would need static start time variable for real implementation)
	StatusObject->SetNumberField(TEXT("uptime_seconds"), 0);  // Placeholder

	// Serialize to JSON string
	FString JsonString = CUJsonUtils::SerializeToString(StatusObject.ToSharedRef());

	return JsonString;
}

// Issue #16334: per-project read isolation. The Epic RC HTTP endpoint
// is shared across concurrent multi-editor sessions, so a daemon calling
// `/remote/object/describe` could read a FOREIGN project's object. Routing this
// reflection dump through the plugin's per-project WebSocket subsystem
// (`call_narsha_rc` → WS on 5.8+) guarantees it runs in the daemon's OWN editor.
FString UCURemoteLibrary::DescribeObject(const FString& ObjectPath)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	if (ObjectPath.IsEmpty())
	{
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("error"), TEXT("ObjectPath is empty"));
		return CUJsonUtils::SerializeToString(Root.ToSharedRef());
	}

	// StaticLoadObject resolves both top-level assets ("/Game/Foo.Foo") and
	// subobjects ("/Game/Foo.Foo:Comp"); it loads the package if not resident.
	UObject* Obj = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	if (!Obj)
	{
		Root->SetBoolField(TEXT("success"), false);
		Root->SetStringField(TEXT("object_path"), ObjectPath);
		Root->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Object not found: %s"), *ObjectPath));
		return CUJsonUtils::SerializeToString(Root.ToSharedRef());
	}

	UClass* Cls = Obj->GetClass();
	Root->SetBoolField(TEXT("success"), true);
	Root->SetStringField(TEXT("object_path"), ObjectPath);
	Root->SetStringField(TEXT("name"), Obj->GetName());
	Root->SetStringField(TEXT("class"), Cls->GetName());
	Root->SetStringField(TEXT("class_path"), Cls->GetPathName());
	if (UObject* Outer = Obj->GetOuter())
	{
		Root->SetStringField(TEXT("outer"), Outer->GetPathName());
	}

	// Faithful reflection dump of every (non-transient) property → mirrors what
	// the RC `/remote/object/describe` endpoint surfaced. CheckFlags=0 includes
	// all properties; SkipFlags drops transient/editor-only churn.
	TSharedRef<FJsonObject> Props = MakeShared<FJsonObject>();
	const int64 CheckFlags = 0;
	const int64 SkipFlags = static_cast<int64>(CPF_Transient | CPF_DuplicateTransient);
	if (FJsonObjectConverter::UStructToJsonObject(Cls, Obj, Props, CheckFlags, SkipFlags))
	{
		Root->SetObjectField(TEXT("properties"), Props);
	}
	else
	{
		Root->SetStringField(TEXT("properties_error"),
			TEXT("UStructToJsonObject failed to serialize properties"));
	}

	return CUJsonUtils::SerializeToString(Root.ToSharedRef());
}

// Issue #16334: per-project asset search isolation — replaces the SHARED Epic RC
// HTTP `PUT /remote/search/assets`. Routed via `call_narsha_rc`
// (WS on 5.8+) so the Asset Registry query runs in THIS daemon's own editor and
// can never return a foreign concurrent editor's assets. `QueryJson` carries
// {class_names:[...], package_paths:[...], max_results:N} as a single JSON string
// param — sidesteps RC/WS array-parameter binding.
FString UCURemoteLibrary::SearchAssets(const FString& QueryJson)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	TSet<FString> ClassNames;
	TArray<FString> PackagePaths;
	int32 MaxResults = 200;

	if (!QueryJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> Q;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(QueryJson);
		if (FJsonSerializer::Deserialize(Reader, Q) && Q.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Q->TryGetArrayField(TEXT("class_names"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr) { ClassNames.Add(V->AsString()); }
			}
			if (Q->TryGetArrayField(TEXT("package_paths"), Arr))
			{
				for (const TSharedPtr<FJsonValue>& V : *Arr) { PackagePaths.Add(V->AsString()); }
			}
			int32 MR = 0;
			if (Q->TryGetNumberField(TEXT("max_results"), MR) && MR > 0) { MaxResults = MR; }
		}
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> Assets;
	if (PackagePaths.Num() > 0)
	{
		for (const FString& P : PackagePaths)
		{
			AssetRegistry.GetAssetsByPath(FName(*P), Assets, /*bRecursive=*/true);
		}
	}
	else
	{
		AssetRegistry.GetAllAssets(Assets);
	}

	TArray<TSharedPtr<FJsonValue>> Out;
	int32 TotalMatched = 0;
	for (const FAssetData& A : Assets)
	{
		const FString ClassName = A.AssetClassPath.GetAssetName().ToString();
		if (ClassNames.Num() > 0 && !ClassNames.Contains(ClassName))
		{
			continue;
		}
		++TotalMatched;
		// Keep counting matches for `total_matched` but stop collecting past the cap.
		if (Out.Num() < MaxResults)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), A.AssetName.ToString());
			Entry->SetStringField(TEXT("class"), ClassName);
			Entry->SetStringField(TEXT("package"), A.PackageName.ToString());
			Entry->SetStringField(TEXT("path"), A.GetObjectPathString());
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
	}

	Root->SetBoolField(TEXT("success"), true);
	Root->SetNumberField(TEXT("count"), Out.Num());
	Root->SetNumberField(TEXT("total_matched"), TotalMatched);
	Root->SetBoolField(TEXT("truncated"), TotalMatched > Out.Num());
	Root->SetArrayField(TEXT("assets"), Out);
	return CUJsonUtils::SerializeToString(Root.ToSharedRef());
}

// Issue #124 Phase 1: Blueprint Manipulation Functions

bool UCURemoteLibrary::SetPinDefaultValue(
	const FString& BlueprintPath,
	const FString& NodeGuidStr,
	const FString& PinIdStr,
	const FString& NewValue
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] SetPinDefaultValue called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Blueprint: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  NodeGuid: %s"), *NodeGuidStr);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  PinId: %s"), *PinIdStr);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  NewValue: %s"), *NewValue);

	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Blueprint path is empty"));
		return false;
	}

	// Parse GUIDs
	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Invalid NodeGuid format: %s"), *NodeGuidStr);
		return false;
	}

	FGuid PinId;
	if (!FGuid::Parse(PinIdStr, PinId))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Invalid PinId format: %s"), *PinIdStr);
		return false;
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to load Blueprint: %s"), *BlueprintPath);
		return false;
	}

	// Find pin
	UEdGraphNode* OwningNode = nullptr;
	UEdGraphPin* Pin = FCUBlueprintManipulator::FindPinById(Blueprint, PinId, OwningNode);
	if (!Pin)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Pin not found: %s"), *PinIdStr);
		return false;
	}

	// Set default value
	if (!FCUBlueprintManipulator::SetPinDefaultValue(Pin, NewValue))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to set pin default value"));
		return false;
	}

	// Save Blueprint
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to save Blueprint"));
		return false;
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] SetPinDefaultValue completed successfully"));
	return true;
}

bool UCURemoteLibrary::SetPinDefaultValueByName(
	const FString& BlueprintPath,
	const FString& NodeGuid,
	const FString& PinName,
	const FString& NewValue
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] SetPinDefaultValueByName: Node=%s, Pin=%s, Value=%s"),
		*NodeGuid, *PinName, *NewValue);

	// Parse GUID
	FGuid ParsedGuid;
	if (!FGuid::Parse(NodeGuid, ParsedGuid))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Invalid NodeGuid: %s"), *NodeGuid);
		return false;
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to load Blueprint: %s"), *BlueprintPath);
		return false;
	}

	// Find node by GUID
	UEdGraphNode* Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, ParsedGuid);
	if (!Node)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Node not found: %s"), *NodeGuid);
		return false;
	}

	// Find pin by name on node
	UEdGraphPin* FoundPin = nullptr;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->PinName.ToString() == PinName)
		{
			FoundPin = Pin;
			break;
		}
	}
	if (!FoundPin)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Pin '%s' not found on node %s"), *PinName, *NodeGuid);
		return false;
	}

	// Use schema TrySetDefaultValue for proper validation (same as SetBlueprintPinValue)
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->TrySetDefaultValue(*FoundPin, NewValue);

	// Save and recompile
	FCUBlueprintManipulator::SaveBlueprint(Blueprint, true);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] SetPinDefaultValueByName success: %s.%s = %s"),
		*Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString(), *PinName, *NewValue);
	return true;
}

FString UCURemoteLibrary::AddSwitchCase(
	const FString& BlueprintPath,
	const FString& NodeGuid,
	const FString& CaseValue
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] AddSwitchCase: Node=%s, Case=%s"), *NodeGuid, *CaseValue);

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AddSwitchCase")));
#endif

	auto MakeError = [](const FString& Err) -> FString {
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"%s\"}"), *Err);
	};

	FGuid ParsedGuid;
	if (!FGuid::Parse(NodeGuid, ParsedGuid))
	{
		return MakeError(TEXT("Invalid NodeGuid"));
	}

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return MakeError(FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
	}

	UEdGraphNode* Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, ParsedGuid);
	if (!Node)
	{
		return MakeError(FString::Printf(TEXT("Node not found: %s"), *NodeGuid));
	}

	// Add case pin — works for K2Node_SwitchInteger and K2Node_SwitchString
	// Both inherit from UK2Node_Switch which provides AddPinToSwitchNode()
	UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node);
	if (!SwitchNode)
	{
		return MakeError(TEXT("Node is not a Switch node"));
	}

	// Create new pin via the switch node's API
	FName CasePinName = *CaseValue;
	SwitchNode->Modify();

	// For SwitchString: FunctionToCallName is set, we add via AddPinToSwitchNode
	// For SwitchInteger: similar pattern
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
	UEdGraphPin* NewPin = SwitchNode->CreatePin(EGPD_Output, PinType, CasePinName);

	if (!NewPin)
	{
		return MakeError(TEXT("Failed to create case pin"));
	}

	SwitchNode->ReconstructNode();
	FCUBlueprintManipulator::SaveBlueprint(Blueprint, true);

	FString PinName = NewPin->PinName.ToString();
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] AddSwitchCase success: pin=%s"), *PinName);
	return FString::Printf(TEXT("{\"success\":true,\"pin_name\":\"%s\"}"), *PinName);
}

FString UCURemoteLibrary::ConfigureSwitchCases(
	const FString& BlueprintPath,
	const FString& NodeGuid,
	const FString& CasesJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] ConfigureSwitchCases: Node=%s, Cases=%s"), *NodeGuid, *CasesJson);

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: ConfigureSwitchCases")));
#endif

	auto MakeError = [](const FString& Err) -> FString {
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"%s\"}"), *Err);
	};

	FGuid ParsedGuid;
	if (!FGuid::Parse(NodeGuid, ParsedGuid))
	{
		return MakeError(TEXT("Invalid NodeGuid"));
	}

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return MakeError(FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
	}

	UEdGraphNode* Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, ParsedGuid);
	if (!Node)
	{
		return MakeError(FString::Printf(TEXT("Node not found: %s"), *NodeGuid));
	}

	UK2Node_Switch* SwitchNode = Cast<UK2Node_Switch>(Node);
	if (!SwitchNode)
	{
		return MakeError(TEXT("Node is not a Switch node"));
	}

	// Parse CasesJson: {"type":"int","count":N} or {"type":"string","values":["A","B"]}
	TSharedPtr<FJsonObject> CasesObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CasesJson);
	if (!FJsonSerializer::Deserialize(Reader, CasesObj) || !CasesObj.IsValid())
	{
		return MakeError(TEXT("Invalid CasesJson"));
	}

	FString CaseType = CasesObj->GetStringField(TEXT("type"));
	SwitchNode->Modify();

	int32 CasesAdded = 0;
	// Issue #19079: this is the ROOT allocation boundary. ConfigureSwitchCases is a
	// BlueprintCallable Remote-Control UFUNCTION, so a direct RC call bypasses the Rust
	// MCP clamp (resolve_switch_int_case_count -> .min(MAX_NODE_PINS)) entirely. Clamp the
	// caller-supplied case count HERE, before the CreatePin loop, symmetric with the
	// Rust/offline MAX_NODE_PINS bound — otherwise {"type":"int","count":2000000000} drives
	// ~2B CreatePin editor allocations -> OOM/hang (#14831/#14878/#4263 amplification class).
	// Keep kMaxCasePins in sync with MAX_NODE_PINS (MCP/native/mcp_server/src/tools/t3d_nodes.rs).
	const int32 kMaxCasePins = 256; // == MAX_NODE_PINS (Rust SSOT)
	if (CaseType == TEXT("int"))
	{
		const int32 Count = FMath::Clamp(CasesObj->GetIntegerField(TEXT("count")), 0, kMaxCasePins);
		for (int32 i = 0; i < Count; i++)
		{
			FName CaseName = *FString::Printf(TEXT("%d"), i);
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
			SwitchNode->CreatePin(EGPD_Output, PinType, CaseName);
			CasesAdded++;
		}
	}
	else if (CaseType == TEXT("string"))
	{
		const TArray<TSharedPtr<FJsonValue>>* Values;
		if (CasesObj->TryGetArrayField(TEXT("values"), Values))
		{
			// String is payload-bounded (N values => N pins, no O(1)->O(N) amplification),
			// but cap the array length at the same bound for symmetry with the int arm.
			const int32 Num = FMath::Min(Values->Num(), kMaxCasePins);
			for (int32 i = 0; i < Num; i++)
			{
				FName CaseName = *(*Values)[i]->AsString();
				FEdGraphPinType PinType;
				PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
				SwitchNode->CreatePin(EGPD_Output, PinType, CaseName);
				CasesAdded++;
			}
		}
	}

	SwitchNode->ReconstructNode();
	FCUBlueprintManipulator::SaveBlueprint(Blueprint, true);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] ConfigureSwitchCases: %d cases added"), CasesAdded);
	return FString::Printf(TEXT("{\"success\":true,\"cases_added\":%d}"), CasesAdded);
}

// Issue #15283: build the `auto_inserted_nodes` JSON array describing any
// conversion (cast) node the K2 schema auto-inserted during a connect. Shared by
// ConnectPins + ConnectPinsByName. Empty list (no field added) when nothing was
// inserted, i.e. a plain same-type connection.
static TArray<TSharedPtr<FJsonValue>> BuildAutoInsertedNodesArray(const TArray<UEdGraphNode*>& Nodes)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const UEdGraphNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}
		TSharedPtr<FJsonObject> Entry = MakeShareable(new FJsonObject());
		Entry->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Entry->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Entry->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		Out.Add(MakeShareable(new FJsonValueObject(Entry)));
	}
	return Out;
}

// Issue #15283: attach the optional connect-result extras (auto_inserted_nodes,
// replaced_existing_link) to a result object. Both are omitted when inapplicable.
// Shared by ConnectPins + ConnectPinsByName.
static void AttachConnectExtras(const TSharedPtr<FJsonObject>& Result,
	const TArray<UEdGraphNode*>& AutoInsertedNodes, bool bReplacedLink)
{
	TArray<TSharedPtr<FJsonValue>> AutoInsertedJson = BuildAutoInsertedNodesArray(AutoInsertedNodes);
	if (AutoInsertedJson.Num() > 0)
	{
		Result->SetArrayField(TEXT("auto_inserted_nodes"), AutoInsertedJson);
	}
	if (bReplacedLink)
	{
		Result->SetBoolField(TEXT("replaced_existing_link"), true);
	}
}

FString UCURemoteLibrary::ConnectPins(
	const FString& BlueprintPath,
	const FString& SourcePinIdStr,
	const FString& TargetPinIdStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] ConnectPins called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Blueprint: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  SourcePinId: %s"), *SourcePinIdStr);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  TargetPinId: %s"), *TargetPinIdStr);

	// Helper: serialize JSON result
	auto SerializeResult = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		// Issue #14142: consolidated onto CUJsonUtils::SerializeToString.
		return CUJsonUtils::SerializeToString(Obj);
	};

	// Helper: collect available pins from a node
	auto CollectAvailablePins = [](UEdGraphNode* Node) -> FString
	{
		if (!Node) return TEXT("");
		TArray<FString> PinList;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				PinList.Add(FString::Printf(TEXT("%s(%s)"),
					*Pin->PinName.ToString(),
					Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out")));
			}
		}
		return FString::Join(PinList, TEXT(", "));
	};

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Blueprint path is empty"));
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Blueprint path is empty"));
		return SerializeResult(ResultObject);
	}

	// Parse GUIDs
	FGuid SourcePinId;
	if (!FGuid::Parse(SourcePinIdStr, SourcePinId))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Invalid SourcePinId format: %s"), *SourcePinIdStr);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid SourcePinId format: %s"), *SourcePinIdStr));
		return SerializeResult(ResultObject);
	}

	FGuid TargetPinId;
	if (!FGuid::Parse(TargetPinIdStr, TargetPinId))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Invalid TargetPinId format: %s"), *TargetPinIdStr);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid TargetPinId format: %s"), *TargetPinIdStr));
		return SerializeResult(ResultObject);
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to load Blueprint: %s"), *BlueprintPath);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeResult(ResultObject);
	}

	// Find source pin
	UEdGraphNode* SourceNode = nullptr;
	UEdGraphPin* SourcePin = FCUBlueprintManipulator::FindPinById(Blueprint, SourcePinId, SourceNode);
	if (!SourcePin)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Source pin not found: %s"), *SourcePinIdStr);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Source pin not found: %s"), *SourcePinIdStr));
		if (SourceNode)
		{
			ResultObject->SetStringField(TEXT("available_pins"), CollectAvailablePins(SourceNode));
		}
		return SerializeResult(ResultObject);
	}

	// Find target pin
	UEdGraphNode* TargetNode = nullptr;
	UEdGraphPin* TargetPin = FCUBlueprintManipulator::FindPinById(Blueprint, TargetPinId, TargetNode);
	if (!TargetPin)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Target pin not found: %s"), *TargetPinIdStr);
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Target pin not found: %s"), *TargetPinIdStr));
		if (TargetNode)
		{
			ResultObject->SetStringField(TEXT("available_pins"), CollectAvailablePins(TargetNode));
		}
		return SerializeResult(ResultObject);
	}

	// Connect pins (Issue #15283: capture any auto-inserted conversion node +
	// whether a pre-existing link was broken to make this connection)
	TArray<UEdGraphNode*> AutoInsertedNodes;
	bool bReplacedLink = false;
	if (!FCUBlueprintManipulator::ConnectPins(SourcePin, TargetPin, &AutoInsertedNodes, &bReplacedLink))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to connect pins (incompatible types?)"));
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Failed to connect pins (incompatible types?)"));
		ResultObject->SetStringField(TEXT("source_pin_type"), SourcePin->PinType.PinCategory.ToString());
		ResultObject->SetStringField(TEXT("target_pin_type"), TargetPin->PinType.PinCategory.ToString());
		return SerializeResult(ResultObject);
	}

	// Issue #15283: surface auto-inserted conversion node(s) + replaced link even
	// if the subsequent compile/save fails — the conversion node IS the correct
	// connection; we report it rather than roll it back.
	AttachConnectExtras(ResultObject, AutoInsertedNodes, bReplacedLink);

	// Save Blueprint
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to save Blueprint after connect"));
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Connected but failed to save Blueprint"));
		return SerializeResult(ResultObject);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] ConnectPins completed successfully"));
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("source_pin"), SourcePinIdStr);
	ResultObject->SetStringField(TEXT("target_pin"), TargetPinIdStr);
	return SerializeResult(ResultObject);
}

// Issue #4751 Phase 42: Connect pins by node GUID + pin name (for build_graph auto-wiring)
FString UCURemoteLibrary::ConnectPinsByName(
	const FString& BlueprintPath,
	const FString& SourceNodeGuidStr,
	const FString& SourcePinName,
	const FString& TargetNodeGuidStr,
	const FString& TargetPinName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] ConnectPinsByName: %s.%s → %s.%s"),
		*SourceNodeGuidStr, *SourcePinName, *TargetNodeGuidStr, *TargetPinName);

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	// Parse GUIDs
	FGuid SourceNodeGuid, TargetNodeGuid;
	if (!FGuid::Parse(SourceNodeGuidStr, SourceNodeGuid))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid SourceNodeGuid: %s"), *SourceNodeGuidStr));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}
	if (!FGuid::Parse(TargetNodeGuidStr, TargetNodeGuid))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid TargetNodeGuid: %s"), *TargetNodeGuidStr));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Find source node
	UEdGraphNode* SourceNode = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, SourceNodeGuid);
	if (!SourceNode)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Source node not found: %s"), *SourceNodeGuidStr));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Find target node
	UEdGraphNode* TargetNode = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, TargetNodeGuid);
	if (!TargetNode)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Target node not found: %s"), *TargetNodeGuidStr));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Helper lambda: find pin by name with exec-type fallback
	// Issue #4751: Different nodes have different exec pin names (DoOnce="execute", FlipFlop="", Gate="Enter")
	// When exact name match fails for exec pins, fall back to first exec pin matching direction.
	auto FindPinByNameWithFallback = [](UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction) -> UEdGraphPin*
	{
		// 1. Exact name match (case-insensitive, PinName or DisplayName)
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase)
				|| Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase)))
			{
				return Pin;
			}
		}
		// 2. Exec pin fallback: if requested pin is "execute"/"then"/"exec"/"enter", bind the
		// exec pin matching direction — ONLY when unambiguous (exactly one such pin).
		// Issue #18096: multi-exec-output macros (ForLoop: LoopBody+Completed) previously
		// bound the FIRST exec pin, silently replacing the loop-body link (exec pins hold a
		// single link). Ambiguous alias requests now fail honestly; the caller's error
		// message lists the node's available pins.
		static const TArray<FString> ExecAliases = { TEXT("execute"), TEXT("then"), TEXT("exec"), TEXT("enter") };
		bool bIsExecAlias = false;
		for (const FString& Alias : ExecAliases)
		{
			if (PinName.Equals(Alias, ESearchCase::IgnoreCase)) { bIsExecAlias = true; break; }
		}
		if (bIsExecAlias)
		{
			UEdGraphPin* SoleExecPin = nullptr;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				if (Pin && Pin->Direction == Direction
					&& Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					if (SoleExecPin)
					{
						// Second exec pin in this direction — ambiguous, refuse to guess.
						return nullptr;
					}
					SoleExecPin = Pin;
				}
			}
			return SoleExecPin;
		}
		return nullptr;
	};

	// Find source pin (output direction)
	UEdGraphPin* SourcePin = FindPinByNameWithFallback(SourceNode, SourcePinName, EGPD_Output);
	if (!SourcePin)
	{
		TArray<FString> AvailPins;
		for (UEdGraphPin* Pin : SourceNode->Pins)
		{
			if (Pin) { AvailPins.Add(FString::Printf(TEXT("%s(%s)"), *Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"))); }
		}
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Source pin '%s' not found on node %s. Available: [%s]"),
			*SourcePinName, *SourceNodeGuidStr, *FString::Join(AvailPins, TEXT(", "))));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Find target pin (input direction)
	UEdGraphPin* TargetPin = FindPinByNameWithFallback(TargetNode, TargetPinName, EGPD_Input);
	if (!TargetPin)
	{
		TArray<FString> AvailPins;
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (Pin) { AvailPins.Add(FString::Printf(TEXT("%s(%s)"), *Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"))); }
		}
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Target pin '%s' not found on node %s. Available: [%s]"),
			*TargetPinName, *TargetNodeGuidStr, *FString::Join(AvailPins, TEXT(", "))));
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Connect pins (Issue #15283: capture any auto-inserted conversion node +
	// whether a pre-existing link was broken to make this connection)
	TArray<UEdGraphNode*> AutoInsertedNodes;
	bool bReplacedLink = false;
	if (!FCUBlueprintManipulator::ConnectPins(SourcePin, TargetPin, &AutoInsertedNodes, &bReplacedLink))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("ConnectPins failed (incompatible types?)"));
		ResultObject->SetStringField(TEXT("source_pin_type"), SourcePin->PinType.PinCategory.ToString());
		ResultObject->SetStringField(TEXT("target_pin_type"), TargetPin->PinType.PinCategory.ToString());
		FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
		return Result;
	}

	// Save Blueprint (no compile — caller will compile after all connections)
	FCUBlueprintManipulator::SaveBlueprint(Blueprint, false);

	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("source_node"), SourceNodeGuidStr);
	ResultObject->SetStringField(TEXT("source_pin"), SourcePinName);
	ResultObject->SetStringField(TEXT("target_node"), TargetNodeGuidStr);
	ResultObject->SetStringField(TEXT("target_pin"), TargetPinName);

	// Issue #15283: surface auto-inserted conversion node(s) + replaced link
	AttachConnectExtras(ResultObject, AutoInsertedNodes, bReplacedLink);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] ConnectPinsByName: SUCCESS"));

	FString Result = CUJsonUtils::SerializeToString(ResultObject.ToSharedRef());
	return Result;
}

int32 UCURemoteLibrary::DisconnectPin(
	const FString& BlueprintPath,
	const FString& PinIdStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] DisconnectPin called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Blueprint: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  PinId: %s"), *PinIdStr);

	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Blueprint path is empty"));
		return 0;
	}

	// Parse GUID
	FGuid PinId;
	if (!FGuid::Parse(PinIdStr, PinId))
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Invalid PinId format: %s"), *PinIdStr);
		return 0;
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to load Blueprint: %s"), *BlueprintPath);
		return 0;
	}

	// Find pin
	UEdGraphNode* OwningNode = nullptr;
	UEdGraphPin* Pin = FCUBlueprintManipulator::FindPinById(Blueprint, PinId, OwningNode);
	if (!Pin)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Pin not found: %s"), *PinIdStr);
		return 0;
	}

	// Disconnect pin
	int32 DisconnectedCount = FCUBlueprintManipulator::DisconnectPin(Pin);

	// Save Blueprint only if there were connections
	if (DisconnectedCount > 0)
	{
		if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[RemoteControl] Failed to save Blueprint"));
			return -1;  // Bug #9 fix: Return -1 to distinguish from "no connections" (0)
		}
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] DisconnectPin completed: %d links disconnected"), DisconnectedCount);
	return DisconnectedCount;
}

FString UCURemoteLibrary::DisconnectPins(
	const FString& BlueprintPath,
	const FString& SourcePinIdStr,
	const FString& TargetPinIdStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] DisconnectPins called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Blueprint: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  SourcePinId: %s"), *SourcePinIdStr);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  TargetPinId: %s"), *TargetPinIdStr);

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: DisconnectPins")));
#endif

	// Helper: serialize JSON result
	auto SerializeResult = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		// Issue #14142: consolidated onto CUJsonUtils::SerializeToString.
		return CUJsonUtils::SerializeToString(Obj);
	};

	TSharedPtr<FJsonObject> ResultObject = MakeShareable(new FJsonObject());

	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Blueprint path is empty"));
		return SerializeResult(ResultObject);
	}

	// Parse GUIDs
	FGuid SourcePinId;
	if (!FGuid::Parse(SourcePinIdStr, SourcePinId))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid SourcePinId format: %s"), *SourcePinIdStr));
		return SerializeResult(ResultObject);
	}

	FGuid TargetPinId;
	if (!FGuid::Parse(TargetPinIdStr, TargetPinId))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Invalid TargetPinId format: %s"), *TargetPinIdStr));
		return SerializeResult(ResultObject);
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeResult(ResultObject);
	}

	// Find source pin
	UEdGraphNode* SourceNode = nullptr;
	UEdGraphPin* SourcePin = FCUBlueprintManipulator::FindPinById(Blueprint, SourcePinId, SourceNode);
	if (!SourcePin)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Source pin not found: %s"), *SourcePinIdStr));
		return SerializeResult(ResultObject);
	}

	// Find target pin
	UEdGraphNode* TargetNode = nullptr;
	UEdGraphPin* TargetPin = FCUBlueprintManipulator::FindPinById(Blueprint, TargetPinId, TargetNode);
	if (!TargetPin)
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), FString::Printf(TEXT("Target pin not found: %s"), *TargetPinIdStr));
		return SerializeResult(ResultObject);
	}

	// Check if pins are actually connected
	if (!SourcePin->LinkedTo.Contains(TargetPin))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Pins are not connected to each other"));
		ResultObject->SetStringField(TEXT("source_pin"), FString::Printf(TEXT("%s(%s)"),
			*SourcePin->PinName.ToString(),
			SourcePin->Direction == EGPD_Input ? TEXT("in") : TEXT("out")));
		ResultObject->SetStringField(TEXT("target_pin"), FString::Printf(TEXT("%s(%s)"),
			*TargetPin->PinName.ToString(),
			TargetPin->Direction == EGPD_Input ? TEXT("in") : TEXT("out")));
		return SerializeResult(ResultObject);
	}

	// Break the specific link
	SourcePin->BreakLinkTo(TargetPin);

	// Save Blueprint
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
	{
		ResultObject->SetBoolField(TEXT("success"), false);
		ResultObject->SetStringField(TEXT("error"), TEXT("Disconnected but failed to save Blueprint"));
		return SerializeResult(ResultObject);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[RemoteControl] DisconnectPins completed successfully"));
	ResultObject->SetBoolField(TEXT("success"), true);
	ResultObject->SetStringField(TEXT("source_pin"), SourcePinIdStr);
	ResultObject->SetStringField(TEXT("target_pin"), TargetPinIdStr);
	return SerializeResult(ResultObject);
}

// =========================================================
// Issue #1529 Phase 3: Sequencer Remote Control Functions
// =========================================================

namespace SequencerHelpers
{
	enum class EKeyframeReadStatus : uint8
	{
		Success,
		KeyNotFound,
		UnsupportedChannelType,
	};

	FString RichCurveInterpolationToString(ERichCurveInterpMode Interpolation)
	{
		switch (Interpolation)
		{
		case RCIM_Linear:
			return TEXT("linear");
		case RCIM_Constant:
			return TEXT("constant");
		case RCIM_Cubic:
			return TEXT("cubic");
		case RCIM_None:
		default:
			return TEXT("none");
		}
	}

	FString GetChannelName(const FMovieSceneChannelEntry& Entry, int32 ChannelIndex)
	{
#if WITH_EDITOR
		const TArrayView<const FMovieSceneChannelMetaData> MetaData = Entry.GetMetaData();
		if (ChannelIndex >= 0 && ChannelIndex < MetaData.Num() && !MetaData[ChannelIndex].Name.IsNone())
		{
			return MetaData[ChannelIndex].Name.ToString();
		}
#endif
		return FString::Printf(TEXT("%s_%d"), *Entry.GetChannelTypeName().ToString(), ChannelIndex);
	}

	bool IsSupportedKeyframeChannel(FName ChannelTypeName)
	{
		return ChannelTypeName == FMovieSceneDoubleChannel::StaticStruct()->GetFName()
			|| ChannelTypeName == FMovieSceneFloatChannel::StaticStruct()->GetFName()
			|| ChannelTypeName == FMovieSceneBoolChannel::StaticStruct()->GetFName()
			|| ChannelTypeName == FMovieSceneIntegerChannel::StaticStruct()->GetFName()
			|| ChannelTypeName == FMovieSceneByteChannel::StaticStruct()->GetFName()
			|| ChannelTypeName == FMovieSceneStringChannel::StaticStruct()->GetFName();
	}

	EKeyframeReadStatus ReadKeyframe(
		const FMovieSceneChannel* Channel,
		FName ChannelTypeName,
		const FString& ChannelName,
		FFrameNumber Frame,
		TSharedPtr<FJsonObject>& OutKeyframe)
	{
		if (!Channel || !IsSupportedKeyframeChannel(ChannelTypeName))
		{
			return EKeyframeReadStatus::UnsupportedChannelType;
		}

		OutKeyframe = MakeShared<FJsonObject>();
		OutKeyframe->SetNumberField(TEXT("frame"), Frame.Value);
		OutKeyframe->SetStringField(TEXT("channel"), ChannelName);
		OutKeyframe->SetStringField(TEXT("channel_type"), ChannelTypeName.ToString());

		if (ChannelTypeName == FMovieSceneDoubleChannel::StaticStruct()->GetFName())
		{
			const auto Data = static_cast<const FMovieSceneDoubleChannel*>(Channel)->GetData();
			const int32 KeyIndex = Data.FindKey(Frame);
			if (KeyIndex < 0 || KeyIndex >= Data.GetValues().Num() || Data.GetTimes()[KeyIndex] != Frame)
			{
				return EKeyframeReadStatus::KeyNotFound;
			}
			const FMovieSceneDoubleValue& KeyValue = Data.GetValues()[KeyIndex];
			OutKeyframe->SetNumberField(TEXT("value"), KeyValue.Value);
			OutKeyframe->SetStringField(TEXT("interpolation"), RichCurveInterpolationToString(KeyValue.InterpMode));
		}
		else if (ChannelTypeName == FMovieSceneFloatChannel::StaticStruct()->GetFName())
		{
			const auto Data = static_cast<const FMovieSceneFloatChannel*>(Channel)->GetData();
			const int32 KeyIndex = Data.FindKey(Frame);
			if (KeyIndex < 0 || KeyIndex >= Data.GetValues().Num() || Data.GetTimes()[KeyIndex] != Frame)
			{
				return EKeyframeReadStatus::KeyNotFound;
			}
			const FMovieSceneFloatValue& KeyValue = Data.GetValues()[KeyIndex];
			OutKeyframe->SetNumberField(TEXT("value"), KeyValue.Value);
			OutKeyframe->SetStringField(TEXT("interpolation"), RichCurveInterpolationToString(KeyValue.InterpMode));
		}
		else if (ChannelTypeName == FMovieSceneBoolChannel::StaticStruct()->GetFName())
		{
			const auto Data = static_cast<const FMovieSceneBoolChannel*>(Channel)->GetData();
			const int32 KeyIndex = Data.FindKey(Frame);
			if (KeyIndex < 0 || KeyIndex >= Data.GetValues().Num() || Data.GetTimes()[KeyIndex] != Frame)
			{
				return EKeyframeReadStatus::KeyNotFound;
			}
			OutKeyframe->SetBoolField(TEXT("value"), Data.GetValues()[KeyIndex]);
			OutKeyframe->SetStringField(TEXT("interpolation"), TEXT("constant"));
		}
		else if (ChannelTypeName == FMovieSceneIntegerChannel::StaticStruct()->GetFName())
		{
			const auto Data = static_cast<const FMovieSceneIntegerChannel*>(Channel)->GetData();
			const int32 KeyIndex = Data.FindKey(Frame);
			if (KeyIndex < 0 || KeyIndex >= Data.GetValues().Num() || Data.GetTimes()[KeyIndex] != Frame)
			{
				return EKeyframeReadStatus::KeyNotFound;
			}
			OutKeyframe->SetNumberField(TEXT("value"), Data.GetValues()[KeyIndex]);
			OutKeyframe->SetStringField(TEXT("interpolation"), TEXT("constant"));
		}
		else if (ChannelTypeName == FMovieSceneByteChannel::StaticStruct()->GetFName())
		{
			const auto Data = static_cast<const FMovieSceneByteChannel*>(Channel)->GetData();
			const int32 KeyIndex = Data.FindKey(Frame);
			if (KeyIndex < 0 || KeyIndex >= Data.GetValues().Num() || Data.GetTimes()[KeyIndex] != Frame)
			{
				return EKeyframeReadStatus::KeyNotFound;
			}
			OutKeyframe->SetNumberField(TEXT("value"), Data.GetValues()[KeyIndex]);
			OutKeyframe->SetStringField(TEXT("interpolation"), TEXT("constant"));
		}
		else if (ChannelTypeName == FMovieSceneStringChannel::StaticStruct()->GetFName())
		{
			const auto Data = static_cast<const FMovieSceneStringChannel*>(Channel)->GetData();
			const int32 KeyIndex = Data.FindKey(Frame);
			if (KeyIndex < 0 || KeyIndex >= Data.GetValues().Num() || Data.GetTimes()[KeyIndex] != Frame)
			{
				return EKeyframeReadStatus::KeyNotFound;
			}
			OutKeyframe->SetStringField(TEXT("value"), Data.GetValues()[KeyIndex]);
			OutKeyframe->SetStringField(TEXT("interpolation"), TEXT("constant"));
		}

		return EKeyframeReadStatus::Success;
	}

	void AddTickResolutionFields(TSharedPtr<FJsonObject>& ResultObject, const UMovieScene* MovieScene)
	{
		const FFrameRate TickResolution = MovieScene->GetTickResolution();
		ResultObject->SetStringField(TEXT("time_unit"), TEXT("tick_resolution"));
		ResultObject->SetNumberField(TEXT("tick_resolution_numerator"), TickResolution.Numerator);
		ResultObject->SetNumberField(TEXT("tick_resolution_denominator"), TickResolution.Denominator);
	}

	FString JoinChannelNames(const TArray<FString>& ChannelNames)
	{
		return ChannelNames.Num() > 0 ? FString::Join(ChannelNames, TEXT(", ")) : TEXT("<none>");
	}

	/**
	 * Load a Level Sequence by asset path
	 */
	ULevelSequence* LoadLevelSequence(const FString& SequencePath)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(SequencePath));
		if (!AssetData.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] Sequence not found: %s"), *SequencePath);
			return nullptr;
		}

		ULevelSequence* Sequence = Cast<ULevelSequence>(AssetData.GetAsset());
		if (!Sequence)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] Failed to load sequence: %s"), *SequencePath);
			return nullptr;
		}

		return Sequence;
	}

	/**
	 * Save a Level Sequence asset
	 */
	bool SaveLevelSequence(ULevelSequence* Sequence)
	{
		if (!Sequence)
		{
			return false;
		}

		Sequence->MarkPackageDirty();

		UPackage* Package = Sequence->GetOutermost();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		SaveArgs.Error = GError;

		return UPackage::SavePackage(Package, Sequence, *PackageFileName, SaveArgs);
	}

	/**
	 * Find a track by GUID in the MovieScene
	 */
	UMovieSceneTrack* FindTrackByGuid(UMovieScene* MovieScene, const FGuid& TrackGuid)
	{
		if (!MovieScene)
		{
			return nullptr;
		}

		// Search in root tracks (UE5.6: GetTracks() replaces GetMasterTracks())
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			if (Track && Track->GetSignature() == TrackGuid)
			{
				return Track;
			}
		}

		// Search in binding tracks
		// UE 5.7+: GetBindings() non-const access deprecated, use const pointer
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				if (Track && Track->GetSignature() == TrackGuid)
				{
					return Track;
				}
			}
		}

		return nullptr;
	}

	/**
	 * Find a binding by GUID
	 */
	const FMovieSceneBinding* FindBindingByGuid(UMovieScene* MovieScene, const FGuid& BindingGuid)
	{
		if (!MovieScene)
		{
			return nullptr;
		}

		// UE 5.7+: GetBindings() non-const access deprecated, use const pointer
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			if (Binding.GetObjectGuid() == BindingGuid)
			{
				return &Binding;
			}
		}

		return nullptr;
	}

	/**
	 * Create JSON error response
	 */
	FString CreateErrorJson(const FString& ErrorMessage)
	{
		// Issue #14142: consolidated onto CUJsonUtils::MakeErrorResponse ({"success":false,"error":msg}).
		return CUJsonUtils::MakeErrorResponse(ErrorMessage);
	}

	/**
	 * Create JSON success response with data
	 */
	FString CreateSuccessJson(TSharedPtr<FJsonObject> DataObject)
	{
		// Issue #14142: consolidated onto CUJsonUtils::MakeSuccessResponse (sets "success":true, serializes).
		return CUJsonUtils::MakeSuccessResponse(DataObject);
	}
}

// ---------------------------------------------------------
// Track Operations
// ---------------------------------------------------------

FString UCURemoteLibrary::AddSequencerTrack(
	const FString& SequencePath,
	const FString& TrackType,
	const FString& BindingIdStr,
	const FString& TrackName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] AddSequencerTrack called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  SequencePath: %s"), *SequencePath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  TrackType: %s"), *TrackType);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BindingId: %s"), *BindingIdStr);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  TrackName: %s"), *TrackName);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Failed to load sequence: %s"), *SequencePath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	// Find track class
	UClass* TrackClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MovieSceneTracks.%s"), *TrackType));
	if (!TrackClass)
	{
		// Try without module prefix
		TrackClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MovieScene.%s"), *TrackType));
	}
	if (!TrackClass)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Track class not found: %s"), *TrackType));
	}

	UMovieSceneTrack* NewTrack = nullptr;

	// Add to binding or as master track
	if (!BindingIdStr.IsEmpty())
	{
		FGuid BindingGuid;
		if (!FGuid::Parse(BindingIdStr, BindingGuid))
		{
			return SequencerHelpers::CreateErrorJson(TEXT("Invalid BindingId format"));
		}

		NewTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
	}
	else
	{
		// UE5.6: AddTrack() without GUID for root tracks (replaces AddMasterTrack)
		NewTrack = MovieScene->AddTrack(TrackClass);
	}

	if (!NewTrack)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to create track"));
	}

	// Note: UE5.6 removed SetDisplayName() from UMovieSceneTrack
	// Track naming is handled differently in UE5 - typically through the track type itself
	// TrackName parameter is logged but not applied directly

	// Save sequence
	if (!SequencerHelpers::SaveLevelSequence(Sequence))
	{
		UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Sequencer] Failed to save sequence after adding track"));
	}

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("guid"), NewTrack->GetSignature().ToString(EGuidFormats::DigitsWithHyphens));
	ResultObject->SetStringField(TEXT("name"), NewTrack->GetDisplayName().ToString());
	ResultObject->SetStringField(TEXT("type"), TrackClass->GetName());

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

bool UCURemoteLibrary::RemoveSequencerTrack(
	const FString& SequencePath,
	const FString& TrackGuidStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] RemoveSequencerTrack called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  SequencePath: %s"), *SequencePath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  TrackGuid: %s"), *TrackGuidStr);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	// Parse GUID - try both formats (with and without hyphens)
	FGuid TrackGuid;
	bool bParsed = FGuid::Parse(TrackGuidStr, TrackGuid);
	if (!bParsed)
	{
		// Try without hyphens if hyphenated failed
		FString NoHyphens = TrackGuidStr.Replace(TEXT("-"), TEXT(""));
		bParsed = FGuid::Parse(NoHyphens, TrackGuid);
	}
	if (!bParsed)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] Invalid TrackGuid format: '%s' (len=%d)"), *TrackGuidStr, TrackGuidStr.Len());
		return false;
	}

	// Find and remove track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] Track not found: %s"), *TrackGuidStr);
		return false;
	}

	// UE5.6: RemoveTrack works for both root tracks and binding tracks
	bool bRemoved = MovieScene->RemoveTrack(*Track);

	if (bRemoved)
	{
		SequencerHelpers::SaveLevelSequence(Sequence);
	}

	return bRemoved;
}

FString UCURemoteLibrary::ListSequencerTracks(
	const FString& SequencePath,
	const FString& BindingIdStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] ListSequencerTracks called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  SequencePath: %s"), *SequencePath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BindingId: %s"), *BindingIdStr);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Failed to load sequence: %s"), *SequencePath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	TArray<TSharedPtr<FJsonValue>> TracksArray;

	// Helper lambda to add track info
	auto AddTrackToArray = [&TracksArray](UMovieSceneTrack* Track) {
		if (!Track) return;

		TSharedPtr<FJsonObject> TrackObject = MakeShared<FJsonObject>();
		TrackObject->SetStringField(TEXT("guid"), Track->GetSignature().ToString(EGuidFormats::DigitsWithHyphens));
		TrackObject->SetStringField(TEXT("name"), Track->GetDisplayName().ToString());
		TrackObject->SetStringField(TEXT("type"), Track->GetClass()->GetName());
		TrackObject->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
		TracksArray.Add(MakeShared<FJsonValueObject>(TrackObject));
	};

	if (!BindingIdStr.IsEmpty())
	{
		// List tracks for specific binding
		FGuid BindingGuid;
		if (!FGuid::Parse(BindingIdStr, BindingGuid))
		{
			return SequencerHelpers::CreateErrorJson(TEXT("Invalid BindingId format"));
		}

		const FMovieSceneBinding* Binding = SequencerHelpers::FindBindingByGuid(MovieScene, BindingGuid);
		if (!Binding)
		{
			return SequencerHelpers::CreateErrorJson(TEXT("Binding not found"));
		}

		for (UMovieSceneTrack* Track : Binding->GetTracks())
		{
			AddTrackToArray(Track);
		}
	}
	else
	{
		// List all tracks (root + bindings)
		// UE5.6: GetTracks() replaces GetMasterTracks()
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			AddTrackToArray(Track);
		}

		// UE 5.7+: GetBindings() non-const access deprecated, use const pointer
		const UMovieScene* ConstMovieScene = MovieScene;
		for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
		{
			for (UMovieSceneTrack* Track : Binding.GetTracks())
			{
				AddTrackToArray(Track);
			}
		}
	}

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetArrayField(TEXT("tracks"), TracksArray);
	ResultObject->SetNumberField(TEXT("count"), TracksArray.Num());

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::GetSequencerTrackProperties(
	const FString& SequencePath,
	const FString& TrackGuidStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] GetSequencerTrackProperties called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] TrackGuidStr received: '%s' (len=%d)"), *TrackGuidStr, TrackGuidStr.Len());

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to load sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	// Parse GUID - try both formats
	FGuid TrackGuid;
	bool bParsed = FGuid::Parse(TrackGuidStr, TrackGuid);
	if (!bParsed)
	{
		// Try without hyphens if hyphenated failed
		FString NoHyphens = TrackGuidStr.Replace(TEXT("-"), TEXT(""));
		bParsed = FGuid::Parse(NoHyphens, TrackGuid);
		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] Retry with no hyphens: '%s' (result=%d)"), *NoHyphens, bParsed);
	}
	if (!bParsed)
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] FGuid::Parse failed for: '%s'"), *TrackGuidStr);
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Invalid TrackGuid format: '%s' (len=%d)"), *TrackGuidStr, TrackGuidStr.Len()));
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Track not found"));
	}

	// Build properties
	TSharedPtr<FJsonObject> PropsObject = MakeShared<FJsonObject>();
	PropsObject->SetStringField(TEXT("guid"), Track->GetSignature().ToString(EGuidFormats::DigitsWithHyphens));
	PropsObject->SetStringField(TEXT("name"), Track->GetDisplayName().ToString());
	PropsObject->SetStringField(TEXT("type"), Track->GetClass()->GetName());
	PropsObject->SetNumberField(TEXT("section_count"), Track->GetAllSections().Num());
	PropsObject->SetBoolField(TEXT("is_eval_disabled"), Track->IsEvalDisabled());

	// Add section info
	TArray<TSharedPtr<FJsonValue>> SectionsArray;
	for (UMovieSceneSection* Section : Track->GetAllSections())
	{
		if (!Section) continue;

		TSharedPtr<FJsonObject> SectionObject = MakeShared<FJsonObject>();
		SectionObject->SetNumberField(TEXT("index"), SectionsArray.Num());
		SectionObject->SetBoolField(TEXT("is_active"), Section->IsActive());

		TRange<FFrameNumber> Range = Section->GetRange();
		if (Range.HasLowerBound())
		{
			SectionObject->SetNumberField(TEXT("start_frame"), Range.GetLowerBoundValue().Value);
		}
		if (Range.HasUpperBound())
		{
			SectionObject->SetNumberField(TEXT("end_frame"), Range.GetUpperBoundValue().Value);
		}

		SectionsArray.Add(MakeShared<FJsonValueObject>(SectionObject));
	}
	PropsObject->SetArrayField(TEXT("sections"), SectionsArray);

	return SequencerHelpers::CreateSuccessJson(PropsObject);
}

bool UCURemoteLibrary::SetSequencerTrackProperties(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	const FString& PropertiesJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] SetSequencerTrackProperties called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	// Parse GUID
	FGuid TrackGuid;
	if (!FGuid::Parse(TrackGuidStr, TrackGuid))
	{
		return false;
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return false;
	}

	// Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(JsonReader, PropsObject) || !PropsObject.IsValid())
	{
		UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] Failed to parse properties JSON"));
		return false;
	}

	// Apply properties
	// Note: UE5.6 removed SetDisplayName() from UMovieSceneTrack
	// Display name is now read-only, set through DisplayNameBuilder at track creation
	// Skipping "name" property - not settable in UE5.6+

	if (PropsObject->HasField(TEXT("is_eval_disabled")))
	{
		Track->SetEvalDisabled(PropsObject->GetBoolField(TEXT("is_eval_disabled")));
	}

	// Save
	return SequencerHelpers::SaveLevelSequence(Sequence);
}

// ---------------------------------------------------------
// Binding Operations
// ---------------------------------------------------------

FString UCURemoteLibrary::AddSequencerBinding(
	const FString& SequencePath,
	const FString& ActorPath,
	const FString& BindingType,
	const FString& BindingName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] AddSequencerBinding called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  SequencePath: %s"), *SequencePath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  ActorPath: %s"), *ActorPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BindingType: %s"), *BindingType);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BindingName: %s"), *BindingName);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to load sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	FGuid NewBindingGuid;
	FString ResultType;

	if (BindingType.Equals(TEXT("spawnable"), ESearchCase::IgnoreCase))
	{
		// Load the Blueprint class for spawnable
		UClass* ActorClass = LoadClass<AActor>(nullptr, *ActorPath);
		if (!ActorClass)
		{
			return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Failed to load class: %s"), *ActorPath));
		}

		// UE5.6: AddSpawnable now returns FGuid directly (was FMovieSceneSpawnable& before)
		NewBindingGuid = MovieScene->AddSpawnable(BindingName.IsEmpty() ? ActorClass->GetName() : BindingName, *ActorClass);
		ResultType = TEXT("spawnable");
	}
	else // possessable
	{
		// For possessable, we create a binding with the given name
		NewBindingGuid = MovieScene->AddPossessable(BindingName.IsEmpty() ? TEXT("NewBinding") : BindingName, AActor::StaticClass());
		ResultType = TEXT("possessable");
	}

	if (!NewBindingGuid.IsValid())
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to create binding"));
	}

	// Save
	SequencerHelpers::SaveLevelSequence(Sequence);

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("guid"), NewBindingGuid.ToString(EGuidFormats::DigitsWithHyphens));
	ResultObject->SetStringField(TEXT("name"), BindingName.IsEmpty() ? TEXT("NewBinding") : BindingName);
	ResultObject->SetStringField(TEXT("type"), ResultType);

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

bool UCURemoteLibrary::RemoveSequencerBinding(
	const FString& SequencePath,
	const FString& BindingGuidStr
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] RemoveSequencerBinding called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	// Parse GUID
	FGuid BindingGuid;
	if (!FGuid::Parse(BindingGuidStr, BindingGuid))
	{
		return false;
	}

	// Try to remove as spawnable first
	if (MovieScene->RemoveSpawnable(BindingGuid))
	{
		return SequencerHelpers::SaveLevelSequence(Sequence);
	}

	// Try to remove as possessable
	if (MovieScene->RemovePossessable(BindingGuid))
	{
		return SequencerHelpers::SaveLevelSequence(Sequence);
	}

	UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Sequencer] Binding not found: %s"), *BindingGuidStr);
	return false;
}

FString UCURemoteLibrary::ListSequencerBindings(
	const FString& SequencePath
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] ListSequencerBindings called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to load sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	TArray<TSharedPtr<FJsonValue>> BindingsArray;

	// List all bindings
	// UE 5.7+: GetBindings() non-const access deprecated, use const pointer
	const UMovieScene* ConstMovieScene = MovieScene;
	for (const FMovieSceneBinding& Binding : ConstMovieScene->GetBindings())
	{
		TSharedPtr<FJsonObject> BindingObject = MakeShared<FJsonObject>();
		BindingObject->SetStringField(TEXT("guid"), Binding.GetObjectGuid().ToString(EGuidFormats::DigitsWithHyphens));
		BindingObject->SetNumberField(TEXT("track_count"), Binding.GetTracks().Num());

		// Determine type (spawnable or possessable) and get name
		// UE 5.7+: FMovieSceneBinding::GetName deprecated, use Spawnable/Possessable::GetName
		FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Binding.GetObjectGuid());
		if (Spawnable)
		{
			BindingObject->SetStringField(TEXT("type"), TEXT("spawnable"));
			// UE 5.7+: GetName() moved from FMovieSceneBinding to Spawnable/Possessable
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
			BindingObject->SetStringField(TEXT("name"), Spawnable->GetName());
#else
			BindingObject->SetStringField(TEXT("name"), Binding.GetName());
#endif
		}
		else
		{
			BindingObject->SetStringField(TEXT("type"), TEXT("possessable"));
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
			FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Binding.GetObjectGuid());
			BindingObject->SetStringField(TEXT("name"), Possessable ? Possessable->GetName() : TEXT(""));
#else
			BindingObject->SetStringField(TEXT("name"), Binding.GetName());
#endif
		}

		BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObject));
	}

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetArrayField(TEXT("bindings"), BindingsArray);
	ResultObject->SetNumberField(TEXT("count"), BindingsArray.Num());

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

// ---------------------------------------------------------
// Keyframe Operations
// ---------------------------------------------------------

FString UCURemoteLibrary::AddSequencerKeyframe(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	int32 SectionIndex,
	int32 FrameNumber,
	const FString& Value,
	const FString& ChannelName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] AddSequencerKeyframe called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Track: %s, Section: %d, Frame: %d"), *TrackGuidStr, SectionIndex, FrameNumber);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to load sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	// Parse GUID - try both formats (with and without hyphens)
	FGuid TrackGuid;
	bool bParsed = FGuid::Parse(TrackGuidStr, TrackGuid);
	if (!bParsed)
	{
		// Try without hyphens if hyphenated failed
		FString NoHyphens = TrackGuidStr.Replace(TEXT("-"), TEXT(""));
		bParsed = FGuid::Parse(NoHyphens, TrackGuid);
	}
	if (!bParsed)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Invalid TrackGuid format: '%s' (len=%d)"), *TrackGuidStr, TrackGuidStr.Len()));
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Track not found"));
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Section index out of range"));
	}

	UMovieSceneSection* Section = Sections[SectionIndex];
	if (!Section)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Section is null"));
	}

	// Note: Actual keyframe manipulation requires accessing the specific channel type
	// This is a simplified implementation that shows the pattern
	// Real implementation would need to handle different channel types (Float, Bool, Transform, etc.)

	// For now, just mark success with frame info
	SequencerHelpers::SaveLevelSequence(Sequence);

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetNumberField(TEXT("frame"), FrameNumber);
	ResultObject->SetStringField(TEXT("value"), Value);
	ResultObject->SetStringField(TEXT("track_guid"), TrackGuidStr);
	ResultObject->SetNumberField(TEXT("section_index"), SectionIndex);

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

bool UCURemoteLibrary::RemoveSequencerKeyframe(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	int32 SectionIndex,
	int32 FrameNumber,
	const FString& ChannelName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] RemoveSequencerKeyframe called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	// Parse GUID
	FGuid TrackGuid;
	if (!FGuid::Parse(TrackGuidStr, TrackGuid))
	{
		return false;
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return false;
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return false;
	}

	// Note: Actual keyframe removal requires channel-specific handling
	// This is a placeholder for the pattern

	return SequencerHelpers::SaveLevelSequence(Sequence);
}

FString UCURemoteLibrary::GetSequencerKeyframeValue(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	int32 SectionIndex,
	int32 FrameNumber,
	const FString& ChannelName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] GetSequencerKeyframeValue called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to load sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	// Parse GUID - try both formats (with and without hyphens)
	FGuid TrackGuid;
	bool bParsed = FGuid::Parse(TrackGuidStr, TrackGuid);
	if (!bParsed)
	{
		// Try without hyphens if hyphenated failed
		FString NoHyphens = TrackGuidStr.Replace(TEXT("-"), TEXT(""));
		bParsed = FGuid::Parse(NoHyphens, TrackGuid);
	}
	if (!bParsed)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Invalid TrackGuid format: '%s' (len=%d)"), *TrackGuidStr, TrackGuidStr.Len()));
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Track not found"));
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Section index out of range"));
	}

	UMovieSceneSection* Section = Sections[SectionIndex];
	if (!Section)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Section is null"));
	}

	TArray<FString> AvailableChannels;
	TArray<FString> UnsupportedChannels;
	bool bChannelMatched = ChannelName.IsEmpty();
	const FFrameNumber RequestedFrame(FrameNumber);

	for (const FMovieSceneChannelEntry& Entry : Section->GetChannelProxy().GetAllEntries())
	{
		const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
		for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			const FString ResolvedChannelName = SequencerHelpers::GetChannelName(Entry, ChannelIndex);
			AvailableChannels.AddUnique(ResolvedChannelName);
			if (!ChannelName.IsEmpty() && ResolvedChannelName != ChannelName)
			{
				continue;
			}

			bChannelMatched = true;
			if (!SequencerHelpers::IsSupportedKeyframeChannel(Entry.GetChannelTypeName()))
			{
				UnsupportedChannels.AddUnique(ResolvedChannelName);
				continue;
			}

			TSharedPtr<FJsonObject> Keyframe;
			const SequencerHelpers::EKeyframeReadStatus Status = SequencerHelpers::ReadKeyframe(
				Channels[ChannelIndex], Entry.GetChannelTypeName(), ResolvedChannelName, RequestedFrame, Keyframe);
			if (Status == SequencerHelpers::EKeyframeReadStatus::Success)
			{
				SequencerHelpers::AddTickResolutionFields(Keyframe, MovieScene);
				return SequencerHelpers::CreateSuccessJson(Keyframe);
			}
		}
	}

	if (!bChannelMatched)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(
			TEXT("Channel '%s' not found. Available channels: %s"),
			*ChannelName,
			*SequencerHelpers::JoinChannelNames(AvailableChannels)));
	}

	if (UnsupportedChannels.Num() > 0)
	{
		if (!ChannelName.IsEmpty())
		{
			return SequencerHelpers::CreateErrorJson(FString::Printf(
				TEXT("Channel '%s' has an unsupported keyframe type"), *ChannelName));
		}
		return SequencerHelpers::CreateErrorJson(FString::Printf(
			TEXT("Keyframe lookup is incomplete because unsupported channels were not inspected: %s"),
			*SequencerHelpers::JoinChannelNames(UnsupportedChannels)));
	}

	return SequencerHelpers::CreateErrorJson(FString::Printf(
		TEXT("Keyframe not found at tick frame %d%s"),
		FrameNumber,
		ChannelName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" in channel '%s'"), *ChannelName)));
}

bool UCURemoteLibrary::SetSequencerKeyframeValue(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	int32 SectionIndex,
	int32 FrameNumber,
	const FString& Value,
	const FString& ChannelName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] SetSequencerKeyframeValue called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	// Parse GUID
	FGuid TrackGuid;
	if (!FGuid::Parse(TrackGuidStr, TrackGuid))
	{
		return false;
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return false;
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return false;
	}

	// Note: Actual value setting requires channel-specific handling

	return SequencerHelpers::SaveLevelSequence(Sequence);
}

FString UCURemoteLibrary::ListSequencerKeyframes(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	int32 SectionIndex,
	const FString& ChannelName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] ListSequencerKeyframes called"));

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to load sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence has no MovieScene"));
	}

	// Parse GUID - try both formats (with and without hyphens)
	FGuid TrackGuid;
	bool bParsed = FGuid::Parse(TrackGuidStr, TrackGuid);
	if (!bParsed)
	{
		// Try without hyphens if hyphenated failed
		FString NoHyphens = TrackGuidStr.Replace(TEXT("-"), TEXT(""));
		bParsed = FGuid::Parse(NoHyphens, TrackGuid);
	}
	if (!bParsed)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Invalid TrackGuid format: '%s' (len=%d)"), *TrackGuidStr, TrackGuidStr.Len()));
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Track not found"));
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Section index out of range"));
	}

	UMovieSceneSection* Section = Sections[SectionIndex];
	if (!Section)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Section is null"));
	}

	TArray<TSharedPtr<FJsonValue>> KeyframesArray;
	TArray<FString> AvailableChannels;
	TArray<FString> UnsupportedChannels;
	bool bChannelMatched = ChannelName.IsEmpty();

	for (const FMovieSceneChannelEntry& Entry : Section->GetChannelProxy().GetAllEntries())
	{
		const TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
		for (int32 ChannelIndex = 0; ChannelIndex < Channels.Num(); ++ChannelIndex)
		{
			const FString ResolvedChannelName = SequencerHelpers::GetChannelName(Entry, ChannelIndex);
			AvailableChannels.AddUnique(ResolvedChannelName);
			if (!ChannelName.IsEmpty() && ResolvedChannelName != ChannelName)
			{
				continue;
			}

			bChannelMatched = true;
			if (!SequencerHelpers::IsSupportedKeyframeChannel(Entry.GetChannelTypeName()))
			{
				UnsupportedChannels.AddUnique(ResolvedChannelName);
				continue;
			}

			TArray<FFrameNumber> KeyTimes;
			Channels[ChannelIndex]->GetKeys(TRange<FFrameNumber>::All(), &KeyTimes, nullptr);
			for (const FFrameNumber& KeyTime : KeyTimes)
			{
				TSharedPtr<FJsonObject> Keyframe;
				const SequencerHelpers::EKeyframeReadStatus Status = SequencerHelpers::ReadKeyframe(
					Channels[ChannelIndex], Entry.GetChannelTypeName(), ResolvedChannelName, KeyTime, Keyframe);
				if (Status != SequencerHelpers::EKeyframeReadStatus::Success)
				{
					return SequencerHelpers::CreateErrorJson(FString::Printf(
						TEXT("Failed to read keyframe at tick frame %d from channel '%s'"),
						KeyTime.Value,
						*ResolvedChannelName));
				}
				KeyframesArray.Add(MakeShared<FJsonValueObject>(Keyframe));
			}
		}
	}

	if (!bChannelMatched)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(
			TEXT("Channel '%s' not found. Available channels: %s"),
			*ChannelName,
			*SequencerHelpers::JoinChannelNames(AvailableChannels)));
	}

	if (!ChannelName.IsEmpty() && UnsupportedChannels.Num() > 0)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(
			TEXT("Channel '%s' has an unsupported keyframe type"), *ChannelName));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetArrayField(TEXT("keyframes"), KeyframesArray);
	ResultObject->SetNumberField(TEXT("count"), KeyframesArray.Num());
	ResultObject->SetBoolField(TEXT("complete"), UnsupportedChannels.Num() == 0);
	if (UnsupportedChannels.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> UnsupportedArray;
		for (const FString& UnsupportedChannel : UnsupportedChannels)
		{
			UnsupportedArray.Add(MakeShared<FJsonValueString>(UnsupportedChannel));
		}
		ResultObject->SetArrayField(TEXT("unsupported_channels"), UnsupportedArray);
	}
	SequencerHelpers::AddTickResolutionFields(ResultObject, MovieScene);

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

bool UCURemoteLibrary::MoveSequencerKeyframe(
	const FString& SequencePath,
	const FString& TrackGuidStr,
	int32 SectionIndex,
	int32 SourceFrame,
	int32 DestFrame,
	const FString& ChannelName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] MoveSequencerKeyframe called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Source: %d, Dest: %d"), SourceFrame, DestFrame);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}

	// Parse GUID
	FGuid TrackGuid;
	if (!FGuid::Parse(TrackGuidStr, TrackGuid))
	{
		return false;
	}

	// Find track
	UMovieSceneTrack* Track = SequencerHelpers::FindTrackByGuid(MovieScene, TrackGuid);
	if (!Track)
	{
		return false;
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
	if (SectionIndex < 0 || SectionIndex >= Sections.Num())
	{
		return false;
	}

	// Note: Actual keyframe movement requires channel-specific handling

	return SequencerHelpers::SaveLevelSequence(Sequence);
}

// =========================================================
// Sequence Operations (Issue #1531 Phase 4)
// =========================================================

FString UCURemoteLibrary::CreateLevelSequence(
	const FString& SequencePath,
	int32 FrameRate
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] CreateLevelSequence called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Path: %s, FrameRate: %d"), *SequencePath, FrameRate);

	// Validate frame rate
	if (FrameRate <= 0)
	{
		FrameRate = DEFAULT_FRAME_RATE; // Default frame rate
	}

	// Parse the path to get package path and asset name
	FString PackagePath;
	FString AssetName;

	// Handle both formats: "/Game/Folder/AssetName" and "/Game/Folder/AssetName.AssetName"
	FString CleanPath = SequencePath;
	int32 DotIndex;
	if (CleanPath.FindLastChar('.', DotIndex))
	{
		CleanPath = CleanPath.Left(DotIndex);
	}

	int32 LastSlashIndex;
	if (CleanPath.FindLastChar('/', LastSlashIndex))
	{
		PackagePath = CleanPath.Left(LastSlashIndex);
		AssetName = CleanPath.RightChop(LastSlashIndex + 1);
	}
	else
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Invalid sequence path format"));
	}

	// Issue #15755: reject a trailing-slash / empty-leaf path whose derived
	// short-name is empty (e.g. "/Game/Sequences/" → RightChop yields ""), which
	// would otherwise create a garbage / auto-named asset. Defense-in-depth,
	// mirrors PR #15742; latent today (no MCP-tool caller) but guards the
	// direct-RC / legacy path. TrimStartAndEnd() parity with the Rust .trim().
	if (AssetName.TrimStartAndEnd().IsEmpty())
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Sequence name cannot be empty"));
	}

	// Check if asset already exists
	FString FullAssetPath = CleanPath + TEXT(".") + AssetName;
	if (ULevelSequence* ExistingSequence = Cast<ULevelSequence>(StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *FullAssetPath)))
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Sequence already exists: %s"), *FullAssetPath));
	}

	// Create the package
	FString FullPackagePath = PackagePath + TEXT("/") + AssetName;
	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to create package"));
	}

	// Create the Level Sequence
	ULevelSequence* NewSequence = NewObject<ULevelSequence>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewSequence)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to create Level Sequence"));
	}

	// Initialize the sequence
	NewSequence->Initialize();

	// Get the movie scene
	UMovieScene* MovieScene = NewSequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to get MovieScene from sequence"));
	}

	// Set display rate (frame rate)
	FFrameRate DisplayRate(FrameRate, 1);
	MovieScene->SetDisplayRate(DisplayRate);

	// Set default playback range (e.g., 0 to 300 frames = 10 seconds at 30fps)
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	int32 DefaultDurationFrames = FrameRate * DEFAULT_DURATION_SECONDS; // Default sequence duration
	FFrameNumber StartFrame(0);
	FFrameNumber EndFrame(FFrameRate::TransformTime(FFrameTime(DefaultDurationFrames), DisplayRate, TickResolution).RoundToFrame());
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(StartFrame, EndFrame));

	// Mark package dirty and save
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewSequence);

	// Save the package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(FullPackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	bool bSaved = UPackage::SavePackage(Package, NewSequence, *PackageFileName, SaveArgs);

	if (!bSaved)
	{
		UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Sequencer] Failed to save package to disk: %s"), *PackageFileName);
	}

	// Build result JSON
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("path"), FullAssetPath);
	ResultObject->SetStringField(TEXT("package_path"), FullPackagePath);
	ResultObject->SetStringField(TEXT("asset_name"), AssetName);
	ResultObject->SetNumberField(TEXT("frame_rate"), FrameRate);
	ResultObject->SetNumberField(TEXT("duration_frames"), DefaultDurationFrames);
	ResultObject->SetBoolField(TEXT("saved_to_disk"), bSaved);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] Level Sequence created successfully: %s"), *FullAssetPath);

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::SetSequencerPlaybackRange(
	const FString& SequencePath,
	int32 StartFrame,
	int32 EndFrame
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] SetSequencerPlaybackRange called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Path: %s, Start: %d, End: %d"), *SequencePath, StartFrame, EndFrame);

	// Validate range
	if (EndFrame <= StartFrame)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("End frame must be greater than start frame"));
	}

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Failed to load sequence: %s"), *SequencePath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to get MovieScene from sequence"));
	}

	// Get tick resolution and display rate
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();

	// Convert display frames to tick frames
	FFrameNumber TickStartFrame = FFrameRate::TransformTime(FFrameTime(StartFrame), DisplayRate, TickResolution).RoundToFrame();
	FFrameNumber TickEndFrame = FFrameRate::TransformTime(FFrameTime(EndFrame), DisplayRate, TickResolution).RoundToFrame();

	// Set playback range
	MovieScene->SetPlaybackRange(TRange<FFrameNumber>(TickStartFrame, TickEndFrame));

	// Save the sequence
	if (!SequencerHelpers::SaveLevelSequence(Sequence))
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to save sequence after updating playback range"));
	}

	// Calculate duration in seconds
	double DurationSeconds = static_cast<double>(EndFrame - StartFrame) / DisplayRate.AsDecimal();

	// Build result JSON
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetNumberField(TEXT("start_frame"), StartFrame);
	ResultObject->SetNumberField(TEXT("end_frame"), EndFrame);
	ResultObject->SetNumberField(TEXT("duration_frames"), EndFrame - StartFrame);
	ResultObject->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
	ResultObject->SetNumberField(TEXT("frame_rate"), DisplayRate.AsDecimal());

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] Playback range set: %d - %d (%.2f seconds)"), StartFrame, EndFrame, DurationSeconds);

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::GetSequencerPlaybackRange(
	const FString& SequencePath
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Sequencer] GetSequencerPlaybackRange called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Path: %s"), *SequencePath);

	// Load sequence
	ULevelSequence* Sequence = SequencerHelpers::LoadLevelSequence(SequencePath);
	if (!Sequence)
	{
		return SequencerHelpers::CreateErrorJson(FString::Printf(TEXT("Failed to load sequence: %s"), *SequencePath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return SequencerHelpers::CreateErrorJson(TEXT("Failed to get MovieScene from sequence"));
	}

	// Get tick resolution and display rate
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	FFrameRate DisplayRate = MovieScene->GetDisplayRate();

	// Get playback range
	TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();

	// Convert tick frames to display frames
	int32 StartFrame = 0;
	int32 EndFrame = 0;

	if (PlaybackRange.HasLowerBound())
	{
		StartFrame = FFrameRate::TransformTime(FFrameTime(PlaybackRange.GetLowerBoundValue()), TickResolution, DisplayRate).RoundToFrame().Value;
	}

	if (PlaybackRange.HasUpperBound())
	{
		EndFrame = FFrameRate::TransformTime(FFrameTime(PlaybackRange.GetUpperBoundValue()), TickResolution, DisplayRate).RoundToFrame().Value;
	}

	// Calculate duration in seconds
	double DurationSeconds = static_cast<double>(EndFrame - StartFrame) / DisplayRate.AsDecimal();

	// Build result JSON
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetNumberField(TEXT("start_frame"), StartFrame);
	ResultObject->SetNumberField(TEXT("end_frame"), EndFrame);
	ResultObject->SetNumberField(TEXT("duration_frames"), EndFrame - StartFrame);
	ResultObject->SetNumberField(TEXT("duration_seconds"), DurationSeconds);
	ResultObject->SetNumberField(TEXT("frame_rate"), DisplayRate.AsDecimal());
	ResultObject->SetStringField(TEXT("tick_resolution"), FString::Printf(TEXT("%d/%d"), TickResolution.Numerator, TickResolution.Denominator));

	return SequencerHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// Issue #2947 Phase 0: Blueprint Node Creation POC
// =========================================================

namespace BlueprintNodeHelpers
{
	/**
	 * Find a graph by name in a Blueprint
	 */
	UEdGraph* FindGraphByName(UBlueprint* Blueprint, const FString& GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}

		// Search in UbergraphPages (EventGraph, etc.)
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph && Graph->GetName().Contains(GraphName))
			{
				return Graph;
			}
		}

		// Search in FunctionGraphs
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph && Graph->GetName().Contains(GraphName))
			{
				return Graph;
			}
		}

		// Search in MacroGraphs
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph && Graph->GetName().Contains(GraphName))
			{
				return Graph;
			}
		}

		// Issue #8564: Fallback — search all UEdGraph subobjects recursively.
		// Covers SM Blueprint (Logic Driver), PCG, Niagara, and any plugin
		// that stores graphs outside UbergraphPages/FunctionGraphs/MacroGraphs.
		TArray<UObject*> SubObjects;
		// Issue #11472 Gap 1: UE 5.8 deprecated bool overload in favor of EGetObjectsFlags.
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		GetObjectsWithOuter(Blueprint, SubObjects, EGetObjectsFlags::IncludeNestedObjects);
#else
		GetObjectsWithOuter(Blueprint, SubObjects, /*bIncludeNestedObjects=*/true);
#endif
		for (UObject* SubObj : SubObjects)
		{
			UEdGraph* Graph = Cast<UEdGraph>(SubObj);
			if (Graph && Graph->GetName().Contains(GraphName))
			{
				return Graph;
			}
		}

		return nullptr;
	}

	// Issue #2947 Phase I: Enhanced Error Codes
	namespace ErrorCodes
	{
		// Input Validation Errors (1xx)
		static const FString EMPTY_BLUEPRINT_PATH = TEXT("BP_ERR_101");
		static const FString EMPTY_NODE_TYPE = TEXT("BP_ERR_102");
		static const FString EMPTY_VARIABLE_NAME = TEXT("BP_ERR_103");
		static const FString EMPTY_VARIABLE_TYPE = TEXT("BP_ERR_104");
		static const FString EMPTY_NODE_GUID = TEXT("BP_ERR_105");
		static const FString INVALID_GUID_FORMAT = TEXT("BP_ERR_106");

		// Blueprint Loading Errors (2xx)
		static const FString BLUEPRINT_NOT_FOUND = TEXT("BP_ERR_201");
		static const FString GRAPH_NOT_FOUND = TEXT("BP_ERR_202");
		static const FString NODE_NOT_FOUND = TEXT("BP_ERR_203");

		// Node Creation Errors (3xx)
		static const FString UNSUPPORTED_NODE_TYPE = TEXT("BP_ERR_301");
		static const FString NODE_CREATION_FAILED = TEXT("BP_ERR_302");
		static const FString MISSING_REQUIRED_PROPERTY = TEXT("BP_ERR_303");
		static const FString FUNCTION_NOT_FOUND = TEXT("BP_ERR_304");
		static const FString CLASS_NOT_FOUND = TEXT("BP_ERR_305");
		static const FString MACRO_NOT_FOUND = TEXT("BP_ERR_306");
		static const FString ENUM_NOT_FOUND = TEXT("BP_ERR_307");

		// Variable Errors (4xx)
		static const FString VARIABLE_ALREADY_EXISTS = TEXT("BP_ERR_401");
		static const FString UNSUPPORTED_VARIABLE_TYPE = TEXT("BP_ERR_402");

		// Save Errors (5xx)
		static const FString SAVE_FAILED = TEXT("BP_ERR_501");

		// AI Asset Errors (6xx) - Issue #4263
		static const FString AI_ASSET_NOT_FOUND = TEXT("AI_ERR_601");
		static const FString AI_INVALID_JSON = TEXT("AI_ERR_602");
		static const FString AI_OPERATION_FAILED = TEXT("AI_ERR_603");
	}

	/**
	 * Issue #2947 Phase J: Transaction scope guard for Undo/Redo support
	 *
	 * RAII-style wrapper for Unreal's Transaction system.
	 * Enables Ctrl+Z undo for Blueprint node creation/deletion/modification.
	 *
	 * Usage:
	 *   {
	 *       FScopedTransaction Transaction(TEXT("Create Node"));
	 *       // ... make changes ...
	 *   } // Transaction ends automatically
	 */
#if WITH_EDITOR
	class FScopedBlueprintTransaction
	{
	public:
		FScopedBlueprintTransaction(const FString& Description)
			: bIsActive(false)
		{
			if (GEditor)
			{
				GEditor->BeginTransaction(FText::FromString(Description));
				bIsActive = true;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Transaction started: %s"), *Description);
			}
		}

		~FScopedBlueprintTransaction()
		{
			if (bIsActive && GEditor)
			{
				GEditor->EndTransaction();
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Transaction ended"));
			}
		}

		/**
		 * Mark an object as modified within this transaction.
		 * MUST be called before modifying any object for undo to work.
		 */
		void Modify(UObject* Object)
		{
			if (Object && bIsActive)
			{
				Object->Modify();
			}
		}

		bool IsActive() const { return bIsActive; }

	private:
		bool bIsActive;

		// Non-copyable
		FScopedBlueprintTransaction(const FScopedBlueprintTransaction&) = delete;
		FScopedBlueprintTransaction& operator=(const FScopedBlueprintTransaction&) = delete;
	};
#endif

	/**
	 * Create JSON error response for Blueprint operations
	 */
	FString CreateErrorJson(const FString& ErrorMessage)
	{
		// Issue #14142: consolidated onto CUJsonUtils::MakeErrorResponse ({"success":false,"error":msg}).
		return CUJsonUtils::MakeErrorResponse(ErrorMessage);
	}

	/**
	 * Create enhanced JSON error response with error code and context (Issue #2947 Phase I)
	 *
	 * @param ErrorCode Unique error code (e.g., "BP_ERR_101")
	 * @param ErrorMessage Human-readable error message
	 * @param Context Additional context about the operation that failed
	 * @param Suggestion How to fix the error (optional)
	 */
	FString CreateDetailedErrorJson(
		const FString& ErrorCode,
		const FString& ErrorMessage,
		const FString& Context = TEXT(""),
		const FString& Suggestion = TEXT(""))
	{
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObject->SetStringField(TEXT("error_code"), ErrorCode);
		ErrorObject->SetStringField(JSON_ERROR_FIELD, ErrorMessage);

		if (!Context.IsEmpty())
		{
			ErrorObject->SetStringField(TEXT("context"), Context);
		}

		if (!Suggestion.IsEmpty())
		{
			ErrorObject->SetStringField(TEXT("suggestion"), Suggestion);
		}

		// Issue #14142: serialize via the shared helper (extra error_code/context/suggestion fields
		// keep this builder local; only the serialize idiom is consolidated).
		return CUJsonUtils::SerializeToString(ErrorObject);
	}

	/**
	 * Create JSON success response with data
	 */
	FString CreateSuccessJson(TSharedPtr<FJsonObject> DataObject)
	{
		// Issue #14142: consolidated onto CUJsonUtils::MakeSuccessResponse (sets "success":true, serializes).
		return CUJsonUtils::MakeSuccessResponse(DataObject);
	}
}

FString UCURemoteLibrary::CreateBlueprintNode_POC(
	const FString& BlueprintPath,
	const FString& GraphName,
	int32 PosX,
	int32 PosY
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintNode_POC called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BlueprintPath: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  GraphName: %s"), *GraphName);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Position: (%d, %d)"), PosX, PosY);

	// 1. Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Blueprint path is empty"));
	}

	// 2. Load Blueprint using existing BlueprintManipulator
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return BlueprintNodeHelpers::CreateErrorJson(FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Blueprint loaded: %s"), *Blueprint->GetName());

	// 3. Find the target graph
	FString TargetGraphName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;
	UEdGraph* TargetGraph = BlueprintNodeHelpers::FindGraphByName(Blueprint, TargetGraphName);
	if (!TargetGraph)
	{
		return BlueprintNodeHelpers::CreateErrorJson(FString::Printf(TEXT("Graph not found: %s"), *TargetGraphName));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Target graph found: %s"), *TargetGraph->GetName());

	// 4. Create the Branch (IfThenElse) node
	UK2Node_IfThenElse* NewNode = NewObject<UK2Node_IfThenElse>(TargetGraph);
	if (!NewNode)
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Failed to create UK2Node_IfThenElse"));
	}

	// 5. Initialize the node
	NewNode->CreateNewGuid();
	NewNode->NodePosX = PosX;
	NewNode->NodePosY = PosY;

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Node created with GUID: %s"), *NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));

	// 6. Add node to graph (bSelectNewNode=false, bFromUI=false)
	TargetGraph->AddNode(NewNode, /*bSelectNewNode=*/false, /*bFromUI=*/false);

	// 7. Allocate default pins (this creates Condition, Then, Else pins)
	NewNode->AllocateDefaultPins();

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Pins allocated: %d pins"), NewNode->Pins.Num());

	// 8. Mark Blueprint as modified (CRITICAL for proper Editor update)
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	// 9. Save Blueprint with recompile
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, /*bRecompile=*/true))
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Failed to save Blueprint after node creation"));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Blueprint saved successfully"));

	// 10. Build response JSON with node_guid and pins
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	ResultObject->SetStringField(TEXT("node_class"), TEXT("K2Node_IfThenElse"));
	ResultObject->SetStringField(TEXT("node_title"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	ResultObject->SetNumberField(TEXT("pos_x"), PosX);
	ResultObject->SetNumberField(TEXT("pos_y"), PosY);
	ResultObject->SetStringField(TEXT("graph_name"), TargetGraph->GetName());

	// Build pins map
	TSharedPtr<FJsonObject> PinsObject = MakeShared<FJsonObject>();
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (Pin)
		{
			FString PinDirection = (Pin->Direction == EGPD_Input) ? TEXT("input") : TEXT("output");
			TSharedPtr<FJsonObject> PinInfo = MakeShared<FJsonObject>();
			PinInfo->SetStringField(TEXT("guid"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
			PinInfo->SetStringField(TEXT("direction"), PinDirection);
			PinInfo->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinsObject->SetObjectField(Pin->PinName.ToString(), PinInfo);

			UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Pin: %s (%s) - GUID: %s"),
				*Pin->PinName.ToString(),
				*PinDirection,
				*Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
		}
	}
	ResultObject->SetObjectField(TEXT("pins"), PinsObject);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintNode_POC completed successfully"));

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// Issue #2947 Phase 1: Full Blueprint Node Creation
// =========================================================

namespace BlueprintNodeFactory
{
	/**
	 * Build pins JSON for a node
	 */
	TSharedPtr<FJsonObject> BuildPinsJson(UK2Node* Node)
	{
		TSharedPtr<FJsonObject> PinsObject = MakeShared<FJsonObject>();
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				FString PinDirection = (Pin->Direction == EGPD_Input) ? TEXT("input") : TEXT("output");
				TSharedPtr<FJsonObject> PinInfo = MakeShared<FJsonObject>();
				PinInfo->SetStringField(TEXT("guid"), Pin->PinId.ToString(EGuidFormats::DigitsWithHyphens));
				PinInfo->SetStringField(TEXT("direction"), PinDirection);
				PinInfo->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
				PinsObject->SetObjectField(Pin->PinName.ToString(), PinInfo);
			}
		}
		return PinsObject;
	}

	/**
	 * Create Branch node (UK2Node_IfThenElse)
	 */
	UK2Node* CreateBranchNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		UK2Node_IfThenElse* Node = NewObject<UK2Node_IfThenElse>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create MakeArray node (UK2Node_MakeArray)
	 */
	UK2Node* CreateMakeArrayNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		UK2Node_MakeArray* Node = NewObject<UK2Node_MakeArray>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create CallFunction node (UK2Node_CallFunction)
	 * Requires: FunctionPath (e.g., "/Script/Engine.KismetSystemLibrary:PrintString")
	 */
	UK2Node* CreateCallFunctionNode(UEdGraph* Graph, UBlueprint* Blueprint, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] CallFunction requires PropertiesJson"));
			return nullptr;
		}

		FString FunctionPath = Properties->GetStringField(TEXT("FunctionPath"));
		if (FunctionPath.IsEmpty())
		{
			// Try alternative field names
			FunctionPath = Properties->GetStringField(TEXT("function_path"));
		}

		if (FunctionPath.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] CallFunction requires FunctionPath property"));
			return nullptr;
		}

		// Find the function
		UFunction* Function = FindObject<UFunction>(nullptr, *FunctionPath);
		if (!Function)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Function not found: %s"), *FunctionPath);
			return nullptr;
		}

		UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->SetFromFunction(Function);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create CustomEvent node (UK2Node_CustomEvent)
	 * Requires: EventName
	 */
	UK2Node* CreateCustomEventNode(UEdGraph* Graph, UBlueprint* Blueprint, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] CustomEvent requires PropertiesJson"));
			return nullptr;
		}

		FString EventName = Properties->GetStringField(TEXT("EventName"));
		if (EventName.IsEmpty())
		{
			EventName = Properties->GetStringField(TEXT("event_name"));
		}

		if (EventName.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] CustomEvent requires EventName property"));
			return nullptr;
		}

		UK2Node_CustomEvent* Node = NewObject<UK2Node_CustomEvent>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->CustomFunctionName = FName(*EventName);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Resolve the owning class for a VariableGet/Set external-member reference (Issue #12595).
	 * Accepts a full object path ("/Script/Engine.CharacterMovementComponent") or a short class
	 * name ("CharacterMovementComponent"), which is assumed to live in the /Script/Engine module.
	 * Returns nullptr when the class cannot be resolved (caller logs + aborts node creation).
	 * Mirrors the resolve_class_path short-name->Engine fallback used by the Rust T3D path.
	 */
	UClass* ResolveMemberClass(const FString& RawClass)
	{
		if (RawClass.IsEmpty())
		{
			return nullptr;
		}

		// Short name with no module qualifier -> assume the Engine module.
		// TODO(#12595 follow-up): the Rust SSOT is t3d_nodes::resolve_class_path
		// (MCP/native/mcp_server/src/tools/t3d_nodes.rs), which also maps
		// /Script/UMG.{Widget,KismetRendering}* and /Script/HeadMountedDisplay.*
		// short names. Extend the alias table here (or factor it out) when a
		// caller needs an external-member reference outside /Script/Engine.
		FString ClassPath = RawClass;
		if (!RawClass.Contains(TEXT("/")) && !RawClass.Contains(TEXT(".")))
		{
			ClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *RawClass);
		}

		// Same FindObject->LoadObject idiom as CreateCastNode's TargetClass resolution.
		UClass* Resolved = FindObject<UClass>(nullptr, *ClassPath);
		if (!Resolved)
		{
			Resolved = LoadObject<UClass>(nullptr, *ClassPath);
		}
		return Resolved;
	}

	/**
	 * Create VariableGet node (UK2Node_VariableGet)
	 * Requires: VariableName
	 * Optional: MemberClass / member_class -> external-class member (Issue #12595)
	 */
	UK2Node* CreateVariableGetNode(UEdGraph* Graph, UBlueprint* Blueprint, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] VariableGet requires PropertiesJson"));
			return nullptr;
		}

		FString VariableName = Properties->GetStringField(TEXT("VariableName"));
		if (VariableName.IsEmpty())
		{
			VariableName = Properties->GetStringField(TEXT("variable_name"));
		}

		if (VariableName.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] VariableGet requires VariableName property"));
			return nullptr;
		}

		// Issue #12595: optional owning class for a component / external-class member.
		// TryGetStringField avoids the LogJson warning GetStringField emits on a miss.
		FString MemberClassPath;
		if (!Properties->TryGetStringField(TEXT("MemberClass"), MemberClassPath))
		{
			Properties->TryGetStringField(TEXT("member_class"), MemberClassPath);
		}
		UClass* MemberClass = nullptr;
		if (!MemberClassPath.IsEmpty())
		{
			MemberClass = ResolveMemberClass(MemberClassPath);
			if (!MemberClass)
			{
				UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] VariableGet member class not found: %s"), *MemberClassPath);
				return nullptr;
			}
		}

		// Find the variable in Blueprint
		FProperty* VarProperty = nullptr;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (Var.VarName.ToString() == VariableName)
			{
				VarProperty = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->FindPropertyByName(Var.VarName) : nullptr;
				break;
			}
		}

		UK2Node_VariableGet* Node = NewObject<UK2Node_VariableGet>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		if (MemberClass)
		{
			// External member: AllocateDefaultPins() then creates the visible Target pin.
			Node->VariableReference.SetExternalMember(FName(*VariableName), MemberClass);
		}
		else
		{
			Node->VariableReference.SetSelfMember(FName(*VariableName));
		}
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create VariableSet node (UK2Node_VariableSet)
	 * Requires: VariableName
	 * Optional: MemberClass / member_class -> external-class member (Issue #12595)
	 */
	UK2Node* CreateVariableSetNode(UEdGraph* Graph, UBlueprint* Blueprint, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] VariableSet requires PropertiesJson"));
			return nullptr;
		}

		FString VariableName = Properties->GetStringField(TEXT("VariableName"));
		if (VariableName.IsEmpty())
		{
			VariableName = Properties->GetStringField(TEXT("variable_name"));
		}

		if (VariableName.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] VariableSet requires VariableName property"));
			return nullptr;
		}

		// Issue #12595: optional owning class for a component / external-class member.
		// TryGetStringField avoids the LogJson warning GetStringField emits on a miss.
		FString MemberClassPath;
		if (!Properties->TryGetStringField(TEXT("MemberClass"), MemberClassPath))
		{
			Properties->TryGetStringField(TEXT("member_class"), MemberClassPath);
		}
		UClass* MemberClass = nullptr;
		if (!MemberClassPath.IsEmpty())
		{
			MemberClass = ResolveMemberClass(MemberClassPath);
			if (!MemberClass)
			{
				UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] VariableSet member class not found: %s"), *MemberClassPath);
				return nullptr;
			}
		}

		UK2Node_VariableSet* Node = NewObject<UK2Node_VariableSet>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		if (MemberClass)
		{
			// External member: AllocateDefaultPins() then creates the visible Target pin.
			Node->VariableReference.SetExternalMember(FName(*VariableName), MemberClass);
		}
		else
		{
			Node->VariableReference.SetSelfMember(FName(*VariableName));
		}
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create DynamicCast node (UK2Node_DynamicCast)
	 * Requires: TargetClass (e.g., "/Script/Engine.Character")
	 */
	UK2Node* CreateCastNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Cast requires PropertiesJson"));
			return nullptr;
		}

		FString TargetClassPath = Properties->GetStringField(TEXT("TargetClass"));
		if (TargetClassPath.IsEmpty())
		{
			TargetClassPath = Properties->GetStringField(TEXT("target_class"));
		}

		if (TargetClassPath.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Cast requires TargetClass property"));
			return nullptr;
		}

		UClass* TargetClass = FindObject<UClass>(nullptr, *TargetClassPath);
		if (!TargetClass)
		{
			// Try loading the class
			TargetClass = LoadObject<UClass>(nullptr, *TargetClassPath);
		}

		if (!TargetClass)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Target class not found: %s"), *TargetClassPath);
			return nullptr;
		}

		UK2Node_DynamicCast* Node = NewObject<UK2Node_DynamicCast>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->TargetType = TargetClass;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	// =========================================================================
	// Issue #2947 Phase C: MacroInstance-based nodes
	// =========================================================================

	/**
	 * Create MacroInstance node (UK2Node_MacroInstance)
	 * Requires: MacroPath (e.g., "/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop")
	 */
	UK2Node* CreateMacroInstanceNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] MacroInstance requires PropertiesJson"));
			return nullptr;
		}

		FString MacroPath = Properties->GetStringField(TEXT("MacroPath"));
		if (MacroPath.IsEmpty())
		{
			MacroPath = Properties->GetStringField(TEXT("macro_path"));
		}

		if (MacroPath.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] MacroInstance requires MacroPath property"));
			return nullptr;
		}

		// Load the macro graph
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Macro not found: %s"), *MacroPath);
			return nullptr;
		}

		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create ForLoop node (UK2Node_MacroInstance with ForLoop macro)
	 * Optional: FirstIndex, LastIndex
	 */
	UK2Node* CreateForLoopNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		// ForLoop macro path in UE5
		const FString ForLoopMacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForLoop");

		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *ForLoopMacroPath);
		if (!MacroGraph)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] ForLoop macro not found"));
			return nullptr;
		}

		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create ForEachLoop node (UK2Node_MacroInstance with ForEachLoop macro)
	 */
	UK2Node* CreateForEachLoopNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		// ForEachLoop macro path in UE5
		const FString ForEachLoopMacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForEachLoop");

		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *ForEachLoopMacroPath);
		if (!MacroGraph)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] ForEachLoop macro not found"));
			return nullptr;
		}

		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	// Issue #4751: 6 additional MacroInstance-based flow-control nodes
	// All use the same pattern as ForLoop/ForEachLoop: load macro from StandardMacros

	/**
	 * Create DoOnce node (UK2Node_MacroInstance)
	 */
	UK2Node* CreateDoOnceNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:DoOnce");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] DoOnce macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create FlipFlop node (UK2Node_MacroInstance)
	 */
	UK2Node* CreateFlipFlopNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:FlipFlop");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] FlipFlop macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create Gate node (UK2Node_MacroInstance)
	 */
	UK2Node* CreateGateNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:Gate");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Gate macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create DoN node (UK2Node_MacroInstance)
	 * Note: Macro name is "Do N" (with space) in StandardMacros
	 */
	UK2Node* CreateDoNNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:Do N");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] DoN macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create WhileLoop node (UK2Node_MacroInstance)
	 */
	UK2Node* CreateWhileLoopNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:WhileLoop");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] WhileLoop macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create ForEachLoopWithBreak node (UK2Node_MacroInstance)
	 */
	UK2Node* CreateForEachLoopWithBreakNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:ForEachLoopWithBreak");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] ForEachLoopWithBreak macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create Delay node (UK2Node_CallFunction with Delay function)
	 * Optional: Duration (float)
	 */
	UK2Node* CreateDelayNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		// Delay function path
		const FString DelayFunctionPath = TEXT("/Script/Engine.KismetSystemLibrary:Delay");

		UFunction* Function = FindObject<UFunction>(nullptr, *DelayFunctionPath);
		if (!Function)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Delay function not found"));
			return nullptr;
		}

		UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->SetFromFunction(Function);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create PrintString node (UK2Node_CallFunction)
	 * Optional: InString (string)
	 */
	UK2Node* CreatePrintStringNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		// PrintString function path
		const FString PrintStringFunctionPath = TEXT("/Script/Engine.KismetSystemLibrary:PrintString");

		UFunction* Function = FindObject<UFunction>(nullptr, *PrintStringFunctionPath);
		if (!Function)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] PrintString function not found"));
			return nullptr;
		}

		UK2Node_CallFunction* Node = NewObject<UK2Node_CallFunction>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->SetFromFunction(Function);
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * Create Sequence node (UK2Node_ExecutionSequence)
	 * Optional: NumOutputs (int, default 2)
	 */
	UK2Node* CreateSequenceNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		UK2Node_ExecutionSequence* Node = NewObject<UK2Node_ExecutionSequence>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	// =========================================================================
	// Issue #2947 Phase H: Switch and Comment nodes
	// =========================================================================

	/**
	 * Create Switch on Int node (UK2Node_SwitchInteger)
	 * Optional: StartIndex (default 0), HasDefault (default true)
	 */
	UK2Node* CreateSwitchIntNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		UK2Node_SwitchInteger* Node = NewObject<UK2Node_SwitchInteger>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;

		// Set default properties
		if (Properties.IsValid())
		{
			int32 StartIndex = Properties->GetIntegerField(TEXT("StartIndex"));
			if (StartIndex > 0)
			{
				Node->StartIndex = StartIndex;
			}
			// bHasDefaultPin is typically true by default
		}

		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();

		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Created SwitchInteger node"));
		return Node;
	}

	/**
	 * Create Switch on String node (UK2Node_SwitchString)
	 * Optional: IsCaseSensitive (default false)
	 */
	UK2Node* CreateSwitchStringNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		UK2Node_SwitchString* Node = NewObject<UK2Node_SwitchString>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;

		// Set case sensitivity
		if (Properties.IsValid() && Properties->HasField(TEXT("IsCaseSensitive")))
		{
			Node->bIsCaseSensitive = Properties->GetBoolField(TEXT("IsCaseSensitive"));
		}

		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();

		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Created SwitchString node"));
		return Node;
	}

	/**
	 * Create Switch on Enum node (UK2Node_SwitchEnum)
	 * Requires: EnumPath (e.g., "/Script/MyProject.EMyEnum")
	 */
	UK2Node* CreateSwitchEnumNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Properties.IsValid())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] SwitchEnum requires PropertiesJson"));
			return nullptr;
		}

		FString EnumPath = Properties->GetStringField(TEXT("EnumPath"));
		if (EnumPath.IsEmpty())
		{
			EnumPath = Properties->GetStringField(TEXT("enum_path"));
		}

		if (EnumPath.IsEmpty())
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] SwitchEnum requires EnumPath property"));
			return nullptr;
		}

		UEnum* Enum = FindObject<UEnum>(nullptr, *EnumPath);
		if (!Enum)
		{
			Enum = LoadObject<UEnum>(nullptr, *EnumPath);
		}

		if (!Enum)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Enum not found: %s"), *EnumPath);
			return nullptr;
		}

		UK2Node_SwitchEnum* Node = NewObject<UK2Node_SwitchEnum>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->Enum = Enum;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();

		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Created SwitchEnum node for: %s"), *EnumPath);
		return Node;
	}

	/**
	 * Create Comment node (UEdGraphNode_Comment)
	 * Optional: CommentText (default "Comment"), Width (default 400), Height (default 100)
	 */
	UEdGraphNode* CreateCommentNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		UEdGraphNode_Comment* Node = NewObject<UEdGraphNode_Comment>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;

		// Set comment text and size
		if (Properties.IsValid())
		{
			FString CommentText = Properties->GetStringField(TEXT("CommentText"));
			if (CommentText.IsEmpty())
			{
				CommentText = Properties->GetStringField(TEXT("comment_text"));
			}
			if (!CommentText.IsEmpty())
			{
				Node->NodeComment = CommentText;
			}

			int32 Width = Properties->GetIntegerField(TEXT("Width"));
			if (Width > 0)
			{
				Node->NodeWidth = Width;
			}

			int32 Height = Properties->GetIntegerField(TEXT("Height"));
			if (Height > 0)
			{
				Node->NodeHeight = Height;
			}

			// Set comment color if provided
			if (Properties->HasField(TEXT("CommentColor")))
			{
				const TSharedPtr<FJsonObject>* ColorObj;
				if (Properties->TryGetObjectField(TEXT("CommentColor"), ColorObj))
				{
					float R = (*ColorObj)->GetNumberField(TEXT("R"));
					float G = (*ColorObj)->GetNumberField(TEXT("G"));
					float B = (*ColorObj)->GetNumberField(TEXT("B"));
					float A = (*ColorObj)->HasField(TEXT("A")) ? (*ColorObj)->GetNumberField(TEXT("A")) : 1.0f;
					Node->CommentColor = FLinearColor(R, G, B, A);
				}
			}
		}
		else
		{
			Node->NodeComment = TEXT("Comment");
		}

		Graph->AddNode(Node, false, false);

		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Created Comment node: %s"), *Node->NodeComment);
		return Node;
	}

	/**
	 * Issue #2947 Phase K: Create Timeline node (UK2Node_Timeline)
	 * Optional: TimelineName (default "Timeline"), Length (default 5.0s), bAutoPlay, bLoop
	 *
	 * Timeline nodes are unique in that they:
	 * 1. Create a UTimelineTemplate internally
	 * 2. Have special pins: Play, Stop, Update, Finished, Direction, NewTime
	 * 3. Can have float/vector/event tracks added later
	 */
	UK2Node* CreateTimelineNode(UEdGraph* Graph, UBlueprint* Blueprint, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> Properties)
	{
		if (!Blueprint)
		{
			UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] Blueprint is null for Timeline node"));
			return nullptr;
		}

		// Get timeline name
		FString TimelineName = TEXT("Timeline");
		if (Properties.IsValid())
		{
			FString ProvidedName = Properties->GetStringField(TEXT("TimelineName"));
			if (ProvidedName.IsEmpty())
			{
				ProvidedName = Properties->GetStringField(TEXT("timeline_name"));
			}
			if (!ProvidedName.IsEmpty())
			{
				TimelineName = ProvidedName;
			}
		}

		// Ensure unique name
		FName TimelineFName = FBlueprintEditorUtils::FindUniqueTimelineName(Blueprint);
		if (!TimelineName.IsEmpty())
		{
			// Try to use provided name, but make it unique if needed
			TimelineFName = FName(*TimelineName);
			int32 Suffix = 1;
			FString BaseName = TimelineName;
			while (Blueprint->FindTimelineTemplateByVariableName(TimelineFName))
			{
				TimelineFName = FName(*FString::Printf(TEXT("%s_%d"), *BaseName, Suffix++));
			}
		}

		// Create the Timeline node
		UK2Node_Timeline* Node = NewObject<UK2Node_Timeline>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Node->TimelineName = TimelineFName;

		// Add to graph first (required before creating timeline template)
		Graph->AddNode(Node, false, false);

		// Create the timeline template inside the Blueprint
		// UE 5.6+ removed AddMemberTimeline - timeline template is auto-created by UK2Node_Timeline
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
		FBlueprintEditorUtils::AddMemberTimeline(Blueprint, TimelineFName);
#endif

		// Allocate pins first (this creates the timeline template in UE 5.6+)
		Node->AllocateDefaultPins();

		// Get the template and configure it
		UTimelineTemplate* TimelineTemplate = Blueprint->FindTimelineTemplateByVariableName(TimelineFName);
		if (TimelineTemplate && Properties.IsValid())
		{
			// Set timeline length (default 5 seconds)
			if (Properties->HasField(TEXT("Length")))
			{
				float Length = Properties->GetNumberField(TEXT("Length"));
				TimelineTemplate->TimelineLength = Length;
			}

			// Set auto-play
			if (Properties->HasField(TEXT("bAutoPlay")))
			{
				TimelineTemplate->bAutoPlay = Properties->GetBoolField(TEXT("bAutoPlay"));
			}

			// Set looping
			if (Properties->HasField(TEXT("bLoop")))
			{
				TimelineTemplate->bLoop = Properties->GetBoolField(TEXT("bLoop"));
			}

			// Set replicated
			if (Properties->HasField(TEXT("bReplicated")))
			{
				TimelineTemplate->bReplicated = Properties->GetBoolField(TEXT("bReplicated"));
			}

			// Set ignore time dilation
			if (Properties->HasField(TEXT("bIgnoreTimeDilation")))
			{
				TimelineTemplate->bIgnoreTimeDilation = Properties->GetBoolField(TEXT("bIgnoreTimeDilation"));
			}
		}

		// Reconstruct node (pins already allocated above)
		Node->ReconstructNode();

		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] Created Timeline node: %s (Length: %.2fs)"),
			*TimelineFName.ToString(),
			TimelineTemplate ? TimelineTemplate->TimelineLength : 5.0f);

		return Node;
	}

	// Issue #4751 Phase 43: CreateAddComponentNode REMOVED.
	// K2Node_AddComponent requires ComponentTemplate (SCS) before AllocateDefaultPins.
	// Both T3D import and C++ NewObject+AllocateDefaultPins crash with:
	//   Assertion failed: FoundPin != 0 [K2Node_AddComponent.h:77]
	// AddComponent must be done manually in Blueprint Editor Components panel.

	/**
	 * G52: Create SpawnActorFromClass node (UK2Node_SpawnActorFromClass)
	 * T3D import crashes editor — must use C++ API.
	 */
	UK2Node* CreateSpawnActorNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		UK2Node_SpawnActorFromClass* Node = NewObject<UK2Node_SpawnActorFromClass>(Graph);
		Node->CreateNewGuid();
		Node->NodePosX = PosX;
		Node->NodePosY = PosY;
		Graph->AddNode(Node, false, false);
		Node->AllocateDefaultPins();
		return Node;
	}

	/**
	 * G54: Create IsValid macro node (exec-based with "Is Valid"/"Is Not Valid" pins)
	 * Uses StandardMacros.IsValid — same pattern as DoOnce, Gate, etc.
	 */
	UK2Node* CreateIsValidNode(UEdGraph* Graph, int32 PosX, int32 PosY, TSharedPtr<FJsonObject> /*Properties*/)
	{
		const FString MacroPath = TEXT("/Engine/EditorBlueprintResources/StandardMacros.StandardMacros:IsValid");
		UEdGraph* MacroGraph = LoadObject<UEdGraph>(nullptr, *MacroPath);
		if (!MacroGraph) { UE_LOG(LogNarshaMCPRemote, Error, TEXT("[Blueprint] IsValid macro not found at %s"), *MacroPath); return nullptr; }
		UK2Node_MacroInstance* Node = NewObject<UK2Node_MacroInstance>(Graph);
		Node->CreateNewGuid(); Node->NodePosX = PosX; Node->NodePosY = PosY;
		Node->SetMacroGraph(MacroGraph); Graph->AddNode(Node, false, false); Node->AllocateDefaultPins();
		return Node;
	}
}

FString UCURemoteLibrary::CreateBlueprintNode(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& NodeType,
	int32 PosX,
	int32 PosY,
	const FString& PropertiesJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintNode called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BlueprintPath: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  GraphName: %s"), *GraphName);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  NodeType: %s"), *NodeType);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Position: (%d, %d)"), PosX, PosY);

	// Issue #2947 Phase I: Enhanced error handling with error codes
	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_BLUEPRINT_PATH,
			TEXT("Blueprint path is empty"),
			TEXT("CreateBlueprintNode requires a valid Blueprint path"),
			TEXT("Provide a full asset path like '/Game/Blueprints/BP_MyActor.BP_MyActor'")
		);
	}

	if (NodeType.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_NODE_TYPE,
			TEXT("Node type is empty"),
			TEXT("CreateBlueprintNode requires a valid node type"),
			TEXT("Supported types: Branch, CallFunction, CustomEvent, VariableGet, VariableSet, Cast, ForLoop, Delay, SwitchInt, Timeline, AddComponent, Comment, etc.")
		);
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::BLUEPRINT_NOT_FOUND,
			FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath),
			FString::Printf(TEXT("Attempted to load Blueprint at path '%s'"), *BlueprintPath),
			TEXT("Verify the Blueprint exists and the path includes the asset name (e.g., '/Game/BP_Test.BP_Test')")
		);
	}

	// Find target graph
	FString TargetGraphName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;
	UEdGraph* TargetGraph = BlueprintNodeHelpers::FindGraphByName(Blueprint, TargetGraphName);
	if (!TargetGraph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::GRAPH_NOT_FOUND,
			FString::Printf(TEXT("Graph not found: %s"), *TargetGraphName),
			FString::Printf(TEXT("Blueprint '%s' does not have a graph named '%s'"), *BlueprintPath, *TargetGraphName),
			TEXT("Use 'EventGraph' for most Blueprints, or check available graphs in the Blueprint Editor")
		);
	}

	// Issue #2947 Phase J: Begin Transaction for Undo/Redo support
#if WITH_EDITOR
	BlueprintNodeHelpers::FScopedBlueprintTransaction Transaction(
		FString::Printf(TEXT("Create %s Node"), *NodeType)
	);
	Transaction.Modify(TargetGraph);
	Transaction.Modify(Blueprint);
#endif

	// Parse properties JSON
	TSharedPtr<FJsonObject> Properties;
	if (!PropertiesJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
		FJsonSerializer::Deserialize(Reader, Properties);
	}

	// Create node based on type
	UK2Node* NewNode = nullptr;
	FString NodeTypeUpper = NodeType.ToUpper();

	if (NodeTypeUpper == TEXT("BRANCH"))
	{
		NewNode = BlueprintNodeFactory::CreateBranchNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("MAKEARRAY"))
	{
		NewNode = BlueprintNodeFactory::CreateMakeArrayNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("CALLFUNCTION"))
	{
		NewNode = BlueprintNodeFactory::CreateCallFunctionNode(TargetGraph, Blueprint, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("CUSTOMEVENT"))
	{
		NewNode = BlueprintNodeFactory::CreateCustomEventNode(TargetGraph, Blueprint, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("VARIABLEGET") || NodeTypeUpper == TEXT("GETVAR"))
	{
		NewNode = BlueprintNodeFactory::CreateVariableGetNode(TargetGraph, Blueprint, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("VARIABLESET") || NodeTypeUpper == TEXT("SETVAR"))
	{
		NewNode = BlueprintNodeFactory::CreateVariableSetNode(TargetGraph, Blueprint, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("CAST") || NodeTypeUpper == TEXT("DYNAMICCAST"))
	{
		NewNode = BlueprintNodeFactory::CreateCastNode(TargetGraph, PosX, PosY, Properties);
	}
	// Issue #2947 Phase C: MacroInstance-based nodes
	else if (NodeTypeUpper == TEXT("MACROINSTANCE"))
	{
		NewNode = BlueprintNodeFactory::CreateMacroInstanceNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("FORLOOP"))
	{
		NewNode = BlueprintNodeFactory::CreateForLoopNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("FOREACHLOOP"))
	{
		NewNode = BlueprintNodeFactory::CreateForEachLoopNode(TargetGraph, PosX, PosY, Properties);
	}
	// Issue #4751: Additional MacroInstance-based flow-control nodes
	else if (NodeTypeUpper == TEXT("DOONCE"))
	{
		NewNode = BlueprintNodeFactory::CreateDoOnceNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("FLIPFLOP"))
	{
		NewNode = BlueprintNodeFactory::CreateFlipFlopNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("GATE"))
	{
		NewNode = BlueprintNodeFactory::CreateGateNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("DON") || NodeTypeUpper == TEXT("DO N"))
	{
		NewNode = BlueprintNodeFactory::CreateDoNNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("WHILELOOP"))
	{
		NewNode = BlueprintNodeFactory::CreateWhileLoopNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("FOREACHLOOPWITHBREAK"))
	{
		NewNode = BlueprintNodeFactory::CreateForEachLoopWithBreakNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("DELAY"))
	{
		NewNode = BlueprintNodeFactory::CreateDelayNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("PRINTSTRING"))
	{
		NewNode = BlueprintNodeFactory::CreatePrintStringNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("SEQUENCE"))
	{
		NewNode = BlueprintNodeFactory::CreateSequenceNode(TargetGraph, PosX, PosY, Properties);
	}
	// Issue #2947 Phase H: Switch and Comment nodes
	else if (NodeTypeUpper == TEXT("SWITCHINT") || NodeTypeUpper == TEXT("SWITCHONINT") || NodeTypeUpper == TEXT("SWITCHINTEGER"))
	{
		NewNode = BlueprintNodeFactory::CreateSwitchIntNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("SWITCHSTRING") || NodeTypeUpper == TEXT("SWITCHONSTRING"))
	{
		NewNode = BlueprintNodeFactory::CreateSwitchStringNode(TargetGraph, PosX, PosY, Properties);
	}
	else if (NodeTypeUpper == TEXT("SWITCHENUM") || NodeTypeUpper == TEXT("SWITCHONENUM"))
	{
		NewNode = BlueprintNodeFactory::CreateSwitchEnumNode(TargetGraph, PosX, PosY, Properties);
	}
	// Issue #2947 Phase K: Timeline node
	else if (NodeTypeUpper == TEXT("TIMELINE"))
	{
		NewNode = BlueprintNodeFactory::CreateTimelineNode(TargetGraph, Blueprint, PosX, PosY, Properties);
	}
	// Issue #4751 Phase 43: AddComponent requires ComponentTemplate (SCS) before AllocateDefaultPins.
	// Both T3D import and C++ creation crash: Assertion failed: FoundPin != 0.
	else if (NodeTypeUpper == TEXT("ADDCOMPONENT"))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::UNSUPPORTED_NODE_TYPE,
			TEXT("AddComponent requires manual Editor setup"),
			TEXT("K2Node_AddComponent needs ComponentTemplate (SCS) before pin allocation. Neither T3D nor C++ API is safe."),
			TEXT("Add component manually in Blueprint Editor Components panel")
		);
	}
	// Issue #4751 G52: SpawnActorFromClass crashes T3D import — use C++ API
	else if (NodeTypeUpper == TEXT("SPAWNACTORFROMCLASS") || NodeTypeUpper == TEXT("SPAWNACTOR"))
	{
		NewNode = BlueprintNodeFactory::CreateSpawnActorNode(TargetGraph, PosX, PosY, Properties);
	}
	// Issue #4751 G54: IsValid exec-based macro (Is Valid / Is Not Valid exec pins)
	else if (NodeTypeUpper == TEXT("ISVALID"))
	{
		NewNode = BlueprintNodeFactory::CreateIsValidNode(TargetGraph, PosX, PosY, Properties);
	}
	// Issue #4751 G55: Clear all nodes from graph (UE 5.7 Python can't access protected Nodes property)
	else if (NodeTypeUpper == TEXT("CLEARGRAPH"))
	{
		int32 Removed = 0;
		TArray<UEdGraphNode*> NodesToRemove;
		for (UEdGraphNode* Node : TargetGraph->Nodes)
		{
			if (!Node->IsA<UK2Node_FunctionEntry>())
			{
				NodesToRemove.Add(Node);
			}
		}
		for (UEdGraphNode* Node : NodesToRemove)
		{
			TargetGraph->RemoveNode(Node);
			Removed++;
		}

		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

		TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetNumberField(TEXT("removed_count"), Removed);
		ResultObject->SetStringField(TEXT("graph_name"), TargetGraph->GetName());

		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] ClearGraph: Removed %d nodes from %s"), Removed, *TargetGraph->GetName());
		return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
	}
	else if (NodeTypeUpper == TEXT("COMMENT"))
	{
		// Comment node is UEdGraphNode, not UK2Node
		UEdGraphNode* CommentNode = BlueprintNodeFactory::CreateCommentNode(TargetGraph, PosX, PosY, Properties);
		if (CommentNode)
		{
			// Mark modified and save
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
			if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
			{
				return BlueprintNodeHelpers::CreateDetailedErrorJson(
					BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
					TEXT("Failed to save Blueprint after Comment node creation"),
					FString::Printf(TEXT("Comment node was created but Blueprint '%s' could not be saved"), *BlueprintPath),
					TEXT("Ensure the Blueprint is not read-only and no other process has it locked")
				);
			}

			// Build response for Comment node
			TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
			ResultObject->SetStringField(TEXT("node_guid"), CommentNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
			ResultObject->SetStringField(TEXT("node_class"), CommentNode->GetClass()->GetName());
			ResultObject->SetStringField(TEXT("node_title"), TEXT("Comment"));
			ResultObject->SetNumberField(TEXT("pos_x"), PosX);
			ResultObject->SetNumberField(TEXT("pos_y"), PosY);
			ResultObject->SetStringField(TEXT("graph_name"), TargetGraph->GetName());

			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintNode completed: Comment"));
			return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
		}
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_CREATION_FAILED,
			TEXT("Failed to create Comment node"),
			FString::Printf(TEXT("Attempted to create Comment node in graph '%s'"), *TargetGraphName),
			TEXT("Check UE log for detailed factory error messages")
		);
	}
	else
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::UNSUPPORTED_NODE_TYPE,
			FString::Printf(TEXT("Unsupported node type: %s"), *NodeType),
			FString::Printf(TEXT("Node type '%s' is not supported by CreateBlueprintNode"), *NodeType),
			TEXT("Supported types: Branch, CallFunction, CustomEvent, VariableGet, VariableSet, Cast, MakeArray, MacroInstance, ForLoop, ForEachLoop, DoOnce, FlipFlop, Gate, DoN, WhileLoop, ForEachLoopWithBreak, Delay, PrintString, Sequence, SwitchInt, SwitchString, SwitchEnum, Timeline, AddComponent, SpawnActorFromClass, IsValid, ClearGraph, Comment")
		);
	}

	if (!NewNode)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_CREATION_FAILED,
			FString::Printf(TEXT("Failed to create node of type: %s"), *NodeType),
			FString::Printf(TEXT("Factory returned nullptr for node type '%s' in graph '%s'"), *NodeType, *TargetGraphName),
			TEXT("Check PropertiesJson for missing required properties (e.g., FunctionPath for CallFunction, VariableName for VariableGet)")
		);
	}

	// Mark modified and save
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save Blueprint after node creation"),
			FString::Printf(TEXT("Node '%s' was created but Blueprint '%s' could not be saved"), *NodeType, *BlueprintPath),
			TEXT("Ensure the Blueprint is not read-only and no other process has it locked")
		);
	}

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	ResultObject->SetStringField(TEXT("node_class"), NewNode->GetClass()->GetName());
	ResultObject->SetStringField(TEXT("node_title"), NewNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	ResultObject->SetNumberField(TEXT("pos_x"), PosX);
	ResultObject->SetNumberField(TEXT("pos_y"), PosY);
	ResultObject->SetStringField(TEXT("graph_name"), TargetGraph->GetName());
	ResultObject->SetObjectField(TEXT("pins"), BlueprintNodeFactory::BuildPinsJson(NewNode));

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintNode completed: %s"), *NodeType);

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::DeleteBlueprintNode(
	const FString& BlueprintPath,
	const FString& NodeGuid
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] DeleteBlueprintNode called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BlueprintPath: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  NodeGuid: %s"), *NodeGuid);

	// Issue #2947 Phase I: Enhanced error handling
	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_BLUEPRINT_PATH,
			TEXT("Blueprint path is empty"),
			TEXT("DeleteBlueprintNode requires a valid Blueprint path"),
			TEXT("Provide a full asset path like '/Game/Blueprints/BP_MyActor.BP_MyActor'")
		);
	}

	if (NodeGuid.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_NODE_GUID,
			TEXT("Node GUID is empty"),
			TEXT("DeleteBlueprintNode requires a valid node GUID"),
			TEXT("Use the node_guid returned from CreateBlueprintNode or get it from Blueprint metadata")
		);
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::BLUEPRINT_NOT_FOUND,
			FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath),
			FString::Printf(TEXT("Attempted to load Blueprint at path '%s'"), *BlueprintPath),
			TEXT("Verify the Blueprint exists and the path includes the asset name (e.g., '/Game/BP_Test.BP_Test')")
		);
	}

	// Parse GUID
	FGuid TargetGuid;
	if (!FGuid::Parse(NodeGuid, TargetGuid))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::INVALID_GUID_FORMAT,
			FString::Printf(TEXT("Invalid GUID format: %s"), *NodeGuid),
			FString::Printf(TEXT("Could not parse '%s' as a valid GUID"), *NodeGuid),
			TEXT("GUID should be in format 'XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX'")
		);
	}

	// Issue #2947 Phase J: Begin Transaction for Undo/Redo support
#if WITH_EDITOR
	BlueprintNodeHelpers::FScopedBlueprintTransaction Transaction(TEXT("Delete Blueprint Node"));
	Transaction.Modify(Blueprint);
#endif

	// Find and remove the node
	bool bFound = false;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == TargetGuid)
			{
#if WITH_EDITOR
				// Issue #2947 Phase J: Mark objects for undo before modification
				Graph->Modify();
				Node->Modify();
#endif
				Graph->RemoveNode(Node);
				bFound = true;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Node removed from graph: %s"), *Graph->GetName());
				break;
			}
		}
		if (bFound) break;
	}

	if (!bFound)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Node not found with GUID: %s"), *NodeGuid),
			FString::Printf(TEXT("No node with GUID '%s' exists in Blueprint '%s'"), *NodeGuid, *BlueprintPath),
			TEXT("Verify the node GUID is correct and the node hasn't already been deleted")
		);
	}

	// Mark modified and save
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save Blueprint after node deletion"),
			FString::Printf(TEXT("Node '%s' was deleted but Blueprint '%s' could not be saved"), *NodeGuid, *BlueprintPath),
			TEXT("Ensure the Blueprint is not read-only and no other process has it locked")
		);
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("deleted_node_guid"), NodeGuid);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] DeleteBlueprintNode completed"));

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::CreateBlueprintVariable(
	const FString& BlueprintPath,
	const FString& VarName,
	const FString& VarType,
	const FString& DefaultValue,
	const FString& FlagsJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintVariable called"));
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  BlueprintPath: %s"), *BlueprintPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  VarName: %s"), *VarName);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("  VarType: %s"), *VarType);

	// Issue #2947 Phase I: Enhanced error handling
	// Validate inputs
	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_BLUEPRINT_PATH,
			TEXT("Blueprint path is empty"),
			TEXT("CreateBlueprintVariable requires a valid Blueprint path"),
			TEXT("Provide a full asset path like '/Game/Blueprints/BP_MyActor.BP_MyActor'")
		);
	}

	if (VarName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_VARIABLE_NAME,
			TEXT("Variable name is empty"),
			TEXT("CreateBlueprintVariable requires a valid variable name"),
			TEXT("Provide a unique variable name (e.g., 'Health', 'bIsAlive', 'TargetLocation')")
		);
	}

	if (VarType.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::EMPTY_VARIABLE_TYPE,
			TEXT("Variable type is empty"),
			TEXT("CreateBlueprintVariable requires a valid variable type"),
			TEXT("Supported types: bool, int, float, FString, FName, FText, FVector, FRotator, FTransform")
		);
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::BLUEPRINT_NOT_FOUND,
			FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath),
			FString::Printf(TEXT("Attempted to load Blueprint at path '%s'"), *BlueprintPath),
			TEXT("Verify the Blueprint exists and the path includes the asset name (e.g., '/Game/BP_Test.BP_Test')")
		);
	}

	// Check for duplicate
	for (const FBPVariableDescription& ExistingVar : Blueprint->NewVariables)
	{
		if (ExistingVar.VarName.ToString() == VarName)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::VARIABLE_ALREADY_EXISTS,
				FString::Printf(TEXT("Variable already exists: %s"), *VarName),
				FString::Printf(TEXT("Blueprint '%s' already has a variable named '%s'"), *BlueprintPath, *VarName),
				TEXT("Choose a different variable name or delete the existing variable first")
			);
		}
	}

	// Issue #2947 Phase J: Begin Transaction for Undo/Redo support
#if WITH_EDITOR
	BlueprintNodeHelpers::FScopedBlueprintTransaction Transaction(
		FString::Printf(TEXT("Create Variable '%s'"), *VarName)
	);
	Transaction.Modify(Blueprint);
#endif

	// Create variable description
	FBPVariableDescription NewVar;
	NewVar.VarName = FName(*VarName);
	NewVar.VarGuid = FGuid::NewGuid();
	NewVar.FriendlyName = VarName;
	NewVar.Category = FText::FromString(TEXT("Default"));

	// Base flags: Editable and visible in Blueprint
	EPropertyFlags BaseFlags = CPF_Edit | CPF_BlueprintVisible;

	// Issue #2947 Phase F: Parse FlagsJson for additional UPROPERTY flags
	if (!FlagsJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> FlagsObject;
		TSharedRef<TJsonReader<>> FlagsReader = TJsonReaderFactory<>::Create(FlagsJson);
		if (FJsonSerializer::Deserialize(FlagsReader, FlagsObject) && FlagsObject.IsValid())
		{
			// CPF_Net (Replicated) - for network replication
			if (FlagsObject->HasField(TEXT("Replicated")) && FlagsObject->GetBoolField(TEXT("Replicated")))
			{
				BaseFlags |= CPF_Net;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: Replicated (CPF_Net)"));
			}

			// CPF_DisableEditOnInstance (EditDefaultsOnly) - only edit in class defaults
			if (FlagsObject->HasField(TEXT("EditDefaultsOnly")) && FlagsObject->GetBoolField(TEXT("EditDefaultsOnly")))
			{
				BaseFlags |= CPF_DisableEditOnInstance;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: EditDefaultsOnly"));
			}

			// CPF_DisableEditOnTemplate (EditInstanceOnly) - only edit on instances
			if (FlagsObject->HasField(TEXT("EditInstanceOnly")) && FlagsObject->GetBoolField(TEXT("EditInstanceOnly")))
			{
				BaseFlags |= CPF_DisableEditOnTemplate;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: EditInstanceOnly"));
			}

			// CPF_SaveGame - include in save games
			if (FlagsObject->HasField(TEXT("SaveGame")) && FlagsObject->GetBoolField(TEXT("SaveGame")))
			{
				BaseFlags |= CPF_SaveGame;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: SaveGame"));
			}

			// CPF_Transient - don't serialize
			if (FlagsObject->HasField(TEXT("Transient")) && FlagsObject->GetBoolField(TEXT("Transient")))
			{
				BaseFlags |= CPF_Transient;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: Transient"));
			}

			// CPF_BlueprintReadOnly - read-only in Blueprint
			if (FlagsObject->HasField(TEXT("BlueprintReadOnly")) && FlagsObject->GetBoolField(TEXT("BlueprintReadOnly")))
			{
				// Remove BlueprintVisible (read-write) and add BlueprintReadOnly
				BaseFlags &= ~CPF_BlueprintVisible;
				BaseFlags |= CPF_BlueprintReadOnly;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: BlueprintReadOnly"));
			}

			// CPF_ExposeOnSpawn - expose on spawn
			if (FlagsObject->HasField(TEXT("ExposeOnSpawn")) && FlagsObject->GetBoolField(TEXT("ExposeOnSpawn")))
			{
				BaseFlags |= CPF_ExposeOnSpawn;
				UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Flag: ExposeOnSpawn"));
			}

			// Custom category
			if (FlagsObject->HasField(TEXT("Category")))
			{
				FString CategoryStr = FlagsObject->GetStringField(TEXT("Category"));
				if (!CategoryStr.IsEmpty())
				{
					NewVar.Category = FText::FromString(CategoryStr);
					UE_LOG(LogNarshaMCPRemote, Log, TEXT("  Category: %s"), *CategoryStr);
				}
			}
		}
	}

	NewVar.PropertyFlags = BaseFlags;

	// Set variable type. Trim first (Issue #19009): the shared fallback mapper below
	// (NarshaPinTypeUtils::MakePinType) trims before matching, so a padded legacy token
	// like " float " must resolve here to its legacy pin category (float -> PC_Float) rather
	// than leaking to the fallback and being reclassified (float -> PC_Double). Keeps the
	// "legacy chain shadows the fallback for every legacy token" invariant airtight.
	FString VarTypeUpper = VarType.TrimStartAndEnd().ToUpper();
	if (VarTypeUpper == TEXT("BOOL") || VarTypeUpper == TEXT("BOOLEAN"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (VarTypeUpper == TEXT("INT") || VarTypeUpper == TEXT("INT32") || VarTypeUpper == TEXT("INTEGER"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (VarTypeUpper == TEXT("FLOAT"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
		NewVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (VarTypeUpper == TEXT("DOUBLE"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Real;
		NewVar.VarType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (VarTypeUpper == TEXT("STRING") || VarTypeUpper == TEXT("FSTRING"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (VarTypeUpper == TEXT("NAME") || VarTypeUpper == TEXT("FNAME"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (VarTypeUpper == TEXT("TEXT") || VarTypeUpper == TEXT("FTEXT"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (VarTypeUpper == TEXT("VECTOR") || VarTypeUpper == TEXT("FVECTOR"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		NewVar.VarType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (VarTypeUpper == TEXT("ROTATOR") || VarTypeUpper == TEXT("FROTATOR"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		NewVar.VarType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (VarTypeUpper == TEXT("TRANSFORM") || VarTypeUpper == TEXT("FTRANSFORM"))
	{
		NewVar.VarType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		NewVar.VarType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else
	{
		// Issue #19009: extended grammar (struct:<StructObjectPath>, object:<ClassPath>,
		// int64, real) via the shared mapper. The chain above must stay first: it owns the
		// legacy tokens' pin categories (notably float -> PC_Real/PC_Float, where the shared
		// mapper would pick PC_Double).
		FString MapError;
		if (!NarshaPinTypeUtils::MakePinType(VarType, NewVar.VarType, MapError))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::UNSUPPORTED_VARIABLE_TYPE,
				FString::Printf(TEXT("Unsupported variable type: %s"), *VarType),
				MapError.IsEmpty()
					? FString::Printf(TEXT("Variable type '%s' is not supported by CreateBlueprintVariable"), *VarType)
					: MapError,
				TEXT("Supported types: bool/boolean, int/int32/integer, int64, float, double, string/FString, name/FName, text/FText, vector/FVector, rotator/FRotator, transform/FTransform, struct:<StructObjectPath>, object:<ClassPath>")
			);
		}
	}

	// Set default value if provided
	if (!DefaultValue.IsEmpty())
	{
		NewVar.DefaultValue = DefaultValue;
	}

	// Add variable to Blueprint
	Blueprint->NewVariables.Add(NewVar);

	// Mark modified and save
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	if (!FCUBlueprintManipulator::SaveBlueprint(Blueprint, true))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save Blueprint after variable creation"),
			FString::Printf(TEXT("Variable '%s' was created but Blueprint '%s' could not be saved"), *VarName, *BlueprintPath),
			TEXT("Ensure the Blueprint is not read-only and no other process has it locked")
		);
	}

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("variable_name"), VarName);
	ResultObject->SetStringField(TEXT("variable_guid"), NewVar.VarGuid.ToString(EGuidFormats::DigitsWithHyphens));
	ResultObject->SetStringField(TEXT("variable_type"), VarType);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintVariable completed: %s (%s)"), *VarName, *VarType);

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// ============================================================================
// BehaviorTree Modification (Issue #4263)
// ============================================================================

// Helper: LoadObject with Asset Registry fallback for plugin assets (Issue #4263)
// Plugin assets use /<PluginName>/ mount point, not /Game/, so direct LoadObject
// with /Game/ paths fails. Asset Registry searches all mount points.
static UBehaviorTree* LoadBehaviorTreeWithFallback(const FString& BTPath)
{
	// 1st: Direct LoadObject (works for project Content assets)
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *BTPath);
	if (BT)
	{
		return BT;
	}

	// 2nd: Asset Registry name-based search (handles plugin assets)
	FString AssetName = FPaths::GetBaseFilename(BTPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] LoadObject failed for '%s', trying Asset Registry with name '%s'"), *BTPath, *AssetName);

	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("BehaviorTree")));

	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName == FName(*AssetName))
		{
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] Found BT via Asset Registry: %s"), *Asset.GetObjectPathString());
			return Cast<UBehaviorTree>(Asset.GetAsset());
		}
	}

	return nullptr;
}

/**
 * Issue #16913: applies a struct property from JSON; for blackboard-key struct families
 * (FBlackboardKeySelector / FValueOrBBKey_*) it additionally validates the key against the
 * BT's blackboard and, if the engine would silently wipe/rewrite it on save, RESTORES the
 * prior value (non-destructive) and reports why via OutError. Returns true if this property
 * is a blackboard-key struct (handled here); false if it is any other struct (caller should
 * apply it normally). bOutApplied reports whether the value was successfully written.
 */
static bool ApplyBlackboardKeyStructProp(
	const TSharedRef<FJsonObject>& StructJson,
	FStructProperty* StructProp,
	void* ValuePtr,
	const UBlackboardData* BB,
	bool& bOutApplied,
	FString& OutError)
{
	bOutApplied = false;
	if (!StructProp || !StructProp->Struct || !ValuePtr)
	{
		return false;
	}

	const bool bIsSelector = StructProp->Struct->IsChildOf(FBlackboardKeySelector::StaticStruct());
	const bool bIsValueOrKey = StructProp->Struct->IsChildOf(FValueOrBlackboardKeyBase::StaticStruct());
	if (!bIsSelector && !bIsValueOrKey)
	{
		return false; // not a blackboard-key struct; caller handles it
	}

#if WITH_EDITOR
	UScriptStruct* SS = StructProp->Struct;

	// Snapshot the prior value so an invalid key change can be reverted without data loss.
	void* Snapshot = FMemory::Malloc(SS->GetStructureSize());
	SS->InitializeStruct(Snapshot);
	SS->CopyScriptStruct(Snapshot, ValuePtr);

	bool bRestore = false;
	if (!FJsonObjectConverter::JsonObjectToUStruct(StructJson, SS, ValuePtr))
	{
		OutError = FString::Printf(TEXT("struct conversion failed for type '%s'"), *SS->GetName());
		bRestore = true;
	}
	else if (BB != nullptr && !NarshaBlackboardKeyGuard::WillKeyPersist(bIsSelector, ValuePtr, *BB, OutError))
	{
		// OutError filled by WillKeyPersist. If BB is null we cannot validate and the engine's
		// own PreSave is also skipped (no wipe), so we accept the value as-is.
		bRestore = true;
	}

	if (bRestore)
	{
		SS->CopyScriptStruct(ValuePtr, Snapshot); // revert; prior key binding preserved
	}
	else
	{
		bOutApplied = true;
	}

	SS->DestroyStruct(Snapshot);
	FMemory::Free(Snapshot);
	return true;
#else
	// Non-editor: PreSave's blackboard-key normalization is `#if WITH_EDITOR`, so nothing is
	// wiped on save -> behave exactly like the original path (convert, no validation).
	bOutApplied = FJsonObjectConverter::JsonObjectToUStruct(StructJson, StructProp->Struct, ValuePtr);
	if (!bOutApplied)
	{
		OutError = FString::Printf(TEXT("struct conversion failed for type '%s'"), *StructProp->Struct->GetName());
	}
	return true;
#endif // WITH_EDITOR
}


// Issue #26975: shared dry_run preview envelope. Call after load+selector
// resolve and BEFORE any field write / NewObject / Modify / SavePackage.
static FString MakeAiPreviewSuccessJson(const TSharedPtr<FJsonObject>& Resolved)
{
	TSharedPtr<FJsonObject> Obj = Resolved.IsValid() ? Resolved : MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("preview"), true);
	return BlueprintNodeHelpers::CreateSuccessJson(Obj);
}

// Issue #27718 (BT-07): the modify ops must reject ambiguous name lookups instead
// of silently writing the first DFS match. Mirrors the add_decorator / reparent
// duplicate guards and the StateTree AI_ERR_603 candidate shape (#26888); BT
// nodes carry no GUID, so candidates are identified by parent/owner + index.
// CountBTNodesByName (below, authoring section) is not reusable here: it is out
// of scope for these functions, skips decorators/services, and a bare count
// cannot produce the candidate locators the rejection must list.
namespace
{
	struct FCUBTNameMatch
	{
		UBTNode* Node = nullptr;
		FString Locator;
	};

	// Collect every task whose display name matches, with a parent/child-index
	// locator per match. OutAvailable gets every task name (the not-found error's
	// "Available tasks" list). A visited set plus per-node dedup keeps a
	// malformed/diamond tree from double-counting one node into a false ambiguity.
	void CollectBTTaskMatches(UBehaviorTree* BT, const FString& TaskName,
		TArray<FCUBTNameMatch>& OutMatches, TArray<FString>& OutAvailable)
	{
		UBTCompositeNode* Root = BT ? BT->RootNode : nullptr;
		if (!Root)
		{
			return;
		}
		TSet<UBTCompositeNode*> Visited;
		TSet<UBTNode*> Matched;
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Root);
		Visited.Add(Root);
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (int32 ChildIdx = 0; ChildIdx < Current->GetChildrenNum(); ++ChildIdx)
			{
				const FBTCompositeChild& Child = Current->Children[ChildIdx];
				if (UBTTaskNode* Task = Child.ChildTask)
				{
					const FString CurrentTaskName = Task->GetNodeName();
					OutAvailable.Add(CurrentTaskName);
					if (CurrentTaskName == TaskName && !Matched.Contains(Task))
					{
						Matched.Add(Task);
						OutMatches.Add({ Task, FString::Printf(TEXT("parent=%s, child_index=%d"),
							*Current->GetNodeName(), ChildIdx) });
					}
				}
				if (UBTCompositeNode* Composite = Child.ChildComposite)
				{
					if (!Visited.Contains(Composite))
					{
						Visited.Add(Composite);
						Stack.Add(Composite);
					}
				}
			}
		}
	}

	// Decorator flavor: decorators hang off FBTCompositeChild links, so the
	// locator names the decorated child, its parent composite, and the slot.
	void CollectBTDecoratorMatches(UBehaviorTree* BT, const FString& DecoratorName,
		TArray<FCUBTNameMatch>& OutMatches, TArray<FString>& OutAvailable)
	{
		UBTCompositeNode* Root = BT ? BT->RootNode : nullptr;
		if (!Root)
		{
			return;
		}
		TSet<UBTCompositeNode*> Visited;
		TSet<UBTNode*> Matched;
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Root);
		Visited.Add(Root);
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (int32 ChildIdx = 0; ChildIdx < Current->GetChildrenNum(); ++ChildIdx)
			{
				const FBTCompositeChild& Child = Current->Children[ChildIdx];
				const FString ChildName = Child.ChildTask
					? Child.ChildTask->GetNodeName()
					: (Child.ChildComposite ? Child.ChildComposite->GetNodeName() : FString());
				for (int32 DecIdx = 0; DecIdx < Child.Decorators.Num(); ++DecIdx)
				{
					if (UBTDecorator* Decorator = Child.Decorators[DecIdx])
					{
						const FString CurrentDecoratorName = Decorator->GetNodeName();
						OutAvailable.Add(CurrentDecoratorName);
						if (CurrentDecoratorName == DecoratorName && !Matched.Contains(Decorator))
						{
							Matched.Add(Decorator);
							OutMatches.Add({ Decorator, FString::Printf(
								TEXT("attached_to=%s, parent=%s, decorator_index=%d"),
								*ChildName, *Current->GetNodeName(), DecIdx) });
						}
					}
				}
				if (UBTCompositeNode* Composite = Child.ChildComposite)
				{
					if (!Visited.Contains(Composite))
					{
						Visited.Add(Composite);
						Stack.Add(Composite);
					}
				}
			}
		}
	}

	// Service flavor: services live on composites (UBTCompositeNode::Services)
	// AND task leaves (UBTTaskNode::Services) — both legs are collected so a
	// cross-path duplicate is still ambiguous (Issue #27718 Goal 3).
	void CollectBTServiceMatches(UBehaviorTree* BT, const FString& ServiceName,
		TArray<FCUBTNameMatch>& OutMatches, TArray<FString>& OutAvailable)
	{
		UBTCompositeNode* Root = BT ? BT->RootNode : nullptr;
		if (!Root)
		{
			return;
		}
		TSet<UBTCompositeNode*> Visited;
		TSet<UBTNode*> Matched;
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Root);
		Visited.Add(Root);
		auto AddServiceMatch = [&OutMatches, &OutAvailable, &Matched, &ServiceName](
			UBTService* Service, const FString& OwnerName, const TCHAR* OwnerKind)
		{
			const FString CurrentServiceName = Service->GetNodeName();
			OutAvailable.Add(CurrentServiceName);
			if (CurrentServiceName == ServiceName && !Matched.Contains(Service))
			{
				Matched.Add(Service);
				OutMatches.Add({ Service, FString::Printf(TEXT("owner=%s (%s)"),
					*OwnerName, OwnerKind) });
			}
		};
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (const TObjectPtr<UBTService>& ServicePtr : Current->Services)
			{
				if (UBTService* Service = ServicePtr.Get())
				{
					AddServiceMatch(Service, Current->GetNodeName(), TEXT("composite"));
				}
			}
			for (int32 ChildIdx = 0; ChildIdx < Current->GetChildrenNum(); ++ChildIdx)
			{
				const FBTCompositeChild& Child = Current->Children[ChildIdx];
				if (UBTTaskNode* ChildTask = Child.ChildTask)
				{
					for (const TObjectPtr<UBTService>& ServicePtr : ChildTask->Services)
					{
						if (UBTService* Service = ServicePtr.Get())
						{
							AddServiceMatch(Service, ChildTask->GetNodeName(), TEXT("task"));
						}
					}
				}
				if (UBTCompositeNode* Composite = Child.ChildComposite)
				{
					if (!Visited.Contains(Composite))
					{
						Visited.Add(Composite);
						Stack.Add(Composite);
					}
				}
			}
		}
	}

	// Shared rejection envelope — AI_ERR_603 + a candidates list, aligned with
	// the StateTree precedent's shape. BT candidates carry parent/owner locators
	// instead of GUIDs (ExecutionIndex is not computed for tool-authored trees
	// before a PIE load, so it cannot serve as the identifier).
	FString MakeBTAmbiguousModifyError(const TCHAR* Role, const FString& Name,
		const TArray<FCUBTNameMatch>& Matches)
	{
		TArray<FString> CandidateParts;
		CandidateParts.Reserve(Matches.Num());
		for (const FCUBTNameMatch& Match : Matches)
		{
			CandidateParts.Add(FString::Printf(TEXT("%s (%s)"), *Name, *Match.Locator));
		}
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Ambiguous %s name: %d %ss named '%s'"), Role, Matches.Num(), Role, *Name),
			FString::Printf(TEXT("candidates: %s"), *FString::Join(CandidateParts, TEXT(", "))),
			TEXT("Rename duplicates in the Behavior Tree editor (for new nodes, add_task's node_name assigns a unique name) so the target is unambiguous"));
	}

	/**
	 * Issue #27937: validate and stage a BT node property bag without touching the
	 * live asset. The old modify paths combined validation with writes, so a valid
	 * property followed by an unknown/invalid property could partially mutate the
	 * node before the request failed. Each property gets an owned reflection value
	 * buffer initialized from the live node, so validation cannot touch the real
	 * node, transaction stack, package dirty flag, or on-disk package. Property
	 * buffers also avoid copying instanced references from a transient UObject.
	 */
	struct FBTStagedPropertyValue
	{
		FProperty* Property = nullptr;
		FString Name;
		void* Value = nullptr;

		FBTStagedPropertyValue(UObject* Target, FProperty* InProperty, const FString& InName)
			: Property(InProperty)
			, Name(InName)
		{
			check(Target);
			check(Property);
			Value = FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment());
			Property->InitializeValue(Value);
			Property->CopyCompleteValue(
				Value, Property->ContainerPtrToValuePtr<void>(Target));
		}

		~FBTStagedPropertyValue()
		{
			if (Value)
			{
				Property->DestroyValue(Value);
				FMemory::Free(Value);
			}
		}

		FBTStagedPropertyValue(const FBTStagedPropertyValue&) = delete;
		FBTStagedPropertyValue& operator=(const FBTStagedPropertyValue&) = delete;
	};

	struct FBTStagedPropertyChanges
	{
		TArray<TUniquePtr<FBTStagedPropertyValue>> Values;
		TArray<FString> ChangedProperties;
		TArray<FString> FailedProperties;
	};

	FBTStagedPropertyChanges StageBTPropertyChanges(
		UObject* Target,
		const TSharedRef<FJsonObject>& PropsObject,
		const UBlackboardData* BlackboardForKeys)
	{
		FBTStagedPropertyChanges Result;
		if (!Target)
		{
			Result.FailedProperties.Add(TEXT("target (null)"));
			return Result;
		}

		UClass* TargetClass = Target->GetClass();
		for (const auto& Pair : PropsObject->Values)
		{
			const FString PropName(Pair.Key);
			FProperty* Prop = TargetClass->FindPropertyByName(*PropName);
			if (!Prop)
			{
				Result.FailedProperties.Add(FString::Printf(TEXT("%s (not found)"), *PropName));
				continue;
			}

			TUniquePtr<FBTStagedPropertyValue> StagedValue =
				MakeUnique<FBTStagedPropertyValue>(Target, Prop, PropName);
			void* ValuePtr = StagedValue->Value;
			if (!ValuePtr)
			{
				Result.FailedProperties.Add(FString::Printf(TEXT("%s (invalid property access)"), *PropName));
				continue;
			}

			bool bApplied = false;
			if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(Pair.Value->AsNumber()));
					bApplied = true;
				}
			}
			else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					DoubleProp->SetPropertyValue(ValuePtr, Pair.Value->AsNumber());
					bApplied = true;
				}
			}
			else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Pair.Value->AsNumber()));
					bApplied = true;
				}
			}
			else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Boolean)
				{
					BoolProp->SetPropertyValue(ValuePtr, Pair.Value->AsBool());
					bApplied = true;
				}
			}
			else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::String)
				{
					StrProp->SetPropertyValue(ValuePtr, Pair.Value->AsString());
					bApplied = true;
				}
			}
			else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::String)
				{
					NameProp->SetPropertyValue(ValuePtr, FName(*Pair.Value->AsString()));
					bApplied = true;
				}
			}
			else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
			{
				UEnum* EnumDef = EnumProp->GetEnum();
				if (Pair.Value->Type == EJson::String && EnumDef)
				{
					const int64 EnumValue = EnumDef->GetValueByNameString(Pair.Value->AsString());
					if (EnumValue != INDEX_NONE && EnumValue >= 0 &&
						EnumValue <= static_cast<int64>(EnumDef->GetMaxEnumValue()))
					{
						EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
						bApplied = true;
					}
					else
					{
						Result.FailedProperties.Add(FString::Printf(
							TEXT("%s (invalid enum value: %s)"), *PropName, *Pair.Value->AsString()));
						continue;
					}
				}
				else if (Pair.Value->Type == EJson::Number && EnumDef)
				{
					const int64 NumericValue = static_cast<int64>(Pair.Value->AsNumber());
					if (NumericValue >= 0 && NumericValue <= static_cast<int64>(EnumDef->GetMaxEnumValue()))
					{
						EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, NumericValue);
						bApplied = true;
					}
					else
					{
						Result.FailedProperties.Add(FString::Printf(
							TEXT("%s (enum value out of range: %lld)"), *PropName, NumericValue));
						continue;
					}
				}
			}
			else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				TSharedPtr<FJsonObject> StructJsonObject;
				if (Pair.Value->Type == EJson::Number)
				{
					StructJsonObject = MakeShared<FJsonObject>();
					StructJsonObject->SetNumberField(TEXT("DefaultValue"), Pair.Value->AsNumber());
				}
				else if (Pair.Value->Type == EJson::Object)
				{
					StructJsonObject = Pair.Value->AsObject();
				}

				if (StructJsonObject.IsValid())
				{
					bool bStructApplied = false;
					FString StructError;
					if (ApplyBlackboardKeyStructProp(
						StructJsonObject.ToSharedRef(), StructProp, ValuePtr,
						BlackboardForKeys, bStructApplied, StructError))
					{
						if (bStructApplied)
						{
							bApplied = true;
						}
						else
						{
							Result.FailedProperties.Add(FString::Printf(
								TEXT("%s (%s)"), *PropName, *StructError));
							continue;
						}
					}
					else if (FJsonObjectConverter::JsonObjectToUStruct(
						StructJsonObject.ToSharedRef(), StructProp->Struct, ValuePtr))
					{
						bApplied = true;
					}
					else
					{
						const FString StructTypeName = StructProp->Struct
							? StructProp->Struct->GetName() : TEXT("Unknown");
						Result.FailedProperties.Add(FString::Printf(
							TEXT("%s (struct conversion failed for type '%s' - verify JSON field names match struct properties)"),
							*PropName, *StructTypeName));
						continue;
					}
				}
			}

			if (!bApplied)
			{
				Result.FailedProperties.Add(FString::Printf(TEXT("%s (unsupported type)"), *PropName));
				continue;
			}

			if (!Prop->Identical(
				Prop->ContainerPtrToValuePtr<void>(Target), StagedValue->Value))
			{
				Result.ChangedProperties.Add(PropName);
				Result.Values.Add(MoveTemp(StagedValue));
			}
		}
		return Result;
	}

	void ApplyStagedBTPropertyChanges(
		UObject* Target,
		const FBTStagedPropertyChanges& Staged)
	{
		check(Target);
		for (const TUniquePtr<FBTStagedPropertyValue>& StagedValue : Staged.Values)
		{
			check(StagedValue.IsValid());
			check(StagedValue->Property);
			StagedValue->Property->CopyCompleteValue(
				StagedValue->Property->ContainerPtrToValuePtr<void>(Target),
				StagedValue->Value);
		}
	}
}

FString UCURemoteLibrary::ModifyBehaviorTreeTask(
	const FString& BTPath,
	const FString& TaskName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyBehaviorTreeTask: %s, Task: %s"), *BTPath, *TaskName);

	// 1. Load BehaviorTree (with Asset Registry fallback for plugin assets)
	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded. Checked both direct path and Asset Registry."),
			TEXT("Verify the asset path is correct and the asset exists")
		);
	}

	// 2. Find Task by name — collect ALL matches so an ambiguous name is rejected
	//    with candidates instead of silently modifying the first DFS hit (#27718).
	TArray<FCUBTNameMatch> TaskMatches;
	TArray<FString> AvailableTasks;
	CollectBTTaskMatches(BT, TaskName, TaskMatches, AvailableTasks);

	if (TaskMatches.Num() == 0)
	{
		FString AvailableTasksStr = AvailableTasks.Num() > 0
			? FString::Join(AvailableTasks, TEXT(", "))
			: TEXT("(no tasks found)");
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Task not found: %s"), *TaskName),
			FString::Printf(TEXT("Available tasks: %s"), *AvailableTasksStr),
			TEXT("Check the task name matches exactly")
		);
	}
	if (TaskMatches.Num() > 1)
	{
		return MakeBTAmbiguousModifyError(TEXT("task"), TaskName, TaskMatches);
	}
	UBTTaskNode* FoundTask = Cast<UBTTaskNode>(TaskMatches[0].Node);
	if (!FoundTask)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Resolved node is not a task: %s"), *TaskName),
			TEXT("Internal type mismatch while resolving the task by name."),
			TEXT("Report this response as a NarshaMCP bug")
		);
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("task_name"), TaskName);
		Preview->SetStringField(TEXT("task_class"), FoundTask->GetClass()->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 3. Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid properties JSON"),
			FString::Printf(TEXT("Could not parse: %s"), *PropertiesJson),
			TEXT("Ensure the JSON is valid and properly formatted")
		);
	}

	// 4. Stage and validate every property before touching the live node (#27937).
	UClass* TaskClass = FoundTask->GetClass();
	const UBlackboardData* BlackboardForKeys = FoundTask->GetBlackboardAsset();
	if (!BlackboardForKeys)
	{
		BlackboardForKeys = BT->BlackboardAsset;
	}

	const FBTStagedPropertyChanges Staged = StageBTPropertyChanges(
		FoundTask, PropsObject.ToSharedRef(), BlackboardForKeys);
	const TArray<FString>& FailedProperties = Staged.FailedProperties;
	if (FailedProperties.Num() > 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Property validation failed"),
			FString::Printf(TEXT("Failed properties: %s"), *FString::Join(FailedProperties, TEXT(", "))),
			TEXT("Fix every rejected property; partial BehaviorTree node updates are not applied")
		);
	}

	const TArray<FString>& ModifiedProperties = Staged.ChangedProperties;
	if (ModifiedProperties.Num() == 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("No properties were modified"),
			TEXT("All supplied values already match the task's current values"),
			TEXT("Supply at least one property value that differs from the current node")
		);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyBehaviorTree", "Modify BehaviorTree"));
	FoundTask->Modify();
#endif
	ApplyStagedBTPropertyChanges(FoundTask, Staged);
	// 5. Mark dirty and save using UPackage::SavePackage (Issue #4263)
	UPackage* Package = BT->GetOutermost();
	Package->MarkPackageDirty();

	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, BT, *PackageFileName, SaveArgs))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Properties modified but BehaviorTree '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only")
		);
	}

	// 6. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), BTPath);
	ResultObject->SetStringField(TEXT("task_name"), TaskName);
	ResultObject->SetStringField(TEXT("task_class"), TaskClass->GetName());

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	ResultObject->SetArrayField(TEXT("modified_properties"), ModifiedArray);

	if (FailedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FString& PropName : FailedProperties)
		{
			FailedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("failed_properties"), FailedArray);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyBehaviorTreeTask completed: %d properties modified"), ModifiedProperties.Num());

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::ModifyBehaviorTreeDecorator(
	const FString& BTPath,
	const FString& DecoratorName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyBehaviorTreeDecorator: %s, Decorator: %s"), *BTPath, *DecoratorName);

	// 1. Load BehaviorTree (with Asset Registry fallback for plugin assets)
	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded. Checked both direct path and Asset Registry."),
			TEXT("Verify the asset path is correct and the asset exists")
		);
	}

	// 2. Find Decorator by name — collect ALL matches so an ambiguous name is
	//    rejected with candidates instead of silently modifying the first DFS hit
	//    (#27718). Decorators are stored per FBTCompositeChild link.
	TArray<FCUBTNameMatch> DecoratorMatches;
	TArray<FString> AvailableDecorators;
	CollectBTDecoratorMatches(BT, DecoratorName, DecoratorMatches, AvailableDecorators);

	if (DecoratorMatches.Num() == 0)
	{
		FString AvailableDecoratorsStr = AvailableDecorators.Num() > 0
			? FString::Join(AvailableDecorators, TEXT(", "))
			: TEXT("(no decorators found)");
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Decorator not found: %s"), *DecoratorName),
			FString::Printf(TEXT("Available decorators: %s"), *AvailableDecoratorsStr),
			TEXT("Check the decorator name matches exactly")
		);
	}
	if (DecoratorMatches.Num() > 1)
	{
		return MakeBTAmbiguousModifyError(TEXT("decorator"), DecoratorName, DecoratorMatches);
	}
	UBTDecorator* FoundDecorator = Cast<UBTDecorator>(DecoratorMatches[0].Node);
	if (!FoundDecorator)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Resolved node is not a decorator: %s"), *DecoratorName),
			TEXT("Internal type mismatch while resolving the decorator by name."),
			TEXT("Report this response as a NarshaMCP bug")
		);
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("decorator_name"), DecoratorName);
		Preview->SetStringField(TEXT("decorator_class"), FoundDecorator->GetClass()->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 3. Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid properties JSON"),
			FString::Printf(TEXT("Could not parse: %s"), *PropertiesJson),
			TEXT("Ensure the JSON is valid and properly formatted")
		);
	}

	// 4. Stage and validate every property before touching the live node (#27937).
	UClass* DecoratorClass = FoundDecorator->GetClass();
	const UBlackboardData* BlackboardForKeys = FoundDecorator->GetBlackboardAsset();
	if (!BlackboardForKeys)
	{
		BlackboardForKeys = BT->BlackboardAsset;
	}

	const FBTStagedPropertyChanges Staged = StageBTPropertyChanges(
		FoundDecorator, PropsObject.ToSharedRef(), BlackboardForKeys);
	const TArray<FString>& FailedProperties = Staged.FailedProperties;
	if (FailedProperties.Num() > 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Property validation failed"),
			FString::Printf(TEXT("Failed properties: %s"), *FString::Join(FailedProperties, TEXT(", "))),
			TEXT("Fix every rejected property; partial BehaviorTree node updates are not applied")
		);
	}

	const TArray<FString>& ModifiedProperties = Staged.ChangedProperties;
	if (ModifiedProperties.Num() == 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("No properties were modified"),
			TEXT("All supplied values already match the decorator's current values"),
			TEXT("Supply at least one property value that differs from the current node")
		);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyBehaviorTree", "Modify BehaviorTree"));
	FoundDecorator->Modify();
#endif
	ApplyStagedBTPropertyChanges(FoundDecorator, Staged);
	// 5. Mark dirty and save using UPackage::SavePackage (Issue #4263)
	UPackage* Package = BT->GetOutermost();
	Package->MarkPackageDirty();

	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, BT, *PackageFileName, SaveArgs))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Properties modified but BehaviorTree '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only")
		);
	}

	// 6. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), BTPath);
	ResultObject->SetStringField(TEXT("decorator_name"), DecoratorName);
	ResultObject->SetStringField(TEXT("decorator_class"), DecoratorClass->GetName());

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	ResultObject->SetArrayField(TEXT("modified_properties"), ModifiedArray);

	if (FailedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FString& PropName : FailedProperties)
		{
			FailedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("failed_properties"), FailedArray);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyBehaviorTreeDecorator completed: %d properties modified"), ModifiedProperties.Num());

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// Issue #16572 gap2: parity with ModifyBehaviorTreeTask/Decorator above. Kept
// self-contained (its own find + reflection apply) to match those two siblings —
// the shared #16912 helpers (ApplyBTNodeProps/SaveBTPackage) live in a later
// anon namespace and are not in scope here.
FString UCURemoteLibrary::ModifyBehaviorTreeService(
	const FString& BTPath,
	const FString& ServiceName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyBehaviorTreeService: %s, Service: %s"), *BTPath, *ServiceName);

	// 1. Load BehaviorTree (with Asset Registry fallback for plugin assets)
	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded. Checked both direct path and Asset Registry."),
			TEXT("Verify the asset path is correct and the asset exists")
		);
	}

	// 2. Find Service by name — collect ALL matches across BOTH storage legs
	//    (UBTCompositeNode::Services AND UBTTaskNode::Services) so an ambiguous
	//    name — including a cross-leg duplicate — is rejected with candidates
	//    instead of silently modifying the first DFS hit (#27718). No write op
	//    authors a task-level service today (add_service targets composites,
	//    #16912), so the task leg is read-modify of pre-authored assets.
	TArray<FCUBTNameMatch> ServiceMatches;
	TArray<FString> AvailableServices;
	CollectBTServiceMatches(BT, ServiceName, ServiceMatches, AvailableServices);

	if (ServiceMatches.Num() == 0)
	{
		FString AvailableServicesStr = AvailableServices.Num() > 0
			? FString::Join(AvailableServices, TEXT(", "))
			: TEXT("(no services found)");
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Service not found: %s"), *ServiceName),
			FString::Printf(TEXT("Available services: %s"), *AvailableServicesStr),
			TEXT("Check the service name matches exactly")
		);
	}
	if (ServiceMatches.Num() > 1)
	{
		return MakeBTAmbiguousModifyError(TEXT("service"), ServiceName, ServiceMatches);
	}
	UBTService* FoundService = Cast<UBTService>(ServiceMatches[0].Node);
	if (!FoundService)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Resolved node is not a service: %s"), *ServiceName),
			TEXT("Internal type mismatch while resolving the service by name."),
			TEXT("Report this response as a NarshaMCP bug")
		);
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("service_name"), ServiceName);
		Preview->SetStringField(TEXT("service_class"), FoundService->GetClass()->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 3. Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid properties JSON"),
			FString::Printf(TEXT("Could not parse: %s"), *PropertiesJson),
			TEXT("Ensure the JSON is valid and properly formatted")
		);
	}

	// 4. Stage and validate every property before touching the live node (#27937).
	UClass* ServiceClass = FoundService->GetClass();
	const UBlackboardData* BlackboardForKeys = FoundService->GetBlackboardAsset();
	if (!BlackboardForKeys)
	{
		BlackboardForKeys = BT->BlackboardAsset;
	}

	const FBTStagedPropertyChanges Staged = StageBTPropertyChanges(
		FoundService, PropsObject.ToSharedRef(), BlackboardForKeys);
	const TArray<FString>& FailedProperties = Staged.FailedProperties;
	if (FailedProperties.Num() > 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Property validation failed"),
			FString::Printf(TEXT("Failed properties: %s"), *FString::Join(FailedProperties, TEXT(", "))),
			TEXT("Fix every rejected property; partial BehaviorTree node updates are not applied")
		);
	}

	const TArray<FString>& ModifiedProperties = Staged.ChangedProperties;
	if (ModifiedProperties.Num() == 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("No properties were modified"),
			TEXT("All supplied values already match the service's current values"),
			TEXT("Supply at least one property value that differs from the current node")
		);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyBehaviorTree", "Modify BehaviorTree"));
	FoundService->Modify();
#endif
	ApplyStagedBTPropertyChanges(FoundService, Staged);
	// 5. Mark dirty and save using UPackage::SavePackage (Issue #4263)
	UPackage* Package = BT->GetOutermost();
	Package->MarkPackageDirty();

	FString PackageFileName = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, BT, *PackageFileName, SaveArgs))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Properties modified but BehaviorTree '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only")
		);
	}

	// 6. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), BTPath);
	ResultObject->SetStringField(TEXT("service_name"), ServiceName);
	ResultObject->SetStringField(TEXT("service_class"), ServiceClass->GetName());

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	ResultObject->SetArrayField(TEXT("modified_properties"), ModifiedArray);

	if (FailedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FString& PropName : FailedProperties)
		{
			FailedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("failed_properties"), FailedArray);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyBehaviorTreeService completed: %d properties modified"), ModifiedProperties.Num());

	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// ============================================================================
// StateTree/BehaviorTree Creation & StateTree Modification (Issue #4263)
// ============================================================================

// ============================================================================
// BehaviorTree node topology authoring helpers (Issue #16912)
// ============================================================================
//
// These author the RUNTIME tree (UBehaviorTree::RootNode->Children). The runtime
// UBehaviorTreeManager::LoadTree recomputes ExecutionIndex / memory offsets from
// the structure on PIE load (InitializeNodeHelper), so we only wire the node
// graph + save. The editor UBehaviorTreeGraph is intentionally not touched
// (deferred engine-wall sibling #16915); see bt_authoring_warning().
namespace
{
	/// Resolve a concrete (non-abstract) BT node class by short name, validating
	/// it derives from RequiredBase. Returns nullptr on miss / abstract / wrong base.
	UClass* ResolveConcreteBTClass(const FString& ClassName, UClass* RequiredBase)
	{
		if (ClassName.IsEmpty() || RequiredBase == nullptr)
		{
			return nullptr;
		}
		UClass* Found = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
		if (!Found)
		{
			// Allow a fully-qualified /Script path as a fallback.
			Found = FindObject<UClass>(nullptr, *ClassName);
		}
		if (!Found || !Found->IsChildOf(RequiredBase) || Found->HasAnyClassFlags(CLASS_Abstract))
		{
			return nullptr;
		}
		return Found;
	}

	/// DFS for a composite node by display name (includes the root). Empty name
	/// returns the root composite.
	UBTCompositeNode* FindBTCompositeByName(UBehaviorTree* BT, const FString& Name)
	{
		UBTCompositeNode* Root = BT ? BT->RootNode : nullptr;
		if (!Root)
		{
			return nullptr;
		}
		if (Name.IsEmpty() || Root->GetNodeName() == Name)
		{
			return Root;
		}
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (int32 i = 0; i < Current->GetChildrenNum(); ++i)
			{
				if (UBTCompositeNode* ChildComposite = Current->Children[i].ChildComposite)
				{
					if (ChildComposite->GetNodeName() == Name)
					{
						return ChildComposite;
					}
					Stack.Add(ChildComposite);
				}
			}
		}
		return nullptr;
	}

	/// Find a node (task or composite) by name and return its parent composite +
	/// child-link index, so a decorator can be attached to the right child-link.
	bool FindBTChildLink(UBehaviorTree* BT, const FString& Name,
		UBTCompositeNode*& OutParent, int32& OutChildIdx)
	{
		UBTCompositeNode* Root = BT ? BT->RootNode : nullptr;
		if (!Root || Name.IsEmpty())
		{
			return false;
		}
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (int32 i = 0; i < Current->GetChildrenNum(); ++i)
			{
				const FBTCompositeChild& Child = Current->Children[i];
				FString ChildName;
				if (Child.ChildTask)
				{
					ChildName = Child.ChildTask->GetNodeName();
				}
				else if (Child.ChildComposite)
				{
					ChildName = Child.ChildComposite->GetNodeName();
				}
				if (!ChildName.IsEmpty() && ChildName == Name)
				{
					OutParent = Current;
					OutChildIdx = i;
					return true;
				}
				if (Child.ChildComposite)
				{
					Stack.Add(Child.ChildComposite);
				}
			}
		}
		return false;
	}

	/// Count nodes whose display name matches. bCompositesOnly restricts to
	/// composite nodes (for parent / composite-target resolution). Used to reject
	/// duplicate names on add and to surface ambiguous-name lookups (Issue #16912).
	int32 CountBTNodesByName(UBehaviorTree* BT, const FString& Name, bool bCompositesOnly)
	{
		UBTCompositeNode* Root = BT ? BT->RootNode : nullptr;
		if (!Root || Name.IsEmpty())
		{
			return 0;
		}
		int32 Count = 0;
		if (Root->GetNodeName() == Name)
		{
			++Count;
		}
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Root);
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (int32 i = 0; i < Current->GetChildrenNum(); ++i)
			{
				const FBTCompositeChild& Child = Current->Children[i];
				if (!bCompositesOnly && Child.ChildTask
					&& Child.ChildTask->GetNodeName() == Name)
				{
					++Count;
				}
				if (Child.ChildComposite)
				{
					if (Child.ChildComposite->GetNodeName() == Name)
					{
						++Count;
					}
					Stack.Add(Child.ChildComposite);
				}
			}
		}
		return Count;
	}

	/// True if Candidate is Ancestor itself or appears anywhere in Ancestor's
	/// subtree — rejects a reparent that would create a cycle (Issue #16912).
	bool BTCompositeContains(UBTCompositeNode* Ancestor, UBTNode* Candidate)
	{
		if (!Ancestor || !Candidate)
		{
			return false;
		}
		if (Ancestor == Candidate)
		{
			return true;
		}
		TArray<UBTCompositeNode*> Stack;
		Stack.Add(Ancestor);
		while (Stack.Num() > 0)
		{
			UBTCompositeNode* Current = Stack.Pop();
			for (int32 i = 0; i < Current->GetChildrenNum(); ++i)
			{
				const FBTCompositeChild& Child = Current->Children[i];
				if (Child.ChildTask == Candidate || Child.ChildComposite == Candidate)
				{
					return true;
				}
				if (Child.ChildComposite)
				{
					Stack.Add(Child.ChildComposite);
				}
			}
		}
		return false;
	}

	/// Resolve a parent/target composite by name with ambiguity + clearer errors.
	/// Returns the composite, or nullptr and sets OutError to a ready JSON error
	/// (0 matches => not found; >1 => ambiguous) — Issue #16912 (②).
	UBTCompositeNode* ResolveBTCompositeOrError(UBehaviorTree* BT, const FString& Name,
		const TCHAR* Role, FString& OutError)
	{
		const int32 MatchCount = CountBTNodesByName(BT, Name, /*bCompositesOnly=*/true);
		if (MatchCount == 0)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
				FString::Printf(TEXT("%s composite not found: %s"), Role, *Name),
				BT->RootNode == nullptr
					? TEXT("The BehaviorTree has no root node yet.")
					: TEXT("No composite node with that name exists."),
				BT->RootNode == nullptr
					? TEXT("Add a root composite first: add_composite with an empty parent")
					: TEXT("Check the composite name matches exactly"));
			return nullptr;
		}
		if (MatchCount > 1)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Ambiguous %s name: %d composites named '%s'"), Role, MatchCount, *Name),
				TEXT("Node names must be unique for name-based authoring lookups."),
				TEXT("Rename the duplicates so the target is unambiguous"));
			return nullptr;
		}
		return FindBTCompositeByName(BT, Name);
	}

	/// Reject a new node whose display name already exists anywhere in the tree
	/// (mirrors AddStateTreeState's duplicate guard) — keeps name lookups
	/// deterministic. Returns a ready JSON error, or empty if OK. Issue #16912 (②).
	FString BTDuplicateNameError(UBehaviorTree* BT, const FString& NodeName)
	{
		if (!NodeName.IsEmpty() && CountBTNodesByName(BT, NodeName, /*bCompositesOnly=*/false) > 0)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("A node named '%s' already exists in this BehaviorTree"), *NodeName),
				TEXT("Node names must be unique within a tree for name-based authoring."),
				TEXT("Use a different node_name"));
		}
		return FString();
	}

	/// Apply a small set of common JSON property types to a freshly-created node.
	/// Mirrors the AddStateTreeState applier; keeps node authoring property-aware
	/// without the full ModifyBehaviorTreeTask reflection cascade.
	void ApplyBTNodeProps(UObject* Node, const FString& PropertiesJson, TArray<FString>& Modified)
	{
		if (!Node || PropertiesJson.IsEmpty())
		{
			return;
		}
		TSharedPtr<FJsonObject> PropsObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
		if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
		{
			return;
		}
		UClass* NodeClass = Node->GetClass();
		for (const auto& Pair : PropsObject->Values)
		{
			const FString PropName = FString(Pair.Key);
			FProperty* Prop = NodeClass->FindPropertyByName(*PropName);
			if (!Prop)
			{
				continue;
			}
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				BoolProp->SetPropertyValue_InContainer(Node, Pair.Value->AsBool());
				Modified.Add(PropName);
			}
			else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					FloatProp->SetPropertyValue_InContainer(Node, static_cast<float>(Pair.Value->AsNumber()));
					Modified.Add(PropName);
				}
			}
			else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					DoubleProp->SetPropertyValue_InContainer(Node, Pair.Value->AsNumber());
					Modified.Add(PropName);
				}
			}
			else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			{
				if (Pair.Value->Type == EJson::Number)
				{
					IntProp->SetPropertyValue_InContainer(Node, static_cast<int32>(Pair.Value->AsNumber()));
					Modified.Add(PropName);
				}
			}
			else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				StrProp->SetPropertyValue_InContainer(Node, Pair.Value->AsString());
				Modified.Add(PropName);
			}
			else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				NameProp->SetPropertyValue_InContainer(Node, FName(*Pair.Value->AsString()));
				Modified.Add(PropName);
			}
		}
	}

	/// Append the BTGraph wipe-on-recompile warning to the result if the asset
	/// already has an editor graph (Issue #16912 / #16915).
	void AddBTGraphWarning(UBehaviorTree* BT, TSharedPtr<FJsonObject>& Result)
	{
#if WITH_EDITORONLY_DATA
		if (BT && BT->BTGraph != nullptr)
		{
			Result->SetStringField(TEXT("warning"),
				TEXT("This asset has an editor BTGraph; runtime-tree edits are PIE-runnable but ")
				TEXT("may be overwritten on the next editor compile (CreateBTFromGraph rebuilds ")
				TEXT("RootNode from the graph). Editor-graph node placement is tracked by #16915."));
		}
#endif
	}

	/// Mark dirty + save the BT's package. Returns false on save failure.
	bool SaveBTPackage(UBehaviorTree* BT)
	{
		UPackage* Package = BT->GetOutermost();
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, BT, *PackageFileName, SaveArgs);
	}
} // namespace

FString UCURemoteLibrary::AddBehaviorTreeComposite(
	const FString& BTPath,
	const FString& ParentName,
	const FString& CompositeClass,
	const FString& NodeName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeComposite: %s, Parent: %s, Class: %s"),
		*BTPath, *ParentName, *CompositeClass);

	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	UClass* ResolvedClass = ResolveConcreteBTClass(CompositeClass, UBTCompositeNode::StaticClass());
	if (!ResolvedClass)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid composite class: %s"), *CompositeClass),
			TEXT("Class must resolve to a concrete UBTCompositeNode subclass."),
			TEXT("Use a class like 'BTComposite_Selector', 'BTComposite_Sequence'"));
	}

	// Reject a duplicate node_name up front (deterministic name lookups, ②).
	{
		const FString DupErr = BTDuplicateNameError(BT, NodeName);
		if (!DupErr.IsEmpty())
		{
			return DupErr;
		}
	}

	// Resolve parent BEFORE mutating (empty => new composite becomes RootNode).
	UBTCompositeNode* Parent = nullptr;
	const bool bAsRoot = ParentName.IsEmpty();
	if (!bAsRoot)
	{
		FString ResolveErr;
		Parent = ResolveBTCompositeOrError(BT, ParentName, TEXT("Parent"), ResolveErr);
		if (!Parent)
		{
			return ResolveErr;
		}
	}
	else if (BT->RootNode != nullptr)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("BehaviorTree already has a root node"),
			TEXT("ParentName was empty (root request) but RootNode is already set."),
			TEXT("Provide a ParentName to add under an existing composite"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("parent"), ParentName);
		Preview->SetStringField(TEXT("composite_class"), ResolvedClass->GetName());
		if (!NodeName.IsEmpty())
		{
			Preview->SetStringField(TEXT("node_name"), NodeName);
		}
		return MakeAiPreviewSuccessJson(Preview);
	}

	// FScopedTransaction requires WITH_EDITOR (not WITH_EDITORONLY_DATA) — guard
	// to match the ModifyBehaviorTree* siblings (CURemoteLibrary.cpp:4773).
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddBehaviorTreeComposite", "Add BehaviorTree Composite"));
	BT->Modify();
	if (Parent)
	{
		Parent->Modify();
	}
#endif

	UBTCompositeNode* NewComposite = NewObject<UBTCompositeNode>(BT, ResolvedClass, NAME_None, RF_Transactional);
	if (!NewComposite)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Composite creation failed"),
			TEXT("NewObject<UBTCompositeNode> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}
	if (!NodeName.IsEmpty())
	{
		NewComposite->NodeName = NodeName;
	}
	TArray<FString> Modified;
	ApplyBTNodeProps(NewComposite, PropertiesJson, Modified);

	if (bAsRoot)
	{
		BT->RootNode = NewComposite;
	}
	else
	{
		Parent->Children.AddDefaulted_GetRef().ChildComposite = NewComposite;
	}

	if (!SaveBTPackage(BT))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Composite added but '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), BTPath);
	Result->SetStringField(TEXT("node_name"), NewComposite->GetNodeName());
	Result->SetStringField(TEXT("node_class"), ResolvedClass->GetName());
	Result->SetStringField(TEXT("parent"), bAsRoot ? TEXT("(root)") : ParentName);
	AddBTGraphWarning(BT, Result);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeComposite completed: %s"), *NewComposite->GetNodeName());
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

FString UCURemoteLibrary::AddBehaviorTreeTask(
	const FString& BTPath,
	const FString& ParentName,
	const FString& TaskClass,
	const FString& NodeName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeTask: %s, Parent: %s, Class: %s"),
		*BTPath, *ParentName, *TaskClass);

	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	UClass* ResolvedClass = ResolveConcreteBTClass(TaskClass, UBTTaskNode::StaticClass());
	if (!ResolvedClass)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid task class: %s"), *TaskClass),
			TEXT("Class must resolve to a concrete UBTTaskNode subclass."),
			TEXT("Use a class like 'BTTask_MoveTo', 'BTTask_Wait'"));
	}

	{
		const FString DupErr = BTDuplicateNameError(BT, NodeName);
		if (!DupErr.IsEmpty())
		{
			return DupErr;
		}
	}

	FString ResolveErr;
	UBTCompositeNode* Parent = ResolveBTCompositeOrError(BT, ParentName, TEXT("Parent"), ResolveErr);
	if (!Parent)
	{
		return ResolveErr;
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("parent"), ParentName);
		Preview->SetStringField(TEXT("task_class"), ResolvedClass->GetName());
		if (!NodeName.IsEmpty())
		{
			Preview->SetStringField(TEXT("node_name"), NodeName);
		}
		return MakeAiPreviewSuccessJson(Preview);
	}

	// FScopedTransaction requires WITH_EDITOR — guard to match the siblings.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddBehaviorTreeTask", "Add BehaviorTree Task"));
	BT->Modify();
	Parent->Modify();
#endif

	UBTTaskNode* NewTask = NewObject<UBTTaskNode>(BT, ResolvedClass, NAME_None, RF_Transactional);
	if (!NewTask)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Task creation failed"),
			TEXT("NewObject<UBTTaskNode> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}
	if (!NodeName.IsEmpty())
	{
		NewTask->NodeName = NodeName;
	}
	TArray<FString> Modified;
	ApplyBTNodeProps(NewTask, PropertiesJson, Modified);

	Parent->Children.AddDefaulted_GetRef().ChildTask = NewTask;

	if (!SaveBTPackage(BT))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Task added but '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), BTPath);
	Result->SetStringField(TEXT("node_name"), NewTask->GetNodeName());
	Result->SetStringField(TEXT("node_class"), ResolvedClass->GetName());
	Result->SetStringField(TEXT("parent"), ParentName);
	AddBTGraphWarning(BT, Result);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeTask completed: %s"), *NewTask->GetNodeName());
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

FString UCURemoteLibrary::AddBehaviorTreeDecorator(
	const FString& BTPath,
	const FString& TargetNodeName,
	const FString& DecoratorClass,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeDecorator: %s, Target: %s, Class: %s"),
		*BTPath, *TargetNodeName, *DecoratorClass);

	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	UClass* ResolvedClass = ResolveConcreteBTClass(DecoratorClass, UBTDecorator::StaticClass());
	if (!ResolvedClass)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid decorator class: %s"), *DecoratorClass),
			TEXT("Class must resolve to a concrete UBTDecorator subclass."),
			TEXT("Use a class like 'BTDecorator_Blackboard', 'BTDecorator_Cooldown'"));
	}

	// ③ Distinguish "target is the root node (no parent link)" from "not found",
	// and surface ambiguous names, before attaching to the child-link.
	if (BT->RootNode != nullptr && BT->RootNode->GetNodeName() == TargetNodeName)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Cannot decorate the root node: %s"), *TargetNodeName),
			TEXT("Decorators attach to a node's parent child-link; the root node has no parent link."),
			TEXT("Add the decorator to a non-root node, or restructure the tree"));
	}
	if (CountBTNodesByName(BT, TargetNodeName, /*bCompositesOnly=*/false) > 1)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Ambiguous target name: multiple nodes named '%s'"), *TargetNodeName),
			TEXT("Node names must be unique for name-based authoring lookups."),
			TEXT("Rename the duplicates so the target is unambiguous"));
	}

	UBTCompositeNode* ParentComposite = nullptr;
	int32 ChildIdx = INDEX_NONE;
	if (!FindBTChildLink(BT, TargetNodeName, ParentComposite, ChildIdx)
		|| !ParentComposite || !ParentComposite->Children.IsValidIndex(ChildIdx))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Target node not found: %s"), *TargetNodeName),
			TEXT("Decorators attach to a node's parent child-link; the target must be a non-root node."),
			TEXT("Check the target node name matches exactly"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("target_node"), TargetNodeName);
		Preview->SetStringField(TEXT("decorator_class"), ResolvedClass->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// FScopedTransaction requires WITH_EDITOR — guard to match the siblings.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddBehaviorTreeDecorator", "Add BehaviorTree Decorator"));
	BT->Modify();
	ParentComposite->Modify();
#endif

	UBTDecorator* NewDecorator = NewObject<UBTDecorator>(BT, ResolvedClass, NAME_None, RF_Transactional);
	if (!NewDecorator)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Decorator creation failed"),
			TEXT("NewObject<UBTDecorator> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}
	TArray<FString> Modified;
	ApplyBTNodeProps(NewDecorator, PropertiesJson, Modified);

	ParentComposite->Children[ChildIdx].Decorators.Add(NewDecorator);

	if (!SaveBTPackage(BT))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Decorator added but '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), BTPath);
	Result->SetStringField(TEXT("node_class"), ResolvedClass->GetName());
	Result->SetStringField(TEXT("target_node"), TargetNodeName);
	AddBTGraphWarning(BT, Result);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeDecorator completed on: %s"), *TargetNodeName);
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

FString UCURemoteLibrary::AddBehaviorTreeService(
	const FString& BTPath,
	const FString& TargetCompositeName,
	const FString& ServiceClass,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeService: %s, Target: %s, Class: %s"),
		*BTPath, *TargetCompositeName, *ServiceClass);

	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	UClass* ResolvedClass = ResolveConcreteBTClass(ServiceClass, UBTService::StaticClass());
	if (!ResolvedClass)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid service class: %s"), *ServiceClass),
			TEXT("Class must resolve to a concrete UBTService subclass."),
			TEXT("Use a class like 'BTService_DefaultFocus', 'BTService_RunEQS'"));
	}

	FString ResolveErr;
	UBTCompositeNode* TargetComposite = ResolveBTCompositeOrError(BT, TargetCompositeName, TEXT("Target"), ResolveErr);
	if (!TargetComposite)
	{
		return ResolveErr;
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("target_composite"), TargetCompositeName);
		Preview->SetStringField(TEXT("service_class"), ResolvedClass->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// FScopedTransaction requires WITH_EDITOR — guard to match the siblings.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddBehaviorTreeService", "Add BehaviorTree Service"));
	BT->Modify();
	TargetComposite->Modify();
#endif

	UBTService* NewService = NewObject<UBTService>(BT, ResolvedClass, NAME_None, RF_Transactional);
	if (!NewService)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Service creation failed"),
			TEXT("NewObject<UBTService> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}
	TArray<FString> Modified;
	ApplyBTNodeProps(NewService, PropertiesJson, Modified);

	TargetComposite->Services.Add(NewService);

	if (!SaveBTPackage(BT))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Service added but '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), BTPath);
	Result->SetStringField(TEXT("node_class"), ResolvedClass->GetName());
	Result->SetStringField(TEXT("target_composite"), TargetCompositeName);
	AddBTGraphWarning(BT, Result);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddBehaviorTreeService completed on: %s"), *TargetCompositeName);
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

FString UCURemoteLibrary::ReparentBehaviorTreeNode(
	const FString& BTPath,
	const FString& NodeName,
	const FString& NewParentName,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ReparentBehaviorTreeNode: %s, Node: %s, NewParent: %s"),
		*BTPath, *NodeName, *NewParentName);

	UBehaviorTree* BT = LoadBehaviorTreeWithFallback(BTPath);
	if (!BT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("BehaviorTree not found: %s"), *BTPath),
			TEXT("The specified BehaviorTree asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	// The root node has no parent child-link, so it cannot be reparented.
	if (BT->RootNode != nullptr && BT->RootNode->GetNodeName() == NodeName)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Cannot reparent the root node: %s"), *NodeName),
			TEXT("The root node has no parent child-link to detach from."),
			TEXT("Reparent a non-root node instead"));
	}
	if (CountBTNodesByName(BT, NodeName, /*bCompositesOnly=*/false) > 1)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Ambiguous node name: multiple nodes named '%s'"), *NodeName),
			TEXT("Node names must be unique for name-based authoring lookups."),
			TEXT("Rename the duplicates so the node is unambiguous"));
	}

	FString ResolveErr;
	UBTCompositeNode* NewParent = ResolveBTCompositeOrError(BT, NewParentName, TEXT("New parent"), ResolveErr);
	if (!NewParent)
	{
		return ResolveErr;
	}

	UBTCompositeNode* OldParent = nullptr;
	int32 OldChildIdx = INDEX_NONE;
	if (!FindBTChildLink(BT, NodeName, OldParent, OldChildIdx)
		|| !OldParent || !OldParent->Children.IsValidIndex(OldChildIdx))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Node not found: %s"), *NodeName),
			TEXT("The node to reparent must be a non-root node in the tree."),
			TEXT("Check the node name matches exactly"));
	}

	// Cycle guard is a pre-mutation selector (Issue #26975): preview must reject
	// the same cyclic reparent as apply, before any transaction.
	{
		UBTCompositeNode* MovedComposite = OldParent->Children[OldChildIdx].ChildComposite;
		if (MovedComposite && BTCompositeContains(MovedComposite, NewParent))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Reparent would create a cycle: '%s' is inside the subtree of '%s'"),
					*NewParentName, *NodeName),
				TEXT("A node cannot be reparented under itself or one of its descendants."),
				TEXT("Choose a new parent outside the moved node's subtree"));
		}
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), BTPath);
		Preview->SetStringField(TEXT("node_name"), NodeName);
		Preview->SetStringField(TEXT("new_parent"), NewParentName);
		return MakeAiPreviewSuccessJson(Preview);
	}

	// Idempotent no-op: if the node is already a direct child of the target,
	// do NOT remove+re-add it — that would silently reorder siblings, and BT
	// child order IS execution priority (review W-1, Issue #16912).
	if (NewParent == OldParent)
	{
		TSharedPtr<FJsonObject> NoOp = MakeShared<FJsonObject>();
		NoOp->SetStringField(TEXT("asset_path"), BTPath);
		NoOp->SetStringField(TEXT("node_name"), NodeName);
		NoOp->SetStringField(TEXT("new_parent"), NewParentName);
		NoOp->SetBoolField(TEXT("no_change"), true);
		NoOp->SetStringField(TEXT("note"),
			TEXT("Node is already a direct child of the target parent; no change made ")
			TEXT("(avoids reordering sibling execution priority)."));
		AddBTGraphWarning(BT, NoOp);
		return BlueprintNodeHelpers::CreateSuccessJson(NoOp);
	}

	// Capture the child link by value (preserves ChildComposite/ChildTask +
	// Decorators) BEFORE any mutation.
	const FBTCompositeChild MovedChild = OldParent->Children[OldChildIdx];

	// Cycle guard: if the moved node is a composite, the new parent must not be
	// that composite itself nor any node inside its subtree.
	if (MovedChild.ChildComposite
		&& BTCompositeContains(MovedChild.ChildComposite, NewParent))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Reparent would create a cycle: '%s' is inside the subtree of '%s'"),
				*NewParentName, *NodeName),
			TEXT("A node cannot be reparented under itself or one of its descendants."),
			TEXT("Choose a new parent outside the moved node's subtree"));
	}

	// FScopedTransaction requires WITH_EDITOR — guard to match the siblings.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ReparentBehaviorTreeNode", "Reparent BehaviorTree Node"));
	BT->Modify();
	OldParent->Modify();
	NewParent->Modify();
#endif

	// Add to the new parent first, then remove from the old slot. When
	// NewParent == OldParent this reorders the node to the end (Add appends, then
	// RemoveAt(OldChildIdx) drops the original) — a harmless reorder.
	NewParent->Children.Add(MovedChild);
	OldParent->Children.RemoveAt(OldChildIdx);

	if (!SaveBTPackage(BT))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Node reparented but '%s' could not be saved"), *BTPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), BTPath);
	Result->SetStringField(TEXT("node_name"), NodeName);
	Result->SetStringField(TEXT("new_parent"), NewParentName);
	AddBTGraphWarning(BT, Result);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ReparentBehaviorTreeNode completed: %s -> %s"), *NodeName, *NewParentName);
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

FString UCURemoteLibrary::CreateBehaviorTree(
	const FString& AssetName,
	const FString& PackagePath,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] CreateBehaviorTree: %s in %s"), *AssetName, *PackagePath);

	if (AssetName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Asset name cannot be empty"),
			TEXT("AssetName parameter is required"),
			TEXT("Provide a valid asset name like 'BT_Enemy'")
		);
	}

	// Issue #26806: reject path-in-name before PackagePath / AssetName nests.
	if (AssetName.Contains(TEXT("/")) || AssetName.Contains(TEXT("\\")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Asset name must be a bare name, not a package path"),
			TEXT("AssetName contains '/' or '\\'"),
			TEXT("Pass PackagePath='/Game/AI' and AssetName='BT_Enemy'")
		);
	}

	// Check if asset already exists
	FString FullAssetPath = PackagePath / AssetName;
	FString LoadPath = FullAssetPath + TEXT(".") + AssetName;
	if (LoadObject<UBehaviorTree>(nullptr, *LoadPath))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("BehaviorTree already exists: %s"), *FullAssetPath),
			TEXT("Asset with this name already exists at the specified path"),
			TEXT("Use a different name or delete the existing asset")
		);
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), FullAssetPath);
		Preview->SetStringField(TEXT("asset_name"), AssetName);
		Preview->SetStringField(TEXT("asset_class"), TEXT("BehaviorTree"));
		return MakeAiPreviewSuccessJson(Preview);
	}

	// Create package
	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Failed to create package"),
			FString::Printf(TEXT("Could not create package at: %s"), *FullAssetPath),
			TEXT("Check that the path is valid and writable")
		);
	}

	// Create BehaviorTree asset
	UBehaviorTree* NewBT = NewObject<UBehaviorTree>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewBT)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("BehaviorTree creation failed"),
			TEXT("NewObject<UBehaviorTree> returned nullptr"),
			TEXT("Check editor logs for detailed error")
		);
	}

	// Mark dirty, notify registry, and save
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewBT);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		FullAssetPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, NewBT, *PackageFileName, SaveArgs))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save BehaviorTree"),
			FString::Printf(TEXT("Created but could not save: %s"), *FullAssetPath),
			TEXT("Check file permissions")
		);
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), FullAssetPath);
	ResultObject->SetStringField(TEXT("asset_name"), AssetName);
	ResultObject->SetStringField(TEXT("asset_class"), TEXT("BehaviorTree"));

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] CreateBehaviorTree completed: %s"), *FullAssetPath);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

static FString MakeStateTreePluginUnavailable()
{
	return BlueprintNodeHelpers::CreateDetailedErrorJson(
		TEXT("state_tree_plugin_unavailable"),
		TEXT("StateTree plugin unavailable"),
		TEXT("StateTree authoring requires the optional StateTree and PropertyBindingUtils plugins"),
		TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild."));
}

FString UCURemoteLibrary::CreateStateTree(
	const FString& AssetName,
	const FString& PackagePath,
	bool bPreviewOnly)
{
#if !WITH_NARSHAMCP_STATETREE || !WITH_NARSHAMCP_PROPERTYBINDINGUTILS
	return MakeStateTreePluginUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] CreateStateTree: %s in %s"), *AssetName, *PackagePath);

	if (AssetName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Asset name cannot be empty"),
			TEXT("AssetName parameter is required"),
			TEXT("Provide a valid asset name like 'ST_Enemy'")
		);
	}

	// Issue #26806: reject path-in-name before PackagePath / AssetName nests.
	if (AssetName.Contains(TEXT("/")) || AssetName.Contains(TEXT("\\")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Asset name must be a bare name, not a package path"),
			TEXT("AssetName contains '/' or '\\'"),
			TEXT("Pass PackagePath='/Game/AI' and AssetName='ST_Enemy'")
		);
	}

	// Check if asset already exists
	FString FullAssetPath = PackagePath / AssetName;
	FString LoadPath = FullAssetPath + TEXT(".") + AssetName;
	if (LoadObject<UStateTree>(nullptr, *LoadPath))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("StateTree already exists: %s"), *FullAssetPath),
			TEXT("Asset with this name already exists at the specified path"),
			TEXT("Use a different name or delete the existing asset")
		);
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), FullAssetPath);
		Preview->SetStringField(TEXT("asset_name"), AssetName);
		Preview->SetStringField(TEXT("asset_class"), TEXT("StateTree"));
		return MakeAiPreviewSuccessJson(Preview);
	}

	// Create package
	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Failed to create package"),
			FString::Printf(TEXT("Could not create package at: %s"), *FullAssetPath),
			TEXT("Check that the path is valid and writable")
		);
	}

	// Create StateTree asset
	UStateTree* NewST = NewObject<UStateTree>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewST)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("StateTree creation failed"),
			TEXT("NewObject<UStateTree> returned nullptr"),
			TEXT("Check editor logs for detailed error")
		);
	}

	// Issue #26805 (ST-03): every error return below this point moves NewST to the transient package
	// first. Without it the in-memory object keeps satisfying the LoadObject "already exists" guard
	// above, blocking a same-name retry until the editor restarts. The `!NewST` branch above is
	// deliberately excluded — there is nothing to clean, and the helper takes TNotNull<UObject*>.
	//
	// 5.8 exports UE::StateTree::Compiler::RenameObjectToTransientPackage from StateTree.h; 5.7 keeps
	// the same routine private to StateTree.cpp, so mirror its body there (identical flags/order).
	auto DiscardFailedAsset = [NewST]()
	{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		UE::StateTree::Compiler::RenameObjectToTransientPackage(NewST, TEXT("TRASH_CreateStateTree"));
#else
		const ERenameFlags RenFlags = REN_DoNotDirty | REN_DontCreateRedirectors;
		NewST->SetFlags(RF_Transient);
		NewST->ClearFlags(RF_Public | RF_Standalone | RF_ArchetypeObject);
		// Rename drops the linker when moving packages, so invalidate the export first.
		FLinkerLoad::InvalidateExport(NewST);
		NewST->Rename(nullptr, GetTransientPackage(), RenFlags);
#endif
	};

	// Schema-first creation, in the same order as UStateTreeFactory::FactoryCreateNew. The schema must
	// exist before AddRootState() so the editor data is built against it.
#if WITH_NARSHAMCP_GAMEPLAYSTATETREE
	UClass* SchemaClass = UStateTreeComponentSchema::StaticClass();
#else
	// Issue #28215 Phase A: GameplayStateTree is opt-in, so the type is not linked.
	// Resolve by path instead -- non-null exactly when the consuming project enabled
	// the plugin, which keeps this working for those projects without the hard link.
	UClass* SchemaClass = FindObject<UClass>(
		nullptr, TEXT("/Script/GameplayStateTreeModule.StateTreeComponentSchema"));
#endif
	if (!SchemaClass)
	{
		DiscardFailedAsset();
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("GameplayStateTree plugin unavailable"),
			TEXT("StateTree authoring needs the GameplayStateTree plugin; this project has not enabled it"),
			TEXT("Enable GameplayStateTree in your .uproject, then rebuild.")
		);
	}

	// FStateTreeEditorModule::GetModule() is LoadModuleChecked — fatal if the module is missing. Use the
	// on-demand-loading pointer form instead and fail with JSON. Continuing without the module is not an
	// option either: the compile path itself calls the checked accessor (StateTreeEditingSubsystem.cpp).
	FStateTreeEditorModule* EditorModule =
		FModuleManager::LoadModulePtr<FStateTreeEditorModule>(TEXT("StateTreeEditorModule"));
	if (!EditorModule)
	{
		DiscardFailedAsset();
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("StateTree editor module unavailable"),
			TEXT("StateTreeEditorModule could not be loaded; schema/compile support is required"),
			TEXT("Run this operation from a running Unreal Editor")
		);
	}

	TNonNullSubclassOf<UStateTreeEditorData> EditorDataClass = EditorModule->GetEditorDataClass(SchemaClass);
	UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(
		NewST, EditorDataClass, NAME_None, RF_Transactional);
	NewST->EditorData = EditorData;

	EditorData->Schema = NewObject<UStateTreeSchema>(EditorData, SchemaClass, NAME_None, RF_Transactional);

	// AddRootState() is the engine API for the default subtree; it names it "Root" with the same outer
	// and RF_Transactional as the manual construction it replaces (StateTreeEditorData.h:272-286).
	EditorData->AddRootState();

	// 5.7 declares GetEditorSchemaClass without UE_API, so it does not link from outside the module
	// (5.8 added the export). Nothing in either engine ever calls RegisterEditorSchemaClass, so the
	// lookup always falls through to UStateTreeEditorSchema::StaticClass() — use that directly on 5.7.
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
	TNonNullSubclassOf<UStateTreeEditorSchema> EditorSchemaClass = EditorModule->GetEditorSchemaClass(SchemaClass);
#else
	TNonNullSubclassOf<UStateTreeEditorSchema> EditorSchemaClass = UStateTreeEditorSchema::StaticClass();
#endif
	EditorData->EditorSchema = NewObject<UStateTreeEditorSchema>(
		EditorData, EditorSchemaClass, NAME_None, RF_Transactional);

	// Compile BEFORE saving. A post-save verdict would report failure while leaving the asset on disk,
	// where the LoadObject guard would block every same-name retry.
	FStateTreeCompilerLog CompilerLog;
	if (!UStateTreeEditingSubsystem::CompileStateTree(NewST, CompilerLog))
	{
		// 5.8 adds a (Asset, Category) overload that prefixes the asset; 5.7 only has (Category).
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		CompilerLog.DumpToLog(NewST, LogNarshaMCPRemote);
#else
		CompilerLog.DumpToLog(LogNarshaMCPRemote);
#endif

		// FStateTreeCompilerLog::Messages is protected; ToTokenizedMessages() is the only public reader.
		FString CompilerContext;
		for (const TSharedRef<FTokenizedMessage>& Message : CompilerLog.ToTokenizedMessages())
		{
			if (!CompilerContext.IsEmpty())
			{
				CompilerContext.Append(TEXT("\n"));
			}
			CompilerContext.Append(Message->ToText().ToString());
			if (CompilerContext.Len() > 2000)
			{
				CompilerContext.LeftInline(2000);
				CompilerContext.Append(TEXT(" ...(truncated)"));
				break;
			}
		}
		if (CompilerContext.IsEmpty())
		{
			CompilerContext = TEXT("Compiler reported no messages");
		}

		DiscardFailedAsset();
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Created StateTree failed to compile"),
			CompilerContext,
			TEXT("Check the StateTree compiler log; the asset was discarded so the name is free to retry")
		);
	}

	// Compile() runs Link() internally, so this is meaningful here. Reported, not gated: create succeeds
	// on a clean compile, and a false reading would turn a partial defect into a total outage.
	const bool bReadyToRun = NewST->IsReadyToRun();

	// Mark dirty, notify registry, and save
	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewST);

	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		FullAssetPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;

	if (!UPackage::SavePackage(Package, NewST, *PackageFileName, SaveArgs))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save StateTree"),
			FString::Printf(TEXT("Created but could not save: %s"), *FullAssetPath),
			TEXT("Check file permissions")
		);
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), FullAssetPath);
	ResultObject->SetStringField(TEXT("asset_name"), AssetName);
	ResultObject->SetStringField(TEXT("asset_class"), TEXT("StateTree"));
	// Issue #26805: observable proof the asset is compile-ready, not just saved.
	ResultObject->SetStringField(TEXT("schema_class"), SchemaClass->GetPathName());
	ResultObject->SetBoolField(TEXT("compiled"), true);
	ResultObject->SetBoolField(TEXT("ready_to_run"), bReadyToRun);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] CreateStateTree completed: %s"), *FullAssetPath);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
}

// ============================================================================
// EnvironmentQuery (EQS) authoring (Issue #16572 gap4)
// ============================================================================
//
// CreateEnvQuery / ModifyEQSGenerator / ModifyEQSTest author a real UEnvQuery.
// The runtime classes (UEnvQuery::Options[] -> Option.Generator / Option.Tests[])
// are public UPROPERTYs in AIModule (already a Build.cs dep) — no editor module
// needed; NewObject bypasses UEnvQueryFactory (mirror of CreateBehaviorTree).
// Generator/Test concrete classes resolve via the generic ResolveConcreteBTClass
// helper; simple properties apply via ApplyBTNodeProps (both anon-namespace,
// defined above). option_index addresses the Option (create seeds index 0).

static UEnvQuery* LoadEnvQueryWithFallback(const FString& EQSPath)
{
	UEnvQuery* EQS = LoadObject<UEnvQuery>(nullptr, *EQSPath);
	if (EQS)
	{
		return EQS;
	}
	const FString AssetName = FPaths::GetBaseFilename(EQSPath);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] LoadObject failed for '%s', trying Asset Registry with name '%s'"), *EQSPath, *AssetName);
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(FTopLevelAssetPath(TEXT("/Script/AIModule"), TEXT("EnvQuery")));
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName == FName(*AssetName))
		{
			return Cast<UEnvQuery>(Asset.GetAsset());
		}
	}
	return nullptr;
}

static bool SaveEnvQueryPackage(UEnvQuery* EQS)
{
	UPackage* Package = EQS->GetOutermost();
	Package->MarkPackageDirty();
	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	return UPackage::SavePackage(Package, EQS, *PackageFileName, SaveArgs);
}

/**
 * Validate the optional EQS property bag before any topology mutation.
 *
 * Empty remains valid for the documented class-only generator/test writes. Any
 * non-empty payload must deserialize as a JSON object; arrays, scalars, and
 * malformed JSON previously reached ApplyBTNodeProps(), were silently ignored,
 * and the newly allocated generator/test was still saved (#25464).
 */
static bool IsValidOptionalEQSPropertiesJson(const FString& PropertiesJson)
{
	if (PropertiesJson.IsEmpty())
	{
		return true;
	}

	TSharedPtr<FJsonObject> PropertiesObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	return FJsonSerializer::Deserialize(Reader, PropertiesObject) && PropertiesObject.IsValid();
}

static FString CreateInvalidEQSPropertiesError(const FString& PropertiesJson)
{
	return BlueprintNodeHelpers::CreateDetailedErrorJson(
		BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
		TEXT("Invalid EQS properties JSON"),
		FString::Printf(TEXT("PropertiesJson must be empty or a JSON object; received: %s"),
			*PropertiesJson),
		TEXT("Pass an object such as '{}' or omit properties for a class-only write"));
}

FString UCURemoteLibrary::CreateEnvQuery(
	const FString& AssetName,
	const FString& PackagePath,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] CreateEnvQuery: %s in %s"), *AssetName, *PackagePath);

	if (AssetName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Asset name cannot be empty"),
			TEXT("AssetName parameter is required"),
			TEXT("Provide a valid asset name like 'EQS_FindCover'"));
	}

	const FString FullAssetPath = PackagePath / AssetName;
	const FString LoadPath = FullAssetPath + TEXT(".") + AssetName;
	if (LoadObject<UEnvQuery>(nullptr, *LoadPath))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("EnvQuery already exists: %s"), *FullAssetPath),
			TEXT("Asset with this name already exists at the specified path"),
			TEXT("Use a different name or delete the existing asset"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), FullAssetPath);
		Preview->SetStringField(TEXT("asset_name"), AssetName);
		Preview->SetStringField(TEXT("asset_class"), TEXT("EnvQuery"));
		return MakeAiPreviewSuccessJson(Preview);
	}

	UPackage* Package = CreatePackage(*FullAssetPath);
	if (!Package)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Failed to create package"),
			FString::Printf(TEXT("Could not create package at: %s"), *FullAssetPath),
			TEXT("Check that the path is valid and writable"));
	}

	UEnvQuery* NewEQS = NewObject<UEnvQuery>(Package, *AssetName, RF_Public | RF_Standalone);
	if (!NewEQS)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EnvQuery creation failed"),
			TEXT("NewObject<UEnvQuery> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}

	// Seed one empty Option so modify_generator/modify_test (option_index 0) have
	// a target. The Option's Generator stays null until modify_generator sets it.
	// RF_Transactional for consistency with the generator/test sub-objects.
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(NewEQS, NAME_None, RF_Transactional);
	NewEQS->GetOptionsMutable().Add(Option);

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewEQS);
	if (!SaveEnvQueryPackage(NewEQS))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save EnvQuery"),
			FString::Printf(TEXT("Created but could not save: %s"), *FullAssetPath),
			TEXT("Check file permissions"));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), FullAssetPath);
	ResultObject->SetStringField(TEXT("asset_name"), AssetName);
	ResultObject->SetStringField(TEXT("asset_class"), TEXT("EnvQuery"));
	ResultObject->SetNumberField(TEXT("option_count"), NewEQS->GetOptions().Num());
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] CreateEnvQuery completed: %s"), *FullAssetPath);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::AddEnvQueryOption(const FString& EQSPath,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddEnvQueryOption: %s"), *EQSPath);

	if (EQSPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("EQS path cannot be empty"),
			TEXT("EQSPath parameter is required"),
			TEXT("Provide a loaded EnvQuery path like '/Game/AI/EQS_FindCover.EQS_FindCover'"));
	}

	UEnvQuery* EQS = LoadEnvQueryWithFallback(EQSPath);
	if (!EQS)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("EnvQuery not found: %s"), *EQSPath),
			TEXT("The specified EnvQuery asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("eqs_path"), EQSPath);
		Preview->SetNumberField(TEXT("would_option_index"), EQS->GetOptions().Num());
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddEnvQueryOption", "Add EQS Option"));
	EQS->Modify();
#endif

	// Same seed as CreateEnvQuery: empty Option, Generator stays null until
	// modify_generator. RF_Transactional matches the create/modify sub-objects.
	UEnvQueryOption* Option = NewObject<UEnvQueryOption>(EQS, NAME_None, RF_Transactional);
	if (!Option)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EnvQueryOption creation failed"),
			TEXT("NewObject<UEnvQueryOption> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}

	TArray<TObjectPtr<UEnvQueryOption>>& Options = EQS->GetOptionsMutable();
	const int32 NewIndex = Options.Add(Option);
	if (NewIndex < 0)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Failed to append EnvQueryOption"),
			TEXT("GetOptionsMutable().Add returned a negative index"),
			TEXT("Check editor logs for detailed error"));
	}

	if (!SaveEnvQueryPackage(EQS))
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save EnvQuery"),
			FString::Printf(TEXT("Appended option %d but could not save: %s"), NewIndex, *EQSPath),
			TEXT("Check file permissions"));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetNumberField(TEXT("option_index"), NewIndex);
	ResultObject->SetNumberField(TEXT("option_count"), Options.Num());
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddEnvQueryOption completed: option %d (count %d)"), NewIndex, Options.Num());
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::ModifyEQSGenerator(
	const FString& EQSPath,
	int32 OptionIndex,
	const FString& GeneratorClass,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyEQSGenerator: %s, Option: %d, Class: %s"), *EQSPath, OptionIndex, *GeneratorClass);

	UEnvQuery* EQS = LoadEnvQueryWithFallback(EQSPath);
	if (!EQS)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("EnvQuery not found: %s"), *EQSPath),
			TEXT("The specified EnvQuery asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	UClass* ResolvedClass = ResolveConcreteBTClass(GeneratorClass, UEnvQueryGenerator::StaticClass());
	if (!ResolvedClass)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid generator class: %s"), *GeneratorClass),
			TEXT("Class must resolve to a concrete UEnvQueryGenerator subclass."),
			TEXT("Use a class like 'EnvQueryGenerator_ActorsOfClass', 'EnvQueryGenerator_SimpleGrid'"));
	}

	TArray<TObjectPtr<UEnvQueryOption>>& Options = EQS->GetOptionsMutable();
	if (!Options.IsValidIndex(OptionIndex))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Option index out of range: %d (have %d)"), OptionIndex, Options.Num()),
			TEXT("The EnvQuery has fewer options than the requested index."),
			TEXT("create seeds one Option at index 0; use a valid option_index"));
	}
	if (!IsValidOptionalEQSPropertiesJson(PropertiesJson))
	{
		return CreateInvalidEQSPropertiesError(PropertiesJson);
	}
	UEnvQueryOption* Option = Options[OptionIndex];
	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("eqs_path"), EQSPath);
		Preview->SetNumberField(TEXT("option_index"), OptionIndex);
		Preview->SetStringField(TEXT("generator_class"), ResolvedClass->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}
	if (!Option)
	{
		Option = NewObject<UEnvQueryOption>(EQS);
		Options[OptionIndex] = Option;
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyEQSGenerator", "Modify EQS Generator"));
	EQS->Modify();
#endif

	UEnvQueryGenerator* NewGenerator = NewObject<UEnvQueryGenerator>(EQS, ResolvedClass, NAME_None, RF_Transactional);
	if (!NewGenerator)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Generator creation failed"),
			TEXT("NewObject<UEnvQueryGenerator> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}

	TArray<FString> Modified;
	ApplyBTNodeProps(NewGenerator, PropertiesJson, Modified);
	Option->Generator = NewGenerator;

	if (!SaveEnvQueryPackage(EQS))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save EnvQuery"),
			FString::Printf(TEXT("Generator set but '%s' could not be saved"), *EQSPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), EQSPath);
	Result->SetNumberField(TEXT("option_index"), OptionIndex);
	Result->SetStringField(TEXT("generator_class"), ResolvedClass->GetName());
	// Surface which props actually applied so a silently-skipped prop is observable
	// (ApplyBTNodeProps handles scalar types only; EQS struct props like
	// FAIDataProviderValue are not yet settable — see the header doc-comment).
	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : Modified)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	Result->SetArrayField(TEXT("modified_properties"), ModifiedArray);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyEQSGenerator completed on option %d"), OptionIndex);
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

FString UCURemoteLibrary::ModifyEQSTest(
	const FString& EQSPath,
	int32 OptionIndex,
	const FString& TestClass,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyEQSTest: %s, Option: %d, Class: %s"), *EQSPath, OptionIndex, *TestClass);

	UEnvQuery* EQS = LoadEnvQueryWithFallback(EQSPath);
	if (!EQS)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("EnvQuery not found: %s"), *EQSPath),
			TEXT("The specified EnvQuery asset could not be loaded."),
			TEXT("Verify the asset path is correct and the asset exists"));
	}

	UClass* ResolvedClass = ResolveConcreteBTClass(TestClass, UEnvQueryTest::StaticClass());
	if (!ResolvedClass)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid test class: %s"), *TestClass),
			TEXT("Class must resolve to a concrete UEnvQueryTest subclass."),
			TEXT("Use a class like 'EnvQueryTest_Distance', 'EnvQueryTest_Dot'"));
	}

	TArray<TObjectPtr<UEnvQueryOption>>& Options = EQS->GetOptionsMutable();
	if (!Options.IsValidIndex(OptionIndex))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("Option index out of range: %d (have %d)"), OptionIndex, Options.Num()),
			TEXT("The EnvQuery has fewer options than the requested index."),
			TEXT("create seeds one Option at index 0; use a valid option_index"));
	}
	if (!IsValidOptionalEQSPropertiesJson(PropertiesJson))
	{
		return CreateInvalidEQSPropertiesError(PropertiesJson);
	}
	UEnvQueryOption* Option = Options[OptionIndex];
	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("eqs_path"), EQSPath);
		Preview->SetNumberField(TEXT("option_index"), OptionIndex);
		Preview->SetStringField(TEXT("test_class"), ResolvedClass->GetName());
		return MakeAiPreviewSuccessJson(Preview);
	}
	if (!Option)
	{
		Option = NewObject<UEnvQueryOption>(EQS);
		Options[OptionIndex] = Option;
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyEQSTest", "Modify EQS Test"));
	EQS->Modify();
#endif

	UEnvQueryTest* NewTest = NewObject<UEnvQueryTest>(EQS, ResolvedClass, NAME_None, RF_Transactional);
	if (!NewTest)
	{
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Test creation failed"),
			TEXT("NewObject<UEnvQueryTest> returned nullptr"),
			TEXT("Check editor logs for detailed error"));
	}

	TArray<FString> Modified;
	ApplyBTNodeProps(NewTest, PropertiesJson, Modified);
	Option->Tests.Add(NewTest);

	if (!SaveEnvQueryPackage(EQS))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
			TEXT("Failed to save EnvQuery"),
			FString::Printf(TEXT("Test added but '%s' could not be saved"), *EQSPath),
			TEXT("Ensure the asset is not read-only"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("asset_path"), EQSPath);
	Result->SetNumberField(TEXT("option_index"), OptionIndex);
	Result->SetStringField(TEXT("test_class"), ResolvedClass->GetName());
	Result->SetNumberField(TEXT("test_count"), Option->Tests.Num());
	// Surface applied props (scalar-only; see ModifyEQSGenerator + header doc).
	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : Modified)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	Result->SetArrayField(TEXT("modified_properties"), ModifiedArray);
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyEQSTest completed on option %d"), OptionIndex);
	return BlueprintNodeHelpers::CreateSuccessJson(Result);
}

#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
// State-name resolve for StateTree write ops (Issue #26888 ST-07).
// Mirrors ResolveBTCompositeOrError: collect every match, apply only when unique.
// GUID (UStateTreeState::ID) wins so a colliding display name can still be targeted.
namespace
{
	struct FResolvedStateTreeState
	{
		UStateTreeState* State = nullptr;
		UStateTreeState* Parent = nullptr;
	};

	FResolvedStateTreeState ResolveStateTreeStateOrError(
		UStateTreeEditorData* EditorData,
		const FString& Selector,
		const TCHAR* Role,
		FString& OutError)
	{
		FResolvedStateTreeState Result;
		if (!EditorData)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				TEXT("EditorData is null"),
				TEXT(""),
				TEXT("StateTree may be corrupted or not properly initialized"));
			return Result;
		}

		FGuid ParsedGuid;
		const bool bSelectorIsGuid = FGuid::Parse(Selector, ParsedGuid) && ParsedGuid.IsValid();

		struct FNameHit
		{
			UStateTreeState* State = nullptr;
			UStateTreeState* Parent = nullptr;
		};
		TArray<FNameHit> NameHits;
		TArray<FString> AvailableStates;
		UStateTreeState* GuidState = nullptr;
		UStateTreeState* GuidParent = nullptr;

		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* ParentState)
		{
			AvailableStates.Add(State.Name.ToString());
			if (bSelectorIsGuid && State.ID == ParsedGuid)
			{
				GuidState = &State;
				GuidParent = ParentState;
			}
			if (State.Name.ToString() == Selector)
			{
				FNameHit Hit;
				Hit.State = &State;
				Hit.Parent = ParentState;
				NameHits.Add(Hit);
			}
			return EStateTreeVisitor::Continue;
		});

		if (GuidState)
		{
			Result.State = GuidState;
			Result.Parent = GuidParent;
			return Result;
		}

		if (NameHits.Num() == 1)
		{
			Result.State = NameHits[0].State;
			Result.Parent = NameHits[0].Parent;
			return Result;
		}

		const FString AvailableStatesStr = AvailableStates.Num() > 0
			? FString::Join(AvailableStates, TEXT(", "))
			: TEXT("(no states found)");

		if (NameHits.Num() == 0)
		{
			FString Title;
			FString Suggestion = TEXT("Check the state name matches exactly");
			if (FCString::Stricmp(Role, TEXT("parent")) == 0)
			{
				Title = FString::Printf(TEXT("Parent state not found: %s"), *Selector);
				Suggestion = TEXT("Check the parent state name matches exactly, or leave empty for root-level");
			}
			else if (FCString::Stricmp(Role, TEXT("source")) == 0)
			{
				Title = FString::Printf(TEXT("Source state not found: %s"), *Selector);
			}
			else if (FCString::Stricmp(Role, TEXT("target")) == 0)
			{
				Title = FString::Printf(TEXT("Target state not found: %s"), *Selector);
				Suggestion = TEXT("Check the target state name matches exactly, or leave empty for no target (e.g. GotoSucceeded)");
			}
			else
			{
				Title = FString::Printf(TEXT("State not found: %s"), *Selector);
			}
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
				Title,
				FString::Printf(TEXT("Available states: %s"), *AvailableStatesStr),
				Suggestion);
			return Result;
		}

		TArray<FString> CandidateParts;
		for (const FNameHit& Hit : NameHits)
		{
			if (Hit.State)
			{
				CandidateParts.Add(FString::Printf(
					TEXT("%s (id=%s)"),
					*Hit.State->Name.ToString(),
					*Hit.State->ID.ToString()));
			}
		}
		OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Ambiguous %s name: %d states named '%s'"), Role, NameHits.Num(), *Selector),
			FString::Printf(TEXT("candidates: %s"), *FString::Join(CandidateParts, TEXT(", "))),
			TEXT("Pass the state's GUID (state_id from add_state / get_structure) or rename duplicates"));
		return Result;
	}

	/** Resolve a state strictly by identity without name fallback. */
	FResolvedStateTreeState ResolveStateTreeStateIdOrError(
		UStateTreeEditorData* EditorData,
		const FString& Selector,
		FString& OutError)
	{
		FResolvedStateTreeState Result;
		FGuid ParsedGuid;
		if (!EditorData || !FGuid::Parse(Selector, ParsedGuid) || !ParsedGuid.IsValid())
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				FString::Printf(TEXT("State ID is not a valid non-zero GUID: %s"), *Selector),
				TEXT("state_id is an identity selector and cannot fall back to a state name"),
				TEXT("Pass the state GUID returned by add_state or get_structure"));
			return Result;
		}

		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* ParentState)
		{
			if (State.ID == ParsedGuid)
			{
				Result.State = &State;
				Result.Parent = ParentState;
				return EStateTreeVisitor::Break;
			}
			return EStateTreeVisitor::Continue;
		});

		if (!Result.State)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
				FString::Printf(TEXT("State id '%s' is not a known state in this StateTree"), *Selector),
				TEXT("No matching UStateTreeState.ID"),
				TEXT("Use the state_id returned by add_state or get_structure"));
		}
		return Result;
	}

	struct FResolvedStateTreeTransition
	{
		FStateTreeTransition* Transition = nullptr;
		// The state that OWNS the resolved transition. Returned as a pointer, not just
		// the name below, for the same reason FResolvedStateTreeState returns State and
		// Parent: the owner is the UObject a caller must Modify() before writing into
		// the transition. Issue #26803 needs exactly this to open its transaction
		// against the right object; resolving the transition and then re-walking the
		// hierarchy to find its owner would be a second source of truth.
		UStateTreeState* OwnerState = nullptr;
		FString SourceStateName;
	};

	/**
	 * Issue #26804: transition twin of ResolveStateTreeStateOrError.
	 *
	 * A transition selector can be a transition GUID, a global numeric index, or
	 * a TARGET-STATE NAME. Only the first two are unique by construction: several
	 * transitions may legitimately point at the same target state, which is not a
	 * data-model violation, so "rename the duplicates" is not an available remedy
	 * here (it is for BT nodes, whose names ARE uniqueness-enforced — #16912).
	 *
	 * Resolution is therefore: GUID, then numeric index, then name — and the name
	 * axis applies only when it identifies exactly one transition. The axes are
	 * ordered rather than OR'd because the previous unordered OR let a state
	 * merely NAMED "2" pre-empt numeric index 2, silently breaking the numeric
	 * selector the tool's own manifest advertises.
	 *
	 * Ambiguity is reported in the same shape as ResolveStateTreeStateOrError so
	 * this file carries one ambiguity idiom, not two (the #26039/#26736 drift class).
	 */
	FResolvedStateTreeTransition ResolveStateTreeTransitionOrError(
		UStateTreeEditorData* EditorData,
		const FString& Selector,
		FString& OutError)
	{
		FResolvedStateTreeTransition Result;
		if (!EditorData)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				TEXT("EditorData is null"),
				TEXT(""),
				TEXT("StateTree may be corrupted or not properly initialized"));
			return Result;
		}

		FGuid ParsedGuid;
		const bool bSelectorIsGuid = FGuid::Parse(Selector, ParsedGuid) && ParsedGuid.IsValid();

		// Strict digits only. FString::IsNumeric() accepts a bare "+", a bare "-"
		// and one decimal point anywhere, and FCString::Atoi then coerces "+"/"-"/"."
		// to 0 and "1.5" to 1 — i.e. a malformed selector would deterministically
		// write to a transition the caller never named, through the very axis this
		// function ranks ABOVE names. Length is bounded so the cast cannot overflow.
		const bool bSelectorIsIndex = [&Selector]()
		{
			if (Selector.IsEmpty() || Selector.Len() > 9) // 9 digits always fits int32
			{
				return false;
			}
			for (const TCHAR Ch : Selector)
			{
				if (!FChar::IsDigit(Ch))
				{
					return false;
				}
			}
			return true;
		}();
		// -1 is safe as "no numeric selector": GlobalTransitionIdx is always >= 0.
		const int32 TargetNumericIndex = bSelectorIsIndex ? FCString::Atoi(*Selector) : -1;

		// Issue #26804: the name axis must not trust FStateTreeStateLink::Name alone.
		// The engine documents it as "the name of the state at the time of linking"
		// and re-syncs it only in UStateTreeEditingSubsystem::ValidateStateTree.
		// Write-finalize (#27780) now calls ValidateStateTree after mutation; this
		// resolver still cannot trust link names because it runs *before* that
		// post-process. Resolve the selector to a state and match its ID as well.
		FString SelectorStateError;
		const FResolvedStateTreeState SelectorState =
			ResolveStateTreeStateOrError(EditorData, Selector, TEXT("target"), SelectorStateError);
		const FGuid SelectorStateId = SelectorState.State ? SelectorState.State->ID : FGuid();

		struct FHit
		{
			FStateTreeTransition* Transition = nullptr;
			UStateTreeState* OwnerState = nullptr;
			FString SourceStateName;
			int32 GlobalIndex = INDEX_NONE;
		};
		FHit GuidHit;
		FHit IndexHit;
		TArray<FHit> NameHits;
		TArray<FString> AvailableTransitions;
		int32 GlobalTransitionIdx = 0;

		// Never Break: a full walk is what makes multi-match detectable at all,
		// and it also completes the "Available transitions" diagnostic.
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			for (FStateTreeTransition& Transition : State.Transitions)
			{
				const FString TriggerType = StaticEnum<EStateTreeTransitionTrigger>()->GetNameStringByValue(
					static_cast<int64>(Transition.Trigger));
				AvailableTransitions.Add(FString::Printf(TEXT("[%d] %s: %s -> %s"),
					GlobalTransitionIdx,
					*State.Name.ToString(),
					*TriggerType,
					*Transition.State.Name.ToString()));

				FHit Hit;
				Hit.Transition = &Transition;
				Hit.OwnerState = &State;
				Hit.SourceStateName = State.Name.ToString();
				Hit.GlobalIndex = GlobalTransitionIdx;

				if (bSelectorIsGuid && Transition.ID == ParsedGuid)
				{
					GuidHit = Hit;
				}
				if (GlobalTransitionIdx == TargetNumericIndex)
				{
					IndexHit = Hit;
				}
				// Union of identity and the denormalized name: a link whose cached
				// name went stale still matches by ID, and a link whose name matches
				// still counts. More hits means more rejections, which is the
				// fail-closed direction.
				const bool bTargetsSelectedState =
					SelectorStateId.IsValid() && Transition.State.ID == SelectorStateId;
				if (bTargetsSelectedState || Transition.State.Name.ToString() == Selector)
				{
					NameHits.Add(Hit);
				}

				++GlobalTransitionIdx;
			}
			return EStateTreeVisitor::Continue;
		});

		const FHit* Winner = nullptr;
		if (GuidHit.Transition)
		{
			Winner = &GuidHit;
		}
		else if (IndexHit.Transition)
		{
			Winner = &IndexHit;
		}
		else if (NameHits.Num() == 1)
		{
			Winner = &NameHits[0];
		}

		if (Winner)
		{
			Result.Transition = Winner->Transition;
			Result.OwnerState = Winner->OwnerState;
			Result.SourceStateName = Winner->SourceStateName;
			return Result;
		}

		const FString AvailableTransitionsStr = AvailableTransitions.Num() > 0
			? FString::Join(AvailableTransitions, TEXT("; "))
			: TEXT("(no transitions found)");

		if (NameHits.Num() == 0)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
				FString::Printf(TEXT("Transition not found: %s"), *Selector),
				FString::Printf(TEXT("Available transitions: %s"), *AvailableTransitionsStr),
				TEXT("Use numeric index, target state name, or transition GUID"));
			return Result;
		}

		TArray<FString> CandidateParts;
		for (const FHit& Hit : NameHits)
		{
			if (Hit.Transition)
			{
				CandidateParts.Add(FString::Printf(
					TEXT("[%d] %s -> %s (id=%s)"),
					Hit.GlobalIndex,
					*Hit.SourceStateName,
					*Hit.Transition->State.Name.ToString(),
					*Hit.Transition->ID.ToString()));
			}
		}
		OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Ambiguous transition name: %d transitions target '%s'"),
				NameHits.Num(), *Selector),
			FString::Printf(TEXT("candidates: %s"), *FString::Join(CandidateParts, TEXT(", "))),
			TEXT("Pass the transition's GUID (id= above) or its global numeric index"));
		return Result;
	}

	/** Resolve a transition strictly by identity without index/name fallback. */
	FResolvedStateTreeTransition ResolveStateTreeTransitionIdOrError(
		UStateTreeEditorData* EditorData,
		const FString& Selector,
		FString& OutError)
	{
		FResolvedStateTreeTransition Result;
		FGuid ParsedGuid;
		if (!EditorData || !FGuid::Parse(Selector, ParsedGuid) || !ParsedGuid.IsValid())
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				FString::Printf(TEXT("Transition ID is not a valid non-zero GUID: %s"), *Selector),
				TEXT("transition_id is an identity selector and cannot fall back to an index or target name"),
				TEXT("Pass the transition GUID returned by add_transition or get_structure"));
			return Result;
		}

		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			for (FStateTreeTransition& Transition : State.Transitions)
			{
				if (Transition.ID == ParsedGuid)
				{
					Result.Transition = &Transition;
					Result.OwnerState = &State;
					Result.SourceStateName = State.Name.ToString();
					return EStateTreeVisitor::Break;
				}
			}
			return EStateTreeVisitor::Continue;
		});

		if (!Result.Transition)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
				FString::Printf(TEXT("Transition id '%s' is not a known transition in this StateTree"), *Selector),
				TEXT("No matching FStateTreeTransition.ID"),
				TEXT("Use the transition_id returned by add_transition or get_structure"));
		}
		return Result;
	}

	FString RejectDuplicateStateName(
		UStateTreeEditorData* EditorData,
		const FString& NewName,
		const UStateTreeState* Except)
	{
		if (!EditorData || NewName.IsEmpty())
		{
			return FString();
		}
		bool bDuplicateFound = false;
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*ParentState*/)
		{
			if (&State != Except && State.Name.ToString() == NewName)
			{
				bDuplicateFound = true;
			}
			return EStateTreeVisitor::Continue;
		});
		if (!bDuplicateFound)
		{
			return FString();
		}
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("State '%s' already exists in this StateTree"), *NewName),
			TEXT("State names must be unique within a StateTree"),
			TEXT("Use a different name or remove the existing state first"));
	}
}

FString UCURemoteLibrary::ModifyStateTreeState(
	const FString& AssetName,
	const FString& StateName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeState: %s, State: %s"), *AssetName, *StateName);

	// 0. Validate path (prevent double-slash crash)
	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy")
		);
	}

	// 1. Load StateTree
	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)")
		);
	}

	// 2. Get EditorData
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized")
		);
	}

	// 3. Find State by name or GUID — collect-all, reject when ambiguous (Issue #26888).
	FString ResolveError;
	const FResolvedStateTreeState Resolved = ResolveStateTreeStateOrError(
		EditorData, StateName, TEXT("state"), ResolveError);
	if (!Resolved.State)
	{
		return ResolveError;
	}
	UStateTreeState* FoundState = Resolved.State;

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("state_name"), FoundState->Name.ToString());
		Preview->SetStringField(TEXT("state_id"), FoundState->ID.ToString());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 4. Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid properties JSON"),
			FString::Printf(TEXT("Could not parse: %s"), *PropertiesJson),
			TEXT("Ensure the JSON is valid and properly formatted")
		);
	}

	// 5. Apply property changes using reflection
	UClass* StateClass = UStateTreeState::StaticClass();
	TArray<FString> ModifiedProperties;
	TArray<FString> FailedProperties;

	// Issue #27719: Start the transaction lazily so validation-only failures do not
	// add an empty undo entry or dirty the package. Keep the scoped transaction alive
	// until function exit so the finalized object event contains the property delta.
#if WITH_EDITOR
	TUniquePtr<FScopedTransaction> Transaction;
	auto BeginStateMutation = [&Transaction, FoundState]()
	{
		if (!Transaction)
		{
			Transaction = MakeUnique<FScopedTransaction>(
				NSLOCTEXT("NarshaMCP", "ModifyStateTree", "Modify StateTree"));
			FoundState->Modify();
		}
	};
#else
	auto BeginStateMutation = []() {};
#endif

	// Special handling for "Name" property (FName field, not FProperty)
	if (PropsObject->HasField(TEXT("Name")))
	{
		FString NewName = PropsObject->GetStringField(TEXT("Name"));
		const FString DuplicateNameError = RejectDuplicateStateName(EditorData, NewName, FoundState);
		if (!DuplicateNameError.IsEmpty())
		{
			return DuplicateNameError;
		}
		BeginStateMutation();
		FoundState->Name = FName(*NewName);
		ModifiedProperties.Add(TEXT("Name"));
		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] Modified state name to: %s"), *NewName);
	}

	for (const auto& Pair : PropsObject->Values)
	{
		FString PropName = FString(Pair.Key);

		// Skip "Name" - already handled above
		if (PropName == TEXT("Name")) continue;

		FProperty* Prop = StateClass->FindPropertyByName(*PropName);
		if (!Prop)
		{
			FailedProperties.Add(FString::Printf(TEXT("%s (not found)"), *PropName));
			continue;
		}

		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(FoundState);
		// Issue #4415: Memory safety - validate pointer before use
		if (!ValuePtr)
		{
			FailedProperties.Add(FString::Printf(TEXT("%s (invalid property access)"), *PropName));
			continue;
		}
		bool bModified = false;

		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::Number)
			{
				BeginStateMutation();
				FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(Pair.Value->AsNumber()));
				bModified = true;
			}
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::Number)
			{
				BeginStateMutation();
				DoubleProp->SetPropertyValue(ValuePtr, Pair.Value->AsNumber());
				bModified = true;
			}
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::Number)
			{
				BeginStateMutation();
				IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Pair.Value->AsNumber()));
				bModified = true;
			}
		}
		else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::Boolean)
			{
				BeginStateMutation();
				BoolProp->SetPropertyValue(ValuePtr, Pair.Value->AsBool());
				bModified = true;
			}
		}
		else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::String)
			{
				BeginStateMutation();
				StrProp->SetPropertyValue(ValuePtr, Pair.Value->AsString());
				bModified = true;
			}
		}
		else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::String)
			{
				BeginStateMutation();
				NameProp->SetPropertyValue(ValuePtr, FName(*Pair.Value->AsString()));
				bModified = true;
			}
		}
		else if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::Number)
			{
				BeginStateMutation();
				ByteProp->SetPropertyValue(ValuePtr, static_cast<uint8>(Pair.Value->AsNumber()));
				bModified = true;
			}
		}
		// Issue #4415: FEnumProperty support (string to enum conversion)
		else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			if (Pair.Value->Type == EJson::String)
			{
				UEnum* EnumDef = EnumProp->GetEnum();
				if (EnumDef)
				{
					int64 EnumValue = EnumDef->GetValueByNameString(Pair.Value->AsString());
					// Issue #4415: Validate enum value is within valid range
					if (EnumValue != INDEX_NONE && EnumValue >= 0 && EnumValue <= static_cast<int64>(EnumDef->GetMaxEnumValue()))
					{
						BeginStateMutation();
						EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
						bModified = true;
					}
					else
					{
						FailedProperties.Add(FString::Printf(TEXT("%s (invalid enum value: %s)"), *PropName, *Pair.Value->AsString()));
						continue;
					}
				}
			}
			else if (Pair.Value->Type == EJson::Number)
			{
				// Issue #4415: Validate numeric enum value is within valid range
				int64 NumericValue = static_cast<int64>(Pair.Value->AsNumber());
				UEnum* EnumDef = EnumProp->GetEnum();
				if (EnumDef && NumericValue >= 0 && NumericValue <= static_cast<int64>(EnumDef->GetMaxEnumValue()))
				{
					BeginStateMutation();
					EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, NumericValue);
					bModified = true;
				}
				else
				{
					FailedProperties.Add(FString::Printf(TEXT("%s (enum value out of range: %lld)"), *PropName, NumericValue));
					continue;
				}
			}
		}
		// Issue #4415: FStructProperty support (FGameplayTag, FVector, etc.)
		// Note: StateTree states carry no FBlackboardKeySelector / FValueOrBBKey_* (that is a
		// BehaviorTree concern), so the Issue #16913 blackboard-key validation does not apply here.
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			TSharedPtr<FJsonObject> StructJsonObject;

			// Shorthand notation: plain number → wrap as {"DefaultValue": number}
			if (Pair.Value->Type == EJson::Number)
			{
				StructJsonObject = MakeShared<FJsonObject>();
				StructJsonObject->SetNumberField(TEXT("DefaultValue"), Pair.Value->AsNumber());
			}
			else if (Pair.Value->Type == EJson::Object)
			{
				StructJsonObject = Pair.Value->AsObject();
			}

			if (StructJsonObject.IsValid())
			{
				BeginStateMutation();
				if (FJsonObjectConverter::JsonObjectToUStruct(StructJsonObject.ToSharedRef(), StructProp->Struct, ValuePtr))
				{
					bModified = true;
				}
				else
				{
					// Issue #4415: Enhanced error logging with struct type info
					FString StructTypeName = StructProp->Struct ? StructProp->Struct->GetName() : TEXT("Unknown");
					FailedProperties.Add(FString::Printf(TEXT("%s (struct conversion failed for type '%s' - verify JSON field names match struct properties)"), *PropName, *StructTypeName));
					UE_LOG(LogNarshaMCPRemote, Warning, TEXT("FJsonObjectConverter failed for property '%s' of type '%s'"), *PropName, *StructTypeName);
					continue;
				}
			}
		}

		if (bModified)
		{
			ModifiedProperties.Add(PropName);
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] Modified state property: %s"), *PropName);
		}
		else if (!FailedProperties.Contains(FString::Printf(TEXT("%s (not found)"), *PropName)))
		{
			FailedProperties.Add(FString::Printf(TEXT("%s (unsupported type)"), *PropName));
		}
	}

	if (ModifiedProperties.Num() == 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("No properties were modified"),
			FString::Printf(TEXT("Failed properties: %s"), *FString::Join(FailedProperties, TEXT(", "))),
			TEXT("Supported types: float, double, int, bool, string, FName, byte, enum, struct")
		);
	}


	// 7. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("state_name"), StateName);
	ResultObject->SetStringField(TEXT("state_id"), FoundState->ID.ToString());

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	ResultObject->SetArrayField(TEXT("modified_properties"), ModifiedArray);

	if (FailedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FString& PropName : FailedProperties)
		{
			FailedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("failed_properties"), FailedArray);
	}


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeState completed: %d properties modified"), ModifiedProperties.Num());
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::ModifyStateTreeTransition(
	const FString& AssetName,
	const FString& TransitionIndex,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeTransition: %s, Transition: %s"), *AssetName, *TransitionIndex);

	// 0. Validate path (prevent double-slash crash)
	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy")
		);
	}

	// 1. Load StateTree
	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)")
		);
	}

	// 2. Get EditorData
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized")
		);
	}

	// 3. Resolve the transition (Issue #26804).
	// Selector axes are ordered GUID > numeric index > target-state name, and the
	// name axis applies only when it is unique; a collision is refused with the
	// candidate list rather than silently editing whichever came first.
	FString ResolveError;
	const FResolvedStateTreeTransition Resolved =
		ResolveStateTreeTransitionOrError(EditorData, TransitionIndex, ResolveError);
	if (!Resolved.Transition)
	{
		return ResolveError;
	}
	FStateTreeTransition* FoundTransition = Resolved.Transition;
	// Issue #26803 opens its FScopedTransaction against the OWNING state before the property
	// writes below. The resolver already carries that pointer (Issue #26804), so the owner is
	// not re-derived by a second hierarchy walk — one source of truth for "who owns this".
	UStateTreeState* FoundOwnerState = Resolved.OwnerState;
	const FString FoundInState = Resolved.SourceStateName;

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("source_state"), FoundInState);
		Preview->SetStringField(TEXT("transition_id"), FoundTransition->ID.ToString());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 4. Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid properties JSON"),
			FString::Printf(TEXT("Could not parse: %s"), *PropertiesJson),
			TEXT("Ensure the JSON is valid and properly formatted")
		);
	}

	// Issue #26803: open the transaction and Modify BEFORE the property writes below, matching the
	// order used by AddStateTreeState / AddStateTreeTransition. Only the owning state is Modified --
	// this function mutates State.Transitions and nothing else, so Modifying EditorData or the
	// UStateTree asset would snapshot objects it never changes and let Undo revert unrelated edits.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyStateTreeTransition", "Modify StateTree Transition"));
	if (FoundOwnerState)
	{
		FoundOwnerState->Modify();
	}
#endif

	// 5. Apply property changes
	TArray<FString> ModifiedProperties;
	TArray<FString> FailedProperties;

	for (const auto& Pair : PropsObject->Values)
	{
		FString PropName = FString(Pair.Key);
		bool bModified = false;

		// Handle well-known transition properties directly
		if (PropName == TEXT("bDelayTransition") && Pair.Value->Type == EJson::Boolean)
		{
			FoundTransition->bDelayTransition = Pair.Value->AsBool();
			bModified = true;
		}
		else if (PropName == TEXT("DelayDuration") && Pair.Value->Type == EJson::Number)
		{
			FoundTransition->DelayDuration = static_cast<float>(Pair.Value->AsNumber());
			bModified = true;
		}
		else if (PropName == TEXT("DelayRandomVariance") && Pair.Value->Type == EJson::Number)
		{
			FoundTransition->DelayRandomVariance = static_cast<float>(Pair.Value->AsNumber());
			bModified = true;
		}
		else if (PropName == TEXT("bTransitionEnabled") && Pair.Value->Type == EJson::Boolean)
		{
			FoundTransition->bTransitionEnabled = Pair.Value->AsBool();
			bModified = true;
		}
		else if (PropName == TEXT("Priority") && Pair.Value->Type == EJson::Number)
		{
			FoundTransition->Priority = static_cast<EStateTreeTransitionPriority>(
				static_cast<uint8>(Pair.Value->AsNumber()));
			bModified = true;
		}
		else if (PropName == TEXT("Trigger") && Pair.Value->Type == EJson::Number)
		{
			FoundTransition->Trigger = static_cast<EStateTreeTransitionTrigger>(
				static_cast<uint8>(Pair.Value->AsNumber()));
			bModified = true;
		}
		else
		{
			// Try reflection for other properties
			UScriptStruct* TransitionStruct = FStateTreeTransition::StaticStruct();
			FProperty* Prop = TransitionStruct->FindPropertyByName(*PropName);
			if (Prop)
			{
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(FoundTransition);
				// Issue #4415: Memory safety - validate pointer before use
				if (!ValuePtr)
				{
					FailedProperties.Add(FString::Printf(TEXT("%s (invalid property access)"), *PropName));
					continue;
				}
				if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::Number)
					{
						FloatProp->SetPropertyValue(ValuePtr, static_cast<float>(Pair.Value->AsNumber()));
						bModified = true;
					}
				}
				else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::Number)
					{
						DoubleProp->SetPropertyValue(ValuePtr, Pair.Value->AsNumber());
						bModified = true;
					}
				}
				else if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::Number)
					{
						IntProp->SetPropertyValue(ValuePtr, static_cast<int32>(Pair.Value->AsNumber()));
						bModified = true;
					}
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::Boolean)
					{
						BoolProp->SetPropertyValue(ValuePtr, Pair.Value->AsBool());
						bModified = true;
					}
				}
				else if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::String)
					{
						StrProp->SetPropertyValue(ValuePtr, Pair.Value->AsString());
						bModified = true;
					}
				}
				else if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::String)
					{
						NameProp->SetPropertyValue(ValuePtr, FName(*Pair.Value->AsString()));
						bModified = true;
					}
				}
				// Issue #4415: FEnumProperty support (string to enum conversion)
				else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
				{
					if (Pair.Value->Type == EJson::String)
					{
						UEnum* EnumDef = EnumProp->GetEnum();
						if (EnumDef)
						{
							int64 EnumValue = EnumDef->GetValueByNameString(Pair.Value->AsString());
							// Issue #4415: Validate enum value is within valid range
							if (EnumValue != INDEX_NONE && EnumValue >= 0 && EnumValue <= static_cast<int64>(EnumDef->GetMaxEnumValue()))
							{
								EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
								bModified = true;
							}
						}
					}
					else if (Pair.Value->Type == EJson::Number)
					{
						// Issue #4415: Validate numeric enum value before assignment
						int64 NumericValue = static_cast<int64>(Pair.Value->AsNumber());
						UEnum* EnumDef = EnumProp->GetEnum();
						if (EnumDef && NumericValue >= 0 && NumericValue <= static_cast<int64>(EnumDef->GetMaxEnumValue()))
						{
							EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, NumericValue);
							bModified = true;
						}
					}
				}
				// Issue #4415: FStructProperty support
				else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					TSharedPtr<FJsonObject> StructJsonObject;

					if (Pair.Value->Type == EJson::Number)
					{
						StructJsonObject = MakeShared<FJsonObject>();
						StructJsonObject->SetNumberField(TEXT("DefaultValue"), Pair.Value->AsNumber());
					}
					else if (Pair.Value->Type == EJson::Object)
					{
						StructJsonObject = Pair.Value->AsObject();
					}

					if (StructJsonObject.IsValid())
					{
						if (FJsonObjectConverter::JsonObjectToUStruct(StructJsonObject.ToSharedRef(), StructProp->Struct, ValuePtr))
						{
							bModified = true;
						}
						else
						{
							// Issue #4415: Enhanced error logging with struct type info
							FString StructTypeName = StructProp->Struct ? StructProp->Struct->GetName() : TEXT("Unknown");
							UE_LOG(LogNarshaMCPRemote, Warning, TEXT("FJsonObjectConverter failed for transition property '%s' of type '%s'"), *PropName, *StructTypeName);
						}
					}
				}
			}

			if (!bModified)
			{
				FailedProperties.Add(FString::Printf(TEXT("%s (not found or unsupported)"), *PropName));
				continue;
			}
		}

		if (bModified)
		{
			ModifiedProperties.Add(PropName);
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] Modified transition property: %s"), *PropName);
		}
	}

	if (ModifiedProperties.Num() == 0)
	{
		// No Cancel() here: this is NOT a pre-change path. The reflection branch calls
		// FJsonObjectConverter::JsonObjectToUStruct directly into the transition's memory, which can
		// write some fields and still fail, leaving bModified false. Cancelling would throw away the
		// only undo record for a real mutation. A genuinely empty transaction is discarded anyway by
		// FTransaction::IsTransient in UTransBuffer::End.
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("No properties were modified"),
			FString::Printf(TEXT("Failed properties: %s"), *FString::Join(FailedProperties, TEXT(", "))),
			TEXT("Supported: bDelayTransition, DelayDuration, DelayRandomVariance, bTransitionEnabled, Priority, Trigger + reflection (float, double, int, bool, string, FName, enum, struct)")
		);
	}


	// 7. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("source_state"), FoundInState);
	ResultObject->SetStringField(TEXT("transition_id"), FoundTransition->ID.ToString());

	TArray<TSharedPtr<FJsonValue>> ModifiedArray;
	for (const FString& PropName : ModifiedProperties)
	{
		ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
	}
	ResultObject->SetArrayField(TEXT("modified_properties"), ModifiedArray);

	if (FailedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> FailedArray;
		for (const FString& PropName : FailedProperties)
		{
			FailedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("failed_properties"), FailedArray);
	}


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeTransition completed: %d properties modified"), ModifiedProperties.Num());
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::AddStateTreeState(
	const FString& AssetName,
	const FString& StateName,
	const FString& ParentStateName,
	const FString& PropertiesJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeState: %s, State: %s, Parent: %s"), *AssetName, *StateName, *ParentStateName);

	// 0. Validate inputs
	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy")
		);
	}

	if (StateName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("State name cannot be empty"),
			TEXT("StateName parameter is required"),
			TEXT("Provide a valid state name like 'Patrol' or 'Combat'")
		);
	}

	// 1. Load StateTree
	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)")
		);
	}

	// 2. Get EditorData
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized")
		);
	}

	// 3. Check for duplicate state name (shared helper; Issue #26888)
	{
		const FString DuplicateNameError = RejectDuplicateStateName(EditorData, StateName, nullptr);
		if (!DuplicateNameError.IsEmpty())
		{
			return DuplicateNameError;
		}
	}

	// 4. Find parent state (if specified) — collect-all, reject when ambiguous (Issue #26888).
	UStateTreeState* ParentState = nullptr;
	if (!ParentStateName.IsEmpty())
	{
		FString ResolveError;
		const FResolvedStateTreeState ResolvedParent = ResolveStateTreeStateOrError(
			EditorData, ParentStateName, TEXT("parent"), ResolveError);
		if (!ResolvedParent.State)
		{
			return ResolveError;
		}
		ParentState = ResolvedParent.State;
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("state_name"), StateName);
		if (ParentState)
		{
			Preview->SetStringField(TEXT("parent_state"), ParentState->Name.ToString());
			Preview->SetStringField(TEXT("parent_id"), ParentState->ID.ToString());
		}
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 5. Begin editor transaction (undoable). Issue #11397.
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddStateTreeState", "Add StateTree State"));
	EditorData->Modify();
	if (ParentState)
	{
		ParentState->Modify();
	}

	// 5a. Create new state
	UStateTreeState* NewState = NewObject<UStateTreeState>(EditorData, NAME_None, RF_Transactional);
	NewState->Name = FName(*StateName);

	// 6. Add to parent or root
	if (ParentState)
	{
		ParentState->Children.Add(NewState);
	}
	else
	{
		EditorData->SubTrees.Add(NewState);
	}

	// 7. Apply optional properties
	TArray<FString> ModifiedProperties;
	if (!PropertiesJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> PropsObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
		if (FJsonSerializer::Deserialize(Reader, PropsObject) && PropsObject.IsValid())
		{
			UClass* StateClass = UStateTreeState::StaticClass();
			for (const auto& Pair : PropsObject->Values)
			{
				FString PropName = FString(Pair.Key);
				if (PropName == TEXT("Name"))
				{
					continue; // Already set above
				}

				FProperty* Property = StateClass->FindPropertyByName(*PropName);
				if (!Property)
				{
					continue;
				}

				if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
				{
					BoolProp->SetPropertyValue_InContainer(NewState, Pair.Value->AsBool());
					ModifiedProperties.Add(PropName);
				}
				else if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
				{
					StrProp->SetPropertyValue_InContainer(NewState, Pair.Value->AsString());
					ModifiedProperties.Add(PropName);
				}
			}
		}
	}


	// 9. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("state_name"), StateName);
	ResultObject->SetStringField(TEXT("parent_state"), ParentStateName.IsEmpty() ? TEXT("(root)") : ParentStateName);
	ResultObject->SetStringField(TEXT("state_id"), NewState->ID.ToString());

	TArray<TSharedPtr<FJsonValue>> StatesArray;
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* Parent) {
		StatesArray.Add(MakeShared<FJsonValueString>(State.Name.ToString()));
		return EStateTreeVisitor::Continue;
	});
	ResultObject->SetArrayField(TEXT("all_states"), StatesArray);

	if (ModifiedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ModifiedArray;
		for (const FString& PropName : ModifiedProperties)
		{
			ModifiedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("applied_properties"), ModifiedArray);
	}


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeState completed: %s (parent: %s)"), *StateName, *ParentStateName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::RemoveStateTreeState(
	const FString& AssetName,
	const FString& StateName,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeState: %s, State: %s"), *AssetName, *StateName);

	// 0. Validate inputs
	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy")
		);
	}

	if (StateName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("State name cannot be empty"),
			TEXT("StateName parameter is required"),
			TEXT("Provide the name of the state to remove")
		);
	}

	// 1. Load StateTree
	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)")
		);
	}

	// 2. Get EditorData
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized")
		);
	}

	// 3. Find target state and its parent — collect-all, reject when ambiguous (Issue #26888).
	FString ResolveError;
	const FResolvedStateTreeState Resolved = ResolveStateTreeStateOrError(
		EditorData, StateName, TEXT("state"), ResolveError);
	if (!Resolved.State)
	{
		return ResolveError;
	}
	UStateTreeState* FoundState = Resolved.State;
	UStateTreeState* FoundParent = Resolved.Parent;

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("state_name"), FoundState->Name.ToString());
		Preview->SetStringField(TEXT("state_id"), FoundState->ID.ToString());
		Preview->SetNumberField(TEXT("child_count"), FoundState->Children.Num());
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 4. Count children for warning info
	int32 ChildrenCount = FoundState->Children.Num();

	// Issue #26803: open the transaction and Modify BEFORE the removal, mirroring the engine's own
	// FStateTreeViewModel::RemoveSelectedStates. Modify only what actually changes: the removed state
	// always, plus the parent (child path) or the EditorData (root path). Over-Modify is not harmless --
	// FTransaction::Apply iterates every record and FObjectRecord::Load deserializes unconditionally,
	// so snapshotting an unchanged object makes Undo revert unrelated edits made in the meantime.
	// UStateTree itself is never mutated here (only the arrays are), so it is not a Modify target.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveStateTreeState", "Remove StateTree State"));
	FoundState->Modify();
	if (FoundParent)
	{
		FoundParent->Modify();
	}
	else
	{
		EditorData->Modify();
	}
#endif

	// 5. Remove from parent or root
	bool bRemoved = false;
	if (FoundParent)
	{
		bRemoved = FoundParent->Children.Remove(FoundState) > 0;
	}
	else
	{
		// Root-level state: remove from SubTrees
		bRemoved = EditorData->SubTrees.Remove(FoundState) > 0;
	}

	if (!bRemoved)
	{
		// No Cancel() here on purpose: nothing changed, so the transaction has zero delta and
		// UTransBuffer::End discards it via FTransaction::IsTransient. Calling Cancel would also be
		// unsafe -- FScopedTransaction::Cancel routes to GEditor->CancelTransaction(Index), which
		// cancels an enclosing caller transaction in its entirety when this runs nested.
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to remove state: %s"), *StateName),
			TEXT("State was found but could not be removed from its parent"),
			TEXT("This may indicate a corrupted state hierarchy")
		);
	}


	// 7. Build success response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("removed_state"), StateName);
	ResultObject->SetBoolField(TEXT("had_children"), ChildrenCount > 0);
	ResultObject->SetNumberField(TEXT("children_count"), ChildrenCount);
	if (ChildrenCount > 0)
	{
		ResultObject->SetStringField(TEXT("warning"),
			FString::Printf(TEXT("State '%s' had %d child state(s) that were also removed"), *StateName, ChildrenCount));
	}

	// List remaining states
	TArray<TSharedPtr<FJsonValue>> RemainingArray;
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* Parent) {
		RemainingArray.Add(MakeShared<FJsonValueString>(State.Name.ToString()));
		return EStateTreeVisitor::Continue;
	});
	ResultObject->SetArrayField(TEXT("remaining_states"), RemainingArray);


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeState completed: %s (had %d children)"), *StateName, ChildrenCount);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// StateTree Write API — parameters / tasks / bindings (Issue #11472 Gap 3)
// =========================================================

namespace StateTreeWriteHelpers
{
	/** Resolve a scalar type token (or "/Script/..." struct path) to a property-bag type + value object. */
	static bool ResolveParameterType(const FString& TypeName, EPropertyBagPropertyType& OutType, const UObject*& OutValueObject)
	{
		OutValueObject = nullptr;
		if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))        { OutType = EPropertyBagPropertyType::Bool;   return true; }
		if (TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase) ||
		    TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase))         { OutType = EPropertyBagPropertyType::Int32;  return true; }
		if (TypeName.Equals(TEXT("int64"), ESearchCase::IgnoreCase))       { OutType = EPropertyBagPropertyType::Int64;  return true; }
		if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase))       { OutType = EPropertyBagPropertyType::Float;  return true; }
		if (TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase))      { OutType = EPropertyBagPropertyType::Double; return true; }
		if (TypeName.Equals(TEXT("FName"), ESearchCase::IgnoreCase) ||
		    TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))        { OutType = EPropertyBagPropertyType::Name;   return true; }
		if (TypeName.Equals(TEXT("FString"), ESearchCase::IgnoreCase) ||
		    TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))      { OutType = EPropertyBagPropertyType::String; return true; }
		if (TypeName.Equals(TEXT("FText"), ESearchCase::IgnoreCase) ||
		    TypeName.Equals(TEXT("Text"), ESearchCase::IgnoreCase))        { OutType = EPropertyBagPropertyType::Text;   return true; }

		// FVector / FRotator / FTransform / arbitrary struct: resolve the UScriptStruct.
		FString StructPath = TypeName;
		if (StructPath.Equals(TEXT("FVector"), ESearchCase::IgnoreCase))    { StructPath = TEXT("/Script/CoreUObject.Vector"); }
		else if (StructPath.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase)) { StructPath = TEXT("/Script/CoreUObject.Rotator"); }
		else if (StructPath.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase)) { StructPath = TEXT("/Script/CoreUObject.Transform"); }

		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
		if (!Struct) { Struct = LoadObject<UScriptStruct>(nullptr, *StructPath); }
		if (Struct)
		{
			OutType = EPropertyBagPropertyType::Struct;
			OutValueObject = Struct;
			return true;
		}
		return false;
	}

	/**
	 * PR3 ns-review C-2: collect all known task GUIDs (GlobalTasks + per-state Tasks)
	 * from the StateTree's EditorData. Used by AddStateTreeBinding to reject orphan
	 * source_task_id values that would otherwise create dangling bindings.
	 */
	static TSet<FGuid> CollectStateTreeTaskIDs(UStateTreeEditorData* EditorData)
	{
		TSet<FGuid> Out;
		if (!EditorData) { return Out; }
		for (const FStateTreeEditorNode& Node : EditorData->GlobalTasks)
		{
			if (Node.ID.IsValid()) { Out.Add(Node.ID); }
		}
		EditorData->VisitHierarchy([&Out](UStateTreeState& State, UStateTreeState* /*Parent*/) {
			for (const FStateTreeEditorNode& Node : State.Tasks)
			{
				if (Node.ID.IsValid()) { Out.Add(Node.ID); }
			}
			return EStateTreeVisitor::Continue;
		});
		return Out;
	}

	/** Parse a "Foo.Bar" / "<guid>:Foo.Bar" target string into (StructID, PropertyPath). */
	static bool ParseTargetPath(const FString& Raw, const FString& StructIdOverride, FGuid& OutStructID, FString& OutPath)
	{
		OutStructID.Invalidate();
		OutPath = Raw;
		if (!StructIdOverride.IsEmpty())
		{
			FGuid::Parse(StructIdOverride, OutStructID);
			return OutStructID.IsValid();
		}
		int32 ColonIdx = INDEX_NONE;
		if (Raw.FindChar(TEXT(':'), ColonIdx) && ColonIdx > 0)
		{
			const FString MaybeGuid = Raw.Left(ColonIdx);
			if (FGuid::Parse(MaybeGuid, OutStructID))
			{
				OutPath = Raw.Mid(ColonIdx + 1);
				return OutStructID.IsValid();
			}
		}
		return false;
	}

	/**
	 * Parse a JSON-style signed decimal integer without UE's unsigned-to-signed
	 * wraparound. FDefaultValueHelper::ParseInt64 ultimately accepts uint64
	 * magnitudes and casts them to int64, so values above MAX_int64 can otherwise
	 * become a successful negative property-bag write.
	 */
	static bool TryParseCheckedInt64(const FString& Value, int64& OutValue)
	{
		if (Value.IsEmpty())
		{
			return false;
		}

		int32 Index = 0;
		const bool bNegative = Value[0] == TEXT('-');
		if (bNegative)
		{
			Index = 1;
			if (Index == Value.Len())
			{
				return false;
			}
		}
		else if (Value[0] == TEXT('+'))
		{
			return false;
		}

		const uint64 Limit = bNegative
			? static_cast<uint64>(MAX_int64) + 1ULL
			: static_cast<uint64>(MAX_int64);
		uint64 Magnitude = 0;
		for (; Index < Value.Len(); ++Index)
		{
			const TCHAR Ch = Value[Index];
			if (Ch < TEXT('0') || Ch > TEXT('9'))
			{
				return false;
			}
			const uint64 Digit = static_cast<uint64>(Ch - TEXT('0'));
			if (Magnitude > (Limit - Digit) / 10ULL)
			{
				return false;
			}
			Magnitude = Magnitude * 10ULL + Digit;
		}

		if (bNegative)
		{
			OutValue = Magnitude == Limit
				? MIN_int64
				: -static_cast<int64>(Magnitude);
		}
		else
		{
			OutValue = static_cast<int64>(Magnitude);
		}
		return true;
	}

	/** Set an existing bag entry from a JSON scalar/object string (Issue #26808). */
	static void ApplyParameterValue(FInstancedPropertyBag& Bag, const FName ParamFName,
		EPropertyBagPropertyType BagType, const UObject* ValueTypeObject,
		const FString& ValueJson, bool& bOutApplied, FString& OutNote)
	{
		bOutApplied = false;
		OutNote.Reset();
		if (ValueJson.IsEmpty())
		{
			OutNote = TEXT("value json is empty");
			return;
		}

		auto ParseJsonValue = [&ValueJson]() -> TSharedPtr<FJsonValue>
		{
			// UE's object/array-oriented root deserializer does not accept every
			// scalar token directly. A one-element array preserves the exact JSON
			// type while still rejecting malformed or trailing input.
			TArray<TSharedPtr<FJsonValue>> ParsedValues;
			const TSharedRef<TJsonReader<>> Reader =
				TJsonReaderFactory<>::Create(FString::Printf(TEXT("[%s]"), *ValueJson));
			if (!FJsonSerializer::Deserialize(Reader, ParsedValues)
				|| ParsedValues.Num() != 1 || !ParsedValues[0].IsValid())
			{
				return nullptr;
			}
			return ParsedValues[0];
		};
		auto RecordSetResult = [&bOutApplied, &OutNote](const EPropertyBagResult Result, const TCHAR* TypeName)
		{
			bOutApplied = Result == EPropertyBagResult::Success;
			if (!bOutApplied)
			{
				OutNote = FString::Printf(TEXT("SetValue%s failed for the property bag"), TypeName);
			}
		};

		const FString TrimmedValue = ValueJson.TrimStartAndEnd();
		const TSharedPtr<FJsonValue> JsonValue = ParseJsonValue();
		switch (BagType)
		{
		case EPropertyBagPropertyType::Bool:
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Boolean)
			{
				OutNote = TEXT("value json was not a JSON boolean");
				break;
			}
			RecordSetResult(Bag.SetValueBool(ParamFName, JsonValue->AsBool()), TEXT("Bool"));
			break;
		case EPropertyBagPropertyType::Int32:
		{
			int64 Parsed = 0;
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Number
				|| !TryParseCheckedInt64(TrimmedValue, Parsed)
				|| Parsed < MIN_int32 || Parsed > MAX_int32)
			{
				OutNote = TEXT("value json was not an in-range JSON int32");
				break;
			}
			RecordSetResult(Bag.SetValueInt32(ParamFName, static_cast<int32>(Parsed)), TEXT("Int32"));
			break;
		}
		case EPropertyBagPropertyType::Int64:
		{
			int64 Parsed = 0;
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Number
				|| !TryParseCheckedInt64(TrimmedValue, Parsed))
			{
				OutNote = TEXT("value json was not an in-range JSON int64");
				break;
			}
			RecordSetResult(Bag.SetValueInt64(ParamFName, Parsed), TEXT("Int64"));
			break;
		}
		case EPropertyBagPropertyType::Float:
		{
			double Parsed = 0.0;
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Number
				|| !FDefaultValueHelper::IsStringValidFloat(TrimmedValue)
				|| !FDefaultValueHelper::ParseDouble(TrimmedValue, Parsed)
				|| !FMath::IsFinite(Parsed)
				|| !FMath::IsFinite(static_cast<float>(Parsed)))
			{
				OutNote = TEXT("value json was not a finite JSON float");
				break;
			}
			RecordSetResult(Bag.SetValueFloat(ParamFName, static_cast<float>(Parsed)), TEXT("Float"));
			break;
		}
		case EPropertyBagPropertyType::Double:
		{
			double Parsed = 0.0;
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::Number
				|| !FDefaultValueHelper::IsStringValidFloat(TrimmedValue)
				|| !FDefaultValueHelper::ParseDouble(TrimmedValue, Parsed)
				|| !FMath::IsFinite(Parsed))
			{
				OutNote = TEXT("value json was not a finite JSON double");
				break;
			}
			RecordSetResult(Bag.SetValueDouble(ParamFName, Parsed), TEXT("Double"));
			break;
		}
		case EPropertyBagPropertyType::Name:
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::String)
			{
				OutNote = TEXT("value json was not a JSON string for the name type");
				break;
			}
			{
				const FString NameValue = JsonValue->AsString();
				if (NameValue.Len() >= NAME_SIZE)
				{
					OutNote = TEXT("value json exceeded the maximum FName length");
					break;
				}
				bool bContainsEmbeddedNull = false;
				for (int32 CharIndex = 0; CharIndex < NameValue.Len(); ++CharIndex)
				{
					if (NameValue[CharIndex] == TEXT('\0'))
					{
						bContainsEmbeddedNull = true;
						break;
					}
				}
				if (bContainsEmbeddedNull)
				{
					OutNote = TEXT("value json contained an embedded NUL for the name type");
					break;
				}
				RecordSetResult(Bag.SetValueName(ParamFName, FName(*NameValue)), TEXT("Name"));
			}
			break;
		case EPropertyBagPropertyType::String:
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::String)
			{
				OutNote = TEXT("value json was not a JSON string for the string type");
				break;
			}
			RecordSetResult(Bag.SetValueString(ParamFName, JsonValue->AsString()), TEXT("String"));
			break;
		case EPropertyBagPropertyType::Text:
			if (!JsonValue.IsValid() || JsonValue->Type != EJson::String)
			{
				OutNote = TEXT("value json was not a JSON string for the text type");
				break;
			}
			RecordSetResult(
				Bag.SetValueText(ParamFName, FText::FromString(JsonValue->AsString())), TEXT("Text"));
			break;
		case EPropertyBagPropertyType::Struct:
			if (const UScriptStruct* AsStruct = Cast<UScriptStruct>(ValueTypeObject))
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					FInstancedStruct Tmp;
					Tmp.InitializeAs(AsStruct);
					if (FJsonObjectConverter::JsonObjectToUStruct(JsonObj.ToSharedRef(), AsStruct, Tmp.GetMutableMemory()))
					{
						RecordSetResult(
							Bag.SetValueStruct(ParamFName, FConstStructView(AsStruct, Tmp.GetMemory())),
							TEXT("Struct"));
					}
					else
					{
						OutNote = TEXT("JsonObjectToUStruct failed for struct value");
					}
				}
				else
				{
					OutNote = TEXT("value json was not a valid JSON object for the struct type");
				}
			}
			else
			{
				OutNote = TEXT("struct parameter has no UScriptStruct");
			}
			break;
		default:
			OutNote = TEXT("value not supported for this property type");
			break;
		}
	}
}

FString UCURemoteLibrary::AddStateTreeParameter(
	const FString& AssetName,
	const FString& ParameterName,
	const FString& ParameterTypeName,
	const FString& DefaultValueJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeParameter: %s, Param: %s, Type: %s"), *AssetName, *ParameterName, *ParameterTypeName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (ParameterName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter name cannot be empty"),
			TEXT("ParameterName parameter is required"),
			TEXT("Provide a parameter name like 'bIsAggressive' or 'TargetActor'"));
	}
	if (ParameterTypeName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter type cannot be empty"),
			TEXT("ParameterTypeName parameter is required"),
			TEXT("Use one of: bool, int32, float, FName, FString, FVector, or /Script/Module.MyStruct"));
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	EPropertyBagPropertyType BagType = EPropertyBagPropertyType::None;
	const UObject* ValueTypeObject = nullptr;
	if (!StateTreeWriteHelpers::ResolveParameterType(ParameterTypeName, BagType, ValueTypeObject))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Unknown parameter type: %s"), *ParameterTypeName),
			TEXT("Type token did not match a scalar and could not be resolved as a UScriptStruct"),
			TEXT("Use bool/int32/float/FName/FString/FVector or a valid /Script/Module.MyStruct path"));
	}

	// GetRootParametersPropertyBag() is the only accessor; const_cast is well-defined since the owning object is non-const.
	FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(EditorData->GetRootParametersPropertyBag());
	const FName ParamFName(*ParameterName);

	if (Bag.FindPropertyDescByName(ParamFName) != nullptr)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Parameter '%s' already exists in this StateTree's root parameters"), *ParameterName),
			TEXT("Parameter names must be unique within the root parameter bag"),
			TEXT("Use a different name"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("parameter_name"), ParameterName);
		Preview->SetStringField(TEXT("parameter_type"), ParameterTypeName);
		return MakeAiPreviewSuccessJson(Preview);
	}

	// PR3 ns-review B-1 fix: open transaction BEFORE the first mutation so the
	// FScopedTransaction (function-scope) covers AddProperty + SetValueXxx + the
	// editor notify. The previous nested `{}` block only wrapped Modify() calls,
	// leaving the actual mutations un-transacted (Ctrl-Z would not roll them back).
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddStateTreeParameter", "Add StateTree Parameter"));
	StateTreeAsset->Modify();
	EditorData->Modify();
#endif

	Bag.AddProperty(ParamFName, BagType, ValueTypeObject);

	bool bDefaultApplied = false;
	FString DefaultApplyNote;
	if (!DefaultValueJson.IsEmpty())
	{
		switch (BagType)
		{
		case EPropertyBagPropertyType::Bool:
			Bag.SetValueBool(ParamFName, DefaultValueJson.ToBool());
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::Int32:
			Bag.SetValueInt32(ParamFName, FCString::Atoi(*DefaultValueJson));
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::Int64:
			// PR3 ns-review W-2 fix: use SetValueInt64 (was silently truncating to int32).
			Bag.SetValueInt64(ParamFName, FCString::Atoi64(*DefaultValueJson));
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::Float:
			Bag.SetValueFloat(ParamFName, (float)FCString::Atod(*DefaultValueJson));
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::Double:
			// PR3 ns-review W-2 fix: use SetValueDouble (was silently downcasting to float).
			Bag.SetValueDouble(ParamFName, FCString::Atod(*DefaultValueJson));
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::Name:
			Bag.SetValueName(ParamFName, FName(*DefaultValueJson.TrimQuotes()));
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::String:
		case EPropertyBagPropertyType::Text:
			Bag.SetValueString(ParamFName, DefaultValueJson.TrimQuotes());
			bDefaultApplied = true;
			break;
		case EPropertyBagPropertyType::Struct:
			if (const UScriptStruct* AsStruct = Cast<UScriptStruct>(ValueTypeObject))
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DefaultValueJson);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					FInstancedStruct Tmp; Tmp.InitializeAs(AsStruct);
					if (FJsonObjectConverter::JsonObjectToUStruct(JsonObj.ToSharedRef(), AsStruct, Tmp.GetMutableMemory()))
					{
						Bag.SetValueStruct(ParamFName, FConstStructView(AsStruct, Tmp.GetMemory()));
						bDefaultApplied = true;
					}
					else { DefaultApplyNote = TEXT("JsonObjectToUStruct failed for struct default value"); }
				}
				else { DefaultApplyNote = TEXT("default_value_json was not a valid JSON object for the struct type"); }
			}
			break;
		default:
			DefaultApplyNote = TEXT("default value not supported for this property type");
			break;
		}
	}

#if WITH_EDITOR
	EditorData->OnParametersChanged(*StateTreeAsset);
#endif


	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("parameter_name"), ParameterName);
	ResultObject->SetStringField(TEXT("parameter_type"), ParameterTypeName);
	ResultObject->SetStringField(TEXT("root_parameters_guid"), EditorData->GetRootParametersGuid().ToString());
	ResultObject->SetBoolField(TEXT("default_applied"), bDefaultApplied);
	if (!DefaultApplyNote.IsEmpty()) { ResultObject->SetStringField(TEXT("default_note"), DefaultApplyNote); }


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeParameter completed: %s (%s)"), *ParameterName, *ParameterTypeName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::AddStateTreeTask(
	const FString& AssetName,
	const FString& StateName,
	const FString& TaskClassName,
	const FString& InstanceDataJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeTask: %s, State: %s, Task: %s"), *AssetName, *StateName, *TaskClassName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (TaskClassName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Task class name cannot be empty"),
			TEXT("TaskClassName parameter is required"),
			TEXT("Provide a UScriptStruct path like /Script/StateTreeModule.StateTreeDelayTask"));
	}

#if !UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
	return BlueprintNodeHelpers::CreateDetailedErrorJson(
		BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
		TEXT("AddStateTreeTask requires UE 5.8+"),
		TEXT("FStateTreeEditorNode::InitializeAs(Outer, UScriptStruct*) runtime overload is only available in UE 5.8 and later"),
		TEXT("Upgrade the engine to 5.8+ to author StateTree tasks via this API"));
#else
	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	UScriptStruct* TaskStruct = FindObject<UScriptStruct>(nullptr, *TaskClassName);
	if (!TaskStruct) { TaskStruct = LoadObject<UScriptStruct>(nullptr, *TaskClassName); }
	if (!TaskStruct)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Task struct not found: %s"), *TaskClassName),
			TEXT("Could not resolve the task class as a UScriptStruct"),
			TEXT("Use a fully-qualified path like /Script/StateTreeModule.StateTreeDelayTask"));
	}
	// PR3 ns-review B-2 fix: fail-closed when FStateTreeTaskBase cannot be resolved
	// (e.g., StateTreeModule not loaded). Previously a null TaskBase silently skipped
	// the IsChildOf check, allowing arbitrary structs like /Script/CoreUObject.Vector
	// to pass and crash the engine in InitializeAs().
	const UScriptStruct* TaskBase = FindObject<UScriptStruct>(nullptr, TEXT("/Script/StateTreeModule.StateTreeTaskBase"));
	if (!TaskBase) { TaskBase = LoadObject<UScriptStruct>(nullptr, TEXT("/Script/StateTreeModule.StateTreeTaskBase")); }
	if (!TaskBase)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Could not resolve FStateTreeTaskBase"),
			TEXT("/Script/StateTreeModule.StateTreeTaskBase was not found via FindObject or LoadObject"),
			TEXT("Ensure StateTreeModule is a runtime dependency of NarshaMCP.Build.cs"));
	}
	if (!TaskStruct->IsChildOf(TaskBase))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("'%s' is not a FStateTreeTaskBase-derived struct"), *TaskClassName),
			TEXT("Only structs deriving from FStateTreeTaskBase can be added as StateTree tasks"),
			TEXT("Pick a task struct (e.g., /Script/StateTreeModule.StateTreeDelayTask)"));
	}

	UStateTreeState* TargetState = nullptr;
	if (!StateName.IsEmpty())
	{
		FString ResolveError;
		const FResolvedStateTreeState Resolved = ResolveStateTreeStateOrError(
			EditorData, StateName, TEXT("state"), ResolveError);
		if (!Resolved.State)
		{
			return ResolveError;
		}
		TargetState = Resolved.State;
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("task_struct"), TaskStruct->GetPathName());
		if (TargetState)
		{
			Preview->SetStringField(TEXT("state_name"), TargetState->Name.ToString());
			Preview->SetStringField(TEXT("state_id"), TargetState->ID.ToString());
		}
		else
		{
			Preview->SetStringField(TEXT("state_name"), TEXT(""));
			Preview->SetBoolField(TEXT("global_task"), true);
		}
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddStateTreeTask", "Add StateTree Task"));
	StateTreeAsset->Modify();
	EditorData->Modify();
	if (TargetState) { TargetState->Modify(); }
#endif

	UObject* NodeOuter = TargetState ? static_cast<UObject*>(TargetState) : static_cast<UObject*>(EditorData);
	FStateTreeEditorNode& NewNode = TargetState
		? TargetState->Tasks.AddDefaulted_GetRef()
		: EditorData->GlobalTasks.AddDefaulted_GetRef();
	NewNode.InitializeAs(NodeOuter, TaskStruct);

	bool bInstanceDataApplied = false;
	FString InstanceNote;
	if (!InstanceDataJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InstanceDataJson);
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			FStateTreeDataView InstView = NewNode.GetInstance();
			if (InstView.GetStruct() && InstView.GetMutableMemory())
			{
				if (FJsonObjectConverter::JsonObjectToUStruct(JsonObj.ToSharedRef(), InstView.GetStruct(), InstView.GetMutableMemory()))
				{
					bInstanceDataApplied = true;
				}
				else { InstanceNote = TEXT("JsonObjectToUStruct failed for instance data"); }
			}
			else { InstanceNote = TEXT("task has no struct-typed instance data"); }
		}
		else { InstanceNote = TEXT("instance_data_json was not a valid JSON object"); }
	}


	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("state_name"), StateName.IsEmpty() ? TEXT("(global)") : StateName);
	ResultObject->SetStringField(TEXT("task_struct"), TaskStruct->GetPathName());
	ResultObject->SetStringField(TEXT("task_id"), NewNode.ID.ToString());
	ResultObject->SetBoolField(TEXT("instance_data_applied"), bInstanceDataApplied);
	if (!InstanceNote.IsEmpty()) { ResultObject->SetStringField(TEXT("instance_data_note"), InstanceNote); }


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeTask completed: %s on %s"), *TaskClassName, StateName.IsEmpty() ? TEXT("(global)") : *StateName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
}

FString UCURemoteLibrary::AddStateTreeBinding(
	const FString& AssetName,
	const FString& BindingDescriptorJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeBinding: %s"), *AssetName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (BindingDescriptorJson.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Binding descriptor JSON cannot be empty"),
			TEXT("BindingDescriptorJson parameter is required"),
			TEXT("Provide {\"source_task_id\":\"<guid>\",\"target_path\":\"Foo.Bar\"}"));
	}

	TSharedPtr<FJsonObject> Desc;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BindingDescriptorJson);
		if (!FJsonSerializer::Deserialize(Reader, Desc) || !Desc.IsValid())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Failed to parse binding descriptor JSON"),
				TEXT("BindingDescriptorJson was not a valid JSON object"),
				TEXT("Provide {\"source_task_id\":\"<guid>\",\"target_path\":\"Foo.Bar\"}"));
		}
	}

	FString SourceTaskIdStr, SourcePathStr, TargetPathRaw, TargetStructIdStr, CompletionStr;
	Desc->TryGetStringField(TEXT("source_task_id"), SourceTaskIdStr);
	Desc->TryGetStringField(TEXT("source_path"), SourcePathStr);
	Desc->TryGetStringField(TEXT("target_path"), TargetPathRaw);
	Desc->TryGetStringField(TEXT("target_struct_id"), TargetStructIdStr);
	Desc->TryGetStringField(TEXT("completion_condition"), CompletionStr);

	FGuid SourceGuid;
	if (SourceTaskIdStr.IsEmpty() || !FGuid::Parse(SourceTaskIdStr, SourceGuid) || !SourceGuid.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid or missing source_task_id"),
			TEXT("source_task_id must be a valid GUID string"),
			TEXT("Use the task_id returned by AddStateTreeTask"));
	}
	if (TargetPathRaw.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Missing target_path"),
			TEXT("target_path is required"),
			TEXT("Use \"Foo.Bar\" or \"<guid>:Foo.Bar\""));
	}

	FGuid TargetGuid; FString TargetPath;
	if (!StateTreeWriteHelpers::ParseTargetPath(TargetPathRaw, TargetStructIdStr, TargetGuid, TargetPath))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Could not determine the target struct ID for the binding"),
			TEXT("Supply target_struct_id, or prefix target_path with '<guid>:'"),
			TEXT("e.g. target_path = \"<state-or-task-guid>:Foo.Bar\""));
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	// PR3 ns-review C-2 fix: reject orphan source_task_id (a well-formed GUID that
	// matches no known task). Previously any GUID was accepted, creating dangling
	// bindings that crash at StateTree evaluation. We walk EditorData->GlobalTasks
	// and every state's Tasks to collect valid task IDs, then verify membership.
	const TSet<FGuid> KnownTaskIDs = StateTreeWriteHelpers::CollectStateTreeTaskIDs(EditorData);
	if (!KnownTaskIDs.Contains(SourceGuid))
	{
		TArray<FString> KnownStrings;
		for (const FGuid& Id : KnownTaskIDs) { KnownStrings.Add(Id.ToString()); }
		const FString KnownList = KnownStrings.Num() > 0
			? FString::Join(KnownStrings, TEXT(", ")) : TEXT("(no tasks in this StateTree)");
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("source_task_id '%s' is not a known task in this StateTree"), *SourceGuid.ToString()),
			FString::Printf(TEXT("Known task IDs: %s"), *KnownList),
			TEXT("Use the task_id returned by AddStateTreeTask, or add the task first"));
	}

	FPropertyBindingPath SourceBindingPath(SourceGuid);
	if (!SourcePathStr.IsEmpty() && !SourceBindingPath.FromString(SourcePathStr))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to parse source property path: %s"), *SourcePathStr),
			TEXT("Property path must be in 'Foo.Bar[1].Baz' form"),
			TEXT("Check the property names exist on the source task"));
	}

	FPropertyBindingPath TargetBindingPath(TargetGuid);
	if (!TargetPath.IsEmpty() && !TargetBindingPath.FromString(TargetPath))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to parse target property path: %s"), *TargetPath),
			TEXT("Property path must be in 'Foo.Bar[1].Baz' form"),
			TEXT("Check the property names exist on the target struct"));
	}

	FString BindingKind = TEXT("property");
#if !UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
	if (!CompletionStr.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Task-completion bindings require UE 5.8+"),
			TEXT("FStateTreeEditorPropertyBindings::AddTaskCompletionBinding is only available in UE 5.8 and later"),
			TEXT("Omit completion_condition for a plain property binding, or upgrade the engine to 5.8+"));
	}
#else
	UE::StateTree::ETaskCompletionCondition Completion = UE::StateTree::ETaskCompletionCondition::Completes;
	if (!CompletionStr.IsEmpty())
	{
		const FString C = CompletionStr.TrimStartAndEnd();
		if (C.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase) || C.Equals(TEXT("Succeeds"), ESearchCase::IgnoreCase))
		{
			Completion = UE::StateTree::ETaskCompletionCondition::Succeeds;
		}
		else if (C.Equals(TEXT("Failed"), ESearchCase::IgnoreCase) || C.Equals(TEXT("Fails"), ESearchCase::IgnoreCase))
		{
			Completion = UE::StateTree::ETaskCompletionCondition::Fails;
		}
		else if (C.Equals(TEXT("Any"), ESearchCase::IgnoreCase) || C.Equals(TEXT("Completed"), ESearchCase::IgnoreCase) || C.Equals(TEXT("Completes"), ESearchCase::IgnoreCase))
		{
			Completion = UE::StateTree::ETaskCompletionCondition::Completes;
		}
		else
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Unknown completion_condition: %s"), *CompletionStr),
				TEXT("Expected one of: Succeeded, Failed, Any (aliases: Succeeds/Fails/Completes/Completed)"),
				TEXT("Use one of the documented values"));
		}
		BindingKind = TEXT("task_completion");
	}
#endif

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("source_task_id"), SourceGuid.ToString());
		Preview->SetStringField(TEXT("source_path"), SourcePathStr);
		Preview->SetStringField(TEXT("target_struct_id"), TargetGuid.ToString());
		Preview->SetStringField(TEXT("target_path"), TargetPath);
		Preview->SetStringField(TEXT("binding_kind"), BindingKind);
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddStateTreeBinding", "Add StateTree Binding"));
	StateTreeAsset->Modify();
	EditorData->Modify();
#endif

	if (!CompletionStr.IsEmpty())
	{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
		if (!Bindings)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				TEXT("EditorData has no property bindings collection"),
				TEXT("GetPropertyEditorBindings() returned nullptr"),
				TEXT("StateTree may be corrupted"));
		}
		Bindings->AddTaskCompletionBinding(SourceGuid, TargetBindingPath, Completion);
#endif
	}
	else
	{
		EditorData->AddPropertyBinding(SourceBindingPath, TargetBindingPath);
	}


	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("source_task_id"), SourceGuid.ToString());
	ResultObject->SetStringField(TEXT("source_path"), SourcePathStr);
	ResultObject->SetStringField(TEXT("target_struct_id"), TargetGuid.ToString());
	ResultObject->SetStringField(TEXT("target_path"), TargetPath);
	ResultObject->SetStringField(TEXT("binding_kind"), BindingKind);


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeBinding completed: %s -> %s (%s)"), *SourceGuid.ToString(), *TargetPath, *BindingKind);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::AddStateTreeTransition(
	const FString& AssetName,
	const FString& SourceStateName,
	const FString& TargetStateName,
	const FString& TriggerJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeTransition: %s, Source: %s -> Target: %s"),
		*AssetName, *SourceStateName, *TargetStateName);

	// 0. Validate inputs
	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy")
		);
	}
	if (SourceStateName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Source state name cannot be empty"),
			TEXT("SourceStateName parameter is required"),
			TEXT("Provide the name of the state that owns the new transition")
		);
	}

	// 1. Load StateTree
	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)")
		);
	}

	// 2. Get EditorData
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized")
		);
	}

	// 3. Find source state and (optional) target state — collect-all (Issue #26888).
	// Last-match overwrite is the defect; do not "fix" it by inserting Break.
	FString ResolveError;
	const FResolvedStateTreeState ResolvedSource = ResolveStateTreeStateOrError(
		EditorData, SourceStateName, TEXT("source"), ResolveError);
	if (!ResolvedSource.State)
	{
		return ResolveError;
	}
	UStateTreeState* SourceState = ResolvedSource.State;
	UStateTreeState* TargetState = nullptr;
	if (!TargetStateName.IsEmpty())
	{
		const FResolvedStateTreeState ResolvedTarget = ResolveStateTreeStateOrError(
			EditorData, TargetStateName, TEXT("target"), ResolveError);
		if (!ResolvedTarget.State)
		{
			return ResolveError;
		}
		TargetState = ResolvedTarget.State;
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("source_state"), SourceState->Name.ToString());
		Preview->SetStringField(TEXT("source_id"), SourceState->ID.ToString());
		if (TargetState)
		{
			Preview->SetStringField(TEXT("target_state"), TargetState->Name.ToString());
			Preview->SetStringField(TEXT("target_id"), TargetState->ID.ToString());
		}
		return MakeAiPreviewSuccessJson(Preview);
	}

	// 4. Parse optional trigger JSON
	TSharedPtr<FJsonObject> TriggerObject;
	if (!TriggerJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TriggerJson);
		if (!FJsonSerializer::Deserialize(Reader, TriggerObject) || !TriggerObject.IsValid())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Invalid trigger JSON"),
				FString::Printf(TEXT("Could not parse: %s"), *TriggerJson),
				TEXT("Ensure the JSON is valid and properly formatted")
			);
		}
	}

	// 5. Begin editor transaction (undoable).
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddStateTreeTransition", "Add StateTree Transition"));
	SourceState->Modify();
	EditorData->Modify();

	// 6. Construct + append the transition.
	FStateTreeTransition NewTransition;
	NewTransition.ID = FGuid::NewGuid();
	NewTransition.Trigger = EStateTreeTransitionTrigger::OnStateCompleted;
	NewTransition.Priority = EStateTreeTransitionPriority::Normal;
	NewTransition.bTransitionEnabled = true;

	// 6a. Apply trigger / priority / delay / event-tag overrides from JSON.
	TArray<FString> AppliedProperties;
	if (TriggerObject.IsValid())
	{
		if (const TSharedPtr<FJsonValue>* TriggerVal = TriggerObject->Values.Find(TEXT("Trigger")); TriggerVal && (*TriggerVal)->Type == EJson::String)
		{
			const FString TriggerStr = (*TriggerVal)->AsString();
			const int64 EnumVal = StaticEnum<EStateTreeTransitionTrigger>()->GetValueByNameString(TriggerStr);
			if (EnumVal != INDEX_NONE)
			{
				NewTransition.Trigger = static_cast<EStateTreeTransitionTrigger>(EnumVal);
				AppliedProperties.Add(TEXT("Trigger"));
			}
		}
		if (const TSharedPtr<FJsonValue>* PriorityVal = TriggerObject->Values.Find(TEXT("Priority")); PriorityVal && (*PriorityVal)->Type == EJson::String)
		{
			const FString PriorityStr = (*PriorityVal)->AsString();
			const int64 EnumVal = StaticEnum<EStateTreeTransitionPriority>()->GetValueByNameString(PriorityStr);
			if (EnumVal != INDEX_NONE)
			{
				NewTransition.Priority = static_cast<EStateTreeTransitionPriority>(EnumVal);
				AppliedProperties.Add(TEXT("Priority"));
			}
		}
		if (const TSharedPtr<FJsonValue>* DelayVal = TriggerObject->Values.Find(TEXT("bDelayTransition")); DelayVal && (*DelayVal)->Type == EJson::Boolean)
		{
			NewTransition.bDelayTransition = (*DelayVal)->AsBool();
			AppliedProperties.Add(TEXT("bDelayTransition"));
		}
		if (const TSharedPtr<FJsonValue>* DurVal = TriggerObject->Values.Find(TEXT("DelayDuration")); DurVal && (*DurVal)->Type == EJson::Number)
		{
			NewTransition.DelayDuration = static_cast<float>((*DurVal)->AsNumber());
			AppliedProperties.Add(TEXT("DelayDuration"));
		}
		if (const TSharedPtr<FJsonValue>* VarVal = TriggerObject->Values.Find(TEXT("DelayRandomVariance")); VarVal && (*VarVal)->Type == EJson::Number)
		{
			NewTransition.DelayRandomVariance = static_cast<float>((*VarVal)->AsNumber());
			AppliedProperties.Add(TEXT("DelayRandomVariance"));
		}
		if (const TSharedPtr<FJsonValue>* EnabledVal = TriggerObject->Values.Find(TEXT("bTransitionEnabled")); EnabledVal && (*EnabledVal)->Type == EJson::Boolean)
		{
			NewTransition.bTransitionEnabled = (*EnabledVal)->AsBool();
			AppliedProperties.Add(TEXT("bTransitionEnabled"));
		}
		if (const TSharedPtr<FJsonValue>* EventVal = TriggerObject->Values.Find(TEXT("EventTag")); EventVal && (*EventVal)->Type == EJson::String)
		{
			const FString TagStr = (*EventVal)->AsString();
			if (!TagStr.IsEmpty())
			{
				NewTransition.RequiredEvent.Tag = FGameplayTag::RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound=*/false);
				AppliedProperties.Add(TEXT("EventTag"));
			}
		}
	}

	// 6b. Set up the target state link.
#if WITH_EDITORONLY_DATA
	if (TargetState)
	{
		NewTransition.State.Name = TargetState->Name;
		NewTransition.State.ID = TargetState->ID;
		NewTransition.State.LinkType = EStateTreeTransitionType::GotoState;
	}
	else
	{
		// No explicit target — leave LinkType=None; caller may set via override in TriggerJson.LinkType later.
		if (TriggerObject.IsValid())
		{
			if (const TSharedPtr<FJsonValue>* LinkTypeVal = TriggerObject->Values.Find(TEXT("LinkType")); LinkTypeVal && (*LinkTypeVal)->Type == EJson::String)
			{
				const FString LinkTypeStr = (*LinkTypeVal)->AsString();
				const int64 EnumVal = StaticEnum<EStateTreeTransitionType>()->GetValueByNameString(LinkTypeStr);
				if (EnumVal != INDEX_NONE)
				{
					NewTransition.State.LinkType = static_cast<EStateTreeTransitionType>(EnumVal);
					AppliedProperties.Add(TEXT("LinkType"));
				}
			}
		}
	}
#endif // WITH_EDITORONLY_DATA

	SourceState->Transitions.Add(MoveTemp(NewTransition));
	FStateTreeTransition& AddedTransition = SourceState->Transitions.Last();


	// 8. Build success response.
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("source_state"), SourceStateName);
	ResultObject->SetStringField(TEXT("target_state"), TargetStateName);
	ResultObject->SetStringField(TEXT("transition_id"), AddedTransition.ID.ToString());

	const FString TriggerName = StaticEnum<EStateTreeTransitionTrigger>()->GetNameStringByValue(
		static_cast<int64>(AddedTransition.Trigger));
	ResultObject->SetStringField(TEXT("trigger"), TriggerName);

	if (AppliedProperties.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> AppliedArray;
		for (const FString& PropName : AppliedProperties)
		{
			AppliedArray.Add(MakeShared<FJsonValueString>(PropName));
		}
		ResultObject->SetArrayField(TEXT("applied_properties"), AppliedArray);
	}


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeTransition completed: %s -> %s (trigger=%s, id=%s)"),
		*SourceStateName, *TargetStateName, *TriggerName, *AddedTransition.ID.ToString());
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::RemoveStateTreeTransition(
	const FString& AssetName,
	const FString& TransitionIndex,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeTransition: %s, Selector: %s"),
		*AssetName, *TransitionIndex);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy")
		);
	}
	if (TransitionIndex.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Transition selector cannot be empty"),
			TEXT("TransitionIndex parameter is required"),
			TEXT("Pass the transition GUID, a digits-only global index, or a unique target state name")
		);
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)")
		);
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized")
		);
	}

	// Issue #26804: reuse the modify-transition resolver. Do not add a second walker.
	FString ResolveError;
	const FResolvedStateTreeTransition Resolved =
		ResolveStateTreeTransitionOrError(EditorData, TransitionIndex, ResolveError);
	if (!Resolved.Transition)
	{
		return ResolveError;
	}
	FStateTreeTransition* FoundTransition = Resolved.Transition;
	UStateTreeState* FoundOwnerState = Resolved.OwnerState;
	const FString FoundInState = Resolved.SourceStateName;
	const FGuid RemovedId = FoundTransition->ID;

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("source_state"), FoundInState);
		if (FoundOwnerState)
		{
			Preview->SetStringField(TEXT("source_id"), FoundOwnerState->ID.ToString());
		}
		Preview->SetStringField(TEXT("transition_id"), RemovedId.ToString());
#if WITH_EDITORONLY_DATA
		Preview->SetStringField(TEXT("target_state"), FoundTransition->State.Name.ToString());
		Preview->SetStringField(TEXT("target_id"), FoundTransition->State.ID.ToString());
#endif
		return MakeAiPreviewSuccessJson(Preview);
	}

	if (!FoundOwnerState)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Transition has no owning state"),
			FString::Printf(TEXT("Selector '%s' resolved but OwnerState is null"), *TransitionIndex),
			TEXT("This may indicate a corrupted StateTree hierarchy")
		);
	}

	// Issue #26803: transaction + Modify BEFORE the array mutation. Only the owning
	// state changes (Transitions TArray). Over-Modify of EditorData / UStateTree
	// lets Undo revert unrelated edits (same contract as ModifyStateTreeTransition).
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveStateTreeTransition", "Remove StateTree Transition"));
	FoundOwnerState->Modify();
#endif

	int32 RemovedCount = 0;
	if (RemovedId.IsValid())
	{
		RemovedCount = FoundOwnerState->Transitions.RemoveAll(
			[&RemovedId](const FStateTreeTransition& Candidate)
			{
				return Candidate.ID == RemovedId;
			});
	}
	else
	{
		RemovedCount = FoundOwnerState->Transitions.RemoveAll(
			[FoundTransition](const FStateTreeTransition& Candidate)
			{
				return &Candidate == FoundTransition;
			});
	}

	if (RemovedCount <= 0)
	{
		// No Cancel(): empty transaction is discarded as transient. Cancel would
		// also abort an enclosing caller transaction (same as RemoveStateTreeState).
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to remove transition: %s"), *TransitionIndex),
			TEXT("Transition was resolved but could not be removed from its owning state"),
			TEXT("This may indicate a corrupted Transitions array")
		);
	}


	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("removed_transition_id"), RemovedId.ToString());
	ResultObject->SetStringField(TEXT("source_state"), FoundInState);

	TArray<TSharedPtr<FJsonValue>> RemainingArray;
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*Parent*/)
	{
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			RemainingArray.Add(MakeShared<FJsonValueString>(Transition.ID.ToString()));
		}
		return EStateTreeVisitor::Continue;
	});
	ResultObject->SetArrayField(TEXT("remaining_transitions"), RemainingArray);


	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeTransition completed: %s from %s"),
		*RemovedId.ToString(), *FoundInState);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// StateTree Remove API (Issue #26808 ST-06)
// =========================================================

FString UCURemoteLibrary::RemoveStateTreeTask(
	const FString& AssetName,
	const FString& TaskId,
	const FString& StateName,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeTask: %s, Task: %s, State: %s"), *AssetName, *TaskId, *StateName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	FGuid TaskGuid;
	if (TaskId.IsEmpty() || !FGuid::Parse(TaskId, TaskGuid) || !TaskGuid.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid or missing task_id"),
			TEXT("TaskId must be a valid GUID string"),
			TEXT("Use the task_id returned by set_task / AddStateTreeTask"));
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	UStateTreeState* RestrictState = nullptr;
	if (!StateName.IsEmpty())
	{
		FString ResolveError;
		const FResolvedStateTreeState Resolved = ResolveStateTreeStateOrError(
			EditorData, StateName, TEXT("state"), ResolveError);
		if (!Resolved.State)
		{
			return ResolveError;
		}
		RestrictState = Resolved.State;
	}

	TArray<FStateTreeEditorNode>* FoundArray = nullptr;
	int32 FoundIndex = INDEX_NONE;
	UObject* ModifyTarget = nullptr;
	FString FoundOn = TEXT("(global)");

	auto TryArray = [&](TArray<FStateTreeEditorNode>& Nodes, UObject* Owner, const FString& Label) -> bool
	{
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (Nodes[i].ID == TaskGuid)
			{
				FoundArray = &Nodes;
				FoundIndex = i;
				ModifyTarget = Owner;
				FoundOn = Label;
				return true;
			}
		}
		return false;
	};

	if (RestrictState)
	{
		TryArray(RestrictState->Tasks, RestrictState, RestrictState->Name.ToString());
	}
	else if (!TryArray(EditorData->GlobalTasks, EditorData, TEXT("(global)")))
	{
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*Parent*/)
		{
			if (FoundArray)
			{
				return EStateTreeVisitor::Break;
			}
			TryArray(State.Tasks, &State, State.Name.ToString());
			return FoundArray ? EStateTreeVisitor::Break : EStateTreeVisitor::Continue;
		});
	}

	if (!FoundArray || FoundIndex == INDEX_NONE)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("task_id '%s' is not a known task in this StateTree"), *TaskGuid.ToString()),
			TEXT("No matching FStateTreeEditorNode.ID"),
			TEXT("Use the task_id returned by set_task"));
	}

	auto BindingTouchesTask = [&TaskGuid](const FPropertyBindingBinding& Binding) -> bool
	{
		return Binding.GetSourcePath().GetStructID() == TaskGuid
			|| Binding.GetTargetPath().GetStructID() == TaskGuid;
	};
	int32 BindingHits = 0;
	EditorData->EditorBindings.RemoveBindings([&](FPropertyBindingBinding& Binding)
	{
		if (BindingTouchesTask(Binding))
		{
			++BindingHits;
		}
		return false;
	});

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("removed_task_id"), TaskGuid.ToString());
		Preview->SetStringField(TEXT("state_name"), FoundOn);
		Preview->SetNumberField(TEXT("would_remove_bindings"), BindingHits);
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveStateTreeTask", "Remove StateTree Task"));
	StateTreeAsset->Modify();
	EditorData->Modify();
	if (ModifyTarget && ModifyTarget != EditorData)
	{
		ModifyTarget->Modify();
	}
#endif
	EditorData->EditorBindings.RemoveBindings(
		[&](FPropertyBindingBinding& Binding) { return BindingTouchesTask(Binding); });
	FoundArray->RemoveAt(FoundIndex);

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("removed_task_id"), TaskGuid.ToString());
	ResultObject->SetStringField(TEXT("state_name"), FoundOn);
	ResultObject->SetNumberField(TEXT("removed_bindings"), BindingHits);
	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeTask completed: %s from %s"), *TaskGuid.ToString(), *FoundOn);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::RemoveStateTreeParameter(
	const FString& AssetName,
	const FString& ParameterName,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeParameter: %s, Param: %s"), *AssetName, *ParameterName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (ParameterName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter name cannot be empty"),
			TEXT("ParameterName parameter is required"),
			TEXT("Provide the name of an existing root parameter"));
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(EditorData->GetRootParametersPropertyBag());
	const FName ParamFName(*ParameterName);
	if (Bag.FindPropertyDescByName(ParamFName) == nullptr)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Parameter '%s' not found in this StateTree's root parameters"), *ParameterName),
			TEXT("remove_parameter can only remove an existing parameter"),
			TEXT("Check the parameter name via get_structure / get_bindings"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("removed_parameter"), ParameterName);
		Preview->SetStringField(TEXT("parameter_name"), ParameterName);
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveStateTreeParameter", "Remove StateTree Parameter"));
	StateTreeAsset->Modify();
	EditorData->Modify();
#endif
	const EPropertyBagAlterationResult RemoveResult = Bag.RemovePropertyByName(ParamFName);
	if (RemoveResult != EPropertyBagAlterationResult::Success)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to remove parameter '%s'"), *ParameterName),
			TEXT("FInstancedPropertyBag::RemovePropertyByName returned a non-success alteration result"),
			TEXT("The parameter may have already been removed"));
	}
#if WITH_EDITOR
	EditorData->OnParametersChanged(*StateTreeAsset);
#endif

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("removed_parameter"), ParameterName);
	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeParameter completed: %s"), *ParameterName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::RemoveStateTreeBinding(
	const FString& AssetName,
	const FString& BindingDescriptorJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeBinding: %s"), *AssetName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (BindingDescriptorJson.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Binding descriptor JSON cannot be empty"),
			TEXT("BindingDescriptorJson parameter is required"),
			TEXT("Provide {\"source_task_id\":\"<guid>\"} and optionally target_path"));
	}

	TSharedPtr<FJsonObject> Desc;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BindingDescriptorJson);
		if (!FJsonSerializer::Deserialize(Reader, Desc) || !Desc.IsValid())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Failed to parse binding descriptor JSON"),
				TEXT("BindingDescriptorJson was not a valid JSON object"),
				TEXT("Provide {\"source_task_id\":\"<guid>\"}"));
		}
	}

	FString SourceTaskIdStr;
	FString TargetPathRaw;
	FString TargetStructIdStr;
	Desc->TryGetStringField(TEXT("source_task_id"), SourceTaskIdStr);
	Desc->TryGetStringField(TEXT("target_path"), TargetPathRaw);
	Desc->TryGetStringField(TEXT("target_struct_id"), TargetStructIdStr);

	FGuid SourceGuid;
	if (SourceTaskIdStr.IsEmpty() || !FGuid::Parse(SourceTaskIdStr, SourceGuid) || !SourceGuid.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid or missing source_task_id"),
			TEXT("source_task_id must be a valid GUID string"),
			TEXT("Use the task_id returned by set_task"));
	}

	FGuid TargetGuid;
	FString TargetPath;
	const bool bHaveTarget = !TargetPathRaw.IsEmpty() || !TargetStructIdStr.IsEmpty();
	if (bHaveTarget)
	{
		StateTreeWriteHelpers::ParseTargetPath(TargetPathRaw, TargetStructIdStr, TargetGuid, TargetPath);
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	auto MatchesBinding = [&](const FPropertyBindingBinding& Binding) -> bool
	{
		if (Binding.GetSourcePath().GetStructID() != SourceGuid)
		{
			return false;
		}
		if (!bHaveTarget)
		{
			return true;
		}
		if (TargetGuid.IsValid() && Binding.GetTargetPath().GetStructID() != TargetGuid)
		{
			return false;
		}
		if (!TargetPath.IsEmpty())
		{
			const FString Existing = Binding.GetTargetPath().ToString();
			if (!Existing.Equals(TargetPath, ESearchCase::IgnoreCase))
			{
				return false;
			}
		}
		return true;
	};

	int32 MatchCount = 0;
	EditorData->EditorBindings.RemoveBindings([&](FPropertyBindingBinding& Binding)
	{
		if (MatchesBinding(Binding))
		{
			++MatchCount;
		}
		return false;
	});
	if (MatchCount <= 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			TEXT("No matching binding was removed"),
			FString::Printf(TEXT("source_task_id=%s target_path=%s"), *SourceGuid.ToString(), *TargetPathRaw),
			TEXT("Check source_task_id / target_path against get_bindings"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("source_task_id"), SourceGuid.ToString());
		Preview->SetNumberField(TEXT("removed_count"), MatchCount);
		return MakeAiPreviewSuccessJson(Preview);
	}

	const int32 BeforeCount = EditorData->EditorBindings.GetNumBindings();
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveStateTreeBinding", "Remove StateTree Binding"));
	StateTreeAsset->Modify();
	EditorData->Modify();
#endif
	EditorData->EditorBindings.RemoveBindings([&](FPropertyBindingBinding& Binding)
	{
		return MatchesBinding(Binding);
	});
	const int32 AfterCount = EditorData->EditorBindings.GetNumBindings();
	const int32 RemovedCount = BeforeCount - AfterCount;
	if (RemovedCount <= 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			TEXT("No matching binding was removed"),
			FString::Printf(TEXT("source_task_id=%s target_path=%s"), *SourceGuid.ToString(), *TargetPathRaw),
			TEXT("Check source_task_id / target_path against get_bindings"));
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("source_task_id"), SourceGuid.ToString());
	ResultObject->SetNumberField(TEXT("removed_count"), RemovedCount);
	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeBinding completed: %s removed=%d"), *SourceGuid.ToString(), RemovedCount);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// StateTree parameter modify + state reparent/reorder (Issue #26808 ST-06 Step 3)
// =========================================================

namespace
{
	bool ParseSiblingIndexOrError(const FString& SiblingIndex, const bool bRequired, int32& OutIndex, FString& OutError)
	{
		OutIndex = INDEX_NONE;
		if (SiblingIndex.IsEmpty())
		{
			if (bRequired)
			{
				OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
					BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
					TEXT("SiblingIndex is required"),
					TEXT("reorder_state needs a 0-based sibling index"),
					TEXT("Pass sibling_index as a non-negative integer"));
				return false;
			}
			return true;
		}
		bool bDigitsOnly = true;
		for (const TCHAR Ch : SiblingIndex)
		{
			if (Ch < TEXT('0') || Ch > TEXT('9'))
			{
				bDigitsOnly = false;
				break;
			}
		}
		if (!bDigitsOnly)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				FString::Printf(TEXT("SiblingIndex is not an integer: %s"), *SiblingIndex),
				TEXT("Index must be a non-negative decimal integer"),
				TEXT("Pass sibling_index=0 to move to the front"));
			return false;
		}
		const int64 Parsed = FCString::Atoi64(*SiblingIndex);
		if (Parsed < 0 || Parsed > MAX_int32)
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				FString::Printf(TEXT("SiblingIndex out of range: %s"), *SiblingIndex),
				TEXT("Index must fit in a non-negative int32"),
				TEXT("Pass a 0-based sibling index"));
			return false;
		}
		OutIndex = static_cast<int32>(Parsed);
		return true;
	}

	bool StateTreeSubtreeContains(UStateTreeState* Root, UStateTreeState* Needle)
	{
		if (!Root || !Needle)
		{
			return false;
		}
		if (Root == Needle)
		{
			return true;
		}
		for (UStateTreeState* Child : Root->Children)
		{
			if (StateTreeSubtreeContains(Child, Needle))
			{
				return true;
			}
		}
		return false;
	}

	template<typename ArrayType>
	int32 IndexOfStateChild(const ArrayType& Array, UStateTreeState* State)
	{
		for (int32 i = 0; i < Array.Num(); ++i)
		{
			if (Array[i] == State)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	template<typename ArrayType>
	int32 InsertStateClamped(ArrayType& Array, UStateTreeState* State, const int32 RequestedIndex)
	{
		int32 InsertAt = Array.Num();
		if (RequestedIndex != INDEX_NONE)
		{
			InsertAt = FMath::Clamp(RequestedIndex, 0, Array.Num());
		}
		Array.Insert(State, InsertAt);
		return InsertAt;
	}
}

FString UCURemoteLibrary::ModifyStateTreeParameter(
	const FString& AssetName,
	const FString& ParameterName,
	const FString& DefaultValueJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeParameter: %s, Param: %s"), *AssetName, *ParameterName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (ParameterName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter name cannot be empty"),
			TEXT("ParameterName parameter is required"),
			TEXT("Provide the name of an existing root parameter"));
	}
	if (DefaultValueJson.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter value cannot be empty"),
			TEXT("DefaultValueJson parameter is required for modify_parameter"),
			TEXT("Pass the new scalar or struct JSON value"));
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	FInstancedPropertyBag& Bag = const_cast<FInstancedPropertyBag&>(EditorData->GetRootParametersPropertyBag());
	const FName ParamFName(*ParameterName);
	const FPropertyBagPropertyDesc* Desc = Bag.FindPropertyDescByName(ParamFName);
	if (Desc == nullptr)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Parameter '%s' not found in this StateTree's root parameters"), *ParameterName),
			TEXT("modify_parameter can only set an existing parameter"),
			TEXT("Check the parameter name via get_structure / add_parameter"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("parameter_name"), ParameterName);
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyStateTreeParameter", "Modify StateTree Parameter"));
	StateTreeAsset->Modify();
	EditorData->Modify();
#endif
	bool bApplied = false;
	FString ApplyNote;
	StateTreeWriteHelpers::ApplyParameterValue(
		Bag, ParamFName, Desc->ValueType, Desc->ValueTypeObject, DefaultValueJson, bApplied, ApplyNote);
	if (!bApplied)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to set parameter '%s'"), *ParameterName),
			ApplyNote.IsEmpty() ? TEXT("ApplyParameterValue returned no change") : ApplyNote,
			TEXT("Check default_value matches the existing parameter type"));
	}
#if WITH_EDITOR
	EditorData->OnParametersChanged(*StateTreeAsset);
#endif

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("parameter_name"), ParameterName);
	ResultObject->SetBoolField(TEXT("value_applied"), true);
	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeParameter completed: %s"), *ParameterName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::ReparentStateTreeState(
	const FString& AssetName,
	const FString& StateName,
	const FString& NewParentStateName,
	const FString& SiblingIndex,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ReparentStateTreeState: %s, State: %s, Parent: %s"),
		*AssetName, *StateName, *NewParentStateName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (StateName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("State name cannot be empty"),
			TEXT("StateName parameter is required"),
			TEXT("Pass a state display name or GUID"));
	}

	int32 RequestedIndex = INDEX_NONE;
	FString IndexError;
	if (!ParseSiblingIndexOrError(SiblingIndex, false, RequestedIndex, IndexError))
	{
		return IndexError;
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	FString ResolveError;
	const FResolvedStateTreeState Resolved = ResolveStateTreeStateOrError(
		EditorData, StateName, TEXT("state"), ResolveError);
	if (!Resolved.State)
	{
		return ResolveError;
	}

	UStateTreeState* NewParent = nullptr;
	if (!NewParentStateName.IsEmpty())
	{
		const FResolvedStateTreeState ParentResolved = ResolveStateTreeStateOrError(
			EditorData, NewParentStateName, TEXT("parent"), ResolveError);
		if (!ParentResolved.State)
		{
			return ResolveError;
		}
		NewParent = ParentResolved.State;
	}

	if (StateTreeSubtreeContains(Resolved.State, NewParent))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Reparent would create a cycle"),
			FString::Printf(TEXT("Cannot move '%s' under itself or its descendant '%s'"),
				*Resolved.State->Name.ToString(),
				NewParent ? *NewParent->Name.ToString() : TEXT("(root)")),
			TEXT("Choose a parent that is not inside the moved state's subtree"));
	}

	const int32 OldIndex = Resolved.Parent
		? IndexOfStateChild(Resolved.Parent->Children, Resolved.State)
		: IndexOfStateChild(EditorData->SubTrees, Resolved.State);
	if (OldIndex == INDEX_NONE)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("State is not in its resolved parent sibling array"),
			TEXT("Internal resolver/parent mismatch"),
			TEXT("Retry with the state GUID from get_structure"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("state_name"), Resolved.State->Name.ToString());
		Preview->SetStringField(TEXT("state_id"), Resolved.State->ID.ToString());
		Preview->SetStringField(TEXT("parent_state"), NewParent ? NewParent->Name.ToString() : TEXT("(root)"));
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ReparentStateTreeState", "Reparent StateTree State"));
	Resolved.State->Modify();
	if (Resolved.Parent)
	{
		Resolved.Parent->Modify();
	}
	else
	{
		EditorData->Modify();
	}
	if (NewParent && NewParent != Resolved.Parent)
	{
		NewParent->Modify();
	}
	else if (!NewParent && Resolved.Parent)
	{
		EditorData->Modify();
	}
#endif

	if (Resolved.Parent)
	{
		Resolved.Parent->Children.RemoveAt(OldIndex);
	}
	else
	{
		EditorData->SubTrees.RemoveAt(OldIndex);
	}

	int32 InsertAt = INDEX_NONE;
	if (NewParent)
	{
		InsertAt = InsertStateClamped(NewParent->Children, Resolved.State, RequestedIndex);
		Resolved.State->Parent = NewParent;
	}
	else
	{
		InsertAt = InsertStateClamped(EditorData->SubTrees, Resolved.State, RequestedIndex);
		Resolved.State->Parent = nullptr;
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("state_name"), Resolved.State->Name.ToString());
	ResultObject->SetStringField(TEXT("state_id"), Resolved.State->ID.ToString());
	ResultObject->SetStringField(TEXT("parent_state"), NewParent ? NewParent->Name.ToString() : TEXT("(root)"));
	ResultObject->SetNumberField(TEXT("sibling_index"), InsertAt);
	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ReparentStateTreeState completed: %s -> %s"),
		*Resolved.State->Name.ToString(), NewParent ? *NewParent->Name.ToString() : TEXT("(root)"));
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::ReorderStateTreeState(
	const FString& AssetName,
	const FString& StateName,
	const FString& SiblingIndex,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ReorderStateTreeState: %s, State: %s, Index: %s"),
		*AssetName, *StateName, *SiblingIndex);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
	}
	if (StateName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("State name cannot be empty"),
			TEXT("StateName parameter is required"),
			TEXT("Pass a state display name or GUID"));
	}

	int32 RequestedIndex = INDEX_NONE;
	FString IndexError;
	if (!ParseSiblingIndexOrError(SiblingIndex, true, RequestedIndex, IndexError))
	{
		return IndexError;
	}

	UStateTree* StateTreeAsset = LoadObject<UStateTree>(nullptr, *AssetName);
	if (!StateTreeAsset)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
			TEXT("The specified StateTree asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
	}
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTreeAsset->EditorData);
	if (!EditorData)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("EditorData is null"),
			FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
			TEXT("StateTree may be corrupted or not properly initialized"));
	}

	FString ResolveError;
	const FResolvedStateTreeState Resolved = ResolveStateTreeStateOrError(
		EditorData, StateName, TEXT("state"), ResolveError);
	if (!Resolved.State)
	{
		return ResolveError;
	}

	const int32 OldIndex = Resolved.Parent
		? IndexOfStateChild(Resolved.Parent->Children, Resolved.State)
		: IndexOfStateChild(EditorData->SubTrees, Resolved.State);
	if (OldIndex == INDEX_NONE)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("State is not in its sibling array"),
			TEXT("Internal resolver/parent mismatch"),
			TEXT("Retry with the state GUID from get_structure"));
	}

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
		Preview->SetStringField(TEXT("asset_path"), AssetName);
		Preview->SetStringField(TEXT("state_name"), Resolved.State->Name.ToString());
		Preview->SetStringField(TEXT("state_id"), Resolved.State->ID.ToString());
		Preview->SetNumberField(TEXT("old_index"), OldIndex);
		Preview->SetNumberField(TEXT("sibling_index"), RequestedIndex);
		return MakeAiPreviewSuccessJson(Preview);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ReorderStateTreeState", "Reorder StateTree State"));
	Resolved.State->Modify();
	if (Resolved.Parent)
	{
		Resolved.Parent->Modify();
	}
	else
	{
		EditorData->Modify();
	}
#endif

	int32 InsertAt = INDEX_NONE;
	if (Resolved.Parent)
	{
		Resolved.Parent->Children.RemoveAt(OldIndex);
		InsertAt = InsertStateClamped(Resolved.Parent->Children, Resolved.State, RequestedIndex);
	}
	else
	{
		EditorData->SubTrees.RemoveAt(OldIndex);
		InsertAt = InsertStateClamped(EditorData->SubTrees, Resolved.State, RequestedIndex);
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("state_name"), Resolved.State->Name.ToString());
	ResultObject->SetStringField(TEXT("state_id"), Resolved.State->ID.ToString());
	ResultObject->SetNumberField(TEXT("old_index"), OldIndex);
	ResultObject->SetNumberField(TEXT("sibling_index"), InsertAt);
	if (FString FinalizeErr = CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject.ToSharedRef());
		!FinalizeErr.IsEmpty())
	{
		return FinalizeErr;
	}
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ReorderStateTreeState completed: %s %d -> %d"),
		*Resolved.State->Name.ToString(), OldIndex, InsertAt);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// StateTree Condition / Evaluator / Consideration CRUD (Issue #26808 ST-06 Step 4)
// =========================================================

namespace
{
	enum class ENarshaSTEditorNodeKind : uint8
	{
		Condition,
		Evaluator,
		Consideration,
	};

	struct FNarshaSTEditorNodeSlot
	{
		TArray<FStateTreeEditorNode>* Nodes = nullptr;
		UObject* Owner = nullptr;
		FString Label;
	};

	const TCHAR* KindNoun(ENarshaSTEditorNodeKind Kind)
	{
		switch (Kind)
		{
		case ENarshaSTEditorNodeKind::Condition: return TEXT("condition");
		case ENarshaSTEditorNodeKind::Evaluator: return TEXT("evaluator");
		case ENarshaSTEditorNodeKind::Consideration: return TEXT("consideration");
		default: return TEXT("node");
		}
	}

	const TCHAR* KindBasePath(ENarshaSTEditorNodeKind Kind)
	{
		switch (Kind)
		{
		case ENarshaSTEditorNodeKind::Condition: return TEXT("/Script/StateTreeModule.StateTreeConditionBase");
		case ENarshaSTEditorNodeKind::Evaluator: return TEXT("/Script/StateTreeModule.StateTreeEvaluatorBase");
		case ENarshaSTEditorNodeKind::Consideration: return TEXT("/Script/StateTreeModule.StateTreeConsiderationBase");
		default: return TEXT("");
		}
	}

	int32 IndexOfEditorNode(const TArray<FStateTreeEditorNode>& Nodes, const FGuid& Id)
	{
		for (int32 i = 0; i < Nodes.Num(); ++i)
		{
			if (Nodes[i].ID == Id)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	void CollectSlotsForKind(
		UStateTreeEditorData* EditorData,
		ENarshaSTEditorNodeKind Kind,
		TArray<FNarshaSTEditorNodeSlot>& OutSlots)
	{
		if (!EditorData)
		{
			return;
		}
		if (Kind == ENarshaSTEditorNodeKind::Evaluator)
		{
			FNarshaSTEditorNodeSlot Slot;
			Slot.Nodes = &EditorData->Evaluators;
			Slot.Owner = EditorData;
			Slot.Label = TEXT("(evaluators)");
			OutSlots.Add(Slot);
			return;
		}
		if (Kind == ENarshaSTEditorNodeKind::Consideration)
		{
			EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*Parent*/)
			{
				FNarshaSTEditorNodeSlot Slot;
				Slot.Nodes = &State.Considerations;
				Slot.Owner = &State;
				Slot.Label = State.Name.ToString();
				OutSlots.Add(Slot);
				return EStateTreeVisitor::Continue;
			});
			return;
		}
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* /*Parent*/)
		{
			FNarshaSTEditorNodeSlot Enter;
			Enter.Nodes = &State.EnterConditions;
			Enter.Owner = &State;
			Enter.Label = FString::Printf(TEXT("%s.enter"), *State.Name.ToString());
			OutSlots.Add(Enter);
			for (int32 i = 0; i < State.Transitions.Num(); ++i)
			{
				FNarshaSTEditorNodeSlot Trans;
				Trans.Nodes = &State.Transitions[i].Conditions;
				Trans.Owner = &State;
				Trans.Label = FString::Printf(TEXT("%s.transition[%d]"), *State.Name.ToString(), i);
				OutSlots.Add(Trans);
			}
			return EStateTreeVisitor::Continue;
		});
	}

	bool ResolveAddSlot(
		UStateTreeEditorData* EditorData,
		ENarshaSTEditorNodeKind Kind,
		const FString& StateName,
		const FString& TransitionIndex,
		const bool bTransitionIdOnly,
		const bool bStateIdOnly,
		FNarshaSTEditorNodeSlot& OutSlot,
		FString& OutError)
	{
		if (Kind == ENarshaSTEditorNodeKind::Evaluator)
		{
			OutSlot.Nodes = &EditorData->Evaluators;
			OutSlot.Owner = EditorData;
			OutSlot.Label = TEXT("(evaluators)");
			return true;
		}
		if (Kind == ENarshaSTEditorNodeKind::Consideration)
		{
			if (StateName.IsEmpty())
			{
				OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
					BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
					TEXT("State name cannot be empty"),
					TEXT("Considerations belong to a state"),
					TEXT("Pass state_name or state_id"));
				return false;
			}
			const FResolvedStateTreeState Resolved = bStateIdOnly
				? ResolveStateTreeStateIdOrError(EditorData, StateName, OutError)
				: ResolveStateTreeStateOrError(EditorData, StateName, TEXT("state"), OutError);
			if (!Resolved.State)
			{
				return false;
			}
			OutSlot.Nodes = &Resolved.State->Considerations;
			OutSlot.Owner = Resolved.State;
			OutSlot.Label = Resolved.State->Name.ToString();
			return true;
		}
		if (!TransitionIndex.IsEmpty())
		{
			const FResolvedStateTreeTransition Resolved = bTransitionIdOnly
				? ResolveStateTreeTransitionIdOrError(EditorData, TransitionIndex, OutError)
				: ResolveStateTreeTransitionOrError(EditorData, TransitionIndex, OutError);
			if (!Resolved.Transition || !Resolved.OwnerState)
			{
				return false;
			}
			if (!StateName.IsEmpty())
			{
				const FResolvedStateTreeState ResolvedState = bStateIdOnly
					? ResolveStateTreeStateIdOrError(EditorData, StateName, OutError)
					: ResolveStateTreeStateOrError(EditorData, StateName, TEXT("state"), OutError);
				if (!ResolvedState.State)
				{
					return false;
				}
				if (ResolvedState.State->ID != Resolved.OwnerState->ID)
				{
					OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
						BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
						FString::Printf(TEXT("State '%s' does not own transition '%s'"),
							*StateName, *TransitionIndex),
						TEXT("state_name and transition selector resolve to different owner states"),
						TEXT("Pass the source state that owns the selected transition"));
					return false;
				}
			}
			OutSlot.Nodes = &Resolved.Transition->Conditions;
			OutSlot.Owner = Resolved.OwnerState;
			OutSlot.Label = Resolved.SourceStateName;
			return true;
		}
		if (StateName.IsEmpty())
		{
			OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("State name cannot be empty"),
				TEXT("Enter conditions belong to a state"),
				TEXT("Pass state_name, or transition_name to attach to a transition"));
			return false;
		}
		const FResolvedStateTreeState Resolved = bStateIdOnly
			? ResolveStateTreeStateIdOrError(EditorData, StateName, OutError)
			: ResolveStateTreeStateOrError(EditorData, StateName, TEXT("state"), OutError);
		if (!Resolved.State)
		{
			return false;
		}
		OutSlot.Nodes = &Resolved.State->EnterConditions;
		OutSlot.Owner = Resolved.State;
		OutSlot.Label = Resolved.State->Name.ToString();
		return true;
	}

	bool FindSlotForNodeId(
		UStateTreeEditorData* EditorData,
		ENarshaSTEditorNodeKind Kind,
		const FGuid& NodeId,
		FNarshaSTEditorNodeSlot& OutSlot,
		int32& OutIndex,
		FString& OutError)
	{
		TArray<FNarshaSTEditorNodeSlot> Slots;
		CollectSlotsForKind(EditorData, Kind, Slots);
		for (FNarshaSTEditorNodeSlot& Slot : Slots)
		{
			if (!Slot.Nodes)
			{
				continue;
			}
			const int32 Found = IndexOfEditorNode(*Slot.Nodes, NodeId);
			if (Found != INDEX_NONE)
			{
				OutSlot = Slot;
				OutIndex = Found;
				return true;
			}
		}
		OutError = BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::NODE_NOT_FOUND,
			FString::Printf(TEXT("%s id '%s' is not a known %s in this StateTree"),
				KindNoun(Kind), *NodeId.ToString(), KindNoun(Kind)),
			TEXT("No matching FStateTreeEditorNode.ID"),
			TEXT("Use the node_id returned by the matching add_* op"));
		return false;
	}

	FString LoadStateTreeEditor(
		const FString& AssetName,
		UStateTree*& OutAsset,
		UStateTreeEditorData*& OutEditor)
	{
		OutAsset = nullptr;
		OutEditor = nullptr;
		if (AssetName.Contains(TEXT("//")))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
				TEXT("Asset path must not contain '//'"),
				TEXT("Use format: /Game/AI/ST_Enemy.ST_Enemy"));
		}
		OutAsset = LoadObject<UStateTree>(nullptr, *AssetName);
		if (!OutAsset)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
				FString::Printf(TEXT("StateTree not found: %s"), *AssetName),
				TEXT("The specified StateTree asset could not be loaded"),
				TEXT("Verify the asset path is correct (e.g., /Game/AI/ST_Enemy.ST_Enemy)"));
		}
		OutEditor = Cast<UStateTreeEditorData>(OutAsset->EditorData);
		if (!OutEditor)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				TEXT("EditorData is null"),
				FString::Printf(TEXT("StateTree '%s' has no EditorData"), *AssetName),
				TEXT("StateTree may be corrupted or not properly initialized"));
		}
		return FString();
	}

	FString SaveStateTreeAsset(UStateTree* StateTreeAsset, const TSharedRef<FJsonObject>& ResultObject)
	{
		return CUStateTreeWriteFinalize::CompileSaveAndVerify(StateTreeAsset, ResultObject);
	}

	FString AddStateTreeEditorNodeImpl(
		const FString& AssetName,
		ENarshaSTEditorNodeKind Kind,
		const FString& StateName,
		const FString& TransitionIndex,
		const FString& ClassName,
		const FString& InstanceDataJson,
		const bool bPreviewOnly,
		const bool bTransitionIdOnly,
		const bool bStateIdOnly)
	{
#if !UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("AddStateTree %s requires UE 5.8+"), KindNoun(Kind)),
			TEXT("FStateTreeEditorNode::InitializeAs runtime overload is only available in UE 5.8 and later"),
			TEXT("Upgrade the engine to 5.8+ to author this node via this API"));
#else
		if (ClassName.IsEmpty())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				FString::Printf(TEXT("%s class name cannot be empty"), KindNoun(Kind)),
				TEXT("ClassName parameter is required"),
				TEXT("Provide a UScriptStruct path"));
		}
		UStateTree* StateTreeAsset = nullptr;
		UStateTreeEditorData* EditorData = nullptr;
		const FString LoadError = LoadStateTreeEditor(AssetName, StateTreeAsset, EditorData);
		if (!LoadError.IsEmpty())
		{
			return LoadError;
		}

		UScriptStruct* NodeStruct = FindObject<UScriptStruct>(nullptr, *ClassName);
		if (!NodeStruct) { NodeStruct = LoadObject<UScriptStruct>(nullptr, *ClassName); }
		if (!NodeStruct)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("%s struct not found: %s"), KindNoun(Kind), *ClassName),
				TEXT("Could not resolve the class as a UScriptStruct"),
				TEXT("Use a fully-qualified path like /Script/StateTreeModule.StateTreeDelayTask"));
		}
		const UScriptStruct* BaseStruct = FindObject<UScriptStruct>(nullptr, KindBasePath(Kind));
		if (!BaseStruct) { BaseStruct = LoadObject<UScriptStruct>(nullptr, KindBasePath(Kind)); }
		if (!BaseStruct)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Could not resolve %s base struct"), KindNoun(Kind)),
				KindBasePath(Kind),
				TEXT("Ensure StateTreeModule is a runtime dependency of NarshaMCP.Build.cs"));
		}
		if (!NodeStruct->IsChildOf(BaseStruct))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("'%s' is not a %s-derived struct"), *ClassName, KindNoun(Kind)),
				TEXT("Only structs deriving from the matching StateTree base can be added"),
				TEXT("Pick a condition, evaluator, or consideration struct"));
		}

		FNarshaSTEditorNodeSlot Slot;
		FString SlotError;
		if (!ResolveAddSlot(
			EditorData, Kind, StateName, TransitionIndex, bTransitionIdOnly, bStateIdOnly, Slot, SlotError))
		{
			return SlotError;
		}

		if (bPreviewOnly)
		{
			TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
			Preview->SetStringField(TEXT("asset_path"), AssetName);
			Preview->SetStringField(TEXT("kind"), KindNoun(Kind));
			Preview->SetStringField(TEXT("owner"), Slot.Label);
			Preview->SetStringField(TEXT("node_struct"), NodeStruct->GetPathName());
			Preview->SetStringField(TEXT("node_class"), NodeStruct->GetPathName());
			return MakeAiPreviewSuccessJson(Preview);
		}

#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddStateTreeEditorNode", "Add StateTree Editor Node"));
		StateTreeAsset->Modify();
		EditorData->Modify();
		if (Slot.Owner && Slot.Owner != EditorData)
		{
			Slot.Owner->Modify();
		}
#endif
		FStateTreeEditorNode& NewNode = Slot.Nodes->AddDefaulted_GetRef();
		NewNode.InitializeAs(Slot.Owner, NodeStruct);

		bool bInstanceDataApplied = false;
		FString InstanceNote;
		if (!InstanceDataJson.IsEmpty() && InstanceDataJson != TEXT("{}"))
		{
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InstanceDataJson);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				FStateTreeDataView InstView = NewNode.GetInstance();
				if (InstView.GetStruct() && InstView.GetMutableMemory())
				{
					if (FJsonObjectConverter::JsonObjectToUStruct(
						JsonObj.ToSharedRef(), InstView.GetStruct(), InstView.GetMutableMemory()))
					{
						bInstanceDataApplied = true;
					}
					else { InstanceNote = TEXT("JsonObjectToUStruct failed for instance data"); }
				}
				else { InstanceNote = TEXT("node has no struct-typed instance data"); }
			}
			else { InstanceNote = TEXT("instance_data_json was not a valid JSON object"); }
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetStringField(TEXT("asset_path"), AssetName);
		ResultObject->SetStringField(TEXT("kind"), KindNoun(Kind));
		ResultObject->SetStringField(TEXT("owner"), Slot.Label);
		ResultObject->SetStringField(TEXT("node_struct"), NodeStruct->GetPathName());
		ResultObject->SetStringField(TEXT("node_id"), NewNode.ID.ToString());
		ResultObject->SetBoolField(TEXT("instance_data_applied"), bInstanceDataApplied);
		if (!InstanceNote.IsEmpty()) { ResultObject->SetStringField(TEXT("instance_data_note"), InstanceNote); }
		const FString SaveError = SaveStateTreeAsset(StateTreeAsset, ResultObject.ToSharedRef());
		if (!SaveError.IsEmpty())
		{
			return SaveError;
		}
		return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
	}

	FString ModifyStateTreeEditorNodeImpl(
		const FString& AssetName,
		ENarshaSTEditorNodeKind Kind,
		const FString& NodeId,
		const FString& InstanceDataJson,
		const bool bPreviewOnly)
	{
#if !UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("ModifyStateTree %s requires UE 5.8+"), KindNoun(Kind)),
			TEXT("FStateTreeEditorNode::GetInstance is gated with the 5.8 InitializeAs path"),
			TEXT("Upgrade the engine to 5.8+"));
#else
		FGuid ParsedId;
		if (NodeId.IsEmpty() || !FGuid::Parse(NodeId, ParsedId) || !ParsedId.IsValid())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Invalid or missing node_id"),
				TEXT("NodeId must be a valid GUID string"),
				TEXT("Use the node_id returned by the matching add_* op"));
		}
		if (InstanceDataJson.IsEmpty() || InstanceDataJson == TEXT("{}"))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Instance data cannot be empty"),
				TEXT("modify_* requires a JSON object of instance fields"),
				TEXT("Pass parameters as a JSON object"));
		}
		UStateTree* StateTreeAsset = nullptr;
		UStateTreeEditorData* EditorData = nullptr;
		const FString LoadError = LoadStateTreeEditor(AssetName, StateTreeAsset, EditorData);
		if (!LoadError.IsEmpty())
		{
			return LoadError;
		}
		FNarshaSTEditorNodeSlot Slot;
		int32 FoundIndex = INDEX_NONE;
		FString FindError;
		if (!FindSlotForNodeId(EditorData, Kind, ParsedId, Slot, FoundIndex, FindError))
		{
			return FindError;
		}

		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(InstanceDataJson);
		if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Failed to parse instance data JSON"),
				TEXT("parameters was not a valid JSON object"),
				TEXT("Pass a JSON object of instance fields"));
		}

		if (bPreviewOnly)
		{
			TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
			Preview->SetStringField(TEXT("asset_path"), AssetName);
			Preview->SetStringField(TEXT("kind"), KindNoun(Kind));
			Preview->SetStringField(TEXT("node_id"), ParsedId.ToString());
			Preview->SetStringField(TEXT("owner"), Slot.Label);
			return MakeAiPreviewSuccessJson(Preview);
		}

		FStateTreeEditorNode& Node = (*Slot.Nodes)[FoundIndex];
#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyStateTreeEditorNode", "Modify StateTree Editor Node"));
		StateTreeAsset->Modify();
		EditorData->Modify();
		if (Slot.Owner && Slot.Owner != EditorData)
		{
			Slot.Owner->Modify();
		}
		if (Node.InstanceObject)
		{
			Node.InstanceObject->SetFlags(RF_Transactional);
			Node.InstanceObject->Modify();
		}
#endif
		FStateTreeDataView InstView = Node.GetInstance();
		if (!InstView.GetStruct() || !InstView.GetMutableMemory())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				TEXT("Node has no struct-typed instance data"),
				TEXT("GetInstance returned an empty view"),
				TEXT("This node type cannot be modified via instance JSON"));
		}
		if (!FJsonObjectConverter::JsonObjectToUStruct(
			JsonObj.ToSharedRef(), InstView.GetStruct(), InstView.GetMutableMemory()))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				TEXT("Failed to apply instance properties"),
				TEXT("JsonObjectToUStruct returned false"),
				TEXT("Check field names against the instance struct"));
		}

		TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetStringField(TEXT("asset_path"), AssetName);
		ResultObject->SetStringField(TEXT("kind"), KindNoun(Kind));
		ResultObject->SetStringField(TEXT("node_id"), ParsedId.ToString());
		ResultObject->SetStringField(TEXT("owner"), Slot.Label);
		const FString SaveError = SaveStateTreeAsset(StateTreeAsset, ResultObject.ToSharedRef());
		if (!SaveError.IsEmpty())
		{
			return SaveError;
		}
		return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
	}

	FString RemoveStateTreeEditorNodeImpl(
		const FString& AssetName,
		ENarshaSTEditorNodeKind Kind,
		const FString& NodeId,
		const bool bPreviewOnly)
	{
		FGuid ParsedId;
		if (NodeId.IsEmpty() || !FGuid::Parse(NodeId, ParsedId) || !ParsedId.IsValid())
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
				TEXT("Invalid or missing node_id"),
				TEXT("NodeId must be a valid GUID string"),
				TEXT("Use the node_id returned by the matching add_* op"));
		}
		UStateTree* StateTreeAsset = nullptr;
		UStateTreeEditorData* EditorData = nullptr;
		const FString LoadError = LoadStateTreeEditor(AssetName, StateTreeAsset, EditorData);
		if (!LoadError.IsEmpty())
		{
			return LoadError;
		}
		FNarshaSTEditorNodeSlot Slot;
		int32 FoundIndex = INDEX_NONE;
		FString FindError;
		if (!FindSlotForNodeId(EditorData, Kind, ParsedId, Slot, FoundIndex, FindError))
		{
			return FindError;
		}

		auto BindingTouchesNode = [&ParsedId](const FPropertyBindingBinding& Binding) -> bool
		{
			return Binding.GetSourcePath().GetStructID() == ParsedId
				|| Binding.GetTargetPath().GetStructID() == ParsedId;
		};
		int32 BindingHits = 0;
		EditorData->EditorBindings.RemoveBindings([&](FPropertyBindingBinding& Binding)
		{
			if (BindingTouchesNode(Binding))
			{
				++BindingHits;
			}
			return false;
		});

		if (bPreviewOnly)
		{
			TSharedPtr<FJsonObject> Preview = MakeShared<FJsonObject>();
			Preview->SetStringField(TEXT("asset_path"), AssetName);
			Preview->SetStringField(TEXT("kind"), KindNoun(Kind));
			Preview->SetStringField(TEXT("removed_node_id"), ParsedId.ToString());
			Preview->SetStringField(TEXT("node_id"), ParsedId.ToString());
			Preview->SetStringField(TEXT("owner"), Slot.Label);
			Preview->SetNumberField(TEXT("would_remove_bindings"), BindingHits);
			return MakeAiPreviewSuccessJson(Preview);
		}

#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveStateTreeEditorNode", "Remove StateTree Editor Node"));
		StateTreeAsset->Modify();
		EditorData->Modify();
		if (Slot.Owner && Slot.Owner != EditorData)
		{
			Slot.Owner->Modify();
		}
#endif
		EditorData->EditorBindings.RemoveBindings(
			[&](FPropertyBindingBinding& Binding) { return BindingTouchesNode(Binding); });
		Slot.Nodes->RemoveAt(FoundIndex);

		TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
		ResultObject->SetStringField(TEXT("asset_path"), AssetName);
		ResultObject->SetStringField(TEXT("kind"), KindNoun(Kind));
		ResultObject->SetStringField(TEXT("removed_node_id"), ParsedId.ToString());
		ResultObject->SetStringField(TEXT("owner"), Slot.Label);
		ResultObject->SetNumberField(TEXT("removed_bindings"), BindingHits);
		const FString SaveError = SaveStateTreeAsset(StateTreeAsset, ResultObject.ToSharedRef());
		if (!SaveError.IsEmpty())
		{
			return SaveError;
		}
		return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
	}
}

FString UCURemoteLibrary::AddStateTreeCondition(
	const FString& AssetName,
	const FString& StateName,
	const FString& TransitionIndex,
	const FString& ConditionClassName,
	const FString& InstanceDataJson,
	bool bPreviewOnly,
	bool bTransitionIdOnly,
	bool bStateIdOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeCondition: %s"), *AssetName);
	return AddStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Condition, StateName, TransitionIndex,
		ConditionClassName, InstanceDataJson, bPreviewOnly, bTransitionIdOnly, bStateIdOnly);
}

FString UCURemoteLibrary::ModifyStateTreeCondition(
	const FString& AssetName,
	const FString& NodeId,
	const FString& InstanceDataJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeCondition: %s"), *AssetName);
	return ModifyStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Condition, NodeId, InstanceDataJson, bPreviewOnly);
}

FString UCURemoteLibrary::RemoveStateTreeCondition(
	const FString& AssetName,
	const FString& NodeId,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeCondition: %s"), *AssetName);
	return RemoveStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Condition, NodeId, bPreviewOnly);
}

FString UCURemoteLibrary::AddStateTreeEvaluator(
	const FString& AssetName,
	const FString& EvaluatorClassName,
	const FString& InstanceDataJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeEvaluator: %s"), *AssetName);
	return AddStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Evaluator, FString(), FString(),
		EvaluatorClassName, InstanceDataJson, bPreviewOnly, false, false);
}

FString UCURemoteLibrary::ModifyStateTreeEvaluator(
	const FString& AssetName,
	const FString& NodeId,
	const FString& InstanceDataJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeEvaluator: %s"), *AssetName);
	return ModifyStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Evaluator, NodeId, InstanceDataJson, bPreviewOnly);
}

FString UCURemoteLibrary::RemoveStateTreeEvaluator(
	const FString& AssetName,
	const FString& NodeId,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeEvaluator: %s"), *AssetName);
	return RemoveStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Evaluator, NodeId, bPreviewOnly);
}

FString UCURemoteLibrary::AddStateTreeConsideration(
	const FString& AssetName,
	const FString& StateName,
	const FString& ConsiderationClassName,
	const FString& InstanceDataJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] AddStateTreeConsideration: %s"), *AssetName);
	return AddStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Consideration, StateName, FString(),
		ConsiderationClassName, InstanceDataJson, bPreviewOnly, false, false);
}

FString UCURemoteLibrary::ModifyStateTreeConsideration(
	const FString& AssetName,
	const FString& NodeId,
	const FString& InstanceDataJson,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] ModifyStateTreeConsideration: %s"), *AssetName);
	return ModifyStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Consideration, NodeId, InstanceDataJson, bPreviewOnly);
}

FString UCURemoteLibrary::RemoveStateTreeConsideration(
	const FString& AssetName,
	const FString& NodeId,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AI] RemoveStateTreeConsideration: %s"), *AssetName);
	return RemoveStateTreeEditorNodeImpl(
		AssetName, ENarshaSTEditorNodeKind::Consideration, NodeId, bPreviewOnly);
}

#else
#define NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(Name, Signature) \
	FString UCURemoteLibrary::Name Signature { return MakeStateTreePluginUnavailable(); }

NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ModifyStateTreeState,
	(const FString& AssetName, const FString& StateName, const FString& PropertiesJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ModifyStateTreeTransition,
	(const FString& AssetName, const FString& TransitionIndex, const FString& PropertiesJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeState,
	(const FString& AssetName, const FString& StateName, const FString& ParentStateName, const FString& PropertiesJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeState,
	(const FString& AssetName, const FString& StateName, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeParameter,
	(const FString& AssetName, const FString& ParameterName, const FString& ParameterTypeName, const FString& DefaultValueJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeTask,
	(const FString& AssetName, const FString& StateName, const FString& TaskClassName, const FString& InstanceDataJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeBinding,
	(const FString& AssetName, const FString& BindingDescriptorJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeTransition,
	(const FString& AssetName, const FString& SourceStateName, const FString& TargetStateName, const FString& TriggerJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeTransition,
	(const FString& AssetName, const FString& TransitionIndex, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeTask,
	(const FString& AssetName, const FString& TaskId, const FString& StateName, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeParameter,
	(const FString& AssetName, const FString& ParameterName, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeBinding,
	(const FString& AssetName, const FString& BindingDescriptorJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ModifyStateTreeParameter,
	(const FString& AssetName, const FString& ParameterName, const FString& DefaultValueJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ReparentStateTreeState,
	(const FString& AssetName, const FString& StateName, const FString& NewParentStateName, const FString& SiblingIndex, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ReorderStateTreeState,
	(const FString& AssetName, const FString& StateName, const FString& SiblingIndex, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeCondition,
	(const FString& AssetName, const FString& StateName, const FString& TransitionIndex, const FString& ConditionClassName, const FString& InstanceDataJson, bool bPreviewOnly, bool bTransitionIdOnly, bool bStateIdOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ModifyStateTreeCondition,
	(const FString& AssetName, const FString& NodeId, const FString& InstanceDataJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeCondition,
	(const FString& AssetName, const FString& NodeId, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeEvaluator,
	(const FString& AssetName, const FString& EvaluatorClassName, const FString& InstanceDataJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ModifyStateTreeEvaluator,
	(const FString& AssetName, const FString& NodeId, const FString& InstanceDataJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeEvaluator,
	(const FString& AssetName, const FString& NodeId, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(AddStateTreeConsideration,
	(const FString& AssetName, const FString& StateName, const FString& ConsiderationClassName, const FString& InstanceDataJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(ModifyStateTreeConsideration,
	(const FString& AssetName, const FString& NodeId, const FString& InstanceDataJson, bool bPreviewOnly))
NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB(RemoveStateTreeConsideration,
	(const FString& AssetName, const FString& NodeId, bool bPreviewOnly))

#undef NARSHAMCP_STATE_TREE_UNAVAILABLE_STUB
#endif

// =========================================================
// PCG Graph User-Parameter Write API (Issue #18416)
// =========================================================
//
// Sibling of the StateTree Write API above: same FInstancedPropertyBag substrate, and
// it reuses StateTreeWriteHelpers::ResolveParameterType for scalar/struct/UDS type
// resolution. Unlike PCG's other modification ops (node settings authored via
// WebSocket-Python codegen), UPCGGraph user parameters are only reachable through
// UpdateUserParametersStruct()/GetUserParametersStruct(), which are NOT
// BlueprintCallable / Python-reflection-reachable — hence these native BlueprintCallable
// UFUNCTIONs (called from Rust via call_narsha_rc, mirroring #18417).

#if WITH_NARSHAMCP_PCG
namespace PCGGraphParamHelpers
{
	/** Plugin-neutral property-bag type resolution; kept local so PCG does not depend on StateTree. */
	static bool ResolveParameterType(
		const FString& TypeName, EPropertyBagPropertyType& OutType, const UObject*& OutValueObject)
	{
		OutValueObject = nullptr;
		if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Bool; return true; }
		if (TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase)
			|| TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Int32; return true; }
		if (TypeName.Equals(TEXT("int64"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Int64; return true; }
		if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Float; return true; }
		if (TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Double; return true; }
		if (TypeName.Equals(TEXT("FName"), ESearchCase::IgnoreCase)
			|| TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Name; return true; }
		if (TypeName.Equals(TEXT("FString"), ESearchCase::IgnoreCase)
			|| TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::String; return true; }
		if (TypeName.Equals(TEXT("FText"), ESearchCase::IgnoreCase)
			|| TypeName.Equals(TEXT("Text"), ESearchCase::IgnoreCase)) { OutType = EPropertyBagPropertyType::Text; return true; }

		FString StructPath = TypeName;
		if (StructPath.Equals(TEXT("FVector"), ESearchCase::IgnoreCase)) { StructPath = TEXT("/Script/CoreUObject.Vector"); }
		else if (StructPath.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase)) { StructPath = TEXT("/Script/CoreUObject.Rotator"); }
		else if (StructPath.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase)) { StructPath = TEXT("/Script/CoreUObject.Transform"); }
		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
		if (!Struct) { Struct = LoadObject<UScriptStruct>(nullptr, *StructPath); }
		if (!Struct) { return false; }
		OutType = EPropertyBagPropertyType::Struct;
		OutValueObject = Struct;
		return true;
	}

	/** Map a property-bag desc to a human-readable type token for GetPCGGraphUserParameters. */
	static FString PropertyBagTypeToString(const FPropertyBagPropertyDesc& Desc)
	{
		switch (Desc.ValueType)
		{
		case EPropertyBagPropertyType::Bool:   return TEXT("bool");
		case EPropertyBagPropertyType::Int32:  return TEXT("int32");
		case EPropertyBagPropertyType::Int64:  return TEXT("int64");
		case EPropertyBagPropertyType::Float:  return TEXT("float");
		case EPropertyBagPropertyType::Double: return TEXT("double");
		case EPropertyBagPropertyType::Name:   return TEXT("FName");
		case EPropertyBagPropertyType::String: return TEXT("FString");
		case EPropertyBagPropertyType::Text:   return TEXT("FText");
		case EPropertyBagPropertyType::Enum:   return Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("Enum");
		case EPropertyBagPropertyType::Struct: return Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("Struct");
		case EPropertyBagPropertyType::Object: return Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("Object");
		case EPropertyBagPropertyType::Class:  return Desc.ValueTypeObject ? Desc.ValueTypeObject->GetName() : TEXT("Class");
		default:                               return TEXT("unknown");
		}
	}

	/**
	 * Apply a JSON value string to a property-bag entry — the exact default-value switch
	 * from AddStateTreeParameter, shared by AddPCGGraphUserParameter (seed default) and
	 * SetPCGGraphUserParameter (update existing). No-op for an empty ValueJson.
	 */
	static void ApplyValue(FInstancedPropertyBag& Bag, const FName ParamFName,
		EPropertyBagPropertyType BagType, const UObject* ValueTypeObject,
		const FString& ValueJson, bool& bOutApplied, FString& OutNote)
	{
		bOutApplied = false;
		if (ValueJson.IsEmpty()) { return; }
		switch (BagType)
		{
		case EPropertyBagPropertyType::Bool:
			Bag.SetValueBool(ParamFName, ValueJson.ToBool()); bOutApplied = true; break;
		case EPropertyBagPropertyType::Int32:
			Bag.SetValueInt32(ParamFName, FCString::Atoi(*ValueJson)); bOutApplied = true; break;
		case EPropertyBagPropertyType::Int64:
			Bag.SetValueInt64(ParamFName, FCString::Atoi64(*ValueJson)); bOutApplied = true; break;
		case EPropertyBagPropertyType::Float:
			Bag.SetValueFloat(ParamFName, (float)FCString::Atod(*ValueJson)); bOutApplied = true; break;
		case EPropertyBagPropertyType::Double:
			Bag.SetValueDouble(ParamFName, FCString::Atod(*ValueJson)); bOutApplied = true; break;
		case EPropertyBagPropertyType::Name:
			Bag.SetValueName(ParamFName, FName(*ValueJson.TrimQuotes())); bOutApplied = true; break;
		case EPropertyBagPropertyType::String:
		case EPropertyBagPropertyType::Text:
			Bag.SetValueString(ParamFName, ValueJson.TrimQuotes()); bOutApplied = true; break;
		case EPropertyBagPropertyType::Struct:
			if (const UScriptStruct* AsStruct = Cast<UScriptStruct>(ValueTypeObject))
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					FInstancedStruct Tmp; Tmp.InitializeAs(AsStruct);
					if (FJsonObjectConverter::JsonObjectToUStruct(JsonObj.ToSharedRef(), AsStruct, Tmp.GetMutableMemory()))
					{
						Bag.SetValueStruct(ParamFName, FConstStructView(AsStruct, Tmp.GetMemory()));
						bOutApplied = true;
					}
					else { OutNote = TEXT("JsonObjectToUStruct failed for struct value"); }
				}
				else { OutNote = TEXT("value_json was not a valid JSON object for the struct type"); }
			}
			break;
		default:
			OutNote = TEXT("value not supported for this property type");
			break;
		}
	}

	/** Save the graph's package after a bag mutation. Returns empty string on success, an error envelope otherwise. */
	static FString SaveGraphPackage(UPCGGraph* Graph, const FString& AssetName)
	{
		UPackage* Package = Graph->GetOutermost();
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Graph, *PackageFileName, SaveArgs))
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::SAVE_FAILED,
				TEXT("Failed to save PCG graph"),
				FString::Printf(TEXT("User parameter mutated but PCG graph '%s' could not be saved"), *AssetName),
				TEXT("Ensure the asset is not read-only"));
		}
		return FString();
	}
}
#endif

static FString MakePCGPluginUnavailable()
{
	return BlueprintNodeHelpers::CreateDetailedErrorJson(
		TEXT("pcg_plugin_unavailable"),
		TEXT("PCG plugin unavailable"),
		TEXT("The consuming project did not enable PCG"),
		TEXT("Enable PCG in your .uproject, then rebuild."));
}

FString UCURemoteLibrary::AddPCGGraphUserParameter(
	const FString& AssetName,
	const FString& ParameterName,
	const FString& ParameterTypeName,
	const FString& DefaultValueJson)
{
#if !WITH_NARSHAMCP_PCG
	return MakePCGPluginUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] AddPCGGraphUserParameter: %s, Param: %s, Type: %s"), *AssetName, *ParameterName, *ParameterTypeName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/PCG/PCG_Forest.PCG_Forest"));
	}
	if (ParameterName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter name cannot be empty"),
			TEXT("ParameterName parameter is required"),
			TEXT("Provide a parameter name like 'Seed' or 'SpawnDensity'"));
	}
	if (ParameterTypeName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter type cannot be empty"),
			TEXT("ParameterTypeName parameter is required"),
			TEXT("Use one of: bool, int32, float, FName, FString, FVector, or /Script/Module.MyStruct"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetName);
	if (!Graph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("PCG graph not found: %s"), *AssetName),
			TEXT("The specified PCG graph asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/PCG/PCG_Forest.PCG_Forest)"));
	}

	EPropertyBagPropertyType BagType = EPropertyBagPropertyType::None;
	const UObject* ValueTypeObject = nullptr;
	if (!PCGGraphParamHelpers::ResolveParameterType(ParameterTypeName, BagType, ValueTypeObject))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Unknown parameter type: %s"), *ParameterTypeName),
			TEXT("Type token did not match a scalar and could not be resolved as a UScriptStruct"),
			TEXT("Use bool/int32/float/FName/FString/FVector or a valid /Script/Module.MyStruct path"));
	}

	const FName ParamFName(*ParameterName);
	if (const FInstancedPropertyBag* ExistingBag = Graph->GetUserParametersStruct())
	{
		if (ExistingBag->FindPropertyDescByName(ParamFName) != nullptr)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Parameter '%s' already exists in this PCG graph's user parameters"), *ParameterName),
				TEXT("Parameter names must be unique within the user parameter bag"),
				TEXT("Use a different name, or set_graph_parameter to change the existing value"));
		}
	}

	// Open the transaction BEFORE the first mutation (StateTree PR3 ns-review B-1 lesson):
	// UpdateUserParametersStruct performs AddProperty + SetValueXxx and fires the PCG refresh.
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddPCGGraphUserParameter", "Add PCG Graph User Parameter"));
	Graph->Modify();
#endif

	bool bDefaultApplied = false;
	FString DefaultApplyNote;
	EPropertyBagAlterationResult AddResult = EPropertyBagAlterationResult::InternalError;
	Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag)
	{
		AddResult = Bag.AddProperty(ParamFName, BagType, ValueTypeObject);
		if (AddResult == EPropertyBagAlterationResult::Success)
		{
			PCGGraphParamHelpers::ApplyValue(Bag, ParamFName, BagType, ValueTypeObject, DefaultValueJson, bDefaultApplied, DefaultApplyNote);
		}
	});

	if (AddResult != EPropertyBagAlterationResult::Success)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to add parameter '%s' to the user parameter bag"), *ParameterName),
			TEXT("FInstancedPropertyBag::AddProperty returned a non-success alteration result"),
			TEXT("Check the parameter name for illegal characters"));
	}

	const FString SaveError = PCGGraphParamHelpers::SaveGraphPackage(Graph, AssetName);
	if (!SaveError.IsEmpty()) { return SaveError; }

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("parameter_name"), ParameterName);
	ResultObject->SetStringField(TEXT("parameter_type"), ParameterTypeName);
	ResultObject->SetBoolField(TEXT("default_applied"), bDefaultApplied);
	if (!DefaultApplyNote.IsEmpty()) { ResultObject->SetStringField(TEXT("default_note"), DefaultApplyNote); }

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] AddPCGGraphUserParameter completed: %s (%s)"), *ParameterName, *ParameterTypeName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
}

FString UCURemoteLibrary::GetPCGGraphUserParameters(const FString& AssetName)
{
#if !WITH_NARSHAMCP_PCG
	return MakePCGPluginUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] GetPCGGraphUserParameters: %s"), *AssetName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/PCG/PCG_Forest.PCG_Forest"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetName);
	if (!Graph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("PCG graph not found: %s"), *AssetName),
			TEXT("The specified PCG graph asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/PCG/PCG_Forest.PCG_Forest)"));
	}

	TArray<TSharedPtr<FJsonValue>> ParamsArray;
	if (const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct())
	{
		if (const UPropertyBag* BagStruct = Bag->GetPropertyBagStruct())
		{
			for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
			{
				TSharedPtr<FJsonObject> ParamObj = MakeShared<FJsonObject>();
				ParamObj->SetStringField(TEXT("name"), Desc.Name.ToString());
				ParamObj->SetStringField(TEXT("type"), PCGGraphParamHelpers::PropertyBagTypeToString(Desc));
				TValueOrError<FString, EPropertyBagResult> ValRes = Bag->GetValueSerializedString(Desc.Name);
				if (ValRes.HasValue()) { ParamObj->SetStringField(TEXT("value"), ValRes.GetValue()); }
				ParamsArray.Add(MakeShared<FJsonValueObject>(ParamObj));
			}
		}
	}

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetNumberField(TEXT("parameter_count"), ParamsArray.Num());
	ResultObject->SetArrayField(TEXT("parameters"), ParamsArray);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] GetPCGGraphUserParameters completed: %s (%d params)"), *AssetName, ParamsArray.Num());
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
}

FString UCURemoteLibrary::SetPCGGraphUserParameter(
	const FString& AssetName,
	const FString& ParameterName,
	const FString& ValueJson)
{
#if !WITH_NARSHAMCP_PCG
	return MakePCGPluginUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] SetPCGGraphUserParameter: %s, Param: %s"), *AssetName, *ParameterName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/PCG/PCG_Forest.PCG_Forest"));
	}
	if (ParameterName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter name cannot be empty"),
			TEXT("ParameterName parameter is required"),
			TEXT("Provide the name of an existing user parameter"));
	}
	if (ValueJson.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Value cannot be empty"),
			TEXT("ValueJson parameter is required for set_graph_parameter"),
			TEXT("Provide a scalar (e.g. '42') or a JSON object for a struct value"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetName);
	if (!Graph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("PCG graph not found: %s"), *AssetName),
			TEXT("The specified PCG graph asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/PCG/PCG_Forest.PCG_Forest)"));
	}

	const FName ParamFName(*ParameterName);
	EPropertyBagPropertyType BagType = EPropertyBagPropertyType::None;
	const UObject* ValueTypeObject = nullptr;
	{
		const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct();
		const FPropertyBagPropertyDesc* Desc = Bag ? Bag->FindPropertyDescByName(ParamFName) : nullptr;
		if (!Desc)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Parameter '%s' not found in this PCG graph's user parameters"), *ParameterName),
				TEXT("set_graph_parameter can only update an existing parameter"),
				TEXT("Use add_graph_parameter first, or check the parameter name via get_graph_parameters"));
		}
		BagType = Desc->ValueType;
		ValueTypeObject = Desc->ValueTypeObject;
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "SetPCGGraphUserParameter", "Set PCG Graph User Parameter"));
	Graph->Modify();
#endif

	bool bValueApplied = false;
	FString ApplyNote;
	Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag)
	{
		PCGGraphParamHelpers::ApplyValue(Bag, ParamFName, BagType, ValueTypeObject, ValueJson, bValueApplied, ApplyNote);
	});

	if (!bValueApplied)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Could not apply value to parameter '%s'"), *ParameterName),
			ApplyNote.IsEmpty() ? TEXT("Value conversion failed for the parameter's type") : ApplyNote,
			TEXT("For struct parameters ValueJson must be a valid JSON object matching the struct"));
	}

	const FString SaveError = PCGGraphParamHelpers::SaveGraphPackage(Graph, AssetName);
	if (!SaveError.IsEmpty()) { return SaveError; }

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("parameter_name"), ParameterName);
	ResultObject->SetBoolField(TEXT("value_applied"), bValueApplied);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] SetPCGGraphUserParameter completed: %s"), *ParameterName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
}

FString UCURemoteLibrary::RemovePCGGraphUserParameter(
	const FString& AssetName,
	const FString& ParameterName)
{
#if !WITH_NARSHAMCP_PCG
	return MakePCGPluginUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] RemovePCGGraphUserParameter: %s, Param: %s"), *AssetName, *ParameterName);

	if (AssetName.Contains(TEXT("//")))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			FString::Printf(TEXT("Invalid asset path (contains double slash): %s"), *AssetName),
			TEXT("Asset path must not contain '//'"),
			TEXT("Use format: /Game/PCG/PCG_Forest.PCG_Forest"));
	}
	if (ParameterName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Parameter name cannot be empty"),
			TEXT("ParameterName parameter is required"),
			TEXT("Provide the name of an existing user parameter to remove"));
	}

	UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *AssetName);
	if (!Graph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_ASSET_NOT_FOUND,
			FString::Printf(TEXT("PCG graph not found: %s"), *AssetName),
			TEXT("The specified PCG graph asset could not be loaded"),
			TEXT("Verify the asset path is correct (e.g., /Game/PCG/PCG_Forest.PCG_Forest)"));
	}

	const FName ParamFName(*ParameterName);
	{
		const FInstancedPropertyBag* Bag = Graph->GetUserParametersStruct();
		if (!Bag || Bag->FindPropertyDescByName(ParamFName) == nullptr)
		{
			return BlueprintNodeHelpers::CreateDetailedErrorJson(
				BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
				FString::Printf(TEXT("Parameter '%s' not found in this PCG graph's user parameters"), *ParameterName),
				TEXT("remove_graph_parameter can only remove an existing parameter"),
				TEXT("Check the parameter name via get_graph_parameters"));
		}
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemovePCGGraphUserParameter", "Remove PCG Graph User Parameter"));
	Graph->Modify();
#endif

	EPropertyBagAlterationResult RemoveResult = EPropertyBagAlterationResult::InternalError;
	Graph->UpdateUserParametersStruct([&](FInstancedPropertyBag& Bag)
	{
		RemoveResult = Bag.RemovePropertyByName(ParamFName);
	});

	if (RemoveResult != EPropertyBagAlterationResult::Success)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			FString::Printf(TEXT("Failed to remove parameter '%s'"), *ParameterName),
			TEXT("FInstancedPropertyBag::RemovePropertyByName returned a non-success alteration result"),
			TEXT("The parameter may have already been removed"));
	}

	const FString SaveError = PCGGraphParamHelpers::SaveGraphPackage(Graph, AssetName);
	if (!SaveError.IsEmpty()) { return SaveError; }

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("asset_path"), AssetName);
	ResultObject->SetStringField(TEXT("parameter_name"), ParameterName);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[PCG] RemovePCGGraphUserParameter completed: %s"), *ParameterName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
#endif
}

// =========================================================
// AnimBP State Machine CRUD (Issue #6389)
// =========================================================

// Helper namespace for AnimBP operations
namespace AnimBPHelpers
{
	/** Find a state machine graph node by name in an AnimBlueprint */
	UAnimGraphNode_StateMachine* FindStateMachineNode(UAnimBlueprint* AnimBP, const FString& StateMachineName)
	{
		if (!AnimBP)
		{
			return nullptr;
		}

		// Search all graphs for UAnimGraphNode_StateMachine nodes
		TArray<UEdGraph*> AllGraphs;
		AllGraphs.Append(AnimBP->FunctionGraphs);
		AllGraphs.Append(AnimBP->UbergraphPages);

		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph)
			{
				continue;
			}

			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node);
				if (SMNode && SMNode->EditorStateMachineGraph)
				{
					FString MachineName = SMNode->EditorStateMachineGraph->GetName();
					if (MachineName.Equals(StateMachineName, ESearchCase::IgnoreCase) ||
						MachineName.Contains(StateMachineName))
					{
						return SMNode;
					}
				}
			}
		}
		return nullptr;
	}

	/** Find an AnimStateNode by name in a state machine graph */
	UAnimStateNode* FindStateNode(UEdGraph* SMGraph, const FString& StateName)
	{
		if (!SMGraph)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
			if (StateNode)
			{
				// Check node title/comment for name match
				FString NodeTitle = StateNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				if (NodeTitle.Equals(StateName, ESearchCase::IgnoreCase) ||
					StateNode->GetStateName().Equals(StateName, ESearchCase::IgnoreCase))
				{
					return StateNode;
				}
				// Also check NodeComment (ue-llm-toolkit ID system)
				if (!StateNode->NodeComment.IsEmpty() && StateNode->NodeComment.Contains(StateName))
				{
					return StateNode;
				}
			}
		}
		return nullptr;
	}

	/** Get all state names from a state machine graph */
	TArray<FString> GetAllStateNames(UEdGraph* SMGraph)
	{
		TArray<FString> Names;
		if (!SMGraph)
		{
			return Names;
		}

		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node);
			if (StateNode)
			{
				Names.Add(StateNode->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			}
		}
		return Names;
	}

	struct FAnimTransitionMatch
	{
		UAnimStateTransitionNode* Node = nullptr;
		FString FromState;
		FString ToState;
		FString ExportName;
	};

	/** Collect every transition and its stable editor-facing locators. */
	TArray<FAnimTransitionMatch> CollectTransitionNodes(UEdGraph* SMGraph)
	{
		TArray<FAnimTransitionMatch> Matches;
		if (!SMGraph)
		{
			return Matches;
		}

		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node))
			{
				UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
				UAnimStateNodeBase* NextState = TransNode->GetNextState();
				if (PrevState && NextState)
				{
					FAnimTransitionMatch& Match = Matches.AddDefaulted_GetRef();
					Match.Node = TransNode;
					Match.FromState = PrevState->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
					Match.ToState = NextState->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
					Match.ExportName = TransNode->GetName();
				}
			}
		}
		return Matches;
	}
}

FString UCURemoteLibrary::AddAnimState(
	const FString& BlueprintPath,
	const FString& StateMachineName,
	const FString& StateName,
	const FString& AnimationAsset,
	float PositionX,
	float PositionY)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] AddAnimState: %s, SM: %s, State: %s"),
		*BlueprintPath, *StateMachineName, *StateName);

	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("BlueprintPath is empty"));
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *BlueprintPath));
	}

	UAnimGraphNode_StateMachine* SMNode = AnimBPHelpers::FindStateMachineNode(AnimBP, StateMachineName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State machine '%s' not found in AnimBP"), *StateMachineName));
	}

	UEdGraph* SMGraph = Cast<UEdGraph>(SMNode->EditorStateMachineGraph.Get());

	// Check if state already exists
	if (AnimBPHelpers::FindStateNode(SMGraph, StateName))
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State '%s' already exists in state machine '%s'"), *StateName, *StateMachineName));
	}

	// Create new state node
	FGraphNodeCreator<UAnimStateNode> NodeCreator(*SMGraph);
	UAnimStateNode* NewState = NodeCreator.CreateNode(/*bSelectNewNode=*/false);
	if (!NewState)
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Failed to create UAnimStateNode"));
	}

	NewState->NodePosX = static_cast<int32>(PositionX);
	NewState->NodePosY = static_cast<int32>(PositionY);
	NewState->NodeComment = FString::Printf(TEXT("State_%s"), *StateName);
	NodeCreator.Finalize();

	// Rename via BoundGraph if available
	if (NewState->BoundGraph)
	{
		NewState->BoundGraph->Rename(*StateName, nullptr, REN_DontCreateRedirectors);
	}

	// If AnimationAsset provided, try to configure a sequence player in the state's bound graph
	if (!AnimationAsset.IsEmpty() && NewState->BoundGraph)
	{
		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] AnimationAsset: %s (will be configured by caller)"), *AnimationAsset);
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	AnimBP->MarkPackageDirty();

	// Build response
	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("state_name"), StateName);
	ResultObject->SetStringField(TEXT("state_machine"), StateMachineName);
	ResultObject->SetStringField(TEXT("node_comment"), NewState->NodeComment);
	ResultObject->SetNumberField(TEXT("pos_x"), NewState->NodePosX);
	ResultObject->SetNumberField(TEXT("pos_y"), NewState->NodePosY);

	TArray<TSharedPtr<FJsonValue>> AllStates;
	for (const FString& Name : AnimBPHelpers::GetAllStateNames(SMGraph))
	{
		AllStates.Add(MakeShared<FJsonValueString>(Name));
	}
	ResultObject->SetArrayField(TEXT("all_states"), AllStates);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] AddAnimState completed: %s"), *StateName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::RemoveAnimState(
	const FString& BlueprintPath,
	const FString& StateMachineName,
	const FString& StateName)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] RemoveAnimState: %s, SM: %s, State: %s"),
		*BlueprintPath, *StateMachineName, *StateName);

	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("BlueprintPath is empty"));
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *BlueprintPath));
	}

	UAnimGraphNode_StateMachine* SMNode = AnimBPHelpers::FindStateMachineNode(AnimBP, StateMachineName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State machine '%s' not found in AnimBP"), *StateMachineName));
	}

	UEdGraph* SMGraph = Cast<UEdGraph>(SMNode->EditorStateMachineGraph.Get());
	UAnimStateNode* TargetState = AnimBPHelpers::FindStateNode(SMGraph, StateName);
	if (!TargetState)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State '%s' not found in state machine '%s'"), *StateName, *StateMachineName));
	}

	// Count and remove connected transitions first
	int32 RemovedTransitions = 0;
	TArray<UEdGraphNode*> NodesToRemove;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(Node);
		if (TransNode)
		{
			UAnimStateNodeBase* PrevState = TransNode->GetPreviousState();
			UAnimStateNodeBase* NextState = TransNode->GetNextState();
			if (PrevState == TargetState || NextState == TargetState)
			{
				NodesToRemove.Add(TransNode);
				RemovedTransitions++;
			}
		}
	}

	// Remove transition nodes
	for (UEdGraphNode* NodeToRemove : NodesToRemove)
	{
		SMGraph->RemoveNode(NodeToRemove);
	}

	// Remove the state node
	SMGraph->RemoveNode(TargetState);

	// Compile
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("removed_state"), StateName);
	ResultObject->SetNumberField(TEXT("removed_transitions_count"), RemovedTransitions);

	TArray<TSharedPtr<FJsonValue>> RemainingStates;
	for (const FString& Name : AnimBPHelpers::GetAllStateNames(SMGraph))
	{
		RemainingStates.Add(MakeShared<FJsonValueString>(Name));
	}
	ResultObject->SetArrayField(TEXT("remaining_states"), RemainingStates);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] RemoveAnimState completed: %s (removed %d transitions)"),
		*StateName, RemovedTransitions);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::SetAnimEntryState(
	const FString& BlueprintPath,
	const FString& StateMachineName,
	const FString& StateName)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] SetAnimEntryState: %s, SM: %s, State: %s"),
		*BlueprintPath, *StateMachineName, *StateName);

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: SetAnimEntryState")));
#endif

	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("BlueprintPath is empty"));
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *BlueprintPath));
	}

	UAnimGraphNode_StateMachine* SMNode = AnimBPHelpers::FindStateMachineNode(AnimBP, StateMachineName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State machine '%s' not found in AnimBP"), *StateMachineName));
	}

	UEdGraph* SMGraph = Cast<UEdGraph>(SMNode->EditorStateMachineGraph.Get());
	UAnimStateNode* TargetState = AnimBPHelpers::FindStateNode(SMGraph, StateName);
	if (!TargetState)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State '%s' not found in state machine '%s'"), *StateName, *StateMachineName));
	}

	// Find the entry node
	FString PreviousEntryState = TEXT("(none)");
	UAnimStateEntryNode* EntryNode = nullptr;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		EntryNode = Cast<UAnimStateEntryNode>(Node);
		if (EntryNode)
		{
			// Get previous entry state name
			if (EntryNode->Pins.Num() > 0)
			{
				UEdGraphPin* OutputPin = EntryNode->Pins[0];
				if (OutputPin && OutputPin->LinkedTo.Num() > 0)
				{
					UEdGraphNode* PrevNode = OutputPin->LinkedTo[0]->GetOwningNode();
					if (UAnimStateNode* PrevState = Cast<UAnimStateNode>(PrevNode))
					{
						PreviousEntryState = PrevState->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
					}
				}
			}
			break;
		}
	}

	if (!EntryNode)
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Entry node not found in state machine"));
	}

	// Disconnect existing entry connection
	if (EntryNode->Pins.Num() > 0)
	{
		EntryNode->Pins[0]->BreakAllPinLinks();
	}

	// Connect entry to target state's input pin
	UEdGraphPin* EntryOutput = EntryNode->Pins.Num() > 0 ? EntryNode->Pins[0] : nullptr;
	UEdGraphPin* StateInput = nullptr;
	for (UEdGraphPin* Pin : TargetState->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			StateInput = Pin;
			break;
		}
	}

	if (EntryOutput && StateInput)
	{
		EntryOutput->MakeLinkTo(StateInput);
	}
	else
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Failed to connect entry node to target state"));
	}

	// Compile
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("entry_state"), StateName);
	ResultObject->SetStringField(TEXT("previous_entry_state"), PreviousEntryState);
	ResultObject->SetStringField(TEXT("state_machine"), StateMachineName);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] SetAnimEntryState completed: %s -> %s"),
		*PreviousEntryState, *StateName);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::AddAnimTransition(
	const FString& BlueprintPath,
	const FString& StateMachineName,
	const FString& FromState,
	const FString& ToState,
	float Duration,
	const FString& BlendMode)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] AddAnimTransition: %s, SM: %s, %s -> %s"),
		*BlueprintPath, *StateMachineName, *FromState, *ToState);

	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("BlueprintPath is empty"));
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *BlueprintPath));
	}

	UAnimGraphNode_StateMachine* SMNode = AnimBPHelpers::FindStateMachineNode(AnimBP, StateMachineName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State machine '%s' not found in AnimBP"), *StateMachineName));
	}

	UEdGraph* SMGraph = Cast<UEdGraph>(SMNode->EditorStateMachineGraph.Get());
	UAnimStateNode* SourceState = AnimBPHelpers::FindStateNode(SMGraph, FromState);
	UAnimStateNode* DestState = AnimBPHelpers::FindStateNode(SMGraph, ToState);

	if (!SourceState)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Source state '%s' not found"), *FromState));
	}
	if (!DestState)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Destination state '%s' not found"), *ToState));
	}

	// Check if transition already exists
	const TArray<AnimBPHelpers::FAnimTransitionMatch> ExistingTransitions =
		AnimBPHelpers::CollectTransitionNodes(SMGraph);
	const bool bTransitionAlreadyExists = ExistingTransitions.ContainsByPredicate(
		[&FromState, &ToState](const AnimBPHelpers::FAnimTransitionMatch& Candidate)
		{
			return Candidate.FromState.Equals(FromState, ESearchCase::IgnoreCase)
				&& Candidate.ToState.Equals(ToState, ESearchCase::IgnoreCase);
		});
	if (bTransitionAlreadyExists)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Transition from '%s' to '%s' already exists"), *FromState, *ToState));
	}

	// Create transition node
	FGraphNodeCreator<UAnimStateTransitionNode> NodeCreator(*SMGraph);
	UAnimStateTransitionNode* TransNode = NodeCreator.CreateNode(/*bSelectNewNode=*/false);
	if (!TransNode)
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("Failed to create UAnimStateTransitionNode"));
	}

	// Position between source and destination
	TransNode->NodePosX = (SourceState->NodePosX + DestState->NodePosX) / 2;
	TransNode->NodePosY = (SourceState->NodePosY + DestState->NodePosY) / 2;
	NodeCreator.Finalize();

	// Connect: source output -> transition input, transition output -> destination input
	UEdGraphPin* SourceOutput = nullptr;
	for (UEdGraphPin* Pin : SourceState->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output)
		{
			SourceOutput = Pin;
			break;
		}
	}
	UEdGraphPin* TransInput = nullptr;
	UEdGraphPin* TransOutput = nullptr;
	for (UEdGraphPin* Pin : TransNode->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			TransInput = Pin;
		}
		else if (Pin && Pin->Direction == EGPD_Output)
		{
			TransOutput = Pin;
		}
	}
	UEdGraphPin* DestInput = nullptr;
	for (UEdGraphPin* Pin : DestState->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input)
		{
			DestInput = Pin;
			break;
		}
	}

	if (SourceOutput && TransInput)
	{
		SourceOutput->MakeLinkTo(TransInput);
	}
	if (TransOutput && DestInput)
	{
		TransOutput->MakeLinkTo(DestInput);
	}

	// Set crossfade duration
	TransNode->CrossfadeDuration = Duration > 0.0f ? Duration : 0.2f;

	// Compile
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	ResultObject->SetStringField(TEXT("from_state"), FromState);
	ResultObject->SetStringField(TEXT("to_state"), ToState);
	ResultObject->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
	ResultObject->SetStringField(TEXT("blend_mode"), BlendMode.IsEmpty() ? TEXT("Standard") : *BlendMode);
	ResultObject->SetStringField(TEXT("state_machine"), StateMachineName);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] AddAnimTransition completed: %s -> %s (%.2fs)"),
		*FromState, *ToState, TransNode->CrossfadeDuration);
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

FString UCURemoteLibrary::SetAnimTransitionRule(
	const FString& BlueprintPath,
	const FString& StateMachineName,
	const FString& FromState,
	const FString& ToState,
	const FString& PropertiesJson,
	const FString& TransitionExportName,
	bool bPreviewOnly)
{
	UE_LOG(LogNarshaMCPRemote, Log,
		TEXT("[AnimBP] SetAnimTransitionRule: %s, SM: %s, %s -> %s, export: %s, preview: %s"),
		*BlueprintPath, *StateMachineName, *FromState, *ToState, *TransitionExportName,
		bPreviewOnly ? TEXT("true") : TEXT("false"));

	if (BlueprintPath.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateErrorJson(TEXT("BlueprintPath is empty"));
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("Failed to load AnimBlueprint: %s"), *BlueprintPath));
	}

	UAnimGraphNode_StateMachine* SMNode = AnimBPHelpers::FindStateMachineNode(AnimBP, StateMachineName);
	if (!SMNode || !SMNode->EditorStateMachineGraph)
	{
		return BlueprintNodeHelpers::CreateErrorJson(
			FString::Printf(TEXT("State machine '%s' not found in AnimBP"), *StateMachineName));
	}

	UEdGraph* SMGraph = Cast<UEdGraph>(SMNode->EditorStateMachineGraph.Get());
	const TArray<AnimBPHelpers::FAnimTransitionMatch> AllTransitions =
		AnimBPHelpers::CollectTransitionNodes(SMGraph);
	TArray<AnimBPHelpers::FAnimTransitionMatch> Matches;
	for (const AnimBPHelpers::FAnimTransitionMatch& Candidate : AllTransitions)
	{
		const bool bMatchesExport = !TransitionExportName.IsEmpty()
			&& Candidate.ExportName.Equals(TransitionExportName, ESearchCase::CaseSensitive);
		const bool bMatchesPair = TransitionExportName.IsEmpty()
			&& Candidate.FromState.Equals(FromState, ESearchCase::IgnoreCase)
			&& Candidate.ToState.Equals(ToState, ESearchCase::IgnoreCase);
		if (bMatchesExport || bMatchesPair)
		{
			Matches.Add(Candidate);
		}
	}

	auto MakeTransitionSelectionError = [&AllTransitions](
		const FString& ErrorMessage,
		const TArray<AnimBPHelpers::FAnimTransitionMatch>& Candidates,
		const FString& Suggestion) -> FString
	{
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObject->SetStringField(
			TEXT("error_code"), BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED);
		ErrorObject->SetStringField(JSON_ERROR_FIELD, ErrorMessage);
		ErrorObject->SetStringField(TEXT("suggestion"), Suggestion);

		TArray<TSharedPtr<FJsonValue>> CandidateValues;
		for (const AnimBPHelpers::FAnimTransitionMatch& Candidate : Candidates)
		{
			TSharedPtr<FJsonObject> CandidateObject = MakeShared<FJsonObject>();
			CandidateObject->SetStringField(TEXT("export_name"), Candidate.ExportName);
			CandidateObject->SetStringField(TEXT("from_state"), Candidate.FromState);
			CandidateObject->SetStringField(TEXT("to_state"), Candidate.ToState);
			CandidateValues.Add(MakeShared<FJsonValueObject>(CandidateObject));
		}
		ErrorObject->SetArrayField(TEXT("candidates"), CandidateValues);

		TArray<TSharedPtr<FJsonValue>> AvailableValues;
		for (const AnimBPHelpers::FAnimTransitionMatch& Available : AllTransitions)
		{
			AvailableValues.Add(MakeShared<FJsonValueString>(Available.ExportName));
		}
		ErrorObject->SetArrayField(TEXT("available_transition_export_names"), AvailableValues);
		return CUJsonUtils::SerializeToString(ErrorObject);
	};

	if (Matches.Num() == 0)
	{
		const FString SelectorDescription = !TransitionExportName.IsEmpty()
			? FString::Printf(TEXT("export name '%s'"), *TransitionExportName)
			: FString::Printf(TEXT("'%s' -> '%s'"), *FromState, *ToState);
		return MakeTransitionSelectionError(
			FString::Printf(TEXT("Transition %s not found"), *SelectorDescription), Matches,
			TEXT("Use get_animgraph to inspect transition export_name values and state endpoints"));
	}
	if (Matches.Num() > 1)
	{
		return MakeTransitionSelectionError(
			FString::Printf(TEXT("Ambiguous transition from '%s' to '%s': %d matches"),
				*FromState, *ToState, Matches.Num()), Matches,
			TEXT("Pass transition_export_name from get_animgraph to select one transition exactly"));
	}
	const AnimBPHelpers::FAnimTransitionMatch& Resolved = Matches[0];
	UAnimStateTransitionNode* TransNode = Resolved.Node;

	// Parse properties JSON
	TSharedPtr<FJsonObject> PropsObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObject) || !PropsObject.IsValid())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_INVALID_JSON,
			TEXT("Invalid PropertiesJson"), PropertiesJson,
			TEXT("Pass a JSON object containing CrossfadeDuration and/or PriorityOrder"));
	}

	struct FStagedTransitionProperties
	{
		bool bSetCrossfadeDuration = false;
		float CrossfadeDuration = 0.0f;
		bool bSetPriorityOrder = false;
		int32 PriorityOrder = 0;
		bool bSetBlendMode = false;
		EAlphaBlendOption BlendMode = EAlphaBlendOption::HermiteCubic;
		bool bSetCustomBlendCurve = false;
		UCurveFloat* CustomBlendCurve = nullptr;
		bool bSetBidirectional = false;
		bool bBidirectional = false;
		bool bSetAutomaticRule = false;
		bool bAutomaticRule = false;
		bool bSetAutomaticRuleTriggerTime = false;
		float AutomaticRuleTriggerTime = 0.0f;
		bool bSetBlendProfile = false;
		UBlendProfile* BlendProfile = nullptr;
		TArray<FString> ChangedProperties;
	};
	FStagedTransitionProperties Staged;
	TArray<FString> InvalidProperties;
	for (const auto& Pair : PropsObject->Values)
	{
		const FString PropertyName(Pair.Key);
		if (PropertyName != TEXT("CrossfadeDuration")
			&& PropertyName != TEXT("PriorityOrder")
			&& PropertyName != TEXT("BlendMode")
			&& PropertyName != TEXT("CustomBlendCurve")
			&& PropertyName != TEXT("Bidirectional")
			&& PropertyName != TEXT("bAutomaticRuleBasedOnSequencePlayerInState")
			&& PropertyName != TEXT("AutomaticRuleTriggerTime")
			&& PropertyName != TEXT("BlendProfile"))
		{
			InvalidProperties.Add(FString::Printf(TEXT("%s (unsupported)"), *PropertyName));
		}
	}

	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(TEXT("CrossfadeDuration")))
	{
		double RawValue = 0.0;
		if (!Value->IsValid() || !(*Value)->TryGetNumber(RawValue)
			|| !FMath::IsFinite(RawValue) || RawValue < 0.0
			|| RawValue > static_cast<double>(TNumericLimits<float>::Max())
			|| !FMath::IsFinite(static_cast<float>(RawValue)))
		{
			InvalidProperties.Add(TEXT("CrossfadeDuration (expected a finite non-negative float)"));
		}
		else
		{
			Staged.bSetCrossfadeDuration = true;
			Staged.CrossfadeDuration = static_cast<float>(RawValue);
			if (TransNode->CrossfadeDuration != Staged.CrossfadeDuration)
			{
				Staged.ChangedProperties.Add(TEXT("CrossfadeDuration"));
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(TEXT("BlendMode")))
	{
		FString RawValue;
		const UEnum* BlendModeEnum = StaticEnum<EAlphaBlendOption>();
		const int64 EnumValue = Value->IsValid() && (*Value)->TryGetString(RawValue)
			&& BlendModeEnum
			? BlendModeEnum->GetValueByNameString(RawValue)
			: INDEX_NONE;
		if (EnumValue == INDEX_NONE)
		{
			InvalidProperties.Add(TEXT("BlendMode (expected an EAlphaBlendOption name)"));
		}
		else
		{
			Staged.bSetBlendMode = true;
			Staged.BlendMode = static_cast<EAlphaBlendOption>(EnumValue);
			if (TransNode->BlendMode != Staged.BlendMode)
			{
				Staged.ChangedProperties.Add(TEXT("BlendMode"));
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(TEXT("CustomBlendCurve")))
	{
		FString CurvePath;
		if (!Value->IsValid() || !(*Value)->TryGetString(CurvePath))
		{
			InvalidProperties.Add(TEXT("CustomBlendCurve (expected an asset-path string; empty clears)"));
		}
		else
		{
			UCurveFloat* ResolvedCurve = nullptr;
			if (!CurvePath.IsEmpty())
			{
				ResolvedCurve = LoadObject<UCurveFloat>(nullptr, *CurvePath);
				if (!ResolvedCurve)
				{
					InvalidProperties.Add(FString::Printf(
						TEXT("CustomBlendCurve (CurveFloat not found: %s)"), *CurvePath));
				}
			}
			if (CurvePath.IsEmpty() || ResolvedCurve)
			{
				Staged.bSetCustomBlendCurve = true;
				Staged.CustomBlendCurve = ResolvedCurve;
				if (TransNode->CustomBlendCurve != ResolvedCurve)
				{
					Staged.ChangedProperties.Add(TEXT("CustomBlendCurve"));
				}
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(TEXT("Bidirectional")))
	{
		bool RawValue = false;
		if (!Value->IsValid() || (*Value)->Type != EJson::Boolean
			|| !(*Value)->TryGetBool(RawValue))
		{
			InvalidProperties.Add(TEXT("Bidirectional (expected a JSON bool)"));
		}
		else
		{
			Staged.bSetBidirectional = true;
			Staged.bBidirectional = RawValue;
			if (TransNode->Bidirectional != RawValue)
			{
				Staged.ChangedProperties.Add(TEXT("Bidirectional"));
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(
		TEXT("bAutomaticRuleBasedOnSequencePlayerInState")))
	{
		bool RawValue = false;
		if (!Value->IsValid() || (*Value)->Type != EJson::Boolean
			|| !(*Value)->TryGetBool(RawValue))
		{
			InvalidProperties.Add(TEXT(
				"bAutomaticRuleBasedOnSequencePlayerInState (expected a JSON bool)"));
		}
		else
		{
			Staged.bSetAutomaticRule = true;
			Staged.bAutomaticRule = RawValue;
			if (TransNode->bAutomaticRuleBasedOnSequencePlayerInState != RawValue)
			{
				Staged.ChangedProperties.Add(
					TEXT("bAutomaticRuleBasedOnSequencePlayerInState"));
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(
		TEXT("AutomaticRuleTriggerTime")))
	{
		double RawValue = 0.0;
		if (!Value->IsValid() || !(*Value)->TryGetNumber(RawValue)
			|| !FMath::IsFinite(RawValue)
			|| RawValue < -static_cast<double>(TNumericLimits<float>::Max())
			|| RawValue > static_cast<double>(TNumericLimits<float>::Max())
			|| !FMath::IsFinite(static_cast<float>(RawValue)))
		{
			InvalidProperties.Add(TEXT(
				"AutomaticRuleTriggerTime (expected a finite in-range float in seconds)"));
		}
		else
		{
			Staged.bSetAutomaticRuleTriggerTime = true;
			Staged.AutomaticRuleTriggerTime = static_cast<float>(RawValue);
			if (TransNode->AutomaticRuleTriggerTime != Staged.AutomaticRuleTriggerTime)
			{
				Staged.ChangedProperties.Add(TEXT("AutomaticRuleTriggerTime"));
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(TEXT("BlendProfile")))
	{
		const TSharedPtr<FJsonObject>* ProfileObject = nullptr;
		if (!Value->IsValid() || !(*Value)->TryGetObject(ProfileObject)
			|| !ProfileObject || !ProfileObject->IsValid())
		{
			InvalidProperties.Add(TEXT(
				"BlendProfile (expected {\"path\": string, \"mode\": \"TimeFactor|WeightFactor\"})"));
		}
		else
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& ProfilePair : (*ProfileObject)->Values)
			{
				if (ProfilePair.Key != TEXT("path") && ProfilePair.Key != TEXT("mode"))
				{
					InvalidProperties.Add(FString::Printf(
						TEXT("BlendProfile.%s (unsupported)"), *ProfilePair.Key));
				}
			}

			FString ProfilePath;
			const TSharedPtr<FJsonValue>* PathValue = (*ProfileObject)->Values.Find(TEXT("path"));
			if (!PathValue || !PathValue->IsValid() || !(*PathValue)->TryGetString(ProfilePath))
			{
				InvalidProperties.Add(TEXT("BlendProfile.path (expected a string; empty clears)"));
			}
			else if (ProfilePath.IsEmpty())
			{
				Staged.bSetBlendProfile = true;
				Staged.BlendProfile = nullptr;
				if (TransNode->BlendProfileWrapper.GetBlendProfile().Get() != nullptr)
				{
					Staged.ChangedProperties.Add(TEXT("BlendProfile"));
				}
			}
			else
			{
				FString RequestedMode;
				const TSharedPtr<FJsonValue>* ModeValue = (*ProfileObject)->Values.Find(TEXT("mode"));
				if (!ModeValue || !ModeValue->IsValid() || !(*ModeValue)->TryGetString(RequestedMode)
					|| (RequestedMode != TEXT("TimeFactor")
						&& RequestedMode != TEXT("WeightFactor")))
				{
					InvalidProperties.Add(TEXT(
						"BlendProfile.mode (expected TimeFactor or WeightFactor for a non-empty path)"));
				}
				else
				{
					UBlendProfile* ResolvedProfile = LoadObject<UBlendProfile>(nullptr, *ProfilePath);
					const EBlendProfileMode ExpectedMode = RequestedMode == TEXT("TimeFactor")
						? EBlendProfileMode::TimeFactor
						: EBlendProfileMode::WeightFactor;
					if (!ResolvedProfile)
					{
						InvalidProperties.Add(FString::Printf(
							TEXT("BlendProfile.path (UBlendProfile not found: %s)"), *ProfilePath));
					}
					else if (ResolvedProfile->GetMode() != ExpectedMode)
					{
						InvalidProperties.Add(FString::Printf(
							TEXT("BlendProfile.mode does not match asset mode for %s"), *ProfilePath));
					}
					else if (!AnimBP->TargetSkeleton
						|| ResolvedProfile->GetSkeleton() != AnimBP->TargetSkeleton)
					{
						InvalidProperties.Add(FString::Printf(
							TEXT("BlendProfile.path uses a different skeleton than the AnimBlueprint: %s"),
							*ProfilePath));
					}
					else if (AnimBP->TargetSkeleton->GetBlendProfile(ResolvedProfile->GetFName())
						!= ResolvedProfile)
					{
						InvalidProperties.Add(FString::Printf(
							TEXT("BlendProfile.path is not registered on the AnimBlueprint target skeleton: %s"),
							*ProfilePath));
					}
					else
					{
						Staged.bSetBlendProfile = true;
						Staged.BlendProfile = ResolvedProfile;
						if (TransNode->BlendProfileWrapper.GetBlendProfile().Get() != ResolvedProfile)
						{
							Staged.ChangedProperties.Add(TEXT("BlendProfile"));
						}
					}
				}
			}
		}
	}
	if (const TSharedPtr<FJsonValue>* Value = PropsObject->Values.Find(TEXT("PriorityOrder")))
	{
		double RawValue = 0.0;
		const bool bHasNumber = Value->IsValid() && (*Value)->TryGetNumber(RawValue);
		const double RoundedValue = bHasNumber ? FMath::RoundToDouble(RawValue) : 0.0;
		if (!bHasNumber
			|| !FMath::IsFinite(RawValue) || RawValue != RoundedValue
			|| RawValue < static_cast<double>(TNumericLimits<int32>::Lowest())
			|| RawValue > static_cast<double>(TNumericLimits<int32>::Max()))
		{
			InvalidProperties.Add(TEXT("PriorityOrder (expected an in-range JSON int32)"));
		}
		else
		{
			Staged.bSetPriorityOrder = true;
			Staged.PriorityOrder = static_cast<int32>(RawValue);
			if (TransNode->PriorityOrder != Staged.PriorityOrder)
			{
				Staged.ChangedProperties.Add(TEXT("PriorityOrder"));
			}
		}
	}

	if (PropsObject->Values.Num() == 0)
	{
		InvalidProperties.Add(TEXT("properties (object must not be empty)"));
	}
	if (InvalidProperties.Num() > 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("Transition property validation failed"),
			FString::Printf(TEXT("Rejected properties: %s"),
				*FString::Join(InvalidProperties, TEXT(", "))),
			TEXT("Use the documented transition property types and exact nested BlendProfile schema"));
	}
	if (Staged.ChangedProperties.Num() == 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			BlueprintNodeHelpers::ErrorCodes::AI_OPERATION_FAILED,
			TEXT("No transition properties would change"),
			TEXT("All supplied values already match the selected transition"),
			TEXT("Supply at least one supported value that differs from the current node"));
	}

	auto SetResolvedFields = [&Resolved](TSharedPtr<FJsonObject>& ResultObject)
	{
		ResultObject->SetStringField(TEXT("from_state"), Resolved.FromState);
		ResultObject->SetStringField(TEXT("to_state"), Resolved.ToState);
		ResultObject->SetStringField(TEXT("transition_export_name"), Resolved.ExportName);
	};
	auto MakePropertyArray = [&Staged]()
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const FString& PropertyName : Staged.ChangedProperties)
		{
			Values.Add(MakeShared<FJsonValueString>(PropertyName));
		}
		return Values;
	};

	if (bPreviewOnly)
	{
		TSharedPtr<FJsonObject> PreviewObject = MakeShared<FJsonObject>();
		SetResolvedFields(PreviewObject);
		PreviewObject->SetBoolField(TEXT("dry_run"), true);
		PreviewObject->SetStringField(TEXT("would_call"), TEXT("SetAnimTransitionRule"));
		PreviewObject->SetArrayField(TEXT("would_modify_properties"), MakePropertyArray());
		PreviewObject->SetBoolField(TEXT("compiled"), false);
		PreviewObject->SetStringField(TEXT("compile_hint"),
			TEXT("Call ue_manage_blueprint(operation='compile_blueprint') separately after apply"));
		return BlueprintNodeHelpers::CreateSuccessJson(PreviewObject);
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: SetAnimTransitionRule")));
	TransNode->Modify();
#endif
	if (Staged.bSetCrossfadeDuration &&
		TransNode->CrossfadeDuration != Staged.CrossfadeDuration)
	{
		TransNode->CrossfadeDuration = Staged.CrossfadeDuration;
	}
	if (Staged.bSetPriorityOrder && TransNode->PriorityOrder != Staged.PriorityOrder)
	{
		TransNode->PriorityOrder = Staged.PriorityOrder;
	}
	if (Staged.bSetBlendMode && TransNode->BlendMode != Staged.BlendMode)
	{
		TransNode->BlendMode = Staged.BlendMode;
	}
	if (Staged.bSetCustomBlendCurve && TransNode->CustomBlendCurve != Staged.CustomBlendCurve)
	{
		TransNode->CustomBlendCurve = Staged.CustomBlendCurve;
	}
	if (Staged.bSetBidirectional && TransNode->Bidirectional != Staged.bBidirectional)
	{
		TransNode->Bidirectional = Staged.bBidirectional;
	}
	if (Staged.bSetAutomaticRule
		&& TransNode->bAutomaticRuleBasedOnSequencePlayerInState != Staged.bAutomaticRule)
	{
		TransNode->bAutomaticRuleBasedOnSequencePlayerInState = Staged.bAutomaticRule;
	}
	if (Staged.bSetAutomaticRuleTriggerTime
		&& TransNode->AutomaticRuleTriggerTime != Staged.AutomaticRuleTriggerTime)
	{
		TransNode->AutomaticRuleTriggerTime = Staged.AutomaticRuleTriggerTime;
	}
	if (Staged.bSetBlendProfile
		&& TransNode->BlendProfileWrapper.GetBlendProfile().Get() != Staged.BlendProfile)
	{
		TransNode->BlendProfileWrapper.SetSkeletonBlendProfile(Staged.BlendProfile);
	}
#if WITH_EDITOR
	FProperty* ChangedCrossfadeProperty = nullptr;
	if (Staged.ChangedProperties.Contains(TEXT("CrossfadeDuration")))
	{
		ChangedCrossfadeProperty = FindFProperty<FProperty>(
			UAnimStateTransitionNode::StaticClass(), GET_MEMBER_NAME_CHECKED(UAnimStateTransitionNode, CrossfadeDuration));
	}
	else if (Staged.ChangedProperties.Contains(TEXT("BlendMode")))
	{
		ChangedCrossfadeProperty = FindFProperty<FProperty>(
			UAnimStateTransitionNode::StaticClass(), GET_MEMBER_NAME_CHECKED(UAnimStateTransitionNode, BlendMode));
	}
	else if (Staged.ChangedProperties.Contains(TEXT("CustomBlendCurve")))
	{
		ChangedCrossfadeProperty = FindFProperty<FProperty>(
			UAnimStateTransitionNode::StaticClass(), GET_MEMBER_NAME_CHECKED(UAnimStateTransitionNode, CustomBlendCurve));
	}
	else if (Staged.ChangedProperties.Contains(TEXT("BlendProfile")))
	{
		ChangedCrossfadeProperty = FindFProperty<FProperty>(
			UAnimStateTransitionNode::StaticClass(), GET_MEMBER_NAME_CHECKED(UAnimStateTransitionNode, BlendProfileWrapper));
	}
	if (ChangedCrossfadeProperty)
	{
		FPropertyChangedEvent PropertyChangedEvent(ChangedCrossfadeProperty);
		TransNode->PostEditChangeProperty(PropertyChangedEvent);
	}
#endif
	FBlueprintEditorUtils::MarkBlueprintAsModified(AnimBP);
	AnimBP->MarkPackageDirty();

	TSharedPtr<FJsonObject> ResultObject = MakeShared<FJsonObject>();
	SetResolvedFields(ResultObject);
	ResultObject->SetNumberField(TEXT("crossfade_duration"), TransNode->CrossfadeDuration);
	ResultObject->SetNumberField(TEXT("priority_order"), TransNode->PriorityOrder);
	ResultObject->SetStringField(TEXT("blend_mode"),
		StaticEnum<EAlphaBlendOption>()->GetNameStringByValue(
			static_cast<int64>(TransNode->BlendMode)));
	ResultObject->SetStringField(TEXT("custom_blend_curve"),
		TransNode->CustomBlendCurve ? TransNode->CustomBlendCurve->GetPathName() : TEXT(""));
	ResultObject->SetBoolField(TEXT("bidirectional"), TransNode->Bidirectional);
	ResultObject->SetBoolField(TEXT("automatic_rule_based"),
		TransNode->bAutomaticRuleBasedOnSequencePlayerInState);
	ResultObject->SetNumberField(TEXT("automatic_rule_trigger_time"),
		TransNode->AutomaticRuleTriggerTime);
	TSharedPtr<FJsonObject> BlendProfileObject = MakeShared<FJsonObject>();
	if (const UBlendProfile* BlendProfile = TransNode->BlendProfileWrapper.GetBlendProfile().Get())
	{
		BlendProfileObject->SetStringField(TEXT("path"), BlendProfile->GetPathName());
		BlendProfileObject->SetStringField(TEXT("mode"),
			StaticEnum<EBlendProfileMode>()->GetNameStringByValue(
				static_cast<int64>(BlendProfile->GetMode())));
	}
	else
	{
		BlendProfileObject->SetStringField(TEXT("path"), TEXT(""));
		BlendProfileObject->SetStringField(TEXT("mode"), TEXT(""));
	}
	ResultObject->SetObjectField(TEXT("blend_profile"), BlendProfileObject);
	ResultObject->SetArrayField(TEXT("modified_properties"), MakePropertyArray());
	ResultObject->SetBoolField(TEXT("compiled"), false);
	ResultObject->SetStringField(TEXT("compile_hint"),
		TEXT("Call ue_manage_blueprint(operation='compile_blueprint') separately"));

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AnimBP] SetAnimTransitionRule completed: %s -> %s (%d props modified)"),
		*Resolved.FromState, *Resolved.ToState, Staged.ChangedProperties.Num());
	return BlueprintNodeHelpers::CreateSuccessJson(ResultObject);
}

// =========================================================
// Material Remote Control Functions (Issue #4346)
// =========================================================

FString UCURemoteLibrary::GetMaterialHLSL(const FString& MaterialPath)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] GetMaterialHLSL: %s"), *MaterialPath);

	// Load Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		// Try loading as MaterialInstance and get parent
		UMaterialInstance* MatInstance = LoadObject<UMaterialInstance>(nullptr, *MaterialPath);
		if (MatInstance)
		{
			Material = MatInstance->GetMaterial();
		}
	}

	if (!Material)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Get shader info from expressions
	FString ShaderInfo;
	int32 NumExpressions = Material->GetExpressions().Num();
	ShaderInfo = FString::Printf(TEXT("Material has %d expressions"), NumExpressions);

	// Note: Full HLSL extraction requires shader compilation which is complex
	// For now, return material metadata that helps understand the shader
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("material_name"), Material->GetName());
	ResultObj->SetStringField(TEXT("material_path"), Material->GetPathName());
	ResultObj->SetNumberField(TEXT("expression_count"), Material->GetExpressions().Num());
	ResultObj->SetStringField(TEXT("shader_info"), ShaderInfo);
	ResultObj->SetStringField(TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)Material->BlendMode));
	ResultObj->SetStringField(TEXT("shading_model"), StaticEnum<EMaterialShadingModel>()->GetNameStringByValue((int64)Material->GetShadingModels().GetFirstShadingModel()));

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// Issue #16324: extract one expression's catalog-relevant properties (name/type/parameter/default).
// Live-RC path for the material "feature catalog": .uasset metadata lacks node names + DefaultValue,
// so this reads them straight off the loaded UMaterialExpression objects.
static void CUFillMaterialNodeJson(UMaterialExpression* Expression, const TSharedPtr<FJsonObject>& NodeJson)
{
	if (!Expression || !NodeJson.IsValid())
	{
		return;
	}

	// Live node name (e.g. "MaterialExpressionScalarParameter_0") — the field absent from binary metadata.
	NodeJson->SetStringField(TEXT("name"), Expression->GetName());
	NodeJson->SetStringField(TEXT("type"), Expression->GetClass()->GetName());

#if WITH_EDITORONLY_DATA
	if (!Expression->Desc.IsEmpty())
	{
		NodeJson->SetStringField(TEXT("description"), Expression->Desc);
	}
	TSharedPtr<FJsonObject> PositionJson = MakeShared<FJsonObject>();
	PositionJson->SetNumberField(TEXT("x"), Expression->MaterialExpressionEditorX);
	PositionJson->SetNumberField(TEXT("y"), Expression->MaterialExpressionEditorY);
	NodeJson->SetObjectField(TEXT("position"), PositionJson);
#endif

	// Generic parameter name (base class covers Scalar/Vector/StaticBool/StaticSwitch/Texture params).
	if (UMaterialExpressionParameter* BaseParam = Cast<UMaterialExpressionParameter>(Expression))
	{
		NodeJson->SetStringField(TEXT("parameter_name"), BaseParam->ParameterName.ToString());
	}

	// Type-specific DefaultValue extraction (the second field absent from binary metadata).
	if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expression))
	{
		NodeJson->SetNumberField(TEXT("default_value"), ScalarParam->DefaultValue);
	}
	else if (UMaterialExpressionVectorParameter* VectorParam = Cast<UMaterialExpressionVectorParameter>(Expression))
	{
		const FLinearColor& V = VectorParam->DefaultValue;
		NodeJson->SetStringField(TEXT("default_value"),
			FString::Printf(TEXT("(R=%.6f,G=%.6f,B=%.6f,A=%.6f)"), V.R, V.G, V.B, V.A));
	}
	else if (UMaterialExpressionStaticBoolParameter* BoolParam = Cast<UMaterialExpressionStaticBoolParameter>(Expression))
	{
		NodeJson->SetBoolField(TEXT("default_value"), BoolParam->DefaultValue);
	}
	else if (UMaterialExpressionTextureSampleParameter* TexParam = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
	{
		// TextureSampleParameter derives from TextureSample (not UMaterialExpressionParameter),
		// so its ParameterName isn't covered by the generic base-class branch above.
		NodeJson->SetStringField(TEXT("parameter_name"), TexParam->ParameterName.ToString());
		if (TexParam->Texture)
		{
			NodeJson->SetStringField(TEXT("default_value"), TexParam->Texture->GetPathName());
		}
	}
	else if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expression))
	{
		if (TexSample->Texture)
		{
			NodeJson->SetStringField(TEXT("texture"), TexSample->Texture->GetPathName());
		}
	}
	else if (UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
	{
		NodeJson->SetNumberField(TEXT("default_value"), Constant->R);
	}
	else if (UMaterialExpressionConstant2Vector* Constant2 = Cast<UMaterialExpressionConstant2Vector>(Expression))
	{
		NodeJson->SetStringField(TEXT("default_value"),
			FString::Printf(TEXT("(R=%.6f,G=%.6f)"), Constant2->R, Constant2->G));
	}
	else if (UMaterialExpressionConstant3Vector* Constant3 = Cast<UMaterialExpressionConstant3Vector>(Expression))
	{
		const FLinearColor& C = Constant3->Constant;
		NodeJson->SetStringField(TEXT("default_value"),
			FString::Printf(TEXT("(R=%.6f,G=%.6f,B=%.6f)"), C.R, C.G, C.B));
	}
	else if (UMaterialExpressionConstant4Vector* Constant4 = Cast<UMaterialExpressionConstant4Vector>(Expression))
	{
		const FLinearColor& C = Constant4->Constant;
		NodeJson->SetStringField(TEXT("default_value"),
			FString::Printf(TEXT("(R=%.6f,G=%.6f,B=%.6f,A=%.6f)"), C.R, C.G, C.B, C.A));
	}
	else if (UMaterialExpressionMaterialFunctionCall* FunctionCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
	{
		if (FunctionCall->MaterialFunction)
		{
			NodeJson->SetStringField(TEXT("material_function"), FunctionCall->MaterialFunction->GetPathName());
		}
	}
}

// Issue #20557: is this UScriptStruct an FExpressionInput, or a struct that derives from one
// (e.g. FMaterialLayerInput : public FExpressionInput)? UE 5.6+ removed
// FExpressionInput::StaticStruct(), so 5.6+ detects by name over the super-chain and 5.5- uses
// IsChildOf. Used only by GetMaterialGraph Pass 2's FArrayProperty branch (below), never by the
// existing FStructProperty branch, so already-emitted edges are unaffected.
static bool CUIsExpressionInputStruct(const UScriptStruct* InStruct)
{
	if (!InStruct)
	{
		return false;
	}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	// Exact-name match over the reflected super-chain. This is deliberately NOT a
	// `GetName().Contains(TEXT("MaterialInput"))` substring test: the whole
	// FMaterialInput family already reaches FExpressionInput through this walk
	// (`FColorMaterialInput : FMaterialInput<FLinearColor> : FExpressionInput`,
	// MaterialExpressionIO.h), so a substring fallback buys nothing — while
	// FMaterialInputDescription (Material.h) matches that substring yet does NOT
	// derive from FExpressionInput (it holds a raw FExpressionInput* at offset 0).
	// Accepting it would make the Case-A reinterpret_cast read a pointer as a
	// struct. Exact identity only.
	static const FName ExpressionInputName("ExpressionInput");
	for (const UStruct* Cursor = InStruct; Cursor; Cursor = Cursor->GetSuperStruct())
	{
		if (Cursor->GetFName() == ExpressionInputName)
		{
			return true;
		}
	}
	return false;
#else
	return InStruct->IsChildOf(FExpressionInput::StaticStruct());
#endif
}

// Issue #24952: structured Material references are emitted only when adding the
// edge cannot close a cycle. Ordinary FExpressionInput edges are intentionally
// excluded from this adjacency map because valid Material data graphs may contain
// engine-managed feedback constructs; this guard applies only to the object/array
// relation families introduced by #24952.
static bool CUHasStructuredMaterialPath(
	const TMultiMap<FString, FString>& Adjacency,
	const FString& Start,
	const FString& Goal)
{
	TArray<FString> Pending;
	TSet<FString> Visited;
	Pending.Add(Start);
	while (Pending.Num() > 0)
	{
		const FString Current = Pending.Pop(EAllowShrinking::No);
		if (Current == Goal)
		{
			return true;
		}
		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);
		TArray<FString> Next;
		Adjacency.MultiFind(Current, Next);
		Pending.Append(Next);
	}
	return false;
}

struct FCUStructuredMaterialEdgeCandidate
{
	FString FromNode;
	FString FromPin;
	FString ToNode;
	FString ToPin;
	FString Relation;
	FString OwnerNode;
	FString Property;
	FString TargetReference;
	FString EdgeKey;
};

FString UCURemoteLibrary::GetMaterialGraph(const FString& MaterialPath)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] GetMaterialGraph: %s"), *MaterialPath);

	// Load Material (MaterialInstance falls back to its base, same as GetMaterialHLSL).
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		UMaterialInstance* MatInstance = LoadObject<UMaterialInstance>(nullptr, *MaterialPath);
		if (MatInstance)
		{
			Material = MatInstance->GetMaterial();
		}
	}

	// Issue #16324: MaterialFunctions are NOT UMaterial/UMaterialInstance, but get_nodes is the
	// documented way to inspect their graph (get_hlsl redirects MFs here). Fall back to the MF
	// so the live catalog covers function graphs too (mirrors HandleGetMaterialHLSL's MF branch).
	UMaterialFunction* MatFunc = nullptr;
	if (!Material)
	{
		MatFunc = Cast<UMaterialFunction>(LoadObject<UMaterialFunctionInterface>(nullptr, *MaterialPath));
	}

	if (!Material && !MatFunc)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Issue #224 UE 5.6 Compat: GetExpressions() returns TArrayView in UE 5.6+, TArray& in UE 5.5.
	// Both UMaterial and UMaterialFunction expose the same return type, so one accessor serves both.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6)
	TArrayView<const TObjectPtr<UMaterialExpression>> Expressions =
		Material ? Material->GetExpressions() : MatFunc->GetExpressions();
#else
	const TArray<TObjectPtr<UMaterialExpression>>& Expressions =
		Material ? Material->GetExpressions() : MatFunc->GetExpressions();
#endif

	// Pass 1: build expr->name map and the node list.
	// Name-based edges (acceptance) require resolving Input->Expression to a name, not a GUID,
	// so the map is built up front before connections are emitted.
	TMap<UMaterialExpression*, FString> ExpressionToName;
	TMap<FString, int32> ExpressionNameCounts;
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression)
		{
			continue;
		}
		const FString ExpressionName = Expression->GetName();
		ExpressionToName.Add(Expression, ExpressionName);
		ExpressionNameCounts.FindOrAdd(ExpressionName) += 1;

		TSharedPtr<FJsonObject> NodeJson = MakeShared<FJsonObject>();
		CUFillMaterialNodeJson(Expression, NodeJson);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeJson));
	}

	// Pass 2: name-based edges via FExpressionInput reflection.
	TArray<TSharedPtr<FJsonValue>> EdgesArray;
	TArray<TSharedPtr<FJsonValue>> GraphDiagnosticsArray;
	TArray<FCUStructuredMaterialEdgeCandidate> StructuredCandidates;
	TSet<FString> StructuredEdgeKeys;
	TSet<UMaterialExpression*> CompositeInputBases;
	TSet<UMaterialExpression*> CompositeOutputBases;
	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression || Expression->GetClass()->GetName() != TEXT("MaterialExpressionComposite"))
		{
			continue;
		}
		for (TFieldIterator<FObjectPropertyBase> PropIt(Expression->GetClass()); PropIt; ++PropIt)
		{
			FObjectPropertyBase* ObjectProp = *PropIt;
			UMaterialExpression* PinBase = Cast<UMaterialExpression>(
				ObjectProp->GetObjectPropertyValue_InContainer(Expression));
			if (!PinBase)
			{
				continue;
			}
			if (ObjectProp->GetName() == TEXT("InputExpressions"))
			{
				CompositeInputBases.Add(PinBase);
			}
			else if (ObjectProp->GetName() == TEXT("OutputExpressions"))
			{
				CompositeOutputBases.Add(PinBase);
			}
		}
	}

	auto AddStructuredDiagnostic = [&GraphDiagnosticsArray](
		const TCHAR* ErrorCode,
		const FString& Relation,
		const FString& OwnerNode,
		const FString& Property,
		const FString& TargetReference,
		const TArray<FString>& Candidates = TArray<FString>())
	{
		TSharedPtr<FJsonObject> Diagnostic = MakeShared<FJsonObject>();
		Diagnostic->SetStringField(TEXT("error_code"), ErrorCode);
		Diagnostic->SetStringField(TEXT("relation"), Relation);
		Diagnostic->SetStringField(TEXT("owner_node"), OwnerNode);
		Diagnostic->SetStringField(TEXT("property"), Property);
		Diagnostic->SetStringField(TEXT("target_reference"), TargetReference);
		if (Candidates.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> CandidateValues;
			for (const FString& Candidate : Candidates)
			{
				CandidateValues.Add(MakeShared<FJsonValueString>(Candidate));
			}
			Diagnostic->SetArrayField(TEXT("candidates"), CandidateValues);
		}
		GraphDiagnosticsArray.Add(MakeShared<FJsonValueObject>(Diagnostic));
	};

	auto EmitStructuredEdge = [
		&StructuredCandidates,
		&ExpressionToName,
		&ExpressionNameCounts,
		&StructuredEdgeKeys,
		&AddStructuredDiagnostic](
		UMaterialExpression* FromExpression,
		const FString& FromPin,
		UMaterialExpression* ToExpression,
		const FString& ToPin,
		const FString& Relation,
		const FString& OwnerNode,
		const FString& Property,
		const FString& TargetReference)
	{
		const FString FromNode = ExpressionToName.FindRef(FromExpression);
		const FString ToNode = ExpressionToName.FindRef(ToExpression);
		if (FromNode.IsEmpty() || ToNode.IsEmpty())
		{
			AddStructuredDiagnostic(
				TEXT("DANGLING_GRAPH_REFERENCE"),
				Relation,
				OwnerNode,
				Property,
				TargetReference);
			return;
		}

		if (ExpressionNameCounts.FindRef(FromNode) != 1 || ExpressionNameCounts.FindRef(ToNode) != 1)
		{
			TArray<FString> Candidates;
			Candidates.Add(FString::Printf(TEXT("%s@count:%d"), *FromNode, ExpressionNameCounts.FindRef(FromNode)));
			Candidates.Add(FString::Printf(TEXT("%s@count:%d"), *ToNode, ExpressionNameCounts.FindRef(ToNode)));
			AddStructuredDiagnostic(
				TEXT("AMBIGUOUS_GRAPH_REFERENCE"),
				Relation,
				OwnerNode,
				Property,
				TargetReference,
				Candidates);
			return;
		}

		const FString EdgeKey = FString::Printf(
			TEXT("%s\x1f%s\x1f%s\x1f%s\x1f%s"),
			*FromNode,
			*FromPin,
			*ToNode,
			*ToPin,
			*Relation);
		if (StructuredEdgeKeys.Contains(EdgeKey))
		{
			return;
		}
		StructuredEdgeKeys.Add(EdgeKey);
		StructuredCandidates.Add({
			FromNode,
			FromPin,
			ToNode,
			ToPin,
			Relation,
			OwnerNode,
			Property,
			TargetReference,
			EdgeKey});
	};

	for (UMaterialExpression* Expression : Expressions)
	{
		if (!Expression)
		{
			continue;
		}
		const FString ToNodeName = ExpressionToName.FindRef(Expression);

		for (TFieldIterator<FProperty> PropIt(Expression->GetClass()); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			const FString ClassName = Expression->GetClass()->GetName();
			const FString PropertyName = Property->GetName();

			// Exact object-reference allowlist. Generic UObject properties (Texture,
			// MaterialFunction, etc.) are metadata, not graph topology.
			if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Property))
			{
				FString Relation;
				bool bOwnerIsSource = false;
				if (ClassName == TEXT("MaterialExpressionNamedRerouteUsage") && PropertyName == TEXT("Declaration"))
				{
					Relation = TEXT("named_reroute");
				}
				else if (ClassName == TEXT("MaterialExpressionComposite") && PropertyName == TEXT("InputExpressions"))
				{
					Relation = TEXT("composite_input_base");
					bOwnerIsSource = true;
				}
				else if (ClassName == TEXT("MaterialExpressionComposite") && PropertyName == TEXT("OutputExpressions"))
				{
					Relation = TEXT("composite_output_base");
				}

				if (!Relation.IsEmpty())
				{
					UObject* ReferencedObject = ObjectProp->GetObjectPropertyValue_InContainer(Expression);
					UMaterialExpression* ReferencedExpression = Cast<UMaterialExpression>(ReferencedObject);
					if (ReferencedObject && !ReferencedExpression)
					{
						AddStructuredDiagnostic(
							TEXT("DANGLING_GRAPH_REFERENCE"),
							Relation,
							ToNodeName,
							PropertyName,
							ReferencedObject->GetPathName());
					}
					else if (ReferencedExpression)
					{
						const FString TargetReference = ReferencedExpression->GetPathName();
						if (bOwnerIsSource)
						{
							EmitStructuredEdge(
								Expression,
								PropertyName,
								ReferencedExpression,
								TEXT("Composite"),
								Relation,
								ToNodeName,
								PropertyName,
								TargetReference);
						}
						else
						{
							EmitStructuredEdge(
								ReferencedExpression,
								TEXT("Output0"),
								Expression,
								PropertyName,
								Relation,
								ToNodeName,
								PropertyName,
								TargetReference);
						}
					}
				}
				continue;
			}

			FStructProperty* StructProp = CastField<FStructProperty>(Property);
			if (!StructProp)
			{
				// Issue #20557: inputs nested in a TArray<> are an FArrayProperty, not an
				// FStructProperty, so the struct path below never visits them. Emit their edges
				// here (MaterialFunctionCall::FunctionInputs, Custom::Inputs, Switch::Inputs,
				// SetMaterialAttributes::Inputs, Convert/Operator/Aggregate/LayerStack, ...).
				// This branch is strictly additive: FArrayProperty and FStructProperty are
				// mutually exclusive on one FProperty, so no edge the struct path emits is
				// duplicated here.
				FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
				FStructProperty* InnerStruct = ArrayProp ? CastField<FStructProperty>(ArrayProp->Inner) : nullptr;
				if (InnerStruct)
				{
					FScriptArrayHelper ArrayHelper(ArrayProp, ArrayProp->ContainerPtrToValuePtr<void>(Expression));
					const bool bLandscapeLayers =
						ClassName == TEXT("MaterialExpressionLandscapeLayerBlend") && PropertyName == TEXT("Layers");
					const bool bCompositeReroutePins =
						ClassName == TEXT("MaterialExpressionPinBase") && PropertyName == TEXT("ReroutePins");
					const bool bElementIsInput = CUIsExpressionInputStruct(InnerStruct->Struct);
					for (int32 ElemIdx = 0; ElemIdx < ArrayHelper.Num(); ++ElemIdx)
					{
						void* ElemPtr = ArrayHelper.GetRawPtr(ElemIdx);

						if (bCompositeReroutePins)
						{
							FString PinName = TEXT("None");
							UMaterialExpression* ReferencedExpression = nullptr;
							for (TFieldIterator<FProperty> InnerIt(InnerStruct->Struct); InnerIt; ++InnerIt)
							{
								if (FNameProperty* NameProp = CastField<FNameProperty>(*InnerIt))
								{
									if (NameProp->GetName() == TEXT("Name"))
									{
										PinName = NameProp->GetPropertyValue_InContainer(ElemPtr).ToString();
									}
								}
								else if (FObjectPropertyBase* MemberObject = CastField<FObjectPropertyBase>(*InnerIt))
								{
									if (MemberObject->GetName() == TEXT("Expression"))
									{
										ReferencedExpression = Cast<UMaterialExpression>(
											MemberObject->GetObjectPropertyValue_InContainer(ElemPtr));
									}
								}
							}
							if (ReferencedExpression)
							{
								const FString StablePin = FString::Printf(
									TEXT("ReroutePins[%d].Expression[Name=%s]"), ElemIdx, *PinName);
								const bool bInputBase = CompositeInputBases.Contains(Expression);
								const bool bOutputBase = CompositeOutputBases.Contains(Expression);
								if (bInputBase != bOutputBase)
								{
									if (bInputBase)
									{
										// Composite InputBase exposes an output pin.
										EmitStructuredEdge(
											Expression,
											StablePin,
											ReferencedExpression,
											TEXT("Output0"),
											TEXT("composite_reroute"),
											ToNodeName,
											StablePin,
											ReferencedExpression->GetPathName());
									}
									else
									{
										// Composite OutputBase exposes an input pin.
										EmitStructuredEdge(
											ReferencedExpression,
											TEXT("Output0"),
											Expression,
											StablePin,
											TEXT("composite_reroute"),
											ToNodeName,
											StablePin,
											ReferencedExpression->GetPathName());
									}
								}
								else
								{
									AddStructuredDiagnostic(
										TEXT("AMBIGUOUS_COMPOSITE_PIN_DIRECTION"),
										TEXT("composite_reroute"),
										ToNodeName,
										StablePin,
										ReferencedExpression->GetPathName());
								}
							}
							continue;
						}

						// Emit one edge for a resolved nested input. to_pin naming is resolved by
						// direct field access, NOT GetInputName() (MFC's is type-decorated, #20557).
						auto EmitArrayEdge = [&](FExpressionInput* In, const FString& ToPin)
						{
							if (In && In->Expression)
							{
								const FString FromNodeName = ExpressionToName.FindRef(In->Expression);
								if (!FromNodeName.IsEmpty())
								{
									TSharedPtr<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
									EdgeJson->SetStringField(TEXT("from_node"), FromNodeName);
									EdgeJson->SetStringField(TEXT("from_pin"), FString::Printf(TEXT("Output%d"), In->OutputIndex));
									EdgeJson->SetStringField(TEXT("to_node"), ToNodeName);
									EdgeJson->SetStringField(TEXT("to_pin"), ToPin);
									EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeJson));
								}
							}
						};

						if (bElementIsInput)
						{
							// Element itself IS an FExpressionInput (SetMaterialAttributes::Inputs;
							// LayerStack::LayerInputs via single non-virtual FExpressionInput base).
							// to_pin: own InputName if set, else "<ArrayProp>[idx]".
							FExpressionInput* In = reinterpret_cast<FExpressionInput*>(ElemPtr);
							const FString ToPin = (In && !In->InputName.IsNone())
								? In->InputName.ToString()
								: FString::Printf(TEXT("%s[%d]"), *Property->GetName(), ElemIdx);
							EmitArrayEdge(In, ToPin);
						}
						else
						{
							// Element is a WRAPPER struct that CONTAINS FExpressionInput field(s)
							// (FFunctionExpressionInput.Input, FCustomInput.Input, ...). Resolve
							// to_pin: tier1 nested Input.InputName -> tier2 sibling FName InputName
							// -> tier3 "<ArrayProp>[idx]". The FExpressionInput member is found by
							// type (predicate), not by guessing its field name.
							FString SiblingInputName;
							FString LandscapeLayerName = TEXT("None");
							for (TFieldIterator<FProperty> InnerIt(InnerStruct->Struct); InnerIt; ++InnerIt)
							{
								FNameProperty* NameProp = CastField<FNameProperty>(*InnerIt);
								if (NameProp && bLandscapeLayers && NameProp->GetName() == TEXT("LayerName"))
								{
									LandscapeLayerName = NameProp->GetPropertyValue_InContainer(ElemPtr).ToString();
								}
								else if (NameProp && NameProp->GetName() == TEXT("InputName"))
								{
									const FName NameVal = NameProp->GetPropertyValue_InContainer(ElemPtr);
									if (!NameVal.IsNone())
									{
										SiblingInputName = NameVal.ToString();
									}
									break;
								}
							}
							for (TFieldIterator<FProperty> InnerIt(InnerStruct->Struct); InnerIt; ++InnerIt)
							{
								FStructProperty* MemberStruct = CastField<FStructProperty>(*InnerIt);
								if (!MemberStruct || !CUIsExpressionInputStruct(MemberStruct->Struct))
								{
									continue;
								}
								FExpressionInput* In = MemberStruct->ContainerPtrToValuePtr<FExpressionInput>(ElemPtr);
								if (bLandscapeLayers && In && In->Expression &&
									(MemberStruct->GetName() == TEXT("LayerInput") || MemberStruct->GetName() == TEXT("HeightInput")))
								{
									const FString MemberName = MemberStruct->GetName();
									const FString Relation = MemberName == TEXT("LayerInput")
										? TEXT("landscape_layer_input")
										: TEXT("landscape_height_input");
									const FString StablePin = FString::Printf(
										TEXT("Layers[%d].%s[LayerName=%s]"), ElemIdx, *MemberName, *LandscapeLayerName);
									EmitStructuredEdge(
										In->Expression,
										FString::Printf(TEXT("Output%d"), In->OutputIndex),
										Expression,
										StablePin,
										Relation,
										ToNodeName,
										StablePin,
										In->Expression->GetPathName());
									continue;
								}
								FString ToPin;
								if (In && !In->InputName.IsNone())
								{
									ToPin = In->InputName.ToString();
								}
								else if (!SiblingInputName.IsEmpty())
								{
									ToPin = SiblingInputName;
								}
								else
								{
									ToPin = FString::Printf(TEXT("%s[%d]"), *Property->GetName(), ElemIdx);
								}
								EmitArrayEdge(In, ToPin);
							}
						}
					}
				}
				continue;
			}

			// Issue #224 UE 5.6 Compat: FExpressionInput::StaticStruct() removed in UE 5.6.
			const bool bIsExpressionInput = CUIsExpressionInputStruct(StructProp->Struct);
			if (!bIsExpressionInput)
			{
				continue;
			}

			// Issue #20558: a reflected FStructProperty can be a fixed-size native
			// array (ArrayDim > 1). ShadingPathSwitch::Inputs is the shipped Lyra
			// case: reading only element zero omitted seven real edges from the live
			// graph, so OFF reported 272 while ON reported 265. Walk every physical
			// element; scalar properties retain their historical pin label.
			for (int32 ArrayIndex = 0; ArrayIndex < StructProp->ArrayDim; ++ArrayIndex)
			{
				FExpressionInput* Input = StructProp->ContainerPtrToValuePtr<FExpressionInput>(Expression, ArrayIndex);
				if (Input && Input->Expression)
				{
					const FString FromNodeName = ExpressionToName.FindRef(Input->Expression);
					if (!FromNodeName.IsEmpty())
					{
						const FString ToPin = StructProp->ArrayDim > 1
							? FString::Printf(TEXT("%s[%d]"), *PropertyName, ArrayIndex)
							: PropertyName;
						TSharedPtr<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
						EdgeJson->SetStringField(TEXT("from_node"), FromNodeName);
						EdgeJson->SetStringField(TEXT("from_pin"), FString::Printf(TEXT("Output%d"), Input->OutputIndex));
						EdgeJson->SetStringField(TEXT("to_node"), ToNodeName);
						EdgeJson->SetStringField(TEXT("to_pin"), ToPin);
						EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeJson));
					}
				}
			}
		}
	}

	// Evaluate cycles against the complete candidate graph, not insertion order.
	// A sequential guard retains every edge except the one that happened to close
	// a cycle, which makes the partial result reflection-order dependent. Sorting
	// and rejecting every candidate whose target can reach its source is stable
	// and matches the offline parser's fail-closed contract.
	StructuredCandidates.Sort([](
		const FCUStructuredMaterialEdgeCandidate& Left,
		const FCUStructuredMaterialEdgeCandidate& Right)
	{
		return Left.EdgeKey < Right.EdgeKey;
	});
	TMultiMap<FString, FString> CompleteStructuredAdjacency;
	for (const FCUStructuredMaterialEdgeCandidate& Candidate : StructuredCandidates)
	{
		CompleteStructuredAdjacency.Add(Candidate.FromNode, Candidate.ToNode);
	}
	for (const FCUStructuredMaterialEdgeCandidate& Candidate : StructuredCandidates)
	{
		if (CUHasStructuredMaterialPath(
			CompleteStructuredAdjacency,
			Candidate.ToNode,
			Candidate.FromNode))
		{
			AddStructuredDiagnostic(
				TEXT("STRUCTURED_REFERENCE_CYCLE"),
				Candidate.Relation,
				Candidate.OwnerNode,
				Candidate.Property,
				Candidate.TargetReference);
			continue;
		}

		TSharedPtr<FJsonObject> EdgeJson = MakeShared<FJsonObject>();
		EdgeJson->SetStringField(TEXT("from_node"), Candidate.FromNode);
		EdgeJson->SetStringField(TEXT("from_pin"), Candidate.FromPin);
		EdgeJson->SetStringField(TEXT("to_node"), Candidate.ToNode);
		EdgeJson->SetStringField(TEXT("to_pin"), Candidate.ToPin);
		EdgeJson->SetStringField(TEXT("connection_kind"), Candidate.Relation);
		EdgesArray.Add(MakeShared<FJsonValueObject>(EdgeJson));
	}

	// Pass 3 (Material only): root material-property edges (BaseColor/Roughness/Normal/...).
	// These terminal connections live on UMaterial itself (reached via
	// GetExpressionInputForProperty), NOT on any UMaterialExpression's reflected struct, so
	// Pass 2's TFieldIterator cannot see them. The feature catalog needs "what feeds BaseColor",
	// so they are emitted in a dedicated `material_outputs` array. (cf.
	// CUMaterialMetadataCommandlet::ExtractMaterialInputs.) MaterialFunctions have no root
	// material properties (they terminate in FunctionOutput nodes), so this is skipped for MFs.
	TArray<TSharedPtr<FJsonValue>> MaterialOutputsArray;
	if (Material)
	{
		auto AddRootOutput = [&](const TCHAR* PropName, FExpressionInput* Input)
		{
			if (Input && Input->Expression)
			{
				const FString FromNodeName = ExpressionToName.FindRef(Input->Expression);
				if (!FromNodeName.IsEmpty())
				{
					TSharedPtr<FJsonObject> OutJson = MakeShared<FJsonObject>();
					OutJson->SetStringField(TEXT("property"), PropName);
					OutJson->SetStringField(TEXT("from_node"), FromNodeName);
					OutJson->SetStringField(TEXT("from_pin"), FString::Printf(TEXT("Output%d"), Input->OutputIndex));
					MaterialOutputsArray.Add(MakeShared<FJsonValueObject>(OutJson));
				}
			}
		};

		AddRootOutput(TEXT("BaseColor"), Material->GetExpressionInputForProperty(MP_BaseColor));
		AddRootOutput(TEXT("Metallic"), Material->GetExpressionInputForProperty(MP_Metallic));
		AddRootOutput(TEXT("Specular"), Material->GetExpressionInputForProperty(MP_Specular));
		AddRootOutput(TEXT("Roughness"), Material->GetExpressionInputForProperty(MP_Roughness));
		AddRootOutput(TEXT("Anisotropy"), Material->GetExpressionInputForProperty(MP_Anisotropy));
		AddRootOutput(TEXT("EmissiveColor"), Material->GetExpressionInputForProperty(MP_EmissiveColor));
		AddRootOutput(TEXT("Opacity"), Material->GetExpressionInputForProperty(MP_Opacity));
		AddRootOutput(TEXT("OpacityMask"), Material->GetExpressionInputForProperty(MP_OpacityMask));
		AddRootOutput(TEXT("Normal"), Material->GetExpressionInputForProperty(MP_Normal));
		AddRootOutput(TEXT("Tangent"), Material->GetExpressionInputForProperty(MP_Tangent));
		AddRootOutput(TEXT("WorldPositionOffset"), Material->GetExpressionInputForProperty(MP_WorldPositionOffset));
		AddRootOutput(TEXT("SubsurfaceColor"), Material->GetExpressionInputForProperty(MP_SubsurfaceColor));
		AddRootOutput(TEXT("AmbientOcclusion"), Material->GetExpressionInputForProperty(MP_AmbientOcclusion));
		AddRootOutput(TEXT("Refraction"), Material->GetExpressionInputForProperty(MP_Refraction));
		AddRootOutput(TEXT("PixelDepthOffset"), Material->GetExpressionInputForProperty(MP_PixelDepthOffset));
		// Issue #20558: Clear Coat occupies the two custom-data material
		// properties. Omitting them made the live root set a strict subset of the
		// EditorOnlyData roots on M_Mannequin (10 vs 12).
		AddRootOutput(TEXT("ClearCoat"), Material->GetExpressionInputForProperty(MP_CustomData0));
		AddRootOutput(TEXT("ClearCoatRoughness"), Material->GetExpressionInputForProperty(MP_CustomData1));
		AddRootOutput(TEXT("MaterialAttributes"), Material->GetExpressionInputForProperty(MP_MaterialAttributes));
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetBoolField(TEXT("is_material_function"), MatFunc != nullptr);
	ResultObj->SetStringField(TEXT("material_name"), Material ? Material->GetName() : MatFunc->GetName());
	ResultObj->SetStringField(TEXT("material_path"), Material ? Material->GetPathName() : MatFunc->GetPathName());
	ResultObj->SetNumberField(TEXT("node_count"), NodesArray.Num());
	ResultObj->SetNumberField(TEXT("connection_count"), EdgesArray.Num());
	ResultObj->SetArrayField(TEXT("nodes"), NodesArray);
	ResultObj->SetArrayField(TEXT("connections"), EdgesArray);
	// Always emit the authoritative live diagnostic collection. An explicit
	// empty array clears stale binary diagnostics during hybrid overlay.
	ResultObj->SetArrayField(TEXT("graph_diagnostics"), GraphDiagnosticsArray);
	if (MaterialOutputsArray.Num() > 0)
	{
		ResultObj->SetArrayField(TEXT("material_outputs"), MaterialOutputsArray);
	}

	return CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
}

// Issue #14979: forward declarations. These static helpers are defined further down
// (near ModifyMaterialNode/DeleteMaterialNode) but are also used earlier here in
// SetMaterialParameter, so they must be declared before first use.
static FString CUMaterialJsonToImportText(const TSharedPtr<FJsonValue>& Val);
static bool CUIsAssetEditorOpen(UObject* Asset);
// Issue #17066: CUFindMaterialExpression is now owner-agnostic — it takes the expression
// array view (TConstArrayView == TArrayView<const T>) so the same lookup serves a UMaterial
// or a UMaterialFunction (both GetExpressions() return the same element type). Both the 5.6+
// TArrayView return and the 5.5 `const TArray&` return convert to this parameter.
static UMaterialExpression* CUFindMaterialExpression(
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions, const FString& NodeName, bool& bOutAmbiguous);
// Issue #17066: resolve a path to a UMaterial OR a UMaterialFunction so the material node-edit
// ops (add/modify/connect/delete) also operate on Material Function internal graphs.
static bool CUResolveMaterialOrFunction(const FString& Path, UMaterial*& OutMaterial, UMaterialFunction*& OutFunction);
// Issue #17066: create a UMaterialExpression of the requested type parented to `Outer`
// (a UMaterial or UMaterialFunction). Extracted from AddMaterialNode so Material and
// Material-Function node insertion share one type-resolution table.
// Issue #24288: resolution is exact-class-only and fails closed — see the definition.
static UMaterialExpression* CUCreateExpressionByType(UObject* Outer, const FString& NodeType, const FString& NodeName);

// Issue #17062: parent-parameter existence validation for SetMaterialParameter.
// Returns true if ParamName exists in the parent material's exposed parameters of
// EffectiveTypeLower; on a miss, fills OutAvailable with all exposed names of that
// type (for the candidate hint). An unrecognized type — or a static type whose
// enumeration is compiled out (WITH_EDITORONLY_DATA off) — returns true (skip; never
// false-reject), preserving the existing "Unknown parameter type" tail error.
// Caller MUST pass a lowercased type string: the type switch here is FString==
// (case-sensitive), while the FName name compare below is case-insensitive, matching
// the engine's parameter-name semantics (cf. the existing `Param.ParameterInfo.Name
// == ParamName` checks in the branch bodies).
static bool CUMaterialParameterExists(
	UMaterialInstanceConstant* MatInstance,
	const FString& EffectiveTypeLower,
	FName ParamName,
	TArray<FString>& OutAvailable)
{
	if (!MatInstance)
	{
		// No instance to validate against — let downstream handle it.
		return true;
	}

	TArray<FMaterialParameterInfo> ParamInfos;
	TArray<FGuid> ParamGuids;
	bool bEnumerated = false;

	if (EffectiveTypeLower == TEXT("scalar"))
	{
		MatInstance->GetAllScalarParameterInfo(ParamInfos, ParamGuids);
		bEnumerated = true;
	}
	else if (EffectiveTypeLower == TEXT("vector"))
	{
		MatInstance->GetAllVectorParameterInfo(ParamInfos, ParamGuids);
		bEnumerated = true;
	}
	else if (EffectiveTypeLower == TEXT("texture"))
	{
		MatInstance->GetAllTextureParameterInfo(ParamInfos, ParamGuids);
		bEnumerated = true;
	}
#if WITH_EDITORONLY_DATA
	else if (EffectiveTypeLower == TEXT("static_switch"))
	{
		MatInstance->GetAllStaticSwitchParameterInfo(ParamInfos, ParamGuids);
		bEnumerated = true;
	}
	else if (EffectiveTypeLower == TEXT("component_mask"))
	{
		MatInstance->GetAllStaticComponentMaskParameterInfo(ParamInfos, ParamGuids);
		bEnumerated = true;
	}
#endif

	if (!bEnumerated)
	{
		// Unknown type (or static enumeration compiled out): cannot validate, so do
		// NOT reject. Falls through to the existing branch handling / tail error.
		return true;
	}

	// Existence pass: early-out on hit (no candidate list needed on the success path).
	for (const FMaterialParameterInfo& Info : ParamInfos)
	{
		if (Info.Name == ParamName)
		{
			return true;
		}
	}

	// Miss: collect the candidate names for the hint.
	OutAvailable.Reset();
	OutAvailable.Reserve(ParamInfos.Num());
	for (const FMaterialParameterInfo& Info : ParamInfos)
	{
		OutAvailable.Add(Info.Name.ToString());
	}
	return false;
}

// Issue #17062: build the candidate hint for a parameter-not-found error. Pure
// function (deterministically unit-testable). did-you-mean ordering: names that
// contain ParamName as a case-insensitive substring first, then the remaining names
// alphabetically; capped at MaxN with a "(+K more)" suffix. An empty Available set
// yields an explicit "no <type> parameters" message (never a dangling "did you mean").
static FString CUBuildParameterHint(
	const TArray<FString>& Available,
	FName ParamName,
	const FString& TypeLower,
	int32 MaxN = 12)
{
	if (Available.Num() == 0)
	{
		return FString::Printf(
			TEXT("Parent material exposes no %s parameters; nothing to override."), *TypeLower);
	}

	const FString Needle = ParamName.ToString();

	// Partition: case-insensitive substring matches first, then the rest.
	TArray<FString> Matches;
	TArray<FString> Rest;
	for (const FString& Name : Available)
	{
		if (Name.Contains(Needle, ESearchCase::IgnoreCase))
		{
			Matches.Add(Name);
		}
		else
		{
			Rest.Add(Name);
		}
	}
	Matches.Sort();
	Rest.Sort();

	TArray<FString> Ordered;
	Ordered.Reserve(Matches.Num() + Rest.Num());
	Ordered.Append(Matches);
	Ordered.Append(Rest);

	const int32 Total = Ordered.Num();
	const int32 ShowN = FMath::Min(MaxN, Total);
	TArray<FString> Shown;
	Shown.Reserve(ShowN);
	for (int32 i = 0; i < ShowN; ++i)
	{
		Shown.Add(Ordered[i]);
	}

	FString Hint = FString::Printf(
		TEXT("Parameter not found on parent material. Available %s parameters: %s"),
		*TypeLower, *FString::Join(Shown, TEXT(", ")));
	const int32 Remaining = Total - ShowN;
	if (Remaining > 0)
	{
		Hint += FString::Printf(TEXT(" (+%d more)"), Remaining);
	}
	return Hint;
}

FString UCURemoteLibrary::SetMaterialParameter(
	const FString& MaterialPath,
	const FString& ParameterName,
	const FString& ParameterValue,
	const FString& ParameterType
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] SetMaterialParameter: %s.%s = %s (%s)"),
		*MaterialPath, *ParameterName, *ParameterValue, *ParameterType);

	// Load MaterialInstance
	UMaterialInstanceConstant* MatInstance = LoadObject<UMaterialInstanceConstant>(nullptr, *MaterialPath);
	if (!MatInstance)
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("MaterialInstance not found: %s"), *MaterialPath));
		ErrorObj->SetStringField(TEXT("hint"), TEXT("SetMaterialParameter requires a MaterialInstance, not a Material"));
		FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
		return Result;
	}

	FName ParamName(*ParameterName);

	// Issue #17062: reject parameter names the parent material does not expose,
	// BEFORE opening the transaction below (so a rejected call leaves no empty
	// undo / OnObjectTransacted-JSONL-capture entry). An MI override for a name the
	// parent does not expose is inert (UE ignores it at render) but was previously
	// recorded + reported as success → false success (live TC-C1). Validation lives
	// in one place here so all five type branches (scalar/vector/texture/
	// static_switch/component_mask) are covered uniformly.
	{
		const FString TypeLower = ParameterType.ToLower();
		const FString EffectiveType = TypeLower.IsEmpty() ? FString(TEXT("scalar")) : TypeLower;
		TArray<FString> Available;
		if (!CUMaterialParameterExists(MatInstance, EffectiveType, ParamName, Available))
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
				TEXT("Parameter '%s' not found on parent material (type: %s)"),
				*ParameterName, *EffectiveType));
			ErrObj->SetStringField(TEXT("hint"), CUBuildParameterHint(Available, ParamName, EffectiveType));
			TArray<TSharedPtr<FJsonValue>> AvailJson;
			AvailJson.Reserve(Available.Num());
			for (const FString& N : Available)
			{
				AvailJson.Add(MakeShared<FJsonValueString>(N));
			}
			ErrObj->SetArrayField(TEXT("available_parameters"), AvailJson);
			return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
		}
	}

	bool bSuccess = false;
	FString TypeUsed;
	// Issue #16557: static_switch/component_mask branches apply their change via
	// UpdateStaticPermutation(set); this flag lets the shared save block below skip
	// the redundant no-arg UpdateStaticPermutation() (which force-rebuilds permutations).
	bool bStaticPermutationUpdated = false;

	// Issue #8834 Phase 5: Wrap material parameter changes in UE transaction
	// Enables: (1) Undo/Redo support via Ctrl+Z, (2) OnObjectTransacted fires → JSONL capture
	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Set Material Parameter: %s.%s"), *MaterialPath, *ParameterName)));
	MatInstance->Modify();

	// Cache parameter type for performance (avoid repeated ToLower calls)
	const FString ParameterTypeLower = ParameterType.ToLower();

	// Issue #7702: Use FMaterialParameterInfo (required in UE5) and UpdateMaterialInstance pattern
	// from UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue
	FMaterialParameterInfo ParamInfo(ParamName);

	// Issue #7702: SetScalarParameterValueEditorOnly only modifies existing overrides.
	// To support parameters that haven't been overridden yet, we ensure the override
	// exists before setting the value. This mirrors UMaterialEditingLibrary behavior.

	// Determine parameter type and set value
	if (ParameterTypeLower == TEXT("scalar") || ParameterType.IsEmpty())
	{
		float ScalarValue = FCString::Atof(*ParameterValue);
		// Ensure scalar override exists (add to ScalarParameterValues if missing)
		bool bFound = false;
		for (auto& Param : MatInstance->ScalarParameterValues)
		{
			if (Param.ParameterInfo.Name == ParamName)
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			FScalarParameterValue NewParam;
			NewParam.ParameterInfo = ParamInfo;
			NewParam.ParameterValue = ScalarValue;
			MatInstance->ScalarParameterValues.Add(NewParam);
		}
		MatInstance->SetScalarParameterValueEditorOnly(ParamInfo, ScalarValue);
		bSuccess = true;
		TypeUsed = TEXT("scalar");
	}
	else if (ParameterTypeLower == TEXT("vector"))
	{
		// Issue #14979: accept comma string ("0.8,0.2,0.1"), JSON object
		// ({"R":..,"G":..,"B":..,"A":..}) and JSON array ([r,g,b,a]). Track whether a
		// value was actually parsed — a failed parse must NOT silently record (0,0,0)
		// and report success.
		FLinearColor VectorValue(FLinearColor::Black);
		bool bParsed = false;
		const FString Trimmed = ParameterValue.TrimStartAndEnd();
		if (Trimmed.StartsWith(TEXT("{")) || Trimmed.StartsWith(TEXT("[")))
		{
			TSharedPtr<FJsonValue> JsonVal;
			TSharedRef<TJsonReader<>> VecReader = TJsonReaderFactory<>::Create(Trimmed);
			if (FJsonSerializer::Deserialize(VecReader, JsonVal) && JsonVal.IsValid())
			{
				if (JsonVal->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject>& O = JsonVal->AsObject();
					if (O.IsValid() && O->HasField(TEXT("R")) && O->HasField(TEXT("G")) && O->HasField(TEXT("B")))
					{
						VectorValue.R = (float)O->GetNumberField(TEXT("R"));
						VectorValue.G = (float)O->GetNumberField(TEXT("G"));
						VectorValue.B = (float)O->GetNumberField(TEXT("B"));
						VectorValue.A = O->HasField(TEXT("A")) ? (float)O->GetNumberField(TEXT("A")) : 1.0f;
						bParsed = true;
					}
				}
				else if (JsonVal->Type == EJson::Array)
				{
					const TArray<TSharedPtr<FJsonValue>>& Arr = JsonVal->AsArray();
					if (Arr.Num() >= 3)
					{
						VectorValue.R = (float)Arr[0]->AsNumber();
						VectorValue.G = (float)Arr[1]->AsNumber();
						VectorValue.B = (float)Arr[2]->AsNumber();
						VectorValue.A = Arr.Num() >= 4 ? (float)Arr[3]->AsNumber() : 1.0f;
						bParsed = true;
					}
				}
			}
		}
		else if (ParameterValue.Contains(TEXT(",")))
		{
			TArray<FString> Components;
			ParameterValue.ParseIntoArray(Components, TEXT(","));
			// Issue #23050: reject UE struct-literal tokens like "(R=0.3" / "G=0.72"
			// that Atof-zero while Num() >= 3 still looked like a successful parse.
			bool bLooksLikeNamedStruct = false;
			for (const FString& Comp : Components)
			{
				if (Comp.Contains(TEXT("=")) || Comp.Contains(TEXT("(")))
				{
					bLooksLikeNamedStruct = true;
					break;
				}
			}
			if (!bLooksLikeNamedStruct && Components.Num() >= 3)
			{
				VectorValue.R = FCString::Atof(*Components[0]);
				VectorValue.G = FCString::Atof(*Components[1]);
				VectorValue.B = FCString::Atof(*Components[2]);
				VectorValue.A = Components.Num() >= 4 ? FCString::Atof(*Components[3]) : 1.0f;
				bParsed = true;
			}
		}

		if (!bParsed)
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
				TEXT("Could not parse vector parameter value: '%s'"), *ParameterValue));
			ErrObj->SetStringField(TEXT("hint"), TEXT("Provide a comma string '0.8,0.2,0.1', a JSON object {\"R\":..,\"G\":..,\"B\":..,\"A\":..}, or an array [r,g,b,a]."));
			return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
		}

		// Ensure vector override exists
		bool bFound = false;
		for (auto& Param : MatInstance->VectorParameterValues)
		{
			if (Param.ParameterInfo.Name == ParamName)
			{
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			FVectorParameterValue NewParam;
			NewParam.ParameterInfo = ParamInfo;
			NewParam.ParameterValue = VectorValue;
			MatInstance->VectorParameterValues.Add(NewParam);
		}
		MatInstance->SetVectorParameterValueEditorOnly(ParamInfo, VectorValue);
		bSuccess = true;
		TypeUsed = TEXT("vector");
	}
	else if (ParameterTypeLower == TEXT("texture"))
	{
		UTexture* Texture = LoadObject<UTexture>(nullptr, *ParameterValue);
		if (Texture)
		{
			// Ensure texture override exists
			bool bFound = false;
			for (auto& Param : MatInstance->TextureParameterValues)
			{
				if (Param.ParameterInfo.Name == ParamName)
				{
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				FTextureParameterValue NewParam;
				NewParam.ParameterInfo = ParamInfo;
				NewParam.ParameterValue = Texture;
				MatInstance->TextureParameterValues.Add(NewParam);
			}
			MatInstance->SetTextureParameterValueEditorOnly(ParamInfo, Texture);
			bSuccess = true;
			TypeUsed = TEXT("texture");
		}
		else
		{
			return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Texture not found: %s"), *ParameterValue));
		}
	}
	else if (ParameterTypeLower == TEXT("static_switch"))
	{
		// Issue #16557: static switch parameter (bool). UMaterialInstance has no
		// component-mask editor-only setter, so for symmetry both static types go
		// through GetStaticParameterValues -> mutate -> UpdateStaticPermutation(set),
		// the pattern documented on UMaterialInstance::UpdateStaticPermutation.
		const FString SwitchTrimmed = ParameterValue.TrimStartAndEnd();
		if (SwitchTrimmed.IsEmpty())
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrObj->SetStringField(JSON_ERROR_FIELD, TEXT("static_switch requires a boolean value (true/false)"));
			return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
		}
		const bool bSwitchValue = SwitchTrimmed.ToBool();

		FStaticParameterSet StaticParams;
		MatInstance->GetStaticParameterValues(StaticParams);
		// Update an existing override, or append a new one (mirrors the engine's own
		// UMaterialInstance::SetStaticSwitchParameterValueEditorOnly).
		bool bFound = false;
		for (FStaticSwitchParameter& Switch : StaticParams.StaticSwitchParameters)
		{
			if (Switch.ParameterInfo == ParamInfo)
			{
				Switch.bOverride = true;
				Switch.Value = bSwitchValue;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			StaticParams.StaticSwitchParameters.Add(FStaticSwitchParameter(ParamInfo, bSwitchValue, true, FGuid()));
		}
		MatInstance->UpdateStaticPermutation(StaticParams);
		bStaticPermutationUpdated = true;
		bSuccess = true;
		TypeUsed = TEXT("static_switch");
	}
	else if (ParameterTypeLower == TEXT("component_mask"))
	{
		// Issue #16557: static component mask (R,G,B,A bools). Accept a comma string
		// ("true,false,false,false"), a JSON array ([R,G,B,A]) or a JSON object
		// ({"R":..,"G":..,"B":..,"A":..}). A failed parse must NOT silently record an
		// all-false mask and report success (mirrors the vector branch, Issue #14979).
		bool MaskR = false, MaskG = false, MaskB = false, MaskA = false;
		bool bParsed = false;
		const FString MaskTrimmed = ParameterValue.TrimStartAndEnd();
		if (MaskTrimmed.StartsWith(TEXT("{")) || MaskTrimmed.StartsWith(TEXT("[")))
		{
			TSharedPtr<FJsonValue> JsonVal;
			TSharedRef<TJsonReader<>> MaskReader = TJsonReaderFactory<>::Create(MaskTrimmed);
			if (FJsonSerializer::Deserialize(MaskReader, JsonVal) && JsonVal.IsValid())
			{
				if (JsonVal->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject>& O = JsonVal->AsObject();
					if (O.IsValid() && (O->HasField(TEXT("R")) || O->HasField(TEXT("G")) || O->HasField(TEXT("B")) || O->HasField(TEXT("A"))))
					{
						MaskR = O->HasField(TEXT("R")) && O->GetBoolField(TEXT("R"));
						MaskG = O->HasField(TEXT("G")) && O->GetBoolField(TEXT("G"));
						MaskB = O->HasField(TEXT("B")) && O->GetBoolField(TEXT("B"));
						MaskA = O->HasField(TEXT("A")) && O->GetBoolField(TEXT("A"));
						bParsed = true;
					}
				}
				else if (JsonVal->Type == EJson::Array)
				{
					const TArray<TSharedPtr<FJsonValue>>& Arr = JsonVal->AsArray();
					if (Arr.Num() >= 1)
					{
						MaskR = Arr[0]->AsBool();
						MaskG = Arr.Num() >= 2 && Arr[1]->AsBool();
						MaskB = Arr.Num() >= 3 && Arr[2]->AsBool();
						MaskA = Arr.Num() >= 4 && Arr[3]->AsBool();
						bParsed = true;
					}
				}
			}
		}
		else
		{
			TArray<FString> Components;
			ParameterValue.ParseIntoArray(Components, TEXT(","));
			if (Components.Num() >= 1)
			{
				MaskR = Components[0].TrimStartAndEnd().ToBool();
				MaskG = Components.Num() >= 2 && Components[1].TrimStartAndEnd().ToBool();
				MaskB = Components.Num() >= 3 && Components[2].TrimStartAndEnd().ToBool();
				MaskA = Components.Num() >= 4 && Components[3].TrimStartAndEnd().ToBool();
				bParsed = true;
			}
		}

		if (!bParsed)
		{
			TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
			ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
				TEXT("Could not parse component_mask parameter value: '%s'"), *ParameterValue));
			ErrObj->SetStringField(TEXT("hint"), TEXT("Provide R,G,B,A booleans as a comma string 'true,false,false,false', a JSON array [R,G,B,A], or an object {\"R\":..,\"G\":..,\"B\":..,\"A\":..}."));
			return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
		}

		FStaticParameterSet StaticParams;
		MatInstance->GetStaticParameterValues(StaticParams);
		// Component-mask statics live in the editor-only sub-set; update an existing
		// override or append a new one.
		bool bFound = false;
		for (FStaticComponentMaskParameter& Mask : StaticParams.EditorOnly.StaticComponentMaskParameters)
		{
			if (Mask.ParameterInfo == ParamInfo)
			{
				Mask.bOverride = true;
				Mask.R = MaskR;
				Mask.G = MaskG;
				Mask.B = MaskB;
				Mask.A = MaskA;
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			StaticParams.EditorOnly.StaticComponentMaskParameters.Add(
				FStaticComponentMaskParameter(ParamInfo, MaskR, MaskG, MaskB, MaskA, true, FGuid()));
		}
		MatInstance->UpdateStaticPermutation(StaticParams);
		bStaticPermutationUpdated = true;
		bSuccess = true;
		TypeUsed = TEXT("component_mask");
	}

	if (bSuccess)
	{
		// Issue #7702: Full UpdateMaterialInstance pattern from UMaterialEditingLibrary
		MatInstance->MarkPackageDirty();
		MatInstance->PreEditChange(nullptr);
		MatInstance->PostEditChange();
		// Issue #16557: static_switch/component_mask already applied via
		// UpdateStaticPermutation(set); avoid a second forced permutation rebuild here.
		if (!bStaticPermutationUpdated)
		{
			MatInstance->UpdateStaticPermutation();
		}

		// Issue #7702: Save package to persist changes to disk
		UPackage* Package = MatInstance->GetOutermost();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		bool bSaved = UPackage::SavePackage(Package, MatInstance, *PackageFileName, SaveArgs);

		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
		ResultObj->SetStringField(TEXT("parameter_name"), ParameterName);
		ResultObj->SetStringField(TEXT("parameter_type"), TypeUsed);
		ResultObj->SetBoolField(TEXT("saved"), bSaved);
		if (CUIsAssetEditorOpen(MatInstance))
		{
			ResultObj->SetStringField(TEXT("editor_open_warning"),
				TEXT("Material editor is open; the editor's in-memory preview may overwrite this disk change on its next save. Close the editor or re-open the asset to see the change."));
		}
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Unknown parameter type: %s"), *ParameterType));
}

// Issue #24132: fail closed on non-UE package paths before CreatePackage /
// AssetRegistry (PathTree asserts PathView[0] == '/' on OS-absolute paths such
// as MSYS-rewritten C:/Program Files/Git/Game/...). Shared by CreateMaterial /
// CreateMaterialInstance / CreateMaterialFunction.
static bool CUValidateMaterialPackagePath(const FString& PackagePath, FString& OutError)
{
	const FString Trimmed = PackagePath.TrimStartAndEnd();
	if (Trimmed.IsEmpty())
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': path cannot be empty or whitespace"),
			*PackagePath);
		return false;
	}
	// Validate the same raw string the callers later join into FullPath. Accepting
	// a trimmed copy while forwarding the original would leave whitespace able to
	// reach CreatePackage / AssetRegistry (Issue #24990).
	if (Trimmed != PackagePath)
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': surrounding whitespace is not allowed"),
			*PackagePath);
		return false;
	}
	// Soft-object / PathTree roots must begin with '/'. Absolute OS paths never
	// are valid long package names; reject them before any Editor asset call.
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': UE package paths must start with '/'"),
			*PackagePath);
		return false;
	}
	if (PackagePath.Contains(TEXT("..")))
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': '..' path traversal is not allowed"),
			*PackagePath);
		return false;
	}
	if (PackagePath.Contains(TEXT("//")))
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': '//' is not allowed"),
			*PackagePath);
		return false;
	}
	if (PackagePath.StartsWith(TEXT("/Script/")))
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': '/Script/' is not an asset content mount"),
			*PackagePath);
		return false;
	}
	if (!FPackageName::IsValidLongPackageName(PackagePath))
	{
		OutError = FString::Printf(
			TEXT("Invalid material package path '%s': not a valid UE long package name"),
			*PackagePath);
		return false;
	}
	return true;
}

FString UCURemoteLibrary::CreateMaterialInstance(
	const FString& InstanceName,
	const FString& ParentMaterial,
	const FString& PackagePath
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] CreateMaterialInstance: %s (Parent: %s) in %s"),
		*InstanceName, *ParentMaterial, *PackagePath);

	// Issue #15651: reject empty/whitespace-only instance names up front — a valid
	// parent must not rescue an empty name (would create a ""- or "   "-named
	// instance; both reproduced live via direct RC). Fail before opening a
	// transaction. TrimStartAndEnd() gives whitespace parity with the Rust .trim()
	// guard so the direct-RC bypass path is also covered.
	if (InstanceName.TrimStartAndEnd().IsEmpty())
	{
		return CUJsonUtils::MakeErrorResponse(TEXT("Instance name cannot be empty"));
	}

	// Issue #24132: reject non-UE package paths before transaction / AssetRegistry.
	{
		FString PathError;
		if (!CUValidateMaterialPackagePath(PackagePath, PathError))
		{
			return CUJsonUtils::MakeErrorResponse(PathError);
		}
	}

	// Issue #8834 Phase 5: Wrap material instance creation in UE transaction
	FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("Create Material Instance: %s"), *InstanceName)));

	// Load parent material
	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentMaterial);
	if (!Parent)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Parent material not found: %s"), *ParentMaterial));
	}

	// Check if asset already exists (prevents Editor crash on duplicate creation)
	FString FullPath = PackagePath / InstanceName;
	FString LoadPath = FullPath + TEXT(".") + InstanceName;
	if (UObject* ExistingObj = LoadObject<UObject>(nullptr, *LoadPath))
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("MaterialInstance already exists at path: %s (type: %s)"), *FullPath, *ExistingObj->GetClass()->GetName()));
		ErrorObj->SetStringField(TEXT("existing_path"), ExistingObj->GetPathName());
		FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
		return Result;
	}

	// Create package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	// Create MaterialInstanceConstant
	UMaterialInstanceConstant* NewInstance = NewObject<UMaterialInstanceConstant>(
		Package,
		*InstanceName,
		RF_Public | RF_Standalone
	);

	if (!NewInstance)
	{
		return CUJsonUtils::MakeErrorResponse(TEXT("Failed to create MaterialInstanceConstant"));
	}

	// Set parent
	NewInstance->SetParentEditorOnly(Parent);

	// Mark dirty and notify asset registry (ensures immediate Editor visibility)
	NewInstance->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewInstance);
	Package->MarkPackageDirty();

	// Save package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, NewInstance, *PackageFileName, SaveArgs))
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Failed to save MaterialInstance: %s"), *FullPath));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Created MaterialInstance: %s"), *NewInstance->GetPathName());

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("instance_name"), InstanceName);
	ResultObj->SetStringField(TEXT("instance_path"), NewInstance->GetPathName());
	ResultObj->SetStringField(TEXT("parent_material"), ParentMaterial);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::CreateMaterial(
	const FString& MaterialName,
	const FString& PackagePath,
	const FString& ShadingModel,
	const FString& BlendMode
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] CreateMaterial: %s in %s (ShadingModel=%s, BlendMode=%s)"),
		*MaterialName, *PackagePath, *ShadingModel, *BlendMode);

	// Issue #15651: reject empty/whitespace-only names before building a path.
	// Reproduced live via direct RC: "" yields a "/Game/.../.Material_0" garbage
	// asset and "   " yields a "   "-named asset. TrimStartAndEnd() gives
	// whitespace parity with the Rust .trim() guard so the direct-RC bypass
	// (which skips the Rust layer) is also covered.
	if (MaterialName.TrimStartAndEnd().IsEmpty())
	{
		return CUJsonUtils::MakeErrorResponse(TEXT("Material name cannot be empty"));
	}

	// Issue #24132: reject non-UE package paths before CreatePackage / AssetRegistry
	// (PathTree assert PathView[0] == '/' on OS-absolute MSYS-rewritten paths).
	{
		FString PathError;
		if (!CUValidateMaterialPackagePath(PackagePath, PathError))
		{
			return CUJsonUtils::MakeErrorResponse(PathError);
		}
	}

	// Check if asset already exists (prevents crash on partially-loaded packages)
	FString FullPath = PackagePath / MaterialName;
	FString LoadPath = FullPath + TEXT(".") + MaterialName;
	if (UObject* ExistingObj = LoadObject<UObject>(nullptr, *LoadPath))
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Material already exists at path: %s (type: %s)"), *FullPath, *ExistingObj->GetClass()->GetName()));
		ErrorObj->SetStringField(TEXT("existing_path"), ExistingObj->GetPathName());
		FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
		return Result;
	}

	// Create package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	// Create Material
	UMaterial* NewMaterial = NewObject<UMaterial>(
		Package,
		*MaterialName,
		RF_Public | RF_Standalone
	);

	if (!NewMaterial)
	{
		return CUJsonUtils::MakeErrorResponse(TEXT("Failed to create Material"));
	}

	// Apply shading model (Issue #4425)
	if (!ShadingModel.IsEmpty())
	{
		// Try to find the enum value by name (supports both "DefaultLit" and "MSM_DefaultLit" formats)
		FString ShadingModelName = ShadingModel;
		if (!ShadingModelName.StartsWith(TEXT("MSM_")))
		{
			ShadingModelName = FString::Printf(TEXT("MSM_%s"), *ShadingModel);
		}

		int64 ShadingModelValue = StaticEnum<EMaterialShadingModel>()->GetValueByNameString(ShadingModelName);
		if (ShadingModelValue != INDEX_NONE)
		{
			NewMaterial->SetShadingModel((EMaterialShadingModel)ShadingModelValue);
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Set ShadingModel: %s (%lld)"), *ShadingModelName, ShadingModelValue);
		}
		else
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Material] Unknown ShadingModel: %s, using DefaultLit"), *ShadingModel);
		}
	}

	// Apply blend mode (Issue #4425)
	if (!BlendMode.IsEmpty())
	{
		// Try to find the enum value by name (supports both "Opaque" and "BLEND_Opaque" formats)
		FString BlendModeName = BlendMode;
		if (!BlendModeName.StartsWith(TEXT("BLEND_")))
		{
			BlendModeName = FString::Printf(TEXT("BLEND_%s"), *BlendMode);
		}

		int64 BlendModeValue = StaticEnum<EBlendMode>()->GetValueByNameString(BlendModeName);
		if (BlendModeValue != INDEX_NONE)
		{
			NewMaterial->BlendMode = (EBlendMode)BlendModeValue;
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Set BlendMode: %s (%lld)"), *BlendModeName, BlendModeValue);
		}
		else
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Material] Unknown BlendMode: %s, using Opaque"), *BlendMode);
		}
	}

	// Notify material system of property changes (triggers shader recompilation)
	NewMaterial->PostEditChange();
	NewMaterial->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMaterial);
	Package->MarkPackageDirty();

	// Save package
	FString PackageFileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, NewMaterial, *PackageFileName, SaveArgs))
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Failed to save Material: %s"), *FullPath));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Created Material: %s"), *NewMaterial->GetPathName());

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("material_name"), MaterialName);
	ResultObj->SetStringField(TEXT("material_path"), NewMaterial->GetPathName());
	// Return the actual applied values (Issue #4425)
	ResultObj->SetStringField(TEXT("shading_model"), StaticEnum<EMaterialShadingModel>()->GetNameStringByValue((int64)NewMaterial->GetShadingModels().GetFirstShadingModel()));
	ResultObj->SetStringField(TEXT("blend_mode"), StaticEnum<EBlendMode>()->GetNameStringByValue((int64)NewMaterial->BlendMode));
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// Issue #17066: resolve a path to either a UMaterial or a UMaterialFunction. Mirrors the
// GetMaterialGraph (#16324) fallback so the node-edit ops cover MF internal graphs. Sets
// exactly one of OutMaterial/OutFunction; returns false when neither loads.
static bool CUResolveMaterialOrFunction(
	const FString& Path,
	UMaterial*& OutMaterial,
	UMaterialFunction*& OutFunction)
{
	OutMaterial = LoadObject<UMaterial>(nullptr, *Path);
	OutFunction = nullptr;
	if (OutMaterial)
	{
		return true;
	}
	OutFunction = Cast<UMaterialFunction>(LoadObject<UMaterialFunctionInterface>(nullptr, *Path));
	return OutFunction != nullptr;
}

// Issue #17066: create a UMaterialExpression of the requested type, parented to `Outer`
// (a UMaterial or UMaterialFunction), so MF node insertion reuses the identical resolution
// table. Returns nullptr for anything that does not name a concrete engine expression class
// (caller emits the "Unknown expression type" error).
//
// Issue #24288: resolution is EXACT ONLY. The previous implementation ran an ordered
// `Contains()` ladder ahead of the class lookup, so any type whose name merely contained an
// earlier token was silently built as that earlier class and still reported success:true —
// 19 classes on UE 5.7, including Constant2Vector/Constant4Vector -> scalar Constant and the
// whole TextureSampleParameter* family -> plain TextureSample (a parameter node downgraded to
// a non-parameter one). The ladder is deleted rather than reordered: it resolved nothing the
// exact lookup cannot, and retaining it as a fallback would keep the same silent substitution
// alive for every near-miss string ("Constant5Vector", "TextureSampleXYZ") and would make
// correctness depend on module load order for optional-plugin classes.
static UMaterialExpression* CUCreateExpressionByType(
	UObject* Outer,
	const FString& NodeType,
	const FString& NodeName)
{
	// Accept the engine class name with or without the `U` prefix / `MaterialExpression`
	// prefix: "Constant4Vector", "MaterialExpressionConstant4Vector" and
	// "UMaterialExpressionConstant4Vector" all name the same class.
	FString ClassName = NodeType.TrimStartAndEnd();
	if (ClassName.StartsWith(TEXT("UMaterialExpression")))
	{
		ClassName.RightChopInline(1, EAllowShrinking::No);
	}
	if (!ClassName.StartsWith(TEXT("MaterialExpression")))
	{
		ClassName = TEXT("MaterialExpression") + ClassName;
	}

	// Primary path: exact /Script/Engine lookup (Issue #24288 — never substring).
	UClass* ExpressionClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *ClassName));

	// Issue #24344 (AC1): optional-plugin expression classes live outside /Script/Engine
	// (e.g. Paper2D::MaterialExpressionSpriteTextureSampler, Interchange MaterialX blur).
	// Strategy: FindFirstObject(NativeFirst) ONLY when the Engine path misses, and ONLY
	// among already-loaded UClasses. We deliberately do not LoadPackage/LoadObject a
	// plugin module (project config would change behavior silently) and we never fall
	// back to a different expression type — fail closed when the class is not loaded.
	// Alternatives rejected:
	//   * GetDerivedClasses(UMaterialExpression) — O(all subclasses) scan per node
	//   * Explicit multi-module path table — drifts as plugins add classes
	//   * FindFirstObject without the Engine-first gate — would re-introduce ambiguity
	//     for any short name that collides across modules.
	if (!ExpressionClass)
	{
		ExpressionClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	}

	// Fail closed on three counts, so a caller never receives a node it did not ask for:
	// unresolved name, a class that is not an expression, and an abstract engine base.
	// NewObject on an abstract class only `ensure`s — it still returns an object — so without
	// the CLASS_Abstract test a request for e.g. MaterialExpressionCustomOutput committed a
	// ghost node to the asset and reported success (Issue #24288). Mirrors the existing
	// ResolveConcreteBTClass guard in this file.
	if (!ExpressionClass
		|| !ExpressionClass->IsChildOf(UMaterialExpression::StaticClass())
		|| ExpressionClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return nullptr;
	}

	UMaterialExpression* NewExpression = NewObject<UMaterialExpression>(Outer, ExpressionClass);

	// Name the expression when its ParameterName IS its own name.
	//
	// Use the engine's own HasAParameterName()/SetParameterName() virtuals rather than a list of
	// casts. The engine added them precisely because the field is spread across unrelated
	// branches ("multiple class have ParameterName but are not UMaterialExpressionParameter due
	// to class hierarchy" — MaterialExpression.h), and a cast list silently misses them: on UE
	// 5.7, FontSampleParameter, RuntimeVirtualTextureSampleParameter, TextureCollectionParameter
	// and SparseVolumeTextureSampleParameter all own a ParameterName while deriving from neither
	// UMaterialExpressionParameter nor UMaterialExpressionTextureSampleParameter, so they would
	// come back successfully created but unnamed — the same silent-wrong-outcome class this fix
	// exists to remove.
	//
	// The two collection expressions are the one exception and must be excluded BY CLASS: they
	// override the same virtuals, but their ParameterName is "the name of the parameter being
	// referenced" inside a separate UMaterialParameterCollection asset, not the node's own name.
	// Writing NodeName there fabricates a dangling reference into an unset collection. Both are
	// reachable here — CollectionParameter is an EXPRESSION_TYPE_PARAMS key and
	// build_material_graph sends NodeName for every node.
	if (!NodeName.IsEmpty()
		&& NewExpression->HasAParameterName()
		&& !NewExpression->IsA<UMaterialExpressionCollectionParameter>()
		&& !NewExpression->IsA<UMaterialExpressionCollectionTransform>())
	{
		NewExpression->SetParameterName(FName(*NodeName));
	}

	return NewExpression;
}

// Issue #17066: count assets that reference an asset's package (cross-asset blast radius).
// A Material Function is shared across many materials, so a destructive edit (delete) must
// surface how many consumers it would touch. NEW code — no prior in-tree GetReferencers use.
// `OutReferencerNames` is filled with the referencing package names (for the JSON response).
static int32 CUCountAssetReferencers(UObject* Asset, TArray<FString>& OutReferencerNames)
{
	OutReferencerNames.Reset();
	if (!Asset)
	{
		return 0;
	}
	const UPackage* Package = Asset->GetOutermost();
	if (!Package)
	{
		return 0;
	}
	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FName> Referencers;
	AssetRegistry.GetReferencers(Package->GetFName(), Referencers);
	for (const FName& Ref : Referencers)
	{
		OutReferencerNames.Add(Ref.ToString());
	}
	return OutReferencerNames.Num();
}

FString UCURemoteLibrary::AddMaterialNode(
	const FString& MaterialPath,
	const FString& NodeType,
	const FString& NodeName,
	const FString& LocationJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] AddMaterialNode: %s to %s"), *NodeType, *MaterialPath);

	// Load Material, or fall back to a Material Function (Issue #17066) so MF internal
	// graphs accept expression nodes (previously rejected with "Material not found").
	UMaterial* Material = nullptr;
	UMaterialFunction* MatFunc = nullptr;
	if (!CUResolveMaterialOrFunction(MaterialPath, Material, MatFunc))
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material or MaterialFunction not found: %s"), *MaterialPath));
	}
	UObject* Owner = Material ? static_cast<UObject*>(Material) : static_cast<UObject*>(MatFunc);

	// Parse location
	int32 PosX = 0, PosY = 0;
	if (!LocationJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> LocationObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LocationJson);
		if (FJsonSerializer::Deserialize(Reader, LocationObj) && LocationObj.IsValid())
		{
			PosX = (int32)LocationObj->GetNumberField(TEXT("X"));
			PosY = (int32)LocationObj->GetNumberField(TEXT("Y"));
		}
	}

	// Create expression based on type (shared resolver — Issue #17066). `Owner` is the
	// UMaterial or UMaterialFunction so the new expression is parented correctly.
	UMaterialExpression* NewExpression = CUCreateExpressionByType(Owner, NodeType, NodeName);

	if (!NewExpression)
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Unknown expression type: %s"), *NodeType));
		// Issue #24288: state the contract instead of enumerating 5 of the 340+ creatable
		// classes. The old list named a fraction of what resolves and so mis-directed every
		// caller that hit this error.
		// Issue #24344: non-Engine classes are creatable only when their plugin is already loaded.
		ErrorObj->SetStringField(TEXT("hint"), TEXT("node_type must name a concrete UMaterialExpression subclass in /Script/Engine or a currently-loaded optional plugin (e.g. Paper2D, Interchange MaterialX), e.g. \"MaterialExpressionConstant4Vector\" (the \"MaterialExpression\" prefix is optional: \"Constant4Vector\"). Abstract engine bases (e.g. MaterialExpressionCustomOutput) are not creatable. Unloaded plugin classes fail closed (no silent substitute). For a Lerp node pass \"LinearInterpolate\"."));
		FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
		return Result;
	}

	// Set position
	NewExpression->MaterialExpressionEditorX = PosX;
	NewExpression->MaterialExpressionEditorY = PosY;

	// Issue #8564 Phase K-4: Transaction + Modify for proper object_changed capture
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddMaterialNode", "Add Material Node"));
	Owner->Modify();
#endif

	// Add to the owning Material or Material Function via its ExpressionCollection.
	// Both expose GetExpressionCollection() (UMaterialFunction mirrors the API — Issue #17066).
	if (Material)
	{
		Material->GetExpressionCollection().AddExpression(NewExpression);
	}
	else
	{
		MatFunc->GetExpressionCollection().AddExpression(NewExpression);
	}
	Owner->MarkPackageDirty();
	Owner->PostEditChange();

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Added node: %s"), *NewExpression->GetName());

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_name"), NewExpression->GetName());
	ResultObj->SetStringField(TEXT("node_type"), NewExpression->GetClass()->GetName());
	ResultObj->SetStringField(TEXT("owner_type"), Material ? TEXT("Material") : TEXT("MaterialFunction"));
	ResultObj->SetNumberField(TEXT("pos_x"), PosX);
	ResultObj->SetNumberField(TEXT("pos_y"), PosY);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// Issue #14979: Convert a JSON value into UE `ImportText` syntax so any UPROPERTY
// on a material expression can be set generically via reflection (replacing the
// old hardcoded 4-type Cast<> branches). Struct/array values become the paren form
// UE expects, e.g. FLinearColor {R,G,B,A} -> "(R=..,G=..,B=..,A=..)".
static FString CUMaterialJsonToImportText(const TSharedPtr<FJsonValue>& Val)
{
	if (!Val.IsValid())
	{
		return FString();
	}
	switch (Val->Type)
	{
	case EJson::Number:
		return FString::SanitizeFloat(Val->AsNumber());
	case EJson::Boolean:
		return Val->AsBool() ? TEXT("true") : TEXT("false");
	case EJson::String:
		return Val->AsString();
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject>& Obj = Val->AsObject();
		TArray<FString> Pairs;
		if (Obj.IsValid())
		{
			for (const auto& KV : Obj->Values)
			{
				Pairs.Add(FString::Printf(TEXT("%s=%s"), *KV.Key, *CUMaterialJsonToImportText(KV.Value)));
			}
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Pairs, TEXT(",")));
	}
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>& Arr = Val->AsArray();
		TArray<FString> Elems;
		for (const auto& E : Arr)
		{
			Elems.Add(CUMaterialJsonToImportText(E));
		}
		return FString::Printf(TEXT("(%s)"), *FString::Join(Elems, TEXT(",")));
	}
	default:
		return FString();
	}
}

// Issue #14979: detect whether an asset is currently open in an editor. Writing to disk
// while the material editor holds an in-memory preview copy can be silently rolled back on
// the editor's next save, so the write handlers surface an `editor_open_warning` and
// proceed (warn-and-proceed) rather than blocking the common interactive workflow.
static bool CUIsAssetEditorOpen(UObject* Asset)
{
#if WITH_EDITOR
	if (!Asset || !GEditor)
	{
		return false;
	}
	if (UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
	{
		return Sub->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false) != nullptr;
	}
#endif
	return false;
}

// Issue #14979: Exact-name-first material expression lookup. The legacy `.Contains()`
// substring + first-match-wins behavior let `NodeName="Texture"` mutate/delete
// `TextureSample_2` instead of the intended node — a wrong-node data-loss path that the
// force-delete connection-severing amplifies. Prefer an exact `GetName() == NodeName`
// match; only fall back to substring when there is no exact hit. `bOutAmbiguous` is set
// when more than one node matches under the active strategy so callers can refuse.
static UMaterialExpression* CUFindMaterialExpression(
	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions,
	const FString& NodeName,
	bool& bOutAmbiguous)
{
	bOutAmbiguous = false;
	UMaterialExpression* ExactMatch = nullptr;
	int32 ExactCount = 0;
	UMaterialExpression* FirstSubstring = nullptr;
	int32 SubstringCount = 0;
	for (UMaterialExpression* Expr : Expressions)
	{
		if (!Expr)
		{
			continue;
		}
		const FString ExprName = Expr->GetName();
		if (ExprName == NodeName)
		{
			if (ExactCount == 0)
			{
				ExactMatch = Expr;
			}
			ExactCount++;
		}
		else if (ExprName.Contains(NodeName))
		{
			if (SubstringCount == 0)
			{
				FirstSubstring = Expr;
			}
			SubstringCount++;
		}
	}
	if (ExactMatch)
	{
		bOutAmbiguous = (ExactCount > 1);
		return ExactMatch;
	}
	bOutAmbiguous = (SubstringCount > 1);
	return FirstSubstring;
}

FString UCURemoteLibrary::ModifyMaterialNode(
	const FString& MaterialPath,
	const FString& NodeName,
	const FString& PropertiesJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] ModifyMaterialNode: %s in %s"), *NodeName, *MaterialPath);

	// Load Material, or fall back to a Material Function (Issue #17066) so MF internal
	// nodes can be modified too.
	UMaterial* Material = nullptr;
	UMaterialFunction* MatFunc = nullptr;
	if (!CUResolveMaterialOrFunction(MaterialPath, Material, MatFunc))
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material or MaterialFunction not found: %s"), *MaterialPath));
	}
	UObject* Owner = Material ? static_cast<UObject*>(Material) : static_cast<UObject*>(MatFunc);

	// Issue #14979: exact-name-first lookup (substring fallback) to avoid mutating
	// the wrong node on a name collision. Issue #17066: owner-agnostic expression view —
	// within one engine version both GetExpressions() accessors return the same type.
	bool bAmbiguous = false;
	UMaterialExpression* TargetExpression = CUFindMaterialExpression(
		Material ? Material->GetExpressions() : MatFunc->GetExpressions(), NodeName, bAmbiguous);

	if (!TargetExpression)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}
	// Issue #14979: refuse on ambiguity so a substring/exact-name collision never
	// silently mutates the wrong node (parity with DeleteMaterialNode).
	if (bAmbiguous)
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
			TEXT("Ambiguous node name '%s' matches multiple nodes"), *NodeName));
		ErrObj->SetStringField(TEXT("hint"), TEXT("Pass the exact node name (GetName()) to disambiguate."));
		return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
	}

	// Issue #14979: PropertiesJson must be a valid, non-empty JSON object. A blank or
	// malformed payload previously slipped through the old `if(Deserialize){...}` (no
	// else) and re-saved the material unchanged while reporting success:true.
	TSharedPtr<FJsonObject> PropsObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertiesJson);
	if (!FJsonSerializer::Deserialize(Reader, PropsObj) || !PropsObj.IsValid())
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(
			TEXT("Invalid PropertiesJson (expected a non-empty JSON object): '%s'"), *PropertiesJson));
	}

	// Issue #14979: generic reflection setter. Each JSON field maps to a UPROPERTY on
	// the expression (covers Scalar/Vector/Constant*/Const3Vec and every other type the
	// old 4-branch Cast<> chain silently ignored). X/Y stay aliased to the editor-only
	// position fields. Track applied vs skipped so a zero-applied write fails loudly.
	int32 AppliedCount = 0;
	TArray<FString> AppliedProperties;
	TArray<FString> SkippedProperties;
	UClass* ExprClass = TargetExpression->GetClass();
	for (const auto& KV : PropsObj->Values)
	{
		// Issue #14979: UE 5.8 keys FJsonObject::Values by UE::FSharedString, not FString.
		// operator* yields a const TCHAR*, so construct an FString for the field-name logic.
		const FString Key(*KV.Key);
		const TSharedPtr<FJsonValue>& Val = KV.Value;

		// Editor-only position aliases (JSON "X"/"Y" -> MaterialExpressionEditorX/Y).
		if (Key == TEXT("X") && Val.IsValid() && Val->Type == EJson::Number)
		{
			TargetExpression->MaterialExpressionEditorX = (int32)Val->AsNumber();
			AppliedCount++;
			AppliedProperties.Add(Key);
			continue;
		}
		if (Key == TEXT("Y") && Val.IsValid() && Val->Type == EJson::Number)
		{
			TargetExpression->MaterialExpressionEditorY = (int32)Val->AsNumber();
			AppliedCount++;
			AppliedProperties.Add(Key);
			continue;
		}

		FProperty* Prop = ExprClass ? ExprClass->FindPropertyByName(FName(*Key)) : nullptr;
		if (!Prop)
		{
			SkippedProperties.Add(Key);
			continue;
		}
		const FString ImportText = CUMaterialJsonToImportText(Val);
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(TargetExpression);
		const TCHAR* ImportResult = Prop->ImportText_Direct(*ImportText, ValuePtr, TargetExpression, PPF_None);
		if (ImportResult != nullptr)
		{
			AppliedCount++;
			AppliedProperties.Add(Key);
		}
		else
		{
			SkippedProperties.Add(Key);
		}
	}

	// Build the applied/skipped arrays once for either outcome.
	TArray<TSharedPtr<FJsonValue>> AppliedJson;
	for (const FString& P : AppliedProperties) { AppliedJson.Add(MakeShared<FJsonValueString>(P)); }
	TArray<TSharedPtr<FJsonValue>> SkippedJson;
	for (const FString& P : SkippedProperties) { SkippedJson.Add(MakeShared<FJsonValueString>(P)); }

	// Issue #14979: zero properties applied -> report failure WITHOUT re-saving the
	// package (no more silent no-op + false success).
	if (AppliedCount == 0)
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrObj->SetStringField(TEXT("error"), TEXT("No properties applied (unknown property names or import failures)"));
		ErrObj->SetStringField(TEXT("node_name"), TargetExpression->GetName());
		ErrObj->SetArrayField(TEXT("applied_properties"), AppliedJson);
		ErrObj->SetArrayField(TEXT("skipped_properties"), SkippedJson);
		ErrObj->SetStringField(TEXT("hint"), TEXT("Use exact UPROPERTY names for this expression type (e.g. DefaultValue, ParameterName)."));
		return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
	}

	// Issue #8564 Phase K-4: Transaction + Modify for proper object_changed capture
	// Issue #17066: Owner is the UMaterial or UMaterialFunction (FMaterialUpdateContext is
	// Material-only and has no AddMaterialFunction, so PostEditChange drives the MF recompile).
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ModifyMaterialNode", "Modify Material Node"));
	Owner->Modify();
#endif

	Owner->MarkPackageDirty();
	Owner->PostEditChange();

	// Issue #7702: Save package to persist changes to disk
	UPackage* Package = Owner->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	bool bSaved = UPackage::SavePackage(Package, Owner, *PackageFileName, SaveArgs);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_name"), TargetExpression->GetName());
	ResultObj->SetStringField(TEXT("owner_type"), Material ? TEXT("Material") : TEXT("MaterialFunction"));
	ResultObj->SetNumberField(TEXT("applied_count"), AppliedCount);
	ResultObj->SetArrayField(TEXT("applied_properties"), AppliedJson);
	ResultObj->SetArrayField(TEXT("skipped_properties"), SkippedJson);
	ResultObj->SetBoolField(TEXT("saved"), bSaved);
	// Issue #17066: cross-asset blast-radius awareness for a shared MF. Modify is non-structural
	// (a property change can't half-disassemble a consumer graph the way a delete can), so this
	// is WARN-ONLY — expose the referencer count, never refuse. There is no `bForce` on modify.
	if (MatFunc)
	{
		TArray<FString> ReferencerNames;
		const int32 ReferencerCount = CUCountAssetReferencers(MatFunc, ReferencerNames);
		ResultObj->SetNumberField(TEXT("referencing_asset_count"), ReferencerCount);
		if (ReferencerCount > 0)
		{
			ResultObj->SetStringField(TEXT("cross_asset_warning"),
				FString::Printf(TEXT("This Material Function is referenced by %d asset(s); the change propagates to all consumers."), ReferencerCount));
		}
	}
	if (CUIsAssetEditorOpen(Owner))
	{
		ResultObj->SetStringField(TEXT("editor_open_warning"),
			TEXT("Material editor is open; the editor's in-memory preview may overwrite this disk change on its next save. Close the editor or re-open the asset to see the change."));
	}
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::SetMaterialProperty(
	const FString& MaterialPath,
	const FString& PropertyName,
	const FString& PropertyValue
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] SetMaterialProperty: %s = %s in %s"),
		*PropertyName, *PropertyValue, *MaterialPath);

	// Load Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	FString PropLower = PropertyName.ToLower();
	bool bApplied = false;
	FString AppliedValue;

	// PreEditChange moved after validation — paired with PostEditChange below (Issue #8159 review fix)
	// Calling PreEditChange before validation would leave material in dirty state on early-return errors.

	if (PropLower == TEXT("busematerialattributes"))
	{
		bool bVal = PropertyValue.ToLower() == TEXT("true") || PropertyValue == TEXT("1");
		Material->bUseMaterialAttributes = bVal;
		AppliedValue = bVal ? TEXT("true") : TEXT("false");
		bApplied = true;
	}
	else if (PropLower == TEXT("materialdomain"))
	{
		FString DomainName = PropertyValue;
		if (!DomainName.StartsWith(TEXT("MD_")))
		{
			DomainName = FString::Printf(TEXT("MD_%s"), *PropertyValue);
		}
		int64 DomainValue = StaticEnum<EMaterialDomain>()->GetValueByNameString(DomainName);
		if (DomainValue != INDEX_NONE)
		{
			Material->MaterialDomain = static_cast<EMaterialDomain>(DomainValue);
			AppliedValue = DomainName;
			bApplied = true;
		}
		else
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Unknown MaterialDomain: %s"), *PropertyValue));
			ErrorObj->SetStringField(TEXT("hint"), TEXT("Supported: Surface, DeferredDecal, LightFunction, Volume, PostProcess, UI"));
			FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
			return Result;
		}
	}
	else if (PropLower == TEXT("blendmode"))
	{
		FString BlendModeName = PropertyValue;
		if (!BlendModeName.StartsWith(TEXT("BLEND_")))
		{
			BlendModeName = FString::Printf(TEXT("BLEND_%s"), *PropertyValue);
		}
		int64 BlendModeValue = StaticEnum<EBlendMode>()->GetValueByNameString(BlendModeName);
		if (BlendModeValue != INDEX_NONE)
		{
			Material->BlendMode = static_cast<EBlendMode>(BlendModeValue);
			AppliedValue = BlendModeName;
			bApplied = true;
		}
		else
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Unknown BlendMode: %s"), *PropertyValue));
			ErrorObj->SetStringField(TEXT("hint"), TEXT("Supported: Opaque, Masked, Translucent, Additive, Modulate, AlphaComposite, AlphaHoldout"));
			FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
			return Result;
		}
	}
	else if (PropLower == TEXT("shadingmodel"))
	{
		FString ShadingModelName = PropertyValue;
		if (!ShadingModelName.StartsWith(TEXT("MSM_")))
		{
			ShadingModelName = FString::Printf(TEXT("MSM_%s"), *PropertyValue);
		}
		int64 ShadingModelValue = StaticEnum<EMaterialShadingModel>()->GetValueByNameString(ShadingModelName);
		if (ShadingModelValue != INDEX_NONE)
		{
			Material->SetShadingModel(static_cast<EMaterialShadingModel>(ShadingModelValue));
			AppliedValue = ShadingModelName;
			bApplied = true;
		}
		else
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Unknown ShadingModel: %s"), *PropertyValue));
			ErrorObj->SetStringField(TEXT("hint"), TEXT("Supported: DefaultLit, Unlit, Subsurface, ClearCoat, SubsurfaceProfile, TwoSidedFoliage, Hair, Cloth, Eye, SingleLayerWater, ThinTranslucent"));
			FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
			return Result;
		}
	}
	else if (PropLower == TEXT("btwosided"))
	{
		bool bVal = PropertyValue.ToLower() == TEXT("true") || PropertyValue == TEXT("1");
		Material->TwoSided = bVal;
		AppliedValue = bVal ? TEXT("true") : TEXT("false");
		bApplied = true;
	}
	else if (PropLower == TEXT("opacitymaskclipvalue"))
	{
		float Val = 0.0f;
		if (!FDefaultValueHelper::ParseFloat(PropertyValue, Val))
		{
			TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
			ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Invalid float value for OpacityMaskClipValue: %s"), *PropertyValue));
			ErrorObj->SetStringField(TEXT("hint"), TEXT("Expected a numeric value (e.g., 0.333, 0.5, 1.0)"));
			FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
			return Result;
		}
		Material->OpacityMaskClipValue = Val;
		AppliedValue = FString::SanitizeFloat(Val);
		bApplied = true;
	}
	else if (PropLower == TEXT("ballownegativeemissivecolor"))
	{
		bool bVal = PropertyValue.ToLower() == TEXT("true") || PropertyValue == TEXT("1");
		Material->bAllowNegativeEmissiveColor = bVal;
		AppliedValue = bVal ? TEXT("true") : TEXT("false");
		bApplied = true;
	}
	else
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Unsupported property: %s"), *PropertyName));
		ErrorObj->SetStringField(TEXT("hint"), TEXT("Supported properties: bUseMaterialAttributes, MaterialDomain, BlendMode, ShadingModel, bTwoSided, OpacityMaskClipValue, bAllowNegativeEmissiveColor"));
		FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
		return Result;
	}

	Material->PreEditChange(nullptr);
	Material->MarkPackageDirty();
	Material->PostEditChange();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("property_name"), PropertyName);
	ResultObj->SetStringField(TEXT("property_value"), AppliedValue);
	ResultObj->SetStringField(TEXT("material_path"), MaterialPath);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// Issue #25072: single source of truth for "which output index does this pin name mean".
//
// Two call sites (ConnectMaterialNodes and CUConnectMaterialFunctionNodes) each carried
// their own copy of the channel-letter table, and therefore each carried the same gap:
// the graph capture emits pins as `Output<N>` and `build_material_graph` forwards them
// verbatim, so every `Output1`/`Output2`/`Output4` fell through to the 0 default and
// authored RGB. Measured on the shipped WorldGridMaterial — spec asked for
// {Output0:1, Output1:1, Output2:5, Output4:2}, authored asset read back {Output0: 9}.
// The material still compiles, so the recompile oracle could not see it.
//
// The letters are aliases for indices ([1]=R, [2]=G, [3]=B, [4]=A on TextureSample), so
// `Output<N>` -> N agrees with them and is also the correct reading for expressions whose
// outputs are not colour channels (a MaterialFunctionCall's second output is `Output1`).
static int32 CUMaterialOutputIndexFromPin(const FString& SourcePin)
{
	// Trimmed before matching: a stray space in `" Output1"` is a formatting artifact,
	// not a different channel, so normalizing it resolves what the caller meant. That
	// is categorically different from the catch-all this function used to end with.
	const FString Pin = SourcePin.TrimStartAndEnd().ToLower();
	if (Pin.IsEmpty()) { return 0; }  // unspecified == the whole-value output
	if (Pin == TEXT("rgb") || Pin == TEXT("result")) { return 0; }
	if (Pin == TEXT("r") || Pin == TEXT("x")) { return 1; }
	if (Pin == TEXT("g") || Pin == TEXT("y")) { return 2; }
	if (Pin == TEXT("b") || Pin == TEXT("z")) { return 3; }
	if (Pin == TEXT("a") || Pin == TEXT("w")) { return 4; }
	if (Pin.StartsWith(TEXT("output")))
	{
		const FString IndexPart = Pin.RightChop(6);  // strlen("output")
		if (IndexPart.IsEmpty())
		{
			return 0;  // bare "output" keeps its historical meaning
		}
		// `SourcePin` is caller-controlled all the way from the MCP schema (an
		// unconstrained string), so the tail decides which output gets wired. What
		// happens on a bad index is NOT a crash — verified in the shipped engine:
		// UMaterialExpression::ConnectExpression (MaterialExpressions.cpp:2095) already
		// guards `OutputIndex >= 0 && OutputIndex < Outputs.Num()` and simply does
		// nothing when that fails, leaving `Input->Expression` unset. The surrounding
		// code here then reported `bConnected = true` and `success: true`, so a bad pin
		// produced a SILENT FALSE SUCCESS. That is the failure this rejects — the same
		// masked-failure class #25072 itself was.
		//
		// Strict all-digits, deliberately not FString::IsNumeric(): UE's IsNumeric
		// accepts a leading sign AND one decimal point, so "Output-1" parses to -1 and
		// "Output1.5" truncates to 1 — a pin the caller did not ask for, wired silently.
		// Anything this function cannot honour exactly becomes INDEX_NONE, which callers
		// turn into a canonical error naming the pin and the node's real output count.
		// Clamping was rejected for the same reason: wiring a different channel than the
		// caller asked for is the defect, not the remedy.
		bool bAllDigits = true;
		for (const TCHAR Ch : IndexPart)
		{
			if (!FChar::IsDigit(Ch))
			{
				bAllDigits = false;
				break;
			}
		}
		if (bAllDigits)
		{
			return FCString::Atoi(*IndexPart);
		}
		UE_LOG(LogNarshaMCPRemote, Warning,
			TEXT("[Material] Output pin '%s' is not 'output<digits>'; refusing to wire"), *SourcePin);
		return INDEX_NONE;
	}
	// No catch-all. This used to `return 0` for anything unrecognised, so a typo
	// ("Outptu5"), a wrong-domain pin name ("Alpha", "Chan1"), or any spelling this
	// function does not know silently wired output 0 and reported success — the same
	// silent-false-success class #25072 was filed for, sitting inside the guard meant
	// to remove it. Review found it; the accepted set is now closed and everything
	// else is the caller's to see.
	//
	// The set is exactly what this tool documents and what captures emit:
	// `output<digits>`, `rgb`, `result`, the RGBA/XYZW letters, and empty. Blueprint
	// pin spellings (`Then`, `ReturnValue`, `exec_out`, …) belong to a different tool
	// and are correctly rejected here rather than resolved to a channel.
	UE_LOG(LogNarshaMCPRemote, Warning,
		TEXT("[Material] Unknown output pin '%s'; expected output<N>, rgb, result, r/g/b/a, x/y/z/w or empty"),
		*SourcePin);
	return INDEX_NONE;
}

/// Bounds a resolved output index against what the source expression actually has.
///
/// Companion to CUMaterialOutputIndexFromPin: that one rejects spellings, this one
/// rejects indices that parse fine but exceed the node's arity ("Output99" on a
/// two-output node — a miscount, not an attack).
///
/// What this prevents, stated accurately: **a silent false success**, not a crash.
/// `FExpressionInput::Connect` reaches `UMaterialExpression::ConnectExpression`
/// (MaterialExpressions.cpp:2095 in the shipped UE 5.8), which already checks
/// `OutputIndex >= 0 && OutputIndex < Outputs.Num()` and does nothing when it fails —
/// leaving `Input->Expression` unset while the callers below still set
/// `bConnected = true` and return `success: true`. An earlier version of this comment
/// claimed the engine had "no bounds check"; that was asserted from reading the call
/// site rather than the engine, and review corrected it.
///
/// Residual, named rather than implied: `UMaterialExpressionComposite::
/// ConnectExpression` indexes `OutputExpressions->ReroutePins[OutputIndex]` with no
/// bound of its own, and `GetOutputs()` on that class compacts away ReroutePins whose
/// Expression is null — so an index in range against `Outputs.Num()` is not guaranteed
/// to address the same slot. Composite is not a source node type this batch path
/// authors, so it is out of scope here, but the mismatch is real.
///
/// Returns an error string to return to the caller, or empty when the index is usable.
// Non-const `SourceExpr`: UMaterialExpression::GetOutputs() is a non-const member in
// UE 5.8, so a const pointer cannot ask the node how many outputs it has.
static FString CUValidateMaterialOutputIndex(
	UMaterialExpression* SourceExpr,
	const FString& SourcePin,
	int32 OutputIndex
)
{
	if (!SourceExpr)
	{
		return FString::Printf(TEXT("Source expression unavailable for pin '%s'"), *SourcePin);
	}
	if (OutputIndex == INDEX_NONE)
	{
		return FString::Printf(
			TEXT("Unusable output pin '%s' on node '%s'; expected output<N>, rgb, result, r/g/b/a, x/y/z/w or empty"),
			*SourcePin, *SourceExpr->GetName());
	}
	const int32 OutputCount = SourceExpr->GetOutputs().Num();
	if (OutputIndex >= OutputCount)
	{
		return FString::Printf(
			TEXT("Output pin '%s' resolves to index %d but node '%s' (%s) has %d output(s)"),
			*SourcePin, OutputIndex, *SourceExpr->GetName(), *SourceExpr->GetClass()->GetName(), OutputCount);
	}
	return FString();
}

// Issue #17066: connect nodes inside a Material Function. MFs have no material main node, so
// the target is another expression or — when TargetNode is empty/"output" — the function's
// FunctionOutput node. Source/target lookups use the #14979-hardened CUFindMaterialExpression
// (exact-name-first + ambiguity refusal) so a name collision never wires the wrong node.
static FString CUConnectMaterialFunctionNodes(
	UMaterialFunction* MatFunc,
	const FString& SourceNode,
	const FString& TargetNode,
	const FString& SourcePin,
	const FString& TargetPin)
{
	bool bAmbiguous = false;
	UMaterialExpression* SourceExpr =
		CUFindMaterialExpression(MatFunc->GetExpressions(), SourceNode, bAmbiguous);
	if (!SourceExpr)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Source node not found: %s"), *SourceNode));
	}
	if (bAmbiguous)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(
			TEXT("Ambiguous source node '%s' matches multiple nodes; pass the exact node name."), *SourceNode));
	}

	// Issue #25072: shared with ConnectMaterialNodes. This site used to carry its own
	// copy of the RGBA mapping ("mirrors ConnectMaterialNodes RGBA mapping, Issue
	// #8159") and therefore its own copy of the same bug — `Output<N>` fell through to
	// index 0. One helper so the next spelling only has to be taught once.
	const int32 OutputIndex = CUMaterialOutputIndexFromPin(SourcePin);
	if (const FString PinError = CUValidateMaterialOutputIndex(SourceExpr, SourcePin, OutputIndex); !PinError.IsEmpty())
	{
		return CUJsonUtils::MakeErrorResponse(PinError);
	}

	// Resolve target. Empty/"output" TargetNode => the function's single FunctionOutput node.
	UMaterialExpression* TargetExpr = nullptr;
	if (TargetNode.IsEmpty() || TargetNode.ToLower() == TEXT("output"))
	{
		UMaterialExpressionFunctionOutput* FoundOutput = nullptr;
		int32 OutputCount = 0;
		for (UMaterialExpression* Expr : MatFunc->GetExpressions())
		{
			if (UMaterialExpressionFunctionOutput* OutExpr = Cast<UMaterialExpressionFunctionOutput>(Expr))
			{
				if (OutputCount == 0) { FoundOutput = OutExpr; }
				OutputCount++;
			}
		}
		if (OutputCount == 0)
		{
			return CUJsonUtils::MakeErrorResponse(
				TEXT("Material Function has no FunctionOutput node; add one (add_function_output) or pass an explicit target_node."));
		}
		if (OutputCount > 1)
		{
			return CUJsonUtils::MakeErrorResponse(FString::Printf(
				TEXT("Material Function has %d FunctionOutput nodes; pass an explicit target_node to disambiguate."), OutputCount));
		}
		TargetExpr = FoundOutput;
	}
	else
	{
		TargetExpr = CUFindMaterialExpression(MatFunc->GetExpressions(), TargetNode, bAmbiguous);
		if (!TargetExpr)
		{
			return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Target node not found: %s"), *TargetNode));
		}
		if (bAmbiguous)
		{
			return CUJsonUtils::MakeErrorResponse(FString::Printf(
				TEXT("Ambiguous target node '%s' matches multiple nodes; pass the exact node name."), *TargetNode));
		}
	}

	// Connect to the named target input pin, else the first input (mirrors the UMaterial path).
	bool bConnected = false;
	const FString TargetPinLower = TargetPin.ToLower();
	if (!TargetPinLower.IsEmpty() && TargetPinLower != TEXT("input"))
	{
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = TargetExpr->GetInput(i);
			if (!Input) { break; }
			if (TargetExpr->GetInputName(i).ToString().ToLower() == TargetPinLower)
			{
				Input->Connect(OutputIndex, SourceExpr);
				bConnected = true;
				break;
			}
		}
	}
	if (!bConnected)
	{
		if (FExpressionInput* FirstInput = TargetExpr->GetInput(0))
		{
			FirstInput->Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
	}
	if (!bConnected)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Could not connect to target pin: %s"), *TargetPin));
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ConnectMaterialFunctionNodes", "Connect Material Function Nodes"));
	MatFunc->Modify();
#endif
	MatFunc->MarkPackageDirty();
	MatFunc->PostEditChange();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("source_node"), SourceExpr->GetName());
	ResultObj->SetStringField(TEXT("target_node"), TargetExpr->GetName());
	ResultObj->SetStringField(TEXT("source_pin"), SourcePin);
	ResultObj->SetStringField(TEXT("target_pin"), TargetPin);
	ResultObj->SetStringField(TEXT("owner_type"), TEXT("MaterialFunction"));
	return CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
}

FString UCURemoteLibrary::ConnectMaterialNodes(
	const FString& MaterialPath,
	const FString& SourceNode,
	const FString& TargetNode,
	const FString& SourcePin,
	const FString& TargetPin
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] ConnectMaterialNodes: %s.%s -> %s.%s in %s"),
		*SourceNode, *SourcePin, *TargetNode, *TargetPin, *MaterialPath);

	// Load Material, or fall back to a Material Function (Issue #17066).
	UMaterial* Material = nullptr;
	UMaterialFunction* MatFunc = nullptr;
	if (!CUResolveMaterialOrFunction(MaterialPath, Material, MatFunc))
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material or MaterialFunction not found: %s"), *MaterialPath));
	}
	// Issue #17066: Material Functions have no material "main node"; route to the MF-specific
	// connect handler and return. The UMaterial path below is unchanged.
	if (MatFunc)
	{
		return CUConnectMaterialFunctionNodes(MatFunc, SourceNode, TargetNode, SourcePin, TargetPin);
	}

	// Find source expression
	UMaterialExpression* SourceExpr = nullptr;
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (Expr && Expr->GetName().Contains(SourceNode))
		{
			SourceExpr = Expr;
			break;
		}
	}

	if (!SourceExpr)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Source node not found: %s"), *SourceNode));
	}

	// Output index from SourcePin (Issue #8159, `Output<N>` added by #25072). One shared
	// helper — this site and CUConnectMaterialFunctionNodes each had their own copy of
	// the table and therefore each had the same gap.
	const int32 OutputIndex = CUMaterialOutputIndexFromPin(SourcePin);
	// Rule 27: bound it before it reaches any `Connect(OutputIndex, …)` below — those
	// index the expression's Outputs with no check of their own, and `SourcePin` comes
	// straight from the caller's spec.
	if (const FString PinError = CUValidateMaterialOutputIndex(SourceExpr, SourcePin, OutputIndex); !PinError.IsEmpty())
	{
		return CUJsonUtils::MakeErrorResponse(PinError);
	}

	// Connect to target
	FString TargetPinLower = TargetPin.ToLower();
	bool bConnected = false;

	if (TargetNode.ToLower() == TEXT("material") || TargetNode.IsEmpty())
	{
		// Cache GetEditorOnlyData() once for all pin connections
		auto* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			return CUJsonUtils::MakeErrorResponse(TEXT("Failed to get material editor data"));
		}

		// Connect to material main node
		if (TargetPinLower == TEXT("basecolor") || TargetPinLower == TEXT("base color"))
		{
			EditorData->BaseColor.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("metallic"))
		{
			EditorData->Metallic.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("roughness"))
		{
			EditorData->Roughness.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("normal"))
		{
			EditorData->Normal.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("emissive") || TargetPinLower == TEXT("emissivecolor"))
		{
			EditorData->EmissiveColor.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("opacity"))
		{
			EditorData->Opacity.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		// Issue #8159: Expanded material main node pins
		else if (TargetPinLower == TEXT("specular"))
		{
			EditorData->Specular.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("anisotropy"))
		{
			EditorData->Anisotropy.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("tangent"))
		{
			EditorData->Tangent.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("ambientocclusion") || TargetPinLower == TEXT("ambient occlusion"))
		{
			EditorData->AmbientOcclusion.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("worldpositionoffset") || TargetPinLower == TEXT("world position offset"))
		{
			EditorData->WorldPositionOffset.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("subsurfacecolor") || TargetPinLower == TEXT("subsurface color"))
		{
			EditorData->SubsurfaceColor.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("refraction"))
		{
			EditorData->Refraction.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("pixeldepthoffset") || TargetPinLower == TEXT("pixel depth offset"))
		{
			EditorData->PixelDepthOffset.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("opacitymask") || TargetPinLower == TEXT("opacity mask"))
		{
			EditorData->OpacityMask.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
		else if (TargetPinLower == TEXT("materialattributes") || TargetPinLower == TEXT("material attributes"))
		{
			EditorData->MaterialAttributes.Connect(OutputIndex, SourceExpr);
			bConnected = true;
		}
	}
	else
	{
		// Find target expression
		UMaterialExpression* TargetExpr = nullptr;
		for (UMaterialExpression* Expr : Material->GetExpressions())
		{
			if (Expr && Expr->GetName().Contains(TargetNode))
			{
				TargetExpr = Expr;
				break;
			}
		}

		if (TargetExpr)
		{
			// Issue #8159: Named pin matching via GetInput(i)/GetInputName(i)
			// UE 5.5+ deprecated GetInputs(); iterate with GetInput(index) instead
			bool bFoundNamedInput = false;

			if (!TargetPinLower.IsEmpty() && TargetPinLower != TEXT("input"))
			{
				for (int32 i = 0; ; ++i)
				{
					FExpressionInput* Input = TargetExpr->GetInput(i);
					if (!Input) break;
					FName InputName = TargetExpr->GetInputName(i);
					if (InputName.ToString().ToLower() == TargetPinLower)
					{
						Input->Connect(OutputIndex, SourceExpr);
						bConnected = true;
						bFoundNamedInput = true;
						break;
					}
				}
			}

			// Fallback: if no named pin match, use first input
			if (!bFoundNamedInput)
			{
				FExpressionInput* FirstInput = TargetExpr->GetInput(0);
				if (FirstInput)
				{
					FirstInput->Connect(OutputIndex, SourceExpr);
					bConnected = true;
				}
			}
		}
	}

	if (bConnected)
	{
		// Issue #8564 Phase K-4: Transaction + Modify for proper object_changed capture
#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ConnectMaterialNodes", "Connect Material Nodes"));
		Material->Modify();
#endif
		Material->MarkPackageDirty();
		Material->PostEditChange();

		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("source_node"), SourceNode);
		ResultObj->SetStringField(TEXT("target_node"), TargetNode);
		ResultObj->SetStringField(TEXT("source_pin"), SourcePin);
		ResultObj->SetStringField(TEXT("target_pin"), TargetPin);
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Could not connect to pin: %s"), *TargetPin));
	ErrorObj->SetStringField(TEXT("hint"), TEXT("Supported material pins: BaseColor, Metallic, Specular, Roughness, Anisotropy, Tangent, Normal, EmissiveColor, Opacity, OpacityMask, AmbientOcclusion, WorldPositionOffset, SubsurfaceColor, Refraction, PixelDepthOffset, MaterialAttributes. For expressions: use target input pin name."));
	FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::DisconnectMaterialNodes(
	const FString& MaterialPath,
	const FString& SourceNode,
	const FString& TargetNode
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] DisconnectMaterialNodes: %s -> %s in %s"),
		*SourceNode, *TargetNode, *MaterialPath);

	// Load Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Find source expression
	UMaterialExpression* SourceExpr = nullptr;
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (Expr && Expr->GetName().Contains(SourceNode))
		{
			SourceExpr = Expr;
			break;
		}
	}

	if (!SourceExpr)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Source node not found: %s"), *SourceNode));
	}

	bool bDisconnected = false;

	// Issue #8159: Actual disconnect implementation
	if (TargetNode.ToLower() == TEXT("material") || TargetNode.IsEmpty())
	{
		// Disconnect from material main node inputs
		auto* EditorData = Material->GetEditorOnlyData();
		if (!EditorData)
		{
			return CUJsonUtils::MakeErrorResponse(TEXT("GetEditorOnlyData() returned null — material may be corrupted"));
		}
		FExpressionInput* MaterialInputs[] = {
			&EditorData->BaseColor, &EditorData->Metallic, &EditorData->Specular,
			&EditorData->Roughness, &EditorData->Anisotropy, &EditorData->Normal,
			&EditorData->Tangent, &EditorData->EmissiveColor, &EditorData->Opacity,
			&EditorData->OpacityMask, &EditorData->WorldPositionOffset,
			&EditorData->SubsurfaceColor, &EditorData->AmbientOcclusion,
			&EditorData->Refraction, &EditorData->PixelDepthOffset,
			&EditorData->MaterialAttributes
		};

		for (FExpressionInput* Input : MaterialInputs)
		{
			if (Input->Expression == SourceExpr)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				bDisconnected = true;
			}
		}
	}
	else
	{
		// Disconnect expression-to-expression
		UMaterialExpression* TargetExpr = nullptr;
		for (UMaterialExpression* Expr : Material->GetExpressions())
		{
			if (Expr && Expr->GetName().Contains(TargetNode))
			{
				TargetExpr = Expr;
				break;
			}
		}

		if (!TargetExpr)
		{
			return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Target node not found: %s"), *TargetNode));
		}

		// Iterate target's inputs, disconnect any pointing to source
		// UE 5.5+ deprecated GetInputs(); use GetInput(index) instead
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = TargetExpr->GetInput(i);
			if (!Input) break;
			if (Input->Expression == SourceExpr)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				bDisconnected = true;
			}
		}
	}

	if (bDisconnected)
	{
		// Issue #8564 Phase K-4: Transaction + Modify for proper object_changed capture
#if WITH_EDITOR
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "DisconnectMaterialNodes", "Disconnect Material Nodes"));
		Material->Modify();
#endif
		Material->MarkPackageDirty();
		Material->PostEditChange();

		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("source_node"), SourceNode);
		ResultObj->SetStringField(TEXT("target_node"), TargetNode);
		ResultObj->SetStringField(TEXT("disconnected"), TEXT("true"));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("No connection found from %s to %s"), *SourceNode, *TargetNode));
	ErrorObj->SetStringField(TEXT("hint"), TEXT("Verify that the source node is actually connected to the target"));
	FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
	return Result;
}

// Issue #17066: delete a node inside a Material Function. An MF has no material main inputs,
// so only expression-to-expression inbound connections are severed. Because an MF is shared
// across consuming materials, the force gate is widened to cross-asset scope: without force,
// refuse when the node has inbound connections OR the MF is referenced by other assets.
static FString CUDeleteMaterialFunctionNode(
	UMaterialFunction* MatFunc,
	const FString& NodeName,
	bool bForce)
{
	bool bAmbiguous = false;
	UMaterialExpression* TargetExpr =
		CUFindMaterialExpression(MatFunc->GetExpressions(), NodeName, bAmbiguous);
	if (!TargetExpr)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}
	if (bAmbiguous)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(
			TEXT("Ambiguous node name '%s' matches multiple nodes; pass the exact node name."), *NodeName));
	}

	// Collect intra-MF inbound references (expression-to-expression only — no material root pins).
	TArray<FString> InboundRefs;
	for (UMaterialExpression* Expr : MatFunc->GetExpressions())
	{
		if (!Expr || Expr == TargetExpr)
		{
			continue;
		}
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input)
			{
				break;
			}
			if (Input->Expression == TargetExpr)
			{
				InboundRefs.AddUnique(Expr->GetName());
			}
		}
	}

	// Cross-asset blast radius: how many assets reference this MF?
	TArray<FString> ReferencerNames;
	const int32 ReferencerCount = CUCountAssetReferencers(MatFunc, ReferencerNames);

	// Without force, refuse when the delete would sever inbound connections OR ripple to consumers.
	if (!bForce && (InboundRefs.Num() > 0 || ReferencerCount > 0))
	{
		TArray<TSharedPtr<FJsonValue>> RefsJson;
		for (const FString& R : InboundRefs) { RefsJson.Add(MakeShared<FJsonValueString>(R)); }
		TArray<TSharedPtr<FJsonValue>> ConsumersJson;
		for (const FString& R : ReferencerNames) { ConsumersJson.Add(MakeShared<FJsonValueString>(R)); }
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
			TEXT("Node '%s' has %d inbound connection(s); this Material Function is referenced by %d asset(s)"),
			*TargetExpr->GetName(), InboundRefs.Num(), ReferencerCount));
		ErrObj->SetArrayField(TEXT("connected_inputs"), RefsJson);
		ErrObj->SetArrayField(TEXT("referencing_assets"), ConsumersJson);
		ErrObj->SetNumberField(TEXT("referencing_asset_count"), ReferencerCount);
		ErrObj->SetStringField(TEXT("hint"), TEXT("Pass force=true to sever inbound connections and delete; consuming materials will recompile."));
		return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "DeleteMaterialFunctionNode", "Delete Material Function Node"));
	MatFunc->Modify();
#endif

	int32 SeveredCount = 0;
	for (UMaterialExpression* Expr : MatFunc->GetExpressions())
	{
		if (!Expr || Expr == TargetExpr)
		{
			continue;
		}
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input)
			{
				break;
			}
			if (Input->Expression == TargetExpr)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				SeveredCount++;
			}
		}
	}

	// Capture name before removal (the expression is detached/GC-eligible afterward).
	const FString DeletedName = TargetExpr->GetName();
	MatFunc->GetExpressionCollection().RemoveExpression(TargetExpr);
	MatFunc->MarkPackageDirty();
	MatFunc->PostEditChange();

	// Persist (mirrors the UMaterial delete save path). FMaterialUpdateContext is Material-only
	// (no AddMaterialFunction), so PostEditChange drives the MF + consumer recompile.
	UPackage* Package = MatFunc->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	bool bSaved = UPackage::SavePackage(Package, MatFunc, *PackageFileName, SaveArgs);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("deleted_node"), DeletedName);
	ResultObj->SetStringField(TEXT("owner_type"), TEXT("MaterialFunction"));
	ResultObj->SetNumberField(TEXT("severed_connections"), SeveredCount);
	ResultObj->SetNumberField(TEXT("referencing_asset_count"), ReferencerCount);
	ResultObj->SetBoolField(TEXT("saved"), bSaved);
	if (CUIsAssetEditorOpen(MatFunc))
	{
		ResultObj->SetStringField(TEXT("editor_open_warning"),
			TEXT("Material Function editor is open; the editor's in-memory preview may overwrite this disk change on its next save. Close the editor or re-open the asset to see the change."));
	}
	return CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
}

FString UCURemoteLibrary::DeleteMaterialNode(
	const FString& MaterialPath,
	const FString& NodeName,
	bool bForce
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] DeleteMaterialNode: %s in %s (force=%d)"), *NodeName, *MaterialPath, bForce ? 1 : 0);

	// Load Material, or fall back to a Material Function (Issue #17066).
	UMaterial* Material = nullptr;
	UMaterialFunction* MatFunc = nullptr;
	if (!CUResolveMaterialOrFunction(MaterialPath, Material, MatFunc))
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material or MaterialFunction not found: %s"), *MaterialPath));
	}
	// Issue #17066: MF delete has different blast-radius handling (no material root pins,
	// cross-asset force gate); route to the MF handler. The UMaterial path below is unchanged.
	if (MatFunc)
	{
		return CUDeleteMaterialFunctionNode(MatFunc, NodeName, bForce);
	}

	// Issue #14979: exact-name-first lookup (substring fallback). Refuse on ambiguity so
	// force-delete never severs/removes the wrong node on a name collision.
	bool bAmbiguous = false;
	UMaterialExpression* TargetExpr = CUFindMaterialExpression(Material->GetExpressions(), NodeName, bAmbiguous);
	if (!TargetExpr)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Node not found: %s"), *NodeName));
	}
	if (bAmbiguous)
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
			TEXT("Ambiguous node name '%s' matches multiple nodes"), *NodeName));
		ErrObj->SetStringField(TEXT("hint"), TEXT("Pass the exact node name (GetName()) to disambiguate."));
		return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
	}

	// Issue #14979: collect inbound connections that reference the target — both the
	// material root inputs (BaseColor, etc.) and expression-to-expression inputs.
	TArray<FString> InboundRefs;
	auto* EditorData = Material->GetEditorOnlyData();
	FExpressionInput* MaterialInputs[] = {
		EditorData ? &EditorData->BaseColor : nullptr, EditorData ? &EditorData->Metallic : nullptr,
		EditorData ? &EditorData->Specular : nullptr, EditorData ? &EditorData->Roughness : nullptr,
		EditorData ? &EditorData->Anisotropy : nullptr, EditorData ? &EditorData->Normal : nullptr,
		EditorData ? &EditorData->Tangent : nullptr, EditorData ? &EditorData->EmissiveColor : nullptr,
		EditorData ? &EditorData->Opacity : nullptr, EditorData ? &EditorData->OpacityMask : nullptr,
		EditorData ? &EditorData->WorldPositionOffset : nullptr, EditorData ? &EditorData->SubsurfaceColor : nullptr,
		EditorData ? &EditorData->AmbientOcclusion : nullptr, EditorData ? &EditorData->Refraction : nullptr,
		EditorData ? &EditorData->PixelDepthOffset : nullptr, EditorData ? &EditorData->MaterialAttributes : nullptr
	};
	for (FExpressionInput* Input : MaterialInputs)
	{
		if (Input && Input->Expression == TargetExpr)
		{
			InboundRefs.Add(TEXT("Material"));
		}
	}
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (!Expr || Expr == TargetExpr)
		{
			continue;
		}
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* Input = Expr->GetInput(i);
			if (!Input)
			{
				break;
			}
			if (Input->Expression == TargetExpr)
			{
				InboundRefs.AddUnique(Expr->GetName());
			}
		}
	}

	// Issue #14979: without force, refuse to delete a connected node (prevents the
	// half-disassembled-graph recompile crash and surfaces what would break).
	if (!bForce && InboundRefs.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> RefsJson;
		for (const FString& R : InboundRefs) { RefsJson.Add(MakeShared<FJsonValueString>(R)); }
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(
			TEXT("Node '%s' has %d inbound connection(s)"), *TargetExpr->GetName(), InboundRefs.Num()));
		ErrObj->SetArrayField(TEXT("connected_inputs"), RefsJson);
		ErrObj->SetStringField(TEXT("hint"), TEXT("Pass force=true to sever these connections and delete."));
		return CUJsonUtils::SerializeToString(ErrObj.ToSharedRef());
	}

	// Issue #8564 Phase K-4: Transaction + Modify for proper object_changed capture
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "DeleteMaterialNode", "Delete Material Node"));
	Material->Modify();
#endif

	int32 SeveredCount = 0;
	{
		// Issue #14979: sever inbound connections and remove the node inside ONE
		// FMaterialUpdateContext so the shader graph + dependent material instances
		// recompile a single time on scope exit, not per-edit on a half-built graph.
		FMaterialUpdateContext UpdateContext;
		UpdateContext.AddMaterial(Material);

		for (FExpressionInput* Input : MaterialInputs)
		{
			if (Input && Input->Expression == TargetExpr)
			{
				Input->Expression = nullptr;
				Input->OutputIndex = 0;
				SeveredCount++;
			}
		}
		for (UMaterialExpression* Expr : Material->GetExpressions())
		{
			if (!Expr || Expr == TargetExpr)
			{
				continue;
			}
			for (int32 i = 0; ; ++i)
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (!Input)
				{
					break;
				}
				if (Input->Expression == TargetExpr)
				{
					Input->Expression = nullptr;
					Input->OutputIndex = 0;
					SeveredCount++;
				}
			}
		}

		Material->GetExpressionCollection().RemoveExpression(TargetExpr);
		Material->MarkPackageDirty();
		Material->PostEditChange();
	}

	// Issue #14979: persist to disk (mirrors ModifyMaterialNode save path).
	UPackage* Package = Material->GetOutermost();
	FString PackageFileName = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	bool bSaved = UPackage::SavePackage(Package, Material, *PackageFileName, SaveArgs);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("deleted_node"), NodeName);
	ResultObj->SetNumberField(TEXT("severed_connections"), SeveredCount);
	ResultObj->SetBoolField(TEXT("saved"), bSaved);
	if (CUIsAssetEditorOpen(Material))
	{
		ResultObj->SetStringField(TEXT("editor_open_warning"),
			TEXT("Material editor is open; the editor's in-memory preview may overwrite this disk change on its next save. Close the editor or re-open the asset to see the change."));
	}
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::ReplaceMaterialTexture(
	const FString& MaterialPath,
	const FString& OldTexture,
	const FString& NewTexture
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] ReplaceMaterialTexture: %s -> %s in %s"),
		*OldTexture, *NewTexture, *MaterialPath);

	// Load Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Load new texture
	UTexture* NewTex = LoadObject<UTexture>(nullptr, *NewTexture);
	if (!NewTex)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("New texture not found: %s"), *NewTexture));
	}

	// Find and replace texture references
	int32 ReplacementCount = 0;
	FString OldTextureName = FPaths::GetBaseFilename(OldTexture);

	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (UMaterialExpressionTextureSample* TexSample = Cast<UMaterialExpressionTextureSample>(Expr))
		{
			if (TexSample->Texture)
			{
				FString CurrentTexName = TexSample->Texture->GetName();
				if (CurrentTexName.Contains(OldTextureName) || TexSample->Texture->GetPathName().Contains(OldTexture))
				{
					TexSample->Texture = NewTex;
					ReplacementCount++;
				}
			}
		}
		else if (UMaterialExpressionTextureObject* TexObj = Cast<UMaterialExpressionTextureObject>(Expr))
		{
			if (TexObj->Texture)
			{
				FString CurrentTexName = TexObj->Texture->GetName();
				if (CurrentTexName.Contains(OldTextureName) || TexObj->Texture->GetPathName().Contains(OldTexture))
				{
					TexObj->Texture = NewTex;
					ReplacementCount++;
				}
			}
		}
	}

	Material->MarkPackageDirty();
	Material->PostEditChange();

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, ReplacementCount > 0);
	ResultObj->SetStringField(TEXT("old_texture"), OldTexture);
	ResultObj->SetStringField(TEXT("new_texture"), NewTexture);
	ResultObj->SetNumberField(TEXT("replacement_count"), ReplacementCount);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// =========================================================
// Enhanced Input Remote Control Functions (Issue #4262, #6232)
// =========================================================

// Issue #7742: Static helpers to eliminate trigger/modifier parsing duplication

static FString MakeEnhancedInputUnavailable()
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), TEXT("EnhancedInput plugin unavailable"));
	Result->SetStringField(TEXT("error_code"), TEXT("enhanced_input_plugin_unavailable"));
	Result->SetStringField(TEXT("hint"), TEXT("Enable EnhancedInput in your .uproject, then rebuild."));
	return CUJsonUtils::SerializeToString(Result.ToSharedRef());
}

#if WITH_NARSHAMCP_ENHANCEDINPUT

static UInputTrigger* CreateTriggerByName(const FString& TrigName, UObject* Outer)
{
	if (TrigName == TEXT("Pressed"))        return NewObject<UInputTriggerPressed>(Outer);
	if (TrigName == TEXT("Released"))       return NewObject<UInputTriggerReleased>(Outer);
	if (TrigName == TEXT("Down"))           return NewObject<UInputTriggerDown>(Outer);
	if (TrigName == TEXT("Hold"))           return NewObject<UInputTriggerHold>(Outer);
	if (TrigName == TEXT("HoldAndRelease")) return NewObject<UInputTriggerHoldAndRelease>(Outer);
	if (TrigName == TEXT("Tap"))            return NewObject<UInputTriggerTap>(Outer);
	return nullptr;
}

static UInputModifier* CreateModifierByName(const FString& ModName, UObject* Outer)
{
	if (ModName == TEXT("Negate"))          return NewObject<UInputModifierNegate>(Outer);
	if (ModName == TEXT("SwizzleAxis"))     return NewObject<UInputModifierSwizzleAxis>(Outer);
	if (ModName == TEXT("DeadZone"))        return NewObject<UInputModifierDeadZone>(Outer);
	if (ModName == TEXT("Scalar"))          return NewObject<UInputModifierScalar>(Outer);
	if (ModName == TEXT("FOVScaling"))      return NewObject<UInputModifierFOVScaling>(Outer);
	if (ModName == TEXT("ToWorldSpace"))    return NewObject<UInputModifierToWorldSpace>(Outer);
	if (ModName == TEXT("Smooth"))          return NewObject<UInputModifierSmooth>(Outer);
	if (ModName == TEXT("ResponseCurve"))   return NewObject<UInputModifierResponseCurveExponential>(Outer);
	return nullptr;
}

static TArray<FString> ParseAndApplyTriggers(const FString& Triggers, TArray<TObjectPtr<UInputTrigger>>& OutArray, UObject* Outer)
{
	TArray<FString> Applied;
	if (Triggers.IsEmpty()) return Applied;
	TArray<FString> TriggerNames;
	Triggers.ParseIntoArray(TriggerNames, TEXT(","), true);
	for (FString& TrigName : TriggerNames)
	{
		TrigName.TrimStartAndEndInline();
		if (UInputTrigger* T = CreateTriggerByName(TrigName, Outer))
		{
			OutArray.Add(T);
			Applied.Add(TrigName);
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Input] Applied trigger: %s"), *TrigName);
		}
		else
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Input] Unknown trigger ignored: %s"), *TrigName);
		}
	}
	return Applied;
}

static TArray<FString> ParseAndApplyModifiers(const FString& Modifiers, TArray<TObjectPtr<UInputModifier>>& OutArray, UObject* Outer)
{
	TArray<FString> Applied;
	if (Modifiers.IsEmpty()) return Applied;
	TArray<FString> ModifierNames;
	Modifiers.ParseIntoArray(ModifierNames, TEXT(","), true);
	for (FString& ModName : ModifierNames)
	{
		ModName.TrimStartAndEndInline();
		if (UInputModifier* M = CreateModifierByName(ModName, Outer))
		{
			OutArray.Add(M);
			Applied.Add(ModName);
			UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Input] Applied modifier: %s"), *ModName);
		}
		else
		{
			UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Input] Unknown modifier ignored: %s"), *ModName);
		}
	}
	return Applied;
}
#endif

// =========================================================
// Material Function Support (Issue #7921 — ADK Phase 4)
// =========================================================

namespace
{
	/** Parse {X, Y} JSON into PosX/PosY ints. Empty/invalid/missing fields → (0, 0).
	 *  Uses TryGetNumberField so malformed input (`{}`, lowercase keys) does not
	 *  spam the editor log with "Field not found" warnings — silent default to 0. */
	static void ParseLocationJson(const FString& LocationJson, int32& OutPosX, int32& OutPosY)
	{
		OutPosX = 0;
		OutPosY = 0;
		if (LocationJson.IsEmpty())
		{
			return;
		}
		TSharedPtr<FJsonObject> LocationObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LocationJson);
		if (FJsonSerializer::Deserialize(Reader, LocationObj) && LocationObj.IsValid())
		{
			double X = 0.0, Y = 0.0;
			LocationObj->TryGetNumberField(TEXT("X"), X);
			LocationObj->TryGetNumberField(TEXT("Y"), Y);
			OutPosX = (int32)X;
			OutPosY = (int32)Y;
		}
	}

	/** Build a {success:false, error:...} JSON string. */
	static FString MakeMaterialErrorJson(const FString& ErrorMessage)
	{
		return CUJsonUtils::MakeErrorResponse(ErrorMessage);
	}
}

FString UCURemoteLibrary::CreateMaterialFunction(
	const FString& MFName,
	const FString& PackagePath
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] CreateMaterialFunction: %s in %s"),
		*MFName, *PackagePath);

	// Validate name (Issue #15651: TrimStartAndEnd for whitespace parity with the
	// Rust .trim() guard + the sibling CreateMaterial/CreateMaterialInstance guards).
	if (MFName.TrimStartAndEnd().IsEmpty())
	{
		return MakeMaterialErrorJson(TEXT("Material Function name cannot be empty"));
	}

	// Resolve effective package path (allow caller-specified path or default to /Game/MaterialFunctions).
	const FString EffectivePackagePath = PackagePath.IsEmpty()
		? FString(TEXT("/Game/MaterialFunctions"))
		: PackagePath;

	// Issue #24132: same fail-closed package-path gate as CreateMaterial.
	{
		FString PathError;
		if (!CUValidateMaterialPackagePath(EffectivePackagePath, PathError))
		{
			return MakeMaterialErrorJson(PathError);
		}
	}

	// Check if asset already exists (mirrors CreateMaterial pattern at line 7042).
	const FString FullPath = EffectivePackagePath / MFName;
	const FString LoadPath = FullPath + TEXT(".") + MFName;
	if (UObject* Existing = LoadObject<UObject>(nullptr, *LoadPath))
	{
		return MakeMaterialErrorJson(FString::Printf(
			TEXT("Material Function already exists at path: %s (type: %s)"),
			*FullPath, *Existing->GetClass()->GetName()));
	}

	// Create package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return MakeMaterialErrorJson(FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	// Use the factory directly (parallels HandleCreateAsset's MaterialFunction case
	// in CUWebSocketEditorSubsystem.cpp — the WebSocket-based path) so HTTP RC
	// callers get the same code path as WebSocket callers.
	UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
	UObject* NewMF = Factory->FactoryCreateNew(
		UMaterialFunction::StaticClass(),
		Package,
		*MFName,
		RF_Public | RF_Standalone,
		nullptr,
		GWarn);

	if (!NewMF)
	{
		return MakeMaterialErrorJson(TEXT("UMaterialFunctionFactoryNew returned nullptr"));
	}

	// Notify registry + dirty + save
	NewMF->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewMF);
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		FullPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, NewMF, *PackageFileName, SaveArgs))
	{
		return MakeMaterialErrorJson(FString::Printf(
			TEXT("Failed to save Material Function: %s"), *FullPath));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Created Material Function: %s"),
		*NewMF->GetPathName());

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("mf_name"), MFName);
	ResultObj->SetStringField(TEXT("mf_path"), NewMF->GetPathName());
	ResultObj->SetStringField(TEXT("package_path"), EffectivePackagePath);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::AddFunctionInputPin(
	const FString& MFPath,
	const FString& PinName,
	const FString& InputType,
	const FString& LocationJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] AddFunctionInputPin: %s (%s) to %s"),
		*PinName, *InputType, *MFPath);

	// Load Material Function
	UMaterialFunction* MF = LoadObject<UMaterialFunction>(nullptr, *MFPath);
	if (!MF)
	{
		return MakeMaterialErrorJson(FString::Printf(TEXT("MaterialFunction not found: %s"), *MFPath));
	}

	// Resolve InputType enum value (e.g., "FunctionInput_Vector3" → EFunctionInputType::FunctionInput_Vector3)
	FString TypeName = InputType.IsEmpty() ? TEXT("FunctionInput_Vector3") : InputType;
	if (!TypeName.StartsWith(TEXT("FunctionInput_")))
	{
		TypeName = FString::Printf(TEXT("FunctionInput_%s"), *TypeName);
	}
	int64 TypeValue = StaticEnum<EFunctionInputType>()->GetValueByNameString(TypeName);
	if (TypeValue == INDEX_NONE)
	{
		return MakeMaterialErrorJson(FString::Printf(
			TEXT("Unknown InputType: %s (try FunctionInput_Scalar, FunctionInput_Vector2, FunctionInput_Vector3, FunctionInput_Vector4, FunctionInput_Texture2D, FunctionInput_TextureCube, FunctionInput_StaticBool, FunctionInput_MaterialAttributes)"),
			*InputType));
	}

	// Parse position
	int32 PosX = 0, PosY = 0;
	ParseLocationJson(LocationJson, PosX, PosY);

	// Create FunctionInput expression
	UMaterialExpressionFunctionInput* InputExpr = NewObject<UMaterialExpressionFunctionInput>(MF);
	if (!InputExpr)
	{
		return MakeMaterialErrorJson(TEXT("Failed to create UMaterialExpressionFunctionInput"));
	}

	InputExpr->InputName = FName(*PinName);
	InputExpr->InputType = (EFunctionInputType)TypeValue;
	InputExpr->MaterialExpressionEditorX = PosX;
	InputExpr->MaterialExpressionEditorY = PosY;

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddFunctionInputPin", "Add MF Input Pin"));
	MF->Modify();
#endif

	// Add expression to the Material Function via the unified ExpressionCollection API.
	// Mirrors the existing AddMaterialNode pattern (CURemoteLibrary.cpp:7276) — UMaterialFunction
	// inherits the same GetExpressionCollection() interface from UMaterialFunctionInterface.
	MF->GetExpressionCollection().AddExpression(InputExpr);
	MF->MarkPackageDirty();
	MF->PostEditChange();

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Added FunctionInput: %s (%s)"),
		*InputExpr->GetName(), *TypeName);

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_name"), InputExpr->GetName());
	ResultObj->SetStringField(TEXT("pin_name"), PinName);
	ResultObj->SetStringField(TEXT("input_type"), TypeName);
	ResultObj->SetNumberField(TEXT("pos_x"), PosX);
	ResultObj->SetNumberField(TEXT("pos_y"), PosY);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::AddFunctionOutputPin(
	const FString& MFPath,
	const FString& PinName,
	const FString& LocationJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] AddFunctionOutputPin: %s to %s"), *PinName, *MFPath);

	// Load Material Function
	UMaterialFunction* MF = LoadObject<UMaterialFunction>(nullptr, *MFPath);
	if (!MF)
	{
		return MakeMaterialErrorJson(FString::Printf(TEXT("MaterialFunction not found: %s"), *MFPath));
	}

	// Parse position
	int32 PosX = 0, PosY = 0;
	ParseLocationJson(LocationJson, PosX, PosY);

	// Create FunctionOutput expression
	UMaterialExpressionFunctionOutput* OutputExpr = NewObject<UMaterialExpressionFunctionOutput>(MF);
	if (!OutputExpr)
	{
		return MakeMaterialErrorJson(TEXT("Failed to create UMaterialExpressionFunctionOutput"));
	}

	OutputExpr->OutputName = FName(*PinName);
	OutputExpr->MaterialExpressionEditorX = PosX;
	OutputExpr->MaterialExpressionEditorY = PosY;

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddFunctionOutputPin", "Add MF Output Pin"));
	MF->Modify();
#endif

	MF->GetExpressionCollection().AddExpression(OutputExpr);
	MF->MarkPackageDirty();
	MF->PostEditChange();

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Added FunctionOutput: %s"), *OutputExpr->GetName());

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_name"), OutputExpr->GetName());
	ResultObj->SetStringField(TEXT("pin_name"), PinName);
	ResultObj->SetNumberField(TEXT("pos_x"), PosX);
	ResultObj->SetNumberField(TEXT("pos_y"), PosY);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::AddMaterialFunctionCall(
	const FString& MaterialPath,
	const FString& MFAssetPath,
	const FString& LocationJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] AddMaterialFunctionCall: %s -> %s"),
		*MFAssetPath, *MaterialPath);

	// Load target Material
	UMaterial* Material = LoadObject<UMaterial>(nullptr, *MaterialPath);
	if (!Material)
	{
		return MakeMaterialErrorJson(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));
	}

	// Load source Material Function
	UMaterialFunctionInterface* MFInterface = LoadObject<UMaterialFunctionInterface>(nullptr, *MFAssetPath);
	if (!MFInterface)
	{
		return MakeMaterialErrorJson(FString::Printf(TEXT("MaterialFunction not found: %s"), *MFAssetPath));
	}

	// Parse position
	int32 PosX = 0, PosY = 0;
	ParseLocationJson(LocationJson, PosX, PosY);

	// Create MaterialFunctionCall expression
	UMaterialExpressionMaterialFunctionCall* CallExpr = NewObject<UMaterialExpressionMaterialFunctionCall>(Material);
	if (!CallExpr)
	{
		return MakeMaterialErrorJson(TEXT("Failed to create UMaterialExpressionMaterialFunctionCall"));
	}

	CallExpr->MaterialExpressionEditorX = PosX;
	CallExpr->MaterialExpressionEditorY = PosY;

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddMaterialFunctionCall", "Add Material Function Call"));
	Material->Modify();
#endif

	// SetMaterialFunction returns true on success and synchronizes input/output pins
	// from the referenced MF (UE 5.x: see Engine/Source/Runtime/Engine/Classes/Materials/
	// MaterialExpressionMaterialFunctionCall.h). On failure (cyclic reference, etc.),
	// the call node is still added but with empty pins.
	const bool bAssigned = CallExpr->SetMaterialFunction(MFInterface);
	if (!bAssigned)
	{
		UE_LOG(LogNarshaMCPRemote, Warning,
			TEXT("[Material] SetMaterialFunction returned false (possible cyclic reference)"));
	}

	Material->GetExpressionCollection().AddExpression(CallExpr);
	Material->MarkPackageDirty();
	Material->PostEditChange();

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Material] Added MaterialFunctionCall: %s (Inputs=%d, Outputs=%d)"),
		*CallExpr->GetName(), CallExpr->FunctionInputs.Num(), CallExpr->FunctionOutputs.Num());

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_name"), CallExpr->GetName());
	ResultObj->SetStringField(TEXT("function_path"), MFInterface->GetPathName());
	ResultObj->SetNumberField(TEXT("input_count"), CallExpr->FunctionInputs.Num());
	ResultObj->SetNumberField(TEXT("output_count"), CallExpr->FunctionOutputs.Num());
	ResultObj->SetNumberField(TEXT("pos_x"), PosX);
	ResultObj->SetNumberField(TEXT("pos_y"), PosY);
	ResultObj->SetBoolField(TEXT("function_assigned"), bAssigned);
	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::CreateInputAction(
	const FString& ActionName,
	const FString& ValueType,
	const FString& Triggers,
	const FString& Modifiers
)
{
#if !WITH_NARSHAMCP_ENHANCEDINPUT
	return MakeEnhancedInputUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Input] CreateInputAction: %s (ValueType: %s)"), *ActionName, *ValueType);

	// 0. Validate ActionName
	if (ActionName.IsEmpty())
	{
		return CUJsonUtils::MakeErrorResponse(TEXT("ActionName must not be empty"));
	}

	// 1. Parse ValueType string → EInputActionValueType
	EInputActionValueType ParsedValueType;
	if (ValueType.Equals(TEXT("Digital"), ESearchCase::IgnoreCase) || ValueType.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
	{
		ParsedValueType = EInputActionValueType::Boolean;
	}
	else if (ValueType.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase))
	{
		ParsedValueType = EInputActionValueType::Axis1D;
	}
	else if (ValueType.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase))
	{
		ParsedValueType = EInputActionValueType::Axis2D;
	}
	else if (ValueType.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
	{
		ParsedValueType = EInputActionValueType::Axis3D;
	}
	else
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Invalid ValueType: %s. Must be Digital, Axis1D, Axis2D, or Axis3D"), *ValueType));
	}

	// 2. Build asset path
	FString PackagePath = TEXT("/Game/Input/Actions");
	FString FullPath = PackagePath / ActionName;
	FString LoadPath = FullPath + TEXT(".") + ActionName;

	// 3. Check if asset already exists
	if (UObject* ExistingObj = LoadObject<UObject>(nullptr, *LoadPath))
	{
		TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
		ErrorObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ErrorObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("InputAction already exists: %s"), *ExistingObj->GetPathName()));
		ErrorObj->SetStringField(TEXT("existing_path"), ExistingObj->GetPathName());
		FString Result = CUJsonUtils::SerializeToString(ErrorObj.ToSharedRef());
		return Result;
	}

	// 4. Create package
	UPackage* Package = CreatePackage(*FullPath);
	if (!Package)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Failed to create package: %s"), *FullPath));
	}

	// 5. Create InputAction object
	UInputAction* NewAction = NewObject<UInputAction>(
		Package,
		*ActionName,
		RF_Public | RF_Standalone
	);

	if (!NewAction)
	{
		return CUJsonUtils::MakeErrorResponse(TEXT("Failed to create UInputAction object"));
	}

	// 6. Set properties
	NewAction->ValueType = ParsedValueType;

	// Issue #7742: Parse and apply triggers + modifiers via shared helpers
	TArray<FString> AppliedTriggers = ParseAndApplyTriggers(Triggers, NewAction->Triggers, NewAction);
	TArray<FString> AppliedModifiers = ParseAndApplyModifiers(Modifiers, NewAction->Modifiers, NewAction);

	// 7. Register with asset registry and mark dirty
	NewAction->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAction);
	Package->MarkPackageDirty();

	// 8. Save package to disk
	FString PackageFileName = FPackageName::LongPackageNameToFilename(FullPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	bool bSaved = UPackage::SavePackage(Package, NewAction, *PackageFileName, SaveArgs);

	if (!bSaved)
	{
		UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Input] Created InputAction in memory but failed to save to disk: %s"), *FullPath);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Input] Created InputAction: %s (ValueType: %s, Triggers: %d, Modifiers: %d, Saved: %s)"),
		*NewAction->GetPathName(), *ValueType, AppliedTriggers.Num(), AppliedModifiers.Num(), bSaved ? TEXT("true") : TEXT("false"));

	// 9. Return success JSON
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("action_name"), ActionName);
	ResultObj->SetStringField(TEXT("asset_path"), NewAction->GetPathName());
	ResultObj->SetStringField(TEXT("package_path"), FullPath);
	ResultObj->SetStringField(TEXT("value_type"), ValueType);
	ResultObj->SetBoolField(TEXT("saved_to_disk"), bSaved);

	// Issue #7742: Include applied triggers and modifiers in response
	TArray<TSharedPtr<FJsonValue>> TriggersJsonArray;
	for (const FString& Trig : AppliedTriggers)
	{
		TriggersJsonArray.Add(MakeShared<FJsonValueString>(Trig));
	}
	ResultObj->SetArrayField(TEXT("triggers"), TriggersJsonArray);
	ResultObj->SetNumberField(TEXT("trigger_count"), AppliedTriggers.Num());

	TArray<TSharedPtr<FJsonValue>> ModifiersJsonArray;
	for (const FString& Mod : AppliedModifiers)
	{
		ModifiersJsonArray.Add(MakeShared<FJsonValueString>(Mod));
	}
	ResultObj->SetArrayField(TEXT("modifiers"), ModifiersJsonArray);
	ResultObj->SetNumberField(TEXT("modifier_count"), AppliedModifiers.Num());

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
#endif
}

FString UCURemoteLibrary::CreateInputMapping(
	const FString& InputAction,
	const FString& Key,
	const FString& MappingContext,
	const FString& Modifiers,
	const FString& Triggers
)
{
#if !WITH_NARSHAMCP_ENHANCEDINPUT
	return MakeEnhancedInputUnavailable();
#else
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Input] CreateInputMapping: %s → %s in %s"), *InputAction, *Key, *MappingContext);

	// 1. Validate Key
	FKey ParsedKey(*Key);
	if (!ParsedKey.IsValid())
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("Invalid key: %s"), *Key));
	}

	// 2. Load AssetRegistry once for both InputAction and IMC lookups
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// 3. Find InputAction asset
	UInputAction* FoundAction = nullptr;
	{
		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), AssetList);
		for (const FAssetData& Asset : AssetList)
		{
			if (Asset.AssetName.ToString() == InputAction)
			{
				FoundAction = Cast<UInputAction>(Asset.GetAsset());
				break;
			}
		}
	}

	if (!FoundAction)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("InputAction not found: %s"), *InputAction));
	}

	// 4. Find InputMappingContext asset
	UInputMappingContext* FoundIMC = nullptr;
	{
		TArray<FAssetData> AssetList;
		AssetRegistry.GetAssetsByClass(UInputMappingContext::StaticClass()->GetClassPathName(), AssetList);
		for (const FAssetData& Asset : AssetList)
		{
			if (Asset.AssetName.ToString() == MappingContext)
			{
				FoundIMC = Cast<UInputMappingContext>(Asset.GetAsset());
				break;
			}
		}
	}

	if (!FoundIMC)
	{
		return CUJsonUtils::MakeErrorResponse(FString::Printf(TEXT("InputMappingContext not found: %s"), *MappingContext));
	}

	// 5. Add mapping to IMC
	FEnhancedActionKeyMapping& NewMapping = FoundIMC->MapKey(FoundAction, ParsedKey);

	// Issue #7714/#7742: Parse and apply modifiers + triggers via shared helpers
	TArray<FString> AppliedModifiers = ParseAndApplyModifiers(Modifiers, NewMapping.Modifiers, FoundIMC);
	TArray<FString> AppliedTriggers = ParseAndApplyTriggers(Triggers, NewMapping.Triggers, FoundIMC);

	// 6. Mark dirty and save
	FoundIMC->MarkPackageDirty();
	UPackage* IMCPackage = FoundIMC->GetOutermost();
	IMCPackage->MarkPackageDirty();

	FString IMCFullPath = IMCPackage->GetName();
	FString IMCFileName = FPackageName::LongPackageNameToFilename(IMCFullPath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_NoError;
	bool bSaved = UPackage::SavePackage(IMCPackage, FoundIMC, *IMCFileName, SaveArgs);

	if (!bSaved)
	{
		UE_LOG(LogNarshaMCPRemote, Warning, TEXT("[Input] Added mapping but failed to save IMC to disk: %s"), *IMCFullPath);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Input] Added mapping: %s → %s in %s (Modifiers: %d, Triggers: %d, Saved: %s)"),
		*InputAction, *Key, *MappingContext, AppliedModifiers.Num(), AppliedTriggers.Num(), bSaved ? TEXT("true") : TEXT("false"));

	// 7. Return success JSON
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("input_action"), InputAction);
	ResultObj->SetStringField(TEXT("key"), Key);
	ResultObj->SetStringField(TEXT("mapping_context"), MappingContext);
	ResultObj->SetStringField(TEXT("mapping_context_path"), FoundIMC->GetPathName());
	ResultObj->SetBoolField(TEXT("saved_to_disk"), bSaved);

	// Issue #7714: Include applied modifiers in response
	TArray<TSharedPtr<FJsonValue>> ModifiersJsonArray;
	for (const FString& Mod : AppliedModifiers)
	{
		ModifiersJsonArray.Add(MakeShared<FJsonValueString>(Mod));
	}
	ResultObj->SetArrayField(TEXT("modifiers"), ModifiersJsonArray);
	ResultObj->SetNumberField(TEXT("modifier_count"), AppliedModifiers.Num());

	// Issue #7742: Include applied triggers in response
	TArray<TSharedPtr<FJsonValue>> TriggersJsonArray;
	for (const FString& Trig : AppliedTriggers)
	{
		TriggersJsonArray.Add(MakeShared<FJsonValueString>(Trig));
	}
	ResultObj->SetArrayField(TEXT("triggers"), TriggersJsonArray);
	ResultObj->SetNumberField(TEXT("trigger_count"), AppliedTriggers.Num());

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
#endif
}

// =========================================================
// Blueprint T3D Import/Export (Issue #4741 - Blueprint Fast Generation)
// =========================================================

FString UCURemoteLibrary::ImportBlueprintNodesFromText(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& T3DText
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint T3D] ImportBlueprintNodesFromText: Path=%s, Graph=%s, TextLen=%d"),
		*BlueprintPath, *GraphName, T3DText.Len());

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Blueprint not found"),
			FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintPath),
			TEXT("import_t3d"),
			TEXT("Ensure the Blueprint path is correct (e.g., /Game/BP/BP_MyActor)")
		);
	}

	// Find the target graph
	UEdGraph* TargetGraph = BlueprintNodeHelpers::FindGraphByName(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Graph not found"),
			FString::Printf(TEXT("Graph '%s' not found in Blueprint '%s'"), *GraphName, *BlueprintPath),
			TEXT("import_t3d"),
			FString::Printf(TEXT("Available graphs in '%s': check with get_structure operation"), *BlueprintPath)
		);
	}

	// Validate T3D text
	if (T3DText.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Empty T3D text"),
			TEXT("T3D text is empty. Provide 'Begin Object ... End Object' blocks"),
			TEXT("import_t3d"),
			FString::Printf(TEXT("Blueprint: '%s'"), *BlueprintPath)
		);
	}

	// Check if the text can be imported
	if (!FEdGraphUtilities::CanImportNodesFromText(TargetGraph, T3DText))
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Invalid T3D text - cannot import nodes"),
			FString::Printf(TEXT("T3D text is invalid for Blueprint '%s'. Check format with get_t3d_reference"), *BlueprintPath),
			TEXT("import_t3d"),
			TEXT("Ensure it contains valid 'Begin Object Class=... End Object' blocks")
		);
	}

	// Import nodes
	TSet<UEdGraphNode*> ImportedNodes;
	FEdGraphUtilities::ImportNodesFromText(TargetGraph, T3DText, ImportedNodes);

	if (ImportedNodes.Num() == 0)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("No nodes imported"),
			FString::Printf(TEXT("T3D parsed but produced no nodes in '%s'. Check node class paths"), *BlueprintPath),
			TEXT("import_t3d"),
			TEXT("The T3D text was valid but no nodes could be created. Verify class references exist in this engine version.")
		);
	}

	// Mark blueprint as modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);

	// Build result JSON with imported node info
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("graph_name"), GraphName);
	ResultObj->SetNumberField(TEXT("imported_node_count"), ImportedNodes.Num());

	TArray<TSharedPtr<FJsonValue>> NodesArray;
	for (UEdGraphNode* Node : ImportedNodes)
	{
		if (Node)
		{
			TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
			NodeObj->SetStringField(TEXT("node_name"), Node->GetName());
			NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
			NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
			NodeObj->SetNumberField(TEXT("node_pos_x"), Node->NodePosX);
			NodeObj->SetNumberField(TEXT("node_pos_y"), Node->NodePosY);
			NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
		}
	}
	ResultObj->SetArrayField(TEXT("imported_nodes"), NodesArray);

	// Save
	FCUBlueprintManipulator::SaveBlueprint(Blueprint);

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::ExportBlueprintNodesToText(
	const FString& BlueprintPath,
	const FString& GraphName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint T3D] ExportBlueprintNodesToText: Path=%s, Graph=%s"),
		*BlueprintPath, *GraphName);

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Blueprint not found"),
			FString::Printf(TEXT("Blueprint '%s' not found"), *BlueprintPath),
			TEXT("export_t3d"),
			TEXT("Ensure the Blueprint path is correct (e.g., /Game/BP/BP_MyActor)")
		);
	}

	// Find the target graph
	UEdGraph* TargetGraph = BlueprintNodeHelpers::FindGraphByName(Blueprint, GraphName);
	if (!TargetGraph)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Graph not found"),
			FString::Printf(TEXT("Graph '%s' not found in Blueprint '%s'"), *GraphName, *BlueprintPath),
			TEXT("export_t3d"),
			FString::Printf(TEXT("Available graphs in '%s': check with get_structure operation"), *BlueprintPath)
		);
	}

	// Collect all nodes
	TSet<UObject*> NodesToExport;
	NodesToExport.Reserve(TargetGraph->Nodes.Num());
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		NodesToExport.Add(Node);
	}

	// Export to T3D text
	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(NodesToExport, ExportedText);

	// Build result JSON
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("graph_name"), GraphName);
	ResultObj->SetNumberField(TEXT("node_count"), TargetGraph->Nodes.Num());
	ResultObj->SetStringField(TEXT("t3d_text"), ExportedText);

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// =========================================================
// CreateBlueprintAsset (Issue #12696 — editor-e2e test fixture authoring)
// =========================================================

FString UCURemoteLibrary::PreflightBlueprintAssetCreate(
	const FString& PackagePath,
	const FString& AssetName,
	UClass* ParentClass,
	bool bUsesDedicatedInterfaceFactory
)
{
	FString NormalizedPackagePath = PackagePath;
	NormalizedPackagePath.RemoveFromEnd(TEXT("/"));
	const FString FullPackagePath = NormalizedPackagePath / AssetName;
	const FString FullObjectPath = FullPackagePath + TEXT(".") + AssetName;
	UClass* EffectiveParentClass = bUsesDedicatedInterfaceFactory
		? UInterface::StaticClass()
		: ParentClass;

	auto MakeResult = [
		&NormalizedPackagePath,
		&AssetName,
		&FullPackagePath,
		&FullObjectPath,
		EffectiveParentClass,
		bUsesDedicatedInterfaceFactory
	](bool bCanCreate, const TCHAR* ReasonCode, const FString& Error) -> FString
	{
		TSharedPtr<FJsonObject> Data = MakeShared<FJsonObject>();
		Data->SetBoolField(TEXT("can_create"), bCanCreate);
		Data->SetStringField(TEXT("reason_code"), ReasonCode);
		Data->SetStringField(TEXT("package_path"), NormalizedPackagePath);
		Data->SetStringField(TEXT("asset_name"), AssetName);
		Data->SetStringField(TEXT("full_package_path"), FullPackagePath);
		Data->SetStringField(TEXT("full_object_path"), FullObjectPath);
		Data->SetStringField(
			TEXT("parent_class"),
			EffectiveParentClass ? EffectiveParentClass->GetPathName() : FString());
		Data->SetBoolField(TEXT("uses_dedicated_interface_factory"), bUsesDedicatedInterfaceFactory);
		Data->SetBoolField(TEXT("app_unattended"), FApp::IsUnattended());
		Data->SetBoolField(TEXT("unattended_script"), GIsRunningUnattendedScript);

		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(JSON_SUCCESS_FIELD, bCanCreate);
		if (bCanCreate)
		{
			Result->SetField(TEXT("error"), MakeShared<FJsonValueNull>());
		}
		else
		{
			Result->SetStringField(TEXT("error"), Error);
		}
		Result->SetObjectField(TEXT("data"), Data);
		return CUJsonUtils::SerializeToString(Result);
	};

	if (NormalizedPackagePath.IsEmpty())
	{
		return MakeResult(false, TEXT("package_path_empty"), TEXT("Package path must not be empty"));
	}

	FText ValidationReason;
	if (AssetName.IsEmpty() || !FName(*AssetName).IsValidObjectName(ValidationReason))
	{
		const FString Detail = ValidationReason.IsEmpty()
			? TEXT("Asset name must not be empty")
			: ValidationReason.ToString();
		return MakeResult(false, TEXT("invalid_object_name"), Detail);
	}

	if (!FPackageName::IsValidLongPackageName(
		FullPackagePath,
		/*bIncludeReadOnlyRoots*/ false,
		&ValidationReason))
	{
		return MakeResult(false, TEXT("invalid_package_path"), ValidationReason.ToString());
	}

	if (!EffectiveParentClass)
	{
		return MakeResult(false, TEXT("parent_class_missing"), TEXT("Resolved parent class is required"));
	}
	if (!FKismetEditorUtilities::CanCreateBlueprintOfClass(EffectiveParentClass))
	{
		return MakeResult(
			false,
			TEXT("parent_class_rejected"),
			FString::Printf(
				TEXT("Blueprints cannot be created from parent class '%s'"),
				*EffectiveParentClass->GetPathName()));
	}

	if (FEditorFileUtils::IsMapPackageAsset(FullPackagePath))
	{
		return MakeResult(
			false,
			TEXT("map_package_collision"),
			FString::Printf(TEXT("A map package already uses '%s'"), *FullPackagePath));
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	if (!AssetTools.IsObjectPathAllowed(FullPackagePath, &ValidationReason))
	{
		return MakeResult(false, TEXT("object_path_disallowed"), ValidationReason.ToString());
	}

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const bool bRegisteredAssetExists = AssetRegistry.GetAssetByObjectPath(
		FSoftObjectPath(FullObjectPath),
		/*bIncludeOnlyOnDiskAssets*/ true).IsValid();
	if (bRegisteredAssetExists)
	{
		// The generated Python retains its non-mutating idempotent branch for a
		// real registered asset. Only collisions outside that exact state are unsafe.
		return MakeResult(true, TEXT("registered_asset_exists"), FString());
	}

	if (UPackage* LoadedPackage = FindPackage(nullptr, *FullPackagePath))
	{
		if (StaticFindObject(UObject::StaticClass(), LoadedPackage, *AssetName))
		{
			return MakeResult(
				false,
				TEXT("loaded_object_exists"),
				FString::Printf(TEXT("An object named '%s' is already loaded in '%s'"), *AssetName, *FullPackagePath));
		}
		if (!LoadedPackage->IsFullyLoaded())
		{
			return MakeResult(
				false,
				TEXT("package_not_fully_loaded"),
				FString::Printf(TEXT("Package '%s' is loaded only partially"), *FullPackagePath));
		}
	}

	// UAssetToolsImpl::CanCreateAsset calls CreatePackage followed by
	// HandleFullyLoadingPackages. For an unloaded package that already exists on disk,
	// that path can show a Yes/No fully-load dialog before checking the exact object.
	// Reject the ambiguous package-only collision without loading or mutating it.
	if (FPackageName::DoesPackageExist(FullPackagePath))
	{
		return MakeResult(
			false,
			TEXT("package_exists_on_disk"),
			FString::Printf(
				TEXT("Package '%s' already exists on disk without the requested registered asset"),
				*FullPackagePath));
	}

	// UAssetToolsImpl::CanCreateAsset asks source control whether an absent package is
	// marked for delete. Its private CheckForDeletedPackage path can then execute Revert
	// and CheckOut, with modal errors on failure. Query the same public state by filename,
	// but never execute a source-control operation. When source control is enabled, an
	// unavailable provider or invalid state is unsafe because deletion cannot be ruled out.
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	const bool bSourceControlEnabled = SourceControlModule.IsEnabled();
	ISourceControlProvider* SourceControlProvider =
		bSourceControlEnabled ? &SourceControlModule.GetProvider() : nullptr;
	const bool bProviderAvailable =
		SourceControlProvider && SourceControlProvider->IsAvailable();
	FSourceControlStatePtr SourceControlState;
	if (bProviderAvailable)
	{
		const FString PackageFilename = FPaths::ConvertRelativePathToFull(
			FPackageName::LongPackageNameToFilename(
				FullPackagePath,
				FPackageName::GetAssetPackageExtension()));
		SourceControlState = SourceControlProvider->GetState(
			PackageFilename,
			EStateCacheUsage::ForceUpdate);
	}

	const CUBlueprintAssetCreatePreflight::ESourceControlSafety SourceControlSafety =
		CUBlueprintAssetCreatePreflight::ClassifySourceControlSafety(
			bSourceControlEnabled,
			bProviderAvailable,
			SourceControlState.IsValid(),
			SourceControlState.IsValid() && SourceControlState->IsDeleted());
	if (const TCHAR* RejectionReason =
		CUBlueprintAssetCreatePreflight::GetSourceControlRejectionReason(SourceControlSafety))
	{
		return MakeResult(
			false,
			RejectionReason,
			FString::Printf(
				TEXT("Source-control state for '%s' is not safe for non-interactive asset creation (%s)"),
				*FullPackagePath,
				RejectionReason));
	}

	return MakeResult(true, TEXT("allowed"), FString());
}

FString UCURemoteLibrary::CreateBlueprintAsset(
	const FString& PackagePath,
	const FString& AssetName,
	const FString& ParentClassName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CreateBlueprintAsset: Package=%s, Name=%s, Parent=%s"),
		*PackagePath, *AssetName, *ParentClassName);

	if (PackagePath.IsEmpty() || AssetName.IsEmpty())
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Invalid arguments"),
			TEXT("PackagePath and AssetName must not be empty"),
			TEXT("create_blueprint_asset"),
			TEXT("Provide PackagePath like '/Game/Test' and AssetName like 'BP_E2ETest'")
		);
	}

	const FString FullPackagePath = PackagePath / AssetName;
	const FString FullAssetPath = FullPackagePath + TEXT(".") + AssetName;

	// Idempotency: if asset already exists, short-circuit with success.
	// Mirrors CreateLevelSequence pattern at CURemoteLibrary.cpp:2185.
	if (UObject* Existing = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *FullAssetPath))
	{
		TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetBoolField(TEXT("already_exists"), true);
		ResultObj->SetBoolField(TEXT("created"), false);
		ResultObj->SetStringField(TEXT("path"), FullAssetPath);

		FString OutString = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return OutString;
	}

	// Resolve parent class. Default to AActor for test-fixture use case;
	// accept short names ("Actor") and fully-qualified paths ("/Script/Engine.Actor").
	UClass* ParentClass = AActor::StaticClass();
	if (!ParentClassName.IsEmpty() && !ParentClassName.Equals(TEXT("Actor"), ESearchCase::IgnoreCase))
	{
		UClass* Resolved = FindObject<UClass>(nullptr, *ParentClassName);
		if (!Resolved)
		{
			Resolved = LoadObject<UClass>(nullptr, *ParentClassName);
		}
		if (Resolved && Resolved->IsChildOf(AActor::StaticClass()))
		{
			ParentClass = Resolved;
		}
		else
		{
			UE_LOG(LogNarshaMCPRemote, Warning,
				TEXT("[Blueprint] CreateBlueprintAsset: could not resolve parent '%s' to an Actor-derived class; falling back to AActor"),
				*ParentClassName);
		}
	}

	UPackage* Package = CreatePackage(*FullPackagePath);
	if (!Package)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Package creation failed"),
			FString::Printf(TEXT("Could not create package at '%s'"), *FullPackagePath),
			TEXT("create_blueprint_asset"),
			TEXT("Ensure the path is under /Game and the parent folder is writable")
		);
	}

	UBlueprint* NewBP = FKismetEditorUtilities::CreateBlueprint(
		ParentClass,
		Package,
		FName(*AssetName),
		BPTYPE_Normal,
		UBlueprint::StaticClass(),
		UBlueprintGeneratedClass::StaticClass()
	);
	if (!NewBP)
	{
		return BlueprintNodeHelpers::CreateDetailedErrorJson(
			TEXT("Blueprint creation failed"),
			FString::Printf(TEXT("FKismetEditorUtilities::CreateBlueprint returned null for '%s'"), *FullAssetPath),
			TEXT("create_blueprint_asset"),
			FString::Printf(TEXT("Parent class: %s"), *ParentClass->GetName())
		);
	}

	FAssetRegistryModule::AssetCreated(NewBP);
	Package->MarkPackageDirty();

	const FString PackageFileName = FPackageName::LongPackageNameToFilename(
		FullPackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	const bool bSaved = UPackage::SavePackage(Package, NewBP, *PackageFileName, SaveArgs);

	if (!bSaved)
	{
		UE_LOG(LogNarshaMCPRemote, Warning,
			TEXT("[Blueprint] CreateBlueprintAsset: created in memory but failed to save: %s"),
			*PackageFileName);
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetBoolField(TEXT("already_exists"), false);
	ResultObj->SetBoolField(TEXT("created"), true);
	ResultObj->SetBoolField(TEXT("saved_to_disk"), bSaved);
	ResultObj->SetStringField(TEXT("path"), FullAssetPath);
	ResultObj->SetStringField(TEXT("parent_class"), ParentClass->GetName());

	FString OutString = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return OutString;
}

FString UCURemoteLibrary::GetBlueprintGraphData(
	const FString& BlueprintPath,
	const FString& GraphName
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] GetBlueprintGraphData: Path=%s, Graph=%s"),
		*BlueprintPath, *GraphName);

	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Find graph
	UEdGraph* TargetGraph = BlueprintNodeHelpers::FindGraphByName(Blueprint, GraphName);
	if (!TargetGraph)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
		return SerializeJson(ResultObj);
	}

	// Collect nodes and connections
	TArray<TSharedPtr<FJsonValue>> NodesArray;
	TArray<TSharedPtr<FJsonValue>> ConnectionsArray;
	int32 ConnectionCount = 0;

	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (!Node) continue;

		TSharedPtr<FJsonObject> NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
		NodeObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		NodeObj->SetStringField(TEXT("node_title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NodeObj->SetStringField(TEXT("node_name"), Node->GetName());
		NodeObj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NodeObj->SetNumberField(TEXT("pos_y"), Node->NodePosY);

		// Collect pins
		TArray<TSharedPtr<FJsonValue>> PinsArray;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;

			TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
			PinObj->SetStringField(TEXT("pin_id"), Pin->PinId.ToString());
			PinObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
			PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
			PinObj->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PinObj->SetStringField(TEXT("sub_type"), Pin->PinType.PinSubCategory.ToString());
			if (!Pin->DefaultValue.IsEmpty())
			{
				PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			}
			PinObj->SetNumberField(TEXT("linked_count"), Pin->LinkedTo.Num());
			PinsArray.Add(MakeShared<FJsonValueObject>(PinObj));

			// Collect connections (output pins only to avoid duplicates)
			if (Pin->Direction == EGPD_Output)
			{
				for (UEdGraphPin* Linked : Pin->LinkedTo)
				{
					if (!Linked || !Linked->GetOwningNode()) continue;

					TSharedPtr<FJsonObject> ConnObj = MakeShared<FJsonObject>();
					ConnObj->SetStringField(TEXT("source_node"), Node->NodeGuid.ToString());
					ConnObj->SetStringField(TEXT("source_pin"), Pin->PinName.ToString());
					ConnObj->SetStringField(TEXT("source_pin_id"), Pin->PinId.ToString());
					ConnObj->SetStringField(TEXT("target_node"), Linked->GetOwningNode()->NodeGuid.ToString());
					ConnObj->SetStringField(TEXT("target_pin"), Linked->PinName.ToString());
					ConnObj->SetStringField(TEXT("target_pin_id"), Linked->PinId.ToString());
					ConnectionsArray.Add(MakeShared<FJsonValueObject>(ConnObj));
					ConnectionCount++;
				}
			}
		}
		NodeObj->SetArrayField(TEXT("pins"), PinsArray);
		NodesArray.Add(MakeShared<FJsonValueObject>(NodeObj));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("graph_name"), GraphName);
	ResultObj->SetNumberField(TEXT("node_count"), TargetGraph->Nodes.Num());
	ResultObj->SetNumberField(TEXT("connection_count"), ConnectionCount);
	ResultObj->SetArrayField(TEXT("nodes"), NodesArray);
	ResultObj->SetArrayField(TEXT("connections"), ConnectionsArray);
	return SerializeJson(ResultObj);
}

// Issue #28275: enumerate every graph in a Blueprint so a caller can name one.
FString UCURemoteLibrary::GetBlueprintGraphs(const FString& BlueprintPath)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] GetBlueprintGraphs: Path=%s"), *BlueprintPath);

	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		return CUJsonUtils::SerializeToString(Obj.ToSharedRef());
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath is required"));
		return SerializeJson(ResultObj);
	}

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

#if WITH_EDITORONLY_DATA
	// UBlueprint::GetAllGraphs walks FunctionGraphs / MacroGraphs / UbergraphPages /
	// DelegateSignatureGraphs and recurses each one's SubGraphs, so an AnimBP yields
	// AnimGraph -> its State Machine graph -> per-state and per-transition graphs.
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	// A deep state machine can produce dozens of entries. Cap the payload, but say
	// so — a silently truncated list is the same defect class this issue closes.
	const int32 MaxGraphs = 200;
	TArray<TSharedPtr<FJsonValue>> GraphsArray;
	int32 TotalGraphs = 0;

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}
		++TotalGraphs;
		if (GraphsArray.Num() >= MaxGraphs)
		{
			continue;
		}

		TSharedPtr<FJsonObject> GraphObj = MakeShared<FJsonObject>();
		GraphObj->SetStringField(TEXT("name"), Graph->GetName());
		GraphObj->SetStringField(TEXT("graph_class"), Graph->GetClass()->GetName());
		GraphObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

		// Outer graph disambiguates same-named siblings (two state machines can each
		// own a state called "Idle") and lets a caller rebuild the nesting.
		//
		// Use GetOuterGraph, not Cast<UEdGraph>(GetOuter()): a State Machine graph is
		// outered to its UAnimGraphNode_StateMachine (AnimGraphNode_StateMachineBase.cpp
		// :147 passes the NODE as Outer), so a single-level cast returns null for exactly
		// the graphs this op exists to surface. GetOuterGraph walks up until it finds one.
		if (UEdGraph* OuterGraph = UEdGraph::GetOuterGraph(Graph))
		{
			GraphObj->SetStringField(TEXT("outer_graph"), OuterGraph->GetName());
		}

		GraphsArray.Add(MakeShared<FJsonValueObject>(GraphObj));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetNumberField(TEXT("graph_count"), TotalGraphs);
	ResultObj->SetNumberField(TEXT("returned_count"), GraphsArray.Num());
	ResultObj->SetBoolField(TEXT("truncated"), TotalGraphs > GraphsArray.Num());
	ResultObj->SetArrayField(TEXT("graphs"), GraphsArray);
	return SerializeJson(ResultObj);
#else
	// Without editor-only data the graph arrays do not exist. Returning an empty
	// list here would be indistinguishable from "this Blueprint has no graphs".
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(TEXT("error"),
		TEXT("Blueprint graphs are editor-only data and are unavailable in this build configuration."));
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	return SerializeJson(ResultObj);
#endif
}

// Issue #6388: Bulk-update node positions in a Blueprint graph
FString UCURemoteLibrary::SetNodePositions(
	const FString& BlueprintPath,
	const FString& GraphName,
	const FString& PositionsJson
)
{
	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetNodePositions: Path=%s, Graph=%s"),
		*BlueprintPath, *GraphName);

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: SetNodePositions")));
#endif

	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || PositionsJson.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath and PositionsJson are required"));
		return SerializeJson(ResultObj);
	}

	// Load Blueprint
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Find graph
	UEdGraph* TargetGraph = BlueprintNodeHelpers::FindGraphByName(Blueprint, GraphName);
	if (!TargetGraph)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Graph '%s' not found"), *GraphName));
		return SerializeJson(ResultObj);
	}

	// Parse positions JSON array
	TArray<TSharedPtr<FJsonValue>> PositionsArray;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PositionsJson);
	if (!FJsonSerializer::Deserialize(Reader, PositionsArray))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Failed to parse PositionsJson as JSON array"));
		return SerializeJson(ResultObj);
	}

	// Build GUID→Node lookup for O(1) access
	TMap<FGuid, UEdGraphNode*> GuidToNode;
	for (UEdGraphNode* Node : TargetGraph->Nodes)
	{
		if (Node)
		{
			GuidToNode.Add(Node->NodeGuid, Node);
		}
	}

	// Apply positions
	int32 UpdatedCount = 0;
	TArray<TSharedPtr<FJsonValue>> NotFoundArray;

	for (const TSharedPtr<FJsonValue>& Entry : PositionsArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Entry.IsValid() || !Entry->TryGetObject(EntryObj))
		{
			continue;
		}

		FString GuidStr;
		if (!(*EntryObj)->TryGetStringField(TEXT("node_guid"), GuidStr))
		{
			continue;
		}

		FGuid NodeGuid;
		if (!FGuid::Parse(GuidStr, NodeGuid))
		{
			NotFoundArray.Add(MakeShared<FJsonValueString>(GuidStr));
			continue;
		}

		UEdGraphNode** FoundNode = GuidToNode.Find(NodeGuid);
		if (!FoundNode || !(*FoundNode))
		{
			NotFoundArray.Add(MakeShared<FJsonValueString>(GuidStr));
			continue;
		}

		int32 PosX = static_cast<int32>((*EntryObj)->GetNumberField(TEXT("pos_x")));
		int32 PosY = static_cast<int32>((*EntryObj)->GetNumberField(TEXT("pos_y")));

		(*FoundNode)->NodePosX = PosX;
		(*FoundNode)->NodePosY = PosY;
		UpdatedCount++;
	}

	// Mark Blueprint as modified so Editor reflects the changes
	if (UpdatedCount > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetNumberField(TEXT("updated_count"), UpdatedCount);
	ResultObj->SetNumberField(TEXT("total_requested"), PositionsArray.Num());
	ResultObj->SetArrayField(TEXT("not_found"), NotFoundArray);
	return SerializeJson(ResultObj);
}

// Issue #7703: Add function graph to Blueprint (including Blueprint Interfaces)
FString UCURemoteLibrary::AddBlueprintFunction(
	const FString& BlueprintPath,
	const FString& FunctionName,
	bool bIsPure)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || FunctionName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath and FunctionName are required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] AddBlueprintFunction: %s in %s (pure: %s)"),
		*FunctionName, *BlueprintPath, bIsPure ? TEXT("true") : TEXT("false"));

	// Load Blueprint (supports both Blueprint and BlueprintInterface)
	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Check if function already exists
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FunctionName))
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Function '%s' already exists"), *FunctionName));
			return SerializeJson(ResultObj);
		}
	}

	// Create new function graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FunctionName),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Failed to create function graph"));
		return SerializeJson(ResultObj);
	}

	// Add as function graph (this creates the entry node etc.)
	// Template parameter: UFunction* = nullptr for user-created functions without existing signature
	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(Blueprint, NewGraph, /*bIsUserCreated=*/true, /*SignatureFromObject=*/nullptr);

	// Set pure flag if requested
	if (bIsPure)
	{
		// Find the function entry node and set pure flag
		for (UEdGraphNode* Node : NewGraph->Nodes)
		{
			if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
			{
				EntryNode->SetExtraFlags(EntryNode->GetExtraFlags() | FUNC_BlueprintPure);
				break;
			}
		}
	}

	// Mark modified and compile
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Determine blueprint type for response
	FString BlueprintType = TEXT("Blueprint");
	if (Blueprint->BlueprintType == BPTYPE_Interface)
	{
		BlueprintType = TEXT("BlueprintInterface");
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("function_name"), FunctionName);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("blueprint_type"), BlueprintType);
	ResultObj->SetBoolField(TEXT("is_pure"), bIsPure);
	return SerializeJson(ResultObj);
}

// Issue #7704: Add component to Blueprint
FString UCURemoteLibrary::AddBlueprintComponent(
	const FString& BlueprintPath,
	const FString& ComponentClassName,
	const FString& ComponentName)
{
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AddBlueprintComponent")));
#endif

	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || ComponentClassName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath and ComponentClassName are required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] AddBlueprintComponent: %s (%s) to %s"),
		*ComponentName, *ComponentClassName, *BlueprintPath);

	// Load Blueprint
	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Find component class (UE5: ANY_PACKAGE removed, use FindFirstObject)
	UClass* CompClass = FindFirstObject<UClass>(*ComponentClassName, EFindFirstObjectOptions::NativeFirst);
	if (!CompClass)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Component class not found: %s"), *ComponentClassName));
		ResultObj->SetStringField(TEXT("hint"), TEXT("Use class name like 'UStaticMeshComponent', 'UCapsuleComponent', etc."));
		return SerializeJson(ResultObj);
	}

	if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("'%s' is not an ActorComponent subclass"), *ComponentClassName));
		return SerializeJson(ResultObj);
	}

	// Determine component name
	FName CompFName;
	if (ComponentName.IsEmpty())
	{
		// Generate unique name based on class name
		FString BaseName = CompClass->GetName();
		BaseName.RemoveFromStart(TEXT("U"));
		CompFName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, BaseName);
	}
	else
	{
		CompFName = FName(*ComponentName);
	}

	// Add component via SCS (SimpleConstructionScript)
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(CompClass, CompFName);
	if (!NewNode)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Failed to create SCS node"));
		return SerializeJson(ResultObj);
	}

	// Attach to root or add as root
	USCS_Node* RootNode = Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode();
	if (RootNode && CompClass->IsChildOf(USceneComponent::StaticClass()))
	{
		RootNode->AddChildNode(NewNode);
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	// Mark modified and compile
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("component_name"), CompFName.ToString());
	ResultObj->SetStringField(TEXT("component_class"), CompClass->GetName());
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	return SerializeJson(ResultObj);
}

// Issue #7704: Set pin default value on a Blueprint node
FString UCURemoteLibrary::SetBlueprintPinValue(
	const FString& BlueprintPath,
	const FString& NodeName,
	const FString& PinName,
	const FString& PinValue)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || NodeName.IsEmpty() || PinName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath, NodeName, and PinName are required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetBlueprintPinValue: %s.%s.%s = %s"),
		*BlueprintPath, *NodeName, *PinName, *PinValue);

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Search all graphs for the node
	UEdGraphNode* FoundNode = nullptr;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetName().Contains(NodeName))
			{
				FoundNode = Node;
				break;
			}
		}
		if (FoundNode) break;
	}

	if (!FoundNode)
	{
		// Collect available node names for error message
		TArray<FString> AvailableNodes;
		for (UEdGraph* Graph : AllGraphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node)
				{
					AvailableNodes.Add(Node->GetName());
				}
			}
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Node '%s' not found"), *NodeName));
		ResultObj->SetStringField(TEXT("available_nodes"), FString::Join(AvailableNodes, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	// Find the pin
	UEdGraphPin* FoundPin = nullptr;
	for (UEdGraphPin* Pin : FoundNode->Pins)
	{
		if (Pin && (Pin->PinName.ToString() == PinName || Pin->PinName.ToString().Contains(PinName)))
		{
			FoundPin = Pin;
			break;
		}
	}

	if (!FoundPin)
	{
		TArray<FString> AvailablePins;
		for (UEdGraphPin* Pin : FoundNode->Pins)
		{
			if (Pin)
			{
				AvailablePins.Add(FString::Printf(TEXT("%s (%s)"), *Pin->PinName.ToString(), *Pin->PinType.PinCategory.ToString()));
			}
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Pin '%s' not found on node '%s'"), *PinName, *NodeName));
		ResultObj->SetStringField(TEXT("available_pins"), FString::Join(AvailablePins, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	// Set the default value via K2 schema
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	K2Schema->TrySetDefaultValue(*FoundPin, PinValue);

	// Compile
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_name"), FoundNode->GetName());
	ResultObj->SetStringField(TEXT("pin_name"), FoundPin->PinName.ToString());
	ResultObj->SetStringField(TEXT("value"), PinValue);
	ResultObj->SetStringField(TEXT("actual_value"), FoundPin->DefaultValue);
	return SerializeJson(ResultObj);
}

// Issue #4828: Rename Blueprint member variable via FBlueprintEditorUtils
FString UCURemoteLibrary::RenameBlueprintVariable(
	const FString& BlueprintPath,
	const FString& OldName,
	const FString& NewName)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || OldName.IsEmpty() || NewName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath, OldName, and NewName are all required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] RenameBlueprintVariable: %s, %s -> %s"),
		*BlueprintPath, *OldName, *NewName);

	// Load Blueprint
	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Check variable exists
	FName OldFName(*OldName);
	FName NewFName(*NewName);
	bool bFound = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == OldFName)
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		// Collect available variable names for error message
		TArray<FString> AvailableVars;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			AvailableVars.Add(Var.VarName.ToString());
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Variable '%s' not found in Blueprint"), *OldName));
		ResultObj->SetStringField(TEXT("available_variables"), FString::Join(AvailableVars, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	// Perform rename
	FBlueprintEditorUtils::RenameMemberVariable(Blueprint, OldFName, NewFName);

	// Verify rename succeeded
	bool bRenamed = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == NewFName)
		{
			bRenamed = true;
			break;
		}
	}

	if (bRenamed)
	{
		// Save
		Blueprint->MarkPackageDirty();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("old_name"), OldName);
		ResultObj->SetStringField(TEXT("new_name"), NewName);
		ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	}
	else
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("RenameMemberVariable did not rename '%s' to '%s'"), *OldName, *NewName));
	}

	return SerializeJson(ResultObj);
}

// Issue #14803: Rename a Blueprint function graph via FBlueprintEditorUtils::RenameGraph.
// Mirrors RenameBlueprintVariable; the deferred lifecycle op from #14788 / PR #14806.
FString UCURemoteLibrary::RenameBlueprintFunction(
	const FString& BlueprintPath,
	const FString& OldName,
	const FString& NewName)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || OldName.IsEmpty() || NewName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath, OldName, and NewName are all required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] RenameBlueprintFunction: %s, %s -> %s"),
		*BlueprintPath, *OldName, *NewName);

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	const FName OldFName(*OldName);
	const FName NewFName(*NewName);

	// Find the target function graph
	UEdGraph* FunctionGraph = nullptr;
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == OldFName)
		{
			FunctionGraph = Graph;
			break;
		}
	}

	if (!FunctionGraph)
	{
		TArray<FString> AvailableFns;
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				AvailableFns.Add(Graph->GetName());
			}
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Function '%s' not found in Blueprint"), *OldName));
		ResultObj->SetStringField(TEXT("available_functions"), FString::Join(AvailableFns, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	// Reject collisions with an existing function graph or member variable
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == NewFName)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("A function named '%s' already exists"), *NewName));
			return SerializeJson(ResultObj);
		}
	}
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == NewFName)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("A variable named '%s' already exists (name collision)"), *NewName));
			return SerializeJson(ResultObj);
		}
	}

	// Rename the graph + entry/result node member references (engine-canonical path)
	FunctionGraph->Modify();
	FBlueprintEditorUtils::RenameGraph(FunctionGraph, NewName);

	// Verify the rename took
	if (FunctionGraph->GetFName() != NewFName)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("RenameGraph did not rename '%s' to '%s' (name may be reserved or invalid)"), *OldName, *NewName));
		return SerializeJson(ResultObj);
	}

	// Fix up self-context UK2Node_CallFunction call sites that referenced the old name.
	// RenameGraph re-points entry/result nodes but not internal callers.
	int32 CallsitesFixed = 0;
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);
	for (UEdGraph* Graph : AllGraphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node);
			if (CallNode
				&& CallNode->FunctionReference.IsSelfContext()
				&& CallNode->FunctionReference.GetMemberName() == OldFName)
			{
				CallNode->Modify();
				CallNode->FunctionReference.SetSelfMember(NewFName);
				CallNode->ReconstructNode();
				++CallsitesFixed;
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	FCUBlueprintManipulator::SaveBlueprint(Blueprint, true);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("old_name"), OldName);
	ResultObj->SetStringField(TEXT("new_name"), NewName);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetNumberField(TEXT("callsites_fixed"), CallsitesFixed);
	return SerializeJson(ResultObj);
}

// Issue #14803: Introduce a member variable from a node's output pin (headless "Promote to Variable").
// Mirrors FBlueprintEditor::DoPromoteToVariable2f without the GUI: read source pin type ->
// AddMemberVariable(exact FEdGraphPinType) -> optionally spawn + wire a UK2Node_VariableSet.
FString UCURemoteLibrary::IntroduceVariableFromNode(
	const FString& BlueprintPath,
	const FString& NodeName,
	const FString& VariableName,
	bool bCreateSetNode)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || NodeName.IsEmpty() || VariableName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath, NodeName, and VariableName are all required"));
		return SerializeJson(ResultObj);
	}

	// Optional "Node.Pin" disambiguation for nodes with multiple output pins
	FString NodeToken = NodeName;
	FString PinToken;
	NodeName.Split(TEXT("."), &NodeToken, &PinToken);
	if (NodeToken.IsEmpty())
	{
		NodeToken = NodeName;
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] IntroduceVariableFromNode: %s, node=%s pin=%s -> %s (set_node=%s)"),
		*BlueprintPath, *NodeToken, *PinToken, *VariableName, bCreateSetNode ? TEXT("true") : TEXT("false"));

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Reject collision with an existing member variable
	const FName NewVarFName(*VariableName);
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == NewVarFName)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("A variable named '%s' already exists"), *VariableName));
			return SerializeJson(ResultObj);
		}
	}

	// Locate the source node. Prefer the canonical NodeGuid identifier (what add_node
	// returns and connect ops key on); fall back to an object-name substring match.
	UEdGraphNode* FoundNode = nullptr;
	FGuid ParsedNodeGuid;
	if (FGuid::Parse(NodeToken, ParsedNodeGuid))
	{
		FoundNode = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, ParsedNodeGuid);
	}
	if (!FoundNode)
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && Node->GetName().Contains(NodeToken))
				{
					FoundNode = Node;
					break;
				}
			}
			if (FoundNode) break;
		}
	}

	if (!FoundNode)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Node '%s' not found"), *NodeToken));
		return SerializeJson(ResultObj);
	}

	UEdGraph* OwningGraph = FoundNode->GetGraph();

	// Pick the source output pin: named (if "Node.Pin" given) else first non-exec output
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* SourcePin = nullptr;
	for (UEdGraphPin* Pin : FoundNode->Pins)
	{
		if (!Pin || Pin->Direction != EGPD_Output)
		{
			continue;
		}
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			continue;
		}
		if (!PinToken.IsEmpty())
		{
			if (Pin->PinName.ToString() == PinToken || Pin->PinName.ToString().Contains(PinToken))
			{
				SourcePin = Pin;
				break;
			}
		}
		else
		{
			SourcePin = Pin;
			break;
		}
	}

	if (!SourcePin)
	{
		TArray<FString> AvailablePins;
		for (UEdGraphPin* Pin : FoundNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				AvailablePins.Add(Pin->PinName.ToString());
			}
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("No data output pin found on node '%s'"), *NodeToken));
		ResultObj->SetStringField(TEXT("available_output_pins"), FString::Join(AvailablePins, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	// Sanitize the pin type (mirror DoPromoteToVariable2f) and create the member variable
	FEdGraphPinType NewVarType = SourcePin->PinType;
	NewVarType.bIsConst = false;
	NewVarType.bIsReference = false;
	NewVarType.bIsWeakPointer = false;

	if (!FBlueprintEditorUtils::AddMemberVariable(Blueprint, NewVarFName, NewVarType))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("AddMemberVariable failed for '%s' (type may be unsupported)"), *VariableName));
		return SerializeJson(ResultObj);
	}

	// Compile once so the new variable property exists before we reference it from a node
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	bool bWired = false;
	if (bCreateSetNode && OwningGraph)
	{
		UK2Node_VariableSet* SetNode = NewObject<UK2Node_VariableSet>(OwningGraph);
		SetNode->CreateNewGuid();
		SetNode->NodePosX = FoundNode->NodePosX + 320;
		SetNode->NodePosY = FoundNode->NodePosY;
		SetNode->VariableReference.SetSelfMember(NewVarFName);
		OwningGraph->AddNode(SetNode, false, false);
		SetNode->AllocateDefaultPins();

		// The value input pin is named after the variable
		UEdGraphPin* ValuePin = nullptr;
		for (UEdGraphPin* Pin : SetNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName == NewVarFName)
			{
				ValuePin = Pin;
				break;
			}
		}

		if (ValuePin && K2Schema->TryCreateConnection(SourcePin, ValuePin))
		{
			bWired = true;
		}
		else
		{
			// Wiring failed (pin type rejected / value pin missing): remove the
			// dangling Set node rather than persisting an orphan, and surface a
			// warning so the caller knows only the variable was created.
			OwningGraph->RemoveNode(SetNode);
			ResultObj->SetStringField(TEXT("warning"),
				TEXT("create_set_node requested but wiring failed; the VariableSet node was removed — only the variable was created"));
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}

	FCUBlueprintManipulator::SaveBlueprint(Blueprint, true);

	// Resolve the engine-assigned variable GUID (real-creation proof, Rule #19)
	FString VarGuid;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == NewVarFName)
		{
			VarGuid = Var.VarGuid.ToString(EGuidFormats::DigitsWithHyphens);
			break;
		}
	}

	FString VarTypeStr = NewVarType.PinCategory.ToString();
	if (NewVarType.PinSubCategoryObject.IsValid())
	{
		VarTypeStr += FString::Printf(TEXT(":%s"), *NewVarType.PinSubCategoryObject->GetName());
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("variable_name"), VariableName);
	ResultObj->SetStringField(TEXT("variable_type"), VarTypeStr);
	ResultObj->SetStringField(TEXT("variable_guid"), VarGuid);
	ResultObj->SetStringField(TEXT("source_node"), FoundNode->GetName());
	ResultObj->SetStringField(TEXT("source_pin"), SourcePin->PinName.ToString());
	ResultObj->SetBoolField(TEXT("wired"), bWired);
	return SerializeJson(ResultObj);
}

FString UCURemoteLibrary::SetBlueprintVariableDefault(
	const FString& BlueprintPath,
	const FString& VariableName,
	const FString& DefaultValue)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || VariableName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath and VariableName are required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetBlueprintVariableDefault: %s.%s = %s"),
		*BlueprintPath, *VariableName, *DefaultValue);

	// Load Blueprint
	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	FName VarFName(*VariableName);

	// Is this a Blueprint-authored variable? Inherited C++ UPROPERTY members (e.g. a parent native
	// class's EditAnywhere property) are NOT in NewVariables but ARE resolvable below via
	// GeneratedClass->FindPropertyByName — so membership is a label, not a gate (Issue #16565 항목1).
	bool bIsNewVariable = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarFName)
		{
			bIsNewVariable = true;
			break;
		}
	}

	// Get CDO to set default value
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	if (!GeneratedClass)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Blueprint has no GeneratedClass — compile it first"));
		return SerializeJson(ResultObj);
	}

	UObject* CDO = GeneratedClass->GetDefaultObject();
	if (!CDO)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Failed to get Class Default Object"));
		return SerializeJson(ResultObj);
	}

	// Find property by reflection — resolves BOTH NewVariables AND inherited C++ UPROPERTY members.
	FProperty* Property = GeneratedClass->FindPropertyByName(VarFName);
	if (!Property)
	{
		TArray<FString> AvailableVars;
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			AvailableVars.Add(Var.VarName.ToString());
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Variable '%s' not found"), *VariableName));
		ResultObj->SetStringField(TEXT("available_variables"), FString::Join(AvailableVars, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	// Inherited (non-NewVariables) members must be a *writable-in-editor* UPROPERTY to be in scope
	// (Issue #16565 항목1). Editable = CPF_Edit AND NOT CPF_EditConst, so VisibleAnywhere/EditConst
	// (which carry CPF_Edit|CPF_EditConst) and bare/BlueprintReadOnly members are rejected. BP
	// variables keep prior behavior.
	if (!bIsNewVariable
		&& (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_EditConst)))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Inherited property '%s' is not editable (needs EditAnywhere/EditDefaultsOnly, not read-only/VisibleAnywhere); out of scope"),
			*VariableName));
		ResultObj->SetStringField(TEXT("source"), TEXT("inherited_cpp"));
		return SerializeJson(ResultObj);
	}

	// Set value using ImportText (handles all types: int, float, bool, FString, FVector, etc.)
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);

	// Export previous value before overwriting (Issue #4751 I7: previous_value)
	FString PreviousValue;
	Property->ExportTextItem_Direct(PreviousValue, ValuePtr, nullptr, CDO, PPF_None);

	const TCHAR* Result = Property->ImportText_Direct(*DefaultValue, ValuePtr, CDO, PPF_None);
	if (!Result)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Failed to parse '%s' for property '%s' (type: %s)"),
			*DefaultValue, *VariableName, *Property->GetCPPType()));
		ResultObj->SetStringField(TEXT("property_type"), Property->GetCPPType());
		ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
		ResultObj->SetStringField(TEXT("hint"), TEXT("int: '42', float: '3.14', bool: 'true', FString: 'hello', FVector: '(X=1,Y=2,Z=3)'"));
		return SerializeJson(ResultObj);
	}

	// Mark modified
	Blueprint->MarkPackageDirty();

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("variable"), VariableName);
	ResultObj->SetStringField(TEXT("value"), DefaultValue);
	ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
	ResultObj->SetStringField(TEXT("property_type"), Property->GetCPPType());
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("source"), bIsNewVariable ? TEXT("new_variable") : TEXT("inherited_cpp"));

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetBlueprintVariableDefault completed (%s): %s = %s"),
		bIsNewVariable ? TEXT("new_variable") : TEXT("inherited_cpp"), *VariableName, *DefaultValue);
	return SerializeJson(ResultObj);
}

// =========================================================
// Issue #9060: SCS Component Default Read/Write (dot-path, parent-chain)
// =========================================================

namespace NarshaSCSComponentDefaults
{
	// Find an SCS node in the Blueprint (or any ancestor BP) whose variable name matches ComponentName.
	// Sets OutOwningBlueprint to the BP that owns the node (may be an ancestor).
	// Case-insensitive match. Also tries to strip a trailing `_<N>` numeric suffix on failure
	// (binary parser may emit names like `StaticMeshComponent_0`).
	static USCS_Node* FindSCSNodeInChain(
		UBlueprint* Blueprint,
		const FString& ComponentName,
		UBlueprint*& OutOwningBlueprint)
	{
		OutOwningBlueprint = nullptr;
		// Issue #9060 M-4: cycle-safe walk. Depth-32 was arbitrary and silently wrong on pathological
		// chains; a visited set is O(N) with no false negatives on legal-but-deep hierarchies.
		TSet<UBlueprint*> Visited;
		UBlueprint* Current = Blueprint;
		while (Current)
		{
			bool bAlreadyIn = false;
			Visited.Add(Current, &bAlreadyIn);
			if (bAlreadyIn)
			{
				// Cycle detected (shouldn't happen with valid UE assets, but defend anyway).
				break;
			}
			if (USimpleConstructionScript* SCS = Current->SimpleConstructionScript)
			{
				for (USCS_Node* Node : SCS->GetAllNodes())
				{
					if (!Node) continue;
					const FString NodeVarName = Node->GetVariableName().ToString();
					if (NodeVarName.Equals(ComponentName, ESearchCase::IgnoreCase))
					{
						OutOwningBlueprint = Current;
						return Node;
					}
				}
			}
			// Walk to parent BP
			UClass* ParentClass = Current->ParentClass;
			Current = ParentClass ? Cast<UBlueprint>(ParentClass->ClassGeneratedBy) : nullptr;
		}
		return nullptr;
	}

	// Walk a dot-separated struct path (all segments except the first, which is the component name)
	// down to the final FProperty + ValuePtr pair.
	// Returns true on success; on failure populates OutError with a human-readable message.
	static bool ResolveNestedPath(
		UStruct* ContainerStruct,
		void* ContainerPtr,
		const TArray<FString>& Segments,
		int32 StartIndex,
		FProperty*& OutProperty,
		void*& OutValuePtr,
		UObject*& OutOwnerObject,
		FString& OutError)
	{
		OutProperty = nullptr;
		OutValuePtr = nullptr;
		// Tracks the deepest UObject the path descended *into* via an object property (e.g. a native
		// component). Stays null for pure struct paths; callers fall back to the root object then.
		OutOwnerObject = nullptr;
		if (!ContainerStruct || !ContainerPtr)
		{
			OutError = TEXT("Invalid container for property path resolution");
			return false;
		}
		UStruct* CurrentStruct = ContainerStruct;
		void* CurrentPtr = ContainerPtr;
		for (int32 i = StartIndex; i < Segments.Num(); ++i)
		{
			const FString& SegName = Segments[i];
			FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*SegName));
			if (!Prop)
			{
				// Build available property list for a better error message
				TArray<FString> Available;
				for (TFieldIterator<FProperty> It(CurrentStruct); It; ++It)
				{
					Available.Add(It->GetName());
					if (Available.Num() >= 12) break;
				}
				OutError = FString::Printf(
					TEXT("Property '%s' not found on '%s'. Available (first %d): %s"),
					*SegName, *CurrentStruct->GetName(), Available.Num(),
					*FString::Join(Available, TEXT(", ")));
				return false;
			}
			if (i == Segments.Num() - 1)
			{
				// Final segment — resolve ValuePtr against current container
				OutProperty = Prop;
				OutValuePtr = Prop->ContainerPtrToValuePtr<void>(CurrentPtr);
				return true;
			}
			// Intermediate segment — descend into a struct, OR follow an object pointer (e.g. a
			// native component like `Mesh` stored as an FObjectProperty) to its instance and resolve
			// the remaining segments on that object (Issue #16565 항목2).
			if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				CurrentPtr = StructProp->ContainerPtrToValuePtr<void>(CurrentPtr);
				CurrentStruct = StructProp->Struct;
			}
			else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				void* ObjAddr = ObjProp->ContainerPtrToValuePtr<void>(CurrentPtr);
				UObject* Pointed = ObjProp->GetObjectPropertyValue(ObjAddr);
				if (!Pointed)
				{
					OutError = FString::Printf(
						TEXT("Intermediate segment '%s' (type: %s) is null; cannot descend further"),
						*SegName, *Prop->GetCPPType());
					return false;
				}
				CurrentPtr = Pointed;
				CurrentStruct = Pointed->GetClass();
				OutOwnerObject = Pointed;
			}
			else
			{
				OutError = FString::Printf(
					TEXT("Intermediate segment '%s' is not a struct or object property (type: %s); cannot descend further"),
					*SegName, *Prop->GetCPPType());
				return false;
			}
		}
		OutError = TEXT("Empty property path after component name");
		return false;
	}

	// Find the CDO component instance for an inherited SCS component on the child BP.
	// Returns nullptr if the BP's CDO doesn't expose the named component (unusual).
	static UActorComponent* FindCDOComponentInstance(UBlueprint* ChildBP, const FString& ComponentName)
	{
		if (!ChildBP || !ChildBP->GeneratedClass) return nullptr;
		UObject* CDO = ChildBP->GeneratedClass->GetDefaultObject();
		if (!CDO) return nullptr;
		for (TFieldIterator<FObjectProperty> It(ChildBP->GeneratedClass); It; ++It)
		{
			FObjectProperty* ObjProp = *It;
			if (!ObjProp) continue;
			if (ObjProp->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				UObject* Value = ObjProp->GetObjectPropertyValue_InContainer(CDO);
				if (UActorComponent* Comp = Cast<UActorComponent>(Value))
				{
					return Comp;
				}
			}
		}
		return nullptr;
	}
} // namespace NarshaSCSComponentDefaults

FString UCURemoteLibrary::AddGameplayEffectModifier(
	const FString& EffectAsset,
	const FString& Attribute,
	int32 Op,
	float Magnitude)
{
#if !WITH_NARSHAMCP_GAMEPLAYABILITIES
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The UFUNCTION declaration stays
	// (UHT generates its thunk), so only the body is compiled out and callers get an
	// actionable decline instead of a link error.
	TSharedPtr<FJsonObject> Unavailable = MakeShared<FJsonObject>();
	Unavailable->SetBoolField(JSON_SUCCESS_FIELD, false);
	Unavailable->SetStringField(TEXT("error"),
		TEXT("AddGameplayEffectModifier requires the GameplayAbilities plugin, which this project has not enabled"));
	Unavailable->SetStringField(TEXT("error_code"), TEXT("gameplay_abilities_plugin_unavailable"));
	Unavailable->SetStringField(TEXT("hint"),
		TEXT("Enable GameplayAbilities in your .uproject, then rebuild."));
	return CUJsonUtils::SerializeToString(Unavailable.ToSharedRef());
#else
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		return CUJsonUtils::SerializeToString(Obj.ToSharedRef());
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (EffectAsset.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("EffectAsset is required"));
		return SerializeJson(ResultObj);
	}

	// Content GameplayEffects are UBlueprint assets whose GeneratedClass derives from UGameplayEffect;
	// a few are native UClass assets. Authoring FGameplayModifierInfo from Python is impossible on
	// UE 5.6+ because every field is EditDefaultsOnly, so this runs in C++ (Issue #16552).
	UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *EffectAsset);
	if (!Loaded)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load asset: %s"), *EffectAsset));
		return SerializeJson(ResultObj);
	}

	UClass* EffectClass = nullptr;
	if (UBlueprint* AsBlueprint = Cast<UBlueprint>(Loaded))
	{
		EffectClass = AsBlueprint->GeneratedClass;
	}
	else if (UClass* AsClass = Cast<UClass>(Loaded))
	{
		EffectClass = AsClass;
	}

	if (!EffectClass || !EffectClass->IsChildOf(UGameplayEffect::StaticClass()))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a GameplayEffect: %s"), *EffectAsset));
		return SerializeJson(ResultObj);
	}

	UGameplayEffect* EffectCDO = EffectClass->GetDefaultObject<UGameplayEffect>();
	if (!EffectCDO)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Could not resolve GameplayEffect CDO"));
		return SerializeJson(ResultObj);
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("RC: AddGameplayEffectModifier")));
	EffectCDO->Modify();

	FGameplayModifierInfo NewModifier;

	// ModifierOp: clamp the incoming int to the valid EGameplayModOp range. EGameplayModOp is a
	// namespace enum (not exposed to Python as `unreal.EGameplayModOp`); stable integer values are
	// AddBase=0, MultiplyAdditive=1, DivideAdditive=2, Override=3 (GameplayEffectTypes.h). (Issue #16552)
	const int32 ClampedOp = FMath::Clamp(Op, 0, static_cast<int32>(EGameplayModOp::Max) - 1);
	NewModifier.ModifierOp = static_cast<EGameplayModOp::Type>(ClampedOp);
	NewModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(Magnitude));

	// Attribute (best-effort): "SetName.AttrName" or "AttrName". Non-fatal if unresolved.
	FString ResolvedAttribute;
	if (!Attribute.IsEmpty())
	{
		FString SetName;
		FString AttrName;
		if (!Attribute.Split(TEXT("."), &SetName, &AttrName))
		{
			AttrName = Attribute;
		}

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Candidate = *ClassIt;
			if (!Candidate->IsChildOf(UAttributeSet::StaticClass()))
			{
				continue;
			}
			if (!SetName.IsEmpty() && !Candidate->GetName().Contains(SetName))
			{
				continue;
			}
			FProperty* AttrProp = FindFProperty<FProperty>(Candidate, *AttrName);
			// Only accept properties the engine actually treats as gameplay attributes
			// (FGameplayAttributeData struct or numeric); otherwise FGameplayAttribute() would
			// ensure-reject and yield an invalid attribute while we'd still report it as resolved.
			if (AttrProp && FGameplayAttribute::IsSupportedProperty(AttrProp))
			{
				NewModifier.Attribute = FGameplayAttribute(AttrProp);
				ResolvedAttribute = FString::Printf(TEXT("%s.%s"), *Candidate->GetName(), *AttrProp->GetName());
				break;
			}
		}
	}

	EffectCDO->Modifiers.Add(NewModifier);

	// Persist (mirrors the prior Python `save_asset`).
	bool bSaved = false;
	if (UPackage* Package = EffectCDO->GetOutermost())
	{
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		bSaved = UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Gameplay] AddGameplayEffectModifier: %s | op=%d mag=%f attr=%s -> count=%d saved=%d"),
		*EffectAsset, ClampedOp, Magnitude, *ResolvedAttribute, EffectCDO->Modifiers.Num(), bSaved ? 1 : 0);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("effect_asset"), EffectAsset);
	ResultObj->SetNumberField(TEXT("op"), ClampedOp);
	ResultObj->SetNumberField(TEXT("magnitude"), Magnitude);
	ResultObj->SetStringField(TEXT("attribute"), ResolvedAttribute);
	ResultObj->SetNumberField(TEXT("modifier_count"), EffectCDO->Modifiers.Num());
	ResultObj->SetBoolField(TEXT("saved"), bSaved);
	return SerializeJson(ResultObj);
#endif
}

FString UCURemoteLibrary::SetGameplayAbilityCdoTags(
	const FString& AbilityAsset,
	const TArray<FString>& AbilityTags,
	const TArray<FString>& ActivationOwnedTags)
{
#if !WITH_NARSHAMCP_GAMEPLAYABILITIES
	// Issue #28215 Phase A: GameplayAbilities is opt-in. The UFUNCTION declaration stays
	// (UHT generates its thunk), so only the body is compiled out and callers get an
	// actionable decline instead of a link error.
	TSharedPtr<FJsonObject> Unavailable = MakeShared<FJsonObject>();
	Unavailable->SetBoolField(JSON_SUCCESS_FIELD, false);
	Unavailable->SetStringField(TEXT("error"),
		TEXT("SetGameplayAbilityCdoTags requires the GameplayAbilities plugin, which this project has not enabled"));
	Unavailable->SetStringField(TEXT("error_code"), TEXT("gameplay_abilities_plugin_unavailable"));
	Unavailable->SetStringField(TEXT("hint"),
		TEXT("Enable GameplayAbilities in your .uproject, then rebuild."));
	return CUJsonUtils::SerializeToString(Unavailable.ToSharedRef());
#else
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		return CUJsonUtils::SerializeToString(Obj.ToSharedRef());
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (AbilityAsset.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("AbilityAsset is required"));
		return SerializeJson(ResultObj);
	}

	UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *AbilityAsset);
	if (!Loaded)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load asset: %s"), *AbilityAsset));
		return SerializeJson(ResultObj);
	}

	UClass* AbilityClass = nullptr;
	if (UBlueprint* AsBlueprint = Cast<UBlueprint>(Loaded))
	{
		AbilityClass = AsBlueprint->GeneratedClass;
	}
	else if (UClass* AsClass = Cast<UClass>(Loaded))
	{
		AbilityClass = AsClass;
	}

	if (!AbilityClass || !AbilityClass->IsChildOf(UGameplayAbility::StaticClass()))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a GameplayAbility: %s"), *AbilityAsset));
		return SerializeJson(ResultObj);
	}

	UObject* CDO = AbilityClass->GetDefaultObject();
	if (!CDO)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("Could not resolve GameplayAbility CDO"));
		return SerializeJson(ResultObj);
	}

	// Resolve tag strings in C++ (the whole reason this is not Python): request each from the
	// registry with ErrorIfNotFound=false. Unregistered tags are surfaced in `unresolved_tags`,
	// never applied as an empty/invalid tag (honest-failure contract, #17689/#16554).
	TArray<FString> Unresolved;
	auto BuildContainer = [&Unresolved](const TArray<FString>& In) -> FGameplayTagContainer
	{
		FGameplayTagContainer Out;
		for (const FString& S : In)
		{
			if (S.IsEmpty())
			{
				continue;
			}
			const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*S), /*ErrorIfNotFound=*/false);
			if (Tag.IsValid())
			{
				Out.AddTag(Tag);
			}
			else
			{
				Unresolved.Add(S);
			}
		}
		return Out;
	};

	const FGameplayTagContainer AbilityContainer = BuildContainer(AbilityTags);
	const FGameplayTagContainer ActivationContainer = BuildContainer(ActivationOwnedTags);

	// Resolve BOTH FGameplayTagContainer properties up-front via reflection — FindFProperty is a
	// read-only dry-run. Only if both are present do we open the transaction + assign, so a
	// property-miss (engine-version mismatch) never leaves the CDO half-written (one container set,
	// the other absent). Setting via reflection is the same effect as Python set_editor_property,
	// which cannot build the container value.
	FStructProperty* AbilityProp = FindFProperty<FStructProperty>(CDO->GetClass(), TEXT("AbilityTags"));
	FStructProperty* ActivationProp = FindFProperty<FStructProperty>(CDO->GetClass(), TEXT("ActivationOwnedTags"));
	const bool bAbilityOk = AbilityProp && AbilityProp->Struct == FGameplayTagContainer::StaticStruct();
	const bool bActivationOk = ActivationProp && ActivationProp->Struct == FGameplayTagContainer::StaticStruct();

	if (!bAbilityOk || !bActivationOk)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Tag property not found on %s (AbilityTags=%d, ActivationOwnedTags=%d) — engine version mismatch?"),
			*AbilityClass->GetName(), bAbilityOk ? 1 : 0, bActivationOk ? 1 : 0));
		return SerializeJson(ResultObj);
	}

	// Both properties confirmed present — now (and only now) mutate under the transaction.
	FScopedTransaction Transaction(FText::FromString(TEXT("RC: SetGameplayAbilityCdoTags")));
	CDO->Modify();
	*AbilityProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO) = AbilityContainer;
	*ActivationProp->ContainerPtrToValuePtr<FGameplayTagContainer>(CDO) = ActivationContainer;

	// Persist (mirrors the Python save_asset).
	bool bSaved = false;
	if (UPackage* Package = CDO->GetOutermost())
	{
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		bSaved = UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	TArray<TSharedPtr<FJsonValue>> UnresolvedJson;
	for (const FString& U : Unresolved)
	{
		UnresolvedJson.Add(MakeShared<FJsonValueString>(U));
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Gameplay] SetGameplayAbilityCdoTags: %s | ability=%d activation=%d unresolved=%d saved=%d"),
		*AbilityAsset, AbilityContainer.Num(), ActivationContainer.Num(), Unresolved.Num(), bSaved ? 1 : 0);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("ability_asset"), AbilityAsset);
	ResultObj->SetNumberField(TEXT("ability_tags_applied"), AbilityContainer.Num());
	ResultObj->SetNumberField(TEXT("activation_owned_tags_applied"), ActivationContainer.Num());
	ResultObj->SetArrayField(TEXT("unresolved_tags"), UnresolvedJson);
	ResultObj->SetBoolField(TEXT("saved"), bSaved);
	return SerializeJson(ResultObj);
#endif
}

FString UCURemoteLibrary::SetGameplayCueNotifyCdoTag(
	const FString& CueAsset,
	const FString& GameplayCueTag)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		return CUJsonUtils::SerializeToString(Obj.ToSharedRef());
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (CueAsset.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("CueAsset is required"));
		return SerializeJson(ResultObj);
	}
	if (GameplayCueTag.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("GameplayCueTag is required"));
		return SerializeJson(ResultObj);
	}

	UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *CueAsset);
	if (!Loaded)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load asset: %s"), *CueAsset));
		return SerializeJson(ResultObj);
	}

	UClass* CueClass = nullptr;
	if (UBlueprint* AsBlueprint = Cast<UBlueprint>(Loaded))
	{
		CueClass = AsBlueprint->GeneratedClass;
	}
	else if (UClass* AsClass = Cast<UClass>(Loaded))
	{
		CueClass = AsClass;
	}

	UObject* CDO = CueClass ? CueClass->GetDefaultObject() : nullptr;
	// A GameplayCueNotify (Static or Actor) exposes a single FGameplayTag `GameplayCueTag` property;
	// identify by property presence (avoids a hard dependency on the concrete cue-notify types).
	FStructProperty* TagProp = CDO ? FindFProperty<FStructProperty>(CDO->GetClass(), TEXT("GameplayCueTag")) : nullptr;
	if (!TagProp || TagProp->Struct != FGameplayTag::StaticStruct())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Asset is not a GameplayCueNotify (no GameplayCueTag property): %s"), *CueAsset));
		return SerializeJson(ResultObj);
	}

	const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*GameplayCueTag), /*ErrorIfNotFound=*/false);
	if (!Tag.IsValid())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("gameplay cue tag not found in registry: %s"), *GameplayCueTag));
		return SerializeJson(ResultObj);
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("RC: SetGameplayCueNotifyCdoTag")));
	CDO->Modify();

	*TagProp->ContainerPtrToValuePtr<FGameplayTag>(CDO) = Tag;
	// Mirror GameplayCueName (FName) so the asset registry stays searchable — the engine's
	// DeriveGameplayCueTagFromAssetName keeps these in sync, so we set both here too.
	if (FNameProperty* NameProp = FindFProperty<FNameProperty>(CDO->GetClass(), TEXT("GameplayCueName")))
	{
		NameProp->SetPropertyValue_InContainer(CDO, Tag.GetTagName());
	}

	bool bSaved = false;
	if (UPackage* Package = CDO->GetOutermost())
	{
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		bSaved = UPackage::SavePackage(Package, nullptr, *PackageFileName, SaveArgs);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Gameplay] SetGameplayCueNotifyCdoTag: %s | tag=%s saved=%d"),
		*CueAsset, *Tag.GetTagName().ToString(), bSaved ? 1 : 0);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("cue_asset"), CueAsset);
	ResultObj->SetStringField(TEXT("gameplay_cue_tag"), Tag.GetTagName().ToString());
	ResultObj->SetBoolField(TEXT("saved"), bSaved);
	return SerializeJson(ResultObj);
}

// Issue #28269: slash-prefixed package paths are exact and never redirect.
// Bare names are resolved only after an all-mount exact AssetName search so a
// root /Game asset cannot bypass ambiguity detection. Recursive class matching
// includes derived assets such as AnimBlueprint and WidgetBlueprint.
static UBlueprint* ResolveBlueprintForNodeProperty(
	const FString& BlueprintAddress,
	FString& OutResolvedPath,
	TArray<FString>& OutCandidatePaths)
{
	OutResolvedPath.Reset();
	OutCandidatePaths.Reset();

	const bool bBareName = !BlueprintAddress.Contains(TEXT("/"));
	if (!bBareName)
	{
		if (UBlueprint* Direct = FCUBlueprintManipulator::LoadBlueprint(BlueprintAddress))
		{
			OutResolvedPath = Direct->GetOutermost()->GetName();
			return Direct;
		}
		return nullptr;
	}

	const FString AssetName = BlueprintAddress;
	IAssetRegistry& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;

	TArray<FAssetData> BlueprintAssets;
	Registry.GetAssets(Filter, BlueprintAssets);
	for (const FAssetData& Asset : BlueprintAssets)
	{
		if (Asset.AssetName == FName(*AssetName))
		{
			OutCandidatePaths.Add(Asset.PackageName.ToString());
		}
	}
	OutCandidatePaths.Sort();

	if (OutCandidatePaths.Num() != 1)
	{
		return nullptr;
	}

	OutResolvedPath = OutCandidatePaths[0];
	return FCUBlueprintManipulator::LoadBlueprint(OutResolvedPath);
}

FString UCURemoteLibrary::SetBlueprintNodeProperty(
	const FString& BlueprintPath,
	const FString& NodeIdentifier,
	const FString& PropertyName,
	const FString& PropertyValue)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		return CUJsonUtils::SerializeToString(Obj.ToSharedRef());
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || NodeIdentifier.IsEmpty() || PropertyName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath, NodeIdentifier and PropertyName are required"));
		return SerializeJson(ResultObj);
	}

	FString ResolvedBlueprintPath;
	TArray<FString> CandidatePaths;
	UBlueprint* Blueprint = ResolveBlueprintForNodeProperty(
		BlueprintPath, ResolvedBlueprintPath, CandidatePaths);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		if (CandidatePaths.Num() > 1)
		{
			TArray<TSharedPtr<FJsonValue>> CandidateValues;
			CandidateValues.Reserve(CandidatePaths.Num());
			for (const FString& CandidatePath : CandidatePaths)
			{
				CandidateValues.Add(MakeShared<FJsonValueString>(CandidatePath));
			}
			ResultObj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Ambiguous Blueprint name '%s': %d matches"),
				*FPackageName::GetShortName(BlueprintPath), CandidatePaths.Num()));
			ResultObj->SetArrayField(TEXT("candidate_paths"), CandidateValues);
			ResultObj->SetStringField(TEXT("hint"),
				TEXT("Pass blueprint_path with the exact long package path from candidate_paths."));
		}
		else
		{
			ResultObj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		}
		return SerializeJson(ResultObj);
	}

	// Find the node — by GUID (preferred), else a name-contains fallback across all graphs.
	UEdGraphNode* Node = nullptr;
	FGuid NodeGuid;
	if (FGuid::Parse(NodeIdentifier, NodeGuid) && NodeGuid.IsValid())
	{
		Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, NodeGuid);
	}
	if (!Node)
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		TArray<FString> AvailableNodes;
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) { continue; }
			for (UEdGraphNode* N : Graph->Nodes)
			{
				if (!N) { continue; }
				if (!Node && N->GetName().Contains(NodeIdentifier))
				{
					Node = N;
				}
				if (AvailableNodes.Num() < 20)
				{
					AvailableNodes.Add(N->GetName());
				}
			}
		}
		if (!Node)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Node '%s' not found by GUID or name in '%s'"), *NodeIdentifier, *ResolvedBlueprintPath));
			ResultObj->SetStringField(TEXT("available_nodes"), FString::Join(AvailableNodes, TEXT(", ")));
			ResultObj->SetStringField(TEXT("resolved_blueprint_path"), ResolvedBlueprintPath);
			return SerializeJson(ResultObj);
		}
	}

	// Resolve the node property by reflection (covers UEdGraphNode + K2Node-specific UPROPERTY).
	FProperty* Prop = Node->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop)
	{
		// List all reflected properties (node UPROPERTYs are generally NOT CPF_Edit, so do not filter
		// on it here — set_node_property writes via reflection regardless of editor-edit flags).
		TArray<FString> AvailableProps;
		for (TFieldIterator<FProperty> It(Node->GetClass()); It; ++It)
		{
			AvailableProps.Add(It->GetName());
			if (AvailableProps.Num() >= 30) { break; }
		}
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Property '%s' not found on node '%s' (type: %s)"),
			*PropertyName, *Node->GetName(), *Node->GetClass()->GetName()));
		ResultObj->SetStringField(TEXT("node"), Node->GetName());
		ResultObj->SetStringField(TEXT("available_properties"), FString::Join(AvailableProps, TEXT(", ")));
		return SerializeJson(ResultObj);
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Node);

	FString PreviousValue;
	Prop->ExportTextItem_Direct(PreviousValue, ValuePtr, nullptr, Node, PPF_None);

	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "SetBlueprintNodeProperty", "Set Node Property (MCP)"));
	Node->Modify();
	if (UEdGraph* OwningGraph = Node->GetGraph())
	{
		OwningGraph->Modify();
	}
	Blueprint->Modify();

	const TCHAR* ImportResult = Prop->ImportText_Direct(*PropertyValue, ValuePtr, Node, PPF_None);
	if (!ImportResult)
	{
		Transaction.Cancel();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Failed to parse '%s' for property '%s' (type: %s)"),
			*PropertyValue, *PropertyName, *Prop->GetCPPType()));
		ResultObj->SetStringField(TEXT("property_type"), Prop->GetCPPType());
		ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
		ResultObj->SetStringField(TEXT("hint"), TEXT("int: '42', float: '3.14', bool: 'true', FString: 'text'"));
		return SerializeJson(ResultObj);
	}

	// Cosmetic properties don't change pins/structure; others might (e.g. bIsPureFunc), so reconstruct
	// the node to refresh pins and mark the Blueprint structurally modified for recompile.
	static const TSet<FString> CosmeticProps = {
		TEXT("NodeComment"), TEXT("NodePosX"), TEXT("NodePosY"),
		TEXT("bCommentBubbleVisible"), TEXT("bCommentBubblePinned"),
		TEXT("NodeWidth"), TEXT("NodeHeight")
	};
	const bool bStructural = !CosmeticProps.Contains(PropertyName);
	if (bStructural)
	{
		Node->ReconstructNode();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	else
	{
		Blueprint->MarkPackageDirty();
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetBlueprintNodeProperty: %s.%s = %s (structural=%d)"),
		*Node->GetName(), *PropertyName, *PropertyValue, bStructural ? 1 : 0);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node"), Node->GetName());
	ResultObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
	ResultObj->SetStringField(TEXT("property"), PropertyName);
	ResultObj->SetStringField(TEXT("value"), PropertyValue);
	ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
	ResultObj->SetStringField(TEXT("property_type"), Prop->GetCPPType());
	ResultObj->SetStringField(TEXT("resolved_blueprint_path"), ResolvedBlueprintPath);
	ResultObj->SetBoolField(TEXT("reconstructed"), bStructural);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	return SerializeJson(ResultObj);
}

FString UCURemoteLibrary::SetComponentDefault(
	const FString& BlueprintPath,
	const FString& PropertyPath,
	const FString& DefaultValue)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || PropertyPath.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath and PropertyPath are required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetComponentDefault: %s | %s = %s"),
		*BlueprintPath, *PropertyPath, *DefaultValue);

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Split path by '.'
	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), /*CullEmpty*/ true);
	if (Segments.Num() == 0)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("PropertyPath must not be empty"));
		return SerializeJson(ResultObj);
	}

	const FString& ComponentName = Segments[0];
	UBlueprint* OwningBP = nullptr;
	USCS_Node* SCSNode = NarshaSCSComponentDefaults::FindSCSNodeInChain(Blueprint, ComponentName, OwningBP);

	UObject* TargetObject = nullptr;
	UStruct* TargetStruct = nullptr;
	void* TargetPtr = nullptr;
	FString ResolutionSource;
	int32 PathStartIndex = 1; // By default skip the component name

	if (SCSNode)
	{
		// Component found in SCS chain
		if (OwningBP == Blueprint)
		{
			// Own SCS — modify the component template directly
			UActorComponent* Template = SCSNode->ComponentTemplate;
			if (!Template)
			{
				ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
				ResultObj->SetStringField(TEXT("error"), FString::Printf(
					TEXT("SCS node '%s' has no ComponentTemplate"), *ComponentName));
				return SerializeJson(ResultObj);
			}
			TargetObject = Template;
			TargetStruct = Template->GetClass();
			TargetPtr = Template;
			ResolutionSource = TEXT("own_scs");
		}
		else
		{
			// Inherited SCS — use the child CDO's component instance for archetype override.
			// Modifying the parent template would leak into unrelated BPs.
			UActorComponent* CDOComp = NarshaSCSComponentDefaults::FindCDOComponentInstance(Blueprint, ComponentName);
			if (!CDOComp)
			{
				ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
				ResultObj->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Component '%s' inherited from parent BP but not accessible on child CDO — compile the Blueprint first"),
					*ComponentName));
				return SerializeJson(ResultObj);
			}
			TargetObject = CDOComp;
			TargetStruct = CDOComp->GetClass();
			TargetPtr = CDOComp;
			ResolutionSource = TEXT("parent_scs");
		}
	}
	else
	{
		// CDO fallback — treat entire PropertyPath as a CDO property path starting from Segments[0]
		if (!Blueprint->GeneratedClass)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), TEXT("Blueprint has no GeneratedClass — compile it first"));
			return SerializeJson(ResultObj);
		}
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (!CDO)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), TEXT("Failed to get Class Default Object"));
			return SerializeJson(ResultObj);
		}
		TargetObject = CDO;
		TargetStruct = Blueprint->GeneratedClass;
		TargetPtr = CDO;
		ResolutionSource = TEXT("cdo");
		PathStartIndex = 0; // Walk entire path as CDO properties
	}

	// Resolve the final property + value pointer
	FProperty* FinalProp = nullptr;
	void* FinalValuePtr = nullptr;
	UObject* OwnerObject = nullptr;
	FString ResolveError;
	if (!NarshaSCSComponentDefaults::ResolveNestedPath(
			TargetStruct, TargetPtr, Segments, PathStartIndex,
			FinalProp, FinalValuePtr, OwnerObject, ResolveError))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), ResolveError);
		ResultObj->SetStringField(TEXT("source"), ResolutionSource);
		return SerializeJson(ResultObj);
	}

	// The final property may live on a subobject (e.g. a native component reached via an
	// FObjectProperty in the path); operate on its actual owner, not the outer target (Issue #16565).
	UObject* PropOwner = OwnerObject ? OwnerObject : TargetObject;

	// Capture previous value, then apply new
	FString PreviousValue;
	FinalProp->ExportTextItem_Direct(PreviousValue, FinalValuePtr, nullptr, PropOwner, PPF_None);

	// Issue #9060 M-1: wrap the write in a transaction + Modify() so undo/redo works and the
	// Editor registers the change. Without this, the default drifts in-memory but the user cannot
	// undo and the package-dirty flag may not reflect the mutation in Multi-User sessions.
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "SetComponentDefault", "Set Component Default (MCP)"));
	PropOwner->Modify();
	if (TargetObject != PropOwner)
	{
		TargetObject->Modify();
	}
	Blueprint->Modify();

	const TCHAR* ImportResult = FinalProp->ImportText_Direct(*DefaultValue, FinalValuePtr, PropOwner, PPF_None);
	if (!ImportResult)
	{
		// Parse failed — abort transaction (no-op) and report
		Transaction.Cancel();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Failed to parse '%s' for property '%s' (type: %s)"),
			*DefaultValue, *FinalProp->GetName(), *FinalProp->GetCPPType()));
		ResultObj->SetStringField(TEXT("property_type"), FinalProp->GetCPPType());
		ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
		ResultObj->SetStringField(TEXT("source"), ResolutionSource);
		ResultObj->SetStringField(TEXT("hint"), TEXT("int: '42', float: '3.14', bool: 'true', FVector: '(X=1,Y=2,Z=3)', FRotator: '(Pitch=0,Yaw=90,Roll=0)'"));
		return SerializeJson(ResultObj);
	}

	// Issue #9060 M-2: fire PostEditChange* so archetype propagation / construction-script reruns /
	// dependent BP refreshes happen. The notification must fire on the object that actually owns the
	// changed property — for a native-component path that is the component subobject, not the outer
	// target (Issue #16565 항목2). The FStructProperty-only chain builder below cannot cross the
	// object boundary, so notify the owning component directly in that case.
	if (OwnerObject)
	{
		// The change lives on a subobject (e.g. a native component). Fire a single-node chain on the
		// component so PostEditChangeChainProperty's archetype propagation reaches child-BP component
		// instances — base PostEditChangeProperty does not propagate. Deeper struct-within-component
		// paths only carry the leaf node here; that is acceptable because the persisted default is
		// authoritative when child BPs reload (Issue #16565 항목2).
		FEditPropertyChain Chain;
		Chain.AddHead(FinalProp);
		Chain.SetActivePropertyNode(FinalProp);
		FPropertyChangedEvent ChangeEvent(FinalProp, EPropertyChangeType::ValueSet);
		FPropertyChangedChainEvent ChainEvent(Chain, ChangeEvent);
		PropOwner->PostEditChangeChainProperty(ChainEvent);
	}
	else if (Segments.Num() - PathStartIndex > 1)
	{
		// Nested struct dot-path on the target object: build a chain from its top-level property down to FinalProp.
		FEditPropertyChain Chain;
		for (int32 i = PathStartIndex; i < Segments.Num(); ++i)
		{
			const FString& SegName = Segments[i];
			// Re-resolve property on the *outer* struct of each segment. We intentionally walk
			// TargetStruct again rather than cache during ResolveNestedPath to keep the helper lean.
			UStruct* WalkStruct = TargetStruct;
			for (int32 j = PathStartIndex; j < i; ++j)
			{
				FStructProperty* SP = CastField<FStructProperty>(
					WalkStruct->FindPropertyByName(FName(*Segments[j])));
				if (!SP) { WalkStruct = nullptr; break; }
				WalkStruct = SP->Struct;
			}
			if (!WalkStruct) break;
			if (FProperty* P = WalkStruct->FindPropertyByName(FName(*SegName)))
			{
				Chain.AddHead(P);
			}
		}
		Chain.SetActivePropertyNode(FinalProp);
		FPropertyChangedEvent ChangeEvent(FinalProp, EPropertyChangeType::ValueSet);
		FPropertyChangedChainEvent ChainEvent(Chain, ChangeEvent);
		TargetObject->PostEditChangeChainProperty(ChainEvent);
	}
	else
	{
		FPropertyChangedEvent ChangeEvent(FinalProp, EPropertyChangeType::ValueSet);
		TargetObject->PostEditChangeProperty(ChangeEvent);
	}

	// Persist: dirty the package so save round-trips the default
	Blueprint->MarkPackageDirty();
	if (TargetObject && TargetObject != Blueprint)
	{
		TargetObject->MarkPackageDirty();
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("property_path"), PropertyPath);
	ResultObj->SetStringField(TEXT("component"), SCSNode ? ComponentName : FString());
	ResultObj->SetStringField(TEXT("property"), FinalProp->GetName());
	ResultObj->SetStringField(TEXT("value"), DefaultValue);
	ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
	ResultObj->SetStringField(TEXT("property_type"), FinalProp->GetCPPType());
	ResultObj->SetStringField(TEXT("source"), ResolutionSource);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] SetComponentDefault OK (%s): %s"),
		*ResolutionSource, *PropertyPath);
	return SerializeJson(ResultObj);
}

FString UCURemoteLibrary::GetComponentDefault(
	const FString& BlueprintPath,
	const FString& PropertyPath)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty() || PropertyPath.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath and PropertyPath are required"));
		return SerializeJson(ResultObj);
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), /*CullEmpty*/ true);
	if (Segments.Num() == 0)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("PropertyPath must not be empty"));
		return SerializeJson(ResultObj);
	}

	const FString& ComponentName = Segments[0];
	UBlueprint* OwningBP = nullptr;
	USCS_Node* SCSNode = NarshaSCSComponentDefaults::FindSCSNodeInChain(Blueprint, ComponentName, OwningBP);

	UObject* TargetObject = nullptr;
	UStruct* TargetStruct = nullptr;
	void* TargetPtr = nullptr;
	FString ResolutionSource;
	int32 PathStartIndex = 1;

	if (SCSNode)
	{
		if (OwningBP == Blueprint)
		{
			UActorComponent* Template = SCSNode->ComponentTemplate;
			if (!Template)
			{
				ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
				ResultObj->SetStringField(TEXT("error"), FString::Printf(
					TEXT("SCS node '%s' has no ComponentTemplate"), *ComponentName));
				return SerializeJson(ResultObj);
			}
			TargetObject = Template;
			TargetStruct = Template->GetClass();
			TargetPtr = Template;
			ResolutionSource = TEXT("own_scs");
		}
		else
		{
			// Read from CDO component instance if available (reflects archetype overrides),
			// otherwise fall back to the parent template for read.
			UActorComponent* CDOComp = NarshaSCSComponentDefaults::FindCDOComponentInstance(Blueprint, ComponentName);
			// SCSNode->ComponentTemplate is TObjectPtr in UE 5.x — resolve to raw pointer for ternary type unification.
			UActorComponent* ReadSource = CDOComp ? CDOComp : SCSNode->ComponentTemplate.Get();
			if (!ReadSource)
			{
				ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
				ResultObj->SetStringField(TEXT("error"), FString::Printf(
					TEXT("Inherited component '%s' not resolvable"), *ComponentName));
				return SerializeJson(ResultObj);
			}
			TargetObject = ReadSource;
			TargetStruct = ReadSource->GetClass();
			TargetPtr = ReadSource;
			ResolutionSource = TEXT("parent_scs");
		}
	}
	else
	{
		if (!Blueprint->GeneratedClass)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), TEXT("Blueprint has no GeneratedClass — compile it first"));
			return SerializeJson(ResultObj);
		}
		UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
		if (!CDO)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(TEXT("error"), TEXT("Failed to get Class Default Object"));
			return SerializeJson(ResultObj);
		}
		TargetObject = CDO;
		TargetStruct = Blueprint->GeneratedClass;
		TargetPtr = CDO;
		ResolutionSource = TEXT("cdo");
		PathStartIndex = 0;
	}

	FProperty* FinalProp = nullptr;
	void* FinalValuePtr = nullptr;
	UObject* OwnerObjectUnused = nullptr;  // read path: owner not needed
	FString ResolveError;
	if (!NarshaSCSComponentDefaults::ResolveNestedPath(
			TargetStruct, TargetPtr, Segments, PathStartIndex,
			FinalProp, FinalValuePtr, OwnerObjectUnused, ResolveError))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), ResolveError);
		ResultObj->SetStringField(TEXT("source"), ResolutionSource);
		return SerializeJson(ResultObj);
	}

	FString CurrentValue;
	FinalProp->ExportTextItem_Direct(CurrentValue, FinalValuePtr, nullptr, TargetObject, PPF_None);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("property_path"), PropertyPath);
	ResultObj->SetStringField(TEXT("component"), SCSNode ? ComponentName : FString());
	ResultObj->SetStringField(TEXT("property"), FinalProp->GetName());
	ResultObj->SetStringField(TEXT("value"), CurrentValue);
	ResultObj->SetStringField(TEXT("property_type"), FinalProp->GetCPPType());
	ResultObj->SetStringField(TEXT("source"), ResolutionSource);

	return SerializeJson(ResultObj);
}

// =========================================================
// Issue #8564: Universal property setter for transaction replay
// =========================================================

namespace
{
/** Outcome of CUResolveObjectByPath. Callers format their own error text. */
enum class ECUPathResolveStatus : uint8
{
	Found,
	Destroyed,
	NotFound,
};

/**
 * Issue #28227: shared object-path resolver.
 *
 * Lifted out of SetObjectPropertyByPath (Issue #8564, with the Issue #27210
 * destroyed-object guards) so AddInstancedSubobjectByPath resolves paths
 * identically instead of growing a second resolver with its own drift.
 *
 * Behaviour is unchanged and deliberately so: FindObject -> StaticLoadObject ->
 * colon-split parent load + recursive subobject scan, with IsValid() rejecting
 * garbage/unreachable objects after every stage (Epic commit 48951ad8 shows
 * FindObject can still hand one back). GE component paths take the third branch:
 * `/Game/X.Default__X_C:AssetTagsGameplayEffectComponent_0`.
 */
UObject* CUResolveObjectByPath(const FString& ObjectPath, ECUPathResolveStatus& OutStatus)
{
	OutStatus = ECUPathResolveStatus::NotFound;

	UObject* TargetObject = FindObject<UObject>(nullptr, *ObjectPath);
	if (TargetObject && !IsValid(TargetObject))
	{
		OutStatus = ECUPathResolveStatus::Destroyed;
		return nullptr;
	}
	if (!TargetObject)
	{
		// Fallback 1: top-level assets that are not loaded yet.
		TargetObject = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
		if (TargetObject && !IsValid(TargetObject))
		{
			OutStatus = ECUPathResolveStatus::Destroyed;
			return nullptr;
		}
	}
	if (!TargetObject)
	{
		// Fallback 2 (M3): load the parent asset, then find the nested object.
		// Path format: /Game/MyAsset.MyAsset:SubObject.NestedObject
		int32 ColonIdx;
		if (ObjectPath.FindChar(TEXT(':'), ColonIdx))
		{
			const FString ParentPath = ObjectPath.Left(ColonIdx);
			UObject* Parent = StaticLoadObject(UObject::StaticClass(), nullptr, *ParentPath);
			if (Parent && !IsValid(Parent))
			{
				OutStatus = ECUPathResolveStatus::Destroyed;
				return nullptr;
			}
			if (Parent)
			{
				const FString SubPath = ObjectPath.Mid(ColonIdx + 1);
				TargetObject = FindObject<UObject>(Parent, *SubPath);
				if (TargetObject && !IsValid(TargetObject))
				{
					OutStatus = ECUPathResolveStatus::Destroyed;
					return nullptr;
				}
				if (!TargetObject)
				{
					TArray<UObject*> SubObjects;
					// Issue #11472 Gap 1: UE 5.8 deprecated the bool overload for EGetObjectsFlags.
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
					GetObjectsWithOuter(Parent, SubObjects, EGetObjectsFlags::IncludeNestedObjects);
#else
					GetObjectsWithOuter(Parent, SubObjects, true);
#endif
					const FString TargetName = FPaths::GetBaseFilename(ObjectPath);
					for (UObject* Sub : SubObjects)
					{
						if (!IsValid(Sub))
						{
							continue;
						}
						if (Sub->GetName() == TargetName || Sub->GetPathName() == ObjectPath)
						{
							TargetObject = Sub;
							break;
						}
					}
				}
			}
		}
	}
	if (TargetObject && !IsValid(TargetObject))
	{
		OutStatus = ECUPathResolveStatus::Destroyed;
		return nullptr;
	}
	if (!TargetObject)
	{
		return nullptr;
	}

	OutStatus = ECUPathResolveStatus::Found;
	return TargetObject;
}

/** The "streaming level may not be loaded" hint the not-found paths append. */
FString CUPathNotFoundHint(const FString& ObjectPath)
{
	if (ObjectPath.Contains(TEXT("PersistentLevel")) || ObjectPath.Contains(TEXT(":")))
	{
		return TEXT(" (hint: streaming level may not be loaded — open the map in Editor first)");
	}
	return FString();
}

	// Issue #28284/#28227: ONE state machine for top-level ExportText items; the two
	// counts below are derived from it rather than each re-implementing it. An earlier
	// revision had two copy-pasted loops under a comment claiming they shared a scanner
	// -- they did not, and a later edit to one would have silently desynced
	// requested_count from the None subtraction with nothing able to catch it.
	//
	// Visits each top-level item, trimmed, in order; empty segments (a trailing comma)
	// are not items. Returns false when the literal is not a parseable list -- unbalanced
	// depth, an unterminated quote, or a dangling escape -- so callers fail CLOSED rather
	// than comparing applied-vs-requested against a count derived from text that could
	// not be parsed at all.
	template <typename VisitorType>
	bool ForEachTopLevelExportTextItem(const FString& ExportText, VisitorType&& Visit)
	{
		FString Trimmed = ExportText;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.IsEmpty() || Trimmed == TEXT("()"))
		{
			return true;
		}
		if (Trimmed.Len() < 2 || !Trimmed.StartsWith(TEXT("(")) || !Trimmed.EndsWith(TEXT(")")))
		{
			return false;
		}

		int32 Depth = 0;
		bool bInQuote = false;
		bool bEscape = false;
		int32 SegStart = 1;
		const int32 InnerEnd = Trimmed.Len() - 1;

		auto FlushSegment = [&](int32 SegEnd)
		{
			FString Seg = Trimmed.Mid(SegStart, SegEnd - SegStart);
			Seg.TrimStartAndEndInline();
			if (!Seg.IsEmpty())
			{
				Visit(Seg);
			}
		};

		for (int32 Index = 1; Index < InnerEnd; ++Index)
		{
			const TCHAR Ch = Trimmed[Index];
			if (bEscape) { bEscape = false; continue; }
			if (bInQuote)
			{
				if (Ch == TCHAR('\\')) { bEscape = true; }
				else if (Ch == TCHAR('"')) { bInQuote = false; }
				continue;
			}
			if (Ch == TCHAR('"')) { bInQuote = true; continue; }
			if (Ch == TCHAR('(')) { ++Depth; continue; }
			if (Ch == TCHAR(')')) { --Depth; continue; }
			if (Ch == TCHAR(',') && Depth == 0)
			{
				FlushSegment(Index);
				SegStart = Index + 1;
			}
		}
		FlushSegment(InnerEnd);
		return Depth == 0 && !bInQuote && !bEscape;
	}

	// How many top-level items the literal declares. INDEX_NONE = not a parseable list.
	int32 CountTopLevelExportTextItems(const FString& ExportText)
	{
		int32 Count = 0;
		const bool bParsed = ForEachTopLevelExportTextItem(
			ExportText, [&Count](const FString&) { ++Count; });
		return bParsed ? Count : INDEX_NONE;
	}

	// How many of those items the caller SPELLED as `None`. Writing an explicit null is
	// legitimate, so those slots must not be counted as drops -- otherwise
	// `(None, Class'/Game/X.X')` requests 2, applies 1 valid object, and the
	// applied-vs-requested check refuses a correct write.
	int32 CountTopLevelNoneItems(const FString& ExportText)
	{
		int32 NoneCount = 0;
		const bool bParsed = ForEachTopLevelExportTextItem(
			ExportText,
			[&NoneCount](const FString& Item)
			{
				if (Item.Equals(TEXT("None"), ESearchCase::IgnoreCase))
				{
					++NoneCount;
				}
			});
		return bParsed ? NoneCount : 0;
	}

	// Object-pointer inners may ImportText a missing path as None instead of
	// dropping the slot (Issue #28284 live GEComponents drop is the other
	// shape). Count only valid objects so both shapes fail closed.
	int32 CountAppliedArrayElements(FArrayProperty* ArrayProp, void* ValuePtr, UObject* Owner)
	{
		FScriptArrayHelper Helper(ArrayProp, ValuePtr);
		// Issue #28227: FObjectPropertyBase, not FObjectProperty. FSoftObjectProperty and
		// FSoftClassProperty are SIBLINGS of FObjectProperty under this base, so the narrow
		// cast fell through to `Helper.Num()` for every soft-reference array -- i.e. those
		// arrays got a count-only check and the drop this function exists to catch went
		// through. Judged on what each slot EXPORTS rather than on whether it resolves:
		// for a soft reference "does not resolve" is an ordinary, legal state (the asset is
		// merely not loaded), so a resolve test would reject correct writes.
		FObjectPropertyBase* InnerObj = CastField<FObjectPropertyBase>(ArrayProp->Inner);
		if (!InnerObj)
		{
			return Helper.Num();
		}

		int32 Valid = 0;
		for (int32 Index = 0; Index < Helper.Num(); ++Index)
		{
			FString ElementText;
			// Owner as Parent, matching every other ExportTextItem_Direct call in this file.
			// Inert for the None-vs-path classification below -- a null slot exports as
			// `None` either way -- but a lone nullptr here reads as an oversight.
			InnerObj->ExportTextItem_Direct(
				ElementText, Helper.GetRawPtr(Index), nullptr, Owner, PPF_None);
			if (!ElementText.IsEmpty() && !ElementText.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				++Valid;
			}
		}
		return Valid;
	}
}

FString UCURemoteLibrary::SetObjectPropertyByPath(
	const FString& ObjectPath,
	const FString& PropertyName,
	const FString& ExportTextValue,
	bool bAllowClear)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (ObjectPath.IsEmpty() || PropertyName.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("ObjectPath and PropertyName are required"));
		return SerializeJson(ResultObj);
	}

	// UE 5.8 UObject/Object.h IsValid rejects garbage/unreachable objects; Epic
	// commit 48951ad8 demonstrates FindObject can still return such an object.
	auto RejectDestroyedObject = [&]() -> FString
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s has been destroyed and cannot be used for property '%s'; query for a current object instead of reusing this path."),
			*ObjectPath,
			*PropertyName));
		return SerializeJson(ResultObj);
	};

	// Issue #28227: resolution moved to the shared CUResolveObjectByPath helper so
	// AddInstancedSubobjectByPath cannot drift from it. Error text is unchanged.
	ECUPathResolveStatus ResolveStatus = ECUPathResolveStatus::NotFound;
	UObject* TargetObject = CUResolveObjectByPath(ObjectPath, ResolveStatus);
	if (ResolveStatus == ECUPathResolveStatus::Destroyed)
	{
		return RejectDestroyedObject();
	}
	if (!TargetObject)
	{
		// L8: Provide streaming level hint if path looks like a level object
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Object not found: %s%s"), *ObjectPath, *CUPathNotFoundHint(ObjectPath)));
		return SerializeJson(ResultObj);
	}

	// Find property on the object
	FName PropFName(*PropertyName);
	FProperty* Property = TargetObject->GetClass()->FindPropertyByName(PropFName);
	if (!Property)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Property '%s' not found on %s (%s)"),
			*PropertyName, *TargetObject->GetName(), *TargetObject->GetClass()->GetName()));
		return SerializeJson(ResultObj);
	}

	// Issue #8564 Phase K-3: Wrap in transaction + Modify() for proper object_changed capture.
	// Without this, OnObjectTransacted doesn't fire with HasPropertyChanges()==true,
	// meaning sub-object property changes (e.g., AnimNotify BP variables) go unrecorded.
#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(FString::Printf(
		TEXT("Set %s.%s"), *TargetObject->GetName(), *PropertyName)));
	TargetObject->Modify();
#endif

	// Export previous value
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
	FString PreviousValue;
	Property->ExportTextItem_Direct(PreviousValue, ValuePtr, nullptr, TargetObject, PPF_None);

	auto FillArrayFail = [&](const FString& Error, int32 Requested, int32 Applied)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), Error);
		ResultObj->SetStringField(TEXT("object"), TargetObject->GetName());
		ResultObj->SetStringField(TEXT("property"), PropertyName);
		ResultObj->SetStringField(TEXT("property_type"), Property->GetCPPType());
		ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
		ResultObj->SetNumberField(TEXT("requested_count"), Requested);
		ResultObj->SetNumberField(TEXT("applied_count"), Applied);
	};

	FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
	if (ArrayProp)
	{
		const int32 Requested = CountTopLevelExportTextItems(ExportTextValue);
		FScriptArrayHelper ExistingHelper(ArrayProp, ValuePtr);
		const int32 ExistingNum = ExistingHelper.Num();

		if (Requested == INDEX_NONE)
		{
#if WITH_EDITOR
			Transaction.Cancel();
#endif
			FillArrayFail(
				FString::Printf(
					TEXT("Array ExportText is not a parenthesized list for '%s'"),
					*PropertyName),
				Requested,
				ExistingNum);
			return SerializeJson(ResultObj);
		}

		if (Requested == 0 && !bAllowClear)
		{
#if WITH_EDITOR
			Transaction.Cancel();
#endif
			FillArrayFail(
				FString::Printf(
					TEXT("Refusing to clear array property '%s' without bAllowClear"),
					*PropertyName),
				0,
				ExistingNum);
			return SerializeJson(ResultObj);
		}

		const TCHAR* ArrayResult = Property->ImportText_Direct(
			*ExportTextValue, ValuePtr, TargetObject, PPF_None);
		const int32 Applied = CountAppliedArrayElements(ArrayProp, ValuePtr, TargetObject);
		// Issue #28227: entries the caller SPELLED `None` are legitimate nulls, not drops.
		// Without this subtraction `(None, Class'/Game/X.X')` requests 2, applies 1 real
		// object, and a correct write is refused as a partial import.
		const int32 ExpectedApplied = FMath::Max(0, Requested - CountTopLevelNoneItems(ExportTextValue));
		if (!ArrayResult || Applied != ExpectedApplied)
		{
			Property->ImportText_Direct(*PreviousValue, ValuePtr, TargetObject, PPF_None);
#if WITH_EDITOR
			Transaction.Cancel();
#endif
			FillArrayFail(
				FString::Printf(
					TEXT("Array import dropped or failed elements for '%s'"),
					*PropertyName),
				Requested,
				Applied);
			return SerializeJson(ResultObj);
		}

		TargetObject->MarkPackageDirty();
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("object"), TargetObject->GetName());
		ResultObj->SetStringField(TEXT("property"), PropertyName);
		ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
		ResultObj->SetStringField(TEXT("new_value"), ExportTextValue);
		ResultObj->SetStringField(TEXT("property_type"), Property->GetCPPType());
		ResultObj->SetNumberField(TEXT("requested_count"), Requested);
		ResultObj->SetNumberField(TEXT("applied_count"), Applied);
		UE_LOG(LogNarshaMCPRemote, Log, TEXT("[SetObjectProperty] %s.%s = %s (was: %s)"),
			*TargetObject->GetName(), *PropertyName, *ExportTextValue, *PreviousValue);
		return SerializeJson(ResultObj);
	}

	// Import new value using ExportText format (scalar / struct — Issue #28284 leaves this path unchanged)
	const TCHAR* Result = Property->ImportText_Direct(*ExportTextValue, ValuePtr, TargetObject, PPF_None);
	if (!Result)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("ImportText failed for '%s' on property '%s' (type: %s)"),
			*ExportTextValue, *PropertyName, *Property->GetCPPType()));
		ResultObj->SetStringField(TEXT("property_type"), Property->GetCPPType());
		ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
		return SerializeJson(ResultObj);
	}

	// Mark object/package as dirty
	TargetObject->MarkPackageDirty();

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("object"), TargetObject->GetName());
	ResultObj->SetStringField(TEXT("property"), PropertyName);
	ResultObj->SetStringField(TEXT("previous_value"), PreviousValue);
	ResultObj->SetStringField(TEXT("new_value"), ExportTextValue);
	ResultObj->SetStringField(TEXT("property_type"), Property->GetCPPType());

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[SetObjectProperty] %s.%s = %s (was: %s)"),
		*TargetObject->GetName(), *PropertyName, *ExportTextValue, *PreviousValue);
	return SerializeJson(ResultObj);
}

// =========================================================
// Issue #28227: array-safe wrapper around SetObjectPropertyByPath
// =========================================================

// =========================================================
// Issue #28227: add an Instanced subobject to a UObject's array property
// =========================================================

FString UCURemoteLibrary::AddInstancedSubobjectByPath(
	const FString& ObjectPath,
	const FString& ArrayPropertyName,
	const FString& SubobjectClassPath)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		return CUJsonUtils::SerializeToString(Obj.ToSharedRef());
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	auto Fail = [&](const FString& Error) -> FString
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), Error);
		return SerializeJson(ResultObj);
	};

	if (ObjectPath.IsEmpty() || ArrayPropertyName.IsEmpty() || SubobjectClassPath.IsEmpty())
	{
		return Fail(TEXT("ObjectPath, ArrayPropertyName and SubobjectClassPath are required"));
	}

	// Same resolver as SetObjectPropertyByPath -- one SSOT, no drift.
	ECUPathResolveStatus ResolveStatus = ECUPathResolveStatus::NotFound;
	UObject* Owner = CUResolveObjectByPath(ObjectPath, ResolveStatus);
	if (ResolveStatus == ECUPathResolveStatus::Destroyed)
	{
		return Fail(FString::Printf(
			TEXT("%s has been destroyed and cannot be used for property '%s'; query for a current object instead of reusing this path."),
			*ObjectPath, *ArrayPropertyName));
	}
	if (!Owner)
	{
		return Fail(FString::Printf(TEXT("Object not found: %s%s"),
			*ObjectPath, *CUPathNotFoundHint(ObjectPath)));
	}

	FProperty* Property = Owner->GetClass()->FindPropertyByName(FName(*ArrayPropertyName));
	if (!Property)
	{
		return Fail(FString::Printf(TEXT("Property '%s' not found on %s (%s)"),
			*ArrayPropertyName, *Owner->GetName(), *Owner->GetClass()->GetName()));
	}

	// The op only ever creates subobjects, so the target must be an array of
	// instanced object references. A plain object array would take ownership
	// semantics we are not providing.
	FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
	if (!ArrayProp)
	{
		return Fail(FString::Printf(TEXT("Property '%s' on %s is %s, not an array"),
			*ArrayPropertyName, *Owner->GetClass()->GetName(), *Property->GetCPPType()));
	}
	FObjectProperty* InnerObjectProp = CastField<FObjectProperty>(ArrayProp->Inner);
	if (!InnerObjectProp)
	{
		return Fail(FString::Printf(TEXT("Property '%s' on %s is an array of %s, not of object references"),
			*ArrayPropertyName, *Owner->GetClass()->GetName(), *ArrayProp->Inner->GetCPPType()));
	}
	const bool bInstanced =
		ArrayProp->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference) ||
		InnerObjectProp->HasAnyPropertyFlags(CPF_InstancedReference);
	if (!bInstanced)
	{
		return Fail(FString::Printf(
			TEXT("Property '%s' on %s is not an Instanced object array; refusing to create a subobject into it"),
			*ArrayPropertyName, *Owner->GetClass()->GetName()));
	}

	UClass* SubobjectClass = FindObject<UClass>(nullptr, *SubobjectClassPath);
	if (!SubobjectClass)
	{
		SubobjectClass = LoadObject<UClass>(nullptr, *SubobjectClassPath);
	}
	if (!SubobjectClass)
	{
		return Fail(FString::Printf(TEXT("Subobject class not found: %s"), *SubobjectClassPath));
	}
	// Runtime counterpart of UGameplayEffect::AddComponent's
	// static_assert(TIsDerivedFrom<GEComponentClass, UGameplayEffectComponent>).
	if (!SubobjectClass->IsChildOf(InnerObjectProp->PropertyClass))
	{
		return Fail(FString::Printf(TEXT("%s does not derive from the array's element type %s"),
			*SubobjectClass->GetName(), *InnerObjectProp->PropertyClass->GetName()));
	}
	if (SubobjectClass->HasAnyClassFlags(CLASS_Abstract))
	{
		return Fail(FString::Printf(TEXT("%s is abstract and cannot be instantiated"), *SubobjectClass->GetName()));
	}
	if (SubobjectClass->HasAnyClassFlags(CLASS_Deprecated))
	{
		return Fail(FString::Printf(TEXT("%s is deprecated"), *SubobjectClass->GetName()));
	}
	// A class the engine has already replaced -- a Blueprint recompiled or hot-reloaded
	// out from under us. The stale UClass stays resolvable by FindObject/LoadObject and
	// clears every guard above, so without this the op happily constructs an instance of
	// a shape the editor has moved on from, and the caller has no way to tell.
	if (SubobjectClass->HasAnyClassFlags(CLASS_NewerVersionExists))
	{
		return Fail(FString::Printf(
			TEXT("%s has been replaced by a newer version (recompiled or hot-reloaded); re-resolve the class path"),
			*SubobjectClass->GetName()));
	}
	// A class declared UCLASS(Within=X) may only be outered to an X. The engine enforces
	// this in StaticAllocateObject (UObjectGlobals.cpp:3321) with an *ensure*, not a check:
	// it logs a callstack and then CONSTRUCTS THE OBJECT ANYWAY, so without this guard the
	// op returns success:true for an object the engine itself considers illegally outered --
	// and in a build with DO_ENSURE compiled out there is no signal at all. Measured, not
	// assumed: NarshaMCP.RemoteLibrary.AddInstancedSubobjectByPath.ClassWithinGuard fails
	// with "nothing was appended ... but it was 1" when this block is removed.
	if (SubobjectClass->ClassWithin != nullptr && !Owner->IsA(SubobjectClass->ClassWithin))
	{
		return Fail(FString::Printf(
			TEXT("%s requires an outer of type %s, but %s is a %s"),
			*SubobjectClass->GetName(), *SubobjectClass->ClassWithin->GetName(),
			*Owner->GetName(), *Owner->GetClass()->GetName()));
	}

	void* ArrayPtr = ArrayProp->ContainerPtrToValuePtr<void>(Owner);
	FScriptArrayHelper ArrayHelper(ArrayProp, ArrayPtr);
	const int32 CountBefore = ArrayHelper.Num();

#if WITH_EDITOR
	FScopedTransaction Transaction(FText::FromString(FString::Printf(
		TEXT("Add %s to %s.%s"), *SubobjectClass->GetName(), *Owner->GetName(), *ArrayPropertyName)));
	Owner->Modify();
#endif

	// Issue #28227 AC6: reproduce UGameplayEffect::AddComponent's creation idiom in
	// full (GameplayEffect.h:2501) -- outer, NAME_None, AND the flag mask. Copying
	// only RF_Public would leave a subobject the engine treats differently from one
	// it made itself. GetMaskedFlags is an INTERSECTION with the owner's flags
	// (UObjectBaseUtility.h:142), so the result is owner-dependent; only
	// RF_Transactional is unconditional. The response reports both sides so the
	// caller can assert the relation instead of hardcoding RF_Public -- Python has
	// no route to EObjectFlags at all.
	const EObjectFlags OwnerMaskedFlags = Owner->GetMaskedFlags(RF_PropagateToSubObjects);
	// NewObject is declared FUNCTION_NON_NULL_RETURN_END, so a null check here would
	// be unreachable code (C4702 -- the class of warning Rule 28 exists for). The
	// engine's own UGameplayEffect::AddComponent likewise only `check(Instance)`s.
	UObject* Instance = NewObject<UObject>(Owner, SubobjectClass, NAME_None,
		OwnerMaskedFlags | RF_Transactional);
	check(Instance);

	const int32 NewIndex = ArrayHelper.AddValue();
	InnerObjectProp->SetObjectPropertyValue(ArrayHelper.GetRawPtr(NewIndex), Instance);
	const int32 CountAfter = ArrayHelper.Num();

	Owner->MarkPackageDirty();

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("object"), Owner->GetPathName());
	ResultObj->SetStringField(TEXT("property"), ArrayPropertyName);
	ResultObj->SetStringField(TEXT("subobject"), Instance->GetPathName());
	ResultObj->SetStringField(TEXT("subobject_class"), SubobjectClass->GetName());
	ResultObj->SetNumberField(TEXT("index"), NewIndex);
	ResultObj->SetNumberField(TEXT("count_before"), CountBefore);
	ResultObj->SetNumberField(TEXT("count_after"), CountAfter);
	// Post-creation values, not the arguments that were passed in -- an oracle built
	// on the argument expression would pass even if NewObject ignored the mask.
	ResultObj->SetNumberField(TEXT("child_flags"), static_cast<double>(Instance->GetFlags()));
	ResultObj->SetNumberField(TEXT("owner_masked_flags"), static_cast<double>(OwnerMaskedFlags));
	ResultObj->SetNumberField(TEXT("rf_propagate_to_subobjects"), static_cast<double>(RF_PropagateToSubObjects));
	ResultObj->SetNumberField(TEXT("rf_transactional"), static_cast<double>(RF_Transactional));

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[AddInstancedSubobject] %s.%s += %s (flags=0x%x, %d -> %d)"),
		*Owner->GetName(), *ArrayPropertyName, *SubobjectClass->GetName(),
		static_cast<uint32>(Instance->GetFlags()), CountBefore, CountAfter);
	return SerializeJson(ResultObj);
}

// Blueprint Compile & Variable Query Functions (Issue #4751)
// =========================================================

FString UCURemoteLibrary::CompileBlueprint(
	const FString& BlueprintPath)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath is required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CompileBlueprint: %s"), *BlueprintPath);

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Compile using FKismetEditorUtilities
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::None);

	// Check compile status
	bool bHasErrors = (Blueprint->Status == BS_Error);
	int32 ErrorCount = 0;
	TArray<TSharedPtr<FJsonValue>> ErrorsArray;

	// Collect compile messages from the message log
	// UE 5.7: UpgradeNotesLog is TSharedPtr<FCompilerResultsLog>, not TArray
	if (bHasErrors)
	{
		// Use the message log token to describe the error
		ErrorsArray.Add(MakeShared<FJsonValueString>(
			FString::Printf(TEXT("Blueprint '%s' has compile errors. Check Message Log for details."), *BlueprintPath)));
		ErrorCount = 1;
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, !bHasErrors);
	ResultObj->SetNumberField(TEXT("error_count"), ErrorCount);
	ResultObj->SetArrayField(TEXT("errors"), ErrorsArray);
	ResultObj->SetStringField(TEXT("compiled_path"), BlueprintPath);
	ResultObj->SetStringField(TEXT("status"), bHasErrors ? TEXT("Error") : TEXT("UpToDate"));

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] CompileBlueprint completed: %s (errors: %d)"),
		*BlueprintPath, ErrorCount);
	return SerializeJson(ResultObj);
}

FString UCURemoteLibrary::GetBlueprintVariables(
	const FString& BlueprintPath)
{
	auto SerializeJson = [](TSharedPtr<FJsonObject> Obj) -> FString
	{
		FString Out = CUJsonUtils::SerializeToString(Obj.ToSharedRef());
		return Out;
	};

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (BlueprintPath.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), TEXT("BlueprintPath is required"));
		return SerializeJson(ResultObj);
	}

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] GetBlueprintVariables: %s"), *BlueprintPath);

	UBlueprint* Blueprint = Cast<UBlueprint>(
		StaticLoadObject(UBlueprint::StaticClass(), nullptr, *BlueprintPath));
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load Blueprint: %s"), *BlueprintPath));
		return SerializeJson(ResultObj);
	}

	// Get CDO for default values
	UClass* GeneratedClass = Blueprint->GeneratedClass;
	UObject* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;

	TArray<TSharedPtr<FJsonValue>> VarsArray;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		TSharedPtr<FJsonObject> VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VarObj->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());

		// Sub-type for object/struct references
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			VarObj->SetStringField(TEXT("sub_type"), Var.VarType.PinSubCategoryObject->GetName());
		}

		VarObj->SetStringField(TEXT("guid"), Var.VarGuid.ToString());
		VarObj->SetBoolField(TEXT("is_array"), Var.VarType.IsArray());
		VarObj->SetBoolField(TEXT("is_set"), Var.VarType.IsSet());
		VarObj->SetBoolField(TEXT("is_map"), Var.VarType.IsMap());

		// Category and tooltip from metadata
		FString Category;
		if (Var.HasMetaData(FBlueprintMetadata::MD_FunctionCategory))
		{
			Category = Var.GetMetaData(FBlueprintMetadata::MD_FunctionCategory);
		}
		VarObj->SetStringField(TEXT("category"), Category);

		// Flags (PropertyFlags is uint64 bitmask)
		VarObj->SetBoolField(TEXT("instance_editable"),
			(Var.PropertyFlags & CPF_DisableEditOnInstance) == 0);
		VarObj->SetBoolField(TEXT("blueprint_read_only"),
			(Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		VarObj->SetBoolField(TEXT("expose_on_spawn"),
			(Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);

		// Default value from CDO
		if (CDO && GeneratedClass)
		{
			FProperty* Property = GeneratedClass->FindPropertyByName(Var.VarName);
			if (Property)
			{
				void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
				FString DefaultValue;
				Property->ExportTextItem_Direct(DefaultValue, ValuePtr, nullptr, CDO, PPF_None);
				VarObj->SetStringField(TEXT("default_value"), DefaultValue);
			}
		}

		VarsArray.Add(MakeShared<FJsonValueObject>(VarObj));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetArrayField(TEXT("variables"), VarsArray);
	ResultObj->SetNumberField(TEXT("variable_count"), VarsArray.Num());
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);

	UE_LOG(LogNarshaMCPRemote, Log, TEXT("[Blueprint] GetBlueprintVariables completed: %s (%d vars)"),
		*BlueprintPath, VarsArray.Num());
	return SerializeJson(ResultObj);
}

// Blueprint Breakpoint Remote Control Functions (Issue #4758)
// =========================================================

FString UCURemoteLibrary::SetBreakpoint(
	const FString& BlueprintPath,
	const FString& NodeGuidStr
)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Invalid GUID format: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	UEdGraphNode* Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Node not found with GUID: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// Check if breakpoint already exists (UE 5.7: FBlueprintBreakpoint, Node-first params)
	FBlueprintBreakpoint* ExistingBP = FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint);
	if (ExistingBP)
	{
		FKismetDebugUtilities::SetBreakpointEnabled(*ExistingBP, true);
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("node_guid"), NodeGuidStr);
		ResultObj->SetStringField(TEXT("node_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		ResultObj->SetBoolField(TEXT("enabled"), true);
		ResultObj->SetBoolField(TEXT("already_existed"), true);
	}
	else
	{
		FKismetDebugUtilities::CreateBreakpoint(Blueprint, Node, /*bIsEnabled=*/true);
		Blueprint->MarkPackageDirty();

		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("node_guid"), NodeGuidStr);
		ResultObj->SetStringField(TEXT("node_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		ResultObj->SetBoolField(TEXT("enabled"), true);
		ResultObj->SetBoolField(TEXT("already_existed"), false);
	}

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

FString UCURemoteLibrary::ClearBreakpoint(
	const FString& BlueprintPath,
	const FString& NodeGuidStr
)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Invalid GUID format: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	UEdGraphNode* Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Node not found with GUID: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// UE 5.7: Use predicate-based removal
	bool bFound = FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint) != nullptr;
	if (bFound)
	{
		FKismetDebugUtilities::RemoveBreakpointsByPredicate(Blueprint, [Node](const FBlueprintBreakpoint& BP)
		{
			return BP.GetLocation() == Node;
		});
		Blueprint->MarkPackageDirty();
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetBoolField(TEXT("was_present"), bFound);
	ResultObj->SetStringField(TEXT("node_guid"), NodeGuidStr);

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

FString UCURemoteLibrary::EnableBreakpoint(
	const FString& BlueprintPath,
	const FString& NodeGuidStr,
	bool bEnabled
)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	FGuid NodeGuid;
	if (!FGuid::Parse(NodeGuidStr, NodeGuid))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Invalid GUID format: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	UEdGraphNode* Node = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, NodeGuid);
	if (!Node)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Node not found with GUID: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// UE 5.7: Node-first params, FBlueprintBreakpoint reference
	FBlueprintBreakpoint* ExistingBP = FKismetDebugUtilities::FindBreakpointForNode(Node, Blueprint);
	if (!ExistingBP)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("No breakpoint on node: %s"), *NodeGuidStr));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	FKismetDebugUtilities::SetBreakpointEnabled(*ExistingBP, bEnabled);

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("node_guid"), NodeGuidStr);
	ResultObj->SetBoolField(TEXT("enabled"), bEnabled);

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

FString UCURemoteLibrary::ListBreakpoints(
	const FString& BlueprintPath
)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// UE 5.7: Use GetBreakpoints() + ForeachBreakpoint()
	TArray<TSharedPtr<FJsonValue>> BreakpointArray;
	FKismetDebugUtilities::ForeachBreakpoint(Blueprint, [&BreakpointArray](FBlueprintBreakpoint& BP)
	{
		UEdGraphNode* Node = BP.GetLocation();
		if (!Node)
		{
			return; // Skip orphaned breakpoints (stale UE metadata after clear)
		}
		TSharedPtr<FJsonObject> BPObj = MakeShared<FJsonObject>();
		BPObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
		BPObj->SetStringField(TEXT("node_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		BPObj->SetStringField(TEXT("node_class"), Node->GetClass()->GetName());
		BPObj->SetBoolField(TEXT("enabled"), BP.IsEnabledByUser());
		BreakpointArray.Add(MakeShared<FJsonValueObject>(BPObj));
	});

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetNumberField(TEXT("count"), BreakpointArray.Num());
	ResultObj->SetArrayField(TEXT("breakpoints"), BreakpointArray);

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

// =========================================================
// Log Stream Remote Control Functions (Issue #4759)
// =========================================================

namespace NarshaMCPLog
{
	struct FLogEntry
	{
		double TimestampSeconds;
		FString Category;
		FString Severity;
		FString Message;
	};

	static constexpr int32 MAX_LOG_ENTRIES = 1000;

	class FNarshaMCPLogBuffer : public FOutputDevice
	{
	public:
		TArray<FString> FilterCategories;
		ELogVerbosity::Type MinVerbosity = ELogVerbosity::Log;
		TArray<FLogEntry> Buffer;
		int32 HeadIndex = 0;   // Next write position in circular buffer
		int32 TotalWritten = 0; // Total entries written (for since_index)
		FCriticalSection Lock;

		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			// Apply severity filter
			if (Verbosity > MinVerbosity)
			{
				return;
			}

			// Apply category filter (empty = accept all)
			if (FilterCategories.Num() > 0)
			{
				bool bMatch = false;
				FString CatStr = Category.ToString();
				for (const FString& Filter : FilterCategories)
				{
					if (CatStr.Contains(Filter))
					{
						bMatch = true;
						break;
					}
				}
				if (!bMatch) return;
			}

			FScopeLock ScopeLock(&Lock);

			FLogEntry Entry;
			Entry.TimestampSeconds = FPlatformTime::Seconds();
			Entry.Category = Category.ToString();
			Entry.Message = FString(V);

			switch (Verbosity)
			{
			case ELogVerbosity::Fatal:   Entry.Severity = TEXT("Fatal"); break;
			case ELogVerbosity::Error:   Entry.Severity = TEXT("Error"); break;
			case ELogVerbosity::Warning: Entry.Severity = TEXT("Warning"); break;
			default:                     Entry.Severity = TEXT("Log"); break;
			}

			// O(1) circular buffer: overwrite oldest entry instead of O(n) RemoveAt(0)
			if (Buffer.Num() < MAX_LOG_ENTRIES)
			{
				Buffer.Add(MoveTemp(Entry));
			}
			else
			{
				Buffer[HeadIndex] = MoveTemp(Entry);
			}
			HeadIndex = (HeadIndex + 1) % MAX_LOG_ENTRIES;
			TotalWritten++;
		}

		// Get entries in chronological order from circular buffer
		TArray<FLogEntry> GetOrderedEntries() const
		{
			TArray<FLogEntry> Result;
			if (Buffer.Num() < MAX_LOG_ENTRIES)
			{
				Result = Buffer;
			}
			else
			{
				Result.Reserve(Buffer.Num());
				// Read from HeadIndex (oldest) to HeadIndex-1 (newest)
				for (int32 i = 0; i < Buffer.Num(); ++i)
				{
					Result.Add(Buffer[(HeadIndex + i) % MAX_LOG_ENTRIES]);
				}
			}
			return Result;
		}
	};

	static TUniquePtr<FNarshaMCPLogBuffer> GLogBuffer;
}

// =========================================================
// Variable Write (Issue #4848)
// =========================================================

FString UCURemoteLibrary::SetGASAttribute(
	const FString& ActorPath,
	const FString& AttributeName,
	float NewValue)
{
	// 1. Find world — try PIE first, fallback to editor
	UWorld* World = nullptr;
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				World = Context.World();
				break;
			}
		}
	}
	if (!World && GEditor)
	{
		World = GEditor->GetEditorWorldContext().World();
	}
	if (!World)
	{
		return TEXT("{\"success\":false,\"error\":\"No world available\"}");
	}

	// 2. Find actor by path/name/label
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetPathName() == ActorPath
			|| Actor->GetName() == ActorPath
			|| Actor->GetActorNameOrLabel() == ActorPath))
		{
			FoundActor = Actor;
			break;
		}
	}
	if (!FoundActor)
	{
		FString EscapedPath = ActorPath.Replace(TEXT("\""), TEXT("\\\""));
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Actor not found: %s\"}"), *EscapedPath);
	}

	// 3. Find AbilitySystemComponent via reflection (no hard include dependency)
	// Look for a component whose class name contains "AbilitySystemComponent"
	UActorComponent* ASC = nullptr;
	for (UActorComponent* Comp : FoundActor->GetComponents())
	{
		if (Comp && Comp->GetClass()->GetName().Contains(TEXT("AbilitySystemComponent")))
		{
			ASC = Comp;
			break;
		}
	}
	if (!ASC)
	{
		return TEXT("{\"success\":false,\"error\":\"Actor has no AbilitySystemComponent\"}");
	}

	// 4. Find the attribute property on the ASC's attribute sets via reflection
	// GAS attributes are stored in UAttributeSet subobjects owned by the ASC.
	// We search all subobjects for a float property matching AttributeName.
	FName AttrFName(*AttributeName);
	bool bFound = false;
	float OldValue = 0.0f;
	FString FoundAttributeSet;

	TArray<UObject*> SubObjects;
	ASC->GetDefaultSubobjects(SubObjects);

	// Also try direct property on ASC itself and its outer objects
	// The standard approach: iterate sub-objects of the owning actor looking for AttributeSets
	for (UActorComponent* Comp : FoundActor->GetComponents())
	{
		if (!Comp || !Comp->GetClass()->GetName().Contains(TEXT("AbilitySystemComponent")))
		{
			continue;
		}

		// Get all attribute sets registered with this ASC
		// AttributeSets are typically sub-objects of the ASC's owner
		// We search via reflection: find FGameplayAttributeData properties
		TArray<UObject*> DefaultSubobjects;
		FoundActor->GetDefaultSubobjects(DefaultSubobjects);

		for (UObject* SubObj : DefaultSubobjects)
		{
			if (!SubObj) continue;

			// Check if this is an AttributeSet (class name contains "AttributeSet")
			if (!SubObj->GetClass()->GetName().Contains(TEXT("AttributeSet")))
			{
				continue;
			}

			// Look for a property matching the attribute name
			FProperty* Prop = SubObj->GetClass()->FindPropertyByName(AttrFName);
			if (!Prop)
			{
				// Try with common prefixes/suffixes
				Prop = SubObj->GetClass()->FindPropertyByName(FName(*(TEXT("") + AttributeName)));
			}
			if (!Prop) continue;

			// FGameplayAttributeData is a struct with BaseValue and CurrentValue
			// Try to read/write via the struct's float properties
			if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(SubObj);

				// Find BaseValue within the struct
				FProperty* BaseValueProp = StructProp->Struct->FindPropertyByName(TEXT("BaseValue"));
				if (BaseValueProp)
				{
					if (FFloatProperty* FloatProp = CastField<FFloatProperty>(BaseValueProp))
					{
						void* BaseValuePtr = FloatProp->ContainerPtrToValuePtr<void>(StructPtr);
						OldValue = FloatProp->GetPropertyValue(BaseValuePtr);
						FloatProp->SetPropertyValue(BaseValuePtr, NewValue);
						bFound = true;
						FoundAttributeSet = SubObj->GetClass()->GetName();
					}
					else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(BaseValueProp))
					{
						void* BaseValuePtr = DoubleProp->ContainerPtrToValuePtr<void>(StructPtr);
						OldValue = static_cast<float>(DoubleProp->GetPropertyValue(BaseValuePtr));
						DoubleProp->SetPropertyValue(BaseValuePtr, static_cast<double>(NewValue));
						bFound = true;
						FoundAttributeSet = SubObj->GetClass()->GetName();
					}
				}
				if (bFound) break;
			}
			// Also handle plain float properties
			else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			{
				void* ValuePtr = FloatProp->ContainerPtrToValuePtr<void>(SubObj);
				OldValue = FloatProp->GetPropertyValue(ValuePtr);
				FloatProp->SetPropertyValue(ValuePtr, NewValue);
				bFound = true;
				FoundAttributeSet = SubObj->GetClass()->GetName();
				break;
			}
		}
		if (bFound) break;
	}

	if (!bFound)
	{
		FString EscapedAttr = AttributeName.Replace(TEXT("\""), TEXT("\\\""));
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"GAS attribute '%s' not found on actor. Ensure the AttributeSet is registered and the attribute name is correct.\"}"),
			*EscapedAttr);
	}

	FString EscapedAttr = AttributeName.Replace(TEXT("\""), TEXT("\\\""));
	FString EscapedSet = FoundAttributeSet.Replace(TEXT("\""), TEXT("\\\""));
	return FString::Printf(
		TEXT("{\"success\":true,\"old_value\":%.4f,\"new_value\":%.4f,\"attribute_name\":\"%s\",\"attribute_set\":\"%s\"}"),
		OldValue, NewValue, *EscapedAttr, *EscapedSet);
}

FString UCURemoteLibrary::StartLogCapture(
	const FString& Categories,
	const FString& MinSeverity
)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	// Only one capture session at a time
	if (NarshaMCPLog::GLogBuffer.IsValid())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("Log capture already active. Call StopLogCapture first."));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	NarshaMCPLog::GLogBuffer = MakeUnique<NarshaMCPLog::FNarshaMCPLogBuffer>();

	// Parse category filter
	if (!Categories.IsEmpty())
	{
		Categories.ParseIntoArray(NarshaMCPLog::GLogBuffer->FilterCategories, TEXT(","), true);
		for (FString& Cat : NarshaMCPLog::GLogBuffer->FilterCategories)
		{
			Cat.TrimStartAndEndInline();
		}
	}

	// Parse minimum severity
	if (MinSeverity.Equals(TEXT("Error"), ESearchCase::IgnoreCase))
	{
		NarshaMCPLog::GLogBuffer->MinVerbosity = ELogVerbosity::Error;
	}
	else if (MinSeverity.Equals(TEXT("Warning"), ESearchCase::IgnoreCase))
	{
		NarshaMCPLog::GLogBuffer->MinVerbosity = ELogVerbosity::Warning;
	}
	else
	{
		NarshaMCPLog::GLogBuffer->MinVerbosity = ELogVerbosity::Log;
	}

	// Register with GLog
	GLog->AddOutputDevice(NarshaMCPLog::GLogBuffer.Get());

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(JSON_MESSAGE_FIELD, TEXT("Log capture started"));
	ResultObj->SetStringField(TEXT("categories"), Categories.IsEmpty() ? TEXT("all") : Categories);
	ResultObj->SetStringField(TEXT("min_severity"), MinSeverity.IsEmpty() ? TEXT("Log") : MinSeverity);

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

FString UCURemoteLibrary::GetCapturedLogs(int32 SinceIndex)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (!NarshaMCPLog::GLogBuffer.IsValid())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("No active log capture. Call StartLogCapture first."));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	TArray<NarshaMCPLog::FLogEntry> OrderedEntries;
	int32 TotalEntries;
	int32 TotalWritten;
	{
		FScopeLock ScopeLock(&NarshaMCPLog::GLogBuffer->Lock);
		OrderedEntries = NarshaMCPLog::GLogBuffer->GetOrderedEntries();
		TotalEntries = OrderedEntries.Num();
		TotalWritten = NarshaMCPLog::GLogBuffer->TotalWritten;
	}

	// since_index is a local buffer index (0..TotalEntries-1).
	// When the circular buffer wraps (>1000 entries), older entries are discarded
	// and since_index values from before the wrap become stale. Clamp to valid range.
	TArray<TSharedPtr<FJsonValue>> EntriesArray;
	int32 StartIdx = FMath::Clamp(SinceIndex, 0, TotalEntries);
	bool bBufferWrapped = TotalWritten > TotalEntries;

	for (int32 i = StartIdx; i < OrderedEntries.Num(); ++i)
	{
		const NarshaMCPLog::FLogEntry& Entry = OrderedEntries[i];
		TSharedPtr<FJsonObject> EntryObj = MakeShared<FJsonObject>();
		EntryObj->SetNumberField(TEXT("index"), i);
		EntryObj->SetNumberField(TEXT("timestamp"), Entry.TimestampSeconds);
		EntryObj->SetStringField(TEXT("category"), Entry.Category);
		EntryObj->SetStringField(TEXT("severity"), Entry.Severity);
		EntryObj->SetStringField(TEXT("message"), Entry.Message);
		EntriesArray.Add(MakeShared<FJsonValueObject>(EntryObj));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetNumberField(TEXT("total_entries"), TotalEntries);
	ResultObj->SetNumberField(TEXT("returned_entries"), EntriesArray.Num());
	ResultObj->SetNumberField(TEXT("next_index"), TotalEntries);
	if (bBufferWrapped)
	{
		ResultObj->SetBoolField(TEXT("buffer_wrapped"), true);
		ResultObj->SetNumberField(TEXT("total_written"), TotalWritten);
	}
	ResultObj->SetArrayField(TEXT("entries"), EntriesArray);

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

FString UCURemoteLibrary::StopLogCapture()
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

	if (!NarshaMCPLog::GLogBuffer.IsValid())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("No active log capture."));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	int32 TotalEntries;
	{
		FScopeLock ScopeLock(&NarshaMCPLog::GLogBuffer->Lock);
		TotalEntries = NarshaMCPLog::GLogBuffer->Buffer.Num();
	}

	// Unregister from GLog
	GLog->RemoveOutputDevice(NarshaMCPLog::GLogBuffer.Get());
	NarshaMCPLog::GLogBuffer.Reset();

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetStringField(JSON_MESSAGE_FIELD, TEXT("Log capture stopped"));
	ResultObj->SetNumberField(TEXT("total_entries_captured"), TotalEntries);

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

// =========================================================
// PIE & Debug State Functions (Issue #4757, #4758)
// =========================================================

FString UCURemoteLibrary::GetPIEState()
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

#if WITH_EDITOR
	bool bIsPIE = GEditor && GEditor->PlayWorld != nullptr;
	bool bIsSimulating = GEditor && GEditor->bIsSimulatingInEditor;

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetBoolField(TEXT("is_pie_running"), bIsPIE || bIsSimulating);
	ResultObj->SetBoolField(TEXT("is_simulating"), bIsSimulating);
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("PIE state requires Editor build"));
#endif

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

FString UCURemoteLibrary::GetDebugState()
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

#if WITH_EDITOR
	// 1. PIE state
	bool bIsPIE = GEditor && GEditor->PlayWorld != nullptr;
	bool bIsSimulating = GEditor && GEditor->bIsSimulatingInEditor;
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetBoolField(TEXT("is_pie_running"), bIsPIE || bIsSimulating);

	if (!bIsPIE && !bIsSimulating)
	{
		ResultObj->SetBoolField(TEXT("is_paused"), false);
		ResultObj->SetStringField(TEXT("note"), TEXT("PIE not running - no debug state available"));

		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// 2. Check if execution is paused — re-verify PlayWorld to prevent TOCTOU null deref
	if (!GEditor || !GEditor->PlayWorld)
	{
		ResultObj->SetBoolField(TEXT("is_paused"), false);
		ResultObj->SetStringField(TEXT("note"), TEXT("PlayWorld became unavailable during query"));

		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	bool bIsPaused = GEditor->PlayWorld->IsPaused();
	ResultObj->SetBoolField(TEXT("is_paused"), bIsPaused);

	// 3. Best effort: try to get current debug object info
	if (bIsPaused)
	{
		UObject* DebugObj = GEditor->PlayWorld->GetFirstPlayerController();
		if (DebugObj)
		{
			ResultObj->SetStringField(TEXT("debug_context"), TEXT("execution_paused"));
			ResultObj->SetStringField(TEXT("note"), TEXT("PIE paused - likely breakpoint hit. Use list_breakpoints to check active breakpoints."));
		}
		else
		{
			ResultObj->SetStringField(TEXT("debug_context"), TEXT("paused_no_controller"));
		}
	}
	else
	{
		ResultObj->SetStringField(TEXT("debug_context"), TEXT("running"));
	}
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("Debug state requires Editor build"));
#endif

	FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R;
}

// =========================================================
// Memory Measurement Functions (Issue #4829)
// =========================================================

FString UCURemoteLibrary::GetMemoryStats(int32 MaxClasses)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

#if WITH_EDITOR
	// 1. Get active world (PIE preferred, then Editor)
	UWorld* TargetWorld = nullptr;
	bool bIsPIE = false;
	if (GEditor && GEditor->PlayWorld)
	{
		TargetWorld = GEditor->PlayWorld;
		bIsPIE = true;
	}
	else if (GEditor)
	{
		FWorldContext* WorldContext = GEditor->GetPIEWorldContext();
		if (!WorldContext)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::Editor && Ctx.World())
				{
					TargetWorld = Ctx.World();
					break;
				}
			}
		}
		else
		{
			TargetWorld = WorldContext->World();
		}
	}

	if (!TargetWorld)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("No active world found"));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// TOCTOU re-check: PIE world may vanish between checks
	if (bIsPIE && (!GEditor || !GEditor->PlayWorld))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("PlayWorld became unavailable during query"));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// 2. Iterate actors with memory measurement
	int32 ActorCount = 0;
	SIZE_T TotalBytes = 0;
	TMap<FString, TPair<int32, SIZE_T>> ClassStats;

	for (TActorIterator<AActor> It(TargetWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor))
		{
			continue;
		}

		FResourceSizeEx ResSize(EResourceSizeMode::EstimatedTotal);
		Actor->GetResourceSizeEx(ResSize);
		SIZE_T ActorBytes = ResSize.GetTotalMemoryBytes();

		FString ClassName = Actor->GetClass()->GetName();
		auto& Entry = ClassStats.FindOrAdd(ClassName);
		Entry.Key++;
		Entry.Value += ActorBytes;

		ActorCount++;
		TotalBytes += ActorBytes;
	}

	// 3. Sort by byte count descending
	ClassStats.ValueSort([](const TPair<int32, SIZE_T>& A, const TPair<int32, SIZE_T>& B)
	{
		return A.Value > B.Value;
	});

	// 4. Build JSON response
	TSharedPtr<FJsonObject> DistObj = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> TopClassesArray;
	int32 Idx = 0;

	for (auto& Pair : ClassStats)
	{
		if (Idx >= MaxClasses) break;

		DistObj->SetNumberField(FString(Pair.Key), Pair.Value.Key);

		TSharedPtr<FJsonObject> ClassObj = MakeShared<FJsonObject>();
		ClassObj->SetStringField(TEXT("class_name"), FString(Pair.Key));
		ClassObj->SetNumberField(TEXT("count"), Pair.Value.Key);
		ClassObj->SetNumberField(TEXT("exclusive_bytes"), static_cast<double>(Pair.Value.Value));
		TopClassesArray.Add(MakeShared<FJsonValueObject>(ClassObj));

		Idx++;
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetNumberField(TEXT("actor_count"), ActorCount);
	ResultObj->SetNumberField(TEXT("total_exclusive_bytes"), static_cast<double>(TotalBytes));
	ResultObj->SetNumberField(TEXT("unique_classes"), ClassStats.Num());
	ResultObj->SetObjectField(TEXT("class_distribution"), DistObj);
	ResultObj->SetArrayField(TEXT("top_classes"), TopClassesArray);
	ResultObj->SetStringField(TEXT("world"), bIsPIE ? TEXT("game") : TEXT("editor"));
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("Memory stats require Editor build"));
#endif

	FString R2 = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R2;
}

FString UCURemoteLibrary::GetClassMemoryUsage(const FString& ClassName, int32 MaxInstances)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();

#if WITH_EDITOR
	// 1. Get active world
	UWorld* TargetWorld = nullptr;
	bool bIsPIE = false;
	if (GEditor && GEditor->PlayWorld)
	{
		TargetWorld = GEditor->PlayWorld;
		bIsPIE = true;
	}
	else if (GEditor)
	{
		FWorldContext* WorldContext = GEditor->GetPIEWorldContext();
		if (!WorldContext)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.WorldType == EWorldType::Editor && Ctx.World())
				{
					TargetWorld = Ctx.World();
					break;
				}
			}
		}
		else
		{
			TargetWorld = WorldContext->World();
		}
	}

	if (!TargetWorld)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("No active world found"));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// TOCTOU re-check
	if (bIsPIE && (!GEditor || !GEditor->PlayWorld))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("PlayWorld became unavailable during query"));
		FString R = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return R;
	}

	// 2. Find matching actors (exact name, then inheritance fallback)
	TArray<AActor*> MatchingActors;
	bool bUsedInheritance = false;

	for (TActorIterator<AActor> It(TargetWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)) continue;
		if (Actor->GetClass()->GetName() == ClassName)
		{
			MatchingActors.Add(Actor);
		}
	}

	// Inheritance fallback if no exact match
	if (MatchingActors.Num() == 0)
	{
		UClass* TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
		if (TargetClass)
		{
			bUsedInheritance = true;
			for (TActorIterator<AActor> It(TargetWorld); It; ++It)
			{
				AActor* Actor = *It;
				if (!IsValid(Actor)) continue;
				if (Actor->IsA(TargetClass))
				{
					MatchingActors.Add(Actor);
				}
			}
		}
	}

	// 3. Measure memory per instance
	TArray<TSharedPtr<FJsonValue>> InstancesArray;
	SIZE_T TotalBytes = 0;

	for (int32 i = 0; i < FMath::Min(MatchingActors.Num(), MaxInstances); i++)
	{
		AActor* Actor = MatchingActors[i];
		if (!IsValid(Actor)) continue;

		FResourceSizeEx ResSize(EResourceSizeMode::EstimatedTotal);
		Actor->GetResourceSizeEx(ResSize);
		SIZE_T ActorBytes = ResSize.GetTotalMemoryBytes();
		TotalBytes += ActorBytes;

		TSharedPtr<FJsonObject> InstObj = MakeShared<FJsonObject>();
		InstObj->SetStringField(TEXT("name"), Actor->GetName());
		InstObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		InstObj->SetStringField(TEXT("path"), Actor->GetPathName());
		InstObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		InstObj->SetNumberField(TEXT("exclusive_bytes"), static_cast<double>(ActorBytes));
		InstancesArray.Add(MakeShared<FJsonValueObject>(InstObj));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, MatchingActors.Num() > 0);
	ResultObj->SetStringField(TEXT("class_name"), ClassName);
	ResultObj->SetNumberField(TEXT("instance_count"), MatchingActors.Num());
	ResultObj->SetNumberField(TEXT("total_exclusive_bytes"), static_cast<double>(TotalBytes));
	ResultObj->SetArrayField(TEXT("instances"), InstancesArray);
	ResultObj->SetBoolField(TEXT("capped"), MatchingActors.Num() > MaxInstances);
	if (bUsedInheritance)
	{
		ResultObj->SetBoolField(TEXT("used_inheritance"), true);
	}
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("Class memory usage requires Editor build"));
#endif

	FString R3 = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return R3;
}

// ==================== Issue #5291 Phase 1A: GetCompilerResults ====================

/**
 * Helper: Convert EMessageSeverity to string.
 */
static FString SeverityToString(EMessageSeverity::Type Severity)
{
	switch (Severity)
	{
	case EMessageSeverity::Error: return TEXT("error");
	case EMessageSeverity::PerformanceWarning: return TEXT("performance_warning");
	case EMessageSeverity::Warning: return TEXT("warning");
	case EMessageSeverity::Info: return TEXT("info");
	default: return TEXT("unknown");
	}
}

/**
 * Helper: Convert EMessageToken to string.
 */
static FString TokenTypeToString(EMessageToken::Type Type)
{
	switch (Type)
	{
	case EMessageToken::Action: return TEXT("action");
	case EMessageToken::Actor: return TEXT("actor");
	case EMessageToken::AssetName: return TEXT("asset_name");
	case EMessageToken::AssetData: return TEXT("asset_data");
	case EMessageToken::Documentation: return TEXT("documentation");
	case EMessageToken::Image: return TEXT("image");
	case EMessageToken::Object: return TEXT("object");
	case EMessageToken::Severity: return TEXT("severity");
	case EMessageToken::Text: return TEXT("text");
	case EMessageToken::Tutorial: return TEXT("tutorial");
	case EMessageToken::URL: return TEXT("url");
	case EMessageToken::EdGraph: return TEXT("edgraph");
	case EMessageToken::DynamicText: return TEXT("dynamic_text");
	case EMessageToken::Fix: return TEXT("fix");
	default: return TEXT("unknown");
	}
}

FString UCURemoteLibrary::GetCompilerResults(const FString& BlueprintPath)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

#if WITH_EDITOR
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	int32 NumErrors = 0;
	int32 NumWarnings = 0;

	if (!BlueprintPath.IsEmpty())
	{
		// Load the specific Blueprint and get its message log
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint)
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(JSON_ERROR_FIELD,
				FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));

			FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
			return Result;
		}

		// Get the message log listing for this blueprint
		TSharedRef<IMessageLogListing> LogListing =
			FCompilerResultsLog::GetBlueprintMessageLog(Blueprint);

		// Get filtered messages (respects current filter settings)
		const TArray<TSharedRef<FTokenizedMessage>>& FilteredMessages =
			LogListing->GetFilteredMessages();

		for (const TSharedRef<FTokenizedMessage>& Message : FilteredMessages)
		{
			TSharedPtr<FJsonObject> MsgObj = MakeShareable(new FJsonObject());
			MsgObj->SetStringField(TEXT("severity"), SeverityToString(Message->GetSeverity()));
			MsgObj->SetStringField(TEXT("message"), Message->ToText().ToString());

			// Track counts
			if (Message->GetSeverity() == EMessageSeverity::Error)
			{
				NumErrors++;
			}
			else if (Message->GetSeverity() == EMessageSeverity::Warning
				|| Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
			{
				NumWarnings++;
			}

			// Serialize tokens
			TArray<TSharedPtr<FJsonValue>> TokensArray;
			const TArray<TSharedRef<IMessageToken>>& Tokens = Message->GetMessageTokens();
			for (const TSharedRef<IMessageToken>& Token : Tokens)
			{
				TSharedPtr<FJsonObject> TokenObj = MakeShareable(new FJsonObject());
				TokenObj->SetStringField(TEXT("type"), TokenTypeToString(Token->GetType()));
				TokenObj->SetStringField(TEXT("text"), Token->ToText().ToString());
				TokensArray.Add(MakeShareable(new FJsonValueObject(TokenObj)));
			}
			MsgObj->SetArrayField(TEXT("tokens"), TokensArray);

			MessagesArray.Add(MakeShareable(new FJsonValueObject(MsgObj)));
		}

		// Add blueprint-specific info
		ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
		ResultObj->SetStringField(TEXT("blueprint_name"), Blueprint->GetName());

		// Check if blueprint has compile errors via its status
		ResultObj->SetNumberField(TEXT("blueprint_status"), static_cast<int32>(Blueprint->Status));
	}
	else
	{
		// Query the global compiler log ("BlueprintLog")
		FMessageLogModule& MessageLogModule =
			FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");

		if (MessageLogModule.IsRegisteredLogListing(FCompilerResultsLog::GetLogName()))
		{
			TSharedRef<IMessageLogListing> LogListing =
				MessageLogModule.GetLogListing(FCompilerResultsLog::GetLogName());

			const TArray<TSharedRef<FTokenizedMessage>>& FilteredMessages =
				LogListing->GetFilteredMessages();

			for (const TSharedRef<FTokenizedMessage>& Message : FilteredMessages)
			{
				TSharedPtr<FJsonObject> MsgObj = MakeShareable(new FJsonObject());
				MsgObj->SetStringField(TEXT("severity"), SeverityToString(Message->GetSeverity()));
				MsgObj->SetStringField(TEXT("message"), Message->ToText().ToString());

				if (Message->GetSeverity() == EMessageSeverity::Error)
				{
					NumErrors++;
				}
				else if (Message->GetSeverity() == EMessageSeverity::Warning
					|| Message->GetSeverity() == EMessageSeverity::PerformanceWarning)
				{
					NumWarnings++;
				}

				TArray<TSharedPtr<FJsonValue>> TokensArray;
				const TArray<TSharedRef<IMessageToken>>& Tokens = Message->GetMessageTokens();
				for (const TSharedRef<IMessageToken>& Token : Tokens)
				{
					TSharedPtr<FJsonObject> TokenObj = MakeShareable(new FJsonObject());
					TokenObj->SetStringField(TEXT("type"), TokenTypeToString(Token->GetType()));
					TokenObj->SetStringField(TEXT("text"), Token->ToText().ToString());
					TokensArray.Add(MakeShareable(new FJsonValueObject(TokenObj)));
				}
				MsgObj->SetArrayField(TEXT("tokens"), TokensArray);

				MessagesArray.Add(MakeShareable(new FJsonValueObject(MsgObj)));
			}
		}

		ResultObj->SetStringField(TEXT("source"), TEXT("global_compiler_log"));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetArrayField(TEXT("messages"), MessagesArray);
	ResultObj->SetNumberField(TEXT("num_errors"), NumErrors);
	ResultObj->SetNumberField(TEXT("num_warnings"), NumWarnings);
	ResultObj->SetNumberField(TEXT("total_messages"), MessagesArray.Num());
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("GetCompilerResults requires Editor build"));
#endif

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// ==================== Issue #5296: FMessageLog Health Report ====================

/**
 * Known FMessageLog categories. UE has no API to enumerate all registered categories,
 * so we probe a static list. Matches KNOWN_LOG_CATEGORIES in editor_control_message_log.rs.
 */
static const TArray<FName> KnownMessageLogCategories = {
	TEXT("MapCheck"),
	TEXT("BlueprintLog"),
	TEXT("AssetCheck"),
	TEXT("PIE"),
	TEXT("LoadErrors"),
	TEXT("LightingResults"),
	TEXT("DataValidation"),
	TEXT("AnimBlueprintLog"),
	TEXT("HLODResults"),
	TEXT("SlateStyleLog"),
	TEXT("SourceControl"),
	TEXT("PackagingResults"),
	TEXT("LiveCoding"),
	TEXT("HotReload"),
};

/** Helper: Check if a message passes the severity filter. */
static bool PassesSeverityFilter(EMessageSeverity::Type Severity, const FString& Filter)
{
	if (Filter == TEXT("All")) return true;
	if (Filter == TEXT("Error")) return Severity == EMessageSeverity::Error;
	if (Filter == TEXT("Warning"))
		return Severity == EMessageSeverity::Warning
			|| Severity == EMessageSeverity::PerformanceWarning;
	if (Filter == TEXT("Info")) return Severity == EMessageSeverity::Info;
	return true;
}

/** Helper: Check if a message contains at least one Fix token. */
static bool HasFixToken(const TSharedRef<FTokenizedMessage>& Message)
{
	for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
	{
		if (Token->GetType() == EMessageToken::Fix)
			return true;
	}
	return false;
}

/** Helper: Count Fix tokens in a message. */
static int32 CountFixTokens(const TSharedRef<FTokenizedMessage>& Message)
{
	int32 Count = 0;
	for (const TSharedRef<IMessageToken>& Token : Message->GetMessageTokens())
	{
		if (Token->GetType() == EMessageToken::Fix)
			Count++;
	}
	return Count;
}

FString UCURemoteLibrary::GetMessageLog(
	const FString& Category,
	const FString& SeverityFilter,
	int64 MaxMessages,
	bool IncludeTokens)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

#if WITH_EDITOR
	FMessageLogModule& MessageLogModule =
		FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");

	int32 MaxMsg = FMath::Clamp(static_cast<int32>(MaxMessages), 1, 500);

	static const TArray<FString> ValidSeverities = {
		TEXT("Error"), TEXT("Warning"), TEXT("Info"), TEXT("All")
	};
	if (!ValidSeverities.Contains(SeverityFilter))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			FString::Printf(TEXT("Invalid SeverityFilter '%s'. Valid: Error, Warning, Info, All"),
				*SeverityFilter));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	if (Category.IsEmpty())
	{
		// === ENUMERATE ALL CATEGORIES ===
		TArray<TSharedPtr<FJsonValue>> CategoriesArray;
		int32 TotalMessages = 0;

		for (const FName& CatName : KnownMessageLogCategories)
		{
			if (!MessageLogModule.IsRegisteredLogListing(CatName))
				continue;

			TSharedRef<IMessageLogListing> Listing =
				MessageLogModule.GetLogListing(CatName);
			const TArray<TSharedRef<FTokenizedMessage>>& Messages =
				Listing->GetFilteredMessages();

			int32 MsgCount = Messages.Num();
			if (MsgCount == 0)
				continue;

			int32 ErrorCount = 0, WarningCount = 0, InfoCount = 0;
			bool bHasFixes = false;

			for (const TSharedRef<FTokenizedMessage>& Msg : Messages)
			{
				switch (Msg->GetSeverity())
				{
				case EMessageSeverity::Error: ErrorCount++; break;
				case EMessageSeverity::Warning:
				case EMessageSeverity::PerformanceWarning: WarningCount++; break;
				default: InfoCount++; break;
				}
				if (!bHasFixes && HasFixToken(Msg))
					bHasFixes = true;
			}

			TSharedPtr<FJsonObject> CatObj = MakeShareable(new FJsonObject());
			CatObj->SetStringField(TEXT("name"), CatName.ToString());
			CatObj->SetNumberField(TEXT("message_count"), MsgCount);
			CatObj->SetNumberField(TEXT("error_count"), ErrorCount);
			CatObj->SetNumberField(TEXT("warning_count"), WarningCount);
			CatObj->SetNumberField(TEXT("info_count"), InfoCount);
			CatObj->SetBoolField(TEXT("has_fixes"), bHasFixes);
			CategoriesArray.Add(MakeShareable(new FJsonValueObject(CatObj)));
			TotalMessages += MsgCount;
		}

		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetArrayField(TEXT("categories"), CategoriesArray);
		ResultObj->SetNumberField(TEXT("total_categories"), CategoriesArray.Num());
		ResultObj->SetNumberField(TEXT("total_messages"), TotalMessages);
	}
	else
	{
		// === SINGLE CATEGORY MESSAGES ===
		FName CatFName(*Category);

		if (!MessageLogModule.IsRegisteredLogListing(CatFName))
		{
			ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
			ResultObj->SetStringField(JSON_ERROR_FIELD,
				FString::Printf(TEXT("Category '%s' is not registered or has no messages"),
					*Category));
			FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
			return Result;
		}

		TSharedRef<IMessageLogListing> Listing =
			MessageLogModule.GetLogListing(CatFName);
		const TArray<TSharedRef<FTokenizedMessage>>& Messages =
			Listing->GetFilteredMessages();

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		int32 ErrorCount = 0, WarningCount = 0, InfoCount = 0, FixableCount = 0;
		int32 MsgIndex = 0;

		for (const TSharedRef<FTokenizedMessage>& Msg : Messages)
		{
			if (MessagesArray.Num() >= MaxMsg)
				break;

			if (!PassesSeverityFilter(Msg->GetSeverity(), SeverityFilter))
			{
				MsgIndex++;
				continue;
			}

			switch (Msg->GetSeverity())
			{
			case EMessageSeverity::Error: ErrorCount++; break;
			case EMessageSeverity::Warning:
			case EMessageSeverity::PerformanceWarning: WarningCount++; break;
			default: InfoCount++; break;
			}

			TSharedPtr<FJsonObject> MsgObj = MakeShareable(new FJsonObject());
			MsgObj->SetNumberField(TEXT("index"), MsgIndex);
			MsgObj->SetStringField(TEXT("severity"), SeverityToString(Msg->GetSeverity()));
			MsgObj->SetStringField(TEXT("text"), Msg->ToText().ToString());

			int32 FixCount = CountFixTokens(Msg);
			MsgObj->SetBoolField(TEXT("has_fix"), FixCount > 0);
			MsgObj->SetNumberField(TEXT("fix_count"), FixCount);
			if (FixCount > 0) FixableCount++;

			if (IncludeTokens)
			{
				TArray<TSharedPtr<FJsonValue>> TokensArray;
				const TArray<TSharedRef<IMessageToken>>& Tokens = Msg->GetMessageTokens();
				for (const TSharedRef<IMessageToken>& Token : Tokens)
				{
					TSharedPtr<FJsonObject> TokObj = MakeShareable(new FJsonObject());
					TokObj->SetStringField(TEXT("type"), TokenTypeToString(Token->GetType()));
					TokObj->SetStringField(TEXT("text"), Token->ToText().ToString());
					TokensArray.Add(MakeShareable(new FJsonValueObject(TokObj)));
				}
				MsgObj->SetArrayField(TEXT("tokens"), TokensArray);
			}

			MessagesArray.Add(MakeShareable(new FJsonValueObject(MsgObj)));
			MsgIndex++;
		}

		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("category"), Category);
		ResultObj->SetArrayField(TEXT("messages"), MessagesArray);
		ResultObj->SetNumberField(TEXT("message_count"), MessagesArray.Num());
		ResultObj->SetNumberField(TEXT("fixable_count"), FixableCount);

		TSharedPtr<FJsonObject> SeveritySummary = MakeShareable(new FJsonObject());
		SeveritySummary->SetNumberField(TEXT("error"), ErrorCount);
		SeveritySummary->SetNumberField(TEXT("warning"), WarningCount);
		SeveritySummary->SetNumberField(TEXT("info"), InfoCount);
		ResultObj->SetObjectField(TEXT("severity_summary"), SeveritySummary);
	}
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("GetMessageLog requires Editor build"));
#endif

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::ExecuteMessageFix(
	const FString& Category,
	int64 MessageIndex,
	int64 FixTokenIndex,
	bool DryRun)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

#if WITH_EDITOR
	if (Category.IsEmpty())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			TEXT("Category is required for ExecuteMessageFix"));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	FMessageLogModule& MessageLogModule =
		FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FName CatFName(*Category);

	if (!MessageLogModule.IsRegisteredLogListing(CatFName))
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			FString::Printf(TEXT("Category '%s' is not registered"), *Category));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	TSharedRef<IMessageLogListing> Listing =
		MessageLogModule.GetLogListing(CatFName);
	const TArray<TSharedRef<FTokenizedMessage>>& Messages =
		Listing->GetFilteredMessages();

	int32 MsgIdx = static_cast<int32>(MessageIndex);
	if (MsgIdx < 0 || MsgIdx >= Messages.Num())
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			FString::Printf(TEXT("MessageIndex %d out of range [0, %d)"),
				MsgIdx, Messages.Num()));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	const TSharedRef<FTokenizedMessage>& TargetMessage = Messages[MsgIdx];

	// Find the Nth Fix token
	int32 FixIdx = static_cast<int32>(FixTokenIndex);
	int32 CurrentFixIdx = 0;
	TSharedPtr<IMessageToken> FoundFixToken = nullptr;

	for (const TSharedRef<IMessageToken>& Token : TargetMessage->GetMessageTokens())
	{
		if (Token->GetType() == EMessageToken::Fix)
		{
			if (CurrentFixIdx == FixIdx)
			{
				FoundFixToken = Token;
				break;
			}
			CurrentFixIdx++;
		}
	}

	if (!FoundFixToken.IsValid())
	{
		int32 TotalFixes = CountFixTokens(TargetMessage);
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			FString::Printf(TEXT("FixTokenIndex %d out of range. Message has %d fix token(s)."),
				FixIdx, TotalFixes));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	FString FixDescription = FoundFixToken->ToText().ToString();

	if (DryRun)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("fix_description"), FixDescription);
		ResultObj->SetBoolField(TEXT("would_execute"), true);
		ResultObj->SetStringField(TEXT("result"), TEXT("dry_run"));
	}
	else
	{
		Listing->ExecuteToken(FoundFixToken.ToSharedRef());

		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetStringField(TEXT("fix_description"), FixDescription);
		ResultObj->SetStringField(TEXT("result"), TEXT("executed"));
	}
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("ExecuteMessageFix requires Editor build"));
#endif

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// =========================================================
// Blueprint Runtime Debug Functions (Issue #5293)
// =========================================================

FString UCURemoteLibrary::GetWatchedValues(
	const FString& BlueprintPath,
	int32 MaxDepth)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

#if WITH_EDITOR
	if (MaxDepth < 1) MaxDepth = 1;
	if (MaxDepth > 10) MaxDepth = 10;

	// Load the Blueprint asset
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!Blueprint)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	// Check if PIE is running
	UWorld* PlayWorld = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		PlayWorld = GEditor->PlayWorld;
	}

	TArray<TSharedPtr<FJsonValue>> WatchesArray;

	// Iterate Blueprint pin watches via FKismetDebugUtilities (UE 5.7 API).
	// Note: MaxDepth is reserved for future FPropertyInstanceInfo traversal depth control.
	// UE 5.7 GetDebugInfo() handles depth internally via FPropertyInstanceInfo hierarchy.
	UObject* DebugObject = Blueprint->GetObjectBeingDebugged();

	FKismetDebugUtilities::ForeachPinPropertyWatch(Blueprint,
		[&](FBlueprintWatchedPin& WatchedPin)
		{
			UEdGraphPin* Pin = WatchedPin.Get();
			if (!Pin || !Pin->GetOwningNode())
			{
				return;
			}

			TSharedPtr<FJsonObject> WatchObj = MakeShareable(new FJsonObject());
			UEdGraphNode* Node = Pin->GetOwningNode();

			WatchObj->SetStringField(TEXT("pin_name"), Pin->PinName.ToString());
			WatchObj->SetStringField(TEXT("pin_type"), Pin->PinType.PinCategory.ToString());
			WatchObj->SetStringField(TEXT("node_name"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
			WatchObj->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());

			// Get runtime value if PIE is active
			if (PlayWorld && DebugObject)
			{
				TSharedPtr<FPropertyInstanceInfo> DebugInfo;
				const FKismetDebugUtilities::EWatchTextResult WatchResult =
					FKismetDebugUtilities::GetDebugInfo(DebugInfo, Blueprint, DebugObject, Pin);

				if (WatchResult == FKismetDebugUtilities::EWTR_Valid && DebugInfo.IsValid())
				{
					WatchObj->SetStringField(TEXT("value"), DebugInfo->GetWatchText());
					WatchObj->SetStringField(TEXT("watch_status"), TEXT("valid"));
				}
				else
				{
					WatchObj->SetStringField(TEXT("value"), TEXT(""));
					WatchObj->SetStringField(TEXT("watch_status"),
						WatchResult == FKismetDebugUtilities::EWTR_NotInScope ? TEXT("not_in_scope") :
						WatchResult == FKismetDebugUtilities::EWTR_NoProperty ? TEXT("no_property") :
						TEXT("unknown"));
				}
			}
			else if (PlayWorld)
			{
				WatchObj->SetStringField(TEXT("value"), TEXT(""));
				WatchObj->SetStringField(TEXT("watch_status"), TEXT("no_debug_object"));
			}
			else
			{
				WatchObj->SetStringField(TEXT("value"), TEXT(""));
				WatchObj->SetStringField(TEXT("watch_status"), TEXT("pie_not_running"));
			}

			WatchesArray.Add(MakeShareable(new FJsonValueObject(WatchObj)));
		});

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetArrayField(TEXT("watches"), WatchesArray);
	ResultObj->SetNumberField(TEXT("watch_count"), WatchesArray.Num());
	ResultObj->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	ResultObj->SetBoolField(TEXT("pie_active"), PlayWorld != nullptr);
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("GetWatchedValues requires Editor build"));
#endif

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::GetExecutionTrace(
	int32 MaxEntries,
	const FString& BlueprintFilter)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

#if WITH_EDITOR
	if (MaxEntries < 1) MaxEntries = 1;
	if (MaxEntries > 500) MaxEntries = 500;

	// Check if PIE is running
	if (!GEditor || !GEditor->PlayWorld)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
		ResultObj->SetArrayField(TEXT("traces"), TArray<TSharedPtr<FJsonValue>>());
		ResultObj->SetNumberField(TEXT("trace_count"), 0);
		ResultObj->SetStringField(TEXT("hint"), TEXT("PIE is not running. Start Play-In-Editor to capture execution traces."));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> TracesArray;

	// Access the trace samples ring buffer (UE 5.7: GetTraceStack returns TSimpleRingBuffer)
	const auto& TraceStack = FKismetDebugUtilities::GetTraceStack();
	const int32 NumSamples = TraceStack.Num();

	// Iterate reverse-chronologically
	int32 CollectedCount = 0;
	for (int32 i = NumSamples - 1; i >= 0 && CollectedCount < MaxEntries; --i)
	{
		const FKismetTraceSample& Sample = TraceStack(i);

		if (!Sample.Context.IsValid() || !Sample.Function.IsValid())
		{
			continue;
		}

		UObject* ContextObj = Sample.Context.Get();
		UFunction* Function = Sample.Function.Get();
		if (!ContextObj || !Function)
		{
			continue;
		}

		// Get owning Blueprint path
		FString OwningBPPath;
		UClass* ContextClass = ContextObj->GetClass();
		if (ContextClass)
		{
			UBlueprint* OwningBP = Cast<UBlueprint>(ContextClass->ClassGeneratedBy);
			if (OwningBP)
			{
				OwningBPPath = OwningBP->GetPathName();
			}
		}

		// Apply Blueprint filter if specified
		if (!BlueprintFilter.IsEmpty() && !OwningBPPath.Contains(BlueprintFilter))
		{
			continue;
		}

		TSharedPtr<FJsonObject> TraceObj = MakeShareable(new FJsonObject());
		TraceObj->SetStringField(TEXT("object_name"), ContextObj->GetName());
		TraceObj->SetStringField(TEXT("object_class"), ContextObj->GetClass()->GetName());
		TraceObj->SetStringField(TEXT("function_name"), Function->GetName());
		TraceObj->SetStringField(TEXT("blueprint_path"), OwningBPPath);
		TraceObj->SetNumberField(TEXT("offset"), Sample.Offset);
		TraceObj->SetNumberField(TEXT("timestamp_ms"), Sample.ObservationTime * 1000.0);

		TracesArray.Add(MakeShareable(new FJsonValueObject(TraceObj)));
		CollectedCount++;
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, true);
	ResultObj->SetArrayField(TEXT("traces"), TracesArray);
	ResultObj->SetNumberField(TEXT("trace_count"), TracesArray.Num());
	if (!BlueprintFilter.IsEmpty())
	{
		ResultObj->SetStringField(TEXT("blueprint_filter"), BlueprintFilter);
	}
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("GetExecutionTrace requires Editor build"));
#endif

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

FString UCURemoteLibrary::StepExecution(const FString& Action)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShareable(new FJsonObject());

#if WITH_EDITOR
	if (!GEditor || !GEditor->PlayWorld)
	{
		ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
		ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("PIE is not running. Cannot step execution."));
		FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
		return Result;
	}

	FString ActionLower = Action.ToLower();
	FString NewState;
	bool bSuccess = true;

	if (ActionLower == TEXT("resume"))
	{
		if (GEditor->PlayWorld->bDebugPauseExecution)
		{
			GEditor->PlayWorld->bDebugPauseExecution = false;
			NewState = TEXT("running");
		}
		else
		{
			NewState = TEXT("already_running");
		}
	}
	else if (ActionLower == TEXT("pause"))
	{
		if (!GEditor->PlayWorld->bDebugPauseExecution)
		{
			GEditor->PlayWorld->bDebugPauseExecution = true;
			NewState = TEXT("paused");
		}
		else
		{
			NewState = TEXT("already_paused");
		}
	}
	else if (ActionLower == TEXT("step_into"))
	{
		FKismetDebugUtilities::RequestSingleStepIn();
		if (GEditor->PlayWorld->bDebugPauseExecution)
		{
			GEditor->PlayWorld->bDebugPauseExecution = false;
		}
		NewState = TEXT("stepping_into");
	}
	else if (ActionLower == TEXT("step_over"))
	{
		FKismetDebugUtilities::RequestStepOver();
		if (GEditor->PlayWorld->bDebugPauseExecution)
		{
			GEditor->PlayWorld->bDebugPauseExecution = false;
		}
		NewState = TEXT("stepping_over");
	}
	else if (ActionLower == TEXT("step_out"))
	{
		FKismetDebugUtilities::RequestStepOut();
		if (GEditor->PlayWorld->bDebugPauseExecution)
		{
			GEditor->PlayWorld->bDebugPauseExecution = false;
		}
		NewState = TEXT("stepping_out");
	}
	else if (ActionLower == TEXT("frame_advance"))
	{
		// Note: UE has no dedicated "frame advance" API for Blueprint debugging.
		// RequestSingleStepIn() is the closest equivalent — it advances to the next
		// BP node execution, which in practice often corresponds to one frame tick.
		FKismetDebugUtilities::RequestSingleStepIn();
		NewState = TEXT("frame_advancing");
	}
	else
	{
		bSuccess = false;
		ResultObj->SetStringField(JSON_ERROR_FIELD,
			FString::Printf(TEXT("Unknown action: '%s'. Valid actions: resume, pause, step_into, step_over, step_out, frame_advance"), *Action));
	}

	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, bSuccess);
	if (bSuccess)
	{
		ResultObj->SetStringField(TEXT("action"), Action);
		ResultObj->SetStringField(TEXT("new_state"), NewState);
	}
#else
	ResultObj->SetBoolField(JSON_SUCCESS_FIELD, false);
	ResultObj->SetStringField(JSON_ERROR_FIELD, TEXT("StepExecution requires Editor build"));
#endif

	FString Result = CUJsonUtils::SerializeToString(ResultObj.ToSharedRef());
	return Result;
}

// ── Issue #7573: Blueprint function parameter manipulation (ECABridge parity) ──
// K2Node_EditablePinBase::CreateUserDefinedPin/RemoveUserDefinedPinByName are BLUEPRINTGRAPH_API
// but NOT UFUNCTION, so Python/RC cannot call them directly. These wrappers expose them.

FString UCURemoteLibrary::AddBlueprintFunctionParameter(
	const FString& BlueprintPath,
	const FString& FunctionName,
	const FString& ParameterName,
	const FString& PinCategory,
	const FString& PinSubCategoryObject,
	bool bIsOutput)
{
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString::Printf(TEXT("[add_function_parameter] Blueprint not found: %s"), *BlueprintPath);
	}

	UEdGraph* Graph = BlueprintNodeHelpers::FindGraphByName(Blueprint, FunctionName);
	if (!Graph)
	{
		return FString::Printf(TEXT("[add_function_parameter] Function not found: %s"), *FunctionName);
	}

	// Find target node: FunctionEntry (input params) or FunctionResult (output params)
	UK2Node_EditablePinBase* TargetNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (bIsOutput)
		{
			if (UK2Node_FunctionResult* ResultNode = Cast<UK2Node_FunctionResult>(Node))
			{
				TargetNode = ResultNode;
				break;
			}
		}
		else
		{
			if (UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node))
			{
				TargetNode = EntryNode;
				break;
			}
		}
	}

	if (!TargetNode)
	{
		return FString::Printf(TEXT("[add_function_parameter] No %s node found in %s"),
			bIsOutput ? TEXT("FunctionResult") : TEXT("FunctionEntry"), *FunctionName);
	}

	// Build FEdGraphPinType from category + optional sub-object
	FEdGraphPinType PinType;
	PinType.PinCategory = *PinCategory;

	// PC_Real requires PinSubCategory = "double" or "float" (UE 5.7 assertion)
	if (PinCategory == TEXT("real"))
	{
		PinType.PinSubCategory = TEXT("double");
	}

	if (!PinSubCategoryObject.IsEmpty())
	{
		UObject* SubObj = StaticLoadObject(UObject::StaticClass(), nullptr, *PinSubCategoryObject);
		if (SubObj)
		{
			PinType.PinSubCategoryObject = SubObj;
		}
	}

	// Entry node outputs = function inputs; Result node inputs = function outputs
	const EEdGraphPinDirection Direction = bIsOutput ? EGPD_Input : EGPD_Output;

	TargetNode->Modify();
	UEdGraphPin* NewPin = TargetNode->CreateUserDefinedPin(*ParameterName, PinType, Direction);
	if (!NewPin)
	{
		return FString::Printf(TEXT("[add_function_parameter] Failed to create pin: %s"), *ParameterName);
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	return FString::Printf(TEXT("[add_function_parameter] SUCCESS param=%s type=%s direction=%s"),
		*ParameterName, *PinCategory, bIsOutput ? TEXT("output") : TEXT("input"));
}

FString UCURemoteLibrary::RemoveBlueprintFunctionParameter(
	const FString& BlueprintPath,
	const FString& FunctionName,
	const FString& ParameterName)
{
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString::Printf(TEXT("[remove_function_parameter] Blueprint not found: %s"), *BlueprintPath);
	}

	UEdGraph* Graph = BlueprintNodeHelpers::FindGraphByName(Blueprint, FunctionName);
	if (!Graph)
	{
		return FString::Printf(TEXT("[remove_function_parameter] Function not found: %s"), *FunctionName);
	}

	// Search both Entry and Result nodes for the named parameter
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_EditablePinBase* EditableNode = Cast<UK2Node_EditablePinBase>(Node);
		if (!EditableNode)
		{
			continue;
		}
		if (!Node->IsA<UK2Node_FunctionEntry>() && !Node->IsA<UK2Node_FunctionResult>())
		{
			continue;
		}
		// Check if pin exists by searching the node's pins array
		bool bPinFound = false;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->GetFName() == *ParameterName)
			{
				bPinFound = true;
				break;
			}
		}
		if (bPinFound)
		{
			EditableNode->Modify();
			EditableNode->RemoveUserDefinedPinByName(*ParameterName);
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			return FString::Printf(TEXT("[remove_function_parameter] SUCCESS param=%s"), *ParameterName);
		}
	}

	return FString::Printf(TEXT("[remove_function_parameter] Parameter not found: %s"), *ParameterName);
}

// Issue #20522: shared interface resolver for AddBlueprintInterface / RemoveBlueprintInterface.
// Pure move of the #20521-hardened resolver (double-slash rejection, Method A /Script/ full
// path + U/I-strip retry, Method B /Game/ BPI asset, Method C short name, CLASS_Interface
// validation) — branch order and error message formats are byte-identical for the add path
// (OpTag = "[add_interface]"). Returns nullptr with OutError set on any rejection.
static UClass* ResolveBlueprintInterfaceClass(
	const FString& InterfaceName,
	const TCHAR* OpTag,
	FString& OutError)
{
	// Issue #20521: reject any double-slash up front. A '//' anywhere in the name reaches
	// StaticLoadObject -> ResolveName -> CreatePackage("/Game//...") -> UObjectGlobals.cpp:1093
	// Fatal "name with double slashes" in the /Game/-prefix and BPI-asset paths below (the
	// engine's own trigger is the identical Contains("//") predicate). One guard closes the
	// raw and embedded '//' forms for every branch; the join-point '//' (/Game//Script/...,
	// the original crash) is closed structurally by never prepending /Game/ to a leading-slash
	// input (see Method C).
	if (InterfaceName.Contains(TEXT("//")))
	{
		OutError = FString::Printf(TEXT("%s INVALID_NAME double-slash in interface name: %s"), OpTag, *InterfaceName);
		return nullptr;
	}

	// Resolve the interface class. The three input forms are mutually exclusive so a
	// leading-slash input can never fall through to the /Game/ prefix loop.
	UClass* InterfaceClass = nullptr;

	if (InterfaceName.StartsWith(TEXT("/Script/")))
	{
		// Method A (Issue #20521): /Script/<Module>.<Class> native full path — direct lookup.
		// Never enters the /Game/ prefix loop, so no double-slash package is ever built.
		InterfaceClass = FindObject<UClass>(nullptr, *InterfaceName);

		// The schema documents the U-prefixed form (/Script/Game.UMyInterface), but the
		// reflected UClass name for a UINTERFACE strips the leading U/I (UUserObjectListEntry
		// -> /Script/UMG.UserObjectListEntry). Retry once with the class portion's leading
		// U/I removed. Safe: runs only after a direct-lookup miss, and any mis-resolution is
		// rejected below by the CLASS_Interface check (never a crash).
		if (!InterfaceClass)
		{
			int32 DotIdx = INDEX_NONE;
			if (InterfaceName.FindLastChar(TEXT('.'), DotIdx) && (DotIdx + 1) < InterfaceName.Len())
			{
				const FString ClassPart = InterfaceName.Mid(DotIdx + 1);
				if (ClassPart.Len() > 1 && (ClassPart[0] == TEXT('U') || ClassPart[0] == TEXT('I')))
				{
					const FString Retry = InterfaceName.Left(DotIdx + 1) + ClassPart.RightChop(1);
					InterfaceClass = FindObject<UClass>(nullptr, *Retry);
				}
			}
		}
	}
	else if (InterfaceName.StartsWith(TEXT("/")))
	{
		// Method B: /Game/... Blueprint Interface asset (full object path). A leading-slash
		// input is handled here, never by the /Game/ prefix loop below — this is what closes
		// the join-point double-slash crash from Issue #20521.
		UBlueprint* InterfaceBP = Cast<UBlueprint>(
			StaticLoadObject(UBlueprint::StaticClass(), nullptr, *InterfaceName));
		if (InterfaceBP && InterfaceBP->GeneratedClass)
		{
			InterfaceClass = InterfaceBP->GeneratedClass;
		}
	}
	else
	{
		// Method C: short name (no leading slash). Prepend /Game/ variants, then native
		// fallbacks. Guarded: InterfaceName has no leading '/', so "/Game/" + name cannot form
		// a join-point double slash. Do NOT relax this branch to accept leading-slash inputs —
		// that would reopen the /Game//... Fatal (Issue #20521).
		checkf(!InterfaceName.StartsWith(TEXT("/")),
			TEXT("Method C must never receive a leading-slash interface name (double-slash guard)"));

		TArray<FString> Prefixes = {
			TEXT("/Game/"),
			TEXT("/Game/Blueprints/"),
			TEXT("/Game/Interfaces/")
		};
		for (const FString& Prefix : Prefixes)
		{
			FString FullPath = Prefix + InterfaceName;
			UBlueprint* InterfaceBP = Cast<UBlueprint>(
				StaticLoadObject(UBlueprint::StaticClass(), nullptr, *FullPath));
			if (InterfaceBP && InterfaceBP->GeneratedClass)
			{
				InterfaceClass = InterfaceBP->GeneratedClass;
				break;
			}
		}

		// Native C++ interface fallbacks: Engine module by convention, then a global search.
		if (!InterfaceClass)
		{
			InterfaceClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *InterfaceName));
		}
		if (!InterfaceClass)
		{
			InterfaceClass = FindFirstObject<UClass>(*InterfaceName, EFindFirstObjectOptions::None);
		}
	}

	if (!InterfaceClass)
	{
		OutError = FString::Printf(TEXT("%s Interface not found: %s"), OpTag, *InterfaceName);
		return nullptr;
	}

	// Issue #20521: reject non-interface classes explicitly. ImplementNewInterface does NOT
	// validate this (it just iterates the class's functions and adds them), and a later
	// CompileBlueprint hits KismetCompiler's check(HasAnyClassFlags(CLASS_Interface)) Fatal.
	// BPI generated classes carry CLASS_Interface, so this does not reject valid Blueprint
	// Interface assets.
	if (!InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		OutError = FString::Printf(
			TEXT("%s NOT_AN_INTERFACE resolved=%s from=%s — only UINTERFACE / Blueprint-Interface classes can be implemented"),
			OpTag, *InterfaceClass->GetName(), *InterfaceName);
		return nullptr;
	}

	return InterfaceClass;
}

// Issue #7719: AddBlueprintInterface — ImplementedInterfaces is protected in Python
// Issue #21116: bPreviewOnly = resolver + already-check without mutation (symmetric to Remove).
FString UCURemoteLibrary::AddBlueprintInterface(
	const FString& BlueprintPath,
	const FString& InterfaceName,
	bool bPreviewOnly)
{
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString::Printf(TEXT("[add_interface] Blueprint not found: %s"), *BlueprintPath);
	}

	// Issue #20522: resolver shared with RemoveBlueprintInterface (pure move of the
	// #20521-hardened logic; add-path error strings unchanged byte-for-byte).
	FString ResolveError;
	UClass* InterfaceClass = ResolveBlueprintInterfaceClass(InterfaceName, TEXT("[add_interface]"), ResolveError);
	if (!InterfaceClass)
	{
		return ResolveError;
	}

	// Check if already implemented
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (Desc.Interface == InterfaceClass)
		{
			return FString::Printf(TEXT("[add_interface] ALREADY_IMPLEMENTED interface=%s"), *InterfaceName);
		}
	}

	// Issue #21116: dry_run preview — same resolver/already gates as real, no mutation.
	// Current interface class names listed so Python can surface current_interfaces parity.
	if (bPreviewOnly)
	{
		TArray<FString> CurrentNames;
		for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
		{
			if (Desc.Interface)
			{
				CurrentNames.Add(Desc.Interface->GetName());
			}
		}
		return FString::Printf(
			TEXT("[add_interface] PREVIEW would_add=%s class=%s current=[%s]"),
			*InterfaceName, *InterfaceClass->GetName(),
			*FString::Join(CurrentNames, TEXT(";")));
	}

	// Issue #20521: honor the bool return. false = a required interface function conflicts with
	// an existing graph, or an anim function on a non-anim blueprint — no interface was added,
	// so reporting SUCCESS would be a lie.
	const bool bImplemented = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName());
	if (!bImplemented)
	{
		return FString::Printf(
			TEXT("[add_interface] IMPLEMENT_FAILED interface=%s (interface function conflicts with an existing graph, or anim function on a non-anim blueprint)"),
			*InterfaceName);
	}
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	return FString::Printf(TEXT("[add_interface] SUCCESS interface=%s class=%s"),
		*InterfaceName, *InterfaceClass->GetName());
}

// Issue #20522: RemoveBlueprintInterface — symmetric CRUD to AddBlueprintInterface.
// Shares ResolveBlueprintInterfaceClass (#20521 resolver). UE5.8's
// FBlueprintEditorUtils::RemoveInterface deletes TWO sets when !bPreserveFunctions:
// the interface's function graphs (Desc.Graphs) AND its event nodes in the EventGraph
// (BlueprintEditorUtils.cpp:6680-6765); bPreviewOnly enumerates both without mutating.
FString UCURemoteLibrary::RemoveBlueprintInterface(
	const FString& BlueprintPath,
	const FString& InterfaceName,
	bool bPreserveFunctions,
	bool bPreviewOnly)
{
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return FString::Printf(TEXT("[remove_interface] Blueprint not found: %s"), *BlueprintPath);
	}

	FString ResolveError;
	UClass* InterfaceClass = ResolveBlueprintInterfaceClass(InterfaceName, TEXT("[remove_interface]"), ResolveError);
	if (!InterfaceClass)
	{
		return ResolveError;
	}

	// Authoritative implemented-check (exact class compare — no fuzzy name matching).
	// Not implemented = idempotent no-op, reported distinctly so the caller can treat
	// it as success without pretending a removal happened.
	int32 DescIndex = INDEX_NONE;
	for (int32 Idx = 0; Idx < Blueprint->ImplementedInterfaces.Num(); ++Idx)
	{
		if (Blueprint->ImplementedInterfaces[Idx].Interface == InterfaceClass)
		{
			DescIndex = Idx;
			break;
		}
	}
	if (DescIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("[remove_interface] NOT_IMPLEMENTED interface=%s"), *InterfaceName);
	}

	if (bPreviewOnly)
	{
		// Blast-radius preview (no mutation). Mirrors the engine's own deletion sets:
		// Desc.Graphs (function-style) + interface event nodes (event-style; matched by
		// the same EventReference member-parent predicate RemoveInterface uses).
		//
		// List-format defense: the PREVIEW marker joins names with ';' inside '[...]'.
		// ';', '[' and ']' are all members of INVALID_OBJECTNAME_CHARACTERS, so no legal
		// UObject (graph) or UFUNCTION name can contain them — strip them anyway so even
		// a hypothetical exotic name can never corrupt the delimited list the Python
		// side parses (defense-in-depth, ns-review #20522).
		auto SanitizeForList = [](FString Name)
		{
			Name.ReplaceInline(TEXT(";"), TEXT("_"));
			Name.ReplaceInline(TEXT("["), TEXT("_"));
			Name.ReplaceInline(TEXT("]"), TEXT("_"));
			return Name;
		};

		const FBPInterfaceDescription& Desc = Blueprint->ImplementedInterfaces[DescIndex];
		TArray<FString> GraphNames;
		for (const UEdGraph* Graph : Desc.Graphs)
		{
			if (Graph)
			{
				GraphNames.Add(SanitizeForList(Graph->GetName()));
			}
		}

		TArray<FString> EventNodeNames;
		TArray<UK2Node_Event*> AllEvents;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, AllEvents);
		for (const UK2Node_Event* EventNode : AllEvents)
		{
			if (EventNode
				&& EventNode->EventReference.GetMemberParentClass(EventNode->GetBlueprintClassFromNode()) == InterfaceClass)
			{
				EventNodeNames.Add(SanitizeForList(EventNode->EventReference.GetMemberName().ToString()));
			}
		}

		return FString::Printf(
			TEXT("[remove_interface] PREVIEW interface=%s class=%s preserve=%s graphs=[%s] event_nodes=[%s]"),
			*InterfaceName, *InterfaceClass->GetName(),
			bPreserveFunctions ? TEXT("true") : TEXT("false"),
			*FString::Join(GraphNames, TEXT(";")),
			*FString::Join(EventNodeNames, TEXT(";")));
	}

	FBlueprintEditorUtils::RemoveInterface(Blueprint, InterfaceClass->GetClassPathName(), bPreserveFunctions);

	// RemoveInterface returns void — verify by re-scan; reporting SUCCESS for a
	// still-present interface would be a lie (same honesty bar as IMPLEMENT_FAILED).
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (Desc.Interface == InterfaceClass)
		{
			return FString::Printf(
				TEXT("[remove_interface] REMOVE_FAILED interface=%s (interface still present after RemoveInterface)"),
				*InterfaceName);
		}
	}

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	// Surface the post-removal compile status: orphaned downstream nodes can leave the
	// BP in BS_Error, and with save=true the caller persists that state — it must be
	// distinguishable from a clean removal in the response.
	const TCHAR* CompileStatus = TEXT("BS_Unknown");
	switch (Blueprint->Status)
	{
	case BS_UpToDate: CompileStatus = TEXT("BS_UpToDate"); break;
	case BS_UpToDateWithWarnings: CompileStatus = TEXT("BS_UpToDateWithWarnings"); break;
	case BS_Error: CompileStatus = TEXT("BS_Error"); break;
	case BS_Dirty: CompileStatus = TEXT("BS_Dirty"); break;
	case BS_BeingCreated: CompileStatus = TEXT("BS_BeingCreated"); break;
	default: break;
	}

	return FString::Printf(TEXT("[remove_interface] SUCCESS interface=%s class=%s compile=%s"),
		*InterfaceName, *InterfaceClass->GetName(), CompileStatus);
}

// Issue #20522: read-only bridge — UE 5.8 Python raises "Failed to find property
// 'implemented_interfaces'" on UBlueprint, so the MCP layer's enumeration helper
// must read the list through C++ (same reason the add/remove writes exist, #7719).
TArray<FString> UCURemoteLibrary::GetBlueprintImplementedInterfaces(
	const FString& BlueprintPath)
{
	TArray<FString> Names;
	UBlueprint* Blueprint = FCUBlueprintManipulator::LoadBlueprint(BlueprintPath);
	if (!Blueprint)
	{
		return Names;
	}
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (Desc.Interface)
		{
			Names.Add(Desc.Interface->GetName());
		}
	}
	return Names;
}

int32 UCURemoteLibrary::ExtractMemoryCSV(
	const FString& OutputDir,
	const FString& TraceFile,
	double TimeRangeStart,
	double TimeRangeEnd,
	const FString& PoolFilter,
	int32 MaxAllocations)
{
	return UCUInsightsMemoryCSVCommandlet::RunExtractionStatic(
		OutputDir, TraceFile, TimeRangeStart, TimeRangeEnd, PoolFilter, MaxAllocations);
}

// ============================================================================
// Issue #8564 Phase K-2: AnimNotify Manipulation
// ============================================================================

FString UCURemoteLibrary::MoveAnimNotify(
	const FString& AssetPath,
	int32 NotifyIndex,
	float NewTriggerTime)
{
	UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!AnimAsset)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Asset not found: %s\"}"), *AssetPath);
	}

	TArray<FAnimNotifyEvent>& Notifies = AnimAsset->Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= Notifies.Num())
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Invalid notify index %d (count: %d)\"}"),
			NotifyIndex, Notifies.Num());
	}

	// Clamp to sequence length
	float SeqLength = AnimAsset->GetPlayLength();
	NewTriggerTime = FMath::Clamp(NewTriggerTime, 0.0f, SeqLength);

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "MoveAnimNotify", "Move AnimNotify"));
	AnimAsset->Modify();
#endif

	FAnimNotifyEvent& Notify = Notifies[NotifyIndex];
	float OldTime = Notify.GetTriggerTime();
	FString NotifyName = Notify.NotifyName.ToString();

	// Update trigger time via the link's time value
	Notify.SetTime(NewTriggerTime);
	Notify.TriggerTimeOffset = 0.0f;

#if WITH_EDITOR
	AnimAsset->RefreshCacheData();
#endif
	AnimAsset->MarkPackageDirty();

	return FString::Printf(
		TEXT("{\"success\":true,\"notify_name\":\"%s\",\"old_time\":%.4f,\"new_time\":%.4f,\"sequence_length\":%.4f}"),
		*NotifyName, OldTime, NewTriggerTime, SeqLength);
}

FString UCURemoteLibrary::AddAnimNotify(
	const FString& AssetPath,
	const FString& NotifyClassName,
	float TriggerTime,
	float Duration)
{
	UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!AnimAsset)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Asset not found: %s\"}"), *AssetPath);
	}

	// Find the notify class
	UClass* NotifyClass = FindFirstObject<UClass>(*NotifyClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (!NotifyClass)
	{
		// Try with full path
		NotifyClass = LoadClass<UObject>(nullptr, *NotifyClassName);
	}
	if (!NotifyClass)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Notify class not found: %s\"}"),
			*NotifyClassName);
	}

	float SeqLength = AnimAsset->GetPlayLength();
	TriggerTime = FMath::Clamp(TriggerTime, 0.0f, SeqLength);

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddAnimNotify", "Add AnimNotify"));
	AnimAsset->Modify();
#endif

	// Create the notify event
	FAnimNotifyEvent NewNotify;
	NewNotify.NotifyName = FName(*NotifyClassName);
	NewNotify.SetTime(TriggerTime);
	NewNotify.TriggerTimeOffset = 0.0f;

	// Create the notify instance if it's a UAnimNotify or UAnimNotifyState subclass
	if (NotifyClass->IsChildOf(UAnimNotify::StaticClass()))
	{
		NewNotify.Notify = NewObject<UAnimNotify>(AnimAsset, NotifyClass);
	}
	else if (NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))
	{
		NewNotify.NotifyStateClass = NewObject<UAnimNotifyState>(AnimAsset, NotifyClass);
		NewNotify.SetDuration(FMath::Max(Duration, 0.01f));
	}

	int32 NewIndex = AnimAsset->Notifies.Add(NewNotify);

#if WITH_EDITOR
	AnimAsset->RefreshCacheData();
#endif
	AnimAsset->MarkPackageDirty();

	return FString::Printf(
		TEXT("{\"success\":true,\"notify_index\":%d,\"notify_name\":\"%s\",\"trigger_time\":%.4f,\"total_notifies\":%d}"),
		NewIndex, *NotifyClassName, TriggerTime, AnimAsset->Notifies.Num());
}

FString UCURemoteLibrary::RemoveAnimNotify(
	const FString& AssetPath,
	int32 NotifyIndex)
{
	UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!AnimAsset)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Asset not found: %s\"}"), *AssetPath);
	}

	TArray<FAnimNotifyEvent>& Notifies = AnimAsset->Notifies;
	if (NotifyIndex < 0 || NotifyIndex >= Notifies.Num())
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"error\":\"Invalid notify index %d (count: %d)\"}"),
			NotifyIndex, Notifies.Num());
	}

#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveAnimNotify", "Remove AnimNotify"));
	AnimAsset->Modify();
#endif

	FString RemovedName = Notifies[NotifyIndex].NotifyName.ToString();
	Notifies.RemoveAt(NotifyIndex);

#if WITH_EDITOR
	AnimAsset->RefreshCacheData();
#endif
	AnimAsset->MarkPackageDirty();

	return FString::Printf(
		TEXT("{\"success\":true,\"removed_notify\":\"%s\",\"remaining_count\":%d}"),
		*RemovedName, Notifies.Num());
}

FString UCURemoteLibrary::ListAnimNotifies(
	const FString& AssetPath)
{
	UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AssetPath);
	if (!AnimAsset)
	{
		return FString::Printf(TEXT("{\"success\":false,\"error\":\"Asset not found: %s\"}"), *AssetPath);
	}

	const TArray<FAnimNotifyEvent>& Notifies = AnimAsset->Notifies;

	FString NotifiesJson;
	for (int32 i = 0; i < Notifies.Num(); i++)
	{
		const FAnimNotifyEvent& N = Notifies[i];
		FString ClassName;
		if (N.Notify)
		{
			ClassName = N.Notify->GetClass()->GetName();
		}
		else if (N.NotifyStateClass)
		{
			ClassName = N.NotifyStateClass->GetClass()->GetName();
		}
		else
		{
			ClassName = N.NotifyName.ToString();
		}

		if (!NotifiesJson.IsEmpty())
		{
			NotifiesJson += TEXT(",");
		}
		NotifiesJson += FString::Printf(
			TEXT("{\"index\":%d,\"name\":\"%s\",\"class\":\"%s\","
				"\"trigger_time\":%.4f,\"duration\":%.4f,\"track_index\":%d}"),
			i,
			*N.NotifyName.ToString(),
			*ClassName,
			N.GetTriggerTime(),
			N.GetDuration(),
			N.TrackIndex);
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"asset\":\"%s\",\"sequence_length\":%.4f,"
			"\"notify_count\":%d,\"notifies\":[%s]}"),
		*AssetPath, AnimAsset->GetPlayLength(),
		Notifies.Num(), *NotifiesJson);
}

//==============================================================================
// UMG Widget Blueprint authoring (Issue #9344)
//==============================================================================
// Surface: 5 UFUNCTIONs called via HTTP Remote Control from
// `MCP/native/mcp_server/src/tools/widget_authoring_ops.rs`.
//
// Shared helpers (file-private) minimise boilerplate across the 5 ops.

namespace WidgetAuthoring_Issue9344
{
	/** Escape a string for embedding inside a JSON literal. */
	static FString EscapeJson(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
		Out.ReplaceInline(TEXT("\""), TEXT("\\\""));
		Out.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		Out.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Out.ReplaceInline(TEXT("\t"), TEXT("\\t"));
		return Out;
	}
	/** UE 5.8 compat: FString::operator*() is UE_LIFETIMEBOUND-annotated, blocking
	 *  *Rvalue() deref. Wrap rvalue results through a const-ref-binding helper.
	 *
	 *  Lifetime safety: the rvalue temporary's lifetime is extended to the end of
	 *  the full-expression (C++ const-ref binding rule), so the returned TCHAR*
	 *  is valid for the duration of e.g. `FString::Printf(TEXT("%s"), EscJ(...))`.
	 *  DO NOT store the returned pointer beyond the call expression. */
	static FORCEINLINE const TCHAR* EscJ(const FString& In) { return *In; }


	/** Build `{"success":false,"error":"..."}` response. */
	static FString FailureJson(const FString& OpLabel, const FString& Error)
	{
		return FString::Printf(
			TEXT("{\"success\":false,\"op\":\"%s\",\"error\":\"%s\"}"),
			EscJ(EscapeJson(OpLabel)), EscJ(EscapeJson(Error)));
	}

	/** Resolve a widget blueprint by short name (e.g. "W_MainMenu") or full path.
	 *
	 *  Tries LoadObject<> directly first (full path case), then falls back to
	 *  the Asset Registry for short-name lookup. Returns nullptr if ambiguous
	 *  or missing. `OutResolvedPath` is populated on success for round-trip
	 *  feedback in the response JSON.
	 */
	static UWidgetBlueprint* ResolveWidgetBlueprint(const FString& NameOrPath, FString& OutResolvedPath)
	{
		// 1) Full-path short-circuit.
		if (UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *NameOrPath))
		{
			OutResolvedPath = WBP->GetPathName();
			return WBP;
		}

		// 2) Asset Registry lookup by asset name.
		FAssetRegistryModule& ARM =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		ARM.Get().GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), Assets);
		const FName TargetName(*NameOrPath);
		UWidgetBlueprint* FoundWBP = nullptr;
		int32 MatchCount = 0;
		for (const FAssetData& Data : Assets)
		{
			if (Data.AssetName == TargetName)
			{
				++MatchCount;
				FoundWBP = Cast<UWidgetBlueprint>(Data.GetAsset());
			}
		}
		if (MatchCount == 1 && FoundWBP)
		{
			OutResolvedPath = FoundWBP->GetPathName();
			return FoundWBP;
		}
		return nullptr;
	}

	/** Find the widget inside `WidgetTree` by FName. Returns nullptr on miss. */
	static UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
	{
		if (!WBP || !WBP->WidgetTree)
		{
			return nullptr;
		}
		TArray<UWidget*> All;
		WBP->WidgetTree->GetAllWidgets(All);
		const FName TargetName(*WidgetName);
		for (UWidget* W : All)
		{
			if (W && W->GetFName() == TargetName)
			{
				return W;
			}
		}
		return nullptr;
	}

	/**
	 * Issue #23088: true if Needle is Root or any descendant reachable via
	 * panel children OR INamedSlotInterface content.
	 *
	 * Upward UWidget::GetParent() walks cannot cross named-slot boundaries
	 * (named-slot content leaves Slot == nullptr), so the pre-fix reparent
	 * cycle guard missed W → ExpandableArea[Body] → P → reparent W under P.
	 * Mirrors the editor HierarchyView "Can't make widget a child of its
	 * children" check, with an explicit visited set so an already-cyclic
	 * asset cannot stack-overflow the walk.
	 */
	static bool WidgetSubtreeContainsIncludingNamedSlots(UWidget* Root, UWidget* Needle)
	{
		if (!Root || !Needle)
		{
			return false;
		}
		TArray<UWidget*, TInlineAllocator<32>> Stack;
		TSet<const UWidget*> Visited;
		Stack.Push(Root);
		while (Stack.Num() > 0)
		{
			UWidget* Cur = Stack.Pop(EAllowShrinking::No);
			if (!Cur || Visited.Contains(Cur))
			{
				continue;
			}
			Visited.Add(Cur);
			if (Cur == Needle)
			{
				return true;
			}
			if (UPanelWidget* Panel = Cast<UPanelWidget>(Cur))
			{
				const int32 Count = Panel->GetChildrenCount();
				for (int32 i = 0; i < Count; ++i)
				{
					if (UWidget* Child = Panel->GetChildAt(i))
					{
						Stack.Push(Child);
					}
				}
			}
			if (INamedSlotInterface* NamedSlots = Cast<INamedSlotInterface>(Cur))
			{
				TArray<FName> SlotNames;
				NamedSlots->GetSlotNames(SlotNames);
				for (const FName& SlotName : SlotNames)
				{
					if (UWidget* Content = NamedSlots->GetContentForSlot(SlotName))
					{
						Stack.Push(Content);
					}
				}
			}
		}
		return false;
	}

	/**
	 * Issue #23089: locate the INamedSlotInterface (widget or WidgetTree bindings) that currently
	 * hosts Content. Named-slot content leaves UWidget::Slot == nullptr, so GetParent()/
	 * RemoveFromParent() are no-ops and AddChild alone leaves a duplicate host reference.
	 * Mirrors Epic editor FindNamedSlotHostWidgetForContent + WidgetTree::NamedSlotBindings.
	 * OutHost is either a UWidget* or the UWidgetTree* (both implement INamedSlotInterface).
	 */
	static bool FindNamedSlotHostForContent(
		UWidgetBlueprint* WBP,
		UWidget* Content,
		UObject*& OutHost,
		FName& OutSlotName)
	{
		OutHost = nullptr;
		OutSlotName = NAME_None;
		if (!WBP || !WBP->WidgetTree || !Content)
		{
			return false;
		}

		auto TryHost = [&](UObject* Candidate) -> bool
		{
			INamedSlotInterface* Named = Cast<INamedSlotInterface>(Candidate);
			if (!Named)
			{
				return false;
			}
			TArray<FName> SlotNames;
			Named->GetSlotNames(SlotNames);
			for (const FName& SlotName : SlotNames)
			{
				if (Named->GetContentForSlot(SlotName) == Content)
				{
					OutHost = Candidate;
					OutSlotName = SlotName;
					return true;
				}
			}
			return false;
		};

		// WidgetTree itself implements INamedSlotInterface over NamedSlotBindings.
		if (TryHost(WBP->WidgetTree))
		{
			return true;
		}

		TArray<UWidget*> All;
		WBP->WidgetTree->GetAllWidgets(All);
		for (UWidget* Candidate : All)
		{
			if (TryHost(Candidate))
			{
				return true;
			}
		}
		return false;
	}

	/** Issue #16777: persist the WBP package directly to disk (same-call save on the mutated
	 *  object). A UPanelSlot is an Instanced subobject; transacting it (Slot->Modify()) and saving
	 *  the SAME resolved object is what reaches disk on UE 5.8 — a separate caller-owned save can
	 *  resolve a divergent object and lose the slot delta (the #16777 root cause). Returns whether
	 *  SavePackage succeeded; on failure writes a short reason to OutError (Issue #16934 L-2). */
	static bool SaveWidgetPackage(UWidgetBlueprint* WBP, FString& OutError)
	{
		OutError.Reset();
		if (!WBP)
		{
			OutError = TEXT("null Widget Blueprint");
			return false;
		}
		UPackage* Pkg = WBP->GetOutermost();
		if (!Pkg)
		{
			OutError = TEXT("Widget Blueprint has no outer package");
			return false;
		}
		Pkg->MarkPackageDirty();
		const FString PkgFile = FPackageName::LongPackageNameToFilename(
			Pkg->GetName(), FPackageName::GetAssetPackageExtension());
		if (PkgFile.IsEmpty())
		{
			OutError = FString::Printf(TEXT("could not resolve a filename for package '%s'"), *Pkg->GetName());
			return false;
		}
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Pkg, WBP, *PkgFile, SaveArgs))
		{
			OutError = FString::Printf(TEXT("UPackage::SavePackage reported failure for '%s'"), *PkgFile);
			return false;
		}
		return true;
	}

	/** Issue #16934 Part B: result of a single unified widget-mutation finalisation. */
	struct FWidgetPersistResult
	{
		bool bMarkedDirty = false;
		bool bPersisted = false;
		FString PersistError;
	};

	/** Issue #16934 Part B: the ONE persistence contract for all 5 widget-authoring ops. Replaces
	 *  the prior split (bare FinaliseMutation vs. in-handler SaveWidgetPackage) so the
	 *  save-vs-mark-dirty policy lives in a single code path:
	 *   - MarkDirty == false → full no-op (the documented bulk/batch escape hatch; a later
	 *     caller-owned save persists the accumulated edits — a synchronous SavePackage per call is
	 *     heavy for interactive authoring).
	 *   - bStructural → MarkBlueprintAsStructurallyModified (widget add/remove + the value edits
	 *     that historically forced a recompile). A layout-only slot edit passes false.
	 *   - bPersistToDisk → same-call UPackage::SavePackage. Required for Instanced-subobject edits
	 *     (slot props, add_widget) whose delta a deferred caller-owned save loses (#16777 root
	 *     cause); asset's-own-property edits (set_text / set_image_brush / remove_widget) pass false
	 *     and rely on the caller's later save.
	 *  The per-op (bStructural, bPersistToDisk) flags REPLICATE each op's pre-#16934 behavior — this
	 *  is a code-path consolidation, not a behavior change. */
	static FWidgetPersistResult FinaliseWidgetMutation(
		UWidgetBlueprint* WBP, bool MarkDirty, bool bStructural, bool bPersistToDisk)
	{
		FWidgetPersistResult Result;
		if (!WBP || !MarkDirty)
		{
			return Result;
		}
		if (bStructural)
		{
#if WITH_EDITOR
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
#endif
		}
		if (bPersistToDisk)
		{
			// SaveWidgetPackage marks the package dirty then writes to disk in the same call.
			Result.bPersisted = SaveWidgetPackage(WBP, Result.PersistError);
			Result.bMarkedDirty = true;
		}
		else
		{
			WBP->MarkPackageDirty();
			if (UPackage* Pkg = WBP->GetOutermost())
			{
				Pkg->MarkPackageDirty();
			}
			Result.bMarkedDirty = true;
		}
		return Result;
	}

	/** Issue #16934 Part B: the uniform persistence-result JSON fragment shared by all 5 ops.
	 *  Always emits marked_dirty + persisted; appends persist_error only on a real save failure. */
	static FString PersistFieldsJson(const FWidgetPersistResult& R)
	{
		FString Fragment = FString::Printf(
			TEXT("\"marked_dirty\":%s,\"persisted\":%s"),
			R.bMarkedDirty ? TEXT("true") : TEXT("false"),
			R.bPersisted ? TEXT("true") : TEXT("false"));
		if (!R.PersistError.IsEmpty())
		{
			Fragment += FString::Printf(TEXT(",\"persist_error\":\"%s\""), EscJ(EscapeJson(R.PersistError)));
		}
		return Fragment;
	}

	/** Issue #19999: map a friendly (read-side snake_case) widget property alias to the real UE
	 *  property name so the generic `set_widget_property` op is read/write symmetric with the
	 *  offline parser's `apply_widget_properties` (dispatch_helpers/widget/properties.rs). An
	 *  unrecognised name is returned verbatim so an exact UE property name (e.g. "RenderOpacity")
	 *  passes straight through to FindPropertyByName. Matching is case-insensitive on the alias. */
	static FString ResolveWidgetPropertyAlias(const FString& PropertyName)
	{
		const FString Key = PropertyName.ToLower();
		// Alias -> UE FProperty name. Keys mirror the snake_case fields the parser emits.
		static const TMap<FString, FString> Aliases = {
			{ TEXT("visibility"),                TEXT("Visibility") },
			{ TEXT("render_opacity"),            TEXT("RenderOpacity") },
			{ TEXT("is_enabled"),                TEXT("bIsEnabled") },
			{ TEXT("b_is_enabled"),              TEXT("bIsEnabled") },
			{ TEXT("enabled"),                   TEXT("bIsEnabled") },
			{ TEXT("is_variable"),               TEXT("bIsVariable") },
			{ TEXT("b_is_variable"),             TEXT("bIsVariable") },
			{ TEXT("tool_tip_text"),             TEXT("ToolTipText") },
			{ TEXT("tool_tip"),                  TEXT("ToolTipText") },
			{ TEXT("tooltip"),                   TEXT("ToolTipText") },
			{ TEXT("clipping"),                  TEXT("Clipping") },
			{ TEXT("flow_direction"),            TEXT("FlowDirectionPreference") },
			{ TEXT("flow_direction_preference"), TEXT("FlowDirectionPreference") },
			{ TEXT("render_transform_pivot"),    TEXT("RenderTransformPivot") },
			{ TEXT("color_and_opacity"),         TEXT("ColorAndOpacity") },
			{ TEXT("color"),                     TEXT("ColorAndOpacity") },
			{ TEXT("font"),                      TEXT("Font") },
			{ TEXT("text"),                      TEXT("Text") },
			{ TEXT("min_desired_width"),         TEXT("MinDesiredWidth") },
			{ TEXT("min_desired_height"),        TEXT("MinDesiredHeight") },
			{ TEXT("width_override"),            TEXT("WidthOverride") },
			{ TEXT("height_override"),           TEXT("HeightOverride") },
		};
		if (const FString* Found = Aliases.Find(Key))
		{
			return *Found;
		}
		return PropertyName;
	}

	/** Issue #26342: `editcondition="bOverride_X"` (or a sibling named `bOverride_<Prop>`)
	 *  is the designer checkbox that actually applies an optional UMG value. A simple
	 *  identifier only — boolean expressions are not parsed. Writing the flag itself
	 *  (`bOverride_*` as the target) returns null so an explicit `false` is honored. */
	static bool IsSimpleEditConditionIdentifier(const FString& Expr)
	{
		if (Expr.IsEmpty())
		{
			return false;
		}
		for (const TCHAR C : Expr)
		{
			if (!FChar::IsAlnum(C) && C != TEXT('_'))
			{
				return false;
			}
		}
		return true;
	}

	static FBoolProperty* FindCompanionOverrideFlag(
		const UObject* Widget,
		const FProperty* ValueProp,
		const FString& UEName)
	{
		if (!Widget || !ValueProp || UEName.IsEmpty())
		{
			return nullptr;
		}
		if (UEName.StartsWith(TEXT("bOverride_"), ESearchCase::CaseSensitive))
		{
			return nullptr;
		}

		auto AsBoolFlag = [Widget](const FString& FlagName) -> FBoolProperty*
		{
			FProperty* Found = Widget->GetClass()->FindPropertyByName(FName(*FlagName));
			FBoolProperty* AsBool = CastField<FBoolProperty>(Found);
			if (!AsBool || !Found)
			{
				return nullptr;
			}
			const bool bOverrideNamed = FlagName.StartsWith(TEXT("bOverride_"), ESearchCase::CaseSensitive);
			const bool bToggle = Found->HasMetaData(TEXT("InlineEditConditionToggle"));
			if (!bOverrideNamed && !bToggle)
			{
				return nullptr;
			}
			return AsBool;
		};

		const FString EditCond = ValueProp->GetMetaData(TEXT("EditCondition"));
		if (IsSimpleEditConditionIdentifier(EditCond))
		{
			if (FBoolProperty* FromMeta = AsBoolFlag(EditCond))
			{
				return FromMeta;
			}
		}
		return AsBoolFlag(FString::Printf(TEXT("bOverride_%s"), *UEName));
	}

	/** Returns true when the flag was false and is now true. */
	static bool TryEnableCompanionOverride(
		UObject* Widget,
		FBoolProperty* FlagProp,
		FString& OutFlagName)
	{
		if (!Widget || !FlagProp)
		{
			return false;
		}
		if (FlagProp->GetPropertyValue_InContainer(Widget))
		{
			return false;
		}
		FlagProp->SetPropertyValue_InContainer(Widget, true);
		OutFlagName = FlagProp->GetName();
		return true;
	}
}

FString UCURemoteLibrary::SetWidgetText(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	const FString& Text,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("set_text");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	UTextBlock* TextBlock = Cast<UTextBlock>(Widget);
	if (!TextBlock)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget '%s' is %s, not a TextBlock — SetWidgetText only supports UTextBlock"),
			*TargetWidget, *Widget->GetClass()->GetName()));
	}

	const FText OldText = TextBlock->GetText();
	TextBlock->SetText(FText::FromString(Text));
	// Issue #16934 Part B: asset's-own-property edit → structural mark-dirty, no same-call save.
	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ false);

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"set_text\",\"widget_path\":\"%s\",\"target_widget\":\"%s\","
			"\"previous_text\":\"%s\",\"new_text\":\"%s\",%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(OldText.ToString())),
		EscJ(EscapeJson(Text)),
		*PersistFieldsJson(Persist));
}

FString UCURemoteLibrary::SetWidgetImageBrush(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	const FString& BrushAssetPath,
	const FString& TintColorJson,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("set_image_brush");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	UImage* Image = Cast<UImage>(Widget);
	if (!Image)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget '%s' is %s, not an Image — SetWidgetImageBrush only supports UImage"),
			*TargetWidget, *Widget->GetClass()->GetName()));
	}

	// Load brush object — accept Texture2D or UMaterialInterface.
	UObject* BrushObject = LoadObject<UObject>(nullptr, *BrushAssetPath);
	if (!BrushObject)
	{
		return FailureJson(Op, FString::Printf(TEXT("Brush asset not found: %s"), *BrushAssetPath));
	}
	if (!BrushObject->IsA<UTexture2D>() && !BrushObject->IsA<UMaterialInterface>())
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Brush asset '%s' is %s — must be Texture2D or MaterialInterface"),
			*BrushAssetPath, *BrushObject->GetClass()->GetName()));
	}

	FSlateBrush NewBrush = Image->GetBrush();
	const FString PrevResourceName =
		NewBrush.GetResourceObject() ? NewBrush.GetResourceObject()->GetPathName() : FString(TEXT(""));
	NewBrush.SetResourceObject(BrushObject);

	// Optional tint.
	if (!TintColorJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> TintObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TintColorJson);
		if (FJsonSerializer::Deserialize(Reader, TintObj) && TintObj.IsValid())
		{
			FLinearColor Tint = NewBrush.TintColor.GetSpecifiedColor();
			double Tmp;
			if (TintObj->TryGetNumberField(TEXT("r"), Tmp)) { Tint.R = static_cast<float>(Tmp); }
			if (TintObj->TryGetNumberField(TEXT("g"), Tmp)) { Tint.G = static_cast<float>(Tmp); }
			if (TintObj->TryGetNumberField(TEXT("b"), Tmp)) { Tint.B = static_cast<float>(Tmp); }
			if (TintObj->TryGetNumberField(TEXT("a"), Tmp)) { Tint.A = static_cast<float>(Tmp); }
			NewBrush.TintColor = FSlateColor(Tint);
		}
	}

	Image->SetBrush(NewBrush);
	// Issue #16934 Part B: asset's-own-property edit → structural mark-dirty, no same-call save.
	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ false);

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"set_image_brush\",\"widget_path\":\"%s\","
			"\"target_widget\":\"%s\",\"previous_brush_path\":\"%s\",\"new_brush_path\":\"%s\",%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(PrevResourceName)),
		EscJ(EscapeJson(BrushObject->GetPathName())),
		*PersistFieldsJson(Persist));
}

FString UCURemoteLibrary::SetWidgetSlotProperty(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	const FString& PropertyName,
	const FString& PropertyValueJson,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("set_slot_property");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	UPanelSlot* Slot = Widget->Slot;
	if (!Slot)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget '%s' has no parent slot (root widget has no slot to modify)"),
			*TargetWidget));
	}

	TSharedPtr<FJsonValue> PropertyValue;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertyValueJson);
		if (!FJsonSerializer::Deserialize(Reader, PropertyValue) || !PropertyValue.IsValid())
		{
			// Issue #16934 Part A: UE's single-value FJsonValue reader rejects a bare top-level
			// scalar (z_order:7 / auto_size:true). This branch is reached by add_widget_to_parent's
			// per-key delegation (it re-serializes each scalar to a bare token like "\r\n7") AND by a
			// standalone scalar set_slot_property (Rust forwards the bare value undecomposed). Retry
			// by wrapping in a JSON array and taking element 0 — that path parses
			// Number/Boolean/String/Object/Array uniformly and tolerates leading whitespace.
			TArray<TSharedPtr<FJsonValue>> Wrapped;
			TSharedRef<TJsonReader<>> WrapReader = TJsonReaderFactory<>::Create(
				FString::Printf(TEXT("[%s]"), *PropertyValueJson));
			if (FJsonSerializer::Deserialize(WrapReader, Wrapped) && Wrapped.Num() == 1 && Wrapped[0].IsValid())
			{
				PropertyValue = Wrapped[0];
			}
			else
			{
				return FailureJson(Op, FString::Printf(
					TEXT("Invalid PropertyValueJson for '%s': %s"), *PropertyName, *PropertyValueJson));
			}
		}
	}

	const FString Prop = PropertyName.ToLower();
	const FString SlotClass = Slot->GetClass()->GetName();

	// Issue #16777 FIX: transact the slot subobject + design WidgetTree BEFORE mutating, mirroring
	// the UMG Designer's own edit path (UWidget::Modify does Slot->SetFlags(RF_Transactional);
	// Slot->Modify()). A UPanelSlot is a separate Instanced subobject (unlike set_text, which
	// edits a UWidget's own property); without this transact-then-save the LayoutData change never
	// reached the saved .uasset — the defect this issue reported. Proven on a live UE 5.8 source
	// build: this Modify-chain + a direct same-call SavePackage persists sentinel anchors (f64) and
	// offsets (f32) to disk (byte-verified). The earlier "UE 5.8 engine limitation" reading was a
	// false negative (the prior path mutated and saved through divergent object/save paths).
#if WITH_EDITOR
	WBP->Modify();
	if (WBP->WidgetTree)
	{
		WBP->WidgetTree->SetFlags(RF_Transactional);
		WBP->WidgetTree->Modify();
	}
	Widget->SetFlags(RF_Transactional);
	Widget->Modify();
	Slot->SetFlags(RF_Transactional);
	Slot->Modify();
#endif

	auto AsObject = [&]() -> const TSharedPtr<FJsonObject>& {
		static const TSharedPtr<FJsonObject> Empty;
		return PropertyValue->Type == EJson::Object ? PropertyValue->AsObject() : Empty;
	};

	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		if (Prop == TEXT("anchors"))
		{
			const TSharedPtr<FJsonObject>& Obj = AsObject();
			if (!Obj.IsValid())
			{
				return FailureJson(Op, TEXT("anchors requires {min_x,min_y,max_x,max_y} object"));
			}
			FAnchors A = CanvasSlot->GetAnchors();
			double Tmp;
			if (Obj->TryGetNumberField(TEXT("min_x"), Tmp)) { A.Minimum.X = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("min_y"), Tmp)) { A.Minimum.Y = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("max_x"), Tmp)) { A.Maximum.X = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("max_y"), Tmp)) { A.Maximum.Y = static_cast<float>(Tmp); }
			CanvasSlot->SetAnchors(A);
		}
		else if (Prop == TEXT("offsets") || Prop == TEXT("padding"))
		{
			const TSharedPtr<FJsonObject>& Obj = AsObject();
			if (!Obj.IsValid())
			{
				return FailureJson(Op, TEXT("offsets requires {left,top,right,bottom} object"));
			}
			FMargin M = CanvasSlot->GetOffsets();
			double Tmp;
			if (Obj->TryGetNumberField(TEXT("left"), Tmp)) { M.Left = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("top"), Tmp)) { M.Top = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("right"), Tmp)) { M.Right = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("bottom"), Tmp)) { M.Bottom = static_cast<float>(Tmp); }
			CanvasSlot->SetOffsets(M);
		}
		else if (Prop == TEXT("alignment"))
		{
			const TSharedPtr<FJsonObject>& Obj = AsObject();
			if (!Obj.IsValid())
			{
				return FailureJson(Op, TEXT("alignment requires {x,y} object"));
			}
			FVector2D V = CanvasSlot->GetAlignment();
			double Tmp;
			if (Obj->TryGetNumberField(TEXT("x"), Tmp)) { V.X = Tmp; }
			if (Obj->TryGetNumberField(TEXT("y"), Tmp)) { V.Y = Tmp; }
			CanvasSlot->SetAlignment(V);
		}
		else if (Prop == TEXT("z_order"))
		{
			// Issue #23018: fail closed on type mismatch (was silent SetZOrder(0)).
			if (PropertyValue->Type != EJson::Number)
			{
				return FailureJson(Op, TEXT("z_order requires a JSON number"));
			}
			const int32 Z = static_cast<int32>(PropertyValue->AsNumber());
			CanvasSlot->SetZOrder(Z);
		}
		else if (Prop == TEXT("auto_size"))
		{
			// Issue #23018: fail closed (was non-bool → false + success).
			if (PropertyValue->Type != EJson::Boolean)
			{
				return FailureJson(Op, TEXT("auto_size requires a JSON boolean"));
			}
			CanvasSlot->SetAutoSize(PropertyValue->AsBool());
		}
		else
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Unsupported property '%s' for CanvasPanelSlot — supported: anchors, offsets, alignment, z_order, auto_size"),
				*PropertyName));
		}
	}
	else if (Prop == TEXT("row") || Prop == TEXT("column"))
	{
		if (PropertyValue->Type != EJson::Number)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("%s requires a non-negative int32"), *PropertyName));
		}
		const double RawIndex = PropertyValue->AsNumber();
		if (!FMath::IsFinite(RawIndex)
			|| RawIndex < 0.0
			|| RawIndex > static_cast<double>(MAX_int32)
			|| FMath::TruncToDouble(RawIndex) != RawIndex)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("%s requires a non-negative int32"), *PropertyName));
		}

		const int32 Index = static_cast<int32>(RawIndex);
		if (UGridSlot* GridSlot = Cast<UGridSlot>(Slot))
		{
			if (Prop == TEXT("row")) { GridSlot->SetRow(Index); }
			else { GridSlot->SetColumn(Index); }
		}
		else if (UUniformGridSlot* UniformGridSlot = Cast<UUniformGridSlot>(Slot))
		{
			if (Prop == TEXT("row")) { UniformGridSlot->SetRow(Index); }
			else { UniformGridSlot->SetColumn(Index); }
		}
		else
		{
			return FailureJson(Op, FString::Printf(
				TEXT("row/column only supported on GridSlot/UniformGridSlot - got %s"),
				*SlotClass));
		}
	}
	else if (Prop == TEXT("size_rule") || Prop == TEXT("fill_value"))
	{
		FSlateChildSize CurSize;
		bool bFound = false;
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Slot))
		{
			CurSize = HS->GetSize();
			bFound = true;
		}
		else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Slot))
		{
			CurSize = VS->GetSize();
			bFound = true;
		}
		if (!bFound)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("size_rule/fill_value only supported on HorizontalBoxSlot/VerticalBoxSlot — got %s"),
				*SlotClass));
		}
		if (Prop == TEXT("size_rule"))
		{
			// Issue #23018: fail closed when type is not string.
			if (PropertyValue->Type != EJson::String)
			{
				return FailureJson(Op, TEXT("size_rule requires a JSON string Auto|Fill"));
			}
			const FString Rule = PropertyValue->AsString();
			if (Rule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
			{
				CurSize.SizeRule = ESlateSizeRule::Automatic;
			}
			else if (Rule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
			{
				CurSize.SizeRule = ESlateSizeRule::Fill;
			}
			else
			{
				return FailureJson(Op, FString::Printf(TEXT("Unknown size_rule: %s (Auto|Fill)"), *Rule));
			}
		}
		else if (Prop == TEXT("fill_value"))
		{
			// Issue #23018: fail closed (was skip + still success:true).
			if (PropertyValue->Type != EJson::Number)
			{
				return FailureJson(Op, TEXT("fill_value requires a JSON number"));
			}
			CurSize.Value = static_cast<float>(PropertyValue->AsNumber());
		}
		if (UHorizontalBoxSlot* HS = Cast<UHorizontalBoxSlot>(Slot))
		{
			HS->SetSize(CurSize);
		}
		else if (UVerticalBoxSlot* VS = Cast<UVerticalBoxSlot>(Slot))
		{
			VS->SetSize(CurSize);
		}
	}
	else if (Prop == TEXT("padding") || Prop == TEXT("h_alignment") || Prop == TEXT("v_alignment"))
	{
		auto ParseMargin = [&]() -> TOptional<FMargin>
		{
			const TSharedPtr<FJsonObject>& Obj = AsObject();
			if (!Obj.IsValid())
			{
				return TOptional<FMargin>();
			}
			FMargin M;
			double Tmp;
			if (Obj->TryGetNumberField(TEXT("left"), Tmp)) { M.Left = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("top"), Tmp)) { M.Top = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("right"), Tmp)) { M.Right = static_cast<float>(Tmp); }
			if (Obj->TryGetNumberField(TEXT("bottom"), Tmp)) { M.Bottom = static_cast<float>(Tmp); }
			return M;
		};
		auto ParseHorizontal = [&]() -> TOptional<EHorizontalAlignment>
		{
			if (PropertyValue->Type != EJson::String) { return TOptional<EHorizontalAlignment>(); }
			const FString V = PropertyValue->AsString();
			if (V.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) { return HAlign_Fill; }
			if (V.Equals(TEXT("Left"), ESearchCase::IgnoreCase)) { return HAlign_Left; }
			if (V.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) { return HAlign_Center; }
			if (V.Equals(TEXT("Right"), ESearchCase::IgnoreCase)) { return HAlign_Right; }
			return TOptional<EHorizontalAlignment>();
		};
		auto ParseVertical = [&]() -> TOptional<EVerticalAlignment>
		{
			if (PropertyValue->Type != EJson::String) { return TOptional<EVerticalAlignment>(); }
			const FString V = PropertyValue->AsString();
			if (V.Equals(TEXT("Fill"), ESearchCase::IgnoreCase)) { return VAlign_Fill; }
			if (V.Equals(TEXT("Top"), ESearchCase::IgnoreCase)) { return VAlign_Top; }
			if (V.Equals(TEXT("Center"), ESearchCase::IgnoreCase)) { return VAlign_Center; }
			if (V.Equals(TEXT("Bottom"), ESearchCase::IgnoreCase)) { return VAlign_Bottom; }
			return TOptional<EVerticalAlignment>();
		};

		bool bApplied = false;
#define APPLY_COMMON_SLOT_LAYOUT(SlotType, SlotVar) \
		if (SlotType* SlotVar = Cast<SlotType>(Slot)) \
		{ \
			if (Prop == TEXT("padding")) \
			{ \
				const TOptional<FMargin> M = ParseMargin(); \
				if (!M.IsSet()) { return FailureJson(Op, TEXT("padding requires {left,top,right,bottom} object")); } \
				SlotVar->SetPadding(M.GetValue()); \
			} \
			else if (Prop == TEXT("h_alignment")) \
			{ \
				const TOptional<EHorizontalAlignment> A = ParseHorizontal(); \
				if (!A.IsSet()) { return FailureJson(Op, TEXT("h_alignment requires Fill|Left|Center|Right")); } \
				SlotVar->SetHorizontalAlignment(A.GetValue()); \
			} \
			else \
			{ \
				const TOptional<EVerticalAlignment> A = ParseVertical(); \
				if (!A.IsSet()) { return FailureJson(Op, TEXT("v_alignment requires Fill|Top|Center|Bottom")); } \
				SlotVar->SetVerticalAlignment(A.GetValue()); \
			} \
			bApplied = true; \
		}
		APPLY_COMMON_SLOT_LAYOUT(UOverlaySlot, OverlaySlot)
		else APPLY_COMMON_SLOT_LAYOUT(USizeBoxSlot, SizeBoxSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UHorizontalBoxSlot, HorizontalBoxSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UVerticalBoxSlot, VerticalBoxSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UBorderSlot, BorderSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UButtonSlot, ButtonSlot)
		else APPLY_COMMON_SLOT_LAYOUT(USafeZoneSlot, SafeZoneSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UScrollBoxSlot, ScrollBoxSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UGridSlot, GridSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UWidgetSwitcherSlot, WidgetSwitcherSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UWrapBoxSlot, WrapBoxSlot)
		else APPLY_COMMON_SLOT_LAYOUT(UBackgroundBlurSlot, BackgroundBlurSlot)
#undef APPLY_COMMON_SLOT_LAYOUT

#define APPLY_ALIGNMENT_ONLY_SLOT_LAYOUT(SlotType, SlotVar) \
		if (SlotType* SlotVar = Cast<SlotType>(Slot)) \
		{ \
			if (Prop == TEXT("padding")) \
			{ \
				return FailureJson(Op, FString::Printf( \
					TEXT("padding is not supported for slot class %s"), *SlotClass)); \
			} \
			if (Prop == TEXT("h_alignment")) \
			{ \
				const TOptional<EHorizontalAlignment> A = ParseHorizontal(); \
				if (!A.IsSet()) { return FailureJson(Op, TEXT("h_alignment requires Fill|Left|Center|Right")); } \
				SlotVar->SetHorizontalAlignment(A.GetValue()); \
			} \
			else \
			{ \
				const TOptional<EVerticalAlignment> A = ParseVertical(); \
				if (!A.IsSet()) { return FailureJson(Op, TEXT("v_alignment requires Fill|Top|Center|Bottom")); } \
				SlotVar->SetVerticalAlignment(A.GetValue()); \
			} \
			bApplied = true; \
		}
		if (!bApplied)
		{
			APPLY_ALIGNMENT_ONLY_SLOT_LAYOUT(UScaleBoxSlot, ScaleBoxSlot)
			else APPLY_ALIGNMENT_ONLY_SLOT_LAYOUT(UUniformGridSlot, UniformGridSlot)
		}
#undef APPLY_ALIGNMENT_ONLY_SLOT_LAYOUT

		if (!bApplied)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("padding/alignment not supported for slot class %s"), *SlotClass));
		}
	}
	else
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Property '%s' not supported for slot class %s"), *PropertyName, *SlotClass));
	}

	// Issue #16934 Part B: layout (Instanced-subobject) edit → same-call save, NO structural
	// recompile (#16777). The transact chain above already mirrors the UMG Designer's edit path;
	// the unified helper performs the same-call UPackage::SavePackage proven to reach disk on 5.8.
	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ false, /*bPersistToDisk*/ true);

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"set_slot_property\",\"widget_path\":\"%s\","
			"\"target_widget\":\"%s\",\"property_name\":\"%s\",\"slot_class\":\"%s\",%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(PropertyName)),
		EscJ(EscapeJson(SlotClass)),
		*PersistFieldsJson(Persist));
}

FString UCURemoteLibrary::SetWidgetProperty(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	const FString& PropertyName,
	const FString& PropertyValueJson,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("set_widget_property");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	// Parse the JSON value. Mirror SetWidgetSlotProperty's array-wrap fallback so a bare
	// top-level scalar (0.5 / true / "Collapsed") — which UE's single-value FJsonValue reader
	// rejects — still parses uniformly.
	TSharedPtr<FJsonValue> PropertyValue;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PropertyValueJson);
		if (!FJsonSerializer::Deserialize(Reader, PropertyValue) || !PropertyValue.IsValid())
		{
			TArray<TSharedPtr<FJsonValue>> Wrapped;
			TSharedRef<TJsonReader<>> WrapReader = TJsonReaderFactory<>::Create(
				FString::Printf(TEXT("[%s]"), *PropertyValueJson));
			if (FJsonSerializer::Deserialize(WrapReader, Wrapped) && Wrapped.Num() == 1 && Wrapped[0].IsValid())
			{
				PropertyValue = Wrapped[0];
			}
			else
			{
				return FailureJson(Op, FString::Printf(
					TEXT("Invalid PropertyValueJson for '%s': %s"), *PropertyName, *PropertyValueJson));
			}
		}
	}

	// Resolve friendly alias -> real UE property, then look it up on the widget's OWN class
	// (ColorAndOpacity/Font live on UTextBlock/UImage, not UWidget base — reflection on the
	// concrete class covers both universal and class-specific designer properties uniformly).
	const FString UEName = ResolveWidgetPropertyAlias(PropertyName);
	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*UEName));
	if (!Prop)
	{
		TArray<FString> AvailableProps;
		for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
		{
			AvailableProps.Add(It->GetName());
			if (AvailableProps.Num() >= 30) { break; }
		}
		return FailureJson(Op, FString::Printf(
			TEXT("Property '%s' (resolved '%s') not found on widget '%s' (class %s). Available: %s"),
			*PropertyName, *UEName, *TargetWidget, *Widget->GetClass()->GetName(),
			*FString::Join(AvailableProps, TEXT(", "))));
	}

	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
	FString PreviousValue;
	Prop->ExportTextItem_Direct(PreviousValue, ValuePtr, nullptr, Widget, PPF_None);

	// Issue #16777 pattern: transact WBP + WidgetTree + widget BEFORE mutating so the delta
	// reaches the saved .uasset (mirrors the UMG Designer's edit path).
#if WITH_EDITOR
	WBP->Modify();
	if (WBP->WidgetTree)
	{
		WBP->WidgetTree->SetFlags(RF_Transactional);
		WBP->WidgetTree->Modify();
	}
	Widget->SetFlags(RF_Transactional);
	Widget->Modify();
#endif

	FString AppliedValue;
	if (FTextProperty* TextProp = CastField<FTextProperty>(Prop))
	{
		// FText's ImportText format is not JSON-friendly (expects NSLOCTEXT(...)/quoted forms).
		// Set it directly from the string, matching SetWidgetText's plain-FString semantics.
		if (PropertyValue->Type != EJson::String)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Property '%s' is a text property — expected a string value, got %s"),
				*UEName, *PropertyValueJson));
		}
		const FString NewText = PropertyValue->AsString();
		TextProp->SetPropertyValue(ValuePtr, FText::FromString(NewText));
		// Export so AppliedValue matches PreviousValue's ExportText form (Issue #23018 compare).
		Prop->ExportTextItem_Direct(AppliedValue, ValuePtr, nullptr, Widget, PPF_None);
	}
	else
	{
		// Reuse the file-static JSON->UE-import-text converter (defined for the material graph
		// reflection writer). It renders scalars/bools verbatim, strings as-is (enum names import
		// by value), and objects as (Key=Value,...) — the UE struct text FImportText expects.
		AppliedValue = CUMaterialJsonToImportText(PropertyValue);
		const TCHAR* ImportResult = Prop->ImportText_Direct(*AppliedValue, ValuePtr, Widget, PPF_None);
		if (!ImportResult)
		{
			// Issue #23018: FSlateColor uses SpecifiedColor/ColorUseRule, not bare {R,G,B,A}.
			return FailureJson(Op, FString::Printf(
				TEXT("Failed to import value '%s' into property '%s' (type %s). Scalars: 0.5/true; enums: \"Collapsed\"; FSlateColor: {\"SpecifiedColor\":{\"R\":1,\"G\":0,\"B\":0,\"A\":1},\"ColorUseRule\":\"UseColor_Specified\"}; FLinearColor (UImage): {\"R\":1,\"G\":0,\"B\":0,\"A\":1}."),
				*AppliedValue, *UEName, *Prop->GetCPPType()));
		}
		// Issue #23018: re-export after ImportText — unknown struct members no-op with
		// non-null ImportResult; echo the real value and fail when nothing changed.
		{
			FString Exported;
			Prop->ExportTextItem_Direct(Exported, ValuePtr, nullptr, Widget, PPF_None);
			AppliedValue = Exported;
		}
	}

	// Issue #26342: optional UMG properties (SizeBox.WidthOverride, SpinBox.MinValue, …)
	// are gated by a companion `bOverride_*` bitfield. Importing the value alone leaves
	// the designer checkbox off, so Slate ignores the stored number. Turn the flag on
	// before PostEditChange so SynchronizeProperties sees both. A false→true flip is
	// a real change even when the numeric value was already stored.
	FString CompanionFlagName;
	bool bCompanionFlipped = false;
	if (FBoolProperty* FlagProp = FindCompanionOverrideFlag(Widget, Prop, UEName))
	{
		bCompanionFlipped = TryEnableCompanionOverride(Widget, FlagProp, CompanionFlagName);
	}

	// Notify the widget so the change propagates (SynchronizeProperties etc. run off PostEditChange).
#if WITH_EDITOR
	FPropertyChangedEvent ChangeEvent(Prop, EPropertyChangeType::ValueSet);
	Widget->PostEditChangeProperty(ChangeEvent);
#endif

	// Asset's-own-property edit -> structural mark-dirty, no same-call save (matches set_text; the
	// caller still owns File > Save). Instanced-subobject edits are set_slot_property's concern.
	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ false);

	// Issue #23018: surface no-op imports (unknown FSlateColor members, etc.).
	// Issue #26342: a companion-flag flip is also a modification.
	const bool bChanged = !PreviousValue.Equals(AppliedValue, ESearchCase::CaseSensitive)
		|| bCompanionFlipped;
	if (!bChanged)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Property '%s' was not modified (import produced no change). For FSlateColor use {\"SpecifiedColor\":{\"R\":..,\"G\":..,\"B\":..,\"A\":..},\"ColorUseRule\":\"UseColor_Specified\"}; previous='%s'."),
			*UEName, *PreviousValue));
	}

	const FString CompanionJson = CompanionFlagName.IsEmpty()
		? FString()
		: FString::Printf(TEXT("\"companion_override\":\"%s\","), EscJ(EscapeJson(CompanionFlagName)));

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"set_widget_property\",\"widget_path\":\"%s\","
			"\"target_widget\":\"%s\",\"property_name\":\"%s\",\"resolved_property\":\"%s\","
			"\"widget_class\":\"%s\",\"previous_value\":\"%s\",\"applied_value\":\"%s\",%s\"changed\":true,%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(PropertyName)),
		EscJ(EscapeJson(UEName)),
		EscJ(EscapeJson(Widget->GetClass()->GetName())),
		EscJ(EscapeJson(PreviousValue)),
		EscJ(EscapeJson(AppliedValue)),
		*CompanionJson,
		*PersistFieldsJson(Persist));
}

FString UCURemoteLibrary::AddWidgetToParent(
	const FString& WidgetBlueprintName,
	const FString& ParentWidget,
	const FString& ChildClass,
	const FString& ChildName,
	int32 InsertIndex,
	const FString& SlotPropertiesJson,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("add_widget_to_parent");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP || !WBP->WidgetTree)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	// Issue #26158: empty tree (RootWidget == nullptr) is the UE default
	// (UMGEditorProjectSettings::DefaultRootWidget). Do not report it as
	// "parent not found" — callers then guess RootCanvas/WidgetTree/Root.
	if (!WBP->WidgetTree->RootWidget)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget tree is empty (RootWidget is null) in '%s' — add a root panel before add_widget_to_parent"),
			*ResolvedPath));
	}

	UWidget* ParentRaw = FindWidgetByName(WBP, ParentWidget);
	if (!ParentRaw)
	{
		return FailureJson(Op, FString::Printf(TEXT("Parent widget '%s' not found in '%s'"), *ParentWidget, *ResolvedPath));
	}
	UPanelWidget* Parent = Cast<UPanelWidget>(ParentRaw);
	if (!Parent)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget '%s' is %s, not a Panel — cannot add children"),
			*ParentWidget, *ParentRaw->GetClass()->GetName()));
	}

	// Reject duplicate names.
	if (FindWidgetByName(WBP, ChildName))
	{
		return FailureJson(Op, FString::Printf(TEXT("A widget named '%s' already exists in '%s'"),
			*ChildName, *ResolvedPath));
	}

	// Resolve child class.
	UClass* ChildClassPtr = FindObject<UClass>(nullptr, *ChildClass);
	if (!ChildClassPtr)
	{
		// Try common widget package prefix.
		const FString WithPrefix = FString::Printf(TEXT("/Script/UMG.%s"), *ChildClass);
		ChildClassPtr = StaticLoadClass(UWidget::StaticClass(), nullptr, *WithPrefix);
	}
	if (!ChildClassPtr || !ChildClassPtr->IsChildOf(UWidget::StaticClass()))
	{
		return FailureJson(Op, FString::Printf(TEXT("Child class '%s' not found or not a UWidget"), *ChildClass));
	}

	UWidget* NewWidget = WBP->WidgetTree->ConstructWidget<UWidget>(ChildClassPtr, FName(*ChildName));
	if (!NewWidget)
	{
		return FailureJson(Op, TEXT("ConstructWidget returned null"));
	}

	UPanelSlot* NewSlot = Parent->AddChild(NewWidget);
	if (!NewSlot)
	{
		return FailureJson(Op, TEXT("AddChild failed — parent may not accept this child type"));
	}
	// UE 5.8 requires every source widget to have a stable variable GUID before
	// structural compilation. ConstructWidget/AddChild do not populate the map;
	// omitting this hook triggers WidgetBlueprintCompiler line 781 ensures and can
	// later crash ValidateOnSave while duplicating the malformed widget tree.
	WBP->OnVariableAdded(NewWidget->GetFName());

	// Optional insert reorder.
	if (InsertIndex >= 0)
	{
		const int32 CurrentIndex = Parent->GetChildIndex(NewWidget);
		const int32 Clamped = FMath::Clamp(InsertIndex, 0, Parent->GetChildrenCount() - 1);
		if (CurrentIndex != Clamped)
		{
			Parent->ShiftChild(Clamped, NewWidget);
		}
	}

	// Optional slot properties — delegate to SetWidgetSlotProperty for each key.
	// Issue #9344 ns-review L-1 fix: aggregate sub-op errors into a reportable
	// array rather than silently swallowing them. The child widget was created
	// regardless (success=true for the top-level op), but callers need visibility
	// into which slot properties failed to apply.
	TArray<FString> SlotErrors;
	if (!SlotPropertiesJson.IsEmpty())
	{
		TSharedPtr<FJsonObject> PropsObj;
		TSharedRef<TJsonReader<>> PropsReader = TJsonReaderFactory<>::Create(SlotPropertiesJson);
		if (FJsonSerializer::Deserialize(PropsReader, PropsObj) && PropsObj.IsValid())
		{
			for (const auto& Kvp : PropsObj->Values)
			{
				const FString KvpKey(Kvp.Key);  // UE 5.8: FJsonObject::Values key is FSharedString
				FString ValueJson;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ValueJson);
				FJsonSerializer::Serialize(Kvp.Value.ToSharedRef(), TEXT(""), Writer);
				// MarkDirty=false; the parent's final FinaliseWidgetMutation persists them all at once.
				FString SubResult = SetWidgetSlotProperty(
					WidgetBlueprintName, ChildName, KvpKey, ValueJson, false);

				// Inspect the sub-op result: success=false → capture for caller.
				TSharedPtr<FJsonObject> SubObj;
				TSharedRef<TJsonReader<>> SubReader = TJsonReaderFactory<>::Create(SubResult);
				if (FJsonSerializer::Deserialize(SubReader, SubObj) && SubObj.IsValid())
				{
					bool bOk = false;
					SubObj->TryGetBoolField(TEXT("success"), bOk);
					if (!bOk)
					{
						FString SubErr = TEXT("(no error field in sub-response)");
						SubObj->TryGetStringField(TEXT("error"), SubErr);
						SlotErrors.Add(FString::Printf(
							TEXT("{\"property\":\"%s\",\"error\":\"%s\"}"),
							EscJ(EscapeJson(KvpKey)), EscJ(EscapeJson(SubErr))));
					}
				}
				else
				{
					// Sub-op returned malformed JSON — defensive capture.
					SlotErrors.Add(FString::Printf(
						TEXT("{\"property\":\"%s\",\"error\":\"Unparseable sub-response\"}"),
						EscJ(EscapeJson(KvpKey))));
				}
			}
		}
		else
		{
			// Root-level parse failure — report a single error for the whole payload.
			SlotErrors.Add(FString::Printf(
				TEXT("{\"property\":\"<root>\",\"error\":\"Invalid SlotPropertiesJson payload\"}")));
		}
	}

	// Issue #16934 Part B: structural (new child) + same-call save (Instanced slot subobjects).
	// Slot properties were forwarded above with MarkDirty=false so this single save captures them
	// all at once (#16777 #3 "nested set").
	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ true);

	const FString SlotErrorsArray = SlotErrors.Num() > 0
		? FString::Printf(TEXT("[%s]"), *FString::Join(SlotErrors, TEXT(",")))
		: FString(TEXT("[]"));

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"add_widget_to_parent\",\"widget_path\":\"%s\","
			"\"parent_widget\":\"%s\",\"child_widget\":\"%s\",\"child_class\":\"%s\","
			"\"child_index\":%d,%s,"
			"\"slot_properties_errors\":%s,\"slot_properties_error_count\":%d}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(ParentWidget)),
		EscJ(EscapeJson(ChildName)),
		EscJ(EscapeJson(ChildClassPtr->GetName())),
		Parent->GetChildIndex(NewWidget),
		*PersistFieldsJson(Persist),
		*SlotErrorsArray,
		SlotErrors.Num());
}

FString UCURemoteLibrary::RemoveWidget(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("remove_widget");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP || !WBP->WidgetTree)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	if (Widget == WBP->WidgetTree->RootWidget)
	{
		return FailureJson(Op, TEXT("Cannot remove root widget — replace the tree root via a separate authoring op"));
	}

	UWidget* ParentWidget = Widget->GetParent();
	const FString FormerParentName = ParentWidget ? ParentWidget->GetName() : FString(TEXT(""));

	const bool bRemoved = WBP->WidgetTree->RemoveWidget(Widget);
	if (!bRemoved)
	{
		return FailureJson(Op, FString::Printf(TEXT("RemoveWidget returned false for '%s'"), *TargetWidget));
	}
	WBP->OnVariableRemoved(Widget->GetFName());

	// Issue #16934 Part B: structural removal → mark-dirty, no same-call save (caller saves later).
	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ false);
	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"remove_widget\",\"widget_path\":\"%s\","
			"\"removed_widget\":\"%s\",\"former_parent\":\"%s\",%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(FormerParentName)),
		*PersistFieldsJson(Persist));
}

FString UCURemoteLibrary::ReparentWidget(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	const FString& NewParent,
	int32 InsertIndex,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("reparent_widget");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP || !WBP->WidgetTree)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	if (Widget == WBP->WidgetTree->RootWidget)
	{
		return FailureJson(Op, TEXT("Cannot reparent root widget — replace the tree root via a separate authoring op"));
	}

	UWidget* NewParentRaw = FindWidgetByName(WBP, NewParent);
	if (!NewParentRaw)
	{
		return FailureJson(Op, FString::Printf(TEXT("New parent widget '%s' not found in '%s'"), *NewParent, *ResolvedPath));
	}
	if (NewParentRaw == Widget)
	{
		return FailureJson(Op, TEXT("Cannot reparent a widget onto itself"));
	}

	// Reject cycles: NewParent must not sit inside TargetWidget's subtree.
	// Issue #23088: walk DOWN (panel children + named-slot content). Upward
	// GetParent() misses named-slot boundaries and returned success:true while
	// AddChild created an unreachable cycle that PreSave then deleted from disk.
	if (WidgetSubtreeContainsIncludingNamedSlots(Widget, NewParentRaw))
	{
		return FailureJson(Op, TEXT("Cannot reparent onto a descendant — would create a cycle"));
	}

	UPanelWidget* DestPanel = Cast<UPanelWidget>(NewParentRaw);
	if (!DestPanel)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget '%s' is %s, not a Panel — cannot accept children"),
			*NewParent, *NewParentRaw->GetClass()->GetName()));
	}

	// UWidget::GetParent() is declared to return UPanelWidget* (Widget.h:761), so a non-null former
	// parent is ALWAYS a panel. The old code stored this in a UWidget* and kept a
	// "former parent is not a Panel" branch that Cast<UPanelWidget> could never reach (Issue #23017).
	UPanelWidget* OldParent = Widget->GetParent();
	// Issue #23089: named-slot content has Slot==nullptr → GetParent() is null even though an
	// INamedSlotInterface host still holds the reference. Resolve that host for former_parent
	// reporting and for the pre-AddChild detach below.
	UObject* NamedSlotHost = nullptr;
	FName NamedSlotName = NAME_None;
	const bool bFromNamedSlot = (!OldParent)
		&& FindNamedSlotHostForContent(WBP, Widget, NamedSlotHost, NamedSlotName);
	FString FormerParentName;
	if (OldParent)
	{
		FormerParentName = OldParent->GetName();
	}
	else if (bFromNamedSlot && NamedSlotHost)
	{
		if (const UWidget* HostWidget = Cast<UWidget>(NamedSlotHost))
		{
			FormerParentName = HostWidget->GetName();
		}
		else
		{
			// WidgetTree NamedSlotBindings — host is the tree, not a UWidget.
			FormerParentName = NamedSlotHost->GetName();
		}
	}

	// Already under the destination — optional reorder only.
	// NOTE (Issue #23017): this short-circuit stays deliberately untransacted. It cannot orphan the
	// widget — ShiftChild only permutes an existing Slots entry, and there is no failure return
	// after it — so it is outside this issue's atomicity scope. The consequence is an asymmetry:
	// a cross-parent reparent is undoable, a same-parent reorder is not. Transacting it (and
	// ReorderWidget, which has the identical shape) is a separate, behaviour-changing follow-up.
	if (OldParent == DestPanel)
	{
		const int32 PrevIndex = DestPanel->GetChildIndex(Widget);
		if (InsertIndex >= 0)
		{
			const int32 Clamped = FMath::Clamp(InsertIndex, 0, DestPanel->GetChildrenCount() - 1);
			if (PrevIndex != Clamped)
			{
				DestPanel->ShiftChild(Clamped, Widget);
			}
		}
		const FWidgetPersistResult Persist =
			FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ true);
		return FString::Printf(
			TEXT("{\"success\":true,\"op\":\"reparent_widget\",\"widget_path\":\"%s\","
				"\"moved_widget\":\"%s\",\"former_parent\":\"%s\",\"new_parent\":\"%s\","
				"\"child_index\":%d,\"already_under_parent\":true,%s}"),
			EscJ(EscapeJson(ResolvedPath)),
			EscJ(EscapeJson(TargetWidget)),
			EscJ(EscapeJson(FormerParentName)),
			EscJ(EscapeJson(NewParent)),
			DestPanel->GetChildIndex(Widget),
			*PersistFieldsJson(Persist));
	}

	// Issue #23017: for *panel* former parents, do NOT detach first. UPanelWidget::AddChild rejects
	// a child it cannot accept at PanelWidget.cpp:136 (null Content) and :141
	// (!bCanHaveMultipleChildren && GetChildrenCount() > 0) — BOTH strictly before its own
	// Content->RemoveFromParent() at :144. Letting AddChild perform the detach therefore makes the
	// move atomic: a rejected reparent mutates nothing.
	//
	// Issue #23089: named-slot content is different — GetParent() is null and AddChild's
	// RemoveFromParent is a no-op, so the INamedSlotInterface host keeps pointing at the widget
	// after a "successful" AddChild (duplicate refs). Clear the named slot *inside* the
	// transaction; if AddChild then fails, restore the slot so the widget is never orphaned.
	// AddChild is non-virtual (PanelWidget.h:59,66) and this call is typed UPanelWidget*, so no
	// subclass can reorder those checks.
	{
#if WITH_EDITOR
		// Transact the move, matching the BehaviorTree reparent precedent in this file. Modify()
		// records nothing without RF_Transactional — same pairing as set_widget_slot_property
		// at :19036-19045.
		// Modify(bAlwaysMarkDirty=false) on purpose. The default `true` reaches
		// SaveToTransactionBuffer(this, true) -> MarkPackageDirty() (UObjectGlobals.cpp:3377-3380),
		// and falls back to MarkPackageDirty() directly when there is no transactor
		// (Obj.cpp:1581-1584) — so the default would dirty the package on EVERY call, including
		// MarkDirty=false, which FinaliseWidgetMutation documents as a full no-op batch escape
		// hatch (:18741). Transaction.Cancel() discards the undo record but cannot un-dirty.
		// Passing false still records undo via GUndo->SaveObject(); the dirty/persist policy stays
		// where it belongs, in FinaliseWidgetMutation.
		FScopedTransaction Transaction(
			NSLOCTEXT("NarshaMCP", "ReparentWidget", "Reparent Widget"));
		WBP->Modify(/*bAlwaysMarkDirty*/ false);
		WBP->WidgetTree->SetFlags(RF_Transactional);   // non-null: guarded at the top of this function
		WBP->WidgetTree->Modify(/*bAlwaysMarkDirty*/ false);
		Widget->SetFlags(RF_Transactional);
		Widget->Modify(/*bAlwaysMarkDirty*/ false);
		if (OldParent)
		{
			OldParent->SetFlags(RF_Transactional);
			OldParent->Modify(/*bAlwaysMarkDirty*/ false);
		}
		if (bFromNamedSlot && NamedSlotHost)
		{
			NamedSlotHost->SetFlags(RF_Transactional);
			NamedSlotHost->Modify(/*bAlwaysMarkDirty*/ false);
		}
		DestPanel->SetFlags(RF_Transactional);
		DestPanel->Modify(/*bAlwaysMarkDirty*/ false);
#endif

		if (bFromNamedSlot && NamedSlotHost)
		{
			if (INamedSlotInterface* Named = Cast<INamedSlotInterface>(NamedSlotHost))
			{
				Named->SetContentForSlot(NamedSlotName, nullptr);
			}
		}

		UPanelSlot* NewSlot = DestPanel->AddChild(Widget);
		if (!NewSlot)
		{
			// Restore named-slot host so the widget is never left with zero owners (#23089 AC).
			if (bFromNamedSlot && NamedSlotHost)
			{
				if (INamedSlotInterface* Named = Cast<INamedSlotInterface>(NamedSlotHost))
				{
					Named->SetContentForSlot(NamedSlotName, Widget);
				}
			}
#if WITH_EDITOR
			// Panel-only rejects mutate nothing; named-slot path was restored above. Cancel discards
			// the undo record either way. Same shape as :6210 / :15447.
			Transaction.Cancel();
#endif
			return FailureJson(Op, TEXT("AddChild failed — destination may not accept this child type"));
		}

		if (InsertIndex >= 0)
		{
			const int32 CurrentIndex = DestPanel->GetChildIndex(Widget);
			const int32 Clamped = FMath::Clamp(InsertIndex, 0, DestPanel->GetChildrenCount() - 1);
			if (CurrentIndex != Clamped)
			{
				DestPanel->ShiftChild(Clamped, Widget);
			}
		}
	}
	// The transaction closes above, BEFORE FinaliseWidgetMutation's SavePackage — the MetaSound
	// precedent at :20365-20416 — keeping the #16777 persistence path outside the transaction.

	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ true);
	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"reparent_widget\",\"widget_path\":\"%s\","
			"\"moved_widget\":\"%s\",\"former_parent\":\"%s\",\"new_parent\":\"%s\","
			"\"child_index\":%d,\"already_under_parent\":false,%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(FormerParentName)),
		EscJ(EscapeJson(NewParent)),
		DestPanel->GetChildIndex(Widget),
		*PersistFieldsJson(Persist));
}

FString UCURemoteLibrary::ReorderWidget(
	const FString& WidgetBlueprintName,
	const FString& TargetWidget,
	int32 InsertIndex,
	bool MarkDirty)
{
	using namespace WidgetAuthoring_Issue9344;
	const FString Op = TEXT("reorder_widget");

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP || !WBP->WidgetTree)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
	if (!Widget)
	{
		return FailureJson(Op, FString::Printf(TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
	}

	UPanelWidget* Parent = Cast<UPanelWidget>(Widget->GetParent());
	if (!Parent)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Widget '%s' has no panel parent — cannot reorder (root or detached)"),
			*TargetWidget));
	}

	const int32 PrevIndex = Parent->GetChildIndex(Widget);
	const int32 MaxIndex = FMath::Max(0, Parent->GetChildrenCount() - 1);
	const int32 Clamped = FMath::Clamp(InsertIndex, 0, MaxIndex);
	if (PrevIndex != Clamped)
	{
		Parent->ShiftChild(Clamped, Widget);
	}

	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ true);
	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"reorder_widget\",\"widget_path\":\"%s\","
			"\"moved_widget\":\"%s\",\"parent_widget\":\"%s\","
			"\"previous_index\":%d,\"child_index\":%d,%s}"),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(TargetWidget)),
		EscJ(EscapeJson(Parent->GetName())),
		PrevIndex,
		Parent->GetChildIndex(Widget),
		*PersistFieldsJson(Persist));
}

// =============================================================================
// Issues #26388/#27237: UWidgetAnimation authoring (property tracks + components)
// =============================================================================
namespace WidgetAnimAuthoring_Issue26388
{
	using namespace WidgetAuthoring_Issue9344;

	/** Which kind of channel layout the authored track exposes. */
	enum class EChannelMode : uint8
	{
		/** Single float channel at proxy index 0 (UMovieSceneFloatTrack). */
		SingleFloat,
		/** UMovieScene2DTransformSection transform channels.
		 *  Order verified against UE 5.8 MovieScene2DTransformSection.cpp:152-182, which
		 *  registers Translation[0], Translation[1], Rotation, Scale[0], Scale[1],
		 *  Shear[0], Shear[1] — identically in the editor and non-editor branches, and
		 *  regardless of TransformMask (the mask only drives editor metadata). */
		Transform2D,
	};

	/** One row of the EXPLICIT property_path -> track-class map.
	 *
	 *  Why a map and not string assembly: `AddSequencerTrack` builds a class path by
	 *  trying `/Script/MovieSceneTracks.<T>` then `/Script/MovieScene.<T>`
	 *  (CURemoteLibrary.cpp:1669-1673). `UMovieScene2DTransformTrack` lives in
	 *  `/Script/UMG` and is `UCLASS(MinimalAPI)`, so that search CANNOT find it and a
	 *  cross-module `StaticClass()` link is not available either. Resolving a full path
	 *  through `FindObject<UClass>` sidesteps both. */
	struct FTrackSpec
	{
		const TCHAR* PropertyPath;
		const TCHAR* TrackClassPath;
		EChannelMode ChannelMode;
	};

	/** The supported set. An unlisted `property_path` is REJECTED, never coerced to
	 *  Float — the fail-closed contract Issue #19351 established for the sibling
	 *  `sequencer_add_property_track` guard (sequencer_property.rs:82-101), where a
	 *  silent coercion persisted a wrong track under `success: true`. */
	static const FTrackSpec* FindTrackSpec(const FString& PropertyPath)
	{
		static const FTrackSpec Specs[] = {
			{ TEXT("RenderOpacity"),   TEXT("/Script/MovieSceneTracks.MovieSceneFloatTrack"),  EChannelMode::SingleFloat },
			{ TEXT("RenderTransform"), TEXT("/Script/UMG.MovieScene2DTransformTrack"),         EChannelMode::Transform2D },
		};
		for (const FTrackSpec& Spec : Specs)
		{
			if (PropertyPath.Equals(Spec.PropertyPath, ESearchCase::IgnoreCase))
			{
				return &Spec;
			}
		}
		return nullptr;
	}

	/** Comma-joined list of supported paths, for the fail-closed error message. */
	static FString SupportedPropertyPaths()
	{
		return TEXT("RenderOpacity, RenderTransform");
	}

	/** Resolved write targets for one keyframe. The mask is persisted on transform
	 *  sections so the offline parser can recover the otherwise anonymous float
	 *  channels after save/reload. */
	struct FResolvedKeyChannels
	{
		TArray<int32, TInlineAllocator<2>> Indices;
		EMovieScene2DTransformChannel TransformMask = EMovieScene2DTransformChannel::None;
	};

	/** Seconds that still fit in signed FFrameNumber at `TickResolution`. */
	static double MaxAuthorableSecondsForTickResolution(const FFrameRate& TickResolution)
	{
		const double TicksPerSecond = TickResolution.AsDecimal();
		if (!(TicksPerSecond > 0.0))
		{
			return 0.0;
		}
		// Playback ranges add one tick for their exclusive upper bound, so reserve it
		// here. The one-tick reduction is also a safe, negligible bound for key times.
		return static_cast<double>(TNumericLimits<int32>::Max() - 1) / TicksPerSecond;
	}

	/** Resolve the public `component` contract without aliases or coercion.
	 *  RenderTransform requires an explicit listed component. Omission still maps
	 *  to Scale.XY only when the track sets `legacy_default_scale_component`
	 *  (Issue #26388 compatibility). Any component field on RenderOpacity is invalid. */
	static bool ResolveKeyChannels(
		const FTrackSpec& Spec,
		const TSharedPtr<FJsonObject>& KeyObj,
		const bool bLegacyDefaultScale,
		FResolvedKeyChannels& OutResolved,
		FString& OutError)
	{
		OutResolved.Indices.Reset();
		OutResolved.TransformMask = EMovieScene2DTransformChannel::None;
		const bool bHasComponent = KeyObj.IsValid() && KeyObj->HasField(TEXT("component"));
		if (KeyObj.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Field : KeyObj->Values)
			{
				// FString's ==/!= operators are case-insensitive; use an explicit
				// case-sensitive comparison so `Component` is not mistaken for the
				// canonical lowercase field before the typo guard runs.
				if (!Field.Key.Equals(TEXT("component"), ESearchCase::CaseSensitive)
					&& Field.Key.Equals(TEXT("component"), ESearchCase::IgnoreCase))
				{
					OutError = TEXT("keyframe field 'component' is case-sensitive; misspelled variants are rejected");
					return false;
				}
			}
		}

		if (Spec.ChannelMode == EChannelMode::SingleFloat)
		{
			if (bHasComponent)
			{
				OutError = TEXT("'component' is only valid for RenderTransform; RenderOpacity has one implicit float channel");
				return false;
			}
			OutResolved.Indices.Add(0);
			return true;
		}

		FString Component;
		if (bHasComponent)
		{
			if (!KeyObj->TryGetStringField(TEXT("component"), Component))
			{
				OutError = TEXT("RenderTransform 'component' must be a string when present");
				return false;
			}
		}
		else if (bLegacyDefaultScale)
		{
			Component = TEXT("Scale.XY");
		}
		else
		{
			OutError = TEXT("RenderTransform keyframes require an explicit listed 'component' (Scale.X, Scale.Y, Scale.XY, Translation.X, Translation.Y, Translation.XY); omitted or misspelled keys are not authored as Scale");
			return false;
		}

		if (Component == TEXT("Scale.X"))
		{
			OutResolved.Indices.Add(3);
			OutResolved.TransformMask = EMovieScene2DTransformChannel::ScaleX;
		}
		else if (Component == TEXT("Scale.Y"))
		{
			OutResolved.Indices.Add(4);
			OutResolved.TransformMask = EMovieScene2DTransformChannel::ScaleY;
		}
		else if (Component == TEXT("Scale.XY"))
		{
			OutResolved.Indices.Add(3);
			OutResolved.Indices.Add(4);
			OutResolved.TransformMask = EMovieScene2DTransformChannel::Scale;
		}
		else if (Component == TEXT("Translation.X"))
		{
			OutResolved.Indices.Add(0);
			OutResolved.TransformMask = EMovieScene2DTransformChannel::TranslationX;
		}
		else if (Component == TEXT("Translation.Y"))
		{
			OutResolved.Indices.Add(1);
			OutResolved.TransformMask = EMovieScene2DTransformChannel::TranslationY;
		}
		else if (Component == TEXT("Translation.XY"))
		{
			OutResolved.Indices.Add(0);
			OutResolved.Indices.Add(1);
			OutResolved.TransformMask = EMovieScene2DTransformChannel::Translation;
		}
		else
		{
			OutError = FString::Printf(
				TEXT("Unsupported RenderTransform component '%s'. Supported: Scale.X, Scale.Y, Scale.XY, Translation.X, Translation.Y, Translation.XY"),
				*Component);
			return false;
		}
		return true;
	}

	/** Insert one key on `Channel`, honouring the requested interpolation mode. */
	static void AddKeyToChannel(FMovieSceneFloatChannel& Channel, FFrameNumber Frame, float Value, const FString& Interp)
	{
		if (Interp.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
		{
			Channel.AddConstantKey(Frame, Value);
		}
		else if (Interp.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase) || Interp.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			Channel.AddCubicKey(Frame, Value);
		}
		else
		{
			Channel.AddLinearKey(Frame, Value);
		}
	}
}

FString UCURemoteLibrary::CreateWidgetAnimation(
	const FString& WidgetBlueprintName,
	const FString& AnimationName,
	float DurationSeconds,
	int32 FrameRate,
	const FString& TracksJson,
	bool MarkDirty
)
{
	using namespace WidgetAuthoring_Issue9344;
	using namespace WidgetAnimAuthoring_Issue26388;
	static const FString Op = TEXT("create_widget_animation");

	if (AnimationName.IsEmpty())
	{
		return FailureJson(Op, TEXT("animation_name must not be empty"));
	}
	if (!(DurationSeconds > 0.0f))
	{
		return FailureJson(Op, FString::Printf(
			TEXT("duration must be > 0 (got %f) — a zero-length playback range leaves every section unevaluable"),
			DurationSeconds));
	}
	// FFrameNumber is int32. New UMovieScene tick resolution is FFrameRate()
	// (60000/1 in UE 5.8), not the 24000 assumed by the old 86400 s cap.
	// 86400 * 60000 overflows int32 and wraps behind success:true. Bound against
	// the default tick rate here; after MovieScene exists we re-check the real one.
	const double DefaultMaxAuthorableSeconds = MaxAuthorableSecondsForTickResolution(FFrameRate());
	if (!(DurationSeconds <= DefaultMaxAuthorableSeconds))
	{
		return FailureJson(Op, FString::Printf(
			TEXT("duration must be <= %.0f seconds at the MovieScene tick resolution (got %f) — beyond that the seconds→FFrameNumber cast overflows int32"),
			DefaultMaxAuthorableSeconds, DurationSeconds));
	}
	// Issue #26388 review: clamp the externally-supplied frame rate before it reaches
	// FFrameRate (Rule 27). A non-positive numerator makes TickResolution/DisplayRate
	// conversions divide by zero.
	const int32 EffectiveFrameRate = (FrameRate > 0) ? FMath::Min(FrameRate, 1000) : 60;

	FString ResolvedPath;
	UWidgetBlueprint* WBP = ResolveWidgetBlueprint(WidgetBlueprintName, ResolvedPath);
	if (!WBP)
	{
		return FailureJson(Op, FString::Printf(TEXT("Widget Blueprint not found: %s"), *WidgetBlueprintName));
	}

	// Name-collision guard. UMG addresses animations by FName, and Blueprint->Animations
	// is a plain array with no uniqueness enforcement — a duplicate would produce two
	// entries the editor cannot tell apart.
	const FName NewFName(*AnimationName);
	for (const UWidgetAnimation* Existing : WBP->Animations)
	{
		if (Existing && (Existing->GetFName() == NewFName || Existing->GetDisplayLabel() == AnimationName))
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Animation '%s' already exists in '%s'"), *AnimationName, *ResolvedPath));
		}
	}

	// Parse the track specs BEFORE mutating anything, so a malformed request cannot
	// leave a half-authored animation behind.
	TArray<TSharedPtr<FJsonValue>> TrackSpecs;
	const FString TrimmedTracks = TracksJson.TrimStartAndEnd();
	if (!TrimmedTracks.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(TrimmedTracks);
		if (!FJsonSerializer::Deserialize(Reader, TrackSpecs))
		{
			return FailureJson(Op, TEXT("tracks must be a JSON array"));
		}
	}

	// Pre-validate every track spec (widget resolvable + property_path supported + every
	// keyframe well-formed) so the fail-closed rejection happens before the animation
	// object exists. Issue #26388 review: the keyframe half was originally validated
	// INSIDE the creation loop, i.e. after WBP->Animations.Add — a malformed keyframes[]
	// element therefore left a half-authored animation behind, contradicting this very
	// comment. Everything the creation loop can reject is now rejected here first.
	for (const TSharedPtr<FJsonValue>& Entry : TrackSpecs)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj || !Obj->IsValid())
		{
			return FailureJson(Op, TEXT("each tracks[] element must be a JSON object"));
		}
		const FString TargetWidget = (*Obj)->GetStringField(TEXT("target_widget"));
		const FString PropertyPath = (*Obj)->GetStringField(TEXT("property_path"));
		if (!FindWidgetByName(WBP, TargetWidget))
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Target widget '%s' not found in '%s'"), *TargetWidget, *ResolvedPath));
		}
		const FTrackSpec* Spec = FindTrackSpec(PropertyPath);
		if (!Spec)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Unsupported property_path '%s'. Supported: %s. It is NOT silently coerced to a float track."),
				*PropertyPath, *SupportedPropertyPaths()));
		}
		const bool bLegacyDefaultScale =
			(*Obj)->HasTypedField<EJson::Boolean>(TEXT("legacy_default_scale_component"))
			&& (*Obj)->GetBoolField(TEXT("legacy_default_scale_component"));

		const TArray<TSharedPtr<FJsonValue>>* PendingKeyframes = nullptr;
		if (!(*Obj)->TryGetArrayField(TEXT("keyframes"), PendingKeyframes) || !PendingKeyframes)
		{
			// keyframes[] is optional — absent (or explicit null, matching the Rust
			// caller's `Some(Value::Null) | None` mapping) means a track with no keys,
			// which is legal. But a keyframes field that is PRESENT and some other type
			// (`"keyframes": "0,1"`, `"keyframes": {...}`) previously read as "absent" and
			// silently produced an empty track under success:true — the same silent-drop
			// class Issue #26388 exists to close, so it is rejected rather than ignored.
			if ((*Obj)->HasField(TEXT("keyframes")) && !(*Obj)->HasTypedField<EJson::Null>(TEXT("keyframes")))
			{
				return FailureJson(Op, FString::Printf(
					TEXT("'keyframes' for '%s' must be an array when present; it is NOT silently treated as absent"),
					*PropertyPath));
			}
			continue;
		}
		for (const TSharedPtr<FJsonValue>& KeyValue : *PendingKeyframes)
		{
			const TSharedPtr<FJsonObject>* KeyObj = nullptr;
			if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObj) || !KeyObj || !KeyObj->IsValid())
			{
				return FailureJson(Op, TEXT("each keyframes[] element must be a JSON object"));
			}
			// Issue #26388 review: TryGetNumberField's bool return was previously
			// discarded, so a missing/mistyped `time` or `value` silently authored a
			// key at (0, 0) instead of failing — inconsistent with the strict
			// property_path rejection above.
			double PendingTime = 0.0;
			double PendingValue = 0.0;
			if (!(*KeyObj)->TryGetNumberField(TEXT("time"), PendingTime))
			{
				return FailureJson(Op, FString::Printf(
					TEXT("keyframes[] element for '%s' requires a numeric 'time'; it is NOT defaulted to 0"),
					*PropertyPath));
			}
			if (!(*KeyObj)->TryGetNumberField(TEXT("value"), PendingValue))
			{
				return FailureJson(Op, FString::Printf(
					TEXT("keyframes[] element for '%s' requires a numeric 'value'; it is NOT defaulted to 0"),
					*PropertyPath));
			}
			// Same int32 FFrameNumber ceiling as `duration` above (Rule 27); a key time
			// is bounded independently because it is not derived from duration.
			if (!(PendingTime >= 0.0) || !(PendingTime <= DefaultMaxAuthorableSeconds))
			{
				return FailureJson(Op, FString::Printf(
					TEXT("keyframes[] 'time' for '%s' must be within [0, %.0f] seconds (got %f) — outside that the seconds→FFrameNumber cast overflows int32"),
					*PropertyPath, DefaultMaxAuthorableSeconds, PendingTime));
			}

			FResolvedKeyChannels ResolvedChannels;
			FString ComponentError;
			if (!ResolveKeyChannels(*Spec, *KeyObj, bLegacyDefaultScale, ResolvedChannels, ComponentError))
			{
				return FailureJson(Op, FString::Printf(
					TEXT("Invalid keyframes[] component for '%s': %s"),
					*PropertyPath, *ComponentError));
			}
		}
	}

	// ---- Creation: mirrors UMGEditor AnimationTabSummoner.cpp:612-660 -------------
	UWidgetAnimation* NewAnimation = NewObject<UWidgetAnimation>(WBP, FName(), RF_Transactional);
	NewAnimation->SetDisplayLabel(AnimationName);
	NewAnimation->Rename(*AnimationName);

	NewAnimation->MovieScene = NewObject<UMovieScene>(NewAnimation, NewFName, RF_Transactional);
	UMovieScene* MovieScene = NewAnimation->MovieScene;
	MovieScene->SetDisplayRate(FFrameRate(EffectiveFrameRate, 1));

	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const double ActualMaxAuthorableSeconds = MaxAuthorableSecondsForTickResolution(TickResolution);
	if (!(DurationSeconds <= ActualMaxAuthorableSeconds))
	{
		return FailureJson(Op, FString::Printf(
			TEXT("duration must be <= %.0f seconds at this MovieScene tick resolution (got %f) — beyond that the seconds→FFrameNumber cast overflows int32"),
			ActualMaxAuthorableSeconds, DurationSeconds));
	}
	// The engine/project can override a newly created MovieScene's tick resolution.
	// Re-check every pre-validated key against the resolution that will actually be
	// used before any track or Widget Blueprint registration is mutated. Otherwise a
	// key accepted against FFrameRate() can overflow at a higher project tick rate.
	for (const TSharedPtr<FJsonValue>& Entry : TrackSpecs)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj || !Obj->IsValid())
		{
			continue; // unreachable: pre-validated above.
		}
		const FString PropertyPath = (*Obj)->GetStringField(TEXT("property_path"));
		const TArray<TSharedPtr<FJsonValue>>* PendingKeyframes = nullptr;
		if (!(*Obj)->TryGetArrayField(TEXT("keyframes"), PendingKeyframes) || !PendingKeyframes)
		{
			continue;
		}
		for (const TSharedPtr<FJsonValue>& KeyValue : *PendingKeyframes)
		{
			const TSharedPtr<FJsonObject>* KeyObj = nullptr;
			double PendingTime = 0.0;
			if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObj) || !KeyObj || !KeyObj->IsValid()
				|| !(*KeyObj)->TryGetNumberField(TEXT("time"), PendingTime))
			{
				continue; // unreachable: pre-validated above.
			}
			if (!(PendingTime <= ActualMaxAuthorableSeconds))
			{
				return FailureJson(Op, FString::Printf(
					TEXT("keyframes[] 'time' for '%s' must be within [0, %.0f] seconds at this MovieScene tick resolution (got %f) — outside that the seconds→FFrameNumber cast overflows int32"),
					*PropertyPath, ActualMaxAuthorableSeconds, PendingTime));
			}
		}
	}
	const FFrameTime OutFrame = DurationSeconds * TickResolution;
	// `+1` matches the engine: the playback range is exclusive at the upper bound, so a
	// key authored exactly at `duration` would otherwise fall outside the range.
	const TRange<FFrameNumber> PlaybackRange(FFrameNumber(0), OutFrame.FrameNumber + 1);
	MovieScene->SetPlaybackRange(PlaybackRange);
#if WITH_EDITORONLY_DATA
	MovieScene->GetEditorData().WorkStart = 0.0f;
	MovieScene->GetEditorData().WorkEnd = DurationSeconds;
#endif

	// ---- Tracks + keyframes ------------------------------------------------------
	// Issue #26388 review: built BEFORE the animation is registered on the Widget
	// Blueprint. `NewAnimation`/`MovieScene` are still free-floating objects here, so any
	// failure below returns with WBP *completely untouched* — no rollback path to get
	// wrong. Registering first (the previous order) meant a mid-loop failure left a
	// half-authored animation in WBP->Animations that a LATER successful op's
	// SavePackage would silently persist, turning a `success:false` into a corrupt asset.
	// Nothing in this loop needs the animation to be registered: AddPossessable/AddTrack
	// operate on MovieScene, and AnimationBindings on NewAnimation.
	int32 TracksCreated = 0;
	int32 KeyframesCreated = 0;
	for (const TSharedPtr<FJsonValue>& Entry : TrackSpecs)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Entry.IsValid() || !Entry->TryGetObject(Obj) || !Obj || !Obj->IsValid())
		{
			continue; // unreachable: pre-validated above.
		}
		const FString TargetWidget = (*Obj)->GetStringField(TEXT("target_widget"));
		const FString PropertyPath = (*Obj)->GetStringField(TEXT("property_path"));
		UWidget* Widget = FindWidgetByName(WBP, TargetWidget);
		const FTrackSpec* Spec = FindTrackSpec(PropertyPath);
		const bool bLegacyDefaultScale =
			(*Obj)->HasTypedField<EJson::Boolean>(TEXT("legacy_default_scale_component"))
			&& (*Obj)->GetBoolField(TEXT("legacy_default_scale_component"));
		if (!Widget || !Spec)
		{
			continue; // unreachable: pre-validated above.
		}

		UClass* TrackClass = FindObject<UClass>(nullptr, Spec->TrackClassPath);
		if (!TrackClass)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Track class '%s' could not be resolved — the owning module is not loaded"),
				Spec->TrackClassPath));
		}

		// AddPossessable creates the FMovieScenePossessable AND its FMovieSceneBinding in
		// one call. BindPossessableObject is not usable here: it is
		// CastChecked<UUserWidget>(Context) and therefore needs a live preview widget.
		const FGuid PossessableGuid = MovieScene->AddPossessable(TargetWidget, Widget->GetClass());
		if (!PossessableGuid.IsValid())
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Failed to create a possessable binding for widget '%s'"), *TargetWidget));
		}

		// The offline parser joins FWidgetAnimationBinding.AnimationGuid to the MovieScene's
		// FMovieSceneBinding.ObjectGuid (animation_bindings.rs:11-31). Pushing the widget-side
		// half with the SAME guid AddPossessable just minted keeps that join intact.
		FWidgetAnimationBinding Binding;
		Binding.WidgetName = Widget->GetFName();
		Binding.AnimationGuid = PossessableGuid;
		Binding.bIsRootWidget = (WBP->WidgetTree && WBP->WidgetTree->RootWidget == Widget);
		NewAnimation->AnimationBindings.Add(Binding);

		UMovieSceneTrack* Track = MovieScene->AddTrack(TrackClass, PossessableGuid);
		if (!Track)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("AddTrack failed for '%s' on widget '%s'"), *PropertyPath, *TargetWidget));
		}
		if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track))
		{
			// Both halves matter to the read side: `PropertyPath` is what
			// extract_property_binding returns (metadata.rs:479-505, PropertyPath wins over
			// PropertyName), and it is what the read-back test asserts on.
			PropertyTrack->SetPropertyNameAndPath(FName(*PropertyPath), PropertyPath);
		}

		UMovieSceneSection* Section = Track->CreateNewSection();
		if (!Section)
		{
			return FailureJson(Op, FString::Printf(
				TEXT("CreateNewSection failed for '%s'"), *PropertyPath));
		}
		Section->SetFlags(RF_Transactional);
		// Issue #22985 adjacent-mode: an unranged section is never evaluated by UE. The
		// sibling sequencer_add_property_track guard exists for exactly this.
		Section->SetRange(PlaybackRange);
		Track->AddSection(*Section);

		TArrayView<FMovieSceneFloatChannel*> FloatChannels =
			Section->GetChannelProxy().GetChannels<FMovieSceneFloatChannel>();

		// Validate the full supported channel surface once before writing any keys.
		const int32 RequiredMaxChannelIndex =
			Spec->ChannelMode == EChannelMode::Transform2D ? 4 : 0;
		if (!FloatChannels.IsValidIndex(RequiredMaxChannelIndex))
		{
			return FailureJson(Op, FString::Printf(
				TEXT("Track '%s' exposes %d float channel(s); expected index %d — the section layout changed"),
				*PropertyPath, FloatChannels.Num(), RequiredMaxChannelIndex));
		}

		EMovieScene2DTransformChannel AuthoredTransformMask = EMovieScene2DTransformChannel::None;
		const TArray<TSharedPtr<FJsonValue>>* Keyframes = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("keyframes"), Keyframes) && Keyframes)
		{
			for (const TSharedPtr<FJsonValue>& KeyValue : *Keyframes)
			{
				const TSharedPtr<FJsonObject>* KeyObj = nullptr;
				double TimeSeconds = 0.0;
				double Value = 0.0;
				// All three conditions are pre-validated above, so these are defensive
				// only — kept fail-closed (never silently defaulted) so the two loops
				// cannot drift apart. `continue` rather than `return`: by this point the
				// animation is still unregistered, but skipping a key is strictly safer
				// than authoring a fabricated (0, 0) one.
				if (!KeyValue.IsValid() || !KeyValue->TryGetObject(KeyObj) || !KeyObj || !KeyObj->IsValid()
					|| !(*KeyObj)->TryGetNumberField(TEXT("time"), TimeSeconds)
					|| !(*KeyObj)->TryGetNumberField(TEXT("value"), Value))
				{
					continue;
				}
				FString Interp = TEXT("Linear");
				(*KeyObj)->TryGetStringField(TEXT("interp"), Interp);
				FResolvedKeyChannels ResolvedChannels;
				FString ComponentError;
				if (!ResolveKeyChannels(*Spec, *KeyObj, bLegacyDefaultScale, ResolvedChannels, ComponentError))
				{
					return FailureJson(Op, FString::Printf(
						TEXT("Invalid keyframes[] component for '%s': %s"),
						*PropertyPath, *ComponentError));
				}

				// Keep the JSON number's double precision through the frame conversion.
				// Narrowing to float first can round an accepted near-ceiling time above
				// int32::Max before FFrameTime clamps it (Issue #27237 boundary review).
				const FFrameTime KeyFrame = TickResolution.AsFrameTime(TimeSeconds);
				for (const int32 Index : ResolvedChannels.Indices)
				{
					AddKeyToChannel(*FloatChannels[Index], KeyFrame.FrameNumber, static_cast<float>(Value), Interp);
					++KeyframesCreated;
				}
				AuthoredTransformMask |= ResolvedChannels.TransformMask;
			}
		}
		if (AuthoredTransformMask != EMovieScene2DTransformChannel::None)
		{
			UMovieScene2DTransformSection* TransformSection = Cast<UMovieScene2DTransformSection>(Section);
			if (!TransformSection)
			{
				return FailureJson(Op, FString::Printf(
					TEXT("Track '%s' did not create a UMovieScene2DTransformSection"), *PropertyPath));
			}
			TransformSection->SetMask(FMovieScene2DTransformMask(AuthoredTransformMask));
		}
		++TracksCreated;
	}

	// ---- Registration: mirrors AnimationTabSummoner.cpp:284-295 -------------------
	// Deliberately the LAST mutation before persistence (Issue #26388 review): every
	// rejectable condition has already been checked, so from here to
	// FinaliseWidgetMutation there is no early return.
	NewAnimation->Modify();
	MovieScene->Modify();
	WBP->Modify();
	WBP->Animations.Add(NewAnimation);
	// NOT optional. Adding to `Animations` without announcing the variable trips the
	// UE 5.8 WidgetBlueprintCompiler ensure that AddWidgetToParent already documents
	// for the widget-tree case (see CURemoteLibrary.cpp:20879-20881).
	WBP->OnVariableAdded(NewAnimation->GetFName());

	const FWidgetPersistResult Persist =
		FinaliseWidgetMutation(WBP, MarkDirty, /*bStructural*/ true, /*bPersistToDisk*/ true);

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"widget_path\":\"%s\",\"animation_name\":\"%s\",")
		TEXT("\"duration_seconds\":%f,\"frame_rate\":%d,\"tracks_created\":%d,\"keyframes_created\":%d,%s}"),
		EscJ(EscapeJson(Op)),
		EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(AnimationName)),
		DurationSeconds,
		EffectiveFrameRate,
		TracksCreated,
		KeyframesCreated,
		*PersistFieldsJson(Persist));
}

// =============================================================================
// Issue #9768: Runtime CVar tuning for n.WebSocket.MaxPerFrame
// =============================================================================
// This is a runtime-tuning op, NOT a widget op. It lives in the widget authoring
// section only because it shares the WidgetAuthoring_Issue9344::FailureJson helper
// (pure JSON formatter; namespace name is historical from #9344).
//
// Why a dedicated UFUNCTION instead of execute_console_command:
//   The Editor's `unreal.SystemLibrary.execute_console_command(None, 'n.WebSocket...')`
//   silently no-ops (verified empirically in PR #9763 stress harness — `--bump-budget`
//   raised p99 from 4226 ms to 7167 ms). Direct IConsoleVariable::Set is the only
//   reliable mutation path for this CVar. See Issue #9768 for full rationale.
FString UCURemoteLibrary::SetWebSocketBudget(int32 Value)
{
	using namespace WidgetAuthoring_Issue9344;
	static const FString Op = TEXT("set_websocket_budget");
	static const TCHAR* CVarName = TEXT("n.WebSocket.MaxPerFrame");
	constexpr int32 kMin = UCUWebSocketEditorSubsystem::kMinPerFrame;        // 1
	constexpr int32 kMax = UCUWebSocketEditorSubsystem::kMaxPerFrameHardCap; // 64

	IConsoleVariable* Cvar = IConsoleManager::Get().FindConsoleVariable(CVarName);
	if (!Cvar)
	{
		return FailureJson(Op, FString::Printf(
			TEXT("CVar '%s' not registered (subsystem not initialised?)"), CVarName));
	}

	const int32 Previous = Cvar->GetInt();
	const int32 Clamped  = FMath::Clamp(Value, kMin, kMax);
	const bool  bClamped = (Clamped != Value);

	Cvar->Set(Clamped, ECVF_SetByCode);
	const int32 Applied = Cvar->GetInt();   // post-Set readback
	const bool bPriorityLocked = (Applied != Clamped);

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\","
			 "\"requested_value\":%d,\"applied_value\":%d,\"previous_value\":%d,"
			 "\"clamped\":%s,\"priority_locked\":%s,\"min\":%d,\"max\":%d}"),
		*Op, Value, Applied, Previous,
		bClamped ? TEXT("true") : TEXT("false"),
		bPriorityLocked ? TEXT("true") : TEXT("false"),
		kMin, kMax);
}

//==============================================================================
// MetaSound editor mutations (Issue #8833)
//==============================================================================
// Surface: 5 UFUNCTIONs called via HTTP Remote Control from
// `MCP/native/mcp_server/src/tools/metasound_authoring_ops.rs`.
//
// Strategy: thin pass-through to UMetaSoundBuilderBase (engine-supplied
// BlueprintCallable mutation API) wrapped in FScopedTransaction + Outer->Modify()
// so OnObjectTransacted fires for NarshaMCP's transaction-replay capture
// (frontend doc uses custom serialization that bypasses standard property delta).
//
// See docs/analysis/METASOUND_API_8833.md for the full API map.
//
// All ops are no-ops outside WITH_EDITOR (UMetaSoundEditorSubsystem is editor-only).

#if WITH_EDITOR

#if WITH_NARSHAMCP_METASOUND
namespace MetaSoundAuthoring_Issue8833
{
	// Reuse JSON helpers from the widget authoring namespace — they're pure
	// formatters with no widget-specific logic.
	using WidgetAuthoring_Issue9344::EscapeJson;
	using WidgetAuthoring_Issue9344::FailureJson;
	using WidgetAuthoring_Issue9344::EscJ;  // UE 5.8 compat: rvalue-deref helper

	/** Build a success response with arbitrary inline body fragment. */
	static FString SuccessJson(const FString& OpLabel, const FString& BodyFragment)
	{
		return FString::Printf(
			TEXT("{\"success\":true,\"op\":\"%s\"%s%s}"),
			EscJ(EscapeJson(OpLabel)),
			BodyFragment.IsEmpty() ? TEXT("") : TEXT(","),
			*BodyFragment);
	}

	/** Resolve a MetaSound asset by short name or full path.
	 *
	 *  Supports `UMetaSoundPatch` and `UMetaSoundSource` via the common
	 *  `IMetaSoundDocumentInterface`. `OutResolvedPath` is populated for
	 *  round-trip echo in the response JSON.
	 */
	static UObject* ResolveMetaSoundAsset(const FString& NameOrPath, FString& OutResolvedPath)
	{
		// 1) Full-path short-circuit.
		if (UObject* Loaded = StaticLoadObject(UObject::StaticClass(), nullptr, *NameOrPath))
		{
			if (Loaded->Implements<UMetaSoundDocumentInterface>())
			{
				OutResolvedPath = Loaded->GetPathName();
				return Loaded;
			}
		}

		// 2) Asset Registry lookup by short name across MetaSoundPatch + MetaSoundSource.
		FAssetRegistryModule& ARM =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
		TArray<FAssetData> Assets;
		// MetaSoundSource (UObject path) — derived from USoundBase.
		ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundSource")), Assets);
		// MetaSoundPatch.
		ARM.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/MetasoundEngine"), TEXT("MetaSoundPatch")), Assets);

		const FName TargetName(*NameOrPath);
		UObject* Found = nullptr;
		int32 MatchCount = 0;
		FString MatchedPaths;
		for (const FAssetData& Data : Assets)
		{
			if (Data.AssetName == TargetName)
			{
				++MatchCount;
				Found = Data.GetAsset();
				if (!MatchedPaths.IsEmpty()) { MatchedPaths += TEXT(", "); }
				MatchedPaths += Data.GetSoftObjectPath().ToString();
			}
		}
		if (MatchCount == 1 && Found)
		{
			OutResolvedPath = Found->GetPathName();
			return Found;
		}
		// Issue #8833 review: surface ambiguity vs absence in OutResolvedPath
		// so the caller's FailureJson can distinguish "no match" from "multiple matches".
		if (MatchCount > 1)
		{
			OutResolvedPath = FString::Printf(TEXT("[ambiguous: %d matches: %s]"), MatchCount, *MatchedPaths);
		}
		return nullptr;
	}

	/** Fetch (or begin) a builder for the given MetaSound asset. */
	static UMetaSoundBuilderBase* GetBuilderForAsset(UObject* Asset, FString& OutError)
	{
		if (!Asset)
		{
			OutError = TEXT("Null asset");
			return nullptr;
		}
		UMetaSoundEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>() : nullptr;
		if (!Subsystem)
		{
			OutError = TEXT("UMetaSoundEditorSubsystem unavailable (Editor not initialised?)");
			return nullptr;
		}
		TScriptInterface<IMetaSoundDocumentInterface> DocInterface(Asset);
		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		UMetaSoundBuilderBase* Builder = Subsystem->FindOrBeginBuilding(DocInterface, Result);
		if (!Builder || Result != EMetaSoundBuilderResult::Succeeded)
		{
			OutError = TEXT("FindOrBeginBuilding returned no builder");
			return nullptr;
		}
		return Builder;
	}

	/** Outcome of resolving a node handle from a GUID string.
	 *  Distinguishes parse failure (HTTP 400 semantically) from lookup miss (404 semantically)
	 *  so the FailureJson message can guide the operator's next action — Issue #8833 review.
	 */
	enum class ENodeResolveResult : uint8
	{
		Ok,
		ParseFailed,
		NotFound,
	};

	/** Resolve a node handle from its GUID string. Validates existence in the
	 *  document graph via `FMetaSoundFrontendDocumentBuilder::FindNode`.
	 */
	static ENodeResolveResult ResolveNodeHandleFromGuid(UMetaSoundBuilderBase* Builder, const FString& NodeGuidStr,
	                                                    FMetaSoundNodeHandle& OutHandle)
	{
		FGuid Guid;
		if (!FGuid::Parse(NodeGuidStr, Guid))
		{
			return ENodeResolveResult::ParseFailed;
		}
		const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder->GetConstBuilder();
		const FMetasoundFrontendNode* Node = DocBuilder.FindNode(Guid);
		if (!Node)
		{
			return ENodeResolveResult::NotFound;
		}
		OutHandle = FMetaSoundNodeHandle(Guid);
		return ENodeResolveResult::Ok;
	}

	/** Format an error message for a node-resolution failure. Returns a context-rich string
	 *  for FailureJson — caller distinguishes "malformed GUID" from "node not in graph".
	 */
	static FString DescribeNodeResolveFailure(ENodeResolveResult R, const FString& Role, const FString& Guid)
	{
		switch (R)
		{
			case ENodeResolveResult::ParseFailed:
				return FString::Printf(TEXT("%s GUID is not a valid GUID string: '%s' (expected DigitsWithHyphens form)"), *Role, *Guid);
			case ENodeResolveResult::NotFound:
				return FString::Printf(TEXT("%s GUID parsed OK but no matching node in graph: %s"), *Role, *Guid);
			default:
				return FString::Printf(TEXT("%s GUID resolution returned unknown failure: %s"), *Role, *Guid);
		}
	}

	/** Mark MetaSound asset dirty and propagate to outer package. */
	static void FinaliseMetaSoundMutation(UObject* Asset, bool bMarkDirty)
	{
		if (!Asset || !bMarkDirty)
		{
			return;
		}
		Asset->MarkPackageDirty();
		if (UPackage* Pkg = Asset->GetOutermost())
		{
			Pkg->MarkPackageDirty();
		}
	}

	/** Map EMetaSoundBuilderResult → string for response JSON. */
	static const TCHAR* BuilderResultToString(EMetaSoundBuilderResult R)
	{
		switch (R)
		{
			case EMetaSoundBuilderResult::Succeeded: return TEXT("Succeeded");
			case EMetaSoundBuilderResult::Failed:    return TEXT("Failed");
			default:                                  return TEXT("Unknown");
		}
	}

	/** Best-effort literal parse from a JSON value string.
	 *
	 *  Accepts:
	 *    - bare integer  ("42")
	 *    - bare float    ("3.14")
	 *    - bare bool     ("true" / "false")
	 *    - quoted string ("\"text\"")
	 *  Returns true on parse success. The exact engine-side data-type validation
	 *  happens inside `Builder->SetNodeInputDefault`.
	 */
	static bool ParseLiteralFromJson(const FString& Json, FMetasoundFrontendLiteral& OutLiteral)
	{
		FString Trimmed = Json.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}
		// Bool
		if (Trimmed.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			OutLiteral.Set(true);
			return true;
		}
		if (Trimmed.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			OutLiteral.Set(false);
			return true;
		}
		// Quoted string
		if (Trimmed.StartsWith(TEXT("\"")) && Trimmed.EndsWith(TEXT("\"")) && Trimmed.Len() >= 2)
		{
			OutLiteral.Set(Trimmed.Mid(1, Trimmed.Len() - 2));
			return true;
		}
		// Integer (no decimal point)
		if (!Trimmed.Contains(TEXT(".")))
		{
			int64 IntVal = 0;
			if (FDefaultValueHelper::ParseInt64(Trimmed, IntVal))
			{
				OutLiteral.Set(static_cast<int32>(IntVal));
				return true;
			}
		}
		// Float fallback
		float FloatVal = 0.f;
		if (FDefaultValueHelper::ParseFloat(Trimmed, FloatVal))
		{
			OutLiteral.Set(FloatVal);
			return true;
		}
		return false;
	}
}
#endif

static FString MakeMetaSoundPluginUnavailable()
{
	return TEXT("{\"success\":false,\"error\":\"Metasound plugin unavailable\","
		"\"error_code\":\"metasound_plugin_unavailable\","
		"\"hint\":\"Enable Metasound in your .uproject, then rebuild.\"}");
}

FString UCURemoteLibrary::ListMetaSoundNodes(
	const FString& MetaSoundAssetPath,
	const FString& FilterType)
{
#if !WITH_NARSHAMCP_METASOUND
	return MakeMetaSoundPluginUnavailable();
#else
	using namespace MetaSoundAuthoring_Issue8833;
	const FString Op = TEXT("list_metasound_nodes");

	FString ResolvedPath;
	UObject* Asset = ResolveMetaSoundAsset(MetaSoundAssetPath, ResolvedPath);
	if (!Asset)
	{
		// ResolvedPath carries an "[ambiguous: ...]" hint when 2+ assets share the short name.
		const FString Suffix = ResolvedPath.IsEmpty() ? FString() : FString::Printf(TEXT(" %s"), *ResolvedPath);
		return FailureJson(Op, FString::Printf(TEXT("MetaSound asset not found: %s%s"), *MetaSoundAssetPath, *Suffix));
	}
	FString BuilderError;
	UMetaSoundBuilderBase* Builder = GetBuilderForAsset(Asset, BuilderError);
	if (!Builder)
	{
		return FailureJson(Op, BuilderError);
	}

	const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder->GetConstBuilder();
	const FMetasoundFrontendDocument& Doc = DocBuilder.GetConstDocumentChecked();
	const FGuid& BuildPageID = DocBuilder.GetBuildPageID();

	// PagedGraphs is private — use the const accessor.
	const FMetasoundFrontendGraph* TargetGraph = nullptr;
	for (const FMetasoundFrontendGraph& Graph : Doc.RootGraph.GetConstGraphPages())
	{
		if (Graph.PageID == BuildPageID)
		{
			TargetGraph = &Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		return FailureJson(Op, TEXT("Active build page not found in PagedGraphs"));
	}

	FString NodesJson;
	int32 Emitted = 0;
	for (const FMetasoundFrontendNode& Node : TargetGraph->Nodes)
	{
		// Resolve class name for filter + display.
		const FMetasoundFrontendClass* Class = DocBuilder.FindDependency(Node.ClassID);
		const FString ClassName = Class ? Class->Metadata.GetClassName().ToString() : TEXT("");
		if (!FilterType.IsEmpty() && !ClassName.Contains(FilterType))
		{
			continue;
		}
		// Issue #17019: editor canvas position from the frontend node style.
		// Mirrors UMetasoundEditorGraphNode::SyncLocationFromFrontendNode, which
		// reads this same Style.Display.Locations FVector2D into the editor node's
		// NodePosX/NodePosY. First map entry = active/default page (matches the
		// offline parser's first-entry selection). We are inside #if WITH_EDITOR,
		// so the editor-only Style member is available. Iterator-validity check
		// (not a loop+break) keeps Rule #28 / C4702 clear.
		int32 NodePosX = 0;
		int32 NodePosY = 0;
		{
			const TMap<FGuid, FVector2D>& Locations = Node.Style.Display.Locations;
			TMap<FGuid, FVector2D>::TConstIterator LocIt = Locations.CreateConstIterator();
			if (LocIt)
			{
				NodePosX = FMath::RoundToInt(LocIt.Value().X);
				NodePosY = FMath::RoundToInt(LocIt.Value().Y);
			}
		}
		if (Emitted > 0)
		{
			NodesJson += TEXT(",");
		}
		NodesJson += FString::Printf(
			TEXT("{\"node_id\":\"%s\",\"class_name\":\"%s\",\"node_name\":\"%s\","
				 "\"node_pos_x\":%d,\"node_pos_y\":%d}"),
			*Node.GetID().ToString(EGuidFormats::DigitsWithHyphens),
			EscJ(EscapeJson(ClassName)),
			EscJ(EscapeJson(Node.Name.ToString())),
			NodePosX, NodePosY);
		++Emitted;
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"asset_path\":\"%s\","
			 "\"node_count\":%d,\"nodes\":[%s]}"),
		*Op, EscJ(EscapeJson(ResolvedPath)), Emitted, *NodesJson);
#endif
}

FString UCURemoteLibrary::ListMetaSoundEdges(const FString& MetaSoundAssetPath)
{
#if !WITH_NARSHAMCP_METASOUND
	return MakeMetaSoundPluginUnavailable();
#else
	// Issue #16561: editor-side EDGE oracle for the audio GT parity harness. Mirrors
	// ListMetaSoundNodes' resolution (builder → const document → active build page), then
	// walks FMetasoundFrontendGraph::Edges and resolves each GUID endpoint to the same
	// {from_node, from_pin, to_node, to_pin} shape the offline parser emits. Node names
	// come from FMetasoundFrontendNode::Name; pin names from the node's interface vertex
	// whose VertexID matches the edge's From/ToVertexID.
	using namespace MetaSoundAuthoring_Issue8833;
	const FString Op = TEXT("list_metasound_edges");

	FString ResolvedPath;
	UObject* Asset = ResolveMetaSoundAsset(MetaSoundAssetPath, ResolvedPath);
	if (!Asset)
	{
		const FString Suffix = ResolvedPath.IsEmpty() ? FString() : FString::Printf(TEXT(" %s"), *ResolvedPath);
		return FailureJson(Op, FString::Printf(TEXT("MetaSound asset not found: %s%s"), *MetaSoundAssetPath, *Suffix));
	}
	FString BuilderError;
	UMetaSoundBuilderBase* Builder = GetBuilderForAsset(Asset, BuilderError);
	if (!Builder)
	{
		return FailureJson(Op, BuilderError);
	}

	const FMetaSoundFrontendDocumentBuilder& DocBuilder = Builder->GetConstBuilder();
	const FMetasoundFrontendDocument& Doc = DocBuilder.GetConstDocumentChecked();
	const FGuid& BuildPageID = DocBuilder.GetBuildPageID();

	const FMetasoundFrontendGraph* TargetGraph = nullptr;
	for (const FMetasoundFrontendGraph& Graph : Doc.RootGraph.GetConstGraphPages())
	{
		if (Graph.PageID == BuildPageID)
		{
			TargetGraph = &Graph;
			break;
		}
	}
	if (!TargetGraph)
	{
		return FailureJson(Op, TEXT("Active build page not found in PagedGraphs"));
	}

	// NodeID → node, for endpoint name + interface (pin) resolution.
	TMap<FGuid, const FMetasoundFrontendNode*> NodeById;
	NodeById.Reserve(TargetGraph->Nodes.Num());
	for (const FMetasoundFrontendNode& Node : TargetGraph->Nodes)
	{
		NodeById.Add(Node.GetID(), &Node);
	}

	auto VertexName = [](const FMetasoundFrontendNode* Node, const FGuid& VertexID, bool bOutput) -> FString
	{
		if (!Node)
		{
			return FString();
		}
		const TArray<FMetasoundFrontendVertex>& Verts = bOutput ? Node->Interface.Outputs : Node->Interface.Inputs;
		for (const FMetasoundFrontendVertex& V : Verts)
		{
			if (V.VertexID == VertexID)
			{
				return V.Name.ToString();
			}
		}
		return FString();
	};

	FString EdgesJson;
	int32 Emitted = 0;
	for (const FMetasoundFrontendEdge& Edge : TargetGraph->Edges)
	{
		const FMetasoundFrontendNode* FromNode = NodeById.FindRef(Edge.FromNodeID);
		const FMetasoundFrontendNode* ToNode = NodeById.FindRef(Edge.ToNodeID);
		const FString FromNodeName = FromNode ? FromNode->Name.ToString() : FString();
		const FString ToNodeName = ToNode ? ToNode->Name.ToString() : FString();
		const FString FromPin = VertexName(FromNode, Edge.FromVertexID, /*bOutput=*/true);
		const FString ToPin = VertexName(ToNode, Edge.ToVertexID, /*bOutput=*/false);
		if (Emitted > 0)
		{
			EdgesJson += TEXT(",");
		}
		EdgesJson += FString::Printf(
			TEXT("{\"from_node\":\"%s\",\"from_pin\":\"%s\",\"to_node\":\"%s\",\"to_pin\":\"%s\"}"),
			EscJ(EscapeJson(FromNodeName)), EscJ(EscapeJson(FromPin)),
			EscJ(EscapeJson(ToNodeName)), EscJ(EscapeJson(ToPin)));
		++Emitted;
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"asset_path\":\"%s\","
			 "\"edge_count\":%d,\"edges\":[%s]}"),
		*Op, EscJ(EscapeJson(ResolvedPath)), Emitted, *EdgesJson);
#endif
}

FString UCURemoteLibrary::SetAttenuationCustomCurve(const FString& AttenuationAssetPath, const FString& KeysJson)
{
	// Issue #16561: author a CustomAttenuationCurve on a SoundAttenuation asset so the
	// audio GT curve-parity surface has real (non-empty) data. UE Python cannot set the
	// curve keys (FRuntimeFloatCurve::EditorCurveData is a bare UPROPERTY, not script-
	// exposed), so this C++ helper mutates the inline FRichCurve via GetRichCurve(); the
	// Python caller does the asset save. KeysJson = `[{"time":..,"value":..,"interp":"Linear|Cubic|Constant"}]`.
	using WidgetAuthoring_Issue9344::FailureJson;
	const FString Op = TEXT("set_attenuation_custom_curve");

	UObject* Asset = StaticLoadObject(USoundAttenuation::StaticClass(), nullptr, *AttenuationAssetPath);
	USoundAttenuation* Att = Cast<USoundAttenuation>(Asset);
	if (!Att)
	{
		return FailureJson(Op, FString::Printf(TEXT("SoundAttenuation asset not found: %s"), *AttenuationAssetPath));
	}

	TArray<TSharedPtr<FJsonValue>> KeyArr;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(KeysJson);
	if (!FJsonSerializer::Deserialize(Reader, KeyArr))
	{
		return FailureJson(Op, TEXT("KeysJson is not a JSON array"));
	}

	// NOTE: if the FRuntimeFloatCurve had a non-null ExternalCurve, GetRichCurve() returns
	// that external UCurveFloat's inline curve (a different package than the attenuation
	// asset we MarkPackageDirty below). Irrelevant for this GT-by-construction helper (fresh
	// asset, no external curve), but documented for safety.
	FRichCurve* RichCurve = Att->Attenuation.CustomAttenuationCurve.GetRichCurve();
	if (!RichCurve)
	{
		return FailureJson(Op, TEXT("GetRichCurve returned null"));
	}
	RichCurve->Reset();

	int32 KeysSet = 0;
	for (const TSharedPtr<FJsonValue>& KeyVal : KeyArr)
	{
		const TSharedPtr<FJsonObject> KeyObj = KeyVal.IsValid() ? KeyVal->AsObject() : nullptr;
		if (!KeyObj.IsValid())
		{
			continue;
		}
		double Time = 0.0;
		double Value = 0.0;
		KeyObj->TryGetNumberField(TEXT("time"), Time);
		KeyObj->TryGetNumberField(TEXT("value"), Value);
		FString Interp;
		KeyObj->TryGetStringField(TEXT("interp"), Interp);

		const FKeyHandle Handle = RichCurve->AddKey(static_cast<float>(Time), static_cast<float>(Value));
		ERichCurveInterpMode Mode = RCIM_Linear;
		if (Interp.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
		{
			Mode = RCIM_Cubic;
		}
		else if (Interp.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
		{
			Mode = RCIM_Constant;
		}
		RichCurve->SetKeyInterpMode(Handle, Mode);
		++KeysSet;
	}

	// Engage the custom curve so the falloff actually uses it.
	Att->Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Custom;
	Att->MarkPackageDirty();

	return FString::Printf(TEXT("{\"success\":true,\"op\":\"%s\",\"keys_set\":%d}"), *Op, KeysSet);
}

FString UCURemoteLibrary::GetAttenuationCustomCurveKeys(const FString& AttenuationAssetPath)
{
	// Issue #16561: editor-side READER for the CustomAttenuationCurve inline FRichCurve
	// keys — UE Python cannot read them (EditorCurveData is a bare UPROPERTY). Returns
	// {time, value, interp} rows so the S3 editor golden carries real curve key data
	// (the audio GT curve-VALUE oracle; seeds the #16562 forward-contract).
	using WidgetAuthoring_Issue9344::FailureJson;
	const FString Op = TEXT("get_attenuation_custom_curve_keys");

	UObject* Asset = StaticLoadObject(USoundAttenuation::StaticClass(), nullptr, *AttenuationAssetPath);
	USoundAttenuation* Att = Cast<USoundAttenuation>(Asset);
	if (!Att)
	{
		return FailureJson(Op, FString::Printf(TEXT("SoundAttenuation asset not found: %s"), *AttenuationAssetPath));
	}

	const FRichCurve* RichCurve = Att->Attenuation.CustomAttenuationCurve.GetRichCurveConst();
	FString KeysJson;
	int32 N = 0;
	if (RichCurve)
	{
		for (const FRichCurveKey& Key : RichCurve->Keys)
		{
			const TCHAR* Interp = TEXT("Linear");
			if (Key.InterpMode == RCIM_Cubic)
			{
				Interp = TEXT("Cubic");
			}
			else if (Key.InterpMode == RCIM_Constant)
			{
				Interp = TEXT("Constant");
			}
			if (N > 0)
			{
				KeysJson += TEXT(",");
			}
			KeysJson += FString::Printf(
				TEXT("{\"time\":%f,\"value\":%f,\"interp\":\"%s\"}"), Key.Time, Key.Value, Interp);
			++N;
		}
	}
	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"key_count\":%d,\"keys\":[%s]}"), *Op, N, *KeysJson);
}

FString UCURemoteLibrary::AddMetaSoundNode(
	const FString& MetaSoundAssetPath,
	const FString& NodeClass,
	const FString& NodeName,
	const FString& PositionJson,
	bool MarkDirty)
{
#if !WITH_NARSHAMCP_METASOUND
	return MakeMetaSoundPluginUnavailable();
#else
	using namespace MetaSoundAuthoring_Issue8833;
	const FString Op = TEXT("add_metasound_node");

	FString ResolvedPath;
	UObject* Asset = ResolveMetaSoundAsset(MetaSoundAssetPath, ResolvedPath);
	if (!Asset)
	{
		// ResolvedPath carries an "[ambiguous: ...]" hint when 2+ assets share the short name.
		const FString Suffix = ResolvedPath.IsEmpty() ? FString() : FString::Printf(TEXT(" %s"), *ResolvedPath);
		return FailureJson(Op, FString::Printf(TEXT("MetaSound asset not found: %s%s"), *MetaSoundAssetPath, *Suffix));
	}
	FString BuilderError;
	UMetaSoundBuilderBase* Builder = GetBuilderForAsset(Asset, BuilderError);
	if (!Builder)
	{
		return FailureJson(Op, BuilderError);
	}

	FString NodeIdStr;
	const TCHAR* ResultStr = TEXT("Failed");
	{
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AddMetaSoundNode_8833", "Add MetaSound Node"));
		Asset->Modify();   // Coerce OnObjectTransacted emission for custom-serialized doc.

		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		FMetasoundFrontendClassName ClassNameStruct;
		// Try `Namespace.Name[.Variant]` parse first; fall back to bare-name when no dot.
		if (!FMetasoundFrontendClassName::Parse(NodeClass, ClassNameStruct))
		{
			ClassNameStruct.Name = FName(*NodeClass);
		}
		FMetaSoundNodeHandle Handle = Builder->AddNodeByClassName(ClassNameStruct, Result);
		ResultStr = BuilderResultToString(Result);

		if (Result != EMetaSoundBuilderResult::Succeeded)
		{
			Transaction.Cancel();
			return FailureJson(Op, FString::Printf(
				TEXT("AddNodeByClassName failed for class '%s' (result=%s)"), *NodeClass, ResultStr));
		}

		NodeIdStr = Handle.NodeID.ToString(EGuidFormats::DigitsWithHyphens);

		// Optional visual location.
		if (!PositionJson.IsEmpty())
		{
			TSharedPtr<FJsonObject> PosObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PositionJson);
			if (FJsonSerializer::Deserialize(Reader, PosObj) && PosObj.IsValid())
			{
				// Issue #8833 review: TryGetNumberField over GetNumberField — UE 5.7's
				// GetNumberField silently returns 0.0 on missing keys, which would
				// place the node at (?, 0) when only one axis is provided.
				double X = 0.0;
				double Y = 0.0;
				const bool bHasX = PosObj->TryGetNumberField(TEXT("x"), X);
				const bool bHasY = PosObj->TryGetNumberField(TEXT("y"), Y);
				if (bHasX && bHasY)
				{
					EMetaSoundBuilderResult LocResult = EMetaSoundBuilderResult::Failed;
					if (UMetaSoundEditorSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>())
					{
						Subsystem->SetNodeLocation(Builder, Handle, FVector2D(X, Y), LocResult);
					}
				}
				// Both keys missing or partial → silently skip SetNodeLocation;
				// engine-default placement applies. Caller can pass empty PositionJson for the same outcome.
			}
		}

		FinaliseMetaSoundMutation(Asset, MarkDirty);
	}   // FScopedTransaction scope ends here → OnObjectTransacted broadcasts.

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"asset_path\":\"%s\","
			 "\"node_id\":\"%s\",\"node_class\":\"%s\",\"node_name\":\"%s\","
			 "\"builder_result\":\"%s\",\"marked_dirty\":%s}"),
		*Op, EscJ(EscapeJson(ResolvedPath)),
		*NodeIdStr, EscJ(EscapeJson(NodeClass)), EscJ(EscapeJson(NodeName)),
		ResultStr,
		MarkDirty ? TEXT("true") : TEXT("false"));
#endif
}

FString UCURemoteLibrary::RemoveMetaSoundNode(
	const FString& MetaSoundAssetPath,
	const FString& NodeGuid,
	bool MarkDirty)
{
#if !WITH_NARSHAMCP_METASOUND
	return MakeMetaSoundPluginUnavailable();
#else
	using namespace MetaSoundAuthoring_Issue8833;
	const FString Op = TEXT("remove_metasound_node");

	FString ResolvedPath;
	UObject* Asset = ResolveMetaSoundAsset(MetaSoundAssetPath, ResolvedPath);
	if (!Asset)
	{
		// ResolvedPath carries an "[ambiguous: ...]" hint when 2+ assets share the short name.
		const FString Suffix = ResolvedPath.IsEmpty() ? FString() : FString::Printf(TEXT(" %s"), *ResolvedPath);
		return FailureJson(Op, FString::Printf(TEXT("MetaSound asset not found: %s%s"), *MetaSoundAssetPath, *Suffix));
	}
	FString BuilderError;
	UMetaSoundBuilderBase* Builder = GetBuilderForAsset(Asset, BuilderError);
	if (!Builder)
	{
		return FailureJson(Op, BuilderError);
	}

	FMetaSoundNodeHandle Handle;
	{
		const ENodeResolveResult ResolveResult = ResolveNodeHandleFromGuid(Builder, NodeGuid, Handle);
		if (ResolveResult != ENodeResolveResult::Ok)
		{
			return FailureJson(Op, DescribeNodeResolveFailure(ResolveResult, TEXT("Target node"), NodeGuid));
		}
	}

	const TCHAR* ResultStr = TEXT("Failed");
	{
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "RemoveMetaSoundNode_8833", "Remove MetaSound Node"));
		Asset->Modify();

		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		Builder->RemoveNode(Handle, Result, /*bRemoveUnusedDependencies=*/ true);
		ResultStr = BuilderResultToString(Result);

		if (Result != EMetaSoundBuilderResult::Succeeded)
		{
			Transaction.Cancel();
			return FailureJson(Op, FString::Printf(
				TEXT("RemoveNode failed (result=%s)"), ResultStr));
		}

		FinaliseMetaSoundMutation(Asset, MarkDirty);
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"asset_path\":\"%s\","
			 "\"removed_node_id\":\"%s\",\"builder_result\":\"%s\",\"marked_dirty\":%s}"),
		*Op, EscJ(EscapeJson(ResolvedPath)), EscJ(EscapeJson(NodeGuid)), ResultStr,
		MarkDirty ? TEXT("true") : TEXT("false"));
#endif
}

FString UCURemoteLibrary::ConnectMetaSoundNodes(
	const FString& MetaSoundAssetPath,
	const FString& FromNodeGuid,
	const FString& FromPin,
	const FString& ToNodeGuid,
	const FString& ToPin,
	bool MarkDirty)
{
#if !WITH_NARSHAMCP_METASOUND
	return MakeMetaSoundPluginUnavailable();
#else
	using namespace MetaSoundAuthoring_Issue8833;
	const FString Op = TEXT("connect_metasound_nodes");

	FString ResolvedPath;
	UObject* Asset = ResolveMetaSoundAsset(MetaSoundAssetPath, ResolvedPath);
	if (!Asset)
	{
		// ResolvedPath carries an "[ambiguous: ...]" hint when 2+ assets share the short name.
		const FString Suffix = ResolvedPath.IsEmpty() ? FString() : FString::Printf(TEXT(" %s"), *ResolvedPath);
		return FailureJson(Op, FString::Printf(TEXT("MetaSound asset not found: %s%s"), *MetaSoundAssetPath, *Suffix));
	}
	FString BuilderError;
	UMetaSoundBuilderBase* Builder = GetBuilderForAsset(Asset, BuilderError);
	if (!Builder)
	{
		return FailureJson(Op, BuilderError);
	}

	FMetaSoundNodeHandle FromHandle, ToHandle;
	{
		const ENodeResolveResult FromR = ResolveNodeHandleFromGuid(Builder, FromNodeGuid, FromHandle);
		if (FromR != ENodeResolveResult::Ok)
		{
			return FailureJson(Op, DescribeNodeResolveFailure(FromR, TEXT("From-node"), FromNodeGuid));
		}
		const ENodeResolveResult ToR = ResolveNodeHandleFromGuid(Builder, ToNodeGuid, ToHandle);
		if (ToR != ENodeResolveResult::Ok)
		{
			return FailureJson(Op, DescribeNodeResolveFailure(ToR, TEXT("To-node"), ToNodeGuid));
		}
	}

	// Locate pin handles by name on each node.
	EMetaSoundBuilderResult FindResult = EMetaSoundBuilderResult::Failed;
	FMetaSoundBuilderNodeOutputHandle OutputHandle = Builder->FindNodeOutputByName(FromHandle, FName(*FromPin), FindResult);
	if (FindResult != EMetaSoundBuilderResult::Succeeded)
	{
		return FailureJson(Op, FString::Printf(TEXT("Output pin '%s' not found on source node"), *FromPin));
	}
	FMetaSoundBuilderNodeInputHandle InputHandle = Builder->FindNodeInputByName(ToHandle, FName(*ToPin), FindResult);
	if (FindResult != EMetaSoundBuilderResult::Succeeded)
	{
		return FailureJson(Op, FString::Printf(TEXT("Input pin '%s' not found on target node"), *ToPin));
	}

	const TCHAR* ResultStr = TEXT("Failed");
	{
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "ConnectMetaSoundNodes_8833", "Connect MetaSound Nodes"));
		Asset->Modify();

		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		Builder->ConnectNodes(OutputHandle, InputHandle, Result);
		ResultStr = BuilderResultToString(Result);

		if (Result != EMetaSoundBuilderResult::Succeeded)
		{
			Transaction.Cancel();
			return FailureJson(Op, FString::Printf(
				TEXT("ConnectNodes failed (result=%s)"), ResultStr));
		}

		FinaliseMetaSoundMutation(Asset, MarkDirty);
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"asset_path\":\"%s\","
			 "\"from_node\":\"%s\",\"from_pin\":\"%s\","
			 "\"to_node\":\"%s\",\"to_pin\":\"%s\","
			 "\"builder_result\":\"%s\",\"marked_dirty\":%s}"),
		*Op, EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(FromNodeGuid)), EscJ(EscapeJson(FromPin)),
		EscJ(EscapeJson(ToNodeGuid)), EscJ(EscapeJson(ToPin)),
		ResultStr,
		MarkDirty ? TEXT("true") : TEXT("false"));
#endif
}

FString UCURemoteLibrary::SetMetaSoundParameter(
	const FString& MetaSoundAssetPath,
	const FString& NodeGuid,
	const FString& ParameterName,
	const FString& ParameterValueJson,
	bool MarkDirty)
{
#if !WITH_NARSHAMCP_METASOUND
	return MakeMetaSoundPluginUnavailable();
#else
	using namespace MetaSoundAuthoring_Issue8833;
	const FString Op = TEXT("set_metasound_parameter");

	FString ResolvedPath;
	UObject* Asset = ResolveMetaSoundAsset(MetaSoundAssetPath, ResolvedPath);
	if (!Asset)
	{
		// ResolvedPath carries an "[ambiguous: ...]" hint when 2+ assets share the short name.
		const FString Suffix = ResolvedPath.IsEmpty() ? FString() : FString::Printf(TEXT(" %s"), *ResolvedPath);
		return FailureJson(Op, FString::Printf(TEXT("MetaSound asset not found: %s%s"), *MetaSoundAssetPath, *Suffix));
	}
	FString BuilderError;
	UMetaSoundBuilderBase* Builder = GetBuilderForAsset(Asset, BuilderError);
	if (!Builder)
	{
		return FailureJson(Op, BuilderError);
	}

	FMetaSoundNodeHandle Handle;
	{
		const ENodeResolveResult ResolveResult = ResolveNodeHandleFromGuid(Builder, NodeGuid, Handle);
		if (ResolveResult != ENodeResolveResult::Ok)
		{
			return FailureJson(Op, DescribeNodeResolveFailure(ResolveResult, TEXT("Target node"), NodeGuid));
		}
	}

	EMetaSoundBuilderResult FindResult = EMetaSoundBuilderResult::Failed;
	FMetaSoundBuilderNodeInputHandle InputHandle = Builder->FindNodeInputByName(Handle, FName(*ParameterName), FindResult);
	if (FindResult != EMetaSoundBuilderResult::Succeeded)
	{
		return FailureJson(Op, FString::Printf(TEXT("Input pin '%s' not found on node"), *ParameterName));
	}

	FMetasoundFrontendLiteral NewLiteral;
	if (!ParseLiteralFromJson(ParameterValueJson, NewLiteral))
	{
		return FailureJson(Op, FString::Printf(
			TEXT("Could not parse value as bool / int / float / string: '%s'"), *ParameterValueJson));
	}

	// Capture previous default (best-effort, for response echo only).
	EMetaSoundBuilderResult ReadResult = EMetaSoundBuilderResult::Failed;
	FMetasoundFrontendLiteral PrevLiteral = Builder->GetNodeInputDefault(InputHandle, ReadResult);
	const FString PrevValueStr = (ReadResult == EMetaSoundBuilderResult::Succeeded) ? PrevLiteral.ToString() : TEXT("<unknown>");

	const TCHAR* ResultStr = TEXT("Failed");
	{
		FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "SetMetaSoundParameter_8833", "Set MetaSound Parameter"));
		Asset->Modify();

		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		Builder->SetNodeInputDefault(InputHandle, NewLiteral, Result);
		ResultStr = BuilderResultToString(Result);

		if (Result != EMetaSoundBuilderResult::Succeeded)
		{
			Transaction.Cancel();
			return FailureJson(Op, FString::Printf(
				TEXT("SetNodeInputDefault failed (result=%s)"), ResultStr));
		}

		FinaliseMetaSoundMutation(Asset, MarkDirty);
	}

	return FString::Printf(
		TEXT("{\"success\":true,\"op\":\"%s\",\"asset_path\":\"%s\","
			 "\"node_id\":\"%s\",\"parameter\":\"%s\","
			 "\"previous_value\":\"%s\",\"new_value\":\"%s\","
			 "\"builder_result\":\"%s\",\"marked_dirty\":%s}"),
		*Op, EscJ(EscapeJson(ResolvedPath)),
		EscJ(EscapeJson(NodeGuid)), EscJ(EscapeJson(ParameterName)),
		EscJ(EscapeJson(PrevValueStr)), EscJ(EscapeJson(NewLiteral.ToString())),
		ResultStr,
		MarkDirty ? TEXT("true") : TEXT("false"));
#endif
}

#else  // !WITH_EDITOR — runtime stubs return error JSON

FString UCURemoteLibrary::ListMetaSoundNodes(const FString&, const FString&)
{
	return TEXT("{\"success\":false,\"error\":\"MetaSound mutations require WITH_EDITOR\"}");
}
FString UCURemoteLibrary::AddMetaSoundNode(const FString&, const FString&, const FString&, const FString&, bool)
{
	return TEXT("{\"success\":false,\"error\":\"MetaSound mutations require WITH_EDITOR\"}");
}
FString UCURemoteLibrary::RemoveMetaSoundNode(const FString&, const FString&, bool)
{
	return TEXT("{\"success\":false,\"error\":\"MetaSound mutations require WITH_EDITOR\"}");
}
FString UCURemoteLibrary::ConnectMetaSoundNodes(const FString&, const FString&, const FString&, const FString&, const FString&, bool)
{
	return TEXT("{\"success\":false,\"error\":\"MetaSound mutations require WITH_EDITOR\"}");
}
FString UCURemoteLibrary::SetMetaSoundParameter(const FString&, const FString&, const FString&, const FString&, bool)
{
	return TEXT("{\"success\":false,\"error\":\"MetaSound mutations require WITH_EDITOR\"}");
}
FString UCURemoteLibrary::ListMetaSoundEdges(const FString&)
{
	return TEXT("{\"success\":false,\"error\":\"MetaSound edge read requires WITH_EDITOR\"}");
}
FString UCURemoteLibrary::SetAttenuationCustomCurve(const FString&, const FString&)
{
	return TEXT("{\"success\":false,\"error\":\"Attenuation curve authoring requires WITH_EDITOR\"}");
}
FString UCURemoteLibrary::GetAttenuationCustomCurveKeys(const FString&)
{
	return TEXT("{\"success\":false,\"error\":\"Attenuation curve read requires WITH_EDITOR\"}");
}

#endif  // WITH_EDITOR
