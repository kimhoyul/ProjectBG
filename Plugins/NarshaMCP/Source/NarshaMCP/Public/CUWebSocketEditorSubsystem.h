// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #1437: WebSocket Editor Subsystem (UEditorSubsystem migration from FCUWebSocketServer Singleton)

#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "IWebSocketServer.h"
#include "INetworkingWebSocket.h"
#include "SecurityLimits.h"
#include "Containers/Ticker.h"  // Issue #1632: Ticker for WebSocket polling
#include "Containers/Queue.h"    // Issue #9394: TQueue for per-frame dispatch budget
#include "Misc/OutputDeviceRedirector.h"  // Issue #4843: FOutputDevice for log capture
#include "Framework/Application/IInputProcessor.h"
#include "CUPIESnapshot.h"  // Issue #8783: PIE snapshot data model
#include "CUWebSocketEditorSubsystem.generated.h"

// Forward declare for IInputProcessor
class UCUWebSocketEditorSubsystem;
class FWidgetBlueprintEditor;
class FCUWidgetDesignerCaptureGate;

/** Single recorded input event for PIE record/replay (Issue #6378) */
struct FCURecordedInputEvent
{
	double TimestampMs;
	FString InputType;
	FKey Key;
	FVector2D MousePosition;
	bool bShift;
	bool bCtrl;
	bool bAlt;
};

/**
 * IInputProcessor for capturing keyboard/mouse input during PIE recording (Issue #6388)
 * Follows the same pattern as FCUChatInputProcessor in CUChatPanelSubsystem.h
 * All handlers return false so PIE still processes the input normally.
 */
class FCUInputRecordingProcessor : public IInputProcessor
{
public:
	FCUInputRecordingProcessor(UCUWebSocketEditorSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override;
	virtual bool HandleKeyUpEvent(FSlateApplication& SlateApp, const FKeyEvent& Event) override;
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override;
	virtual bool HandleMouseButtonUpEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override;
	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& Event) override;

private:
	UCUWebSocketEditorSubsystem* Owner;
};

/**
 * Issue #9394: One WebSocket message awaiting dispatch on the Editor main thread.
 *
 * Pre-#9394 `OnMessageReceived` parsed the JSON and immediately called the
 * matching Handle*() on the network receive callback — which runs on the
 * Editor main thread inside `Server->Tick()`. During a 20-parallel MCP burst
 * this saturated the main thread for 30+ seconds (Day 2 trace
 * `C:/temp/NarshaMCP-perf/Day2/T1/T1_cond2_cond3.utrace`), tripping the
 * client 30 s timeout even though the Editor was still working through the
 * backlog.
 *
 * The fix enqueues parsed messages into `UCUWebSocketEditorSubsystem::PendingMessages`
 * from `OnMessageReceived` and drains up to `n.WebSocket.MaxPerFrame` per
 * Tick. `Socket` is a raw pointer because it is owned by the server's client
 * list; the dequeue path guards every dispatch with `Clients.Contains(Socket)`
 * and `OnClientDisconnected` purges matching entries, so a disconnected
 * client never observes its queued dispatch.
 */
struct FCUPendingMessage
{
	TSharedPtr<FJsonObject> Json;
	INetworkingWebSocket* Socket = nullptr;  // raw, validated at dequeue via Clients.Contains
	double EnqueueTimeSeconds = 0.0;          // FPlatformTime::Seconds() at enqueue — queue-latency metric
};

/** Single captured log entry (Issue #4843) */
struct FCULogEntry
{
	FDateTime Timestamp;
	ELogVerbosity::Type Verbosity;
	FName Category;
	FString Message;
};

/**
 * FOutputDevice subclass that captures UE Output Log into a ring buffer (Issue #4843)
 * Registered with GLog to intercept all log output for get_error_log queries.
 */
class FCULogCapture : public FOutputDevice
{
public:
	static constexpr int32 DefaultLogBufferSize = 2000;
	explicit FCULogCapture(int32 InMaxEntries = DefaultLogBufferSize)
		: MaxEntries(InMaxEntries)
	{
	}

	virtual ~FCULogCapture() override = default;

	// FOutputDevice interface
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
	{
		// Filter out self-generated WebSocket log spam to prevent ring buffer overflow (Issue #4843).
		// During run_automation_tests polling, each WS call generates 3+ log entries (LogTemp from other modules)
		// (connect/message/disconnect), which can fill the 500-entry buffer in ~150 polls,
		// pushing out the LogAutomationController entries we actually need to detect.
		if (Category == TEXT("LogTemp"))
		{
			FString Msg(V);
			if (Msg.Contains(TEXT("[WebSocket]"), ESearchCase::IgnoreCase) ||
				Msg.Contains(TEXT("WebSocket:"), ESearchCase::IgnoreCase))
			{
				return;
			}
		}

		FScopeLock Lock(&EntriesLock);

		FCULogEntry Entry;
		Entry.Timestamp = FDateTime::UtcNow();
		Entry.Verbosity = Verbosity;
		Entry.Category = Category;
		Entry.Message = FString(V);

		if (Entries.Num() >= MaxEntries)
		{
			Entries.RemoveAt(0, 1, EAllowShrinking::No);
		}
		Entries.Add(MoveTemp(Entry));
	}

	/** Get entries matching filters (newest first) */
	TArray<FCULogEntry> GetEntries(int32 Count, const FString& SeverityFilter, const FString& CategoryFilter) const
	{
		FScopeLock Lock(&EntriesLock);

		TArray<FCULogEntry> Result;
		// Iterate newest-first
		for (int32 i = Entries.Num() - 1; i >= 0 && Result.Num() < Count; --i)
		{
			const FCULogEntry& Entry = Entries[i];

			// Severity filter
			if (!SeverityFilter.IsEmpty() && SeverityFilter != TEXT("all"))
			{
				if (SeverityFilter == TEXT("error") && Entry.Verbosity != ELogVerbosity::Error && Entry.Verbosity != ELogVerbosity::Fatal)
				{
					continue;
				}
				if (SeverityFilter == TEXT("warning") && Entry.Verbosity != ELogVerbosity::Warning)
				{
					continue;
				}
			}

			// Category filter
			if (!CategoryFilter.IsEmpty() && !Entry.Category.ToString().Contains(CategoryFilter))
			{
				continue;
			}

			Result.Add(Entry);
		}
		return Result;
	}

	/** Clear all captured entries */
	void Clear()
	{
		FScopeLock Lock(&EntriesLock);
		Entries.Empty();
	}

private:
	TArray<FCULogEntry> Entries;
	int32 MaxEntries;
	mutable FCriticalSection EntriesLock;
};

