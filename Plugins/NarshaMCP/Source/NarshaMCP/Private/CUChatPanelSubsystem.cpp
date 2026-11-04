// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5478: AI Chat Panel Subsystem implementation
// Native Slate AI chat panel with Claude CLI backend

#include "CUChatPanelSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/App.h"               // Issue #23127: FApp::CanEverRender headless guard
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructureModule.h"
#include "WorkspaceMenuStructure.h"
#include "Styling/AppStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"   // Issue #5652: SBorder for native Slate tab background
#include "Widgets/SOverlay.h"         // Issue #5652: SOverlay for Recessed background layer (SSceneOutliner pattern)
#include "Widgets/Layout/SSeparator.h" // Issue #5652: SSeparator for section dividers (SContentBrowser pattern)
#include "Widgets/Text/STextBlock.h"
#include "Styling/SlateTypes.h"      // FEditableTextBoxStyle, FButtonStyle
#include "Styling/CoreStyle.h"       // FCoreStyle::GetDefaultFontStyle
#if WITH_NARSHAMCP_LIVE_CODING
#include "ILiveCodingModule.h"       // Issue #5478 Phase 4: ILiveCodingModule::Compile (Win64-only — Issue #9229)
#endif
#include "FileHelpers.h"             // Issue #5478 Phase 4: FEditorFileUtils::SaveDirtyPackages
#include "Widgets/Images/SImage.h"   // Issue #5478 Phase 4: SImage for toolbar icons
#include "Widgets/Images/SThrobber.h" // Issue #5822: SThrobber for Send button loading state
#include "CUAnnotationData.h"         // FCUAnnotationData full definition for ParseId/MakeId
#include "CUPlatformPaths.h"          // Issue #9230: cross-platform path / shell helpers
#include "ToolMenus.h"                // Issue #5822: UToolMenus for Level Editor toolbar button
#include "Misc/FileHelper.h"        // Issue #5478 Phase 5: FFileHelper for session persistence
#include "Widgets/SNullWidget.h"     // Issue #5478 Phase 5: SNullWidget for toolbar spacer
#include "SourceCodeNavigation.h"    // Issue #5478 Phase 6: FSourceCodeNavigation::OpenSourceFile
#include "Editor.h"                  // Issue #5478 Phase 8: GEditor
#include "Selection.h"               // Issue #5478 Phase 8: FSelectionIterator
#include "GameFramework/Actor.h"     // Issue #5478 Phase 8: AActor
#include "EngineUtils.h"             // Issue #5478: TActorIterator for HandleActorLink
#include "CUChatBackendClient.h"     // Issue #5652: FCUChatBackendClient, FCUClaudeCliBackendClient
#include "CUWebSocketEditorSubsystem.h" // Issue #8842: subscribe to OnEventBroadcast for context injection
#include "Slate/SCUChatMessageBubble.h"  // Issue #5652: SCUChatMessageBubble
#include "Slate/SCUChatMessageList.h"    // Issue #5652: SCUChatMessageList
#include "CUChatPanelSettings.h"         // Issue #5589: UDeveloperSettings for AI Chat Panel
#include "Async/Async.h"                     // Issue #5589: AsyncTask for CLI health check
#include "Serialization/JsonReader.h"        // Issue #5657: JSON deserialization
#include "Serialization/JsonSerializer.h"    // Issue #5657: JSON serialization
#include "Serialization/JsonWriter.h"        // Issue #5657: JSON string output
#include "Widgets/Layout/SWidgetSwitcher.h"  // Issue #5657: Chat/History view switching
#include "Slate/SCUChatHistoryPanel.h"       // Issue #5657: History panel widget
#include "HAL/FileManager.h"                 // Issue #5657: Directory scanning
#include "CUAnnotationChatBridge.h"          // Issue #5821: Annotation ↔ Chat bridge delegates
#include "CUAnnotationData.h"                  // Issue #5821: FCUAnnotationData full definition (ParseId, MakeId, AnnotateLinkPrefix)
#include "GenericPlatform/GenericPlatformHttp.h" // Issue #5821: URL decoding for ue-annotate:// links
#include "Slate/SCUChatDetailPanel.h"            // Chat Visualization: Detail side panel
#include "CUChatLinkifier.h"                     // Chat Visualization: Auto-linkification utility
#include "Engine/Blueprint.h"                    // Chat Visualization: UBlueprint for CDO resolution

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPChat, Log, All);

const FName UCUChatPanelSubsystem::AIChatTabId(TEXT("NarshaMCPAI"));
static constexpr int32 kMaxAutoTitleLength = 50;

bool UCUChatPanelSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// Issue #10756: cook/headless commandlets do NOT initialize FSlateApplication.
	// Skipping subsystem creation here prevents Initialize() from touching Slate
	// (RegisterInputPreProcessor, OnApplicationActivationStateChanged) and crashing
	// Development packaging. NarshaMCP's own UCommandlet subclasses are unaffected
	// because they are independent UCommandlet types, not editor subsystems.
	if (IsRunningCommandlet())
	{
		return false;
	}
	return Super::ShouldCreateSubsystem(Outer);
}

void UCUChatPanelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 1. Get plugin directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NarshaMCP"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Plugin not found — AI panel disabled"));
		return;
	}
	FString PluginDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());

	// 1b. Read AI Chat Panel settings (Issue #5589)
	// Priority: config.ini [AIChatPanel] (explicit) > UDeveloperSettings (Project Settings UI)
	bool bShowInEditor = true;
	{
		// Step 1: Read from UDeveloperSettings (Edit > Project Settings > Plugins > NarshaMCP - AI Chat Panel)
		const UCUChatPanelSettings* Settings = GetDefault<UCUChatPanelSettings>();
		if (Settings)
		{
			bShowInEditor = Settings->bShowInEditor;
		}

		// Step 2: config.ini override (backward compatibility — takes precedence when present)
		// Note: config.ini uses '#' comments (Python-style), not ';' (UE-style),
		// so GConfig->GetBool() can't parse it. Manual file read instead.
		FString PluginConfigPath = PluginDir / TEXT("Config") / TEXT("config.ini");
		FString ConfigText;
		if (FFileHelper::LoadFileToString(ConfigText, *PluginConfigPath))
		{
			int32 SectionStart = ConfigText.Find(TEXT("[AIChatPanel]"));
			if (SectionStart != INDEX_NONE)
			{
				int32 NextSection = ConfigText.Find(TEXT("["), ESearchCase::CaseSensitive, ESearchDir::FromStart, SectionStart + 15);
				FString Section = (NextSection != INDEX_NONE)
					? ConfigText.Mid(SectionStart, NextSection - SectionStart)
					: ConfigText.Mid(SectionStart);
				if (Section.Contains(TEXT("show_in_editor = false")) || Section.Contains(TEXT("show_in_editor=false")))
				{
					bShowInEditor = false;
					UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] AI Chat Panel disabled via config.ini override"));
				}
				else if (Section.Contains(TEXT("show_in_editor = true")) || Section.Contains(TEXT("show_in_editor=true")))
				{
					bShowInEditor = true;
					UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] AI Chat Panel enabled via config.ini override"));
				}
			}
		}

		if (!bShowInEditor)
		{
			UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] AI Chat Panel disabled"));
			return;
		}
	}

	// Detect claude CLI path and create backend client
	CachedClaudePath = DetectClaudeCliPath();
	BackendClient = MakeShared<FCUClaudeCliBackendClient>(CachedClaudePath);

	// Issue #8842 (Phase 10-3): subscribe to in-process editor event delegate (#8840)
	// to maintain a rolling buffer of recent editor actions, and wire BackendClient's
	// ContextProvider to inject those actions into outgoing chat messages when the
	// bInjectRecentActions toggle is enabled.
	if (UCUWebSocketEditorSubsystem* WS = GEditor ? GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>() : nullptr)
	{
		EditorEventHandle = WS->OnEventBroadcast.AddUObject(this, &UCUChatPanelSubsystem::OnEditorEvent);
	}
	if (BackendClient.IsValid())
	{
		TWeakObjectPtr<UCUChatPanelSubsystem> WeakThis(this);
		BackendClient->ContextProvider = [WeakThis]() -> FString
		{
			if (UCUChatPanelSubsystem* Strong = WeakThis.Get())
			{
				return Strong->BuildRecentActionsContext();
			}
			return FString();
		};
	}


	// 7. Session management: migrate legacy, load last session (Issue #5657)
	MigrateLegacySession();
	LoadLastSessionId();
	if (!LastSessionId.IsEmpty())
	{
		FString SessionPath = FPaths::Combine(GetSessionsDirectory(), LastSessionId + TEXT(".json"));
		if (FPaths::FileExists(SessionPath))
		{
			FString JsonString;
			if (FFileHelper::LoadFileToString(JsonString, *SessionPath))
			{
				TSharedPtr<FJsonObject> JsonObj;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
				if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
				{
					CurrentSession = FCUChatSession::FromJson(JsonObj);
				}
			}
		}
		else
		{
			// Session file missing — start fresh with same ID
			CurrentSession.SessionId = LastSessionId;
		}
	}

	// 8. Register SDockTab
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(AIChatTabId,
		FOnSpawnTab::CreateUObject(this, &UCUChatPanelSubsystem::SpawnAITab))
		.SetDisplayName(NSLOCTEXT("NarshaMCP", "AITabTitle", "NarshaMCP AI"))
		.SetTooltipText(NSLOCTEXT("NarshaMCP", "AITabTooltip", "AI Assistant powered by Claude + NarshaMCP MCP tools"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner"));

	// 8b. Register Level Editor toolbar button (Issue #5822: M8 UX — toolbar icon)
	UToolMenus::Get()->RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateUObject(
		this, &UCUChatPanelSubsystem::RegisterToolbarButton));

	// 9. Register health check ticker
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UCUChatPanelSubsystem::Tick),
		0.1f  // 100ms interval for responsive streaming (Issue #5652)
	);

	// Register global keyboard shortcut (Ctrl+Shift+P)
	// Issue #10756: defense-in-depth — ShouldCreateSubsystem already blocks commandlet
	// creation, but Slate could still be uninitialized in unusual host configurations
	// (early-init paths, automation runners). Guard the actual call.
	InputProcessor = MakeShared<FCUChatInputProcessor>(this);
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().RegisterInputPreProcessor(InputProcessor);
		AppActivationHandle = FSlateApplication::Get().OnApplicationActivationStateChanged().AddSP(
			InputProcessor.ToSharedRef(), &FCUChatInputProcessor::HandleAppActivationChanged);
	}

#if WITH_NARSHAMCP_LIVE_CODING
	// Register Live Coding patch complete delegate (Phase 4 — Win64-only — Issue #9229)
	if (FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")))
	{
		ILiveCodingModule& LiveCoding = FModuleManager::LoadModuleChecked<ILiveCodingModule>(TEXT("LiveCoding"));
		LiveCodingPatchHandle = LiveCoding.GetOnPatchCompleteDelegate().AddUObject(
			this, &UCUChatPanelSubsystem::OnLiveCodingPatchComplete);
	}
#endif

	// 10. Bind annotation → chat bridge delegate (Issue #5821)
	GetOnCUAnnotationDiscussRequested().BindUObject(this, &UCUChatPanelSubsystem::HandleAnnotationDiscuss);

	// 11. Invalidate linkifier actor name cache on level change (Issue #5998)
	OnMapOpenedHandle = FEditorDelegates::OnMapOpened.AddLambda([](const FString&, bool) {
		FCUChatLinkifier::InvalidateCache();
	});

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] AI Chat Panel initialized (native Slate)"));
}

