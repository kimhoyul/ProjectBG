// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Subsystem implementation
// Issue #5816: M2 — multi-node, auto-analysis polling, batch methods
// Issue #5817: M3 — Material editor parameter annotation
// Issue #5818: M4 — Sequencer track/keyframe annotation support

#include "CUAnnotationSubsystem.h"
#include "CUAnnotationData.h"
#include "CUAnnotationRenderers.h"
#include "CUAnnotationSettings.h"
#include "CUAnnotationChatBridge.h"  // Issue #5821: bridge delegates

#include "Editor.h"
#include "ToolMenus.h"
#include "ToolMenuContext.h"
#include "GraphEditorModule.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInterface.h"
#include "LevelSequence.h"
#if WITH_NARSHAMCP_LEVELSEQUENCEEDITOR
#include "LevelSequenceEditorBlueprintLibrary.h"
#endif
#include "MovieSceneTrack.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "BlueprintEditor.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPAI, Log, All);

#define LOCTEXT_NAMESPACE "NarshaMCPAIAnnotation"

namespace AIAnnotationConstants
{
	// Time-related constants
	/** Cache TTL in seconds (5 minutes) - Balances memory usage vs. responsiveness for repeated queries */
	static constexpr double CacheTTLSeconds = 300.0;
	/** Cache purge interval in seconds (60 seconds) - Frequent enough to prevent memory bloat, not too aggressive to impact performance */
	static constexpr float PurgeIntervalSeconds = 60.0f;

	// Menu-related constants
	/** UToolMenus menu name for Blueprint graph context menu */
	static const FName BPGraphContextMenuName = TEXT("GraphEditor.GraphContextMenu");
	/** Section name for AI annotations in context menus */
	static const FName AIAnnotationSectionName = TEXT("AIAnnotation");

	// Auto-analysis constants (Issue #5816)
	/** Selection polling interval in seconds - Fast enough for responsive UI, not too frequent to impact Editor performance */
	static constexpr float SelectionPollIntervalSeconds = 0.5f;
	/** Debounce threshold for auto-analysis in seconds - Prevents analysis spam during rapid selection changes while remaining responsive */
	static constexpr double AutoAnalysisDebounceSeconds = 1.0;

	// Sequencer constants (Issue #5818)
	/** UToolMenus menu name for Sequencer track area context menu */
	static const FName SequencerContextMenuName = TEXT("Sequencer.TrackAreaContextMenu");
	/** Section name for AI annotations in Sequencer context menus */
	static const FName AISequencerAnnotationSectionName = TEXT("AISequencerAnnotation");

	// Performance constants (Issue #5820)
	/** Debounce threshold for explicit annotation requests in seconds */
	static constexpr double ExplicitRequestDebounceSeconds = 0.3;
	/** Persistence file name */
	static const FString AnnotationCacheFileName = TEXT("AnnotationCache.json");
	/** Max age for request timestamps before cleanup (seconds) */
	static constexpr double RequestTimestampMaxAge = 60.0;
}

UCUAnnotationSubsystem::UCUAnnotationSubsystem() = default;
UCUAnnotationSubsystem::~UCUAnnotationSubsystem() = default;

// ===================================================================
// Initialize / Deinitialize
// ===================================================================

void UCUAnnotationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Cache = MakeUnique<FCUAnnotationCache>();

	// Load persisted cache from previous session (Issue #5820)
	const FString CachePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NarshaMCP"), AIAnnotationConstants::AnnotationCacheFileName);
	if (FPaths::FileExists(CachePath))
	{
		Cache->LoadFromFile(CachePath);
	}

	// Register Sequencer renderer first (prefix-based Track::/Keyframe::, Issue #5818)
	RegisterRenderer(MakeShared<FCUSequencerAnnotationRenderer>());

	// Register Material renderer second (prefix-based MatParam_/MatExpr_, Issue #5817)
	RegisterRenderer(MakeShared<FCUMaterialAnnotationRenderer>());

	// Register BP renderer last (GUID-based, broadest match)
	RegisterRenderer(MakeShared<FCUBlueprintAnnotationRenderer>());

	// Register context menus
	RegisterContextMenus();

	// Read settings for ticker intervals (Issue #5822)
	const UCUAnnotationSettings* Settings = GetDefault<UCUAnnotationSettings>();

	// Set auto-analysis from settings (Issue #5822)
	bAutoAnalysisEnabled = Settings->bAutoAnalysisEnabled;

	// Set up periodic cache purge
	PurgeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
		{
			PurgeExpiredAnnotations();
			return true; // Keep ticking
		}),
		Settings->PurgeIntervalSeconds
	);

	// Set up selection polling for auto-analysis (Issue #5816)
	SelectionPollTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([this](float DeltaTime) -> bool
		{
			PollSelectionChanges();
			return true; // Keep ticking
		}),
		Settings->SelectionPollIntervalSeconds
	);

	// Bind chat → annotation bridge delegate (Issue #5821)
	GetOnCUChatAnnotationRequested().BindUObject(this, &UCUAnnotationSubsystem::HandleChatAnnotationCreated);

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Subsystem initialized with %d renderer(s)"), Renderers.Num());
}

void UCUAnnotationSubsystem::Deinitialize()
{
	// Remove tickers
	if (SelectionPollTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SelectionPollTickerHandle);
		SelectionPollTickerHandle.Reset();
	}
	if (PurgeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PurgeTickerHandle);
		PurgeTickerHandle.Reset();
	}

	// Hide all annotations
	DismissAll();

	// Unregister context menus
	UnregisterContextMenus();

	// Clear renderers
	Renderers.Empty();

	// Persist cache to disk before clearing (Issue #5820)
	if (Cache.IsValid() && Cache->Num() > 0)
	{
		const FString CachePath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NarshaMCP"), AIAnnotationConstants::AnnotationCacheFileName);
		Cache->SaveToFile(CachePath);
	}

	// Clear cache
	if (Cache.IsValid())
	{
		Cache->Clear();
		Cache.Reset();
	}

	// Clear debounce state (Issue #5820)
	RequestTimestamps.Empty();

	// Unbind chat bridge delegate (Issue #5821)
	GetOnCUChatAnnotationRequested().Unbind();

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Subsystem deinitialized"));

	Super::Deinitialize();
}

// ===================================================================
// Annotation Request / Result
// ===================================================================

void UCUAnnotationSubsystem::RequestBlueprintNodeAnnotation(UBlueprint* Blueprint, UEdGraphNode* Node)
{
	if (!Blueprint || !Node)
	{
		return;
	}

	const FString AssetPath = Blueprint->GetPathName();
	const FString NodeGuid = Node->NodeGuid.ToString();
	const FString AnnotationId = FCUAnnotationData::MakeId(AssetPath, NodeGuid);

	// Check if we already have a cached annotation
	const FCUAnnotationData* Cached = Cache.IsValid() ? Cache->Find(AnnotationId) : nullptr;
	if (Cached)
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("Using cached annotation for %s"), *AnnotationId);
		// Re-show from cache
		for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
		{
			if (Renderer->CanRender(*Cached))
			{
				Renderer->ShowAnnotation(*Cached);
				break;
			}
		}
		return;
	}

	// Debounce explicit requests (Issue #5820)
	if (!ShouldAllowRequest(AnnotationId))
	{
		return;
	}

	// Show "Analyzing..." placeholder while waiting for AI response
	FCUAnnotationData Placeholder;
	Placeholder.AnnotationId = AnnotationId;
	Placeholder.SourceAssetPath = AssetPath;
	Placeholder.TargetIdentifier = NodeGuid;
	Placeholder.AnnotationText = FCUAnnotationData::AnalyzingPlaceholderText;
	Placeholder.Category = FCUAnnotationData::DefaultCategory;

	if (Cache.IsValid())
	{
		Cache->Add(Placeholder);
	}

	for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
	{
		if (Renderer->CanRender(Placeholder))
		{
			Renderer->ShowAnnotation(Placeholder);
			break;
		}
	}

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Requested annotation for node %s in %s"), *NodeGuid, *AssetPath);

	// Actual AI call happens via WebSocket handler (HandleAnnotateBlueprintNode)
	// The result comes back through OnAnnotationResult()
}

