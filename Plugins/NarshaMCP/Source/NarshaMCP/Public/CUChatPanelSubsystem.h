// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5478: AI Chat Panel Subsystem — UEditorSubsystem for native Slate AI chat

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Containers/Ticker.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Docking/TabManager.h"            // ETabActivationCause (Issue #9168 multi-tab)
#include "Styling/SlateTypes.h"  // FEditableTextBoxStyle, FButtonStyle (by-value members)
#include "Widgets/Input/SMultiLineEditableTextBox.h"  // Full definition for test accessor (GetText())
#include "CUChatBackendClient.h"  // FCUChatSession/FCUChatMessage full definition (needed for by-value member)
#include "CUChatPanelSubsystem.generated.h"

class SWidgetSwitcher;
class SMultiLineEditableTextBox;
class STextBlock;
class SButton;
class UCUChatPanelSubsystem;
class FCUChatBackendClient;
class SCUChatMessageList;
class SCUChatMessageBubble;
class SCUChatHistoryPanel;
class SCUChatDetailPanel;
class SBox;
class SDockTab;
enum ETabActivationCause : uint8;  // UE 5.7: unscoped enum (see SDockTab.h)

/**
 * Global input processor for AI Chat Panel keyboard shortcut (Ctrl+Shift+P)
 */
class FCUChatInputProcessor : public IInputProcessor
{
public:
	FCUChatInputProcessor(UCUChatPanelSubsystem* InOwner) : Owner(InOwner) {}
	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override;
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override;

	/** Synthesizes Alt KeyUp when the app loses focus to prevent stuck modifier state (Alt+Tab fix) */
	void HandleAppActivationChanged(bool bIsActive);
private:
	UCUChatPanelSubsystem* Owner;
};

/**
 * Per-tab state container (Issue #9168).
 * Holds the chat session + the widgets that MUST be unique per tab
 * (message list + streaming bubble). Shared widgets (input, toolbar,
 * history panel) remain on the subsystem and follow the active tab via mirror members.
 */
struct FCUChatTabState
{
	/** Chat session — messages, SessionId, timestamps. One session per tab. */
	FCUChatSession Session;

	/** Weak ref to the SDockTab this state belongs to (for lookup). */
	TWeakPtr<SDockTab> DockTab;

	/** Per-tab message list — each tab scrolls its own conversation. */
	TSharedPtr<SCUChatMessageList> MessageList;

	/** Currently-streaming assistant bubble in this tab (null when idle). */
	TSharedPtr<SCUChatMessageBubble> CurrentAssistantBubble;
};

/**
 * AI Chat Panel Subsystem (Issue #5478, #5652)
 *
 * Native Slate AI chat panel with Claude CLI backend.
 * - Initialize(): detect CLI, register SDockTab, restore session
 * - Deinitialize(): save session, unregister SDockTab
 * - Tick: poll CLI backend for streaming response tokens
 */
UCLASS()
class NARSHAMCP_API UCUChatPanelSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

	friend class FCUChatInputProcessor;

public:
	//~ Begin UEditorSubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Issue #10756: skip subsystem creation under cook/headless commandlets where
	// FSlateApplication is uninitialized (was crashing Development packaging).
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End UEditorSubsystem Interface

	/** Handle ue-asset:// URL clicks — navigate Content Browser to asset */
	bool HandleAssetLink(const FString& URL);

	/** Handle ue-source:// URL clicks — open source file in IDE (Phase 6) */
	bool HandleSourceLink(const FString& URL);

	/** Handle actor-link clicks — select actor in viewport and focus camera (matched by unique FName) */
	bool HandleActorLink(const FString& ActorName);

	/** Handle ue-annotate:// URL clicks — creates annotation from chat context (Issue #5821) */
	bool HandleAnnotateLink(const FString& URL);

