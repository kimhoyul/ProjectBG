// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Subsystem — lifecycle, cache, renderer registry
// Issue #5816: M2 — multi-node, auto-analysis, batch methods
// Issue #5817: M3 — Material editor parameter annotation
// Issue #5818: M4 — Sequencer track/keyframe annotation support
// Issue #5820: M6 — LRU cache, request debounce, lazy widgets, persistence

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "CUAnnotationData.h"
#include "CUAnnotationSubsystem.generated.h"
class ICUAnnotationRenderer;
class UBlueprint;
class UEdGraphNode;
class UMaterialInterface;
class ULevelSequence;

/**
 * Editor subsystem managing AI annotation overlays (Issue #5647).
 *
 * Lifecycle:
 *   Initialize() → register context menus via UToolMenus, create BP renderer
 *   Deinitialize() → clear cache, hide all overlays, unregister menus
 *
 * Usage:
 *   RequestBlueprintNodeAnnotation(Blueprint, Node) → async AI call
 *   OnAnnotationResult(Id, Text, Category) → cache + renderer dispatch
 *
 * M1 scope: Blueprint nodes only. M3-M4 add Material/Sequencer renderers.
 *
 * M2 additions (Issue #5816):
 *   RequestBlueprintNodeAnnotations(Blueprint, Nodes[]) → batch multi-node
 *   SetAutoAnalysisEnabled() → toggle auto-analysis on selection change
 *   PollSelectionChanges() → 0.5s ticker, 1.0s debounce
 *
 * M3 additions (Issue #5817):
 *   RequestMaterialAnnotation(Material, Target) → Material param/expression annotation
 *   FCUMaterialAnnotationRenderer registered in Initialize()
 *
 * M4 additions (Issue #5818):
 *   RequestSequencerTrackAnnotation(Sequence, TrackName) → single track annotation
 *   RequestSequencerTrackAnnotations(Sequence, TrackNames[]) → batch tracks
 *   Sequencer context menu "Ask AI About This Track"
 */
UCLASS()
class NARSHAMCP_API UCUAnnotationSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	/** Constructor/Destructor (defined in .cpp where FCUAnnotationCache is complete) */
	UCUAnnotationSubsystem();
	~UCUAnnotationSubsystem();

	//~ Begin UEditorSubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End UEditorSubsystem Interface

	/**
	 * Request AI annotation for a Blueprint node (triggers async AI analysis)
	 * @param Blueprint The blueprint asset containing the node
	 * @param Node The graph node to annotate
	 */
	void RequestBlueprintNodeAnnotation(UBlueprint* Blueprint, UEdGraphNode* Node);

	/**
	 * Request AI annotation for multiple Blueprint nodes (Issue #5816).
	 * @param Blueprint The blueprint asset containing the nodes
	 * @param Nodes Array of graph nodes to annotate
	 */
	void RequestBlueprintNodeAnnotations(UBlueprint* Blueprint, const TArray<UEdGraphNode*>& Nodes);

	/**
	 * Request AI annotation for a Sequencer track (Issue #5818).
	 * @param Sequence The LevelSequence asset
	 * @param TrackName Display name of the track to annotate
	 */
	void RequestSequencerTrackAnnotation(ULevelSequence* Sequence, const FString& TrackName);

	/**
	 * Request AI annotation for multiple Sequencer tracks (Issue #5818).
	 * @param Sequence The LevelSequence asset
	 * @param TrackNames Array of track display names to annotate
	 */
	void RequestSequencerTrackAnnotations(ULevelSequence* Sequence, const TArray<FString>& TrackNames);

	/** Navigate to the next annotation in the current cache (Issue #5822) */
	void NavigateToNextAnnotation();

	/** Navigate to the previous annotation in the current cache (Issue #5822) */
	void NavigateToPreviousAnnotation();

	/** Enable/disable auto-analysis on Blueprint node selection change (Issue #5816) */
	void SetAutoAnalysisEnabled(bool bEnabled);

	/** Check if auto-analysis is enabled */
	[[nodiscard]] bool IsAutoAnalysisEnabled() const { return bAutoAnalysisEnabled; }

	/**
	 * Request AI annotation for a Material parameter or expression (Issue #5817).
	 * @param Material The material asset
	 * @param TargetIdentifier "MatParam_{Name}" or "MatExpr_{Index}"
	 */
	void RequestMaterialAnnotation(UMaterialInterface* Material, const FString& TargetIdentifier);

	/**
	 * Called when AI responds with annotation text.
	 * Stores in cache and dispatches to appropriate renderer.
	 * @param AnnotationId Unique identifier for the annotation
	 * @param Text AI-generated annotation text
	 * @param Category Annotation category ("analysis" | "suggestion" | "warning" | "error")
	 */
	void OnAnnotationResult(const FString& AnnotationId, const FString& Text, const FString& Category);

	/**
	 * Remove a specific annotation overlay and clear it from cache.
	 * @param AnnotationId Unique identifier of the annotation to dismiss
	 */
	void DismissAnnotation(const FString& AnnotationId);

	/** Remove all active annotation overlays and clear the entire cache. */
	void DismissAll();

	/**
	 * Register a new annotation renderer for handling specific editor types.
	 * Renderers are tried in registration order until first match via CanRender().
	 * @param Renderer Implementation of ICUAnnotationRenderer interface
	 */
	void RegisterRenderer(TSharedRef<ICUAnnotationRenderer> Renderer);

	/** Get the annotation cache (for tests and internal access) */
	FCUAnnotationCache& GetCache();
	const FCUAnnotationCache& GetCache() const;