void UCUAnnotationSubsystem::RequestBlueprintNodeAnnotations(UBlueprint* Blueprint, const TArray<UEdGraphNode*>& Nodes)
{
	if (!Blueprint)
	{
		return;
	}

	for (UEdGraphNode* Node : Nodes)
	{
		RequestBlueprintNodeAnnotation(Blueprint, Node);
	}
}

void UCUAnnotationSubsystem::RequestMaterialAnnotation(UMaterialInterface* Material, const FString& TargetIdentifier)
{
	if (!Material || TargetIdentifier.IsEmpty())
	{
		return;
	}

	if (!FCUAnnotationData::HasMaterialPrefix(TargetIdentifier))
	{
		UE_LOG(LogNarshaMCPAI, Warning, TEXT("RequestMaterialAnnotation: invalid target '%s' - expected 'MatParam_<ParamName>' or 'MatExpr_<Index>' format"), *TargetIdentifier);
		return;
	}

	const FString AssetPath = Material->GetPathName();
	const FString AnnotationId = FCUAnnotationData::MakeId(AssetPath, TargetIdentifier);

	// Check if we already have a cached annotation
	const FCUAnnotationData* Cached = Cache.IsValid() ? Cache->Find(AnnotationId) : nullptr;
	if (Cached)
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("Using cached Material annotation for %s"), *AnnotationId);
		// Re-show from cache
		for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
		{
			if (Renderer->CanRender(*Cached))
			{
				Renderer->ShowAnnotation(*Cached);
				break;
			}
		}
		return;
	}

	// Show "Analyzing..." placeholder while waiting for AI response
	FCUAnnotationData Placeholder;
	Placeholder.AnnotationId = AnnotationId;
	Placeholder.SourceAssetPath = AssetPath;
	Placeholder.TargetIdentifier = TargetIdentifier;
	Placeholder.AnnotationText = TEXT("Analyzing material...");
	Placeholder.Category = TEXT("analysis");

	if (Cache.IsValid())
	{
		Cache->Add(Placeholder);
	}

	for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
	{
		if (Renderer->CanRender(Placeholder))
		{
			Renderer->ShowAnnotation(Placeholder);
			break;
		}
	}

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Requested Material annotation for %s in %s"), *TargetIdentifier, *AssetPath);
}

void UCUAnnotationSubsystem::SetAutoAnalysisEnabled(bool bEnabled)
{
	bAutoAnalysisEnabled = bEnabled;
	if (!bEnabled)
	{
		PreviousSelectedNodeGuids.Empty();
		LastSelectionChangeTime = 0.0;
	}
	UE_LOG(LogNarshaMCPAI, Log, TEXT("Auto-analysis %s"), bEnabled ? TEXT("enabled") : TEXT("disabled"));
}

// ===================================================================
// Sequencer Annotation Request (Issue #5818)
// ===================================================================

void UCUAnnotationSubsystem::RequestSequencerTrackAnnotation(ULevelSequence* Sequence, const FString& TrackName)
{
	if (!Sequence || TrackName.IsEmpty())
	{
		return;
	}

	const FString AssetPath = Sequence->GetPathName();
	const FString TargetIdentifier = FCUAnnotationData::TrackTargetPrefix + TrackName;
	const FString AnnotationId = FCUAnnotationData::MakeId(AssetPath, TargetIdentifier);

	// Check cache
	if (Cache.IsValid() && Cache->Find(AnnotationId))
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("Using cached Sequencer annotation for %s"), *AnnotationId);
		const FCUAnnotationData* Cached = Cache->Find(AnnotationId);
		if (Cached)
		{
			for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
			{
				if (Renderer->CanRender(*Cached))
				{
					Renderer->ShowAnnotation(*Cached);
					break;
				}
			}
		}
		return;
	}

	// Debounce explicit requests (Issue #5820)
	if (!ShouldAllowRequest(AnnotationId))
	{
		return;
	}

	// Show placeholder while waiting for AI response
	FCUAnnotationData Placeholder;
	Placeholder.AnnotationId = AnnotationId;
	Placeholder.SourceAssetPath = AssetPath;
	Placeholder.TargetIdentifier = TargetIdentifier;
	Placeholder.AnnotationText = FString::Printf(TEXT("Analyzing track: %s..."), *TrackName);
	Placeholder.Category = FCUAnnotationData::DefaultCategory;

	if (Cache.IsValid())
	{
		Cache->Add(Placeholder);
	}

	for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
	{
		if (Renderer->CanRender(Placeholder))
		{
			Renderer->ShowAnnotation(Placeholder);
			break;
		}
	}

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Requested Sequencer annotation for track %s in %s"), *TrackName, *AssetPath);
}

