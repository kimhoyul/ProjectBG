// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5652: AI Chat Backend Client — abstraction for CLI-based chat communication
// Issue #5656: Extended with tool_use/tool_result parsing for MCP tool result widgets

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Templates/SharedPointer.h"
#include "Dom/JsonObject.h"
#include "Misc/Guid.h"
#include "CUChatToolResult.h"

DECLARE_DELEGATE_OneParam(FOnChatToken, const FString& /* Token */);
DECLARE_DELEGATE_OneParam(FOnChatComplete, const FString& /* FullResponse */);
DECLARE_DELEGATE_OneParam(FOnChatError, const FString& /* ErrorMessage */);

/**
 * A single chat message with role, content, and timestamp (Issue #5657).
 */
struct FCUChatMessage
{
	FString Role;       // "user" | "assistant" | "system"
	FString Content;
	FDateTime Timestamp;

	/** Annotation ID that triggered this chat message (empty if user-initiated, Issue #5821) */
	FString SourceAnnotationId;

	FCUChatMessage() : Timestamp(FDateTime::UtcNow()) {}
	FCUChatMessage(const FString& InRole, const FString& InContent)
		: Role(InRole), Content(InContent), Timestamp(FDateTime::UtcNow()) {}

	TSharedPtr<FJsonObject> ToJson() const;
	static FCUChatMessage FromJson(const TSharedPtr<FJsonObject>& Obj);
};

/**
 * A chat session containing metadata and message history (Issue #5657).
 * Serialized as JSON to {ProjectSaved}/NarshaMCP/Sessions/{SessionId}.json
 */
struct FCUChatSession
{
	FString SessionId;
	FString Title;          // Auto-generated from first user message (first 50 chars)
	FDateTime CreatedAt;
	FDateTime UpdatedAt;
	TArray<FCUChatMessage> Messages;

	/**
	 * Cached message count for lightweight session listing.
	 * Set by FromJsonMetadataOnly() without message deserialization.
	 * Use GetMessageCount() accessor, which prefers Messages.Num() when available.
	 */
	int32 MessageCount = 0;

	FCUChatSession()
		: SessionId(FGuid::NewGuid().ToString())
		, CreatedAt(FDateTime::UtcNow())
		, UpdatedAt(FDateTime::UtcNow())
	{}

	TSharedPtr<FJsonObject> ToJson() const;
	static FCUChatSession FromJson(const TSharedPtr<FJsonObject>& Obj);

	/** Lightweight parse: reads metadata fields + counts messages without deserializing them */
	static FCUChatSession FromJsonMetadataOnly(const TSharedPtr<FJsonObject>& Obj);

	FString GetDisplayTitle() const { return Title.IsEmpty() ? TEXT("New Chat") : Title; }

	/** Get message count — prefers MessageCount if set, otherwise Messages.Num() */
	int32 GetMessageCount() const { return Messages.Num() > 0 ? Messages.Num() : MessageCount; }
};

/**
 * Abstract backend client for AI chat communication (Issue #5652).
 *
 * Decouples message transport from UI layer — supports Plan A (HTTP/SSE) or Plan B (CLI pipe).
 * NOT a UObject — plain C++ class using TSharedFromThis for safe async lambda capture.
 */
class FCUChatBackendClient : public TSharedFromThis<FCUChatBackendClient>
{
public:
	virtual ~FCUChatBackendClient() = default;

	/**
	 * Send a message to the AI backend
	 * @param Message The user message text to send
	 * @param SessionId Unique session identifier for continuity (empty for new session)
	 */
	virtual void SendMessage(const FString& Message, const FString& SessionId) = 0;

	/** Cancel the currently streaming request */
	virtual void CancelCurrentRequest() = 0;

	/**
	 * Whether a response is currently being streamed
	 * @return true if actively streaming, false otherwise
	 */
	[[nodiscard]] virtual bool IsStreaming() const = 0;

	/**
	 * Poll for new data — called from Subsystem Tick each frame.
	 * Default no-op; CLI implementation overrides for pipe reading.
	 */
	virtual void Poll() {}

	/** Streaming token delegate — fired for each chunk of response text */
	FOnChatToken OnToken;

	/** Response complete delegate — fired when full response is available */
	FOnChatComplete OnComplete;

	/** Error delegate — fired on process failure or CLI error */
	FOnChatError OnError;

	/** Mark this client as destroyed — prevents pending AsyncTasks from firing delegates */
	void MarkDestroyed() { bDestroyed = true; }