/**
 * Issue #8840 (Phase 10-1): in-process broadcast delegate for AI Chat Panel.
 *
 * Fired by BroadcastEvent() BEFORE the WebSocket fan-out, so in-process
 * subscribers (e.g. SCUChatTimelineBar in #8841, CUChatPanelSubsystem in
 * #8842) receive editor events even when no external WebSocket client
 * is connected.
 *
 * Signature mirrors BroadcastEvent: (EventType, EventData).
 *   EventType — short identifier ("asset_updated", "compile_finished", ...)
 *   EventData — JSON payload string
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(
	FOnCUWebSocketEventBroadcast,
	const FString& /* EventType */,
	const FString& /* EventData */);

/**
 * WebSocket Editor Subsystem for NarshaMCP (Issue #1437)
 *
 * Provides bidirectional real-time communication between Unreal Editor and MCP server.
 * Replaces FCUWebSocketServer singleton with Editor lifecycle management.
 *
 * Benefits over Singleton:
 * - Automatic lifecycle management (Initialize/Deinitialize)
 * - GC integration (no memory leaks)
 * - Testable via dependency injection
 * - Editor-aware (only active when Editor is running)
 *
 * Performance:
 * - Event latency: <1ms
 * - Port: 30011 (default)
 * - Protocol: WebSocket (RFC 6455)
 *
 * Usage:
 *   UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
 *   WS->Start(30011);
 *   WS->BroadcastEvent("asset_updated", EventData);
 *
 * In-process subscription (Issue #8840):
 *   WS->OnEventBroadcast.AddUObject(this, &UMyClass::HandleEditorEvent);
 */
UCLASS()
class NARSHAMCP_API UCUWebSocketEditorSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

	// Issue #6388: Allow input recording processor to access recording state
	friend class FCUInputRecordingProcessor;
	friend FCURecordedInputEvent MakeRecordedEvent(UCUWebSocketEditorSubsystem*, const FString&, FKey, FVector2D, bool, bool, bool);
	friend bool EnsurePIERunning(UCUWebSocketEditorSubsystem*, INetworkingWebSocket*, const FString&, const FString&);
	// Issue #9895: EnsurePIERunningSinglePIE remains file-scope `static`.
	// EnsurePIERunningMultiPIE has external linkage (Issue #27012) so CUPieMeasurementMode.cpp
	// can share the PIE_NOT_RUNNING envelope; it only needs SendResponse() (public).
	// ResolveClientWorldOrReject DOES need friend access because it routes the private
	// ResolveClientWorld() — same rationale as the comment at L649-651 on the NetEmulation helper.
	friend UWorld* ResolveClientWorldOrReject(UCUWebSocketEditorSubsystem*, INetworkingWebSocket*,
		const TSharedPtr<FJsonObject>&, const FString&, const TCHAR*, int32&);

public:
	//~ Begin UEditorSubsystem Interface
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// Issue #10756: skip subsystem creation under cook/headless commandlets where
	// FSlateApplication is uninitialized (was crashing Development packaging).
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End UEditorSubsystem Interface

	/**
	 * Start WebSocket server on specified port
	 *
	 * @param Port - Port to listen on (default: 30011)
	 * @return True if server started successfully
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	bool Start(int32 Port = 30011);

	/**
	 * Start the WebSocket server, auto-selecting a free port for concurrent
	 * multi-editor isolation (Issue #16334).
	 *
	 * Resolution order (Issue #16334 / #25250):
	 *   1. Explicit override — `-NarshaMCPWSPort=N` command line
	 *   2. `UECODEGEN_RC_WS_PORT` environment variable
	 *   3. `UCUConnectionSettings::WebSocketPort` (EditorPerProjectUserSettings; when >0)
	 *   4. Otherwise scan upward from the default base port (kDefaultWsPort)
	 *      across kWsRangeSpan and bind the FIRST free port.
	 *   Explicit sources (1–3) bind exactly N (deterministic; fail loud if taken, #25249).
	 *
	 * Lets multiple Editors (multi-worktree / multi-project) each bind their own
	 * WS port instead of all colliding on 30011; the MCP daemon then
	 * identity-matches the right Editor via get_editor_info. A single Editor still
	 * lands on 30011 (no behavior change).
	 *
	 * @return True if the server bound a port and started.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	bool StartAuto();

	/**
	 * Issue #25249: stop (if running) then restart the WebSocket server.
	 *
	 * @param Port - When >0, bind exactly that port via Start(Port). When 0
	 *               (default), reuse StartAuto() resolution (explicit override
	 *               or free-port scan). Console: `NarshaMCP.RestartWS [port]`.
	 * @return True if the server is running after the restart attempt.
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	bool RestartWS(int32 Port = 0);

	/**
	 * Stop WebSocket server
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	void Stop();

	/**
	 * Check if server is running
	 *
	 * @return True if server is active
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	bool IsRunning() const;

	/** Issue #16334: the port the WS server actually bound (StartAuto() may scan). */
	uint16 GetServerPort() const { return ServerPort; }

	/**
	 * Broadcast event to all connected clients
	 *
	 * @param EventType - Event type (e.g., "asset_updated", "compile_finished")
	 * @param EventData - JSON string with event data
	 *
	 * Example:
	 *   Subsystem->BroadcastEvent("asset_updated", "{\"path\":\"/Game/BP_Player\"}");
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	void BroadcastEvent(const FString& EventType, const FString& EventData);

	/**
	 * Issue #8840 (Phase 10-1): in-process broadcast delegate.
	 *
	 * Fires inside BroadcastEvent() before WebSocket fan-out, so in-process
	 * subscribers receive events regardless of WebSocket client count.
	 *
	 * Subscribe with:
	 *   OnEventBroadcast.AddUObject(this, &UMyClass::HandleEvent);
	 *   OnEventBroadcast.AddSP(SharedThis(this), &SMyWidget::HandleEvent);
	 *
	 * Unsubscribe by holding the FDelegateHandle returned from Add* and
	 * calling OnEventBroadcast.Remove(Handle).
	 */
	FOnCUWebSocketEventBroadcast OnEventBroadcast;

	/**
	 * Get number of connected clients
	 *
	 * @return Number of active WebSocket connections
	 */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|WebSocket")
	int32 GetClientCount() const;

	/**
	 * Python execution result structure
	 */
	struct FCUPythonExecutionResult
	{
		bool bSuccess;
		FString Stdout;
		FString Stderr;
		FString Exception;
		FString ErrorCode;
		FString Hint;
		double ExecutionTimeMs;
	};

	/**
	 * Execute Python code in Unreal Editor
	 *
	 * @param Code - Python code to execute
	 * @param TimeoutMs - Execution timeout in milliseconds
	 * @return Execution result with stdout/stderr/exception
	 */
	FCUPythonExecutionResult ExecutePythonCode(const FString& Code, int32 TimeoutMs);