private:
	/** Annotation data cache with TTL */
	TUniquePtr<FCUAnnotationCache> Cache;

	/** Registered renderers (BP, Material, Sequencer) */
	TArray<TSharedRef<ICUAnnotationRenderer>> Renderers;

	/** Register context menus via UToolMenus */
	void RegisterContextMenus();

	/** Unregister context menus */
	void UnregisterContextMenus();

	/** Context menu callback: "Ask AI About This Node" (Blueprint) */
	void OnContextMenuAskAI();

	/** Context menu callback: "Ask AI About This Track" (Sequencer, Issue #5818) */
	void OnContextMenuAskAISequencer();

	/** Context menu callback: "Ask AI About This Material" (Material, Issue #5817) */
	void OnContextMenuAskAIMaterial();

	/** Context menu callback: "Dismiss All Annotations" */
	void OnContextMenuDismissAll();

	/** Context menu callback: "Annotation Settings" (toggle auto-analysis) */
	void OnContextMenuAnnotationSettings();

	/**
	 * Extract focused Material and target identifiers from the active editor (Issue #5817).
	 * @param OutMaterial The focused Material (nullptr if none)
	 * @param OutTargets Target identifiers (e.g. "MatParam_Color")
	 * @return true if a Material editor with selection was found
	 */
	bool GetFocusedMaterialSelection(UMaterialInterface*& OutMaterial, TArray<FString>& OutTargets);

	/**
	 * Extract focused Blueprint and selected nodes from the active editor (Issue #5816).
	 * @param OutBlueprint The focused Blueprint (nullptr if none)
	 * @param OutNodes Selected graph nodes
	 * @return true if a Blueprint editor with selections was found
	 */
	bool GetFocusedBlueprintSelection(UBlueprint*& OutBlueprint, TArray<UEdGraphNode*>& OutNodes);

	/**
	 * Extract focused Sequencer and selected track names from the active editor (Issue #5818).
	 * @param OutSequence The focused LevelSequence (nullptr if none)
	 * @param OutTrackNames Selected track display names
	 * @param OutUnavailableReason Optional caller-visible plugin-unavailable diagnostic
	 * @return true if a Sequencer editor with selections was found
	 */
	bool GetFocusedSequencerSelection(
		ULevelSequence*& OutSequence,
		TArray<FString>& OutTrackNames,
		FString* OutUnavailableReason = nullptr);

	/** Purge expired cache entries (called periodically) */
	void PurgeExpiredAnnotations();

	/** Poll for Blueprint selection changes (Issue #5816, 0.5s interval) */
	void PollSelectionChanges();

	/** Ticker handle for periodic cache cleanup */
	FTSTicker::FDelegateHandle PurgeTickerHandle;

	/** Ticker handle for selection polling (Issue #5816) */
	FTSTicker::FDelegateHandle SelectionPollTickerHandle;

	/** Previously selected node GUIDs for change detection (Issue #5816) */
	TSet<FGuid> PreviousSelectedNodeGuids;

	/** Timestamp of last selection change detection (Issue #5816) */
	double LastSelectionChangeTime = 0.0;

	/** Whether auto-analysis on selection is enabled (Issue #5816) */
	bool bAutoAnalysisEnabled = false;

	/** Per-annotation-ID request timestamps for explicit debounce (Issue #5820) */
	TMap<FString, double> RequestTimestamps;

	/** Check if an explicit request should proceed (false = debounced). Issue #5820 */
	bool ShouldAllowRequest(const FString& AnnotationId);

	// ── Keyboard Navigation (Issue #5822) ──

	/** Index of currently focused annotation for keyboard navigation */
	int32 CurrentAnnotationIndex = INDEX_NONE;

	/** Ordered list of annotation IDs for keyboard navigation */
	TArray<FString> NavigationOrder;

	/** Rebuild the navigation order from current cache state */
	void RebuildNavigationOrder();

	/** Handle annotation creation from chat panel (Issue #5821) */
	void HandleChatAnnotationCreated(const FString& AnnotationId, const FString& Text, const FString& Category, const FString& ChatMessageId);