	/** Check if this client has been marked destroyed */
	bool IsDestroyed() const { return bDestroyed; }

	/** Get session ID extracted from backend (override in derived clients) */
	virtual FString GetLastExtractedSessionId() const { return FString(); }

	/** Tool result delegate — fired when a tool_result is correlated with its tool_use (Issue #5656) */
	FOnCUChatToolResult OnToolResult;

	/**
	 * Issue #8842 (Phase 10-3): optional context provider — when set, derived
	 * SendMessage implementations PREPEND the provider's return value to the
	 * outgoing message. Returning an empty string is a no-op (transparent).
	 *
	 * Wiring: CUChatPanelSubsystem sets this to a lambda that builds a
	 * "[Recent Editor Actions]…[End Recent Actions]\n\n" block from the
	 * rolling buffer fed by OnEventBroadcast (#8840).
	 */
	TFunction<FString()> ContextProvider;

protected:
	/** Set true when CancelCurrentRequest is called */
	TAtomic<bool> bCancelled{false};

	/** Set true during Deinitialize — prevents pending AsyncTasks from firing delegates */
	TAtomic<bool> bDestroyed{false};
};

/**
 * Claude CLI backend client (Plan B — Issue #5652).
 *
 * Spawns `claude --print --output-format stream-json` per message,
 * reads stdout pipe each frame via Poll(), fires delegates on GameThread.
 */
class FCUClaudeCliBackendClient : public FCUChatBackendClient
{
public:
	/**
	 * Construct CLI backend client with claude executable path
	 * @param InClaudeCliPath Full path to claude CLI executable
	 */
	explicit FCUClaudeCliBackendClient(const FString& InClaudeCliPath);
	virtual ~FCUClaudeCliBackendClient() override;

	virtual void SendMessage(const FString& Message, const FString& SessionId) override;
	virtual void CancelCurrentRequest() override;
	[[nodiscard]] virtual bool IsStreaming() const override;
	virtual void Poll() override;

	/** Parse a stream-json line and extract content text (public for testability) */
	FString ExtractContentFromStreamJson(const FString& JsonLine);

	/** Get session ID extracted from result events (for session continuity) */
	virtual FString GetLastExtractedSessionId() const override { return LastExtractedSessionId; }

	/** Parse stream-json line for tool_use/tool_result events (Issue #5656, public for testability) */
	bool ExtractToolEventsFromStreamJson(const FString& JsonLine);

	/** Get count of pending tool_use entries (public for testability) */
	[[nodiscard]] int32 GetPendingToolUseCount() const noexcept { return PendingToolUses.Num(); }

	/** Test-only: look up a pending tool_use by its id; returns {ToolName, Operation}
	 *  or {"",""} if absent. Used by LiveFix regression tests to verify prefix-strip
	 *  synchronously without waiting for the async GameThread delegate delivery. */
	[[nodiscard]] TPair<FString, FString> TestGetPendingTool(const FString& ToolUseId) const
	{
		if (const TPair<FString, FString>* Found = PendingToolUses.Find(ToolUseId))
		{
			return *Found;
		}
		return TPair<FString, FString>(FString(), FString());
	}

	/** Issue #9238: tail the daemon's tool-call JSONL log and synthesize tool_result events
	 *  for MCP calls that were wrapped inside Skill/Task/Agent (invisible to parent stream). */
	void TailToolCallLog();