void UCUChatPanelSubsystem::Deinitialize()
{
#if WITH_NARSHAMCP_LIVE_CODING
	// 0. Unregister Live Coding delegate (Phase 4 — Win64-only — Issue #9229)
	if (LiveCodingPatchHandle.IsValid() && FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")))
	{
		ILiveCodingModule& LiveCoding = FModuleManager::LoadModuleChecked<ILiveCodingModule>(TEXT("LiveCoding"));
		LiveCoding.GetOnPatchCompleteDelegate().Remove(LiveCodingPatchHandle);
		LiveCodingPatchHandle.Reset();
	}
#endif

	// 1. Remove ticker
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}

	// 2. Unregister keyboard shortcut + activation delegate (Alt+Tab stuck-key fix)
	// Issue #10756: pair with the Initialize() guard — only unregister if Slate is up.
	if (InputProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(AppActivationHandle);
		FSlateApplication::Get().UnregisterInputPreProcessor(InputProcessor);
		InputProcessor.Reset();
	}

	// 3. Unregister tab + toolbar button (Issue #5822)
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AIChatTabId);
	if (UToolMenus* ToolMenus = UToolMenus::TryGet())
	{
		ToolMenus->RemoveSection("LevelEditor.LevelEditorToolBar.PlayToolBar", "NarshaMCPAI");
	}

	// 4. Save session for resume on next launch (Issue #5657)
	SaveCurrentSession();
	SaveLastSessionId();

	// Issue #8842: unsubscribe from WebSocket subsystem's OnEventBroadcast.
	if (EditorEventHandle.IsValid())
	{
		if (GEditor)
		{
			if (UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>())
			{
				WS->OnEventBroadcast.Remove(EditorEventHandle);
			}
		}
		EditorEventHandle.Reset();
	}
	{
		FScopeLock Lock(&RecentActionsLock);
		RecentEditorActions.Reset();
	}

	// 5. Cancel and destroy backend client (Issue #5652)
	if (BackendClient)
	{
		BackendClient->ContextProvider = nullptr; // Issue #8842: drop captured WeakThis lambda
		BackendClient->CancelCurrentRequest();
		BackendClient->MarkDestroyed();
		BackendClient.Reset();
	}

	// 5b. Clear native widget references (Issue #5652)
	CurrentAssistantBubble.Reset();
	NativeMessageList.Reset();

	// 5c. Stop MCP daemon readiness poller + prewarm
	if (MCPStatusTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(MCPStatusTickerHandle);
		MCPStatusTickerHandle.Reset();
	}
	StopMCPPrewarm();

	// 7. Clear widget references
	InputWidget.Reset();
	SendButton.Reset();
	CompileButton.Reset();
	SaveAllButton.Reset();
	ContextToggleButton.Reset();
	ContextPreviewText.Reset();
	HistoryButton.Reset();
	NewChatButton.Reset();
	HistoryPanel.Reset();
	ViewSwitcher.Reset();

	// 8. Unbind annotation bridge delegate (Issue #5821)
	GetOnCUAnnotationDiscussRequested().Unbind();

	// 9. Unbind map change delegate (Issue #5998)
	if (OnMapOpenedHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(OnMapOpenedHandle);
		OnMapOpenedHandle.Reset();
	}

	Super::Deinitialize();
	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] AI Chat Panel deinitialized"));
}

FString UCUChatPanelSubsystem::DetectClaudeCliPath()
{
	// Issue #26457: native-first cross-platform resolution, including the
	// canonical Windows standalone path outside the Editor's inherited PATH.
	FString Found = NarshaMCP::Platform::FindClaudeCliPath();
	if (!Found.IsEmpty())
	{
		const TCHAR* InstallKind = Found.EndsWith(TEXT(".cmd"), ESearchCase::IgnoreCase)
			? TEXT("npm")
			: TEXT("native");
		UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Found Claude CLI (%s): %s"), InstallKind, *Found);
		return Found;
	}

	UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Claude CLI not found — chat backend unavailable"));
	return FString();
}

TSharedRef<SDockTab> UCUChatPanelSubsystem::SpawnAITab(const FSpawnTabArgs& Args)
{
	// First tab (Ctrl+Shift+P / toolbar button / WorkspaceMenu) — delegate to the
	// existing layout builder. SpawnNativeSlateTab assigns subsystem-level
	// widget members (NativeMessageList, ViewSwitcher, etc.) — we capture those
	// into a fresh FCUChatTabState AFTER construction so multi-tab (#9168) works.
	TSharedRef<SDockTab> Tab = SpawnNativeSlateTab(Args);

	// Register the tab in our multi-tab registry.
	TSharedRef<FCUChatTabState> NewState = MakeShared<FCUChatTabState>();
	NewState->DockTab = Tab;
	NewState->MessageList = NativeMessageList;
	NewState->CurrentAssistantBubble = CurrentAssistantBubble;
	NewState->Session = CurrentSession;  // Start from whatever Initialize loaded (auto-resume)

	TabStates.Add(NewState);
	ActiveTabIndex = TabStates.Num() - 1;
	NextTabCounter = FMath::Max(NextTabCounter, ActiveTabIndex + 2);

	// Wire lifecycle hooks so tab switches / closes update state.
	Tab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateUObject(
		this, &UCUChatPanelSubsystem::OnChatTabActivated));
	Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
		[WeakThis = TWeakObjectPtr<UCUChatPanelSubsystem>(this)](TSharedRef<SDockTab> ClosedTab) {
			if (UCUChatPanelSubsystem* Self = WeakThis.Get())
			{
				Self->OnChatTabClosed(ClosedTab);
			}
		}));

	return Tab;
}

// === Multi-Tab Helpers (Issue #9168) ===

FCUChatTabState* UCUChatPanelSubsystem::GetActiveTabState()
{
	if (!TabStates.IsValidIndex(ActiveTabIndex))
	{
		return nullptr;
	}
	return &TabStates[ActiveTabIndex].Get();
}

FCUChatTabState* UCUChatPanelSubsystem::ResolveStreamingTabState()
{
	TSharedPtr<SDockTab> Pinned = StreamingTab.Pin();
	if (!Pinned.IsValid())
	{
		// StreamingTab was closed mid-stream — drop the update.
		return nullptr;
	}
	for (TSharedRef<FCUChatTabState>& State : TabStates)
	{
		if (State->DockTab.Pin() == Pinned)
		{
			return &State.Get();
		}
	}
	return nullptr;
}

FCUChatTabState* UCUChatPanelSubsystem::FindTabStateFor(const TSharedRef<SDockTab>& Tab)
{
	for (TSharedRef<FCUChatTabState>& State : TabStates)
	{
		if (State->DockTab.Pin() == Tab)
		{
			return &State.Get();
		}
	}
	return nullptr;
}

void UCUChatPanelSubsystem::SyncMirrorsToActiveTab()
{
	FCUChatTabState* Active = GetActiveTabState();
	if (!Active)
	{
		return;
	}
	NativeMessageList = Active->MessageList;
	CurrentAssistantBubble = Active->CurrentAssistantBubble;
	CurrentSession = Active->Session;
	LastSessionId = Active->Session.SessionId;
}

void UCUChatPanelSubsystem::SaveSessionForTab(FCUChatTabState& TabState)
{
	// Route through the existing SaveCurrentSession by temporarily swapping
	// CurrentSession — keeps the save format consistent and single code path.
	FCUChatSession Backup = CurrentSession;
	CurrentSession = TabState.Session;
	SaveCurrentSession();
	TabState.Session = CurrentSession;  // capture any SessionId mutations
	CurrentSession = Backup;
}

void UCUChatPanelSubsystem::SnapshotActiveTabState()
{
	// Snapshot + freeze the active tab BEFORE SpawnNativeSlateTab overwrites
	// subsystem members (NativeMessageList / CurrentAssistantBubble). Without
	// this the freshly-built tab would orphan the active tab's widgets.
	if (FCUChatTabState* OldActive = GetActiveTabState())
	{
		OldActive->Session = CurrentSession;
		OldActive->CurrentAssistantBubble = CurrentAssistantBubble;
		// MessageList already pinned when the old tab was spawned — no re-capture.
	}
}

void UCUChatPanelSubsystem::ResetSessionStateForNewChat()
{
	// Reset session state so the new tab starts fresh.
	CurrentSession = FCUChatSession();

	// Issue #9168: clear LastSessionId so the new chat does not auto-resume an
	// existing thread, and persist the cleared pointer so an editor restart
	// does not resurrect it either.
	LastSessionId.Empty();
	SaveLastSessionId();

	CurrentAssistantBubble.Reset();
	NativeMessageList.Reset();
}

TSharedRef<SDockTab> UCUChatPanelSubsystem::BuildNewChatTab()
{
	// Order is load-bearing: snapshot the outgoing tab first, then reset.
	SnapshotActiveTabState();
	ResetSessionStateForNewChat();

	// Build via the existing layout builder — overwrites subsystem members.
	// FSpawnTabArgs takes TSharedPtr<SWindow>+FTabId; SpawnNativeSlateTab ignores
	// the window arg, so a null SWindow ptr is safe here.
	TSharedRef<SDockTab> Tab = SpawnNativeSlateTab(
		FSpawnTabArgs(TSharedPtr<SWindow>(), FTabId(AIChatTabId)));

	// Label with counter for visual distinction ("AI Chat 2", "AI Chat 3", ...).
	Tab->SetLabel(FText::Format(
		NSLOCTEXT("NarshaMCP", "AITabLabelNth", "AI Chat {0}"),
		FText::AsNumber(NextTabCounter)));

	TSharedRef<FCUChatTabState> NewState = MakeShared<FCUChatTabState>();
	NewState->DockTab = Tab;
	NewState->MessageList = NativeMessageList;
	NewState->CurrentAssistantBubble = CurrentAssistantBubble;
	NewState->Session = CurrentSession;

	TabStates.Add(NewState);
	ActiveTabIndex = TabStates.Num() - 1;
	++NextTabCounter;

	Tab->SetOnTabActivated(SDockTab::FOnTabActivatedCallback::CreateUObject(
		this, &UCUChatPanelSubsystem::OnChatTabActivated));
	Tab->SetOnTabClosed(SDockTab::FOnTabClosedCallback::CreateLambda(
		[WeakThis = TWeakObjectPtr<UCUChatPanelSubsystem>(this)](TSharedRef<SDockTab> ClosedTab) {
			if (UCUChatPanelSubsystem* Self = WeakThis.Get())
			{
				Self->OnChatTabClosed(ClosedTab);
			}
		}));

	return Tab;
}

void UCUChatPanelSubsystem::OnChatTabActivated(TSharedRef<SDockTab> ActivatedTab, ETabActivationCause /*Cause*/)
{
	// Find + switch ActiveTabIndex. Save outgoing tab's mirror state first.
	int32 NewIndex = INDEX_NONE;
	for (int32 i = 0; i < TabStates.Num(); ++i)
	{
		if (TabStates[i]->DockTab.Pin() == ActivatedTab)
		{
			NewIndex = i;
			break;
		}
	}
	if (NewIndex == INDEX_NONE || NewIndex == ActiveTabIndex)
	{
		return;  // Unknown tab or no-op
	}

	// Freeze outgoing tab's state.
	if (FCUChatTabState* OldActive = GetActiveTabState())
	{
		OldActive->Session = CurrentSession;
		OldActive->CurrentAssistantBubble = CurrentAssistantBubble;
	}

	// Switch.
	ActiveTabIndex = NewIndex;
	SyncMirrorsToActiveTab();
}

void UCUChatPanelSubsystem::OnChatTabClosed(TSharedRef<SDockTab> ClosedTab)
{
	int32 ClosedIndex = INDEX_NONE;
	for (int32 i = 0; i < TabStates.Num(); ++i)
	{
		if (TabStates[i]->DockTab.Pin() == ClosedTab)
		{
			ClosedIndex = i;
			break;
		}
	}
	if (ClosedIndex == INDEX_NONE)
	{
		return;
	}

	// Save the closed tab's session to disk before dropping.
	if (ClosedIndex == ActiveTabIndex)
	{
		// Mirrors hold the latest state — push back to TabState first.
		TabStates[ClosedIndex]->Session = CurrentSession;
	}
	SaveSessionForTab(*TabStates[ClosedIndex]);

	// Remove + re-index.
	TabStates.RemoveAt(ClosedIndex);
	if (TabStates.Num() == 0)
	{
		ActiveTabIndex = INDEX_NONE;
		// Reset mirrors so a subsequent TryInvokeTab starts clean.
		CurrentSession = FCUChatSession();
		NativeMessageList.Reset();
		CurrentAssistantBubble.Reset();
		return;
	}
	if (ActiveTabIndex == ClosedIndex)
	{
		ActiveTabIndex = FMath::Clamp(ClosedIndex, 0, TabStates.Num() - 1);
		SyncMirrorsToActiveTab();
	}
	else if (ActiveTabIndex > ClosedIndex)
	{
		--ActiveTabIndex;
	}
}


// === Issue #5652: BuildToolbar() — toolbar for the native Slate chat tab ===

namespace ToolbarLayout
{
	static const FMargin ButtonContentPadding(8.0f, 4.0f);
	static const FMargin SlotGap(4.0f, 0.0f, 0.0f, 0.0f);
}

static constexpr float DetailPanelWidth = 350.0f;