void UCUAnnotationSubsystem::RequestSequencerTrackAnnotations(ULevelSequence* Sequence, const TArray<FString>& TrackNames)
{
	if (!Sequence)
	{
		return;
	}

	for (const FString& TrackName : TrackNames)
	{
		RequestSequencerTrackAnnotation(Sequence, TrackName);
	}
}

// ===================================================================
// Annotation Result / Dismiss
// ===================================================================

void UCUAnnotationSubsystem::OnAnnotationResult(const FString& AnnotationId, const FString& Text, const FString& Category)
{
	if (!Cache.IsValid())
	{
		return;
	}

	// Update or create the annotation in cache
	const FCUAnnotationData* Existing = Cache->Find(AnnotationId);
	FCUAnnotationData Data;
	if (Existing)
	{
		Data = *Existing;
	}
	else
	{
		Data.AnnotationId = AnnotationId;
		// Parse asset path and target from annotation ID using the canonical separator
		FCUAnnotationData::ParseId(AnnotationId, Data.SourceAssetPath, Data.TargetIdentifier);
	}

	Data.AnnotationText = Text;
	Data.Category = Category;
	Data.Severity = FCUAnnotationData::SeverityFromCategory(Category);
	Data.CreatedAt = FDateTime::Now();

	Cache->Add(Data);

	// Severity filter: skip rendering if below minimum threshold (Issue #5822)
	const ECUAnnotationSeverity MinSeverity = GetDefault<UCUAnnotationSettings>()->MinimumSeverity;
	if (Data.Severity < MinSeverity)
	{
		UE_LOG(LogNarshaMCPAI, Verbose, TEXT("Filtered annotation %s (severity %d < min %d)"),
			*AnnotationId, static_cast<uint8>(Data.Severity), static_cast<uint8>(MinSeverity));
		return;
	}

	// Dispatch to renderer
	for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
	{
		if (Renderer->CanRender(Data))
		{
			Renderer->ShowAnnotation(Data);
			break;
		}
	}

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Received annotation result: %s (category: %s)"), *AnnotationId, *Category);
}

void UCUAnnotationSubsystem::HandleChatAnnotationCreated(
	const FString& AnnotationId,
	const FString& Text,
	const FString& Category,
	const FString& ChatMessageId)
{
	// Reuse existing annotation result pipeline
	OnAnnotationResult(AnnotationId, Text, Category);

	// Set the originating chat message ID on the cached annotation (Issue #5821)
	if (Cache.IsValid())
	{
		const FCUAnnotationData* Cached = Cache->Peek(AnnotationId);
		if (Cached)
		{
			// Modify via re-add (cache promotes to MRU)
			FCUAnnotationData Updated = *Cached;
			Updated.OriginatingChatMessageId = ChatMessageId;
			Cache->Add(Updated);
		}
	}

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Chat → Annotation created: %s (from chat msg: %s)"),
		*AnnotationId, *ChatMessageId);
}

void UCUAnnotationSubsystem::DismissAnnotation(const FString& AnnotationId)
{
	// Remove from renderers
	for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
	{
		Renderer->HideAnnotation(AnnotationId);
	}

	// Remove from cache
	if (Cache.IsValid())
	{
		Cache->Remove(AnnotationId);
	}
}

void UCUAnnotationSubsystem::DismissAll()
{
	for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
	{
		Renderer->HideAll();
	}

	if (Cache.IsValid())
	{
		Cache->Clear();
	}

	// Reset navigation state to prevent stale index leaking across calls (Issue #5822)
	CurrentAnnotationIndex = INDEX_NONE;
	NavigationOrder.Empty();
}

void UCUAnnotationSubsystem::RegisterRenderer(TSharedRef<ICUAnnotationRenderer> Renderer)
{
	Renderers.Add(Renderer);
}