private:
	/** Ticker for polling CLI backend */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Cached claude CLI path (detected once in Initialize, reused on restart) */
	FString CachedClaudePath;

	/** Slate native input widget (Korean IME works here) */
	TSharedPtr<SMultiLineEditableTextBox> InputWidget;

	/** Send button reference (for SetEnabled after construction) */
	TSharedPtr<SButton> SendButton;

	/** Custom dark-themed styles (initialized in SpawnNativeSlateTab, must outlive widgets) */
	FEditableTextBoxStyle ChatInputStyle;
	FButtonStyle ChatSendButtonStyle;

	/** Whether a message submission is in flight (guards input disable + ESC abort) */
	bool bIsSubmitting = false;

	/** Tab ID constant */
	static const FName AIChatTabId;

	// === Multi-Tab State (Issue #9168) ===

	/** All open chat tabs. Tab 0 is the initial/primary tab. */
	TArray<TSharedRef<FCUChatTabState>> TabStates;

	/** Index into TabStates for the currently-active tab. INDEX_NONE if no tabs. */
	int32 ActiveTabIndex = INDEX_NONE;

	/** Counter for "AI Chat N" tab labels — increments per new tab, never resets. */
	int32 NextTabCounter = 1;

	/**
	 * The tab that issued the most recent message send. BackendClient delegates
	 * (OnToken/OnComplete/OnError/OnToolResult) route their results to this tab
	 * so responses stream into the correct tab even if the user switches tabs
	 * mid-stream. TWeakPtr so closing the tab cleanly drops stale tokens.
	 */
	TWeakPtr<SDockTab> StreamingTab;

	/** Get the active tab state, or nullptr if no tabs. */
	FCUChatTabState* GetActiveTabState();

	/**
	 * Resolve the TabState for StreamingTab. Returns nullptr if the streaming
	 * tab was closed. Called from BackendClient delegates.
	 */
	FCUChatTabState* ResolveStreamingTabState();

	/** Find TabState by DockTab. Returns nullptr if not tracked. */
	FCUChatTabState* FindTabStateFor(const TSharedRef<SDockTab>& Tab);

	/** Sync subsystem-level mirror members (NativeMessageList, CurrentSession, CurrentAssistantBubble) to the active tab. */
	void SyncMirrorsToActiveTab();

	/** Save session for the given tab state to disk. Idempotent; skips empty. */
	void SaveSessionForTab(FCUChatTabState& TabState);

	/**
	 * Freeze the active tab's mirrors into its FCUChatTabState before the
	 * subsystem-level members are overwritten. MUST run BEFORE
	 * ResetSessionStateForNewChat() or the old tab keeps a cleared session.
	 * No-op only when ActiveTabIndex is out of range. Headless TabStates is NOT
	 * guaranteed empty — the ambient count is environment-dependent (Issue #28657
	 * measured 0 on a UE 5.7 lane and 1 on a UE 5.8 lane, same commit).
	 */
	void SnapshotActiveTabState();

	/**
	 * The "New Chat" state transition, with no UI side effect (Issue #23127).
	 * Resets CurrentSession, clears + persists LastSessionId, and drops the
	 * widget mirrors. Runs in every process, rendering or not — the document
	 * tab is the only part gated on FApp::CanEverRender().
	 */
	void ResetSessionStateForNewChat();

	/** Build a new SDockTab + FCUChatTabState pair. Does NOT insert into TabManager; caller does. */
	TSharedRef<SDockTab> BuildNewChatTab();

	/** SDockTab::OnTabActivated callback — updates ActiveTabIndex + syncs mirrors. */
	void OnChatTabActivated(TSharedRef<SDockTab> ActivatedTab, ETabActivationCause Cause);

	/** SDockTab::OnTabClosed callback — saves session, removes from TabStates, re-indexes. */
	void OnChatTabClosed(TSharedRef<SDockTab> ClosedTab);

	/** Detect claude CLI path via 'where' command + npm fallback */
	FString DetectClaudeCliPath();

	/** Spawn the AI chat dock tab */
	TSharedRef<SDockTab> SpawnAITab(const FSpawnTabArgs& Args);

	/** Update submitting state — disables/enables input widgets via TAttribute polling */
	void SetSubmittingState(bool bSubmitting);

	// === Native Slate Chat (Issue #5652) ===

	/** Backend client for CLI-based chat */
	TSharedPtr<FCUChatBackendClient> BackendClient;

	/** Native message list widget (Slate path only) */
	TSharedPtr<SCUChatMessageList> NativeMessageList;

	/** Fixed status bar text at top of panel (CLI status, auth state) */
	TSharedPtr<STextBlock> StatusBarText;

	/** Currently streaming assistant bubble (set per SubmitNativeInput, read by delegates) */
	TSharedPtr<SCUChatMessageBubble> CurrentAssistantBubble;

	// === MCP daemon readiness (Issue #8835 live-test polish) ===
	/** True once `narshamcp.exe status` reports the daemon is running, or the
	 *  first tool_result fires successfully. Gates the "MCP 연결 중…" banner
	 *  shown above the input row so users don't submit before MCP is reachable. */
	bool bMCPDaemonReady = false;
	/** Repeating poll handle (2s). Stopped once bMCPDaemonReady flips true. */
	FTSTicker::FDelegateHandle MCPStatusTickerHandle;
	/** One-shot guard so we only fire `narshamcp connect` prewarm once per panel session. */
	bool bMCPDaemonPrewarmAttempted = false;
	/** Long-lived `narshamcp.exe connect --stdio` process kept alive for the
	 *  lifetime of the panel so the daemon stays warm across message sends.
	 *  Without this, the daemon exits shortly after each Claude CLI call ends
	 *  and every new message triggers a 20–30s cold boot race. */
	FProcHandle MCPPrewarmProcess;
	void* MCPPrewarmStdinWrite = nullptr; // kept open so the connect process doesn't exit on EOF
	/** Tear down the prewarm process — idempotent. */
	void StopMCPPrewarm();
	/** Background-thread probe that runs `narshamcp.exe status --project-path X`
	 *  and flips bMCPDaemonReady on GameThread when daemon reports Running.
	 *  Also fire-and-forgets `narshamcp daemon --no-gui` on first probe if no
	 *  daemon is running — guarantees Claude CLI sees tools on the user's first
	 *  message instead of racing the daemon's 5–30s cold boot. */
	bool TickMCPStatusProbe(float DeltaTime);

	/** Submit input via native backend (Slate path) */
	void SubmitNativeInput();

	/** Spawn native Slate chat tab */
	TSharedRef<SDockTab> SpawnNativeSlateTab(const FSpawnTabArgs& Args);

	/** Bind backend client delegates (token streaming, completion, error) */
	void BindBackendDelegates();

	/** Build shared toolbar (Compile, SaveAll, Context, ActorLink, History, NewChat) */
	TSharedRef<SWidget> BuildToolbar();

	// === Actor Link Toggle (Issue #5478) ===

	/** Whether actor name linkification is enabled (gold clickable links in chat) */
	bool bActorLinksEnabled = true;

	/** Actor link toggle button in toolbar */
	TSharedPtr<SButton> ActorLinkToggleButton;

	/** Toggle actor linkification on/off */
	void ToggleActorLinks();

	// === Phase 8: Context Awareness (Issue #5478) ===

	/** Whether editor context (selected Actors/Assets/Level) is auto-included in messages */
	bool bContextEnabled = false;

	/** Context toggle button in toolbar */
	TSharedPtr<SButton> ContextToggleButton;

	/** Context preview text block (shows selected items when Context ON) */
	TSharedPtr<STextBlock> ContextPreviewText;

	/** Cached context string (refreshed every 0.5s, not every frame) */
	FString CachedContextText;
	double LastContextUpdateTime = 0.0;

	/** Query current editor context: selected Actors, Content Browser assets, current Level */
	FString GetEditorContext() const;

	/** Toggle context inclusion on/off */
	void ToggleContext();

	// === Session Management (Issue #5657) ===

	/** Current active chat session (messages, metadata) */
	FCUChatSession CurrentSession;

	/** Last session ID for auto-resume across editor restarts */
	FString LastSessionId;

	/** History + New Chat toolbar buttons */
	TSharedPtr<SButton> HistoryButton;
	TSharedPtr<SButton> NewChatButton;

	/** History panel widget (shown when History button clicked) */
	TSharedPtr<SCUChatHistoryPanel> HistoryPanel;

	/** Widget switcher to toggle between chat view and history view */
	TSharedPtr<SWidgetSwitcher> ViewSwitcher;

	/** Get sessions directory: {ProjectSaved}/NarshaMCP/Sessions/ */
	FString GetSessionsDirectory() const;

	/** Legacy persistence path (for migration): Saved/NarshaMCP/last_session.txt */
	FString GetSessionPersistencePath() const;

	/** Save current session to JSON file */
	void SaveCurrentSession();

	/** Load a session from JSON by ID — populates message list */
	void LoadSession(const FString& SessionId);

	/** Get list of all saved sessions, sorted by UpdatedAt descending (cached) */
	TArray<FCUChatSession> GetSessionList() const;

	/** Invalidate cached session list — next GetSessionList() reloads from disk */
	void InvalidateSessionListCache();

	/** Cached session metadata list — metadata-only parse, skips message deserialization.
	 * Only valid when bSessionListCacheDirty is false. Thread-safe on Game Thread only. */
	mutable TArray<FCUChatSession> CachedSessionList;
	mutable bool bSessionListCacheDirty = true;

	/** Delete a session JSON file */
	void DeleteSession(const FString& SessionId);

	/** Migrate legacy last_session.txt to Sessions/ directory */
	void MigrateLegacySession();

	/** Save/Load just the last session ID (for auto-resume) */
	void SaveLastSessionId();
	void LoadLastSessionId();

	/** Navigate to history view or start new chat */
	void ShowHistoryView();
	void StartNewChat();

	/** Toggle between chat and history view (Issue #5822: M8 UX) */
	void ToggleHistoryView();

	/** Get history button color: blue when active, foreground when inactive */
	FSlateColor GetHistoryButtonSlateColor() const;

	/** Register toolbar button in Level Editor toolbar (Issue #5822: M8 UX) */
	void RegisterToolbarButton();

	/** Open terminal window for Claude CLI authentication */
	void OpenTerminal();

	/** Run async CLI health check when panel opens (Issue #5589) */
	void RunCliHealthCheck();

	// === Phase 4: Editor Integration (Issue #5478) ===

	/** Quick action toolbar buttons */
	TSharedPtr<SButton> CompileButton;
	TSharedPtr<SButton> SaveAllButton;

	/** Flat dark toolbar button style (matching UE Editor) */
	FButtonStyle ToolbarButtonStyle;