private:
	// WebSocket server instance (TUniquePtr for UE 5.6+)
	TUniquePtr<IWebSocketServer> Server;

	// Connected clients (raw pointers - server owns them)
	TArray<INetworkingWebSocket*> Clients;

	// Server port
	uint16 ServerPort = 30011;

	// Is server running
	bool bIsRunning = false;

	// Issue #16334: default WS base port + scan span for StartAuto()'s free-port
	// scan. kWsRangeSpan = 14 → scans 30011..=30025 (15 ports inclusive), matching
	// the daemon's WS scan range (port_scanner ws_range = (30011, 30025)).
	static constexpr int32 kDefaultWsPort = 30011;
	static constexpr int32 kWsRangeSpan = 14;

	/**
	 * Issue #16334: attempt to create + Init the WS server on a single `Port`.
	 * On success sets ServerPort, starts the ticker, sets bIsRunning, returns true.
	 * On failure resets any half-created server and returns false (an occupied port
	 * during a range scan is expected — logged at Verbose). Caller MUST guarantee
	 * !bIsRunning.
	 */
	bool TryBind(int32 Port);

	/**
	 * Issue #16334 / #25250: resolve the WS base port. Sets bOutExplicit=true when
	 * an explicit override is present (`-NarshaMCPWSPort=` cmdline,
	 * `UECODEGEN_RC_WS_PORT` env, or UCUConnectionSettings::WebSocketPort > 0) —
	 * StartAuto then binds exactly the returned port; otherwise returns
	 * kDefaultWsPort as the scan start.
	 */
	static int32 ResolveWsBasePort(bool& bOutExplicit);

	/**
	 * Issue #25249: surface an explicit-port bind failure so the Editor is not a
	 * silent "ghost" (up, but no WS / no editor_ports advertisement).
	 * Emits LogError + FMessageLog + FNotificationInfo (toast) when Slate is up.
	 * Headless/commandlet paths log only.
	 */
	void NotifyExplicitWsBindFailure(int32 Port);

	/** Issue #25249: console handler for `NarshaMCP.RestartWS [port]`. */
	void HandleRestartWSCommand(const TArray<FString>& Args);

	/**
	 * Issue #16334: probe whether `Port` is free to bind on 127.0.0.1.
	 *
	 * UE's WebSocketNetworking `Server->Init()` binds with SO_REUSEADDR, so it
	 * does NOT fail when another Editor already holds the port — which would make
	 * StartAuto()'s free-port scan never advance (two Editors silently share
	 * 30011). A plain (non-reuseaddr) FSocket Bind() correctly reports the port as
	 * unavailable, so we call this BEFORE Init() and skip occupied ports. Returns
	 * true if the port looks bindable (or if probing is impossible — fail-open so
	 * a probe failure never blocks the legacy single-Editor path).
	 */
	static bool IsPortAvailable(int32 Port);

	/**
	 * Issue #16695: advertise this Editor's bound WS port so the daemon discovers
	 * its own project's Editor without relying on the shared convention default
	 * (30011). Writes `~/.narshamcp/editor_ports/<pid>.json` with
	 * {ws_port, project_root, pid, engine_version, ts}. `project_root`
	 * mirrors HandleGetEditorInfo's normalization contract so the daemon's
	 * `ownership::project_root_matches` compares equal.
	 *
	 * Best-effort: a directory-create or write failure is logged at Warning and
	 * swallowed — it must NEVER block or fail the WS bind. Called after a
	 * successful bind in TryBind; an Editor that rebinds a new port overwrites the
	 * same <pid>.json. (Follow-up #16334.)
	 */
	void WriteEditorPortAdvertisement();

	/**
	 * Issue #16695: remove this Editor's `~/.narshamcp/editor_ports/<pid>.json`
	 * advertisement (called on Deinitialize). Best-effort; a missing file or delete
	 * failure is logged at Verbose and ignored.
	 */
	void RemoveEditorPortAdvertisement();

	/**
	 * Issue #16695: absolute path to this Editor's advertisement file
	 * (`<USERPROFILE>/.narshamcp/editor_ports/<pid>.json`). Uses the `USERPROFILE`
	 * env var so the base matches the daemon's Rust `dirs::home_dir()`
	 * (FOLDERID_Profile == %USERPROFILE% on Windows). Returns an empty string if
	 * USERPROFILE is unavailable (advertisement then silently skips).
	 */
	static FString GetEditorPortAdvertisementPath();

	// Issue #4843: Output Log capture for get_error_log
	TUniquePtr<FCULogCapture> LogCapture;

	// Issue #1632: Ticker for WebSocket polling
	FTSTicker::FDelegateHandle TickerHandle;

	// ============================================
	// Issue #9394: Per-frame dispatch budget
	// ============================================

	// Pending messages parsed by OnMessageReceived, drained by Tick() up to
	// kMaxPerFrame entries per frame. SPSC because both enqueue
	// (OnMessageReceived → Server::Tick → FTSTicker on main thread) and
	// dequeue (Tick on main thread) run on the Editor game thread.
	TQueue<FCUPendingMessage, EQueueMode::Spsc> PendingMessages;

	// Best-effort depth counter — main-thread only, no atomic needed because
	// SPSC + single-threaded access. Used for the overflow cap and for the
	// burst-test assertions. `int32` wraps around billions of frames away.
	int32 PendingQueueDepth = 0;

	// Issue #9394 (review W-1): reentrancy guard for DrainPendingMessages.
	// A handler can pump Slate (screenshot capture, PIE start) which may
	// recursively invoke FTSTicker::Tick → our Tick() → another drain pass.
	// Without this guard the inner drain would eat into the outer drain's
	// Budget bookkeeping and FTSTicker's own same-thread reentry check
	// would fire. Main-thread only so a plain bool is sufficient.
	bool bDrainingPending = false;