FCUAnnotationCache& UCUAnnotationSubsystem::GetCache()
{
	if (!ensure(Cache.IsValid()))
	{
		// Create fallback cache for safety in release builds
		// This ensures graceful degradation if initialization failed during subsystem startup
		Cache = MakeUnique<FCUAnnotationCache>();
	}
	return *Cache;
}

const FCUAnnotationCache& UCUAnnotationSubsystem::GetCache() const
{
	if (!ensure(Cache.IsValid()))
	{
		// Fallback: thread_local avoids shared-state races if called from multiple threads
		static thread_local FCUAnnotationCache EmptyCache;
		return EmptyCache;
	}
	return *Cache;
}

// ===================================================================
// Context Menus
// ===================================================================

void UCUAnnotationSubsystem::RegisterContextMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	UToolMenu* Menu = ToolMenus->ExtendMenu(AIAnnotationConstants::BPGraphContextMenuName);
	if (!Menu)
	{
		UE_LOG(LogNarshaMCPAI, Warning, TEXT("Could not extend menu: %s"),
			*AIAnnotationConstants::BPGraphContextMenuName.ToString());
		return;
	}

	FToolMenuSection& Section = Menu->FindOrAddSection(AIAnnotationConstants::AIAnnotationSectionName);
	Section.Label = LOCTEXT("AIAnnotationSection", "AI Analysis");

	Section.AddMenuEntry(
		"AskAIAboutNode",
		LOCTEXT("AskAIAboutNode", "Ask AI About This Node"),
		LOCTEXT("AskAIAboutNodeTooltip", "Request AI analysis of the selected Blueprint node"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"),
		FUIAction(FExecuteAction::CreateUObject(this, &UCUAnnotationSubsystem::OnContextMenuAskAI))
	);

	UE_LOG(LogNarshaMCPAI, Log, TEXT("Registered Blueprint context menu entry"));

	// Register Sequencer context menu (Issue #5818)
	UToolMenu* SeqMenu = ToolMenus->ExtendMenu(AIAnnotationConstants::SequencerContextMenuName);
	if (SeqMenu)
	{
		FToolMenuSection& SeqSection = SeqMenu->FindOrAddSection(AIAnnotationConstants::AISequencerAnnotationSectionName);
		SeqSection.Label = LOCTEXT("AISequencerAnnotationSection", "AI Analysis");

		SeqSection.AddMenuEntry(
			"AskAIAboutTrack",
			LOCTEXT("AskAIAboutTrack", "Ask AI About This Track"),
			LOCTEXT("AskAIAboutTrackTooltip", "Request AI analysis of the selected Sequencer track"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Info"),
			FUIAction(FExecuteAction::CreateUObject(this, &UCUAnnotationSubsystem::OnContextMenuAskAISequencer))
		);

		UE_LOG(LogNarshaMCPAI, Log, TEXT("Registered Sequencer context menu entry"));
	}
	else
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("Sequencer context menu not available (menu: %s)"),
			*AIAnnotationConstants::SequencerContextMenuName.ToString());
	}
}

void UCUAnnotationSubsystem::UnregisterContextMenus()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	if (!ToolMenus)
	{
		return;
	}

	// Remove BP context menu section
	UToolMenu* Menu = ToolMenus->ExtendMenu(AIAnnotationConstants::BPGraphContextMenuName);
	if (Menu)
	{
		Menu->RemoveSection(AIAnnotationConstants::AIAnnotationSectionName);
	}

	// Remove Sequencer context menu section (Issue #5818)
	UToolMenu* SeqMenu = ToolMenus->ExtendMenu(AIAnnotationConstants::SequencerContextMenuName);
	if (SeqMenu)
	{
		SeqMenu->RemoveSection(AIAnnotationConstants::AISequencerAnnotationSectionName);
	}
}