#if WITH_NARSHAMCP_LIVE_CODING
	/** Live Coding patch complete delegate handle (Win64-only — see Issue #9229) */
	FDelegateHandle LiveCodingPatchHandle;
#endif

	/** Map opened delegate handle — invalidates linkifier cache on level change */
	FDelegateHandle OnMapOpenedHandle;

#if WITH_NARSHAMCP_LIVE_CODING
	/** Trigger Live Coding compile (Win64-only — see Issue #9229) */
	void TriggerCompile();
#endif

	/** Save all dirty packages */
	void TriggerSaveAll();

#if WITH_NARSHAMCP_LIVE_CODING
	/** Called when Live Coding patch completes (success only — Win64-only — see Issue #9229) */
	void OnLiveCodingPatchComplete();
#endif

	/** Global keyboard shortcut processor (Ctrl+Shift+P) */
	TSharedPtr<FCUChatInputProcessor> InputProcessor;

	/** Delegate handle for OnApplicationActivationStateChanged (Alt+Tab stuck-key fix) */
	FDelegateHandle AppActivationHandle;

	/** Ticker callback for backend polling */
	bool Tick(float DeltaTime);

	// === Detail Side Panel (Chat Visualization) ===

	/** Detail side panel — shows UPROPERTY inspector for clicked asset/actor */
	TSharedPtr<SCUChatDetailPanel> DetailPanel;

	/** Wrapper SBox for detail panel — visibility toggle via SetVisibility(Collapsed) */
	TSharedPtr<SBox> DetailPanelBox;

	/** Whether detail panel is currently visible */
	bool bDetailPanelVisible = false;

	/** Show detail panel with the given object */
	void ShowDetailPanel(UObject* Object, const FString& DisplayName);

	/** Hide detail panel */
	void HideDetailPanel();

	/** Resolve asset path to UObject for detail view (Blueprint→CDO, others→direct) */
	UObject* ResolveAssetForDetailView(const FString& AssetPath);

	// === Annotation ↔ Chat Bridge (Issue #5821) ===

	/** Handle "Discuss in Chat" from annotation overlay — opens panel with context */
	void HandleAnnotationDiscuss(const FString& AnnotationId, const FString& AnnotationText, const FString& AssetPath);

	// === Phase 10-3 (Issue #8842) — Recent Editor Actions context injection ===