public:
	// Issue #9394: dispatch-budget constants exposed publicly so the CVar
	// default, the docs (WEBSOCKET_FRAME_BUDGET.md), and the automation test
	// all reference the same source. See the named comments below for the
	// rationale behind each value.

	// Hard ceiling on `PendingMessages` depth. Beyond this we drop + error
	// the incoming message rather than let the queue grow without bound
	// (risk table: queue-latency unbounded → OOM). 256 is well above the
	// worst observed burst (~20 messages/burst, Day 2 trace) while still
	// bounding memory — each FCUPendingMessage is ~32 bytes + the TSharedPtr
	// JSON payload (~1 KB typical), so the cap = ~256 KB worst case.
	static constexpr int32 kPendingQueueDepthCap = 256;

	// Clamp bounds for the `n.WebSocket.MaxPerFrame` CVar so a user can't
	// accidentally set it to 0 (queue never drains) or 99999 (no budget at
	// all). Values outside [kMinPerFrame, kMaxPerFrameHardCap] are clamped
	// at read time in Tick.
	static constexpr int32 kMinPerFrame = 1;
	static constexpr int32 kMaxPerFrameHardCap = 64;

	// Default drain budget. Chosen empirically from Day 2 trace: each handler
	// costs 1-500 ms; at 60 Hz we have ~16 ms/frame, so 4 cheap handlers fit
	// inside a frame's worst-case budget (Ping/GetStatus ≈ 0.5-2 ms each).
	// Expensive handlers (play_in_editor ≈ 500 ms) still exceed the frame
	// budget, but the budget caps *additional* pile-up behind them.
	static constexpr int32 kDefaultMaxPerFrame = 4;