TSharedRef<SWidget> UCUChatPanelSubsystem::BuildToolbar()
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SAssignNew(CompileButton, SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "CompileTip2", "Trigger Live Coding (Ctrl+Alt+F11)"))
				.OnClicked_Lambda([this]() -> FReply {
#if WITH_NARSHAMCP_LIVE_CODING
					TriggerCompile();
#else
					UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Live Coding compile not available on this platform (Win64-only — Issue #9229)."));
#endif
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("LevelEditor.Recompile"))
							.DesiredSizeOverride(FVector2D(20, 20))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "CompileBtn2", "Compile"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(SaveAllButton, SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "SaveAllTip2", "Save all modified assets"))
				.OnClicked_Lambda([this]() -> FReply {
					TriggerSaveAll();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("AssetEditor.SaveAsset"))
							.DesiredSizeOverride(FVector2D(20, 20))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "SaveAllBtn2", "Save All"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
		]

		// Context toggle
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(ToolbarLayout::SlotGap)
		[
			SAssignNew(ContextToggleButton, SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "ContextTip2", "Include selected Actor/Asset/Level info in messages"))
				.OnClicked_Lambda([this]() -> FReply {
					ToggleContext();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Info"))
							.DesiredSizeOverride(FVector2D(20, 20))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(TAttribute<FText>::CreateLambda([this]() -> FText {
								return bContextEnabled
									? NSLOCTEXT("NarshaMCP", "ContextOn2", "Context ON")
									: NSLOCTEXT("NarshaMCP", "ContextOff2", "Context");
							}))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() -> FSlateColor {
								return bContextEnabled
									? FSlateColor(FLinearColor(0.306f, 0.788f, 0.690f))
									: FSlateColor::UseForeground();
							}))
					]
				]
		]

		// Actor Link toggle
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(ToolbarLayout::SlotGap)
		[
			SAssignNew(ActorLinkToggleButton, SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "ActorLinksTip2", "Enable/disable clickable actor name links in chat responses"))
				.OnClicked_Lambda([this]() -> FReply {
					ToggleActorLinks();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Link"))
							.DesiredSizeOverride(FVector2D(20, 20))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(TAttribute<FText>::CreateLambda([this]() -> FText {
								return bActorLinksEnabled
									? NSLOCTEXT("NarshaMCP", "ActorLinksOn2", "Links ON")
									: NSLOCTEXT("NarshaMCP", "ActorLinksOff2", "Links");
							}))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() -> FSlateColor {
								return bActorLinksEnabled
									? FSlateColor(FLinearColor(1.0f, 0.843f, 0.0f))
									: FSlateColor::UseForeground();
							}))
					]
				]
		]

		// Terminal button (for CLI authentication)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(ToolbarLayout::SlotGap)
		[
			SNew(SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "TerminalTip", "Open terminal for Claude CLI authentication"))
				.OnClicked_Lambda([this]() -> FReply {
					OpenTerminal();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Console"))
							.DesiredSizeOverride(FVector2D(20, 20))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "TerminalBtn", "Terminal"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
		]

		// Context preview (fills remaining space)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(ToolbarLayout::SlotGap)
		[
			SAssignNew(ContextPreviewText, STextBlock)
				.Text(TAttribute<FText>::CreateLambda([this]() -> FText {
					double Now = FPlatformTime::Seconds();
					if (Now - LastContextUpdateTime > 0.5)
					{
						LastContextUpdateTime = Now;
						FString Ctx = GetEditorContext();
						if (Ctx.IsEmpty())
							CachedContextText = TEXT("(nothing selected)");
						else
						{
							Ctx.ReplaceInline(TEXT("\n"), TEXT(" | "));
							CachedContextText = Ctx;
						}
					}
					return FText::FromString(CachedContextText);
				}))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() -> FSlateColor {
					return bContextEnabled
						? FSlateColor(FLinearColor(0.306f, 0.788f, 0.690f, 0.8f))
						: FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f, 0.5f));
				}))
				.AutoWrapText(false)
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
		]

		// "+ New" button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SAssignNew(NewChatButton, SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "NewChatTip2", "Start a new conversation"))
				.OnClicked_Lambda([this]() -> FReply {
					StartNewChat();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Plus"))
							.DesiredSizeOverride(FVector2D(20, 20))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "NewChatBtn2", "New"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(FSlateColor::UseForeground())
					]
				]
		]

		// "History" button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(HistoryButton, SButton)
				.ButtonStyle(&ToolbarButtonStyle)
				.ContentPadding(ToolbarLayout::ButtonContentPadding)
				.ToolTipText(NSLOCTEXT("NarshaMCP", "HistoryTip2", "View conversation history"))
				.OnClicked_Lambda([this]() -> FReply {
					ToggleHistoryView();
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(SImage)
							.Image(FAppStyle::GetBrush("Icons.Recent"))
							.DesiredSizeOverride(FVector2D(20, 20))
							.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() -> FSlateColor {
								return GetHistoryButtonSlateColor();
							}))
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0, 0, 0)
					[
						SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "HistoryBtn2", "History"))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(TAttribute<FSlateColor>::CreateLambda([this]() -> FSlateColor {
								return GetHistoryButtonSlateColor();
							}))
					]
				]
		];
}

// === Issue #5652: SpawnNativeSlateTab() — native Slate chat tab ===

TSharedRef<SDockTab> UCUChatPanelSubsystem::SpawnNativeSlateTab(const FSpawnTabArgs& Args)
{
	// UE native input style — matches Editor text fields, no custom color overrides
	ChatInputStyle = FAppStyle::Get().GetWidgetStyle<FEditableTextBoxStyle>("NormalEditableTextBox");
	ChatInputStyle.SetFont(FCoreStyle::GetDefaultFontStyle("Regular", 11));

	// Send button style
	ChatSendButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("FlatButton");
	const FLinearColor BluePrimary(0.0f, 0.44f, 0.88f, 1.0f);    // #0070E0
	const FLinearColor BlueHover(0.0f, 0.50f, 1.0f, 1.0f);       // #0080FF
	const FLinearColor BluePressed(0.0f, 0.35f, 0.70f, 1.0f);    // #0059B3
	ChatSendButtonStyle.Normal.TintColor = FSlateColor(BluePrimary);
	ChatSendButtonStyle.Hovered.TintColor = FSlateColor(BlueHover);
	ChatSendButtonStyle.Pressed.TintColor = FSlateColor(BluePressed);
	ChatSendButtonStyle.Disabled.TintColor = FSlateColor(FLinearColor(0.3f, 0.3f, 0.3f, 0.5f));
	ChatSendButtonStyle.NormalForeground = FSlateColor(FLinearColor::White);
	ChatSendButtonStyle.HoveredForeground = FSlateColor(FLinearColor::White);
	ChatSendButtonStyle.PressedForeground = FSlateColor(FLinearColor::White);

	// Toolbar button style — UE native (transparent bg, themed hover)
	ToolbarButtonStyle = FAppStyle::Get().GetWidgetStyle<FButtonStyle>("SimpleButtonLabelAndIcon");

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.Label(NSLOCTEXT("NarshaMCP", "AITabLabelNative", "NarshaMCP AI"))
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Panel"))
		.Padding(0.0f)
		[
			SNew(SHorizontalBox)

			// Slot 0: Chat panel (existing layout)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
			SNew(SVerticalBox)

			// Toolbar row — SSceneOutliner pattern: Header background for toolbars
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
				.Padding(FMargin(2.0f, 1.0f))
				[
					BuildToolbar()
				]
			]

			// Status bar — SSceneOutliner::ShowStatusBar pattern: Brushes.Header + FMargin(14,9)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.Padding(FMargin(14.0f, 9.0f))
				[
					SAssignNew(StatusBarText, STextBlock)
						.Text(NSLOCTEXT("NarshaMCP", "StatusChecking", "Checking Claude CLI..."))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				]
			]

			// Separator — SContentBrowser pattern: visual boundary between header and content
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SSeparator)
				.Thickness(2.0f)
			]

			// ViewSwitcher — toggles between Chat view (index 0) and History view (index 1)
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SAssignNew(ViewSwitcher, SWidgetSwitcher)

				// Index 0: Chat view
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)

					// Message list — SSceneOutliner pattern: SOverlay with Recessed background layer
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SOverlay)

						+ SOverlay::Slot()
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
						]

						+ SOverlay::Slot()
						[
							SAssignNew(NativeMessageList, SCUChatMessageList)
						]
					]

					// Separator — visual boundary between content and input
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SSeparator)
						.Thickness(2.0f)
					]

					// MCP daemon readiness banner (Issue #8835 live-test polish)
					// Visible while `narshamcp.exe status` does not yet report a
					// Running daemon; auto-collapses when bMCPDaemonReady flips.
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
						.Padding(FMargin(8.0f, 4.0f))
						.Visibility_Lambda([this]() {
							return bMCPDaemonReady ? EVisibility::Collapsed : EVisibility::Visible;
						})
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Center)
							.Padding(FMargin(0.0f, 0.0f, 6.0f, 0.0f))
							[
								SNew(SThrobber)
								.NumPieces(4)
								.Animate(SThrobber::All)
							]
							+ SHorizontalBox::Slot()
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(NSLOCTEXT("NarshaMCP", "MCPConnecting",
									"MCP \uc11c\ubc84 \uc5f0\uacb0 \uc911\u2026 (\ucd5c\ucd08 5\u201330\ucd08 \uc18c\uc694)"))
								.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.8f, 0.2f)))
							]
						]
					]

					// Input row — Header background to separate from message area
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
						.Padding(FMargin(4.0f, 4.0f))
						[
							SNew(SHorizontalBox)

							+ SHorizontalBox::Slot()
							.FillWidth(1.0f)
							.Padding(0.0f, 0.0f, 4.0f, 0.0f)
							[
								SNew(SBox)
								.MinDesiredHeight(32.0f)
								.MaxDesiredHeight(120.0f)
								[
									SAssignNew(InputWidget, SMultiLineEditableTextBox)
										.Style(&ChatInputStyle)
										.HintText(NSLOCTEXT("NarshaMCP", "InputHintNative", "Type a message... (Enter to send)"))
										.AutoWrapText(true)
										.OnKeyDownHandler_Lambda([this](const FGeometry& /*Geo*/, const FKeyEvent& KeyEvent) -> FReply {
											// Enter → submit (Shift+Enter = newline)
											if (KeyEvent.GetKey() == EKeys::Enter && !KeyEvent.IsShiftDown())
											{
												SubmitNativeInput();
												return FReply::Handled();
											}
											// ESC → abort streaming
											if (KeyEvent.GetKey() == EKeys::Escape && bIsSubmitting)
											{
												if (BackendClient)
												{
													BackendClient->CancelCurrentRequest();
												}
												SetSubmittingState(false);
												return FReply::Handled();
											}
											return FReply::Unhandled();
										})
								]
							]

							+ SHorizontalBox::Slot()
							.AutoWidth()
							.VAlign(VAlign_Bottom)
							[
								SAssignNew(SendButton, SButton)
									.ButtonStyle(&ChatSendButtonStyle)
									.ContentPadding(FMargin(12.0f, 4.0f))
									.OnClicked_Lambda([this]() -> FReply {
										SubmitNativeInput();
										return FReply::Handled();
									})
									[
										SNew(SOverlay)
										+ SOverlay::Slot()
										[
											SNew(STextBlock)
												.Text(NSLOCTEXT("NarshaMCP", "SendBtnNative", "Send"))
												.Visibility(TAttribute<EVisibility>::CreateLambda([this]() {
													return bIsSubmitting ? EVisibility::Collapsed : EVisibility::Visible;
												}))
												.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
												.ColorAndOpacity(FSlateColor(FLinearColor::White))
										]
										+ SOverlay::Slot()
										.HAlign(HAlign_Center)
										.VAlign(VAlign_Center)
										[
											SNew(SThrobber)
												.Visibility(TAttribute<EVisibility>::CreateLambda([this]() {
													return bIsSubmitting ? EVisibility::Visible : EVisibility::Collapsed;
												}))
												.NumPieces(3)
										]
									]
							]
						]
					]
				]

				// Index 1: History view
				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(HistoryPanel, SCUChatHistoryPanel)
					.OnSessionSelected_Lambda([this](const FString& SessionId) {
						LoadSession(SessionId);
						if (ViewSwitcher.IsValid())
						{
							ViewSwitcher->SetActiveWidgetIndex(0);
						}
					})
					.OnBackRequested_Lambda([this]() {
						if (ViewSwitcher.IsValid())
						{
							ViewSwitcher->SetActiveWidgetIndex(0);
						}
					})
					.OnSessionDeleted_Lambda([this](const FString& SessionId) {
						DeleteSession(SessionId);
						// Refresh list after deletion
						if (HistoryPanel.IsValid())
						{
							HistoryPanel->RefreshSessionList(GetSessionList());
						}
					})
				]
			]
			] // end SVerticalBox / HBox Slot 0 (chat)

			// Slot 1: Detail side panel (initially hidden)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SAssignNew(DetailPanelBox, SBox)
				.WidthOverride(DetailPanelWidth)
				.Visibility(EVisibility::Collapsed)
				[
					SNew(SHorizontalBox)

					// Vertical separator between chat and detail panel
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SSeparator)
						.Orientation(Orient_Vertical)
						.Thickness(2.0f)
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SAssignNew(DetailPanel, SCUChatDetailPanel)
						.OnCloseRequested_Lambda([this]() { HideDetailPanel(); })
					]
				]
			]
		] // end SBorder (SHorizontalBox is inside)
	]; // end SDockTab

	// SetEnabled via TAttribute lambda — polls bIsSubmitting each frame
	InputWidget->SetEnabled(TAttribute<bool>::CreateLambda([this]() { return !bIsSubmitting; }));
	SendButton->SetEnabled(TAttribute<bool>::CreateLambda([this]() {
		if (bIsSubmitting) return false;
		if (!InputWidget.IsValid()) return false;
		return !InputWidget->GetText().ToString().TrimStartAndEnd().IsEmpty();
	}));

	// Bind backend client delegates once (not per-message)
	BindBackendDelegates();

	// Health check on panel open — async CLI validation (Issue #5589)
	RunCliHealthCheck();

	return Tab;
}