public:
	/**
	 * Toggle whether recent editor actions are auto-prepended to outgoing chat messages.
	 * When enabled, the BackendClient's ContextProvider returns a formatted block:
	 *
	 *   [Recent Editor Actions]
	 *   asset_updated: {...}
	 *   compile_finished: {...}
	 *   [End Recent Actions]
	 */
	bool bInjectRecentActions = false;

	/** Build formatted "[Recent Editor Actions]…[End Recent Actions]" block from rolling buffer.
	 *  Returns empty string when bInjectRecentActions is false or buffer is empty. */
	FString BuildRecentActionsContext() const;

private:
	/** Maximum number of recent editor actions to keep in the rolling buffer (#8842). */
	static constexpr int32 MaxRecentActions = 5;

	/** Rolling buffer of formatted "EventType: EventData" entries — newest at end. */
	TArray<FString> RecentEditorActions;

	/** Lock for RecentEditorActions — OnEditorEvent fires off the WS Game Thread? Always Game-Thread per #8840 docs, but be defensive. */
	mutable FCriticalSection RecentActionsLock;

	/** Subscription handle for UCUWebSocketEditorSubsystem::OnEventBroadcast (#8840). */
	FDelegateHandle EditorEventHandle;

	/** Append an event to RecentEditorActions, trimming to MaxRecentActions. */
	void OnEditorEvent(const FString& EventType, const FString& EventData);