private:

	// Issue #6378: PIE input recording state
	TArray<FCURecordedInputEvent> RecordedInputEvents;
	double RecordingStartTime = 0.0;
	double MaxRecordingDuration = 60.0;
	bool bIsRecordingInput = false;
	TSharedPtr<FCUInputRecordingProcessor> InputRecordingProcessor;
	bool Tick(float DeltaTime);

	// Issue #7022: Replay state tracking for auto-cleanup
	bool bIsReplayingInput = false;

	// Issue #6938: Console command recording — NarshaMCP.Record <seconds>|start|stop [TestName]
	bool bAutoStopRecording = false;
	FString PendingTestName;
	void HandleRecordCommand(const TArray<FString>& Args);
	void StartRecordingDirect(double MaxDuration, bool bAutoStop);
	void StopAndExportRecording(const FString& TestName = TEXT(""));

	// ============================================
	// Security: Rate Limiting with Token Bucket (Issue #1402 C-2)
	// ============================================

	/** Token bucket state for rate limiting */
	struct FCURateLimitState
	{
		FDateTime LastRequestTime;
		double TokenBucket;                  // Current tokens available
		int32 ViolationCount;                // Track violations

		FCURateLimitState()
			: LastRequestTime(FDateTime::UtcNow())
			, TokenBucket(SecurityLimits::TokenBucketCapacity)
			, ViolationCount(0)
		{
		}
	};

	TMap<INetworkingWebSocket*, FCURateLimitState> ClientRateLimitState;
	mutable FCriticalSection RateLimitLock;  // Thread safety

	// WebSocket event handlers
	void OnClientConnected(INetworkingWebSocket* Socket);
	void OnClientDisconnected(INetworkingWebSocket* Socket);
	void OnMessageReceived(void* Data, int32 Size, INetworkingWebSocket* Socket);

	// Issue #9394: Route a parsed JSON message to its typed handler.
	// Returns true if the `type` field matched a known handler (even if the
	// handler itself then rejects the payload). Returns false only when the
	// type is unrecognized — OnMessageReceived uses that to echo raw bytes
	// back to the client for debug/compat, preserving pre-#9394 behavior.
	bool DispatchMessage(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& JsonObject);

	// Issue #9394: Per-frame drain helper. Called from Tick() after
	// Server->Tick() populates PendingMessages. Separated so it can be
	// invoked directly by tests via DrainPendingMessagesOnceForTesting.
	void DrainPendingMessages();

	// Python execution handler
	void HandleExecutePython(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	/** Issue #27130: after-snapshot transform diff for HandleExecutePython. Keys are weak
	 *  because the executed Python can replace the editor world and free every captured
	 *  actor. Returns the count skipped for a no-longer-resolving key. */
	int32 WriteActorMovedEntries(
		const TMap<TWeakObjectPtr<class AActor>, FTransform>& BeforeTransforms,
		const FString& TransLogPath) const;

	// Issue #1632: Screenshot capture handler (native C++ implementation)
	void HandleCaptureScreenshot(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: Ping handler
	void HandlePing(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: Get editor status handler
	void HandleGetEditorStatus(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #9074: Authoritative editor info (project_root, project_name, engine_version,
	// is_pie_active). Distinct from get_editor_status which omits project_root — this
	// lets ue_check_health distinguish the server's configured project from the Editor's.
	void HandleGetEditorInfo(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: Execute console command handler
	void HandleExecuteConsoleCommand(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P1 - Save asset handler
	void HandleSaveAsset(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P1 - Save all dirty packages handler
	void HandleSaveAll(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P1 - Compile blueprints handler
	void HandleCompileBlueprints(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P1 - Focus actor handler (camera move to actor)
	void HandleFocusActor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P1 - Select actors handler
	void HandleSelectActors(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Spawn actor from Blueprint
	void HandleSpawnActor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Set actor transform (location/rotation/scale)
	void HandleSetTransform(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Delete actor
	void HandleDeleteActor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Get actor properties
	void HandleGetActorProperties(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Set actor property
	void HandleSetActorProperty(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #16566: Get a named property by reflection from an actor or its component
	void HandleGetComponentProperty(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #27010: Get a named reflected property from any loaded UObject by path
	// (non-AActor PIE instances included). FindObject only — no actor iterator.
	void HandleGetObjectProperties(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #28227: write twin of get_object_properties. Delegates to
	// UCURemoteLibrary::SetObjectPropertyByPath, which refuses a silent array clear
	// (bAllowClear) and rolls back a partial array apply.
	void HandleSetObjectProperties(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #28227: create an Instanced subobject into a UObject's array property.
	// The engine's own adders for this shape are C++ templates, so reflection cannot
	// reach them; UCURemoteLibrary::AddInstancedSubobjectByPath reproduces the idiom.
	void HandleAddInstancedSubobject(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #28227: shared envelope for the two handlers above — parses the library's
	// JSON and injects type + request_id so error text has a single source.
	void SendLibraryResult(INetworkingWebSocket* Socket, const FString& LibraryJson,
		const FString& ResultType, const FString& RequestId);

	// Issue #1921: P2 - Get level actors by class
	void HandleGetLevelActorsByClass(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Open asset editor
	void HandleOpenAsset(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #9833: Resolve asset path -> top-level OS window handle (HWND on Windows)
	// via UE Slate: AssetEditorSubsystem -> IAssetEditorInstance -> SWindow -> GetOSWindowHandle.
	// Eliminates Win32 EnumWindows title-substring ambiguity in get_preview helpers
	// (manage_widget / manage_niagara / manage_material).
	void HandleGetEditorWindowHandle(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #26157: Capture a specific Widget Blueprint's Designer without closing
	// or foreground-capturing an unrelated editor window.
	void HandleCaptureWidgetDesigner(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Create new asset
	void HandleCreateAsset(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #16566: Create a content folder (on-disk persistent + Content Browser visible)
	void HandleCreateFolder(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Set material parameter
	void HandleSetMaterialParam(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P2 - Control viewport camera
	void HandleControlCamera(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P3 - Start Play-In-Editor session
	void HandlePlayInEditor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P3 - Stop Play-In-Editor session
	void HandleStopPlay(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #27011: Pause/resume the PIE PlayWorld clock (absolute value, not a toggle)
	void HandleSetGamePaused(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #27011: Set PIE PlayWorld global time dilation (0 < dilation <= 1)
	void HandleSetGlobalTimeDilation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P3 - Capture Blueprint editor screenshot
	void HandleCaptureBlueprintEditor(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Focus specific node in Blueprint editor (open + jump to node)
	void HandleFocusBlueprintNode(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #1632: P3 - Get error log entries
	void HandleGetErrorLog(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #16682: Live Coding authoritative compile — captures ELiveCodingCompileResult
	// via ILiveCodingModule::Compile(WaitForCompletion, &Result). Defers the blocking
	// compile onto a later game-thread tick (AsyncTask) and responds by request_id.
	void HandleLiveCodingCompile(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	// Issue #16682: Live Coding live status probe (is_compiling / is_enabled / has_started).
	// Synchronous, cheap — used by the Rust capability/online probe before the authoritative compile.
	void HandleLiveCodingStatus(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Tool Maturity INI handlers
	void HandleGetToolMaturity(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleSetToolMaturity(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #3796: Niagara VFX parameter control handlers
	void HandleSetNiagaraParameter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleActivateNiagara(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleGetNiagaraParameters(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	// Issue #3795 Phase 7B: Add NiagaraComponent to actor
	void HandleAddNiagaraEmitter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #21944 / NIA-N2: Add an emitter handle to a NiagaraSystem asset (authoring).
	// Distinct from HandleAddNiagaraEmitter which attaches a UNiagaraComponent to an actor.
	void HandleAddSystemEmitter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #21945 / NIA-N3: GPU write set
	void HandleSetNiagaraSimTarget(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleSetNiagaraFixedBounds(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleSetNiagaraEmitterEnabled(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #6844: Set User Parameter defaults on Niagara System asset
	void HandleSetNiagaraAssetParameter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #21950 / NIA-N11: Create a new User.* ExposedParameter on a NiagaraSystem asset
	// (optionally records requested module-input bind targets for follow-up attach/promote).
	void HandleAddNiagaraUserParameter(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #27351: attach a Dynamic Input UNiagaraScript tree to a module input
	// (WS message attach_niagara_dynamic_input). Uses exported NiagaraEditor
	// FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput only.
	void HandleAttachNiagaraDynamicInput(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #16555: set material/mesh on a Niagara renderer via dynamic enumeration of
	// the versioned emitter's renderer list (handles set_niagara_renderer_material + _mesh).
	void HandleSetNiagaraRenderer(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #16957: add/configure a Niagara event handler on the 5.8 versioned emitter
	// (FVersionedNiagaraEmitterData::EventHandlerScriptProps) — replaces the broken
	// Python event_handler_script_props path. Handles add_niagara_event_handler +
	// configure_niagara_event.
	void HandleNiagaraEventHandler(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #17149 [4c]: add a Generate{Location|Death|Collision}Event module to the SOURCE
	// emitter's Particle Update script stack (FNiagaraStackGraphUtilities::AddScriptModuleToStack),
	// enable persistent IDs, recompile + save, and read back the canonical generated event NAME
	// from FVersionedNiagaraEmitterData::UpdateScriptProps.EventGenerators[0].ID. Pairs with the
	// [4b] event-HANDLER add. Handles add_niagara_event_generator.
	void HandleNiagaraEventGenerator(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #17149 [4c]: capture a Niagara SimCache from a ticking component in the PIE world and
	// report per-emitter + system instance counts per frame. Requires PIE (multi-PIE client_id
	// aware); reuses a placed actor by actor_name or spawns a transient ANiagaraActor. Handles
	// niagara_capture_simcache.
	void HandleNiagaraCaptureSimCache(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #3797: Collision property handlers
	void HandleSetCollision(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #4272: Material HLSL extraction via WebSocket (requires Editor running)
	void HandleGetMaterialHLSL(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #16324: Live material node graph (names + DefaultValue + name-based edges) via WebSocket
	void HandleGetMaterialGraph(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #4843: Automation test listing via FAutomationTestFramework
	void HandleListAutomationTests(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #5647: Annotate Blueprint node with AI analysis result
	void HandleAnnotateBlueprintNode(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #5816: Batch annotate multiple Blueprint nodes
	void HandleAnnotateBlueprintNodes(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #5818: Annotate Sequencer track with AI analysis result
	void HandleAnnotateSequencerTrack(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #5818: Batch annotate multiple Sequencer tracks
	void HandleAnnotateSequencerTracks(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #5818: Capture AI annotation overlay window screenshot
	void HandleCaptureAnnotationGroup(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #6378: PIE input injection + record/replay + viewport capture
	void HandleInjectInput(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleInjectInputSequence(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleStartInputRecording(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleStopInputRecording(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleReplayInputRecording(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleCapturePIEViewport(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #7914: Enhanced Input InputAction injection via UEnhancedInputLocalPlayerSubsystem
	void HandleInjectInputAction(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #7914: Shared InputAction dispatch helper — used by HandleInjectInputAction and
	// HandleInjectInputSequence when a sequence event has input_type=="action".
	// Returns true on success; on failure fills OutError with a human-readable reason.
	// Issue #9098 Tier 1: when EventObj["duration_ms"] > 0, switches to sustained injection
	// via UEnhancedInputLocalPlayerSubsystem::StartContinuousInputInjectionForAction +
	// scheduled Stop timer. 0 or absent = existing single-tick behavior (#7914).
	// Issue #9098 Tier 2: when EventObj["fallback"] == "raw_key", resolves the action's
	// first applied-IMC binding to an FKey and dispatches Slate key_down + scheduled key_up
	// (auto-release after duration_ms, default 50ms tap). Optional OutResolvedKey /
	// OutGamepadOnly / OutScheduled out-params populate response fields for the caller.
	bool DispatchInputActionEvent(const TSharedPtr<FJsonObject>& EventObj, FString& OutError,
		FString* OutResolvedKey = nullptr, bool* OutGamepadOnly = nullptr, bool* OutScheduled = nullptr);

	// Issue #9098 Tier 1: Set of action_path strings currently being sustained-injected.
	// Used to clean up via StopContinuousInputInjectionForAction on PIE end / Deinitialize.
	// Populated by DispatchInputActionEvent when duration_ms > 0; entries remove themselves
	// when the scheduled-stop timer fires normally.
	TSet<FString> ContinuouslyInjectedActionPaths;

	// Issue #9098 Tier 1: FEditorDelegates::EndPIE subscription handle for cleanup.
	FDelegateHandle EndPIEHandle;

	// Issue #9098 Tier 1: Called when PIE ends (any cause — normal stop, crash, level change).
	// Iterates ContinuouslyInjectedActionPaths and calls StopContinuousInputInjectionForAction
	// for each, then clears the set. Double-safety with world-scoped timer auto-invalidation.
	void OnEndPIE(bool bIsSimulating);

	// Issue #9098 Tier 1: Explicit helper — stops every tracked continuous injection.
	// Safe to call when PlayWorld is null (silent no-op). Called by OnEndPIE + Deinitialize.
	void StopAllContinuousInjections();

	// Issue #26843: FWorldDelegates::OnPIEEnded subscription handle.
	//
	// Deliberately NOT hung off the existing EndPIEHandle above. FEditorDelegates::EndPIE has four
	// broadcast sites and three of them are PIE *start-failure* paths; PlayLevel.cpp:3019 fires it
	// and then returns nullptr WITHOUT tearing down, so sibling PIE instances stay alive. Purging
	// there would break a running session. FWorldDelegates::OnPIEEnded is broadcast from exactly one
	// site (PlayLevel.cpp:307, inside EndPlayMap) and EndPlayMap is always a full teardown of every
	// PIE context (PlayLevel.cpp:327-336) — so every firing is a real end-of-session.
	FDelegateHandle PIEEndedHandle;

	// Issue #26843: Called when a PIE session actually ends. Broadcasts
	// FEditorSupportDelegates::PrepareToCleanseEditorObject for every PIE-owned object, which
	// FPythonScriptPlugin already routes into FPyReferenceCollector::PurgeUnrealObjectReferences
	// (PythonScriptPlugin.cpp:1259 -> :2178), nulling the UObject pointer inside every live Python
	// wrapper that references it. Without this the persistent Python console namespace keeps the
	// PIE world rooted and the next play_in_editor dies with "World Memory Leaks".
	void OnPIEEnded(class UGameInstance* EndingGameInstance);

	// Issue #26843: Single source of truth for the purge target set. Collects every UWorld whose
	// outermost package carries PKG_PlayInEditor (the same criterion the engine's own leak scan uses
	// at PlayLevel.cpp:507-522, so it covers the persistent world, streaming sublevel worlds and
	// sibling multi-instance PIE worlds), plus the PIE world contexts' GameInstances — garbage
	// GameInstances are collected as leaks separately (PlayLevel.cpp:526-532) and are outered to
	// UEditorEngine, so a world-only purge never reaches them.
	//
	// Collection completes BEFORE any broadcast: handlers are third-party code and GetWorldContexts()
	// hands back the live WorldList (Engine.h:3608), which a handler could mutate mid-iteration.
	void CollectPurgeTargets(TArray<TWeakObjectPtr<UObject>>& OutTargets) const;

	// Issue #26843: Broadcast PrepareToCleanseEditorObject for one target. Returns true if a
	// broadcast was actually issued (false for a null/stale target). Single choke point.
	bool PurgeEditorReferencesToObject(UObject* Target);

	// Issue #26997: FWorldDelegates::OnPostWorldCleanup subscription handle.
	//
	// OnPIEEnded (above) fires only at end of SESSION. When a game mode ends a round with
	// ProcessServerTravel/SeamlessTravel it swaps the WORLD inside the same PIE session, so that
	// signal never comes and the old world stays rooted by Python -> "Old World not cleaned up by
	// GC!" -> AsyncGameplayMessageSystem asserts on OuterWorld.IsValid() and the editor dies.
	//
	// OnPostWorldCleanup is the world-axis signal. It is broadcast from exactly one site
	// (World.cpp:6482, inside UWorld::CleanupWorldInternal) and the public entry UWorld::CleanupWorld
	// (World.cpp:6223) is reached by all three teardown paths this issue needs:
	//   World.cpp:8847        FSeamlessTravelHandler::Tick     <- this issue
	//   UnrealEngine.cpp:16262 UEngine::LoadMap                 (non-seamless level change)
	//   PlayLevel.cpp:912      UEditorEngine::TeardownPlaySession (session end; EndPlayMap:224 -> :344)
	// All three land BEFORE GC. On the stop_play path the order is EndPlayMap:224 -> OnPIEEnded:307
	// -> TeardownPlaySession:344 (-> :912 CleanupWorld) -> CollectGarbage:502 -> leak scan :505-524.
	FDelegateHandle PostWorldCleanupHandle;

	// Issue #26997: Purge Python references to a PIE world at the moment that world is cleaned up.
	//
	// Why NOT FWorldDelegates::OnWorldBeginTearDown (World.h:4634, broadcast World.cpp:6150): it is
	// one step too early. At its seamless-travel firing (World.cpp:8679) the actors that SURVIVE the
	// travel are still inside the old world — KeepAnnotation is built at :8684-8732 and the actual
	// Rename into the new world happens at :8831. FPyReferenceCollector::PurgeUnrealObjectReferences
	// walks the target's inners (bIncludeInnerObjects=true), so purging there would null the Python
	// wrappers of the very PlayerController/Pawn seamless travel deliberately keeps. CleanupWorld runs
	// at :8847 — AFTER that rename — so this hazard cannot occur here.
	//
	// No sublevel collection logic is needed: CleanupWorldInternal recurses into GetLevels(),
	// GetStreamingLevels()->GetLoadedLevel() and duplicated level collections (World.cpp:6443-6470)
	// and each recursion broadcasts again, so streaming sublevel worlds arrive here one by one. The
	// CleanupWorldTag/CleanupWorldGlobalTag guard (:6294-6298) keeps that to once per world. Walking
	// World->GetLevels() ourselves would have been strictly worse: streaming sublevels enter that
	// array only via UWorld::AddToWorld (World.cpp:3761), so loaded-but-not-visible sublevel worlds
	// would be missed even though the engine leak scan counts every object in their packages.
	//
	// UGameInstance is intentionally out of reach here (the delegate hands us a UWorld only). Seamless
	// travel keeps the GameInstance alive across the swap, so nulling its wrapper would invalidate a
	// live object. The session axis still purges it via OnPIEEnded/CollectPurgeTargets.
	//
	// This delegate fires for EVERY world cleanup in the editor — asset editor closes, package tools,
	// cooking, PreExit. The PKG_PlayInEditor filter, not the caller set, is what bounds the blast
	// radius. It also covers the paths where the world SURVIVES CleanupWorld (UWorld::ReInitWorld
	// World.cpp:2619 via UnrealEdMisc.cpp:1302; CleanupPhysicsSceneThatWasInitializedForSave
	// EditorEngine.cpp:4626; CreateNewStreamingLevelForWorld EditorLevelUtils.cpp:812;
	// EditorServer.cpp:2899; PreExit EditorEngine.cpp:1620/:1631) — all editor worlds, none of which
	// can carry PKG_PlayInEditor. The list is evidence, not a closed set; the filter is the invariant.
	void OnPostWorldCleanup(class UWorld* World, bool bSessionEnded, bool bCleanupResources);

	// Issue #9098 Tier 2: In-flight raw-key key_up release timers scheduled by
	// DispatchInputActionEvent when fallback="raw_key". Each entry = (timer handle, key).
	// Used by OnEndPIE / Deinitialize to cancel the timer and dispatch a matching synthetic
	// key_up for every in-flight entry — prevents stuck-key state if PIE ends between the
	// key_down dispatch and the scheduled key_up. Scheduled callbacks remove their own
	// entry before firing normally.
	TArray<TPair<FTimerHandle, FKey>> PendingRawKeyReleases;

	// Issue #9098 Tier 2: Release every pending raw-key timer and dispatch matching key_up.
	// Safe to call when FSlateApplication is not initialized (silent no-op). Called by
	// OnEndPIE + Deinitialize.
	void ReleaseAllPendingRawKeys();

	// Issue #7926: Multi-PIE client orchestration
	// Handlers
	void HandleListPieClients(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleCompareActorState(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Helpers — logical client_id (0-based) to UE native PIEInstance mapping.
	// Logical 0 = server (listen/dedicated), 1..N-1 = clients in PIEInstance order.
	// All helpers re-query GEngine->GetWorldContexts() each call (stateless).
	bool MapLogicalClientIdToPIEInstance(int32 LogicalId, int32& OutPIEInstance) const;
	UWorld* ResolveClientWorld(int32 ClientId) const;
	APlayerController* ResolveClientPlayerController(int32 ClientId) const;

	// Issue #7928: NetEmulation parameter control (ADK Phase 3)
	// Typed WebSocket messages for per-client network condition simulation.
	// Handlers resolve UNetDriver via ResolveClientWorld(ClientId)->GetNetDriver()
	// and write FPacketSimulationSettings / UNetConnection::CurrentNetSpeed.
	// Requires #if DO_ENABLE_NET_TEST (non-Shipping); returns NET_EMULATION_UNSUPPORTED otherwise.
	void HandleConfigureNetworkEmulation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleGetNetworkEmulation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleResetNetworkEmulation(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Shared PIE+NetDriver resolution for the three NetEmulation handlers.
	// Needs to be a member so it can reach the private ResolveClientWorld()
	// and SendResponse() helpers; free-standing static helpers cannot.
	// Emits the matching error envelope on failure and returns nullptr
	// (caller should return immediately). ResponseType sets the `type` field
	// on the error JSON so clients can key off the right op name.
	UNetDriver* ResolvePIENetDriverForClient(
		INetworkingWebSocket* Socket,
		const FString& ResponseType,
		const FString& RequestId,
		int32 ClientId);

	// Issue #8784: Screenshot capture for visual verification
	void HandleCaptureEditorViewport(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleCapturePIEWindow(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleCaptureEditorPanel(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleCaptureAllPanels(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request); // Issue #8806
	void HandleEnumerateOpenTabs(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request); // Issue #9251

	// Issue #7915: Runtime UMG widget tree inspection
	void HandleGetWidgetTree(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleSearchWidgets(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #9239: Runtime widget hit-test
	void HandleFindWidgetAtPosition(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #9344: PIE widget enumeration + per-widget screenshot
	void HandleListActivePIEWidgets(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleCaptureWidgetScreenshot(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #26157: One asynchronous Designer capture per asset. The state owns
	// every reversible editor mutation until the settle ticker captures pixels.
	struct FCUWidgetDesignerCaptureState
	{
		FTSTicker::FDelegateHandle TickerHandle;
		INetworkingWebSocket* Socket = nullptr;
		FString RequestId;
		FString AssetPath;
		FString CropMode;
		TWeakPtr<FWidgetBlueprintEditor> Editor;
		TSharedPtr<FJsonObject> Request;
		FName OriginalMode;
		bool bModeChanged = false;
		bool bOriginalDashedOutlines = false;
		bool bOutlinesChanged = false;
		double StartTimeSeconds = 0.0;
	};
	TMap<FString, TSharedPtr<FCUWidgetDesignerCaptureState>> WidgetDesignerCaptures;
	TSharedPtr<FCUWidgetDesignerCaptureGate> WidgetDesignerCaptureGate;
	mutable FCriticalSection WidgetDesignerCapturesLock;

	void CompleteWidgetDesignerCapture(const TSharedPtr<FCUWidgetDesignerCaptureState>& State);
	static void RestoreWidgetDesignerState(const TSharedPtr<FCUWidgetDesignerCaptureState>& State,
		bool& bOutOutlinesRestored, bool& bOutModeRestored);
	void CancelAllWidgetDesignerCaptures();
	void CancelWidgetDesignerCapturesForSocket(INetworkingWebSocket* Socket);

	// Issue #8783: PIE runtime state queries + snapshot/diff
	void HandlePIEGetActorState(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandlePIEGetPlayerState(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandlePieReadGameplayAttributes(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request); // Issue #27039
	void HandlePIEListActiveActors(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandlePIESetMeasurementMode(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandlePIEWaitForCondition(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandlePIESnapshotFrame(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandlePIECompareSnapshots(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #8783: Pending pie_wait_for_condition state. One entry per in-flight wait, keyed
	// by request_id. On ticker expiration / socket close / PIE end, the ticker self-cancels
	// (returns false) and the entry is removed. Guarded by PIEPendingWaitsLock — ticker
	// callbacks run on the game thread but OnClientDisconnected may fire from the WS thread.
	struct FCUPIEPendingWait
	{
		FTSTicker::FDelegateHandle TickerHandle;
		INetworkingWebSocket* Socket = nullptr;
		FString RequestId;
		double DeadlineSeconds = 0.0;  // FPlatformTime::Seconds() + timeout
	};
	TMap<FString, FCUPIEPendingWait> PIEPendingWaits;
	mutable FCriticalSection PIEPendingWaitsLock;

	// Issue #8783: Cap on retained snapshots per session — older entries evicted FIFO.
	// Values <10 would break pie_compare_snapshots during A/B flows; values >16 are excessive
	// memory (each snapshot can be ~1MB at full scope on dense levels).
	static constexpr int32 kPIESnapshotRingCap = 10;

	// Issue #8783: Snapshot ring buffer, keyed by SnapshotId. Populated by pie_snapshot_frame,
	// read by pie_compare_snapshots when snapshot_a / snapshot_b are passed as id strings.
	// Cleared on PIE end via OnEndPIE (ids are meaningless across sessions).
	TArray<FCUPIESnapshot> PIESnapshotRing;
	mutable FCriticalSection PIESnapshotRingLock;
	uint64 PIESnapshotSeq = 0;  // Monotonic counter — part of SnapshotId

	// Issue #8783: Cancel every ticker belonging to this socket (safe when socket just
	// disconnected — we must not send to it). Called from OnClientDisconnected.
	void CancelAllPIEWaitsForSocket(INetworkingWebSocket* Socket);

	// Issue #8783: Cancel every ticker unconditionally. Called from OnEndPIE + Deinitialize.
	void CancelAllPIEWaits();

	// Issue #8783: Empty the snapshot ring (ids are session-scoped).
	void ClearPIESnapshotRing();

	// Issue #8784: Shared screenshot helpers
	bool CaptureViewportPixels(FViewport* Viewport, TArray<FColor>& OutPixels, int32& OutWidth, int32& OutHeight);
	bool EncodeAndSendScreenshot(INetworkingWebSocket* Socket, const FString& RequestId, const FString& ResultType,
		const TArray<FColor>& Pixels, int32 Width, int32 Height,
		const TSharedPtr<FJsonObject>& Request, double StartTime, const FString& ExtraJson = TEXT(""));

	// Issue #8299 (Mirror #26): Stat selective control handlers
	void HandleDisableStat(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);
	void HandleListActiveStats(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

	// Issue #32: RigUnit type → DisplayName mapping for Control Rig node resolution
	void HandleGetRigUnitDisplayNames(INetworkingWebSocket* Socket, const TSharedPtr<FJsonObject>& Request);

public:
	// Helper: Send response to specific client
	// Public so anonymous-namespace helpers in the .cpp (e.g. PIE snapshot /
	// wait-for-condition dispatch) can forward responses through a subsystem
	// instance without needing friend declarations. Issue #8783.
	void SendResponse(INetworkingWebSocket* Socket, const FString& Response);

#if WITH_DEV_AUTOMATION_TESTS
	/**
	 * Test-only entry point for the Niagara asset-parameter transaction.
	 *
	 * Keeping the production handler private prevents accidental direct use while
	 * allowing an Automation test to drive the exact request/response and rollback
	 * path through a capture socket.
	 */
	void HandleSetNiagaraAssetParameterForTesting(
		INetworkingWebSocket* Socket,
		const TSharedPtr<FJsonObject>& Request)
	{
		HandleSetNiagaraAssetParameter(Socket, Request);
	}
#endif

	// ============================================
	// Issue #9394: Test seams for the pending-message queue.
	//
	// These are kept narrow and _ForTesting-suffixed so code-review /
	// reviewers can spot accidental production use. Validated by
	// FWebSocketSubsystemBurstQueueTest in CUWebSocketSubsystemTests.cpp.
	// ============================================

	/** Inject a synthetic pending message directly into the dispatch queue.
	 *  Socket may be nullptr — the drain loop filters on Clients.Contains. */
	void EnqueuePendingMessageForTesting(TSharedPtr<FJsonObject> Json, INetworkingWebSocket* Socket = nullptr);

	/** Invoke one drain pass, respecting `n.WebSocket.MaxPerFrame`. Skips
	 *  Server->Tick() so tests don't need a live server. */
	void DrainPendingMessagesOnceForTesting();

	/** Current PendingMessages queue depth — best-effort (main-thread only). */
	int32 GetPendingQueueDepthForTesting() const;

	// ============================================
	// Issue #26843: Test seams for the PIE reference purge.
	// Same narrow / _ForTesting-suffixed convention as the block above.
	// ============================================

	/** Forward to PurgeEditorReferencesToObject. Returns true if a broadcast was issued. */
	bool PurgeEditorReferencesToObjectForTesting(UObject* Target);

	/** True when the FWorldDelegates::OnPIEEnded subscription is live. Guards the one line
	 *  whose loss silently reintroduces the leak — every other test stays green without it. */
	bool IsPIEEndedSubscribedForTesting() const;

	/** Forward to CollectPurgeTargets. Read-only: issues no broadcast. */
	void CollectPurgeTargetsForTesting(TArray<TWeakObjectPtr<UObject>>& OutTargets) const;

	/** Issue #26997: True when the FWorldDelegates::OnPostWorldCleanup subscription is live. Guards
	 *  the one Initialize() line whose loss silently drops SeamlessTravel coverage. */
	bool IsPostWorldCleanupSubscribedForTesting() const;

	/** Issue #27130: Forward to WriteActorMovedEntries. Returns the number of entries skipped
	 *  because their weak key no longer resolves — a test destroys an actor mid-snapshot and
	 *  asserts on that count. Pre-fix the map held raw AActor*, so this loop dereferenced a
	 *  freed slot and the Editor died on `Index >= 0` inside IsValid(). */
	int32 WriteActorMovedEntriesForTesting(
		const TMap<TWeakObjectPtr<class AActor>, FTransform>& BeforeTransforms,
		const FString& TransLogPath) const;

	/** Issue #26997: Drive OnPostWorldCleanup directly. The engine's own broadcast needs a real world
	 *  teardown, which an automation test cannot stage without breaking engine invariants. */
	void HandlePostWorldCleanupForTesting(class UWorld* World);

private:
	// Helper: Build event JSON
	FString BuildEventJSON(const FString& EventType, const FString& EventData);

	// Helper: Log with prefix
	void LogInfo(const FString& Message);
	void LogError(const FString& Message);
};