void UCUChatPanelSubsystem::StopMCPPrewarm()
{
	if (MCPPrewarmProcess.IsValid())
	{
		FPlatformProcess::TerminateProc(MCPPrewarmProcess, true);
		FPlatformProcess::CloseProc(MCPPrewarmProcess);
		MCPPrewarmProcess.Reset();
	}
	if (MCPPrewarmStdinWrite)
	{
		FPlatformProcess::ClosePipe(nullptr, MCPPrewarmStdinWrite);
		MCPPrewarmStdinWrite = nullptr;
	}
}

// === MCP Daemon Readiness Probe (Issue #8835 live-test polish) ===
//
// narshamcp daemon boot (PDB index + cache warmup) can take 5–30s on cold
// start. Claude CLI's --include-partial-messages stream-json often starts
// tool-searching before the daemon registers its mcp__narshamcp__* tools,
// producing empty ToolSearch results and "not connected" user-facing text.
//
// We poll `narshamcp.exe status --all` every 2s until the daemon registry
// lists an entry for this project, then flip bMCPDaemonReady and collapse the
// "MCP 연결 중…" banner above the input row. Stops on Deinitialize.
//
// Issue #9428: `status --project-path X` emits a human-readable single-project
// view ("MCP Port: 19647") that lacks the `port=` / `[RUNNING]` / `[DEAD]`
// registry markers the probe needs. `--all` emits the machine-parseable
// registry list format (`[RUNNING] port=N pid=M\n    Project: <path>`), which
// also matches the comment block below verbatim.
//
// Also eagerly flipped to true when any OnToolResult fires (successful MCP
// tool response is the strongest possible readiness signal).

bool UCUChatPanelSubsystem::TickMCPStatusProbe(float /*DeltaTime*/)
{
	if (bMCPDaemonReady)
	{
		MCPStatusTickerHandle.Reset();
		return false; // stop ticking
	}

	// Resolve narshamcp.exe. Priority:
	//  1. `.mcp.json` `mcpServers.narshamcp.command` — authoritative, same
	//     binary Claude CLI will spawn via the project config. Matches dev
	//     layouts where users point to a local target/release build.
	//  2. Plugin-bundled dev layout (MCP/bin)
	//  3. Plugin-bundled fab layout (Source/ThirdParty/bin)
	// The plugin-bundled binary can refuse to run with "Integrity verification
	// failed" on unsigned/modified dev installs, so the .mcp.json path wins
	// whenever present.
	FString NarshaExe;
	{
		const FString ConfigPath = FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json"));
		FString ConfigJson;
		if (FFileHelper::LoadFileToString(ConfigJson, *ConfigPath))
		{
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigJson);
			if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
			{
				const TSharedPtr<FJsonObject>* Servers = nullptr;
				if (Root->TryGetObjectField(TEXT("mcpServers"), Servers) && Servers)
				{
					const TSharedPtr<FJsonObject>* Narsha = nullptr;
					if ((*Servers)->TryGetObjectField(TEXT("narshamcp"), Narsha) && Narsha)
					{
						FString CmdFromConfig;
						if ((*Narsha)->TryGetStringField(TEXT("command"), CmdFromConfig)
							&& FPaths::FileExists(CmdFromConfig))
						{
							NarshaExe = CmdFromConfig;
						}
					}
				}
			}
		}
	}
	if (NarshaExe.IsEmpty())
	{
		TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("NarshaMCP"));
		if (!Plugin.IsValid())
		{
			return true;
		}
		const FString PluginDir = Plugin->GetBaseDir();
		// Issue #9230: platform-aware binary name (.exe on Win64, "" on Mac/Linux).
		const FString NarshaExeName = NarshaMCP::Platform::GetNarshaMCPBinaryName();
		NarshaExe = FPaths::Combine(PluginDir, TEXT("MCP/bin"), NarshaExeName);
		if (!FPaths::FileExists(NarshaExe))
		{
			NarshaExe = FPaths::Combine(PluginDir, TEXT("Source/ThirdParty/bin"), NarshaExeName);
		}
		if (!FPaths::FileExists(NarshaExe))
		{
			return true; // cannot probe — keep banner visible, try again
		}
	}

	FString ProjectPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectPath);
	// Registry stores project paths with forward slashes (see
	// commands_daemon.rs registry printer). Mirror that normalization so we
	// can substring-match the `Project: <path>` line directly.
	ProjectPath.ReplaceInline(TEXT("\\"), TEXT("/"));

	TWeakObjectPtr<UCUChatPanelSubsystem> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [WeakThis, NarshaExe, ProjectPath]()
	{
		// Issue #9428: use --all (registry list format) instead of
		// --project-path (single-project human view). The registry format
		// emits `[RUNNING] port=N pid=M\n    Project: <path>` per entry,
		// which is what the comment below was written to parse.
		const FString Args = TEXT("status --all");
		int32 ExitCode = -1;
		FString StdOut, StdErr;
		const bool bOk = FPlatformProcess::ExecProcess(*NarshaExe, *Args, &ExitCode, &StdOut, &StdErr);
		// "status" exits 0 in both ready/not-ready cases; parse output.
		//
		// Ready = a registry entry exists for THIS project (any status —
		// [RUNNING] / [PENDING] / [DEAD] — all mean the connect pathway is
		// in flight). Claude CLI's subsequent call will block on tools/list
		// until the daemon finishes loading, so the user doesn't need our
		// banner the moment a registry slot is reserved.
		//
		// Not ready = no entry mentioning our project path (registry is
		// empty for this project — prewarm hasn't fired or failed silently).
		//
		// Match the `Project: <path>` line specifically so we don't
		// accidentally match a substring elsewhere in stdout. Case-insensitive
		// because the registry lowercases the path on some platforms.
		const FString ProjectMarker = FString::Printf(TEXT("Project: %s"), *ProjectPath);
		const bool bReady = bOk && StdOut.Contains(ProjectMarker, ESearchCase::IgnoreCase);

		AsyncTask(ENamedThreads::GameThread, [WeakThis, bReady, NarshaExe, ProjectPath]()
		{
			UCUChatPanelSubsystem* Self = WeakThis.Get();
			if (!Self || Self->bMCPDaemonReady) return;
			if (bReady)
			{
				Self->bMCPDaemonReady = true;
				UE_LOG(LogNarshaMCPChat, Log,
					TEXT("[NarshaMCP] MCP daemon ready — banner hidden"));
				return;
			}
			// Not ready AND never prewarmed this session → spawn a long-lived
			// `narshamcp connect --stdio` process. The daemon stays warm as
			// long as at least one connect process holds it alive. Keeping
			// stdin open (PipeWrite handle retained for panel lifetime)
			// prevents connect from exiting on EOF. When the user sends their
			// first message Claude CLI spawns its OWN connect — both share
			// the already-warm daemon, so tools register instantly.
			if (!Self->bMCPDaemonPrewarmAttempted)
			{
				Self->bMCPDaemonPrewarmAttempted = true;
				// `connect --project-path X` (without --stdio) bridges stdio
				// to the daemon and AUTO-STARTS the daemon if not running.
				// To trigger the bridge, we must push one MCP JSON-RPC line
				// through stdin; an idle connect process does not boot the
				// daemon on its own. We send an `initialize` request — the
				// response is discarded; its only purpose is to cause the
				// daemon to register as [RUNNING] in the registry.
				const FString PrewarmArgs = FString::Printf(
					TEXT("connect --project-path \"%s\""), *ProjectPath);
				// stdin pipe: we feed an `initialize` JSON-RPC so the connect
				// process's stdio bridge actually starts (and therefore brings
				// up the daemon). Retained for the panel's lifetime — closing
				// stdin would EOF-terminate connect and bring the daemon down.
				void* StdInRead = nullptr;
				void* StdInWrite = nullptr;
				// Issue #29321: the child inherits the READ end, so the WRITE end is the
				// parent-local one. CreatePipe's default (bWritePipeLocal=false) clears
				// HANDLE_FLAG_INHERIT on the READ end; CreateProc then rejects it with
				// "PipeReadChild passed to CreateProc is not inheritable." and substitutes
				// nullptr, so connect launched with NO stdin — the initialize/tools/list
				// below went nowhere and the daemon was never prewarmed, while the
				// UE_LOG further down still reported success.
				if (!NarshaMCP::Platform::CreateInheritableStdinPipe(StdInRead, StdInWrite))
				{
					// Without this the handles stay null, CreateProc's PipeReadChild check
					// no-ops on null, and the child launches with no stdin — i.e. silently
					// back to the pre-fix behaviour, but with nothing in the log to say so.
					UE_LOG(LogNarshaMCPChat, Warning,
						TEXT("[NarshaMCP] Failed to create prewarm stdin pipe — skipping MCP daemon prewarm"));
					return;
				}
				uint32 PrewarmPID = 0;
				FProcHandle H = FPlatformProcess::CreateProc(
					*NarshaExe,
					*PrewarmArgs,
					false,   // bLaunchDetached — we manage lifetime
					true,    // bLaunchHidden
					true,    // bLaunchReallyHidden
					&PrewarmPID,
					0,
					*ProjectPath, // CWD = project dir so daemon finds config.ini
					nullptr,     // child stdout — discard (connect's own logs go to stderr)
					StdInRead);  // child stdin ← we write initialize via StdInWrite
				if (H.IsValid())
				{
					Self->MCPPrewarmProcess = H;
					Self->MCPPrewarmStdinWrite = StdInWrite;
					// Issue #29321: the child holds its own inherited copy of the read
					// end, so the parent's copy is redundant. Release it — StopMCPPrewarm
					// only closes MCPPrewarmStdinWrite, so keeping it would leak one
					// HANDLE for the editor's lifetime. This does NOT EOF the child:
					// that depends on the WRITE end, which we deliberately retain.
					FPlatformProcess::ClosePipe(StdInRead, nullptr);
					StdInRead = nullptr;
					// Measurement (Issue #8835 live-test): narshamcp's FIRST
					// tools/list call after daemon cold-boot takes ~4.5 s (PDB
					// symbol-index + cross-ref + call-graph lazy warmup).
					// Warm calls return in <500 ms. Claude CLI's -p mode can't
					// wait 4.5 s for MCP registration, so it gives up with
					// "pending_mcp_servers: [narshamcp]" and the tool call
					// never fires.
					//
					// Prewarm sends BOTH initialize AND tools/list so the
					// expensive first-time handshake happens BEFORE the user
					// types. When Claude CLI spawns its own `connect`, the
					// daemon is fully warm → tools/list returns fast → tools
					// register in the session → widget dispatch works.
					const FString InitRequest = TEXT(
						"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
						"\"params\":{\"protocolVersion\":\"2024-11-05\","
						"\"capabilities\":{},\"clientInfo\":"
						"{\"name\":\"ue-chat-panel-prewarm\",\"version\":\"1.0\"}}}\n"
						"{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n"
						"{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n");
					FPlatformProcess::WritePipe(StdInWrite, InitRequest);
					UE_LOG(LogNarshaMCPChat, Log,
						TEXT("[NarshaMCP] Prewarming MCP daemon via connect (PID: %u) — init+tools/list sent"),
						PrewarmPID);
				}
				else
				{
					FPlatformProcess::ClosePipe(StdInRead, StdInWrite);
					UE_LOG(LogNarshaMCPChat, Warning,
						TEXT("[NarshaMCP] Failed to spawn prewarm connect — Claude CLI will auto-start on first message (slow)"));
				}
			}
		});
	});

	return true; // keep ticking
}

// === Backend Delegate Binding (extracted from SpawnNativeSlateTab for SRP) ===

