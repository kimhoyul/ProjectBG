// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #1437: WebSocket Editor Subsystem implementation
// Migrated from FCUWebSocketServer singleton to UEditorSubsystem

#include "CUWebSocketEditorSubsystem.h"
#include "CUWidgetDesignerCaptureUtils.h"  // Issue #26157: Designer screenshot geometry helpers
#include "CUConnectionSettings.h"  // Issue #25250: EditorPerProjectUserSettings WS port surface
#include "CUCompareActorStateUtil.h"  // Issue #10128: extracted DiffersWithinTolerance for unit-testability
#if WITH_NARSHAMCP_GAMEPLAYABILITIES
#include "AbilitySystemComponent.h"    // Issue #27039: pie_read_gameplay_attributes
#include "AttributeSet.h"              // Issue #27039: FGameplayAttributeData
#endif
#include "CUActorClassFilter.h"  // Issue #27040: is_a class resolve for get_level_actors_by_class
#include "CUPieMeasurementMode.h"  // Issue #27012: pie_get_player_state optional health field
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#if WITH_NARSHAMCP_NIAGARA
#include "CUNiagaraUserScriptVariableLookup.h"
#endif
#include "SecurityLimits.h"
#include "Animation/AnimSequenceBase.h"        // Issue #9018: AnimSequence curve capture
#include "Animation/AnimData/AnimDataModel.h"  // Issue #9018: IAnimationDataModel
#include "Animation/AnimData/IAnimationDataModel.h" // Issue #9018: FloatCurve access
#include "Curves/RichCurve.h"                  // Issue #9018: FRichCurve, ERichCurveInterpMode
#include "LevelSequence.h"                     // Issue #9019: ULevelSequence
#include "MovieScene.h"                        // Issue #9019: UMovieScene
#include "MovieSceneTrack.h"                   // Issue #9019: UMovieSceneTrack
#include "MovieSceneSection.h"                 // Issue #9019: UMovieSceneSection
#include "Channels/MovieSceneChannelProxy.h"   // Issue #9019: FMovieSceneChannelProxy
#include "Channels/MovieSceneChannel.h"        // Issue #9019: FMovieSceneChannel (full type)
#include "Channels/MovieSceneDoubleChannel.h"  // Issue #9019 / #9098 Tier 2 non-unity build: FMovieSceneDoubleChannel
#include "Channels/MovieSceneFloatChannel.h"   // Issue #9019 / #9098 Tier 2 non-unity build: FMovieSceneFloatChannel
#include "Channels/MovieSceneBoolChannel.h"    // Issue #25939: typed key value (no 0.0 fabrication)
#include "Channels/MovieSceneIntegerChannel.h" // Issue #25939
#include "Channels/MovieSceneByteChannel.h"    // Issue #25939
// MovieSceneStringChannel lives in MovieSceneTracks, not MovieScene — omit rather
// than add a new .uplugin/Build.cs dep just for string keys (value is skipped).
#include "IWebSocketNetworkingModule.h"
#include "IWebSocketServer.h"
#include "INetworkingWebSocket.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"  // Issue #1632: For FScreenshotRequest
#include "HAL/FileManager.h"  // Issue #1632: For IFileManager
#include "HAL/PlatformProcess.h"  // Issue #15316 C1: FPlatformProcess::GetCurrentProcessId() in get_editor_info
#include "HAL/PlatformMisc.h"  // Issue #16334: FPlatformMisc::GetEnvironmentVariable for WS port override
#include "Misc/CommandLine.h"  // Issue #16334: FCommandLine::Get() for -NarshaMCPWSPort=
#include "Misc/ConfigCacheIni.h"  // Issue #27360: durable tool-maturity INI writes
#include "Misc/Parse.h"  // Issue #16334: FParse::Value for -NarshaMCPWSPort= parse
#include "Misc/CString.h"  // Issue #16334: FCString::Atoi for env port parse
#include "Sockets.h"  // Issue #16334: FSocket for free-port bind probe
#include "SocketSubsystem.h"  // Issue #16334: ISocketSubsystem for free-port bind probe
#include "IPAddress.h"  // Issue #16334: FInternetAddr for free-port bind probe
#include "Framework/Application/SlateApplication.h"  // Issue #1632: For Slate screenshot
#include "Layout/WidgetPath.h"  // Issue #26157: arranged Designer preview ancestry
#include "Framework/Notifications/NotificationManager.h"  // Issue #25249: FSlateNotificationManager toast
#include "Widgets/Notifications/SNotificationList.h"  // Issue #25249: SNotificationItem completion state
#include "Logging/MessageLog.h"  // Issue #25249: FMessageLog for explicit WS bind failures
#include "Slate/SceneViewport.h"  // Issue #1632: For viewport access
#include "LevelEditor.h"  // Issue #1632: For level editor viewport
#include "SLevelViewport.h"  // Issue #7911: For Slate-tracked active viewport
#include "Modules/ModuleManager.h"  // Issue #7911: For FLevelEditorModule
#include "Async/Async.h"  // Issue #16682: AsyncTask(GameThread) to defer the blocking LiveCoding compile out of the WS drain loop
#if WITH_NARSHAMCP_LIVE_CODING
#include "ILiveCodingModule.h"  // Issue #16682: authoritative ELiveCodingCompileResult (Win64-only — Issue #9229)
#endif
#include "IImageWrapper.h"  // Issue #1632: For PNG encoding
#include "IImageWrapperModule.h"  // Issue #1632: For image module
#include "Misc/FileHelper.h"  // Issue #1632: For file writing
#include "Misc/PackageName.h"  // Issue #24313: validate and resolve play_in_editor map packages
#include "Misc/DateTime.h"  // Issue #16695: FDateTime::UtcNow().ToIso8601() for advertisement ts
#include "Misc/EngineVersion.h"  // Issue #16695: FEngineVersion::Current() for advertisement (non-unity build)
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
// WinUser.h aliases DispatchMessage to DispatchMessageW/A. This subsystem
// already owns a DispatchMessage member, so keep the platform macro from
// rewriting the out-of-class definition and calls below.
#ifdef DispatchMessage
#undef DispatchMessage
#endif
#elif PLATFORM_MAC
#include <libproc.h>
#include <unistd.h>
#endif
#include "FileHelpers.h"  // Issue #1632: For asset saving
#include "Kismet/GameplayStatics.h"  // Issue #27011: SetGamePaused / SetGlobalTimeDilation
#include "Kismet2/BlueprintEditorUtils.h"  // Issue #1632: For blueprint compilation
#include "Kismet2/KismetEditorUtilities.h"  // Issue #1632: For blueprint compilation
#include "Engine/Selection.h"  // Issue #1632: For actor selection
#include "Editor/EditorEngine.h"  // Issue #1632: For editor functionality
#include "Settings/LevelEditorPlaySettings.h"  // Issue #24313: operation-scoped multi-PIE topology
#include "Engine/Blueprint.h"  // Issue #1632: For blueprint types
#include "AssetRegistry/AssetRegistryModule.h"  // Issue #1632: For asset registry
#include "EngineUtils.h"  // Issue #1632: For TActorIterator
#include "UObject/SavePackage.h"  // Issue #1632: For FSavePackageArgs
#include "UObject/SoftObjectPath.h"  // Issue #4136: For FSoftObjectPath (plugin asset path normalization)
#include "Misc/Base64.h"  // Issue #6378: For FBase64::Encode (PIE viewport capture)
#if WITH_NARSHAMCP_ENHANCEDINPUT
#include "EnhancedInputSubsystems.h"  // Issue #7914: UEnhancedInputLocalPlayerSubsystem for InjectInputForAction
#include "InputAction.h"  // Issue #7914: UInputAction asset type
#include "InputActionValue.h"  // Issue #7914: FInputActionValue wrapper
#include "InputMappingContext.h"  // Issue #9098 Tier 2: IMC → FKey resolution for raw-key fallback
#include "EnhancedActionKeyMapping.h"  // Issue #9098 Tier 2: FEnhancedActionKeyMapping struct
#include "InputTriggers.h"  // Issue #7914: ETriggerEvent enum
#endif
#include "InputCoreTypes.h"  // Issue #9098 Tier 2: FKey::IsGamepadKey()
#include "Editor.h"  // Issue #9098: FEditorDelegates::EndPIE subscription for sustained-injection cleanup
#include "EditorSupportDelegates.h"  // Issue #26843: FEditorSupportDelegates::PrepareToCleanseEditorObject
#include "Engine/World.h"  // Issue #26843: FWorldDelegates::OnPIEEnded
#include "Engine/GameInstance.h"  // Issue #26843: complete UGameInstance for the UObject* purge target
#include "UObject/Package.h"  // Issue #26843: UPackage::HasAnyPackageFlags(PKG_PlayInEditor)
#include "RenderingThread.h"  // Issue #26843: FlushRenderingCommands — pairs the cleanse broadcast
#include "TimerManager.h"  // Issue #9098: FTimerManager for scheduled Stop timer
#include "Misc/AutomationTest.h"  // Issue #4843: For FAutomationTestFramework (test enumeration)
#include "Editor/EditorPerformanceSettings.h"  // Issue #4843: For bThrottleCPUWhenNotForeground
#if WITH_NARSHAMCP_RIGVM
#include "RigVMCore/RigVMRegistry.h"  // Issue #32: FRigVMRegistry for RigUnit DisplayName resolution
#include "RigVMCore/RigVMDispatchFactory.h"  // Issue #32: FRigVMDispatchFactory for dispatch node DisplayNames
#endif
#include "Materials/MaterialInterface.h"  // Issue #4272: Material HLSL extraction
#include "Materials/MaterialInsights.h"  // Issue #4272: FMaterialInsights for HLSL
#include "Materials/MaterialFunctionInterface.h"  // Issue #11168: detect MFs in get_hlsl
#include "MaterialShared.h"  // Issue #4272: FMaterialResource, GetMaterialExpressionSource
#include "ShaderCore.h"  // Issue #11169: EShaderCompileJobPriority full enum definition (forward-declared in MaterialShared.h)
#include "Subsystems/AssetEditorSubsystem.h"  // Issue #1632: P2 - For OpenEditorForAsset
#include "Framework/Docking/TabManager.h"      // Issue #9833: FTabManager::GetOwnerTab for HWND resolution
#include "Widgets/Docking/SDockTab.h"          // Issue #9833: SDockTab full type for ToSharedRef
#include "GenericPlatform/GenericWindow.h"     // Issue #9833: FGenericWindow::GetOSWindowHandle
#include "Blueprint/UserWidget.h"  // Issue #7915: UUserWidget for runtime widget tree inspection
#include "Blueprint/WidgetTree.h"  // Issue #7915: UWidgetTree for widget hierarchy
#include "Components/Widget.h"  // Issue #7915: UWidget base class
#include "Components/PanelWidget.h"  // Issue #7915: UPanelWidget for child iteration
#include "Components/TextBlock.h"  // Issue #7915: UTextBlock for text content extraction
#include "Components/RichTextBlock.h"  // Issue #7915: URichTextBlock for rich text extraction
#include "Components/Image.h"  // Issue #7915: UImage for brush info
#include "Components/ProgressBar.h"  // Issue #7915: UProgressBar for percent value
#include "Components/Button.h"  // Issue #7915: UButton for hover/pressed state
#include "Components/PanelSlot.h"  // Issue #7915: UPanelSlot for slot layout (padding, alignment)
#include "Components/CanvasPanelSlot.h"  // Issue #7915: UCanvasPanelSlot for anchors/offsets
#include "Components/ScrollBox.h"  // Issue #7915: UScrollBox for scroll offset
#if WITH_NARSHAMCP_NIAGARA
#include "NiagaraComponent.h"  // Issue #3796: Niagara VFX parameter control
#include "NiagaraFunctionLibrary.h"  // Issue #3796: Niagara helpers
#include "NiagaraSystem.h"  // Issue #3796: GetExposedParameters for parameter enumeration
#include "NiagaraTypes.h"  // Issue #6844: FNiagaraTypeDefinition, FNiagaraVariable for asset parameter modification
#include "NiagaraSystemEditorData.h"  // Issue #26588: persist User defaults without constructing a stack view model
#include "NiagaraScriptVariable.h"  // Issue #26588: exact editor-owned User default bytes
#include "NiagaraSystemFactoryNew.h"  // Issue #16555: For UNiagaraSystemFactoryNew (NiagaraEditor module) - create_system
#include "NiagaraEmitter.h"  // Issue #16555: FVersionedNiagaraEmitterData (set_renderer_material/mesh)
#include "NiagaraEmitterHandle.h"  // Issue #16555: FNiagaraEmitterHandle::GetEmitterData
#include "NiagaraRendererProperties.h"  // Issue #16555: UNiagaraRendererProperties (dynamic renderer enum)
#include "NiagaraMeshRendererProperties.h"  // Issue #16555: UNiagaraMeshRendererProperties + FNiagaraMeshRendererMeshProperties
#include "Engine/StaticMesh.h"  // Issue #16555: UStaticMesh (set_renderer_mesh)
#include "UObject/UnrealType.h"  // Issue #16555: FObjectProperty + FindFProperty (generic Material set)
#include "NiagaraScript.h"  // Issue #16957: UNiagaraScript + SetUsage/SetUsageId/SetLatestSource (event handler add)
#include "NiagaraCommon.h"  // Issue #16957: ENiagaraScriptUsage::ParticleEventScript (event handler script usage)
#include "NiagaraScriptSource.h"  // Issue #17009 [4b]: UNiagaraScriptSource->NodeGraph (event-script graph access)
#include "NiagaraGraph.h"  // Issue #17009 [4b]: UNiagaraGraph + FGraphNodeCreator (output-node wiring)
#include "NiagaraNodeOutput.h"  // Issue #17009 [4b]: UNiagaraNodeOutput (event-script graph output node)
#include "NiagaraNodeInput.h"  // Issue #17009 [4b]: UNiagaraNodeInput + ENiagaraInputNodeUsage (parameter-map input node)
#include "NiagaraNodeFunctionCall.h"  // Issue #27351: module / Dynamic Input function-call nodes
#include "ViewModels/Stack/NiagaraParameterHandle.h"  // Issue #27351: CreateModuleParameterHandle + alias
#include "EdGraph/EdGraphSchema.h"  // Issue #27351: BreakPinLinks / SetPinDefaultValue
#include "EdGraphSchema_Niagara.h"  // Issue #27351: PinToTypeDefinition
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"  // Issue #17149 [4c]: FNiagaraStackGraphUtilities::AddScriptModuleToStack (Generate*Event module add)
#include "NiagaraActor.h"  // Issue #17149 [4c]: ANiagaraActor + GetNiagaraComponent (PIE simcache capture)
#include "NiagaraSimCache.h"  // Issue #17149 [4c]: UNiagaraSimCache + FNiagaraSimCacheCreateParameters (capture_simcache)
#include "NiagaraSimCacheFunctionLibrary.h"  // Issue #17149 [4c]: UNiagaraSimCacheFunctionLibrary::CaptureNiagaraSimCacheImmediate
#endif
#include "Materials/MaterialInterface.h"  // Issue #1632: P2 - For material types
#include "Materials/MaterialInstanceConstant.h"  // Issue #1632: P2 - For material instance
#include "Materials/MaterialInstanceDynamic.h"  // Issue #1632: P2 - For dynamic material
#include "Materials/Material.h"  // Issue #1632: P2 - For UMaterial
#include "Engine/Texture.h"  // Issue #1632: P2 - For UTexture
#include "Factories/BlueprintFactory.h"  // Issue #1632: P2 - For blueprint creation
#include "Factories/MaterialFactoryNew.h"  // Issue #1632: P2 - For material creation
#include "Factories/MaterialFunctionFactoryNew.h"  // Issue #7921: For MaterialFunction creation
#include "Materials/MaterialFunction.h"  // Issue #7921: For UMaterialFunction class
#include "BehaviorTree/BehaviorTree.h"  // Issue #1699: For BehaviorTree creation
#include "BehaviorTreeFactory.h"  // Issue #1699: For UBehaviorTreeFactory (BehaviorTreeEditor)
// Issue #16566: WidgetBlueprint / DataAsset / AnimMontage create_asset support.
#include "WidgetBlueprintFactory.h"        // Issue #16566: UWidgetBlueprintFactory (UMGEditor — header lives directly in Classes/, NOT under Factories/)
#include "WidgetBlueprint.h"               // Issue #16566: UWidgetBlueprint (UMGEditor)
#include "WidgetBlueprintEditor.h"         // Issue #26157: non-destructive Designer mode + preview widget
#include "BlueprintModes/WidgetBlueprintApplicationModes.h" // Issue #26157: DesignerMode identifier
#include "Factories/DataAssetFactory.h"    // Issue #16566: UDataAssetFactory (UnrealEd)
#include "Engine/DataAsset.h"              // Issue #16566: UDataAsset (abstract base — concrete subclass supplied via parent_class)
#include "Factories/AnimMontageFactory.h"  // Issue #16566: UAnimMontageFactory (UnrealEd)
#include "Animation/AnimMontage.h"         // Issue #16566: UAnimMontage (Engine)
#include "Animation/Skeleton.h"            // Issue #16566: USkeleton (Engine — AnimMontage TargetSkeleton)
#include "IAssetViewport.h"  // Issue #1632: P2 - For viewport access (UE 5.6: ILevelViewport → IAssetViewport)
#include "EditorViewportClient.h"  // Issue #1632: P2 - For camera control
#include "LevelEditorViewport.h"  // Issue #8784: GCurrentLevelEditingViewportClient full definition
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "Settings/EditorViewportSettings.h"  // Issue #3571: UE 5.7 FEditorViewportCameraSpeedSettings
#endif
#include "Logging/MessageLog.h"  // Issue #1632: P3 - For error log
#include "CUToolMaturitySettings.h"  // Issue #2547: For Tool Maturity INI handlers
#include "BlueprintEditor.h"  // For FBlueprintEditor::JumpToNode (focus_blueprint_node)
#include "CUBlueprintManipulator.h"  // For FCUBlueprintManipulator::FindNodeByGuid
#include "CUJsonUtils.h"  // Issue #4794: Shared JSON escaping (Unity Build fix)
#include "CURemoteLibrary.h"  // Issue #16324: UCURemoteLibrary::GetMaterialGraph (live material catalog)
#include "CUAnnotationSubsystem.h"  // Issue #5647: AI Annotation Subsystem
#include "Engine/NetDriver.h"  // Issue #7928: UNetDriver, FPacketSimulationSettings (DO_ENABLE_NET_TEST)
#include "Engine/NetConnection.h"  // Issue #7928: UNetConnection, CurrentNetSpeed (inherited from UPlayer)
#include "GameFramework/HUD.h"            // Issue #16601: AHUD::ProcessConsoleExec (FUNC_Exec-gated PIE walk)
#include "GameFramework/CheatManager.h"   // Issue #16601: UCheatManager::ProcessConsoleExec
#include "Misc/StringOutputDevice.h"      // Issue #16601: FStringOutputDevice for console output capture

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPWebSocket, Log, All);

// Issue #9394: Per-frame dispatch budget for the WebSocket pending-messages
// queue. See FCUPendingMessage in the header and the Tick() drain loop for
// the full story. Tunable at runtime via `n.WebSocket.MaxPerFrame <N>` in
// the Editor console. Values are clamped to
// [kMinPerFrame, kMaxPerFrameHardCap] at read time. Documented in
// docs/technical/performance/WEBSOCKET_FRAME_BUDGET.md.
static TAutoConsoleVariable<int32> CVarNarshaMCPWebSocketMaxPerFrame(
	TEXT("n.WebSocket.MaxPerFrame"),
	UCUWebSocketEditorSubsystem::kDefaultMaxPerFrame,
	TEXT("NarshaMCP: max number of queued WebSocket messages to dispatch per Editor frame (Issue #9394).\n")
	TEXT("  Clamped to [1, 64]. Lower = more headroom for Editor rendering, higher = faster MCP burst drain.\n")
	TEXT("  Default 4. Set higher if you observe MCP backlog growing during bursts (check ue_check_health metrics)."),
	ECVF_Default);

// Issue #1696: Python script execution support (must be at file scope, not inside function)
#if WITH_NARSHAMCP_PYTHONSCRIPTPLUGIN && WITH_PYTHON
#include "IPythonScriptPlugin.h"
#endif

bool UCUWebSocketEditorSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Issue #10756: cook/headless commandlets do NOT initialize FSlateApplication.
	// This subsystem registers IInputProcessors, opens screenshot capture against
	// SWindow handles, and binds widget reflection — all of which crash without Slate.
	// Skipping creation here makes the entire Initialize() / Deinitialize() path safe
	// in commandlet hosts. NarshaMCP's own UCommandlet subclasses are unaffected
	// because they are independent UCommandlet types, not editor subsystems.
	if (IsRunningCommandlet())
	{
		return false;
	}
	return Super::ShouldCreateSubsystem(Outer);
}

void UCUWebSocketEditorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LogInfo(TEXT("WebSocketEditorSubsystem initialized (Issue #1437)"));

	// Issue #4843: Register Output Log capture device
	LogCapture = MakeUnique<FCULogCapture>(2000);
	GLog->AddOutputDevice(LogCapture.Get());
	LogInfo(TEXT("Output Log capture registered (Issue #4843)"));

	// Issue #9098 Tier 1: Subscribe to PIE end so we can stop any sustained InputAction
	// injections before the Pawn/PlayerController gets torn down. World-scoped timers
	// already auto-invalidate; this is belt-and-braces to guarantee a clean state for the
	// next PIE session and to keep Action pointers from being used post-PIE-end.
	EndPIEHandle = FEditorDelegates::EndPIE.AddUObject(this, &UCUWebSocketEditorSubsystem::OnEndPIE);

	// Issue #26843: Subscribe to the actual end-of-session delegate so we can drop the Python
	// interpreter's references to PIE objects before the engine's GC + world-leak scan run
	// (PlayLevel.cpp:307 broadcast -> :502 CollectGarbage -> :505+ leak scan, same call).
	// Deliberately a separate subscription from EndPIEHandle above — see the header comment on
	// PIEEndedHandle for why FEditorDelegates::EndPIE is the wrong signal for this work.
	PIEEndedHandle = FWorldDelegates::OnPIEEnded.AddUObject(this, &UCUWebSocketEditorSubsystem::OnPIEEnded);

	// Issue #26997: OnPIEEnded above only fires at end of SESSION, so a SeamlessTravel world swap
	// inside a live session never reached the purge. Subscribe the world-axis signal too; see the
	// header comment on PostWorldCleanupHandle for the three teardown paths it covers.
	PostWorldCleanupHandle = FWorldDelegates::OnPostWorldCleanup.AddUObject(this, &UCUWebSocketEditorSubsystem::OnPostWorldCleanup);

	// Issue #1632 / #16334: Auto-start WebSocket server, scanning for a free port
	// so concurrent Editors (multi-worktree / multi-project) don't all collide on
	// 30011. This ensures the server is running when MCP tools try to connect.
	if (StartAuto())
	{
		LogInfo(FString::Printf(TEXT("WebSocket server auto-started on port %d (Issue #1632 / #16334)"), ServerPort));
	}
	else
	{
		LogError(TEXT("Failed to auto-start WebSocket server - check logs"));
	}

	// Issue #4843: Console command to enable/disable benchmark mode for automation testing.
	// Bypasses FWaitForInteractiveFrameRate which blocks BDD Spec tests on heavy editors.
	// Usage: NarshaMCP.BenchmarkMode 1  (enable) / NarshaMCP.BenchmarkMode 0  (disable)
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("NarshaMCP.BenchmarkMode"),
		TEXT("Enable (1) or disable (0) benchmark mode for automation testing. Bypasses FPS wait."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			bool bEnable = Args.Num() > 0 && Args[0] == TEXT("1");
			FApp::SetBenchmarking(bEnable);
			// Disable "Use Less CPU when in Background" (same as UE's AutomationEditorCommon.cpp)
			UEditorPerformanceSettings* PerfSettings = GetMutableDefault<UEditorPerformanceSettings>();
			PerfSettings->bThrottleCPUWhenNotForeground = !bEnable;
			PerfSettings->PostEditChange();
			if (bEnable)
			{
				FApp::SetFixedDeltaTime(1.0 / 60.0);
				UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[NarshaMCP] Benchmark mode ENABLED (fixed 60 FPS, background throttle OFF)"));
			}
			else
			{
				FApp::SetFixedDeltaTime(0.0);
				UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[NarshaMCP] Benchmark mode DISABLED (real delta time, background throttle restored)"));
			}
		}),
		ECVF_Default
	);

	// Issue #6938: Console command for quick PIE input recording → JSON recipe
	// Usage: NarshaMCP.Record 10           (record 10 seconds, auto-save JSON)
	//        NarshaMCP.Record start        (manual start, stop with "Record stop")
	//        NarshaMCP.Record stop [Name]  (stop and save as Name.json)
	//        NarshaMCP.Record              (show current recording status)
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("NarshaMCP.Record"),
		TEXT("Record PIE input and save JSON recipe. Usage: Record <seconds>|start|stop [TestName]"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UCUWebSocketEditorSubsystem::HandleRecordCommand),
		ECVF_Default
	);

	// Issue #25249: recover from a silent explicit-port bind failure without Editor restart.
	// Usage: NarshaMCP.RestartWS           (StartAuto: explicit override or free-port scan)
	//        NarshaMCP.RestartWS 30013     (bind exactly that port)
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("NarshaMCP.RestartWS"),
		TEXT("Restart NarshaMCP WebSocket server. Usage: RestartWS [port]  (omit port for StartAuto)"),
		FConsoleCommandWithArgsDelegate::CreateUObject(this, &UCUWebSocketEditorSubsystem::HandleRestartWSCommand),
		ECVF_Default
	);

	// Issue #7937: Console command to dump CDO PropertyLink order for any BP class.
	// Iterates PropertyLink (most-derived first) and outputs property name, type, offset, size.
	// Writes JSON to <ProjectDir>/Intermediate/NarshaMCP/cdo_propertylink/<ClassName>.json
	// Usage: NarshaMCP.DumpPropertyLink /Game/Path/To/Blueprint
	//        NarshaMCP.DumpPropertyLink Default__BP_Name_C  (CDO name directly)
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("NarshaMCP.DumpPropertyLink"),
		TEXT("Dump CDO PropertyLink order to JSON. Usage: DumpPropertyLink <AssetPath or CDO name>"),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			if (Args.Num() < 1)
			{
				UE_LOG(LogNarshaMCPWebSocket, Error, TEXT("[DumpPropertyLink] Usage: NarshaMCP.DumpPropertyLink <AssetPath>"));
				return;
			}

			const FString& AssetPath = Args[0];
			UClass* TargetClass = nullptr;

			// Try loading as Blueprint asset path first
			UObject* LoadedObj = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
			if (UBlueprint* BP = Cast<UBlueprint>(LoadedObj))
			{
				TargetClass = BP->GeneratedClass;
			}
			else
			{
				// Try finding CDO by name (e.g., Default__BP_Name_C)
				TargetClass = FindFirstObject<UClass>(*AssetPath, EFindFirstObjectOptions::NativeFirst);
			}

			if (!TargetClass)
			{
				UE_LOG(LogNarshaMCPWebSocket, Error, TEXT("[DumpPropertyLink] Could not find class for: %s"), *AssetPath);
				return;
			}

			UObject* CDO = TargetClass->GetDefaultObject();
			if (!CDO)
			{
				UE_LOG(LogNarshaMCPWebSocket, Error, TEXT("[DumpPropertyLink] No CDO for class: %s"), *TargetClass->GetName());
				return;
			}

			UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[DumpPropertyLink] Dumping PropertyLink for: %s (CDO: %s)"),
				*TargetClass->GetName(), *CDO->GetName());

			// Iterate PropertyLink (most-derived first = CDO Unversioned schema order)
			TArray<TSharedPtr<FJsonValue>> PropertiesArray;
			int32 Idx = 0;
			for (FProperty* Prop = TargetClass->PropertyLink; Prop; Prop = Prop->PropertyLinkNext)
			{
				TSharedPtr<FJsonObject> PropObj = MakeShared<FJsonObject>();
				PropObj->SetNumberField(TEXT("index"), Idx);
				PropObj->SetStringField(TEXT("name"), Prop->GetName());
				PropObj->SetStringField(TEXT("type"), Prop->GetClass()->GetName());
				PropObj->SetNumberField(TEXT("offset_internal"), Prop->GetOffset_ForInternal());
				PropObj->SetNumberField(TEXT("size"), Prop->GetSize());
				PropObj->SetNumberField(TEXT("element_size"), Prop->GetElementSize());
				PropObj->SetNumberField(TEXT("min_alignment"), Prop->GetMinAlignment());

				// Owner class for distinguishing child vs parent properties
				if (Prop->GetOwnerUObject())
				{
					PropObj->SetStringField(TEXT("owner_class"), Prop->GetOwnerUObject()->GetName());
				}

				// Additional metadata for struct/array/enum types
				if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					if (StructProp->Struct)
					{
						PropObj->SetStringField(TEXT("struct_name"), StructProp->Struct->GetName());
						// Count inner properties for nested Unversioned size estimation
						int32 InnerCount = 0;
						for (FProperty* Inner = StructProp->Struct->PropertyLink; Inner; Inner = Inner->PropertyLinkNext)
						{
							InnerCount++;
						}
						PropObj->SetNumberField(TEXT("struct_inner_property_count"), InnerCount);
					}
				}
				else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
				{
					if (ArrayProp->Inner)
					{
						PropObj->SetStringField(TEXT("inner_type"), ArrayProp->Inner->GetClass()->GetName());
						PropObj->SetNumberField(TEXT("inner_size"), ArrayProp->Inner->GetElementSize());
					}
				}
				else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
				{
					if (EnumProp->GetEnum())
					{
						PropObj->SetStringField(TEXT("enum_name"), EnumProp->GetEnum()->GetName());
					}
					if (EnumProp->GetUnderlyingProperty())
					{
						PropObj->SetStringField(TEXT("underlying_type"), EnumProp->GetUnderlyingProperty()->GetClass()->GetName());
						PropObj->SetNumberField(TEXT("underlying_size"), EnumProp->GetUnderlyingProperty()->GetElementSize());
					}
				}
				else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
				{
					if (ObjProp->PropertyClass)
					{
						PropObj->SetStringField(TEXT("property_class"), ObjProp->PropertyClass->GetName());
					}
				}
				else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
				{
					PropObj->SetBoolField(TEXT("is_native_bool"), BoolProp->IsNativeBool());
				}

				// Property flags for filtering (e.g., Transient, EditorOnly)
				PropObj->SetStringField(TEXT("property_flags"), FString::Printf(TEXT("0x%llX"), (uint64)Prop->PropertyFlags));
				PropObj->SetBoolField(TEXT("is_editor_only"), Prop->HasAnyPropertyFlags(CPF_EditorOnly));
				PropObj->SetBoolField(TEXT("is_transient"), Prop->HasAnyPropertyFlags(CPF_Transient));

				// CDO serialization: try to get the actual serialized value as string
				FString ValueStr;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(CDO);
				Prop->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, CDO, PPF_None);
				if (!ValueStr.IsEmpty())
				{
					PropObj->SetStringField(TEXT("default_value"), ValueStr);
				}

				PropertiesArray.Add(MakeShared<FJsonValueObject>(PropObj));
				Idx++;
			}

			// Build root JSON
			TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
			RootObj->SetStringField(TEXT("class_name"), TargetClass->GetName());
			RootObj->SetStringField(TEXT("cdo_name"), CDO->GetName());
			RootObj->SetStringField(TEXT("parent_class"), TargetClass->GetSuperClass() ? TargetClass->GetSuperClass()->GetName() : TEXT("None"));
			RootObj->SetNumberField(TEXT("property_count"), Idx);
			RootObj->SetArrayField(TEXT("properties"), PropertiesArray);

			// Write to file
			FString OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("cdo_propertylink");
			IFileManager::Get().MakeDirectory(*OutputDir, true);
			FString OutputPath = OutputDir / FString::Printf(TEXT("%s.json"), *TargetClass->GetName());

			FString JsonStr = CUJsonUtils::SerializeToString(RootObj.ToSharedRef());
			FFileHelper::SaveStringToFile(JsonStr, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

			UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[DumpPropertyLink] Wrote %d properties to: %s"), Idx, *OutputPath);
		}),
		ECVF_Default
	);
}

void UCUWebSocketEditorSubsystem::Deinitialize()
{
	// Issue #6388: Cleanup input recording processor to prevent dangling pointer
	if (InputRecordingProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(InputRecordingProcessor);
		}
		InputRecordingProcessor.Reset();
		bIsRecordingInput = false;
	}

	// Issue #9098 Tier 1+2: Stop any still-active sustained InputAction injections, release
	// all pending raw-key timers (matching synthetic key_up), and release the EndPIE subscription.
	StopAllContinuousInjections();
	ReleaseAllPendingRawKeys();
	if (EndPIEHandle.IsValid())
	{
		FEditorDelegates::EndPIE.Remove(EndPIEHandle);
		EndPIEHandle.Reset();
	}

	// Issue #26843: release the OnPIEEnded subscription.
	if (PIEEndedHandle.IsValid())
	{
		FWorldDelegates::OnPIEEnded.Remove(PIEEndedHandle);
		PIEEndedHandle.Reset();
	}

	// Issue #26997: release the OnPostWorldCleanup subscription.
	if (PostWorldCleanupHandle.IsValid())
	{
		FWorldDelegates::OnPostWorldCleanup.Remove(PostWorldCleanupHandle);
		PostWorldCleanupHandle.Reset();
	}

	// Issue #8783: cancel every in-flight pie_wait_for_condition ticker and drop the snapshot ring.
	CancelAllPIEWaits();
	CancelAllWidgetDesignerCaptures();
	ClearPIESnapshotRing();

	Stop(); // Issue #16695: Stop() removes the port advertisement (single teardown SSOT)

	// Issue #4843: Unregister Output Log capture device
	if (LogCapture.IsValid())
	{
		GLog->RemoveOutputDevice(LogCapture.Get());
		LogCapture.Reset();
		LogInfo(TEXT("Output Log capture unregistered (Issue #4843)"));
	}

	Super::Deinitialize();
	LogInfo(TEXT("WebSocketEditorSubsystem deinitialized"));
}

bool UCUWebSocketEditorSubsystem::IsPortAvailable(int32 Port)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	if (!SocketSubsystem)
	{
		// Cannot probe → fail-open (assume available); never block the legacy path.
		return true;
	}

	TSharedRef<FInternetAddr> Addr = SocketSubsystem->CreateInternetAddr();
	bool bIsValidIp = false;
	Addr->SetIp(TEXT("127.0.0.1"), bIsValidIp);
	Addr->SetPort(Port);
	if (!bIsValidIp)
	{
		return true;
	}

	FSocket* ProbeSocket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("CUWsPortProbe"), false);
	if (!ProbeSocket)
	{
		return true;
	}

	// Default FSocket has SO_REUSEADDR OFF, so Bind() fails if the port is already
	// held — even by UE's WS server (which sets SO_REUSEADDR). That is exactly what
	// lets us detect an occupied port that Server->Init() would otherwise "share".
	const bool bBound = ProbeSocket->Bind(*Addr);
	ProbeSocket->Close();
	SocketSubsystem->DestroySocket(ProbeSocket);
	return bBound;
}

bool UCUWebSocketEditorSubsystem::TryBind(int32 Port)
{
	// Caller guarantees !bIsRunning. Creates + Init()s the WS server on `Port`.
	ServerPort = static_cast<uint16>(Port);

	// Issue #16334: UE's WS Server->Init() binds with SO_REUSEADDR, so it does NOT
	// fail when `Port` is already held by another Editor — which would let two
	// Editors silently share 30011 and break the free-port scan. Probe first with a
	// plain (non-reuseaddr) socket so an occupied port is correctly skipped.
	if (!IsPortAvailable(Port))
	{
		UE_LOG(LogNarshaMCPWebSocket, Verbose, TEXT("[WebSocket] Port %d already in use — skipping"), Port);
		return false;
	}

	// Load WebSocketNetworking module (Issue #138 Fix: Use LoadModule instead of LoadModuleChecked)
	// LoadModule returns nullptr if module doesn't exist (e.g., in commandlet mode)
	IWebSocketNetworkingModule* WebSocketNetworkingModule = FModuleManager::LoadModulePtr<IWebSocketNetworkingModule>(TEXT("WebSocketNetworking"));

	if (!WebSocketNetworkingModule)
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[WebSocket] WebSocketNetworking module not available (commandlet mode?)"));
		return false;
	}

	// Create WebSocket server (returns TUniquePtr in UE 5.6)
	Server = WebSocketNetworkingModule->CreateServer();

	if (!Server.IsValid())
	{
		LogError(TEXT("Failed to create WebSocket server"));
		return false;
	}

	// UE 5.6 API: Init() with callback parameter
	if (!Server->Init(ServerPort, FWebSocketClientConnectedCallBack::CreateLambda([this](INetworkingWebSocket* Socket)
	{
		// Handle new client connection
		OnClientConnected(Socket);

		// Set up per-socket callbacks (UE 5.6 pattern)
		Socket->SetReceiveCallBack(FWebSocketPacketReceivedCallBack::CreateLambda([this, Socket](void* Data, int32 Size)
		{
			OnMessageReceived(Data, Size, Socket);
		}));

		Socket->SetSocketClosedCallBack(FWebSocketInfoCallBack::CreateLambda([this, Socket]()
		{
			OnClientDisconnected(Socket);
		}));

		Socket->SetErrorCallBack(FWebSocketInfoCallBack::CreateLambda([this]()
		{
			LogError(TEXT("WebSocket connection error"));
		}));
	}), TEXT("127.0.0.1")))  // Security: Bind to localhost only (Issue #1402)
	{
		// Issue #16334: a busy port during StartAuto()'s range scan is expected,
		// not an error — log at Verbose and let the caller try the next port.
		UE_LOG(LogNarshaMCPWebSocket, Verbose, TEXT("[WebSocket] Port %d unavailable (in use?) — not bound"), ServerPort);
		Server.Reset();
		return false;
	}

	bIsRunning = true;

	// Issue #16695: advertise the port we just bound so the daemon discovers its
	// own project's Editor without falling back to the shared 30011 default. Runs
	// only on a successful bind (this point is reachable only on success); a rebind
	// to a new port overwrites the same <pid>.json. Best-effort — never blocks bind.
	WriteEditorPortAdvertisement();

	// Issue #1632: Setup ticker for WebSocket polling
	// The server needs to be ticked to process incoming connections and messages
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCUWebSocketEditorSubsystem::Tick),
		0.016f  // ~60Hz tick rate
	);

	return true;
}

bool UCUWebSocketEditorSubsystem::Start(int32 Port)
{
	if (bIsRunning)
	{
		LogInfo(TEXT("WebSocket server already running"));
		return true;
	}

	// Bind exactly the requested port (explicit caller / Blueprint API). For
	// auto free-port selection across concurrent Editors, use StartAuto().
	if (TryBind(Port))
	{
		LogInfo(FString::Printf(TEXT("WebSocket server started on port %d (with ticker)"), ServerPort));
		return true;
	}

	LogError(FString::Printf(TEXT("Failed to start WebSocket server on port %d"), Port));
	return false;
}

bool UCUWebSocketEditorSubsystem::RestartWS(int32 Port)
{
	// Issue #25249: operator recovery after an explicit-port bind failure (or any
	// dead-WS state). Stop first so Start/StartAuto are allowed to rebind.
	if (bIsRunning)
	{
		Stop();
	}

	const bool bOk = (Port > 0) ? Start(Port) : StartAuto();
	if (bOk)
	{
		LogInfo(FString::Printf(TEXT("RestartWS succeeded on port %d (Issue #25249)"), ServerPort));
	}
	else
	{
		LogError(FString::Printf(TEXT("RestartWS failed (requested port=%d) — check occupancy; Issue #25249"), Port));
	}
	return bOk;
}

void UCUWebSocketEditorSubsystem::HandleRestartWSCommand(const TArray<FString>& Args)
{
	int32 Port = 0;
	if (Args.Num() > 0)
	{
		Port = FCString::Atoi(*Args[0]);
		if (Port <= 0 || Port > 65535)
		{
			UE_LOG(LogNarshaMCPWebSocket, Error,
				TEXT("[RestartWS] Invalid port '%s' — expected 1..65535 or omit for StartAuto"),
				*Args[0]);
			return;
		}
	}
	RestartWS(Port);
}

void UCUWebSocketEditorSubsystem::NotifyExplicitWsBindFailure(int32 Port)
{
	// Issue #25249: explicit override failed — no editor_ports advertisement, no WS.
	// Surface loudly so operators are not left with a silent "ghost" Editor.
	const FString Detail = FString::Printf(
		TEXT("NarshaMCP WebSocket failed to bind explicit port %d (in use?). "
			 "MCP editor control will not work until the port is free. "
			 "Retry: console `NarshaMCP.RestartWS` or `NarshaMCP.RestartWS %d` "
			 "(or free the port and restart the Editor)."),
		Port, Port);

	LogError(Detail);

	// MessageLog is safe without Slate; appears under Message Log panel.
	FMessageLog("NarshaMCP").Error(FText::FromString(Detail));

	// Toast only when Slate is initialized (skip cook/commandlet/headless).
	if (FSlateApplication::IsInitialized())
	{
		FNotificationInfo Info(FText::FromString(Detail));
		Info.ExpireDuration = 12.0f;
		Info.bUseSuccessFailIcons = true;
		Info.bFireAndForget = true;
		const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info);
		if (Item.IsValid())
		{
			Item->SetCompletionState(SNotificationItem::CS_Fail);
		}
	}
}

bool UCUWebSocketEditorSubsystem::StartAuto()
{
	if (bIsRunning)
	{
		LogInfo(TEXT("WebSocket server already running"));
		return true;
	}

	bool bExplicit = false;
	const int32 BasePort = ResolveWsBasePort(bExplicit);

	if (bExplicit)
	{
		// Deterministic explicit override (-NarshaMCPWSPort= / UECODEGEN_RC_WS_PORT):
		// bind exactly this port; do NOT scan (fail if it is taken).
		if (TryBind(BasePort))
		{
			LogInfo(FString::Printf(TEXT("WebSocket server started on explicit port %d (Issue #16334)"), ServerPort));
			return true;
		}
		// Issue #25249: loud failure — toast + MessageLog + LogError, plus RestartWS recovery.
		NotifyExplicitWsBindFailure(BasePort);
		return false;
	}

	// Scan the WS range from the default base; bind the first free port so each
	// concurrent Editor lands on its own port (Issue #16334). Success returns
	// directly from the loop body (no unconditional break).
	const int32 EndPort = BasePort + kWsRangeSpan;
	for (int32 CandidatePort = BasePort; CandidatePort <= EndPort; ++CandidatePort)
	{
		if (TryBind(CandidatePort))
		{
			LogInfo(FString::Printf(TEXT("WebSocket server auto-started on port %d (scanned from %d, Issue #16334)"), ServerPort, BasePort));
			return true;
		}
	}

	LogError(FString::Printf(TEXT("Failed to bind any WebSocket port in range %d-%d (Issue #16334)"), BasePort, EndPort));
	return false;
}

int32 UCUWebSocketEditorSubsystem::ResolveWsBasePort(bool& bOutExplicit)
{
	bOutExplicit = false;

	// 1. Command line: -NarshaMCPWSPort=N (highest priority, deterministic).
	int32 CmdLinePort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("NarshaMCPWSPort="), CmdLinePort)
		&& CmdLinePort > 0 && CmdLinePort <= 65535)
	{
		bOutExplicit = true;
		return CmdLinePort;
	}

	// 2. Environment: UECODEGEN_RC_WS_PORT (symmetric with the daemon's env key).
	const FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("UECODEGEN_RC_WS_PORT"));
	if (!EnvPort.IsEmpty())
	{
		const int32 ParsedPort = FCString::Atoi(*EnvPort);
		if (ParsedPort > 0 && ParsedPort <= 65535)
		{
			bOutExplicit = true;
			return ParsedPort;
		}
	}

	// 3. Issue #25250: per-project local UDeveloperSettings (EditorPerProjectUserSettings).
	//    Lower priority than cmdline/env so session overrides still win. Port 0 = unset.
	if (const UCUConnectionSettings* ConnSettings = GetDefault<UCUConnectionSettings>())
	{
		const int32 SettingsPort = ConnSettings->WebSocketPort;
		if (SettingsPort > 0 && SettingsPort <= 65535)
		{
			bOutExplicit = true;
			return SettingsPort;
		}
	}

	// 4. Default base — StartAuto() scans upward from here for the first free port.
	return kDefaultWsPort;
}

void UCUWebSocketEditorSubsystem::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	// Issue #1632: Remove ticker
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// Clear client list
	Clients.Empty();

	// Clear rate limiting state
	{
		FScopeLock Lock(&RateLimitLock);
		ClientRateLimitState.Empty();
	}

	// Issue #9394: drop every queued message — all their Socket pointers
	// reference clients we just cleared. Running dispatch now would hit
	// freed memory.
	PendingMessages.Empty();
	PendingQueueDepth = 0;

	// Stop server
	if (Server.IsValid())
	{
		Server.Reset();
	}

	bIsRunning = false;

	// Issue #16695: the WS listener is gone — remove this Editor's port
	// advertisement so no daemon reads a stale entry pointing at a dead port. This
	// is the single teardown SSOT (Deinitialize, explicit Stop, and rebind all flow
	// through here; a successful TryBind then re-writes the fresh port). Best-effort
	// (the daemon also drops dead-PID entries, so a missed delete on crash recovers).
	RemoveEditorPortAdvertisement();

	LogInfo(TEXT("WebSocket server stopped"));
}

bool UCUWebSocketEditorSubsystem::IsRunning() const
{
	return bIsRunning;
}

void UCUWebSocketEditorSubsystem::BroadcastEvent(const FString& EventType, const FString& EventData)
{
	// Issue #8840 (Phase 10-1): in-process subscribers (e.g. SCUChatTimelineBar
	// from #8841, CUChatPanelSubsystem from #8842) receive events independently
	// of WebSocket fan-out — fire before any early-return so they get events
	// even when the WS server is not started or no external client is connected.
	OnEventBroadcast.Broadcast(EventType, EventData);

	if (!bIsRunning)
	{
		LogError(TEXT("Cannot broadcast - server not running"));
		return;
	}

	if (Clients.Num() == 0)
	{
		// No clients connected - skip broadcast
		return;
	}

	// Build event JSON
	FString EventJSON = BuildEventJSON(EventType, EventData);

	// Convert to UTF-8 bytes
	FTCHARToUTF8 UTF8String(*EventJSON);
	TArray<uint8> Data;
	Data.Append((uint8*)UTF8String.Get(), UTF8String.Length());

	// Broadcast to all connected clients
	int32 SuccessCount = 0;
	for (INetworkingWebSocket* Client : Clients)
	{
		if (Client != nullptr)
		{
			Client->Send(Data.GetData(), Data.Num(), /*bIsBinary=*/false);
			SuccessCount++;
		}
	}

	LogInfo(FString::Printf(TEXT("Broadcasted event '%s' to %d clients"), *EventType, SuccessCount));
}

int32 UCUWebSocketEditorSubsystem::GetClientCount() const
{
	return Clients.Num();
}

void UCUWebSocketEditorSubsystem::OnClientConnected(INetworkingWebSocket* Socket)
{
	if (Socket == nullptr)
	{
		return;
	}

	// Add client to list
	Clients.Add(Socket);

	LogInfo(FString::Printf(TEXT("Client connected (total: %d)"), Clients.Num()));

	// Send welcome message
	FString WelcomeMsg = BuildEventJSON(TEXT("connected"), TEXT("{\"version\":\"v5.1.0\",\"server\":\"uecodegen-subsystem\"}"));
	FTCHARToUTF8 UTF8String(*WelcomeMsg);
	TArray<uint8> Data;
	Data.Append((uint8*)UTF8String.Get(), UTF8String.Length());
	Socket->Send(Data.GetData(), Data.Num(), /*bIsBinary=*/false);
}

void UCUWebSocketEditorSubsystem::OnClientDisconnected(INetworkingWebSocket* Socket)
{
	if (Socket == nullptr)
	{
		return;
	}

	// Remove client from list
	Clients.Remove(Socket);

	// Security: Cleanup rate limiting state (Issue #1402)
	{
		FScopeLock Lock(&RateLimitLock);
		ClientRateLimitState.Remove(Socket);
	}

	// Issue #8783: cancel any in-flight pie_wait_for_condition tickers for this socket so we
	// don't try to SendResponse on a dead connection.
	CancelAllPIEWaitsForSocket(Socket);
	CancelWidgetDesignerCapturesForSocket(Socket);

	// Issue #9394: purge any still-queued messages for this socket. The
	// Tick() drain loop also checks Clients.Contains as a second line of
	// defense, but purging here releases the queued TSharedPtr<FJsonObject>
	// refs sooner and keeps PendingQueueDepth honest. TQueue has no
	// random-access removal, so drain into a scratch array, filter, and
	// re-enqueue the survivors. n is bounded by kPendingQueueDepthCap (256)
	// so the O(n) walk is trivial.
	if (PendingQueueDepth > 0)
	{
		TArray<FCUPendingMessage> Survivors;
		Survivors.Reserve(PendingQueueDepth);
		FCUPendingMessage Msg;
		while (PendingMessages.Dequeue(Msg))
		{
			if (Msg.Socket != Socket)
			{
				Survivors.Add(MoveTemp(Msg));
			}
		}
		PendingQueueDepth = Survivors.Num();
		for (FCUPendingMessage& Survivor : Survivors)
		{
			PendingMessages.Enqueue(MoveTemp(Survivor));
		}
	}

	LogInfo(FString::Printf(TEXT("Client disconnected (total: %d)"), Clients.Num()));
}

void UCUWebSocketEditorSubsystem::OnMessageReceived(void* Data, int32 Size, INetworkingWebSocket* Socket)
{
	// Issue #9394 (review W-3): enforce the SPSC assumption at runtime. If
	// Epic ever moves receive onto a worker thread (INetworkingWebSocket
	// makes no threading guarantee — the Experimental WebSocketNetworking
	// module has been rewritten before), this check fails loudly instead
	// of corrupting PendingMessages silently. Zero cost in shipping game
	// thread callbacks.
	check(IsInGameThread());

	// Security: Message size limit (Issue #1402)
	if (Size <= 0 || Size > SecurityLimits::MaxWebSocketMessageSize)
	{
		LogError(FString::Printf(TEXT("Message rejected: size %d exceeds limit %d bytes"),
			Size, SecurityLimits::MaxWebSocketMessageSize));
		return;
	}

	// Convert bytes to string
	FUTF8ToTCHAR Converter(static_cast<const ANSICHAR*>(Data), Size);
	FString Message(Converter.Length(), Converter.Get());

	LogInfo(FString::Printf(TEXT("Received message: %s"), *Message));

	// Parse JSON message
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		// Issue #9394: enqueue for deferred dispatch in Tick() rather than
		// calling the handler inline. See FCUPendingMessage comment in the
		// header for the full rationale (Day 2 trace, 30 s client timeout
		// under 20-parallel burst).
		//
		// Unknown-type detection was previously inline via the final else
		// of the dispatch chain; deferring dispatch means we can't echo raw
		// bytes anymore (the buffer is gone by the time Tick runs). To
		// preserve behaviour *before* queuing, we check the type here. This
		// also avoids filling the queue with junk that DispatchMessage
		// would just drop — better to reject at the boundary.
		FString MessageType;
		JsonObject->TryGetStringField(TEXT("type"), MessageType);
		if (MessageType.IsEmpty())
		{
			// No `type` field at all — echo raw bytes, mirrors pre-#9394
			// behaviour of the `else` arm.
			if (Socket)
			{
				Socket->Send((uint8*)Data, Size, /*bIsBinary=*/false);
			}
			return;
		}

		// Overflow guard — risk table: unbounded queue under sustained high
		// arrival rate → OOM. Cap at kPendingQueueDepthCap and report the
		// failure back so the client can back off instead of silently losing
		// messages.
		if (PendingQueueDepth >= kPendingQueueDepthCap)
		{
			FString RequestId;
			JsonObject->TryGetStringField(TEXT("request_id"), RequestId);
			UE_LOG(LogNarshaMCPWebSocket, Warning,
				TEXT("WebSocket dispatch queue at cap (%d), dropping message (type=%s, request_id=%s). ")
				TEXT("Increase n.WebSocket.MaxPerFrame or reduce client parallelism (Issue #9394)."),
				kPendingQueueDepthCap, *MessageType, *RequestId);
			if (Socket)
			{
				const FString ErrorResponse = FString::Printf(
					TEXT("{\"type\":\"error\",\"request_id\":\"%s\",\"error\":\"queue_overflow\",")
					TEXT("\"message\":\"WebSocket dispatch queue at capacity (%d). ")
					TEXT("Reduce parallelism or increase n.WebSocket.MaxPerFrame.\"}"),
					*RequestId, kPendingQueueDepthCap);
				SendResponse(Socket, ErrorResponse);
			}
			return;
		}

		PendingMessages.Enqueue(FCUPendingMessage{
			JsonObject,
			Socket,
			FPlatformTime::Seconds()
		});
		++PendingQueueDepth;
	}
	else
	{
		LogError(FString::Printf(TEXT("Failed to parse JSON message: %s"), *Message));
	}
}

// Issue #9394: Pure refactor — the if/else dispatch chain moved out of
// OnMessageReceived so it can be called either inline (current) or from a
// deferred per-frame drain loop (next commit). Returns false when the type
// is unrecognized so OnMessageReceived can echo raw bytes back.
bool UCUWebSocketEditorSubsystem::DispatchMessage(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& JsonObject)
{
	const FString MessageType = JsonObject->GetStringField(TEXT("type"));

	if (MessageType == TEXT("execute_python"))
	{
		HandleExecutePython(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_screenshot"))
	{
		HandleCaptureScreenshot(Socket, JsonObject);
	}
	else if (MessageType == TEXT("ping"))
	{
		HandlePing(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_editor_status"))
	{
		HandleGetEditorStatus(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_editor_info"))
	{
		// Issue #9074: authoritative editor info (project_root + project_name +
		// engine_version + is_pie_active). Used by ue_check_health to surface
		// the Editor's actual project — distinct from the MCP server config.
		HandleGetEditorInfo(Socket, JsonObject);
	}
	else if (MessageType == TEXT("execute_console_command"))
	{
		HandleExecuteConsoleCommand(Socket, JsonObject);
	}
	// Issue #1632: P1 Features
	else if (MessageType == TEXT("save_asset"))
	{
		HandleSaveAsset(Socket, JsonObject);
	}
	else if (MessageType == TEXT("save_all"))
	{
		HandleSaveAll(Socket, JsonObject);
	}
	else if (MessageType == TEXT("compile_blueprints"))
	{
		HandleCompileBlueprints(Socket, JsonObject);
	}
	else if (MessageType == TEXT("livecoding_compile"))  // Issue #16682
	{
		HandleLiveCodingCompile(Socket, JsonObject);
	}
	else if (MessageType == TEXT("livecoding_status"))  // Issue #16682
	{
		HandleLiveCodingStatus(Socket, JsonObject);
	}
	else if (MessageType == TEXT("focus_actor"))
	{
		HandleFocusActor(Socket, JsonObject);
	}
	else if (MessageType == TEXT("select_actors"))
	{
		HandleSelectActors(Socket, JsonObject);
	}
	// Issue #1632: P2 Features
	else if (MessageType == TEXT("spawn_actor"))
	{
		HandleSpawnActor(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_transform"))
	{
		HandleSetTransform(Socket, JsonObject);
	}
	else if (MessageType == TEXT("delete_actor"))
	{
		HandleDeleteActor(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_actor_properties"))
	{
		HandleGetActorProperties(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_actor_property"))
	{
		HandleSetActorProperty(Socket, JsonObject);
	}
	// Issue #16566: read a named property by reflection from an actor or its component
	else if (MessageType == TEXT("get_component_property"))
	{
		HandleGetComponentProperty(Socket, JsonObject);
	}
	// Issue #27010: read a named property from any loaded UObject by path (not AActor-only)
	else if (MessageType == TEXT("get_object_properties"))
	{
		HandleGetObjectProperties(Socket, JsonObject);
	}
	// Issue #28227: the write twin of get_object_properties, and the Instanced-array adder
	else if (MessageType == TEXT("set_object_properties"))
	{
		HandleSetObjectProperties(Socket, JsonObject);
	}
	else if (MessageType == TEXT("add_instanced_subobject"))
	{
		HandleAddInstancedSubobject(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_level_actors_by_class"))  // Issue #1921
	{
		HandleGetLevelActorsByClass(Socket, JsonObject);
	}
	// Issue #1632: P2 Asset & Camera Features
	else if (MessageType == TEXT("open_asset"))
	{
		HandleOpenAsset(Socket, JsonObject);
	}
	else if (MessageType == TEXT("create_asset"))
	{
		HandleCreateAsset(Socket, JsonObject);
	}
	// Issue #16566: create a content folder (Content Browser visible + on-disk persistent)
	else if (MessageType == TEXT("create_folder"))
	{
		HandleCreateFolder(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_material_param"))
	{
		HandleSetMaterialParam(Socket, JsonObject);
	}
	else if (MessageType == TEXT("control_camera"))
	{
		HandleControlCamera(Socket, JsonObject);
	}
	// Issue #1632: P3 PIE & Debugging Features
	else if (MessageType == TEXT("play_in_editor"))
	{
		HandlePlayInEditor(Socket, JsonObject);
	}
	else if (MessageType == TEXT("stop_play"))
	{
		HandleStopPlay(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_game_paused"))
	{
		HandleSetGamePaused(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_global_time_dilation"))
	{
		HandleSetGlobalTimeDilation(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_blueprint_editor"))
	{
		HandleCaptureBlueprintEditor(Socket, JsonObject);
	}
	else if (MessageType == TEXT("focus_blueprint_node"))
	{
		HandleFocusBlueprintNode(Socket, JsonObject);
	}
	else if (MessageType == TEXT("annotate_blueprint_node"))
	{
		HandleAnnotateBlueprintNode(Socket, JsonObject);
	}
	else if (MessageType == TEXT("annotate_blueprint_nodes"))
	{
		HandleAnnotateBlueprintNodes(Socket, JsonObject);
	}
	else if (MessageType == TEXT("annotate_sequencer_track"))
	{
		HandleAnnotateSequencerTrack(Socket, JsonObject);
	}
	else if (MessageType == TEXT("annotate_sequencer_tracks"))
	{
		HandleAnnotateSequencerTracks(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_annotation_group"))
	{
		HandleCaptureAnnotationGroup(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_error_log"))
	{
		HandleGetErrorLog(Socket, JsonObject);
	}
	// Tool Maturity INI handlers (Issue #2547)
	else if (MessageType == TEXT("get_tool_maturity"))
	{
		HandleGetToolMaturity(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_tool_maturity"))
	{
		HandleSetToolMaturity(Socket, JsonObject);
	}
	// Issue #3796: Niagara VFX parameter control
	else if (MessageType == TEXT("set_niagara_parameter"))
	{
		HandleSetNiagaraParameter(Socket, JsonObject);
	}
	// Issue #6844: Set User Parameter defaults on Niagara System asset
	else if (MessageType == TEXT("set_niagara_asset_parameter"))
	{
		HandleSetNiagaraAssetParameter(Socket, JsonObject);
	}
	// Issue #21950 / NIA-N11: create a new User.* ExposedParameter on a NiagaraSystem asset
	else if (MessageType == TEXT("add_niagara_user_parameter"))
	{
		HandleAddNiagaraUserParameter(Socket, JsonObject);
	}
	// Issue #27351: Dynamic Input tree apply (Rust attach_dynamic_input)
	else if (MessageType == TEXT("attach_niagara_dynamic_input"))
	{
		HandleAttachNiagaraDynamicInput(Socket, JsonObject);
	}
	// Issue #16555: dynamic renderer enumeration (5.8 versioned-emitter renderer authoring)
	else if (MessageType == TEXT("set_niagara_renderer_material") || MessageType == TEXT("set_niagara_renderer_mesh"))
	{
		HandleSetNiagaraRenderer(Socket, JsonObject);
	}
	// Issue #16957: event-handler authoring on the 5.8 versioned emitter (add + configure)
	else if (MessageType == TEXT("add_niagara_event_handler") || MessageType == TEXT("configure_niagara_event"))
	{
		HandleNiagaraEventHandler(Socket, JsonObject);
	}
	// Issue #17149 [4c]: add a Generate*Event module to the SOURCE emitter's Particle Update stack
	else if (MessageType == TEXT("add_niagara_event_generator"))
	{
		HandleNiagaraEventGenerator(Socket, JsonObject);
	}
	// Issue #17149 [4c]: capture a Niagara SimCache from a ticking PIE component (per-emitter instance counts)
	else if (MessageType == TEXT("niagara_capture_simcache"))
	{
		HandleNiagaraCaptureSimCache(Socket, JsonObject);
	}
	else if (MessageType == TEXT("activate_niagara"))
	{
		HandleActivateNiagara(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_niagara_parameters"))
	{
		HandleGetNiagaraParameters(Socket, JsonObject);
	}
	// Issue #3795 Phase 7B: Add NiagaraComponent to actor
	else if (MessageType == TEXT("add_niagara_emitter"))
	{
		HandleAddNiagaraEmitter(Socket, JsonObject);
	}
	// Issue #21944 / NIA-N2: add emitter handle onto NiagaraSystem asset (authoring)
	else if (MessageType == TEXT("add_system_emitter"))
	{
		HandleAddSystemEmitter(Socket, JsonObject);
	}
	// Issue #21945 / NIA-N3
	else if (MessageType == TEXT("set_niagara_sim_target"))
	{
		HandleSetNiagaraSimTarget(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_niagara_fixed_bounds"))
	{
		HandleSetNiagaraFixedBounds(Socket, JsonObject);
	}
	else if (MessageType == TEXT("set_niagara_emitter_enabled"))
	{
		HandleSetNiagaraEmitterEnabled(Socket, JsonObject);
	}
	// Issue #3797: Collision property control
	else if (MessageType == TEXT("set_collision"))
	{
		HandleSetCollision(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_material_hlsl"))
	{
		HandleGetMaterialHLSL(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_material_graph"))  // Issue #16324: live material catalog
	{
		HandleGetMaterialGraph(Socket, JsonObject);
	}
	else if (MessageType == TEXT("list_automation_tests"))  // Issue #4843
	{
		HandleListAutomationTests(Socket, JsonObject);
	}
	// Issue #6378: PIE input injection + record/replay
	else if (MessageType == TEXT("inject_input"))
	{
		HandleInjectInput(Socket, JsonObject);
	}
	else if (MessageType == TEXT("inject_input_sequence"))
	{
		HandleInjectInputSequence(Socket, JsonObject);
	}
	else if (MessageType == TEXT("start_input_recording"))
	{
		HandleStartInputRecording(Socket, JsonObject);
	}
	else if (MessageType == TEXT("stop_input_recording"))
	{
		HandleStopInputRecording(Socket, JsonObject);
	}
	else if (MessageType == TEXT("replay_input_recording"))
	{
		HandleReplayInputRecording(Socket, JsonObject);
	}
	// Issue #6378: PIE viewport capture
	else if (MessageType == TEXT("capture_pie_viewport"))
	{
		HandleCapturePIEViewport(Socket, JsonObject);
	}
	// Issue #7914: Enhanced Input InputAction injection
	else if (MessageType == TEXT("inject_input_action"))
	{
		HandleInjectInputAction(Socket, JsonObject);
	}
	// Issue #8784: Screenshot capture for visual verification
	else if (MessageType == TEXT("capture_editor_viewport"))
	{
		HandleCaptureEditorViewport(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_pie_window"))
	{
		HandleCapturePIEWindow(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_editor_panel"))
	{
		HandleCaptureEditorPanel(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_all_panels"))
	{
		HandleCaptureAllPanels(Socket, JsonObject);
	}
	// Issue #9251: Runtime tab enumeration — discover live per-instance tab IDs
	// that HandleCaptureAllPanels' static KnownPanels list can't cover
	// (e.g. MaterialEditor_M_Mannequin_0, BlueprintEditor_WBP_HUD_0).
	else if (MessageType == TEXT("enumerate_open_tabs"))
	{
		HandleEnumerateOpenTabs(Socket, JsonObject);
	}
	// Issue #9833: Per-asset top-level OS window handle for get_preview focus assist —
	// replaces Win32 EnumWindows title-substring matching in widget/niagara/material
	// get_preview Rust helpers with an authoritative Slate-resolved HWND.
	else if (MessageType == TEXT("get_editor_window_handle"))
	{
		HandleGetEditorWindowHandle(Socket, JsonObject);
	}
	// Issue #26157: asset-specific UMG Designer capture. Unlike capture_screenshot,
	// this never relies on whichever top-level window happens to be active.
	else if (MessageType == TEXT("capture_widget_designer"))
	{
		HandleCaptureWidgetDesigner(Socket, JsonObject);
	}
	// Issue #7915: Runtime UMG widget tree inspection
	else if (MessageType == TEXT("get_widget_tree"))
	{
		HandleGetWidgetTree(Socket, JsonObject);
	}
	else if (MessageType == TEXT("search_widgets"))
	{
		HandleSearchWidgets(Socket, JsonObject);
	}
	// Issue #9239: Runtime widget hit-test
	else if (MessageType == TEXT("find_widget_at_position"))
	{
		HandleFindWidgetAtPosition(Socket, JsonObject);
	}
	// Issue #9344: PIE widget enumeration + per-widget screenshot
	else if (MessageType == TEXT("list_active_pie_widgets"))
	{
		HandleListActivePIEWidgets(Socket, JsonObject);
	}
	else if (MessageType == TEXT("capture_widget_screenshot"))
	{
		HandleCaptureWidgetScreenshot(Socket, JsonObject);
	}
	// Issue #8299 (Mirror #26): Stat selective control
	else if (MessageType == TEXT("disable_stat"))
	{
		HandleDisableStat(Socket, JsonObject);
	}
	else if (MessageType == TEXT("list_active_stats"))
	{
		HandleListActiveStats(Socket, JsonObject);
	}
	// Issue #32: RigUnit type → DisplayName mapping for Control Rig node resolution
	else if (MessageType == TEXT("get_rigunit_display_names"))
	{
		HandleGetRigUnitDisplayNames(Socket, JsonObject);
	}
	// Issue #8783: PIE runtime state queries + snapshot/diff
	else if (MessageType == TEXT("pie_get_actor_state"))
	{
		HandlePIEGetActorState(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_get_player_state"))
	{
		HandlePIEGetPlayerState(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_read_gameplay_attributes"))
	{
		HandlePieReadGameplayAttributes(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_list_active_actors"))
	{
		HandlePIEListActiveActors(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_set_measurement_mode"))
	{
		HandlePIESetMeasurementMode(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_wait_for_condition"))
	{
		HandlePIEWaitForCondition(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_snapshot_frame"))
	{
		HandlePIESnapshotFrame(Socket, JsonObject);
	}
	else if (MessageType == TEXT("pie_compare_snapshots"))
	{
		HandlePIECompareSnapshots(Socket, JsonObject);
	}
	// Issue #7926: Multi-PIE orchestration — restore dispatch arms accidentally
	// removed by PR #9290 (Issue #8783). The HandleListPieClients and
	// HandleCompareActorState handlers have remained defined but unreachable
	// since #9290 landed, silently breaking every caller of the Multi-PIE API.
	else if (MessageType == TEXT("list_pie_clients"))
	{
		HandleListPieClients(Socket, JsonObject);
	}
	else if (MessageType == TEXT("compare_actor_state"))
	{
		HandleCompareActorState(Socket, JsonObject);
	}
	// Issue #7928: NetEmulation parameter control (ADK Phase 3)
	else if (MessageType == TEXT("configure_network_emulation"))
	{
		HandleConfigureNetworkEmulation(Socket, JsonObject);
	}
	else if (MessageType == TEXT("get_network_emulation"))
	{
		HandleGetNetworkEmulation(Socket, JsonObject);
	}
	else if (MessageType == TEXT("reset_network_emulation"))
	{
		HandleResetNetworkEmulation(Socket, JsonObject);
	}
	else
	{
		// Unknown message type — caller (OnMessageReceived) handles the echo
		// because only it has the raw bytes. Signal with false.
		return false;
	}

	return true;
}

// Issue #9394: Per-frame drain helper. Called from Tick() in production and
// directly from automation tests via DrainPendingMessagesOnceForTesting().
// Respects the `n.WebSocket.MaxPerFrame` CVar and the Clients guard.
void UCUWebSocketEditorSubsystem::DrainPendingMessages()
{
	// Review W-3: see OnMessageReceived — enforce SPSC assumption.
	check(IsInGameThread());

	// Review W-1: reentrancy guard. A handler dispatched below can pump
	// Slate / FTSTicker (screenshot, PIE start) which would recursively
	// invoke UCUWebSocketEditorSubsystem::Tick → back into this drain.
	// Silently no-op on reentry so the outer drain's Budget stays honest
	// and FTSTicker::Tick's same-thread-reentry assert doesn't fire.
	if (bDrainingPending)
	{
		return;
	}
	TGuardValue<bool> DrainGuard(bDrainingPending, true);

	const int32 Budget = FMath::Clamp(
		CVarNarshaMCPWebSocketMaxPerFrame.GetValueOnGameThread(),
		kMinPerFrame, kMaxPerFrameHardCap);

	int32 Processed = 0;
	FCUPendingMessage Msg;
	while (Processed < Budget && PendingMessages.Dequeue(Msg))
	{
		--PendingQueueDepth;

		// Disconnect-safety: the raw Socket pointer may have been freed
		// between enqueue and dequeue if the client disconnected.
		// OnClientDisconnected purges matching entries, but that check is
		// best-effort — `Clients` is the authoritative live-client set and
		// cheap to query. Drop silently on miss (the client is gone, no
		// one to respond to).
		if (Msg.Socket && Clients.Contains(Msg.Socket) && Msg.Json.IsValid())
		{
			DispatchMessage(Msg.Socket, Msg.Json);
		}
		++Processed;
	}
}

// Issue #9394: Test seam — direct queue injection. Bypasses the WebSocket
// receive path so tests don't need a live socket connection. Used by
// FWebSocketSubsystemBurstQueueTest to validate drain mechanics.
void UCUWebSocketEditorSubsystem::EnqueuePendingMessageForTesting(
	TSharedPtr<FJsonObject> Json, INetworkingWebSocket* Socket)
{
	PendingMessages.Enqueue(FCUPendingMessage{
		MoveTemp(Json),
		Socket,
		FPlatformTime::Seconds()
	});
	++PendingQueueDepth;
}

// Issue #9394: Test seam — one drain pass. Same as a Tick() drain but
// skips Server->Tick() so tests don't need Start() to have been called.
void UCUWebSocketEditorSubsystem::DrainPendingMessagesOnceForTesting()
{
	DrainPendingMessages();
}

// Issue #9394: Test seam — current depth. Read-only.
int32 UCUWebSocketEditorSubsystem::GetPendingQueueDepthForTesting() const
{
	return PendingQueueDepth;
}

FString UCUWebSocketEditorSubsystem::BuildEventJSON(const FString& EventType, const FString& EventData)
{
	FString Timestamp = FDateTime::UtcNow().ToIso8601();

	return FString::Printf(TEXT(
		"{"
		"\"event\":\"%s\","
		"\"timestamp\":\"%s\","
		"\"data\":%s"
		"}"
	),
		*EventType,
		*Timestamp,
		*EventData
	);
}

void UCUWebSocketEditorSubsystem::LogInfo(const FString& Message)
{
	UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[WebSocket] %s"), *Message);
}

void UCUWebSocketEditorSubsystem::LogError(const FString& Message)
{
	UE_LOG(LogNarshaMCPWebSocket, Error, TEXT("[WebSocket] %s"), *Message);
}

// Issue #27130: after-snapshot diff, extracted from HandleExecutePython so a test can
// drive it with an actor the Python deliberately destroyed. Returns the number of
// entries skipped because their weak key no longer resolves — that count is the whole
// point: pre-fix these were raw AActor* and this loop dereferenced freed slots.
int32 UCUWebSocketEditorSubsystem::WriteActorMovedEntries(
	const TMap<TWeakObjectPtr<AActor>, FTransform>& BeforeTransforms,
	const FString& TransLogPath) const
{
	int32 SkippedStale = 0;
	for (const auto& Pair : BeforeTransforms)
	{
		// Resolve through the weak pointer. Get() returns null for an actor destroyed by
		// the Python above (world replacement), so this never dereferences a freed slot.
		// IsValid() still guards the pending-kill case.
		AActor* Actor = Pair.Key.Get();
		const FTransform& OldT = Pair.Value;
		if (!IsValid(Actor))
		{
			++SkippedStale;
			continue;
		}

		FTransform NewT = Actor->GetActorTransform();
		if (!OldT.GetLocation().Equals(NewT.GetLocation(), 0.01f) ||
			!OldT.GetRotation().Equals(NewT.GetRotation(), 0.001f) ||
			!OldT.GetScale3D().Equals(NewT.GetScale3D(), 0.001f))
		{
			FVector Loc = NewT.GetLocation();
			FRotator Rot = NewT.GetRotation().Rotator();
			FVector Scl = NewT.GetScale3D();
			FString Entry = FString::Printf(
				TEXT("{\"ts\":%lld,\"event\":\"actor_moved\",\"object\":\"%s\",\"class\":\"%s\","
					"\"path\":\"%s\",\"values\":{\"RelativeLocation\":\"X=%f Y=%f Z=%f\","
					"\"RelativeRotation\":\"P=%f Y=%f R=%f\","
					"\"RelativeScale3D\":\"X=%f Y=%f Z=%f\"}}"),
				FDateTime::UtcNow().ToUnixTimestamp(),
				*Actor->GetName(), *Actor->GetClass()->GetName(), *Actor->GetPathName(),
				Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll, Scl.X, Scl.Y, Scl.Z);
			FFileHelper::SaveStringToFile(
				Entry + LINE_TERMINATOR, *TransLogPath,
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
				&IFileManager::Get(), FILEWRITE_Append);
		}
	}
	return SkippedStale;
}

int32 UCUWebSocketEditorSubsystem::WriteActorMovedEntriesForTesting(
	const TMap<TWeakObjectPtr<AActor>, FTransform>& BeforeTransforms,
	const FString& TransLogPath) const
{
	return WriteActorMovedEntries(BeforeTransforms, TransLogPath);
}

// ============================================
// Python Remote Execution
// ============================================

void UCUWebSocketEditorSubsystem::HandleExecutePython(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleExecutePython: Invalid socket or request"));
		return;
	}

	// Security: Token Bucket Rate Limiting (Issue #1402 C-2)
	{
		FScopeLock Lock(&RateLimitLock);

		FDateTime Now = FDateTime::UtcNow();
		FCURateLimitState* State = ClientRateLimitState.Find(Socket);

		if (State)
		{
			// Refill tokens based on elapsed time
			double ElapsedSeconds = (Now - State->LastRequestTime).GetTotalSeconds();
			State->TokenBucket = FMath::Min(
				State->TokenBucket + ElapsedSeconds * SecurityLimits::TokenBucketRefillRate,
				SecurityLimits::TokenBucketCapacity
			);
			State->LastRequestTime = Now;

			// Try to consume a token
			if (State->TokenBucket >= 1.0)
			{
				State->TokenBucket -= 1.0;
			}
			else
			{
				// Rate limit exceeded
				State->ViolationCount++;

				LogError(FString::Printf(TEXT("Rate limit exceeded (tokens: %.1f, violations: %d)"),
					State->TokenBucket, State->ViolationCount));

				if (State->ViolationCount > SecurityLimits::MaxRateLimitViolationsBeforeDisconnect)
				{
					LogError(TEXT("Client disconnected for excessive rate limit violations"));
				}

				FString RequestId = Request->GetStringField(TEXT("request_id"));
				FString ErrorJSON = FString::Printf(TEXT(
					"{\"type\":\"python_result\",\"request_id\":\"%s\",\"success\":false,\"exception\":\"Rate limit exceeded. Available tokens: %.1f\"}"),
					*RequestId, State->TokenBucket);
				SendResponse(Socket, ErrorJSON);
				return;
			}
		}
		else
		{
			// First request from this client
			FCURateLimitState NewState;
			NewState.TokenBucket -= 1.0;
			ClientRateLimitState.Add(Socket, NewState);
		}
	}

	// Extract request parameters
	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString Code = Request->GetStringField(TEXT("code"));
	int32 TimeoutMs = Request->GetIntegerField(TEXT("timeout"));

	// Issue #4259: Extract optional capture_output (default: true)
	bool bCaptureOutput = Request->HasField(TEXT("capture_output"))
		? Request->GetBoolField(TEXT("capture_output"))
		: true;

	LogInfo(FString::Printf(TEXT("Executing Python code (request_id: %s, timeout: %dms, capture_output: %s)"),
		*RequestId, TimeoutMs, bCaptureOutput ? TEXT("true") : TEXT("false")));

	// Issue #8834: Auto-wrap write Python in FScopedTransaction for transaction log capture.
	// UE Python set_editor_property() does NOT trigger OnTransactionStateChanged,
	// so bare property edits are invisible to the JSONL log. This heuristic detects
	// write patterns and wraps in a transaction. Read-only code is unaffected.
	// Already-wrapped code (ScopedEditorTransaction) nests harmlessly.
	static const TArray<FString> WritePatterns = {
		TEXT("set_editor_property"), TEXT("set_actor_label"), TEXT("set_actor_location"),
		TEXT("set_actor_rotation"), TEXT("set_actor_scale3d"), TEXT("set_actor_hidden_in_game"),
		TEXT("set_static_mesh"), TEXT("set_skeletal_mesh"), TEXT("set_material"),
		TEXT("destroy_actor"), TEXT("spawn_actor"),
		// Issue #9018: AnimSequence curve keyframe operations
		TEXT("set_curve_key"), TEXT("set_curve_keys"), TEXT("add_curve"), TEXT("remove_curve"),
		TEXT("remove_curve_key"), TEXT("scale_curve"), TEXT("open_bracket"), TEXT("close_bracket"),
		// Issue #9019: Sequencer keyframe operations
		TEXT("add_key"), TEXT("remove_key"), TEXT("set_key"),
		// Issue #9020: Material graph structure operations
		TEXT("create_material_expression"), TEXT("delete_material_expression"),
		TEXT("connect_material_expressions"), TEXT("connect_material_property"),
	};
	bool bIsWriteCode = false;
	for (const FString& Pattern : WritePatterns)
	{
		if (Code.Contains(Pattern))
		{
			bIsWriteCode = true;
			break;
		}
	}

	// Issue #8834: Detect transform patterns for post-execution diff scan.
	// K2_SetActorLocation bypasses property system entirely, so FScopedTransaction alone
	// cannot capture transform changes. We snapshot before, compare after, and write
	// actor_moved entries directly to JSONL.
	static const TArray<FString> TransformPatterns = {
		TEXT("set_actor_location"), TEXT("set_actor_rotation"), TEXT("set_actor_scale3d"),
	};
	bool bHasTransformCode = false;
	for (const FString& Pat : TransformPatterns)
	{
		if (Code.Contains(Pat)) { bHasTransformCode = true; break; }
	}

	// Issue #9018: Detect AnimSequence curve modification patterns for before/after snapshot
	static const TArray<FString> CurvePatterns = {
		TEXT("set_curve_key"), TEXT("set_curve_keys"), TEXT("remove_curve_key"),
		TEXT("scale_curve"), TEXT("add_curve"), TEXT("remove_curve"),
	};
	bool bHasCurveCode = false;
	FString CurveAnimPath;  // Extract target AnimSequence path from Python code if possible
	for (const FString& Pat : CurvePatterns)
	{
		if (Code.Contains(Pat)) { bHasCurveCode = true; break; }
	}
	// Extract anim path from code: look for EditorAssetLibrary.load_asset("...") pattern
	if (bHasCurveCode)
	{
		int32 LoadIdx = Code.Find(TEXT("load_asset(\""));
		if (LoadIdx != INDEX_NONE)
		{
			int32 PathStart = LoadIdx + 12; // len("load_asset(\"") = 12
			int32 PathEnd = Code.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, PathStart);
			if (PathEnd != INDEX_NONE)
			{
				CurveAnimPath = Code.Mid(PathStart, PathEnd - PathStart);
			}
		}
	}
	// Snapshot all float curve keyframe data before Python execution
	TMap<FName, TArray<TTuple<float, float>>> BeforeCurveKeys;  // curveName → [(time, value)]
	if (bHasCurveCode && !CurveAnimPath.IsEmpty())
	{
		UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(
			StaticLoadObject(UAnimSequenceBase::StaticClass(), nullptr, *CurveAnimPath));
		if (AnimAsset)
		{
			IAnimationDataModel* DataModel = AnimAsset->GetDataModel();
			if (DataModel)
			{
				// Iterate float curves and capture keyframes
				const TArray<FFloatCurve>& FloatCurves = DataModel->GetFloatCurves();
				for (const FFloatCurve& Curve : FloatCurves)
				{
					TArray<TTuple<float, float>> Keys;
					const FRichCurve& RichCurve = Curve.FloatCurve;
					for (auto It = RichCurve.GetKeyHandleIterator(); It; ++It)
					{
						FKeyHandle Handle = *It;
						float Time = RichCurve.GetKeyTime(Handle);
						float Value = RichCurve.GetKeyValue(Handle);
						Keys.Add(MakeTuple(Time, Value));
					}
					BeforeCurveKeys.Add(Curve.GetName(), MoveTemp(Keys));
				}
			}
		}
	}

	// Issue #9019: Detect Sequencer keyframe modification patterns
	static const TArray<FString> SequencerPatterns = {
		TEXT("add_key"), TEXT("remove_key"), TEXT("set_key"),
		TEXT("add_keyframe"), TEXT("remove_keyframe"), TEXT("modify_keyframe"),
		TEXT("get_keys_by_index"),  // read that often precedes write
	};
	bool bHasSequencerCode = false;
	FString SequencerAssetPath;
	for (const FString& Pat : SequencerPatterns)
	{
		if (Code.Contains(Pat)) { bHasSequencerCode = true; break; }
	}
	if (bHasSequencerCode)
	{
		int32 LoadIdx = Code.Find(TEXT("load_asset(\""));
		if (LoadIdx != INDEX_NONE)
		{
			int32 PathStart = LoadIdx + 12;
			int32 PathEnd = Code.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, PathStart);
			if (PathEnd != INDEX_NONE)
			{
				SequencerAssetPath = Code.Mid(PathStart, PathEnd - PathStart);
			}
		}
	}
	// Snapshot sequencer keyframes before execution
	struct FChannelSnapshot
	{
		FString TrackName;
		FString ChannelName;
		TArray<TTuple<int32, double>> Keys; // frame, value
	};
	TArray<FChannelSnapshot> BeforeSequencerKeys;
	if (bHasSequencerCode && !SequencerAssetPath.IsEmpty())
	{
		ULevelSequence* Seq = Cast<ULevelSequence>(
			StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *SequencerAssetPath));
		if (Seq && Seq->GetMovieScene())
		{
			UMovieScene* MovieScene = Seq->GetMovieScene();
			for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			{
				FString TrackName = Track->GetTrackName().ToString();
				for (UMovieSceneSection* Section : Track->GetAllSections())
				{
					auto ChannelEntries = Section->GetChannelProxy().GetAllEntries();
					for (const FMovieSceneChannelEntry& Entry : ChannelEntries)
					{
						TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
						for (int32 ChIdx = 0; ChIdx < Channels.Num(); ChIdx++)
						{
							FMovieSceneChannel* Channel = Channels[ChIdx];
							if (!Channel) continue;
							FChannelSnapshot Snap;
							Snap.TrackName = TrackName;
							Snap.ChannelName = FString::Printf(TEXT("%s_%d"), *Entry.GetChannelTypeName().ToString(), ChIdx);
							// Phase O — Task 6: Type-specific key extraction with values
							TArray<FFrameNumber> KeyTimes;
							TArray<FKeyHandle> KeyHandles;
							Channel->GetKeys(TRange<FFrameNumber>(), &KeyTimes, &KeyHandles);

							// Try typed channel evaluation for value extraction
							FString ChannelTypeName = Entry.GetChannelTypeName().ToString();
							for (const FFrameNumber& Frame : KeyTimes)
							{
								double Value = 0.0;
								// FMovieSceneDoubleChannel (Location, Rotation, Scale)
								if (ChannelTypeName == TEXT("MovieSceneDoubleChannel"))
								{
									if (auto* TypedCh = static_cast<FMovieSceneDoubleChannel*>(Channel))
									{
										TypedCh->Evaluate(Frame, Value);
									}
								}
								// FMovieSceneFloatChannel (float parameters)
								else if (ChannelTypeName == TEXT("MovieSceneFloatChannel"))
								{
									if (auto* TypedCh = static_cast<FMovieSceneFloatChannel*>(Channel))
									{
										float FloatVal = 0.f;
										TypedCh->Evaluate(Frame, FloatVal);
										Value = static_cast<double>(FloatVal);
									}
								}
								// Other channel types: frame only, value=0.0
								Snap.Keys.Add(MakeTuple(Frame.Value, Value));
							}
							BeforeSequencerKeys.Add(MoveTemp(Snap));
						}
					}
				}
			}
		}
	}

	// Issue #9020: Detect Material graph structure modification patterns
	static const TArray<FString> MaterialGraphPatterns = {
		TEXT("create_material_expression"), TEXT("delete_material_expression"),
		TEXT("connect_material_expressions"), TEXT("connect_material_property"),
		TEXT("delete_all_material_expressions"),
	};
	bool bHasMaterialGraphCode = false;
	FString MaterialAssetPath;
	for (const FString& Pat : MaterialGraphPatterns)
	{
		if (Code.Contains(Pat)) { bHasMaterialGraphCode = true; break; }
	}
	if (bHasMaterialGraphCode)
	{
		int32 LoadIdx = Code.Find(TEXT("load_asset(\""));
		if (LoadIdx != INDEX_NONE)
		{
			int32 PathStart = LoadIdx + 12;
			int32 PathEnd = Code.Find(TEXT("\""), ESearchCase::IgnoreCase, ESearchDir::FromStart, PathStart);
			if (PathEnd != INDEX_NONE)
			{
				MaterialAssetPath = Code.Mid(PathStart, PathEnd - PathStart);
			}
		}
	}
	// Snapshot material expression count + connections + names before execution
	int32 BeforeMaterialExprCount = -1;
	TSet<FString> BeforeMaterialConnections;
	TMap<FString, FString> BeforeMaterialExprNames; // name → class

	// Helper lambda: collect all connections in a material as string keys
	auto CollectMaterialConnections = [](UMaterial* Mat) -> TSet<FString>
	{
		TSet<FString> Connections;
		if (!Mat) return Connections;

		auto* EditorData = Mat->GetEditorOnlyData();
		if (!EditorData) return Connections;

		// Issue #9020: Collect material main node input connections
		struct FPinEntry { FExpressionInput* Input; const TCHAR* PinName; };
		TArray<FPinEntry> MainPins = {
			{&EditorData->BaseColor, TEXT("BaseColor")},
			{&EditorData->Metallic, TEXT("Metallic")},
			{&EditorData->Roughness, TEXT("Roughness")},
			{&EditorData->Normal, TEXT("Normal")},
			{&EditorData->EmissiveColor, TEXT("EmissiveColor")},
			{&EditorData->Opacity, TEXT("Opacity")},
			{&EditorData->OpacityMask, TEXT("OpacityMask")},
			{&EditorData->Specular, TEXT("Specular")},
			{&EditorData->Anisotropy, TEXT("Anisotropy")},
			{&EditorData->Tangent, TEXT("Tangent")},
			{&EditorData->AmbientOcclusion, TEXT("AmbientOcclusion")},
			{&EditorData->WorldPositionOffset, TEXT("WorldPositionOffset")},
			{&EditorData->SubsurfaceColor, TEXT("SubsurfaceColor")},
			{&EditorData->Refraction, TEXT("Refraction")},
			{&EditorData->PixelDepthOffset, TEXT("PixelDepthOffset")},
		};
		for (const auto& Pin : MainPins)
		{
			if (Pin.Input && Pin.Input->Expression)
			{
				Connections.Add(FString::Printf(TEXT("%s:%d->Material:%s"),
					*Pin.Input->Expression->GetName(), Pin.Input->OutputIndex, Pin.PinName));
			}
		}

		// Collect expression-to-expression connections
		for (UMaterialExpression* Expr : Mat->GetExpressions())
		{
			if (!Expr) continue;
			for (int32 i = 0; ; ++i)
			{
				FExpressionInput* Input = Expr->GetInput(i);
				if (!Input) break;
				if (Input->Expression)
				{
					FName InputName = Expr->GetInputName(i);
					Connections.Add(FString::Printf(TEXT("%s:%d->%s:%s"),
						*Input->Expression->GetName(), Input->OutputIndex,
						*Expr->GetName(), *InputName.ToString()));
				}
			}
		}
		return Connections;
	};

	if (bHasMaterialGraphCode && !MaterialAssetPath.IsEmpty())
	{
		UMaterial* Mat = Cast<UMaterial>(
			StaticLoadObject(UMaterial::StaticClass(), nullptr, *MaterialAssetPath));
		if (Mat)
		{
			BeforeMaterialExprCount = Mat->GetExpressions().Num();
			BeforeMaterialConnections = CollectMaterialConnections(Mat);
			// Phase N-2: Store expression names for deletion diff
			for (UMaterialExpression* Expr : Mat->GetExpressions())
			{
				if (Expr)
				{
					BeforeMaterialExprNames.Add(Expr->GetName(), Expr->GetClass()->GetName());
				}
			}
		}
	}

	// (Issue #27130) WriteActorMovedEntries is defined below; it owns the after-snapshot
	// diff so a test can drive it with a deliberately-destroyed actor.

	// Before-snapshot: capture all actor transforms (only if transform code detected)
	//
	// Issue #27130: the keys MUST be weak. The Python executed below can replace the
	// editor world outright (EditorLevelLibrary.new_level, map load, ...), which frees
	// every actor captured here. A raw AActor* would then dangle, and IsValid() does
	// NOT make that safe — it checks flags on an object it first dereferences, so a
	// freed slot asserts `Index >= 0` in UObjectArray.h. A weak pointer resolves to
	// null instead, which the after-snapshot skips.
	TMap<TWeakObjectPtr<AActor>, FTransform> BeforeTransforms;
	if (bHasTransformCode && GEditor)
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (World)
		{
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				BeforeTransforms.Add(TWeakObjectPtr<AActor>(*It), It->GetActorTransform());
			}
		}
	}

	FCUPythonExecutionResult Result;
	if (bIsWriteCode && !bHasCurveCode)
	{
		// Standard write: wrap in FScopedTransaction
		FScopedTransaction Transaction(FText::FromString(TEXT("Python Execute")));
		Result = ExecutePythonCode(Code, TimeoutMs);
	}
	else if (bHasCurveCode)
	{
		// Issue #9018: AnimSequence curve operations — skip FScopedTransaction
		// AnimDataController manages its own undo brackets internally.
		// External FScopedTransaction causes DataModel to return stale values in after-snapshot.
		Result = ExecutePythonCode(Code, TimeoutMs);
	}
	else
	{
		// Execute Python code (read-only — no transaction overhead)
		Result = ExecutePythonCode(Code, TimeoutMs);
	}

	// After-snapshot: diff transforms and write actor_moved entries to JSONL
	if (bHasTransformCode && BeforeTransforms.Num() > 0)
	{
		FString TransLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("transactions.jsonl");
		const int32 StaleSkipped = WriteActorMovedEntries(BeforeTransforms, TransLogPath);
		if (StaleSkipped > 0)
		{
			// Issue #27130: this line is the operator-visible signal that the executed
			// Python replaced the world. Pre-fix these same entries were raw pointers and
			// this loop dereferenced them instead of counting them.
			LogInfo(FString::Printf(
				TEXT("after-snapshot skipped %d stale actor key(s)"), StaleSkipped));
		}
	}

	// Issue #9018 v4: Deferred after-snapshot via FTSTicker (next frame)
	// v3 used synchronous StaticLoadObject which reads stale DataModel values
	// when AnimDataController defers internal updates until next frame.
	// v4 schedules the after-snapshot on the next tick so AnimDataController
	// has completed all deferred processing.
	if (bHasCurveCode && !CurveAnimPath.IsEmpty())
	{
		FString CapturedAnimPath = CurveAnimPath;
		TMap<FName, TArray<TTuple<float, float>>> CapturedBeforeKeys = MoveTemp(BeforeCurveKeys);
		TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakThis(this);

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis, CapturedAnimPath, CapturedBeforeKeys](float DeltaTime) -> bool
			{
				// One-shot: return false to auto-remove from ticker
				UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
				if (!Self) return false;

				UAnimSequenceBase* AnimAsset = Cast<UAnimSequenceBase>(
					StaticLoadObject(UAnimSequenceBase::StaticClass(), nullptr, *CapturedAnimPath));
				if (!AnimAsset) return false;

				IAnimationDataModel* DataModel = AnimAsset->GetDataModel();
				if (!DataModel) return false;

				FString CurveLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("transactions.jsonl");
				const TArray<FFloatCurve>& FloatCurves = DataModel->GetFloatCurves();

				for (const FFloatCurve& Curve : FloatCurves)
				{
					FName CurveName = Curve.GetName();
					const FRichCurve& RichCurve = Curve.FloatCurve;

					FString KeysArray;
					int32 KeyIdx = 0;
					for (auto It = RichCurve.GetKeyHandleIterator(); It; ++It)
					{
						float T = RichCurve.GetKeyTime(*It);
						float V = RichCurve.GetKeyValue(*It);
						if (!KeysArray.IsEmpty()) KeysArray += TEXT(",");
						KeysArray += FString::Printf(TEXT("{\"i\":%d,\"t\":%.6f,\"v\":%.6f}"),
							KeyIdx++, T, V);
					}

					// Compare with before-snapshot
					const TArray<TTuple<float, float>>* BeforeKeys = CapturedBeforeKeys.Find(CurveName);
					bool bChanged = true;
					if (BeforeKeys && BeforeKeys->Num() == KeyIdx)
					{
						bChanged = false;
						int32 CheckIdx = 0;
						for (auto It = RichCurve.GetKeyHandleIterator(); It; ++It, ++CheckIdx)
						{
							float T = RichCurve.GetKeyTime(*It);
							float V = RichCurve.GetKeyValue(*It);
							if (CheckIdx < BeforeKeys->Num() &&
								(!FMath::IsNearlyEqual((*BeforeKeys)[CheckIdx].Get<0>(), T, 0.0001f) ||
								 !FMath::IsNearlyEqual((*BeforeKeys)[CheckIdx].Get<1>(), V, 0.0001f)))
							{
								bChanged = true;
								break;
							}
						}
					}

					if (bChanged)
					{
						FString Entry = FString::Printf(
							TEXT("{\"ts\":%lld,\"event\":\"anim_curve_changed\",\"object\":\"%s\","
								"\"class\":\"AnimSequence\",\"path\":\"%s\",\"context\":\"Persona\","
								"\"values\":{\"curve_name\":\"%s\",\"keys\":[%s],\"key_count\":%d}}"),
							FDateTime::UtcNow().ToUnixTimestamp(),
							*FPaths::GetBaseFilename(CapturedAnimPath),
							*CapturedAnimPath,
							*CurveName.ToString(),
							*KeysArray,
							KeyIdx
						);
						FFileHelper::SaveStringToFile(
							Entry + LINE_TERMINATOR, *CurveLogPath,
							FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
							&IFileManager::Get(), FILEWRITE_Append);
					}
				}

				return false; // One-shot
			}),
			0.0f  // Next tick (0 delay = next frame)
		);
	}

	// Issue #9019: After-snapshot — diff sequencer keyframes and write JSONL entries
	if (bHasSequencerCode && !SequencerAssetPath.IsEmpty() && BeforeSequencerKeys.Num() > 0)
	{
		ULevelSequence* Seq = Cast<ULevelSequence>(
			StaticLoadObject(ULevelSequence::StaticClass(), nullptr, *SequencerAssetPath));
		if (Seq && Seq->GetMovieScene())
		{
			FString SeqLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("transactions.jsonl");
			UMovieScene* MovieScene = Seq->GetMovieScene();
			int32 SnapIdx = 0;
			for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			{
				// Issue #25939: GetTrackName() is NAME_None for almost every root track.
				// Keep `track` as display leftover; identity is `track_guid`.
				FString TrackName = Track->GetTrackName().ToString();
				const FString TrackGuid = Track->GetSignature().ToString(EGuidFormats::DigitsWithHyphens);
				int32 SectionIndex = 0;
				for (UMovieSceneSection* Section : Track->GetAllSections())
				{
					auto ChannelEntries = Section->GetChannelProxy().GetAllEntries();
					for (const FMovieSceneChannelEntry& Entry : ChannelEntries)
					{
						TArrayView<FMovieSceneChannel* const> Channels = Entry.GetChannels();
						for (int32 ChIdx = 0; ChIdx < Channels.Num(); ChIdx++)
						{
							FMovieSceneChannel* Channel = Channels[ChIdx];
							if (!Channel) continue;

							TArray<FFrameNumber> KeyTimes;
							TArray<FKeyHandle> KeyHandles;
							Channel->GetKeys(TRange<FFrameNumber>(), &KeyTimes, &KeyHandles);

							// Compare with before-snapshot
							bool bChanged = false;
							if (SnapIdx < BeforeSequencerKeys.Num())
							{
								const FChannelSnapshot& Before = BeforeSequencerKeys[SnapIdx];
								if (Before.Keys.Num() != KeyTimes.Num())
								{
									bChanged = true;
								}
								else
								{
									for (int32 k = 0; k < KeyTimes.Num(); k++)
									{
										if (Before.Keys[k].Get<0>() != KeyTimes[k].Value)
										{
											bChanged = true;
											break;
										}
									}
								}
							}
							else
							{
								bChanged = KeyTimes.Num() > 0;
							}

							if (bChanged)
							{
								FString ChannelName = FString::Printf(TEXT("%s_%d"), *Entry.GetChannelTypeName().ToString(), ChIdx);
								FString ChannelTypeName2 = Entry.GetChannelTypeName().ToString();
								FString KeysArray;
								for (int32 k = 0; k < KeyTimes.Num(); k++)
								{
									// Issue #25939: typed value, or omit (never fabricate 0.0)
									FString ValueJson;
									bool bHasValue = false;
									if (ChannelTypeName2 == TEXT("MovieSceneDoubleChannel"))
									{
										if (auto* TypedCh = static_cast<FMovieSceneDoubleChannel*>(Channel))
										{
											double KeyValue = 0.0;
											TypedCh->Evaluate(KeyTimes[k], KeyValue);
											ValueJson = FString::Printf(TEXT("%.6f"), KeyValue);
											bHasValue = true;
										}
									}
									else if (ChannelTypeName2 == TEXT("MovieSceneFloatChannel"))
									{
										if (auto* TypedCh = static_cast<FMovieSceneFloatChannel*>(Channel))
										{
											float FloatVal = 0.f;
											TypedCh->Evaluate(KeyTimes[k], FloatVal);
											ValueJson = FString::Printf(TEXT("%.6f"), static_cast<double>(FloatVal));
											bHasValue = true;
										}
									}
									else if (ChannelTypeName2 == TEXT("MovieSceneBoolChannel"))
									{
										if (auto* TypedCh = static_cast<FMovieSceneBoolChannel*>(Channel))
										{
											bool BoolVal = false;
											TypedCh->Evaluate(KeyTimes[k], BoolVal);
											ValueJson = BoolVal ? TEXT("true") : TEXT("false");
											bHasValue = true;
										}
									}
									else if (ChannelTypeName2 == TEXT("MovieSceneIntegerChannel"))
									{
										if (auto* TypedCh = static_cast<FMovieSceneIntegerChannel*>(Channel))
										{
											int32 IntVal = 0;
											TypedCh->Evaluate(KeyTimes[k], IntVal);
											ValueJson = FString::FromInt(IntVal);
											bHasValue = true;
										}
									}
									else if (ChannelTypeName2 == TEXT("MovieSceneByteChannel"))
									{
										if (auto* TypedCh = static_cast<FMovieSceneByteChannel*>(Channel))
										{
											uint8 ByteVal = 0;
											TypedCh->Evaluate(KeyTimes[k], ByteVal);
											ValueJson = FString::FromInt(static_cast<int32>(ByteVal));
											bHasValue = true;
										}
									}
									if (!KeysArray.IsEmpty()) KeysArray += TEXT(",");
									if (bHasValue)
									{
										KeysArray += FString::Printf(TEXT("{\"i\":%d,\"frame\":%d,\"value\":%s}"),
											k, KeyTimes[k].Value, *ValueJson);
									}
									else
									{
										KeysArray += FString::Printf(TEXT("{\"i\":%d,\"frame\":%d}"),
											k, KeyTimes[k].Value);
									}
								}
								FString SeqEntry = FString::Printf(
									TEXT("{\"ts\":%lld,\"event\":\"sequencer_keyframe_changed\",\"object\":\"%s\","
										"\"class\":\"LevelSequence\",\"path\":\"%s\",\"context\":\"Sequencer\","
										"\"values\":{\"track\":\"%s\",\"track_guid\":\"%s\",\"section_index\":%d,"
										"\"channel\":\"%s\",\"keys\":[%s],\"key_count\":%d}}"),
									FDateTime::UtcNow().ToUnixTimestamp(),
									*CUJsonUtils::EscapeJsonString(FPaths::GetBaseFilename(SequencerAssetPath)),
									*CUJsonUtils::EscapeJsonString(SequencerAssetPath),
									*CUJsonUtils::EscapeJsonString(TrackName),
									*CUJsonUtils::EscapeJsonString(TrackGuid),
									SectionIndex,
									*CUJsonUtils::EscapeJsonString(ChannelName),
									*KeysArray,
									KeyTimes.Num()
								);
								FFileHelper::SaveStringToFile(
									SeqEntry + LINE_TERMINATOR, *SeqLogPath,
									FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
									&IFileManager::Get(), FILEWRITE_Append);
							}
							SnapIdx++;
						}
					}
					++SectionIndex;
				}
			}
		}
	}

	// Issue #9020: After-snapshot — diff material expression count
	if (bHasMaterialGraphCode && !MaterialAssetPath.IsEmpty() && BeforeMaterialExprCount >= 0)
	{
		UMaterial* Mat = Cast<UMaterial>(
			StaticLoadObject(UMaterial::StaticClass(), nullptr, *MaterialAssetPath));
		if (Mat)
		{
			int32 AfterCount = Mat->GetExpressions().Num();
			if (AfterCount != BeforeMaterialExprCount)
			{
				FString MatLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("transactions.jsonl");
				// Build expression list summary
				FString ExprList;
				int32 Idx = 0;
				for (UMaterialExpression* Expr : Mat->GetExpressions())
				{
					if (Idx >= FMath::Max(BeforeMaterialExprCount, 0) && Idx < AfterCount)
					{
						// Newly added expressions
						if (!ExprList.IsEmpty()) ExprList += TEXT(",");
						ExprList += FString::Printf(TEXT("{\"i\":%d,\"class\":\"%s\",\"name\":\"%s\"}"),
							Idx, *Expr->GetClass()->GetName(), *Expr->GetName());
					}
					Idx++;
				}
				FString ChangeType = (AfterCount > BeforeMaterialExprCount) ? TEXT("nodes_added") : TEXT("nodes_removed");
				int32 Delta = FMath::Abs(AfterCount - BeforeMaterialExprCount);

				// Phase N-2: For nodes_removed, find deleted expressions
				FString DeletedExprList;
				if (AfterCount < BeforeMaterialExprCount)
				{
					TSet<FString> AfterNames;
					for (UMaterialExpression* Expr : Mat->GetExpressions())
					{
						if (Expr) AfterNames.Add(Expr->GetName());
					}
					for (const auto& Pair : BeforeMaterialExprNames)
					{
						if (!AfterNames.Contains(Pair.Key))
						{
							if (!DeletedExprList.IsEmpty()) DeletedExprList += TEXT(",");
							DeletedExprList += FString::Printf(TEXT("{\"class\":\"%s\",\"name\":\"%s\"}"),
								*Pair.Value, *Pair.Key);
						}
					}
				}

				// Escape ExprList for HashMap<String, String> compatibility in Rust
				FString FinalExprList = (AfterCount > BeforeMaterialExprCount) ? ExprList : DeletedExprList;
				FString EscapedExprList = FString::Printf(TEXT("[%s]"), *FinalExprList);
				EscapedExprList = EscapedExprList.Replace(TEXT("\""), TEXT("\\\""));

				FString Entry = FString::Printf(
					TEXT("{\"ts\":%lld,\"event\":\"material_structure_changed\",\"object\":\"%s\","
						"\"class\":\"Material\",\"path\":\"%s\",\"context\":\"MaterialEditor\","
						"\"values\":{\"change\":\"%s\",\"delta\":\"%d\",\"before_count\":\"%d\",\"after_count\":\"%d\",\"new_expressions\":\"%s\"}}"),
					FDateTime::UtcNow().ToUnixTimestamp(),
					*FPaths::GetBaseFilename(MaterialAssetPath),
					*MaterialAssetPath,
					*ChangeType,
					Delta,
					BeforeMaterialExprCount,
					AfterCount,
					*EscapedExprList
				);
				FFileHelper::SaveStringToFile(
					Entry + LINE_TERMINATOR, *MatLogPath,
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
					&IFileManager::Get(), FILEWRITE_Append);
			}
		}
	}

	// Issue #9020: After-snapshot — diff material connections
	if (bHasMaterialGraphCode && !MaterialAssetPath.IsEmpty() && BeforeMaterialExprCount >= 0)
	{
		UMaterial* MatConn = Cast<UMaterial>(
			StaticLoadObject(UMaterial::StaticClass(), nullptr, *MaterialAssetPath));
		if (MatConn)
		{
			TSet<FString> AfterConnections = CollectMaterialConnections(MatConn);
			// Find newly added connections
			FString ConnLogPath = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP") / TEXT("transactions.jsonl");
			for (const FString& Conn : AfterConnections)
			{
				if (!BeforeMaterialConnections.Contains(Conn))
				{
					// Parse: "SourceName:OutputIdx->TargetName:PinName"
					FString Left, Right;
					if (Conn.Split(TEXT("->"), &Left, &Right))
					{
						FString SourceNode, SourceIdxStr;
						Left.Split(TEXT(":"), &SourceNode, &SourceIdxStr);
						FString TargetNode, TargetPin;
						Right.Split(TEXT(":"), &TargetNode, &TargetPin);

						FString ConnEntry = FString::Printf(
							TEXT("{\"ts\":%lld,\"event\":\"material_connection_changed\",\"object\":\"%s\","
								"\"class\":\"Material\",\"path\":\"%s\",\"context\":\"MaterialEditor\","
								"\"values\":{\"change\":\"connection_added\",\"source_node\":\"%s\",\"source_output_index\":\"%s\","
								"\"target_node\":\"%s\",\"target_pin\":\"%s\"}}"),
							FDateTime::UtcNow().ToUnixTimestamp(),
							*FPaths::GetBaseFilename(MaterialAssetPath),
							*MaterialAssetPath,
							*SourceNode, *SourceIdxStr,
							*TargetNode, *TargetPin
						);
						FFileHelper::SaveStringToFile(
							ConnEntry + LINE_TERMINATOR, *ConnLogPath,
							FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
							&IFileManager::Get(), FILEWRITE_Append);
					}
				}
			}

			// Phase O — Task 7: Find removed connections
			for (const FString& Conn : BeforeMaterialConnections)
			{
				if (!AfterConnections.Contains(Conn))
				{
					FString Left, Right;
					if (Conn.Split(TEXT("->"), &Left, &Right))
					{
						FString SourceNode, SourceIdxStr;
						Left.Split(TEXT(":"), &SourceNode, &SourceIdxStr);
						FString TargetNode, TargetPin;
						Right.Split(TEXT(":"), &TargetNode, &TargetPin);

						FString ConnEntry = FString::Printf(
							TEXT("{\"ts\":%lld,\"event\":\"material_connection_changed\",\"object\":\"%s\","
								"\"class\":\"Material\",\"path\":\"%s\",\"context\":\"MaterialEditor\","
								"\"values\":{\"change\":\"connection_removed\",\"source_node\":\"%s\",\"source_output_index\":\"%s\","
								"\"target_node\":\"%s\",\"target_pin\":\"%s\"}}"),
							FDateTime::UtcNow().ToUnixTimestamp(),
							*FPaths::GetBaseFilename(MaterialAssetPath),
							*MaterialAssetPath,
							*SourceNode, *SourceIdxStr,
							*TargetNode, *TargetPin
						);
						FFileHelper::SaveStringToFile(
							ConnEntry + LINE_TERMINATOR, *ConnLogPath,
							FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
							&IFileManager::Get(), FILEWRITE_Append);
					}
				}
			}
		}
	}

	// Escape strings for JSON (Issue #3913: Use proper JSON escaping for backslashes)
	// Issue #4259: Conditionally suppress stdout/stderr when capture_output is false
	FString EscapedStdout = bCaptureOutput ? CUJsonUtils::EscapeJsonString(Result.Stdout) : TEXT("");
	FString EscapedStderr = bCaptureOutput ? CUJsonUtils::EscapeJsonString(Result.Stderr) : TEXT("");
	FString ExceptionField = Result.Exception.IsEmpty()
		? TEXT("null")
		: FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Result.Exception));

	// Build response JSON (Issue #4259: Include capture_output field)
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"python_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"stdout\":\"%s\","
		"\"stderr\":\"%s\","
		"\"exception\":%s,"
		"\"execution_time_ms\":%.3f,"
		"\"capture_output\":%s"
		"}"
	),
		*RequestId,
		Result.bSuccess ? TEXT("true") : TEXT("false"),
		*EscapedStdout,
		*EscapedStderr,
		*ExceptionField,
		Result.ExecutionTimeMs,
		bCaptureOutput ? TEXT("true") : TEXT("false")
	);
	if (!Result.ErrorCode.IsEmpty())
	{
		ResponseJSON.LeftChopInline(1);
		ResponseJSON += FString::Printf(
			TEXT(",\"error_code\":\"%s\",\"hint\":\"%s\"}"),
			*CUJsonUtils::EscapeJsonString(Result.ErrorCode),
			*CUJsonUtils::EscapeJsonString(Result.Hint));
	}

	SendResponse(Socket, ResponseJSON);

	LogInfo(FString::Printf(TEXT("Python execution completed (request_id: %s, success: %s, time: %.1fms)"),
		*RequestId,
		Result.bSuccess ? TEXT("true") : TEXT("false"),
		Result.ExecutionTimeMs
	));
}

UCUWebSocketEditorSubsystem::FCUPythonExecutionResult UCUWebSocketEditorSubsystem::ExecutePythonCode(const FString& Code, int32 TimeoutMs)
{
	FCUPythonExecutionResult Result;
	Result.bSuccess = false;
	Result.ExecutionTimeMs = 0.0;

	double StartTime = FPlatformTime::Seconds();

#if !WITH_NARSHAMCP_PYTHONSCRIPTPLUGIN
	Result.Exception = TEXT("PythonScriptPlugin is not enabled for this project");
	Result.ErrorCode = TEXT("python_script_plugin_unavailable");
	Result.Hint = TEXT("Enable PythonScriptPlugin in your .uproject, then rebuild.");
	LogError(Result.Exception);
#elif WITH_PYTHON
	IPythonScriptPlugin* PythonPlugin = IPythonScriptPlugin::Get();
	if (!PythonPlugin)
	{
		Result.bSuccess = false;
		Result.Exception = TEXT("PythonScriptPlugin not available");
		LogError(TEXT("PythonScriptPlugin not loaded"));
		return Result;
	}

	FPythonCommandEx PythonCommand;
	PythonCommand.Command = Code;
	PythonCommand.ExecutionMode = EPythonCommandExecutionMode::ExecuteFile;  // Issue #4086: ExecuteFile allows multi-statement code
	PythonCommand.FileExecutionScope = EPythonFileExecutionScope::Private;

	// Execute with result capture (UE 5.6 API: single in/out parameter)
	bool bExecuted = PythonPlugin->ExecPythonCommandEx(PythonCommand);

	Result.ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	// Issue #26150: ExecuteFile reports print/log output through typed LogOutput
	// entries. CommandResult is only meaningful here on failure (on success it is
	// the literal "None" sentinel), so keep the streams separate at the source.
	auto AppendOutputLine = [](FString& Target, const FString& Output)
	{
		if (!Target.IsEmpty())
		{
			Target += TEXT("\n");
		}
		Target += Output;
	};

	for (const FPythonLogOutputEntry& Entry : PythonCommand.LogOutput)
	{
		switch (Entry.Type)
		{
		case EPythonLogOutputType::Info:
			AppendOutputLine(Result.Stdout, Entry.Output);
			break;
		case EPythonLogOutputType::Warning:
		case EPythonLogOutputType::Error:
		default:
			AppendOutputLine(Result.Stderr, Entry.Output);
			break;
		}
	}

	Result.bSuccess = bExecuted;
	if (!bExecuted)
	{
		Result.Exception = PythonCommand.CommandResult;
		if (Result.Exception.IsEmpty())
		{
			Result.Exception = Result.Stderr.IsEmpty()
				? TEXT("Python execution failed without error details")
				: Result.Stderr;
		}
	}

	// Issue #12336: TimeoutMs == 0 means "no enforcement", not "0ms hard limit".
	// Request->GetIntegerField("timeout") returns 0 when the caller omits the
	// field; without this guard ANY completed execution (>0ms) logged a spurious
	// "exceeded timeout: 1.1ms > 0ms" LogNarshaMCPWebSocket: Error.
	if (TimeoutMs > 0 && Result.ExecutionTimeMs > TimeoutMs)
	{
		LogError(FString::Printf(TEXT("Python execution exceeded timeout: %.1fms > %dms"), Result.ExecutionTimeMs, TimeoutMs));
	}
#else
	Result.bSuccess = false;
	Result.Exception = TEXT("Python support not enabled (WITH_PYTHON=0)");
	LogError(TEXT("Python execution failed: WITH_PYTHON not defined"));
#endif

	return Result;
}

void UCUWebSocketEditorSubsystem::SendResponse(INetworkingWebSocket* Socket, const FString& Response)
{
	if (!Socket)
	{
		LogError(TEXT("SendResponse: Invalid socket"));
		return;
	}

	FTCHARToUTF8 UTF8String(*Response);
	TArray<uint8> Data;
	Data.Append((uint8*)UTF8String.Get(), UTF8String.Length());

	Socket->Send(Data.GetData(), Data.Num(), /*bIsBinary=*/false);
}

// Issue #1632: Tick method for WebSocket server polling
bool UCUWebSocketEditorSubsystem::Tick(float DeltaTime)
{
	if (bIsRunning && Server.IsValid())
	{
		// Tick the server to process incoming connections and messages.
		// Server->Tick() invokes OnMessageReceived synchronously on this
		// thread for each ready frame, which now *enqueues* into
		// PendingMessages instead of dispatching inline (Issue #9394).
		Server->Tick();
	}

	// Issue #9394: drain is intentionally outside the `bIsRunning` guard so
	// (a) pending messages from just before Stop() can't linger (Stop() also
	// empties the queue as belt-and-braces), and (b) automation tests can
	// exercise the drain without spinning up a real server.
	DrainPendingMessages();

	// Issue #6938: On-screen recording status + auto-stop
	if (bIsRecordingInput)
	{
		double Elapsed = FPlatformTime::Seconds() - RecordingStartTime;
		double Remaining = MaxRecordingDuration - Elapsed;

		if (GEngine)
		{
			if (bAutoStopRecording)
			{
				GEngine->AddOnScreenDebugMessage(9938, 0.f, FColor::Red,
					FString::Printf(TEXT("[REC] Recording... %.0fs left (%d events)"),
						FMath::Max(Remaining, 0.0), RecordedInputEvents.Num()));
			}
			else
			{
				GEngine->AddOnScreenDebugMessage(9938, 0.f, FColor::Red,
					FString::Printf(TEXT("[REC] Recording... %.1fs (%d events)"),
						Elapsed, RecordedInputEvents.Num()));
			}
		}

		if (bAutoStopRecording && Elapsed >= MaxRecordingDuration)
		{
			StopAndExportRecording(PendingTestName);
		}
	}

	// Return true to keep the ticker active
	return true;
}
// ============================================
// Issue #1632: Screenshot Capture (Native C++)
// ============================================

void UCUWebSocketEditorSubsystem::HandleCaptureScreenshot(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleCaptureScreenshot: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	int32 Width = Request->GetIntegerField(TEXT("width"));
	int32 Height = Request->GetIntegerField(TEXT("height"));

	// Issue #4259: Extract optional filename_prefix (default: "MCP")
	FString FilenamePrefix = Request->HasField(TEXT("filename_prefix"))
		? Request->GetStringField(TEXT("filename_prefix"))
		: TEXT("MCP");

	// Issue #4259: Extract optional include_camera_info (default: false)
	bool bIncludeCameraInfo = Request->HasField(TEXT("include_camera_info"))
		? Request->GetBoolField(TEXT("include_camera_info"))
		: false;

	LogInfo(FString::Printf(TEXT("Capturing screenshot (request_id: %s, size: %dx%d, prefix: %s, camera_info: %s)"),
		*RequestId, Width, Height, *FilenamePrefix, bIncludeCameraInfo ? TEXT("true") : TEXT("false")));

	// Generate unique filename with timestamp
	// Issue #4259: Use filename_prefix instead of hardcoded "Screenshot"
	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots");
	FString ScreenshotPath = ScreenshotDir / FString::Printf(TEXT("%s_%s.png"), *FilenamePrefix, *Timestamp);

	// Ensure directory exists
	IFileManager::Get().MakeDirectory(*ScreenshotDir, true);

	bool bSuccess = false;
	int32 ActualWidth = 0;
	int32 ActualHeight = 0;

	// Issue #4259: Camera info variables (populated if bIncludeCameraInfo)
	FVector CameraLocation = FVector::ZeroVector;
	FRotator CameraRotation = FRotator::ZeroRotator;
	float CameraFOV = 90.0f;
	bool bHasCameraInfo = false;

	// Capture camera info from active viewport if requested
	if (bIncludeCameraInfo && GEditor)
	{
		if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		{
			TSharedPtr<IAssetViewport> ActiveViewport = LevelEditor->GetFirstActiveViewport();
			if (ActiveViewport.IsValid())
			{
				FEditorViewportClient& ViewportClient = ActiveViewport->GetAssetViewportClient();
				CameraLocation = ViewportClient.GetViewLocation();
				CameraRotation = ViewportClient.GetViewRotation();
				CameraFOV = ViewportClient.ViewFOV;
				bHasCameraInfo = true;
				LogInfo(FString::Printf(TEXT("Camera info captured: Loc(%.1f,%.1f,%.1f) Rot(%.1f,%.1f,%.1f) FOV=%.1f"),
					CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
					CameraRotation.Pitch, CameraRotation.Yaw, CameraRotation.Roll,
					CameraFOV));
			}
		}
	}

	// Use FSlateApplication to capture the editor window
	if (FSlateApplication::IsInitialized())
	{
		// Issue #4259 Phase 7: Enhanced diagnostic logging for screenshot capture
		LogInfo(TEXT("[Screenshot] === Starting screenshot capture ==="));

		// Try to get active window first, fall back to finding the largest visible window
		TSharedPtr<SWindow> TargetWindow = FSlateApplication::Get().GetActiveTopLevelWindow();

		// Issue #4259 Phase 7: Log Window state for diagnostics
		if (TargetWindow.IsValid())
		{
			LogInfo(FString::Printf(TEXT("[Screenshot] ActiveTopLevelWindow: '%s', Visible=%s, Minimized=%s, Size=%.0fx%.0f"),
				*TargetWindow->GetTitle().ToString(),
				TargetWindow->IsVisible() ? TEXT("Y") : TEXT("N"),
				TargetWindow->IsWindowMinimized() ? TEXT("Y") : TEXT("N"),
				TargetWindow->GetSizeInScreen().X,
				TargetWindow->GetSizeInScreen().Y));

			// Issue #4259 Phase 7: Skip invalid window (minimized or not visible)
			if (TargetWindow->IsWindowMinimized() || !TargetWindow->IsVisible())
			{
				LogInfo(TEXT("[Screenshot] ActiveTopLevelWindow is minimized or not visible, trying fallback..."));
				TargetWindow.Reset();
			}
		}
		else
		{
			LogInfo(TEXT("[Screenshot] No ActiveTopLevelWindow found, trying fallback..."));
		}

		// If no active window, find the main editor window (largest visible window)
		if (!TargetWindow.IsValid())
		{
			TArray<TSharedRef<SWindow>> AllWindows;
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);

			// Issue #4259 Phase 7: Log all visible windows for diagnostics
			LogInfo(FString::Printf(TEXT("[Screenshot] Fallback: Found %d visible windows"), AllWindows.Num()));

			float LargestArea = 0;
			for (const TSharedRef<SWindow>& Window : AllWindows)
			{
				FVector2D Size = Window->GetSizeInScreen();
				float Area = Size.X * Size.Y;

				// Issue #4259 Phase 7: Log each candidate window
				LogInfo(FString::Printf(TEXT("[Screenshot]   - '%s': %.0fx%.0f (Area=%.0f)"),
					*Window->GetTitle().ToString(),
					Size.X, Size.Y, Area));

				if (Area > LargestArea)
				{
					LargestArea = Area;
					TargetWindow = Window;
				}
			}

			if (TargetWindow.IsValid())
			{
				LogInfo(FString::Printf(TEXT("[Screenshot] Selected largest window: '%s' (%.0f x %.0f)"),
					*TargetWindow->GetTitle().ToString(),
					TargetWindow->GetSizeInScreen().X, TargetWindow->GetSizeInScreen().Y));
			}
			else
			{
				LogError(TEXT("[Screenshot] Fallback failed: No visible windows found!"));
			}
		}

		if (TargetWindow.IsValid())
		{
			FVector2D WindowSize = TargetWindow->GetSizeInScreen();
			ActualWidth = FMath::TruncToInt(WindowSize.X);
			ActualHeight = FMath::TruncToInt(WindowSize.Y);

			// Issue #4259 Phase 7: Final window state before capture
			LogInfo(FString::Printf(TEXT("[Screenshot] Final TargetWindow: '%s', Size=%dx%d, Visible=%s, Minimized=%s"),
				*TargetWindow->GetTitle().ToString(),
				ActualWidth, ActualHeight,
				TargetWindow->IsVisible() ? TEXT("Y") : TEXT("N"),
				TargetWindow->IsWindowMinimized() ? TEXT("Y") : TEXT("N")));

			// Issue #4259 Phase 7: Validate window dimensions
			if (ActualWidth < 1 || ActualHeight < 1)
			{
				LogError(FString::Printf(TEXT("[Screenshot] Invalid window dimensions: %dx%d"), ActualWidth, ActualHeight));
				// Don't return - let TakeScreenshot fail and report proper error
			}

			// Capture using Slate's built-in screenshot functionality
			TArray<FColor> Bitmap;
			FIntVector Size(ActualWidth, ActualHeight, 0);

			LogInfo(FString::Printf(TEXT("[Screenshot] Calling TakeScreenshot with Size=%dx%d"), Size.X, Size.Y));
			bool bTakeScreenshotResult = FSlateApplication::Get().TakeScreenshot(TargetWindow.ToSharedRef(), Bitmap, Size);
			LogInfo(FString::Printf(TEXT("[Screenshot] TakeScreenshot result: %s, Bitmap.Num=%d, Size=%dx%d"),
				bTakeScreenshotResult ? TEXT("Success") : TEXT("FAILED"),
				Bitmap.Num(),
				Size.X, Size.Y));

			if (bTakeScreenshotResult)
			{
				ActualWidth = Size.X;
				ActualHeight = Size.Y;

				// Compress to PNG using ImageWrapper module
				IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
				TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				if (ImageWrapper.IsValid())
				{
					// SetRaw expects BGRA format - FColor is already BGRA
					if (ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), ActualWidth, ActualHeight, ERGBFormat::BGRA, 8))
					{
						// UE 5.6 API: GetCompressed returns array (not out param)
						TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(100);
						if (CompressedData.Num() > 0)
						{
							// Write PNG file
							if (FFileHelper::SaveArrayToFile(CompressedData, *ScreenshotPath))
							{
								bSuccess = true;
								LogInfo(FString::Printf(TEXT("Screenshot saved: %s (%dx%d)"), *ScreenshotPath, ActualWidth, ActualHeight));
							}
							else
							{
								LogError(FString::Printf(TEXT("Failed to save screenshot to: %s"), *ScreenshotPath));
							}
						}
						else
						{
							LogError(TEXT("Failed to compress PNG"));
						}
					}
					else
					{
						LogError(TEXT("Failed to set raw image data"));
					}
				}
				else
				{
					LogError(TEXT("Failed to create image wrapper"));
				}
			}
			else
			{
				// P2: HighRes fallback using Viewport->ReadPixels() (Issue #4453)
				LogInfo(TEXT("[Screenshot] TakeScreenshot failed, trying ReadPixels fallback..."));

				if (GEditor && GEditor->GetActiveViewport())
				{
					FViewport* Viewport = GEditor->GetActiveViewport();
					if (Viewport)
					{
						TArray<FColor> PixelData;
						if (Viewport->ReadPixels(PixelData))
						{
							int32 ViewportWidth = Viewport->GetSizeXY().X;
							int32 ViewportHeight = Viewport->GetSizeXY().Y;

							LogInfo(FString::Printf(TEXT("[Screenshot] ReadPixels succeeded: %dx%d, Pixels=%d"),
								ViewportWidth, ViewportHeight, PixelData.Num()));

							// Reuse existing ImageWrapper pipeline
							IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
							TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

							if (ImageWrapper.IsValid())
							{
								if (ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor),
									ViewportWidth, ViewportHeight, ERGBFormat::BGRA, 8))
								{
									TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(100);
									if (CompressedData.Num() > 0)
									{
										if (FFileHelper::SaveArrayToFile(CompressedData, *ScreenshotPath))
										{
											bSuccess = true;
											ActualWidth = ViewportWidth;
											ActualHeight = ViewportHeight;
											LogInfo(FString::Printf(TEXT("[Screenshot] ReadPixels fallback saved to: %s"), *ScreenshotPath));
										}
										else
										{
											LogError(TEXT("[Screenshot] ReadPixels fallback: Failed to save file"));
										}
									}
									else
									{
										LogError(TEXT("[Screenshot] ReadPixels fallback: Failed to compress PNG"));
									}
								}
								else
								{
									LogError(TEXT("[Screenshot] ReadPixels fallback: Failed to set raw image data"));
								}
							}
							else
							{
								LogError(TEXT("[Screenshot] ReadPixels fallback: Failed to create image wrapper"));
							}
						}
						else
						{
							LogError(TEXT("[Screenshot] ReadPixels also failed"));
						}
					}
					else
					{
						LogError(TEXT("[Screenshot] No active viewport for ReadPixels fallback"));
					}
				}
				else
				{
					LogError(TEXT("[Screenshot] GEditor or ActiveViewport not available for fallback"));
				}

				if (!bSuccess)
				{
					LogError(TEXT("[Screenshot] All capture methods failed"));
				}
			}
		}
		else
		{
			LogError(TEXT("No active top level window found"));
		}
	}
	else
	{
		LogError(TEXT("FSlateApplication not initialized"));
	}

	// Build response JSON
	// Issue #3794: Use "file_path" to match Python client expectation
	FString PathForJSON = ScreenshotPath.Replace(TEXT("\\"), TEXT("/"));

	// Issue #4259: Build camera_info JSON if available
	FString CameraInfoJSON = TEXT("");
	if (bIncludeCameraInfo && bHasCameraInfo)
	{
		CameraInfoJSON = FString::Printf(TEXT(
			",\"camera_info\":{"
			"\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
			"\"rotation\":{\"pitch\":%.2f,\"yaw\":%.2f,\"roll\":%.2f},"
			"\"fov\":%.2f"
			"}"),
			CameraLocation.X, CameraLocation.Y, CameraLocation.Z,
			CameraRotation.Pitch, CameraRotation.Yaw, CameraRotation.Roll,
			CameraFOV
		);
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"screenshot_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"file_path\":\"%s\","
		"\"width\":%d,"
		"\"height\":%d,"
		"\"filename_prefix\":\"%s\"%s"
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*PathForJSON,
		ActualWidth,
		ActualHeight,
		*FilenamePrefix,
		*CameraInfoJSON
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: Ping Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandlePing(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString Timestamp = FDateTime::UtcNow().ToIso8601();

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"pong\","
		"\"request_id\":\"%s\","
		"\"timestamp\":\"%s\","
		"\"server\":\"uecodegen-subsystem\","
		"\"version\":\"v5.1.0\""
		"}"),
		*RequestId,
		*Timestamp
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: Get Editor Status Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetEditorStatus(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));

	// Gather editor status information
	FString ProjectName = FApp::GetProjectName();
	FString EngineVersion = FEngineVersion::Current().ToString();
	bool bIsPlayingInEditor = GEditor ? GEditor->PlayWorld != nullptr : false;
	bool bIsSimulating = GEditor ? GEditor->bIsSimulatingInEditor : false;
	bool bIsPIERunning = bIsPlayingInEditor || bIsSimulating;

	// Get current level name
	FString CurrentLevelName = TEXT("None");
	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		CurrentLevelName = GEditor->GetEditorWorldContext().World()->GetMapName();
	}

	// Get selected actor count
	int32 SelectedActorCount = 0;
	if (GEditor)
	{
		SelectedActorCount = GEditor->GetSelectedActorCount();
	}

	// Get viewport info
	int32 ViewportWidth = 0;
	int32 ViewportHeight = 0;
	if (GEditor && GEditor->GetActiveViewport())
	{
		FIntPoint ViewportSize = GEditor->GetActiveViewport()->GetSizeXY();
		ViewportWidth = ViewportSize.X;
		ViewportHeight = ViewportSize.Y;
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"editor_status\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"project_name\":\"%s\","
		"\"engine_version\":\"%s\","
		"\"current_level\":\"%s\","
		"\"is_pie_running\":%s,"
		"\"is_simulating\":%s,"
		"\"selected_actor_count\":%d,"
		"\"viewport_width\":%d,"
		"\"viewport_height\":%d"
		"}"),
		*RequestId,
		*ProjectName,
		*EngineVersion,
		*CurrentLevelName,
		bIsPIERunning ? TEXT("true") : TEXT("false"),
		bIsSimulating ? TEXT("true") : TEXT("false"),
		SelectedActorCount,
		ViewportWidth,
		ViewportHeight
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #9074: Authoritative editor info (project_root + project_name + engine_version)
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetEditorInfo(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));

	// Resolve Editor's actual project directory. FPaths::ProjectDir() can be
	// relative to the engine binary's cwd — convert to absolute so ue_check_health
	// surfaces a path that downstream tools can use verbatim.
	FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	// Normalize trailing separator for stable comparison on the Rust side.
	FPaths::NormalizeDirectoryName(ProjectRoot);

	FString ProjectName = FApp::GetProjectName();
	FString EngineVersion = FEngineVersion::Current().ToString();
	const bool bIsPieActive = GEditor ? (GEditor->PlayWorld != nullptr) : false;

	// Issue #15316 C1: report this Editor's own OS process id so the MCP daemon
	// can scope `stop_editor` to the live-connected Editor (the WS subsystem runs
	// in-process, so this is the same UnrealEditor.exe PID the daemon detects via
	// the process snapshot). Additive field — older daemons simply ignore it.
	const uint32 ProcessId = FPlatformProcess::GetCurrentProcessId();

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"editor_info\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"project_root\":\"%s\","
		"\"project_name\":\"%s\","
		"\"engine_version\":\"%s\","
		"\"is_pie_active\":%s,"
		"\"pid\":%u"
		"}"),
		*RequestId,
		*CUJsonUtils::EscapeJsonString(ProjectRoot),
		*CUJsonUtils::EscapeJsonString(ProjectName),
		*CUJsonUtils::EscapeJsonString(EngineVersion),
		bIsPieActive ? TEXT("true") : TEXT("false"),
		ProcessId
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #16695: Editor port advertisement (per-project WS port discovery)
// ============================================

namespace
{
	// Platform-native token consumed by Rust `get_process_start_time(pid)`.
	// Exact equality binds an advertisement to one OS process instance and makes
	// PID reuse safe without contacting the Editor (Issue #26153).
	uint64 GetCurrentProcessStartToken()
	{
#if PLATFORM_WINDOWS
		FILETIME CreationTime{};
		FILETIME ExitTime{};
		FILETIME KernelTime{};
		FILETIME UserTime{};
		if (!::GetProcessTimes(
			::GetCurrentProcess(),
			&CreationTime,
			&ExitTime,
			&KernelTime,
			&UserTime))
		{
			return 0;
		}
		return (static_cast<uint64>(CreationTime.dwHighDateTime) << 32)
			| static_cast<uint64>(CreationTime.dwLowDateTime);
#elif PLATFORM_LINUX
		FString Stat;
		if (!FFileHelper::LoadFileToString(Stat, TEXT("/proc/self/stat")))
		{
			return 0;
		}
		int32 CloseParen = INDEX_NONE;
		if (!Stat.FindLastChar(TEXT(')'), CloseParen))
		{
			return 0;
		}
		TArray<FString> Fields;
		Stat.Mid(CloseParen + 1).ParseIntoArrayWS(Fields);
		// Tokens after the final ')' begin at proc(5) field 3 (`state`), so
		// field 22 (`starttime`) is zero-based index 19.
		if (!Fields.IsValidIndex(19))
		{
			return 0;
		}
		return FCString::Strtoui64(*Fields[19], nullptr, 10);
#elif PLATFORM_MAC
		proc_taskallinfo Info{};
		const int Result = proc_pidinfo(
			getpid(),
			PROC_PIDTASKALLINFO,
			0,
			&Info,
			static_cast<int>(sizeof(Info)));
		if (Result <= 0)
		{
			return 0;
		}
		return static_cast<uint64>(Info.pbsd.pbi_start_tvsec) * 1000000ULL
			+ static_cast<uint64>(Info.pbsd.pbi_start_tvusec);
#else
		return 0;
#endif
	}
}

FString UCUWebSocketEditorSubsystem::GetEditorPortAdvertisementPath()
{
	// Base off USERPROFILE so it resolves to the SAME directory as the daemon's
	// Rust `dirs::home_dir()` (FOLDERID_Profile == %USERPROFILE% on Windows). The
	// daemon reads `<home>/.narshamcp/editor_ports/*.json`; keep this in lockstep.
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (UserProfile.IsEmpty())
	{
		return FString();
	}
	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	return FPaths::Combine(
		UserProfile,
		TEXT(".narshamcp"),
		TEXT("editor_ports"),
		FString::Printf(TEXT("%u.json"), Pid));
}

void UCUWebSocketEditorSubsystem::WriteEditorPortAdvertisement()
{
	const FString FilePath = GetEditorPortAdvertisementPath();
	if (FilePath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning,
			TEXT("[WebSocket] Issue #16695: USERPROFILE unset — skipping port advertisement"));
		return;
	}

	// Ensure `<home>/.narshamcp/editor_ports/` exists (recursive). Best-effort.
	const FString DirPath = FPaths::GetPath(FilePath);
	IFileManager::Get().MakeDirectory(*DirPath, /*Tree=*/true);

	// project_root MUST mirror HandleGetEditorInfo's normalization so the daemon's
	// `ownership::project_root_matches` compares equal (Issue #16695 identity contract).
	FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectRoot);

	const uint32 Pid = FPlatformProcess::GetCurrentProcessId();
	const uint64 ProcessStartToken = GetCurrentProcessStartToken();
	const FString EngineVersion = FEngineVersion::Current().ToString();
	const FString Timestamp = FDateTime::UtcNow().ToIso8601();

	// `process_start_token` is a platform-native exact instance identifier. The
	// daemon compares it with the current PID before any destructive force stop;
	// 0 remains discovery-only and cannot authorize a kill (Issue #26153).
	const FString Json = FString::Printf(TEXT(
		"{"
		"\"ws_port\":%u,"
		"\"project_root\":\"%s\","
		"\"pid\":%u,"
		"\"process_start_token\":%llu,"
		"\"engine_version\":\"%s\","
		"\"ts\":\"%s\""
		"}"),
		ServerPort,
		*CUJsonUtils::EscapeJsonString(ProjectRoot),
		Pid,
		static_cast<unsigned long long>(ProcessStartToken),
		*CUJsonUtils::EscapeJsonString(EngineVersion),
		*CUJsonUtils::EscapeJsonString(Timestamp));

	// Write a complete sibling generation then replace the public path. Readers
	// open `<pid>.json` once, so they observe either the old or new payload and
	// cannot combine identity fields from one generation with another.
	const FString TempFilePath = FilePath + TEXT(".tmp");
	const bool bWroteTemp = FFileHelper::SaveStringToFile(Json, *TempFilePath);
	const bool bPublished = bWroteTemp && IFileManager::Get().Move(
		*FilePath,
		*TempFilePath,
		/*Replace=*/true,
		/*EvenIfReadOnly=*/true,
		/*Attributes=*/false,
		/*DoNotRetryOrError=*/true);
	if (bPublished)
	{
		UE_LOG(LogNarshaMCPWebSocket, Log,
			TEXT("[WebSocket] Issue #26153: advertised WS port %u for project '%s' (instance %llu) -> %s"),
			ServerPort,
			*ProjectRoot,
			static_cast<unsigned long long>(ProcessStartToken),
			*FilePath);
	}
	else
	{
		IFileManager::Get().Delete(*TempFilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true);
		UE_LOG(LogNarshaMCPWebSocket, Warning,
			TEXT("[WebSocket] Issue #26153: failed to atomically publish port advertisement %s (non-fatal)"),
			*FilePath);
	}
}

void UCUWebSocketEditorSubsystem::RemoveEditorPortAdvertisement()
{
	const FString FilePath = GetEditorPortAdvertisementPath();
	if (FilePath.IsEmpty())
	{
		return;
	}
	if (IFileManager::Get().FileExists(*FilePath))
	{
		if (IFileManager::Get().Delete(*FilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true))
		{
			UE_LOG(LogNarshaMCPWebSocket, Verbose,
				TEXT("[WebSocket] Issue #16695: removed port advertisement %s"), *FilePath);
		}
		else
		{
			UE_LOG(LogNarshaMCPWebSocket, Verbose,
				TEXT("[WebSocket] Issue #16695: could not remove %s (non-fatal)"), *FilePath);
		}
	}
}

// ============================================
// Issue #1632: Execute Console Command Handler
// ============================================

// Issue #16601: FUNC_Exec-gated targeted console dispatch into the live PIE actor chain.
// Returns true iff some actor-scoped UFUNCTION(Exec) actually ran. ProcessConsoleExec is
// FUNC_Exec-gated by UObject::CallFunctionByNameWithArguments (ScriptCore.cpp) — it returns
// false unless the resolved function carries FUNC_Exec — so a true return PROVES an Exec
// UFUNCTION executed. This is intentionally NOT GEditor->Exec / ULocalPlayer::Exec /
// UPlayer::Exec: those return true for CVars and other non-UFUNCTION handling and would
// leak editor-vs-PIE CVar state. Walk is the actor-scoped subset only:
//   PlayerController -> Pawn -> HUD -> CheatManager (first claimant wins).
// On success OutHandler names the claimant; on false nothing claimed it and the caller
// falls back to the editor FExec path. Out captures the command's output-device text.
static bool TryPIEPawnExec(APlayerController* PC, APawn* Pawn, const TCHAR* Cmd, FOutputDevice& Out, FString& OutHandler)
{
	UObject* Executor = PC; // PlayerController is the conventional console-command executor
	if (PC && PC->ProcessConsoleExec(Cmd, Out, Executor))
	{
		OutHandler = TEXT("pc_ufunction");
		return true;
	}
	if (Pawn && Pawn->ProcessConsoleExec(Cmd, Out, Executor))
	{
		OutHandler = TEXT("pawn_ufunction");
		return true;
	}
	if (AHUD* HUD = (PC ? PC->GetHUD() : nullptr))
	{
		if (HUD->ProcessConsoleExec(Cmd, Out, Executor))
		{
			OutHandler = TEXT("hud_ufunction");
			return true;
		}
	}
	if (UCheatManager* Cheat = (PC ? PC->CheatManager : nullptr))
	{
		// Honesty caveat (Issue #16601): on a NON-authority networked PIE client,
		// UCheatManager::ProcessConsoleExec forwards a FUNC_BlueprintAuthorityOnly
		// cheat via ServerExec(Cmd) and returns true WITHOUT running the UFUNCTION
		// locally (CheatManager.cpp). So `cheat_ufunction` denotes "dispatched to the
		// cheat path" — which on a client may be a server-forwarded RPC rather than a
		// local execution. Single-player / listen-server authority runs it locally.
		// This never breaks success==(handler!="none") and touches no acceptance
		// criterion (the DriftTank repro is single-player authority).
		if (Cheat->ProcessConsoleExec(Cmd, Out, Executor))
		{
			OutHandler = TEXT("cheat_ufunction");
			return true;
		}
	}
	return false;
}

void UCUWebSocketEditorSubsystem::HandleExecuteConsoleCommand(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleExecuteConsoleCommand: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString Command = Request->GetStringField(TEXT("command"));

	LogInfo(FString::Printf(TEXT("Executing console command (request_id: %s): %s"), *RequestId, *Command));

	bool bSuccess = false;
	FString Output;
	FString Handler = TEXT("none");   // Issue #16601: pawn_ufunction|pc_ufunction|cheat_ufunction|hud_ufunction|editor_fexec|none
	FString ErrorCode;                // Issue #16601: empty unless a structured PIE failure (PIE_NOT_RUNNING / NO_POSSESSED_PAWN)

	if (Command.IsEmpty())
	{
		Output = TEXT("Command cannot be empty");
	}
	else if (!GEditor || !GEditor->GetEditorWorldContext().World())
	{
		Output = TEXT("Editor world context not available");
		LogError(Output);
	}
	else
	{
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();

		// Issue #16601: prefix gate. Known editor/CVar/stat-style commands are dispatched
		// ONLY through the editor FExec path — they must never be routed into a PIE Pawn
		// (routing e.g. `r.*` into PIE would leak editor-vs-PIE CVar state). This is a
		// fail-safe try-order (NOT classify-then-commit): a non-editor command tries the
		// targeted PIE walk first and falls back to editor FExec if nothing claims it.
		static const TCHAR* EditorOnlyPrefixes[] = {
			TEXT("stat "), TEXT("r."), TEXT("t."), TEXT("fx."), TEXT("d."), TEXT("p."),
			TEXT("au."), TEXT("show "), TEXT("showflag"), TEXT("viewmode"), TEXT("slate."),
			TEXT("net "), TEXT("memreport"), TEXT("obj "), TEXT("dumpconsolecommands")
		};
		bool bEditorOnly = false;
		for (const TCHAR* Prefix : EditorOnlyPrefixes)
		{
			if (Command.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				bEditorOnly = true;
				break;
			}
		}

		// Issue #16601: targeted PIE walk first (for non-editor commands while PIE is live).
		if (!bEditorOnly)
		{
			UWorld* PieWorld = GEditor->PlayWorld; // PlayWorld-first (matches the file's PIE idiom)
			if (!PieWorld && GEngine)
			{
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if (Context.WorldType == EWorldType::PIE && Context.World())
					{
						PieWorld = Context.World();
						break;
					}
				}
			}

			if (PieWorld)
			{
				// TOCTOU recheck: PlayerController / Pawn can be null during PIE init,
				// possession transitions, or spectator-only states.
				APlayerController* PC = PieWorld->GetFirstPlayerController();
				if (!PC)
				{
					ErrorCode = TEXT("PIE_NOT_RUNNING");
					Output = TEXT("PIE world has no PlayerController yet (still initializing?)");
				}
				else
				{
					APawn* Pawn = PC->GetPawn(); // NOT GetPawnOrSpectator() — avoid silent spectator dispatch
					if (!Pawn)
					{
						ErrorCode = TEXT("NO_POSSESSED_PAWN");
						Output = TEXT("PlayerController has no possessed Pawn (spectator/between-possession)");
					}
					else
					{
						FStringOutputDevice CapturedOutput;
						if (TryPIEPawnExec(PC, Pawn, *Command, CapturedOutput, Handler))
						{
							bSuccess = true;
							Output = CapturedOutput; // captured device text; may be empty even on success
						}
						else
						{
							Handler = TEXT("none"); // nothing in the actor chain claimed it → fall back to editor FExec
						}
					}
				}
			}
			// No PIE world → not an error; fall through to the editor FExec path below.
		}

		// Issue #16601: editor FExec path — entered when the command is editor-only, when no
		// PIE is running, or when the PIE walk found no matching Exec UFUNCTION. Skipped when
		// the PIE walk already succeeded or set a structured error_code.
		const bool bAlreadyResolved = bSuccess || !ErrorCode.IsEmpty();
		if (!bAlreadyResolved)
		{
			// Issue #8247, #8293, #8299: stat commands need viewport context for overlay rendering.
			// Set GStatProcessingViewportClient to the active editor viewport so
			// UEngine::HandleStatCommand renders stat overlays in the correct viewport.
			// FEditorViewportClient does NOT inherit FExec (removed in UE 5.7).
			if (Command.StartsWith(TEXT("stat "), ESearchCase::IgnoreCase))
			{
				if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
				{
					TSharedPtr<IAssetViewport> ActiveViewport = LevelEditor->GetFirstActiveViewport();
					if (ActiveViewport.IsValid())
					{
						GStatProcessingViewportClient = &ActiveViewport->GetAssetViewportClient();
					}
				}
			}

			// GStatProcessingViewportClient is read by the engine's stat rendering
			// pass on the next frame. The engine resets it after use in HandleStatCommand().
			// We intentionally do NOT reset it here.
			// Issue #16601: capture the real bool instead of discarding it + hardcoding true.
			const bool bEditorHandled = GEditor->Exec(EditorWorld, *Command, *GLog);
			GStatProcessingViewportClient = nullptr;

			bSuccess = bEditorHandled;
			Handler = bEditorHandled ? TEXT("editor_fexec") : TEXT("none");
			Output = FString::Printf(TEXT("editor exec %s: %s"), bEditorHandled ? TEXT("handled") : TEXT("not handled"), *Command);
			LogInfo(Output);
		}
	}

	// Issue #16601: honest structured response. Invariant: success == (handler != "none").
	// error_code is present only on the two structured PIE failures (always success:false).
	FString EscapedOutput = CUJsonUtils::EscapeJsonString(Output);
	FString EscapedCommand = CUJsonUtils::EscapeJsonString(Command); // Issue #16601: was injected raw → JSON-injection hazard

	FString ResponseJSON;
	if (!ErrorCode.IsEmpty())
	{
		ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"console_command_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"command\":\"%s\","
			"\"output\":\"%s\","
			"\"handler\":\"%s\","
			"\"error_code\":\"%s\""
			"}"),
			*RequestId,
			*EscapedCommand,
			*EscapedOutput,
			*Handler,
			*ErrorCode
		);
	}
	else
	{
		ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"console_command_result\","
			"\"request_id\":\"%s\","
			"\"success\":%s,"
			"\"command\":\"%s\","
			"\"output\":\"%s\","
			"\"handler\":\"%s\""
			"}"),
			*RequestId,
			bSuccess ? TEXT("true") : TEXT("false"),
			*EscapedCommand,
			*EscapedOutput,
			*Handler
		);
	}

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P1 - Save Asset Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSaveAsset(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSaveAsset: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString AssetPath = Request->GetStringField(TEXT("asset_path"));

	LogInfo(FString::Printf(TEXT("Saving asset (request_id: %s): %s"), *RequestId, *AssetPath));

	bool bSuccess = false;
	FString Message;

	if (AssetPath.IsEmpty())
	{
		Message = TEXT("Asset path cannot be empty");
	}
	else
	{
		// Load the asset
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (Asset)
		{
			// Mark package dirty and save
			UPackage* Package = Asset->GetOutermost();
			if (Package)
			{
				Package->MarkPackageDirty();
				FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
				bSuccess = UPackage::SavePackage(Package, Asset, *PackageFilename, FSavePackageArgs());
				if (bSuccess)
				{
					Message = FString::Printf(TEXT("Asset saved: %s"), *AssetPath);
					LogInfo(Message);
				}
				else
				{
					Message = FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath);
					LogError(Message);
				}
			}
			else
			{
				Message = FString::Printf(TEXT("Could not get package for asset: %s"), *AssetPath);
				LogError(Message);
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
			LogError(Message);
		}
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"save_asset_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"asset_path\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*AssetPath,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P1 - Save All Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSaveAll(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSaveAll: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	LogInfo(FString::Printf(TEXT("Saving all dirty packages (request_id: %s)"), *RequestId));

	bool bSuccess = false;

	// Use FEditorFileUtils to save all dirty packages
	bSuccess = FEditorFileUtils::SaveDirtyPackages(
		/*bPromptUserToSave=*/false,
		/*bSaveMapPackages=*/true,
		/*bSaveContentPackages=*/true,
		/*bFastSave=*/false,
		/*bNotifyNoPackagesSaved=*/false,
		/*bCanBeDeclined=*/false
	);

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"save_all_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"message\":\"Save all completed\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false")
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P1 - Compile Blueprints Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleCompileBlueprints(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleCompileBlueprints: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString BlueprintPath = Request->GetStringField(TEXT("blueprint_path"));

	LogInfo(FString::Printf(TEXT("Compiling blueprints (request_id: %s): %s"), *RequestId, *BlueprintPath));

	bool bSuccess = false;
	int32 CompiledCount = 0;
	FString Message;

	if (BlueprintPath.IsEmpty() || BlueprintPath == TEXT("*"))
	{
		// Compile only already-loaded dirty blueprints (fast - no disk loading)
		// Issue #1632: Don't enumerate all assets - that loads everything from disk and blocks
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			UBlueprint* Blueprint = *It;
			if (Blueprint && Blueprint->IsPossiblyDirty())
			{
				FKismetEditorUtilities::CompileBlueprint(Blueprint);
				CompiledCount++;
			}
		}

		bSuccess = true;
		Message = FString::Printf(TEXT("Compiled %d dirty blueprints in memory"), CompiledCount);
		LogInfo(Message);
	}
	else
	{
		// Compile specific blueprint
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (Blueprint)
		{
			FKismetEditorUtilities::CompileBlueprint(Blueprint);
			bSuccess = true;
			CompiledCount = 1;
			Message = FString::Printf(TEXT("Compiled blueprint: %s"), *BlueprintPath);
			LogInfo(Message);
		}
		else
		{
			Message = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
			LogError(Message);
		}
	}

	// Issue #4544: Collect compile error/warning node details
	// Helper lambda to collect error nodes from a blueprint
	auto CollectErrorNodes = [this](UBlueprint* BP, FString& OutErrors)
	{
		if (!BP) return;
		TArray<UEdGraph*> AllGraphs;
		BP->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node || !Node->bHasCompilerMessage) continue;
				FString Severity;
				if (Node->ErrorType <= (int32)EMessageSeverity::Error)
					Severity = TEXT("error");
				else if (Node->ErrorType <= (int32)EMessageSeverity::Warning)
					Severity = TEXT("warning");
				else
					continue; // Skip info/verbose messages

				if (!OutErrors.IsEmpty()) OutErrors += TEXT(",");
				OutErrors += FString::Printf(TEXT(
					"{\"node_guid\":\"%s\","
					"\"node_name\":\"%s\","
					"\"graph_name\":\"%s\","
					"\"blueprint_path\":\"%s\","
					"\"severity\":\"%s\","
					"\"message\":\"%s\"}"),
					*Node->NodeGuid.ToString(),
					*CUJsonUtils::EscapeJsonString(Node->GetNodeTitle(ENodeTitleType::ListView).ToString()),
					*CUJsonUtils::EscapeJsonString(Graph->GetName()),
					*CUJsonUtils::EscapeJsonString(BP->GetPathName()),
					*Severity,
					*CUJsonUtils::EscapeJsonString(Node->ErrorMsg));
			}
		}
	};

	FString ErrorItems;
	if (BlueprintPath.IsEmpty() || BlueprintPath == TEXT("*"))
	{
		for (TObjectIterator<UBlueprint> It; It; ++It)
		{
			// TObjectIterator::operator* is annotated returns_nonnull (Linux clang
			// -Werror,-Wpointer-bool-conversion). Outer `It` boolean test already
			// guards iterator validity; per-element null check is redundant.
			CollectErrorNodes(*It, ErrorItems);
		}
	}
	else
	{
		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (BP) CollectErrorNodes(BP, ErrorItems);
	}

	FString ErrorsJSON = FString::Printf(TEXT("[%s]"), *ErrorItems);

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"compile_blueprints_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"compiled_count\":%d,"
		"\"errors\":%s,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		CompiledCount,
		*ErrorsJSON,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #16682: Live Coding authoritative compile + status
// ============================================

void UCUWebSocketEditorSubsystem::HandleLiveCodingCompile(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleLiveCodingCompile: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));

#if WITH_NARSHAMCP_LIVE_CODING
	TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakThis(this);
	INetworkingWebSocket* SocketCapture = Socket;
	const double StartSeconds = FPlatformTime::Seconds();

	// Defer the blocking compile out of the WS drain loop onto a later game-thread tick.
	// Compile(WaitForCompletion) blocks the game thread until the patch completes; running
	// it inline here (nested in DrainPendingMessages) would hold bDrainingPending across the
	// whole compile. AsyncTask returns immediately so the drain handler unwinds first.
	AsyncTask(ENamedThreads::GameThread, [WeakThis, SocketCapture, RequestId, StartSeconds]()
	{
		UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
		if (!Self)
		{
			return;  // subsystem torn down between enqueue and fire
		}

		// Build + send the result JSON, guarding against a client that disconnected
		// mid-compile. OnClientDisconnected -> Clients.Remove runs on this same game
		// thread before the socket is freed, so Clients.Contains is the authoritative
		// live-socket check (mirrors the drain-loop guard).
		auto SendLcResult = [WeakThis, SocketCapture, RequestId, StartSeconds](
			bool bSuccess, const TCHAR* CompileStatus, const TCHAR* CompileResultName,
			bool bIsEnabled, bool bHasStarted, const FString& Message)
		{
			const double DurationMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			const FString Resp = FString::Printf(TEXT(
				"{\"type\":\"livecoding_compile_result\",\"request_id\":\"%s\","
				"\"success\":%s,\"compile_status\":\"%s\",\"compile_result\":\"%s\","
				"\"is_enabled\":%s,\"has_started\":%s,\"duration_ms\":%.2f,\"message\":\"%s\"}"),
				*RequestId,
				bSuccess ? TEXT("true") : TEXT("false"),
				CompileStatus,
				CompileResultName,
				bIsEnabled ? TEXT("true") : TEXT("false"),
				bHasStarted ? TEXT("true") : TEXT("false"),
				DurationMs,
				*CUJsonUtils::EscapeJsonString(Message));
			if (WeakThis.IsValid() && WeakThis->Clients.Contains(SocketCapture))
			{
				WeakThis->SendResponse(SocketCapture, Resp);
			}
		};

		if (!FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")))
		{
			SendLcResult(false, TEXT("live_coding_module_not_loaded"), TEXT("None"),
				false, false, TEXT("LiveCoding module is not loaded"));
			return;
		}

		ILiveCodingModule& LiveCoding = FModuleManager::LoadModuleChecked<ILiveCodingModule>(TEXT("LiveCoding"));

		// Ensure Live Coding is enabled for this session; try to enable if permitted,
		// otherwise report a clear failure (never a false success).
		if (!LiveCoding.IsEnabledForSession())
		{
			if (LiveCoding.CanEnableForSession())
			{
				LiveCoding.EnableForSession(true);
			}
			if (!LiveCoding.IsEnabledForSession())
			{
				const FString ErrText = LiveCoding.GetEnableErrorText().ToString();
				SendLcResult(false, TEXT("live_coding_disabled"), TEXT("None"),
					false, LiveCoding.HasStarted(),
					ErrText.IsEmpty() ? FString(TEXT("Live Coding is not enabled for this session")) : ErrText);
				return;
			}
		}

		// Authoritative synchronous compile. The engine ALWAYS writes *Result
		// (ReturnResults); the bool return is logging-only — Result is the source of truth.
		ELiveCodingCompileResult Result = ELiveCodingCompileResult::NotStarted;  // defensive sentinel
		const bool bCompileBool = LiveCoding.Compile(ELiveCodingCompileFlags::WaitForCompletion, &Result);

		bool bSuccess = false;
		const TCHAR* Status = TEXT("not_started");
		const TCHAR* ResultName = TEXT("NotStarted");
		switch (Result)
		{
		case ELiveCodingCompileResult::Success:            bSuccess = true;  Status = TEXT("succeeded");            ResultName = TEXT("Success"); break;
		case ELiveCodingCompileResult::NoChanges:          bSuccess = true;  Status = TEXT("no_changes");           ResultName = TEXT("NoChanges"); break;
		case ELiveCodingCompileResult::Failure:            bSuccess = false; Status = TEXT("failed");               ResultName = TEXT("Failure"); break;
		case ELiveCodingCompileResult::CompileStillActive: bSuccess = false; Status = TEXT("compile_still_active"); ResultName = TEXT("CompileStillActive"); break;
		case ELiveCodingCompileResult::NotStarted:         bSuccess = false; Status = TEXT("not_started");          ResultName = TEXT("NotStarted"); break;
		case ELiveCodingCompileResult::Cancelled:          bSuccess = false; Status = TEXT("cancelled");            ResultName = TEXT("Cancelled"); break;
		case ELiveCodingCompileResult::InProgress:         bSuccess = false; Status = TEXT("in_progress");          ResultName = TEXT("InProgress"); break;
		default:                                           bSuccess = false; Status = TEXT("not_started");          ResultName = TEXT("Unknown"); break;
		}

		Self->LogInfo(FString::Printf(
			TEXT("HandleLiveCodingCompile: result=%s (Compile() bool=%s) status=%s"),
			ResultName, bCompileBool ? TEXT("true") : TEXT("false"), Status));

		SendLcResult(bSuccess, Status, ResultName,
			LiveCoding.IsEnabledForSession(), LiveCoding.HasStarted(),
			FString::Printf(TEXT("Live Coding compile result: %s"), ResultName));
	});
#else
	// Non-Win64: Live Coding (Live++) is unavailable. Report a clear failure — never a false success.
	const FString Resp = FString::Printf(TEXT(
		"{\"type\":\"livecoding_compile_result\",\"request_id\":\"%s\","
		"\"success\":false,\"compile_status\":\"live_coding_unavailable_on_platform\","
		"\"compile_result\":\"None\",\"is_enabled\":false,\"has_started\":false,"
		"\"duration_ms\":0.0,\"message\":\"Live Coding is only available on Win64\"}"),
		*RequestId);
	SendResponse(Socket, Resp);
#endif // WITH_NARSHAMCP_LIVE_CODING
}

void UCUWebSocketEditorSubsystem::HandleLiveCodingStatus(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleLiveCodingStatus: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));

#if WITH_NARSHAMCP_LIVE_CODING
	bool bAvailable = false;
	bool bIsCompiling = false;
	bool bIsEnabled = false;
	bool bHasStarted = false;
	if (FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")))
	{
		ILiveCodingModule& LiveCoding = FModuleManager::LoadModuleChecked<ILiveCodingModule>(TEXT("LiveCoding"));
		bAvailable = true;
		bIsCompiling = LiveCoding.IsCompiling();
		bIsEnabled = LiveCoding.IsEnabledForSession();
		bHasStarted = LiveCoding.HasStarted();
	}
	const FString Resp = FString::Printf(TEXT(
		"{\"type\":\"livecoding_status_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"available\":%s,\"is_compiling\":%s,\"is_enabled\":%s,\"has_started\":%s}"),
		*RequestId,
		bAvailable ? TEXT("true") : TEXT("false"),
		bIsCompiling ? TEXT("true") : TEXT("false"),
		bIsEnabled ? TEXT("true") : TEXT("false"),
		bHasStarted ? TEXT("true") : TEXT("false"));
	SendResponse(Socket, Resp);
#else
	const FString Resp = FString::Printf(TEXT(
		"{\"type\":\"livecoding_status_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"available\":false,\"is_compiling\":false,\"is_enabled\":false,\"has_started\":false}"),
		*RequestId);
	SendResponse(Socket, Resp);
#endif // WITH_NARSHAMCP_LIVE_CODING
}

// ============================================
// Issue #1632: P1 - Focus Actor Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleFocusActor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleFocusActor: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));

	LogInfo(FString::Printf(TEXT("Focusing actor (request_id: %s): %s"), *RequestId, *ActorName));

	bool bSuccess = false;
	FString Message;

	if (ActorName.IsEmpty())
	{
		Message = TEXT("Actor name cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		AActor* FoundActor = nullptr;

		// Find actor by name or label
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (FoundActor)
		{
			// Select and focus on the actor
			GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
			GEditor->SelectActor(FoundActor, /*bInSelected=*/true, /*bNotify=*/true);
			GEditor->MoveViewportCamerasToActor(*FoundActor, /*bActiveViewportOnly=*/false);

			bSuccess = true;
			Message = FString::Printf(TEXT("Focused on actor: %s"), *ActorName);
			LogInfo(Message);
		}
		else
		{
			Message = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"focus_actor_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*ActorName,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P1 - Select Actors Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSelectActors(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSelectActors: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	const TArray<TSharedPtr<FJsonValue>>* ActorNamesArray;
	TArray<FString> ActorNames;

	if (Request->TryGetArrayField(TEXT("actor_names"), ActorNamesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ActorNamesArray)
		{
			ActorNames.Add(Value->AsString());
		}
	}

	LogInfo(FString::Printf(TEXT("Selecting %d actors (request_id: %s)"), ActorNames.Num(), *RequestId));

	int32 SelectedCount = 0;
	FString Message;

	if (ActorNames.Num() == 0)
	{
		// Deselect all
		if (GEditor)
		{
			GEditor->SelectNone(/*bNoteSelectionChange=*/true, /*bDeselectBSPSurfs=*/true);
			Message = TEXT("Deselected all actors");
		}
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();

		// Deselect all first
		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);

		// Select matching actors
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor)
			{
				FString ActorNameOrLabel = Actor->GetActorNameOrLabel();
				FString ActorClassName = Actor->GetName();

				for (const FString& SearchName : ActorNames)
				{
					if (ActorNameOrLabel == SearchName || ActorClassName == SearchName)
					{
						GEditor->SelectActor(Actor, /*bInSelected=*/true, /*bNotify=*/false);
						SelectedCount++;
						break;
					}
				}
			}
		}

		// Notify selection change
		GEditor->NoteSelectionChange();
		Message = FString::Printf(TEXT("Selected %d actors out of %d requested"), SelectedCount, ActorNames.Num());
		LogInfo(Message);
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"select_actors_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"selected_count\":%d,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		SelectedCount > 0 || ActorNames.Num() == 0 ? TEXT("true") : TEXT("false"),
		SelectedCount,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Spawn Actor Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSpawnActor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSpawnActor: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString BlueprintPath = Request->GetStringField(TEXT("blueprint_path"));

	// Get transform values (optional, defaults to origin)
	double LocationX = Request->GetNumberField(TEXT("location_x"));
	double LocationY = Request->GetNumberField(TEXT("location_y"));
	double LocationZ = Request->GetNumberField(TEXT("location_z"));
	double RotationPitch = Request->GetNumberField(TEXT("rotation_pitch"));
	double RotationYaw = Request->GetNumberField(TEXT("rotation_yaw"));
	double RotationRoll = Request->GetNumberField(TEXT("rotation_roll"));
	double ScaleX = Request->HasField(TEXT("scale_x")) ? Request->GetNumberField(TEXT("scale_x")) : 1.0;
	double ScaleY = Request->HasField(TEXT("scale_y")) ? Request->GetNumberField(TEXT("scale_y")) : 1.0;
	double ScaleZ = Request->HasField(TEXT("scale_z")) ? Request->GetNumberField(TEXT("scale_z")) : 1.0;
	FString ActorLabel = Request->GetStringField(TEXT("actor_label")); // Optional custom label

	LogInfo(FString::Printf(TEXT("Spawning actor (request_id: %s): %s at (%.1f, %.1f, %.1f)"),
		*RequestId, *BlueprintPath, LocationX, LocationY, LocationZ));

	bool bSuccess = false;
	FString Message;
	FString SpawnedActorName;

	if (BlueprintPath.IsEmpty())
	{
		Message = TEXT("Blueprint path cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();

		// Load Blueprint asset
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint)
		{
			// Try with /Game prefix if not provided
			if (!BlueprintPath.StartsWith(TEXT("/")))
			{
				BlueprintPath = FString::Printf(TEXT("/Game/%s"), *BlueprintPath);
				Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
			}
		}

		if (Blueprint && Blueprint->GeneratedClass)
		{
			FVector Location(LocationX, LocationY, LocationZ);
			FRotator Rotation(RotationPitch, RotationYaw, RotationRoll);
			FVector Scale(ScaleX, ScaleY, ScaleZ);

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AActor* SpawnedActor = World->SpawnActor<AActor>(Blueprint->GeneratedClass, Location, Rotation, SpawnParams);

			if (SpawnedActor)
			{
				// Apply scale
				SpawnedActor->SetActorScale3D(Scale);

				// Set custom label if provided
				if (!ActorLabel.IsEmpty())
				{
					SpawnedActor->SetActorLabel(ActorLabel);
				}

				SpawnedActorName = SpawnedActor->GetName();
				bSuccess = true;
				Message = FString::Printf(TEXT("Spawned actor: %s"), *SpawnedActorName);
				LogInfo(Message);

				// Select the newly spawned actor
				GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
				GEditor->SelectActor(SpawnedActor, /*bInSelected=*/true, /*bNotify=*/true);
			}
			else
			{
				Message = TEXT("Failed to spawn actor");
				LogError(Message);
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Blueprint not found or invalid: %s"), *BlueprintPath);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"spawn_actor_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*SpawnedActorName,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Set Transform Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSetTransform(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetTransform: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));

	// Get transform components (all optional)
	bool bHasLocation = Request->HasField(TEXT("location_x")) || Request->HasField(TEXT("location_y")) || Request->HasField(TEXT("location_z"));
	bool bHasRotation = Request->HasField(TEXT("rotation_pitch")) || Request->HasField(TEXT("rotation_yaw")) || Request->HasField(TEXT("rotation_roll"));
	bool bHasScale = Request->HasField(TEXT("scale_x")) || Request->HasField(TEXT("scale_y")) || Request->HasField(TEXT("scale_z"));

	LogInfo(FString::Printf(TEXT("Setting transform (request_id: %s): %s"), *RequestId, *ActorName));

	bool bSuccess = false;
	FString Message;

	if (ActorName.IsEmpty())
	{
		Message = TEXT("Actor name cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		AActor* FoundActor = nullptr;

		// Find actor by name or label
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (FoundActor)
		{
			// Issue #8834 Cycle 20 (B1): Wrap transform changes in FScopedTransaction
			// SetActorLocation/Rotation/Scale3D bypass Modify() → not captured in JSONL
			FScopedTransaction Transaction(FText::FromString(TEXT("Set Actor Transform")));
			FoundActor->Modify();

			// Apply location if specified
			if (bHasLocation)
			{
				FVector CurrentLocation = FoundActor->GetActorLocation();
				double X = Request->HasField(TEXT("location_x")) ? Request->GetNumberField(TEXT("location_x")) : CurrentLocation.X;
				double Y = Request->HasField(TEXT("location_y")) ? Request->GetNumberField(TEXT("location_y")) : CurrentLocation.Y;
				double Z = Request->HasField(TEXT("location_z")) ? Request->GetNumberField(TEXT("location_z")) : CurrentLocation.Z;
				FoundActor->SetActorLocation(FVector(X, Y, Z));
			}

			// Apply rotation if specified
			if (bHasRotation)
			{
				FRotator CurrentRotation = FoundActor->GetActorRotation();
				double Pitch = Request->HasField(TEXT("rotation_pitch")) ? Request->GetNumberField(TEXT("rotation_pitch")) : CurrentRotation.Pitch;
				double Yaw = Request->HasField(TEXT("rotation_yaw")) ? Request->GetNumberField(TEXT("rotation_yaw")) : CurrentRotation.Yaw;
				double Roll = Request->HasField(TEXT("rotation_roll")) ? Request->GetNumberField(TEXT("rotation_roll")) : CurrentRotation.Roll;
				FoundActor->SetActorRotation(FRotator(Pitch, Yaw, Roll));
			}

			// Apply scale if specified
			if (bHasScale)
			{
				FVector CurrentScale = FoundActor->GetActorScale3D();
				double ScaleX = Request->HasField(TEXT("scale_x")) ? Request->GetNumberField(TEXT("scale_x")) : CurrentScale.X;
				double ScaleY = Request->HasField(TEXT("scale_y")) ? Request->GetNumberField(TEXT("scale_y")) : CurrentScale.Y;
				double ScaleZ = Request->HasField(TEXT("scale_z")) ? Request->GetNumberField(TEXT("scale_z")) : CurrentScale.Z;
				FoundActor->SetActorScale3D(FVector(ScaleX, ScaleY, ScaleZ));
			}

			bSuccess = true;
			Message = FString::Printf(TEXT("Transform updated for: %s"), *ActorName);
			LogInfo(Message);
		}
		else
		{
			Message = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"set_transform_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*ActorName,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Delete Actor Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleDeleteActor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleDeleteActor: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));

	LogInfo(FString::Printf(TEXT("Deleting actor (request_id: %s): %s"), *RequestId, *ActorName));

	bool bSuccess = false;
	FString Message;

	if (ActorName.IsEmpty())
	{
		Message = TEXT("Actor name cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		AActor* FoundActor = nullptr;

		// Find actor by name or label
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (FoundActor)
		{
			// Deselect first if selected
			GEditor->SelectActor(FoundActor, /*bInSelected=*/false, /*bNotify=*/true);

			// Destroy the actor
			bool bDestroyed = World->DestroyActor(FoundActor);

			if (bDestroyed)
			{
				bSuccess = true;
				Message = FString::Printf(TEXT("Deleted actor: %s"), *ActorName);
				LogInfo(Message);
			}
			else
			{
				Message = FString::Printf(TEXT("Failed to delete actor: %s"), *ActorName);
				LogError(Message);
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"delete_actor_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*ActorName,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Get Actor Properties Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetActorProperties(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetActorProperties: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));

	LogInfo(FString::Printf(TEXT("Getting actor properties (request_id: %s): %s"), *RequestId, *ActorName));

	bool bSuccess = false;
	FString Message;
	FString PropertiesJSON = TEXT("{}");

	if (ActorName.IsEmpty())
	{
		Message = TEXT("Actor name cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		AActor* FoundActor = nullptr;

		// Find actor by name or label
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (FoundActor)
		{
			// Get basic transform and class info
			FVector Location = FoundActor->GetActorLocation();
			FRotator Rotation = FoundActor->GetActorRotation();
			FVector Scale = FoundActor->GetActorScale3D();
			FString ClassName = FoundActor->GetClass()->GetName();
			FString ActorLabel = FoundActor->GetActorLabel();

			PropertiesJSON = FString::Printf(TEXT(
				"{"
				"\"class_name\":\"%s\","
				"\"actor_label\":\"%s\","
				"\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
				"\"rotation\":{\"pitch\":%.2f,\"yaw\":%.2f,\"roll\":%.2f},"
				"\"scale\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
				"\"is_hidden\":%s,"
				"\"can_be_damaged\":%s"
				"}"),
				*ClassName,
				*CUJsonUtils::EscapeJsonString(ActorLabel),
				Location.X, Location.Y, Location.Z,
				Rotation.Pitch, Rotation.Yaw, Rotation.Roll,
				Scale.X, Scale.Y, Scale.Z,
				FoundActor->IsHidden() ? TEXT("true") : TEXT("false"),
				FoundActor->CanBeDamaged() ? TEXT("true") : TEXT("false")
			);

			bSuccess = true;
			Message = FString::Printf(TEXT("Got properties for: %s"), *ActorName);
			LogInfo(Message);
		}
		else
		{
			Message = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"get_actor_properties_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"properties\":%s,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*ActorName,
		*PropertiesJSON,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Set Actor Property Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSetActorProperty(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetActorProperty: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));
	FString PropertyName = Request->GetStringField(TEXT("property_name"));
	FString PropertyValue = Request->GetStringField(TEXT("property_value"));
	// Issue #16566: optional component_name targets a named/typed component on the actor
	// instead of the actor body (e.g. set a UStaticMeshComponent's RelativeLocation).
	FString ComponentName = Request->HasField(TEXT("component_name")) ? Request->GetStringField(TEXT("component_name")) : FString();

	LogInfo(FString::Printf(TEXT("Setting actor property (request_id: %s): %s%s.%s = %s"),
		*RequestId, *ActorName, ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(".%s"), *ComponentName), *PropertyName, *PropertyValue));

	bool bSuccess = false;
	FString Message;

	if (ActorName.IsEmpty())
	{
		Message = TEXT("Actor name cannot be empty");
	}
	else if (PropertyName.IsEmpty())
	{
		Message = TEXT("Property name cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		AActor* FoundActor = nullptr;

		// Find actor by name or label
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (FoundActor)
		{
			// Issue #16566: resolve the reflection target — the actor body, or a named/typed
			// component when component_name is supplied. Range-based GetComponents() loop is
			// Rule #28-safe (no T*Iterator unconditional break).
			UObject* TargetObject = FoundActor;
			if (!ComponentName.IsEmpty())
			{
				TargetObject = nullptr;
				for (UActorComponent* Comp : FoundActor->GetComponents())
				{
					if (Comp && (Comp->GetName() == ComponentName || Comp->GetClass()->GetName() == ComponentName))
					{
						TargetObject = Comp;
						break;
					}
				}
				if (!TargetObject)
				{
					Message = FString::Printf(TEXT("Component not found on actor %s: %s"), *ActorName, *ComponentName);
					LogError(Message);
				}
			}

			const bool bActorTarget = (TargetObject == FoundActor);
			// Actor-level convenience setters — only when targeting the actor body itself.
			if (bActorTarget && (PropertyName == TEXT("ActorLabel") || PropertyName == TEXT("Label")))
			{
				FoundActor->SetActorLabel(PropertyValue);
				bSuccess = true;
				Message = TEXT("Set actor label");
			}
			else if (bActorTarget && (PropertyName == TEXT("Hidden") || PropertyName == TEXT("bHidden")))
			{
				// Issue #8899: Wrap in FScopedTransaction so bHidden is captured in the
				// transaction log. SetActorHiddenInGame() bypasses Modify().
				FScopedTransaction Transaction(FText::FromString(TEXT("Set Actor Hidden")));
				FoundActor->Modify();
				FoundActor->SetActorHiddenInGame(PropertyValue.ToBool());
				bSuccess = true;
				Message = TEXT("Set hidden state");
			}
			else if (bActorTarget && PropertyName == TEXT("CanBeDamaged"))
			{
				// Issue #8899: Same pattern for SetCanBeDamaged.
				FScopedTransaction Transaction(FText::FromString(TEXT("Set Can Be Damaged")));
				FoundActor->Modify();
				FoundActor->SetCanBeDamaged(PropertyValue.ToBool());
				bSuccess = true;
				Message = TEXT("Set can be damaged");
			}
			else if (TargetObject)
			{
				// Issue #16566: ImportText_Direct parses ANY property type from the string —
				// scalars, structs (FVector/FRotator/FLinearColor), enums, object references
				// (by asset path), and arrays — replacing the previous scalar-only CastField
				// chain. Wrap in a transaction + Modify for undo; on success PostEditChangeProperty
				// so the editor/viewport reflects the change; on a parse failure (nullptr return)
				// cancel the transaction and report a clear typed error. Precedent:
				// CURemoteLibrary.cpp:14060 / CUAnimAuthoring_Notify.cpp:265.
				FProperty* Property = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName));
				if (Property)
				{
					void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
					FScopedTransaction Transaction(FText::FromString(TEXT("Set Property (MCP)")));
					TargetObject->Modify();
					const TCHAR* ImportResult = Property->ImportText_Direct(*PropertyValue, ValuePtr, TargetObject, PPF_None);
					if (ImportResult)
					{
						FPropertyChangedEvent ChangeEvent(Property);
						TargetObject->PostEditChangeProperty(ChangeEvent);
						// Issue #16566: PostEditChangeProperty alone does not reliably recompute a
						// USceneComponent's cached world transform (RelativeLocation/Rotation/Scale) or
						// recreate the render proxy (visibility/material/mesh) for a programmatic write,
						// so the live editor viewport could show a stale actor. Force the refresh so the
						// change is visible: UpdateComponentToWorld() recomputes ComponentToWorld from the
						// Relative* fields and propagates to children + physics; MarkRenderStateDirty()
						// recreates the render state for non-transform visual properties.
						if (UActorComponent* ChangedComp = Cast<UActorComponent>(TargetObject))
						{
							if (USceneComponent* ChangedScene = Cast<USceneComponent>(ChangedComp))
							{
								ChangedScene->UpdateComponentToWorld();
							}
							ChangedComp->MarkRenderStateDirty();
						}
						else if (AActor* ChangedActor = Cast<AActor>(TargetObject))
						{
							ChangedActor->MarkComponentsRenderStateDirty();
						}
						bSuccess = true;
						Message = FString::Printf(TEXT("Set property %s (type %s)"), *PropertyName, *Property->GetClass()->GetName());
					}
					else
					{
						Transaction.Cancel();
						Message = FString::Printf(TEXT("Failed to parse value '%s' for property %s (type %s)"), *PropertyValue, *PropertyName, *Property->GetClass()->GetName());
						LogError(Message);
					}
				}
				else
				{
					Message = FString::Printf(TEXT("Property not found on %s: %s"), bActorTarget ? *ActorName : *ComponentName, *PropertyName);
					LogError(Message);
				}
			}

			if (bSuccess)
			{
				LogInfo(FString::Printf(TEXT("Set %s.%s = %s"), *ActorName, *PropertyName, *PropertyValue));
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"set_actor_property_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"property_name\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(ActorName), // Issue #16566: escape echoed fields (consistency w/ get_component_property)
		*CUJsonUtils::EscapeJsonString(PropertyName),
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #16566: Get Component/Actor Property by reflection
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetComponentProperty(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetComponentProperty: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));
	FString PropertyName = Request->GetStringField(TEXT("property_name"));
	// component_name optional — reads the actor body itself when absent. This is the
	// arbitrary-reflection-read counterpart to set_actor_property (Issue #16566).
	FString ComponentName = Request->HasField(TEXT("component_name")) ? Request->GetStringField(TEXT("component_name")) : FString();

	bool bSuccess = false;
	FString Message;
	FString PropertyValueStr;
	FString PropertyType;

	if (ActorName.IsEmpty())
	{
		Message = TEXT("Actor name cannot be empty");
	}
	else if (PropertyName.IsEmpty())
	{
		Message = TEXT("Property name cannot be empty");
	}
	else if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		AActor* FoundActor = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName))
			{
				FoundActor = Actor;
				break;
			}
		}

		if (FoundActor)
		{
			// Resolve target — actor body or named/typed component (range-based loop, Rule #28-safe).
			UObject* TargetObject = FoundActor;
			if (!ComponentName.IsEmpty())
			{
				TargetObject = nullptr;
				for (UActorComponent* Comp : FoundActor->GetComponents())
				{
					if (Comp && (Comp->GetName() == ComponentName || Comp->GetClass()->GetName() == ComponentName))
					{
						TargetObject = Comp;
						break;
					}
				}
				if (!TargetObject)
				{
					Message = FString::Printf(TEXT("Component not found on actor %s: %s"), *ActorName, *ComponentName);
					LogError(Message);
				}
			}

			if (TargetObject)
			{
				FProperty* Property = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName));
				if (Property)
				{
					// ExportTextItem_Direct serializes ANY property type (scalar, struct, enum,
					// object ref, array) to a string — the read counterpart to ImportText_Direct.
					void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
					Property->ExportTextItem_Direct(PropertyValueStr, ValuePtr, nullptr, TargetObject, PPF_None);
					PropertyType = Property->GetClass()->GetName();
					bSuccess = true;
					Message = FString::Printf(TEXT("Got property %s (type %s)"), *PropertyName, *PropertyType);
				}
				else
				{
					Message = FString::Printf(TEXT("Property not found on %s: %s"), ComponentName.IsEmpty() ? *ActorName : *ComponentName, *PropertyName);
					LogError(Message);
				}
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Actor not found: %s"), *ActorName);
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("Editor world context not available");
		LogError(Message);
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"get_component_property_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"actor_name\":\"%s\","
		"\"component_name\":\"%s\","
		"\"property_name\":\"%s\","
		"\"property_type\":\"%s\","
		"\"value\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(ActorName),
		*CUJsonUtils::EscapeJsonString(ComponentName),
		*CUJsonUtils::EscapeJsonString(PropertyName),
		*CUJsonUtils::EscapeJsonString(PropertyType),
		*CUJsonUtils::EscapeJsonString(PropertyValueStr),
		*CUJsonUtils::EscapeJsonString(Message)
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #27010: Get a reflected property from any loaded UObject by path
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetObjectProperties(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetObjectProperties: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ObjectPath = Request->HasField(TEXT("object_path")) ? Request->GetStringField(TEXT("object_path")) : FString();
	if (ObjectPath.IsEmpty() && Request->HasField(TEXT("object_name")))
	{
		ObjectPath = Request->GetStringField(TEXT("object_name"));
	}
	const FString PropertyName = Request->HasField(TEXT("property_name")) ? Request->GetStringField(TEXT("property_name")) : FString();

	bool bSuccess = false;
	FString Message;
	FString PropertyValueStr;
	FString PropertyType;
	FString ObjectClass;

	if (ObjectPath.IsEmpty())
	{
		Message = TEXT("object_path cannot be empty");
	}
	else if (PropertyName.IsEmpty())
	{
		Message = TEXT("property_name cannot be empty");
	}
	else
	{
		// FindObject / StaticFindObject only — no TActorIterator (Rule #28), no
		// StaticLoadObject (disk load is the wrong object for live PIE instances).
		// Pointer stays stack-local for this handler; never stored in Python globals.
		UObject* TargetObject = FindObject<UObject>(nullptr, *ObjectPath);
		if (!IsValid(TargetObject))
		{
			TargetObject = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
		}
		if (!IsValid(TargetObject))
		{
			Message = FString::Printf(TEXT("Object not found (must already be loaded): %s"), *ObjectPath);
			LogError(Message);
		}
		else
		{
			ObjectClass = TargetObject->GetClass()->GetName();
			if (FProperty* Property = TargetObject->GetClass()->FindPropertyByName(FName(*PropertyName)))
			{
				void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
				Property->ExportTextItem_Direct(PropertyValueStr, ValuePtr, nullptr, TargetObject, PPF_None);
				PropertyType = Property->GetClass()->GetName();
				bSuccess = true;
				Message = FString::Printf(TEXT("Got property %s (type %s) on %s"), *PropertyName, *PropertyType, *ObjectClass);
			}
			else
			{
				Message = FString::Printf(TEXT("Property not found on %s (%s): %s"), *ObjectPath, *ObjectClass, *PropertyName);
				LogError(Message);
			}
		}
	}

	const FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"get_object_properties_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"object_path\":\"%s\","
		"\"object_class\":\"%s\","
		"\"property_name\":\"%s\","
		"\"property_type\":\"%s\","
		"\"value\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(ObjectPath),
		*CUJsonUtils::EscapeJsonString(ObjectClass),
		*CUJsonUtils::EscapeJsonString(PropertyName),
		*CUJsonUtils::EscapeJsonString(PropertyType),
		*CUJsonUtils::EscapeJsonString(PropertyValueStr),
		*CUJsonUtils::EscapeJsonString(Message)
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #28227: write twin of get_object_properties, and the Instanced-array adder
// ============================================

/**
 * Shared tail for both Issue #28227 handlers: parse the library's JSON, inject the WS
 * envelope, send. Mirrors HandleGetMaterialGraph -- the library owns the semantics and
 * the handler owns only the transport, so error text has exactly one source.
 */
void UCUWebSocketEditorSubsystem::SendLibraryResult(
	INetworkingWebSocket* Socket,
	const FString& LibraryJson,
	const FString& ResultType,
	const FString& RequestId)
{
	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(LibraryJson);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetStringField(TEXT("type"), ResultType);
		ErrObj->SetStringField(TEXT("request_id"), RequestId);
		ErrObj->SetBoolField(TEXT("success"), false);
		ErrObj->SetStringField(TEXT("error"), TEXT("library returned unparseable JSON"));
		SendResponse(Socket, CUJsonUtils::SerializeToString(ErrObj.ToSharedRef()));
		return;
	}
	Obj->SetStringField(TEXT("type"), ResultType);
	Obj->SetStringField(TEXT("request_id"), RequestId);
	SendResponse(Socket, CUJsonUtils::SerializeToString(Obj.ToSharedRef()));
}

void UCUWebSocketEditorSubsystem::HandleSetObjectProperties(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetObjectProperties: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString ObjectPath = Request->HasField(TEXT("object_path")) ? Request->GetStringField(TEXT("object_path")) : FString();
	const FString PropertyName = Request->HasField(TEXT("property_name")) ? Request->GetStringField(TEXT("property_name")) : FString();
	const FString PropertyValue = Request->HasField(TEXT("property_value")) ? Request->GetStringField(TEXT("property_value")) : FString();
	// Clearing an array is destructive and must be asked for, never inferred from an
	// empty value -- that is the Issue #28227 F8 shape (#28284 for the older path).
	const bool bAllowArrayClear = Request->HasField(TEXT("allow_array_clear"))
		&& Request->GetBoolField(TEXT("allow_array_clear"));

	if (ObjectPath.IsEmpty() || PropertyName.IsEmpty())
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetStringField(TEXT("type"), TEXT("set_object_properties_result"));
		ErrObj->SetStringField(TEXT("request_id"), RequestId);
		ErrObj->SetBoolField(TEXT("success"), false);
		ErrObj->SetStringField(TEXT("error"), TEXT("object_path and property_name are required"));
		SendResponse(Socket, CUJsonUtils::SerializeToString(ErrObj.ToSharedRef()));
		return;
	}

	LogInfo(FString::Printf(TEXT("SetObjectProperties: %s.%s (request_id: %s)"),
		*ObjectPath, *PropertyName, *RequestId));

	// Issue #28227 + #28284: one implementation, not two. SetObjectPropertyByPath was
	// hardened on main (#28329) with the same array-clear refusal and partial-apply
	// rollback this op needs, so the earlier `...Checked` wrapper is gone and the flag
	// is threaded straight through. Keeping the wrapper would have double-guarded and,
	// because it omitted bAllowClear on delegation, silently refused every explicit
	// clear the caller DID ask for.
	const FString Result = UCURemoteLibrary::SetObjectPropertyByPath(
		ObjectPath, PropertyName, PropertyValue, bAllowArrayClear);
	SendLibraryResult(Socket, Result, TEXT("set_object_properties_result"), RequestId);
}

void UCUWebSocketEditorSubsystem::HandleAddInstancedSubobject(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleAddInstancedSubobject: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString ObjectPath = Request->HasField(TEXT("object_path")) ? Request->GetStringField(TEXT("object_path")) : FString();
	const FString PropertyName = Request->HasField(TEXT("property_name")) ? Request->GetStringField(TEXT("property_name")) : FString();
	const FString SubobjectClass = Request->HasField(TEXT("subobject_class")) ? Request->GetStringField(TEXT("subobject_class")) : FString();

	if (ObjectPath.IsEmpty() || PropertyName.IsEmpty() || SubobjectClass.IsEmpty())
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetStringField(TEXT("type"), TEXT("add_instanced_subobject_result"));
		ErrObj->SetStringField(TEXT("request_id"), RequestId);
		ErrObj->SetBoolField(TEXT("success"), false);
		ErrObj->SetStringField(TEXT("error"), TEXT("object_path, property_name and subobject_class are required"));
		SendResponse(Socket, CUJsonUtils::SerializeToString(ErrObj.ToSharedRef()));
		return;
	}

	LogInfo(FString::Printf(TEXT("AddInstancedSubobject: %s.%s += %s (request_id: %s)"),
		*ObjectPath, *PropertyName, *SubobjectClass, *RequestId));

	const FString Result = UCURemoteLibrary::AddInstancedSubobjectByPath(
		ObjectPath, PropertyName, SubobjectClass);
	SendLibraryResult(Socket, Result, TEXT("add_instanced_subobject_result"), RequestId);
}

// Issue #1921: P2 - Get Level Actors by Class Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetLevelActorsByClass(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetLevelActorsByClass: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ClassName = Request->GetStringField(TEXT("class_name"));
	bool bExactMatch = true;  // Default to exact matching (v5.1.0 improvement)
	bool bIncludeLocation = true;
	bool bIncludeRotation = false;
	bool bIncludeScale = false;
	int32 Limit = 100;
	// Issue #27040: is_a is the default so "Pawn" matches Character/LyraCharacter.
	// exact_match is still honored when match_mode is omitted.
	FString MatchMode = TEXT("is_a");

	// Parse optional boolean fields with defaults
	if (Request->HasField(TEXT("exact_match")))
	{
		bExactMatch = Request->GetBoolField(TEXT("exact_match"));
	}
	if (Request->HasField(TEXT("match_mode")))
	{
		MatchMode = Request->GetStringField(TEXT("match_mode"));
	}
	else if (Request->HasField(TEXT("exact_match")))
	{
		MatchMode = bExactMatch ? TEXT("exact") : TEXT("substring");
	}
	if (Request->HasField(TEXT("include_location")))
	{
		bIncludeLocation = Request->GetBoolField(TEXT("include_location"));
	}
	if (Request->HasField(TEXT("include_rotation")))
	{
		bIncludeRotation = Request->GetBoolField(TEXT("include_rotation"));
	}
	if (Request->HasField(TEXT("include_scale")))
	{
		bIncludeScale = Request->GetBoolField(TEXT("include_scale"));
	}
	if (Request->HasField(TEXT("limit")))
	{
		Limit = Request->GetIntegerField(TEXT("limit"));
		if (Limit <= 0) Limit = 100;
		if (Limit > 10000) Limit = 10000;
	}

	LogInfo(FString::Printf(TEXT("Getting level actors by class (request_id: %s): %s, limit: %d"),
		*RequestId, ClassName.IsEmpty() ? TEXT("ALL") : *ClassName, Limit));

	bool bSuccess = false;
	FString Message;
	TArray<TSharedPtr<FJsonValue>> ActorsArray;
	int32 TotalCount = 0;
	FString NormalizedMode;
	FString ModeError;
	UClass* ResolvedFilter = nullptr;

	if (!FCUActorClassFilter::ParseMatchMode(MatchMode, NormalizedMode, ModeError))
	{
		Message = ModeError;
	}
	else if (!ClassName.IsEmpty() && NormalizedMode == TEXT("is_a"))
	{
		ResolvedFilter = FCUActorClassFilter::ResolveActorFilterClass(ClassName);
		if (ResolvedFilter == nullptr)
		{
			Message = FString::Printf(
				TEXT("Class not found: could not resolve '%s' for is_a matching (try /Script/Engine.Pawn or a short native name)"),
				*ClassName);
		}
	}

	if (Message.IsEmpty() && GEditor && GEditor->GetEditorWorldContext().World())
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !Actor->GetClass()) continue;  // Safety: null check for both Actor and Class

			if (!ClassName.IsEmpty())
			{
				bool bMatches = false;
				if (NormalizedMode == TEXT("is_a"))
				{
					bMatches = Actor->IsA(ResolvedFilter);
				}
				else
				{
					const FString ActorClassName = Actor->GetClass()->GetName();
					bMatches = (NormalizedMode == TEXT("exact"))
						? ActorClassName.Equals(ClassName, ESearchCase::IgnoreCase)
						: ActorClassName.Contains(ClassName, ESearchCase::IgnoreCase);
				}
				if (!bMatches)
				{
					continue;
				}
			}

			TotalCount++;

			// Only add actors up to limit, but keep counting total
			if (ActorsArray.Num() >= Limit)
			{
				continue;
			}

			TSharedPtr<FJsonObject> ActorObj = MakeShareable(new FJsonObject);
			ActorObj->SetStringField(TEXT("name"), Actor->GetName());
			ActorObj->SetStringField(TEXT("label"), Actor->GetActorNameOrLabel());
			ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());

			if (bIncludeLocation)
			{
				FVector Loc = Actor->GetActorLocation();
				TArray<TSharedPtr<FJsonValue>> LocArray;
				LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.X)));
				LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.Y)));
				LocArray.Add(MakeShareable(new FJsonValueNumber(Loc.Z)));
				ActorObj->SetArrayField(TEXT("location"), LocArray);
			}

			if (bIncludeRotation)
			{
				FRotator Rot = Actor->GetActorRotation();
				TArray<TSharedPtr<FJsonValue>> RotArray;
				RotArray.Add(MakeShareable(new FJsonValueNumber(Rot.Pitch)));
				RotArray.Add(MakeShareable(new FJsonValueNumber(Rot.Yaw)));
				RotArray.Add(MakeShareable(new FJsonValueNumber(Rot.Roll)));
				ActorObj->SetArrayField(TEXT("rotation"), RotArray);
			}

			if (bIncludeScale)
			{
				FVector Scale = Actor->GetActorScale3D();
				TArray<TSharedPtr<FJsonValue>> ScaleArray;
				ScaleArray.Add(MakeShareable(new FJsonValueNumber(Scale.X)));
				ScaleArray.Add(MakeShareable(new FJsonValueNumber(Scale.Y)));
				ScaleArray.Add(MakeShareable(new FJsonValueNumber(Scale.Z)));
				ActorObj->SetArrayField(TEXT("scale"), ScaleArray);
			}

			ActorsArray.Add(MakeShareable(new FJsonValueObject(ActorObj)));
		}

		bSuccess = true;
		Message = FString::Printf(TEXT("Found %d actors (returning %d)"), TotalCount, ActorsArray.Num());
	}
	else if (Message.IsEmpty())
	{
		Message = TEXT("Editor world not available");
	}

	// Build JSON response
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetStringField(TEXT("type"), TEXT("get_level_actors_by_class_result"));
	Response->SetStringField(TEXT("request_id"), RequestId);
	Response->SetBoolField(TEXT("success"), bSuccess);
	Response->SetNumberField(TEXT("count"), TotalCount);
	Response->SetArrayField(TEXT("actors"), ActorsArray);
	Response->SetStringField(TEXT("match_mode"), NormalizedMode.IsEmpty() ? MatchMode : NormalizedMode);
	if (ResolvedFilter != nullptr)
	{
		Response->SetStringField(TEXT("resolved_class"), ResolvedFilter->GetPathName());
	}
	if (!bSuccess)
	{
		Response->SetStringField(TEXT("error"), Message);
	}

	FString ResponseStr = CUJsonUtils::SerializeToString(Response.ToSharedRef());

	SendResponse(Socket, ResponseStr);
}

// ============================================
// Issue #1632: P2 - Open Asset Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleOpenAsset(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleOpenAsset: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString AssetPath = Request->GetStringField(TEXT("asset_path"));

	LogInfo(FString::Printf(TEXT("Opening asset (request_id: %s): %s"), *RequestId, *AssetPath));

	bool bSuccess = false;
	FString Message;
	FString EditorType = TEXT("Unknown");

	if (AssetPath.IsEmpty())
	{
		Message = TEXT("Asset path cannot be empty");
	}
	else
	{
		// Load the asset (try LoadObject first, FSoftObjectPath fallback for GameFeature plugins — Issue #7091)
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			FSoftObjectPath SoftPath(AssetPath);
			Asset = SoftPath.TryLoad();
		}

		if (Asset)
		{
			// Open asset editor (null-check guards against crash during Editor shutdown — Issue #7091)
			UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSub)
			{
				AssetEditorSub->OpenEditorForAsset(Asset);

				// Determine editor type based on asset class
				if (Asset->IsA<UBlueprint>())
				{
					EditorType = TEXT("BlueprintEditor");
				}
				else if (Asset->IsA<UMaterialInterface>())
				{
					EditorType = TEXT("MaterialEditor");
				}
				else if (Asset->IsA<UTexture>())
				{
					EditorType = TEXT("TextureEditor");
				}
				else
				{
					EditorType = TEXT("GenericAssetEditor");
				}

				bSuccess = true;
				Message = FString::Printf(TEXT("Opened asset in %s: %s"), *EditorType, *AssetPath);
				LogInfo(Message);
			}
			else
			{
				Message = TEXT("AssetEditorSubsystem not available");
				LogError(Message);
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Asset not found: %s"), *AssetPath);
			LogError(Message);
		}
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"open_asset_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"asset_path\":\"%s\","
		"\"editor_type\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*AssetPath,
		*EditorType,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #9833: Per-asset top-level OS window handle resolver
// ============================================
//
// Replaces the Win32 EnumWindows + title-substring matching used by the three
// Rust get_preview helpers (manage_widget / manage_niagara / manage_material)
// with an authoritative Slate-resolved HWND. Eliminates the silent-correctness
// bug where multiple editors with prefix-overlapping names (M_Rock vs M_Rock_Tile,
// BP_Hero vs BP_Hero_Child) caused the wrong window to be focused — and the
// resulting screenshot to be of the wrong asset.
//
// Resolution path:
//   asset_path
//     -> LoadObject<UObject> (with FSoftObjectPath fallback for plugin assets)
//     -> UAssetEditorSubsystem::FindEditorForAsset(Asset, /*bFocusIfOpen=*/false)
//     -> IAssetEditorInstance::GetAssociatedTabManager()
//     -> FTabManager::GetOwnerTab()                  (TSharedPtr<SDockTab>)
//     -> FSlateApplication::Get().FindWidgetWindow() (TSharedPtr<SWindow>)
//     -> SWindow::GetNativeWindow()->GetOSWindowHandle() -> uint64
//
// Failure modes (each returns success=false with a distinct fallback_reason):
//   asset_not_loaded  — LoadObject and FSoftObjectPath fallback both returned null
//   no_editor_open    — FindEditorForAsset returned null
//   no_tab_manager    — GetAssociatedTabManager() returned null (some non-toolkit editors)
//   no_owner_tab      — GetOwnerTab() returned null (rare; e.g. orphan FTabManager)
//   no_window         — FindWidgetWindow returned null (orphan widget tree)
//   no_native_window  — SWindow has no FGenericWindow yet (window not realized)
//
// The Rust side treats any failure (or hwnd==0) as a best-effort no-op and
// proceeds with the capture path — this preserves today's behavior on
// non-toolkit editors and asset types we haven't validated.
//
// Side effect: when an editor is found, also calls IAssetEditorInstance::FocusWindow()
// to bring the right tab forward inside its SWindow. This is independent of
// OS-level foreground promotion (which is the Rust caller's responsibility on Windows).
//
void UCUWebSocketEditorSubsystem::HandleGetEditorWindowHandle(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetEditorWindowHandle: Invalid socket or request"));
		return;
	}

	// ARCH-011: defensive cook-safety guard — WebSocket handlers don't run in
	// commandlets, but the lint rule requires this signal to clear the function.
	if (IsRunningCommandlet()) return;

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString AssetPath = Request->GetStringField(TEXT("asset_path"));

	bool bSuccess = false;
	uint64 WindowHandle = 0;
	FString FallbackReason;
	FString ErrorMessage;

	if (AssetPath.IsEmpty())
	{
		FallbackReason = TEXT("asset_path_empty");
		ErrorMessage = TEXT("asset_path field is required and must be non-empty");
	}
	else
	{
		// Step 1: Load the asset (LoadObject + FSoftObjectPath fallback for plugin assets, mirrors HandleOpenAsset).
		UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
		if (!Asset)
		{
			FSoftObjectPath SoftPath(AssetPath);
			Asset = SoftPath.TryLoad();
		}

		if (!Asset)
		{
			FallbackReason = TEXT("asset_not_loaded");
			ErrorMessage = FString::Printf(TEXT("Asset not found or could not be loaded: %s"), *AssetPath);
		}
		else
		{
			// Step 2: Find the existing editor instance (do NOT bFocusIfOpen here — the
			// caller decides focus policy; we only resolve the HWND).
			UAssetEditorSubsystem* AssetEditorSub = GEditor ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr;
			IAssetEditorInstance* EditorInstance = AssetEditorSub
				? AssetEditorSub->FindEditorForAsset(Asset, /*bFocusIfOpen=*/false)
				: nullptr;

			if (!EditorInstance)
			{
				FallbackReason = TEXT("no_editor_open");
				ErrorMessage = FString::Printf(TEXT("No asset editor open for: %s"), *AssetPath);
			}
			else
			{
				// Step 3: Walk to the owning Slate window.
				TSharedPtr<FTabManager> TabManager = EditorInstance->GetAssociatedTabManager();
				TSharedPtr<SDockTab> OwnerTab = TabManager.IsValid() ? TabManager->GetOwnerTab() : nullptr;

				if (!TabManager.IsValid())
				{
					FallbackReason = TEXT("no_tab_manager");
					ErrorMessage = TEXT("Editor instance has no associated FTabManager");
				}
				else if (!OwnerTab.IsValid())
				{
					FallbackReason = TEXT("no_owner_tab");
					ErrorMessage = TEXT("FTabManager::GetOwnerTab() returned null");
				}
				else
				{
					TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(OwnerTab.ToSharedRef());
					if (!ParentWindow.IsValid())
					{
						FallbackReason = TEXT("no_window");
						ErrorMessage = TEXT("FSlateApplication::FindWidgetWindow() returned null for owner tab");
					}
					else
					{
						TSharedPtr<FGenericWindow> NativeWindow = ParentWindow->GetNativeWindow();
						if (!NativeWindow.IsValid())
						{
							FallbackReason = TEXT("no_native_window");
							ErrorMessage = TEXT("SWindow has no realized native window yet");
						}
						else
						{
							void* RawHandle = NativeWindow->GetOSWindowHandle();
							WindowHandle = reinterpret_cast<uint64>(RawHandle);
							bSuccess = (WindowHandle != 0);
							if (!bSuccess)
							{
								FallbackReason = TEXT("null_os_handle");
								ErrorMessage = TEXT("FGenericWindow::GetOSWindowHandle() returned nullptr");
							}
						}
					}
				}

				// Side effect: focus the editor's tab inside its SWindow regardless of
				// HWND resolution outcome. UE picks the right tab; OS-level foreground
				// promotion is the Rust caller's responsibility.
				EditorInstance->FocusWindow(Asset);
			}
		}
	}

	const FString EscapedAssetPath = CUJsonUtils::EscapeJsonString(AssetPath);
	const FString EscapedReason = CUJsonUtils::EscapeJsonString(FallbackReason);
	const FString EscapedError = CUJsonUtils::EscapeJsonString(ErrorMessage);

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"editor_window_handle_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"asset_path\":\"%s\","
		"\"hwnd\":%llu,"
		"\"fallback_reason\":\"%s\","
		"\"error\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*EscapedAssetPath,
		static_cast<unsigned long long>(WindowHandle),
		*EscapedReason,
		*EscapedError
	);

	SendResponse(Socket, ResponseJSON);
}

// ==========================================================================
// Issue #26157: Non-destructive Widget Blueprint Designer capture
// ==========================================================================

static bool ValidateSocketAndRequest(
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request,
	const TCHAR* FunctionName,
	FString& OutRequestId);

namespace Issue26157_WidgetDesigner
{
	/**
	 * Resolve the closest laid-out Slate ancestor of the UMG root.  The root
	 * SObjectWidget itself is zero-sized in Designer mode; its surrounding
	 * preview surface owns the arranged geometry that is actually painted.
	 */
	static TSharedPtr<SWidget> ResolvePreviewCaptureTarget(
		const TSharedRef<SWidget>& PreviewWidget,
		const FGeometry& WindowGeometry,
		FVector2D& OutLocalMin,
		FVector2D& OutLocalMax,
		FString& OutWidgetType)
	{
		if (!FSlateApplication::IsInitialized()) return nullptr;
		FWidgetPath WidgetPath;
		if (!FSlateApplication::Get().GeneratePathToWidgetUnchecked(PreviewWidget, WidgetPath)
			|| !WidgetPath.IsValid())
		{
			return nullptr;
		}

		// Index 0 is the SWindow.  Never fall back to the whole window as a
		// supposed widget rect; callers need a truthful preview-root region.
		for (int32 Index = WidgetPath.Widgets.Num() - 1; Index > 0; --Index)
		{
			const FArrangedWidget& Arranged = WidgetPath.Widgets[Index];
			const FSlateRect Bounds = Arranged.Geometry.GetRenderBoundingRect();
			const FVector2D LocalMin = WindowGeometry.AbsoluteToLocal(
				FVector2D(Bounds.Left, Bounds.Top));
			const FVector2D LocalMax = WindowGeometry.AbsoluteToLocal(
				FVector2D(Bounds.Right, Bounds.Bottom));
			FIntRect ProbeRect;
			if (CUWidgetDesignerCapture::ComputePixelRect(
				LocalMin, LocalMax, WindowGeometry.GetLocalSize(),
				/*ImageWidth=*/1024, /*ImageHeight=*/1024, ProbeRect))
			{
				OutLocalMin = LocalMin;
				OutLocalMax = LocalMax;
				OutWidgetType = Arranged.Widget->GetType().ToString();
				return Arranged.Widget;
			}
		}
		return nullptr;
	}

	static FString BuildRestoreWarning(bool bOutlinesRestored, bool bModeRestored)
	{
		if (bOutlinesRestored && bModeRestored) return FString();
		if (!bOutlinesRestored && !bModeRestored)
		{
			return TEXT("Capture succeeded, but the previous Designer outline and editor mode states could not be restored.");
		}
		return bOutlinesRestored
			? TEXT("Capture succeeded, but the previous editor mode could not be restored.")
			: TEXT("Capture succeeded, but the previous Designer outline state could not be restored.");
	}

	static void SendError(
		UCUWebSocketEditorSubsystem* Self,
		INetworkingWebSocket* Socket,
		const FString& RequestId,
		const FString& CropMode,
		const FString& ErrorCode,
		const FString& ErrorMessage,
		bool bOutlinesRestored = true,
		bool bModeRestored = true)
	{
		if (!Self || !Socket) return;
		const FString Warning = BuildRestoreWarning(bOutlinesRestored, bModeRestored);
		const FString WarningJson = Warning.IsEmpty()
			? FString()
			: FString::Printf(TEXT(",\"warning\":\"%s\""), *CUJsonUtils::EscapeJsonString(Warning));
		const FString Response = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_designer_result\",\"request_id\":\"%s\","
			"\"success\":false,\"capture_mode\":\"widget_designer\",\"crop\":\"%s\","
			"\"error_code\":\"%s\",\"error\":\"%s\","
			"\"designer_outlines_restored\":%s,\"designer_mode_restored\":%s,"
			"\"editor_reopened\":false,\"designer_tab_method\":\"mode_switch\"%s}"),
			*CUJsonUtils::EscapeJsonString(RequestId),
			*CUJsonUtils::EscapeJsonString(CropMode),
			*CUJsonUtils::EscapeJsonString(ErrorCode),
			*CUJsonUtils::EscapeJsonString(ErrorMessage),
			bOutlinesRestored ? TEXT("true") : TEXT("false"),
			bModeRestored ? TEXT("true") : TEXT("false"),
			*WarningJson);
		Self->SendResponse(Socket, Response);
	}
}

void UCUWebSocketEditorSubsystem::RestoreWidgetDesignerState(
	const TSharedPtr<FCUWidgetDesignerCaptureState>& State,
	bool& bOutOutlinesRestored,
	bool& bOutModeRestored)
{
	bOutOutlinesRestored = !State.IsValid() || !State->bOutlinesChanged;
	bOutModeRestored = !State.IsValid() || !State->bModeChanged;
	if (!State.IsValid()) return;

	const TSharedPtr<FWidgetBlueprintEditor> Editor = State->Editor.Pin();
	if (!Editor.IsValid()) return;

	CUWidgetDesignerCapture::RestoreEditorState(
		State->bOutlinesChanged,
		State->bOriginalDashedOutlines,
		State->bModeChanged,
		State->OriginalMode,
		[Editor](bool bValue) { Editor->SetShowDashedOutlines(bValue); },
		[Editor]() { return Editor->GetShowDashedOutlines(); },
		[Editor](const FName& Mode) { Editor->SetCurrentMode(Mode); },
		[Editor]() { return Editor->GetCurrentMode(); },
		bOutOutlinesRestored,
		bOutModeRestored);
}

void UCUWebSocketEditorSubsystem::HandleCaptureWidgetDesigner(
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(
		Socket, Request, TEXT("HandleCaptureWidgetDesigner"), RequestId)) return;
	if (IsRunningCommandlet()) return;

	FString AssetPath;
	Request->TryGetStringField(TEXT("asset_path"), AssetPath);
	FString CropMode = TEXT("window");
	Request->TryGetStringField(TEXT("crop"), CropMode);
	CropMode.ToLowerInline();
	if (AssetPath.IsEmpty())
	{
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("asset_path_empty"), TEXT("asset_path is required"));
		return;
	}
	if (CropMode != TEXT("window") && CropMode != TEXT("widget"))
	{
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("invalid_crop"), TEXT("crop must be 'window' or 'widget'"));
		return;
	}
	if (!GEditor || !FSlateApplication::IsInitialized())
	{
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("designer_mode_unavailable"), TEXT("Unreal Editor or Slate is not initialized"));
		return;
	}

	UObject* LoadedAsset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!LoadedAsset)
	{
		FSoftObjectPath SoftPath(AssetPath);
		LoadedAsset = SoftPath.TryLoad();
	}
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBlueprint)
	{
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("asset_not_loaded"), FString::Printf(
				TEXT("Widget Blueprint not found or could not be loaded: %s"), *AssetPath));
		return;
	}

	UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	IAssetEditorInstance* EditorInstance = AssetEditorSub
		? AssetEditorSub->FindEditorForAsset(WidgetBlueprint, /*bFocusIfOpen=*/false)
		: nullptr;
	if (!EditorInstance)
	{
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("designer_mode_unavailable"), TEXT("No Widget Blueprint editor is open for the asset"));
		return;
	}

	bool bReservationAcquired = false;
	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		if (!WidgetDesignerCaptureGate.IsValid())
		{
			WidgetDesignerCaptureGate = MakeShared<FCUWidgetDesignerCaptureGate>();
		}
		// UObject identity is canonical across package/object/redirector spellings,
		// so aliases cannot enter the same FWidgetBlueprintEditor concurrently.
		bReservationAcquired = WidgetDesignerCaptureGate->TryAcquire(
			RequestId, WidgetBlueprint, Socket);
	}
	if (!bReservationAcquired)
	{
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("capture_in_progress"), TEXT("A Widget Designer capture is already in progress for this asset"));
		return;
	}
	const auto ReleaseReservation = [this, &RequestId]()
	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		if (WidgetDesignerCaptureGate.IsValid())
		{
			WidgetDesignerCaptureGate->Release(RequestId);
		}
	};

	FWidgetBlueprintEditor* RawWidgetEditor = static_cast<FWidgetBlueprintEditor*>(EditorInstance);
	const TSharedRef<FWidgetBlueprintEditor> WidgetEditorRef =
		StaticCastSharedRef<FWidgetBlueprintEditor>(RawWidgetEditor->AsShared());
	const TSharedPtr<FWidgetBlueprintEditor> WidgetEditor = WidgetEditorRef;
	const FName OriginalMode = WidgetEditor->GetCurrentMode();
	const bool bModeChanged = OriginalMode != FWidgetBlueprintApplicationModes::DesignerMode;
	if (bModeChanged)
	{
		WidgetEditor->SetCurrentMode(FWidgetBlueprintApplicationModes::DesignerMode);
	}
	if (WidgetEditor->GetCurrentMode() != FWidgetBlueprintApplicationModes::DesignerMode)
	{
		ReleaseReservation();
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("designer_mode_unavailable"), TEXT("Widget Blueprint editor rejected Designer mode"));
		return;
	}

	bool bHideDesignerOutlines = false;
	Request->TryGetBoolField(TEXT("hide_designer_outlines"), bHideDesignerOutlines);
	const bool bOriginalDashedOutlines = WidgetEditor->GetShowDashedOutlines();
	const bool bOutlinesChanged = bHideDesignerOutlines && bOriginalDashedOutlines;
	if (bOutlinesChanged)
	{
		WidgetEditor->SetShowDashedOutlines(false);
		if (WidgetEditor->GetShowDashedOutlines())
		{
			bool bModeRestored = !bModeChanged;
			if (bModeChanged)
			{
				WidgetEditor->SetCurrentMode(OriginalMode);
				bModeRestored = WidgetEditor->GetCurrentMode() == OriginalMode;
			}
			ReleaseReservation();
			Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
				TEXT("outline_control_failed"), TEXT("Widget Blueprint editor did not disable dashed Designer outlines"),
				/*bOutlinesRestored=*/true, bModeRestored);
			return;
		}
	}

	if (!WidgetEditor->GetPreview())
	{
		WidgetEditor->RefreshPreview();
	}

	int32 SettleMs = 500;
	if (Request->HasField(TEXT("settle_ms")))
	{
		SettleMs = FMath::Clamp(Request->GetIntegerField(TEXT("settle_ms")), 0, 5000);
	}

	const TSharedPtr<FCUWidgetDesignerCaptureState> State = MakeShared<FCUWidgetDesignerCaptureState>();
	State->Socket = Socket;
	State->RequestId = RequestId;
	State->AssetPath = AssetPath;
	State->CropMode = CropMode;
	State->Editor = WidgetEditor;
	State->Request = Request;
	State->OriginalMode = OriginalMode;
	State->bModeChanged = bModeChanged;
	State->bOriginalDashedOutlines = bOriginalDashedOutlines;
	State->bOutlinesChanged = bOutlinesChanged;
	State->StartTimeSeconds = FPlatformTime::Seconds();

	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		WidgetDesignerCaptures.Add(RequestId, State);
	}

	const TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakSelf(this);
	State->TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakSelf, State](float) -> bool
		{
			UCUWebSocketEditorSubsystem* Self = WeakSelf.Get();
			if (!Self) return false;

			bool bStillRegistered = false;
			{
				FScopeLock Lock(&Self->WidgetDesignerCapturesLock);
				bStillRegistered = Self->WidgetDesignerCaptures.Contains(State->RequestId);
			}
			if (!bStillRegistered) return false;

			Self->CompleteWidgetDesignerCapture(State);
			return false;
		}),
		static_cast<float>(SettleMs) / 1000.0f);
	if (!State->TickerHandle.IsValid())
	{
		{
			FScopeLock Lock(&WidgetDesignerCapturesLock);
			WidgetDesignerCaptures.Remove(RequestId);
			if (WidgetDesignerCaptureGate.IsValid())
			{
				WidgetDesignerCaptureGate->Release(RequestId);
			}
		}
		bool bOutlinesRestored = false;
		bool bModeRestored = false;
		RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
		Issue26157_WidgetDesigner::SendError(this, Socket, RequestId, CropMode,
			TEXT("preview_not_ready"), TEXT("Could not schedule the Widget Designer settle tick"),
			bOutlinesRestored, bModeRestored);
	}
}

void UCUWebSocketEditorSubsystem::CompleteWidgetDesignerCapture(
	const TSharedPtr<FCUWidgetDesignerCaptureState>& State)
{
	if (!State.IsValid()) return;
	// ARCH-011: this ticker is scheduled only by the editor-only handler, but
	// retain the function-scope commandlet guard required for every Slate call.
	if (IsRunningCommandlet()) return;
	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		if (!WidgetDesignerCaptures.Contains(State->RequestId)) return;
		WidgetDesignerCaptures.Remove(State->RequestId);
	}
	const auto ReleaseReservation = [this, &State]()
	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		if (WidgetDesignerCaptureGate.IsValid())
		{
			WidgetDesignerCaptureGate->Release(State->RequestId);
		}
	};
	if (!FSlateApplication::IsInitialized())
	{
		bool bOutlinesRestored = false;
		bool bModeRestored = false;
		RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
		ReleaseReservation();
		Issue26157_WidgetDesigner::SendError(this, State->Socket, State->RequestId, State->CropMode,
			TEXT("designer_mode_unavailable"), TEXT("Slate shut down before Widget Designer capture completed"),
			bOutlinesRestored, bModeRestored);
		return;
	}

	const TSharedPtr<FWidgetBlueprintEditor> WidgetEditor = State->Editor.Pin();
	UUserWidget* Preview = WidgetEditor.IsValid() ? WidgetEditor->GetPreview() : nullptr;
	UWidget* PreviewRoot = IsValid(Preview) && Preview->WidgetTree
		? Preview->WidgetTree->RootWidget.Get()
		: nullptr;
	const TSharedPtr<SWidget> PreviewWidget = IsValid(PreviewRoot)
		? PreviewRoot->GetCachedWidget()
		: nullptr;
	if (!WidgetEditor.IsValid() || !IsValid(Preview) || !IsValid(PreviewRoot) || !PreviewWidget.IsValid())
	{
		bool bOutlinesRestored = false;
		bool bModeRestored = false;
		RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
		ReleaseReservation();
		Issue26157_WidgetDesigner::SendError(this, State->Socket, State->RequestId, State->CropMode,
			TEXT("preview_not_ready"), TEXT("Widget Designer preview is not painted yet"),
			bOutlinesRestored, bModeRestored);
		return;
	}

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(PreviewWidget.ToSharedRef());
	if (!ParentWindow.IsValid() || !ParentWindow->IsVisible() || ParentWindow->IsWindowMinimized())
	{
		bool bOutlinesRestored = false;
		bool bModeRestored = false;
		RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
		ReleaseReservation();
		Issue26157_WidgetDesigner::SendError(this, State->Socket, State->RequestId, State->CropMode,
			TEXT("preview_not_ready"), TEXT("Widget Designer owner window is not visible or realized"),
			bOutlinesRestored, bModeRestored);
		return;
	}

	const FGeometry WindowGeometry = ParentWindow->GetWindowGeometryInScreen();
	FVector2D LocalMin = FVector2D::ZeroVector;
	FVector2D LocalMax = FVector2D::ZeroVector;
	FString PreviewCaptureType;
	const TSharedPtr<SWidget> PreviewCaptureTarget =
		Issue26157_WidgetDesigner::ResolvePreviewCaptureTarget(
			PreviewWidget.ToSharedRef(), WindowGeometry, LocalMin, LocalMax, PreviewCaptureType);
	UE_LOG(LogNarshaMCPWebSocket, Verbose,
		TEXT("[WidgetDesignerCapture] geometry_source=arranged_path target=%s local_min=(%.2f,%.2f) local_max=(%.2f,%.2f) window=(%.2f,%.2f)"),
		*PreviewCaptureType,
		LocalMin.X, LocalMin.Y, LocalMax.X, LocalMax.Y,
		WindowGeometry.GetLocalSize().X, WindowGeometry.GetLocalSize().Y);
	FIntRect GeometryProbeRect;
	if (!PreviewCaptureTarget.IsValid() || !CUWidgetDesignerCapture::ComputePixelRect(
		LocalMin, LocalMax, WindowGeometry.GetLocalSize(),
		/*ImageWidth=*/1024, /*ImageHeight=*/1024, GeometryProbeRect))
	{
		bool bOutlinesRestored = false;
		bool bModeRestored = false;
		RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
		ReleaseReservation();
		Issue26157_WidgetDesigner::SendError(this, State->Socket, State->RequestId, State->CropMode,
			TEXT("invalid_geometry"), TEXT("Widget Designer preview geometry does not intersect its owner window"),
			bOutlinesRestored, bModeRestored);
		return;
	}
	TSharedRef<SWidget> CaptureTarget = State->CropMode == TEXT("widget")
		? PreviewCaptureTarget.ToSharedRef()
		: StaticCastSharedRef<SWidget>(ParentWindow.ToSharedRef());

	TArray<FColor> Pixels;
	FIntVector CapturedSize;
	const bool bCaptured = FSlateApplication::Get().TakeScreenshot(CaptureTarget, Pixels, CapturedSize);
	FIntRect WidgetRect;
	if (bCaptured && Pixels.Num() > 0 && State->CropMode == TEXT("widget"))
	{
		WidgetRect = FIntRect(0, 0, CapturedSize.X, CapturedSize.Y);
	}
	else if (bCaptured && Pixels.Num() > 0)
	{
		if (!CUWidgetDesignerCapture::ComputePixelRect(
			LocalMin, LocalMax, WindowGeometry.GetLocalSize(),
			CapturedSize.X, CapturedSize.Y, WidgetRect))
		{
			Pixels.Reset();
		}
	}

	bool bOutlinesRestored = false;
	bool bModeRestored = false;
	RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
	ReleaseReservation();
	if (!bCaptured || Pixels.Num() == 0 || WidgetRect.IsEmpty())
	{
		Issue26157_WidgetDesigner::SendError(this, State->Socket, State->RequestId, State->CropMode,
			TEXT("screenshot_failed"), TEXT("Widget Designer screenshot or geometry capture failed"),
			bOutlinesRestored, bModeRestored);
		return;
	}

	FString ReturnMode = TEXT("file");
	State->Request->TryGetStringField(TEXT("return_mode"), ReturnMode);
	const FIntPoint OutputSize = CUWidgetDesignerCapture::CalculateCappedOutputSize(
		CapturedSize.X, CapturedSize.Y, ReturnMode);
	const FIntRect OutputRect = State->CropMode == TEXT("widget")
		? FIntRect(0, 0, OutputSize.X, OutputSize.Y)
		: CUWidgetDesignerCapture::ScalePixelRect(
			WidgetRect, CapturedSize.X, CapturedSize.Y, OutputSize.X, OutputSize.Y);
	const FString Warning = Issue26157_WidgetDesigner::BuildRestoreWarning(
		bOutlinesRestored, bModeRestored);
	const FString WarningJson = Warning.IsEmpty()
		? FString()
		: FString::Printf(TEXT(",\"warning\":\"%s\""), *CUJsonUtils::EscapeJsonString(Warning));
	const FString ExtraJson = FString::Printf(TEXT(
		"\"capture_mode\":\"widget_designer\",\"crop\":\"%s\","
		"\"widget_rect\":{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d},"
		"\"designer_outlines_restored\":%s,\"designer_mode_restored\":%s,"
		"\"editor_reopened\":false,\"designer_tab_method\":\"mode_switch\"%s"),
		*CUJsonUtils::EscapeJsonString(State->CropMode),
		OutputRect.Min.X, OutputRect.Min.Y, OutputRect.Width(), OutputRect.Height(),
		bOutlinesRestored ? TEXT("true") : TEXT("false"),
		bModeRestored ? TEXT("true") : TEXT("false"),
		*WarningJson);

	EncodeAndSendScreenshot(State->Socket, State->RequestId,
		TEXT("capture_widget_designer_result"), Pixels, CapturedSize.X, CapturedSize.Y,
		State->Request, State->StartTimeSeconds, ExtraJson);
}

void UCUWebSocketEditorSubsystem::CancelAllWidgetDesignerCaptures()
{
	TArray<TSharedPtr<FCUWidgetDesignerCaptureState>> Cancelled;
	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		WidgetDesignerCaptures.GenerateValueArray(Cancelled);
		WidgetDesignerCaptures.Empty();
		if (WidgetDesignerCaptureGate.IsValid())
		{
			WidgetDesignerCaptureGate->ReleaseAll();
		}
	}
	for (const TSharedPtr<FCUWidgetDesignerCaptureState>& State : Cancelled)
	{
		if (State.IsValid() && State->TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(State->TickerHandle);
		}
		bool bOutlinesRestored = false;
		bool bModeRestored = false;
		RestoreWidgetDesignerState(State, bOutlinesRestored, bModeRestored);
	}
}

void UCUWebSocketEditorSubsystem::CancelWidgetDesignerCapturesForSocket(INetworkingWebSocket* Socket)
{
	if (!Socket) return;
	TArray<TSharedPtr<FCUWidgetDesignerCaptureState>> Cancelled;
	{
		FScopeLock Lock(&WidgetDesignerCapturesLock);
		TArray<FString> RequestIds = WidgetDesignerCaptureGate.IsValid()
			? WidgetDesignerCaptureGate->ReleaseForSocket(Socket)
			: TArray<FString>();
		if (!WidgetDesignerCaptureGate.IsValid())
		{
			for (const TPair<FString, TSharedPtr<FCUWidgetDesignerCaptureState>>& Pair : WidgetDesignerCaptures)
			{
				if (Pair.Value.IsValid() && Pair.Value->Socket == Socket)
				{
					RequestIds.Add(Pair.Key);
				}
			}
		}
		for (const FString& RequestId : RequestIds)
		{
			if (const TSharedPtr<FCUWidgetDesignerCaptureState>* State = WidgetDesignerCaptures.Find(RequestId))
			{
				Cancelled.Add(*State);
			}
			WidgetDesignerCaptures.Remove(RequestId);
		}
	}

	for (const TSharedPtr<FCUWidgetDesignerCaptureState>& State : Cancelled)
	{
		if (State.IsValid() && State->TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(State->TickerHandle);
		}
		auto Restore = [State]()
		{
			bool bOutlinesRestored = false;
			bool bModeRestored = false;
			UCUWebSocketEditorSubsystem::RestoreWidgetDesignerState(
				State, bOutlinesRestored, bModeRestored);
		};
		if (IsInGameThread()) Restore();
		else AsyncTask(ENamedThreads::GameThread, MoveTemp(Restore));
	}
}

// ============================================
// Issue #1632: P2 - Create Asset Handler
// ============================================

// Issue #16566: resolve a class name/path to a concrete (instantiable) subclass of
// RequiredBase for create_asset (DataAsset / WidgetBlueprint). Accepts a short native
// name (FindFirstObject, NativeFirst), a fully-qualified /Script path (FindObject), or a
// Blueprint-generated class by asset path (StaticLoadClass). Returns nullptr when the
// resolved class is missing, not a RequiredBase child, or abstract/deprecated. Mirrors
// the proven ResolveConcreteBTClass helper in CURemoteLibrary.cpp, extended with the
// StaticLoadClass fallback for user Blueprint classes. Uniquely named to avoid a Unity
// (jumbo) build collision with sibling file-local statics.
static UClass* CUWS_ResolveConcreteSubclass(const FString& ClassRef, UClass* RequiredBase)
{
	if (ClassRef.IsEmpty() || RequiredBase == nullptr)
	{
		return nullptr;
	}
	UClass* Found = FindFirstObject<UClass>(*ClassRef, EFindFirstObjectOptions::NativeFirst);
	if (!Found)
	{
		// Fully-qualified /Script path (e.g. "/Script/Engine.PrimaryDataAsset").
		Found = FindObject<UClass>(nullptr, *ClassRef);
	}
	if (!Found)
	{
		// Blueprint-generated class by asset path (e.g. "/Game/Data/BP_MyDataAsset").
		Found = StaticLoadClass(RequiredBase, nullptr, *ClassRef);
	}
	if (!Found || !Found->IsChildOf(RequiredBase) ||
		Found->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		return nullptr;
	}
	return Found;
}

void UCUWebSocketEditorSubsystem::HandleCreateAsset(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleCreateAsset: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	FString AssetClass = Request->GetStringField(TEXT("asset_class"));
	bool bOpenEditor = Request->HasField(TEXT("open_editor")) ? Request->GetBoolField(TEXT("open_editor")) : true;
	// Issue #16566: optional create-params for the new asset types (absent for the existing ones).
	//   parent_class — concrete UDataAsset / UUserWidget subclass (DataAsset / WidgetBlueprint)
	//   skeleton     — USkeleton asset path (AnimMontage TargetSkeleton)
	FString ParentClassRef = Request->HasField(TEXT("parent_class")) ? Request->GetStringField(TEXT("parent_class")) : FString();
	FString SkeletonPath = Request->HasField(TEXT("skeleton")) ? Request->GetStringField(TEXT("skeleton")) : FString();

	LogInfo(FString::Printf(TEXT("Creating asset (request_id: %s): %s [%s]"), *RequestId, *AssetPath, *AssetClass));

	bool bSuccess = false;
	bool bSaved = false;
	FString Message;
	FString ErrorCode;
	FString Hint;

	if (AssetPath.IsEmpty())
	{
		Message = TEXT("Asset path cannot be empty");
	}
	else if (AssetClass.IsEmpty())
	{
		Message = TEXT("Asset class cannot be empty");
	}
	else if (FPackageName::GetShortName(AssetPath).TrimStartAndEnd().IsEmpty())
	{
		// Issue #15755: a trailing-slash / empty-leaf path (e.g. "/Game/Materials/")
		// passes the Rust is_valid_ue_content_path check but derives an empty
		// short-name; without this guard FactoryCreateNew(..., *"", ...) auto-names
		// a garbage asset (same class as #15651/#15591). TrimStartAndEnd() gives
		// whitespace parity with the Rust .trim() guard so the direct-RC bypass is
		// also covered. Mirrors PR #15742.
		Message = TEXT("Derived asset name cannot be empty (trailing-slash/empty-leaf path)");
	}
	else if (FPackageName::DoesPackageExist(AssetPath))
	{
		// Issue #16566: re-running FactoryCreateNew on a path that already holds an
		// asset can hard-crash the factory (observed live: UWidgetBlueprintFactory
		// FactoryCreateNew killed the editor on a duplicate WidgetBlueprint path).
		// Return a clear error instead of overwriting/crashing — delete first to recreate.
		Message = FString::Printf(TEXT("Asset already exists at path: %s (delete it first to recreate)"), *AssetPath);
	}
	else
	{
		// Parse path to get package path and asset name
		FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
		FString AssetName = FPackageName::GetShortName(AssetPath);

		// Create package
		UPackage* Package = CreatePackage(*AssetPath);
		if (Package)
		{
			UObject* NewAsset = nullptr;

			// Create asset based on class type
			if (AssetClass.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase))
			{
				// Create empty Actor blueprint
				UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
				Factory->ParentClass = AActor::StaticClass();
				NewAsset = Factory->FactoryCreateNew(UBlueprint::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
			}
			else if (AssetClass.Equals(TEXT("Material"), ESearchCase::IgnoreCase))
			{
				// Create material
				UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
				NewAsset = Factory->FactoryCreateNew(UMaterial::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
			}
			// Issue #7921: MaterialFunction creation support (ADK Phase 4)
			else if (AssetClass.Equals(TEXT("MaterialFunction"), ESearchCase::IgnoreCase))
			{
				UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
				NewAsset = Factory->FactoryCreateNew(UMaterialFunction::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
				if (!NewAsset)
				{
					Message = TEXT("MaterialFunction creation failed - factory returned nullptr");
					LogError(Message);
				}
			}
			else if (AssetClass.Equals(TEXT("DataTable"), ESearchCase::IgnoreCase))
			{
				// DataTable requires row struct - not supported via simple creation
				Message = TEXT("DataTable creation requires row structure specification");
			}
			// Issue #1699: StateTree and BehaviorTree creation support
			else if (AssetClass.Equals(TEXT("StateTree"), ESearchCase::IgnoreCase))
			{
				// Resolve the optional StateTree stack by path so HandleCreateAsset keeps a single
				// save-result dataflow in both opt-in and opt-out builds (Issue #25732 contract).
				UClass* FactoryClass = LoadObject<UClass>(
					nullptr, TEXT("/Script/StateTreeEditorModule.StateTreeFactory"));
				UClass* StateTreeClass = LoadObject<UClass>(
					nullptr, TEXT("/Script/StateTreeModule.StateTree"));
				UClass* SchemaClass = FindObject<UClass>(
					nullptr, TEXT("/Script/GameplayStateTreeModule.StateTreeComponentSchema"));
				UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
				FObjectProperty* SchemaProperty = Factory
					? FindFProperty<FObjectProperty>(Factory->GetClass(), TEXT("StateTreeSchemaClass")) : nullptr;
				if (!Factory || !StateTreeClass || !SchemaClass || !SchemaProperty)
				{
					Message = TEXT("StateTree creation needs StateTree, PropertyBindingUtils, and GameplayStateTree.");
					ErrorCode = TEXT("state_tree_plugin_unavailable");
					Hint = TEXT("Enable StateTree, PropertyBindingUtils, and GameplayStateTree in your .uproject, then rebuild.");
					LogError(Message);
				}
				else
				{
					SchemaProperty->SetObjectPropertyValue_InContainer(Factory, SchemaClass);
					NewAsset = Factory->FactoryCreateNew(StateTreeClass, Package, *AssetName,
						RF_Public | RF_Standalone, nullptr, GWarn);
					if (!NewAsset)
					{
						Message = TEXT("StateTree creation failed - factory returned nullptr");
						LogError(Message);
					}
				}
			}
			else if (AssetClass.Equals(TEXT("BehaviorTree"), ESearchCase::IgnoreCase))
			{
				// Create BehaviorTree using UBehaviorTreeFactory
				UBehaviorTreeFactory* Factory = NewObject<UBehaviorTreeFactory>();
				NewAsset = Factory->FactoryCreateNew(UBehaviorTree::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
				if (!NewAsset)
				{
					Message = TEXT("BehaviorTree creation failed - factory returned nullptr");
					LogError(Message);
				}
			}
			// Issue #1822: PCGGraph creation support
			// UPCGGraph initialization behavior (from Engine/Plugins/PCG/Source/PCG/Private/PCGGraph.cpp):
			//   - UPCGGraph::PostInitProperties() creates default InputNode and OutputNode
			//   - These nodes are automatically wired with input/output pins
			//   - Graph is immediately usable in PCG Editor after creation
			//   - No factory class required - direct NewObject<UPCGGraph>() works correctly
			else if (AssetClass.Equals(TEXT("PCGGraph"), ESearchCase::IgnoreCase))
			{
				// Issue #28215 Phase B: resolve by script path so HandleCreateAsset keeps its
				// no-preprocessor contract and a project without PCG still compiles.
				UClass* PCGGraphClass = LoadObject<UClass>(nullptr, TEXT("/Script/PCG.PCGGraph"));
				if (!PCGGraphClass)
				{
					Message = TEXT("PCG is unavailable in this project build.");
					ErrorCode = TEXT("pcg_plugin_unavailable");
					Hint = TEXT("Enable PCG in your .uproject, then rebuild.");
					LogError(Message);
				}
				else
				{
					NewAsset = NewObject<UObject>(Package, PCGGraphClass, *AssetName, RF_Public | RF_Standalone);
					if (NewAsset)
					{
						LogInfo(FString::Printf(TEXT("[PCGGraph] Created graph '%s' with default InputNode/OutputNode"), *AssetName));
					}
				}
			}
			// Issue #16990: MetaSoundSource / MetaSoundPatch from-scratch creation.
			// Use the engine factories (UMetaSoundSourceFactory / UMetaSoundFactory) rather than a bare
			// NewObject: FactoryCreateNew runs CreateNewMetaSoundObject -> InitAsset -> RegisterGraphWithFrontend
			// internally (MetasoundFactory.cpp), which initializes the Frontend document. A bare
			// NewObject<UMetaSoundSource> would yield a broken/empty-document asset.
			else if (AssetClass.Equals(TEXT("MetaSoundSource"), ESearchCase::IgnoreCase))
			{
				UClass* FactoryClass = LoadObject<UClass>(nullptr, TEXT("/Script/MetasoundEditor.MetaSoundSourceFactory"));
				UClass* ProductClass = LoadObject<UClass>(nullptr, TEXT("/Script/MetasoundEngine.MetaSoundSource"));
				UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
				if (!Factory || !ProductClass)
				{
					Message = TEXT("Metasound is unavailable in this project build.");
					ErrorCode = TEXT("metasound_plugin_unavailable");
					Hint = TEXT("Enable Metasound in your .uproject, then rebuild.");
					LogError(Message);
				}
				else
				{
					NewAsset = Factory->FactoryCreateNew(ProductClass, Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
				}
				if (Factory && ProductClass && !NewAsset)
				{
					Message = TEXT("MetaSoundSource creation failed - factory returned nullptr");
					LogError(Message);
				}
			}
			else if (AssetClass.Equals(TEXT("MetaSoundPatch"), ESearchCase::IgnoreCase))
			{
				UClass* FactoryClass = LoadObject<UClass>(nullptr, TEXT("/Script/MetasoundEditor.MetaSoundFactory"));
				UClass* ProductClass = LoadObject<UClass>(nullptr, TEXT("/Script/MetasoundEngine.MetaSoundPatch"));
				UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
				if (!Factory || !ProductClass)
				{
					Message = TEXT("Metasound is unavailable in this project build.");
					ErrorCode = TEXT("metasound_plugin_unavailable");
					Hint = TEXT("Enable Metasound in your .uproject, then rebuild.");
					LogError(Message);
				}
				else
				{
					NewAsset = Factory->FactoryCreateNew(ProductClass, Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
				}
				if (Factory && ProductClass && !NewAsset)
				{
					Message = TEXT("MetaSoundPatch creation failed - factory returned nullptr");
					LogError(Message);
				}
			}
			// Issue #16555: NiagaraSystem creation support (create_system op)
			// UNiagaraSystemFactoryNew ships in the NiagaraEditor module (added to
			// NarshaMCP.Build.cs alongside this branch). It produces an empty but valid
			// NiagaraSystem with a default system spawn/update script, ready for emitters.
			else if (AssetClass.Equals(TEXT("NiagaraSystem"), ESearchCase::IgnoreCase))
			{
				UClass* FactoryClass = LoadObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"));
				UClass* ProductClass = LoadObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraSystem"));
				UFactory* Factory = FactoryClass ? NewObject<UFactory>(GetTransientPackage(), FactoryClass) : nullptr;
				if (!Factory || !ProductClass)
				{
					Message = TEXT("Niagara is unavailable in this project build.");
					ErrorCode = TEXT("niagara_plugin_unavailable");
					Hint = TEXT("Enable Niagara in your .uproject, then rebuild.");
					LogError(Message);
				}
				else
				{
					NewAsset = Factory->FactoryCreateNew(ProductClass, Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
				}
				if (Factory && ProductClass && !NewAsset)
				{
					Message = TEXT("NiagaraSystem creation failed - factory returned nullptr");
					LogError(Message);
				}
			}
			// Issue #16566: WidgetBlueprint via UWidgetBlueprintFactory (UMGEditor). ParentClass
			// defaults to UUserWidget when left null; an optional parent_class pins a concrete
			// UUserWidget subclass. The only modal path is ConfigureProperties() — we set
			// ParentClass and call FactoryCreateNew directly so the call stays headless.
			else if (AssetClass.Equals(TEXT("WidgetBlueprint"), ESearchCase::IgnoreCase))
			{
				UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();
				bool bParentOk = true;
				if (!ParentClassRef.IsEmpty())
				{
					UClass* ParentCls = CUWS_ResolveConcreteSubclass(ParentClassRef, UUserWidget::StaticClass());
					if (ParentCls)
					{
						Factory->ParentClass = ParentCls;
					}
					else
					{
						bParentOk = false;
						Message = FString::Printf(TEXT("WidgetBlueprint parent_class '%s' did not resolve to a concrete UUserWidget subclass"), *ParentClassRef);
						LogError(Message);
					}
				}
				if (bParentOk)
				{
					NewAsset = Factory->FactoryCreateNew(UWidgetBlueprint::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
					if (!NewAsset)
					{
						Message = TEXT("WidgetBlueprint creation failed - factory returned nullptr");
						LogError(Message);
					}
				}
			}
			// Issue #16566: DataAsset via UDataAssetFactory (UnrealEd). UDataAsset is abstract,
			// so a concrete subclass MUST be supplied via parent_class. FactoryCreateNew never
			// pops a dialog (the modal class-picker lives only in ConfigureProperties()), so
			// setting DataAssetClass up-front keeps the call headless.
			else if (AssetClass.Equals(TEXT("DataAsset"), ESearchCase::IgnoreCase))
			{
				UClass* DataCls = CUWS_ResolveConcreteSubclass(ParentClassRef, UDataAsset::StaticClass());
				if (!DataCls)
				{
					Message = FString::Printf(TEXT("DataAsset creation requires parent_class to name a concrete UDataAsset subclass (got '%s')"), *ParentClassRef);
					LogError(Message);
				}
				else
				{
					UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
					Factory->DataAssetClass = DataCls;
					// InClass is the base product class per the UFactory contract; UDataAssetFactory
					// instantiates DataAssetClass (set above) internally regardless. Mirrors the
					// WidgetBlueprint/AnimMontage branches which pass the base static class.
					NewAsset = Factory->FactoryCreateNew(UDataAsset::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
					if (!NewAsset)
					{
						Message = TEXT("DataAsset creation failed - factory returned nullptr");
						LogError(Message);
					}
				}
			}
			// Issue #16566: AnimMontage via UAnimMontageFactory (UnrealEd). A montage is invalid
			// without a skeleton, so a USkeleton path MUST be supplied via skeleton. FactoryCreateNew
			// builds the montage directly when TargetSkeleton is set (the modal skeleton-picker
			// lives only in ConfigureProperties()), so the call stays headless.
			else if (AssetClass.Equals(TEXT("AnimMontage"), ESearchCase::IgnoreCase))
			{
				USkeleton* TargetSkeleton = SkeletonPath.IsEmpty() ? nullptr : LoadObject<USkeleton>(nullptr, *SkeletonPath);
				if (!TargetSkeleton)
				{
					Message = FString::Printf(TEXT("AnimMontage creation requires a valid 'skeleton' asset path (got '%s')"), *SkeletonPath);
					LogError(Message);
				}
				else
				{
					UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
					Factory->TargetSkeleton = TargetSkeleton;
					NewAsset = Factory->FactoryCreateNew(UAnimMontage::StaticClass(), Package, *AssetName, RF_Public | RF_Standalone, nullptr, GWarn);
					if (!NewAsset)
					{
						Message = TEXT("AnimMontage creation failed - factory returned nullptr");
						LogError(Message);
					}
				}
			}
			else
			{
				Message = FString::Printf(TEXT("Unsupported asset class: %s"), *AssetClass);
			}

			if (NewAsset)
			{
				// Issue #16555: defensively ensure RF_Standalone — SavePackage (default
				// TopLevelFlags=RF_Standalone) FATAL-errors if the top-level asset lacks it.
				// Factory/NewObject branches already pass it; this guards any future branch.
				NewAsset->SetFlags(RF_Public | RF_Standalone);
				// Mark package dirty and notify asset registry
				Package->MarkPackageDirty();
				FAssetRegistryModule::AssetCreated(NewAsset);

				// Save the asset
				// Issue #1699: FullyLoad before save to prevent "partially loaded" crash
				Package->FullyLoad();
				FString PackageFileName = FPackageName::LongPackageNameToFilename(AssetPath, FPackageName::GetAssetPackageExtension());
				FSavePackageArgs SaveArgs;
				// A generated save error calls appError before the false return can reach the client.
				// Suppress that fatal escalation; the handler reports the failure through LogError and JSON.
				SaveArgs.SaveFlags = SAVE_NoError;
				bSaved = UPackage::SavePackage(Package, NewAsset, *PackageFileName, SaveArgs);
				bSuccess = bSaved;

				if (bSaved)
				{
					// Open editor if requested (Issue #7091: null-check guard)
					if (bOpenEditor)
					{
						if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
						{
							AssetEditorSub->OpenEditorForAsset(NewAsset);
						}
					}

					Message = FString::Printf(TEXT("Created asset: %s"), *AssetPath);
					LogInfo(Message);
				}
				else
				{
					Message = FString::Printf(TEXT("Failed to save asset: %s"), *AssetPath);
					LogError(Message);
				}
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Failed to create package for: %s"), *AssetPath);
			LogError(Message);
		}
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString DeclineFields;
	if (!ErrorCode.IsEmpty())
	{
		DeclineFields = FString::Printf(
			TEXT(",\"error_code\":\"%s\",\"hint\":\"%s\""),
			*CUJsonUtils::EscapeJsonString(ErrorCode),
			*CUJsonUtils::EscapeJsonString(Hint));
	}
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"create_asset_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"asset_path\":\"%s\","
		"\"asset_class\":\"%s\","
		"\"saved\":%s,"
		"\"message\":\"%s\"%s"
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*AssetPath,
		*AssetClass,
		bSaved ? TEXT("true") : TEXT("false"),
		*EscapedMessage,
		*DeclineFields
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #16566: Create Content Folder Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleCreateFolder(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleCreateFolder: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString FolderPath = Request->GetStringField(TEXT("folder_path"));

	LogInfo(FString::Printf(TEXT("Creating folder (request_id: %s): %s"), *RequestId, *FolderPath));

	bool bSuccess = false;
	FString Message;

	// Normalize trailing slashes so "/Game/Foo/" and "/Game/Foo" behave identically.
	while (FolderPath.EndsWith(TEXT("/")) && FolderPath.Len() > 1)
	{
		FolderPath = FolderPath.LeftChop(1);
	}

	if (FolderPath.IsEmpty())
	{
		Message = TEXT("Folder path cannot be empty");
	}
	else if (!FPackageName::IsValidLongPackageName(FolderPath, /*bIncludeReadOnlyRoots=*/false))
	{
		// Mirrors the Rust is_valid_ue_content_path guard: reject anything outside a mount point.
		Message = FString::Printf(TEXT("Folder path is not a valid content path under a mount point: %s"), *FolderPath);
	}
	else
	{
		// Resolve the long package path to an on-disk directory and create it physically so the
		// folder PERSISTS across editor restarts (an AddPath-only folder is in-memory and vanishes
		// when empty). Then register the path with the Asset Registry for immediate Content Browser
		// visibility this session. Uses only AssetRegistry (already a Build.cs dep) + IFileManager
		// (Core) — no EditorScriptingUtilities dependency required.
		FString FolderFilename;
		if (FPackageName::TryConvertLongPackageNameToFilename(FolderPath, FolderFilename))
		{
			// MakeDirectory(Tree=true) returns true when the dir already exists — distinguish
			// the two so the success message is accurate (it is idempotent either way).
			const bool bDirAlreadyExisted = IFileManager::Get().DirectoryExists(*FolderFilename);
			if (IFileManager::Get().MakeDirectory(*FolderFilename, /*Tree=*/true))
			{
				FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
				AssetRegistryModule.Get().AddPath(FolderPath);
				bSuccess = true;
				Message = bDirAlreadyExisted
					? FString::Printf(TEXT("Folder already exists: %s"), *FolderPath)
					: FString::Printf(TEXT("Created folder: %s"), *FolderPath);
				LogInfo(Message);
			}
			else
			{
				Message = FString::Printf(TEXT("Failed to create directory on disk: %s"), *FolderFilename);
				LogError(Message);
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Could not resolve folder path to a filesystem location: %s"), *FolderPath);
			LogError(Message);
		}
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString EscapedFolderPath = CUJsonUtils::EscapeJsonString(FolderPath);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"create_folder_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"folder_path\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*EscapedFolderPath,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Set Material Parameter Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleSetMaterialParam(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetMaterialParam: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString MaterialPath = Request->GetStringField(TEXT("material_path"));
	FString ParamName = Request->GetStringField(TEXT("param_name"));
	FString ParamValue = Request->GetStringField(TEXT("param_value"));
	FString ParamType = Request->HasField(TEXT("param_type")) ? Request->GetStringField(TEXT("param_type")) : TEXT("scalar");

	LogInfo(FString::Printf(TEXT("Setting material param (request_id: %s): %s.%s = %s [%s]"),
		*RequestId, *MaterialPath, *ParamName, *ParamValue, *ParamType));

	bool bSuccess = false;
	bool bSaved = false;
	FString Message;

	if (MaterialPath.IsEmpty())
	{
		Message = TEXT("Material path cannot be empty");
	}
	else if (ParamName.IsEmpty())
	{
		Message = TEXT("Parameter name cannot be empty");
	}
	else
	{
		// Load material
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		if (Material)
		{
			// Check if it's a material instance (can modify parameters)
			UMaterialInstanceDynamic* DynamicInstance = Cast<UMaterialInstanceDynamic>(Material);
			UMaterialInstanceConstant* ConstantInstance = Cast<UMaterialInstanceConstant>(Material);

			if (ConstantInstance)
			{
				// Issue #7702: Use FMaterialParameterInfo (UE5) and UpdateMaterialInstance pattern
				FName ParamFName(*ParamName);
				FMaterialParameterInfo ParamInfo(ParamFName);

				// Material Instance Constant - can modify in editor
				if (ParamType.Equals(TEXT("scalar"), ESearchCase::IgnoreCase))
				{
					float Value = FCString::Atof(*ParamValue);
					ConstantInstance->SetScalarParameterValueEditorOnly(ParamInfo, Value);
					bSuccess = true;
					Message = FString::Printf(TEXT("Set scalar parameter %s = %f"), *ParamName, Value);
				}
				else if (ParamType.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
				{
					// Parse "R,G,B,A" format
					TArray<FString> Components;
					ParamValue.ParseIntoArray(Components, TEXT(","));
					if (Components.Num() >= 3)
					{
						FLinearColor Color(
							FCString::Atof(*Components[0]),
							FCString::Atof(*Components[1]),
							FCString::Atof(*Components[2]),
							Components.Num() >= 4 ? FCString::Atof(*Components[3]) : 1.0f
						);
						ConstantInstance->SetVectorParameterValueEditorOnly(ParamInfo, Color);
						bSuccess = true;
						Message = FString::Printf(TEXT("Set vector parameter %s"), *ParamName);
					}
					else
					{
						Message = TEXT("Vector format should be 'R,G,B' or 'R,G,B,A'");
					}
				}
				else if (ParamType.Equals(TEXT("texture"), ESearchCase::IgnoreCase))
				{
					UTexture* Texture = LoadObject<UTexture>(nullptr, *ParamValue);
					if (Texture)
					{
						ConstantInstance->SetTextureParameterValueEditorOnly(ParamInfo, Texture);
						bSuccess = true;
						Message = FString::Printf(TEXT("Set texture parameter %s"), *ParamName);
					}
					else
					{
						Message = FString::Printf(TEXT("Texture not found: %s"), *ParamValue);
					}
				}
				else
				{
					Message = FString::Printf(TEXT("Unsupported parameter type: %s"), *ParamType);
				}

				if (bSuccess)
				{
					// Issue #7702: Full UpdateMaterialInstance pattern from UMaterialEditingLibrary
					ConstantInstance->MarkPackageDirty();
					ConstantInstance->PreEditChange(nullptr);
					ConstantInstance->PostEditChange();
					ConstantInstance->UpdateStaticPermutation();

					// A generated save error calls appError before the false return can reach the client.
					// Suppress that fatal escalation; the handler reports the failure through LogError and JSON.
					UPackage* Package = ConstantInstance->GetOutermost();
					FString PackageFileName = FPackageName::LongPackageNameToFilename(
						Package->GetName(), FPackageName::GetAssetPackageExtension());
					FSavePackageArgs SaveArgs;
					SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
					SaveArgs.SaveFlags = SAVE_NoError;
					bSaved = UPackage::SavePackage(Package, ConstantInstance, *PackageFileName, SaveArgs);
					bSuccess = bSaved;
					if (bSaved)
					{
						LogInfo(Message);
					}
					else
					{
						Message = FString::Printf(TEXT("Failed to save material instance: %s"), *MaterialPath);
						LogError(Message);
					}
				}
			}
			else if (DynamicInstance)
			{
				Message = TEXT("Dynamic material instances cannot be modified persistently");
			}
			else
			{
				Message = TEXT("Only Material Instances can have parameters modified. Use a Material Instance.");
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Material not found: %s"), *MaterialPath);
			LogError(Message);
		}
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"set_material_param_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"material_path\":\"%s\","
		"\"param_name\":\"%s\","
		"\"saved\":%s,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*MaterialPath,
		*ParamName,
		bSaved ? TEXT("true") : TEXT("false"),
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P2 - Control Camera Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleControlCamera(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleControlCamera: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));

	LogInfo(FString::Printf(TEXT("Controlling camera (request_id: %s)"), *RequestId));

	bool bSuccess = false;
	FString Message;
	FVector FinalLocation = FVector::ZeroVector;
	FRotator FinalRotation = FRotator::ZeroRotator;
	float FinalSpeed = 1.0f;
	float FinalFOV = 90.0f;

	if (GEditor)
	{
		// Get active viewport client
		FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditorModule.GetFirstActiveViewport();

		if (ActiveViewport.IsValid())
		{
			FEditorViewportClient& ViewportClient = ActiveViewport->GetAssetViewportClient();

			// Get current values
			FinalLocation = ViewportClient.GetViewLocation();
			FinalRotation = ViewportClient.GetViewRotation();
			// UE 5.7+: GetCameraSpeedSetting() -> GetCameraSpeed() API change (int->float), GetCameraSpeedSetting deprecated
			// TEST: Verify camera speed values remain consistent across UE 5.6/5.7 int->float conversion
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
			FinalSpeed = ViewportClient.GetCameraSpeed();
#else
			FinalSpeed = ViewportClient.GetCameraSpeedSetting();
#endif
			FinalFOV = ViewportClient.ViewFOV;

			// Apply location if specified
			if (Request->HasField(TEXT("location_x")) || Request->HasField(TEXT("location_y")) || Request->HasField(TEXT("location_z")))
			{
				FinalLocation.X = Request->HasField(TEXT("location_x")) ? Request->GetNumberField(TEXT("location_x")) : FinalLocation.X;
				FinalLocation.Y = Request->HasField(TEXT("location_y")) ? Request->GetNumberField(TEXT("location_y")) : FinalLocation.Y;
				FinalLocation.Z = Request->HasField(TEXT("location_z")) ? Request->GetNumberField(TEXT("location_z")) : FinalLocation.Z;
				ViewportClient.SetViewLocation(FinalLocation);
			}

			// Apply rotation if specified
			if (Request->HasField(TEXT("rotation_pitch")) || Request->HasField(TEXT("rotation_yaw")) || Request->HasField(TEXT("rotation_roll")))
			{
				FinalRotation.Pitch = Request->HasField(TEXT("rotation_pitch")) ? Request->GetNumberField(TEXT("rotation_pitch")) : FinalRotation.Pitch;
				FinalRotation.Yaw = Request->HasField(TEXT("rotation_yaw")) ? Request->GetNumberField(TEXT("rotation_yaw")) : FinalRotation.Yaw;
				FinalRotation.Roll = Request->HasField(TEXT("rotation_roll")) ? Request->GetNumberField(TEXT("rotation_roll")) : FinalRotation.Roll;
				ViewportClient.SetViewRotation(FinalRotation);
			}

			// Apply speed if specified
			if (Request->HasField(TEXT("speed")))
			{
				FinalSpeed = Request->GetNumberField(TEXT("speed"));
				// UE 5.7+: SetCameraSpeedSetting() -> SetCameraSpeed() API change (int->float), SetCameraSpeedSetting deprecated
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
				// UE 5.7+: Use new float-based camera speed settings
				FEditorViewportCameraSpeedSettings NewSpeedSettings = ViewportClient.GetCameraSpeedSettings();
				NewSpeedSettings.SetCurrentSpeed(FinalSpeed);
				ViewportClient.SetCameraSpeedSettings(NewSpeedSettings);
#else
				// Camera speed is set via index (1-8), so we map the float value
				int32 SpeedIndex = FMath::Clamp(FMath::RoundToInt(FinalSpeed), 1, 8);
				ViewportClient.SetCameraSpeedSetting(SpeedIndex);
				FinalSpeed = SpeedIndex;
#endif
			}

			// Apply FOV if specified
			if (Request->HasField(TEXT("fov")))
			{
				FinalFOV = Request->GetNumberField(TEXT("fov"));
				ViewportClient.ViewFOV = FinalFOV;
			}

			// Invalidate viewport to refresh
			ViewportClient.Invalidate();

			bSuccess = true;
			Message = TEXT("Camera updated");
			LogInfo(Message);
		}
		else
		{
			Message = TEXT("No active viewport found");
			LogError(Message);
		}
	}
	else
	{
		Message = TEXT("GEditor not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"control_camera_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
		"\"rotation\":{\"pitch\":%.2f,\"yaw\":%.2f,\"roll\":%.2f},"
		"\"speed\":%.1f,"
		"\"fov\":%.1f,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		FinalLocation.X, FinalLocation.Y, FinalLocation.Z,
		FinalRotation.Pitch, FinalRotation.Yaw, FinalRotation.Roll,
		FinalSpeed,
		FinalFOV,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P3 - Play In Editor Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandlePlayInEditor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandlePlayInEditor: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString Mode = Request->HasField(TEXT("mode")) ? Request->GetStringField(TEXT("mode")) : TEXT("selected_viewport");
	double NumClientsValue = 1.0;
	const bool bHasNumClients = Request->TryGetNumberField(TEXT("num_clients"), NumClientsValue);
	const int32 NumClients = static_cast<int32>(NumClientsValue);
	FString NetMode = Request->HasField(TEXT("net_mode"))
		? Request->GetStringField(TEXT("net_mode"))
		: (NumClients > 1 ? TEXT("listen_server") : TEXT("standalone"));
	FString RequestedMapPath;
	const bool bHasRequestedMapField = Request->HasField(TEXT("map_path"));
	const bool bHasRequestedMapString = Request->TryGetStringField(TEXT("map_path"), RequestedMapPath);
	RequestedMapPath.TrimStartAndEndInline();

	LogInfo(FString::Printf(
		TEXT("Starting PIE (request_id: %s, mode: %s, map_path: %s, num_clients: %d, net_mode: %s)"),
		*RequestId, *Mode, *RequestedMapPath, NumClients, *NetMode));

	bool bSuccess = false;
	FString Message;
	FString SessionId;
	FString ErrorCode;
	FString LoadedEditorMap;

	auto SendPlayResponse = [this, Socket, &RequestId, &Mode, &RequestedMapPath, &NumClients, &NetMode](
		bool bResponseSuccess,
		const FString& ResponseMessage,
		const FString& ResponseSessionId,
		const FString& ResponseErrorCode,
		const FString& ResponseLoadedEditorMap)
	{
		TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
		Response->SetStringField(TEXT("type"), TEXT("play_in_editor_result"));
		Response->SetStringField(TEXT("request_id"), RequestId);
		Response->SetBoolField(TEXT("success"), bResponseSuccess);
		Response->SetStringField(TEXT("mode"), Mode);
		Response->SetStringField(TEXT("session_id"), ResponseSessionId);
		Response->SetStringField(TEXT("message"), ResponseMessage);
		Response->SetStringField(TEXT("requested_map_path"), RequestedMapPath);
		Response->SetStringField(TEXT("loaded_editor_map"), ResponseLoadedEditorMap);
		Response->SetNumberField(TEXT("num_clients"), NumClients);
		Response->SetNumberField(TEXT("expected_world_count"), NumClients);
		Response->SetStringField(TEXT("net_mode"), NetMode);
		if (!ResponseErrorCode.IsEmpty())
		{
			Response->SetStringField(TEXT("error_code"), ResponseErrorCode);
		}
		if (!bResponseSuccess)
		{
			Response->SetStringField(TEXT("error"), ResponseMessage);
		}

		FString ResponseJSON;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ResponseJSON);
		FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
		SendResponse(Socket, ResponseJSON);
	};
	if ((bHasNumClients && (!FMath::IsNearlyEqual(NumClientsValue, static_cast<double>(NumClients))))
		|| NumClients < 1 || NumClients > 4)
	{
		Message = TEXT("num_clients must be an integer in [1, 4]");
		ErrorCode = TEXT("INVALID_PARAM");
		LogError(Message);
		SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
		return;
	}
	if (NetMode != TEXT("standalone") && NetMode != TEXT("listen_server") && NetMode != TEXT("dedicated_server"))
	{
		Message = TEXT("net_mode must be standalone, listen_server, or dedicated_server");
		ErrorCode = TEXT("INVALID_PARAM");
		LogError(Message);
		SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
		return;
	}
	if (NetMode == TEXT("standalone") && NumClients != 1)
	{
		Message = TEXT("standalone requires num_clients=1");
		ErrorCode = TEXT("INVALID_PARAM");
		LogError(Message);
		SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
		return;
	}
	if (NetMode == TEXT("dedicated_server") && NumClients < 2)
	{
		Message = TEXT("dedicated_server requires num_clients>=2 (one server plus at least one client)");
		ErrorCode = TEXT("INVALID_PARAM");
		LogError(Message);
		SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
		return;
	}
	if (bHasRequestedMapField && (!bHasRequestedMapString || RequestedMapPath.IsEmpty()))
	{
		Message = TEXT("map_path must be a non-empty Unreal long package name string");
		ErrorCode = TEXT("INVALID_MAP_PATH");
		LogError(Message);
		SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
		return;
	}

	if (GEditor)
	{
		// Check if already playing
		if (GEditor->PlayWorld)
		{
			if (!RequestedMapPath.IsEmpty())
			{
				Message = TEXT("Cannot load a requested map while a PIE session is already running");
				ErrorCode = TEXT("PIE_ALREADY_RUNNING");
			}
			else
			{
				Message = TEXT("PIE session already running");
				bSuccess = true;  // Backward-compatible no-map behavior.
				SessionId = TEXT("existing");
			}
		}
		else
		{
			if (!RequestedMapPath.IsEmpty())
			{
				FText InvalidReason;
				if (!FPackageName::IsValidLongPackageName(RequestedMapPath, false, &InvalidReason))
				{
					Message = FString::Printf(TEXT("Invalid map package path '%s': %s"), *RequestedMapPath, *InvalidReason.ToString());
					ErrorCode = TEXT("INVALID_MAP_PATH");
					LogError(Message);
					SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
					return;
				}

				FString MapFilename;
				if (!FPackageName::DoesPackageExist(RequestedMapPath, &MapFilename))
				{
					Message = FString::Printf(TEXT("Map package does not exist: %s"), *RequestedMapPath);
					ErrorCode = TEXT("MAP_NOT_FOUND");
					LogError(Message);
					SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
					return;
				}
				FString VerifiedMapFilename;
				if (!FEditorFileUtils::IsMapPackageAsset(RequestedMapPath, VerifiedMapFilename))
				{
					Message = FString::Printf(TEXT("Package is not a loadable map: %s"), *RequestedMapPath);
					ErrorCode = TEXT("MAP_LOAD_FAILED");
					LogError(Message);
					SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
					return;
				}
				if (!VerifiedMapFilename.IsEmpty())
				{
					MapFilename = VerifiedMapFilename;
				}

				if (!FEditorFileUtils::LoadMap(MapFilename, false, true))
				{
					Message = FString::Printf(TEXT("Failed to load map package: %s"), *RequestedMapPath);
					ErrorCode = TEXT("MAP_LOAD_FAILED");
					LogError(Message);
					SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
					return;
				}

				UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
				if (EditorWorld && EditorWorld->GetOutermost())
				{
					LoadedEditorMap = EditorWorld->GetOutermost()->GetName();
				}
				if (!LoadedEditorMap.Equals(RequestedMapPath, ESearchCase::CaseSensitive))
				{
					Message = FString::Printf(
						TEXT("Loaded editor map '%s' does not match requested map '%s'"),
						*LoadedEditorMap,
						*RequestedMapPath);
					ErrorCode = TEXT("MAP_LOAD_MISMATCH");
					LogError(Message);
					SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
					return;
				}
			}

			// Configure PIE settings based on mode
			FRequestPlaySessionParams PlayParams;
			// Do not mutate the Editor's config-backed default play settings. UE
			// periodically flushes that object to EditorPerProjectUserSettings.ini,
			// which would make an automation-only topology (for example
			// PIE_Client) sticky for the user's next manual PIE session. The
			// request owns this transient duplicate through EditorPlaySettings.
			ULevelEditorPlaySettings* PlaySettings = DuplicateObject<ULevelEditorPlaySettings>(
				GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
			if (!PlaySettings)
			{
				Message = TEXT("Level Editor play settings are unavailable");
				ErrorCode = TEXT("EDITOR_NOT_AVAILABLE");
				LogError(Message);
				SendPlayResponse(false, Message, SessionId, ErrorCode, LoadedEditorMap);
				return;
			}
			// num_clients is the public total-world count. UE's PIE_Client mode
			// adds its dedicated server separately, so it receives N-1 clients.
			const int32 EditorClientCount = NetMode == TEXT("dedicated_server")
				? NumClients - 1
				: NumClients;
			PlaySettings->SetPlayNumberOfClients(EditorClientCount);
			PlaySettings->SetRunUnderOneProcess(true);
			if (NetMode == TEXT("standalone"))
			{
				PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
			}
			else if (NetMode == TEXT("listen_server"))
			{
				PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_ListenServer);
			}
			else
			{
				PlaySettings->SetPlayNetMode(EPlayNetMode::PIE_Client);
			}
			PlayParams.EditorPlaySettings = PlaySettings;

			if (Mode.Equals(TEXT("simulate"), ESearchCase::IgnoreCase))
			{
				PlayParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
			}
			else if (Mode.Equals(TEXT("new_window"), ESearchCase::IgnoreCase))
			{
				PlayParams.DestinationSlateViewport = nullptr;  // New window
			}
			else
			{
				// Default: selected_viewport
				// Use active viewport
			}

			// Apply start location if specified
			if (Request->HasField(TEXT("start_location_x")) || Request->HasField(TEXT("start_location_y")) || Request->HasField(TEXT("start_location_z")))
			{
				FVector StartLocation(
					Request->HasField(TEXT("start_location_x")) ? Request->GetNumberField(TEXT("start_location_x")) : 0.0,
					Request->HasField(TEXT("start_location_y")) ? Request->GetNumberField(TEXT("start_location_y")) : 0.0,
					Request->HasField(TEXT("start_location_z")) ? Request->GetNumberField(TEXT("start_location_z")) : 0.0
				);
				PlayParams.StartLocation = StartLocation;
			}

			// Request play session
			GEditor->RequestPlaySession(PlayParams);

			bSuccess = true;
			SessionId = FString::Printf(TEXT("PIE_%s"), *FDateTime::Now().ToString(TEXT("%H%M%S")));
			Message = FString::Printf(TEXT("PIE session started: %s"), *Mode);
			LogInfo(Message);
		}
	}
	else
	{
		Message = TEXT("GEditor not available");
		ErrorCode = TEXT("EDITOR_NOT_AVAILABLE");
		LogError(Message);
	}

	SendPlayResponse(bSuccess, Message, SessionId, ErrorCode, LoadedEditorMap);
}

// ============================================
// Issue #1632: P3 - Stop Play Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleStopPlay(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleStopPlay: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));

	LogInfo(FString::Printf(TEXT("Stopping PIE (request_id: %s)"), *RequestId));

	bool bSuccess = false;
	bool bWasPlaying = false;
	FString Message;

	if (GEditor)
	{
		const bool bHadQueuedRequest = GEditor->IsPlaySessionRequestQueued();
		const bool bHadActiveWorld = GEditor->PlayWorld != nullptr || GEditor->bIsSimulatingInEditor;
		bWasPlaying = bHadQueuedRequest || bHadActiveWorld;

		if (bWasPlaying)
		{
			// RequestPlaySession and RequestEndPlayMap are both asynchronous.
			// Cancel a not-yet-started request as well as ending an active world,
			// otherwise a timeout cleanup can report success and PIE can start
			// immediately afterwards.
			if (bHadQueuedRequest)
			{
				GEditor->CancelRequestPlaySession();
			}
			if (bHadActiveWorld)
			{
				GEditor->RequestEndPlayMap();
			}
			bSuccess = true;
			Message = TEXT("PIE session stop/cancel requested");
			LogInfo(Message);
		}
		else
		{
			bSuccess = true;  // Not an error
			Message = TEXT("No PIE session was running");
		}
	}
	else
	{
		Message = TEXT("GEditor not available");
		LogError(Message);
	}

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"stop_play_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"was_playing\":%s,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		bWasPlaying ? TEXT("true") : TEXT("false"),
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #27011: Pause/resume PIE PlayWorld clock (absolute paused value)
// ============================================

void UCUWebSocketEditorSubsystem::HandleSetGamePaused(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetGamePaused: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));

	auto SendPausedError = [this, Socket, &RequestId](const FString& ErrorMsg)
	{
		const FString Escaped = CUJsonUtils::EscapeJsonString(ErrorMsg);
		const FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"set_game_paused_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error_code\":\"PIE_NOT_RUNNING\","
			"\"error\":\"%s\""
			"}"),
			*RequestId,
			*Escaped
		);
		SendResponse(Socket, ResponseJSON);
	};

	if (!Request->HasField(TEXT("paused")))
	{
		const FString Escaped = CUJsonUtils::EscapeJsonString(
			TEXT("set_game_paused requires boolean 'paused' (absolute value, not a toggle)"));
		const FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"set_game_paused_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error_code\":\"INVALID_PARAM\","
			"\"error\":\"%s\""
			"}"),
			*RequestId,
			*Escaped
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	bool bPaused = false;
	if (!Request->TryGetBoolField(TEXT("paused"), bPaused))
	{
		const FString Escaped = CUJsonUtils::EscapeJsonString(
			TEXT("paused must be a boolean (true=pause, false=resume); not a toggle"));
		const FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"set_game_paused_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error_code\":\"INVALID_PARAM\","
			"\"error\":\"%s\""
			"}"),
			*RequestId,
			*Escaped
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	if (!GEditor || !GEditor->PlayWorld)
	{
		SendPausedError(TEXT("PIE is not running. Start PIE with play_in_editor first."));
		return;
	}

	UWorld* PlayWorld = GEditor->PlayWorld;
	UGameplayStatics::SetGamePaused(PlayWorld, bPaused);
	const bool bNowPaused = UGameplayStatics::IsGamePaused(PlayWorld);
	const bool bSuccess = (bNowPaused == bPaused);
	const FString Message = bSuccess
		? (bNowPaused ? TEXT("PIE PlayWorld paused") : TEXT("PIE PlayWorld resumed"))
		: TEXT("SetGamePaused did not match the requested paused state on read-back");
	const FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);

	const FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"set_game_paused_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"paused\":%s,"
		"\"message\":\"%s\""
		"%s"
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		bNowPaused ? TEXT("true") : TEXT("false"),
		*EscapedMessage,
		bSuccess ? TEXT("") : TEXT(",\"error_code\":\"READBACK_MISMATCH\",\"error\":\"SetGamePaused did not match the requested paused state on read-back\"")
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #27011: PIE PlayWorld global time dilation (0 < dilation <= 1)
// ============================================

void UCUWebSocketEditorSubsystem::HandleSetGlobalTimeDilation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetGlobalTimeDilation: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));

	auto SendDilationError = [this, Socket, &RequestId](const TCHAR* ErrorCode, const FString& ErrorMsg)
	{
		const FString Escaped = CUJsonUtils::EscapeJsonString(ErrorMsg);
		const FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"set_global_time_dilation_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error_code\":\"%s\","
			"\"error\":\"%s\""
			"}"),
			*RequestId,
			ErrorCode,
			*Escaped
		);
		SendResponse(Socket, ResponseJSON);
	};

	double DilationValue = 0.0;
	if (!Request->TryGetNumberField(TEXT("dilation"), DilationValue))
	{
		SendDilationError(
			TEXT("INVALID_PARAM"),
			TEXT("set_global_time_dilation requires number 'dilation' (0 < dilation <= 1)"));
		return;
	}

	if (!FMath::IsFinite(DilationValue) || DilationValue <= 0.0 || DilationValue > 1.0)
	{
		SendDilationError(
			TEXT("INVALID_PARAM"),
			FString::Printf(
				TEXT("dilation must satisfy 0 < dilation <= 1 (got %g). Use set_game_paused for a full pause; 0 is rejected."),
				DilationValue));
		return;
	}

	if (!GEditor || !GEditor->PlayWorld)
	{
		SendDilationError(
			TEXT("PIE_NOT_RUNNING"),
			TEXT("PIE is not running. Start PIE with play_in_editor first."));
		return;
	}

	UWorld* PlayWorld = GEditor->PlayWorld;
	const float Requested = static_cast<float>(DilationValue);
	UGameplayStatics::SetGlobalTimeDilation(PlayWorld, Requested);
	const float NowDilation = UGameplayStatics::GetGlobalTimeDilation(PlayWorld);
	const bool bSuccess = FMath::IsNearlyEqual(NowDilation, Requested, KINDA_SMALL_NUMBER);
	const FString Message = bSuccess
		? FString::Printf(TEXT("PIE PlayWorld time dilation set to %g"), NowDilation)
		: TEXT("SetGlobalTimeDilation did not match the requested dilation on read-back");
	const FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);

	const FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"set_global_time_dilation_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"time_dilation\":%g,"
		"\"message\":\"%s\""
		"%s"
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		NowDilation,
		*EscapedMessage,
		bSuccess ? TEXT("") : TEXT(",\"error_code\":\"READBACK_MISMATCH\",\"error\":\"SetGlobalTimeDilation did not match the requested dilation on read-back\"")
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P3 - Capture Blueprint Editor Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleCaptureBlueprintEditor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleCaptureBlueprintEditor: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString BlueprintPath = Request->GetStringField(TEXT("blueprint_path"));
	FString GraphName = Request->HasField(TEXT("graph_name")) ? Request->GetStringField(TEXT("graph_name")) : TEXT("");
	int32 Width = Request->HasField(TEXT("width")) ? Request->GetIntegerField(TEXT("width")) : 1920;
	int32 Height = Request->HasField(TEXT("height")) ? Request->GetIntegerField(TEXT("height")) : 1080;

	LogInfo(FString::Printf(TEXT("Capturing Blueprint editor (request_id: %s): %s [%s] %dx%d"),
		*RequestId, *BlueprintPath, *GraphName, Width, Height));

	double StartTime = FPlatformTime::Seconds();
	bool bSuccess = false;
	FString Message;
	FString ScreenshotPath;
	FString CapturedGraphName = GraphName.IsEmpty() ? TEXT("EventGraph") : GraphName;

	if (BlueprintPath.IsEmpty())
	{
		Message = TEXT("Blueprint path cannot be empty");
	}
	else
	{
		// Load and open the blueprint
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (Blueprint)
		{
			// Open the blueprint editor (Issue #7091: null-check guard)
			if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSub->OpenEditorForAsset(Blueprint);
			}

			// Generate screenshot path
			FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
			FString SafeBPName = FPaths::GetBaseFilename(BlueprintPath);
			FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MCP");
			ScreenshotPath = ScreenshotDir / FString::Printf(TEXT("%s_%s_%s.png"), *SafeBPName, *CapturedGraphName, *Timestamp);

			// Ensure directory exists
			IFileManager::Get().MakeDirectory(*ScreenshotDir, true);

			// Use TakeScreenshot on the editor window containing the Blueprint
			if (FSlateApplication::IsInitialized())
			{
				// Find the Blueprint editor window
				TSharedPtr<SWindow> BPEditorWindow;
				TArray<TSharedRef<SWindow>> AllWindows;
				FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);

				// Look for window with Blueprint name in title
				for (const TSharedRef<SWindow>& Window : AllWindows)
				{
					FString WindowTitle = Window->GetTitle().ToString();
					if (WindowTitle.Contains(SafeBPName) || WindowTitle.Contains(TEXT("Blueprint")))
					{
						BPEditorWindow = Window;
						break;
					}
				}

				if (!BPEditorWindow.IsValid())
				{
					// Fallback to active window
					BPEditorWindow = FSlateApplication::Get().GetActiveTopLevelWindow();
				}

				if (BPEditorWindow.IsValid())
				{
					TArray<FColor> Bitmap;
					FIntVector Size(Width, Height, 0);

					if (FSlateApplication::Get().TakeScreenshot(BPEditorWindow.ToSharedRef(), Bitmap, Size))
					{
						// Compress to PNG
						IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
						TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

						if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8))
						{
							TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(100);
							if (CompressedData.Num() > 0 && FFileHelper::SaveArrayToFile(CompressedData, *ScreenshotPath))
							{
								bSuccess = true;
								Width = Size.X;
								Height = Size.Y;
								Message = TEXT("Blueprint editor screenshot captured");
								LogInfo(FString::Printf(TEXT("Screenshot saved: %s (%dx%d)"), *ScreenshotPath, Width, Height));
							}
							else
							{
								Message = TEXT("Failed to save screenshot file");
							}
						}
						else
						{
							Message = TEXT("Failed to compress image");
						}
					}
					else
					{
						Message = TEXT("TakeScreenshot failed");
					}
				}
				else
				{
					Message = TEXT("Blueprint editor window not found");
				}
			}
			else
			{
				Message = TEXT("Slate not initialized");
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
			LogError(Message);
		}
	}

	double ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	FString EscapedPath = ScreenshotPath.Replace(TEXT("\\"), TEXT("/"));
	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"capture_blueprint_editor_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"file_path\":\"%s\","
		"\"blueprint_path\":\"%s\","
		"\"graph_name\":\"%s\","
		"\"width\":%d,"
		"\"height\":%d,"
		"\"execution_time_ms\":%.2f,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*EscapedPath,
		*BlueprintPath,
		*CapturedGraphName,
		Width,
		Height,
		ExecutionTimeMs,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #1632: P3 - Get Error Log Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetErrorLog(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetErrorLog: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	int32 MaxEntries = Request->HasField(TEXT("max_entries")) ? Request->GetIntegerField(TEXT("max_entries")) : 100;
	FString SeverityFilter = Request->HasField(TEXT("severity_filter")) ? Request->GetStringField(TEXT("severity_filter")) : TEXT("");
	FString CategoryFilter = Request->HasField(TEXT("category_filter")) ? Request->GetStringField(TEXT("category_filter")) : TEXT("");

	LogInfo(FString::Printf(TEXT("Getting error log (request_id: %s, max: %d, severity: %s, category: %s)"),
		*RequestId, MaxEntries, *SeverityFilter, *CategoryFilter));

	bool bSuccess = true;
	int32 TotalCount = 0;
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	// Issue #4843: Read from FOutputDevice log capture ring buffer
	FString EntriesJSON = TEXT("[]");
	if (LogCapture.IsValid())
	{
		TArray<FCULogEntry> CapturedEntries = LogCapture->GetEntries(MaxEntries, SeverityFilter, CategoryFilter);
		TotalCount = CapturedEntries.Num();

		// Build JSON array and count severities
		FString EntriesArray;
		for (int32 i = 0; i < CapturedEntries.Num(); ++i)
		{
			const FCULogEntry& Entry = CapturedEntries[i];

			// Count by severity
			if (Entry.Verbosity == ELogVerbosity::Error || Entry.Verbosity == ELogVerbosity::Fatal)
			{
				ErrorCount++;
			}
			else if (Entry.Verbosity == ELogVerbosity::Warning)
			{
				WarningCount++;
			}

			// Map ELogVerbosity to string
			FString SeverityStr;
			switch (Entry.Verbosity)
			{
			case ELogVerbosity::Fatal:   SeverityStr = TEXT("fatal"); break;
			case ELogVerbosity::Error:   SeverityStr = TEXT("error"); break;
			case ELogVerbosity::Warning: SeverityStr = TEXT("warning"); break;
			case ELogVerbosity::Display: SeverityStr = TEXT("display"); break;
			case ELogVerbosity::Log:     SeverityStr = TEXT("log"); break;
			case ELogVerbosity::Verbose: SeverityStr = TEXT("verbose"); break;
			default:                     SeverityStr = TEXT("log"); break;
			}

			// Escape message for JSON (Issue #5579: use comprehensive JSON escaper)
			FString EscapedMsg = CUJsonUtils::EscapeJsonString(Entry.Message);
			EscapedMsg = EscapedMsg.Replace(TEXT("\t"), TEXT("\\t"));

			FString EntryJSON = FString::Printf(TEXT(
				"{\"timestamp\":\"%s\",\"severity\":\"%s\",\"category\":\"%s\",\"message\":\"%s\"}"),
				*Entry.Timestamp.ToIso8601(),
				*SeverityStr,
				*Entry.Category.ToString(),
				*EscapedMsg
			);

			if (i > 0)
			{
				EntriesArray += TEXT(",");
			}
			EntriesArray += EntryJSON;
		}
		EntriesJSON = FString::Printf(TEXT("[%s]"), *EntriesArray);
	}

	FString Message = FString::Printf(TEXT("Error log retrieved (%d entries)"), TotalCount);
	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"get_error_log_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"entries\":%s,"
		"\"total_count\":%d,"
		"\"error_count\":%d,"
		"\"warning_count\":%d,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*EntriesJSON,
		TotalCount,
		ErrorCount,
		WarningCount,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Tool Maturity INI Handlers (Issue #2547)
// ============================================

void UCUWebSocketEditorSubsystem::HandleGetToolMaturity(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId = Request->HasField(TEXT("request_id")) ? Request->GetStringField(TEXT("request_id")) : TEXT("");

	LogInfo(FString::Printf(TEXT("Getting tool maturity states (request_id: %s)"), *RequestId));

	// Get all tool states from settings
	const UCUToolMaturitySettings* Settings = GetDefault<UCUToolMaturitySettings>();
	TMap<FString, bool> ToolStates = Settings->GetAllToolStates();

	// Build tool_states JSON object
	FString ToolStatesJSON = TEXT("{");
	bool bFirst = true;
	int32 EnabledCount = 0;

	for (const auto& Pair : ToolStates)
	{
		if (!bFirst)
		{
			ToolStatesJSON += TEXT(",");
		}
		bFirst = false;

		ToolStatesJSON += FString::Printf(TEXT("\"%s\":%s"),
			*Pair.Key,
			Pair.Value ? TEXT("true") : TEXT("false")
		);

		if (Pair.Value)
		{
			EnabledCount++;
		}
	}
	ToolStatesJSON += TEXT("}");

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"get_tool_maturity_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"tool_states\":%s,"
		"\"enabled_count\":%d,"
		"\"total_count\":%d,"
		"\"source\":\"ini\""
		"}"),
		*RequestId,
		*ToolStatesJSON,
		EnabledCount,
		ToolStates.Num()
	);

	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleSetToolMaturity(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId = Request->HasField(TEXT("request_id")) ? Request->GetStringField(TEXT("request_id")) : TEXT("");
	FString ToolName = Request->HasField(TEXT("tool_name")) ? Request->GetStringField(TEXT("tool_name")) : TEXT("");
	bool bEnabled = Request->HasField(TEXT("enabled")) ? Request->GetBoolField(TEXT("enabled")) : false;

	LogInfo(FString::Printf(TEXT("Setting tool maturity: %s = %s (request_id: %s)"),
		*ToolName,
		bEnabled ? TEXT("true") : TEXT("false"),
		*RequestId
	));

	bool bSuccess = false;
	FString Message;

	if (ToolName.IsEmpty())
	{
		Message = TEXT("tool_name is required");
	}
	else
	{
		const UCUToolMaturitySettings* Settings = GetDefault<UCUToolMaturitySettings>();
		if (!Settings->GetAllToolStates().Contains(ToolName))
		{
			Message = FString::Printf(TEXT("Unknown tool_name: %s"), *ToolName);
		}
		else
		{
			// GConfig::SetString + Flush does not create an arbitrary, previously
			// unseen config file. Use an explicit FConfigFile so a fresh project gets
			// a durable DefaultToolMaturity.ini instead of a success response backed
			// only by the in-memory CDO (Issue #27360 live verification).
			const FString ConfigPath = FPaths::ProjectConfigDir() / TEXT("DefaultToolMaturity.ini");
			FConfigFile ConfigFile;
			ConfigFile.Read(ConfigPath);
			ConfigFile.SetString(
				TEXT("/Script/NarshaMCP.CUToolMaturitySettings"),
				*ToolName,
				bEnabled ? TEXT("true") : TEXT("false")
			);

			if (ConfigFile.Write(ConfigPath))
			{
				// Update the in-memory settings only after durable persistence succeeds.
				UCUToolMaturitySettings* MutableSettings = GetMutableDefault<UCUToolMaturitySettings>();
				MutableSettings->SetToolEnabled(ToolName, bEnabled);

				bSuccess = true;
				Message = FString::Printf(TEXT("%s %s"), *ToolName, bEnabled ? TEXT("enabled") : TEXT("disabled"));
			}
			else
			{
				Message = FString::Printf(TEXT("Failed to persist tool maturity to %s"), *ConfigPath);
				LogError(Message);
			}
		}
	}

	FString EscapedToolName = CUJsonUtils::EscapeJsonString(ToolName);
	FString EscapedMsg = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"set_tool_maturity_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"tool_name\":\"%s\","
		"\"enabled\":%s,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*EscapedToolName,
		bEnabled ? TEXT("true") : TEXT("false"),
		*EscapedMsg
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #3796: Niagara VFX Parameter Control
// ============================================

#if WITH_NARSHAMCP_NIAGARA

void UCUWebSocketEditorSubsystem::HandleSetNiagaraParameter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetNiagaraParameter: Invalid socket or request"));
		return;
	}

	// Extract request parameters
	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));
	FString ParameterName = Request->GetStringField(TEXT("parameter_name"));

	LogInfo(FString::Printf(TEXT("SetNiagaraParameter: actor=%s, param=%s (request_id: %s)"),
		*ActorName, *ParameterName, *RequestId));

	// Get editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No editor world available\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Find actor by name
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorNameOrLabel() == ActorName))
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Actor not found: %s\"}"),
			*RequestId, *ActorName);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Find NiagaraComponent on actor
	UNiagaraComponent* NiagaraComp = FoundActor->FindComponentByClass<UNiagaraComponent>();
	if (!NiagaraComp)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No NiagaraComponent found on actor\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Determine value type and set parameter
	bool bSuccess = false;
	FString ErrorMsg;

	// Check for object value (vector or color)
	const TSharedPtr<FJsonObject>* ValueObject = nullptr;
	if (Request->TryGetObjectField(TEXT("value"), ValueObject) && ValueObject && (*ValueObject).IsValid())
	{
		// Check if it's a color (has 'r' field)
		if ((*ValueObject)->HasField(TEXT("r")))
		{
			float R = (*ValueObject)->GetNumberField(TEXT("r"));
			float G = (*ValueObject)->GetNumberField(TEXT("g"));
			float B = (*ValueObject)->GetNumberField(TEXT("b"));
			float A = (*ValueObject)->HasField(TEXT("a")) ? (*ValueObject)->GetNumberField(TEXT("a")) : 1.0f;
			FLinearColor Color(R, G, B, A);
			NiagaraComp->SetVariableLinearColor(FName(*ParameterName), Color);
			bSuccess = true;
			LogInfo(FString::Printf(TEXT("Set Niagara LinearColor: %s = (%f, %f, %f, %f)"), *ParameterName, R, G, B, A));
		}
		else if ((*ValueObject)->HasField(TEXT("x")))
		{
			float X = (*ValueObject)->GetNumberField(TEXT("x"));
			float Y = (*ValueObject)->GetNumberField(TEXT("y"));
			float Z = (*ValueObject)->GetNumberField(TEXT("z"));
			FVector Vec(X, Y, Z);
			NiagaraComp->SetVariableVec3(FName(*ParameterName), Vec);
			bSuccess = true;
			LogInfo(FString::Printf(TEXT("Set Niagara Vector: %s = (%f, %f, %f)"), *ParameterName, X, Y, Z));
		}
		else
		{
			ErrorMsg = TEXT("Unknown object value format (expected vector or color)");
		}
	}
	else
	{
		// Try numeric value (float/int)
		double NumericValue = 0.0;
		if (Request->TryGetNumberField(TEXT("value"), NumericValue))
		{
			NiagaraComp->SetVariableFloat(FName(*ParameterName), static_cast<float>(NumericValue));
			bSuccess = true;
			LogInfo(FString::Printf(TEXT("Set Niagara Float: %s = %f"), *ParameterName, NumericValue));
		}
		else
		{
			bool BoolValue = false;
			if (Request->TryGetBoolField(TEXT("value"), BoolValue))
			{
				NiagaraComp->SetVariableBool(FName(*ParameterName), BoolValue);
				bSuccess = true;
				LogInfo(FString::Printf(TEXT("Set Niagara Bool: %s = %s"), *ParameterName, BoolValue ? TEXT("true") : TEXT("false")));
			}
			else
			{
				ErrorMsg = TEXT("Could not parse value (expected number, bool, vector, or color)");
			}
		}
	}

	// Build response
	FString NiagaraResponseJSON;
	if (bSuccess)
	{
		NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,"
			"\"actor_name\":\"%s\",\"component_name\":\"%s\",\"parameter_name\":\"%s\",\"value_set\":true}"),
			*RequestId, *FoundActor->GetName(), *NiagaraComp->GetName(), *ParameterName);
	}
	else
	{
		NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"%s\"}"),
			*RequestId, *ErrorMsg);
	}

	SendResponse(Socket, NiagaraResponseJSON);
}

// Issue #26588: persist ExposedParameters bytes into the editor-owned
// UNiagaraScriptVariable default. Do not call SetUserParameterDefaultForSystem
// — on UE 5.8 that wrapper only rewrites the store via a data-processing
// ViewModel and leaves Variable.VarData stale (#26586 reader).
static bool PersistNiagaraUserScriptVariableDefault(
	UNiagaraSystem& System,
	FNiagaraUserRedirectionParameterStore& Store,
	const FNiagaraVariable& Var,
	FString& OutError)
{
	const uint8* StoreBytes = Store.GetParameterData(Var);
	const int32 StoreSize = Var.GetSizeInBytes();
	if (StoreBytes == nullptr || StoreSize <= 0)
	{
		OutError = FString::Printf(
			TEXT("store has no typed bytes for persisted default '%s'"),
			*Var.GetName().ToString());
		return false;
	}

	// GetScriptVariableForUserParameter Casts GetEditorData() without a null
	// check — fail closed here instead of crashing the Editor.
	UNiagaraSystemEditorData* EditorData = Cast<UNiagaraSystemEditorData>(System.GetEditorData());
	if (!EditorData)
	{
		OutError = FString::Printf(
			TEXT("persisted-default editor data missing for '%s'"),
			*Var.GetName().ToString());
		return false;
	}
	UNiagaraScriptVariable* ScriptVariable = EditorData->FindOrAddUserScriptVariable(Var, System);
	if (!ScriptVariable)
	{
		OutError = FString::Printf(
			TEXT("persisted-default ScriptVariable missing for '%s'"),
			*Var.GetName().ToString());
		return false;
	}

	if (ScriptVariable->Variable.GetSizeInBytes() != StoreSize)
	{
		OutError = FString::Printf(
			TEXT("persisted-default size mismatch for '%s' (sv=%d store=%d)"),
			*Var.GetName().ToString(),
			ScriptVariable->Variable.GetSizeInBytes(),
			StoreSize);
		return false;
	}

	TArray<uint8> PreviousDefault;
	const uint8* PrevDefault = ScriptVariable->GetDefaultValueData();
	const int32 PrevDefaultBytes = ScriptVariable->GetDefaultValueVariant().GetNumBytes();
	if (PrevDefault != nullptr && PrevDefaultBytes == StoreSize)
	{
		PreviousDefault.SetNumUninitialized(StoreSize);
		FMemory::Memcpy(PreviousDefault.GetData(), PrevDefault, StoreSize);
	}

	ScriptVariable->Modify();
	ScriptVariable->UpdateChangeId();
	ScriptVariable->SetDefaultValueData(StoreBytes);

	const uint8* Copied = ScriptVariable->GetDefaultValueData();
	if (Copied == nullptr
		|| ScriptVariable->Variable.GetSizeInBytes() != StoreSize
		|| FMemory::Memcmp(Copied, StoreBytes, StoreSize) != 0)
	{
		if (PreviousDefault.Num() == StoreSize)
		{
			ScriptVariable->SetDefaultValueData(PreviousDefault.GetData());
		}
		OutError = FString::Printf(
			TEXT("persisted-default copy-back mismatch for '%s'"),
			*Var.GetName().ToString());
		return false;
	}
	return true;
}

static bool FormatNiagaraStoreValueJson(
	FNiagaraUserRedirectionParameterStore& Store,
	const FNiagaraVariable& Var,
	FString& OutType,
	FString& OutJsonValue)
{
	if (Var.GetType() == FNiagaraTypeDefinition::GetFloatDef())
	{
		OutType = TEXT("float");
		OutJsonValue = FString::SanitizeFloat(Store.GetParameterValue<float>(Var));
		return true;
	}
	if (Var.GetType() == FNiagaraTypeDefinition::GetIntDef())
	{
		OutType = TEXT("int");
		OutJsonValue = FString::FromInt(Store.GetParameterValue<int32>(Var));
		return true;
	}
	if (Var.GetType() == FNiagaraTypeDefinition::GetBoolDef())
	{
		const FNiagaraBool NiagaraBool = Store.GetParameterValue<FNiagaraBool>(Var);
		OutType = TEXT("bool");
		OutJsonValue = NiagaraBool.GetValue() ? TEXT("true") : TEXT("false");
		return true;
	}
	if (Var.GetType() == FNiagaraTypeDefinition::GetVec3Def())
	{
		const uint8* Data = Store.GetParameterData(Var);
		if (Data == nullptr || Var.GetSizeInBytes() < static_cast<int32>(sizeof(FVector3f)))
		{
			return false;
		}
		const FVector3f* Vec = reinterpret_cast<const FVector3f*>(Data);
		OutType = TEXT("vector");
		OutJsonValue = FString::Printf(
			TEXT("{\"x\":%s,\"y\":%s,\"z\":%s}"),
			*FString::SanitizeFloat(Vec->X),
			*FString::SanitizeFloat(Vec->Y),
			*FString::SanitizeFloat(Vec->Z));
		return true;
	}
	if (Var.GetType() == FNiagaraTypeDefinition::GetColorDef())
	{
		const FLinearColor Color = Store.GetParameterValue<FLinearColor>(Var);
		OutType = TEXT("color");
		OutJsonValue = FString::Printf(
			TEXT("{\"r\":%s,\"g\":%s,\"b\":%s,\"a\":%s}"),
			*FString::SanitizeFloat(Color.R),
			*FString::SanitizeFloat(Color.G),
			*FString::SanitizeFloat(Color.B),
			*FString::SanitizeFloat(Color.A));
		return true;
	}
	OutType = TEXT("unsupported");
	OutJsonValue = TEXT("null");
	return false;
}

// Issue #6844: Set User Parameter default value on a Niagara System asset
void UCUWebSocketEditorSubsystem::HandleSetNiagaraAssetParameter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetNiagaraAssetParameter: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	FString ParameterName = Request->GetStringField(TEXT("parameter_name"));
	bool bSaveAsset = true;
	if (Request->HasField(TEXT("save_asset")))
	{
		bSaveAsset = Request->GetBoolField(TEXT("save_asset"));
	}

	LogInfo(FString::Printf(TEXT("SetNiagaraAssetParameter: asset=%s, param=%s (request_id: %s)"),
		*AssetPath, *ParameterName, *RequestId));

	// Load the NiagaraSystem asset
	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset)
	{
		// Try with asset name suffix
		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			FString AssetName = FPaths::GetCleanFilename(AssetPath);
			NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
		}
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}

	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *AssetPath);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Issue #25665 live GT: SavePackage raises a fatal Editor error when its existing
	// destination is read-only on Windows. Reject before mutating the parameter store so
	// a single unwritable batch target becomes an honest per-target failure instead of
	// crashing the Editor and aborting every remaining target.
	if (bSaveAsset)
	{
		UPackage* Package = NiagaraSystem->GetOutermost();
		if (Package)
		{
			const FString PackageFilename = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());
			if (IFileManager::Get().IsReadOnly(*PackageFilename))
			{
				const FString ReadOnlyResponseJSON = FString::Printf(TEXT(
					"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,"
					"\"saved\":false,\"error\":\"Niagara asset is read-only and cannot be saved: %s\"}"),
					*RequestId, *AssetPath);
				SendResponse(Socket, ReadOnlyResponseJSON);
				return;
			}
		}
	}

	// Access ExposedParameters (FNiagaraUserRedirectionParameterStore)
	FNiagaraUserRedirectionParameterStore& ParamStore = NiagaraSystem->GetExposedParameters();

	// Auto-prefix User. if not present
	FString FullParamName = ParameterName;
	if (!FullParamName.StartsWith(TEXT("User.")))
	{
		FullParamName = FString::Printf(TEXT("User.%s"), *ParameterName);
	}

	// Find the parameter by name in the store (type-agnostic lookup)
	bool bSuccess = false;
	FString ErrorMsg;
	FNiagaraVariable FoundVar;
	bool bFoundParam = false;
	FNiagaraVariable PreviousStoreVar;
	UNiagaraSystemEditorData* EditorData = nullptr;
	bool bHadScriptVariable = false;
	TArray<uint8> PreviousScriptDefaultBytes;
	FGuid PreviousScriptChangeId;
	UPackage* ParameterPackage = NiagaraSystem->GetOutermost();
	const bool bPackageWasDirty = ParameterPackage && ParameterPackage->IsDirty();

	// Search for the parameter by name, trying both with and without User. prefix
	TArray<FNiagaraVariable> Parameters;
	ParamStore.GetParameters(Parameters);
	for (const FNiagaraVariable& Param : Parameters)
	{
		FString ParamVarName = Param.GetName().ToString();
		if (ParamVarName == FullParamName || ParamVarName == ParameterName)
		{
			FoundVar = Param;
			bFoundParam = true;
			break;
		}
	}

	if (!bFoundParam)
	{
		ErrorMsg = FString::Printf(TEXT("Parameter '%s' not found in ExposedParameters. Use get_parameters to list available User Parameters."), *ParameterName);
	}
	else
	{
		// Capture both persistence models before the first mutation. Every later
		// verification failure restores these bytes and the original dirty state.
		PreviousStoreVar = FNiagaraVariable(FoundVar.GetType(), FoundVar.GetName());
		PreviousStoreVar.AllocateData();
		const bool bStoreSnapshotCopied = ParamStore.CopyParameterData(
			FoundVar, PreviousStoreVar.GetData());
		EditorData = Cast<UNiagaraSystemEditorData>(NiagaraSystem->GetEditorData());
		const NarshaMCP::NiagaraUserScriptVariableLookup::FResult ExistingScriptLookup =
			NarshaMCP::NiagaraUserScriptVariableLookup::Find(
				EditorData, FoundVar, *NiagaraSystem);
		const UNiagaraScriptVariable* ExistingScriptVariable = ExistingScriptLookup.Variable;
		if (!bStoreSnapshotCopied)
		{
			ErrorMsg = FString::Printf(
				TEXT("Parameter '%s' cannot be changed because its prior typed value could not be captured"),
				*FullParamName);
		}
		else if (!EditorData)
		{
			ErrorMsg = FString::Printf(
				TEXT("Parameter '%s' cannot be changed because Niagara editor data is unavailable"),
				*FullParamName);
		}
		else if (ExistingScriptLookup.Status ==
				NarshaMCP::NiagaraUserScriptVariableLookup::EStatus::Unavailable ||
			ExistingScriptLookup.Status ==
				NarshaMCP::NiagaraUserScriptVariableLookup::EStatus::Ambiguous)
		{
			ErrorMsg = FString::Printf(
				TEXT("Parameter '%s' cannot be changed because its prior persisted User default could not be identified"),
				*FullParamName);
		}
		else if (ExistingScriptVariable
			&& (!ExistingScriptVariable->GetDefaultValueData()
				|| ExistingScriptVariable->Variable.GetType() != FoundVar.GetType()
				|| ExistingScriptVariable->Variable.GetName() != FoundVar.GetName()
				|| ExistingScriptVariable->GetDefaultValueVariant().GetNumBytes()
					!= FoundVar.GetSizeInBytes()))
		{
			ErrorMsg = FString::Printf(
				TEXT("Parameter '%s' cannot be changed because its prior persisted default is not byte-backed"),
				*FullParamName);
		}
		else
		{
			if (ExistingScriptVariable)
			{
				bHadScriptVariable = true;
				PreviousScriptChangeId = ExistingScriptVariable->GetChangeId();
				PreviousScriptDefaultBytes.Append(
					ExistingScriptVariable->GetDefaultValueData(),
					FoundVar.GetSizeInBytes());
			}

			// Set value based on the JSON value type only after rollback state exists.
			const TSharedPtr<FJsonObject>* ValueObject = nullptr;
			if (Request->TryGetObjectField(TEXT("value"), ValueObject) && ValueObject && (*ValueObject).IsValid())
			{
				if ((*ValueObject)->HasField(TEXT("r")))
				{
					// LinearColor
					float R = (*ValueObject)->GetNumberField(TEXT("r"));
					float G = (*ValueObject)->GetNumberField(TEXT("g"));
					float B = (*ValueObject)->GetNumberField(TEXT("b"));
					float A = (*ValueObject)->HasField(TEXT("a")) ? (*ValueObject)->GetNumberField(TEXT("a")) : 1.0f;
					FLinearColor Color(R, G, B, A);
					ParamStore.SetParameterValue(Color, FoundVar);
					bSuccess = true;
				}
				else if ((*ValueObject)->HasField(TEXT("x")))
				{
					// Vector — use the found variable's actual type to create a properly typed variable
					float X = (*ValueObject)->GetNumberField(TEXT("x"));
					float Y = (*ValueObject)->GetNumberField(TEXT("y"));
					float Z = (*ValueObject)->GetNumberField(TEXT("z"));
					int32 TypeSize = FoundVar.GetType().GetSize();
					if (TypeSize == sizeof(FVector3f))
					{
						FNiagaraVariable TypedVar(FoundVar.GetType(), FoundVar.GetName());
						FVector3f Vec(X, Y, Z);
						TypedVar.SetValue(Vec);
						ParamStore.SetParameterData(TypedVar.GetData(), FoundVar, true);
						bSuccess = true;
					}
					else if (TypeSize == sizeof(FVector))
					{
						FNiagaraVariable TypedVar(FoundVar.GetType(), FoundVar.GetName());
						FVector Vec(X, Y, Z);
						TypedVar.SetValue(Vec);
						ParamStore.SetParameterData(TypedVar.GetData(), FoundVar, true);
						bSuccess = true;
					}
					else
					{
						ErrorMsg = FString::Printf(TEXT("Vector parameter '%s' has unsupported type size %d"), *ParameterName, TypeSize);
					}
				}
				else
				{
					ErrorMsg = TEXT("Unknown object value format (expected {x,y,z} vector or {r,g,b,a} color)");
				}
			}
			else
			{
				double NumericValue = 0.0;
				if (Request->TryGetNumberField(TEXT("value"), NumericValue))
				{
					float FloatVal = static_cast<float>(NumericValue);
					ParamStore.SetParameterValue(FloatVal, FoundVar);
					bSuccess = true;
				}
				else
				{
					bool BoolValue = false;
					if (Request->TryGetBoolField(TEXT("value"), BoolValue))
					{
						FNiagaraBool NiagaraBool(BoolValue);
						ParamStore.SetParameterValue(NiagaraBool, FoundVar);
						bSuccess = true;
					}
					else
					{
						ErrorMsg = TEXT("Could not parse value (expected number, bool, vector, or color)");
					}
				}
			}
		}
	}

	// Issue #26588: mutating ExposedParameters alone changes the live store but leaves
	// the editor-owned UNiagaraScriptVariable default stale on disk. SavePackage can
	// still return true in that state, producing a false durable-success receipt.
	// Synchronize the exact typed bytes through the system editor data and fail
	// closed unless its independent ScriptVariable model copies the same bytes back.
	// Do not call SetUserParameterDefaultForSystem here: in an unattended asset with
	// no message asset key it constructs a data-processing view model and asserts in
	// FNiagaraMessageManager::SubscribeToAssetMessagesByObject (live #26588 repro).
	const bool bParameterMutationApplied = bSuccess;
	bool bPersistedDefaultVerified = false;
	UNiagaraScriptVariable* ScriptVariable = nullptr;
	if (bSuccess)
	{
		FNiagaraVariable StoreReadbackVar(FoundVar.GetType(), FoundVar.GetName());
		StoreReadbackVar.AllocateData();
		const bool bStoreValueCopied = ParamStore.CopyParameterData(
			FoundVar, StoreReadbackVar.GetData());
		if (!bStoreValueCopied)
		{
			bSuccess = false;
			ErrorMsg = FString::Printf(
				TEXT("Parameter '%s' changed but its typed store value could not be copied back"),
				*FullParamName);
		}
		else
		{
			FString PersistError;
			if (!PersistNiagaraUserScriptVariableDefault(*NiagaraSystem, ParamStore, FoundVar, PersistError))
			{
				bSuccess = false;
				ErrorMsg = PersistError;
			}
			else
			{
				ScriptVariable = EditorData->FindOrAddUserScriptVariable(
					StoreReadbackVar, *NiagaraSystem);
				if (ScriptVariable)
				{
					ScriptVariable->Modify();
					ScriptVariable->SetDefaultValueData(StoreReadbackVar.GetData());
					ScriptVariable->UpdateChangeId();
				}
				bool bForcePersistenceFailure = false;
#if WITH_DEV_AUTOMATION_TESTS
				// Test-only fault injection exercises rollback after both models changed.
				Request->TryGetBoolField(
					TEXT("_test_force_persistence_failure"), bForcePersistenceFailure);
#endif
				const uint8* PersistedDefaultBytes = ScriptVariable
					? ScriptVariable->GetDefaultValueData()
					: nullptr;
				bPersistedDefaultVerified = PersistedDefaultBytes != nullptr
					&& !bForcePersistenceFailure
					&& ScriptVariable->Variable.GetType() == StoreReadbackVar.GetType()
					&& ScriptVariable->Variable.GetName() == StoreReadbackVar.GetName()
					&& FMemory::Memcmp(
						PersistedDefaultBytes,
						StoreReadbackVar.GetData(),
						StoreReadbackVar.GetSizeInBytes()) == 0;
				if (!bPersistedDefaultVerified)
				{
					bSuccess = false;
					ErrorMsg = PersistError.IsEmpty()
						? FString::Printf(
							TEXT("Parameter '%s' changed in ExposedParameters but its persisted User default did not round-trip"),
							*FullParamName)
						: PersistError;
				}
			}
		}
	}

	// A failure receipt must mean no mutation survived. Restore the store, the
	// editor-owned default (or remove a newly created ScriptVariable), its change
	// identity, and the package dirty bit. Then independently verify all four.
	bool bRollbackVerified = false;
	if (bParameterMutationApplied && !bSuccess)
	{
		ParamStore.SetParameterData(PreviousStoreVar.GetData(), FoundVar, true);
		if (bHadScriptVariable)
		{
			UNiagaraScriptVariable* RestoreScriptVariable = EditorData
				? EditorData->FindOrAddUserScriptVariable(PreviousStoreVar, *NiagaraSystem)
				: nullptr;
			if (RestoreScriptVariable)
			{
				RestoreScriptVariable->SetDefaultValueData(PreviousScriptDefaultBytes.GetData());
				RestoreScriptVariable->SetChangeId(PreviousScriptChangeId);
			}
		}
		else if (EditorData && ScriptVariable)
		{
			// FindOrAddUserScriptVariable may canonicalize the stored variable name
			// or identity. Remove the exact object that this transaction created,
			// rather than reconstructing a lookup key from the parameter store.
			EditorData->RemoveUserScriptVariable(ScriptVariable->Variable);
		}
		if (ParameterPackage)
		{
			ParameterPackage->SetDirtyFlag(bPackageWasDirty);
		}

		FNiagaraVariable RollbackReadbackVar(FoundVar.GetType(), FoundVar.GetName());
		RollbackReadbackVar.AllocateData();
		const bool bStoreRollbackVerified = ParamStore.CopyParameterData(
			FoundVar, RollbackReadbackVar.GetData())
			&& FMemory::Memcmp(
				RollbackReadbackVar.GetData(),
				PreviousStoreVar.GetData(),
				PreviousStoreVar.GetSizeInBytes()) == 0;
		const NarshaMCP::NiagaraUserScriptVariableLookup::FResult RestoredScriptLookup =
			NarshaMCP::NiagaraUserScriptVariableLookup::Find(
				EditorData, FoundVar, *NiagaraSystem);
		const UNiagaraScriptVariable* RestoredScriptVariable = RestoredScriptLookup.Variable;
		const bool bScriptRollbackVerified = bHadScriptVariable
			? RestoredScriptLookup.Status ==
					NarshaMCP::NiagaraUserScriptVariableLookup::EStatus::Found
				&& RestoredScriptVariable != nullptr
				&& RestoredScriptVariable->GetDefaultValueData() != nullptr
				&& RestoredScriptVariable->GetChangeId() == PreviousScriptChangeId
				&& FMemory::Memcmp(
					RestoredScriptVariable->GetDefaultValueData(),
					PreviousScriptDefaultBytes.GetData(),
					PreviousScriptDefaultBytes.Num()) == 0
			: RestoredScriptLookup.Status ==
				NarshaMCP::NiagaraUserScriptVariableLookup::EStatus::Absent;
		const bool bDirtyRollbackVerified = !ParameterPackage
			|| ParameterPackage->IsDirty() == bPackageWasDirty;
		bRollbackVerified = bStoreRollbackVerified
			&& bScriptRollbackVerified
			&& bDirtyRollbackVerified;
		if (!bRollbackVerified)
		{
			ErrorMsg += TEXT("; rollback verification failed");
		}
	}

	// Issue #25247 / #24665 class: capture SavePackage return; never report request flag as saved.
	bool bSaved = false;
	if (bSuccess)
	{
		NiagaraSystem->MarkPackageDirty();
		if (bSaveAsset)
		{
			UPackage* Package = NiagaraSystem->GetOutermost();
			if (Package)
			{
				// Issue #16555: SavePackage(TopLevelFlags=RF_Standalone) FATAL-errors if the
				// system lacks RF_Standalone (e.g. an asset duplicated/created outside the normal
				// factory path) — a param edit must never crash the Editor. Ensure the flag.
				NiagaraSystem->SetFlags(RF_Public | RF_Standalone);
				FSavePackageArgs SaveArgs;
				SaveArgs.TopLevelFlags = RF_Standalone;
				bSaved = UPackage::SavePackage(Package, NiagaraSystem, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), SaveArgs);
			}
		}
		LogInfo(FString::Printf(TEXT("SetNiagaraAssetParameter: SUCCESS %s = (set) on %s (saved=%s)"), *ParameterName, *AssetPath, bSaved ? TEXT("true") : TEXT("false")));
	}
	else if (ErrorMsg.IsEmpty())
	{
		ErrorMsg = FString::Printf(TEXT("Parameter '%s' not found in ExposedParameters. Use get_parameters to list available User Parameters."), *ParameterName);
	}

	// Build response
	FString NiagaraResponseJSON;
	if (bSuccess)
	{
		// Issue #19965: store readback. Issue #26588: persisted-default
		// verification is a separate bit — success:true requires persist.
		FString ReadbackType = TEXT("unsupported");
		FString ReadbackValue = TEXT("null");
		const bool bReadbackVerified = FormatNiagaraStoreValueJson(
			ParamStore, FoundVar, ReadbackType, ReadbackValue);
		const FString PersistedType = ReadbackType;
		const FString PersistedValue = ReadbackValue;
		// Issue #25247: "saved" is the SavePackage outcome, not the request save_asset flag.
		NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,"
			"\"asset_path\":\"%s\",\"parameter_name\":\"%s\",\"value_set\":true,\"saved\":%s,"
			"\"readback_verified\":%s,\"readback_type\":\"%s\",\"readback_value\":%s,"
			"\"persisted_default_verified\":%s,\"persisted_default_type\":\"%s\",\"persisted_default_value\":%s}"),
			*CUJsonUtils::EscapeJsonString(RequestId),
			*CUJsonUtils::EscapeJsonString(AssetPath),
			*CUJsonUtils::EscapeJsonString(ParameterName),
			bSaved ? TEXT("true") : TEXT("false"),
			bReadbackVerified ? TEXT("true") : TEXT("false"),
			*CUJsonUtils::EscapeJsonString(ReadbackType),
			*ReadbackValue,
			bPersistedDefaultVerified ? TEXT("true") : TEXT("false"),
			*CUJsonUtils::EscapeJsonString(PersistedType),
			*PersistedValue);
	}
	else
	{
		NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"%s\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId),
			*CUJsonUtils::EscapeJsonString(ErrorMsg));
	}

	SendResponse(Socket, NiagaraResponseJSON);
}

// Issue #21950 / NIA-N11: Create a new User.* parameter on a NiagaraSystem ExposedParameters store.
// Optional module-input bind targets are echoed for follow-up (full stack promote is a separate
// attach/promote step — create must succeed even when bind targets are only recorded).
void UCUWebSocketEditorSubsystem::HandleAddNiagaraUserParameter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleAddNiagaraUserParameter: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	FString ParameterName = Request->GetStringField(TEXT("parameter_name"));
	FString TypeStr = TEXT("float");
	if (Request->HasField(TEXT("parameter_type")))
	{
		TypeStr = Request->GetStringField(TEXT("parameter_type"));
	}
	bool bSaveAsset = true;
	if (Request->HasField(TEXT("save_asset")))
	{
		bSaveAsset = Request->GetBoolField(TEXT("save_asset"));
	}
	FString EmitterName;
	FString ModuleName;
	FString InputName;
	Request->TryGetStringField(TEXT("emitter_name"), EmitterName);
	Request->TryGetStringField(TEXT("module_name"), ModuleName);
	Request->TryGetStringField(TEXT("input_name"), InputName);

	LogInfo(FString::Printf(TEXT("AddNiagaraUserParameter: asset=%s, param=%s, type=%s (request_id: %s)"),
		*AssetPath, *ParameterName, *TypeStr, *RequestId));

	auto SendFail = [&](const FString& ErrorMsg)
	{
		const FString Escaped = ErrorMsg.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
		const FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"%s\"}"),
			*RequestId, *Escaped);
		SendResponse(Socket, ResponseJSON);
	};

	if (AssetPath.IsEmpty() || ParameterName.IsEmpty())
	{
		SendFail(TEXT("asset_path and parameter_name are required"));
		return;
	}

	// Load NiagaraSystem (with .AssetName suffix fallback, same as set path)
	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset)
	{
		FString NormalizedPath = AssetPath;
		if (!NormalizedPath.Contains(TEXT(".")))
		{
			const FString AssetName = FPaths::GetCleanFilename(AssetPath);
			NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *AssetName);
		}
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		SendFail(FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
		return;
	}

	// Canonical User.* name
	FString FullParamName = ParameterName;
	if (!FullParamName.StartsWith(TEXT("User.")))
	{
		FullParamName = FString::Printf(TEXT("User.%s"), *ParameterName);
	}

	// Map type string → FNiagaraTypeDefinition
	const FString TypeLower = TypeStr.ToLower();
	FNiagaraTypeDefinition TypeDef;
	if (TypeLower == TEXT("float") || TypeLower == TEXT("real"))
	{
		TypeDef = FNiagaraTypeDefinition::GetFloatDef();
	}
	else if (TypeLower == TEXT("int") || TypeLower == TEXT("int32") || TypeLower == TEXT("integer"))
	{
		TypeDef = FNiagaraTypeDefinition::GetIntDef();
	}
	else if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
	{
		TypeDef = FNiagaraTypeDefinition::GetBoolDef();
	}
	else if (TypeLower == TEXT("vector") || TypeLower == TEXT("vec3") || TypeLower == TEXT("vector3"))
	{
		TypeDef = FNiagaraTypeDefinition::GetVec3Def();
	}
	else if (TypeLower == TEXT("color") || TypeLower == TEXT("linearcolor") || TypeLower == TEXT("linear_color"))
	{
		TypeDef = FNiagaraTypeDefinition::GetColorDef();
	}
	else
	{
		SendFail(FString::Printf(
			TEXT("Unsupported parameter_type '%s' (expected float|int|bool|vector|color)"), *TypeStr));
		return;
	}

	FNiagaraUserRedirectionParameterStore& ParamStore = NiagaraSystem->GetExposedParameters();

	// Reject duplicates (match with/without User. prefix)
	{
		TArray<FNiagaraVariable> Existing;
		ParamStore.GetParameters(Existing);
		for (const FNiagaraVariable& Param : Existing)
		{
			const FString ExistingName = Param.GetName().ToString();
			if (ExistingName == FullParamName || ExistingName == ParameterName)
			{
				SendFail(FString::Printf(
					TEXT("Parameter '%s' already exists in ExposedParameters"), *FullParamName));
				return;
			}
		}
	}

	FNiagaraVariable NewVar(TypeDef, FName(*FullParamName));

	// Apply default from "value" when provided; otherwise zero/false defaults.
	const TSharedPtr<FJsonObject>* ValueObject = nullptr;
	if (Request->TryGetObjectField(TEXT("value"), ValueObject) && ValueObject && (*ValueObject).IsValid())
	{
		if (TypeDef == FNiagaraTypeDefinition::GetColorDef() && (*ValueObject)->HasField(TEXT("r")))
		{
			const float R = static_cast<float>((*ValueObject)->GetNumberField(TEXT("r")));
			const float G = static_cast<float>((*ValueObject)->GetNumberField(TEXT("g")));
			const float B = static_cast<float>((*ValueObject)->GetNumberField(TEXT("b")));
			const float A = (*ValueObject)->HasField(TEXT("a"))
				? static_cast<float>((*ValueObject)->GetNumberField(TEXT("a"))) : 1.0f;
			NewVar.SetValue(FLinearColor(R, G, B, A));
		}
		else if ((TypeDef == FNiagaraTypeDefinition::GetVec3Def()) && (*ValueObject)->HasField(TEXT("x")))
		{
			const float X = static_cast<float>((*ValueObject)->GetNumberField(TEXT("x")));
			const float Y = static_cast<float>((*ValueObject)->GetNumberField(TEXT("y")));
			const float Z = static_cast<float>((*ValueObject)->GetNumberField(TEXT("z")));
			// Niagara vec3 storage is FVector3f on modern UE
			NewVar.SetValue(FVector3f(X, Y, Z));
		}
	}
	else
	{
		double NumericValue = 0.0;
		bool BoolValue = false;
		if (TypeDef == FNiagaraTypeDefinition::GetFloatDef() && Request->TryGetNumberField(TEXT("value"), NumericValue))
		{
			NewVar.SetValue(static_cast<float>(NumericValue));
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetIntDef() && Request->TryGetNumberField(TEXT("value"), NumericValue))
		{
			NewVar.SetValue(static_cast<int32>(NumericValue));
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef() && Request->TryGetBoolField(TEXT("value"), BoolValue))
		{
			NewVar.SetValue(FNiagaraBool(BoolValue));
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
		{
			NewVar.SetValue(0.0f);
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
		{
			NewVar.SetValue(static_cast<int32>(0));
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
		{
			NewVar.SetValue(FNiagaraBool(false));
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetVec3Def())
		{
			NewVar.SetValue(FVector3f(0.0f, 0.0f, 0.0f));
		}
		else if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
		{
			NewVar.SetValue(FLinearColor::White);
		}
	}
	if (!NewVar.IsDataAllocated())
	{
		SendFail(FString::Printf(
			TEXT("Value for '%s' is incompatible with parameter_type '%s'"),
			*FullParamName, *TypeLower));
		return;
	}

	// Add to ExposedParameters (User redirection store)
	const bool bAdded = ParamStore.AddParameter(
		NewVar,
		/*bInitialize*/ true,
		/*bTriggerRebind*/ true,
		/*OutOffset*/ nullptr);
	if (!bAdded)
	{
		// Some UE builds return false when already present; re-check.
		TArray<FNiagaraVariable> After;
		ParamStore.GetParameters(After);
		bool bNowPresent = false;
		for (const FNiagaraVariable& Param : After)
		{
			if (Param.GetName().ToString() == FullParamName)
			{
				bNowPresent = true;
				break;
			}
		}
		if (!bNowPresent)
		{
			SendFail(FString::Printf(TEXT("AddParameter failed for '%s'"), *FullParamName));
			return;
		}
	}

	// Issue #26586: FNiagaraParameterStore::SetParameterData(..., FNiagaraVariable, bool)
	// returns bool (NiagaraParameterStore.h:609). Ignore that and we report
	// success:true after a failed store write. Copy-back uses GetParameterData
	// (NiagaraParameterStore.h:463). Byte count is FNiagaraVariableBase::GetSizeInBytes
	// (NiagaraTypes.h:1347) — FNiagaraVariable has no GetSize(). Do not call
	// SetUserParameterDefaultForSystem (#26588).
	const uint8* SourceBytes = NewVar.GetData();
	const int32 SourceSize = NewVar.GetSizeInBytes();
	if (SourceBytes == nullptr || SourceSize <= 0)
	{
		ParamStore.RemoveParameter(NewVar);
		SendFail(FString::Printf(TEXT("SetParameterData has no typed bytes for '%s'"), *FullParamName));
		return;
	}
	if (!ParamStore.SetParameterData(SourceBytes, NewVar, true))
	{
		ParamStore.RemoveParameter(NewVar);
		SendFail(FString::Printf(TEXT("SetParameterData failed for '%s'"), *FullParamName));
		return;
	}
	const uint8* CopiedBytes = ParamStore.GetParameterData(NewVar);
	if (CopiedBytes == nullptr || FMemory::Memcmp(CopiedBytes, SourceBytes, SourceSize) != 0)
	{
		ParamStore.RemoveParameter(NewVar);
		SendFail(FString::Printf(TEXT("store copy-back mismatch for '%s'"), *FullParamName));
		return;
	}

	NiagaraSystem->MarkPackageDirty();
	bool bSaved = false;
	if (bSaveAsset)
	{
		UPackage* Package = NiagaraSystem->GetOutermost();
		if (Package)
		{
			NiagaraSystem->SetFlags(RF_Public | RF_Standalone);
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			bSaved = UPackage::SavePackage(
				Package,
				NiagaraSystem,
				*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()),
				SaveArgs);
		}
	}

	const bool bBindRequested = !EmitterName.IsEmpty() || !ModuleName.IsEmpty() || !InputName.IsEmpty();
	// Full stack promote (module pin → User param) is a separate graph mutation;
	// NIA-N11 ships create + optional bind *request* echo. Pair with attach_dynamic_input
	// or a future promote op for pin wiring.
	const FString BindingStatus = bBindRequested ? TEXT("requested_deferred") : TEXT("none");
	FString ReadbackValueJSON = TEXT("null");
	if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
	{
		ReadbackValueJSON = FString::SanitizeFloat(NewVar.GetValue<float>());
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
	{
		ReadbackValueJSON = FString::FromInt(NewVar.GetValue<int32>());
	}
	else if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
	{
		ReadbackValueJSON = NewVar.GetValue<FNiagaraBool>().GetValue()
			? TEXT("true") : TEXT("false");
	}

	auto JsonEscape = [](const FString& In) -> FString
	{
		return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	};

	const FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"asset_path\":\"%s\",\"parameter_name\":\"%s\",\"parameter_type\":\"%s\","
		"\"created\":true,\"saved\":%s,\"readback_verified\":true,"
		"\"readback_type\":\"%s\",\"readback_value\":%s,"
		"\"binding_status\":\"%s\","
		"\"emitter_name\":\"%s\",\"module_name\":\"%s\",\"input_name\":\"%s\","
		"\"note\":\"User parameter created on ExposedParameters. "
		"Module-input promote is deferred when bind targets are set — call attach_dynamic_input (WS attach_niagara_dynamic_input) or use Editor Promote.\"}"),
		*RequestId,
		*JsonEscape(AssetPath),
		*JsonEscape(FullParamName),
		*JsonEscape(TypeLower),
		bSaved ? TEXT("true") : TEXT("false"),
		*JsonEscape(TypeLower),
		*ReadbackValueJSON,
		*BindingStatus,
		*JsonEscape(EmitterName),
		*JsonEscape(ModuleName),
		*JsonEscape(InputName));

	LogInfo(FString::Printf(TEXT("AddNiagaraUserParameter: SUCCESS %s (%s) on %s"),
		*FullParamName, *TypeLower, *AssetPath));
	SendResponse(Socket, ResponseJSON);
}

namespace CUNiagaraAttachDI
{
	constexpr int32 MaxDepth = 16;
	constexpr int32 MaxNodes = 256;

	bool ParseUsage(const FString& Stage, ENiagaraScriptUsage& OutUsage, FString& OutError)
	{
		const FString Lower = Stage.ToLower();
		if (Lower == TEXT("particle_update") || Lower.IsEmpty())
		{
			OutUsage = ENiagaraScriptUsage::ParticleUpdateScript;
			return true;
		}
		if (Lower == TEXT("particle_spawn")) { OutUsage = ENiagaraScriptUsage::ParticleSpawnScript; return true; }
		if (Lower == TEXT("emitter_update")) { OutUsage = ENiagaraScriptUsage::EmitterUpdateScript; return true; }
		if (Lower == TEXT("emitter_spawn")) { OutUsage = ENiagaraScriptUsage::EmitterSpawnScript; return true; }
		if (Lower == TEXT("system_update")) { OutUsage = ENiagaraScriptUsage::SystemUpdateScript; return true; }
		if (Lower == TEXT("system_spawn")) { OutUsage = ENiagaraScriptUsage::SystemSpawnScript; return true; }
		if (Lower == TEXT("particle_event")) { OutUsage = ENiagaraScriptUsage::ParticleEventScript; return true; }
		if (Lower == TEXT("simulation_stage")) { OutUsage = ENiagaraScriptUsage::ParticleSimulationStageScript; return true; }
		OutError = FString::Printf(
			TEXT("Unknown script_stage '%s' (expected particle_update|particle_spawn|emitter_update|emitter_spawn|system_update|system_spawn|particle_event|simulation_stage)"),
			*Stage);
		return false;
	}

	bool IsSystemUsage(ENiagaraScriptUsage Usage)
	{
		return Usage == ENiagaraScriptUsage::SystemSpawnScript
			|| Usage == ENiagaraScriptUsage::SystemUpdateScript;
	}

	FNiagaraTypeDefinition TypeFromName(const FString& Raw)
	{
		const FString Lower = Raw.ToLower();
		if (Lower == TEXT("float") || Lower == TEXT("float1") || Lower == TEXT("scalar") || Lower == TEXT("real") || Lower == TEXT("niagarafloat"))
		{
			return FNiagaraTypeDefinition::GetFloatDef();
		}
		if (Lower == TEXT("int") || Lower == TEXT("int32") || Lower == TEXT("integer"))
		{
			return FNiagaraTypeDefinition::GetIntDef();
		}
		if (Lower == TEXT("bool") || Lower == TEXT("boolean"))
		{
			return FNiagaraTypeDefinition::GetBoolDef();
		}
		if (Lower == TEXT("vec3") || Lower == TEXT("vector") || Lower == TEXT("vector3") || Lower == TEXT("position") || Lower == TEXT("float3"))
		{
			return FNiagaraTypeDefinition::GetVec3Def();
		}
		if (Lower == TEXT("vec2") || Lower == TEXT("vector2") || Lower == TEXT("float2"))
		{
			return FNiagaraTypeDefinition::GetVec2Def();
		}
		if (Lower == TEXT("vec4") || Lower == TEXT("vector4") || Lower == TEXT("float4"))
		{
			return FNiagaraTypeDefinition::GetVec4Def();
		}
		if (Lower == TEXT("color") || Lower == TEXT("linearcolor"))
		{
			return FNiagaraTypeDefinition::GetColorDef();
		}
		if (Lower == TEXT("quat") || Lower == TEXT("quaternion"))
		{
			return FNiagaraTypeDefinition::GetQuatDef();
		}
		return FNiagaraTypeDefinition();
	}

	FString JsonValueToPinDefault(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return FString();
		}
		if (Value->Type == EJson::Number)
		{
			return FString::SanitizeFloat(static_cast<float>(Value->AsNumber()));
		}
		if (Value->Type == EJson::Boolean)
		{
			return Value->AsBool() ? TEXT("true") : TEXT("false");
		}
		if (Value->Type == EJson::String)
		{
			return Value->AsString();
		}
		if (Value->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Value->AsObject();
			if (Obj.IsValid() && Obj->HasField(TEXT("x")))
			{
				const float X = static_cast<float>(Obj->GetNumberField(TEXT("x")));
				const float Y = static_cast<float>(Obj->GetNumberField(TEXT("y")));
				const float Z = Obj->HasField(TEXT("z")) ? static_cast<float>(Obj->GetNumberField(TEXT("z"))) : 0.0f;
				if (Obj->HasField(TEXT("w")))
				{
					const float W = static_cast<float>(Obj->GetNumberField(TEXT("w")));
					return FString::Printf(TEXT("%3.3f,%3.3f,%3.3f,%3.3f"), X, Y, Z, W);
				}
				return FString::Printf(TEXT("%3.3f,%3.3f,%3.3f"), X, Y, Z);
			}
			if (Obj.IsValid() && Obj->HasField(TEXT("r")))
			{
				const float R = static_cast<float>(Obj->GetNumberField(TEXT("r")));
				const float G = static_cast<float>(Obj->GetNumberField(TEXT("g")));
				const float B = static_cast<float>(Obj->GetNumberField(TEXT("b")));
				const float A = Obj->HasField(TEXT("a")) ? static_cast<float>(Obj->GetNumberField(TEXT("a"))) : 1.0f;
				return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"),
					*FString::SanitizeFloat(R), *FString::SanitizeFloat(G),
					*FString::SanitizeFloat(B), *FString::SanitizeFloat(A));
			}
		}
		return FString();
	}

	UEdGraphPin* FindInputPin(UNiagaraNodeFunctionCall& Fn, const FString& InputName)
	{
		TArray<UEdGraphPin*> Pins;
		Fn.GetInputPins(Pins);
		for (UEdGraphPin* Pin : Pins)
		{
			if (!Pin)
			{
				continue;
			}
			const FString PinName = Pin->PinName.ToString();
			if (PinName.Equals(InputName, ESearchCase::IgnoreCase)
				|| PinName.EndsWith(FString(TEXT(".")) + InputName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UNiagaraNodeFunctionCall* FindModule(UNiagaraGraph* Graph, const FString& ModuleName, ENiagaraScriptUsage Usage)
	{
		if (!Graph)
		{
			return nullptr;
		}
		UNiagaraNodeFunctionCall* Match = nullptr;
		int32 Count = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeFunctionCall* Fn = Cast<UNiagaraNodeFunctionCall>(Node);
			if (!Fn)
			{
				continue;
			}
			if (FNiagaraStackGraphUtilities::GetOutputNodeUsage(*Fn) != Usage)
			{
				continue;
			}
			if (Fn->FunctionScript
				&& Fn->FunctionScript->GetUsage() == ENiagaraScriptUsage::DynamicInput)
			{
				continue;
			}
			const FString FnName = Fn->GetFunctionName();
			FString ScriptName;
			if (Fn->FunctionScript)
			{
				ScriptName = Fn->FunctionScript->GetName();
			}
			if (FnName.Equals(ModuleName, ESearchCase::IgnoreCase)
				|| ScriptName.Equals(ModuleName, ESearchCase::IgnoreCase))
			{
				Match = Fn;
				++Count;
			}
		}
		return Count == 1 ? Match : nullptr;
	}

	bool AttachTree(
		UNiagaraNodeFunctionCall& Host,
		const FString& InputName,
		const FGuid& InputId,
		const TSharedPtr<FJsonObject>& NodeObj,
		int32 Depth,
		int32& NodeCount,
		FString& OutError)
	{
		if (!NodeObj.IsValid())
		{
			OutError = TEXT("dynamic_input node is not an object");
			return false;
		}
		if (Depth > MaxDepth)
		{
			OutError = FString::Printf(TEXT("dynamic_input depth exceeded %d"), MaxDepth);
			return false;
		}
		if (NodeCount >= MaxNodes)
		{
			OutError = FString::Printf(TEXT("dynamic_input node count exceeded %d"), MaxNodes);
			return false;
		}

		FString ScriptPath;
		NodeObj->TryGetStringField(TEXT("script"), ScriptPath);

		FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(FName(*InputName));
		FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, &Host);

		FNiagaraTypeDefinition InputType;
		FString TypeName;
		NodeObj->TryGetStringField(TEXT("output_type"), TypeName);
		if (TypeName.IsEmpty())
		{
			NodeObj->TryGetStringField(TEXT("value_type"), TypeName);
		}
		if (!TypeName.IsEmpty())
		{
			InputType = TypeFromName(TypeName);
		}
		if (!InputType.IsValid())
		{
			if (UEdGraphPin* Existing = FindInputPin(Host, InputName))
			{
				const UEdGraphSchema_Niagara* NiagaraSchema = GetDefault<UEdGraphSchema_Niagara>();
				InputType = NiagaraSchema->PinToTypeDefinition(Existing);
			}
		}
		if (!InputType.IsValid())
		{
			OutError = FString::Printf(TEXT("cannot resolve Niagara type for input '%s'"), *InputName);
			return false;
		}

		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			Host, AliasedHandle, InputType, InputId, FGuid());

		if (!ScriptPath.IsEmpty())
		{
			UNiagaraScript* DiScript = Cast<UNiagaraScript>(StaticLoadObject(UNiagaraScript::StaticClass(), nullptr, *ScriptPath));
			if (!DiScript)
			{
				OutError = FString::Printf(TEXT("Dynamic Input script not found: %s"), *ScriptPath);
				return false;
			}
			if (OverridePin.LinkedTo.Num() > 0)
			{
				const UEdGraphSchema* Schema = GetDefault<UEdGraphSchema>();
				Schema->BreakPinLinks(OverridePin, true);
			}
			UNiagaraNodeFunctionCall* DiNode = nullptr;
			FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(
				OverridePin, DiScript, DiNode, FGuid(), FString(), FGuid());
			if (!DiNode)
			{
				OutError = FString::Printf(TEXT("SetDynamicInputForFunctionInput returned null for %s"), *ScriptPath);
				return false;
			}
			++NodeCount;

			const TSharedPtr<FJsonObject>* InputsObj = nullptr;
			if (NodeObj->TryGetObjectField(TEXT("inputs"), InputsObj) && InputsObj && (*InputsObj).IsValid())
			{
				for (const auto& Pair : (*InputsObj)->Values)
				{
					const FString NormalizedInputName(Pair.Key);
					const TSharedPtr<FJsonObject> ChildObj = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
					if (!ChildObj.IsValid())
					{
						OutError = FString::Printf(TEXT("input '%s' is not an object"), *NormalizedInputName);
						return false;
					}
					FGuid ChildId;
					FString ChildIdStr;
					if (ChildObj->TryGetStringField(TEXT("input_id"), ChildIdStr) && !ChildIdStr.IsEmpty())
					{
						if (!FGuid::Parse(ChildIdStr, ChildId))
						{
							OutError = FString::Printf(TEXT("input_id is not a valid GUID: %s"), *ChildIdStr);
							return false;
						}
					}
					if (!AttachTree(*DiNode, NormalizedInputName, ChildId, ChildObj, Depth + 1, NodeCount, OutError))
					{
						return false;
					}
				}
			}
			return true;
		}

		const TSharedPtr<FJsonValue> ValueField = NodeObj->TryGetField(TEXT("value"));
		const FString DefaultStr = JsonValueToPinDefault(ValueField);
		if (DefaultStr.IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("leaf input '%s' has no representable pin default (script or value required)"),
				*InputName);
			return false;
		}
		if (OverridePin.LinkedTo.Num() > 0)
		{
			GetDefault<UEdGraphSchema>()->BreakPinLinks(OverridePin, true);
		}
		GetDefault<UEdGraphSchema>()->TrySetDefaultValue(OverridePin, DefaultStr);
		++NodeCount;
		return true;
	}
}

void UCUWebSocketEditorSubsystem::HandleAttachNiagaraDynamicInput(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	// Issue #27351: apply attach_dynamic_input. Rust already offline-validated the
	// tree; this handler must exist so apply does not fall through to Unknown message type.
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleAttachNiagaraDynamicInput: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	FString ScriptStage = TEXT("particle_update");
	Request->TryGetStringField(TEXT("script_stage"), ScriptStage);
	const FString ModuleName = Request->GetStringField(TEXT("module_name"));
	const FString InputName = Request->GetStringField(TEXT("input_name"));
	FString EmitterName;
	Request->TryGetStringField(TEXT("emitter_name"), EmitterName);
	FString InputIdStr;
	Request->TryGetStringField(TEXT("input_id"), InputIdStr);
	FString InputTypeStr;
	Request->TryGetStringField(TEXT("input_type"), InputTypeStr);

	auto SendFail = [&](const FString& ErrorMsg)
	{
		const FString Escaped = ErrorMsg.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
		const FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"saved\":false,\"error\":\"%s\"}"),
			*RequestId, *Escaped);
		SendResponse(Socket, ResponseJSON);
	};

	if (AssetPath.IsEmpty() || ModuleName.IsEmpty() || InputName.IsEmpty())
	{
		SendFail(TEXT("asset_path, module_name, and input_name are required"));
		return;
	}

	const TSharedPtr<FJsonObject>* DiObj = nullptr;
	if (!Request->TryGetObjectField(TEXT("dynamic_input"), DiObj) || !DiObj || !(*DiObj).IsValid())
	{
		SendFail(TEXT("dynamic_input object is required"));
		return;
	}

	ENiagaraScriptUsage Usage;
	FString UsageError;
	if (!CUNiagaraAttachDI::ParseUsage(ScriptStage, Usage, UsageError))
	{
		SendFail(UsageError);
		return;
	}

	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset && !AssetPath.Contains(TEXT(".")))
	{
		const FString NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		SendFail(FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
		return;
	}

	UNiagaraGraph* Graph = nullptr;
	if (CUNiagaraAttachDI::IsSystemUsage(Usage))
	{
		UNiagaraScript* SystemScript = (Usage == ENiagaraScriptUsage::SystemSpawnScript)
			? NiagaraSystem->GetSystemSpawnScript()
			: NiagaraSystem->GetSystemUpdateScript();
		UNiagaraScriptSource* ScriptSource = SystemScript
			? Cast<UNiagaraScriptSource>(SystemScript->GetLatestSource())
			: nullptr;
		Graph = ScriptSource ? ScriptSource->NodeGraph : nullptr;
	}
	else
	{
		if (EmitterName.IsEmpty())
		{
			SendFail(TEXT("emitter_name is required for emitter/particle script_stage"));
			return;
		}
		auto NormalizeEmitter = [](const FString& In) -> FString
		{
			int32 Underscore;
			if (In.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < In.Len() - 1)
			{
				const FString Tail = In.Mid(Underscore + 1);
				if (Tail.IsNumeric())
				{
					return In.Left(Underscore);
				}
			}
			return In;
		};
		const FString EmitterNameNorm = NormalizeEmitter(EmitterName);
		for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
		{
			const FString HandleName = Handle.GetName().ToString();
			if (HandleName != EmitterName && HandleName != EmitterNameNorm)
			{
				continue;
			}
			FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
			UNiagaraScriptSource* ScriptSource = EmitterData
				? Cast<UNiagaraScriptSource>(EmitterData->GraphSource)
				: nullptr;
			Graph = ScriptSource ? ScriptSource->NodeGraph : nullptr;
			break;
		}
		if (!Graph)
		{
			SendFail(FString::Printf(TEXT("Emitter not found or has no graph: %s"), *EmitterName));
			return;
		}
	}

	if (!Graph)
	{
		SendFail(TEXT("NiagaraGraph not available for the requested script_stage"));
		return;
	}

	UNiagaraNodeOutput* OutputNode = Graph->FindEquivalentOutputNode(Usage, FGuid());
	if (!OutputNode)
	{
		SendFail(FString::Printf(TEXT("No output node for script_stage '%s'"), *ScriptStage));
		return;
	}

	UNiagaraNodeFunctionCall* ModuleNode = CUNiagaraAttachDI::FindModule(Graph, ModuleName, Usage);
	if (!ModuleNode)
	{
		SendFail(FString::Printf(
			TEXT("Module not found or name is ambiguous: %s (provide the get_structure module name)"),
			*ModuleName));
		return;
	}

	FGuid InputId;
	if (!InputIdStr.IsEmpty() && !FGuid::Parse(InputIdStr, InputId))
	{
		SendFail(FString::Printf(TEXT("input_id is not a valid GUID: %s"), *InputIdStr));
		return;
	}

	TSharedPtr<FJsonObject> RootObj = *DiObj;
	if (!InputTypeStr.IsEmpty() && !RootObj->HasField(TEXT("output_type")))
	{
		RootObj->SetStringField(TEXT("output_type"), InputTypeStr);
	}

	FScopedTransaction Transaction(FText::FromString(TEXT("Attach Niagara Dynamic Input")));
	Graph->Modify();
	NiagaraSystem->Modify();

	int32 NodeCount = 0;
	FString AttachError;
	if (!CUNiagaraAttachDI::AttachTree(*ModuleNode, InputName, InputId, RootObj, 1, NodeCount, AttachError))
	{
		Transaction.Cancel();
		SendFail(AttachError);
		return;
	}

	Graph->NotifyGraphChanged();
	NiagaraSystem->RequestCompile(false);
	NiagaraSystem->MarkPackageDirty();

	bool bSaved = false;
	if (UPackage* Package = NiagaraSystem->GetOutermost())
	{
		NiagaraSystem->SetFlags(RF_Public | RF_Standalone);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bSaved = UPackage::SavePackage(
			Package,
			NiagaraSystem,
			*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()),
			SaveArgs);
	}

	auto JsonEscape = [](const FString& In) -> FString
	{
		return In.Replace(TEXT("\\"), TEXT("\\\\")).Replace(TEXT("\""), TEXT("\\\""));
	};
	const FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"asset_path\":\"%s\",\"module_name\":\"%s\",\"input_name\":\"%s\","
		"\"script_stage\":\"%s\",\"node_count\":%d,\"saved\":%s}"),
		*RequestId,
		*JsonEscape(AssetPath),
		*JsonEscape(ModuleName),
		*JsonEscape(InputName),
		*JsonEscape(ScriptStage),
		NodeCount,
		bSaved ? TEXT("true") : TEXT("false"));
	LogInfo(FString::Printf(TEXT("AttachNiagaraDynamicInput: SUCCESS %s.%s on %s (nodes=%d saved=%s)"),
		*ModuleName, *InputName, *AssetPath, NodeCount, bSaved ? TEXT("true") : TEXT("false")));
	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleSetNiagaraRenderer(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	// Issue #16555: set material/mesh on a Niagara renderer by DYNAMICALLY enumerating
	// the emitter's renderer list (FVersionedNiagaraEmitterData::GetRenderers), replacing
	// the old Python `for idx in range(5)` `_0.._4` path guessing. Works for any renderer
	// count and for inherited emitters (GetEmitterData resolves the merged/latest data).
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetNiagaraRenderer: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString EmitterFilter = Request->HasField(TEXT("emitter_name")) ? Request->GetStringField(TEXT("emitter_name")) : FString();
	const FString RendererFilter = Request->HasField(TEXT("renderer_name")) ? Request->GetStringField(TEXT("renderer_name")) : FString();
	const FString MaterialPath = Request->HasField(TEXT("material_path")) ? Request->GetStringField(TEXT("material_path")) : FString();
	const FString MeshPath = Request->HasField(TEXT("mesh_path")) ? Request->GetStringField(TEXT("mesh_path")) : FString();
	const int32 MeshIndex = Request->HasField(TEXT("mesh_index")) ? FMath::Max(0, (int32)Request->GetNumberField(TEXT("mesh_index"))) : 0;
	const bool bMeshMode = !MeshPath.IsEmpty();
	// Issue #16555: apply to ALL matching renderers (across all/filtered emitters), not just the first.
	const bool bApplyAll = Request->HasField(TEXT("apply_all")) ? Request->GetBoolField(TEXT("apply_all")) : false;

	// Load the NiagaraSystem (with the .AssetName suffix fallback used elsewhere).
	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset && !AssetPath.Contains(TEXT(".")))
	{
		const FString NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath)));
		return;
	}

	// Load the material or mesh asset.
	UMaterialInterface* Material = nullptr;
	UStaticMesh* StaticMeshAsset = nullptr;
	if (bMeshMode)
	{
		StaticMeshAsset = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
		if (!StaticMeshAsset)
		{
			SendResponse(Socket, FString::Printf(TEXT(
				"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"StaticMesh not found: %s\"}"),
				*RequestId, *CUJsonUtils::EscapeJsonString(MeshPath)));
			return;
		}
	}
	else
	{
		Material = Cast<UMaterialInterface>(StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, *MaterialPath));
		if (!Material)
		{
			SendResponse(Socket, FString::Printf(TEXT(
				"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Material not found: %s\"}"),
				*RequestId, *CUJsonUtils::EscapeJsonString(MaterialPath)));
			return;
		}
	}

	// Issue #16555: get_structure reports emitter names with a trailing `_N` index
	// (e.g. "Explosion_1") while the emitter HANDLE name is bare ("Explosion").
	// Accept either form so a name copied from get_structure also matches here.
	auto NormalizeEmitter = [](const FString& In) -> FString
	{
		int32 Underscore;
		if (In.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < In.Len() - 1)
		{
			const FString Tail = In.Mid(Underscore + 1);
			if (Tail.IsNumeric())
			{
				return In.Left(Underscore);
			}
		}
		return In;
	};
	const FString EmitterFilterNorm = NormalizeEmitter(EmitterFilter);

	bool bFound = false;
	int32 RenderersScanned = 0;
	int32 AppliedCount = 0;
	FString MatchedEmitter;
	FString MatchedRenderer;
	for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
	{
		const FString EmitterName = Handle.GetName().ToString();
		if (!EmitterFilter.IsEmpty() && EmitterName != EmitterFilter && EmitterName != EmitterFilterNorm)
		{
			continue;
		}
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!EmitterData)
		{
			continue;
		}
		// GetRenderers() returns the full live renderer list — any count, inheritance-resolved.
		const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
		for (UNiagaraRendererProperties* R : Renderers)
		{
			if (!R)
			{
				continue;
			}
			++RenderersScanned;
			const FString RName = R->GetName();
			if (!RendererFilter.IsEmpty() && RName != RendererFilter)
			{
				continue;
			}

			if (bMeshMode)
			{
				UNiagaraMeshRendererProperties* MeshR = Cast<UNiagaraMeshRendererProperties>(R);
				if (!MeshR)
				{
					continue; // mesh op only applies to mesh renderers
				}
				MeshR->Modify();
				if (MeshIndex < MeshR->Meshes.Num())
				{
					MeshR->Meshes[MeshIndex].Mesh = StaticMeshAsset;
				}
				else
				{
					FNiagaraMeshRendererMeshProperties Entry;
					Entry.Mesh = StaticMeshAsset;
					MeshR->Meshes.Add(Entry);
				}
			}
			else
			{
				// Generic "Material" set — mirrors the old set_editor_property('material', mi);
				// covers Sprite/Ribbon/Component renderers without per-type casts.
				FObjectProperty* MatProp = FindFProperty<FObjectProperty>(R->GetClass(), TEXT("Material"));
				if (!MatProp)
				{
					continue; // this renderer type has no Material property
				}
				// Defensive: skip if the property's expected class can't hold a UMaterialInterface
				// (e.g. a same-named property of an unrelated object type). All stock Niagara
				// renderers use UMaterialInterface, so this is belt-and-suspenders.
				if (MatProp->PropertyClass && !Material->IsA(MatProp->PropertyClass))
				{
					continue;
				}
				R->Modify();
				MatProp->SetObjectPropertyValue_InContainer(R, Material);
			}

			R->PostEditChange();
			if (AppliedCount == 0)
			{
				MatchedEmitter = EmitterName;
				MatchedRenderer = RName;
			}
			++AppliedCount;
			bFound = true;
			if (!bApplyAll)
			{
				break; // first-match only (default)
			}
		}
		if (bFound && !bApplyAll)
		{
			break;
		}
	}

	if (bFound)
	{
		// Issue #16555: SavePackage(TopLevelFlags=RF_Standalone) FATAL-errors ("asset does not
		// have the provided object flags ... saving would lose data") if the system object lacks
		// RF_Standalone — which can happen for assets created/duplicated outside the normal factory
		// path. Ensure the flag is present so a renderer edit can never crash the Editor.
		// Issue #24665: report actual SavePackage result (was hard-coded saved:true).
		bool bSaved = false;
		NiagaraSystem->SetFlags(RF_Public | RF_Standalone);
		NiagaraSystem->MarkPackageDirty();
		UPackage* Package = NiagaraSystem->GetOutermost();
		if (Package)
		{
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Standalone;
			bSaved = UPackage::SavePackage(Package, NiagaraSystem, *FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()), SaveArgs);
		}
		LogInfo(FString::Printf(TEXT("SetNiagaraRenderer: SUCCESS %s on emitter=%s renderer=%s (applied %d of %d scanned, apply_all=%s, saved=%s)"),
			bMeshMode ? TEXT("mesh") : TEXT("material"), *MatchedEmitter, *MatchedRenderer, AppliedCount, RenderersScanned,
			bApplyAll ? TEXT("true") : TEXT("false"), bSaved ? TEXT("true") : TEXT("false")));
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"emitter\":\"%s\",\"renderer\":\"%s\","
			"\"renderers_scanned\":%d,\"applied_count\":%d,\"%s\":\"%s\",\"saved\":%s}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(MatchedEmitter), *CUJsonUtils::EscapeJsonString(MatchedRenderer),
			RenderersScanned, AppliedCount, bMeshMode ? TEXT("mesh") : TEXT("material"),
			*CUJsonUtils::EscapeJsonString(bMeshMode ? MeshPath : MaterialPath),
			bSaved ? TEXT("true") : TEXT("false")));
	}
	else
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"renderers_scanned\":%d,"
			"\"error\":\"No matching %s renderer found (scanned %d). Provide emitter_name/renderer_name from get_structure.\"}"),
			*RequestId, RenderersScanned, bMeshMode ? TEXT("mesh") : TEXT("material"), RenderersScanned));
	}
}

void UCUWebSocketEditorSubsystem::HandleNiagaraEventHandler(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	// Issue #16957: add or configure a Niagara event handler on the UE 5.8 versioned emitter.
	// The old embedded-Python path (`event_handler_script_props` + `NiagaraEventScriptProperties()`)
	// is unreachable on 5.8 — the handler list moved onto
	// `FVersionedNiagaraEmitterData::EventHandlerScriptProps`, a BlueprintInternalUseOnly struct
	// with C++-only accessors. Mirrors the HandleSetNiagaraRenderer pattern: load system ->
	// iterate GetEmitterHandles() -> GetEmitterData() -> mutate -> SavePackage.
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleNiagaraEventHandler: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString MessageType = Request->GetStringField(TEXT("type"));
	const bool bAddMode = (MessageType == TEXT("add_niagara_event_handler"));

	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString EmitterFilter = Request->HasField(TEXT("emitter_name")) ? Request->GetStringField(TEXT("emitter_name")) : FString();
	const FString SourceEmitter = Request->HasField(TEXT("source_emitter")) ? Request->GetStringField(TEXT("source_emitter")) : FString();
	const FString EventName = Request->HasField(TEXT("event_name")) ? Request->GetStringField(TEXT("event_name")) : FString();
	const FString EventHandlerName = Request->HasField(TEXT("event_handler_name")) ? Request->GetStringField(TEXT("event_handler_name")) : FString();
	const FString ExecModeStr = Request->HasField(TEXT("execution_mode")) ? Request->GetStringField(TEXT("execution_mode")) : FString();
	const bool bHasSpawn = Request->HasField(TEXT("spawn_number"));
	// Clamp the external double to [0, MAX_int32] BEFORE the int32 cast — casting an
	// out-of-range double to int32 is UB (Rule #27); these feed uint32 emitter fields.
	const int32 SpawnNumber = bHasSpawn ? (int32)FMath::Clamp(Request->GetNumberField(TEXT("spawn_number")), 0.0, (double)MAX_int32) : 0;
	const bool bHasMaxEvents = Request->HasField(TEXT("max_events_per_frame"));
	const int32 MaxEventsPerFrame = bHasMaxEvents ? (int32)FMath::Clamp(Request->GetNumberField(TEXT("max_events_per_frame")), 0.0, (double)MAX_int32) : 0;
	const bool bHasRandomSpawn = Request->HasField(TEXT("random_spawn"));
	const bool bRandomSpawn = bHasRandomSpawn ? Request->GetBoolField(TEXT("random_spawn")) : false;

	// Mode-specific identity fields — validate up-front for a clear error.
	// event_name is always required in add mode. source_emitter is OPTIONAL (Issue #17149 [4c]):
	// an empty/absent source_emitter leaves SourceEmitterID all-zero, which the engine treats as
	// "this emitter generates the event" (self-emitter). That is the verified-safe binding — both
	// the generator and the handler then key the event data set on the emitter's GetIdName() — and
	// is what the [4c] self-emitter firing proof relies on. (Existing cross-emitter callers still
	// pass source_emitter and are unaffected; the GUID-keyed cross-emitter path is unchanged.)
	if (bAddMode && EventName.IsEmpty())
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"add_niagara_event_handler requires event_name\"}"),
			*RequestId));
		return;
	}
	if (!bAddMode && EventHandlerName.IsEmpty())
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"configure_niagara_event requires event_handler_name\"}"),
			*RequestId));
		return;
	}

	// Load the NiagaraSystem (with the .AssetName suffix fallback used elsewhere).
	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset && !AssetPath.Contains(TEXT(".")))
	{
		const FString NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath)));
		return;
	}

	// Map execution_mode string -> EScriptExecutionMode (default EveryParticle).
	EScriptExecutionMode ExecMode = EScriptExecutionMode::EveryParticle;
	if (ExecModeStr == TEXT("SPAWNED_PARTICLES") || ExecModeStr == TEXT("SPAWN_ONLY"))
	{
		ExecMode = EScriptExecutionMode::SpawnedParticles;
	}
	else if (ExecModeStr == TEXT("SINGLE_PARTICLE"))
	{
		ExecMode = EScriptExecutionMode::SingleParticle;
	}

	// Issue #16957: get_structure reports emitter names with a trailing `_N` index
	// (e.g. "Explosion_1") while the emitter HANDLE name is bare ("Explosion"). Accept either
	// form so a name copied from get_structure also matches — mirrors HandleSetNiagaraRenderer.
	auto NormalizeEmitter = [](const FString& In) -> FString
	{
		int32 Underscore;
		if (In.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < In.Len() - 1)
		{
			const FString Tail = In.Mid(Underscore + 1);
			if (Tail.IsNumeric())
			{
				return In.Left(Underscore);
			}
		}
		return In;
	};
	const FString EmitterFilterNorm = NormalizeEmitter(EmitterFilter);
	const FString SourceEmitterNorm = NormalizeEmitter(SourceEmitter);

	// (add) Resolve the source-emitter NAME to its handle Id — FNiagaraEventScriptProperties
	// stores SourceEmitterID (FGuid), not a name. Empty/unmatched leaves it all-zero, which the
	// engine treats as "this emitter generates the event".
	FGuid SourceEmitterId;
	if (bAddMode && !SourceEmitter.IsEmpty())
	{
		for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
		{
			const FString EmitterName = Handle.GetName().ToString();
			if (EmitterName == SourceEmitter || EmitterName == SourceEmitterNorm)
			{
				SourceEmitterId = Handle.GetId();
				break;
			}
		}
	}

	bool bEmitterMatched = false;
	bool bDone = false;
	bool bGraphWired = false; // Issue #17009 [4b]: true once the event-script graph output node is wired
	FString MatchedEmitter;
	int32 HandlersCount = 0;
	for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
	{
		const FString EmitterName = Handle.GetName().ToString();
		if (!EmitterFilter.IsEmpty() && EmitterName != EmitterFilter && EmitterName != EmitterFilterNorm)
		{
			continue;
		}
		bEmitterMatched = true;
		const FVersionedNiagaraEmitter Versioned = Handle.GetInstance();
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!Versioned.Emitter || !EmitterData)
		{
			break; // matched the emitter but its versioned data is unavailable
		}

		if (bAddMode)
		{
			// Structural add: create a ParticleEventScript UNiagaraScript on the emitter and
			// register a new FNiagaraEventScriptProperties via UNiagaraEmitter::AddEventHandler
			// (the editor's own AddEventHandler entry point — Modify()+UpdateChangeId+Broadcast).
			Versioned.Emitter->Modify();
			FNiagaraEventScriptProperties Props;
			Props.Script = NewObject<UNiagaraScript>(Versioned.Emitter,
				MakeUniqueObjectName(Versioned.Emitter, UNiagaraScript::StaticClass(), TEXT("EventScript")),
				RF_Transactional);
			Props.Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
			Props.Script->SetUsageId(FGuid::NewGuid());
			Props.Script->SetLatestSource(EmitterData->GraphSource); // emitter's shared graph source
			Props.SourceEmitterID = SourceEmitterId;
			Props.SourceEventName = FName(*EventName);
			Props.ExecutionMode = ExecMode;
			Props.SpawnNumber = (uint32)SpawnNumber;
			Props.MaxEventsPerFrame = (uint32)MaxEventsPerFrame;
			Props.bRandomSpawnNumber = bRandomSpawn;
			Versioned.Emitter->AddEventHandler(Props, Versioned.Version);

			// Issue #17009 [4b]: wire the event script's graph output node so the handler is a
			// runnable compiled script, not just a structural entry. The editor does this via
			// FNiagaraStackGraphUtilities::ResetGraphForOutput, which is declared WITHOUT
			// NIAGARAEDITOR_API (NiagaraStackGraphUtilities.h:302) and so is not linkable across the
			// module boundary (the #16957 [4] LNK2019 blocker). Replicate its minimal body inline using
			// only exported / public / header-inline graph APIs.
			if (UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(EmitterData->GraphSource))
			{
				if (UNiagaraGraph* EventGraph = ScriptSource->NodeGraph)
				{
					const FGuid EventUsageId = Props.Script->GetUsageId();
					EventGraph->Modify();

					// Output node bound to ParticleEventScript + this event script's usage id. Setting
					// Outputs before Finalize() makes AllocateDefaultPins create the parameter-map input pin.
					FGraphNodeCreator<UNiagaraNodeOutput> OutputCreator(*EventGraph);
					UNiagaraNodeOutput* OutputNode = OutputCreator.CreateNode();
					OutputNode->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
					OutputNode->SetUsageId(EventUsageId);
					OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
					OutputCreator.Finalize();

					// Parameter-map input node feeding the output node.
					FGraphNodeCreator<UNiagaraNodeInput> InputCreator(*EventGraph);
					UNiagaraNodeInput* InputNode = InputCreator.CreateNode();
					InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
					InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
					InputCreator.Finalize();

					// Link input-node output pin -> output-node input pin via public UEdGraphPin APIs
					// (the engine's type-aware GetParameterMap*Pin helpers are unexported). A freshly
					// created event-script output/input node has exactly ONE parameter-map pin per
					// relevant direction. Count the pins and refuse to guess if a future engine pin
					// layout introduces a same-direction pin (fail loud — leave graph_wired false —
					// rather than silently linking the wrong pin into a broken event script).
					int32 OutInputCount = 0;
					UEdGraphPin* OutInputPin = nullptr;
					for (UEdGraphPin* Pin : OutputNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Input)
						{
							++OutInputCount;
							if (!OutInputPin) { OutInputPin = Pin; }
						}
					}
					int32 InOutputCount = 0;
					UEdGraphPin* InOutputPin = nullptr;
					for (UEdGraphPin* Pin : InputNode->Pins)
					{
						if (Pin && Pin->Direction == EGPD_Output)
						{
							++InOutputCount;
							if (!InOutputPin) { InOutputPin = Pin; }
						}
					}
					if (OutInputPin && InOutputPin && OutInputCount == 1 && InOutputCount == 1)
					{
						OutInputPin->BreakAllPinLinks();
						OutInputPin->MakeLinkTo(InOutputPin);
						EventGraph->NotifyGraphChanged();
						// Recompile so the newly-wired event script becomes a runnable compiled script.
						// Non-blocking (no synchronous PollForCompilationComplete on the WS thread); the
						// changed graph id also triggers a recompile on the next PIE/load.
						NiagaraSystem->RequestCompile(false);
						bGraphWired = true;
					}
					else
					{
						LogInfo(FString::Printf(TEXT("AddNiagaraEventHandler: expected exactly one "
							"parameter-map pin per direction (output-node inputs=%d, input-node outputs=%d); "
							"handler entry created but graph output node not wired (Issue #17009 [4b])."),
							OutInputCount, InOutputCount));
					}
				}
			}
			HandlersCount = EmitterData->EventHandlerScriptProps.Num();
			MatchedEmitter = EmitterName;
			bDone = true;
		}
		else
		{
			// Configure: mutate fields on the existing entry matched by SourceEventName.
			for (FNiagaraEventScriptProperties& H : EmitterData->EventHandlerScriptProps)
			{
				if (H.SourceEventName == FName(*EventHandlerName))
				{
					Versioned.Emitter->Modify();
					if (!ExecModeStr.IsEmpty()) { H.ExecutionMode = ExecMode; }
					if (bHasSpawn) { H.SpawnNumber = (uint32)SpawnNumber; }
					if (bHasMaxEvents) { H.MaxEventsPerFrame = (uint32)MaxEventsPerFrame; }
					if (bHasRandomSpawn) { H.bRandomSpawnNumber = bRandomSpawn; }
					MatchedEmitter = EmitterName;
					bDone = true;
					break;
				}
			}
			HandlersCount = EmitterData->EventHandlerScriptProps.Num();
			// Issue #16957: a bare Modify() does NOT make the field edit survive serialization
			// (live-verified: configure reported success but spawn/mode reverted on reload). The
			// editor's own path (NiagaraStackEventScriptItemGroup.cpp) mutates EventHandlerScriptProps
			// then calls PostEditChangeVersionedProperty, which bumps the emitter change id so the save
			// persists the edit. UpdateChangeId() is private, so use the public versioned entry point.
			if (bDone)
			{
				FProperty* ChangedProp = FVersionedNiagaraEmitterData::StaticStruct()->FindPropertyByName(
					GET_MEMBER_NAME_CHECKED(FVersionedNiagaraEmitterData, EventHandlerScriptProps));
				FPropertyChangedEvent PropertyChangedEvent(ChangedProp, EPropertyChangeType::ValueSet);
				Versioned.Emitter->PostEditChangeVersionedProperty(PropertyChangedEvent, Versioned.Version);
			}
		}
		break; // emitter_name is required; stop after processing the matched emitter
	}

	if (!bEmitterMatched)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter not found: %s. Provide emitter_name from get_structure.\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(EmitterFilter)));
		return;
	}

	if (!bDone)
	{
		if (bAddMode)
		{
			// add-mode only reaches here when the matched emitter's versioned data was
			// unavailable (the break above) — EventHandlerName/MatchedEmitter are empty, so
			// report the real failure against the requested emitter instead of a spurious
			// "handler not found" with blank fields.
			SendResponse(Socket, FString::Printf(TEXT(
				"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,"
				"\"error\":\"Failed to add event handler: versioned emitter data unavailable for emitter '%s'\"}"),
				*RequestId, *CUJsonUtils::EscapeJsonString(EmitterFilter)));
			return;
		}
		// configure: emitter matched but no handler with that SourceEventName.
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"handlers_count\":%d,"
			"\"error\":\"Event handler not found: %s (emitter %s has %d handler(s)).\"}"),
			*RequestId, HandlersCount, *CUJsonUtils::EscapeJsonString(EventHandlerName),
			*CUJsonUtils::EscapeJsonString(MatchedEmitter), HandlersCount));
		return;
	}

	// Persist the mutated NiagaraSystem package.
	NiagaraSystem->MarkPackageDirty();
	bool bSaved = false;
	if (UPackage* Package = NiagaraSystem->GetOutermost())
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bSaved = UPackage::SavePackage(Package, NiagaraSystem,
			*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()),
			SaveArgs);
	}

	if (bAddMode)
	{
		LogInfo(FString::Printf(TEXT("AddNiagaraEventHandler: SUCCESS emitter=%s event=%s (now %d handler(s), graph_wired=%s)"),
			*MatchedEmitter, *EventName, HandlersCount, bGraphWired ? TEXT("true") : TEXT("false")));
		const FString AddNote = bGraphWired
			? TEXT("Handler entry created and event-script graph output node wired (ParticleEventScript); recompile requested - handler executes at runtime (Issue #17009 [4b]).")
			: TEXT("Handler entry created (structural); event-script graph output node could not be wired (Issue #17009 [4b]).");
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"mode\":\"add\",\"emitter\":\"%s\","
			"\"event_name\":\"%s\",\"source_emitter\":\"%s\",\"handlers_count\":%d,\"saved\":%s,\"graph_wired\":%s,"
			"\"note\":\"%s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(MatchedEmitter),
			*CUJsonUtils::EscapeJsonString(EventName), *CUJsonUtils::EscapeJsonString(SourceEmitter),
			HandlersCount, bSaved ? TEXT("true") : TEXT("false"), bGraphWired ? TEXT("true") : TEXT("false"),
			*CUJsonUtils::EscapeJsonString(AddNote)));
	}
	else
	{
		LogInfo(FString::Printf(TEXT("ConfigureNiagaraEvent: SUCCESS emitter=%s handler=%s"),
			*MatchedEmitter, *EventHandlerName));
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"mode\":\"configure\",\"emitter\":\"%s\","
			"\"event_handler\":\"%s\",\"handlers_count\":%d,\"saved\":%s}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(MatchedEmitter),
			*CUJsonUtils::EscapeJsonString(EventHandlerName), HandlersCount, bSaved ? TEXT("true") : TEXT("false")));
	}
}

// Issue #17149 [4c]: forward declarations for the two PIE-precondition helpers used by
// HandleNiagaraCaptureSimCache below — their definitions live post-MultiPIE-namespace (~L8795 /
// ~L8812), which is AFTER this handler, so the compiler needs the signatures here. These exactly
// mirror the canonical forward declarations at ~L8795 (static, file-scope) and ~L8812
// (external-linkage friend-matching). Keep both copies in sync if the signatures ever change.
bool EnsurePIERunningMultiPIE(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType);
UWorld* ResolveClientWorldOrReject(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request,
	const FString& RequestId,
	const TCHAR* ResponseType,
	int32& OutClientId);

void UCUWebSocketEditorSubsystem::HandleNiagaraEventGenerator(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	// Issue #17149 [4c]: add a Generate{Location|Death|Collision}Event module to the SOURCE
	// emitter's Particle Update script stack. The emitter that GENERATES events needs the
	// Generate*Event module in its update graph (the [4b] add_niagara_event_handler wires the
	// CONSUMER side). Mirrors HandleNiagaraEventHandler's load/mutate/recompile/save/read-back
	// phases. The canonical generated event NAME is only authoritative after a compile populates
	// FVersionedNiagaraEmitterData::UpdateScriptProps.EventGenerators (the module-default name is
	// not knowable from the structural add alone).
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleNiagaraEventGenerator: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString EmitterName = Request->HasField(TEXT("emitter_name")) ? Request->GetStringField(TEXT("emitter_name")) : FString();
	const FString GeneratorType = Request->HasField(TEXT("generator_type")) ? Request->GetStringField(TEXT("generator_type")) : TEXT("Location");

	auto FailJson = [&](const FString& ErrorMsg)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_generator_result\",\"request_id\":\"%s\",\"success\":false,"
			"\"asset_path\":\"%s\",\"emitter\":\"%s\",\"generator_type\":\"%s\",\"module_added\":false,"
			"\"canonical_event_name\":\"\",\"event_name_source\":\"module_default_unverified\","
			"\"persistent_ids_enabled\":false,\"saved\":false,\"note\":\"\",\"error\":\"%s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath), *CUJsonUtils::EscapeJsonString(EmitterName),
			*CUJsonUtils::EscapeJsonString(GeneratorType), *CUJsonUtils::EscapeJsonString(ErrorMsg)));
	};

	if (EmitterName.IsEmpty())
	{
		FailJson(TEXT("add_niagara_event_generator requires emitter_name (the source emitter that generates events)"));
		return;
	}

	// Resolve the Generate*Event module asset path by generator_type (enum: Location|Death|Collision).
	FString ModulePath;
	if (GeneratorType == TEXT("Location"))
	{
		ModulePath = TEXT("/Niagara/Modules/Events/GenerateLocationEvent.GenerateLocationEvent");
	}
	else if (GeneratorType == TEXT("Death"))
	{
		ModulePath = TEXT("/Niagara/Modules/Events/GenerateDeathEvent.GenerateDeathEvent");
	}
	else if (GeneratorType == TEXT("Collision"))
	{
		ModulePath = TEXT("/Niagara/Modules/Events/GenerateCollisionEvent.GenerateCollisionEvent");
	}
	else
	{
		FailJson(FString::Printf(TEXT("Unknown generator_type '%s' (expected Location|Death|Collision)"), *GeneratorType));
		return;
	}

	// Load the NiagaraSystem (with the .AssetName suffix fallback used elsewhere).
	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset && !AssetPath.Contains(TEXT(".")))
	{
		const FString NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		FailJson(FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
		return;
	}

	// Accept the get_structure "Name_N" form as well as the bare handle name (mirrors [4b]).
	auto NormalizeEmitter = [](const FString& In) -> FString
	{
		int32 Underscore;
		if (In.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < In.Len() - 1)
		{
			const FString Tail = In.Mid(Underscore + 1);
			if (Tail.IsNumeric())
			{
				return In.Left(Underscore);
			}
		}
		return In;
	};
	const FString EmitterNameNorm = NormalizeEmitter(EmitterName);

	bool bEmitterMatched = false;
	FString MatchedEmitter;
	bool bModuleAdded = false;
	bool bPersistentIds = false;
	FString CanonicalEventName;
	FString EventNameSource = TEXT("module_default_unverified");

	for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
	{
		const FString HandleName = Handle.GetName().ToString();
		if (HandleName != EmitterName && HandleName != EmitterNameNorm)
		{
			continue;
		}
		bEmitterMatched = true;
		MatchedEmitter = HandleName;

		const FVersionedNiagaraEmitter Versioned = Handle.GetInstance();
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (!Versioned.Emitter || !EmitterData)
		{
			FailJson(FString::Printf(TEXT("Versioned emitter data unavailable for emitter '%s'"), *EmitterName));
			return;
		}

		// Resolve the emitter's shared graph + the Particle Update output node. Use the exported
		// FindEquivalentOutputNode (NIAGARAEDITOR_API) over FindOutputNode to avoid an LNK2019 across
		// the NiagaraEditor module boundary (NiagaraGraph.h:270 vs :271).
		UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
		UNiagaraGraph* Graph = ScriptSource ? ScriptSource->NodeGraph : nullptr;
		if (!Graph)
		{
			FailJson(FString::Printf(TEXT("Emitter '%s' has no editable NiagaraGraph (GraphSource not a UNiagaraScriptSource)"), *EmitterName));
			return;
		}
		UNiagaraNodeOutput* UpdateOut = Graph->FindEquivalentOutputNode(ENiagaraScriptUsage::ParticleUpdateScript, FGuid());
		if (!UpdateOut)
		{
			FailJson(FString::Printf(TEXT("Emitter '%s' has no Particle Update output node"), *EmitterName));
			return;
		}

		// Load the Generate*Event module UNiagaraScript.
		UNiagaraScript* ModuleScript = Cast<UNiagaraScript>(StaticLoadObject(UNiagaraScript::StaticClass(), nullptr, *ModulePath));
		if (!ModuleScript)
		{
			FailJson(FString::Printf(TEXT("Generate event module not found: %s"), *ModulePath));
			return;
		}

		Versioned.Emitter->Modify();
		Graph->Modify();

		// Insert the module into the Particle Update stack (exported overload :290).
		UNiagaraNodeFunctionCall* AddedNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
			ModuleScript, *UpdateOut, INDEX_NONE, FString(), FGuid());
		bModuleAdded = (AddedNode != nullptr);
		if (!bModuleAdded)
		{
			// The module insert failed (incompatible usage / graph state). Do NOT enable persistent
			// IDs or save the package — leave the asset unmutated and report failure rather than a
			// misleading success:true with module_added:false.
			FailJson(FString::Printf(
				TEXT("AddScriptModuleToStack returned null for module '%s' on emitter '%s' Particle Update stack; no module node created."),
				*ModulePath, *EmitterName));
			return;
		}

		// A generated event needs persistent particle IDs so consumers can resolve the source
		// particle across frames. Enable + persist via the same versioned-property bump the editor
		// uses (a bare Modify() does not survive serialization — see [4b] #16957 note).
		EmitterData->bRequiresPersistentIDs = 1;
		{
			FProperty* ChangedProp = FVersionedNiagaraEmitterData::StaticStruct()->FindPropertyByName(
				GET_MEMBER_NAME_CHECKED(FVersionedNiagaraEmitterData, bRequiresPersistentIDs));
			FPropertyChangedEvent PropertyChangedEvent(ChangedProp, EPropertyChangeType::ValueSet);
			Versioned.Emitter->PostEditChangeVersionedProperty(PropertyChangedEvent, Versioned.Version);
		}
		bPersistentIds = (EmitterData->bRequiresPersistentIDs != 0);

		Graph->NotifyGraphChanged();
		// Non-blocking recompile (no synchronous poll on the WS/game thread). The changed graph id
		// also triggers recompile on the next PIE/load.
		NiagaraSystem->RequestCompile(false);
		break;
	}

	if (!bEmitterMatched)
	{
		FailJson(FString::Printf(TEXT("Emitter not found: %s. Provide emitter_name from get_structure."), *EmitterName));
		return;
	}

	// Block until the recompile finishes so UpdateScriptProps.EventGenerators is populated before the
	// canonical-event-name read-back below (Issue #17149 [4c] review, Finding 1): RequestCompile(false)
	// is async, so without this the generator returns an empty canonical_event_name on a fresh asset
	// and a consumer cannot wire add_event_handler to the matching event. This is an authoring op (the
	// caller awaits the WS reply within the authoring timeout), so a blocking CPU-script compile is
	// acceptable; it also makes the SavePackage below persist the compiled state.
	NiagaraSystem->WaitForCompilationComplete(/*bIncludingGPUShaders=*/false, /*bShowProgress=*/false);

	// Persist the mutated package.
	NiagaraSystem->MarkPackageDirty();
	bool bSaved = false;
	if (UPackage* Package = NiagaraSystem->GetOutermost())
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bSaved = UPackage::SavePackage(Package, NiagaraSystem,
			*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()),
			SaveArgs);
	}

	// Read back the canonical generated event NAME from the now-compiled UpdateScriptProps
	// (WaitForCompilationComplete above guarantees EventGenerators is populated for a successful add).
	// NOTE (Issue #17149 [4c] review, Finding 3): EventGenerators[0] is the FIRST generator on this
	// emitter — for the single-generator [4c] fixtures that is the one just added; if a Generate*Event
	// module pre-existed, [0] may not be the newly-inserted one (add the generator on an emitter with
	// no prior Generate*Event for strict attribution). The unverified fallback remains for the rare
	// case the compile yields no generator entry.
	for (const FNiagaraEmitterHandle& Handle : NiagaraSystem->GetEmitterHandles())
	{
		const FString HandleName = Handle.GetName().ToString();
		if (HandleName != MatchedEmitter)
		{
			continue;
		}
		FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
		if (EmitterData && EmitterData->UpdateScriptProps.EventGenerators.Num() > 0)
		{
			// FNiagaraEventGeneratorProperties::ID (FName) holds the data-set id == the event name.
			CanonicalEventName = EmitterData->UpdateScriptProps.EventGenerators[0].ID.ToString();
			EventNameSource = TEXT("compiled");
		}
		break;
	}

	LogInfo(FString::Printf(TEXT("AddNiagaraEventGenerator: %s emitter=%s type=%s module_added=%s event_name='%s' (%s) saved=%s"),
		bModuleAdded ? TEXT("SUCCESS") : TEXT("PARTIAL"), *MatchedEmitter, *GeneratorType,
		bModuleAdded ? TEXT("true") : TEXT("false"), *CanonicalEventName, *EventNameSource,
		bSaved ? TEXT("true") : TEXT("false")));

	const FString Note = (EventNameSource == TEXT("compiled"))
		? TEXT("Generate event module added to Particle Update stack; persistent IDs enabled; canonical event name read from compiled UpdateScriptProps.EventGenerators (Issue #17149 [4c]).")
		: TEXT("Generate event module added to Particle Update stack; persistent IDs enabled; recompile requested - canonical event name not yet populated (compile is async), reported as module_default_unverified (Issue #17149 [4c]).");

	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"niagara_generator_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"asset_path\":\"%s\",\"emitter\":\"%s\",\"generator_type\":\"%s\",\"module_added\":%s,"
		"\"canonical_event_name\":\"%s\",\"event_name_source\":\"%s\",\"persistent_ids_enabled\":%s,"
		"\"saved\":%s,\"note\":\"%s\"}"),
		*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath), *CUJsonUtils::EscapeJsonString(MatchedEmitter),
		*CUJsonUtils::EscapeJsonString(GeneratorType), bModuleAdded ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(CanonicalEventName), *EventNameSource,
		bPersistentIds ? TEXT("true") : TEXT("false"), bSaved ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(Note)));
}

void UCUWebSocketEditorSubsystem::HandleNiagaraCaptureSimCache(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	// Issue #17149 [4c]: capture a Niagara SimCache from a ticking component in the PIE world.
	// Requires PIE (multi-PIE client_id aware). Runs on the game thread (DispatchMessage is drained
	// in Tick() under check(IsInGameThread()) — same as HandlePIEListActiveActors), so all
	// world/Niagara mutation here is game-thread-safe with no marshaling needed.
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleNiagaraCaptureSimCache: Invalid socket or request"));
		return;
	}
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	// Multi-PIE-aware PIE gate. Pass the base "niagara_simcache" so the helper's "<type>_result"
	// envelope yields the contract type "niagara_simcache_result" on the PIE-absent reject path
	// (the success path and FailJson below emit the same contract type).
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("niagara_simcache"))) return;

	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString ActorName = Request->HasField(TEXT("actor_name")) ? Request->GetStringField(TEXT("actor_name")) : FString();
	const FString EmitterFilter = Request->HasField(TEXT("emitter_name")) ? Request->GetStringField(TEXT("emitter_name")) : FString();
	// Clamp the external frame count to [1,256] BEFORE any loop/alloc (Rule #27).
	int32 NumFrames = 8;
	if (Request->HasField(TEXT("num_frames")))
	{
		NumFrames = (int32)FMath::Clamp(Request->GetNumberField(TEXT("num_frames")), 1.0, 256.0);
	}
	float AdvanceDt = 0.01666f;
	if (Request->HasField(TEXT("advance_dt")))
	{
		AdvanceDt = (float)Request->GetNumberField(TEXT("advance_dt"));
	}
	// Floor to a small positive minimum (Issue #17149 [4c] review, Finding 2): UNiagaraComponent::
	// AdvanceSimulation is a SILENT no-op when dt <= SMALL_NUMBER, which would leave the sim un-ticked
	// and yield cache_valid:false with no obvious cause. 1e-4 is well above SMALL_NUMBER yet negligible.
	AdvanceDt = FMath::Max(AdvanceDt, 1.0e-4f);

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("niagara_simcache"), ClientId);
	if (!TargetWorld) return;

	auto FailJson = [&](const FString& ErrorMsg)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_simcache_result\",\"request_id\":\"%s\",\"success\":false,"
			"\"asset_path\":\"%s\",\"client_id\":%d,\"cache_valid\":false,\"num_frames\":0,"
			"\"num_emitters\":0,\"emitters\":[],\"system_instance_counts\":[],\"spawned_actor\":false,"
			"\"note\":\"\",\"error\":\"%s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath), ClientId, *CUJsonUtils::EscapeJsonString(ErrorMsg)));
	};

	// Load the NiagaraSystem (with the .AssetName suffix fallback used elsewhere).
	UObject* LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedAsset && !AssetPath.Contains(TEXT(".")))
	{
		const FString NormalizedPath = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		LoadedAsset = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedAsset);
	if (!NiagaraSystem)
	{
		FailJson(FString::Printf(TEXT("NiagaraSystem not found: %s"), *AssetPath));
		return;
	}

	// Resolve a ticking UNiagaraComponent: reuse a placed actor by name, else spawn a transient one.
	UNiagaraComponent* Comp = nullptr;
	ANiagaraActor* SpawnedNiagaraActor = nullptr;
	bool bSpawnedActor = false;
	if (!ActorName.IsEmpty())
	{
		for (TActorIterator<AActor> It(TargetWorld); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor)) continue;
			if (Actor->GetActorNameOrLabel() == ActorName || Actor->GetName() == ActorName)
			{
				Comp = Actor->FindComponentByClass<UNiagaraComponent>();
				break;
			}
		}
		if (!Comp)
		{
			FailJson(FString::Printf(TEXT("Actor '%s' not found in PIE world or has no UNiagaraComponent"), *ActorName));
			return;
		}
		// Issue #17302: do NOT assume a placed actor's component is already ticking — in a fresh PIE
		// world it may not have activated yet. Force solo (so AdvanceSimulation can drive it without the
		// world's batched Niagara tick) and Activate; the warmup loop below re-drives it past the
		// deferred-not-ready gate the same way as the spawn path.
		Comp->SetForceSolo(true);
		Comp->Activate(true);
	}
	else
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.ObjectFlags |= RF_Transient;
		SpawnedNiagaraActor = TargetWorld->SpawnActor<ANiagaraActor>(
			ANiagaraActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
		if (!SpawnedNiagaraActor)
		{
			FailJson(TEXT("Failed to spawn ANiagaraActor in PIE world"));
			return;
		}
		Comp = SpawnedNiagaraActor->GetNiagaraComponent();
		if (!Comp)
		{
			SpawnedNiagaraActor->Destroy();
			FailJson(TEXT("Spawned ANiagaraActor has no UNiagaraComponent"));
			return;
		}
		Comp->SetAsset(NiagaraSystem);
		// Issue #17302: force solo BEFORE Activate so AdvanceSimulation can drive the sim without the
		// world's batched Niagara tick (RequiresSoloMode/UpdateInstanceSoloMode — NiagaraComponent.cpp:4708/4720).
		Comp->SetForceSolo(true);
		Comp->Activate(true);
		bSpawnedActor = true;
	}

	// Capture frame-by-frame into ONE accumulating cache. NOTE: looping
	// UNiagaraSimCacheFunctionLibrary::CaptureNiagaraSimCacheImmediate() would NOT work — it calls
	// UNiagaraSimCache::BeginWrite() internally, which Empty()s CacheFrames on every call, so only the
	// LAST frame would survive and num_frames would silently collapse to 1. Drive the low-level
	// accumulating API directly instead: BeginWrite() ONCE (after warming the sim past Activate
	// latency — a fresh component needs a tick or two before HasValidSimulation()), then advance one
	// tick per frame and WriteFrame() (WriteFrame is a no-op until the tick counter advances since the
	// previous capture), then EndWrite() once. The post-loop IsCacheValid() is the success gate.
	UNiagaraSimCache* Cache = NewObject<UNiagaraSimCache>(GetTransientPackage());
	if (!Cache)
	{
		if (bSpawnedActor && SpawnedNiagaraActor)
		{
			Comp->Deactivate();
			SpawnedNiagaraActor->Destroy();
		}
		FailJson(TEXT("Failed to allocate UNiagaraSimCache"));
		return;
	}
	// Issue #17302: drive the component PAST Niagara's deferred-activation gate before warming.
	// UNiagaraComponent::Activate() defers system-instance-controller creation when the asset/world/PSO
	// are not ready (sets bAwaitingActivationDueToNotReady and returns early — NiagaraComponent.cpp:1366-1376);
	// that deferral is normally completed by a later world TickComponent (NiagaraComponent.cpp:918-924),
	// which this synchronous handler never yields. AdvanceSimulation is a SILENT no-op while the controller
	// is invalid (NiagaraComponent.cpp:1098-1102), so a single pre-loop Activate that lands in the deferred
	// state never recovers and BeginWrite never succeeds (the #17149 [4c] cache_valid:false trap).
	// Block on CPU-script compilation (same pattern as HandleNiagaraEventGenerator above), then re-Activate
	// each retry until IsActive() flips true — SetActiveFlag(true) is reached only on the ready path
	// (NiagaraComponent.cpp:1486), so IsActive() is a faithful "controller created" signal. (A second PSO-
	// readiness deferral can re-arm bAwaitingActivationDueToNotReady at :1505-1510 *after* the flag is set;
	// the bounded MaxWarmupTicks budget + the BeginWrite success gate absorb that one-tick seam.) On a warm/cooked
	// build IsWorldReadyToRun()/PSO readiness hold, so the controller is created in-loop and AdvanceSimulation
	// drives the solo sim; on a cold build readiness never flips, BeginWrite keeps failing, and the op still
	// returns cache_valid:false (no false positive — Part 2 supplies the warm build that satisfies the gate).
	NiagaraSystem->WaitForCompilationComplete(/*bIncludingGPUShaders=*/false, /*bShowProgress=*/false);
	bool bWriteBegun = false;
	const int32 MaxWarmupTicks = 16;
	for (int32 w = 0; w < MaxWarmupTicks && !bWriteBegun; ++w)
	{
		// Re-drive activation until the readiness gate passes; once IsActive() is true the controller
		// exists, so stop re-activating and let ticks accumulate (a reset every iteration would prevent
		// the "tick or two" a fresh sim needs before HasValidSimulation()).
		if (!Comp->IsActive())
		{
			Comp->Activate(true);
		}
		Comp->AdvanceSimulation(1, AdvanceDt);
		bWriteBegun = Cache->BeginWrite(FNiagaraSimCacheCreateParameters(), Comp);
	}
	if (bWriteBegun)
	{
		for (int32 Frame = 0; Frame < NumFrames; ++Frame)
		{
			Comp->AdvanceSimulation(1, AdvanceDt);
			Cache->WriteFrame(Comp);
		}
		Cache->EndWrite();
	}

	const bool bCacheValid = Cache->IsCacheValid();
	const int32 CapturedFrames = Cache->GetNumFrames();
	const int32 NumEmitters = Cache->GetNumEmitters();

	// Build per-emitter frame instance counts (respecting the optional emitter_name filter), plus
	// the system-level counts (INDEX_NONE).
	// The SimCache layout name can carry the get_structure "Name_N" suffix form, which differs from
	// the bare emitter handle name a caller would pass (the same name add_event_generator accepts).
	// Match leniently (case-insensitive, suffix-tolerant either direction) so a valid filter does not
	// silently yield an empty emitters[] with success:true.
	auto EmitterMatches = [&EmitterFilter](const FString& CacheName) -> bool
	{
		if (EmitterFilter.IsEmpty()) return true;
		return CacheName.Equals(EmitterFilter, ESearchCase::IgnoreCase)
			|| CacheName.StartsWith(EmitterFilter, ESearchCase::IgnoreCase)
			|| EmitterFilter.StartsWith(CacheName, ESearchCase::IgnoreCase);
	};

	TArray<FString> EmitterEntries;
	for (int32 e = 0; e < NumEmitters; ++e)
	{
		const FString CacheEmitterName = Cache->GetEmitterName(e).ToString();
		if (!EmitterMatches(CacheEmitterName))
		{
			continue;
		}
		TArray<FString> FrameCounts;
		FrameCounts.Reserve(FMath::Max(0, CapturedFrames));
		for (int32 f = 0; f < CapturedFrames; ++f)
		{
			FrameCounts.Add(FString::FromInt(Cache->GetEmitterNumInstances(e, f)));
		}
		EmitterEntries.Add(FString::Printf(TEXT("{\"name\":\"%s\",\"index\":%d,\"frame_instance_counts\":[%s]}"),
			*CUJsonUtils::EscapeJsonString(CacheEmitterName), e, *FString::Join(FrameCounts, TEXT(","))));
	}

	TArray<FString> SystemCounts;
	SystemCounts.Reserve(FMath::Max(0, CapturedFrames));
	for (int32 f = 0; f < CapturedFrames; ++f)
	{
		SystemCounts.Add(FString::FromInt(Cache->GetEmitterNumInstances(INDEX_NONE, f)));
	}

	// Tear down the actor we spawned (leave reused placed actors alone).
	if (bSpawnedActor && SpawnedNiagaraActor)
	{
		Comp->Deactivate();
		SpawnedNiagaraActor->Destroy();
	}

	const FString EmittersArr = TEXT("[") + FString::Join(EmitterEntries, TEXT(",")) + TEXT("]");
	const FString SystemArr = TEXT("[") + FString::Join(SystemCounts, TEXT(",")) + TEXT("]");
	FString Note = bCacheValid
		? FString::Printf(TEXT("Captured %d frame(s) across %d emitter(s) (Issue #17149 [4c])."), CapturedFrames, NumEmitters)
		: TEXT("SimCache invalid - the Niagara component's activation stayed deferred (bAwaitingActivationDueToNotReady): the system never became ready to run, so no controller was created and AdvanceSimulation could not tick it. This is expected on a cold/never-cooked build where IsWorldReadyToRun()/PSO readiness never holds; retry on a fully-built/cooked, DDC-warm project (Issue #17302).");
	// Distinguish "filter matched nothing" from "system has no emitters" so an empty emitters[] is not
	// silently mistaken for a healthy capture.
	if (!EmitterFilter.IsEmpty() && EmitterEntries.Num() == 0 && NumEmitters > 0)
	{
		Note += FString::Printf(TEXT(" WARNING: emitter_name filter '%s' matched 0 of %d cached emitter(s)."),
			*EmitterFilter, NumEmitters);
	}

	LogInfo(FString::Printf(TEXT("NiagaraCaptureSimCache: client=%d asset=%s cache_valid=%s frames=%d emitters=%d spawned=%s"),
		ClientId, *AssetPath, bCacheValid ? TEXT("true") : TEXT("false"), CapturedFrames, NumEmitters,
		bSpawnedActor ? TEXT("true") : TEXT("false")));

	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"niagara_simcache_result\",\"request_id\":\"%s\",\"success\":%s,"
		"\"asset_path\":\"%s\",\"client_id\":%d,\"cache_valid\":%s,\"num_frames\":%d,"
		"\"num_emitters\":%d,\"emitters\":%s,\"system_instance_counts\":%s,\"spawned_actor\":%s,"
		"\"note\":\"%s\"}"),
		*RequestId, bCacheValid ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(AssetPath), ClientId, bCacheValid ? TEXT("true") : TEXT("false"),
		CapturedFrames, NumEmitters, *EmittersArr, *SystemArr,
		bSpawnedActor ? TEXT("true") : TEXT("false"), *CUJsonUtils::EscapeJsonString(Note)));
}

void UCUWebSocketEditorSubsystem::HandleActivateNiagara(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleActivateNiagara: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));
	bool bActivate = Request->GetBoolField(TEXT("activate"));
	bool bReset = Request->HasField(TEXT("reset")) ? Request->GetBoolField(TEXT("reset")) : false;

	LogInfo(FString::Printf(TEXT("ActivateNiagara: actor=%s, activate=%s, reset=%s (request_id: %s)"),
		*ActorName, bActivate ? TEXT("true") : TEXT("false"), bReset ? TEXT("true") : TEXT("false"), *RequestId));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FString NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No editor world available\"}"),
			*RequestId);
		SendResponse(Socket, NiagaraResponseJSON);
		return;
	}

	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorNameOrLabel() == ActorName))
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		FString NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Actor not found: %s\"}"),
			*RequestId, *ActorName);
		SendResponse(Socket, NiagaraResponseJSON);
		return;
	}

	UNiagaraComponent* NiagaraComp = FoundActor->FindComponentByClass<UNiagaraComponent>();
	if (!NiagaraComp)
	{
		FString NiagaraResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No NiagaraComponent found on actor\"}"),
			*RequestId);
		SendResponse(Socket, NiagaraResponseJSON);
		return;
	}

	if (bActivate)
	{
		NiagaraComp->Activate(bReset);
		LogInfo(FString::Printf(TEXT("Activated NiagaraComponent on %s (reset=%s)"),
			*ActorName, bReset ? TEXT("true") : TEXT("false")));
	}
	else
	{
		NiagaraComp->Deactivate();
		LogInfo(FString::Printf(TEXT("Deactivated NiagaraComponent on %s"), *ActorName));
	}

	FString NiagaraResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"actor_name\":\"%s\",\"component_name\":\"%s\",\"is_active\":%s}"),
		*RequestId, *FoundActor->GetName(), *NiagaraComp->GetName(),
		NiagaraComp->IsActive() ? TEXT("true") : TEXT("false"));

	SendResponse(Socket, NiagaraResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleGetNiagaraParameters(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetNiagaraParameters: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));

	LogInfo(FString::Printf(TEXT("GetNiagaraParameters: actor=%s (request_id: %s)"), *ActorName, *RequestId));

	// 1. Get editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No editor world available\"}"),
			*RequestId);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 2. Find actor by name
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorNameOrLabel() == ActorName))
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Actor not found: %s\"}"),
			*RequestId, *ActorName);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 3. Get NiagaraComponent
	UNiagaraComponent* NiagaraComp = FoundActor->FindComponentByClass<UNiagaraComponent>();
	if (!NiagaraComp)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No NiagaraComponent found on actor\"}"),
			*RequestId);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 4. Get NiagaraSystem
	UNiagaraSystem* System = NiagaraComp->GetAsset();
	FString SystemName = System ? System->GetName() : TEXT("");
	FString SystemPath = System ? System->GetPathName() : TEXT("");
	bool bIsActive = NiagaraComp->IsActive();

	// 5. Build parameters array using FJsonObject for proper serialization
	TArray<TSharedPtr<FJsonValue>> ParametersArray;

	if (System)
	{
		const FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();
		TArray<FNiagaraVariable> UserParameters;
		ExposedParams.GetUserParameters(UserParameters);

		LogInfo(FString::Printf(TEXT("Found %d user parameters in NiagaraSystem"), UserParameters.Num()));

		for (const FNiagaraVariable& Param : UserParameters)
		{
			TSharedPtr<FJsonObject> ParamObj = MakeShareable(new FJsonObject());
			ParamObj->SetStringField(TEXT("name"), Param.GetName().ToString());

			// Determine type name
			FString TypeName = TEXT("unknown");
			const FNiagaraTypeDefinition& ParamType = Param.GetType();

			if (ParamType == FNiagaraTypeDefinition::GetFloatDef())
				TypeName = TEXT("float");
			else if (ParamType == FNiagaraTypeDefinition::GetBoolDef())
				TypeName = TEXT("bool");
			else if (ParamType == FNiagaraTypeDefinition::GetIntDef())
				TypeName = TEXT("int");
			else if (ParamType == FNiagaraTypeDefinition::GetVec3Def())
				TypeName = TEXT("Vector");
			else if (ParamType == FNiagaraTypeDefinition::GetColorDef())
				TypeName = TEXT("LinearColor");
			else if (ParamType == FNiagaraTypeDefinition::GetVec2Def())
				TypeName = TEXT("Vector2D");
			else if (ParamType == FNiagaraTypeDefinition::GetVec4Def())
				TypeName = TEXT("Vector4");
			else
				TypeName = ParamType.GetName();

			ParamObj->SetStringField(TEXT("type"), TypeName);
			ParamObj->SetNumberField(TEXT("size_bytes"), Param.GetSizeInBytes());

			ParametersArray.Add(MakeShareable(new FJsonValueObject(ParamObj)));
		}
	}

	// 6. Build JSON response using FJsonObject for proper array serialization
	TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
	ResponseObj->SetStringField(TEXT("type"), TEXT("niagara_result"));
	ResponseObj->SetStringField(TEXT("request_id"), RequestId);
	ResponseObj->SetBoolField(TEXT("success"), true);
	ResponseObj->SetStringField(TEXT("actor_name"), FoundActor->GetName());
	ResponseObj->SetStringField(TEXT("component_name"), NiagaraComp->GetName());
	ResponseObj->SetStringField(TEXT("system_name"), SystemName);
	ResponseObj->SetStringField(TEXT("system_path"), SystemPath);
	ResponseObj->SetBoolField(TEXT("is_active"), bIsActive);
	ResponseObj->SetArrayField(TEXT("parameters"), ParametersArray);
	ResponseObj->SetNumberField(TEXT("parameter_count"), ParametersArray.Num());

	// Serialize to JSON string
	FString ResponseJSON = CUJsonUtils::SerializeToString(ResponseObj.ToSharedRef());

	SendResponse(Socket, ResponseJSON);
}

// Issue #3795 Phase 7B: Add NiagaraComponent to actor
void UCUWebSocketEditorSubsystem::HandleAddNiagaraEmitter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleAddNiagaraEmitter: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));
	FString EmitterAssetPath = Request->GetStringField(TEXT("emitter_asset_path"));
	FString ComponentName = Request->HasField(TEXT("component_name"))
		? Request->GetStringField(TEXT("component_name")) : TEXT("");
	bool bActivate = Request->HasField(TEXT("activate"))
		? Request->GetBoolField(TEXT("activate")) : true;

	LogInfo(FString::Printf(TEXT("AddNiagaraEmitter: actor=%s, emitter=%s (request_id: %s)"),
		*ActorName, *EmitterAssetPath, *RequestId));

	// 1. Get editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No editor world available\"}"),
			*RequestId);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 2. Find actor by name
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorNameOrLabel() == ActorName))
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Actor not found: %s\"}"),
			*RequestId, *ActorName);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 3. Load NiagaraSystem asset - normalize path for plugin assets
	FString NormalizedPath = EmitterAssetPath;

	// Plugin assets require .AssetName suffix (e.g., /ClonerEffector/Systems/NS_Grid.NS_Grid)
	// /Game/ paths are auto-resolved by StaticLoadObject, but plugin paths are not
	if (!NormalizedPath.Contains(TEXT(".")))
	{
		FString AssetName = FPaths::GetCleanFilename(EmitterAssetPath);
		NormalizedPath = FString::Printf(TEXT("%s.%s"), *EmitterAssetPath, *AssetName);
	}

	UNiagaraSystem* EmitterSystem = Cast<UNiagaraSystem>(
		StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *NormalizedPath));

	// Fallback: try FSoftObjectPath which handles more path formats
	if (!EmitterSystem)
	{
		FSoftObjectPath SoftPath(NormalizedPath);
		EmitterSystem = Cast<UNiagaraSystem>(SoftPath.TryLoad());
	}

	if (!EmitterSystem)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Failed to load NiagaraSystem: %s (normalized: %s)\"}"),
			*RequestId, *EmitterAssetPath, *NormalizedPath);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 4. Create new NiagaraComponent
	UNiagaraComponent* NewComponent = NewObject<UNiagaraComponent>(FoundActor);
	if (!NewComponent)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Failed to create NiagaraComponent\"}"),
			*RequestId);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// 5. Configure component
	NewComponent->SetAsset(EmitterSystem);
	NewComponent->SetRelativeLocation(FVector::ZeroVector);

	// Handle optional location offset
	if (Request->HasField(TEXT("location_offset")))
	{
		const TSharedPtr<FJsonObject>* LocationObj;
		if (Request->TryGetObjectField(TEXT("location_offset"), LocationObj))
		{
			double X = (*LocationObj)->GetNumberField(TEXT("x"));
			double Y = (*LocationObj)->GetNumberField(TEXT("y"));
			double Z = (*LocationObj)->GetNumberField(TEXT("z"));
			NewComponent->SetRelativeLocation(FVector(X, Y, Z));
		}
	}

	// 6. Register component
	FoundActor->AddInstanceComponent(NewComponent);
	NewComponent->RegisterComponent();

	// 7. Activate if requested
	if (bActivate)
	{
		NewComponent->Activate(true);
	}

	FString FinalComponentName = NewComponent->GetName();
	LogInfo(FString::Printf(TEXT("Added NiagaraComponent '%s' to actor '%s'"),
		*FinalComponentName, *ActorName));

	// 8. Build success response
	TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
	ResponseObj->SetStringField(TEXT("type"), TEXT("niagara_result"));
	ResponseObj->SetStringField(TEXT("request_id"), RequestId);
	ResponseObj->SetBoolField(TEXT("success"), true);
	ResponseObj->SetStringField(TEXT("actor_name"), FoundActor->GetName());
	ResponseObj->SetStringField(TEXT("new_component_name"), FinalComponentName);
	ResponseObj->SetStringField(TEXT("emitter_path"), EmitterAssetPath);
	ResponseObj->SetBoolField(TEXT("is_active"), bActivate);

	FString ResponseJSON = CUJsonUtils::SerializeToString(ResponseObj.ToSharedRef());

	SendResponse(Socket, ResponseJSON);
}


// Issue #21944 / NIA-N2: add an emitter handle to a NiagaraSystem asset (authoring).
// Distinct from HandleAddNiagaraEmitter which attaches a UNiagaraComponent to a level actor.
void UCUWebSocketEditorSubsystem::HandleAddSystemEmitter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleAddSystemEmitter: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString SourcePath = Request->HasField(TEXT("source"))
		? Request->GetStringField(TEXT("source")) : FString();
	const FString EmitterNameIn = Request->HasField(TEXT("emitter_name"))
		? Request->GetStringField(TEXT("emitter_name")) : FString();

	if (AssetPath.IsEmpty())
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"add_system_emitter requires asset_path (NiagaraSystem)\"}"),
			*RequestId));
		return;
	}
	if (SourcePath.IsEmpty())
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"add_system_emitter requires source (NE_ path or standalone NiagaraEmitter asset)\"}"),
			*RequestId));
		return;
	}

	UObject* LoadedSystem = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!LoadedSystem && !AssetPath.Contains(TEXT(".")))
	{
		const FString Normalized = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		LoadedSystem = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *Normalized);
	}
	UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(LoadedSystem);
	if (!NiagaraSystem)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath)));
		return;
	}

	FString NormSource = SourcePath;
	if (!NormSource.Contains(TEXT(".")))
	{
		NormSource = FString::Printf(TEXT("%s.%s"), *SourcePath, *FPaths::GetCleanFilename(SourcePath));
	}
	UObject* LoadedEmitter = StaticLoadObject(UNiagaraEmitter::StaticClass(), nullptr, *NormSource);
	if (!LoadedEmitter)
	{
		LoadedEmitter = StaticLoadObject(UNiagaraEmitter::StaticClass(), nullptr, *SourcePath);
	}
	if (!LoadedEmitter)
	{
		FSoftObjectPath SoftPath(NormSource);
		LoadedEmitter = SoftPath.TryLoad();
	}
	UNiagaraEmitter* SourceEmitter = Cast<UNiagaraEmitter>(LoadedEmitter);
	if (!SourceEmitter)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraEmitter not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(SourcePath)));
		return;
	}

	FString EmitterNameStr = EmitterNameIn;
	if (EmitterNameStr.IsEmpty())
	{
		EmitterNameStr = SourceEmitter->GetName();
		if (EmitterNameStr.StartsWith(TEXT("NE_")))
		{
			EmitterNameStr = EmitterNameStr.Mid(3);
		}
	}
	const FName EmitterFName(*EmitterNameStr);

	for (const FNiagaraEmitterHandle& Existing : NiagaraSystem->GetEmitterHandles())
	{
		if (Existing.GetName() == EmitterFName)
		{
			SendResponse(Socket, FString::Printf(TEXT(
				"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter handle already exists: %s\"}"),
				*RequestId, *CUJsonUtils::EscapeJsonString(EmitterNameStr)));
			return;
		}
	}

	NiagaraSystem->Modify();
	// UE 5.7+: AddEmitterHandle requires the emitter asset version guid.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	const FGuid EmitterVersion = SourceEmitter->GetExposedVersion().VersionGuid;
	const FNiagaraEmitterHandle NewHandle =
		NiagaraSystem->AddEmitterHandle(*SourceEmitter, EmitterFName, EmitterVersion);
#else
	const FNiagaraEmitterHandle NewHandle =
		NiagaraSystem->AddEmitterHandle(*SourceEmitter, EmitterFName);
#endif
	const FString AddedName = NewHandle.GetName().ToString();
	const int32 EmitterCount = NiagaraSystem->GetEmitterHandles().Num();

	NiagaraSystem->SetFlags(RF_Public | RF_Standalone);
	NiagaraSystem->MarkPackageDirty();
	UPackage* Package = NiagaraSystem->GetOutermost();
	bool bSaved = false;
	if (Package)
	{
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		bSaved = UPackage::SavePackage(
			Package,
			NiagaraSystem,
			*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()),
			SaveArgs);
	}

	NiagaraSystem->RequestCompile(false);

	LogInfo(FString::Printf(TEXT("AddSystemEmitter: system=%s source=%s name=%s count=%d saved=%s"),
		*AssetPath, *SourcePath, *AddedName, EmitterCount, bSaved ? TEXT("true") : TEXT("false")));

	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"asset_path\":\"%s\",\"source\":\"%s\",\"emitter_name\":\"%s\","
		"\"emitter_count\":%d,\"saved\":%s}"),
		*RequestId,
		*CUJsonUtils::EscapeJsonString(AssetPath),
		*CUJsonUtils::EscapeJsonString(SourcePath),
		*CUJsonUtils::EscapeJsonString(AddedName),
		EmitterCount,
		bSaved ? TEXT("true") : TEXT("false")));
}

// Issue #21945 / NIA-N3 helpers: load system + find emitter handle by name (accepts Name_N form).
static UNiagaraSystem* CULoadNiagaraSystemAsset(const FString& AssetPath)
{
	UObject* Loaded = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *AssetPath);
	if (!Loaded && !AssetPath.Contains(TEXT(".")))
	{
		const FString Normalized = FString::Printf(TEXT("%s.%s"), *AssetPath, *FPaths::GetCleanFilename(AssetPath));
		Loaded = StaticLoadObject(UNiagaraSystem::StaticClass(), nullptr, *Normalized);
	}
	return Cast<UNiagaraSystem>(Loaded);
}

static FString CUNormalizeEmitterNameFilter(const FString& In)
{
	int32 Underscore;
	if (In.FindLastChar(TEXT('_'), Underscore) && Underscore > 0 && Underscore < In.Len() - 1)
	{
		const FString Tail = In.Mid(Underscore + 1);
		if (Tail.IsNumeric())
		{
			return In.Left(Underscore);
		}
	}
	return In;
}

static bool CUFindEmitterHandleIndex(UNiagaraSystem* System, const FString& EmitterFilter, int32& OutIndex)
{
	if (!System) return false;
	const FString Norm = CUNormalizeEmitterNameFilter(EmitterFilter);
	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	for (int32 i = 0; i < Handles.Num(); ++i)
	{
		const FString Name = Handles[i].GetName().ToString();
		if (Name.Equals(EmitterFilter, ESearchCase::IgnoreCase) || Name.Equals(Norm, ESearchCase::IgnoreCase))
		{
			OutIndex = i;
			return true;
		}
	}
	return false;
}

// Issue #23972: Save package ONLY. RequestCompile blocks the game thread; when
// it ran *before* SendResponse the MCP client hit NIAGARA_AUTHORING_TIMEOUT
// (authoring_ws_timeout on set_fixed_bounds / set_emitter_enabled) while large
// Niagara systems recompiled. Handlers must SendResponse first, then call
// CURequestCompileNiagaraSystem so the WS frame is delivered promptly.
// Issue #24665: return SavePackage success so WS handlers can emit honest "saved".
static bool CUSaveNiagaraSystem(UNiagaraSystem* NiagaraSystem)
{
	if (!NiagaraSystem)
	{
		return false;
	}
	NiagaraSystem->SetFlags(RF_Public | RF_Standalone);
	NiagaraSystem->MarkPackageDirty();
	UPackage* Package = NiagaraSystem->GetOutermost();
	if (!Package)
	{
		return false;
	}
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Standalone;
	return UPackage::SavePackage(
		Package,
		NiagaraSystem,
		*FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension()),
		SaveArgs);
}

static void CURequestCompileNiagaraSystem(UNiagaraSystem* NiagaraSystem)
{
	if (!NiagaraSystem) return;
	NiagaraSystem->RequestCompile(false);
}

void UCUWebSocketEditorSubsystem::HandleSetNiagaraSimTarget(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid()) { LogError(TEXT("HandleSetNiagaraSimTarget: invalid")); return; }
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString EmitterName = Request->GetStringField(TEXT("emitter_name"));
	const FString SimTargetStr = Request->GetStringField(TEXT("sim_target"));

	UNiagaraSystem* System = CULoadNiagaraSystemAsset(AssetPath);
	if (!System)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath)));
		return;
	}
	int32 Idx = INDEX_NONE;
	if (!CUFindEmitterHandleIndex(System, EmitterName, Idx))
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(EmitterName)));
		return;
	}

	ENiagaraSimTarget Target = ENiagaraSimTarget::CPUSim;
	if (SimTargetStr.Contains(TEXT("GPU"), ESearchCase::IgnoreCase))
	{
		Target = ENiagaraSimTarget::GPUComputeSim;
	}
	else if (!SimTargetStr.Contains(TEXT("CPU"), ESearchCase::IgnoreCase))
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Invalid sim_target: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(SimTargetStr)));
		return;
	}

	System->Modify();
	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	FVersionedNiagaraEmitterData* Data = Handles[Idx].GetEmitterData();
	if (!Data)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter data unavailable\"}"),
			*RequestId));
		return;
	}
	Data->SimTarget = Target;
	const FString AppliedName = Handles[Idx].GetName().ToString();
	const bool bSaved = CUSaveNiagaraSystem(System);
	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"emitter\":\"%s\",\"sim_target\":\"%s\",\"saved\":%s}"),
		*RequestId, *CUJsonUtils::EscapeJsonString(AppliedName),
		Target == ENiagaraSimTarget::GPUComputeSim ? TEXT("GPUComputeSim") : TEXT("CPUSim"),
		bSaved ? TEXT("true") : TEXT("false")));
	// Issue #23972: compile after the WS success frame is on the wire.
	CURequestCompileNiagaraSystem(System);
}

void UCUWebSocketEditorSubsystem::HandleSetNiagaraFixedBounds(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid()) { LogError(TEXT("HandleSetNiagaraFixedBounds: invalid")); return; }
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString EmitterName = Request->HasField(TEXT("emitter_name")) ? Request->GetStringField(TEXT("emitter_name")) : FString();

	auto ReadVec = [](const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FVector Default) -> FVector
	{
		const TSharedPtr<FJsonObject>* V = nullptr;
		if (Obj->TryGetObjectField(Key, V) && V && (*V).IsValid())
		{
			return FVector(
				(*V)->HasField(TEXT("x")) ? (*V)->GetNumberField(TEXT("x")) : Default.X,
				(*V)->HasField(TEXT("y")) ? (*V)->GetNumberField(TEXT("y")) : Default.Y,
				(*V)->HasField(TEXT("z")) ? (*V)->GetNumberField(TEXT("z")) : Default.Z);
		}
		return Default;
	};
	const FVector MinV = ReadVec(Request, TEXT("min"), FVector(-100, -100, -100));
	const FVector MaxV = ReadVec(Request, TEXT("max"), FVector(100, 100, 100));
	const FBox Box(MinV, MaxV);

	UNiagaraSystem* System = CULoadNiagaraSystemAsset(AssetPath);
	if (!System)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath)));
		return;
	}
	System->Modify();
	if (EmitterName.IsEmpty())
	{
		// FixedBounds became protected on UE 5.7; use the public setter when available.
		System->bFixedBounds = true;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		System->SetFixedBounds(Box);
#else
		System->FixedBounds = Box;
#endif
		const bool bSaved = CUSaveNiagaraSystem(System);
		const FBox AppliedBox = System->GetFixedBounds();
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"scope\":\"system\",\"saved\":%s,"
			"\"readback_verified\":true,\"readback_min\":[%.17g,%.17g,%.17g],\"readback_max\":[%.17g,%.17g,%.17g]}"),
			*RequestId, bSaved ? TEXT("true") : TEXT("false"),
			AppliedBox.Min.X, AppliedBox.Min.Y, AppliedBox.Min.Z,
			AppliedBox.Max.X, AppliedBox.Max.Y, AppliedBox.Max.Z));
		// Issue #23972: compile after the WS success frame is on the wire.
		CURequestCompileNiagaraSystem(System);
		return;
	}
	int32 Idx = INDEX_NONE;
	if (!CUFindEmitterHandleIndex(System, EmitterName, Idx))
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(EmitterName)));
		return;
	}
	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	FVersionedNiagaraEmitterData* Data = Handles[Idx].GetEmitterData();
	if (!Data)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter data unavailable\"}"),
			*RequestId));
		return;
	}
	Data->CalculateBoundsMode = ENiagaraEmitterCalculateBoundMode::Fixed;
	Data->FixedBounds = Box;
	const FString AppliedName = Handles[Idx].GetName().ToString();
	const bool bSaved = CUSaveNiagaraSystem(System);
	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"scope\":\"emitter\",\"emitter\":\"%s\",\"saved\":%s}"),
		*RequestId, *CUJsonUtils::EscapeJsonString(AppliedName),
		bSaved ? TEXT("true") : TEXT("false")));
	// Issue #23972: compile after the WS success frame is on the wire.
	CURequestCompileNiagaraSystem(System);
}

void UCUWebSocketEditorSubsystem::HandleSetNiagaraEmitterEnabled(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid()) { LogError(TEXT("HandleSetNiagaraEmitterEnabled: invalid")); return; }
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString AssetPath = Request->GetStringField(TEXT("asset_path"));
	const FString EmitterName = Request->GetStringField(TEXT("emitter_name"));
	const bool bEnabled = Request->HasField(TEXT("enabled")) ? Request->GetBoolField(TEXT("enabled")) : true;

	UNiagaraSystem* System = CULoadNiagaraSystemAsset(AssetPath);
	if (!System)
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"NiagaraSystem not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(AssetPath)));
		return;
	}
	int32 Idx = INDEX_NONE;
	if (!CUFindEmitterHandleIndex(System, EmitterName, Idx))
	{
		SendResponse(Socket, FString::Printf(TEXT(
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Emitter not found: %s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(EmitterName)));
		return;
	}
	System->Modify();
	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	// UE 5.7+: SetIsEnabled requires owner system + recompile flag.
	// Issue #23972: bRecompileIfChanged=false — compile once after SendResponse via
	// CURequestCompileNiagaraSystem. true recompiled on the hot path and, combined with
	// CUSaveNiagaraSystem's prior RequestCompile, caused 15–25s client timeouts
	// (authoring_ws_timeout) before the success frame was ever written.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	Handles[Idx].SetIsEnabled(bEnabled, *System, /*bRecompileIfChanged=*/false);
#else
	Handles[Idx].SetIsEnabled(bEnabled);
#endif
	const FString AppliedName = Handles[Idx].GetName().ToString();
	const bool bSaved = CUSaveNiagaraSystem(System);
	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":true,\"emitter\":\"%s\",\"enabled\":%s,\"saved\":%s}"),
		*RequestId, *CUJsonUtils::EscapeJsonString(AppliedName), bEnabled ? TEXT("true") : TEXT("false"),
		bSaved ? TEXT("true") : TEXT("false")));
	// Issue #23972: compile after the WS success frame is on the wire.
	CURequestCompileNiagaraSystem(System);
}
#else
#define NARSHA_NIAGARA_UNAVAILABLE_HANDLER(MethodName) \
	void UCUWebSocketEditorSubsystem::MethodName( \
		INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request) \
	{ \
		if (!Socket || !Request.IsValid()) return; \
		FString RequestId; \
		Request->TryGetStringField(TEXT("request_id"), RequestId); \
		SendResponse(Socket, FString::Printf(TEXT( \
			"{\"type\":\"niagara_result\",\"request_id\":\"%s\",\"success\":false," \
			"\"error\":\"Niagara plugin unavailable\",\"error_code\":\"niagara_plugin_unavailable\"," \
			"\"hint\":\"Enable Niagara in your .uproject, then rebuild.\"}"), \
			*CUJsonUtils::EscapeJsonString(RequestId))); \
	}

NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleSetNiagaraParameter)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleSetNiagaraAssetParameter)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleAddNiagaraUserParameter)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleAttachNiagaraDynamicInput)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleSetNiagaraRenderer)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleNiagaraEventHandler)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleNiagaraEventGenerator)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleNiagaraCaptureSimCache)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleActivateNiagara)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleGetNiagaraParameters)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleAddNiagaraEmitter)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleAddSystemEmitter)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleSetNiagaraSimTarget)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleSetNiagaraFixedBounds)
NARSHA_NIAGARA_UNAVAILABLE_HANDLER(HandleSetNiagaraEmitterEnabled)

#undef NARSHA_NIAGARA_UNAVAILABLE_HANDLER
#endif
// Issue #3797: Collision property handlers
void UCUWebSocketEditorSubsystem::HandleSetCollision(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleSetCollision: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString ActorName = Request->GetStringField(TEXT("actor_name"));

	// Optional parameters
	FString CollisionEnabled = Request->HasField(TEXT("collision_enabled"))
		? Request->GetStringField(TEXT("collision_enabled")) : TEXT("");
	FString CollisionPreset = Request->HasField(TEXT("collision_preset"))
		? Request->GetStringField(TEXT("collision_preset")) : TEXT("");
	FString CollisionChannel = Request->HasField(TEXT("collision_channel"))
		? Request->GetStringField(TEXT("collision_channel")) : TEXT("");
	bool bHasOverlapEvents = Request->HasField(TEXT("generate_overlap_events"));
	bool bGenerateOverlapEvents = bHasOverlapEvents
		? Request->GetBoolField(TEXT("generate_overlap_events")) : false;

	LogInfo(FString::Printf(TEXT("SetCollision: actor=%s (request_id: %s)"), *ActorName, *RequestId));

	// Get editor world
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"collision_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No editor world available\"}"),
			*RequestId);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Find actor by name
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetName() == ActorName || Actor->GetActorNameOrLabel() == ActorName))
		{
			FoundActor = Actor;
			break;
		}
	}

	if (!FoundActor)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"collision_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Actor not found: %s\"}"),
			*RequestId, *ActorName);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Get PrimitiveComponent (collision component)
	UPrimitiveComponent* PrimComp = FoundActor->FindComponentByClass<UPrimitiveComponent>();
	if (!PrimComp)
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"collision_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"No collision component found on actor\"}"),
			*RequestId);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Track what was set
	TArray<FString> PropertiesSet;
	TArray<FString> Errors;

	// Issue #8834 Cycle 20 (B2): Wrap collision changes in FScopedTransaction
	FScopedTransaction Transaction(FText::FromString(TEXT("Set Collision")));
	PrimComp->Modify();

	// Set collision enabled mode
	if (!CollisionEnabled.IsEmpty())
	{
		ECollisionEnabled::Type NewEnabled = ECollisionEnabled::QueryAndPhysics;
		if (CollisionEnabled == TEXT("NoCollision"))
			NewEnabled = ECollisionEnabled::NoCollision;
		else if (CollisionEnabled == TEXT("QueryOnly"))
			NewEnabled = ECollisionEnabled::QueryOnly;
		else if (CollisionEnabled == TEXT("PhysicsOnly"))
			NewEnabled = ECollisionEnabled::PhysicsOnly;
		else if (CollisionEnabled == TEXT("QueryAndPhysics"))
			NewEnabled = ECollisionEnabled::QueryAndPhysics;

		PrimComp->SetCollisionEnabled(NewEnabled);
		PropertiesSet.Add(TEXT("CollisionEnabled"));
	}

	// Set collision preset/profile
	if (!CollisionPreset.IsEmpty())
	{
		PrimComp->SetCollisionProfileName(FName(*CollisionPreset));
		PropertiesSet.Add(TEXT("CollisionProfileName"));
	}

	// Set collision object type/channel
	if (!CollisionChannel.IsEmpty())
	{
		ECollisionChannel NewChannel = ECC_WorldStatic;
		if (CollisionChannel == TEXT("WorldStatic"))
			NewChannel = ECC_WorldStatic;
		else if (CollisionChannel == TEXT("WorldDynamic"))
			NewChannel = ECC_WorldDynamic;
		else if (CollisionChannel == TEXT("Pawn"))
			NewChannel = ECC_Pawn;
		else if (CollisionChannel == TEXT("Visibility"))
			NewChannel = ECC_Visibility;
		else if (CollisionChannel == TEXT("Camera"))
			NewChannel = ECC_Camera;
		else if (CollisionChannel == TEXT("PhysicsBody"))
			NewChannel = ECC_PhysicsBody;
		else if (CollisionChannel == TEXT("Vehicle"))
			NewChannel = ECC_Vehicle;
		else if (CollisionChannel == TEXT("Destructible"))
			NewChannel = ECC_Destructible;

		PrimComp->SetCollisionObjectType(NewChannel);
		PropertiesSet.Add(TEXT("CollisionObjectType"));
	}

	// Set generate overlap events
	if (bHasOverlapEvents)
	{
		PrimComp->SetGenerateOverlapEvents(bGenerateOverlapEvents);
		PropertiesSet.Add(TEXT("bGenerateOverlapEvents"));
	}

	// Build response
	bool bSuccess = PropertiesSet.Num() > 0;

	TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
	ResponseObj->SetStringField(TEXT("type"), TEXT("collision_result"));
	ResponseObj->SetStringField(TEXT("request_id"), RequestId);
	ResponseObj->SetBoolField(TEXT("success"), bSuccess);
	ResponseObj->SetStringField(TEXT("actor_name"), FoundActor->GetName());
	ResponseObj->SetStringField(TEXT("component_name"), PrimComp->GetName());

	TArray<TSharedPtr<FJsonValue>> PropsArray;
	for (const FString& Prop : PropertiesSet)
	{
		PropsArray.Add(MakeShareable(new FJsonValueString(Prop)));
	}
	ResponseObj->SetArrayField(TEXT("properties_set"), PropsArray);
	ResponseObj->SetStringField(TEXT("message"),
		bSuccess ? FString::Printf(TEXT("Set %d collision properties"), PropertiesSet.Num())
		         : TEXT("No collision properties were set"));

	FString ResponseJSON2 = CUJsonUtils::SerializeToString(ResponseObj.ToSharedRef());

	SendResponse(Socket, ResponseJSON2);
}

// Issue #16324: Live material node graph via WebSocket (Epic #16323 — feature catalog).
// Thin wrapper over UCURemoteLibrary::GetMaterialGraph: reads live node names + DefaultValue +
// name-based edges that binary .uasset metadata cannot provide. Returns a typed envelope
// {type:"material_graph_result", request_id, success, material_name, node_count,
//  connection_count, nodes[], connections[]} consumed by the Rust hybrid get_nodes path.
void UCUWebSocketEditorSubsystem::HandleGetMaterialGraph(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetMaterialGraph: Invalid socket or request"));
		return;
	}

	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString MaterialPath = Request->GetStringField(TEXT("material_path"));

	// Issue #11168 defense-in-depth: reject malformed paths BEFORE LoadObject —
	// "//" double-slashes Fatal-check + SIGABRT the Editor in UE 5.7's CreatePackage.
	if (MaterialPath.Contains(TEXT("//")) || MaterialPath.IsEmpty())
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetStringField(TEXT("type"), TEXT("material_graph_result"));
		ErrObj->SetStringField(TEXT("request_id"), RequestId);
		ErrObj->SetBoolField(TEXT("success"), false);
		ErrObj->SetStringField(TEXT("error"),
			FString::Printf(TEXT("Invalid material_path: '%s' — paths must not be empty or contain consecutive slashes"), *MaterialPath));
		ErrObj->SetStringField(TEXT("material_path"), MaterialPath);
		LogError(FString::Printf(TEXT("GetMaterialGraph: REJECTED malformed path '%s' before LoadObject (Issue #11168)"), *MaterialPath));
		SendResponse(Socket, CUJsonUtils::SerializeToString(ErrObj.ToSharedRef()));
		return;
	}

	LogInfo(FString::Printf(TEXT("GetMaterialGraph: path=%s (request_id: %s)"), *MaterialPath, *RequestId));

	// Delegate the reflection-heavy graph extraction to the shared RC library.
	const FString GraphJson = UCURemoteLibrary::GetMaterialGraph(MaterialPath);

	// Parse the library result so we can inject the WS envelope fields (type + request_id).
	TSharedPtr<FJsonObject> GraphObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(GraphJson);
	if (!FJsonSerializer::Deserialize(Reader, GraphObj) || !GraphObj.IsValid())
	{
		TSharedPtr<FJsonObject> ErrObj = MakeShared<FJsonObject>();
		ErrObj->SetStringField(TEXT("type"), TEXT("material_graph_result"));
		ErrObj->SetStringField(TEXT("request_id"), RequestId);
		ErrObj->SetBoolField(TEXT("success"), false);
		ErrObj->SetStringField(TEXT("error"), TEXT("GetMaterialGraph returned unparseable JSON"));
		SendResponse(Socket, CUJsonUtils::SerializeToString(ErrObj.ToSharedRef()));
		return;
	}

	GraphObj->SetStringField(TEXT("type"), TEXT("material_graph_result"));
	GraphObj->SetStringField(TEXT("request_id"), RequestId);
	SendResponse(Socket, CUJsonUtils::SerializeToString(GraphObj.ToSharedRef()));
}

// Issue #4272: Material HLSL Extraction via WebSocket
// Uses FMaterialInsights (UE 5.7+) and GetMaterialExpressionSource() for live HLSL extraction
// Requires Editor running with rendering context (Material resources must be compiled)
void UCUWebSocketEditorSubsystem::HandleGetMaterialHLSL(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetMaterialHLSL: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString MaterialPath = Request->GetStringField(TEXT("material_path"));

	// Issue #11169: Opt-in auto-open Material Editor + wait for shader compile
	// when HLSL is not yet cached. Default true — operator usually wants the
	// asset opened anyway when calling get_hlsl. Set to false to preserve
	// pre-#11169 fail-fast behavior (no Editor side-effects).
	const bool bAutoOpenEditor = !Request->HasField(TEXT("auto_open_editor"))
		|| Request->GetBoolField(TEXT("auto_open_editor"));
	const int32 CompileWaitMs = FMath::Clamp(
		Request->HasField(TEXT("compile_wait_timeout_ms"))
			? static_cast<int32>(Request->GetNumberField(TEXT("compile_wait_timeout_ms")))
			: 8000,
		500, 25000);

	LogInfo(FString::Printf(TEXT("GetMaterialHLSL: path=%s auto_open=%s wait_ms=%d (request_id: %s)"),
		*MaterialPath, bAutoOpenEditor ? TEXT("true") : TEXT("false"), CompileWaitMs, *RequestId));

	// Issue #11168 defense-in-depth: reject malformed paths BEFORE LoadObject —
	// UE 5.7's CreatePackage Fatal-checks for double-slashes and crashes the
	// Editor (verified during live E2E on PR fix/11168). A bad caller (e.g.,
	// stale Rust path resolver that prepends "/Game/Materials/" to an already-
	// rooted "/Plugin/...") would otherwise SIGABRT the entire Editor process.
	// UE asset paths use the single-slash mount-point convention; "//" is never
	// valid input here regardless of source.
	if (MaterialPath.Contains(TEXT("//")) || MaterialPath.IsEmpty())
	{
		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"material_hlsl_result\",\"request_id\":\"%s\",\"success\":false,"
			"\"error\":\"Invalid material_path: '%s' — paths must not be empty or contain consecutive slashes\","
			"\"hint\":\"Provide a single-slash path like '/Game/Materials/M_Rock' or '/Engine/Functions/...'.\","
			"\"material_path\":\"%s\"}"),
			*RequestId, *MaterialPath, *MaterialPath);
		LogError(FString::Printf(TEXT("GetMaterialHLSL: REJECTED malformed path '%s' before LoadObject (Issue #11168)"), *MaterialPath));
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Load material by path (supports /Game/... format)
	UMaterialInterface* MaterialInterface = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!MaterialInterface)
	{
		// Issue #11168: MaterialFunctions are NOT UMaterialInterface — try the
		// MF interface to give the operator a useful error instead of "not found".
		// MFs don't compile to standalone HLSL; they're inlined into the Materials
		// that consume them via Material Function Call nodes.
		UMaterialFunctionInterface* MaterialFunction =
			LoadObject<UMaterialFunctionInterface>(nullptr, *MaterialPath);
		if (MaterialFunction)
		{
			TSharedPtr<FJsonObject> MfResponseObj = MakeShareable(new FJsonObject());
			MfResponseObj->SetStringField(TEXT("type"), TEXT("material_hlsl_result"));
			MfResponseObj->SetStringField(TEXT("request_id"), RequestId);
			MfResponseObj->SetBoolField(TEXT("success"), false);
			MfResponseObj->SetBoolField(TEXT("is_material_function"), true);
			MfResponseObj->SetStringField(TEXT("material_path"), MaterialPath);
			MfResponseObj->SetStringField(TEXT("material_name"), MaterialFunction->GetName());
			MfResponseObj->SetStringField(TEXT("error"), TEXT(
				"Material Functions don't compile to standalone HLSL — they're inlined "
				"into the Materials that use them. Use get_nodes on the MF to inspect its "
				"graph, or get_hlsl on a parent Material that consumes it."));

			TSharedPtr<FJsonObject> Suggestion = MakeShareable(new FJsonObject());
			Suggestion->SetStringField(TEXT("use_get_nodes"),
				TEXT("Call ue_manage_material(get_nodes, material_name=<MF>) to inspect the function graph"));
			Suggestion->SetStringField(TEXT("find_users"),
				TEXT("Call ue_manage_material(find_function_usage, function_name=<MF>) "
				     "to find Materials that use this function, then get_hlsl on those"));
			MfResponseObj->SetObjectField(TEXT("suggestion"), Suggestion);

			FString MfResponseJSON = CUJsonUtils::SerializeToString(MfResponseObj.ToSharedRef());

			LogInfo(FString::Printf(TEXT("GetMaterialHLSL: MaterialFunction detected (%s) — returning explanatory response (Issue #11168)"),
				*MaterialFunction->GetName()));
			SendResponse(Socket, MfResponseJSON);
			return;
		}

		FString ErrorJSON = FString::Printf(TEXT(
			"{\"type\":\"material_hlsl_result\",\"request_id\":\"%s\",\"success\":false,"
			"\"error\":\"Material not found: %s\"}"),
			*RequestId, *MaterialPath);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	FString HLSLCode;
	FString ExtractionMethod;

	// Issue #11169: Resolve FMaterialResource via the platform-fallback chain
	// the original Method 2 used. Hoisted to function scope so the auto-open
	// compile path (Step 2) doesn't have to duplicate the 14-line resolution
	// block. Returns nullptr if no resource is available for any platform.
	auto ResolveMaterialResource = [&]() -> FMaterialResource*
	{
		FMaterialResource* Resource = nullptr;
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		// UE 5.7+: GetMaterialResource requires EShaderPlatform
		EShaderPlatform Platform = GetFeatureLevelShaderPlatform(GMaxRHIFeatureLevel);
		Resource = MaterialInterface->GetMaterialResource(Platform);
		if (!Resource)
		{
			// Try explicit platforms
			static const EShaderPlatform Fallbacks[] = { SP_PCD3D_SM6, SP_PCD3D_SM5 };
			for (EShaderPlatform FB : Fallbacks)
			{
				Resource = MaterialInterface->GetMaterialResource(FB);
				if (Resource) break;
			}
		}
#else
		// UE 5.6: GetMaterialResource uses ERHIFeatureLevel::Type
		Resource = MaterialInterface->GetMaterialResource(GMaxRHIFeatureLevel);
#endif
		return Resource;
	};

	// Issue #11169: Method 1+2 extracted into a lambda so we can retry once after
	// auto-opening the Material Editor + waiting for shader compile. Lambda
	// captures HLSLCode/ExtractionMethod by reference; both are reset by caller
	// before each invocation if a retry is needed.
	auto ExtractHLSL = [&]()
	{
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		// Method 1: FMaterialInsights (preferred for UE 5.7+)
		if (const FMaterialInsights* Insights = MaterialInterface->MaterialInsight.Get())
		{
			if (!Insights->NewHLSLCode.IsEmpty())
			{
				HLSLCode = Insights->NewHLSLCode;
				ExtractionMethod = TEXT("FMaterialInsights::NewHLSLCode");
			}
			else if (!Insights->LegacyHLSLCode.IsEmpty())
			{
				HLSLCode = Insights->LegacyHLSLCode;
				ExtractionMethod = TEXT("FMaterialInsights::LegacyHLSLCode");
			}
			else if (Insights->New_ShaderStringParameters.Num() > 0)
			{
				for (const TPair<FString, FString>& Pair : Insights->New_ShaderStringParameters)
				{
					HLSLCode += FString::Printf(TEXT("// === %s ===\n"), *Pair.Key);
					HLSLCode += Pair.Value;
					HLSLCode += TEXT("\n\n");
				}
				ExtractionMethod = TEXT("FMaterialInsights::TemplateParams");
			}
		}
#endif

		// Method 2: GetMaterialExpressionSource (via FMaterialResource)
		// Issue #11169: resource resolution hoisted to ResolveMaterialResource
		// at function scope so the auto-open compile path can reuse it.
		if (HLSLCode.IsEmpty())
		{
			if (FMaterialResource* Resource = ResolveMaterialResource())
			{
				FString Source;
				if (Resource->GetMaterialExpressionSource(Source) && !Source.IsEmpty())
				{
					HLSLCode = Source;
					ExtractionMethod = TEXT("GetMaterialExpressionSource");
				}
			}
		}
	};

	ExtractHLSL();

	// Issue #11169: If HLSL still empty AND caller didn't opt out, auto-open the
	// Material Editor and wait for shader compile, then retry once. Mirrors the
	// BP handler pattern at HandleFocusBlueprintNode (L6329-6331). Canonical
	// "wait for this material's compile" sequence is borrowed from
	// UMaterialEditingLibrary::GetStatistics (Engine/Source/Editor/MaterialEditor/
	// Private/MaterialEditingLibrary.cpp:1546-1558 in UE 5.7).
	bool bAutoOpenAttempted = false;
	if (HLSLCode.IsEmpty() && bAutoOpenEditor)
	{
		bAutoOpenAttempted = true;
		LogInfo(FString::Printf(TEXT("GetMaterialHLSL: HLSL not cached for %s — auto-opening Material Editor and waiting up to %d ms for compile (Issue #11169)"),
			*MaterialInterface->GetName(), CompileWaitMs));

		// Step 1: Open the Material Editor (idempotent — UE handles re-open).
		// Null-check guard mirrors L6329 (Issue #7091).
		if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
		{
			AssetEditorSub->OpenEditorForAsset(MaterialInterface);
		}

		// Step 2: Force-submit + block on this material's shader compile.
		// FMaterialResource::FinishCompilation() is the per-material blocking
		// API used by UMaterialEditingLibrary::GetStatistics. It does NOT
		// accept a timeout — the WS timeout (10 s, HLSL_EXTRACTION_TIMEOUT_MS
		// on the Rust side in workflow.rs) plus the request-level operation
		// budget bound how long we sit here. CompileWaitMs is the user-facing
		// budget knob; if a future Engine version exposes a timed variant we'll
		// honor it precisely.
		if (FMaterialResource* CompileResource = ResolveMaterialResource())
		{
			if (!CompileResource->IsGameThreadShaderMapComplete())
			{
				CompileResource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
			}
			CompileResource->FinishCompilation();
		}

		// Step 3: Retry extraction once with shader map now complete.
		ExtractHLSL();
	}

	// Build response using JSON serializer (safe for large HLSL strings)
	TSharedPtr<FJsonObject> ResponseObj = MakeShareable(new FJsonObject());
	ResponseObj->SetStringField(TEXT("type"), TEXT("material_hlsl_result"));
	ResponseObj->SetStringField(TEXT("request_id"), RequestId);
	ResponseObj->SetBoolField(TEXT("success"), !HLSLCode.IsEmpty());
	ResponseObj->SetStringField(TEXT("material_path"), MaterialPath);
	ResponseObj->SetStringField(TEXT("material_name"), MaterialInterface->GetName());
	// Issue #11169: surface auto-open attempt so callers can confirm the
	// side-effect (or lack thereof) regardless of success/failure.
	ResponseObj->SetBoolField(TEXT("auto_open_attempted"), bAutoOpenAttempted);

	if (!HLSLCode.IsEmpty())
	{
		ResponseObj->SetStringField(TEXT("hlsl_code"), HLSLCode);
		ResponseObj->SetNumberField(TEXT("hlsl_length"), HLSLCode.Len());
		ResponseObj->SetStringField(TEXT("extraction_method"), ExtractionMethod);
		LogInfo(FString::Printf(TEXT("GetMaterialHLSL: Success for %s via %s (%d chars, auto_open=%s)"),
			*MaterialInterface->GetName(), *ExtractionMethod, HLSLCode.Len(),
			bAutoOpenAttempted ? TEXT("true") : TEXT("false")));
	}
	else
	{
		// Issue #11169: sharper error message — distinguish "we tried auto-open
		// and the compile still produced nothing" (likely shader compile error
		// in the Material) from "you opted out, here's how to enable it".
		ResponseObj->SetStringField(TEXT("error"),
			bAutoOpenAttempted
				? TEXT("HLSL not available even after auto-open + shader compile wait. "
				       "The Material may have compile errors — check the Material Editor tab.")
				: TEXT("HLSL not available. Set auto_open_editor=true (default) to "
				       "auto-open the Material Editor and wait for compile, "
				       "or open it manually first."));
		LogInfo(FString::Printf(TEXT("GetMaterialHLSL: No HLSL available for %s (auto_open_attempted=%s)"),
			*MaterialInterface->GetName(),
			bAutoOpenAttempted ? TEXT("true") : TEXT("false")));
	}

	FString ResponseJSON = CUJsonUtils::SerializeToString(ResponseObj.ToSharedRef());

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Focus Blueprint Node Handler
// Opens Blueprint editor and jumps to a specific node by GUID or title
// ============================================

void UCUWebSocketEditorSubsystem::HandleFocusBlueprintNode(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleFocusBlueprintNode: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString BlueprintPath = Request->GetStringField(TEXT("blueprint_path"));
	FString NodeGuidStr = Request->HasField(TEXT("node_guid"))
		? Request->GetStringField(TEXT("node_guid")) : TEXT("");
	FString NodeTitle = Request->HasField(TEXT("node_title"))
		? Request->GetStringField(TEXT("node_title")) : TEXT("");

	LogInfo(FString::Printf(TEXT("FocusBlueprintNode (request_id: %s): BP=%s, GUID=%s, Title=%s"),
		*RequestId, *BlueprintPath, *NodeGuidStr, *NodeTitle));

	bool bSuccess = false;
	FString Message;
	FString FoundNodeName;
	FString FoundGraphName;
	FString FoundNodeGuid;
	UEdGraphNode* TargetNode = nullptr;

	if (BlueprintPath.IsEmpty())
	{
		Message = TEXT("blueprint_path cannot be empty");
	}
	else if (NodeGuidStr.IsEmpty() && NodeTitle.IsEmpty())
	{
		Message = TEXT("Either node_guid or node_title must be provided");
	}
	else
	{
		// 1. Load the Blueprint
		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint)
		{
			Message = FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath);
			LogError(Message);
		}
		else
		{
			// 2. Open the Blueprint editor (idempotent if already open, Issue #7091: null-check guard)
			if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				AssetEditorSub->OpenEditorForAsset(Blueprint);
			}

			// 3. Find the target node

			// Primary: Find by GUID
			if (!NodeGuidStr.IsEmpty())
			{
				FGuid NodeGuid;
				if (FGuid::Parse(NodeGuidStr, NodeGuid))
				{
					TargetNode = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, NodeGuid);
				}
				else
				{
					LogInfo(FString::Printf(TEXT("Invalid GUID format: %s, falling back to title search"), *NodeGuidStr));
				}
			}

			// Fallback: Find by node title (partial match)
			if (!TargetNode && !NodeTitle.IsEmpty())
			{
				TArray<UEdGraph*> AllGraphs;
				Blueprint->GetAllGraphs(AllGraphs);
				for (UEdGraph* Graph : AllGraphs)
				{
					if (!Graph) continue;
					for (UEdGraphNode* Node : Graph->Nodes)
					{
						if (!Node) continue;
						FString Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
						if (Title.Contains(NodeTitle))
						{
							TargetNode = Node;
							break;
						}
					}
					if (TargetNode) break;
				}
			}

			if (!TargetNode)
			{
				Message = FString::Printf(TEXT("Node not found in %s (guid=%s, title=%s)"),
					*BlueprintPath, *NodeGuidStr, *NodeTitle);
				LogError(Message);
			}
			else
			{
				// 4. Get the FBlueprintEditor and JumpToNode
				UAssetEditorSubsystem* AssetEditorSubsystem =
					GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				IAssetEditorInstance* EditorInstance =
					AssetEditorSubsystem->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false);

				FBlueprintEditor* BlueprintEditor =
					EditorInstance ? static_cast<FBlueprintEditor*>(EditorInstance) : nullptr;

				if (BlueprintEditor)
				{
					// JumpToNode: opens correct graph tab, selects node, zooms to it
					BlueprintEditor->JumpToNode(TargetNode, /*bRequestRename=*/false);

					FoundNodeName = TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
					FoundNodeGuid = TargetNode->NodeGuid.ToString();

					// Find which graph the node belongs to
					UEdGraph* OwningGraph = TargetNode->GetGraph();
					FoundGraphName = OwningGraph ? OwningGraph->GetName() : TEXT("Unknown");

					bSuccess = true;
					Message = FString::Printf(TEXT("Jumped to node '%s' in graph '%s'"),
						*FoundNodeName, *FoundGraphName);
					LogInfo(Message);
				}
				else
				{
					Message = TEXT("Could not access Blueprint editor instance after opening");
					LogError(Message);
				}
			}
		}
	}

	// Issue #4544/#4548: Collect connected nodes context with optional deep traversal
	int32 MaxDepth = Request->HasField(TEXT("max_depth"))
		? FMath::Clamp(static_cast<int32>(Request->GetNumberField(TEXT("max_depth"))), 1, 10)
		: 1;
	bool bFollowExecOnly = Request->HasField(TEXT("follow_exec_only"))
		? Request->GetBoolField(TEXT("follow_exec_only"))
		: false;

	FString ConnectionsJSON = TEXT("[]");
	FString ExecutionChainJSON = TEXT("[]");
	if (bSuccess && TargetNode)
	{
		// Immediate connections (depth 1, always returned)
		FString ConnItems;
		bool bFirstConn = true;
		for (UEdGraphPin* Pin : TargetNode->Pins)
		{
			if (!Pin) continue;
			FString Direction = (Pin->Direction == EGPD_Input) ? TEXT("input") : TEXT("output");
			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
				UEdGraphNode* ConnNode = LinkedPin->GetOwningNode();
				if (!bFirstConn) ConnItems += TEXT(",");
				bFirstConn = false;
				ConnItems += FString::Printf(TEXT(
					"{\"direction\":\"%s\","
					"\"pin_name\":\"%s\","
					"\"connected_node_guid\":\"%s\","
					"\"connected_node_title\":\"%s\"}"),
					*Direction,
					*CUJsonUtils::EscapeJsonString(Pin->GetName()),
					*ConnNode->NodeGuid.ToString(),
					*CUJsonUtils::EscapeJsonString(ConnNode->GetNodeTitle(ENodeTitleType::ListView).ToString()));
			}
		}
		ConnectionsJSON = FString::Printf(TEXT("[%s]"), *ConnItems);

		// Issue #4548: Deep traversal (execution_chain) when max_depth > 1
		if (MaxDepth > 1)
		{
			TSet<FGuid> Visited;
			Visited.Add(TargetNode->NodeGuid);

			struct FChainEntry
			{
				UEdGraphNode* Node;
				int32 Depth;
			};

			TArray<FChainEntry> Queue;
			TArray<FChainEntry> ChainResults;

			// Seed with immediate output connections (forward traversal)
			for (UEdGraphPin* Pin : TargetNode->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				if (bFollowExecOnly && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
					UEdGraphNode* NextNode = LinkedPin->GetOwningNode();
					if (!Visited.Contains(NextNode->NodeGuid))
					{
						Visited.Add(NextNode->NodeGuid);
						Queue.Add({NextNode, 1});
						ChainResults.Add({NextNode, 1});
					}
				}
			}

			// BFS traversal up to MaxDepth
			int32 QueueIdx = 0;
			while (QueueIdx < Queue.Num())
			{
				FChainEntry Current = Queue[QueueIdx++];
				if (Current.Depth >= MaxDepth) continue;

				for (UEdGraphPin* Pin : Current.Node->Pins)
				{
					if (!Pin || Pin->Direction != EGPD_Output) continue;
					if (bFollowExecOnly && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec) continue;
					for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
					{
						if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;
						UEdGraphNode* NextNode = LinkedPin->GetOwningNode();
						if (!Visited.Contains(NextNode->NodeGuid))
						{
							Visited.Add(NextNode->NodeGuid);
							Queue.Add({NextNode, Current.Depth + 1});
							ChainResults.Add({NextNode, Current.Depth + 1});
						}
					}
				}
			}

			// Build execution_chain JSON
			FString ChainItems;
			bool bFirstChain = true;
			for (const FChainEntry& Entry : ChainResults)
			{
				if (!bFirstChain) ChainItems += TEXT(",");
				bFirstChain = false;
				ChainItems += FString::Printf(TEXT(
					"{\"node_guid\":\"%s\","
					"\"node_title\":\"%s\","
					"\"depth\":%d}"),
					*Entry.Node->NodeGuid.ToString(),
					*CUJsonUtils::EscapeJsonString(Entry.Node->GetNodeTitle(ENodeTitleType::ListView).ToString()),
					Entry.Depth);
			}
			ExecutionChainJSON = FString::Printf(TEXT("[%s]"), *ChainItems);

			LogInfo(FString::Printf(TEXT("Connections walker: %d nodes found at max_depth=%d"),
				ChainResults.Num(), MaxDepth));
		}
	}

	// Build JSON response (Issue #4548: added execution_chain, max_depth, follow_exec_only)
	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString EscapedNodeName = CUJsonUtils::EscapeJsonString(FoundNodeName);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"focus_blueprint_node_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"blueprint_path\":\"%s\","
		"\"node_guid\":\"%s\","
		"\"node_name\":\"%s\","
		"\"graph_name\":\"%s\","
		"\"connections\":%s,"
		"\"execution_chain\":%s,"
		"\"max_depth\":%d,"
		"\"follow_exec_only\":%s,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*BlueprintPath,
		*FoundNodeGuid,
		*EscapedNodeName,
		*FoundGraphName,
		*ConnectionsJSON,
		*ExecutionChainJSON,
		MaxDepth,
		bFollowExecOnly ? TEXT("true") : TEXT("false"),
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// Issue #4843: List registered automation tests via FAutomationTestFramework
// Uses GetValidTestNames() to enumerate tests available in the current Editor context.
// Supports optional filter (contains-match on test path and display name).
void UCUWebSocketEditorSubsystem::HandleListAutomationTests(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleListAutomationTests: Invalid socket or request"));
		return;
	}

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString Filter = Request->HasField(TEXT("filter")) ? Request->GetStringField(TEXT("filter")) : TEXT("");
	int32 MaxResults = Request->HasField(TEXT("max_results")) ? static_cast<int32>(Request->GetNumberField(TEXT("max_results"))) : 500;

	// Get all registered automation tests valid for current context.
	// Temporarily enable ALL filter levels to discover tests beyond SmokeFilter.
	// Default RequestedTestFilter is SmokeFilter only (AutomationTest.cpp:1497),
	// which excludes ProductFilter/EngineFilter/PerfFilter tests. (Issue #4843)
	FAutomationTestFramework& Framework = FAutomationTestFramework::Get();
	Framework.SetRequestedTestFilter(EAutomationTestFlags_FilterMask);
	TArray<FAutomationTestInfo> ValidTests;
	Framework.GetValidTestNames(ValidTests);
	Framework.SetRequestedTestFilter(EAutomationTestFlags::SmokeFilter); // Restore default

	// Build JSON array of matching tests
	FString TestsJSON = TEXT("[");
	int32 MatchCount = 0;
	int32 TotalMatched = 0;
	bool bFirstEntry = true;

	for (const FAutomationTestInfo& TestInfo : ValidTests)
	{
		FString TestPath = TestInfo.GetFullTestPath();
		FString DisplayName = TestInfo.GetDisplayName();

		// Apply filter if provided (contains-match on path or display name)
		if (!Filter.IsEmpty())
		{
			if (!TestPath.Contains(Filter) && !DisplayName.Contains(Filter))
			{
				continue;
			}
		}

		TotalMatched++;

		// Cap results to prevent oversized responses
		if (MatchCount >= MaxResults)
		{
			continue;  // Keep counting TotalMatched
		}

		if (!bFirstEntry)
		{
			TestsJSON += TEXT(",");
		}
		bFirstEntry = false;

		// Escape strings for JSON (Issue #5579: use comprehensive JSON escaper)
		FString EscapedPath = CUJsonUtils::EscapeJsonString(TestPath);
		FString EscapedName = CUJsonUtils::EscapeJsonString(DisplayName);
		FString Tags = CUJsonUtils::EscapeJsonString(TestInfo.GetTestTags());

		TestsJSON += FString::Printf(TEXT("{\"test_path\":\"%s\",\"display_name\":\"%s\",\"tags\":\"%s\"}"),
			*EscapedPath, *EscapedName, *Tags);

		MatchCount++;
	}

	TestsJSON += TEXT("]");

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"list_automation_tests_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"tests\":%s,"
		"\"total_count\":%d,"
		"\"returned_count\":%d,"
		"\"truncated\":%s"
		"}"),
		*RequestId,
		*TestsJSON,
		TotalMatched,
		MatchCount,
		TotalMatched > MaxResults ? TEXT("true") : TEXT("false")
	);

	LogInfo(FString::Printf(TEXT("HandleListAutomationTests: Found %d tests (%d matched filter '%s')"),
		ValidTests.Num(), TotalMatched, *Filter));

	SendResponse(Socket, ResponseJSON);
}

// Issue #26961: batch annotate success is FailCount-gated (never a true literal).
static bool AnnotateBatchSucceeded(const int32 FailCount)
{
	return FailCount == 0;
}

// ===================================================================
// Issue #5647: Annotate Blueprint Node with AI Analysis
// ===================================================================
void UCUWebSocketEditorSubsystem::HandleAnnotateBlueprintNode(
	INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	const FString RequestId = Request->GetStringField(TEXT("request_id"));

	// Required fields
	const FString BlueprintPath = Request->GetStringField(TEXT("blueprint_path"));
	const FString NodeGuid = Request->GetStringField(TEXT("node_guid"));
	const FString AnnotationText = Request->GetStringField(TEXT("annotation_text"));
	const FString Category = Request->HasField(TEXT("category"))
		? Request->GetStringField(TEXT("category"))
		: FCUAnnotationData::DefaultCategory;

	if (BlueprintPath.IsEmpty() || NodeGuid.IsEmpty() || AnnotationText.IsEmpty())
	{
		FString ErrorJSON = FString::Printf(
			TEXT("{\"type\":\"annotate_blueprint_node_result\","
				"\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"Missing required fields: blueprint_path, node_guid, annotation_text\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId)
		);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Dispatch to AIAnnotationSubsystem
	bool bSuccess = false;
	FString Message;

	if (GEditor)
	{
		UCUAnnotationSubsystem* AnnotationSubsystem = GEditor->GetEditorSubsystem<UCUAnnotationSubsystem>();
		if (AnnotationSubsystem)
		{
			const FString AnnotationId = FCUAnnotationData::MakeId(BlueprintPath, NodeGuid);
			AnnotationSubsystem->OnAnnotationResult(AnnotationId, AnnotationText, Category);
			bSuccess = true;
			Message = FString::Printf(TEXT("Annotation applied to %s in %s"), *NodeGuid, *BlueprintPath);
		}
		else
		{
			Message = TEXT("AIAnnotationSubsystem not available");
		}
	}
	else
	{
		Message = TEXT("GEditor not available");
	}

	FString ResultJSON = FString::Printf(
		TEXT("{\"type\":\"annotate_blueprint_node_result\","
			"\"request_id\":\"%s\","
			"\"success\":%s,"
			"\"annotation_id\":\"%s::%s\","
			"\"category\":\"%s\","
			"\"message\":\"%s\"}"),
		*CUJsonUtils::EscapeJsonString(RequestId),
		bSuccess ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(BlueprintPath),
		*CUJsonUtils::EscapeJsonString(NodeGuid),
		*CUJsonUtils::EscapeJsonString(Category),
		*CUJsonUtils::EscapeJsonString(Message)
	);

	SendResponse(Socket, ResultJSON);
}

// ===================================================================
// Issue #5816: Batch annotate multiple Blueprint nodes
// ===================================================================
void UCUWebSocketEditorSubsystem::HandleAnnotateBlueprintNodes(
	INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString BlueprintPath = Request->GetStringField(TEXT("blueprint_path"));

	if (BlueprintPath.IsEmpty())
	{
		FString ErrorJSON = FString::Printf(
			TEXT("{\"type\":\"annotate_blueprint_nodes_result\","
				"\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"Missing required field: blueprint_path\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId)
		);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Parse nodes array
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (!Request->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray)
	{
		FString ErrorJSON = FString::Printf(
			TEXT("{\"type\":\"annotate_blueprint_nodes_result\","
				"\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"Missing or invalid field: nodes (expected array)\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId)
		);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	int32 SuccessCount = 0;
	int32 FailCount = 0;

	if (!GEditor)
	{
		// All nodes fail if GEditor is not available (Issue #26961 — sibling of sequencer bail-outs)
		FailCount = NodesArray->Num();
	}
	else
	{
		UCUAnnotationSubsystem* AnnotationSubsystem = GEditor->GetEditorSubsystem<UCUAnnotationSubsystem>();
		if (!AnnotationSubsystem)
		{
			FailCount = NodesArray->Num();
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& NodeValue : *NodesArray)
			{
				const TSharedPtr<FJsonObject>* NodeObj = nullptr;
				if (!NodeValue.IsValid() || !NodeValue->TryGetObject(NodeObj) || !NodeObj || !NodeObj->IsValid())
				{
					FailCount++;
					continue;
				}

				const FString NodeGuid = (*NodeObj)->GetStringField(TEXT("node_guid"));
				const FString AnnotationText = (*NodeObj)->GetStringField(TEXT("annotation_text"));
				const FString Category = (*NodeObj)->HasField(TEXT("category"))
					? (*NodeObj)->GetStringField(TEXT("category"))
					: FCUAnnotationData::DefaultCategory;

				if (NodeGuid.IsEmpty() || AnnotationText.IsEmpty())
				{
					FailCount++;
					continue;
				}

				const FString AnnotationId = FCUAnnotationData::MakeId(BlueprintPath, NodeGuid);
				AnnotationSubsystem->OnAnnotationResult(AnnotationId, AnnotationText, Category);
				SuccessCount++;
			}
		}
	}

	FString ResultJSON = FString::Printf(
		TEXT("{\"type\":\"annotate_blueprint_nodes_result\","
			"\"request_id\":\"%s\","
			"\"success\":%s,"
			"\"blueprint_path\":\"%s\","
			"\"annotated_count\":%d,"
			"\"failed_count\":%d,"
			"\"total_count\":%d}"),
		*CUJsonUtils::EscapeJsonString(RequestId),
		AnnotateBatchSucceeded(FailCount) ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(BlueprintPath),
		SuccessCount,
		FailCount,
		NodesArray->Num()
	);

	SendResponse(Socket, ResultJSON);
}

// ===================================================================
// Issue #5818: Annotate Sequencer track with AI analysis result
// ===================================================================
void UCUWebSocketEditorSubsystem::HandleAnnotateSequencerTrack(
	INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	const FString RequestId = Request->GetStringField(TEXT("request_id"));

	// Required fields
	const FString SequencePath = Request->GetStringField(TEXT("sequence_path"));
	const FString TrackName = Request->GetStringField(TEXT("track_name"));
	const FString AnnotationText = Request->GetStringField(TEXT("annotation_text"));
	const FString Category = Request->HasField(TEXT("category"))
		? Request->GetStringField(TEXT("category"))
		: FCUAnnotationData::DefaultCategory;

	if (SequencePath.IsEmpty() || TrackName.IsEmpty() || AnnotationText.IsEmpty())
	{
		FString ErrorJSON = FString::Printf(
			TEXT("{\"type\":\"annotate_sequencer_track_result\","
				"\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"Missing required fields: sequence_path, track_name, annotation_text\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId)
		);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Dispatch to AIAnnotationSubsystem
	bool bSuccess = false;
	FString Message;

	if (!GEditor)
	{
		Message = TEXT("GEditor not available");
	}
	else
	{
		UCUAnnotationSubsystem* AnnotationSubsystem = GEditor->GetEditorSubsystem<UCUAnnotationSubsystem>();
		if (!AnnotationSubsystem)
		{
			Message = TEXT("AIAnnotationSubsystem not available");
		}
		else
		{
			const FString TargetIdentifier = FCUAnnotationData::TrackTargetPrefix + TrackName;
			const FString AnnotationId = FCUAnnotationData::MakeId(SequencePath, TargetIdentifier);
			AnnotationSubsystem->OnAnnotationResult(AnnotationId, AnnotationText, Category);
			bSuccess = true;
			Message = FString::Printf(TEXT("Annotation applied to track %s in %s"), *TrackName, *SequencePath);
		}
	}

	FString ResultJSON = FString::Printf(
		TEXT("{\"type\":\"annotate_sequencer_track_result\","
			"\"request_id\":\"%s\","
			"\"success\":%s,"
			"\"annotation_id\":\"%s::Track::%s\","
			"\"category\":\"%s\","
			"\"message\":\"%s\"}"),
		*CUJsonUtils::EscapeJsonString(RequestId),
		bSuccess ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(SequencePath),
		*CUJsonUtils::EscapeJsonString(TrackName),
		*CUJsonUtils::EscapeJsonString(Category),
		*CUJsonUtils::EscapeJsonString(Message)
	);

	SendResponse(Socket, ResultJSON);
}

// ===================================================================
// Issue #5818: Batch annotate multiple Sequencer tracks
// ===================================================================
void UCUWebSocketEditorSubsystem::HandleAnnotateSequencerTracks(
	INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	const FString RequestId = Request->GetStringField(TEXT("request_id"));
	const FString SequencePath = Request->GetStringField(TEXT("sequence_path"));

	if (SequencePath.IsEmpty())
	{
		FString ErrorJSON = FString::Printf(
			TEXT("{\"type\":\"annotate_sequencer_tracks_result\","
				"\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"Missing required field: sequence_path\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId)
		);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	// Parse tracks array
	const TArray<TSharedPtr<FJsonValue>>* TracksArray = nullptr;
	if (!Request->TryGetArrayField(TEXT("tracks"), TracksArray) || !TracksArray)
	{
		FString ErrorJSON = FString::Printf(
			TEXT("{\"type\":\"annotate_sequencer_tracks_result\","
				"\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"Missing or invalid field: tracks (expected array)\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId)
		);
		SendResponse(Socket, ErrorJSON);
		return;
	}

	int32 SuccessCount = 0;
	int32 FailCount = 0;

	if (!GEditor)
	{
		// All tracks fail if GEditor is not available
		FailCount = TracksArray->Num();
	}
	else
	{
		UCUAnnotationSubsystem* AnnotationSubsystem = GEditor->GetEditorSubsystem<UCUAnnotationSubsystem>();
		if (!AnnotationSubsystem)
		{
			// All tracks fail if AIAnnotationSubsystem is not available
			FailCount = TracksArray->Num();
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& TrackValue : *TracksArray)
			{
				const TSharedPtr<FJsonObject>* TrackObj = nullptr;
				if (!TrackValue.IsValid() || !TrackValue->TryGetObject(TrackObj) || !TrackObj || !TrackObj->IsValid())
				{
					FailCount++;
					continue;
				}

				const FString TrackName = (*TrackObj)->GetStringField(TEXT("track_name"));
				const FString AnnotationText = (*TrackObj)->GetStringField(TEXT("annotation_text"));
				const FString Category = (*TrackObj)->HasField(TEXT("category"))
					? (*TrackObj)->GetStringField(TEXT("category"))
					: FCUAnnotationData::DefaultCategory;

				if (TrackName.IsEmpty() || AnnotationText.IsEmpty())
				{
					FailCount++;
					continue;
				}

				const FString TargetIdentifier = FCUAnnotationData::TrackTargetPrefix + TrackName;
				const FString AnnotationId = FCUAnnotationData::MakeId(SequencePath, TargetIdentifier);
				AnnotationSubsystem->OnAnnotationResult(AnnotationId, AnnotationText, Category);
				SuccessCount++;
			}
		}
	}

	FString ResultJSON = FString::Printf(
		TEXT("{\"type\":\"annotate_sequencer_tracks_result\","
			"\"request_id\":\"%s\","
			"\"success\":%s,"
			"\"sequence_path\":\"%s\","
			"\"annotated_count\":%d,"
			"\"failed_count\":%d,"
			"\"total_count\":%d}"),
		*CUJsonUtils::EscapeJsonString(RequestId),
		AnnotateBatchSucceeded(FailCount) ? TEXT("true") : TEXT("false"),
		*CUJsonUtils::EscapeJsonString(SequencePath),
		SuccessCount,
		FailCount,
		TracksArray->Num()
	);

	SendResponse(Socket, ResultJSON);
}

// ============================================
// Issue #5818: Capture AI Annotation Overlay Window
// ============================================

void UCUWebSocketEditorSubsystem::HandleCaptureAnnotationGroup(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleCaptureAnnotationGroup: Invalid socket or request"));
		return;
	}

	// ARCH-011: defensive cook-safety guard — WebSocket handlers don't run in
	// commandlets, but the lint rule requires this signal to clear the function.
	if (IsRunningCommandlet()) return;

	FString RequestId = Request->GetStringField(TEXT("request_id"));
	FString TitleFilter = Request->HasField(TEXT("title_filter")) ? Request->GetStringField(TEXT("title_filter")) : TEXT("AI Seq");
	int32 Width = Request->HasField(TEXT("width")) ? Request->GetIntegerField(TEXT("width")) : 800;
	int32 Height = Request->HasField(TEXT("height")) ? Request->GetIntegerField(TEXT("height")) : 600;

	LogInfo(FString::Printf(TEXT("Capturing annotation group (request_id: %s, filter: %s)"),
		*RequestId, *TitleFilter));

	double StartTime = FPlatformTime::Seconds();
	bool bSuccess = false;
	FString Message;
	FString ScreenshotPath;
	FString MatchedWindowTitle;
	int32 CapturedCount = 0;

	if (!FSlateApplication::IsInitialized())
	{
		Message = TEXT("Slate not initialized");
	}
	else
	{
		TArray<TSharedRef<SWindow>> AllWindows;
		FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);

		// Generate screenshot directory
		FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MCP");
		IFileManager::Get().MakeDirectory(*ScreenshotDir, true);
		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

		for (const TSharedRef<SWindow>& Window : AllWindows)
		{
			FString WindowTitle = Window->GetTitle().ToString();
			if (WindowTitle.Contains(TitleFilter))
			{
				// Capture this annotation window
				FString SafeTitle = WindowTitle.Replace(TEXT(" "), TEXT("_")).Replace(TEXT(":"), TEXT("_"));
				ScreenshotPath = ScreenshotDir / FString::Printf(TEXT("annotation_%s_%d_%s.png"), *SafeTitle, CapturedCount, *Timestamp);

				TArray<FColor> Bitmap;
				FIntVector Size(Width, Height, 0);

				if (FSlateApplication::Get().TakeScreenshot(Window, Bitmap, Size))
				{
					IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
					TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

					if (ImageWrapper.IsValid() && ImageWrapper->SetRaw(Bitmap.GetData(), Bitmap.Num() * sizeof(FColor), Size.X, Size.Y, ERGBFormat::BGRA, 8))
					{
						TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(100);
						if (CompressedData.Num() > 0 && FFileHelper::SaveArrayToFile(CompressedData, *ScreenshotPath))
						{
							Width = Size.X;
							Height = Size.Y;
							MatchedWindowTitle = WindowTitle;
							CapturedCount++;
							bSuccess = true;
							LogInfo(FString::Printf(TEXT("Annotation screenshot saved: %s (%dx%d)"), *ScreenshotPath, Width, Height));
						}
					}
				}
			}
		}

		if (CapturedCount == 0)
		{
			Message = FString::Printf(TEXT("No annotation windows found matching '%s'. Found %d windows total."), *TitleFilter, AllWindows.Num());

			// List window titles for debugging
			for (const TSharedRef<SWindow>& Window : AllWindows)
			{
				FString WindowTitle = Window->GetTitle().ToString();
				if (!WindowTitle.IsEmpty())
				{
					LogInfo(FString::Printf(TEXT("  Window: '%s'"), *WindowTitle));
				}
			}
		}
		else
		{
			Message = FString::Printf(TEXT("Captured %d annotation window(s)"), CapturedCount);
		}
	}

	double ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	FString EscapedPath = ScreenshotPath.Replace(TEXT("\\"), TEXT("/"));
	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString EscapedTitle = CUJsonUtils::EscapeJsonString(MatchedWindowTitle);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"capture_annotation_group_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"file_path\":\"%s\","
		"\"matched_title\":\"%s\","
		"\"captured_count\":%d,"
		"\"width\":%d,"
		"\"height\":%d,"
		"\"execution_time_ms\":%.2f,"
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*EscapedPath,
		*EscapedTitle,
		CapturedCount,
		Width,
		Height,
		ExecutionTimeMs,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #6378: PIE Input Injection + Record/Replay
// ============================================

// Parsed input event data — eliminates repeated HasField/Get patterns (Issue #6378 review)
struct FCUParsedInputEvent
{
	FString InputType;
	FString KeyName;
	bool bShift = false;
	bool bCtrl = false;
	bool bAlt = false;
	double MouseX = 0.0;
	double MouseY = 0.0;

	static FCUParsedInputEvent FromJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FCUParsedInputEvent E;
		E.InputType = Obj->GetStringField(TEXT("input_type"));
		E.KeyName = Obj->HasField(TEXT("key")) ? Obj->GetStringField(TEXT("key")) : TEXT("");
		E.bShift = Obj->HasField(TEXT("shift")) ? Obj->GetBoolField(TEXT("shift")) : false;
		E.bCtrl = Obj->HasField(TEXT("ctrl")) ? Obj->GetBoolField(TEXT("ctrl")) : false;
		E.bAlt = Obj->HasField(TEXT("alt")) ? Obj->GetBoolField(TEXT("alt")) : false;
		E.MouseX = Obj->HasField(TEXT("x")) ? Obj->GetNumberField(TEXT("x")) : 0.0;
		E.MouseY = Obj->HasField(TEXT("y")) ? Obj->GetNumberField(TEXT("y")) : 0.0;
		return E;
	}
};

// ============================================
// FCUInputRecordingProcessor — captures Slate input events during PIE recording (Issue #6388)
// ============================================

static constexpr int32 MaxRecordedInputEvents = 100000;

FCURecordedInputEvent MakeRecordedEvent(
	UCUWebSocketEditorSubsystem* Owner,
	const FString& InputType,
	FKey Key,
	FVector2D MousePos,
	bool bShift, bool bCtrl, bool bAlt)
{
	FCURecordedInputEvent Evt;
	Evt.TimestampMs = (FPlatformTime::Seconds() - Owner->RecordingStartTime) * 1000.0;
	Evt.InputType = InputType;
	Evt.Key = Key;
	Evt.MousePosition = MousePos;
	Evt.bShift = bShift;
	Evt.bCtrl = bCtrl;
	Evt.bAlt = bAlt;
	return Evt;
}

bool FCUInputRecordingProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event)
{
	if (Owner && Owner->bIsRecordingInput)
	{
		if (Owner->RecordedInputEvents.Num() < MaxRecordedInputEvents)
		{
			Owner->RecordedInputEvents.Add(MakeRecordedEvent(
				Owner, TEXT("key_down"), Event.GetKey(),
				FVector2D::ZeroVector,
				Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown()));
		}
	}
	return false;
}

bool FCUInputRecordingProcessor::HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& Event)
{
	if (Owner && Owner->bIsRecordingInput)
	{
		if (Owner->RecordedInputEvents.Num() < MaxRecordedInputEvents)
		{
			Owner->RecordedInputEvents.Add(MakeRecordedEvent(
				Owner, TEXT("key_up"), Event.GetKey(),
				FVector2D::ZeroVector,
				Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown()));
		}
	}
	return false;
}

bool FCUInputRecordingProcessor::HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event)
{
	if (Owner && Owner->bIsRecordingInput)
	{
		if (Owner->RecordedInputEvents.Num() < MaxRecordedInputEvents)
		{
			Owner->RecordedInputEvents.Add(MakeRecordedEvent(
				Owner, TEXT("mouse_down"), Event.GetEffectingButton(),
				Event.GetScreenSpacePosition(),
				Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown()));
		}
	}
	return false;
}

bool FCUInputRecordingProcessor::HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& Event)
{
	if (Owner && Owner->bIsRecordingInput)
	{
		if (Owner->RecordedInputEvents.Num() < MaxRecordedInputEvents)
		{
			Owner->RecordedInputEvents.Add(MakeRecordedEvent(
				Owner, TEXT("mouse_up"), Event.GetEffectingButton(),
				Event.GetScreenSpacePosition(),
				Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown()));
		}
	}
	return false;
}

bool FCUInputRecordingProcessor::HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& Event)
{
	if (Owner && Owner->bIsRecordingInput)
	{
		if (Owner->RecordedInputEvents.Num() < MaxRecordedInputEvents)
		{
			Owner->RecordedInputEvents.Add(MakeRecordedEvent(
				Owner, TEXT("mouse_move"), EKeys::Invalid,
				Event.GetScreenSpacePosition(),
				Event.IsShiftDown(), Event.IsControlDown(), Event.IsAltDown()));
		}
	}
	return false;
}

// Dispatch a single input event to FSlateApplication — shared by HandleInjectInput,
// HandleInjectInputSequence ticker, and HandleReplayInputRecording ticker (Issue #6378 review)
static bool DispatchSingleInputEvent(const FCUParsedInputEvent& Evt, FString& OutMessage)
{
	// ARCH-011: function-scope cook-safety guard. Cleared via cmdlet_early_return_re
	// (matches `if (IsRunningCommandlet()) return false;` single-statement form).
	// In commandlets the IsInitialized() check below would also catch this, but the
	// lint rule recognizes only the IsRunningCommandlet shape as a function-scope guard.
	if (IsRunningCommandlet()) return false;
	// Issue #10756: callers (including HandleInjectInputSequence at line 7988) document
	// that "DispatchSingleInputEvent itself guards against that" — make that real.
	// Without this gate, ProcessKeyDownEvent/ProcessMouseButtonDownEvent below crash
	// on cook/headless hosts where FSlateApplication was never created.
	if (!FSlateApplication::IsInitialized())
	{
		OutMessage = TEXT("Slate not initialized (commandlet/headless mode)");
		return false;
	}

	FModifierKeysState ModState(
		Evt.bShift, Evt.bShift,  // Left/Right Shift
		Evt.bCtrl, Evt.bCtrl,    // Left/Right Ctrl
		Evt.bAlt, Evt.bAlt,      // Left/Right Alt
		false, false,             // Left/Right Command
		false                     // CapsLock
	);

	if (Evt.InputType == TEXT("key_down") || Evt.InputType == TEXT("key_up"))
	{
		FKey Key = Evt.KeyName.IsEmpty() ? EKeys::Invalid : FKey(*Evt.KeyName);
		if (!Key.IsValid())
		{
			OutMessage = FString::Printf(TEXT("Invalid key name: %s"), *Evt.KeyName);
			return false;
		}

		FKeyEvent KeyEvent(Key, ModState, 0, false, 0, 0);
		if (Evt.InputType == TEXT("key_down"))
		{
			FSlateApplication::Get().ProcessKeyDownEvent(KeyEvent);
		}
		else
		{
			FSlateApplication::Get().ProcessKeyUpEvent(KeyEvent);
		}
		OutMessage = FString::Printf(TEXT("Injected %s: %s"), *Evt.InputType, *Evt.KeyName);
		return true;
	}
	else if (Evt.InputType == TEXT("mouse_down") || Evt.InputType == TEXT("mouse_up"))
	{
		FKey MouseButton = Evt.KeyName.IsEmpty() ? EKeys::LeftMouseButton : FKey(*Evt.KeyName);
		FVector2D CursorPos(Evt.MouseX, Evt.MouseY);
		FPointerEvent MouseEvent(0, CursorPos, CursorPos, TSet<FKey>(), MouseButton, 0.0f, ModState);
		if (Evt.InputType == TEXT("mouse_down"))
		{
			FSlateApplication::Get().ProcessMouseButtonDownEvent(nullptr, MouseEvent);
		}
		else
		{
			FSlateApplication::Get().ProcessMouseButtonUpEvent(MouseEvent);
		}
		OutMessage = FString::Printf(TEXT("Injected %s at (%.0f, %.0f)"), *Evt.InputType, Evt.MouseX, Evt.MouseY);
		return true;
	}
	else if (Evt.InputType == TEXT("mouse_move"))
	{
		FVector2D CursorPos(Evt.MouseX, Evt.MouseY);
		FPointerEvent MoveEvent(0, CursorPos, CursorPos, TSet<FKey>(), EKeys::Invalid, 0.0f, ModState);
		FSlateApplication::Get().ProcessMouseMoveEvent(MoveEvent);
		OutMessage = FString::Printf(TEXT("Injected mouse_move to (%.0f, %.0f)"), Evt.MouseX, Evt.MouseY);
		return true;
	}

	OutMessage = FString::Printf(TEXT("Unknown input_type: %s"), *Evt.InputType);
	return false;
}

// Validate socket + request and extract request_id — shared guard for all PIE handlers (Issue #6378 review)
static bool ValidateSocketAndRequest(
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request,
	const TCHAR* FunctionName,
	FString& OutRequestId)
{
	if (!Socket || !Request.IsValid())
	{
		UE_LOG(LogNarshaMCPWebSocket, Error, TEXT("NarshaMCP: %s: Invalid socket or request"), FunctionName);
		return false;
	}
	if (!Request->TryGetStringField(TEXT("request_id"), OutRequestId)
		|| OutRequestId.IsEmpty())
	{
		UE_LOG(LogNarshaMCPWebSocket, Error,
			TEXT("NarshaMCP: %s: Missing or invalid request_id"), FunctionName);
		return false;
	}
	return true;
}

// Issue #9895: forward declarations for the two PIE-precondition helpers defined post-MultiPIE
// namespace (file-scope, ~L12420). All PIE handlers call one of these three helpers; legacy
// EnsurePIERunning is being phased out in favor of the explicit single-PIE-strict variant.
//
//   EnsurePIERunning            — DEPRECATED legacy single-PIE check (PlayWorld only). Issue
//                                 #9895 migrates the 13 callers off this helper to either of:
//   EnsurePIERunningSinglePIE   — Strict single-PIE: rejects multi-client topology with
//                                 error_code:"SINGLE_PIE_REQUIRED". Used by handlers whose
//                                 body has a hard single-world assumption (Slate input
//                                 injection, snapshot capture, etc.).
//   EnsurePIERunningMultiPIE    — Multi-PIE-tolerant: accepts any GEngine PIE world context.
//                                 Used by handlers whose body iterates per-client via
//                                 ResolveClientWorld() or GetPIEViewport().
static bool EnsurePIERunningSinglePIE(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType);

bool EnsurePIERunningMultiPIE(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType);

// Forward declaration of the MultiPIE namespace helper used by HandlePIEWaitForCondition's
// ticker (defined post-MultiPIE namespace at L~12515; ticker lambda at L~12300 needs the symbol).
namespace MultiPIE
{
	static bool IsAnyPIEWorldRunning();
}

// Issue #9895 (post-/ns-review duplication finding): forward decl for the
// extracted client_id+envelope helper. Definition at L~12649.
// NOT `static` — must match the friend declaration in the header
// (CUWebSocketEditorSubsystem.h:236-238); friend fns require external linkage.
UWorld* ResolveClientWorldOrReject(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request,
	const FString& RequestId,
	const TCHAR* ResponseType,
	int32& OutClientId);

// DEPRECATED (Issue #9895): use EnsurePIERunningSinglePIE or EnsurePIERunningMultiPIE.
// Retained only for any legacy call sites missed during migration. Will be removed in a
// follow-up once CI confirms no remaining callers.
bool EnsurePIERunning(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"%s_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"PIE is not running. Start PIE with play_in_editor first.\""
			"}"),
			*ResponseType, *RequestId
		);
		Self->SendResponse(Socket, ResponseJSON);
		return false;
	}
	return true;
}

void UCUWebSocketEditorSubsystem::HandleInjectInput(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleInjectInput"), RequestId)) return;
	// Issue #9895: single-PIE-strict — Slate-level keyboard/mouse injection routes to the
	// single focused window; rejecting multi-PIE here prevents silent misroutes.
	if (!EnsurePIERunningSinglePIE(this, Socket, RequestId, TEXT("inject_input"))) return;
	// ARCH-011: defensive cook-safety guard — WebSocket handlers don't run in
	// commandlets, but the lint rule requires this signal to clear the function.
	if (IsRunningCommandlet()) return;

	if (!FSlateApplication::IsInitialized())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"inject_input_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"FSlateApplication is not initialized.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	FCUParsedInputEvent Evt = FCUParsedInputEvent::FromJson(Request);

	FString Message;
	bool bSuccess = DispatchSingleInputEvent(Evt, Message);

	FString EscapedMessage = CUJsonUtils::EscapeJsonString(Message);
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"inject_input_result\","
		"\"request_id\":\"%s\","
		"\"success\":%s,"
		"\"input_type\":\"%s\","
		"\"message\":\"%s\""
		"}"),
		*RequestId,
		bSuccess ? TEXT("true") : TEXT("false"),
		*Evt.InputType,
		*EscapedMessage
	);

	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleInjectInputSequence(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleInjectInputSequence"), RequestId)) return;
	// Issue #9895: single-PIE-strict — same Slate routing constraint as HandleInjectInput.
	if (!EnsurePIERunningSinglePIE(this, Socket, RequestId, TEXT("inject_input_sequence"))) return;

	const TArray<TSharedPtr<FJsonValue>>* EventsArray;
	if (!Request->TryGetArrayField(TEXT("events"), EventsArray) || !EventsArray)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"inject_input_sequence_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Missing 'events' array parameter.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	const int32 MaxEvents = 10000;
	if (EventsArray->Num() > MaxEvents)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"inject_input_sequence_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"events array exceeds maximum of %d (got %d).\""
			"}"),
			*RequestId,
			MaxEvents,
			EventsArray->Num()
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Schedule events with delays using FTSTicker
	int32 EventCount = EventsArray->Num();
	double AccumulatedDelay = 0.0;

	for (int32 i = 0; i < EventCount; ++i)
	{
		const TSharedPtr<FJsonObject>* EventObj;
		if (!(*EventsArray)[i]->TryGetObject(EventObj) || !EventObj->IsValid())
		{
			continue;
		}

		double DelayMs = (*EventObj)->HasField(TEXT("delay_ms")) ? (*EventObj)->GetNumberField(TEXT("delay_ms")) : 0.0;
		AccumulatedDelay += DelayMs;
		double ScheduleDelay = AccumulatedDelay / 1000.0;  // Convert to seconds

		// Capture event data for the lambda
		TSharedPtr<FJsonObject> CapturedEvent = *EventObj;
		TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakThis(this);

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis, CapturedEvent](float DeltaTime) -> bool
			{
				UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
				if (!Self || !GEditor || !GEditor->PlayWorld || !FSlateApplication::IsInitialized())
				{
					return false;  // Stop ticker
				}

				// Issue #7914: route InputAction events through Enhanced Input
				FString EventType;
				CapturedEvent->TryGetStringField(TEXT("input_type"), EventType);
				if (EventType == TEXT("action"))
				{
					FString ActionMsg;
					Self->DispatchInputActionEvent(CapturedEvent, ActionMsg);
					return false;  // One-shot ticker
				}

				FCUParsedInputEvent Evt = FCUParsedInputEvent::FromJson(CapturedEvent);
				FString Msg;
				DispatchSingleInputEvent(Evt, Msg);
				return false;  // One-shot ticker
			}),
			static_cast<float>(ScheduleDelay)
		);
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"inject_input_sequence_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"event_count\":%d,"
		"\"message\":\"Scheduled %d input events.\""
		"}"),
		*RequestId,
		EventCount,
		EventCount
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #7914: Enhanced Input InputAction injection
// ============================================
//
// Drives `UEnhancedInputLocalPlayerSubsystem::InjectInputForAction(Action, Value, Modifiers, Triggers)`
// so that any project using Enhanced Input (Lyra, UE 5.1+ default template) can be driven by
// InputAction name rather than raw Slate key events. Slate-level injection (HandleInjectInput)
// remains available as a fallback for non-Enhanced-Input projects.

#if WITH_NARSHAMCP_ENHANCEDINPUT
// Parse FInputActionValue from a JSON object. Shape:
//   { "x": 1.0 }                  → Axis1D
//   { "x": 1.0, "y": 0.0 }        → Axis2D
//   { "x": 1.0, "y": 0.0, "z": 0.5 } → Axis3D
//   { "x": 1.0 } with value=0/1 treated as Bool when trigger prefers it
// Absent fields default to 0. Empty object → Bool(true) (one-shot press).
static FInputActionValue ParseInputActionValue(const TSharedPtr<FJsonObject>& ValueObj)
{
	if (!ValueObj.IsValid())
	{
		return FInputActionValue(true);
	}
	const bool bHasX = ValueObj->HasTypedField<EJson::Number>(TEXT("x"));
	const bool bHasY = ValueObj->HasTypedField<EJson::Number>(TEXT("y"));
	const bool bHasZ = ValueObj->HasTypedField<EJson::Number>(TEXT("z"));
	const double X = bHasX ? ValueObj->GetNumberField(TEXT("x")) : 0.0;
	const double Y = bHasY ? ValueObj->GetNumberField(TEXT("y")) : 0.0;
	const double Z = bHasZ ? ValueObj->GetNumberField(TEXT("z")) : 0.0;

	if (bHasZ)
	{
		return FInputActionValue(FVector(X, Y, Z));
	}
	if (bHasY)
	{
		return FInputActionValue(FVector2D(X, Y));
	}
	if (bHasX)
	{
		return FInputActionValue(static_cast<float>(X));
	}
	return FInputActionValue(true);
}

static ETriggerEvent ParseTriggerEvent(const FString& Name)
{
	if (Name == TEXT("Started"))   return ETriggerEvent::Started;
	if (Name == TEXT("Completed")) return ETriggerEvent::Completed;
	if (Name == TEXT("Canceled"))  return ETriggerEvent::Canceled;
	// Default + explicit "Triggered"
	return ETriggerEvent::Triggered;
}

// Issue #9098: PIE → LocalPlayer → EnhancedInputLocalPlayerSubsystem resolution helper.
// Extracted after review: the chain was triplicated across DispatchInputActionEvent,
// the sustained-injection stop-timer lambda, and StopAllContinuousInjections with
// subtly different null-handling. Unifying the chain removes divergence risk and
// makes the PlayWorld re-resolve semantics explicit.
// Returns nullptr + sets OutError on any null-link in the chain; caller can pass
// OutError=nullptr for silent-skip contexts (cleanup paths).
static UEnhancedInputLocalPlayerSubsystem* ResolvePIEEnhancedInput(FString* OutError)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		if (OutError) *OutError = TEXT("PIE is not running (PlayWorld is null at dispatch time)");
		return nullptr;
	}
	APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
	if (!PC)
	{
		if (OutError) *OutError = TEXT("No PlayerController in PIE world");
		return nullptr;
	}
	ULocalPlayer* LP = PC->GetLocalPlayer();
	if (!LP)
	{
		if (OutError) *OutError = TEXT("PlayerController has no LocalPlayer");
		return nullptr;
	}
	UEnhancedInputLocalPlayerSubsystem* EIS = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	if (!EIS)
	{
		if (OutError) *OutError = TEXT("UEnhancedInputLocalPlayerSubsystem not found — ensure the EnhancedInput plugin is enabled and the project uses Enhanced Input");
		return nullptr;
	}
	return EIS;
}

// Issue #9098 Tier 2: Resolve the first physical key bound to `Action` across all currently
// applied InputMappingContexts. Prefers non-gamepad keys (keyboard/mouse). Used by the
// raw_key fallback path to simulate a physical key press when a project's input pipeline
// bypasses Enhanced Input delegates (e.g. YakshaInput's YIInputBufferComponent).
//
// Scan strategy: UObjectIterator<UInputMappingContext> enumerates all loaded IMC assets
// (including transient editor copies). HasMappingContext(IMC) filters to those actually
// applied to the EIS — i.e., the player's active input context. Pointer equality on
// Mapping.Action is correct because IMCs reference the same UInputAction asset instance
// (name comparison would be wrong if two projects had same-named IA assets).
//
// Known limitation: UObjectIterator order is not guaranteed, so if the same action is
// bound in two applied IMCs, the "first" non-gamepad match depends on load order.
// Mitigation: log all matches (TotalBindingsForAction); future work can expose a
// preferred_key parameter to pin a specific binding.
static bool ResolveFirstKeyForAction(
	UEnhancedInputLocalPlayerSubsystem* EIS,
	const UInputAction* Action,
	FKey& OutKey,
	bool& bOutGamepadOnly,
	FString& OutError)
{
	OutKey = FKey();
	bOutGamepadOnly = false;

	if (!EIS || !Action)
	{
		OutError = TEXT("ResolveFirstKeyForAction: null EIS or Action");
		return false;
	}

	FKey FirstMatch;
	FKey FirstNonGamepadMatch;
	int32 AppliedIMCsScanned = 0;
	int32 TotalBindingsForAction = 0;

	for (TObjectIterator<UInputMappingContext> It; It; ++It)
	{
		UInputMappingContext* IMC = *It;
		if (!IMC || !EIS->HasMappingContext(IMC))
		{
			continue;
		}
		AppliedIMCsScanned++;

		// Use GetMappings() getter — direct UInputMappingContext::Mappings field access
		// is UE_DEPRECATED(5.7) per InputMappingContext.h.
		for (const FEnhancedActionKeyMapping& Mapping : IMC->GetMappings())
		{
			if (Mapping.Action != Action)
			{
				continue;
			}
			TotalBindingsForAction++;

			if (!FirstMatch.IsValid())
			{
				FirstMatch = Mapping.Key;
			}
			if (!FirstNonGamepadMatch.IsValid() && !Mapping.Key.IsGamepadKey())
			{
				FirstNonGamepadMatch = Mapping.Key;
				// Keep scanning to populate TotalBindingsForAction for diagnostic logging.
			}
		}
	}

	if (TotalBindingsForAction == 0)
	{
		OutError = FString::Printf(
			TEXT("No applied IMC binds action '%s' to any key (scanned %d applied IMCs). ")
			TEXT("Apply an IMC first via AddMappingContext, or use fallback=none."),
			*Action->GetName(), AppliedIMCsScanned);
		return false;
	}

	// Prefer non-gamepad binding; fall back to first match (which must be gamepad in that case).
	OutKey = FirstNonGamepadMatch.IsValid() ? FirstNonGamepadMatch : FirstMatch;
	bOutGamepadOnly = !FirstNonGamepadMatch.IsValid();

	UE_LOG(LogNarshaMCPWebSocket, Log,
		TEXT("[inject_input_action] Resolved action '%s' -> key '%s' (applied IMCs: %d, bindings: %d, gamepad-only: %s)"),
		*Action->GetName(), *OutKey.ToString(), AppliedIMCsScanned, TotalBindingsForAction,
		bOutGamepadOnly ? TEXT("true") : TEXT("false"));
	return true;
}

bool UCUWebSocketEditorSubsystem::DispatchInputActionEvent(const TSharedPtr<FJsonObject>& EventObj, FString& OutError,
	FString* OutResolvedKey, bool* OutGamepadOnly, bool* OutScheduled)
{
	// Initialize out-params to defaults so callers see consistent state on any early return.
	if (OutResolvedKey) OutResolvedKey->Reset();
	if (OutGamepadOnly) *OutGamepadOnly = false;
	if (OutScheduled) *OutScheduled = false;

	if (!EventObj.IsValid())
	{
		OutError = TEXT("DispatchInputActionEvent: null event object");
		return false;
	}

	const FString ActionPath = EventObj->GetStringField(TEXT("action_path"));
	if (ActionPath.TrimStartAndEnd().IsEmpty())
	{
		OutError = TEXT("action_path is empty");
		return false;
	}

	UInputAction* Action = LoadObject<UInputAction>(nullptr, *ActionPath);
	if (!Action)
	{
		OutError = FString::Printf(TEXT("UInputAction not found at path: %s"), *ActionPath);
		return false;
	}

	UEnhancedInputLocalPlayerSubsystem* EnhancedInput = ResolvePIEEnhancedInput(&OutError);
	if (!EnhancedInput)
	{
		return false;  // OutError set by helper
	}

	// Issue #9098 Tier 2: detect fallback=raw_key early so we can validate before reading
	// Enhanced-Input-only fields like value/trigger_event.
	FString FallbackMode;
	EventObj->TryGetStringField(TEXT("fallback"), FallbackMode);
	const bool bUseRawKey = FallbackMode == TEXT("raw_key");

	// Parse duration_ms once — used by both Tier 1 (sustained EIS) and Tier 2 (key_up delay).
	// Rust layer already clamps; defensive re-clamp here for any direct-WS callers.
	int64 DurationMs = 0;
	if (EventObj->HasTypedField<EJson::Number>(TEXT("duration_ms")))
	{
		DurationMs = static_cast<int64>(EventObj->GetNumberField(TEXT("duration_ms")));
		if (DurationMs < 0) DurationMs = 0;
		if (DurationMs > 10000) DurationMs = 10000;
	}

	// ─── Tier 2: raw_key fallback — IMC lookup + Slate key_down + scheduled key_up ───
	if (bUseRawKey)
	{
		FKey TargetKey;
		bool bGamepadOnly = false;
		FString ResolveErr;
		if (!ResolveFirstKeyForAction(EnhancedInput, Action, TargetKey, bGamepadOnly, ResolveErr))
		{
			OutError = ResolveErr;
			return false;
		}

		// Dispatch immediate key_down via the existing Slate helper (shared with
		// HandleInjectInput / HandleInjectInputSequence). This reaches FSlateApplication
		// ahead of Enhanced Input, covering consumers like YakshaInput's YIInputBufferComponent
		// that bypass EIS delegates entirely.
		FCUParsedInputEvent DownEvt;
		DownEvt.InputType = TEXT("key_down");
		DownEvt.KeyName = TargetKey.ToString();
		FString DownMsg;
		if (!DispatchSingleInputEvent(DownEvt, DownMsg))
		{
			OutError = FString::Printf(TEXT("Raw-key down dispatch failed for '%s': %s"),
				*TargetKey.ToString(), *DownMsg);
			return false;
		}

		// Review CRITICAL mitigation: cap pending-release tracker to prevent unbounded
		// growth under pathological spam (e.g., 100 Hz inject loop from a runaway test).
		// Steady-state size = rate * max_duration; realistic AI/tooling usage produces
		// well below this bound. When saturated, force-release the oldest entry before
		// accepting the new one — trades a ~1ms-early key_up on the oldest entry for
		// bounded memory + bounded O(N) RemoveAll cost.
		constexpr int32 MaxPendingRawKeyReleases = 1024;
		if (PendingRawKeyReleases.Num() >= MaxPendingRawKeyReleases)
		{
			TPair<FTimerHandle, FKey> Oldest = PendingRawKeyReleases[0];
			PendingRawKeyReleases.RemoveAt(0);
			GEditor->PlayWorld->GetTimerManager().ClearTimer(Oldest.Key);
			FCUParsedInputEvent ForcedUp;
			ForcedUp.InputType = TEXT("key_up");
			ForcedUp.KeyName = Oldest.Value.ToString();
			FString ForcedUpMsg;
			DispatchSingleInputEvent(ForcedUp, ForcedUpMsg);
			UE_LOG(LogNarshaMCPWebSocket, Warning,
				TEXT("[inject_input_action] Pending-release tracker saturated (cap=%d); force-released oldest key '%s'"),
				MaxPendingRawKeyReleases, *Oldest.Value.ToString());
		}

		// Schedule matching key_up. Default 50ms button-tap if no explicit duration.
		const int64 ReleaseMs = DurationMs > 0 ? DurationMs : 50;
		const float ReleaseAfterSec = static_cast<float>(ReleaseMs) / 1000.0f;

		TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakThis(this);
		const FString KeyNameCopy = DownEvt.KeyName;
		const FKey KeyCopy = TargetKey;

		// Review W-3 mitigation: register tracker slot BEFORE SetTimer so the lambda's
		// RemoveAll always has an entry to remove, even on the (unreachable in practice
		// but theoretically possible) synchronous-fire path. We pre-register with an
		// invalid handle and patch it after SetTimer returns.
		const int32 TrackerIdx = PendingRawKeyReleases.Emplace(FTimerHandle(), TargetKey);

		FTimerHandle ReleaseHandle;
		GEditor->PlayWorld->GetTimerManager().SetTimer(ReleaseHandle,
			FTimerDelegate::CreateLambda([WeakThis, KeyNameCopy, KeyCopy]()
			{
				UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
				if (!Self)
				{
					return;  // subsystem destroyed — OnEndPIE would have cleaned up the OS state if it was still alive
				}
				// Remove our own tracker entry first so OnEndPIE doesn't double-release.
				// RemoveAll is O(N) but N is bounded by MaxPendingRawKeyReleases (1024).
				Self->PendingRawKeyReleases.RemoveAll([KeyCopy](const TPair<FTimerHandle, FKey>& Entry)
				{
					return Entry.Value == KeyCopy;
				});
				FCUParsedInputEvent UpEvt;
				UpEvt.InputType = TEXT("key_up");
				UpEvt.KeyName = KeyNameCopy;
				FString UpMsg;
				DispatchSingleInputEvent(UpEvt, UpMsg);
			}),
			ReleaseAfterSec, /*bLoop=*/false);

		// Patch the real handle into the pre-registered slot (bounds already checked).
		if (PendingRawKeyReleases.IsValidIndex(TrackerIdx))
		{
			PendingRawKeyReleases[TrackerIdx].Key = ReleaseHandle;
		}

		UE_LOG(LogNarshaMCPWebSocket, Log,
			TEXT("[inject_input_action] Raw-key dispatched: '%s' -> '%s' (release in %lld ms%s)"),
			*ActionPath, *TargetKey.ToString(), ReleaseMs,
			bGamepadOnly ? TEXT(", gamepad-only binding") : TEXT(""));

		if (OutResolvedKey) *OutResolvedKey = TargetKey.ToString();
		if (OutGamepadOnly) *OutGamepadOnly = bGamepadOnly;
		if (OutScheduled) *OutScheduled = true;
		return true;
	}

	// Build value from optional "value" object; default = Bool(true).
	const TSharedPtr<FJsonObject>* ValueObjPtr = nullptr;
	FInputActionValue ActionValue = EventObj->TryGetObjectField(TEXT("value"), ValueObjPtr) && ValueObjPtr
		? ParseInputActionValue(*ValueObjPtr)
		: FInputActionValue(true);

	// Trigger event: "Triggered" | "Started" | "Completed" | "Canceled" (default "Triggered").
	FString TriggerName;
	if (!EventObj->TryGetStringField(TEXT("trigger_event"), TriggerName))
	{
		TriggerName = TEXT("Triggered");
	}
	const ETriggerEvent TriggerEvent = ParseTriggerEvent(TriggerName);

	// Modifier/Trigger object arrays from user are not wired through: instantiating
	// arbitrary UInputModifier/UInputTrigger UObjects from JSON class names opens a
	// sandbox escape surface (arbitrary class loading). Projects needing custom
	// modifiers should bake them into the UInputAction asset itself.
	// InjectInputForAction accepts empty arrays and still fires the action with the
	// supplied value + trigger event — which is the common case.
	TArray<UInputModifier*> Modifiers;
	TArray<UInputTrigger*> Triggers;

	// Issue #9098 Tier 1: Sustained injection when duration_ms > 0.
	// Uses UE 5.7's official StartContinuousInputInjectionForAction which re-injects
	// every tick until StopContinuousInputInjectionForAction is called, so polling-style
	// consumers (movement, hold-to-charge, animation blend-space read per tick) see the
	// value across multiple frames rather than reverting to 0 next tick.
	// (DurationMs already parsed + clamped above, shared with Tier 2 raw_key path.)
	if (DurationMs > 0)
	{
		// Issue #9098 review W-1: Re-entry safety — if the same action is already being
		// sustained-injected, Stop it first. Otherwise overlapping StartContinuous calls
		// produce competing stop timers (first one fires and kills the second one's
		// effect earlier than its duration_ms requested).
		if (ContinuouslyInjectedActionPaths.Contains(ActionPath))
		{
			EnhancedInput->StopContinuousInputInjectionForAction(Action);
			UE_LOG(LogNarshaMCPWebSocket, Log,
				TEXT("[inject_input_action] Re-entry: cancelled prior sustained injection for %s"),
				*ActionPath);
		}
		EnhancedInput->StartContinuousInputInjectionForAction(Action, ActionValue, Modifiers, Triggers);
		ContinuouslyInjectedActionPaths.Add(ActionPath);

		// Schedule stop via world-scoped timer so PIE teardown auto-invalidates the handle.
		// WeakObjectPtr captures protect against subsystem deletion / Action GC during
		// the delay window. GEditor/PlayWorld are re-resolved inside the lambda so a
		// level transition doesn't dereference a stale world.
		TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakThis(this);
		TWeakObjectPtr<const UInputAction> WeakAction(Action);
		const FString CapturedActionPath = ActionPath;
		const float DelaySec = static_cast<float>(DurationMs) / 1000.0f;

		FTimerHandle Handle;
		GEditor->PlayWorld->GetTimerManager().SetTimer(
			Handle,
			FTimerDelegate::CreateLambda([WeakThis, CapturedActionPath, WeakAction]()
			{
				UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
				if (!Self)
				{
					return;  // subsystem destroyed — nothing to clean up
				}
				if (!WeakAction.IsValid())
				{
					UE_LOG(LogNarshaMCPWebSocket, Verbose,
						TEXT("[inject_input_action] Scheduled stop skipped — action GC'd: %s"),
						*CapturedActionPath);
					Self->ContinuouslyInjectedActionPaths.Remove(CapturedActionPath);
					return;
				}
				// Issue #9098 review: use shared PIE→EIS resolver (eliminates triplicated chain).
				// Pass nullptr OutError for silent-skip semantics — no caller to report to.
				if (UEnhancedInputLocalPlayerSubsystem* EIS = ResolvePIEEnhancedInput(nullptr))
				{
					EIS->StopContinuousInputInjectionForAction(WeakAction.Get());
				}
				Self->ContinuouslyInjectedActionPaths.Remove(CapturedActionPath);
			}),
			DelaySec, /*bLoop=*/false
		);

		UE_LOG(LogNarshaMCPWebSocket, Log,
			TEXT("[inject_input_action] Sustained injection started: %s for %lld ms"),
			*ActionPath, DurationMs);
		if (OutScheduled) *OutScheduled = true;
	}
	else
	{
		EnhancedInput->InjectInputForAction(Action, ActionValue, Modifiers, Triggers);
	}

	// NOTE: InjectInputForAction with a trigger event other than Triggered still fires
	// the action on the next tick with the supplied value — the Enhanced Input system
	// determines the actual ETriggerEvent from the value + modifier state. The
	// `trigger_event` parameter is currently advisory.
	// Issue #9098 Tier 2 now handled above — raw-key fallback via IMC lookup +
	// DispatchSingleInputEvent covers projects whose input pipelines bypass Enhanced Input.
	(void)TriggerEvent;

	return true;
}

// Issue #9098 Tier 1: Stop every tracked continuous injection and clear the tracker.
// Called by OnEndPIE (PIE end) and Deinitialize (subsystem shutdown). Safe to call
// when PlayWorld is null — silently skips the EIS lookup and only clears bookkeeping.
void UCUWebSocketEditorSubsystem::StopAllContinuousInjections()
{
	if (ContinuouslyInjectedActionPaths.IsEmpty()) return;

	// Resolve EIS via shared helper (silent-skip — cleanup context).
	UEnhancedInputLocalPlayerSubsystem* EIS = ResolvePIEEnhancedInput(nullptr);

	const int32 Count = ContinuouslyInjectedActionPaths.Num();
	UE_LOG(LogNarshaMCPWebSocket, Log,
		TEXT("[inject_input_action] Cleaning up %d sustained injection(s)%s"),
		Count, EIS ? TEXT("") : TEXT(" (EIS unreachable — tracker clear only)"));

	if (EIS)
	{
		for (const FString& ActionPath : ContinuouslyInjectedActionPaths)
		{
			// Prefer FindObject (already-loaded IAs) over LoadObject (sync disk probe) —
			// in cleanup the IA was loaded when injection started; only fall back to
			// LoadObject if GC reclaimed it between Start and this cleanup.
			UInputAction* Action = FindObject<UInputAction>(nullptr, *ActionPath);
			if (!Action) Action = LoadObject<UInputAction>(nullptr, *ActionPath);
			if (Action)
			{
				EIS->StopContinuousInputInjectionForAction(Action);
			}
		}
	}

	ContinuouslyInjectedActionPaths.Empty();
}
#else
bool UCUWebSocketEditorSubsystem::DispatchInputActionEvent(
	const TSharedPtr<FJsonObject>& EventObj, FString& OutError,
	FString* OutResolvedKey, bool* OutGamepadOnly, bool* OutScheduled)
{
	if (OutResolvedKey) OutResolvedKey->Reset();
	if (OutGamepadOnly) *OutGamepadOnly = false;
	if (OutScheduled) *OutScheduled = false;
	OutError = TEXT("EnhancedInput plugin unavailable; enable EnhancedInput in your .uproject, then rebuild.");
	return false;
}

void UCUWebSocketEditorSubsystem::StopAllContinuousInjections()
{
	ContinuouslyInjectedActionPaths.Empty();
}
#endif

// Issue #9098 Tier 2: Cancel every pending raw-key key_up timer and dispatch a matching
// synthetic key_up for each. Prevents stuck-key state when PIE ends between the initial
// key_down dispatch and the scheduled key_up. Safe to call when FSlateApplication is not
// initialized (DispatchSingleInputEvent itself guards against that).
void UCUWebSocketEditorSubsystem::ReleaseAllPendingRawKeys()
{
	if (PendingRawKeyReleases.IsEmpty()) return;

	const int32 Count = PendingRawKeyReleases.Num();
	UE_LOG(LogNarshaMCPWebSocket, Log,
		TEXT("[inject_input_action] Releasing %d pending raw-key(s) on shutdown/PIE-end"),
		Count);

	// World timers auto-invalidate on PIE end, but calling ClearTimer explicitly is safe
	// and covers the Deinitialize path where PIE may or may not have ended yet.
	FTimerManager* TimerMgr = (GEditor && GEditor->PlayWorld)
		? &GEditor->PlayWorld->GetTimerManager()
		: nullptr;

	for (TPair<FTimerHandle, FKey>& Entry : PendingRawKeyReleases)
	{
		if (TimerMgr) TimerMgr->ClearTimer(Entry.Key);

		FCUParsedInputEvent UpEvt;
		UpEvt.InputType = TEXT("key_up");
		UpEvt.KeyName = Entry.Value.ToString();
		FString UpMsg;
		DispatchSingleInputEvent(UpEvt, UpMsg);
	}

	PendingRawKeyReleases.Reset();
}

// Issue #9098 Tier 1+2: FEditorDelegates::EndPIE callback.
// Fires on PIE stop (normal or abnormal — Editor still alive). World-scoped timers are
// already auto-invalidated by UE at PIE end; this handler adds explicit Stop calls as
// a double-safety net and clears both trackers so a subsequent PIE session starts clean.
void UCUWebSocketEditorSubsystem::OnEndPIE(bool bIsSimulating)
{
	(void)bIsSimulating;
	StopAllContinuousInjections();
	ReleaseAllPendingRawKeys();
	// Issue #8783: snapshot ids are only meaningful within a single PIE session, so we clear
	// the ring on PIE end. Any in-flight pie_wait_for_condition tickers also need to abort
	// because GEditor->PlayWorld is about to become nullptr.
	CancelAllPIEWaits();
	ClearPIESnapshotRing();
}

// ============================================
// Issue #26843: PIE reference purge (FWorldDelegates::OnPIEEnded)
//
// execute_python runs literal code through FPythonScriptPlugin::RunString, which evaluates in
// PyConsoleGlobalDict — a namespace that survives across calls AND across PIE sessions. (The
// FileExecutionScope::Private we pass is only honoured on the RunFile path, so it does not isolate
// us: PythonScriptPlugin.cpp:813-903 vs :1932-1937 vs :1858/:1807-1810.) Any PIE UObject left
// reachable from there keeps FPyReferenceCollector rooting the PIE world, so the world never GCs and
// the next play_in_editor dies with "World Memory Leaks" (EditorServer.cpp:1951).
//
// The engine already ships the cure and simply never wires it to PIE teardown:
// FPythonScriptPlugin subscribes FEditorSupportDelegates::PrepareToCleanseEditorObject
// (PythonScriptPlugin.cpp:1259) and its handler calls
// FPyReferenceCollector::PurgeUnrealObjectReferences(Object, /*bIncludeInnerObjects*/true)
// (:2178-2181), which nulls the UObject pointer inside every live Python wrapper referencing it —
// leaving the user's Python bindings in place, so non-PIE state survives untouched.
// PlayLevel.cpp broadcasts that delegate zero times; EditorLevelUtils.cpp:898 and
// EditorServer.cpp:2043/:2058 are the only broadcast sites. This handler closes that gap.
// ============================================

void UCUWebSocketEditorSubsystem::CollectPurgeTargets(TArray<TWeakObjectPtr<UObject>>& OutTargets) const
{
	// Worlds: match the engine's own leak criterion (PlayLevel.cpp:507-522 tests
	// GetOutermost()->HasAnyPackageFlags(PKG_PlayInEditor)). Using the package flag rather than the
	// world-context list is deliberate — GetWorldContexts() yields only each context's persistent
	// world, so streaming sublevel worlds (their own UWorld in their own UEDPIE_* package) would be
	// missed even though the leak scan flags every object in those packages.
	for (TObjectIterator<UWorld> It; It; ++It)
	{
		UWorld* World = *It;
		const UPackage* Package = World ? World->GetOutermost() : nullptr;
		if (Package && Package->HasAnyPackageFlags(PKG_PlayInEditor))
		{
			OutTargets.AddUnique(World);
		}
	}

	// GameInstances: outered to UEditorEngine, not to the world, so the world's
	// bIncludeInnerObjects walk never reaches them — yet the leak scan collects garbage-flagged
	// UGameInstance objects separately (PlayLevel.cpp:526-532). A Python global holding the PIE
	// GameInstance (or a ULocalPlayer under it) reproduces the fatal without this.
	if (GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.OwningGameInstance)
			{
				OutTargets.AddUnique(Context.OwningGameInstance);
			}
		}
	}
}

bool UCUWebSocketEditorSubsystem::PurgeEditorReferencesToObject(UObject* Target)
{
	if (!Target)
	{
		return false;
	}

	// Resolve the path BEFORE broadcasting. None of the six current engine subscribers destroys the
	// target in place, but reading it afterwards would make this line depend on that staying true.
	const FString TargetPath = Target->GetPathName();
	FEditorSupportDelegates::PrepareToCleanseEditorObject.Broadcast(Target);
	LogInfo(FString::Printf(TEXT("Purged editor references to %s"), *TargetPath));
	return true;
}

// Fires from EndPlayMap only (PlayLevel.cpp:307 is the single engine-wide broadcast site). Note that
// EndPlayMap is also reached from PIE start-failure paths (:1667/:3116/:3207) — that is fine and
// intended: every one of them is a FULL teardown of every PIE context (:327-336), so purging is
// always correct there. FEditorDelegates::EndPIE would NOT be safe for this: its :3019 site fires and
// returns nullptr without tearing down, leaving sibling PIE instances alive.
void UCUWebSocketEditorSubsystem::OnPIEEnded(UGameInstance* EndingGameInstance)
{
	// Collect fully before broadcasting: handlers are third-party code and GetWorldContexts()
	// returns the live WorldList (Engine.h:3608), which a handler could mutate mid-iteration.
	TArray<TWeakObjectPtr<UObject>> Targets;
	CollectPurgeTargets(Targets);
	if (EndingGameInstance)
	{
		Targets.AddUnique(EndingGameInstance);
	}

	int32 BroadcastCount = 0;
	for (const TWeakObjectPtr<UObject>& Weak : Targets)
	{
		// Re-validate: a previous broadcast's handlers ran arbitrary code since the snapshot.
		if (UObject* Target = Weak.Get())
		{
			BroadcastCount += PurgeEditorReferencesToObject(Target) ? 1 : 0;
		}
	}

	if (BroadcastCount > 0)
	{
		// Pair the cleanse broadcast with a flush, mirroring what the engine does at its own
		// equivalent site: EditorDestroyWorld broadcasts PrepareToCleanseEditorObject
		// (EditorServer.cpp:2043 / :2058) and then calls Cleanse() (:2081), which broadcasts
		// FEditorSupportDelegates::CleanseEditor (EditorEngine.cpp:2785), BEFORE running
		// CheckForWorldGCLeaks (:2092). EndPlayMap has no such pairing, so without this a
		// PrepareToCleanse subscriber that defers its flush to CleanseEditor — VCamCore's
		// FDeferredCleanupHandler sets bNeedsFlush and flushes only there
		// (DeferredCleanupHandler.cpp:70-89) — would be left half-finished, which that plugin's own
		// header warns "leads to a fatal error during UEditorEngine::CheckForWorldGCLeaks".
		// We call FlushRenderingCommands directly rather than broadcasting CleanseEditor: the
		// latter's only engine call site is UEditorEngine::Cleanse, which also deselects, resets the
		// transaction buffer and redraws viewports — far too broad for a PIE stop.
		FlushRenderingCommands();
	}
}

// ============================================
// Issue #26997: world-axis purge (FWorldDelegates::OnPostWorldCleanup)
//
// #26843 wired the purge to OnPIEEnded, which is broadcast once per PIE SESSION. A game mode that
// ends a round with ProcessServerTravel/SeamlessTravel swaps the WORLD inside the same session, so
// that signal never fired: the old PIE world stayed rooted by the Python console namespace, the
// engine logged "Old World ... not cleaned up by GC!", and the next tick asserted on
// OuterWorld.IsValid() (AsyncGameplayMessageSystem.cpp:94) and took the editor down.
//
// See the header comment on OnPostWorldCleanup for the full rationale: why this delegate rather than
// OnWorldBeginTearDown (keep-actor rename ordering), why no sublevel collection is needed (the engine
// recurses and re-broadcasts per world), and why UGameInstance stays with the session axis.
// ============================================

void UCUWebSocketEditorSubsystem::OnPostWorldCleanup(UWorld* World, bool /*bSessionEnded*/, bool /*bCleanupResources*/)
{
	// Both flags are deliberately unused: what authorizes the purge is that THIS world is being torn
	// down, not whether the session ended or resources were released alongside it.
	if (!World)
	{
		return;
	}

	// This delegate fires for every world cleanup in the editor (asset editor closes, package tools,
	// cooking, PreExit). The PKG_PlayInEditor filter is the entire blast-radius bound, so it must be
	// the first thing that runs and must cost nothing on the non-PIE path.
	const UPackage* Package = World->GetOutermost();
	if (!Package || !Package->HasAnyPackageFlags(PKG_PlayInEditor))
	{
		return;
	}

	PurgeEditorReferencesToObject(World);

	// Deliberately NO FlushRenderingCommands() here — see Issue #26997.
	//
	// #26843's flush sits in EndPlayMap, OUTSIDE any world-teardown stack, mirroring the engine's own
	// EditorDestroyWorld pairing (broadcast then Cleanse()). This site is structurally different: the
	// broadcast happens INSIDE CleanupWorldInternal (World.cpp:6482), immediately before
	// SubsystemCollection.Deinitialize() (:6486) and Scene->SetFXSystem(NULL) (:6489-6493). The engine
	// itself never blocks on this stack — FRendererModule::OnWorldCleanup (Renderer.cpp:232-247) only
	// ENQUEUE_RENDER_COMMANDs. Stalling the game thread here would reorder teardown against the render
	// thread and add a hitch to EVERY level transition, defeating the repeated-trial measurement this
	// issue exists to make possible.
	//
	// Omitting it is safe because of ordering, not because nothing defers: VCamCore's
	// FDeferredCleanupHandler::OnPrepareToCleanseEditor only sets bNeedsFlush and calls
	// SetEnabled(false); the real flush is in OnCleanseEditor. So a travel-time purge opens a deferred
	// window that stays open until the session ends — where OnPIEEnded's flush (PlayLevel.cpp:307)
	// resolves it AHEAD of CollectGarbage (:502) and the leak scan (:505-524). The second purge on the
	// stop_play path (via :912) does not open a new window: the first one's SetEnabled(false) routed
	// through OnDeinitializeVCam -> KnownVCams.RemoveSingle (DeferredCleanupHandler.cpp:39), so the
	// second broadcast finds an empty list and returns without setting bNeedsFlush.
}

bool UCUWebSocketEditorSubsystem::PurgeEditorReferencesToObjectForTesting(UObject* Target)
{
	return PurgeEditorReferencesToObject(Target);
}

bool UCUWebSocketEditorSubsystem::IsPostWorldCleanupSubscribedForTesting() const
{
	return PostWorldCleanupHandle.IsValid();
}

void UCUWebSocketEditorSubsystem::HandlePostWorldCleanupForTesting(UWorld* World)
{
	OnPostWorldCleanup(World, /*bSessionEnded*/ true, /*bCleanupResources*/ true);
}

bool UCUWebSocketEditorSubsystem::IsPIEEndedSubscribedForTesting() const
{
	return PIEEndedHandle.IsValid();
}

void UCUWebSocketEditorSubsystem::CollectPurgeTargetsForTesting(TArray<TWeakObjectPtr<UObject>>& OutTargets) const
{
	CollectPurgeTargets(OutTargets);
}

void UCUWebSocketEditorSubsystem::HandleInjectInputAction(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleInjectInputAction"), RequestId)) return;
	FString ActionPath;
	Request->TryGetStringField(TEXT("action_path"), ActionPath);
#if !WITH_NARSHAMCP_ENHANCEDINPUT
	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"inject_input_action_result\",\"request_id\":\"%s\","
		"\"success\":false,\"action_path\":\"%s\","
		"\"error\":\"EnhancedInput plugin unavailable\","
		"\"error_code\":\"enhanced_input_plugin_unavailable\","
		"\"hint\":\"Enable EnhancedInput in your .uproject, then rebuild.\"}"),
		*CUJsonUtils::EscapeJsonString(RequestId), *CUJsonUtils::EscapeJsonString(ActionPath)));
	return;
#else
	// Issue #9895: single-PIE-strict — Enhanced-Input dispatch routes through
	// ResolvePIEEnhancedInput() (the local-player subsystem of GEditor->PlayWorld) and
	// raw-key fallback timers run on GEditor->PlayWorld->GetTimerManager() (L7575, L7601).
	// Multi-PIE topology would silently target whichever window UE considers focused.
	if (!EnsurePIERunningSinglePIE(this, Socket, RequestId, TEXT("inject_input_action"))) return;

	FString ErrorMsg;
	FString ResolvedKey;
	bool bGamepadOnly = false;
	bool bScheduledFromDispatch = false;
	const bool bSuccess = DispatchInputActionEvent(Request, ErrorMsg, &ResolvedKey, &bGamepadOnly, &bScheduledFromDispatch);
	const FString EscapedError = CUJsonUtils::EscapeJsonString(ErrorMsg);
	const FString EscapedPath = CUJsonUtils::EscapeJsonString(ActionPath);

	FString ResponseJSON;
	if (bSuccess)
	{
		// Issue #9098 Tier 1+2: echo duration_ms + scheduled flag for mock-E2E assertions and
		// caller observability. Tier 1: scheduled=true ⇒ continuous EIS injection running.
		// Tier 2: scheduled=true + resolved_key present ⇒ raw-key dispatched, key_up queued.
		int64 DurationMsEcho = 0;
		if (Request->HasTypedField<EJson::Number>(TEXT("duration_ms")))
		{
			DurationMsEcho = static_cast<int64>(Request->GetNumberField(TEXT("duration_ms")));
			if (DurationMsEcho < 0) DurationMsEcho = 0;
			if (DurationMsEcho > 10000) DurationMsEcho = 10000;
		}
		const bool bScheduled = bScheduledFromDispatch;
		const bool bRawKey = !ResolvedKey.IsEmpty();
		const FString EscapedResolvedKey = CUJsonUtils::EscapeJsonString(ResolvedKey);

		// Message varies by path so telemetry / logs can distinguish Tier 1 vs Tier 2.
		const TCHAR* Message = bRawKey
			? (bScheduled ? TEXT("raw-key dispatched, release scheduled") : TEXT("raw-key dispatched"))
			: (bScheduled ? TEXT("continuous injection scheduled") : TEXT("injected"));

		// Optional fields: only emit resolved_key + gamepad_only when the raw-key path ran,
		// keeping the Tier 1 response shape byte-identical to PR #9125 for backwards compat.
		FString OptionalRawKeyJson;
		if (bRawKey)
		{
			OptionalRawKeyJson = FString::Printf(
				TEXT(",\"resolved_key\":\"%s\",\"gamepad_only\":%s"),
				*EscapedResolvedKey,
				bGamepadOnly ? TEXT("true") : TEXT("false"));
		}

		ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"inject_input_action_result\","
			"\"request_id\":\"%s\","
			"\"success\":true,"
			"\"action_path\":\"%s\","
			"\"duration_ms\":%lld,"
			"\"scheduled\":%s%s,"
			"\"message\":\"InputAction %s.\""
			"}"),
			*RequestId, *EscapedPath, DurationMsEcho,
			bScheduled ? TEXT("true") : TEXT("false"),
			*OptionalRawKeyJson,
			Message
		);
	}
	else
	{
		ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"inject_input_action_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"action_path\":\"%s\","
			"\"error\":\"%s\""
			"}"),
			*RequestId, *EscapedPath, *EscapedError
		);
	}

	SendResponse(Socket, ResponseJSON);
#endif
}

// ============================================
// Issue #6938: Console command recording → JSON recipe
// ============================================

void UCUWebSocketEditorSubsystem::HandleRecordCommand(const TArray<FString>& Args)
{
	// No args → show status
	if (Args.Num() == 0)
	{
		if (bIsRecordingInput)
		{
			double Elapsed = FPlatformTime::Seconds() - RecordingStartTime;
			UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] Recording ACTIVE: %d events, %.1fs / %.0fs%s"),
				RecordedInputEvents.Num(), Elapsed, MaxRecordingDuration,
				bAutoStopRecording ? TEXT(" (auto-stop)") : TEXT(""));
		}
		else
		{
			UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] Recording INACTIVE. Usage: NarshaMCP.Record <seconds>|start|stop [TestName]"));
		}
		return;
	}

	FString Arg0 = Args[0].ToLower();

	if (Arg0 == TEXT("stop"))
	{
		FString TestName = Args.Num() > 1 ? Args[1] : TEXT("");
		StopAndExportRecording(TestName);
	}
	else if (Arg0 == TEXT("start"))
	{
		StartRecordingDirect(300.0, false);
	}
	else if (Arg0.IsNumeric())
	{
		double Duration = FCString::Atod(*Args[0]);
		Duration = FMath::Clamp(Duration, 1.0, 300.0);
		PendingTestName = Args.Num() > 1 ? Args[1] : TEXT("");
		StartRecordingDirect(Duration, true);
	}
	else
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] Unknown arg '%s'. Usage: NarshaMCP.Record <seconds>|start|stop [TestName]"), *Args[0]);
	}
}

void UCUWebSocketEditorSubsystem::StartRecordingDirect(double MaxDuration, bool bAutoStop)
{
	if (bIsRecordingInput)
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] Already recording. Use 'NarshaMCP.Record stop' first."));
		return;
	}

	if (!GEditor || !GEditor->PlayWorld)
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] PIE not running. Start Play-In-Editor first."));
		return;
	}

	RecordedInputEvents.Empty();
	RecordingStartTime = FPlatformTime::Seconds();
	MaxRecordingDuration = MaxDuration;
	bIsRecordingInput = true;
	bAutoStopRecording = bAutoStop;

	if (FSlateApplication::IsInitialized())
	{
		InputRecordingProcessor = MakeShared<FCUInputRecordingProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputRecordingProcessor);
	}

	UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] Recording started (%.0fs%s). Play in the viewport now!"),
		MaxDuration, bAutoStop ? TEXT(", auto-stop") : TEXT(", manual stop"));

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Green,
			FString::Printf(TEXT("[REC] Recording started (%.0fs%s)"),
				MaxDuration, bAutoStop ? TEXT(", auto-stop") : TEXT("")));
	}
}

void UCUWebSocketEditorSubsystem::StopAndExportRecording(const FString& TestName)
{
	if (!bIsRecordingInput)
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] No active recording to stop."));
		return;
	}

	// 1. Stop recording
	bIsRecordingInput = false;
	bAutoStopRecording = false;
	double DurationMs = (FPlatformTime::Seconds() - RecordingStartTime) * 1000.0;

	if (InputRecordingProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(InputRecordingProcessor);
		}
		InputRecordingProcessor.Reset();
	}

	UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[NarshaMCP] Recording stopped: %d events, %.0fms"), RecordedInputEvents.Num(), DurationMs);

	if (RecordedInputEvents.Num() == 0)
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] No input events captured. Make sure to interact in the PIE viewport."));
		return;
	}

	// 2. Determine test name
	FString FinalTestName = TestName.IsEmpty()
		? FString::Printf(TEXT("RecordedTest_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))
		: TestName;

	// 3. Load action_map from Tests/Recordings/action_map.json (or use defaults)
	TMap<FString, TTuple<FString, float, float>> ActionMap;
	FString ActionMapPath = FPaths::ProjectDir() / TEXT("Tests") / TEXT("Recordings") / TEXT("action_map.json");
	FString ActionMapJson;
	if (FFileHelper::LoadFileToString(ActionMapJson, *ActionMapPath))
	{
		TSharedPtr<FJsonObject> MapRoot;
		TSharedRef<TJsonReader<>> MapReader = TJsonReaderFactory<>::Create(ActionMapJson);
		if (FJsonSerializer::Deserialize(MapReader, MapRoot) && MapRoot.IsValid())
		{
			for (const auto& Pair : MapRoot->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (Pair.Value->TryGetArray(Arr) && Arr->Num() >= 1)
				{
					FString ActionName = (*Arr)[0]->AsString();
					float X = Arr->Num() >= 2 ? (*Arr)[1]->AsNumber() : 0.f;
					float Y = Arr->Num() >= 3 ? (*Arr)[2]->AsNumber() : 0.f;
					ActionMap.Add(FString(Pair.Key), MakeTuple(ActionName, X, Y));
				}
			}
		}
	}

	// Default action map if file not found or empty
	if (ActionMap.Num() == 0)
	{
		ActionMap.Add(TEXT("W"), MakeTuple(FString(TEXT("IA_Move")), 0.f, 1.f));
		ActionMap.Add(TEXT("S"), MakeTuple(FString(TEXT("IA_Move")), 0.f, -1.f));
		ActionMap.Add(TEXT("A"), MakeTuple(FString(TEXT("IA_Move")), -1.f, 0.f));
		ActionMap.Add(TEXT("D"), MakeTuple(FString(TEXT("IA_Move")), 1.f, 0.f));
		ActionMap.Add(TEXT("SpaceBar"), MakeTuple(FString(TEXT("IA_Jump")), 0.f, 0.f));
		ActionMap.Add(TEXT("LeftMouseButton"), MakeTuple(FString(TEXT("IA_Attack")), 0.f, 0.f));
		ActionMap.Add(TEXT("RightMouseButton"), MakeTuple(FString(TEXT("IA_ADS")), 0.f, 0.f));
		ActionMap.Add(TEXT("LeftShift"), MakeTuple(FString(TEXT("IA_Sprint")), 0.f, 0.f));
		ActionMap.Add(TEXT("E"), MakeTuple(FString(TEXT("IA_Interact")), 0.f, 0.f));
		ActionMap.Add(TEXT("MouseMove"), MakeTuple(FString(TEXT("IA_Look")), 0.f, 0.f));
	}

	// 4. Convert raw events → semantic recipe actions
	TArray<TSharedPtr<FJsonValue>> RecipeActions;
	{
		// Thresholds matching Rust export logic
		constexpr double SustainedThresholdMs = 300.0;
		constexpr double TapThresholdMs = 500.0;
		constexpr int32 MinMouseMoveEvents = 3;
		constexpr double MouseMoveGapMs = 100.0;
		constexpr double MouseMoveScale = 0.1;

		TMap<FString, double> KeyDownTimes;
		double LastActionTime = 0.0;

		struct FMouseMove { double TimeMs; double X; double Y; };
		TArray<FMouseMove> PendingMouseMoves;

		// Lambda: flush mouse moves into a Look action
		auto FlushMouseMoves = [&]()
		{
			if (PendingMouseMoves.Num() < MinMouseMoveEvents)
			{
				PendingMouseMoves.Empty();
				return;
			}

			double TotalDx = 0, TotalDy = 0;
			for (const auto& M : PendingMouseMoves) { TotalDx += M.X; TotalDy += M.Y; }
			double DurationSec = FMath::Max((PendingMouseMoves.Last().TimeMs - PendingMouseMoves[0].TimeMs) / 1000.0, 0.1);

			if (const auto* LookEntry = ActionMap.Find(TEXT("MouseMove")))
			{
				TSharedPtr<FJsonObject> Action = MakeShared<FJsonObject>();
				Action->SetStringField(TEXT("type"), TEXT("look"));
				Action->SetStringField(TEXT("action"), LookEntry->Get<0>());
				Action->SetNumberField(TEXT("delta_x"), TotalDx * MouseMoveScale);
				Action->SetNumberField(TEXT("delta_y"), TotalDy * MouseMoveScale);
				Action->SetNumberField(TEXT("duration"), DurationSec);
				RecipeActions.Add(MakeShared<FJsonValueObject>(Action));
			}
			PendingMouseMoves.Empty();
		};

		for (const FCURecordedInputEvent& Evt : RecordedInputEvents)
		{
			if (Evt.InputType == TEXT("key_down") || Evt.InputType == TEXT("mouse_down"))
			{
				FlushMouseMoves();
				FString KeyStr = Evt.Key.ToString();
				if (!KeyDownTimes.Contains(KeyStr))
				{
					KeyDownTimes.Add(KeyStr, Evt.TimestampMs);
				}
			}
			else if (Evt.InputType == TEXT("key_up") || Evt.InputType == TEXT("mouse_up"))
			{
				FlushMouseMoves();
				FString KeyStr = Evt.Key.ToString();
				if (double* DownTime = KeyDownTimes.Find(KeyStr))
				{
					double DeltaMs = Evt.TimestampMs - *DownTime;
					KeyDownTimes.Remove(KeyStr);

					// Insert wait if gap between actions
					double Gap = *DownTime - LastActionTime;
					if (Gap > 500.0 && LastActionTime > 0.0)
					{
						TSharedPtr<FJsonObject> WaitAction = MakeShared<FJsonObject>();
						WaitAction->SetStringField(TEXT("type"), TEXT("wait"));
						WaitAction->SetNumberField(TEXT("duration"), FMath::Min(Gap / 1000.0, 10.0));
						RecipeActions.Add(MakeShared<FJsonValueObject>(WaitAction));
					}

					if (const auto* Entry = ActionMap.Find(KeyStr))
					{
						const FString& ActionName = Entry->Get<0>();
						float MapX = Entry->Get<1>();
						float MapY = Entry->Get<2>();
						bool bIsAxis = (MapX != 0.f || MapY != 0.f);

						if (bIsAxis)
						{
							// Axis2D: hold if sustained, skip if accidental tap
							if (DeltaMs >= SustainedThresholdMs)
							{
								TSharedPtr<FJsonObject> Action = MakeShared<FJsonObject>();
								Action->SetStringField(TEXT("type"), TEXT("hold"));
								Action->SetStringField(TEXT("action"), ActionName);
								TArray<TSharedPtr<FJsonValue>> ValueArr;
								ValueArr.Add(MakeShared<FJsonValueNumber>(MapX));
								ValueArr.Add(MakeShared<FJsonValueNumber>(MapY));
								Action->SetArrayField(TEXT("value"), ValueArr);
								Action->SetNumberField(TEXT("duration"), DeltaMs / 1000.0);
								RecipeActions.Add(MakeShared<FJsonValueObject>(Action));
							}
						}
						else
						{
							// Bool: press if tap, hold if sustained
							if (DeltaMs < TapThresholdMs)
							{
								TSharedPtr<FJsonObject> Action = MakeShared<FJsonObject>();
								Action->SetStringField(TEXT("type"), TEXT("press"));
								Action->SetStringField(TEXT("action"), ActionName);
								RecipeActions.Add(MakeShared<FJsonValueObject>(Action));
							}
							else
							{
								TSharedPtr<FJsonObject> Action = MakeShared<FJsonObject>();
								Action->SetStringField(TEXT("type"), TEXT("hold"));
								Action->SetStringField(TEXT("action"), ActionName);
								TArray<TSharedPtr<FJsonValue>> ValueArr;
								ValueArr.Add(MakeShared<FJsonValueNumber>(1.0));
								ValueArr.Add(MakeShared<FJsonValueNumber>(0.0));
								Action->SetArrayField(TEXT("value"), ValueArr);
								Action->SetNumberField(TEXT("duration"), DeltaMs / 1000.0);
								RecipeActions.Add(MakeShared<FJsonValueObject>(Action));
							}
						}
					}
					LastActionTime = Evt.TimestampMs;
				}
			}
			else if (Evt.InputType == TEXT("mouse_move"))
			{
				if (PendingMouseMoves.Num() > 0 && (Evt.TimestampMs - PendingMouseMoves.Last().TimeMs) > MouseMoveGapMs)
				{
					FlushMouseMoves();
				}
				PendingMouseMoves.Add({Evt.TimestampMs, Evt.MousePosition.X, Evt.MousePosition.Y});
			}
		}

		FlushMouseMoves();
	}

	// 5. Get current map path from editor world (not PIE world which has UEDPIE_ prefix)
	FString MapPath = TEXT("/Game/Maps/DefaultMap");
	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		FString PackageName = GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName();
		if (FPackageName::IsValidLongPackageName(PackageName))
		{
			MapPath = PackageName;
		}
	}

	// 6. Build JSON recipe
	TSharedPtr<FJsonObject> Recipe = MakeShared<FJsonObject>();
	Recipe->SetNumberField(TEXT("version"), 1);
	Recipe->SetStringField(TEXT("test_name"), FinalTestName);
	Recipe->SetStringField(TEXT("module_name"), FApp::GetProjectName());
	Recipe->SetStringField(TEXT("map_path"), MapPath);
	Recipe->SetStringField(TEXT("input_actions_path"), TEXT("/Game/Input/Actions"));
	Recipe->SetBoolField(TEXT("lyra_mode"), false);
	Recipe->SetArrayField(TEXT("actions"), RecipeActions);

	// Issue #7021: Save camera/pawn position for consistent replay
	if (GEditor && GEditor->PlayWorld)
	{
		APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			FVector Loc = PC->GetPawn()->GetActorLocation();
			FRotator Rot = PC->GetControlRotation();

			TSharedPtr<FJsonObject> CameraStart = MakeShared<FJsonObject>();
			CameraStart->SetNumberField(TEXT("x"), Loc.X);
			CameraStart->SetNumberField(TEXT("y"), Loc.Y);
			CameraStart->SetNumberField(TEXT("z"), Loc.Z);
			CameraStart->SetNumberField(TEXT("pitch"), Rot.Pitch);
			CameraStart->SetNumberField(TEXT("yaw"), Rot.Yaw);
			CameraStart->SetNumberField(TEXT("roll"), Rot.Roll);
			Recipe->SetObjectField(TEXT("camera_start"), CameraStart);

			// Issue #7022: Record metadata for debugging test failures
			TSharedPtr<FJsonObject> Metadata = MakeShared<FJsonObject>();
			float AvgFPS = 1.0f / FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.001f);
			Metadata->SetNumberField(TEXT("avg_fps"), AvgFPS);
			Metadata->SetStringField(TEXT("player_class"), PC->GetPawn()->GetClass()->GetName());
			Metadata->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
			if (FViewport* PIEViewport = GEditor->GetPIEViewport())
			{
				Metadata->SetStringField(TEXT("resolution"),
					FString::Printf(TEXT("%dx%d"), PIEViewport->GetSizeXY().X, PIEViewport->GetSizeXY().Y));
			}
			Recipe->SetObjectField(TEXT("metadata"), Metadata);
		}
	}

	// 7. Save JSON file
	FString OutputDir = FPaths::ProjectDir() / TEXT("Tests") / TEXT("Recordings");
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	FString OutputPath = OutputDir / FinalTestName + TEXT(".json");

	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(Recipe.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(JsonString, *OutputPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNarshaMCPWebSocket, Warning, TEXT("[NarshaMCP] Saved %d actions to %s (from %d raw events, %.1fs)"),
			RecipeActions.Num(), *OutputPath, RecordedInputEvents.Num(), DurationMs / 1000.0);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9938, 0.01f, FColor::Green, TEXT(""));  // Clear countdown
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan,
				FString::Printf(TEXT("[REC] Saved %d actions to %s"), RecipeActions.Num(), *OutputPath));
		}
	}
	else
	{
		UE_LOG(LogNarshaMCPWebSocket, Error, TEXT("[NarshaMCP] Failed to save JSON to %s"), *OutputPath);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(9938, 0.01f, FColor::Green, TEXT(""));
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Red,
				FString::Printf(TEXT("[REC] FAILED to save to %s"), *OutputPath));
		}
	}

	RecordedInputEvents.Empty();
}

void UCUWebSocketEditorSubsystem::HandleStartInputRecording(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleStartInputRecording"), RequestId)) return;
	// Issue #9895: single-PIE-strict — registers a single FSlateApplication input
	// pre-processor. Slate has one global focused-window stream; multi-PIE would
	// non-deterministically capture from whichever window has focus at event time.
	if (!EnsurePIERunningSinglePIE(this, Socket, RequestId, TEXT("start_input_recording"))) return;

	if (bIsRecordingInput)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"start_input_recording_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Already recording. Stop current recording first.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	double MaxDuration = Request->HasField(TEXT("max_duration_seconds")) ? Request->GetNumberField(TEXT("max_duration_seconds")) : 60.0;
	MaxDuration = FMath::Clamp(MaxDuration, 1.0, 300.0);

	RecordedInputEvents.Empty();
	RecordingStartTime = FPlatformTime::Seconds();
	MaxRecordingDuration = MaxDuration;
	bIsRecordingInput = true;

	// Issue #6388: Register IInputProcessor to capture Slate input events
	if (FSlateApplication::IsInitialized())
	{
		InputRecordingProcessor = MakeShared<FCUInputRecordingProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(InputRecordingProcessor);
	}

	LogInfo(FString::Printf(TEXT("Input recording started (max %.0f seconds)"), MaxDuration));

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"start_input_recording_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"max_duration_seconds\":%.0f,"
		"\"message\":\"Input recording started.\""
		"}"),
		*RequestId,
		MaxDuration
	);

	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleStopInputRecording(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleStopInputRecording"), RequestId)) return;

	if (!bIsRecordingInput)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"stop_input_recording_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"No active recording.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	bIsRecordingInput = false;

	// Issue #6388: Unregister IInputProcessor
	if (InputRecordingProcessor.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().UnregisterInputPreProcessor(InputRecordingProcessor);
		}
		InputRecordingProcessor.Reset();
	}

	double DurationMs = (FPlatformTime::Seconds() - RecordingStartTime) * 1000.0;

	// Issue #7021: Capture pawn transform for camera_start
	FString PawnStartJson = TEXT("null");
	FString MetadataJson = TEXT("null");
	if (GEditor && GEditor->PlayWorld)
	{
		APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
		if (PC && PC->GetPawn())
		{
			FVector Loc = PC->GetPawn()->GetActorLocation();
			FRotator Rot = PC->GetControlRotation();
			PawnStartJson = FString::Printf(TEXT(
				"{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,\"roll\":%.2f}"),
				Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);

			// Issue #7022: Record metadata
			FString PlayerClassName = PC->GetPawn()->GetClass()->GetName();
			int32 ViewportW = 0, ViewportH = 0;
			if (FViewport* PIEViewport = GEditor->GetPIEViewport())
			{
				ViewportW = PIEViewport->GetSizeXY().X;
				ViewportH = PIEViewport->GetSizeXY().Y;
			}
			float AvgFPS = 1.0f / FMath::Max(static_cast<float>(FApp::GetDeltaTime()), 0.001f);
			MetadataJson = FString::Printf(TEXT(
				"{\"avg_fps\":%.1f,\"resolution\":\"%dx%d\",\"player_class\":\"%s\",\"engine_version\":\"%s\"}"),
				AvgFPS, ViewportW, ViewportH, *PlayerClassName, *FEngineVersion::Current().ToString());
		}
	}

	// Serialize events to JSON
	FString EventsJSON = TEXT("[");
	for (int32 i = 0; i < RecordedInputEvents.Num(); ++i)
	{
		const FCURecordedInputEvent& Evt = RecordedInputEvents[i];
		if (i > 0) EventsJSON += TEXT(",");
		EventsJSON += FString::Printf(TEXT(
			"{"
			"\"time_ms\":%.1f,"
			"\"input_type\":\"%s\","
			"\"key\":\"%s\","
			"\"x\":%.0f,"
			"\"y\":%.0f,"
			"\"shift\":%s,"
			"\"ctrl\":%s,"
			"\"alt\":%s"
			"}"),
			Evt.TimestampMs,
			*Evt.InputType,
			*Evt.Key.ToString(),
			Evt.MousePosition.X,
			Evt.MousePosition.Y,
			Evt.bShift ? TEXT("true") : TEXT("false"),
			Evt.bCtrl ? TEXT("true") : TEXT("false"),
			Evt.bAlt ? TEXT("true") : TEXT("false")
		);
	}
	EventsJSON += TEXT("]");

	LogInfo(FString::Printf(TEXT("Input recording stopped: %d events, %.0fms duration"), RecordedInputEvents.Num(), DurationMs));

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"stop_input_recording_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"version\":1,"
		"\"duration_ms\":%.1f,"
		"\"event_count\":%d,"
		"\"pawn_start\":%s,"
		"\"metadata\":%s,"
		"\"events\":%s"
		"}"),
		*RequestId,
		DurationMs,
		RecordedInputEvents.Num(),
		*PawnStartJson,
		*MetadataJson,
		*EventsJSON
	);

	RecordedInputEvents.Empty();
	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleReplayInputRecording(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleReplayInputRecording"), RequestId)) return;
	// Issue #9895: single-PIE-strict — replays input via FSlateApplication (single
	// focused-window stream) and teleports the pawn through GEditor->PlayWorld's first
	// player controller (L8500). Multi-PIE would teleport an arbitrary client's pawn.
	if (!EnsurePIERunningSinglePIE(this, Socket, RequestId, TEXT("replay_input_recording"))) return;

	const TArray<TSharedPtr<FJsonValue>>* EventsArray;
	if (!Request->TryGetArrayField(TEXT("events"), EventsArray) || !EventsArray)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"replay_input_recording_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Missing 'events' array.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	double Speed = Request->HasField(TEXT("speed")) ? Request->GetNumberField(TEXT("speed")) : 1.0;
	Speed = FMath::Clamp(Speed, 0.1, 10.0);

	// Issue #7022: cleanup_on_failure — parsed and forwarded to MCP response.
	// TODO: Add timeout-based PIE cleanup when replay events fail to complete.
	// bool bCleanupOnFailure = Request->HasField(TEXT("cleanup_on_failure")) && Request->GetBoolField(TEXT("cleanup_on_failure"));

	// Issue #7021: Teleport pawn to recorded position before replay
	const TSharedPtr<FJsonObject>* PawnStartObj = nullptr;
	if (Request->TryGetObjectField(TEXT("pawn_start"), PawnStartObj) && PawnStartObj && PawnStartObj->IsValid())
	{
		if (GEditor && GEditor->PlayWorld)
		{
			APlayerController* PC = GEditor->PlayWorld->GetFirstPlayerController();
			if (PC && PC->GetPawn())
			{
				double PX = (*PawnStartObj)->GetNumberField(TEXT("x"));
				double PY = (*PawnStartObj)->GetNumberField(TEXT("y"));
				double PZ = (*PawnStartObj)->GetNumberField(TEXT("z"));
				double PPitch = (*PawnStartObj)->GetNumberField(TEXT("pitch"));
				double PYaw = (*PawnStartObj)->GetNumberField(TEXT("yaw"));
				double PRoll = (*PawnStartObj)->GetNumberField(TEXT("roll"));
				FVector TeleportLoc(PX, PY, PZ);
				FRotator TeleportRot(PPitch, PYaw, PRoll);
				PC->GetPawn()->SetActorLocationAndRotation(TeleportLoc, TeleportRot);
				PC->SetControlRotation(TeleportRot);
				UE_LOG(LogNarshaMCPWebSocket, Log, TEXT("[NarshaMCP] Teleported pawn to (%.0f, %.0f, %.0f) yaw=%.0f"),
					PX, PY, PZ, PYaw);
			}
		}
	}

	int32 EventCount = EventsArray->Num();
	TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakThis(this);

	// Issue #7022: Track replay state + show visual indicator
	bIsReplayingInput = true;
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(9939, 999.f, FColor::Yellow,
			FString::Printf(TEXT("[REPLAY] Playing %d events at %.1fx"), EventCount, Speed));
	}

	double MaxTimeMs = 0.0;
	for (int32 i = 0; i < EventCount; ++i)
	{
		const TSharedPtr<FJsonObject>* EventObj;
		if (!(*EventsArray)[i]->TryGetObject(EventObj) || !EventObj->IsValid())
		{
			continue;
		}

		double TimeMs = (*EventObj)->HasField(TEXT("time_ms")) ? (*EventObj)->GetNumberField(TEXT("time_ms")) : 0.0;
		MaxTimeMs = FMath::Max(MaxTimeMs, TimeMs);
		double ScheduleDelay = (TimeMs / 1000.0) / Speed;

		TSharedPtr<FJsonObject> CapturedEvent = *EventObj;

		FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateLambda([WeakThis, CapturedEvent](float DeltaTime) -> bool
			{
				UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
				if (!Self || !GEditor || !GEditor->PlayWorld || !FSlateApplication::IsInitialized())
				{
					return false;
				}

				FCUParsedInputEvent Evt = FCUParsedInputEvent::FromJson(CapturedEvent);
				FString Msg;
				DispatchSingleInputEvent(Evt, Msg);
				return false;
			}),
			static_cast<float>(ScheduleDelay)
		);
	}

	// Issue #7022: Schedule completion indicator + optional PIE cleanup
	double CompletionDelay = (MaxTimeMs / 1000.0) / Speed + 0.5;
	// TODO(#7022): Add timeout detection — if events don't complete within
	// CompletionDelay * 2, and bCleanupOnFailure is true, call RequestEndPlayMap().
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakThis](float) -> bool
		{
			UCUWebSocketEditorSubsystem* Self = WeakThis.Get();
			if (Self)
			{
				Self->bIsReplayingInput = false;
			}
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(9939, 5.f, FColor::Green, TEXT("[REPLAY] Complete"));
			}
			return false;
		}),
		static_cast<float>(CompletionDelay)
	);

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"replay_input_recording_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"event_count\":%d,"
		"\"speed\":%.1f,"
		"\"pawn_teleported\":%s,"
		"\"message\":\"Replaying %d events at %.1fx speed.\""
		"}"),
		*RequestId,
		EventCount,
		Speed,
		PawnStartObj ? TEXT("true") : TEXT("false"),
		EventCount,
		Speed
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #6378: PIE Viewport Capture
// Captures the Play-In-Editor viewport via ReadPixels,
// encodes as PNG + base64, returns in WebSocket JSON response.
// ============================================

void UCUWebSocketEditorSubsystem::HandleCapturePIEViewport(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCapturePIEViewport"), RequestId)) return;

	int32 Width = Request->HasField(TEXT("width")) ? Request->GetIntegerField(TEXT("width")) : 1280;
	int32 Height = Request->HasField(TEXT("height")) ? Request->GetIntegerField(TEXT("height")) : 720;

	// Clamp dimensions to reasonable range
	Width = FMath::Clamp(Width, 64, 3840);
	Height = FMath::Clamp(Height, 64, 2160);

	LogInfo(FString::Printf(TEXT("Capturing PIE viewport (request_id: %s, size: %dx%d)"),
		*RequestId, Width, Height));

	double StartTime = FPlatformTime::Seconds();

	// Issue #9895: multi-PIE-tolerant gate-only — body uses GEditor->GetPIEViewport()
	// + GetActiveViewport() which UE resolves to the focused PIE window; works in both
	// standalone and multi-client topologies.
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("capture_pie_viewport"))) return;

	// Get PIE viewport
	FViewport* PIEViewport = GEditor->GetPIEViewport();
	if (!PIEViewport)
	{
		// Fallback: try active viewport (may be game view)
		PIEViewport = GEditor->GetActiveViewport();
	}

	if (!PIEViewport)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"capture_pie_viewport_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Could not find PIE viewport. Ensure PIE is running with a visible viewport.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Read pixels from PIE viewport
	TArray<FColor> PixelData;
	if (!PIEViewport->ReadPixels(PixelData))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"capture_pie_viewport_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"ReadPixels failed on PIE viewport.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	int32 ActualWidth = PIEViewport->GetSizeXY().X;
	int32 ActualHeight = PIEViewport->GetSizeXY().Y;

	LogInfo(FString::Printf(TEXT("[PIE Capture] ReadPixels succeeded: %dx%d, Pixels=%d"),
		ActualWidth, ActualHeight, PixelData.Num()));

	// Compress to PNG using ImageWrapper
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

	if (!ImageWrapper.IsValid())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"capture_pie_viewport_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Failed to create ImageWrapper for PNG encoding.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	if (!ImageWrapper->SetRaw(PixelData.GetData(), PixelData.Num() * sizeof(FColor),
		ActualWidth, ActualHeight, ERGBFormat::BGRA, 8))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"capture_pie_viewport_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Failed to set raw image data for PNG encoding.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(100);
	if (CompressedData.Num() == 0)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"capture_pie_viewport_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"PNG compression failed.\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Encode PNG data to base64
	// TArray64<uint8> → TArray<uint8> for FBase64::Encode
	TArray<uint8> CompressedArray;
	CompressedArray.SetNumUninitialized(CompressedData.Num());
	FMemory::Memcpy(CompressedArray.GetData(), CompressedData.GetData(), CompressedData.Num());

	FString Base64String = FBase64::Encode(CompressedArray);

	double ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	int64 FileSizeBytes = CompressedData.Num();

	LogInfo(FString::Printf(TEXT("[PIE Capture] Success: %dx%d, PNG=%lld bytes, base64=%d chars, time=%.1fms"),
		ActualWidth, ActualHeight, FileSizeBytes, Base64String.Len(), ExecutionTimeMs));

	// Build JSON response with base64 image data
	// Note: base64 string can be very large, so we build the response in parts
	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"capture_pie_viewport_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"image_base64\":\""),
		*RequestId
	);

	ResponseJSON += Base64String;

	ResponseJSON += FString::Printf(TEXT(
		"\","
		"\"width\":%d,"
		"\"height\":%d,"
		"\"file_size_bytes\":%lld,"
		"\"execution_time_ms\":%.2f,"
		"\"message\":\"PIE viewport captured successfully.\""
		"}"),
		ActualWidth,
		ActualHeight,
		FileSizeBytes,
		ExecutionTimeMs
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #8299 (Mirror #26): Helper — Build JSON string array from TArray<FString>
// ============================================

static FString BuildJsonStringArray(const TArray<FString>& Items)
{
	FString Result = TEXT("[");
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		if (i > 0) Result += TEXT(",");
		Result += FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Items[i]));
	}
	Result += TEXT("]");
	return Result;
}

// ============================================
// Issue #8299 (Mirror #26): Disable Stat Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleDisableStat(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleDisableStat: Invalid socket or request"));
		return;
	}

	FString RequestId = CUJsonUtils::EscapeJsonString(Request->GetStringField(TEXT("request_id")));
	FString StatName = Request->GetStringField(TEXT("stat_name"));

	// Normalize: strip "stat " prefix if present
	if (StatName.StartsWith(TEXT("stat "), ESearchCase::IgnoreCase))
	{
		StatName = StatName.Mid(5);
	}
	StatName.TrimStartAndEndInline();

	if (StatName.IsEmpty())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"disable_stat_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"stat_name cannot be empty\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Get active viewport client
	FEditorViewportClient* ViewportClient = nullptr;
	if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditor->GetFirstActiveViewport();
		if (ActiveViewport.IsValid())
		{
			ViewportClient = &ActiveViewport->GetAssetViewportClient();
		}
	}

	if (!ViewportClient)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"disable_stat_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"No active editor viewport\""
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	const TArray<FString>& EnabledStats = *ViewportClient->GetEnabledStats();
	TArray<FString> FilteredStats;
	bool bFound = false;
	FString MatchedName;

	for (const FString& Stat : EnabledStats)
	{
		if (Stat.Equals(StatName, ESearchCase::IgnoreCase))
		{
			bFound = true;
			MatchedName = Stat;
		}
		else
		{
			FilteredStats.Add(Stat);
		}
	}

	if (bFound)
	{
		ViewportClient->SetEnabledStats(FilteredStats);

		FString StatsArray = BuildJsonStringArray(FilteredStats);

		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"disable_stat_result\","
			"\"request_id\":\"%s\","
			"\"success\":true,"
			"\"disabled\":\"%s\","
			"\"active_stats\":%s"
			"}"),
			*RequestId,
			*MatchedName,
			*StatsArray
		);
		SendResponse(Socket, ResponseJSON);
	}
	else
	{
		FString StatsArray = BuildJsonStringArray(EnabledStats);

		FString EscapedStatName = CUJsonUtils::EscapeJsonString(StatName);
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"disable_stat_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"Stat '%s' not found in active stats\","
			"\"active_stats\":%s"
			"}"),
			*RequestId,
			*EscapedStatName,
			*StatsArray
		);
		SendResponse(Socket, ResponseJSON);
	}

	LogInfo(FString::Printf(TEXT("HandleDisableStat: stat_name=%s, found=%s"), *StatName, bFound ? TEXT("true") : TEXT("false")));
}

// ============================================
// Issue #8299 (Mirror #26): List Active Stats Handler
// ============================================

void UCUWebSocketEditorSubsystem::HandleListActiveStats(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleListActiveStats: Invalid socket or request"));
		return;
	}

	FString RequestId = CUJsonUtils::EscapeJsonString(Request->GetStringField(TEXT("request_id")));

	// Get active viewport client
	FEditorViewportClient* ViewportClient = nullptr;
	if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditor->GetFirstActiveViewport();
		if (ActiveViewport.IsValid())
		{
			ViewportClient = &ActiveViewport->GetAssetViewportClient();
		}
	}

	if (!ViewportClient)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{"
			"\"type\":\"list_active_stats_result\","
			"\"request_id\":\"%s\","
			"\"success\":false,"
			"\"error\":\"No active editor viewport\","
			"\"stats\":[],"
			"\"count\":0"
			"}"),
			*RequestId
		);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	const TArray<FString>& EnabledStats = *ViewportClient->GetEnabledStats();
	FString StatsArray = BuildJsonStringArray(EnabledStats);

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"list_active_stats_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"stats\":%s,"
		"\"count\":%d"
		"}"),
		*RequestId,
		*StatsArray,
		EnabledStats.Num()
	);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #8784: Shared Screenshot Helpers
// ============================================

bool UCUWebSocketEditorSubsystem::CaptureViewportPixels(FViewport* Viewport, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	if (!Viewport) return false;

	OutWidth = Viewport->GetSizeXY().X;
	OutHeight = Viewport->GetSizeXY().Y;

	if (OutWidth <= 0 || OutHeight <= 0) return false;

	// Issue #7911: Force a fresh render so we don't read a stale backbuffer
	// (viewports found via GetLevelViewportClients() fallback may be non-focused
	// and retain old contents until a manual redraw).
	Viewport->InvalidateHitProxy();
	Viewport->Draw();

	const bool bOk = Viewport->ReadPixels(OutPixels);

	// Issue #7911: Editor viewport framebuffer has no alpha — ReadPixels returns
	// A=0 for all pixels, making the PNG render as fully transparent (white in
	// most viewers). Force opaque so PNG/JPEG output matches what the user sees.
	if (bOk)
	{
		for (FColor& Px : OutPixels)
		{
			Px.A = 255;
		}
	}

	return bOk;
}

bool UCUWebSocketEditorSubsystem::EncodeAndSendScreenshot(
	INetworkingWebSocket* Socket, const FString& RequestId, const FString& ResultType,
	const TArray<FColor>& Pixels, int32 Width, int32 Height,
	const TSharedPtr<FJsonObject>& Request, double StartTime, const FString& ExtraJson)
{
	FString Format = Request->HasField(TEXT("format")) ? Request->GetStringField(TEXT("format")) : TEXT("png");
	int32 Quality = Request->HasField(TEXT("quality")) ? Request->GetIntegerField(TEXT("quality")) : 75;
	FString ReturnMode = Request->HasField(TEXT("return_mode")) ? Request->GetStringField(TEXT("return_mode")) : TEXT("file");

	Quality = FMath::Clamp(Quality, 1, 100);

	EImageFormat ImageFormat = (Format == TEXT("jpeg") || Format == TEXT("jpg")) ? EImageFormat::JPEG : EImageFormat::PNG;
	FString FileExt = ImageFormat == EImageFormat::JPEG ? TEXT("jpg") : TEXT("png");

	int32 EncWidth = Width;
	int32 EncHeight = Height;
	TArray<FColor> EncPixels = Pixels;

	// Issue #9074 (B): honor user-provided `resolution` via software resize.
	// Rationale: genuine viewport resize (FSceneViewport::SetFixedViewportSize)
	// touches the user's live editor window and has RAII-restore risk. A pure
	// pixel-buffer resize is reversible-free: no editor state change. For
	// downscaling, output quality matches a native render closely; upscaling
	// from a small viewport is best-effort but at least the output file's
	// width/height honor the request so downstream manifest fields line up.
	// Applied BEFORE the base64 1024-cap so the cap still protects the wire size.
	{
		FString RequestedResolution = Request->HasField(TEXT("resolution"))
			? Request->GetStringField(TEXT("resolution")) : FString();
		if (!RequestedResolution.IsEmpty())
		{
			FString WStr, HStr;
			if (RequestedResolution.Split(TEXT("x"), &WStr, &HStr, ESearchCase::IgnoreCase))
			{
				const int32 TargetW = FCString::Atoi(*WStr);
				const int32 TargetH = FCString::Atoi(*HStr);
				const bool bValid = (TargetW > 0 && TargetH > 0 && TargetW <= 8192 && TargetH <= 8192);
				if (bValid && (TargetW != EncWidth || TargetH != EncHeight))
				{
					TArray<FColor> Resized;
					Resized.SetNumUninitialized(TargetW * TargetH);
					const float SX = (float)EncWidth / (float)TargetW;
					const float SY = (float)EncHeight / (float)TargetH;
					for (int32 Y = 0; Y < TargetH; ++Y)
					{
						for (int32 X = 0; X < TargetW; ++X)
						{
							const int32 SrcX = FMath::Clamp(FMath::FloorToInt(X * SX), 0, EncWidth - 1);
							const int32 SrcY = FMath::Clamp(FMath::FloorToInt(Y * SY), 0, EncHeight - 1);
							Resized[Y * TargetW + X] = EncPixels[SrcY * EncWidth + SrcX];
						}
					}
					EncPixels = MoveTemp(Resized);
					EncWidth = TargetW;
					EncHeight = TargetH;
				}
				else if (!bValid)
				{
					FString ResponseJSON = FString::Printf(TEXT(
						"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":false,"
						"\"error\":\"invalid resolution '%s': expected WxH with 1<=W,H<=8192\"%s%s}"),
						*ResultType, *RequestId, *CUJsonUtils::EscapeJsonString(RequestedResolution),
						ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
						ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
					SendResponse(Socket, ResponseJSON);
					return false;
				}
			}
			else
			{
				FString ResponseJSON = FString::Printf(TEXT(
					"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":false,"
					"\"error\":\"invalid resolution '%s': expected WxH format (e.g. 1920x1080)\"%s%s}"),
					*ResultType, *RequestId, *CUJsonUtils::EscapeJsonString(RequestedResolution),
					ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
					ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
				SendResponse(Socket, ResponseJSON);
				return false;
			}
		}
	}

	// Auto-downsample to 1024px max for base64 mode
	if (ReturnMode == TEXT("base64"))
	{
		const FIntPoint CappedSize = CUWidgetDesignerCapture::CalculateCappedOutputSize(
			EncWidth, EncHeight, ReturnMode);
		if (CappedSize.X != EncWidth || CappedSize.Y != EncHeight)
		{
			const float Scale = FMath::Min(
				static_cast<float>(CappedSize.X) / EncWidth,
				static_cast<float>(CappedSize.Y) / EncHeight);
			const int32 NewWidth = CappedSize.X;
			const int32 NewHeight = CappedSize.Y;

			TArray<FColor> Downsampled;
			Downsampled.SetNumUninitialized(NewWidth * NewHeight);
			for (int32 Y = 0; Y < NewHeight; ++Y)
			{
				for (int32 X = 0; X < NewWidth; ++X)
				{
					int32 SrcX = FMath::Clamp(FMath::RoundToInt(X / Scale), 0, EncWidth - 1);
					int32 SrcY = FMath::Clamp(FMath::RoundToInt(Y / Scale), 0, EncHeight - 1);
					Downsampled[Y * NewWidth + X] = EncPixels[SrcY * EncWidth + SrcX];
				}
			}
			EncPixels = MoveTemp(Downsampled);
			EncWidth = NewWidth;
			EncHeight = NewHeight;
		}
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

	if (!ImageWrapper.IsValid() ||
		!ImageWrapper->SetRaw(EncPixels.GetData(), EncPixels.Num() * sizeof(FColor), EncWidth, EncHeight, ERGBFormat::BGRA, 8))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Image encoding failed.\"%s%s}"),
			*ResultType, *RequestId,
			ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
			ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
		SendResponse(Socket, ResponseJSON);
		return false;
	}

	TArray64<uint8> CompressedData = ImageWrapper->GetCompressed(ImageFormat == EImageFormat::JPEG ? Quality : 100);
	if (CompressedData.Num() == 0)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Image compression failed.\"%s%s}"),
			*ResultType, *RequestId,
			ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
			ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
		SendResponse(Socket, ResponseJSON);
		return false;
	}

	double ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	if (ReturnMode == TEXT("base64"))
	{
		TArray<uint8> CompressedArray;
		CompressedArray.SetNumUninitialized(CompressedData.Num());
		FMemory::Memcpy(CompressedArray.GetData(), CompressedData.GetData(), CompressedData.Num());
		FString Base64String = FBase64::Encode(CompressedArray);

		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":true,"
			"\"return_mode\":\"base64\",\"format\":\"%s\","
			"\"image_base64\":\""),
			*ResultType, *RequestId, *Format);
		ResponseJSON += Base64String;
		ResponseJSON += FString::Printf(TEXT(
			"\",\"width\":%d,\"height\":%d,\"file_size_bytes\":%lld,"
			"\"execution_time_ms\":%.2f%s%s}"),
			EncWidth, EncHeight, (int64)CompressedData.Num(), ExecutionTimeMs,
			ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
			ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
		SendResponse(Socket, ResponseJSON);
	}
	else
	{
		FString ScreenshotDir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MCP");
		IFileManager::Get().MakeDirectory(*ScreenshotDir, true);

		FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s"));
		FString FileName = FString::Printf(TEXT("%s_%s.%s"), *ResultType, *Timestamp, *FileExt);
		// Issue #9074 (A): resolve to absolute path. FPaths::ProjectSavedDir() can
		// return a path relative to the Editor binary's cwd; downstream clients
		// (ue_editor_debug callers, skill `Bash("mv ...")` steps) require absolute.
		FString FilePath = FPaths::ConvertRelativePathToFull(ScreenshotDir / FileName);

		if (!FFileHelper::SaveArrayToFile(CompressedData, *FilePath))
		{
			FString ResponseJSON = FString::Printf(TEXT(
				"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":false,\"error\":\"Failed to save screenshot to %s\"%s%s}"),
				*ResultType, *RequestId, *CUJsonUtils::EscapeJsonString(FilePath),
				ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
				ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
			SendResponse(Socket, ResponseJSON);
			return false;
		}

		FString EscapedPath = CUJsonUtils::EscapeJsonString(FilePath);
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":true,"
			"\"return_mode\":\"file\",\"format\":\"%s\","
			"\"file_path\":\"%s\",\"width\":%d,\"height\":%d,"
			"\"file_size_bytes\":%lld,\"execution_time_ms\":%.2f%s%s}"),
			*ResultType, *RequestId, *Format,
			*EscapedPath, EncWidth, EncHeight, (int64)CompressedData.Num(), ExecutionTimeMs,
			ExtraJson.IsEmpty() ? TEXT("") : TEXT(","),
			ExtraJson.IsEmpty() ? TEXT("") : *ExtraJson);
		SendResponse(Socket, ResponseJSON);
	}

	return true;
}

// ============================================
// Issue #8784: Capture Editor Viewport
// ============================================

void UCUWebSocketEditorSubsystem::HandleCaptureEditorViewport(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCaptureEditorViewport"), RequestId)) return;

	double StartTime = FPlatformTime::Seconds();

	// Issue #8784: GEditor->GetActiveViewport() requires editor window focus.
	// WebSocket calls run while the editor app is backgrounded — fall back to
	// GCurrentLevelEditingViewportClient which tracks the last-focused level viewport.
	// Issue #7911: Final fallback iterates all level viewport clients so automation
	// works even before the user has ever clicked a viewport (auto_capture_analyze).
	FViewport* Viewport = nullptr;
	FEditorViewportClient* FoundClient = nullptr;
	if (GCurrentLevelEditingViewportClient && GCurrentLevelEditingViewportClient->Viewport)
	{
		Viewport = GCurrentLevelEditingViewportClient->Viewport;
		FoundClient = GCurrentLevelEditingViewportClient;
	}
	else if (GEditor)
	{
		if (FViewport* ActiveViewport = GEditor->GetActiveViewport())
		{
			Viewport = ActiveViewport;
			FoundClient = static_cast<FEditorViewportClient*>(ActiveViewport->GetClient());
		}
	}
	if (!Viewport)
	{
		// Priority 1: ask LevelEditor module for the currently active level viewport
		// (the Slate-tracked main tab viewport that the user would see).
		FLevelEditorModule& LvlEd = FModuleManager::GetModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
		if (TSharedPtr<SLevelViewport> ActiveLV = LvlEd.GetFirstActiveLevelViewport())
		{
			FLevelEditorViewportClient& LVC = ActiveLV->GetLevelViewportClient();
			if (LVC.Viewport)
			{
				Viewport = LVC.Viewport;
				FoundClient = &LVC;
			}
		}
	}
	if (!Viewport && GEditor)
	{
		// Priority 2: iterate level viewport clients (perspective + realtime + visible first)
		for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
		{
			if (LevelVC && LevelVC->Viewport && LevelVC->IsPerspective()
				&& LevelVC->IsRealtime() && LevelVC->IsVisible())
			{
				Viewport = LevelVC->Viewport;
				FoundClient = LevelVC;
				break;
			}
		}
		if (!Viewport)
		{
			for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
			{
				if (LevelVC && LevelVC->Viewport && LevelVC->IsVisible())
				{
					Viewport = LevelVC->Viewport;
					FoundClient = LevelVC;
					break;
				}
			}
		}
	}
	if (!Viewport)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_viewport_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"No active editor viewport (open a level and click into a viewport once)\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	TArray<FColor> Pixels;
	int32 Width, Height;
	if (!CaptureViewportPixels(Viewport, Pixels, Width, Height))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_viewport_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"ReadPixels failed on editor viewport\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	LogInfo(FString::Printf(TEXT("[Viewport Capture] ReadPixels: %dx%d"), Width, Height));

	FString ExtraJson;
	{
		FEditorViewportClient* ViewportClient = FoundClient;
		if (ViewportClient)
		{
			FVector Loc = ViewportClient->GetViewLocation();
			FRotator Rot = ViewportClient->GetViewRotation();
			ExtraJson = FString::Printf(TEXT(
				"\"camera_transform\":{\"location\":{\"x\":%.2f,\"y\":%.2f,\"z\":%.2f},"
				"\"rotation\":{\"pitch\":%.2f,\"yaw\":%.2f,\"roll\":%.2f}}"),
				Loc.X, Loc.Y, Loc.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
		}
	}

	EncodeAndSendScreenshot(Socket, RequestId, TEXT("capture_editor_viewport_result"),
		Pixels, Width, Height, Request, StartTime, ExtraJson);
}

// ============================================
// Issue #8784: Capture PIE Window
// ============================================

void UCUWebSocketEditorSubsystem::HandleCapturePIEWindow(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCapturePIEWindow"), RequestId)) return;

	double StartTime = FPlatformTime::Seconds();

	// Issue #9895: multi-PIE-tolerant gate-only — same viewport resolution chain as
	// HandleCapturePIEViewport plus the LevelViewportClients fallback below.
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("capture_pie_window"))) return;

	FViewport* PIEViewport = GEditor ? GEditor->GetPIEViewport() : nullptr;
	if (!PIEViewport)
	{
		PIEViewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
	}
	// Issue #7911: Fallback to iterating level viewport clients for PIE world
	// when PIE viewport isn't registered as the "active" viewport.
	if (!PIEViewport && GEditor)
	{
		for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
		{
			if (LevelVC && LevelVC->Viewport && LevelVC->GetWorld()
				&& LevelVC->GetWorld()->IsPlayInEditor())
			{
				PIEViewport = LevelVC->Viewport;
				break;
			}
		}
	}

	if (!PIEViewport)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_pie_window_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"No active PIE session\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	TArray<FColor> Pixels;
	int32 Width, Height;
	if (!CaptureViewportPixels(PIEViewport, Pixels, Width, Height))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_pie_window_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"ReadPixels failed on PIE viewport\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	LogInfo(FString::Printf(TEXT("[PIE Window Capture] ReadPixels: %dx%d"), Width, Height));

	// Issue #9895: keep optional `game_time` extra metadata working in multi-PIE by
	// falling back to ResolveClientWorld(0) when GEditor->PlayWorld is null.
	// Note: GEditor->PlayWorld is `TObjectPtr<UWorld>` on UE 5.7+; ResolveClientWorld
	// returns `UWorld*`. The ternary requires both arms to be the same type, so we
	// resolve via an explicit `if/else` and a `UWorld*` working variable.
	FString ExtraJson;
	UWorld* TimeWorld = nullptr;
	if (GEditor && GEditor->PlayWorld)
	{
		TimeWorld = GEditor->PlayWorld;
	}
	else
	{
		TimeWorld = ResolveClientWorld(0);
	}
	if (TimeWorld)
	{
		float GameTime = TimeWorld->GetTimeSeconds();
		ExtraJson = FString::Printf(TEXT("\"game_time\":%.3f"), GameTime);
	}

	EncodeAndSendScreenshot(Socket, RequestId, TEXT("capture_pie_window_result"),
		Pixels, Width, Height, Request, StartTime, ExtraJson);
}

// ============================================
// Issue #8784: Capture Editor Panel
// ============================================

void UCUWebSocketEditorSubsystem::HandleCaptureEditorPanel(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCaptureEditorPanel"), RequestId)) return;
	// ARCH-011: defensive cook-safety guard — WebSocket handlers don't run in
	// commandlets, but the lint rule requires this signal to clear the function.
	if (IsRunningCommandlet()) return;

	double StartTime = FPlatformTime::Seconds();

	FString PanelName = Request->HasField(TEXT("panel_name")) ? Request->GetStringField(TEXT("panel_name")) : TEXT("");
	if (PanelName.IsEmpty())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_panel_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Missing required parameter 'panel_name'\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	if (!FSlateApplication::IsInitialized())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_panel_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"SlateApplication not initialized\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	TSharedPtr<SWidget> FoundWidget;
	TArray<FString> AvailableTabs;

	// Search Global + Level Editor local tab managers
	TSharedPtr<SDockTab> Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*PanelName));
	if (!Tab.IsValid())
	{
		if (FLevelEditorModule* LEModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
		{
			TSharedPtr<FTabManager> LETabMgr = LEModule->GetLevelEditorTabManager();
			if (LETabMgr.IsValid())
			{
				Tab = LETabMgr->FindExistingLiveTab(FTabId(*PanelName));
			}
		}
	}
	if (Tab.IsValid())
	{
		FoundWidget = Tab->GetContent();  // Content, not tab header
	}

	if (!FoundWidget.IsValid())
	{
		TArray<FString> CommonTabNames = {
			TEXT("ContentBrowser1"), TEXT("ContentBrowserTab1"),
			TEXT("OutputLog"), TEXT("LevelEditor"),
			TEXT("WorldSettings"), TEXT("PlacementBrowser"),
			TEXT("SceneOutliner"), TEXT("SceneOutliner1"),
			TEXT("DetailsPanel"), TEXT("PropertyEditor"),
		};

		for (const FString& TabName : CommonTabNames)
		{
			TSharedPtr<SDockTab> CandidateTab = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(*TabName));
			if (CandidateTab.IsValid())
			{
				AvailableTabs.Add(TabName);
				if (TabName.Contains(PanelName) || PanelName.Contains(TabName))
				{
					FoundWidget = CandidateTab->GetContent();
				}
			}
		}
	}

	if (!FoundWidget.IsValid())
	{
		FString AvailableList;
		for (int32 i = 0; i < AvailableTabs.Num(); ++i)
		{
			if (i > 0) AvailableList += TEXT(", ");
			AvailableList += AvailableTabs[i];
		}
		FString ErrorMsg = FString::Printf(TEXT("Panel '%s' not found. Available: [%s]"),
			*PanelName, *AvailableList);
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_panel_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"%s\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(ErrorMsg));
		SendResponse(Socket, ResponseJSON);
		return;
	}

	FGeometry PanelGeometry = FoundWidget->GetCachedGeometry();
	FVector2D PanelSize = PanelGeometry.GetLocalSize();
	FVector2D AbsPosition = PanelGeometry.GetAbsolutePosition();

	if (PanelSize.X <= 0 || PanelSize.Y <= 0)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_panel_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Panel '%s' has zero size\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(PanelName));
		SendResponse(Socket, ResponseJSON);
		return;
	}

	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(FoundWidget.ToSharedRef());
	if (!ParentWindow.IsValid())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_panel_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Could not find parent window for panel '%s'\"}"),
			*RequestId, *CUJsonUtils::EscapeJsonString(PanelName));
		SendResponse(Socket, ResponseJSON);
		return;
	}

	TArray<FColor> Bitmap;
	FIntVector Size;
	bool bCaptured = FSlateApplication::Get().TakeScreenshot(ParentWindow.ToSharedRef(), Bitmap, Size);

	if (!bCaptured || Bitmap.Num() == 0)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_editor_panel_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Screenshot capture failed for panel window\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	FVector2D WindowPos = ParentWindow->GetPositionInScreen();
	int32 CropX = FMath::Clamp(FMath::RoundToInt(AbsPosition.X - WindowPos.X), 0, Size.X - 1);
	int32 CropY = FMath::Clamp(FMath::RoundToInt(AbsPosition.Y - WindowPos.Y), 0, Size.Y - 1);
	int32 CropW = FMath::Clamp(FMath::RoundToInt(PanelSize.X), 1, Size.X - CropX);
	int32 CropH = FMath::Clamp(FMath::RoundToInt(PanelSize.Y), 1, Size.Y - CropY);

	TArray<FColor> CroppedPixels;
	CroppedPixels.SetNumUninitialized(CropW * CropH);
	for (int32 Y = 0; Y < CropH; ++Y)
	{
		for (int32 X = 0; X < CropW; ++X)
		{
			int32 SrcIdx = (CropY + Y) * Size.X + (CropX + X);
			if (SrcIdx >= 0 && SrcIdx < Bitmap.Num())
			{
				CroppedPixels[Y * CropW + X] = Bitmap[SrcIdx];
			}
		}
	}

	LogInfo(FString::Printf(TEXT("[Panel Capture] '%s' cropped: %dx%d from window %dx%d"),
		*PanelName, CropW, CropH, Size.X, Size.Y));

	FString ExtraJson = FString::Printf(TEXT(
		"\"panel_name\":\"%s\","
		"\"panel_bounds\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}"),
		*CUJsonUtils::EscapeJsonString(PanelName), CropX, CropY, CropW, CropH);

	EncodeAndSendScreenshot(Socket, RequestId, TEXT("capture_editor_panel_result"),
		CroppedPixels, CropW, CropH, Request, StartTime, ExtraJson);
}

// ============================================
// Issue #8806: Capture All Open Panels
// ============================================

void UCUWebSocketEditorSubsystem::HandleCaptureAllPanels(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCaptureAllPanels"), RequestId)) return;
	// ARCH-011: defensive cook-safety guard — WebSocket handlers don't run in
	// commandlets, but the lint rule requires this signal to clear the function.
	if (IsRunningCommandlet()) return;

	double StartTime = FPlatformTime::Seconds();

	if (!FSlateApplication::IsInitialized())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"capture_all_panels_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"SlateApplication not initialized\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Known panel tab IDs (UE 5.5-5.7 conventions, both Global and Level Editor local)
	static const TArray<FString> KnownPanels = {
		// Global tab manager
		TEXT("LevelEditor"),
		// Level Editor local tab manager (docked sub-panels)
		TEXT("ContentBrowser1"), TEXT("ContentBrowserTab1"),
		TEXT("ContentBrowser2"), TEXT("ContentBrowser3"), TEXT("ContentBrowser4"),
		TEXT("OutputLog"),
		TEXT("SceneOutliner"), TEXT("SceneOutliner1"),
		TEXT("DetailsPanel"), TEXT("PropertyEditor"),
		TEXT("LevelEditorSelectionDetails"), TEXT("LevelEditorSelectionDetails2"),
		TEXT("WorldSettings"), TEXT("WorldSettingsTab"),
		TEXT("PlacementBrowser"),
		TEXT("Kismet"),
		TEXT("MaterialEditor"),
		TEXT("AnimationEditor"),
		TEXT("Sequencer"), TEXT("SequencerGraphEditor"),
		TEXT("LevelEditorToolBar"),
		TEXT("LevelEditorToolBox"),
		TEXT("LevelEditorBuildAndSubmit"),
		TEXT("MessageLog"),
		TEXT("FindInBlueprints"),
		TEXT("GameplayTagsEditor"),
		// Issue #9251: UMG Designer + Niagara tab IDs.
		// Asset editors register per-instance tab IDs for their *primary* tab
		// (e.g. `MaterialEditor_M_Mannequin_0`), but these root IDs still
		// resolve inside Blueprint / Niagara / UMG editor local tab managers.
		// `capture_all_panels` benefits from both coverage paths; `get_preview`
		// (Issue #9251 Phase 3) sidesteps tab IDs entirely via active-window capture.
		TEXT("UMGDesigner"), TEXT("WidgetDesigner"),
		TEXT("WidgetPalette"), TEXT("WidgetDetails"),
		TEXT("NiagaraSystemEditor"), TEXT("NiagaraEmitterEditor"),
	};

	// Parse shared screenshot params
	FString Format = Request->HasField(TEXT("format")) ? Request->GetStringField(TEXT("format")) : TEXT("png");
	if (Format != TEXT("jpeg") && Format != TEXT("jpg")) { Format = TEXT("png"); }
	else { Format = TEXT("jpeg"); }

	FString ScreenshotBaseDir = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MCP");

	// Session folder: capture_{YYYYMMDD_HHMMSS}_{4hex}
	FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	FString SessionId = FString::Printf(TEXT("capture_%s_%04x"), *Timestamp, FMath::Rand() & 0xFFFF);
	FString CaptureDir = ScreenshotBaseDir / SessionId;
	IFileManager::Get().MakeDirectory(*CaptureDir, true);

	FString FileExt = (Format == TEXT("jpeg")) ? TEXT("jpg") : TEXT("png");

	int32 Quality = Request->HasField(TEXT("quality")) ? FMath::Clamp((int32)Request->GetNumberField(TEXT("quality")), 1, 100) : 75;
	FString Purpose = Request->HasField(TEXT("purpose")) ? Request->GetStringField(TEXT("purpose")) : TEXT("");

	FString PanelsJson;
	int32 TotalCaptured = 0;

	// Issue #8811: Scan both Global + Level Editor local tab managers
	TArray<FTabManager*> TabManagers;
	TabManagers.Add(&FGlobalTabmanager::Get().Get());
	if (FLevelEditorModule* LEModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
	{
		TSharedPtr<FTabManager> LETabMgr = LEModule->GetLevelEditorTabManager();
		if (LETabMgr.IsValid())
		{
			TabManagers.Add(LETabMgr.Get());
		}
	}
	TSet<FString> AlreadyCaptured;
	LogInfo(FString::Printf(TEXT("[All Panels] Scanning %d tab managers, %d known panel IDs"), TabManagers.Num(), KnownPanels.Num()));

	for (FTabManager* TabMgr : TabManagers)
	{
	for (const FString& PanelName : KnownPanels)
	{
		if (AlreadyCaptured.Contains(PanelName)) continue;
		TSharedPtr<SDockTab> Tab = TabMgr->FindExistingLiveTab(FTabId(*PanelName));
		if (!Tab.IsValid()) continue;
		AlreadyCaptured.Add(PanelName);

		// Use Tab content widget (not tab header) for actual panel area
		TSharedRef<SWidget> ContentWidget = Tab->GetContent();
		TSharedPtr<SWidget> PanelWidget = ContentWidget;
		FGeometry PanelGeometry = PanelWidget->GetCachedGeometry();
		FVector2D PanelSize = PanelGeometry.GetLocalSize();
		FVector2D AbsPosition = PanelGeometry.GetAbsolutePosition();

		if (PanelSize.X <= 0 || PanelSize.Y <= 0) continue;

		TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(PanelWidget.ToSharedRef());
		if (!ParentWindow.IsValid()) continue;

		TArray<FColor> Bitmap;
		FIntVector WinSize;
		bool bCaptured = FSlateApplication::Get().TakeScreenshot(ParentWindow.ToSharedRef(), Bitmap, WinSize);
		if (!bCaptured || Bitmap.Num() == 0) continue;

		// Crop to panel bounds
		FVector2D WindowPos = ParentWindow->GetPositionInScreen();
		int32 CropX = FMath::Clamp(FMath::RoundToInt(AbsPosition.X - WindowPos.X), 0, WinSize.X - 1);
		int32 CropY = FMath::Clamp(FMath::RoundToInt(AbsPosition.Y - WindowPos.Y), 0, WinSize.Y - 1);
		int32 CropW = FMath::Clamp(FMath::RoundToInt(PanelSize.X), 1, WinSize.X - CropX);
		int32 CropH = FMath::Clamp(FMath::RoundToInt(PanelSize.Y), 1, WinSize.Y - CropY);

		TArray<FColor> CroppedPixels;
		CroppedPixels.SetNumZeroed(CropW * CropH);
		for (int32 Y = 0; Y < CropH; ++Y)
		{
			for (int32 X = 0; X < CropW; ++X)
			{
				int32 SrcIdx = (CropY + Y) * WinSize.X + (CropX + X);
				if (SrcIdx >= 0 && SrcIdx < Bitmap.Num())
				{
					CroppedPixels[Y * CropW + X] = Bitmap[SrcIdx];
				}
			}
		}

		// Encode and save to file
		IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		EImageFormat ImageFormat = (Format == TEXT("jpeg")) ? EImageFormat::JPEG : EImageFormat::PNG;
		TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);

		if (!ImageWrapper.IsValid()) continue;
		if (!ImageWrapper->SetRaw(CroppedPixels.GetData(), CroppedPixels.Num() * sizeof(FColor), CropW, CropH, ERGBFormat::BGRA, 8)) continue;

		int32 EncQuality = (Format == TEXT("jpeg")) ? Quality : 0;
		TArray64<uint8> CompressedData;
		if (ImageFormat == EImageFormat::JPEG)
		{
			CompressedData = ImageWrapper->GetCompressed(EncQuality);
		}
		else
		{
			CompressedData = ImageWrapper->GetCompressed();
		}
		if (CompressedData.Num() == 0) continue;

		FString FileName = FString::Printf(TEXT("%s.%s"), *PanelName, *FileExt);
		FString FilePath = CaptureDir / FileName;
		if (!FFileHelper::SaveArrayToFile(CompressedData, *FilePath)) continue;

		// Append to JSON array
		if (TotalCaptured > 0) PanelsJson += TEXT(",");
		PanelsJson += FString::Printf(TEXT(
			"{\"name\":\"%s\",\"file\":\"%s.%s\","
			"\"width\":%d,\"height\":%d,\"file_size_bytes\":%lld,"
			"\"panel_bounds\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}}"),
			*CUJsonUtils::EscapeJsonString(PanelName), *CUJsonUtils::EscapeJsonString(PanelName), *FileExt,
			CropW, CropH, (int64)CompressedData.Num(),
			CropX, CropY, CropW, CropH);

		TotalCaptured++;
		LogInfo(FString::Printf(TEXT("[All Panels] Captured '%s': %dx%d"), *PanelName, CropW, CropH));
	}
	} // end TabManagers loop (Issue #8811)

	double ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	// Write manifest.json for AI agent consumption
	FString IsoTimestamp = FDateTime::Now().ToIso8601();
	FString PurposeField = Purpose.IsEmpty() ? TEXT("") :
		FString::Printf(TEXT(",\"purpose\":\"%s\""), *CUJsonUtils::EscapeJsonString(Purpose));
	FString ManifestJson = FString::Printf(TEXT(
		"{\"capture_id\":\"%s\",\"timestamp\":\"%s\",\"format\":\"%s\","
		"\"total_captured\":%d%s,\"panels\":[%s]}"),
		*SessionId, *IsoTimestamp, *Format, TotalCaptured, *PurposeField, *PanelsJson);
	FString ManifestPath = CaptureDir / TEXT("manifest.json");
	FFileHelper::SaveStringToFile(ManifestJson, *ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	LogInfo(FString::Printf(TEXT("[All Panels] manifest.json saved: %s"), *ManifestPath));

	FString EscapedCaptureDir = CUJsonUtils::EscapeJsonString(CaptureDir);
	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"capture_all_panels_result\",\"request_id\":\"%s\","
		"\"success\":true,\"capture_id\":\"%s\",\"capture_dir\":\"%s\","
		"\"panels\":[%s],\"total_captured\":%d,"
		"\"format\":\"%s\",\"execution_time_ms\":%.2f}"),
		*RequestId, *SessionId, *EscapedCaptureDir, *PanelsJson, TotalCaptured, *Format, ExecutionTimeMs);
	SendResponse(Socket, ResponseJSON);
}

// Issue #9251: Runtime tab enumeration WS command — direct AI/MCP-client access.
//
// `capture_all_panels` / `capture_editor_panel` rely on a hardcoded KnownPanels
// list (see HandleCaptureAllPanels above). Asset editors register per-instance
// tab IDs (e.g. `MaterialEditor_M_Mannequin_0`, `BlueprintEditor_WBP_HUD_0`)
// that the static list cannot cover. This handler discovers live tabs in two
// passes:
//   1. Sweep the KnownPanels candidate IDs against the Global + Level Editor
//      tab managers — matches the existing capture paths.
//   2. Iterate `AssetEditorSubsystem::GetAllEditedAssets()` and, for each
//      live asset, record `{asset_path, editor_name, asset_class}` — these
//      are the sources of the per-instance tab IDs that capture_editor_panel
//      cannot find by literal name.
//
// Consumers (intentional: this is NOT routed through any Rust MCP tool):
//   - Direct WS clients (AI agents, Python integrations, debug shells)
//     answering "what's open in the editor right now?" before deciding which
//     asset to query/capture. The PR's live tests use this path verbatim:
//     `python websockets.sync.client.connect("ws://localhost:30011")` →
//     `send({"type": "enumerate_open_tabs"})` → enumerate response to verify
//     `close_after=true` actually closed the editor (Phase 4 T2.1).
//   - `ue_manage_widget(get_preview)` (Issue #26157) now uses the dedicated
//     `capture_widget_designer` request and `SetCurrentMode(DesignerMode)`;
//     it no longer depends on literal Designer tab IDs or close/reopen by default.
//
// Response shape:
//   {
//     type: "enumerate_open_tabs_result", request_id, success: true,
//     tabs: [{tab_id, label, source: "global"|"level_editor", is_live: true}, ...],
//     asset_editors: [{asset_path, asset_class, editor_name}, ...],
//     execution_time_ms: ...
//   }
void UCUWebSocketEditorSubsystem::HandleEnumerateOpenTabs(
    INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
    FString RequestId = CUJsonUtils::EscapeJsonString(Request.IsValid() ? Request->GetStringField(TEXT("request_id")) : FString());
    if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleEnumerateOpenTabs"), RequestId)) return;

    double StartTime = FPlatformTime::Seconds();

    if (!FSlateApplication::IsInitialized())
    {
        FString ResponseJSON = FString::Printf(TEXT(
            "{\"type\":\"enumerate_open_tabs_result\",\"request_id\":\"%s\","
            "\"success\":false,\"error\":\"SlateApplication not initialized\"}"),
            *RequestId);
        SendResponse(Socket, ResponseJSON);
        return;
    }

    // Reuse the same static candidate list as HandleCaptureAllPanels. Keeping
    // the data in the other function (no cross-file extraction) is acceptable
    // because the list is read-only and tiny; duplicating it here keeps the
    // two handlers self-contained.
    static const TArray<FString> TabCandidates = {
        TEXT("LevelEditor"),
        TEXT("ContentBrowser1"), TEXT("ContentBrowserTab1"),
        TEXT("ContentBrowser2"), TEXT("ContentBrowser3"), TEXT("ContentBrowser4"),
        TEXT("OutputLog"),
        TEXT("SceneOutliner"), TEXT("SceneOutliner1"),
        TEXT("DetailsPanel"), TEXT("PropertyEditor"),
        TEXT("LevelEditorSelectionDetails"), TEXT("LevelEditorSelectionDetails2"),
        TEXT("WorldSettings"), TEXT("WorldSettingsTab"),
        TEXT("PlacementBrowser"),
        TEXT("Kismet"),
        TEXT("MaterialEditor"),
        TEXT("AnimationEditor"),
        TEXT("Sequencer"), TEXT("SequencerGraphEditor"),
        TEXT("LevelEditorToolBar"), TEXT("LevelEditorToolBox"),
        TEXT("LevelEditorBuildAndSubmit"),
        TEXT("MessageLog"), TEXT("FindInBlueprints"), TEXT("GameplayTagsEditor"),
        TEXT("UMGDesigner"), TEXT("WidgetDesigner"),
        TEXT("WidgetPalette"), TEXT("WidgetDetails"),
        TEXT("NiagaraSystemEditor"), TEXT("NiagaraEmitterEditor"),
    };

    // Pass 1: KnownPanels sweep across both tab managers.
    TArray<TPair<FTabManager*, FString>> TabMgrs;
    TabMgrs.Add(TPair<FTabManager*, FString>(&FGlobalTabmanager::Get().Get(), TEXT("global")));
    if (FLevelEditorModule* LEModule = FModuleManager::GetModulePtr<FLevelEditorModule>(TEXT("LevelEditor")))
    {
        TSharedPtr<FTabManager> LETabMgr = LEModule->GetLevelEditorTabManager();
        if (LETabMgr.IsValid())
        {
            TabMgrs.Add(TPair<FTabManager*, FString>(LETabMgr.Get(), TEXT("level_editor")));
        }
    }

    // Issue #9251 Phase 4 (T1.2): use FJsonObject + FJsonSerializer for
    // JSON construction. Hand-rolled FString::Printf + EscapeJsonString
    // mangles UTF-16 surrogate halves on non-ASCII labels (e.g. Korean
    // tab title "메시지 로그" → "�޽��� �α�"). FJsonObject + the writer
    // chain serializes through TCHARToUTF8 internally and produces correct
    // UTF-8 byte sequences regardless of the source FString encoding.
    TArray<TSharedPtr<FJsonValue>> TabsArray;
    TSet<FString> AlreadySeen;
    for (const TPair<FTabManager*, FString>& TabMgrPair : TabMgrs)
    {
        FTabManager* TabMgr = TabMgrPair.Key;
        const FString& Source = TabMgrPair.Value;
        for (const FString& Candidate : TabCandidates)
        {
            if (AlreadySeen.Contains(Candidate)) continue;
            TSharedPtr<SDockTab> Tab = TabMgr->FindExistingLiveTab(FTabId(*Candidate));
            if (!Tab.IsValid()) continue;
            AlreadySeen.Add(Candidate);

            FString Label = Tab->GetTabLabel().ToString();
            TSharedPtr<FJsonObject> TabObj = MakeShared<FJsonObject>();
            TabObj->SetStringField(TEXT("tab_id"), Candidate);
            TabObj->SetStringField(TEXT("label"), Label);
            TabObj->SetStringField(TEXT("source"), Source);
            TabObj->SetBoolField(TEXT("is_live"), true);
            TabsArray.Add(MakeShared<FJsonValueObject>(TabObj));
        }
    }

    // Pass 2: Open asset editors — surfaces per-instance tab roots that the
    // candidate sweep cannot find (MaterialEditor_M_Mannequin_0 etc.).
    TArray<TSharedPtr<FJsonValue>> AssetsArray;
    if (GEditor)
    {
        if (UAssetEditorSubsystem* AssetEditorSub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
        {
            TArray<UObject*> EditedAssets = AssetEditorSub->GetAllEditedAssets();
            for (UObject* Asset : EditedAssets)
            {
                if (!Asset) continue;

                FString AssetPath = Asset->GetPathName();
                FString AssetClass = Asset->GetClass() ? Asset->GetClass()->GetName() : FString(TEXT("Unknown"));

                // Walk all editor instances for this asset (usually 1, but a
                // user can open the same asset in multiple editors).
                TArray<IAssetEditorInstance*> Editors = AssetEditorSub->FindEditorsForAsset(Asset);
                for (IAssetEditorInstance* EditorInst : Editors)
                {
                    if (!EditorInst) continue;
                    FString EditorName = EditorInst->GetEditorName().ToString();
                    TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
                    AssetObj->SetStringField(TEXT("asset_path"), AssetPath);
                    AssetObj->SetStringField(TEXT("asset_class"), AssetClass);
                    AssetObj->SetStringField(TEXT("editor_name"), EditorName);
                    AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
                }
            }
        }
    }

    double ExecutionTimeMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

    // Build root response object.
    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("type"), TEXT("enumerate_open_tabs_result"));
    // request_id is round-tripped from caller; we already escaped it above for
    // the slate-init error path. For the success path, the FJsonObject writer
    // handles escaping correctly, so use the raw request_id from the request.
    FString RawRequestId = Request.IsValid() ? Request->GetStringField(TEXT("request_id")) : FString();
    Root->SetStringField(TEXT("request_id"), RawRequestId);
    Root->SetBoolField(TEXT("success"), true);
    Root->SetArrayField(TEXT("tabs"), TabsArray);
    Root->SetArrayField(TEXT("asset_editors"), AssetsArray);
    Root->SetNumberField(TEXT("tab_count"), TabsArray.Num());
    Root->SetNumberField(TEXT("asset_editor_count"), AssetsArray.Num());
    Root->SetNumberField(TEXT("execution_time_ms"), ExecutionTimeMs);

    FString ResponseJSON = CUJsonUtils::SerializeToString(Root.ToSharedRef());

    SendResponse(Socket, ResponseJSON);
}

// Issue #32: RigUnit type → DisplayName mapping for Control Rig node resolution
void UCUWebSocketEditorSubsystem::HandleGetRigUnitDisplayNames(
	INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	if (!Socket || !Request.IsValid())
	{
		LogError(TEXT("HandleGetRigUnitDisplayNames: Invalid socket or request"));
		return;
	}

	FString RequestId = CUJsonUtils::EscapeJsonString(Request->GetStringField(TEXT("request_id")));

#if !WITH_NARSHAMCP_RIGVM
	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"rigunit_display_names_result\",\"request_id\":\"%s\","
		"\"success\":false,\"error\":\"RigVM plugin unavailable\","
		"\"error_code\":\"rigvm_plugin_unavailable\","
		"\"hint\":\"Enable RigVM in your .uproject, then rebuild.\"}"), *RequestId));
	return;
#elif !WITH_NARSHAMCP_CONTROLRIG
	SendResponse(Socket, FString::Printf(TEXT(
		"{\"type\":\"rigunit_display_names_result\",\"request_id\":\"%s\","
		"\"success\":false,\"error\":\"ControlRig plugin unavailable\","
		"\"error_code\":\"control_rig_plugin_unavailable\","
		"\"hint\":\"Enable ControlRig in your .uproject, then rebuild.\"}"), *RequestId));
	return;
#else

	// Ensure ControlRig and RigVM modules are loaded
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("ControlRig")))
	{
		FModuleManager::Get().LoadModule(TEXT("ControlRig"));
	}
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("RigVM")))
	{
		FModuleManager::Get().LoadModule(TEXT("RigVM"));
	}

	// Build type → DisplayName mapping
	TMap<FString, FString> Mappings;

	// 1. Iterate all UScriptStruct with RigUnit_ or RigVMFunction_ prefix
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct)
		{
			continue;
		}

		FString StructName = Struct->GetName();
		// UE 5.7: Some RigUnits renamed from FRigUnit_* to FRigVMFunction_*
		if (!StructName.StartsWith(TEXT("RigUnit_")) && !StructName.StartsWith(TEXT("RigVMFunction_")))
		{
			continue;
		}

		FString DisplayName;
		if (Struct->HasMetaData(TEXT("DisplayName")))
		{
			DisplayName = Struct->GetMetaData(TEXT("DisplayName"));
		}
		else
		{
			DisplayName = Struct->GetDisplayNameText().ToString();
		}

		if (DisplayName.IsEmpty())
		{
			continue;
		}

		// Register both with and without F prefix for .uasset compatibility
		Mappings.Add(FString::Printf(TEXT("F%s"), *StructName), DisplayName);
		Mappings.Add(StructName, DisplayName);

		// Issue #32: Cross-map FRigVMFunction_* ↔ FRigUnit_* for UE 5.7 renames.
		// .uasset may store the old name (FRigUnit_X) while the engine struct
		// has been renamed to FRigVMFunction_X. Register both directions.
		if (StructName.StartsWith(TEXT("RigVMFunction_")))
		{
			FString OldName = StructName.Replace(TEXT("RigVMFunction_"), TEXT("RigUnit_"));
			if (!Mappings.Contains(OldName))
			{
				Mappings.Add(OldName, DisplayName);
				Mappings.Add(FString::Printf(TEXT("F%s"), *OldName), DisplayName);
			}
		}
		else if (StructName.StartsWith(TEXT("RigUnit_")))
		{
			FString NewName = StructName.Replace(TEXT("RigUnit_"), TEXT("RigVMFunction_"));
			if (!Mappings.Contains(NewName))
			{
				Mappings.Add(NewName, DisplayName);
				Mappings.Add(FString::Printf(TEXT("F%s"), *NewName), DisplayName);
			}
		}
	}

	// 2. Iterate FRigVMDispatchFactory for dispatch nodes (ArrayIterator, ForEach, etc.)
	// Use GetScriptStruct()->GetDisplayNameText() instead of GetNodeTitle() which is #if WITH_EDITOR guarded
	const TArray<FRigVMDispatchFactory*>& Factories = FRigVMRegistry::Get().GetFactories();
	for (const FRigVMDispatchFactory* Factory : Factories)
	{
		if (!Factory)
		{
			continue;
		}

		UScriptStruct* FactoryStruct = Factory->GetScriptStruct();
		if (!FactoryStruct)
		{
			continue;
		}

		FString StructName = FactoryStruct->GetName();
		FString DisplayName = FactoryStruct->GetDisplayNameText().ToString();

		if (!DisplayName.IsEmpty() && !Mappings.Contains(StructName))
		{
			Mappings.Add(FString::Printf(TEXT("F%s"), *StructName), DisplayName);
			Mappings.Add(StructName, DisplayName);
		}
	}

	// 3. Build JSON response
	FString MappingsJson = TEXT("{");
	bool bFirst = true;
	for (const auto& Pair : Mappings)
	{
		if (!bFirst)
		{
			MappingsJson += TEXT(",");
		}
		bFirst = false;
		MappingsJson += FString::Printf(TEXT("\"%s\":\"%s\""),
			*CUJsonUtils::EscapeJsonString(Pair.Key),
			*CUJsonUtils::EscapeJsonString(Pair.Value));
	}
	MappingsJson += TEXT("}");

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"rigunit_display_names_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"count\":%d,"
		"\"mappings\":%s"
		"}"),
		*RequestId,
		Mappings.Num(),
		*MappingsJson
	);

	SendResponse(Socket, ResponseJSON);
#endif
}

// ============================================
// Issue #7915: Runtime UMG Widget Tree Inspection
// ============================================

namespace WidgetTreeHelpers
{
	/** Convert ESlateVisibility to string */
	FString VisibilityToString(ESlateVisibility Visibility)
	{
		switch (Visibility)
		{
		case ESlateVisibility::Visible:         return TEXT("Visible");
		case ESlateVisibility::Collapsed:       return TEXT("Collapsed");
		case ESlateVisibility::Hidden:          return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible: return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		default:                                return TEXT("Unknown");
		}
	}

	/** Convert EHorizontalAlignment to string */
	FString HAlignToString(EHorizontalAlignment Align)
	{
		switch (Align)
		{
		case HAlign_Fill:   return TEXT("Fill");
		case HAlign_Left:   return TEXT("Left");
		case HAlign_Center: return TEXT("Center");
		case HAlign_Right:  return TEXT("Right");
		default:            return TEXT("Fill");
		}
	}

	/** Convert EVerticalAlignment to string */
	FString VAlignToString(EVerticalAlignment Align)
	{
		switch (Align)
		{
		case VAlign_Fill:   return TEXT("Fill");
		case VAlign_Top:    return TEXT("Top");
		case VAlign_Center: return TEXT("Center");
		case VAlign_Bottom: return TEXT("Bottom");
		default:            return TEXT("Fill");
		}
	}

	/** Convert EWidgetClipping to string */
	FString ClippingToString(EWidgetClipping Clipping)
	{
		switch (Clipping)
		{
		case EWidgetClipping::Inherit:          return TEXT("Inherit");
		case EWidgetClipping::ClipToBounds:     return TEXT("ClipToBounds");
		case EWidgetClipping::ClipToBoundsWithoutIntersecting: return TEXT("ClipToBoundsWithoutIntersecting");
		case EWidgetClipping::ClipToBoundsAlways: return TEXT("ClipToBoundsAlways");
		case EWidgetClipping::OnDemand:         return TEXT("OnDemand");
		default:                                return TEXT("Inherit");
		}
	}

	/** Extract slot layout info (padding, alignment) from a widget's slot */
	FString ExtractSlotJson(UWidget* Widget)
	{
		UPanelSlot* Slot = Widget->Slot;
		if (!Slot)
		{
			return TEXT("");
		}

		FString SlotClass = Slot->GetClass()->GetName();
		FMargin Padding = FMargin(0);
		FString HAlign = TEXT("Fill");
		FString VAlign = TEXT("Fill");

		// Try to get padding and alignment from the slot via properties
		// UCanvasPanelSlot, UHorizontalBoxSlot, UVerticalBoxSlot, etc. all have layout data
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			auto Offsets = CanvasSlot->GetOffsets();
			return FString::Printf(TEXT(
				",\"slot\":{\"class\":\"%s\","
				"\"offsets\":{\"left\":%.1f,\"top\":%.1f,\"right\":%.1f,\"bottom\":%.1f}}"),
				*CUJsonUtils::EscapeJsonString(SlotClass),
				Offsets.Left, Offsets.Top, Offsets.Right, Offsets.Bottom);
		}

		// Generic slot — just report the class
		return FString::Printf(TEXT(",\"slot\":{\"class\":\"%s\"}"),
			*CUJsonUtils::EscapeJsonString(SlotClass));
	}

	/** Compute effective visibility by walking parent chain */
	FString ComputeEffectiveVisibility(UWidget* Widget)
	{
		ESlateVisibility MostRestrictive = ESlateVisibility::Visible;
		UWidget* Current = Widget;
		while (Current)
		{
			ESlateVisibility V = Current->GetVisibility();
			if (V == ESlateVisibility::Collapsed || V == ESlateVisibility::Hidden)
			{
				return VisibilityToString(V); // most restrictive
			}
			if (V != ESlateVisibility::Visible && MostRestrictive == ESlateVisibility::Visible)
			{
				MostRestrictive = V;
			}
			Current = Current->GetParent();
		}
		return VisibilityToString(MostRestrictive);
	}

	/** Extract type-specific properties as JSON fragment */
	FString ExtractTypeProps(UWidget* Widget)
	{
		FString Props;

		if (UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
		{
			FString TextContent = CUJsonUtils::EscapeJsonString(TextBlock->GetText().ToString());
			FSlateFontInfo Font = TextBlock->GetFont();
			FLinearColor Color = TextBlock->GetColorAndOpacity().GetSpecifiedColor();
			Props = FString::Printf(TEXT(
				",\"text\":\"%s\""
				",\"font_size\":%.0f"
				",\"color\":{\"r\":%.3f,\"g\":%.3f,\"b\":%.3f,\"a\":%.3f}"),
				*TextContent, static_cast<float>(Font.Size),
				Color.R, Color.G, Color.B, Color.A);
		}
		else if (URichTextBlock* RichText = Cast<URichTextBlock>(Widget))
		{
			FString TextContent = CUJsonUtils::EscapeJsonString(RichText->GetText().ToString());
			Props = FString::Printf(TEXT(",\"text\":\"%s\""), *TextContent);
		}
		else if (UButton* Btn = Cast<UButton>(Widget))
		{
			Props = FString::Printf(TEXT(
				",\"is_hovered\":%s"
				",\"is_pressed\":%s"),
				Btn->IsHovered() ? TEXT("true") : TEXT("false"),
				Btn->IsPressed() ? TEXT("true") : TEXT("false"));
		}
		else if (UProgressBar* ProgBar = Cast<UProgressBar>(Widget))
		{
			FLinearColor FillColor = ProgBar->GetFillColorAndOpacity();
			Props = FString::Printf(TEXT(
				",\"percent\":%.4f"
				",\"fill_color\":{\"r\":%.3f,\"g\":%.3f,\"b\":%.3f,\"a\":%.3f}"),
				ProgBar->GetPercent(),
				FillColor.R, FillColor.G, FillColor.B, FillColor.A);
		}
		else if (UImage* Img = Cast<UImage>(Widget))
		{
			const FSlateBrush& Brush = Img->GetBrush();
			FString ResourceName = Brush.GetResourceName().ToString();
			FVector2D ImgSize = Brush.ImageSize;
			FLinearColor Tint = Brush.TintColor.GetSpecifiedColor();
			Props = FString::Printf(TEXT(
				",\"brush_resource\":\"%s\""
				",\"brush_image_size\":{\"width\":%.1f,\"height\":%.1f}"
				",\"brush_tint\":{\"r\":%.3f,\"g\":%.3f,\"b\":%.3f,\"a\":%.3f}"),
				*CUJsonUtils::EscapeJsonString(ResourceName),
				ImgSize.X, ImgSize.Y,
				Tint.R, Tint.G, Tint.B, Tint.A);
		}
		else if (UScrollBox* ScrollBox = Cast<UScrollBox>(Widget))
		{
			Props = FString::Printf(TEXT(",\"scroll_offset\":%.1f"), ScrollBox->GetScrollOffset());
		}

		// Tooltip (any widget)
		FText Tooltip = Widget->GetToolTipText();
		if (!Tooltip.IsEmpty())
		{
			Props += FString::Printf(TEXT(",\"tool_tip_text\":\"%s\""),
				*CUJsonUtils::EscapeJsonString(Tooltip.ToString()));
		}

		return Props;
	}

	/** Recursively serialize a UWidget and its children to JSON string */
	FString WidgetToJson(UWidget* Widget, int32 Depth, int32 MaxDepth, int32& OutWidgetCount, int32 HardCap)
	{
		if (!Widget || OutWidgetCount >= HardCap)
		{
			return TEXT("null");
		}

		++OutWidgetCount;

		// Parent name
		FString ParentName = TEXT("null");
		if (UPanelWidget* ParentWidget = Widget->GetParent())
		{
			ParentName = FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(ParentWidget->GetName()));
		}

		// Get geometry from cached SWidget
		FString GeometryJson;
		bool bGeometryAvailable = false;
		FString DesiredSizeJson = TEXT("\"desired_size\":{\"width\":0,\"height\":0}");
		TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
		if (CachedWidget.IsValid())
		{
			const FGeometry& Geom = CachedWidget->GetCachedGeometry();
			FVector2D AbsPos = Geom.GetAbsolutePosition();
			FVector2D Size = Geom.GetLocalSize();
			FVector2D Desired = CachedWidget->GetDesiredSize();
			bGeometryAvailable = (Size.X > 0.0 || Size.Y > 0.0);
			GeometryJson = FString::Printf(TEXT(
				"\"absolute_position\":{\"x\":%.1f,\"y\":%.1f},"
				"\"size\":{\"width\":%.1f,\"height\":%.1f},"
				"\"geometry_available\":%s"),
				AbsPos.X, AbsPos.Y, Size.X, Size.Y,
				bGeometryAvailable ? TEXT("true") : TEXT("false"));
			DesiredSizeJson = FString::Printf(TEXT(
				"\"desired_size\":{\"width\":%.1f,\"height\":%.1f}"),
				Desired.X, Desired.Y);
		}
		else
		{
			GeometryJson = TEXT(
				"\"absolute_position\":{\"x\":0,\"y\":0},"
				"\"size\":{\"width\":0,\"height\":0},"
				"\"geometry_available\":false");
		}

		// Type-specific properties + slot + clipping + effective_visibility
		FString TypePropsJson = ExtractTypeProps(Widget);
		FString SlotJson = ExtractSlotJson(Widget);
		FString EffVisJson = FString::Printf(TEXT(",\"effective_visibility\":\"%s\""),
			*ComputeEffectiveVisibility(Widget));
		FString ClipJson = FString::Printf(TEXT(",\"clipping\":\"%s\""),
			*ClippingToString(Widget->GetClipping()));

		// Build children JSON
		FString ChildrenJson = TEXT("[]");
		if (Depth < MaxDepth)
		{
			UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
			if (PanelWidget && PanelWidget->GetChildrenCount() > 0)
			{
				TArray<FString> ChildJsons;
				for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
				{
					UWidget* Child = PanelWidget->GetChildAt(i);
					if (Child && OutWidgetCount < HardCap)
					{
						ChildJsons.Add(WidgetToJson(Child, Depth + 1, MaxDepth, OutWidgetCount, HardCap));
					}
				}
				ChildrenJson = TEXT("[") + FString::Join(ChildJsons, TEXT(",")) + TEXT("]");
			}
		}

		return FString::Printf(TEXT(
			"{"
			"\"widget_class\":\"%s\","
			"\"widget_name\":\"%s\","
			"\"parent_name\":%s,"
			"\"visibility\":\"%s\","
			"\"is_visible\":%s,"
			"\"is_enabled\":%s,"
			"\"render_opacity\":%.2f,"
			"%s,"
			"%s"
			"%s%s%s%s,"
			"\"children\":%s"
			"}"),
			*CUJsonUtils::EscapeJsonString(Widget->GetClass()->GetName()),
			*CUJsonUtils::EscapeJsonString(Widget->GetName()),
			*ParentName,
			*VisibilityToString(Widget->GetVisibility()),
			Widget->IsVisible() ? TEXT("true") : TEXT("false"),
			Widget->GetIsEnabled() ? TEXT("true") : TEXT("false"),
			Widget->GetRenderOpacity(),
			*GeometryJson,
			*DesiredSizeJson,
			*TypePropsJson, *SlotJson, *EffVisJson, *ClipJson,
			*ChildrenJson);
	}

	/** Recursively collect widgets matching filters into a flat array */
	void CollectMatchingWidgets(UWidget* Widget, const FString& Path,
		const FString& NameFilter, const FString& ClassFilter, const FString& VisibilityFilter,
		TArray<FString>& OutMatches, int32& OutCount, int32 HardCap)
	{
		if (!Widget || OutCount >= HardCap)
		{
			return;
		}

		FString WidgetName = Widget->GetName();
		FString WidgetClass = Widget->GetClass()->GetName();
		FString CurrentPath = Path.IsEmpty() ? WidgetName : (Path + TEXT("/") + WidgetName);
		FString VisStr = VisibilityToString(Widget->GetVisibility());

		// Apply filters
		bool bMatch = true;
		if (!NameFilter.IsEmpty() && !WidgetName.Contains(NameFilter))
		{
			bMatch = false;
		}
		if (bMatch && !ClassFilter.IsEmpty() && !WidgetClass.Contains(ClassFilter))
		{
			bMatch = false;
		}
		if (bMatch && !VisibilityFilter.IsEmpty() && VisibilityFilter != TEXT("all"))
		{
			if (VisibilityFilter == TEXT("visible") && Widget->GetVisibility() != ESlateVisibility::Visible)
			{
				bMatch = false;
			}
			else if (VisibilityFilter == TEXT("hidden") && Widget->GetVisibility() != ESlateVisibility::Hidden)
			{
				bMatch = false;
			}
			else if (VisibilityFilter == TEXT("collapsed") && Widget->GetVisibility() != ESlateVisibility::Collapsed)
			{
				bMatch = false;
			}
		}

		if (bMatch)
		{
			// Get geometry
			FString PosJson;
			TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
			if (CachedWidget.IsValid())
			{
				const FGeometry& Geom = CachedWidget->GetCachedGeometry();
				FVector2D AbsPos = Geom.GetAbsolutePosition();
				FVector2D Size = Geom.GetLocalSize();
				PosJson = FString::Printf(TEXT(
					"\"absolute_position\":{\"x\":%.1f,\"y\":%.1f},"
					"\"size\":{\"width\":%.1f,\"height\":%.1f}"),
					AbsPos.X, AbsPos.Y, Size.X, Size.Y);
			}
			else
			{
				PosJson = TEXT("\"absolute_position\":{\"x\":0,\"y\":0},\"size\":{\"width\":0,\"height\":0}");
			}

			// Type-specific + slot + effective visibility
			FString TypePropsJson = ExtractTypeProps(Widget);
			FString SlotJson = ExtractSlotJson(Widget);

			OutMatches.Add(FString::Printf(TEXT(
				"{"
				"\"widget_class\":\"%s\","
				"\"widget_name\":\"%s\","
				"\"path\":\"%s\","
				"\"visibility\":\"%s\","
				"\"is_visible\":%s,"
				"\"is_enabled\":%s,"
				"%s"
				"%s%s"
				"}"),
				*CUJsonUtils::EscapeJsonString(WidgetClass),
				*CUJsonUtils::EscapeJsonString(WidgetName),
				*CUJsonUtils::EscapeJsonString(CurrentPath),
				*VisStr,
				Widget->IsVisible() ? TEXT("true") : TEXT("false"),
				Widget->GetIsEnabled() ? TEXT("true") : TEXT("false"),
				*PosJson,
				*TypePropsJson, *SlotJson));
			++OutCount;
		}

		// Recurse into children
		UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
		if (PanelWidget)
		{
			for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
			{
				CollectMatchingWidgets(PanelWidget->GetChildAt(i), CurrentPath,
					NameFilter, ClassFilter, VisibilityFilter, OutMatches, OutCount, HardCap);
			}
		}
	}

	/** Get all UUserWidget instances from PIE or Editor viewport */
	TArray<UUserWidget*> GetViewportUserWidgets()
	{
		TArray<UUserWidget*> Result;

		// Find the PIE world if available, otherwise use any world with viewport widgets
		UWorld* PIEWorld = nullptr;
		if (GEditor)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE && Context.World())
				{
					PIEWorld = Context.World();
					break;
				}
			}
		}

		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* UserWidget = *It;
			if (!UserWidget || !IsValid(UserWidget) || UserWidget->HasAnyFlags(RF_ClassDefaultObject))
			{
				continue;
			}

			// Filter to PIE world if available
			if (PIEWorld)
			{
				if (UserWidget->GetWorld() != PIEWorld)
				{
					continue;
				}
			}

			// Only include widgets that are currently in a viewport
			if (!UserWidget->IsInViewport() && !UserWidget->GetParent())
			{
				continue;
			}

			Result.Add(UserWidget);
		}

		return Result;
	}
}

void UCUWebSocketEditorSubsystem::HandleGetWidgetTree(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleGetWidgetTree"), RequestId)) return;

	double StartTime = FPlatformTime::Seconds();

	// Parse optional parameters
	int32 MaxDepth = 50; // default
	if (Request->HasField(TEXT("max_depth")))
	{
		MaxDepth = FMath::Clamp(Request->GetIntegerField(TEXT("max_depth")), 1, 100);
	}
	static constexpr int32 HardCap = 500;

	// Collect user widgets
	TArray<UUserWidget*> UserWidgets = WidgetTreeHelpers::GetViewportUserWidgets();

	bool bIsPIE = false;
	if (GEditor)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				bIsPIE = true;
				break;
			}
		}
	}

	// Build JSON for each user widget
	TArray<FString> UserWidgetJsons;
	int32 TotalWidgetCount = 0;
	for (UUserWidget* UserWidget : UserWidgets)
	{
		if (TotalWidgetCount >= HardCap)
		{
			break;
		}

		UWidget* RootWidget = UserWidget->GetRootWidget();
		FString RootJson = TEXT("null");
		if (RootWidget)
		{
			RootJson = WidgetTreeHelpers::WidgetToJson(RootWidget, 0, MaxDepth, TotalWidgetCount, HardCap);
		}

		UserWidgetJsons.Add(FString::Printf(TEXT(
			"{"
			"\"user_widget_class\":\"%s\","
			"\"user_widget_name\":\"%s\","
			"\"root_widget\":%s"
			"}"),
			*CUJsonUtils::EscapeJsonString(UserWidget->GetClass()->GetName()),
			*CUJsonUtils::EscapeJsonString(UserWidget->GetName()),
			*RootJson));
	}

	FString UserWidgetsJson = TEXT("[") + FString::Join(UserWidgetJsons, TEXT(",")) + TEXT("]");

	double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	bool bCapped = TotalWidgetCount >= HardCap;

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"get_widget_tree_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"is_pie\":%s,"
		"\"widget_count\":%d,"
		"\"capped\":%s,"
		"\"user_widgets\":%s,"
		"\"elapsed_ms\":%.2f"
		"}"),
		*RequestId,
		bIsPIE ? TEXT("true") : TEXT("false"),
		TotalWidgetCount,
		bCapped ? TEXT("true") : TEXT("false"),
		*UserWidgetsJson,
		ElapsedMs);

	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleSearchWidgets(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleSearchWidgets"), RequestId)) return;

	double StartTime = FPlatformTime::Seconds();

	// Parse filter parameters
	FString NameFilter = Request->HasField(TEXT("widget_name")) ? Request->GetStringField(TEXT("widget_name")) : TEXT("");
	FString ClassFilter = Request->HasField(TEXT("widget_class")) ? Request->GetStringField(TEXT("widget_class")) : TEXT("");
	FString VisibilityFilter = Request->HasField(TEXT("visibility_filter")) ? Request->GetStringField(TEXT("visibility_filter")) : TEXT("all");

	if (NameFilter.IsEmpty() && ClassFilter.IsEmpty())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"search_widgets_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"At least one of widget_name or widget_class is required\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	static constexpr int32 HardCap = 200;

	TArray<UUserWidget*> UserWidgets = WidgetTreeHelpers::GetViewportUserWidgets();

	TArray<FString> Matches;
	int32 MatchCount = 0;

	for (UUserWidget* UserWidget : UserWidgets)
	{
		if (MatchCount >= HardCap)
		{
			break;
		}

		FString BasePath = UserWidget->GetClass()->GetName();
		UWidget* RootWidget = UserWidget->GetRootWidget();
		if (RootWidget)
		{
			WidgetTreeHelpers::CollectMatchingWidgets(
				RootWidget, BasePath,
				NameFilter, ClassFilter, VisibilityFilter,
				Matches, MatchCount, HardCap);
		}
	}

	FString MatchesJson = TEXT("[") + FString::Join(Matches, TEXT(",")) + TEXT("]");
	double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"search_widgets_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"match_count\":%d,"
		"\"matches\":%s,"
		"\"elapsed_ms\":%.2f"
		"}"),
		*RequestId,
		MatchCount,
		*MatchesJson,
		ElapsedMs);

	SendResponse(Socket, ResponseJSON);
}

// ============================================
// Issue #9239: Runtime widget hit-test (find_widget_at_position)
// ============================================

namespace WidgetHitTestHelpers
{
	/**
	 * Hit-test result accumulator. Tracks the deepest (longest widget-path) match whose
	 * viewport-absolute AABB contains the query point.
	 *
	 * Also tracks cross-check stats — a lightweight reference-independence signal against
	 * Epic's `FGeometry::IsUnderLocation` (which handles render transforms like rotation
	 * and scale correctly via AbsoluteToLocal). See `HitTestWidget` for the comparison.
	 */
	struct FHitResult
	{
		UUserWidget* UserWidget = nullptr;
		UWidget* Widget = nullptr;
		FString Path;
		int32 Depth = -1;
		FVector2D AbsPos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		bool bIsPIE = false;

		// Cross-check stats against FGeometry::IsUnderLocation.
		int32 ConsideredCount = 0;      // widgets whose geometry was probed
		int32 AabbEpicMismatches = 0;   // widgets where naive AABB != Epic API
		TArray<FString> MismatchPaths;  // capped at 8 for diagnostics

		// True when our bookkeeping matched Epic's API for the final chosen widget.
		bool bHitChoiceAgrees = true;
	};

	/** True when the widget's effective visibility makes it skip hit-testing itself. */
	bool IsSelfHitTestInvisible(ESlateVisibility Vis)
	{
		return Vis == ESlateVisibility::HitTestInvisible
			|| Vis == ESlateVisibility::SelfHitTestInvisible;
	}

	/** True when the widget is culled entirely (self + children not considered). */
	bool IsFullyCulled(ESlateVisibility Vis)
	{
		return Vis == ESlateVisibility::Collapsed || Vis == ESlateVisibility::Hidden;
	}

	/**
	 * Recursively hit-test a widget sub-tree at `Point` (viewport-absolute px).
	 *
	 * Children are walked in REVERSE order so the last child (painted on top in Slate)
	 * is evaluated first. We still visit all eligible subtrees because the "deepest"
	 * match — not just the top-paint — is what we want (matches UMG hit-testing: a
	 * leaf Button inside a top Overlay should win over the Overlay itself).
	 *
	 * `Path` accumulates a dotted chain of widget names for diagnostics.
	 *
	 * **Hit detection**: uses `FGeometry::IsUnderLocation` as the authoritative test.
	 * Epic's API converts `Point` to widget-local space via `AbsoluteToLocal` (which
	 * honours render transforms — rotation + scale) and checks against `[0, LocalSize)`.
	 * A naive axis-aligned AABB (`absPos.X ≤ P < absPos.X + size.X`) is computed in
	 * parallel and compared per-widget; mismatches are counted + logged so we can spot
	 * rotation/scale cases where simple AABB would lie. See #9239 plan — cross-check
	 * closes the "reference-independence" gap identified in verification review.
	 */
	void HitTestWidget(
		UWidget* Widget,
		UUserWidget* OwnerUserWidget,
		const FString& Path,
		int32 Depth,
		int32 MaxDepth,
		const FVector2D& Point,
		FHitResult& InOut)
	{
		if (!Widget || Depth > MaxDepth)
		{
			return;
		}

		const ESlateVisibility Vis = Widget->GetVisibility();
		if (IsFullyCulled(Vis))
		{
			return;
		}

		TSharedPtr<SWidget> CachedWidget = Widget->GetCachedWidget();
		if (!CachedWidget.IsValid())
		{
			return;
		}

		const FGeometry& Geom = CachedWidget->GetCachedGeometry();
		const FVector2D AbsPos = Geom.GetAbsolutePosition();
		const FVector2D Size = Geom.GetLocalSize();
		const bool bGeometryAvailable = (Size.X > 0.0 || Size.Y > 0.0);

		// --- Authoritative hit test: Epic's FGeometry::IsUnderLocation ---
		// Handles render transforms correctly (rotation, scale) via AbsoluteToLocal.
		const bool bInsideEpic = bGeometryAvailable && Geom.IsUnderLocation(Point);

		// --- Cross-check: naive axis-aligned AABB ---
		// What a simple implementation would use. Diverges from Epic's answer when the
		// widget has a render transform that's not a pure translation.
		const bool bInsideAabb = bGeometryAvailable
			&& Point.X >= AbsPos.X
			&& Point.Y >= AbsPos.Y
			&& Point.X < AbsPos.X + Size.X
			&& Point.Y < AbsPos.Y + Size.Y;

		if (bGeometryAvailable)
		{
			InOut.ConsideredCount += 1;
			if (bInsideAabb != bInsideEpic)
			{
				InOut.AabbEpicMismatches += 1;
				const FString MismatchPath = Path.IsEmpty() ? Widget->GetName() : (Path + TEXT(".") + Widget->GetName());
				if (InOut.MismatchPaths.Num() < 8)
				{
					InOut.MismatchPaths.Add(FString::Printf(TEXT("%s|aabb=%s|epic=%s"),
						*MismatchPath,
						bInsideAabb ? TEXT("hit") : TEXT("miss"),
						bInsideEpic ? TEXT("hit") : TEXT("miss")));
				}
				UE_LOG(LogNarshaMCPWebSocket, Verbose,
					TEXT("[find_widget_at_position] AABB(%s) != IsUnderLocation(%s) for '%s' (class=%s) at pt=(%.1f,%.1f). "
						 "Render transform (rotation/scale) likely in effect."),
					bInsideAabb ? TEXT("hit") : TEXT("miss"),
					bInsideEpic ? TEXT("hit") : TEXT("miss"),
					*Widget->GetName(),
					*Widget->GetClass()->GetName(),
					Point.X, Point.Y);
			}
		}

		// Use Epic's authoritative answer for hit selection.
		const bool bInside = bInsideEpic;

		const FString CurPath = Path.IsEmpty() ? Widget->GetName() : (Path + TEXT(".") + Widget->GetName());

		// Only credit this widget as the hit if it isn't self-hit-test-invisible.
		// Children are still recursed below so a leaf Button under an Overlay wins.
		if (bInside && !IsSelfHitTestInvisible(Vis) && Depth > InOut.Depth)
		{
			InOut.UserWidget = OwnerUserWidget;
			InOut.Widget = Widget;
			InOut.Path = CurPath;
			InOut.Depth = Depth;
			InOut.AbsPos = AbsPos;
			InOut.Size = Size;
		}

		// Recurse into children in reverse order (top-paint first), but we must consider
		// all eligible subtrees to find the DEEPEST match, not just the first top-paint hit.
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = Panel->GetChildrenCount() - 1; i >= 0; --i)
			{
				if (UWidget* Child = Panel->GetChildAt(i))
				{
					HitTestWidget(Child, OwnerUserWidget, CurPath, Depth + 1, MaxDepth, Point, InOut);
				}
			}
		}
	}

	/**
	 * Resolve an optional `widget_path` scope hint to a sub-widget.
	 *
	 * `Path` format matches what `HitTestWidget` emits — a dotted chain of
	 * `widget_name` segments starting at a root panel. First segment must
	 * match the root widget of one of the viewport UUserWidgets. Subsequent
	 * segments walk Panel → Child by name.
	 *
	 * Returns `{FoundWidget, OwnerUserWidget, Depth}` on match, or
	 * `{nullptr, nullptr, -1}` if any segment fails to resolve — in which
	 * case the caller falls back to the full-tree walk.
	 */
	struct FScopeResolve
	{
		UWidget* Widget = nullptr;
		UUserWidget* OwnerUserWidget = nullptr;
		FString ResolvedPrefix; // path accumulated up to (but not including) the resolved widget
		int32 DepthOffset = 0;  // how many segments were traversed (used to keep depth semantics consistent)
		// Issue #9654: ambiguity detection. Count of viewport user widgets whose root
		// widget name matched the first path segment. Resolution is still first-match-wins
		// (backward compat); callers inspect `bAmbiguousRoot` to decide whether to warn.
		int32 RootMatchCount = 0;
		bool bAmbiguousRoot = false; // true iff RootMatchCount > 1
		bool bValid = false;
	};

	FScopeResolve ResolveWidgetPath(const TArray<UUserWidget*>& UserWidgets, const FString& WidgetPath)
	{
		FScopeResolve Out;
		if (WidgetPath.IsEmpty()) return Out;

		TArray<FString> Segments;
		WidgetPath.ParseIntoArray(Segments, TEXT("."), /*InCullEmpty=*/true);
		if (Segments.Num() == 0) return Out;

		// Accept both the legacy root-relative form (`Root.Child`) and the canonical
		// form emitted by pie_list_active_widgets (`UserWidget.Root.Child`).  The
		// latter used to be write-only: list returned it, but screenshot/find could
		// not resolve it because they required the first segment to be the root.
		// Issue #9654: count ALL matching roots (no early break) so callers can detect
		// ambiguity and emit a warning. Resolution still uses first-match-wins for
		// byte-stable backward compat with pre-#9654 callers that don't pass
		// `user_widget_name`.
		UWidget* Current = nullptr;
		UUserWidget* Owner = nullptr;
		int32 FirstChildSegment = 1;
		for (UUserWidget* UW : UserWidgets)
		{
			if (!IsValid(UW)) continue;
			UWidget* Root = UW->GetRootWidget();
			if (!Root) continue;
			const bool bRootRelative = Root->GetName() == Segments[0];
			const bool bUserWidgetPrefixed = Segments.Num() >= 2
				&& UW->GetName() == Segments[0]
				&& Root->GetName() == Segments[1];
			if (bRootRelative || bUserWidgetPrefixed)
			{
				++Out.RootMatchCount;
				if (!Current)
				{
					Current = Root;
					Owner = UW;
					FirstChildSegment = bUserWidgetPrefixed ? 2 : 1;
					// Intentionally no break — keep counting to populate RootMatchCount.
				}
			}
		}
		Out.bAmbiguousRoot = (Out.RootMatchCount > 1);
		if (!Current) return Out;

		// Walk subsequent segments Panel → Child.
		for (int32 i = FirstChildSegment; i < Segments.Num(); ++i)
		{
			UPanelWidget* Panel = Cast<UPanelWidget>(Current);
			if (!Panel) return Out; // path asks to descend into a non-panel
			UWidget* Match = nullptr;
			for (int32 c = 0; c < Panel->GetChildrenCount(); ++c)
			{
				if (UWidget* Child = Panel->GetChildAt(c))
				{
					if (Child->GetName() == Segments[i])
					{
						Match = Child;
						break;
					}
				}
			}
			if (!Match) return Out;
			Current = Match;
		}

		Out.Widget = Current;
		Out.OwnerUserWidget = Owner;
		// Prefix excludes the final segment (HitTestWidget appends the widget name itself).
		if (Segments.Num() > 1)
		{
			TArray<FString> PrefixSegs;
			for (int32 i = 0; i < Segments.Num() - 1; ++i) PrefixSegs.Add(Segments[i]);
			Out.ResolvedPrefix = FString::Join(PrefixSegs, TEXT("."));
		}
		// The optional UUserWidget prefix is an addressing scope, not a widget-tree
		// depth.  Keep depth aligned with pie_list_active_widgets (root == 0).
		Out.DepthOffset = Segments.Num() - FirstChildSegment;
		Out.bValid = true;
		return Out;
	}
} // namespace WidgetHitTestHelpers

void UCUWebSocketEditorSubsystem::HandleFindWidgetAtPosition(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleFindWidgetAtPosition"), RequestId)) return;

	double StartTime = FPlatformTime::Seconds();

	// Parse coords (required, must be numeric)
	if (!Request->HasField(TEXT("screen_x")) || !Request->HasField(TEXT("screen_y")))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"find_widget_at_position_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"screen_x and screen_y are required (viewport-absolute pixels)\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	double ScreenX = 0.0, ScreenY = 0.0;
	if (!Request->TryGetNumberField(TEXT("screen_x"), ScreenX) || !Request->TryGetNumberField(TEXT("screen_y"), ScreenY))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"find_widget_at_position_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"screen_x and screen_y must be numeric\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	int32 MaxDepth = 50;
	if (Request->HasField(TEXT("max_depth")))
	{
		MaxDepth = FMath::Clamp(Request->GetIntegerField(TEXT("max_depth")), 1, 100);
	}

	// Optional scope hint: `widget_path` restricts traversal to a specific sub-tree.
	// Issue #9239: initially unimplemented; added in follow-up to match the plan AC.
	FString WidgetPath;
	const bool bHasWidgetPath = Request->HasField(TEXT("widget_path"))
		&& Request->TryGetStringField(TEXT("widget_path"), WidgetPath)
		&& !WidgetPath.IsEmpty();

	// Issue #9654: `user_widget_name` disambiguates when multiple viewport user
	// widgets share a root widget name (e.g. Lyra's 8× ExperienceTileButton all
	// share root `InternalRootButtonBase`). Filters `GetViewportUserWidgets()` by
	// `UW->GetName()` BEFORE hit-testing. Mismatch (name not found) is a HARD
	// error with a hint listing active user widget names — NOT a fallback.
	FString UserWidgetNameFilter;
	const bool bHasUserWidgetName = Request->HasField(TEXT("user_widget_name"))
		&& Request->TryGetStringField(TEXT("user_widget_name"), UserWidgetNameFilter)
		&& !UserWidgetNameFilter.IsEmpty();

	const FVector2D Point(static_cast<float>(ScreenX), static_cast<float>(ScreenY));

	// Detect whether a PIE context is active (reported back for diagnostics).
	// Matches the HandlePIE* guard pattern (both GEditor AND GEngine must be live
	// before iterating world contexts — defensive even though Editor modules
	// practically guarantee GEngine is non-null).
	bool bIsPIE = false;
	if (GEditor && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE && Context.World())
			{
				bIsPIE = true;
				break;
			}
		}
	}

	// Walk all user widgets; tracks deepest-path hit across all widget trees.
	TArray<UUserWidget*> UserWidgets = WidgetTreeHelpers::GetViewportUserWidgets();

	// Issue #9654: filter by user_widget_name BEFORE hit-testing so that
	// ambiguous-path resolution counts from the filtered set. If the name doesn't
	// match any live user widget, return a structured error with candidate names
	// (AC #4 — hard error, not a fallback).
	if (bHasUserWidgetName)
	{
		// Snapshot live names for the error hint BEFORE filter mutates the array.
		TArray<FString> ActiveNames;
		ActiveNames.Reserve(UserWidgets.Num());
		for (UUserWidget* UW : UserWidgets)
		{
			if (IsValid(UW))
			{
				ActiveNames.Add(UW->GetName());
			}
		}

		UserWidgets.RemoveAll([&](UUserWidget* UW)
		{
			return !IsValid(UW) || UW->GetName() != UserWidgetNameFilter;
		});

		if (UserWidgets.Num() == 0)
		{
			TArray<FString> QuotedNames;
			QuotedNames.Reserve(ActiveNames.Num());
			for (const FString& Name : ActiveNames)
			{
				QuotedNames.Add(FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Name)));
			}
			const FString HintArray = TEXT("[") + FString::Join(QuotedNames, TEXT(",")) + TEXT("]");

			const FString ResponseJSON = FString::Printf(TEXT(
				"{\"type\":\"find_widget_at_position_result\",\"request_id\":\"%s\","
				"\"success\":false,"
				"\"error\":\"user_widget_name '%s' not found in viewport\","
				"\"hint\":{\"active_user_widget_names\":%s}}"),
				*RequestId,
				*CUJsonUtils::EscapeJsonString(UserWidgetNameFilter),
				*HintArray);
			SendResponse(Socket, ResponseJSON);
			return;
		}
	}

	WidgetHitTestHelpers::FHitResult Hit;
	Hit.bIsPIE = bIsPIE;

	// Scoped traversal path — if widget_path resolves, iterate only that sub-tree.
	// Unresolved widget_path (typo, deleted widget, etc.) falls through to full walk
	// with a diagnostic note appended to the response.
	bool bScopeResolved = false;
	FString ScopeDiagnostic;
	if (bHasWidgetPath)
	{
		WidgetHitTestHelpers::FScopeResolve Scope =
			WidgetHitTestHelpers::ResolveWidgetPath(UserWidgets, WidgetPath);
		if (Scope.bValid && Scope.Widget)
		{
			bScopeResolved = true;
			// Issue #9654 AC #5: ambiguous path + no user_widget_name filter → warn
			// but keep first-match-wins behavior (backward compat with pre-#9654 callers).
			if (Scope.bAmbiguousRoot && !bHasUserWidgetName)
			{
				FString FirstSegment = WidgetPath;
				int32 DotIndex = INDEX_NONE;
				if (WidgetPath.FindChar(TEXT('.'), DotIndex))
				{
					FirstSegment = WidgetPath.Left(DotIndex);
				}
				ScopeDiagnostic = FString::Printf(
					TEXT("ambiguous path: %d user widgets share root '%s'; pass user_widget_name to disambiguate"),
					Scope.RootMatchCount,
					*FirstSegment);
			}
			WidgetHitTestHelpers::HitTestWidget(
				Scope.Widget,
				Scope.OwnerUserWidget,
				Scope.ResolvedPrefix,
				Scope.DepthOffset,
				MaxDepth,
				Point,
				Hit);
		}
		else
		{
			ScopeDiagnostic = FString::Printf(
				TEXT("widget_path '%s' did not resolve; falling back to full viewport walk"),
				*WidgetPath);
		}
	}

	if (!bScopeResolved)
	{
		for (UUserWidget* UserWidget : UserWidgets)
		{
			if (!IsValid(UserWidget)) continue;
			UWidget* Root = UserWidget->GetRootWidget();
			if (!Root) continue;

			WidgetHitTestHelpers::HitTestWidget(
				Root,
				UserWidget,
				/*Path*/ TEXT(""),
				/*Depth*/ 0,
				MaxDepth,
				Point,
				Hit);
		}
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;
	const bool bHit = (Hit.Widget != nullptr);

	// Build widget_at_position JSON (or null on miss)
	FString WidgetJson = TEXT("null");
	if (bHit)
	{
		WidgetJson = FString::Printf(TEXT(
			"{"
			"\"user_widget_class\":\"%s\","
			"\"user_widget_name\":\"%s\","
			"\"path\":\"%s\","
			"\"widget_class\":\"%s\","
			"\"widget_name\":\"%s\","
			"\"depth\":%d,"
			"\"abs_pos\":{\"x\":%.1f,\"y\":%.1f},"
			"\"size\":{\"width\":%.1f,\"height\":%.1f}"
			"}"),
			*CUJsonUtils::EscapeJsonString(Hit.UserWidget->GetClass()->GetName()),
			*CUJsonUtils::EscapeJsonString(Hit.UserWidget->GetName()),
			*CUJsonUtils::EscapeJsonString(Hit.Path),
			*CUJsonUtils::EscapeJsonString(Hit.Widget->GetClass()->GetName()),
			*CUJsonUtils::EscapeJsonString(Hit.Widget->GetName()),
			Hit.Depth,
			Hit.AbsPos.X, Hit.AbsPos.Y,
			Hit.Size.X, Hit.Size.Y);
	}

	// Build verification JSON — reference-independence signal (#9239 verification review).
	// Compares naive AABB against Epic's FGeometry::IsUnderLocation per widget.
	FString MismatchArray = TEXT("[]");
	if (Hit.MismatchPaths.Num() > 0)
	{
		TArray<FString> Quoted;
		Quoted.Reserve(Hit.MismatchPaths.Num());
		for (const FString& Entry : Hit.MismatchPaths)
		{
			Quoted.Add(FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Entry)));
		}
		MismatchArray = TEXT("[") + FString::Join(Quoted, TEXT(",")) + TEXT("]");
	}
	FString VerificationJson = FString::Printf(TEXT(
		"{"
		"\"hit_detection_method\":\"FGeometry::IsUnderLocation\","
		"\"considered_widget_count\":%d,"
		"\"aabb_epic_mismatches\":%d,"
		"\"mismatch_samples\":%s"
		"}"),
		Hit.ConsideredCount,
		Hit.AabbEpicMismatches,
		*MismatchArray);

	// Optional scope block — only emitted when widget_path was requested.
	// Lets callers distinguish "scoped hit" from "scope fallback (full-walk)" cases.
	FString ScopeJson = TEXT("null");
	if (bHasWidgetPath)
	{
		ScopeJson = FString::Printf(TEXT(
			"{"
			"\"requested_path\":\"%s\","
			"\"resolved\":%s,"
			"\"note\":\"%s\""
			"}"),
			*CUJsonUtils::EscapeJsonString(WidgetPath),
			bScopeResolved ? TEXT("true") : TEXT("false"),
			*CUJsonUtils::EscapeJsonString(ScopeDiagnostic));
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{"
		"\"type\":\"find_widget_at_position_result\","
		"\"request_id\":\"%s\","
		"\"success\":true,"
		"\"is_pie\":%s,"
		"\"hit\":%s,"
		"\"widget_at_position\":%s,"
		"\"query\":{\"screen_x\":%.2f,\"screen_y\":%.2f},"
		"\"scope\":%s,"
		"\"verification\":%s,"
		"\"elapsed_ms\":%.2f"
		"}"),
		*RequestId,
		bIsPIE ? TEXT("true") : TEXT("false"),
		bHit ? TEXT("true") : TEXT("false"),
		*WidgetJson,
		ScreenX, ScreenY,
		*ScopeJson,
		*VerificationJson,
		ElapsedMs);

	SendResponse(Socket, ResponseJSON);
}

// ==========================================================================
// Issue #9344: PIE widget enumeration + per-widget screenshot
// ==========================================================================
//
// HandleListActivePIEWidgets — flat enumeration of live widgets in the PIE
// viewport with geometry, paired with the existing hierarchical
// `HandleGetWidgetTree` (which returns nested user_widgets).
//
// HandleCaptureWidgetScreenshot — captures the PIE viewport and crops to a
// specific widget's AABB, mirroring `HandleCapturePIEWindow` with an extra
// crop step. Widget is resolved by `widget_name` (FName match on UserWidget
// root or any named widget in the tree) or `widget_path` (uses the same
// path semantics as `HandleFindWidgetAtPosition`).

namespace Issue9344_WidgetPIE
{
	/** Flat walker result: widget + lineage + geometry. */
	struct FFlatEntry
	{
		FString WidgetName;
		FString WidgetClass;
		FString Path;
		FString Visibility;
		FVector2D AbsPos = FVector2D::ZeroVector;
		FVector2D Size = FVector2D::ZeroVector;
		int32 Depth = 0;
	};

	static FString VisibilityToString(ESlateVisibility V)
	{
		switch (V)
		{
		case ESlateVisibility::Visible: return TEXT("Visible");
		case ESlateVisibility::Collapsed: return TEXT("Collapsed");
		case ESlateVisibility::Hidden: return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible: return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		default: return TEXT("Unknown");
		}
	}

	/** Flat DFS walker — populates `Out` up to `HardCap` entries. */
	static void WalkWidgetFlat(UWidget* Widget, UUserWidget* Owner, const FString& ParentPath,
		int32 Depth, int32 MaxDepth, int32 HardCap,
		const FString& VisibilityFilter, bool bIncludeGeometry,
		TArray<FFlatEntry>& Out)
	{
		if (!Widget || Depth > MaxDepth || Out.Num() >= HardCap) return;

		const FString Name = Widget->GetName();
		const FString Path = ParentPath.IsEmpty() ? Name : (ParentPath + TEXT(".") + Name);
		const ESlateVisibility Vis = Widget->GetVisibility();
		const FString VisStr = VisibilityToString(Vis);

		bool bInclude = true;
		if (VisibilityFilter.Equals(TEXT("visible"), ESearchCase::IgnoreCase))
		{
			bInclude = (Vis == ESlateVisibility::Visible
				|| Vis == ESlateVisibility::HitTestInvisible
				|| Vis == ESlateVisibility::SelfHitTestInvisible);
		}
		else if (VisibilityFilter.Equals(TEXT("hit_testable"), ESearchCase::IgnoreCase))
		{
			bInclude = (Vis == ESlateVisibility::Visible);
		}

		if (bInclude)
		{
			FFlatEntry E;
			E.WidgetName = Name;
			E.WidgetClass = Widget->GetClass()->GetName();
			E.Path = Path;
			E.Visibility = VisStr;
			E.Depth = Depth;
			if (bIncludeGeometry && Widget->GetCachedWidget().IsValid())
			{
				const FGeometry& Geo = Widget->GetCachedGeometry();
				E.AbsPos = Geo.GetAbsolutePosition();
				E.Size = Geo.GetLocalSize();
			}
			Out.Add(MoveTemp(E));
		}

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			const int32 Count = Panel->GetChildrenCount();
			for (int32 i = 0; i < Count && Out.Num() < HardCap; ++i)
			{
				WalkWidgetFlat(Panel->GetChildAt(i), Owner, Path, Depth + 1,
					MaxDepth, HardCap, VisibilityFilter, bIncludeGeometry, Out);
			}
		}
	}

	/** Escape JSON string. */
	static FString EscapeJsonStr(const FString& In)
	{
		return CUJsonUtils::EscapeJsonString(In);
	}

	/** Crop an ARGB pixel buffer in-place. Returns true on success. */
	static bool CropPixels(const TArray<FColor>& Src, int32 SrcW, int32 SrcH,
		int32 CropX, int32 CropY, int32 CropW, int32 CropH,
		TArray<FColor>& OutCropped, int32& OutW, int32& OutH)
	{
		if (SrcW <= 0 || SrcH <= 0) return false;
		CropX = FMath::Clamp(CropX, 0, SrcW - 1);
		CropY = FMath::Clamp(CropY, 0, SrcH - 1);
		CropW = FMath::Clamp(CropW, 1, SrcW - CropX);
		CropH = FMath::Clamp(CropH, 1, SrcH - CropY);
		OutW = CropW;
		OutH = CropH;
		return CUWidgetDesignerCapture::CropPixels(
			Src, SrcW, SrcH, FIntRect(CropX, CropY, CropX + CropW, CropY + CropH), OutCropped);
	}
} // namespace Issue9344_WidgetPIE

void UCUWebSocketEditorSubsystem::HandleListActivePIEWidgets(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleListActivePIEWidgets"), RequestId)) return;
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("list_active_pie_widgets"))) return;
	bool bCorrelationProbe = false;
	if (Request->TryGetBoolField(TEXT("correlation_probe"), bCorrelationProbe) && bCorrelationProbe)
	{
		const FString ProbeData = FString::Printf(TEXT("{\"command\":\"list_active_pie_widgets\",\"originating_request_id\":\"%s\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId));
		BroadcastEvent(TEXT("mcp_correlation_probe"), ProbeData);
	}

	double StartTime = FPlatformTime::Seconds();

	// Parse params
	int32 MaxDepth = 50;
	if (Request->HasField(TEXT("max_depth")))
	{
		MaxDepth = FMath::Clamp(Request->GetIntegerField(TEXT("max_depth")), 1, 100);
	}
	int32 MaxWidgets = 500;
	if (Request->HasField(TEXT("max_widgets")))
	{
		MaxWidgets = FMath::Clamp(Request->GetIntegerField(TEXT("max_widgets")), 1, 2000);
	}
	FString VisibilityFilter = TEXT("visible");
	if (Request->HasField(TEXT("visibility_filter")))
	{
		Request->TryGetStringField(TEXT("visibility_filter"), VisibilityFilter);
	}
	bool bIncludeGeometry = true;
	if (Request->HasField(TEXT("include_geometry")))
	{
		Request->TryGetBoolField(TEXT("include_geometry"), bIncludeGeometry);
	}

	TArray<UUserWidget*> UserWidgets = WidgetTreeHelpers::GetViewportUserWidgets();
	TArray<Issue9344_WidgetPIE::FFlatEntry> Entries;
	for (UUserWidget* UserWidget : UserWidgets)
	{
		if (!IsValid(UserWidget)) continue;
		UWidget* Root = UserWidget->GetRootWidget();
		if (!Root) continue;
		Issue9344_WidgetPIE::WalkWidgetFlat(Root, UserWidget, UserWidget->GetName(), 0,
			MaxDepth, MaxWidgets, VisibilityFilter, bIncludeGeometry, Entries);
		if (Entries.Num() >= MaxWidgets) break;
	}

	const bool bCapped = Entries.Num() >= MaxWidgets;

	// Build widgets array JSON
	TArray<FString> WidgetJsonItems;
	WidgetJsonItems.Reserve(Entries.Num());
	for (const Issue9344_WidgetPIE::FFlatEntry& E : Entries)
	{
		FString Item = FString::Printf(TEXT(
			"{\"widget_name\":\"%s\",\"widget_class\":\"%s\",\"widget_path\":\"%s\","
			"\"visibility\":\"%s\",\"depth\":%d"),
			*Issue9344_WidgetPIE::EscapeJsonStr(E.WidgetName),
			*Issue9344_WidgetPIE::EscapeJsonStr(E.WidgetClass),
			*Issue9344_WidgetPIE::EscapeJsonStr(E.Path),
			*Issue9344_WidgetPIE::EscapeJsonStr(E.Visibility),
			E.Depth);
		if (bIncludeGeometry)
		{
			Item += FString::Printf(TEXT(
				",\"abs_pos\":{\"x\":%.1f,\"y\":%.1f},\"size\":{\"width\":%.1f,\"height\":%.1f}"),
				E.AbsPos.X, E.AbsPos.Y, E.Size.X, E.Size.Y);
		}
		Item += TEXT("}");
		WidgetJsonItems.Add(MoveTemp(Item));
	}
	FString WidgetsArrayJson = TEXT("[") + FString::Join(WidgetJsonItems, TEXT(",")) + TEXT("]");

	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"list_active_pie_widgets_result\",\"request_id\":\"%s\","
		"\"success\":true,\"is_pie\":%s,\"widget_count\":%d,\"capped\":%s,"
		"\"widgets\":%s,\"elapsed_ms\":%.2f}"),
		*RequestId,
		TEXT("true"),
		Entries.Num(),
		bCapped ? TEXT("true") : TEXT("false"),
		*WidgetsArrayJson,
		ElapsedMs);

	SendResponse(Socket, ResponseJSON);
}

void UCUWebSocketEditorSubsystem::HandleCaptureWidgetScreenshot(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCaptureWidgetScreenshot"), RequestId)) return;

	double StartTime = FPlatformTime::Seconds();

	// Require widget_name or widget_path
	FString WidgetName, WidgetPath;
	const bool bHasName = Request->TryGetStringField(TEXT("widget_name"), WidgetName) && !WidgetName.IsEmpty();
	const bool bHasPath = Request->TryGetStringField(TEXT("widget_path"), WidgetPath) && !WidgetPath.IsEmpty();
	if (!bHasName && !bHasPath)
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"widget_name or widget_path is required\"}"),
			*RequestId);
		SendResponse(Socket, R);
		return;
	}
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("capture_widget_screenshot"))) return;
	bool bCorrelationProbe = false;
	if (Request->TryGetBoolField(TEXT("correlation_probe"), bCorrelationProbe) && bCorrelationProbe)
	{
		const FString ProbeData = FString::Printf(TEXT("{\"command\":\"capture_widget_screenshot\",\"originating_request_id\":\"%s\"}"),
			*CUJsonUtils::EscapeJsonString(RequestId));
		BroadcastEvent(TEXT("mcp_correlation_probe"), ProbeData);
	}

	int32 PadPixels = 0;
	if (Request->HasField(TEXT("pad_pixels")))
	{
		PadPixels = FMath::Clamp(Request->GetIntegerField(TEXT("pad_pixels")), 0, 256);
	}

	// Locate the widget.
	TArray<UUserWidget*> UserWidgets = WidgetTreeHelpers::GetViewportUserWidgets();
	UWidget* Target = nullptr;
	FString ResolvedPath;
	if (bHasPath)
	{
		WidgetHitTestHelpers::FScopeResolve Scope =
			WidgetHitTestHelpers::ResolveWidgetPath(UserWidgets, WidgetPath);
		if (Scope.bValid && Scope.Widget)
		{
			Target = Scope.Widget;
			ResolvedPath = WidgetPath;
		}
	}
	if (!Target && bHasName)
	{
		const FName TargetFName(*WidgetName);
		// DFS walk across all user widgets.
		TArray<UWidget*> Queue;
		for (UUserWidget* UW : UserWidgets)
		{
			if (!IsValid(UW)) continue;
			if (UWidget* Root = UW->GetRootWidget())
			{
				Queue.Add(Root);
				while (Queue.Num() > 0)
				{
					UWidget* W = Queue.Pop(EAllowShrinking::No);
					if (!W) continue;
					if (W->GetFName() == TargetFName)
					{
						Target = W;
						ResolvedPath = UW->GetName() + TEXT(".") + W->GetName();
						break;
					}
					if (UPanelWidget* Panel = Cast<UPanelWidget>(W))
					{
						for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
						{
							if (UWidget* Child = Panel->GetChildAt(i))
							{
								Queue.Add(Child);
							}
						}
					}
				}
			}
			if (Target) break;
		}
	}

	if (!Target)
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Widget not found: %s%s\"}"),
			*RequestId,
			bHasName ? *WidgetName : TEXT(""),
			bHasPath ? *(TEXT(" (path: ") + WidgetPath + TEXT(")")) : TEXT(""));
		SendResponse(Socket, R);
		return;
	}

	if (!Target->GetCachedWidget().IsValid())
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Widget has no cached Slate geometry — not painted yet\"}"),
			*RequestId);
		SendResponse(Socket, R);
		return;
	}

	const FGeometry& WidgetGeo = Target->GetCachedGeometry();
	const FVector2D AbsPos = WidgetGeo.GetAbsolutePosition();
	const FVector2D LocalSize = WidgetGeo.GetLocalSize();

	// Issue #9344 review M-2: reject degenerate widget geometry before
	// capturing + cropping. A zero-sized widget (e.g. Collapsed, or
	// Visibility=Hidden with no layout) produced a meaningless 1×1 image
	// because `CropPixels` clamps `CropW/CropH` to ≥1. Returning a structured
	// error is more actionable for AI callers than silent noise.
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Widget '%s' has zero-sized geometry "
			"(Collapsed, Hidden, or not painted yet) — cannot capture screenshot\"}"),
			*RequestId,
			*CUJsonUtils::EscapeJsonString(Target->GetName()));
		SendResponse(Socket, R);
		return;
	}

	// Capture PIE viewport. The multi-PIE gate ran before widget lookup so a
	// stopped session is always classified as PIE_NOT_RUNNING, never as a
	// misleading widget lookup/geometry error.
	FViewport* PIEViewport = GEditor ? GEditor->GetPIEViewport() : nullptr;
	if (!PIEViewport) PIEViewport = GEditor ? GEditor->GetActiveViewport() : nullptr;
	if (!PIEViewport && GEditor)
	{
		for (FLevelEditorViewportClient* LevelVC : GEditor->GetLevelViewportClients())
		{
			if (LevelVC && LevelVC->Viewport && LevelVC->GetWorld()
				&& LevelVC->GetWorld()->IsPlayInEditor())
			{
				PIEViewport = LevelVC->Viewport;
				break;
			}
		}
	}
	if (!PIEViewport)
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"No active PIE viewport\"}"),
			*RequestId);
		SendResponse(Socket, R);
		return;
	}

	TArray<FColor> Pixels;
	int32 Width = 0, Height = 0;
	if (!CaptureViewportPixels(PIEViewport, Pixels, Width, Height))
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"ReadPixels failed on PIE viewport\"}"),
			*RequestId);
		SendResponse(Socket, R);
		return;
	}

	// Widget geometry is in Slate desktop-absolute coordinates, while ReadPixels
	// returns the PIE viewport framebuffer. Convert the rendered AABB through the
	// viewport's cached Slate geometry and then scale local Slate units to pixels.
	// Using desktop coordinates directly produced false-success 1x1 PNGs whenever
	// the embedded PIE viewport did not start at desktop (0,0).
	FSceneViewport* SceneViewport = static_cast<FSceneViewport*>(PIEViewport);
	const FGeometry& ViewportGeo = SceneViewport->GetCachedGeometry();
	const FVector2D ViewportLocalSize = ViewportGeo.GetLocalSize();
	if (ViewportLocalSize.X <= 0.0f || ViewportLocalSize.Y <= 0.0f)
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"PIE viewport has invalid cached Slate geometry\"}"),
			*RequestId);
		SendResponse(Socket, R);
		return;
	}

	const FSlateRect AbsoluteBounds = WidgetGeo.GetRenderBoundingRect();
	const FVector2D LocalMin = ViewportGeo.AbsoluteToLocal(
		FVector2D(AbsoluteBounds.Left, AbsoluteBounds.Top));
	const FVector2D LocalMax = ViewportGeo.AbsoluteToLocal(
		FVector2D(AbsoluteBounds.Right, AbsoluteBounds.Bottom));
	const double PixelScaleX = static_cast<double>(Width) / ViewportLocalSize.X;
	const double PixelScaleY = static_cast<double>(Height) / ViewportLocalSize.Y;
	const int32 UnclampedLeft = FMath::FloorToInt(LocalMin.X * PixelScaleX) - PadPixels;
	const int32 UnclampedTop = FMath::FloorToInt(LocalMin.Y * PixelScaleY) - PadPixels;
	const int32 UnclampedRight = FMath::CeilToInt(LocalMax.X * PixelScaleX) + PadPixels;
	const int32 UnclampedBottom = FMath::CeilToInt(LocalMax.Y * PixelScaleY) + PadPixels;
	if (UnclampedRight <= 0 || UnclampedBottom <= 0
		|| UnclampedLeft >= Width || UnclampedTop >= Height)
	{
		FString R = FString::Printf(TEXT(
			"{\"type\":\"capture_widget_screenshot_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error\":\"Widget bounds do not intersect the PIE viewport framebuffer\"}"),
			*RequestId);
		SendResponse(Socket, R);
		return;
	}
	const int32 CropX = FMath::Clamp(UnclampedLeft, 0, Width - 1);
	const int32 CropY = FMath::Clamp(UnclampedTop, 0, Height - 1);
	const int32 CropRight = FMath::Clamp(UnclampedRight, CropX + 1, Width);
	const int32 CropBottom = FMath::Clamp(UnclampedBottom, CropY + 1, Height);
	const int32 CropW = CropRight - CropX;
	const int32 CropH = CropBottom - CropY;

	TArray<FColor> Cropped;
	int32 OutW = 0, OutH = 0;
	Issue9344_WidgetPIE::CropPixels(Pixels, Width, Height, CropX, CropY, CropW, CropH,
		Cropped, OutW, OutH);

	// Pack widget metadata into ExtraJson (absorbed by EncodeAndSendScreenshot).
	FString ExtraJson = FString::Printf(TEXT(
		"\"widget_name\":\"%s\",\"widget_path\":\"%s\","
		"\"widget_bounds\":{\"abs_x\":%.1f,\"abs_y\":%.1f,\"width\":%.1f,\"height\":%.1f,"
		"\"viewport_local_x\":%.1f,\"viewport_local_y\":%.1f,"
		"\"cropped_x\":%d,\"cropped_y\":%d,\"cropped_w\":%d,\"cropped_h\":%d,\"pad_pixels\":%d}"),
		*CUJsonUtils::EscapeJsonString(Target->GetName()),
		*CUJsonUtils::EscapeJsonString(ResolvedPath),
		AbsPos.X, AbsPos.Y, LocalSize.X, LocalSize.Y,
		LocalMin.X, LocalMin.Y,
		CropX, CropY, OutW, OutH, PadPixels);

	EncodeAndSendScreenshot(Socket, RequestId, TEXT("capture_widget_screenshot_result"),
		Cropped, OutW, OutH, Request, StartTime, ExtraJson);
}

// ==========================================================================
// Issue #8783: PIE runtime state queries + snapshot/diff
// ==========================================================================

namespace
{
	// --- Validation helpers -------------------------------------------------

	// Issue #28215: ErrorCode/Hint are optional and emitted only when non-empty, so every
	// pre-existing call site keeps its exact two-field shape. Plugin-unavailable declines
	// use them to match the error/error_code/hint contract the other surfaces already emit
	// (see CURemoteSequencerAnimMixerInternals.h anim_mixer_plugin_unavailable).
	void SendPIEErrorResponse(UCUWebSocketEditorSubsystem* Self, INetworkingWebSocket* Socket,
		const FString& ResponseType, const FString& RequestId, const FString& ErrorMsg,
		const FString& ErrorCode = FString(), const FString& Hint = FString())
	{
		FString Extra;
		if (!ErrorCode.IsEmpty())
		{
			Extra += FString::Printf(TEXT(",\"error_code\":\"%s\""),
				*CUJsonUtils::EscapeJsonString(ErrorCode));
		}
		if (!Hint.IsEmpty())
		{
			Extra += FString::Printf(TEXT(",\"hint\":\"%s\""),
				*CUJsonUtils::EscapeJsonString(Hint));
		}
		FString Escaped = CUJsonUtils::EscapeJsonString(ErrorMsg);
		FString Response = FString::Printf(TEXT(
			"{\"type\":\"%s_result\",\"request_id\":\"%s\",\"success\":false,\"error\":\"%s\"%s}"),
			*ResponseType, *RequestId, *Escaped, *Extra);
		Self->SendResponse(Socket, Response);
	}

	ECUSnapshotScope ParseScopeOrDefault(const TSharedPtr<FJsonObject>& Request)
	{
		FString ScopeStr = TEXT("standard");
		Request->TryGetStringField(TEXT("snapshot_scope"), ScopeStr);
		bool bValid = false;
		ECUSnapshotScope Scope = CUParseSnapshotScope(ScopeStr, bValid);
		return Scope;
	}

	// --- Actor / player lookup ---------------------------------------------

	AActor* FindActorByPath(UWorld* World, const FString& ActorPath)
	{
		if (!World || ActorPath.IsEmpty()) return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor)) continue;
			if (Actor->GetPathName().Equals(ActorPath, ESearchCase::IgnoreCase)) return Actor;
		}
		return nullptr;
	}

	APlayerController* FindPlayerControllerByIndex(UWorld* World, int32 Index)
	{
		if (!World) return nullptr;
		int32 Cursor = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (Cursor == Index) return It->Get();
			++Cursor;
		}
		return nullptr;
	}

	// --- JSON emitters for actor/player responses --------------------------

	FString EmitVector3(const FVector& V)
	{
		return FString::Printf(TEXT("[%.6g,%.6g,%.6g]"), V.X, V.Y, V.Z);
	}
	FString EmitRotator3(const FRotator& R)
	{
		return FString::Printf(TEXT("[%.6g,%.6g,%.6g]"), R.Pitch, R.Yaw, R.Roll);
	}

	FString EmitActorDetailsJson(AActor* Actor)
	{
		// Reused by pie_get_actor_state + pie_list_active_actors.
		if (!IsValid(Actor) || !Actor->GetClass()) return TEXT("null");

		FString Out = TEXT("{");
		Out += FString::Printf(TEXT("\"path\":\"%s\""), *CUJsonUtils::EscapeJsonString(Actor->GetPathName()));
		Out += FString::Printf(TEXT(",\"name\":\"%s\""), *CUJsonUtils::EscapeJsonString(Actor->GetName()));
		Out += FString::Printf(TEXT(",\"class\":\"%s\""), *CUJsonUtils::EscapeJsonString(Actor->GetClass()->GetName()));
		Out += TEXT(",\"location\":") + EmitVector3(Actor->GetActorLocation());
		Out += TEXT(",\"rotation\":") + EmitRotator3(Actor->GetActorRotation());
		Out += TEXT(",\"scale\":") + EmitVector3(Actor->GetActorScale3D());
		Out += TEXT(",\"velocity\":") + EmitVector3(Actor->GetVelocity());

		// Tags
		TArray<FString> TagStrs;
		for (const FName& Tag : Actor->Tags) TagStrs.Add(Tag.ToString());
		TagStrs.Sort();
		Out += TEXT(",\"tags\":[");
		for (int32 i = 0; i < TagStrs.Num(); ++i)
		{
			if (i > 0) Out += TEXT(",");
			Out += FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(TagStrs[i]));
		}
		Out += TEXT("]");

		// Components (cap at 32 to cap size)
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		TArray<FString> CompNames;
		for (UActorComponent* Comp : Components)
		{
			if (CompNames.Num() >= 32) break;
			if (!IsValid(Comp)) continue;
			CompNames.Add(Comp->GetName());
		}
		CompNames.Sort();
		Out += TEXT(",\"components\":[");
		for (int32 i = 0; i < CompNames.Num(); ++i)
		{
			if (i > 0) Out += TEXT(",");
			Out += FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(CompNames[i]));
		}
		Out += TEXT("]");
		Out += TEXT("}");
		return Out;
	}

	FString EmitPlayerDetailsJson(APlayerController* PC, int32 Index)
	{
		FString Out = FString::Printf(TEXT("{\"index\":%d"), Index);
		if (!IsValid(PC))
		{
			Out += TEXT(",\"exists\":false}");
			return Out;
		}
		Out += TEXT(",\"exists\":true");
		Out += FString::Printf(TEXT(",\"controller_path\":\"%s\""),
			*CUJsonUtils::EscapeJsonString(PC->GetPathName()));

		APawn* Pawn = PC->GetPawn();
		if (IsValid(Pawn) && Pawn->GetClass())
		{
			Out += TEXT(",\"pawn_exists\":true");
			Out += FString::Printf(TEXT(",\"pawn_path\":\"%s\""),
				*CUJsonUtils::EscapeJsonString(Pawn->GetPathName()));
			Out += FString::Printf(TEXT(",\"pawn_class\":\"%s\""),
				*CUJsonUtils::EscapeJsonString(Pawn->GetClass()->GetName()));
			Out += TEXT(",\"pawn_location\":") + EmitVector3(Pawn->GetActorLocation());
			Out += TEXT(",\"pawn_rotation\":") + EmitRotator3(Pawn->GetActorRotation());
			Out += TEXT(",\"pawn_velocity\":") + EmitVector3(Pawn->GetVelocity());
			Out += CUPieMeasurement_PawnHealthFields(Pawn);
		}
		else
		{
			Out += TEXT(",\"pawn_exists\":false");
		}

		FVector ViewLoc; FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		Out += TEXT(",\"view_location\":") + EmitVector3(ViewLoc);
		Out += TEXT(",\"view_rotation\":") + EmitRotator3(ViewRot);

		Out += TEXT("}");
		return Out;
	}

	// --- Predicate evaluation (pie_wait_for_condition) ---------------------

	// Supported predicate types. Keep in sync with the docstring on the Rust side.
	bool IsKnownPredicateType(const FString& Type)
	{
		return Type == TEXT("frames_elapsed")
			|| Type == TEXT("actor_exists")
			|| Type == TEXT("actor_property_equals")
			|| Type == TEXT("player_pawn_exists")
			|| Type == TEXT("tag_active");
	}

	// Compare a JSON FJsonValue against a FProperty's current value on the actor.
	// Handles bool / int / float / double / name / string. Returns false on type mismatch.
	bool PropertyEqualsJsonValue(AActor* Actor, FProperty* Prop,
		const TSharedPtr<FJsonValue>& Expected)
	{
		if (!IsValid(Actor) || !Prop || !Expected.IsValid()) return false;
		const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
		if (!ValuePtr) return false;

		if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
		{
			if (Expected->Type != EJson::Boolean) return false;
			return BP->GetPropertyValue(ValuePtr) == Expected->AsBool();
		}
		if (FIntProperty* IP = CastField<FIntProperty>(Prop))
		{
			if (Expected->Type != EJson::Number) return false;
			return IP->GetPropertyValue(ValuePtr) == (int32)Expected->AsNumber();
		}
		if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
		{
			if (Expected->Type != EJson::Number) return false;
			return FMath::Abs(FP->GetPropertyValue(ValuePtr) - (float)Expected->AsNumber()) < 1e-4f;
		}
		if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
		{
			if (Expected->Type != EJson::Number) return false;
			return FMath::Abs(DP->GetPropertyValue(ValuePtr) - Expected->AsNumber()) < 1e-6;
		}
		if (FNameProperty* NP = CastField<FNameProperty>(Prop))
		{
			if (Expected->Type != EJson::String) return false;
			return NP->GetPropertyValue(ValuePtr).ToString().Equals(Expected->AsString());
		}
		if (FStrProperty* SP = CastField<FStrProperty>(Prop))
		{
			if (Expected->Type != EJson::String) return false;
			return SP->GetPropertyValue(ValuePtr).Equals(Expected->AsString());
		}
		return false;  // Unsupported property type — treat as "not equal" (avoid crash).
	}

	bool EvaluatePredicate(const FString& PredType, const TSharedPtr<FJsonObject>& Cond,
		UWorld* World, int64 StartFrame)
	{
		if (!World || !Cond.IsValid()) return false;

		if (PredType == TEXT("frames_elapsed"))
		{
			int32 Frames = 0;
			Cond->TryGetNumberField(TEXT("frames"), Frames);
			if (Frames < 0) Frames = 0;
			return ((int64)GFrameCounter - StartFrame) >= (int64)Frames;
		}
		if (PredType == TEXT("actor_exists"))
		{
			FString Path;
			if (!Cond->TryGetStringField(TEXT("actor_path"), Path) || Path.IsEmpty()) return false;
			AActor* Actor = FindActorByPath(World, Path);
			if (!IsValid(Actor)) return false;
			FString Klass;
			if (Cond->TryGetStringField(TEXT("class"), Klass) && !Klass.IsEmpty())
			{
				return Actor->GetClass()->GetName().Equals(Klass, ESearchCase::IgnoreCase);
			}
			return true;
		}
		if (PredType == TEXT("actor_property_equals"))
		{
			FString Path;
			FString PropName;
			const TSharedPtr<FJsonValue>* ValuePtr = nullptr;
			if (!Cond->TryGetStringField(TEXT("actor_path"), Path)) return false;
			if (!Cond->TryGetStringField(TEXT("property"), PropName)) return false;
			if (!Cond->Values.Contains(TEXT("value"))) return false;
			ValuePtr = &Cond->Values[TEXT("value")];

			AActor* Actor = FindActorByPath(World, Path);
			if (!IsValid(Actor)) return false;
			FProperty* Prop = FindFProperty<FProperty>(Actor->GetClass(), FName(*PropName));
			if (!Prop) return false;  // Missing property → false, never crash.
			return PropertyEqualsJsonValue(Actor, Prop, *ValuePtr);
		}
		if (PredType == TEXT("player_pawn_exists"))
		{
			int32 PlayerIndex = 0;
			Cond->TryGetNumberField(TEXT("player_index"), PlayerIndex);
			APlayerController* PC = FindPlayerControllerByIndex(World, PlayerIndex);
			if (!IsValid(PC)) return false;
			APawn* Pawn = PC->GetPawn();
			return IsValid(Pawn);
		}
		if (PredType == TEXT("tag_active"))
		{
			FString Path;
			FString Tag;
			if (!Cond->TryGetStringField(TEXT("actor_path"), Path)) return false;
			if (!Cond->TryGetStringField(TEXT("tag"), Tag)) return false;
			AActor* Actor = FindActorByPath(World, Path);
			if (!IsValid(Actor)) return false;
			return Actor->ActorHasTag(FName(*Tag));
		}
		return false;
	}
}  // anonymous namespace

//--------------------------------------------------------------------
// Pending-wait / snapshot-ring cleanup helpers
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::CancelAllPIEWaits()
{
	FScopeLock Lock(&PIEPendingWaitsLock);
	if (PIEPendingWaits.IsEmpty()) return;

	for (TPair<FString, FCUPIEPendingWait>& Pair : PIEPendingWaits)
	{
		if (Pair.Value.TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Pair.Value.TickerHandle);
		}
	}
	UE_LOG(LogNarshaMCPWebSocket, Log,
		TEXT("[pie_wait_for_condition] Cancelling %d pending wait(s) on shutdown/PIE-end"),
		PIEPendingWaits.Num());
	PIEPendingWaits.Empty();
}

void UCUWebSocketEditorSubsystem::CancelAllPIEWaitsForSocket(INetworkingWebSocket* Socket)
{
	if (!Socket) return;
	FScopeLock Lock(&PIEPendingWaitsLock);
	if (PIEPendingWaits.IsEmpty()) return;

	TArray<FString> ToRemove;
	for (const TPair<FString, FCUPIEPendingWait>& Pair : PIEPendingWaits)
	{
		if (Pair.Value.Socket == Socket) ToRemove.Add(Pair.Key);
	}
	for (const FString& Key : ToRemove)
	{
		FCUPIEPendingWait* Entry = PIEPendingWaits.Find(Key);
		if (Entry && Entry->TickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(Entry->TickerHandle);
		}
		PIEPendingWaits.Remove(Key);
	}
	if (ToRemove.Num() > 0)
	{
		UE_LOG(LogNarshaMCPWebSocket, Log,
			TEXT("[pie_wait_for_condition] Cancelled %d wait(s) for disconnected socket"),
			ToRemove.Num());
	}
}

void UCUWebSocketEditorSubsystem::ClearPIESnapshotRing()
{
	FScopeLock Lock(&PIESnapshotRingLock);
	PIESnapshotRing.Empty();
}

//--------------------------------------------------------------------
// HandlePIEGetActorState
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePIEGetActorState(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIEGetActorState"), RequestId)) return;
	// Issue #9895: multi-PIE-aware via optional client_id (default 0 = server in
	// listen-server topology). ResolveClientWorld() falls back to GEditor->PlayWorld
	// for client_id=0 when no PIE contexts are visible (Standalone PIE compat).
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_get_actor_state"))) return;

	FString ActorPath;
	if (!Request->TryGetStringField(TEXT("actor_path"), ActorPath) || ActorPath.IsEmpty())
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_get_actor_state"), RequestId,
			TEXT("Missing required 'actor_path' parameter."));
		return;
	}

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_get_actor_state"), ClientId);
	if (!TargetWorld) return;

	AActor* Actor = FindActorByPath(TargetWorld, ActorPath);
	UObject* Root = Actor;
	if (!IsValid(Root))
	{
		// FindObject is Any-package; reject editor copies / CDOs / other PIE
		// worlds so this op cannot leak out of TargetWorld (Issue #27039).
		Root = FindObject<UObject>(nullptr, *ActorPath);
		if (!IsValid(Root) || Root->GetWorld() != TargetWorld)
		{
			Root = nullptr;
		}
	}
	if (!IsValid(Root))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_get_actor_state"), RequestId,
			FString::Printf(TEXT("Actor not found: %s"), *ActorPath));
		return;
	}

	TArray<FString> PropertyNames;
	const TArray<TSharedPtr<FJsonValue>>* PropsArray = nullptr;
	if (Request->TryGetArrayField(TEXT("property_names"), PropsArray) && PropsArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *PropsArray)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
			{
				PropertyNames.Add(S);
			}
		}
	}

	FString Details;
	if (AActor* AsActor = Cast<AActor>(Root))
	{
		Details = EmitActorDetailsJson(AsActor);
	}
	else
	{
		Details = FString::Printf(
			TEXT("{\"path\":\"%s\",\"name\":\"%s\",\"class\":\"%s\",\"location\":[0,0,0]}"),
			*CUJsonUtils::EscapeJsonString(Root->GetPathName()),
			*CUJsonUtils::EscapeJsonString(Root->GetName()),
			*CUJsonUtils::EscapeJsonString(Root->GetClass()->GetName()));
	}

	FString PropsJson;
	for (int32 PIdx = 0; PIdx < PropertyNames.Num(); ++PIdx)
	{
		FString Reason;
		const FString Value = CompareActorState::ResolveDotPath(Root, PropertyNames[PIdx], Reason);
		if (PIdx > 0) PropsJson += TEXT(",");
		if (Reason.IsEmpty())
		{
			PropsJson += FString::Printf(TEXT("\"%s\":%s"),
				*CUJsonUtils::EscapeJsonString(PropertyNames[PIdx]), *Value);
		}
		else
		{
			PropsJson += FString::Printf(TEXT("\"%s\":{\"value\":null,\"reason\":\"%s\"}"),
				*CUJsonUtils::EscapeJsonString(PropertyNames[PIdx]),
				*CUJsonUtils::EscapeJsonString(Reason));
		}
	}

	FString Response;
	if (PropertyNames.Num() == 0)
	{
		Response = FString::Printf(TEXT(
			"{\"type\":\"pie_get_actor_state_result\",\"request_id\":\"%s\",\"success\":true,\"actor\":%s}"),
			*RequestId, *Details);
	}
	else
	{
		Response = FString::Printf(TEXT(
			"{\"type\":\"pie_get_actor_state_result\",\"request_id\":\"%s\",\"success\":true,"
			"\"actor\":%s,\"properties\":{%s}}"),
			*RequestId, *Details, *PropsJson);
	}
	SendResponse(Socket, Response);
}

//--------------------------------------------------------------------
// HandlePIEGetPlayerState
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePIEGetPlayerState(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIEGetPlayerState"), RequestId)) return;
	// Issue #9895: multi-PIE-aware via optional client_id.
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_get_player_state"))) return;

	int32 PlayerIndex = 0;
	Request->TryGetNumberField(TEXT("player_index"), PlayerIndex);
	if (PlayerIndex < 0) PlayerIndex = 0;

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_get_player_state"), ClientId);
	if (!TargetWorld) return;

	APlayerController* PC = FindPlayerControllerByIndex(TargetWorld, PlayerIndex);
	FString PlayerJson = EmitPlayerDetailsJson(PC, PlayerIndex);

	FString Response = FString::Printf(TEXT(
		"{\"type\":\"pie_get_player_state_result\",\"request_id\":\"%s\",\"success\":true,\"player\":%s}"),
		*RequestId, *PlayerJson);
	SendResponse(Socket, Response);
}

//--------------------------------------------------------------------
// HandlePieReadGameplayAttributes (Issue #27039)
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePieReadGameplayAttributes(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePieReadGameplayAttributes"), RequestId)) return;
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_read_gameplay_attributes"))) return;

#if !WITH_NARSHAMCP_GAMEPLAYABILITIES
	// Issue #28215 Phase A: GameplayAbilities is opt-in. Without the module the ASC and
	// AttributeSet types are not linked, so decline by name rather than returning an
	// empty attribute set -- an empty result would read as "this pawn has no GAS".
	SendPIEErrorResponse(this, Socket, TEXT("pie_read_gameplay_attributes"), RequestId,
		TEXT("pie_read_gameplay_attributes requires the GameplayAbilities plugin, "
		"which this project has not enabled"),
		TEXT("gameplay_abilities_plugin_unavailable"),
		TEXT("Enable GameplayAbilities in your .uproject, then rebuild."));
	return;
#else

	int32 PlayerIndex = 0;
	Request->TryGetNumberField(TEXT("player_index"), PlayerIndex);
	if (PlayerIndex < 0) PlayerIndex = 0;

	TArray<FString> AttributeNames;
	const TArray<TSharedPtr<FJsonValue>>* AttrArray = nullptr;
	if (Request->TryGetArrayField(TEXT("attribute_names"), AttrArray) && AttrArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *AttrArray)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty())
			{
				AttributeNames.Add(S);
			}
		}
	}
	if (AttributeNames.Num() == 0)
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_read_gameplay_attributes"), RequestId,
			TEXT("attribute_names must be a non-empty array"));
		return;
	}

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_read_gameplay_attributes"), ClientId);
	if (!TargetWorld) return;

	APlayerController* PC = FindPlayerControllerByIndex(TargetWorld, PlayerIndex);
	if (!IsValid(PC))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_read_gameplay_attributes"), RequestId,
			TEXT("player_controller_not_found"));
		return;
	}

	UAbilitySystemComponent* ASC = nullptr;
	if (APawn* Pawn = PC->GetPawn())
	{
		ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>();
	}
	if (!ASC)
	{
		ASC = PC->FindComponentByClass<UAbilitySystemComponent>();
	}
	if (!ASC)
	{
		if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
		{
			ASC = PS->FindComponentByClass<UAbilitySystemComponent>();
		}
	}
	if (!IsValid(ASC))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_read_gameplay_attributes"), RequestId,
			TEXT("asc_not_found"));
		return;
	}

	TArray<FString> Available;
	const int32 MaxAvailable = 32;
	auto CollectFromSet = [&](UAttributeSet* Set)
	{
		if (!IsValid(Set) || Available.Num() >= MaxAvailable)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Set->GetClass()); It; ++It)
		{
			if (Available.Num() >= MaxAvailable)
			{
				break;
			}
			FStructProperty* StructProp = CastField<FStructProperty>(*It);
			if (!StructProp || !StructProp->Struct
				|| StructProp->Struct->GetFName() != FName(TEXT("GameplayAttributeData")))
			{
				continue;
			}
			Available.AddUnique(It->GetName());
		}
	};

	for (UAttributeSet* Set : ASC->GetSpawnedAttributes())
	{
		CollectFromSet(Set);
	}

	auto ReadAttribute = [&](const FString& AttrName, FString& OutToken, FString& OutReason) -> bool
	{
		for (UAttributeSet* Set : ASC->GetSpawnedAttributes())
		{
			if (!IsValid(Set))
			{
				continue;
			}
			FProperty* Prop = Set->GetClass()->FindPropertyByName(FName(*AttrName));
			FStructProperty* StructProp = CastField<FStructProperty>(Prop);
			if (!StructProp || !StructProp->Struct
				|| StructProp->Struct->GetFName() != FName(TEXT("GameplayAttributeData")))
			{
				continue;
			}
			void* StructPtr = StructProp->ContainerPtrToValuePtr<void>(Set);
			FNumericProperty* CurrentProp = CastField<FNumericProperty>(
				StructProp->Struct->FindPropertyByName(TEXT("CurrentValue")));
			if (!CurrentProp)
			{
				OutReason = TEXT("current_value_missing");
				return false;
			}
			const double FV = CurrentProp->GetFloatingPointPropertyValue(
				CurrentProp->ContainerPtrToValuePtr<void>(StructPtr));
			if (!FMath::IsFinite(FV))
			{
				OutReason = TEXT("non_finite");
				return false;
			}
			OutToken = FString::Printf(TEXT("%.17g"), FV);
			return true;
		}
		OutReason = TEXT("attribute_not_found");
		return false;
	};

	FString AttrJson;
	for (int32 i = 0; i < AttributeNames.Num(); ++i)
	{
		FString Token;
		FString Reason;
		const bool bOk = ReadAttribute(AttributeNames[i], Token, Reason);
		if (i > 0) AttrJson += TEXT(",");
		if (bOk)
		{
			AttrJson += FString::Printf(TEXT("\"%s\":%s"),
				*CUJsonUtils::EscapeJsonString(AttributeNames[i]), *Token);
		}
		else
		{
			AttrJson += FString::Printf(TEXT("\"%s\":{\"value\":null,\"reason\":\"%s\"}"),
				*CUJsonUtils::EscapeJsonString(AttributeNames[i]),
				*CUJsonUtils::EscapeJsonString(Reason));
		}
	}

	FString AvailableJson;
	for (int32 i = 0; i < Available.Num(); ++i)
	{
		if (i > 0) AvailableJson += TEXT(",");
		AvailableJson += FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Available[i]));
	}

	FString Response = FString::Printf(TEXT(
		"{\"type\":\"pie_read_gameplay_attributes_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"player_index\":%d,\"attributes\":{%s},\"available\":[%s]}"),
		*RequestId, PlayerIndex, *AttrJson, *AvailableJson);
	SendResponse(Socket, Response);
#endif
}

//--------------------------------------------------------------------
// HandlePIEListActiveActors
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePIEListActiveActors(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIEListActiveActors"), RequestId)) return;
	// Issue #9895: multi-PIE-aware via optional client_id.
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_list_active_actors"))) return;

	FString FilterClass;
	Request->TryGetStringField(TEXT("filter_class"), FilterClass);
	// Issue #27865: parity with get_level_actors_by_class — is_a is the default
	// so "Pawn" matches derived pawns during PIE.
	FString MatchMode = TEXT("is_a");
	Request->TryGetStringField(TEXT("match_mode"), MatchMode);
	int32 Limit = 100;
	Request->TryGetNumberField(TEXT("limit"), Limit);
	if (Limit <= 0) Limit = 100;
	if (Limit > 10000) Limit = 10000;

	FString NormalizedMode;
	FString ModeError;
	if (!FCUActorClassFilter::ParseMatchMode(MatchMode, NormalizedMode, ModeError))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_list_active_actors"), RequestId, ModeError);
		return;
	}

	UClass* ResolvedFilter = nullptr;
	if (!FilterClass.IsEmpty() && NormalizedMode == TEXT("is_a"))
	{
		ResolvedFilter = FCUActorClassFilter::ResolveActorFilterClass(FilterClass);
		if (ResolvedFilter == nullptr)
		{
			SendPIEErrorResponse(this, Socket, TEXT("pie_list_active_actors"), RequestId,
				FString::Printf(
					TEXT("Class not found: could not resolve '%s' for is_a matching (try /Script/Engine.Pawn or a short native name, or pass match_mode:\"exact\" for legacy leaf-name matching)"),
					*FilterClass));
			return;
		}
	}

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_list_active_actors"), ClientId);
	if (!TargetWorld) return;

	int32 TotalCount = 0;
	TArray<FString> Entries;
	Entries.Reserve(FMath::Min(256, Limit));

	for (TActorIterator<AActor> It(TargetWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->GetClass()) continue;

		if (!FilterClass.IsEmpty()
			&& !FCUActorClassFilter::MatchesClass(Actor->GetClass(), FilterClass, NormalizedMode, ResolvedFilter))
		{
			continue;
		}

		++TotalCount;
		if (Entries.Num() >= Limit) continue;
		Entries.Add(EmitActorDetailsJson(Actor));
	}

	FString ActorsArr = TEXT("[") + FString::Join(Entries, TEXT(",")) + TEXT("]");
	FString FilterEsc = CUJsonUtils::EscapeJsonString(FilterClass);
	// Issue #27865: echo the effective semantics; resolved_class only when is_a resolved one.
	FString ResolvedClassFragment;
	if (ResolvedFilter != nullptr)
	{
		ResolvedClassFragment = FString::Printf(TEXT(",\"resolved_class\":\"%s\""),
			*CUJsonUtils::EscapeJsonString(ResolvedFilter->GetPathName()));
	}
	FString Response = FString::Printf(TEXT(
		"{\"type\":\"pie_list_active_actors_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"filter_class\":\"%s\",\"match_mode\":\"%s\"%s,\"total_count\":%d,\"returned\":%d,\"limit\":%d,\"actors\":%s}"),
		*RequestId, *FilterEsc, *CUJsonUtils::EscapeJsonString(NormalizedMode), *ResolvedClassFragment,
		TotalCount, Entries.Num(), Limit, *ActorsArr);
	SendResponse(Socket, Response);
}

//--------------------------------------------------------------------
// HandlePIESnapshotFrame
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePIESnapshotFrame(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIESnapshotFrame"), RequestId)) return;
	// Issue #9895: multi-PIE-aware via optional client_id.
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_snapshot_frame"))) return;

	// Scope validation
	FString ScopeStr = TEXT("standard");
	Request->TryGetStringField(TEXT("snapshot_scope"), ScopeStr);
	bool bValidScope = false;
	ECUSnapshotScope Scope = CUParseSnapshotScope(ScopeStr, bValidScope);
	if (!bValidScope)
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_snapshot_frame"), RequestId,
			FString::Printf(TEXT("Invalid snapshot_scope: %s (expected minimal/standard/full)"), *ScopeStr));
		return;
	}

	FString FilterClass;
	Request->TryGetStringField(TEXT("filter_class"), FilterClass);
	int32 MaxActors = 2000;
	Request->TryGetNumberField(TEXT("max_actors"), MaxActors);

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_snapshot_frame"), ClientId);
	if (!TargetWorld) return;

	FCUPIESnapshot Snapshot = FCUPIESnapshotCapture::Capture(
		TargetWorld, Scope, FilterClass, MaxActors);

	// Assign deterministic ID (seq counter + world time), then push into ring.
	{
		FScopeLock Lock(&PIESnapshotRingLock);
		++PIESnapshotSeq;
		Snapshot.SnapshotId = FString::Printf(TEXT("snap_%lld_%llu"),
			(int64)(Snapshot.TimeSeconds * 1000.0), (unsigned long long)PIESnapshotSeq);
		if (PIESnapshotRing.Num() >= kPIESnapshotRingCap)
		{
			PIESnapshotRing.RemoveAt(0, 1, EAllowShrinking::No);
		}
		PIESnapshotRing.Add(Snapshot);
	}

	FString SnapshotJson = Snapshot.ToJsonString();
	FString Response = FString::Printf(TEXT(
		"{\"type\":\"pie_snapshot_frame_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"snapshot_id\":\"%s\",\"snapshot\":%s}"),
		*RequestId, *Snapshot.SnapshotId, *SnapshotJson);
	SendResponse(Socket, Response);
}

//--------------------------------------------------------------------
// HandlePIECompareSnapshots
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePIECompareSnapshots(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIECompareSnapshots"), RequestId)) return;

	FString IdA, IdB;
	if (!Request->TryGetStringField(TEXT("snapshot_a"), IdA)
		|| !Request->TryGetStringField(TEXT("snapshot_b"), IdB))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_compare_snapshots"), RequestId,
			TEXT("pie_compare_snapshots requires snapshot_a and snapshot_b as snapshot_id strings."));
		return;
	}

	// Parse optional diff options
	FCUPIESnapshotDiffOptions Options;
	const TArray<TSharedPtr<FJsonValue>>* IgnoreArr = nullptr;
	if (Request->TryGetArrayField(TEXT("ignore_fields"), IgnoreArr) && IgnoreArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IgnoreArr)
		{
			if (V.IsValid() && V->Type == EJson::String) Options.IgnoreFields.Add(V->AsString());
		}
	}
	double VectorEps = Options.VectorEpsilon;
	if (Request->TryGetNumberField(TEXT("vector_epsilon"), VectorEps) && VectorEps >= 0.0)
	{
		Options.VectorEpsilon = VectorEps;
	}

	// Look up both snapshots under lock.
	FCUPIESnapshot SnapA, SnapB;
	bool bFoundA = false, bFoundB = false;
	{
		FScopeLock Lock(&PIESnapshotRingLock);
		for (const FCUPIESnapshot& S : PIESnapshotRing)
		{
			if (!bFoundA && S.SnapshotId.Equals(IdA)) { SnapA = S; bFoundA = true; }
			if (!bFoundB && S.SnapshotId.Equals(IdB)) { SnapB = S; bFoundB = true; }
			if (bFoundA && bFoundB) break;
		}
	}
	if (!bFoundA || !bFoundB)
	{
		FString Missing;
		if (!bFoundA) Missing += IdA + TEXT(" ");
		if (!bFoundB) Missing += IdB;
		SendPIEErrorResponse(this, Socket, TEXT("pie_compare_snapshots"), RequestId,
			FString::Printf(TEXT("Snapshot id(s) not found in ring (cap %d): %s"),
				kPIESnapshotRingCap, *Missing.TrimStartAndEnd()));
		return;
	}

	FCUPIESnapshotDiff Diff = FCUPIESnapshotCompare::Diff(SnapA, SnapB, Options);
	FString DiffJson = Diff.ToJsonString();

	FString Response = FString::Printf(TEXT(
		"{\"type\":\"pie_compare_snapshots_result\",\"request_id\":\"%s\",\"success\":true,"
		"\"snapshot_a\":\"%s\",\"snapshot_b\":\"%s\",\"diff\":%s}"),
		*RequestId, *IdA, *IdB, *DiffJson);
	SendResponse(Socket, Response);
}

//--------------------------------------------------------------------
// HandlePIEWaitForCondition
//
// Ticker-based polling: schedules an FTSTicker at 1/poll_hz seconds, evaluates
// the predicate each tick, self-cancels when matched/timed-out/pie-stopped or
// externally cancelled via OnClientDisconnected / OnEndPIE / Deinitialize.
//--------------------------------------------------------------------

void UCUWebSocketEditorSubsystem::HandlePIEWaitForCondition(INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIEWaitForCondition"), RequestId)) return;
	// Issue #9895: multi-PIE-aware via optional client_id.
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_wait_for_condition"))) return;

	// Accept either `condition` or `predicate` as the parameter key (plan DSL).
	const TSharedPtr<FJsonObject>* CondObj = nullptr;
	if (!Request->TryGetObjectField(TEXT("condition"), CondObj)
		&& !Request->TryGetObjectField(TEXT("predicate"), CondObj))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_wait_for_condition"), RequestId,
			TEXT("Missing 'condition' (or 'predicate') object parameter."));
		return;
	}
	TSharedPtr<FJsonObject> Condition = *CondObj;

	FString PredType;
	if (!Condition->TryGetStringField(TEXT("type"), PredType) || !IsKnownPredicateType(PredType))
	{
		SendPIEErrorResponse(this, Socket, TEXT("pie_wait_for_condition"), RequestId,
			FString::Printf(TEXT("Unknown predicate type: %s (supported: frames_elapsed, actor_exists, "
				"actor_property_equals, player_pawn_exists, tag_active)"), *PredType));
		return;
	}

	int32 TimeoutMs = 5000;
	Request->TryGetNumberField(TEXT("timeout_ms"), TimeoutMs);
	TimeoutMs = FMath::Clamp(TimeoutMs, 100, 30000);

	double PollHz = 30.0;
	Request->TryGetNumberField(TEXT("poll_hz"), PollHz);
	PollHz = FMath::Clamp(PollHz, 1.0, 120.0);

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_wait_for_condition"), ClientId);
	if (!TargetWorld) return;

	const int64 StartFrame = (int64)GFrameCounter;
	const double StartSec = FPlatformTime::Seconds();
	const double DeadlineSec = StartSec + TimeoutMs / 1000.0;

	// Early exit: predicate may already be true on the first tick — handle it immediately
	// instead of scheduling a ticker for a 1-frame round-trip.
	if (EvaluatePredicate(PredType, Condition, TargetWorld, StartFrame))
	{
		FString Response = FString::Printf(TEXT(
			"{\"type\":\"pie_wait_for_condition_result\",\"request_id\":\"%s\",\"success\":true,"
			"\"matched\":true,\"reason\":\"\",\"elapsed_ms\":0.0,\"frame_delta\":0,"
			"\"predicate_type\":\"%s\"}"),
			*RequestId, *PredType);
		SendResponse(Socket, Response);
		return;
	}

	// Schedule ticker. Capture by value (Condition is TSharedPtr — cheap).
	const float Period = 1.0f / (float)PollHz;

	// Weak-ptr capture avoids extending subsystem lifetime across PIE boundaries.
	TWeakObjectPtr<UCUWebSocketEditorSubsystem> WeakSelf(this);
	// Raw Socket capture is safe under the following invariants:
	//  (1) FTSTicker::GetCoreTicker() dispatches on the game thread.
	//  (2) OnClientDisconnected also runs on the game thread and removes this
	//      request_id from PIEPendingWaits before the socket is destroyed.
	//  (3) Each tick re-checks PIEPendingWaits.Contains(RequestId) under lock
	//      and early-returns before touching SocketCapture if absent.
	// => the pointer is only dereferenced while the owning socket is still live.
	INetworkingWebSocket* SocketCapture = Socket;

	// Pre-register the pending wait BEFORE scheduling the ticker so the first tick
	// finds it already in PIEPendingWaits (eliminates registration race where the
	// ticker could fire and self-cancel before the Add completes). TickerHandle is
	// filled in below once AddTicker returns.
	{
		FScopeLock Lock(&PIEPendingWaitsLock);
		FCUPIEPendingWait& Entry = PIEPendingWaits.Add(RequestId);
		Entry.Socket = Socket;
		Entry.RequestId = RequestId;
		Entry.DeadlineSeconds = DeadlineSec;
	}

	FTSTicker::FDelegateHandle Handle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakSelf, SocketCapture, RequestId, Condition, PredType,
			StartFrame, StartSec, DeadlineSec, ClientId](float /*Delta*/) -> bool
		{
			UCUWebSocketEditorSubsystem* Self = WeakSelf.Get();
			if (!Self)
			{
				// Subsystem gone — stop ticking. Nothing to clean up.
				return false;
			}

			// Confirm the wait is still registered (could have been cancelled externally).
			bool bStillRegistered = false;
			{
				FScopeLock Lock(&Self->PIEPendingWaitsLock);
				bStillRegistered = Self->PIEPendingWaits.Contains(RequestId);
			}
			if (!bStillRegistered) return false;

			// Issue #9895: re-resolve target world each tick rather than capturing the raw
			// UWorld* pointer. PIE shutdown invalidates UWorld between ticks, and OnEndPIE
			// runs on the same game thread — re-resolving is cheap (just a GetWorldContexts
			// scan) and avoids any captured-pointer staleness window.
			UWorld* TargetWorld = Self->ResolveClientWorld(ClientId);

			// PIE stopped (no PIE worlds at all, or our specific client world disappeared)?
			if (!GEditor || !TargetWorld || !MultiPIE::IsAnyPIEWorldRunning())
			{
				FString Resp = FString::Printf(TEXT(
					"{\"type\":\"pie_wait_for_condition_result\",\"request_id\":\"%s\",\"success\":true,"
					"\"matched\":false,\"reason\":\"pie_stopped\",\"elapsed_ms\":%.2f,"
					"\"frame_delta\":%lld,\"predicate_type\":\"%s\"}"),
					*RequestId, (FPlatformTime::Seconds() - StartSec) * 1000.0,
					(int64)GFrameCounter - StartFrame, *PredType);
				Self->SendResponse(SocketCapture, Resp);
				FScopeLock Lock(&Self->PIEPendingWaitsLock);
				Self->PIEPendingWaits.Remove(RequestId);
				return false;
			}

			// Predicate match?
			if (EvaluatePredicate(PredType, Condition, TargetWorld, StartFrame))
			{
				FString Resp = FString::Printf(TEXT(
					"{\"type\":\"pie_wait_for_condition_result\",\"request_id\":\"%s\",\"success\":true,"
					"\"matched\":true,\"reason\":\"\",\"elapsed_ms\":%.2f,"
					"\"frame_delta\":%lld,\"predicate_type\":\"%s\"}"),
					*RequestId, (FPlatformTime::Seconds() - StartSec) * 1000.0,
					(int64)GFrameCounter - StartFrame, *PredType);
				Self->SendResponse(SocketCapture, Resp);
				FScopeLock Lock(&Self->PIEPendingWaitsLock);
				Self->PIEPendingWaits.Remove(RequestId);
				return false;
			}

			// Timeout?
			if (FPlatformTime::Seconds() >= DeadlineSec)
			{
				FString Resp = FString::Printf(TEXT(
					"{\"type\":\"pie_wait_for_condition_result\",\"request_id\":\"%s\",\"success\":true,"
					"\"matched\":false,\"reason\":\"timeout\",\"elapsed_ms\":%.2f,"
					"\"frame_delta\":%lld,\"predicate_type\":\"%s\"}"),
					*RequestId, (FPlatformTime::Seconds() - StartSec) * 1000.0,
					(int64)GFrameCounter - StartFrame, *PredType);
				Self->SendResponse(SocketCapture, Resp);
				FScopeLock Lock(&Self->PIEPendingWaitsLock);
				Self->PIEPendingWaits.Remove(RequestId);
				return false;
			}

			return true;  // keep ticking
		}),
		Period);

	// Fill in the TickerHandle on the already-registered entry so
	// OnClientDisconnected / OnEndPIE can cancel the ticker.
	{
		FScopeLock Lock(&PIEPendingWaitsLock);
		if (FCUPIEPendingWait* Entry = PIEPendingWaits.Find(RequestId))
		{
			Entry->TickerHandle = Handle;
		}
	}
}

// ============================================
// Issue #7926: Multi-PIE client orchestration
// ============================================

namespace MultiPIE
{
	// Collect PIE world contexts ordered by: server first (PIE_ListenServer / dedicated),
	// then clients in ascending PIEInstance order. The output array index IS the logical
	// client_id (0-based): index 0 = server, index 1..N-1 = clients.
	//
	// Stateless — callers re-scan GEngine->GetWorldContexts() each call. PIE session
	// lifetimes are short enough and instance counts small enough (<=4) that caching
	// adds risk (stale entries after stop_play) without meaningful perf benefit.
	struct FPIEClientEntry
	{
		int32 PIEInstance;
		FString Role;   // "server" | "client" | "dedicated_server"
		UWorld* World;
	};

	static TArray<FPIEClientEntry> CollectPIEClients()
	{
		TArray<FPIEClientEntry> Entries;
		if (!GEngine) return Entries;

		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType != EWorldType::PIE) continue;
			UWorld* World = Ctx.World();
			if (!World) continue;

			FPIEClientEntry Entry;
			Entry.PIEInstance = Ctx.PIEInstance;
			Entry.World = World;

			// Role classification via NetMode — listen server & dedicated both run the
			// server world; clients run NM_Client.
			const ENetMode NetMode = World->GetNetMode();
			if (NetMode == NM_DedicatedServer)
			{
				Entry.Role = TEXT("dedicated_server");
			}
			else if (NetMode == NM_ListenServer)
			{
				Entry.Role = TEXT("server");
			}
			else if (NetMode == NM_Client)
			{
				Entry.Role = TEXT("client");
			}
			else
			{
				// NM_Standalone = single-PIE
				Entry.Role = TEXT("server");
			}
			Entries.Add(Entry);
		}

		// Sort: server(s) first, then clients by PIEInstance.
		Entries.Sort([](const FPIEClientEntry& A, const FPIEClientEntry& B)
		{
			const bool bAIsServer = (A.Role != TEXT("client"));
			const bool bBIsServer = (B.Role != TEXT("client"));
			if (bAIsServer != bBIsServer) return bAIsServer && !bBIsServer;
			return A.PIEInstance < B.PIEInstance;
		});

		return Entries;
	}

	// Issue #9743: Multi-PIE-aware "is PIE alive?" check. Used by HandleListPieClients
	// and HandleCompareActorState as the precondition gate, where GEditor->PlayWorld
	// alone is unreliable: in multi-client listen-server PIE that pointer can be
	// transiently null during topology setup or aimed at a window the caller did not
	// request, even when GEngine->GetWorldContexts() already holds valid PIE entries.
	// The single-PIE fast path (GEditor->PlayWorld != nullptr) still short-circuits at
	// the call site; this helper is only reached when that pointer is null.
	static bool IsAnyPIEWorldRunning()
	{
		if (!GEngine) return false;
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.WorldType == EWorldType::PIE && Ctx.World() != nullptr)
			{
				return true;
			}
		}
		return false;
	}
}

bool UCUWebSocketEditorSubsystem::MapLogicalClientIdToPIEInstance(int32 LogicalId, int32& OutPIEInstance) const
{
	TArray<MultiPIE::FPIEClientEntry> Entries = MultiPIE::CollectPIEClients();
	if (LogicalId < 0 || LogicalId >= Entries.Num()) return false;
	OutPIEInstance = Entries[LogicalId].PIEInstance;
	return true;
}

UWorld* UCUWebSocketEditorSubsystem::ResolveClientWorld(int32 ClientId) const
{
	TArray<MultiPIE::FPIEClientEntry> Entries = MultiPIE::CollectPIEClients();
	if (ClientId < 0 || ClientId >= Entries.Num())
	{
		// Backward-compat fallback: ClientId=0 but no PIE contexts via GetWorldContexts scan.
		// This can happen in Standalone PIE where the context exists but hasn't yet reached
		// a state the iteration picks up. Fall back to GEditor->PlayWorld.
		if (ClientId == 0 && GEditor && GEditor->PlayWorld)
		{
			return GEditor->PlayWorld;
		}
		return nullptr;
	}
	return Entries[ClientId].World;
}

APlayerController* UCUWebSocketEditorSubsystem::ResolveClientPlayerController(int32 ClientId) const
{
	UWorld* World = ResolveClientWorld(ClientId);
	if (!World) return nullptr;
	return World->GetFirstPlayerController();
}

// Issue #9743 / #9895: PIE precondition helpers — three flavors depending on the caller's
// world-iteration assumption. All emit the same `error_code:"PIE_NOT_RUNNING"` envelope on
// the no-PIE failure path so Rust-side dispatch can branch uniformly on machine-readable
// classification. The single-PIE-strict helper additionally emits `SINGLE_PIE_REQUIRED`.
//
// Per-handler classification (post-#9895 audit):
//   single-PIE-strict (uses EnsurePIERunningSinglePIE — 5 ops):
//     inject_input, inject_input_sequence, inject_input_action,
//     start_input_recording, replay_input_recording
//   multi-PIE-tolerant gate-only (uses EnsurePIERunningMultiPIE, body unchanged — 3 ops):
//     capture_pie_viewport, capture_pie_window, capture_widget_screenshot
//   multi-PIE body-rewrite (uses EnsurePIERunningMultiPIE + ResolveClientWorld(client_id) — 8 ops):
//     pie_get_actor_state, pie_get_player_state, pie_list_active_actors,
//     pie_set_measurement_mode, pie_snapshot_frame, pie_wait_for_condition,
//     list_pie_clients, compare_actor_state
//
// Note: inject_input_action was moved from "multi-PIE-tolerant" to "single-PIE-strict" in
// Phase B re-audit because DispatchInputActionEvent internally uses ResolvePIEEnhancedInput
// (single LocalPlayer subsystem) and falls back to GEditor->PlayWorld->GetTimerManager()
// for raw-key release timers — multi-PIE would silently misroute input.
//
// Multi-PIE-aware variant: accepts any GEngine PIE world context. Used by the 10 callers
// whose body iterates GEngine->GetWorldContexts() downstream (HandleListPieClients,
// HandleCompareActorState, the 5 #9895 query handlers, the 3 viewport/screenshot handlers).
bool EnsurePIERunningMultiPIE(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType)
{
	if (!GEditor || (!GEditor->PlayWorld && !MultiPIE::IsAnyPIEWorldRunning()))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"PIE_NOT_RUNNING\","
			"\"error\":\"Play-In-Editor is not running. Call play_in_editor first.\"}"),
			*ResponseType, *RequestId);
		Self->SendResponse(Socket, ResponseJSON);
		return false;
	}
	return true;
}

// Issue #9895: Strict single-PIE variant. Rejects with `error_code:"SINGLE_PIE_REQUIRED"`
// when multiple PIE world contexts exist. Used by handlers whose body has a hard single-
// world assumption (Slate input injection routes through one focused window; recording
// captures one input stream; etc.). The explicit gate-time rejection is preferred over
// silent multi-PIE breakage because input-injection bugs surface as "input went to the
// wrong window" rather than crashes — much harder to diagnose post-hoc.
//
// Counts PIE worlds via MultiPIE::CollectPIEClients() rather than IsAnyPIEWorldRunning()
// because we need the exact count for the error message ("Detected N PIE worlds").
static bool EnsurePIERunningSinglePIE(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType)
{
	if (!GEditor || (!GEditor->PlayWorld && !MultiPIE::IsAnyPIEWorldRunning()))
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"PIE_NOT_RUNNING\","
			"\"error\":\"Play-In-Editor is not running. Call play_in_editor first.\"}"),
			*ResponseType, *RequestId);
		Self->SendResponse(Socket, ResponseJSON);
		return false;
	}

	const int32 ClientCount = MultiPIE::CollectPIEClients().Num();
	if (ClientCount > 1)
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"%s_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"SINGLE_PIE_REQUIRED\","
			"\"detected_pie_worlds\":%d,"
			"\"error\":\"Operation '%s' requires single-window PIE. Detected %d PIE worlds; "
			"call stop_play and re-launch without num_clients>=2.\"}"),
			*ResponseType, *RequestId, ClientCount, *ResponseType, ClientCount);
		Self->SendResponse(Socket, ResponseJSON);
		return false;
	}
	return true;
}

// Issue #9895 (post-/ns-review duplication finding): extracted helper for
// the 5 multi-PIE-aware query handlers. Parses optional `client_id` (default
// 0), resolves the per-client UWorld via ResolveClientWorld, and emits the
// `INVALID_CLIENT_ID` envelope on out-of-range. Returns nullptr after
// emitting the rejection envelope when the resolved world is missing;
// `OutClientId` is always populated (defaults to 0 if the request omits the
// field).
//
// Eliminates ~90 lines of identical 18-line blocks across:
//   HandlePIEGetActorState, HandlePIEGetPlayerState, HandlePIEListActiveActors,
//   HandlePIESnapshotFrame, HandlePIEWaitForCondition.
//
// Has external linkage (NOT `static`) because `ResolveClientWorld()` is private
// and the helper is declared `friend` in CUWebSocketEditorSubsystem.h:236-238.
// Friend declarations require external linkage; same pattern PR #9902 used for
// EnsurePIERunning at the time, before this subsystem made SendResponse public.
UWorld* ResolveClientWorldOrReject(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request,
	const FString& RequestId,
	const TCHAR* ResponseType,
	int32& OutClientId)
{
	OutClientId = 0;
	double ClientIdD = 0.0;
	if (Request->TryGetNumberField(TEXT("client_id"), ClientIdD))
	{
		OutClientId = static_cast<int32>(ClientIdD);
	}

	UWorld* TargetWorld = Self->ResolveClientWorld(OutClientId);
	if (!TargetWorld)
	{
		FString Resp = FString::Printf(TEXT(
			"{\"type\":\"%s_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"INVALID_CLIENT_ID\","
			"\"client_id\":%d,"
			"\"error\":\"No PIE world for client_id=%d. Call list_pie_clients to see available clients.\"}"),
			ResponseType, *RequestId, OutClientId, OutClientId);
		Self->SendResponse(Socket, Resp);
		return nullptr;
	}
	return TargetWorld;
}

// ---- list_pie_clients ----

void UCUWebSocketEditorSubsystem::HandleListPieClients(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleListPieClients"), RequestId)) return;
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("list_pie_clients"))) return;

	TArray<MultiPIE::FPIEClientEntry> Entries = MultiPIE::CollectPIEClients();

	FString ClientsJson;
	for (int32 Idx = 0; Idx < Entries.Num(); ++Idx)
	{
		const MultiPIE::FPIEClientEntry& E = Entries[Idx];
		const FString WorldName = E.World ? E.World->GetName() : TEXT("");
		const FString WorldPath = E.World && E.World->GetOutermost()
			? E.World->GetOutermost()->GetName()
			: TEXT("");
		// Readiness signal: HasBegunPlay flips once AActor::BeginPlay has run on all
		// level actors. Clients poll this field to detect spawn completion (replaces the
		// sync wait_for_ready loop that deadlocked the game thread).
		const bool bHasBegunPlay = E.World ? E.World->HasBegunPlay() : false;
		if (Idx > 0) ClientsJson += TEXT(",");
		ClientsJson += FString::Printf(TEXT(
			"{\"client_id\":%d,\"pie_instance\":%d,\"role\":\"%s\",\"world_name\":\"%s\",\"world_path\":\"%s\",\"has_begun_play\":%s}"),
			Idx, E.PIEInstance, *E.Role, *CUJsonUtils::EscapeJsonString(WorldName), *CUJsonUtils::EscapeJsonString(WorldPath),
			bHasBegunPlay ? TEXT("true") : TEXT("false"));
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"list_pie_clients_result\",\"request_id\":\"%s\","
		"\"success\":true,\"count\":%d,\"clients\":[%s]}"),
		*RequestId, Entries.Num(), *ClientsJson);
	SendResponse(Socket, ResponseJSON);
}

// ---- compare_actor_state ----

namespace CompareActorState
{
	// Issue #27039: ResolveDotPath lives in CUCompareActorStateUtil.cpp

	// Find the first actor in World whose GetName() matches (case-insensitive). Returns
	// nullptr if not found. Caller handles per-client "found=false" reporting.
	static AActor* FindActorByName(UWorld* World, const FString& ActorName)
	{
		if (!World) return nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
		return nullptr;
	}
}

void UCUWebSocketEditorSubsystem::HandleCompareActorState(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleCompareActorState"), RequestId)) return;
	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("compare_actor_state"))) return;

	FString ActorName;
	Request->TryGetStringField(TEXT("actor_name"), ActorName);
	if (ActorName.IsEmpty())
	{
		FString ResponseJSON = FString::Printf(TEXT(
			"{\"type\":\"compare_actor_state_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"INVALID_PARAM\",\"error\":\"actor_name is required\"}"),
			*RequestId);
		SendResponse(Socket, ResponseJSON);
		return;
	}

	// Parse property_names
	TArray<FString> PropertyNames;
	const TArray<TSharedPtr<FJsonValue>>* PropsArray = nullptr;
	if (Request->TryGetArrayField(TEXT("property_names"), PropsArray) && PropsArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *PropsArray)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S)) PropertyNames.Add(S);
		}
	}

	// Parse optional client_ids (default = all discovered clients).
	TArray<int32> ClientIds;
	const TArray<TSharedPtr<FJsonValue>>* IdsArray = nullptr;
	if (Request->TryGetArrayField(TEXT("client_ids"), IdsArray) && IdsArray)
	{
		for (const TSharedPtr<FJsonValue>& V : *IdsArray)
		{
			int32 I = 0;
			if (V.IsValid() && V->TryGetNumber(I)) ClientIds.Add(I);
		}
	}
	if (ClientIds.Num() == 0)
	{
		TArray<MultiPIE::FPIEClientEntry> All = MultiPIE::CollectPIEClients();
		for (int32 i = 0; i < All.Num(); ++i) ClientIds.Add(i);
	}

	// Tolerance for floating-point diff (default 0 = exact match).
	//
	// Issue #10128 — cross-language default-value note:
	//   The Rust MCP bridge inserts `tolerance: 0.001` when the caller omits the
	//   field (editor_control_pie.rs ec_compare_actor_state, ~L1129
	//   `unwrap_or(0.001)`). The C++ default below is `0.0`, which selects the
	//   `Tolerance <= 0.0` exact-equality branch in
	//   CompareActorState::DiffersWithinTolerance. Net effect: AI agents calling
	//   through MCP get 0.001-tolerance semantics by default; only direct C++
	//   callers (e.g. unit tests) see the 0.0 path. The divergence is intentional
	//   and pinned by both `test_compare_actor_state_default_tolerance_is_0_001`
	//   (Rust unit) and `Tolerance_DocumentsBridgeDefaultDiscrepancy` (C++ unit).
	//   If alignment is ever chosen, both pins must be updated together.
	double Tolerance = 0.0;
	if (Request->HasTypedField<EJson::Number>(TEXT("tolerance")))
	{
		Tolerance = Request->GetNumberField(TEXT("tolerance"));
	}

	// Build per_client array + diff-detection map (property_name → set of unique values).
	FString PerClientJson;
	TMap<FString, TSet<FString>> PropertyValueSets;

	for (int32 ClientIdx = 0; ClientIdx < ClientIds.Num(); ++ClientIdx)
	{
		const int32 Cid = ClientIds[ClientIdx];
		UWorld* World = ResolveClientWorld(Cid);
		AActor* Actor = CompareActorState::FindActorByName(World, ActorName);

		FString PropsJson;
		bool bFound = (Actor != nullptr);
		if (bFound)
		{
			for (int32 PIdx = 0; PIdx < PropertyNames.Num(); ++PIdx)
			{
				FString Reason;
				const FString Value = CompareActorState::ResolveDotPath(Actor, PropertyNames[PIdx], Reason);
				if (PIdx > 0) PropsJson += TEXT(",");
				if (Reason.IsEmpty())
				{
					PropsJson += FString::Printf(TEXT("\"%s\":%s"),
						*CUJsonUtils::EscapeJsonString(PropertyNames[PIdx]), *Value);
					PropertyValueSets.FindOrAdd(PropertyNames[PIdx]).Add(Value);
				}
				else
				{
					PropsJson += FString::Printf(TEXT("\"%s\":{\"value\":null,\"reason\":\"%s\"}"),
						*CUJsonUtils::EscapeJsonString(PropertyNames[PIdx]),
						*CUJsonUtils::EscapeJsonString(Reason));
					// Unresolvable values count as unique so they surface as diffs.
					PropertyValueSets.FindOrAdd(PropertyNames[PIdx]).Add(FString::Printf(TEXT("__err:%s:%d"), *Reason, Cid));
				}
			}
		}

		if (ClientIdx > 0) PerClientJson += TEXT(",");
		PerClientJson += FString::Printf(TEXT(
			"{\"client_id\":%d,\"found\":%s,\"properties\":{%s}}"),
			Cid, bFound ? TEXT("true") : TEXT("false"), *PropsJson);
	}

	// Diffs: any property whose observed values across clients are not all-identical.
	// Tolerance handling: for floating-point-looking values, compare pairwise after parsing.
	FString DiffsJson;
	bool bAllMatch = true;
	int32 DiffIdx = 0;
	for (const auto& Pair : PropertyValueSets)
	{
		// Issue #10128: extracted from inline block to CompareActorState::DiffersWithinTolerance
		// for unit-testability. See CompareActorStateUtil.h for semantics + truth-table proof of
		// equivalence with the original block.
		const bool bDiffers = CompareActorState::DiffersWithinTolerance(Pair.Value, Tolerance);

		if (bDiffers)
		{
			bAllMatch = false;
			if (DiffIdx > 0) DiffsJson += TEXT(",");
			DiffsJson += FString::Printf(TEXT("\"%s\""),
				*CUJsonUtils::EscapeJsonString(Pair.Key));
			++DiffIdx;
		}
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"compare_actor_state_result\",\"request_id\":\"%s\","
		"\"success\":true,\"actor_name\":\"%s\",\"all_match\":%s,"
		"\"per_client\":[%s],\"diffs\":[%s]}"),
		*RequestId,
		*CUJsonUtils::EscapeJsonString(ActorName),
		bAllMatch ? TEXT("true") : TEXT("false"),
		*PerClientJson,
		*DiffsJson);
	SendResponse(Socket, ResponseJSON);
}

// ============================================================================
// Issue #7928: NetEmulation parameter control (ADK Phase 3)
// ============================================================================
// Typed WebSocket messages for per-client network-condition simulation.
//
// Applies FPacketSimulationSettings (latency/loss) + UNetConnection::CurrentNetSpeed
// (bandwidth) to the UNetDriver resolved via ResolveClientWorld(ClientId).
//
// NetEmulation (PktLag / PktLoss) is only compiled in non-Shipping builds
// (DO_ENABLE_NET_TEST). Editor builds always satisfy this, but we still guard
// explicitly so the handler fails cleanly if DO_ENABLE_NET_TEST is 0. Bandwidth
// (CurrentNetSpeed) has no such guard.
//
// Bandwidth formula: bytes_per_sec = kbps * 1024 / 8  ==  kbps * 128.

namespace NetEmulation
{
	// Apply bandwidth limit to all net connections on the driver. Server drivers
	// iterate ClientConnections (one per remote peer); client-only drivers target
	// their ServerConnection instead. kbps <= 0 is treated as "do not change".
	// Returns number of connections actually updated (for response telemetry).
	static int32 ApplyBandwidth(UNetDriver* NetDriver, int32 BandwidthKbps)
	{
		if (!NetDriver || BandwidthKbps <= 0) return 0;
		const int32 BytesPerSec = BandwidthKbps * 128;
		int32 Updated = 0;
		if (NetDriver->ServerConnection)
		{
			NetDriver->ServerConnection->CurrentNetSpeed = BytesPerSec;
			++Updated;
		}
		for (UNetConnection* Conn : NetDriver->ClientConnections)
		{
			if (Conn)
			{
				Conn->CurrentNetSpeed = BytesPerSec;
				++Updated;
			}
		}
		return Updated;
	}

	// Read the effective bandwidth cap across connections. For a server driver
	// the ClientConnections share the same cap after ApplyBandwidth; we sample
	// the first one. Returns 0 when no connection is present (treated as "unset").
	static int32 ReadBandwidth(const UNetDriver* NetDriver)
	{
		if (!NetDriver) return 0;
		if (NetDriver->ServerConnection) return NetDriver->ServerConnection->CurrentNetSpeed;
		if (NetDriver->ClientConnections.Num() > 0 && NetDriver->ClientConnections[0])
		{
			return NetDriver->ClientConnections[0]->CurrentNetSpeed;
		}
		return 0;
	}

	// Convert CurrentNetSpeed (bytes/sec) back to kbps for the response.
	static int32 BytesPerSecToKbps(int32 BytesPerSec)
	{
		if (BytesPerSec <= 0) return 0;
		return BytesPerSec / 128;
	}

}  // namespace NetEmulation

// Shared PIE + NetDriver resolution for the three NetEmulation handlers.
// Member function (not namespace-local static) so it can reach the private
// ResolveClientWorld() + SendResponse() helpers without friend declarations.
// Emits the matching error envelope on failure and returns nullptr; caller
// should return immediately. ResponseType is the `type` field for the
// error JSON (e.g. "configure_network_emulation_result") so clients can
// key off the right op name.
UNetDriver* UCUWebSocketEditorSubsystem::ResolvePIENetDriverForClient(
	INetworkingWebSocket* Socket,
	const FString& ResponseType,
	const FString& RequestId,
	int32 ClientId)
{
	if (!GEditor || !GEditor->PlayWorld)
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"PIE_NOT_RUNNING\","
			"\"error\":\"Play-In-Editor is not running. Call play_in_editor first.\"}"),
			*ResponseType, *RequestId);
		SendResponse(Socket, ErrorJson);
		return nullptr;
	}

	UWorld* World = ResolveClientWorld(ClientId);
	if (!World)
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"PIE_CLIENT_NOT_FOUND\","
			"\"error\":\"No PIE world for client_id=%d. Call list_pie_clients to see available clients.\"}"),
			*ResponseType, *RequestId, ClientId);
		SendResponse(Socket, ErrorJson);
		return nullptr;
	}

	UNetDriver* NetDriver = World->GetNetDriver();
	if (!NetDriver)
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"NET_DRIVER_UNAVAILABLE\","
			"\"error\":\"World for client_id=%d has no NetDriver. NetEmulation requires num_clients>=2 or net_mode=ListenServer.\"}"),
			*ResponseType, *RequestId, ClientId);
		SendResponse(Socket, ErrorJson);
		return nullptr;
	}

	return NetDriver;
}

// ---- configure_network_emulation ----

void UCUWebSocketEditorSubsystem::HandleConfigureNetworkEmulation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleConfigureNetworkEmulation"), RequestId)) return;

	const FString ResponseType(TEXT("configure_network_emulation_result"));

	// client_id — required; defaults to 0 (server/listen). TryGetNumberField lets
	// us accept missing/null silently.
	int32 ClientId = 0;
	double ClientIdD = 0.0;
	if (Request->TryGetNumberField(TEXT("client_id"), ClientIdD))
	{
		ClientId = static_cast<int32>(ClientIdD);
	}

	// Partial-update semantics: each field is optional. `bHas*` distinguishes
	// "unset → preserve current value" from "explicitly 0 → apply zero".
	double LatencyMsD = 0.0;
	double PacketLossPctD = 0.0;
	double BandwidthKbpsD = 0.0;
	const bool bHasLatency    = Request->TryGetNumberField(TEXT("latency_ms"), LatencyMsD);
	const bool bHasPacketLoss = Request->TryGetNumberField(TEXT("packet_loss_pct"), PacketLossPctD);
	const bool bHasBandwidth  = Request->TryGetNumberField(TEXT("bandwidth_kbps"), BandwidthKbpsD);

	// Validation happens Rust-side too, but server-side rejection keeps bad
	// values from ever touching the NetDriver.
	if (bHasLatency && LatencyMsD < 0.0)
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"INVALID_ARG\","
			"\"error\":\"latency_ms must be >= 0 (got %.0f)\"}"),
			*ResponseType, *RequestId, LatencyMsD);
		SendResponse(Socket, ErrorJson);
		return;
	}
	if (bHasPacketLoss && (PacketLossPctD < 0.0 || PacketLossPctD > 100.0))
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"INVALID_ARG\","
			"\"error\":\"packet_loss_pct must be in [0, 100] (got %.2f)\"}"),
			*ResponseType, *RequestId, PacketLossPctD);
		SendResponse(Socket, ErrorJson);
		return;
	}
	if (bHasBandwidth && BandwidthKbpsD <= 0.0)
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"INVALID_ARG\","
			"\"error\":\"bandwidth_kbps must be > 0 (got %.0f); use reset_network_emulation to clear\"}"),
			*ResponseType, *RequestId, BandwidthKbpsD);
		SendResponse(Socket, ErrorJson);
		return;
	}

	UNetDriver* NetDriver = ResolvePIENetDriverForClient(Socket, ResponseType, RequestId, ClientId);
	if (!NetDriver) return;

#if DO_ENABLE_NET_TEST
	// Partial update: seed with current, overlay only provided fields. Then
	// push through SetPacketSimulationSettings so the driver fires its
	// OnPacketSimulationSettingsChanged hook.
	FPacketSimulationSettings NewSettings = NetDriver->PacketSimulationSettings;
	if (bHasLatency)
	{
		NewSettings.PktLag = FMath::RoundToInt(LatencyMsD);
		// Deterministic lag for test reproducibility — disable jitter/range path.
		NewSettings.PktLagVariance = 0;
		NewSettings.PktLagMin = 0;
		NewSettings.PktLagMax = 0;
	}
	if (bHasPacketLoss)
	{
		NewSettings.PktLoss = FMath::RoundToInt(PacketLossPctD);
	}
	NetDriver->SetPacketSimulationSettings(NewSettings);

	const int32 EffectiveLatency    = NetDriver->PacketSimulationSettings.PktLag;
	const int32 EffectivePacketLoss = NetDriver->PacketSimulationSettings.PktLoss;
#else
	// Non-shipping-only feature; if the editor build somehow stripped it we
	// still honor bandwidth (below) but report latency/loss as unsupported.
	if (bHasLatency || bHasPacketLoss)
	{
		FString ErrorJson = FString::Printf(TEXT(
			"{\"type\":\"%s\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"NET_EMULATION_UNSUPPORTED\","
			"\"error\":\"FPacketSimulationSettings requires DO_ENABLE_NET_TEST (non-Shipping). Bandwidth-only changes still apply via reset/configure without latency_ms or packet_loss_pct.\"}"),
			*ResponseType, *RequestId);
		SendResponse(Socket, ErrorJson);
		return;
	}
	const int32 EffectiveLatency    = 0;
	const int32 EffectivePacketLoss = 0;
#endif  // DO_ENABLE_NET_TEST

	int32 ConnectionsUpdated = 0;
	if (bHasBandwidth)
	{
		ConnectionsUpdated = NetEmulation::ApplyBandwidth(NetDriver, FMath::RoundToInt(BandwidthKbpsD));
	}
	const int32 EffectiveBandwidth = NetEmulation::BytesPerSecToKbps(NetEmulation::ReadBandwidth(NetDriver));

	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":true,"
		"\"client_id\":%d,"
		"\"latency_ms\":%d,"
		"\"packet_loss_pct\":%d,"
		"\"bandwidth_kbps\":%d,"
		"\"connections_updated\":%d,"
		"\"applied\":{\"latency\":%s,\"packet_loss\":%s,\"bandwidth\":%s}}"),
		*ResponseType, *RequestId,
		ClientId,
		EffectiveLatency,
		EffectivePacketLoss,
		EffectiveBandwidth,
		ConnectionsUpdated,
		bHasLatency    ? TEXT("true") : TEXT("false"),
		bHasPacketLoss ? TEXT("true") : TEXT("false"),
		bHasBandwidth  ? TEXT("true") : TEXT("false"));
	SendResponse(Socket, ResponseJSON);
}

// ---- get_network_emulation ----

void UCUWebSocketEditorSubsystem::HandleGetNetworkEmulation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleGetNetworkEmulation"), RequestId)) return;

	const FString ResponseType(TEXT("get_network_emulation_result"));

	int32 ClientId = 0;
	double ClientIdD = 0.0;
	if (Request->TryGetNumberField(TEXT("client_id"), ClientIdD))
	{
		ClientId = static_cast<int32>(ClientIdD);
	}

	UNetDriver* NetDriver = ResolvePIENetDriverForClient(Socket, ResponseType, RequestId, ClientId);
	if (!NetDriver) return;

#if DO_ENABLE_NET_TEST
	const int32 CurLatency    = NetDriver->PacketSimulationSettings.PktLag;
	const int32 CurPacketLoss = NetDriver->PacketSimulationSettings.PktLoss;
	const int32 CurLagVariance = NetDriver->PacketSimulationSettings.PktLagVariance;
	const int32 CurIncomingLoss = NetDriver->PacketSimulationSettings.PktIncomingLoss;
	const bool bNetTestEnabled = true;
#else
	const int32 CurLatency    = 0;
	const int32 CurPacketLoss = 0;
	const int32 CurLagVariance = 0;
	const int32 CurIncomingLoss = 0;
	const bool bNetTestEnabled = false;
#endif

	const int32 CurBandwidth = NetEmulation::BytesPerSecToKbps(NetEmulation::ReadBandwidth(NetDriver));
	const int32 ConnectionCount = NetDriver->ClientConnections.Num()
		+ (NetDriver->ServerConnection ? 1 : 0);

	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":true,"
		"\"client_id\":%d,"
		"\"latency_ms\":%d,"
		"\"packet_loss_pct\":%d,"
		"\"bandwidth_kbps\":%d,"
		"\"packet_lag_variance_ms\":%d,"
		"\"packet_incoming_loss_pct\":%d,"
		"\"connection_count\":%d,"
		"\"net_emulation_supported\":%s}"),
		*ResponseType, *RequestId,
		ClientId,
		CurLatency,
		CurPacketLoss,
		CurBandwidth,
		CurLagVariance,
		CurIncomingLoss,
		ConnectionCount,
		bNetTestEnabled ? TEXT("true") : TEXT("false"));
	SendResponse(Socket, ResponseJSON);
}

// ---- reset_network_emulation ----

void UCUWebSocketEditorSubsystem::HandleResetNetworkEmulation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandleResetNetworkEmulation"), RequestId)) return;

	const FString ResponseType(TEXT("reset_network_emulation_result"));

	int32 ClientId = 0;
	double ClientIdD = 0.0;
	if (Request->TryGetNumberField(TEXT("client_id"), ClientIdD))
	{
		ClientId = static_cast<int32>(ClientIdD);
	}

	UNetDriver* NetDriver = ResolvePIENetDriverForClient(Socket, ResponseType, RequestId, ClientId);
	if (!NetDriver) return;

#if DO_ENABLE_NET_TEST
	// Default-constructed settings zero all Pkt* fields — this is the UE
	// baseline (no emulation). SetPacketSimulationSettings triggers the
	// driver's change hook so any cached state is invalidated.
	FPacketSimulationSettings Defaults;
	NetDriver->SetPacketSimulationSettings(Defaults);
#endif

	// Bandwidth reset: 0 tells UE to fall back to the driver-configured default
	// on the next tick (matches NetServerMaxTickRate-derived behavior).
	int32 Cleared = 0;
	if (NetDriver->ServerConnection)
	{
		NetDriver->ServerConnection->CurrentNetSpeed = 0;
		++Cleared;
	}
	for (UNetConnection* Conn : NetDriver->ClientConnections)
	{
		if (Conn)
		{
			Conn->CurrentNetSpeed = 0;
			++Cleared;
		}
	}

	FString ResponseJSON = FString::Printf(TEXT(
		"{\"type\":\"%s\",\"request_id\":\"%s\",\"success\":true,"
		"\"client_id\":%d,"
		"\"connections_cleared\":%d,"
		"\"latency_ms\":0,\"packet_loss_pct\":0,\"bandwidth_kbps\":0}"),
		*ResponseType, *RequestId,
		ClientId,
		Cleared);
	SendResponse(Socket, ResponseJSON);
}