bool UCUAnnotationSubsystem::GetFocusedBlueprintSelection(UBlueprint*& OutBlueprint, TArray<UEdGraphNode*>& OutNodes)
{
	OutBlueprint = nullptr;
	OutNodes.Empty();

	if (!GEditor)
	{
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return false;
	}

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (UObject* Asset : EditedAssets)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (!Blueprint)
		{
			continue;
		}

		IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false);
		if (!EditorInstance)
		{
			continue;
		}

		// Note: dynamic_cast unavailable with /GR-, use editor name check + static_cast
		static const FName BlueprintEditorName("BlueprintEditor");
		FBlueprintEditor* BlueprintEditor = nullptr;
		if (EditorInstance->GetEditorName() == BlueprintEditorName)
		{
			FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(EditorInstance);
			if (Toolkit && Toolkit->GetToolkitName().ToString() == BlueprintEditorName.ToString())
			{
				BlueprintEditor = static_cast<FBlueprintEditor*>(Toolkit);
			}
		}
		if (!BlueprintEditor)
		{
			continue;
		}

		const FGraphPanelSelectionSet& SelectedNodes = BlueprintEditor->GetSelectedNodes();
		for (UObject* SelectedObj : SelectedNodes)
		{
			UEdGraphNode* Node = Cast<UEdGraphNode>(SelectedObj);
			if (Node)
			{
				OutNodes.Add(Node);
			}
		}

		if (OutNodes.Num() > 0)
		{
			OutBlueprint = Blueprint;
			return true;
		}
	}

	return false;
}

void UCUAnnotationSubsystem::OnContextMenuAskAI()
{
	UBlueprint* Blueprint = nullptr;
	TArray<UEdGraphNode*> SelectedNodes;

	if (GetFocusedBlueprintSelection(Blueprint, SelectedNodes))
	{
		// Annotate all selected nodes (Issue #5816: multi-node)
		RequestBlueprintNodeAnnotations(Blueprint, SelectedNodes);
	}
	else
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("No Blueprint node selected for AI analysis"));
	}
}

// ===================================================================
// Sequencer Context Menu (Issue #5818)
// ===================================================================

void UCUAnnotationSubsystem::OnContextMenuAskAISequencer()
{
	ULevelSequence* Sequence = nullptr;
	TArray<FString> SelectedTracks;
	FString UnavailableReason;

	if (GetFocusedSequencerSelection(Sequence, SelectedTracks, &UnavailableReason))
	{
		RequestSequencerTrackAnnotations(Sequence, SelectedTracks);
	}
	else if (!UnavailableReason.IsEmpty())
	{
		UE_LOG(LogNarshaMCPAI, Warning, TEXT("%s"), *UnavailableReason);
	}
	else
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("No Sequencer track selected for AI analysis"));
	}
}

// ===================================================================
// Material Context Menu (Issue #5817)
// ===================================================================

void UCUAnnotationSubsystem::OnContextMenuAskAIMaterial()
{
	UMaterialInterface* Material = nullptr;
	TArray<FString> SelectedTargets;

	if (GetFocusedMaterialSelection(Material, SelectedTargets))
	{
		for (const FString& Target : SelectedTargets)
		{
			RequestMaterialAnnotation(Material, Target);
		}
	}
	else
	{
		UE_LOG(LogNarshaMCPAI, Log, TEXT("No Material selected for AI analysis"));
	}
}

void UCUAnnotationSubsystem::OnContextMenuDismissAll()
{
	DismissAll();
}

void UCUAnnotationSubsystem::OnContextMenuAnnotationSettings()
{
	SetAutoAnalysisEnabled(!bAutoAnalysisEnabled);
}

bool UCUAnnotationSubsystem::GetFocusedMaterialSelection(UMaterialInterface*& OutMaterial, TArray<FString>& OutTargets)
{
	OutMaterial = nullptr;
	OutTargets.Empty();

	if (!GEditor)
	{
		return false;
	}

	// In automation/testing context, no Material editor is open
	// Full implementation would check UAssetEditorSubsystem for open Material editors
	return false;
}