void UCUChatPanelSubsystem::BindBackendDelegates()
{
	if (!BackendClient)
	{
		return;
	}

	// Capture weak self-reference to prevent delegate firing after UObject teardown (Issue #5657)
	TWeakObjectPtr<UCUChatPanelSubsystem> WeakThis(this);

	// Multi-tab routing (Issue #9168): delegates update the TAB that issued the
	// request (StreamingTab), not the currently-active tab. This way responses
	// stream into the correct tab even when the user switches tabs mid-stream.
	// Helper captured into each lambda.
	auto ResolveTarget = [](UCUChatPanelSubsystem* Self) -> FCUChatTabState* {
		if (FCUChatTabState* Streaming = Self->ResolveStreamingTabState())
		{
			return Streaming;
		}
		// Fallback: single-tab or streaming tab already closed — update active.
		return Self->GetActiveTabState();
	};

	BackendClient->OnToken.BindLambda([WeakThis, ResolveTarget](const FString& Token) {
		UCUChatPanelSubsystem* Self = WeakThis.Get();
		if (!Self) return;
		FCUChatTabState* Target = ResolveTarget(Self);
		if (!Target) return;

		// Lazy-create bubble so a new bubble starts after any tool_result
		// closed the previous one — enables chronological interleave.
		if (!Target->CurrentAssistantBubble.IsValid() && Target->MessageList.IsValid())
		{
			Target->CurrentAssistantBubble = Target->MessageList->AddAssistantMessage();
		}
		if (Target->CurrentAssistantBubble.IsValid())
		{
			Target->CurrentAssistantBubble->AppendText(Token);
			if (Target->MessageList.IsValid())
			{
				Target->MessageList->ScrollToBottom();
			}
		}
		// Keep mirrors in sync if the streaming tab is also the active one.
		if (Target == Self->GetActiveTabState())
		{
			Self->CurrentAssistantBubble = Target->CurrentAssistantBubble;
		}
	});
	BackendClient->OnComplete.BindLambda([WeakThis, ResolveTarget](const FString& Full) {
		UCUChatPanelSubsystem* Self = WeakThis.Get();
		if (!Self) return;
		FCUChatTabState* Target = ResolveTarget(Self);
		if (!Target)
		{
			Self->SetSubmittingState(false);
			return;
		}

		if (Target->CurrentAssistantBubble.IsValid())
		{
			// Finalize the LAST segment only — earlier segments were already
			// closed on tool_result. Using Full here would duplicate them.
			FString Tail = Target->CurrentAssistantBubble->GetCurrentText();
			FString Linkified = FCUChatLinkifier::Linkify(Tail, Self->bActorLinksEnabled);
			Target->CurrentAssistantBubble->FinalizeMessage(Linkified);
		}
		Target->CurrentAssistantBubble.Reset();
		Self->SetSubmittingState(false);

		// Track assistant message in the STREAMING tab's session (Issue #9168)
		// Store ORIGINAL text (not linkified) to prevent double-linkification on reload
		Target->Session.Messages.Add(FCUChatMessage(TEXT("assistant"), Full));
		Target->Session.UpdatedAt = FDateTime::UtcNow();

		// Update session ID from backend if available
		if (Self->BackendClient)
		{
			FString ExtractedId = Self->BackendClient->GetLastExtractedSessionId();
			if (!ExtractedId.IsEmpty() && ExtractedId != Target->Session.SessionId)
			{
				Target->Session.SessionId = ExtractedId;
				// Only mirror LastSessionId/save if the streaming tab is active.
				if (Target == Self->GetActiveTabState())
				{
					Self->LastSessionId = ExtractedId;
					Self->SaveLastSessionId();
				}
			}
		}

		// Sync mirrors back from the streaming tab if it's the active one.
		if (Target == Self->GetActiveTabState())
		{
			Self->CurrentSession = Target->Session;
			Self->CurrentAssistantBubble = Target->CurrentAssistantBubble;
		}

		// Auto-save after each response — save the STREAMING tab's session, not
		// the (potentially different) active tab.
		Self->SaveSessionForTab(*Target);

		// Clear StreamingTab — the turn is complete.
		Self->StreamingTab.Reset();
	});
	BackendClient->OnError.BindLambda([WeakThis, ResolveTarget](const FString& Err) {
		UCUChatPanelSubsystem* Self = WeakThis.Get();
		if (!Self) return;
		UE_LOG(LogNarshaMCPChat, Error, TEXT("[NarshaMCP] Backend error: %s"), *Err);
		const FString ErrText = FString::Printf(TEXT("[Error] %s"), *Err);

		FCUChatTabState* Target = ResolveTarget(Self);
		if (Target)
		{
			if (Target->CurrentAssistantBubble.IsValid())
			{
				Target->CurrentAssistantBubble->FinalizeMessage(ErrText);
			}
			else if (Target->MessageList.IsValid())
			{
				// Null-safe: no open bubble (e.g., error after a tool_result closed it)
				Target->MessageList->AddSystemMessage(ErrText);
			}
			Target->CurrentAssistantBubble.Reset();
			if (Target == Self->GetActiveTabState())
			{
				Self->CurrentAssistantBubble.Reset();
			}
		}
		Self->SetSubmittingState(false);
		Self->StreamingTab.Reset();
	});
	// Issue #5656: Bind tool result delegate for native widget rendering
	BackendClient->OnToolResult.BindLambda([WeakThis, ResolveTarget](const FCUChatToolResult& Result) {
		UCUChatPanelSubsystem* Self = WeakThis.Get();
		if (!Self) return;
		// Flip readiness ONLY when a narshamcp tool (`ue_*`) responds — Claude
		// CLI's own `ToolSearch`/`Task`/`Skill` results arrive regardless of
		// narshamcp availability, so they must NOT collapse the banner.
		if (!Self->bMCPDaemonReady && Result.ToolName.StartsWith(TEXT("ue_")))
		{
			Self->bMCPDaemonReady = true;
			UE_LOG(LogNarshaMCPChat, Log,
				TEXT("[NarshaMCP] MCP daemon ready (via OnToolResult: %s) — banner hidden"),
				*Result.ToolName);
		}

		FCUChatTabState* Target = ResolveTarget(Self);
		if (!Target) return;

		// Close the current assistant bubble BEFORE adding the tool widget so
		// subsequent tokens open a fresh bubble AFTER the tool — producing
		// Claude-Code-style chronological interleave:
		//   [text][tool][text][tool][text]
		// instead of the old "all text at top, all tools stacked below".
		if (Target->CurrentAssistantBubble.IsValid())
		{
			FString Tail = Target->CurrentAssistantBubble->GetCurrentText();
			FString Linkified = FCUChatLinkifier::Linkify(Tail, Self->bActorLinksEnabled);
			Target->CurrentAssistantBubble->FinalizeMessage(Linkified);
			Target->CurrentAssistantBubble.Reset();
		}
		if (Target->MessageList.IsValid())
		{
			Target->MessageList->AddToolResult(Result);
		}
		if (Target == Self->GetActiveTabState())
		{
			Self->CurrentAssistantBubble.Reset();
		}
	});

	// Start MCP daemon readiness poller — banner above input shows "MCP 연결 중…"
	// until the daemon registers and responds. Idempotent: guard on ticker handle.
	if (!MCPStatusTickerHandle.IsValid())
	{
		MCPStatusTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UCUChatPanelSubsystem::TickMCPStatusProbe),
			2.0f);
		// Fire once immediately so ready state resolves quickly if already warm.
		TickMCPStatusProbe(0.0f);
	}
}

// === Issue #5652: SubmitNativeInput() — submit via native backend (Slate path) ===

void UCUChatPanelSubsystem::SubmitNativeInput()
{
	if (!InputWidget.IsValid() || !BackendClient || bIsSubmitting)
	{
		return;
	}

	FString Text = InputWidget->GetText().ToString().TrimStartAndEnd();
	if (Text.IsEmpty())
	{
		return;
	}

	// Phase 8: Prepend editor context if enabled
	if (bContextEnabled)
	{
		FString Context = GetEditorContext();
		if (!Context.IsEmpty())
		{
			Text = FString::Printf(TEXT("<context>\n%s\n</context>\n\n%s"), *Context, *Text);
		}
	}

	// Record StreamingTab so BackendClient responses route back to THIS tab
	// even if the user switches tabs mid-stream (Issue #9168).
	FCUChatTabState* Active = GetActiveTabState();
	if (Active)
	{
		StreamingTab = Active->DockTab;
	}

	// Add user message bubble
	FString DisplayText = InputWidget->GetText().ToString().TrimStartAndEnd();
	if (NativeMessageList.IsValid())
	{
		// Show original user text (without context prefix) in the bubble
		NativeMessageList->AddUserMessage(DisplayText);
		CurrentAssistantBubble = NativeMessageList->AddAssistantMessage();
	}

	// Track message in current session (Issue #5657) — mirror + active TabState
	CurrentSession.Messages.Add(FCUChatMessage(TEXT("user"), DisplayText));
	if (CurrentSession.Title.IsEmpty() && !DisplayText.IsEmpty())
	{
		CurrentSession.Title = DisplayText.Left(kMaxAutoTitleLength);
	}
	CurrentSession.UpdatedAt = FDateTime::UtcNow();
	if (Active)
	{
		// Mirror into the TabState so a mid-stream tab switch preserves the
		// user message even before OnComplete fires.
		Active->Session = CurrentSession;
		Active->CurrentAssistantBubble = CurrentAssistantBubble;
	}

	SetSubmittingState(true);
	InputWidget->SetText(FText::GetEmpty());
	BackendClient->SendMessage(Text, LastSessionId);
}



bool UCUChatPanelSubsystem::HandleAssetLink(const FString& URL)
{
	if (!URL.StartsWith(TEXT("ue-asset://")))
	{
		return false; // Normal URL — allow navigation
	}

	FString AssetPath = URL.Mid(11); // Remove "ue-asset://" prefix

	// Normalize path: "Content/..." → "/Game/...", strip ".uasset" extension
	if (AssetPath.StartsWith(TEXT("Content/")))
	{
		AssetPath = TEXT("/Game/") + AssetPath.Mid(8); // "Content/" (8 chars) → "/Game/"
	}
	if (!AssetPath.StartsWith(TEXT("/")))
	{
		AssetPath = TEXT("/") + AssetPath;
	}
	// Strip .uasset extension — UE asset paths don't include file extensions
	if (AssetPath.EndsWith(TEXT(".uasset")))
	{
		AssetPath = AssetPath.LeftChop(7);
	}

	// Look up asset via AssetRegistry by package name (not object path)
	// Package path = "/Game/Environments/Gameplay/BP_GameplayEffectPad"
	// GetAssetsByPackageName works with package paths; GetAssetByObjectPath needs ".AssetName" suffix
	IAssetRegistry& AR = IAssetRegistry::GetChecked();
	TArray<FAssetData> FoundAssets;
	AR.GetAssetsByPackageName(FName(*AssetPath), FoundAssets);

	if (FoundAssets.Num() > 0)
	{
		FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		CB.Get().SyncBrowserToAssets(FoundAssets, false, true); // bFocusContentBrowser=true

		// Show detail panel with asset properties (Chat Visualization)
		UObject* DetailObj = ResolveAssetForDetailView(AssetPath);
		if (DetailObj)
		{
			ShowDetailPanel(DetailObj, FoundAssets[0].AssetName.ToString());
		}
	}
	else
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Asset not found: %s"), *AssetPath);
		if (NativeMessageList.IsValid())
		{
			NativeMessageList->AddSystemMessage(
				FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
		}
	}

	return true; // Block navigation (don't let browser load ue-asset:// URL)
}



void UCUChatPanelSubsystem::SetSubmittingState(bool bSubmitting)
{
	bIsSubmitting = bSubmitting;
	// Widget state updates automatically via TAttribute lambdas:
	// - InputWidget/SendButton SetEnabled() lambdas poll !bIsSubmitting
	// - SendButton Text lambda polls bIsSubmitting for "..." vs "Send"
}

// === Phase 4: Editor Integration Methods (Issue #5478) ===

#if WITH_NARSHAMCP_LIVE_CODING
void UCUChatPanelSubsystem::TriggerCompile()
{
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("LiveCoding")))
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Live Coding module is not loaded."));
		return;
	}

	ILiveCodingModule& LiveCoding = FModuleManager::LoadModuleChecked<ILiveCodingModule>(TEXT("LiveCoding"));
	if (!LiveCoding.IsEnabledForSession())
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Live Coding is not enabled for this session."));
		return;
	}
	if (LiveCoding.IsCompiling())
	{
		UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Compile already in progress"));
		return;
	}

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Triggering Live Coding compile..."));
	LiveCoding.Compile();
}
#endif // WITH_NARSHAMCP_LIVE_CODING

void UCUChatPanelSubsystem::TriggerSaveAll()
{
	const bool bSaved = FEditorFileUtils::SaveDirtyPackages(
		/*bPromptUserToSave=*/ false,
		/*bSaveMapPackages=*/ true,
		/*bSaveContentPackages=*/ true);

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Save All: %s"), bSaved ? TEXT("saved") : TEXT("nothing to save"));
}