#if WITH_EDITOR
public:
	/** MCP live testing: trigger annotation rendering (Issue #5818) */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Testing")
	void LiveTestTriggerAnnotation(const FString& AnnotationId, const FString& Text, const FString& Category);

	/** MCP live testing: get cached annotation count */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|Testing")
	int32 LiveTestGetCacheCount() const;
#endif

#if WITH_AUTOMATION_WORKER
public:
	// Test-only accessors
	int32 TestGetRendererCount() const { return Renderers.Num(); }
	int32 TestGetCacheCount() const;
	bool TestIsInitialized() const { return Cache.IsValid(); }
	bool TestIsAutoAnalysisEnabled() const { return bAutoAnalysisEnabled; }
	bool TestHasAnyAnnotations() const { return TestGetCacheCount() > 0; }
	const TSet<FGuid>& TestGetPreviousNodeGuids() const { return PreviousSelectedNodeGuids; }
	const TSet<FGuid>& TestGetPreviousSelectedNodeGuids() const { return PreviousSelectedNodeGuids; }
	bool TestGetFocusedSequencerSelection(ULevelSequence*& OutSeq, TArray<FString>& OutTracks) { return GetFocusedSequencerSelection(OutSeq, OutTracks); }
	bool TestGetFocusedBlueprintSelection(UBlueprint*& OutBP, TArray<UEdGraphNode*>& OutNodes) { return GetFocusedBlueprintSelection(OutBP, OutNodes); }
	bool TestGetFocusedMaterialSelection(UMaterialInterface*& OutMat, TArray<FString>& OutTargets) { return GetFocusedMaterialSelection(OutMat, OutTargets); }
	bool TestHasBlueprintSelection() { UBlueprint* BP = nullptr; TArray<UEdGraphNode*> Nodes; return GetFocusedBlueprintSelection(BP, Nodes); }
	bool TestHasSequencerSelection() { ULevelSequence* Seq = nullptr; TArray<FString> Tracks; return GetFocusedSequencerSelection(Seq, Tracks); }
	bool TestHasMaterialSelection() { UMaterialInterface* Mat = nullptr; TArray<FString> Targets; return GetFocusedMaterialSelection(Mat, Targets); }
	void TestOnContextMenuAskAI() { OnContextMenuAskAI(); }
	void TestOnContextMenuAskAISequencer() { OnContextMenuAskAISequencer(); }
	void TestOnContextMenuAskAIMaterial() { OnContextMenuAskAIMaterial(); }
	void TestOnContextMenuDismissAll() { OnContextMenuDismissAll(); }
	void TestOnContextMenuAnnotationSettings() { OnContextMenuAnnotationSettings(); }
	void TestUnregisterContextMenus() { UnregisterContextMenus(); }
	void TestRegisterContextMenus() { RegisterContextMenus(); }
	int32 TestGetCurrentAnnotationIndex() const { return CurrentAnnotationIndex; }
	const TArray<FString>& TestGetNavigationOrder() const { return NavigationOrder; }
	// Test-only accessor for chat bridge (Issue #5821)
	void TestHandleChatAnnotationCreated(const FString& Id, const FString& Text, const FString& Cat, const FString& MsgId) { HandleChatAnnotationCreated(Id, Text, Cat, MsgId); }
#endif
};