bool UCUAnnotationSubsystem::GetFocusedSequencerSelection(
	ULevelSequence*& OutSequence,
	TArray<FString>& OutTrackNames,
	FString* OutUnavailableReason)
{
	OutSequence = nullptr;
	OutTrackNames.Empty();
	if (OutUnavailableReason)
	{
		OutUnavailableReason->Empty();
	}

	if (!GEditor)
	{
		return false;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return false;
	}

	TArray<UObject*> EditedAssets = AssetEditorSubsystem->GetAllEditedAssets();
	for (UObject* Asset : EditedAssets)
	{
		ULevelSequence* Sequence = Cast<ULevelSequence>(Asset);
		if (!Sequence)
		{
			continue;
		}

		IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Sequence, /*bFocusIfOpen=*/false);
		if (!EditorInstance)
		{
			continue;
		}

		// Found an open Sequencer editor — extract selected tracks
		OutSequence = Sequence;

		// Issue #5818: Extract selected track names via LevelSequenceEditor static API.
		// Returns empty array if no tracks selected (graceful fallback).
#if WITH_NARSHAMCP_LEVELSEQUENCEEDITOR
		TArray<UMovieSceneTrack*> SelectedTracks = ULevelSequenceEditorBlueprintLibrary::GetSelectedTracks();
		for (UMovieSceneTrack* Track : SelectedTracks)
		{
			if (Track)
			{
				const FString DisplayName = Track->GetDisplayName().ToString();
				if (!DisplayName.IsEmpty())
				{
					OutTrackNames.Add(DisplayName);
				}
			}
		}
#else
		if (OutUnavailableReason)
		{
			*OutUnavailableReason = TEXT(
				"level_sequence_editor_plugin_unavailable: Enable LevelSequenceEditor in your .uproject, then rebuild.");
		}
		return false;
#endif

		// Return true even with empty track names — caller handles this
		return true;
	}

	return false;
}

// ===================================================================
// Auto-Analysis Polling (Issue #5816)
// ===================================================================

void UCUAnnotationSubsystem::PollSelectionChanges()
{
	// Early exit if auto-analysis disabled - avoids unnecessary Blueprint editor queries
	if (!bAutoAnalysisEnabled)
	{
		return;
	}

	UBlueprint* Blueprint = nullptr;
	TArray<UEdGraphNode*> SelectedNodes;

	if (!GetFocusedBlueprintSelection(Blueprint, SelectedNodes))
	{
		// No selection — clear previous state
		if (PreviousSelectedNodeGuids.Num() > 0)
		{
			PreviousSelectedNodeGuids.Empty();
		}
		return;
	}

	// Build current GUID set
	TSet<FGuid> CurrentGuids;
	for (const UEdGraphNode* Node : SelectedNodes)
	{
		CurrentGuids.Add(Node->NodeGuid);
	}

	// Compare with previous using Set operations for better performance
	if (CurrentGuids.Num() == PreviousSelectedNodeGuids.Num())
	{
		// Use Set::Difference to efficiently compare - O(n) vs O(n²) Contains() loops
		// Returns elements in CurrentGuids that are not in PreviousSelectedNodeGuids
		bool bSame = CurrentGuids.Difference(PreviousSelectedNodeGuids).Num() == 0;
		if (bSame)
		{
			return; // No change detected - avoid redundant analysis
		}
	}
	// else: Different sizes = definitely different sets, skip expensive Difference() call

	// Selection changed — update tracking
	PreviousSelectedNodeGuids = CurrentGuids;
	const double Now = FPlatformTime::Seconds();

	// Debounce: only trigger if enough time has passed since last change
	// Prevents excessive AI API calls during rapid selection changes (e.g., arrow key navigation)
	if ((Now - LastSelectionChangeTime) < GetDefault<UCUAnnotationSettings>()->AutoAnalysisDebounceSeconds)
	{
		return;
	}

	LastSelectionChangeTime = Now;
	RequestBlueprintNodeAnnotations(Blueprint, SelectedNodes);
}

// ===================================================================
// Request Debounce (Issue #5820)
// ===================================================================

bool UCUAnnotationSubsystem::ShouldAllowRequest(const FString& AnnotationId)
{
	const double Now = FPlatformTime::Seconds();
	const double* LastRequest = RequestTimestamps.Find(AnnotationId);
	if (LastRequest && (Now - *LastRequest) < GetDefault<UCUAnnotationSettings>()->ExplicitRequestDebounceSeconds)
	{
		UE_LOG(LogNarshaMCPAI, Verbose, TEXT("Debounced request for %s (%.2fs since last)"),
			*AnnotationId, Now - *LastRequest);
		return false;
	}
	RequestTimestamps.Add(AnnotationId, Now);
	return true;
}