#if WITH_NARSHAMCP_LIVE_CODING
void UCUChatPanelSubsystem::OnLiveCodingPatchComplete()
{
	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Live Coding patch applied successfully."));
}
#endif // WITH_NARSHAMCP_LIVE_CODING





// === Phase 6: Source Link Navigation (Issue #5478) ===

bool UCUChatPanelSubsystem::HandleSourceLink(const FString& URL)
{
	FString FileLine = URL.Mid(12); // Remove "ue-source://"
	FString FilePath;
	int32 LineNumber = 1;

	// Parse "path:line" — find last colon, check if numeric
	int32 LastColonIdx;
	if (FileLine.FindLastChar(TEXT(':'), LastColonIdx) && LastColonIdx != INDEX_NONE)
	{
		FString LineStr = FileLine.Mid(LastColonIdx + 1);
		if (LineStr.IsNumeric())
		{
			LineNumber = FCString::Atoi(*LineStr);
			FilePath = FileLine.Left(LastColonIdx);
		}
		else
		{
			FilePath = FileLine;
		}
	}
	else
	{
		FilePath = FileLine;
	}

	FString FullPath = FPaths::ConvertRelativePathToFull(
		FPaths::Combine(FPaths::ProjectDir(), FilePath));

	if (FPaths::FileExists(FullPath))
	{
		FSourceCodeNavigation::OpenSourceFile(FullPath, LineNumber);
		UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Opening source: %s:%d"), *FullPath, LineNumber);
	}
	else
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Source not found: %s"), *FullPath);
		if (NativeMessageList.IsValid())
		{
			NativeMessageList->AddSystemMessage(
				FString::Printf(TEXT("Source file not found: %s"), *FilePath));
		}
	}

	return true; // Block navigation
}

// === Actor Link: Select & Focus Actor in Viewport (Issue #5478) ===

bool UCUChatPanelSubsystem::HandleActorLink(const FString& ActorName)
{
	if (!GEditor)
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] HandleActorLink: No GEditor"));
		return false;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] HandleActorLink: No editor world"));
		return false;
	}

	// Match by FName (GetName()) — guaranteed unique per level by UE
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetName() == ActorName)
		{
			FoundActor = Actor;
			break;
		}
	}

	if (FoundActor)
	{
		GEditor->SelectNone(/*bNoteSelectionChange=*/false, /*bDeselectBSPSurfs=*/true);
		GEditor->SelectActor(FoundActor, /*bInSelected=*/true, /*bNotify=*/true);
		GEditor->MoveViewportCamerasToActor(*FoundActor, /*bActiveViewportOnly=*/false);
		UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Focused on actor: %s (FName: %s, Class: %s)"),
			*FoundActor->GetActorLabel(), *FoundActor->GetName(), *FoundActor->GetClass()->GetName());

		// Show detail panel with actor properties (Chat Visualization)
		ShowDetailPanel(FoundActor, FoundActor->GetActorLabel());
	}
	else
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Actor not found by FName: %s"), *ActorName);
		if (NativeMessageList.IsValid())
		{
			NativeMessageList->AddSystemMessage(
				FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		}
	}

	return true;
}

// === Actor Link Toggle (Issue #5478) ===

void UCUChatPanelSubsystem::ToggleActorLinks()
{
	bActorLinksEnabled = !bActorLinksEnabled;
	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Actor links %s"), bActorLinksEnabled ? TEXT("enabled") : TEXT("disabled"));
}

// === Phase 8: Context Awareness (Issue #5478) ===

void UCUChatPanelSubsystem::ToggleContext()
{
	bContextEnabled = !bContextEnabled;
}

FString UCUChatPanelSubsystem::GetEditorContext() const
{
	TArray<FString> Parts;

	// 1. Current Level
	if (GEditor && GEditor->GetEditorWorldContext().World())
	{
		Parts.Add(FString::Printf(TEXT("Level: %s"), *GEditor->GetEditorWorldContext().World()->GetName()));
	}

	// 2. Selected Actors (max 5)
	if (GEditor)
	{
		int32 Count = 0;
		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It && Count < 5; ++It, ++Count)
		{
			if (AActor* A = Cast<AActor>(*It))
			{
				Parts.Add(FString::Printf(TEXT("Actor: %s (%s)"), *A->GetActorLabel(), *A->GetClass()->GetName()));
			}
		}
		// 2b. Hint for AI: mention actor names exactly so they become clickable
		if (Count > 0)
		{
			Parts.Add(TEXT("[Tip: When referring to the actors listed above, use their exact name — they will become clickable links in the chat that select and focus the actor in the viewport.]"));
		}
	}

	// 3. Selected Assets in Content Browser (max 5)
	FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FAssetData> Assets;
	CB.Get().GetSelectedAssets(Assets);
	for (int32 i = 0; i < FMath::Min(Assets.Num(), 5); i++)
	{
		Parts.Add(FString::Printf(TEXT("Asset: %s (%s)"),
			*Assets[i].AssetName.ToString(),
			*Assets[i].AssetClassPath.GetAssetName().ToString()));
	}

	if (Parts.IsEmpty())
	{
		return FString();
	}
	return FString::Join(Parts, TEXT("\n"));
}

// === Session Management (Issue #5657) ===

FString UCUChatPanelSubsystem::GetSessionsDirectory() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NarshaMCP"), TEXT("Sessions"));
}

FString UCUChatPanelSubsystem::GetSessionPersistencePath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NarshaMCP"), TEXT("last_session.txt"));
}

void UCUChatPanelSubsystem::SaveCurrentSession()
{
	if (CurrentSession.SessionId.IsEmpty() || CurrentSession.Messages.Num() == 0)
	{
		return;
	}

	CurrentSession.UpdatedAt = FDateTime::UtcNow();

	FString SessionsDir = GetSessionsDirectory();
	if (!FPaths::DirectoryExists(SessionsDir))
	{
		IFileManager::Get().MakeDirectory(*SessionsDir, true);
	}

	FString FilePath = FPaths::Combine(SessionsDir, CurrentSession.SessionId + TEXT(".json"));

	TSharedPtr<FJsonObject> JsonObj = CurrentSession.ToJson();
	FString JsonString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);

	if (!FFileHelper::SaveStringToFile(JsonString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNarshaMCPChat, Error, TEXT("[NarshaMCP] Failed to save session file: %s"), *FilePath);
		return;
	}
	InvalidateSessionListCache();

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Session saved: %s (%d messages)"),
		*CurrentSession.SessionId, CurrentSession.Messages.Num());
}

void UCUChatPanelSubsystem::LoadSession(const FString& SessionId)
{
	FString FilePath = FPaths::Combine(GetSessionsDirectory(), SessionId + TEXT(".json"));
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Session file not found: %s"), *FilePath);
		return;
	}

	FString JsonString;
	if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
	{
		UE_LOG(LogNarshaMCPChat, Error, TEXT("[NarshaMCP] Failed to read session file: %s"), *FilePath);
		return;
	}

	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogNarshaMCPChat, Error, TEXT("[NarshaMCP] Failed to parse session JSON: %s"), *FilePath);
		return;
	}

	CurrentSession = FCUChatSession::FromJson(JsonObj);
	LastSessionId = CurrentSession.SessionId;
	SaveLastSessionId();

	// Mirror into the active TabState so multi-tab (#9168) sees the load.
	if (FCUChatTabState* Active = GetActiveTabState())
	{
		Active->Session = CurrentSession;
	}

	// Populate message list widget
	if (NativeMessageList.IsValid())
	{
		NativeMessageList->ClearMessages();
		for (const FCUChatMessage& Msg : CurrentSession.Messages)
		{
			if (Msg.Role == TEXT("user"))
			{
				NativeMessageList->AddUserMessage(Msg.Content);
			}
			else if (Msg.Role == TEXT("assistant"))
			{
				TSharedRef<SCUChatMessageBubble> Bubble = NativeMessageList->AddAssistantMessage();
				// Auto-linkify actor names when loading history (Chat Visualization)
				FString Linkified = FCUChatLinkifier::Linkify(Msg.Content, bActorLinksEnabled);
				Bubble->FinalizeMessage(Linkified);
			}
			else if (Msg.Role == TEXT("system"))
			{
				NativeMessageList->AddSystemMessage(Msg.Content);
			}
		}
	}

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Session loaded: %s (%d messages)"),
		*SessionId, CurrentSession.Messages.Num());
}

TArray<FCUChatSession> UCUChatPanelSubsystem::GetSessionList() const
{
	// Mutable cache is only accessed from Game Thread (UI-driven calls) — no locking needed
	check(IsInGameThread());

	if (!bSessionListCacheDirty)
	{
		return CachedSessionList;
	}

	CachedSessionList.Empty();
	FString SessionsDir = GetSessionsDirectory();

	if (!FPaths::DirectoryExists(SessionsDir))
	{
		// Directory may be created later — leave cache dirty so next call retries
		return CachedSessionList;
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(SessionsDir, TEXT("*.json")), true, false);

	CachedSessionList.Reserve(Files.Num());
	for (const FString& FileName : Files)
	{
		FString FilePath = FPaths::Combine(SessionsDir, FileName);
		FString JsonString;
		if (FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			TSharedPtr<FJsonObject> JsonObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
			if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
			{
				CachedSessionList.Emplace(FCUChatSession::FromJsonMetadataOnly(JsonObj));
			}
		}
	}

	// Sort by UpdatedAt descending (most recent first)
	CachedSessionList.Sort([](const FCUChatSession& A, const FCUChatSession& B)
	{
		return A.UpdatedAt > B.UpdatedAt;
	});

	bSessionListCacheDirty = false;
	return CachedSessionList;
}

void UCUChatPanelSubsystem::InvalidateSessionListCache()
{
	check(IsInGameThread());
	bSessionListCacheDirty = true;
}

void UCUChatPanelSubsystem::DeleteSession(const FString& SessionId)
{
	FString FilePath = FPaths::Combine(GetSessionsDirectory(), SessionId + TEXT(".json"));
	if (FPaths::FileExists(FilePath))
	{
		IFileManager::Get().Delete(*FilePath);
		InvalidateSessionListCache();
		UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Session deleted: %s"), *SessionId);
	}
}

void UCUChatPanelSubsystem::MigrateLegacySession()
{
	FString LegacyPath = GetSessionPersistencePath();
	if (!FPaths::FileExists(LegacyPath))
	{
		return;
	}

	FString LegacyId;
	FFileHelper::LoadFileToString(LegacyId, *LegacyPath);
	LegacyId.TrimStartAndEndInline();

	if (!LegacyId.IsEmpty())
	{
		// Create a minimal session JSON with the legacy ID
		FString SessionsDir = GetSessionsDirectory();
		FString SessionPath = FPaths::Combine(SessionsDir, LegacyId + TEXT(".json"));

		if (!FPaths::FileExists(SessionPath))
		{
			if (!FPaths::DirectoryExists(SessionsDir))
			{
				IFileManager::Get().MakeDirectory(*SessionsDir, true);
			}

			FCUChatSession LegacySession;
			LegacySession.SessionId = LegacyId;
			LegacySession.Title = TEXT("Migrated Session");

			TSharedPtr<FJsonObject> JsonObj = LegacySession.ToJson();
			FString JsonString;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonString);
			FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
			if (!FFileHelper::SaveStringToFile(JsonString, *SessionPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				UE_LOG(LogNarshaMCPChat, Error, TEXT("[NarshaMCP] Failed to save migrated session: %s"), *SessionPath);
			}
		}

		InvalidateSessionListCache();

		// Rename legacy file to .bak (delete existing .bak first to avoid Move failure)
		FString BakPath = LegacyPath + TEXT(".bak");
		if (FPaths::FileExists(BakPath))
		{
			if (!IFileManager::Get().Delete(*BakPath))
			{
				UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Failed to delete existing .bak: %s"), *BakPath);
			}
		}
		if (IFileManager::Get().Move(*BakPath, *LegacyPath))
		{
			UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Migrated legacy session: %s"), *LegacyId);
		}
		else
		{
			UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Failed to rename legacy file to .bak: %s"), *LegacyPath);
		}
	}
}

void UCUChatPanelSubsystem::SaveLastSessionId()
{
	FString FilePath = GetSessionPersistencePath();
	FString DirPath = FPaths::GetPath(FilePath);
	if (!FPaths::DirectoryExists(DirPath))
	{
		IFileManager::Get().MakeDirectory(*DirPath, true);
	}
	if (!FFileHelper::SaveStringToFile(LastSessionId, *FilePath))
	{
		UE_LOG(LogNarshaMCPChat, Error, TEXT("[NarshaMCP] Failed to save last session ID to: %s"), *FilePath);
	}
}