	/** Test-only seq pointer accessor (#9238). */
	uint64 GetLastProcessedToolCallSeq() const noexcept { return LastProcessedToolCallSeq; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Test-only: incremental tail offset (#11458). */
	int64 GetToolCallLogReadOffset() const noexcept { return ToolCallLogReadOffset; }

	/** Test-only: cumulative bytes read by TailToolCallLog() since construction.
	 *  Performance regression tests assert this stays bounded on quiet ticks. (#11458) */
	int64 GetToolCallLogBytesReadTotal() const noexcept { return ToolCallLogBytesReadTotal; }

	/** Test-only: override the JSONL path before TailToolCallLog() observes it.
	 *  Lets unit tests point at a temp fixture instead of ProjectDir/Saved/...
	 *  Resets the initialization flag so the next tail anchors against the new file. */
	void SetToolCallLogPathForTesting(const FString& InPath)
	{
		ToolCallLogPath = InPath;
		ToolCallLogReadOffset = 0;
		ToolCallLogPartialBytes.Reset();
		bToolCallLogTailInitialized = false;
		LastProcessedToolCallSeq = 0;
		ToolCallLogBytesReadTotal = 0;
	}

	/** Test-only: force the streaming flag so we can exercise the streaming
	 *  branch of TailToolCallLog() (full catch-up from offset 0) without
	 *  spawning the CLI process. Note: bStreaming is TAtomic<bool>, but this
	 *  setter MUST only be called from the test thread before any concurrent
	 *  worker is using the client — production code never invokes it. (#11458) */
	void SetStreamingForTesting(bool bInStreaming) noexcept { bStreaming = bInStreaming; }
#endif // WITH_DEV_AUTOMATION_TESTS

private:
	/** Pending tool_use correlation map: tool_use id → (tool_name, operation) (Issue #5656) */
	TMap<FString, TPair<FString, FString>> PendingToolUses;

	/** Path to claude CLI executable */
	FString CliPath;

	/** Current CLI process handle */
	FProcHandle CliProcess;

	/** Pipe handles for capturing CLI stdout */
	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;

	/** Accumulated response text during streaming */
	FString AccumulatedResponse;

	/** Whether currently streaming a response */
	TAtomic<bool> bStreaming{false};

	/** Session ID extracted from result events (Issue #5652) */
	FString LastExtractedSessionId;

	/** Whether content_block_delta tokens have streamed for the current message.
	 *  When true, HandleAssistantMessage() returns empty — the trailing
	 *  `assistant` event with the full message body would otherwise duplicate
	 *  text already appended via deltas (visible as "text shown twice" in the
	 *  bubble after close-and-reopen interleave). Reset on SendMessage. */
	bool bReceivedContentDeltas = false;

	/** Clean up process and pipe handles */
	void CleanupProcess();

	// === Issue #9238: nested-MCP visibility via daemon tool-call log ===

	/** Absolute path to `{ProjectDir}/Saved/NarshaMCP/tool-call-log.jsonl`
	 *  (empty until first Tail call — then cached for process lifetime). */
	FString ToolCallLogPath;

	/** Last JSONL `seq` field already emitted as tool_result (monotonic).
	 *  Entries with seq <= this are skipped on subsequent tail passes. */
	uint64 LastProcessedToolCallSeq = 0;

	/** Dedup window: tool_name → last parent-stream emit time (seconds).
	 *  If a log entry arrives for a tool whose stream emit is < 5s old, skip
	 *  (direct MCP calls get both stream + log; wrapped calls only log). */
	TMap<FString, double> StreamToolEmitTimes;

	// === Issue #11458: incremental tail state (replaces full-file re-read) ===

	/** Byte offset into ToolCallLogPath; only bytes appended past this are read.
	 *  Idle clients anchor this to the file's EOF on first observation so the
	 *  daemon's pre-existing log entries are not replayed into the chat UI. */
	int64 ToolCallLogReadOffset = 0;

	/** Trailing RAW BYTES (UTF-8) of a not-yet-LF-terminated line carried to
	 *  the next tick. Stored as bytes — not TCHAR — so a multi-byte UTF-8
	 *  codepoint (한글 3-byte, emoji 4-byte) split across a read boundary
	 *  is preserved intact and decoded only after the next LF stitches it
	 *  back. LF (0x0A) is never a UTF-8 continuation byte, so slicing at LF
	 *  is always codepoint-safe. */
	TArray<uint8> ToolCallLogPartialBytes;

	/** False until the first observation of an existing file has anchored the
	 *  read offset. Idle: anchor at EOF (skip replay); streaming: anchor at 0
	 *  (capped to last STREAMING_CATCHUP_MAX_BYTES) so a wrapped MCP call
	 *  mid-conversation is still surfaced. */
	bool bToolCallLogTailInitialized = false;

	/** Cumulative bytes read by TailToolCallLog(). Exposed via the test-only
	 *  GetToolCallLogBytesReadTotal() so regression tests can assert quiet
	 *  ticks don't re-read the whole file. */
	int64 ToolCallLogBytesReadTotal = 0;

	/** Synthesize an `FCUChatToolResult` from one log entry and fire OnToolResult. */
	void HandleToolCallLogEntry(const TSharedPtr<class FJsonObject>& Entry);

	/** Handler for content_block_delta stream events */
	FString HandleContentBlockDelta(const TSharedPtr<FJsonObject>& JsonObject);

	/** Handler for assistant message events (initial content) */
	FString HandleAssistantMessage(const TSharedPtr<FJsonObject>& JsonObject);

	/** Handler for result events (session ID extraction) */
	void HandleResultEvent(const TSharedPtr<FJsonObject>& JsonObject);
};