// ===================================================================
// Keyboard Navigation (Issue #5822)
// ===================================================================

void UCUAnnotationSubsystem::NavigateToNextAnnotation()
{
	RebuildNavigationOrder();
	if (NavigationOrder.Num() == 0)
	{
		return;
	}

	if (CurrentAnnotationIndex == INDEX_NONE)
	{
		CurrentAnnotationIndex = 0;
	}
	else
	{
		CurrentAnnotationIndex = (CurrentAnnotationIndex + 1) % NavigationOrder.Num();
	}
	const FString& AnnotationId = NavigationOrder[CurrentAnnotationIndex];

	if (Cache.IsValid())
	{
		const FCUAnnotationData* Data = Cache->Find(AnnotationId);
		if (Data)
		{
			for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
			{
				if (Renderer->CanRender(*Data))
				{
					Renderer->ShowAnnotation(*Data);
					break;
				}
			}
		}
	}
}

void UCUAnnotationSubsystem::NavigateToPreviousAnnotation()
{
	RebuildNavigationOrder();
	if (NavigationOrder.Num() == 0)
	{
		return;
	}

	if (CurrentAnnotationIndex <= 0)
	{
		CurrentAnnotationIndex = NavigationOrder.Num() - 1;
	}
	else
	{
		CurrentAnnotationIndex--;
	}

	const FString& AnnotationId = NavigationOrder[CurrentAnnotationIndex];

	if (Cache.IsValid())
	{
		const FCUAnnotationData* Data = Cache->Find(AnnotationId);
		if (Data)
		{
			for (const TSharedRef<ICUAnnotationRenderer>& Renderer : Renderers)
			{
				if (Renderer->CanRender(*Data))
				{
					Renderer->ShowAnnotation(*Data);
					break;
				}
			}
		}
	}
}

void UCUAnnotationSubsystem::RebuildNavigationOrder()
{
	if (!Cache.IsValid())
	{
		NavigationOrder.Empty();
		CurrentAnnotationIndex = INDEX_NONE;
		return;
	}

	NavigationOrder = Cache->GetAllIds();
	NavigationOrder.Sort();

	// Clamp index if cache shrank
	if (NavigationOrder.Num() == 0)
	{
		CurrentAnnotationIndex = INDEX_NONE;
	}
	else if (CurrentAnnotationIndex >= NavigationOrder.Num())
	{
		CurrentAnnotationIndex = NavigationOrder.Num() - 1;
	}
}

// ===================================================================
// Cache Maintenance
// ===================================================================

void UCUAnnotationSubsystem::PurgeExpiredAnnotations()
{
	if (Cache.IsValid())
	{
		Cache->PurgeExpired(GetDefault<UCUAnnotationSettings>()->CacheTTLSeconds);
	}

	// Clean up old request timestamps to prevent unbounded growth (Issue #5820)
	const double Now = FPlatformTime::Seconds();
	TArray<FString> OldTimestamps;
	for (const auto& Pair : RequestTimestamps)
	{
		if ((Now - Pair.Value) > AIAnnotationConstants::RequestTimestampMaxAge)
		{
			OldTimestamps.Add(Pair.Key);
		}
	}
	for (const FString& Key : OldTimestamps)
	{
		RequestTimestamps.Remove(Key);
	}
}

// ===================================================================
// MCP Live Test Methods (Issue #5818)
// ===================================================================

#if WITH_EDITOR
void UCUAnnotationSubsystem::LiveTestTriggerAnnotation(const FString& AnnotationId, const FString& Text, const FString& Category)
{
	OnAnnotationResult(AnnotationId, Text, Category);
}

int32 UCUAnnotationSubsystem::LiveTestGetCacheCount() const
{
	return Cache.IsValid() ? Cache->GetAllIds().Num() : 0;
}
#endif

// ===================================================================
// Test Accessors
// ===================================================================

#if WITH_AUTOMATION_WORKER
int32 UCUAnnotationSubsystem::TestGetCacheCount() const
{
	return Cache.IsValid() ? Cache->Num() : 0;
}
#endif

#undef LOCTEXT_NAMESPACE