void UCUChatPanelSubsystem::LoadLastSessionId()
{
	FString FilePath = GetSessionPersistencePath();
	if (FPaths::FileExists(FilePath))
	{
		FFileHelper::LoadFileToString(LastSessionId, *FilePath);
		LastSessionId.TrimStartAndEndInline();
	}
}

void UCUChatPanelSubsystem::ShowHistoryView()
{
	// Save current session before switching
	SaveCurrentSession();

	if (ViewSwitcher.IsValid())
	{
		// Refresh history panel with session data
		if (HistoryPanel.IsValid())
		{
			HistoryPanel->RefreshSessionList(GetSessionList());
		}
		ViewSwitcher->SetActiveWidgetIndex(1);
	}
}

void UCUChatPanelSubsystem::ToggleHistoryView()
{
	if (ViewSwitcher.IsValid())
	{
		int32 Current = ViewSwitcher->GetActiveWidgetIndex();
		if (Current == 0)
		{
			// Switch to history — save current session first
			SaveCurrentSession();
			if (HistoryPanel.IsValid())
			{
				HistoryPanel->RefreshSessionList(GetSessionList());
			}
			ViewSwitcher->SetActiveWidgetIndex(1);
		}
		else
		{
			// Switch back to chat
			ViewSwitcher->SetActiveWidgetIndex(0);
		}
	}
}

FSlateColor UCUChatPanelSubsystem::GetHistoryButtonSlateColor() const
{
	bool bHistoryActive = ViewSwitcher.IsValid() && ViewSwitcher->GetActiveWidgetIndex() == 1;
	return bHistoryActive
		? FSlateColor(FLinearColor(0.3f, 0.7f, 1.0f))
		: FSlateColor(FSlateColor::UseForeground());
}

void UCUChatPanelSubsystem::RegisterToolbarButton()
{
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu(
		"LevelEditor.LevelEditorToolBar.PlayToolBar");
	if (!ToolbarMenu)
	{
		return;
	}

	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("NarshaMCPAI");
	Section.AddEntry(FToolMenuEntry::InitToolBarButton(
		"OpenNarshaMCPAI",
		FUIAction(FExecuteAction::CreateLambda([this]() {
			FGlobalTabmanager::Get()->TryInvokeTab(AIChatTabId);
		})),
		NSLOCTEXT("NarshaMCP", "AIToolbarButton", "AI Chat"),
		NSLOCTEXT("NarshaMCP", "AIToolbarTooltip", "Open NarshaMCP AI Chat (Ctrl+Shift+P)"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Outliner")
	));
}

void UCUChatPanelSubsystem::StartNewChat()
{
	// Issue #9168: 'New Chat' must spawn a NEW tab rather than clearing the
	// current one. Old tabs keep their session + messages so users can switch
	// back mid-workflow. Implementation:
	//   1. Persist active tab's session to disk (unchanged behavior for persistence)
	//   2. Clear LastSessionId so the new tab doesn't auto-resume an old thread
	//   3. Build a new tab via BuildNewChatTab (subsystem members now point at the new tab)
	//   4. Insert next to the live AIChatTabId tab via InsertNewDocumentTab

	SaveCurrentSession();

	// Issue #23127: headless / -nullrhi still initializes Slate, so a
	// FSlateApplication::IsInitialized() check does NOT protect this path. Tab
	// churn makes FTabManager::RequestSavePersistentLayout arm a 5-second
	// FTSTicker; when it fires, SDockingArea::GatherPersistentLayout reaches
	// FGenericWindow::GetRestoredDimensions, which is a checkf on any platform
	// that cannot render — killing the whole automation suite mid-run. The
	// document tab is a visual-only side effect, so steps 3-4 are skipped when
	// rendering is off while the state transition (step 2) still happens.
	//
	// The sibling guard in CUAnnotationRenderers.cpp pairs this with
	// `&& FSlateApplication::IsInitialized()`; that term is redundant here.
	// ShouldCreateSubsystem() (Issue #10756) already refuses to create this
	// subsystem under IsRunningCommandlet(), which is the only context in this
	// module where Slate can be uninitialized — so every live StartNewChat()
	// runs with Slate up, and CanEverRender() is the whole question.
	if (!FApp::CanEverRender())
	{
		// Postcondition (Issue #28657): after this branch, ActiveTabIndex indexes a
		// real TabStates entry whose Session mirrors the freshly reset
		// CurrentSession — the same relation BuildNewChatTab() establishes at
		// :513-514 on the rendering path. The entry is deliberately widget-less:
		// DockTab, MessageList and CurrentAssistantBubble stay at their
		// default-constructed null values because no SDockTab is built here. That
		// divergence is declared, not accidental, and is asserted by
		// NarshaMCP.AIChatPanel.Session.StartNewChatHeadlessTabStateMirror.
		//
		// The ambient TabStates count is environment-dependent and must not be
		// assumed either way: measured 0 on this host's UE 5.7 lane and 1 on its
		// UE 5.8 lane, same commit and same suite. SpawnAITab() is registered as a
		// spawner callback at :193-194 and is not gated on CanEverRender(), but
		// registering a spawner is not running it — nothing invokes the tab
		// headless on the 5.7 lane, so the count there is genuinely zero. Read the
		// AmbientCount below rather than predicting it.
		//
		// Cost of the widget-less entry, accepted: OnChatTabClosed() finds its target by
		// `DockTab.Pin() == ClosedTab` (:564), and a default TWeakPtr always pins to null,
		// so this entry can never be matched and is never removed — ActiveTabIndex stays on
		// it until a later SpawnAITab()/BuildNewChatTab() re-targets the index. Bounded in
		// practice: the only production caller of StartNewChat() is the "+ New" toolbar
		// button (:835), which needs a rendered toolbar, so headless this branch is reached
		// by automation alone.
		//
		// Scope: this postcondition covers StartNewChat() only. ResetSessionStateForNewChat()
		// is also reachable on its own (TestStartNewChatLogic), and that path does no
		// bookkeeping — an orphan produced there is out of scope here.
		//
		// Still NOT done, and load-bearing for the Issue #23127 guard: no SDockTab is
		// built or inserted, so FTabManager::RequestSavePersistentLayout is never armed
		// and the deferred GetRestoredDimensions fatal cannot fire; and NextTabCounter
		// is not advanced, since it labels visible tabs only.
		const int32 AmbientCount = TabStates.Num();
		SnapshotActiveTabState();
		ResetSessionStateForNewChat();

		// Mirror BuildNewChatTab():507/511/513/514, omitting :508 (DockTab = Tab)
		// and :515 (++NextTabCounter). :509/:510 are omitted too and would be
		// no-ops regardless — ResetSessionStateForNewChat():486-487 just null'd
		// both mirrors. Do NOT call SyncMirrorsToActiveTab() here:
		// its last line (:448) would repopulate the LastSessionId that
		// ResetSessionStateForNewChat() just cleared.
		TSharedRef<FCUChatTabState> NewState = MakeShared<FCUChatTabState>();
		NewState->Session = CurrentSession;
		TabStates.Add(NewState);
		ActiveTabIndex = TabStates.Num() - 1;

		// Verbose, not Warning: this fires on every headless New Chat and UE
		// automation can promote log warnings into test failures.
		UE_LOG(LogNarshaMCPChat, Verbose,
			TEXT("Headless New Chat: %d ambient tab state(s); appended DockTab-less state at index %d"),
			AmbientCount, ActiveTabIndex);
		UE_LOG(LogNarshaMCPChat, Verbose,
			TEXT("Skipping New Chat document tab (no render / headless)"));
		return;
	}

	TSharedRef<SDockTab> NewTab = BuildNewChatTab();

	// Dock the new tab next to any live AIChatTabId tab; PreferLiveTab uses the
	// existing AIChatTab's dock area as an anchor so "AI Chat 2" appears beside
	// "NarshaMCP AI" rather than at a random corner.
	FGlobalTabmanager::Get()->InsertNewDocumentTab(
		AIChatTabId,
		FTabManager::ESearchPreference::PreferLiveTab,
		NewTab);

	// Force the new tab into the ChatView so it's immediately usable (the freshly
	// built ViewSwitcher defaults to index 0, but cover it defensively).
	if (ViewSwitcher.IsValid())
	{
		ViewSwitcher->SetActiveWidgetIndex(0);
	}
}

#if WITH_AUTOMATION_WORKER
FCUChatSession& UCUChatPanelSubsystem::TestGetCurrentSession()
{
	return CurrentSession;
}
#endif

void UCUChatPanelSubsystem::OpenTerminal()
{
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

#if PLATFORM_WINDOWS
	// Get cmd.exe full path from COMSPEC env var (bare "cmd.exe" may fail in CreateProc)
	FString ComSpec = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
	if (ComSpec.IsEmpty())
	{
		ComSpec = TEXT("C:\\Windows\\System32\\cmd.exe");
	}

	// Auto-run claude CLI — if not authenticated, CLI auto-opens browser for OAuth login
	FString Cmd;
	if (!CachedClaudePath.IsEmpty())
	{
		Cmd = FString::Printf(TEXT("/K cd /d \"%s\" && \"%s\""), *ProjectDir, *CachedClaudePath);
	}
	else
	{
		Cmd = FString::Printf(TEXT("/K cd /d \"%s\""), *ProjectDir);
	}

	FPlatformProcess::CreateProc(
		*ComSpec,
		*Cmd,
		false,   // bLaunchDetached=false — MUST be false so Windows creates a console window
		false,   // bLaunchHidden=false — visible window
		false,   // bLaunchReallyHidden=false
		nullptr, 0, nullptr, nullptr
	);
#elif PLATFORM_MAC
	// Issue #9230: best-effort terminal launch on macOS via `open -a Terminal.app <ProjectDir>`.
	// We do NOT auto-execute the Claude CLI on macOS since `open` does not chain commands;
	// users can `claude` from the spawned shell.
	const FString OpenArgs = FString::Printf(TEXT("-a Terminal.app \"%s\""), *ProjectDir);
	FProcHandle Handle = FPlatformProcess::CreateProc(
		TEXT("/usr/bin/open"),
		*OpenArgs,
		true,    // detached
		false, false,
		nullptr, 0, nullptr, nullptr);
	if (Handle.IsValid())
	{
		FPlatformProcess::CloseProc(Handle);
	}
	else
	{
		UE_LOG(LogNarshaMCPChat, Warning,
			TEXT("[NarshaMCP] OpenTerminal: failed to spawn Terminal.app via /usr/bin/open"));
	}
#elif PLATFORM_LINUX
	// Issue #9230: best-effort terminal launch on Linux. xdg-open is not a terminal
	// emulator, so we try common terminal binaries via `command -v`. If none are
	// available, log a warning instead of crashing — the chat panel still works.
	const TCHAR* CandidateTerminals[] = {
		TEXT("gnome-terminal"),
		TEXT("konsole"),
		TEXT("xfce4-terminal"),
		TEXT("xterm")
	};
	FString FoundTerm;
	for (const TCHAR* Cand : CandidateTerminals)
	{
		FoundTerm = NarshaMCP::Platform::FindOnPath(Cand);
		if (!FoundTerm.IsEmpty()) { break; }
	}
	if (!FoundTerm.IsEmpty())
	{
		// gnome-terminal / xfce4-terminal: --working-directory=<dir>
		// konsole: --workdir <dir>
		// xterm: -e 'cd <dir>; exec $SHELL'
		// Issue #9230 W-2: basename match avoids false positives like
		// `mlterm-xterm` matching the xterm branch via Contains().
		const FString TermBase = FPaths::GetCleanFilename(FoundTerm);
		FString Args;
		if (TermBase == TEXT("xterm"))
		{
			Args = FString::Printf(TEXT("-e \"cd '%s'; exec $SHELL\""), *ProjectDir);
		}
		else if (TermBase == TEXT("konsole"))
		{
			Args = FString::Printf(TEXT("--workdir \"%s\""), *ProjectDir);
		}
		else
		{
			Args = FString::Printf(TEXT("--working-directory=\"%s\""), *ProjectDir);
		}
		FProcHandle Handle = FPlatformProcess::CreateProc(
			*FoundTerm, *Args, true, false, false, nullptr, 0, nullptr, nullptr);
		if (Handle.IsValid())
		{
			FPlatformProcess::CloseProc(Handle);
		}
		else
		{
			UE_LOG(LogNarshaMCPChat, Warning,
				TEXT("[NarshaMCP] OpenTerminal: spawn failed for %s"), *FoundTerm);
		}
	}
	else
	{
		UE_LOG(LogNarshaMCPChat, Warning,
			TEXT("[NarshaMCP] OpenTerminal: no terminal emulator found on PATH "
			     "(tried gnome-terminal, konsole, xfce4-terminal, xterm). "
			     "Install one or open a terminal manually."));
	}
#else
	// Defensive: any non-Win/Mac/Linux platform reaches here. The chat panel
	// keeps working — only the "open terminal" UX is unavailable. Loud-but-
	// non-fatal log so an operator on a future platform (FreeBSD, Console)
	// can diagnose immediately instead of mistaking it for a silent no-op.
	UE_LOG(LogNarshaMCPChat, Warning,
		TEXT("[NarshaMCP] OpenTerminal: unsupported platform "
		     "(only Win64 / Mac / Linux are wired up). Open a terminal manually."));
#endif
}

void UCUChatPanelSubsystem::RunCliHealthCheck()
{
	if (CachedClaudePath.IsEmpty())
	{
		if (StatusBarText.IsValid())
		{
			StatusBarText->SetText(FText::FromString(
				TEXT("Claude CLI not found. Install Claude Code (native recommended): https://code.claude.com/docs/en/setup")));
			StatusBarText->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.3f, 0.3f, 1.0f)));
		}
		return;
	}

	FString ClaudePath = CachedClaudePath;
	// Case-insensitive: Windows may report .EXE, .Exe, etc. (Issue #8831)
	const bool bIsNativeBinary = ClaudePath.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase);

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, ClaudePath, bIsNativeBinary]()
	{
		// Helper: run claude CLI with args, handling native vs shell dispatch (Issue #8831,
		// Issue #9230: shell dispatch is platform-aware via NarshaMCP::Platform::RunShellCommand).
		auto ExecClaude = [&ClaudePath, bIsNativeBinary](const FString& CliArgs, int32& OutCode, FString& OutStdout, FString& OutStderr)
		{
			if (bIsNativeBinary)
			{
				FPlatformProcess::ExecProcess(*ClaudePath, *CliArgs, &OutCode, &OutStdout, &OutStderr);
			}
			else
			{
				// Non-native (e.g. claude.cmd npm wrapper on Windows). Wrap the
				// invocation in the platform shell so the wrapper extension is honored.
				const FString ShellLine = FString::Printf(TEXT("\"%s\" %s"), *ClaudePath, *CliArgs);
				NarshaMCP::Platform::RunShellCommand(ShellLine, OutCode, OutStdout, OutStderr);
			}
		};

		// Step 1: Check CLI installed via --version
		int32 VersionCode = -1;
		FString VersionOut, VersionErr;
		ExecClaude(TEXT("--version"), VersionCode, VersionOut, VersionErr);

		if (VersionCode != 0 || VersionOut.IsEmpty())
		{
			AsyncTask(ENamedThreads::GameThread, [this]()
			{
				if (StatusBarText.IsValid())
				{
					StatusBarText->SetText(FText::FromString(
						TEXT("Claude CLI found but not working. Try reinstalling.")));
					StatusBarText->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.3f, 0.3f, 1.0f)));
				}
			});
			return;
		}

		FString Version = VersionOut.TrimStartAndEnd();

		// Step 2: Check auth by attempting a minimal --print call
		int32 AuthCode = -1;
		FString AuthOut, AuthErr;
		ExecClaude(TEXT("--print -p \"ping\""), AuthCode, AuthOut, AuthErr);

		bool bAuthenticated = (AuthCode == 0 && !AuthOut.IsEmpty());

		FString StatusMsg;
		FLinearColor StatusColor;
		if (bAuthenticated)
		{
			StatusMsg = FString::Printf(TEXT("Claude CLI %s — Logged in"), *Version);
			StatusColor = FLinearColor(0.3f, 0.75f, 0.3f, 1.0f);  // green
		}
		else
		{
			StatusMsg = FString::Printf(TEXT("Claude CLI %s — Not logged in. Click Terminal to authenticate."), *Version);
			StatusColor = FLinearColor(0.9f, 0.7f, 0.2f, 1.0f);  // yellow/orange
		}

		AsyncTask(ENamedThreads::GameThread, [this, StatusMsg, StatusColor]()
		{
			if (StatusBarText.IsValid())
			{
				StatusBarText->SetText(FText::FromString(StatusMsg));
				StatusBarText->SetColorAndOpacity(FSlateColor(StatusColor));
			}
		});
	});
}