#if WITH_AUTOMATION_WORKER
public:
	// Test-only accessors (Issue #5478 regression tests)
	bool TestIsSubmitting() const { return bIsSubmitting; }
	void TestSetSubmitting(bool bVal) { bIsSubmitting = bVal; }
	FString TestGetLastSessionId() const { return LastSessionId; }
	bool TestGetActorLinksEnabled() const { return bActorLinksEnabled; }
	void TestSetLastSessionId(const FString& Id) { LastSessionId = Id; }
	// Issue #23127: this used to be a header-inlined SHADOW of the New Chat
	// state transition, so the tests asserting "StartNewChat must clear
	// LastSessionId" never executed production code. It now delegates to the
	// real seam StartNewChat() itself calls.
	void TestStartNewChatLogic() { ResetSessionStateForNewChat(); }
	bool TestHandleSourceLink(const FString& URL) { return HandleSourceLink(URL); }
	bool TestHandleActorLink(const FString& Name) { return HandleActorLink(Name); }
	bool TestHandleAssetLink(const FString& URL) { return HandleAssetLink(URL); }
	FString TestGetEditorContext() const { return GetEditorContext(); }
	bool TestGetContextEnabled() const { return bContextEnabled; }
	void TestSetSubmittingState(bool b) { SetSubmittingState(b); }
	// Issue #8842 test accessors
	void TestPushEditorEvent(const FString& EventType, const FString& EventData) { OnEditorEvent(EventType, EventData); }
	int32 TestGetRecentActionsCount() const
	{
		FScopeLock Lock(&RecentActionsLock);
		return RecentEditorActions.Num();
	}
	void TestClearRecentActions()
	{
		FScopeLock Lock(&RecentActionsLock);
		RecentEditorActions.Reset();
	}

	// Test-only accessors for native Slate path (Issue #5652)
	TSharedPtr<SCUChatMessageList> TestGetNativeMessageList() const { return NativeMessageList; }
	void TestSetNativeMessageList(TSharedPtr<SCUChatMessageList> InList) { NativeMessageList = InList; }
	TSharedPtr<FCUChatBackendClient> TestGetBackendClient() const { return BackendClient; }

	// Test-only accessors for session management (Issue #5657)
	FCUChatSession& TestGetCurrentSession();
	void TestSaveCurrentSession() { SaveCurrentSession(); }
	void TestLoadSession(const FString& Id) { LoadSession(Id); }
	TArray<FCUChatSession> TestGetSessionList() const { return GetSessionList(); }
	void TestDeleteSession(const FString& Id) { DeleteSession(Id); }
	FString TestGetSessionsDirectory() const { return GetSessionsDirectory(); }
	TSharedPtr<SCUChatHistoryPanel> TestGetHistoryPanel() const { return HistoryPanel; }

	// Test-only accessors for view switching + migration (Issue #5657, 5.0 coverage)
	void TestShowHistoryView() { ShowHistoryView(); }
	void TestStartNewChatFull() { StartNewChat(); }
	void TestMigrateLegacySession() { MigrateLegacySession(); }
	TSharedPtr<SWidgetSwitcher> TestGetViewSwitcher() const { return ViewSwitcher; }

	// Test-only accessors for UX improvements (Issue #5985)
	void TestToggleHistoryView() { ToggleHistoryView(); }
	bool TestGetIsSubmitting() const { return bIsSubmitting; }
	void TestRegisterToolbarButton() { RegisterToolbarButton(); }
	FLinearColor TestGetHistoryButtonColor() const {
		return GetHistoryButtonSlateColor().GetSpecifiedColor();
	}

	// Test-only accessor for cache invalidation (Issue #5657, cache behavior tests)
	void TestInvalidateSessionListCache() { InvalidateSessionListCache(); }

	// Test-only accessors for detail panel (Chat Visualization)
	bool TestGetDetailPanelVisible() const { return bDetailPanelVisible; }
	TSharedPtr<SCUChatDetailPanel> TestGetDetailPanel() const { return DetailPanel; }
	void TestShowDetailPanel(UObject* Obj, const FString& Name) { ShowDetailPanel(Obj, Name); }
	void TestHideDetailPanel() { HideDetailPanel(); }
	UObject* TestResolveAsset(const FString& Path) { return ResolveAssetForDetailView(Path); }

	// Test-only accessors for annotation bridge (Issue #5821)
	bool TestHandleAnnotateLink(const FString& URL) { return HandleAnnotateLink(URL); }
	void TestHandleAnnotationDiscuss(const FString& Id, const FString& Text, const FString& Path) { HandleAnnotationDiscuss(Id, Text, Path); }
	FString TestGetInputWidgetText() const { return InputWidget.IsValid() ? InputWidget->GetText().ToString() : FString(); }

	// Test-only accessors for multi-tab state (Issue #9168)
	int32 TestGetTabStateCount() const { return TabStates.Num(); }
	int32 TestGetActiveTabIndex() const { return ActiveTabIndex; }
	int32 TestGetNextTabCounter() const { return NextTabCounter; }
	FCUChatSession& TestGetTabSession(int32 Index)
	{
		check(TabStates.IsValidIndex(Index));
		return TabStates[Index]->Session;
	}
	TSharedPtr<SCUChatMessageList> TestGetTabMessageList(int32 Index) const
	{
		return TabStates.IsValidIndex(Index) ? TabStates[Index]->MessageList : nullptr;
	}
	/**
	 * Issue #28657: does the tab state at Index still hold a LIVE SDockTab?
	 * Total and non-fatal — follows TestGetTabMessageList's IsValidIndex shape, not
	 * TestGetTabSession's check(). Because it is total, a false return only means
	 * "no live DockTab" once the caller has separately asserted the index is in
	 * range. Note this tests TWeakPtr liveness, not "was ever assigned".
	 */
	bool TestTabHasDockTab(int32 Index) const
	{
		return TabStates.IsValidIndex(Index) && TabStates[Index]->DockTab.IsValid();
	}
	TWeakPtr<SDockTab> TestGetStreamingTab() const { return StreamingTab; }
	void TestSetStreamingTab(TWeakPtr<SDockTab> Tab) { StreamingTab = Tab; }
	void TestSetActiveTabIndex(int32 Index)
	{
		if (TabStates.IsValidIndex(Index))
		{
			ActiveTabIndex = Index;
			SyncMirrorsToActiveTab();
		}
	}
#endif
};