void FCUChatInputProcessor::Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor)
{
}

bool FCUChatInputProcessor::HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event)
{
	// Ctrl+Shift+P (without Alt — Alt+Ctrl+Shift+P is ShaderRecompile)
	if (Event.GetKey() == EKeys::P && Event.IsControlDown() && Event.IsShiftDown() && !Event.IsAltDown())
	{
		// Guard: only invoke tab if spawner is registered (tab disabled when show_in_editor = false)
		if (FGlobalTabmanager::Get()->HasTabSpawner(UCUChatPanelSubsystem::AIChatTabId))
		{
			FGlobalTabmanager::Get()->TryInvokeTab(UCUChatPanelSubsystem::AIChatTabId);
		}
		return true; // consumed
	}
	return false;
}

void FCUChatInputProcessor::HandleAppActivationChanged(bool bIsActive)
{
	if (bIsActive)
	{
		return;
	}

	// When the application loses focus (e.g. Alt+Tab), Windows does not deliver
	// WM_SYSKEYUP for the Alt key to the deactivated window. This leaves Slate's
	// internal KeyStateMap with Alt registered as "key down" indefinitely.
	//
	// NOTE: Do NOT guard with GetModifierKeys().IsAltDown() here.
	// GetModifierKeys() reads from the Windows platform layer, which may have
	// already cleared Alt's state by the time this delegate fires (WM_ACTIVATEAPP
	// is processed before the deactivation delegate). That guard would silently
	// skip the fix even when Slate's KeyStateMap still has Alt stuck.
	// Synthesize unconditionally — ProcessKeyUpEvent is a no-op for keys not in KeyStateMap.
	// Issue #10756: defensive — this delegate is bound only when Slate was up, but if
	// Slate has shut down by the time the activation event fires, skip silently.
	if (!FSlateApplication::IsInitialized())
	{
		return;
	}
	FSlateApplication& SlateApp = FSlateApplication::Get();
	const FModifierKeysState NoMods;

	SlateApp.ProcessKeyUpEvent(FKeyEvent(EKeys::LeftAlt, NoMods, 0, false, 0, 0));
	SlateApp.ProcessKeyUpEvent(FKeyEvent(EKeys::RightAlt, NoMods, 0, false, 0, 0));
}


// ===================================================================
// Annotation ↔ Chat Bridge (Issue #5821)
// ===================================================================

void UCUChatPanelSubsystem::HandleAnnotationDiscuss(
	const FString& AnnotationId,
	const FString& AnnotationText,
	const FString& AssetPath)
{
	// 1. Open the AI Chat tab
	FGlobalTabmanager::Get()->TryInvokeTab(AIChatTabId);

	// 2. Parse target from annotation ID for context
	FString ParsedAsset;
	FString Target;
	FCUAnnotationData::ParseId(AnnotationId, ParsedAsset, Target);

	const FString AssetBaseName = FPaths::GetBaseFilename(AssetPath);

	// 3. Pre-fill the input widget with annotation context
	const FString ContextText = FString::Printf(
		TEXT("Regarding annotation on %s::%s:\n%s\n\nPlease explain further."),
		*AssetBaseName, *Target, *AnnotationText);

	if (InputWidget.IsValid())
	{
		InputWidget->SetText(FText::FromString(ContextText));
	}

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Annotation discuss: %s (from %s)"),
		*AnnotationId, *AssetPath);
}

// ============================================================================
// Issue #8842 (Phase 10-3) — Recent Editor Actions context injection
// ============================================================================

void UCUChatPanelSubsystem::OnEditorEvent(const FString& EventType, const FString& EventData)
{
	// Compose a single-line entry: "EventType: EventData" (truncate huge JSON)
	constexpr int32 MaxEntryChars = 256;
	FString Entry = EventType + TEXT(": ") + EventData;
	if (Entry.Len() > MaxEntryChars)
	{
		Entry = Entry.Left(MaxEntryChars - 3) + TEXT("...");
	}

	FScopeLock Lock(&RecentActionsLock);
	RecentEditorActions.Add(MoveTemp(Entry));
	while (RecentEditorActions.Num() > MaxRecentActions)
	{
		RecentEditorActions.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

FString UCUChatPanelSubsystem::BuildRecentActionsContext() const
{
	if (!bInjectRecentActions)
	{
		return FString();
	}

	FScopeLock Lock(&RecentActionsLock);
	if (RecentEditorActions.Num() == 0)
	{
		return FString();
	}

	FString Out = TEXT("[Recent Editor Actions]\n");
	for (const FString& Entry : RecentEditorActions)
	{
		Out += Entry;
		Out += TEXT("\n");
	}
	Out += TEXT("[End Recent Actions]");
	return Out;
}

bool UCUChatPanelSubsystem::HandleAnnotateLink(const FString& URL)
{
	// Pattern: ue-annotate://AssetPath::Target?category=X&text=Y
	static const FString Prefix = FCUAnnotationData::AnnotateLinkPrefix;

	if (!URL.StartsWith(Prefix))
	{
		return false;
	}

	// Strip the scheme prefix
	FString Body = URL.Mid(Prefix.Len());

	// Extract query parameters
	FString PathPart;
	FString QueryPart;
	if (Body.Split(TEXT("?"), &PathPart, &QueryPart))
	{
		// PathPart = "AssetPath::Target"
	}
	else
	{
		PathPart = Body;
	}

	// Parse annotation ID parts
	FString AssetPath;
	FString Target;
	if (!FCUAnnotationData::ParseId(PathPart, AssetPath, Target))
	{
		UE_LOG(LogNarshaMCPChat, Warning, TEXT("[NarshaMCP] Invalid ue-annotate:// URL: %s"), *URL);
		return false;
	}

	// Parse query parameters for category and text
	FString Category = TEXT("analysis");
	FString AnnotationText;
	if (!QueryPart.IsEmpty())
	{
		TArray<FString> Params;
		QueryPart.ParseIntoArray(Params, TEXT("&"));
		for (const FString& Param : Params)
		{
			FString Key;
			FString Value;
			if (Param.Split(TEXT("="), &Key, &Value))
			{
				if (Key == TEXT("category"))
				{
					Category = Value;
				}
				else if (Key == TEXT("text"))
				{
					AnnotationText = FGenericPlatformHttp::UrlDecode(Value);
				}
			}
		}
	}

	// Build full annotation ID
	const FString AnnotationId = FCUAnnotationData::MakeId(AssetPath, Target);

	// Broadcast to annotation subsystem via bridge delegate
	GetOnCUChatAnnotationRequested().ExecuteIfBound(
		AnnotationId, AnnotationText, Category, CurrentSession.SessionId);

	UE_LOG(LogNarshaMCPChat, Log, TEXT("[NarshaMCP] Chat → Annotation: %s (category: %s)"),
		*AnnotationId, *Category);

	return true;
}

bool UCUChatPanelSubsystem::Tick(float DeltaTime)
{
	if (BackendClient)
	{
		BackendClient->Poll();
	}
	return true;
}

// === Chat Visualization: Detail Side Panel ===

void UCUChatPanelSubsystem::ShowDetailPanel(UObject* Object, const FString& DisplayName)
{
	if (!DetailPanel.IsValid() || !DetailPanelBox.IsValid() || !Object)
	{
		return;
	}

	DetailPanel->SetObject(Object, DisplayName);

	if (!bDetailPanelVisible)
	{
		DetailPanelBox->SetVisibility(EVisibility::Visible);
		bDetailPanelVisible = true;
	}
}

void UCUChatPanelSubsystem::HideDetailPanel()
{
	if (!DetailPanelBox.IsValid())
	{
		return;
	}

	DetailPanelBox->SetVisibility(EVisibility::Collapsed);
	bDetailPanelVisible = false;

	if (DetailPanel.IsValid())
	{
		DetailPanel->ClearObject();
	}
}

UObject* UCUChatPanelSubsystem::ResolveAssetForDetailView(const FString& AssetPath)
{
	IAssetRegistry& AR = IAssetRegistry::GetChecked();
	TArray<FAssetData> Assets;
	AR.GetAssetsByPackageName(FName(*AssetPath), Assets);

	if (Assets.Num() == 0)
	{
		return nullptr;
	}

	UObject* LoadedObj = Assets[0].GetAsset();
	if (!LoadedObj)
	{
		return nullptr;
	}

	// Blueprint → CDO (Class Default Object) for UPROPERTY exposure
	if (UBlueprint* BP = Cast<UBlueprint>(LoadedObj))
	{
		if (BP->GeneratedClass)
		{
			return BP->GeneratedClass->GetDefaultObject();
		}
	}

	return LoadedObj;
}
