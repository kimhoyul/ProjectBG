// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5652: AI Chat Backend Client implementation
// Issue #5656: Extended with tool_use/tool_result event parsing

#include "CUChatBackendClient.h"
#include "CUChatToolResult.h"
#include "Async/Async.h"
#include "Containers/StringConv.h" // Issue #11458: FUTF8ToTCHAR for tail bytes
#include "HAL/FileManager.h"       // Issue #9238: tool-call log tail
#include "Misc/Paths.h"
#include "Serialization/Archive.h" // Issue #11458: FArchive::Seek/Serialize
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPChatBackend, Log, All);

// ============================================================================
// FCUChatMessage JSON Serialization (Issue #5657)
// ============================================================================

TSharedPtr<FJsonObject> FCUChatMessage::ToJson() const
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("role"), Role);
	Obj->SetStringField(TEXT("content"), Content);
	Obj->SetStringField(TEXT("timestamp"), Timestamp.ToIso8601());
	if (!SourceAnnotationId.IsEmpty())
	{
		Obj->SetStringField(TEXT("source_annotation_id"), SourceAnnotationId);
	}
	return Obj;
}

FCUChatMessage FCUChatMessage::FromJson(const TSharedPtr<FJsonObject>& Obj)
{
	FCUChatMessage Msg;
	if (Obj.IsValid())
	{
		Obj->TryGetStringField(TEXT("role"), Msg.Role);
		Obj->TryGetStringField(TEXT("content"), Msg.Content);
		FString TimestampStr;
		if (Obj->TryGetStringField(TEXT("timestamp"), TimestampStr))
		{
			FDateTime::ParseIso8601(*TimestampStr, Msg.Timestamp);
		}
		Obj->TryGetStringField(TEXT("source_annotation_id"), Msg.SourceAnnotationId);
	}
	return Msg;
}

// ============================================================================
// FCUChatSession JSON Serialization (Issue #5657)
// ============================================================================

TSharedPtr<FJsonObject> FCUChatSession::ToJson() const
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("session_id"), SessionId);
	Obj->SetStringField(TEXT("title"), Title);
	Obj->SetStringField(TEXT("created_at"), CreatedAt.ToIso8601());
	Obj->SetStringField(TEXT("updated_at"), UpdatedAt.ToIso8601());

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FCUChatMessage& Msg : Messages)
	{
		MessagesArray.Add(MakeShared<FJsonValueObject>(Msg.ToJson()));
	}
	Obj->SetArrayField(TEXT("messages"), MessagesArray);

	return Obj;
}

FCUChatSession FCUChatSession::FromJson(const TSharedPtr<FJsonObject>& Obj)
{
	FCUChatSession Session;
	if (Obj.IsValid())
	{
		Obj->TryGetStringField(TEXT("session_id"), Session.SessionId);
		Obj->TryGetStringField(TEXT("title"), Session.Title);

		FString CreatedStr, UpdatedStr;
		if (Obj->TryGetStringField(TEXT("created_at"), CreatedStr))
		{
			if (!FDateTime::ParseIso8601(*CreatedStr, Session.CreatedAt))
			{
				UE_LOG(LogNarshaMCPChatBackend, Warning, TEXT("[NarshaMCP] Failed to parse created_at: %s"), *CreatedStr);
			}
		}
		if (Obj->TryGetStringField(TEXT("updated_at"), UpdatedStr))
		{
			if (!FDateTime::ParseIso8601(*UpdatedStr, Session.UpdatedAt))
			{
				UE_LOG(LogNarshaMCPChatBackend, Warning, TEXT("[NarshaMCP] Failed to parse updated_at: %s"), *UpdatedStr);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* MessagesArray = nullptr;
		if (Obj->TryGetArrayField(TEXT("messages"), MessagesArray) && MessagesArray)
		{
			for (const auto& MsgValue : *MessagesArray)
			{
				const TSharedPtr<FJsonObject>* MsgObj = nullptr;
				if (MsgValue->TryGetObject(MsgObj) && MsgObj)
				{
					Session.Messages.Add(FCUChatMessage::FromJson(*MsgObj));
				}
			}
			Session.MessageCount = Session.Messages.Num();
		}
	}
	return Session;
}

FCUChatSession FCUChatSession::FromJsonMetadataOnly(const TSharedPtr<FJsonObject>& Obj)
{
	FCUChatSession Session;
	if (Obj.IsValid())
	{
		Obj->TryGetStringField(TEXT("session_id"), Session.SessionId);
		Obj->TryGetStringField(TEXT("title"), Session.Title);

		FString CreatedStr, UpdatedStr;
		if (Obj->TryGetStringField(TEXT("created_at"), CreatedStr))
		{
			if (!FDateTime::ParseIso8601(*CreatedStr, Session.CreatedAt))
			{
				UE_LOG(LogNarshaMCPChatBackend, Warning, TEXT("[NarshaMCP] Failed to parse created_at: %s"), *CreatedStr);
			}
		}
		if (Obj->TryGetStringField(TEXT("updated_at"), UpdatedStr))
		{
			if (!FDateTime::ParseIso8601(*UpdatedStr, Session.UpdatedAt))
			{
				UE_LOG(LogNarshaMCPChatBackend, Warning, TEXT("[NarshaMCP] Failed to parse updated_at: %s"), *UpdatedStr);
			}
		}

		// Count messages without deserializing content
		const TArray<TSharedPtr<FJsonValue>>* MessagesArray = nullptr;
		if (Obj->TryGetArrayField(TEXT("messages"), MessagesArray) && MessagesArray)
		{
			Session.MessageCount = MessagesArray->Num();
		}
	}
	return Session;
}

// ============================================================================
// FCUClaudeCliBackendClient
// ============================================================================

FCUClaudeCliBackendClient::FCUClaudeCliBackendClient(const FString& InClaudeCliPath)
	: CliPath(InClaudeCliPath)
{
}

FCUClaudeCliBackendClient::~FCUClaudeCliBackendClient()
{
	bDestroyed = true;
	CleanupProcess();
}

void FCUClaudeCliBackendClient::SendMessage(const FString& Message, const FString& SessionId)
{
	if (CliPath.IsEmpty())
	{
		OnError.ExecuteIfBound(TEXT("Claude CLI not found. Install Claude Code (native recommended): https://code.claude.com/docs/en/setup"));
		return;
	}

	if (bStreaming)
	{
		CancelCurrentRequest();
	}

	// Reset state
	bCancelled = false;
	AccumulatedResponse.Empty();
	PendingToolUses.Empty();
	bReceivedContentDeltas = false;

	// Issue #8842 (Phase 10-3): if a context provider is set, prepend its output
	// to the user message. Empty return = no enrichment (transparent).
	FString FinalMessage = Message;
	if (ContextProvider)
	{
		FString ContextBlock = ContextProvider();
		if (!ContextBlock.IsEmpty())
		{
			FinalMessage = ContextBlock + TEXT("\n\n") + Message;
		}
	}

	// Build CLI arguments
	// claude --print --output-format stream-json --verbose --include-partial-messages --permission-mode acceptEdits -p "message"
	// Note: --verbose is REQUIRED for --output-format stream-json to work with --print
	const FString EscapedMessage = FinalMessage.Replace(TEXT("\""), TEXT("\\\""));

	// Issue #8835 hot-fix: --disallowedTools "Task" forces Claude to call MCP tools
	// directly at the top level instead of delegating to a sub-agent. Without this,
	// Claude wraps mcp__narshamcp__* calls inside Task/Agent — the parent stream
	// only sees a "Agent/" tool_result, so FCUChatToolWidgetFactory dispatches on
	// the wrong key and the Phase 8 / 9-11 specialized widgets never render.
	// Claude CLI in -p/--print mode does NOT auto-load project-scoped .mcp.json
	// (workspace trust dialog is skipped). Pass --mcp-config explicitly so the
	// narshamcp MCP server registers for this session and `mcp__narshamcp__*`
	// tools become callable. Without this arg the CLI falls back to user-scope
	// MCP config only (21 deferred tools, none of them narshamcp), producing
	// the "NarshaMCP not available in deferred tool list" UX bug.
	const FString ProjectMCPConfig = FPaths::Combine(FPaths::ProjectDir(), TEXT(".mcp.json"));
	const bool bHasProjectMCPConfig = FPaths::FileExists(ProjectMCPConfig);

	// Issue #8835 live-test (claude-code #44290-adjacent): Claude CLI's
	// project-scoped MCP discovery in -p mode is slow — narshamcp tools can
	// take 60–90s to register in the session's tool list on the first call
	// after the daemon warms up. Claude CLI's default turn budget (~5) is
	// not enough for the internal ToolSearch retries, so the call gives up
	// with "pending_mcp_servers: [narshamcp]". Raising --max-turns to 30
	// lets the CLI burn through ToolSearch retries long enough for
	// narshamcp tools to appear. Once registered, subsequent turns are
	// instant (cached tools), so the extra turn budget only kicks in when
	// actually needed.
	FString Args = FString::Printf(
		TEXT("--print --output-format stream-json --verbose --include-partial-messages --permission-mode acceptEdits --disallowedTools Task --max-turns 30")
	);
	if (bHasProjectMCPConfig)
	{
		Args += FString::Printf(TEXT(" --mcp-config \"%s\""), *ProjectMCPConfig);
	}

	// Session continuity: resume existing session if available
	if (!SessionId.IsEmpty())
	{
		Args += FString::Printf(TEXT(" --resume \"%s\""), *SessionId);
	}

	Args += FString::Printf(TEXT(" -p \"%s\""), *EscapedMessage);

	// Create pipe for stdout capture
	PipeRead = nullptr;
	PipeWrite = nullptr;
	FPlatformProcess::CreatePipe(PipeRead, PipeWrite);

	// Clear CLAUDECODE env var to prevent nested session detection
	FString PrevClaudeCode = FPlatformMisc::GetEnvironmentVariable(TEXT("CLAUDECODE"));
	if (!PrevClaudeCode.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDECODE"), TEXT(""));
	}

	// Issue #8835 hot-fix: Clear ANTHROPIC_API_KEY env var so the spawned Claude CLI
	// uses its own ~/.claude/ OAuth token instead of inheriting a possibly-invalid
	// API key from the parent shell. Editor's Chat Panel always relies on Claude CLI's
	// OAuth flow — env API key inheritance can override it and trigger 401 → 60s
	// "No response received from Claude CLI. Check API key" timeout.
	// Restored after CreateProc so the parent process behavior is unaffected.
	FString PrevApiKey = FPlatformMisc::GetEnvironmentVariable(TEXT("ANTHROPIC_API_KEY"));
	if (!PrevApiKey.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("ANTHROPIC_API_KEY"), TEXT(""));
	}

	// Resolve executable and args. On Win64 (Issue #8831): native .exe launches
	// directly, .cmd needs cmd.exe wrapper. On Mac/Linux (Issue #9230): the
	// claude CLI is always a single native binary — no wrapper concept exists,
	// so we always launch it directly.
	FString ExePath;
	FString ExeArgs;
#if PLATFORM_WINDOWS
	const bool bIsNativeBinary = CliPath.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase);
	if (bIsNativeBinary)
	{
		ExePath = CliPath;
		ExeArgs = Args;
	}
	else
	{
		ExePath = FPlatformMisc::GetEnvironmentVariable(TEXT("COMSPEC"));
		if (ExePath.IsEmpty())
		{
			ExePath = TEXT("C:\\Windows\\System32\\cmd.exe");
		}
		ExeArgs = FString::Printf(TEXT("/c \"\"%s\" %s\""), *CliPath, *Args);
	}
#else
	ExePath = CliPath;
	ExeArgs = Args;
#endif

	uint32 ProcessID = 0;
	CliProcess = FPlatformProcess::CreateProc(
		*ExePath,
		*ExeArgs,
		false,   // bLaunchDetached=false
		true,    // bLaunchHidden
		true,    // bLaunchReallyHidden
		&ProcessID,
		0,       // PriorityModifier
		*FPaths::ProjectDir(),
		PipeWrite
	);

	// Restore env vars (Issue #8835 hot-fix added ANTHROPIC_API_KEY restore)
	if (!PrevClaudeCode.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("CLAUDECODE"), *PrevClaudeCode);
	}
	if (!PrevApiKey.IsEmpty())
	{
		FPlatformMisc::SetEnvironmentVar(TEXT("ANTHROPIC_API_KEY"), *PrevApiKey);
	}

	if (CliProcess.IsValid())
	{
		bStreaming = true;
		UE_LOG(LogNarshaMCPChatBackend, Log, TEXT("[NarshaMCP] Claude CLI started (PID: %u)"), ProcessID);
	}
	else
	{
		// Cleanup pipe on failure
		if (PipeRead)
		{
			FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
			PipeRead = nullptr;
			PipeWrite = nullptr;
		}
		OnError.ExecuteIfBound(TEXT("Failed to start Claude CLI process"));
	}
}

void FCUClaudeCliBackendClient::CancelCurrentRequest()
{
	bCancelled = true;
	bStreaming = false;

	if (CliProcess.IsValid() && FPlatformProcess::IsProcRunning(CliProcess))
	{
		FPlatformProcess::TerminateProc(CliProcess, true);
		UE_LOG(LogNarshaMCPChatBackend, Log, TEXT("[NarshaMCP] Claude CLI request cancelled"));
	}

	CleanupProcess();
}

bool FCUClaudeCliBackendClient::IsStreaming() const
{
	return bStreaming;
}

void FCUClaudeCliBackendClient::Poll()
{
	// Issue #9238: Tail daemon tool-call log every poll tick. Fires BEFORE the
	// streaming guard so widget surfaces work even when the Claude CLI has
	// exited (wrapped MCP calls may still produce log entries after stream end).
	TailToolCallLog();

	if (bCancelled || bDestroyed || !PipeRead || !bStreaming)
	{
		return;
	}

	FString Chunk = FPlatformProcess::ReadPipe(PipeRead);
	if (!Chunk.IsEmpty())
	{
		UE_LOG(LogNarshaMCPChatBackend, Verbose, TEXT("[NarshaMCP] Raw pipe: %s"), *Chunk.Left(500));

		// Parse stream-json lines and extract content
		TArray<FString> Lines;
		Chunk.ParseIntoArrayLines(Lines);

		for (const FString& Line : Lines)
		{
			if (Line.IsEmpty())
			{
				continue;
			}

			// Issue #5656: Try tool event parsing first — tool events should NOT accumulate into text
			if (ExtractToolEventsFromStreamJson(Line))
			{
				continue;
			}

			FString Content = ExtractContentFromStreamJson(Line);
			if (!Content.IsEmpty())
			{
				AccumulatedResponse += Content;

				// Dispatch token to GameThread via WeakPtr (prevents dangling pointer)
				TWeakPtr<FCUChatBackendClient> WeakSelf = AsShared();
				AsyncTask(ENamedThreads::GameThread, [WeakSelf, Content]()
				{
					if (auto Pinned = WeakSelf.Pin())
					{
						Pinned->OnToken.ExecuteIfBound(Content);
					}
				});
			}
		}
	}

	// Check if process has exited
	if (!FPlatformProcess::IsProcRunning(CliProcess))
	{
		// Read any remaining pipe data
		FString Remaining = FPlatformProcess::ReadPipe(PipeRead);
		if (!Remaining.IsEmpty())
		{
			UE_LOG(LogNarshaMCPChatBackend, Verbose, TEXT("[NarshaMCP] Remaining pipe: %s"), *Remaining.Left(500));

			TArray<FString> Lines;
			Remaining.ParseIntoArrayLines(Lines);
			for (const FString& Line : Lines)
			{
				// Issue #5656: Parse tool events from remaining data too
				if (ExtractToolEventsFromStreamJson(Line))
				{
					continue;
				}
				FString Content = ExtractContentFromStreamJson(Line);
				if (!Content.IsEmpty())
				{
					AccumulatedResponse += Content;

					// Also dispatch as token so UI updates before FinalizeMessage
					TWeakPtr<FCUChatBackendClient> WeakSelf = AsShared();
					AsyncTask(ENamedThreads::GameThread, [WeakSelf, Content]()
					{
						if (auto Pinned = WeakSelf.Pin())
						{
							Pinned->OnToken.ExecuteIfBound(Content);
						}
					});
				}
			}
		}

		bStreaming = false;

		TWeakPtr<FCUChatBackendClient> WeakSelf = AsShared();
		if (AccumulatedResponse.IsEmpty())
		{
			// CLI returned no content — likely auth failure, API error, or format mismatch
			UE_LOG(LogNarshaMCPChatBackend, Warning, TEXT("[NarshaMCP] Claude CLI returned 0 chars — treating as error"));
			AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
			{
				if (auto Pinned = WeakSelf.Pin())
				{
					Pinned->OnError.ExecuteIfBound(
						TEXT("No response received from Claude CLI. Check API key and network connection."));
				}
			});
		}
		else
		{
			// Dispatch completion to GameThread via WeakPtr
			FString Final = AccumulatedResponse;
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Final]()
			{
				if (auto Pinned = WeakSelf.Pin())
				{
					Pinned->OnComplete.ExecuteIfBound(Final);
				}
			});
		}

		UE_LOG(LogNarshaMCPChatBackend, Log, TEXT("[NarshaMCP] Claude CLI response complete (%d chars)"), AccumulatedResponse.Len());
		CleanupProcess();
	}
}

void FCUClaudeCliBackendClient::CleanupProcess()
{
	if (CliProcess.IsValid())
	{
		FPlatformProcess::CloseProc(CliProcess);
		CliProcess.Reset();
	}
	if (PipeRead)
	{
		FPlatformProcess::ClosePipe(PipeRead, PipeWrite);
		PipeRead = nullptr;
		PipeWrite = nullptr;
	}
}

FString FCUClaudeCliBackendClient::ExtractContentFromStreamJson(const FString& JsonLine)
{
	// Claude stream-json format (with --verbose --include-partial-messages):
	// Top-level events:
	//   {"type":"system","subtype":"init",...}
	//   {"type":"assistant","message":{"content":[{"type":"text","text":"Hello"}]}}
	//   {"type":"result","result":"...","session_id":"..."}
	// Wrapped streaming events:
	//   {"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":" world"}}}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return FString();
	}

	FString Type;
	if (!JsonObject->TryGetStringField(TEXT("type"), Type))
	{
		return FString();
	}

	// Unwrap stream_event envelope — streaming events are wrapped in {"type":"stream_event","event":{...}}
	if (Type == TEXT("stream_event"))
	{
		const TSharedPtr<FJsonObject>* EventObj = nullptr;
		if (JsonObject->TryGetObjectField(TEXT("event"), EventObj) && EventObj)
		{
			JsonObject = *EventObj;
			if (!JsonObject->TryGetStringField(TEXT("type"), Type))
			{
				return FString();
			}
		}
		else
		{
			return FString();
		}
	}

	// Dispatch to type-specific handlers
	if (Type == TEXT("content_block_delta"))
	{
		return HandleContentBlockDelta(JsonObject);
	}
	else if (Type == TEXT("assistant"))
	{
		return HandleAssistantMessage(JsonObject);
	}
	else if (Type == TEXT("result"))
	{
		HandleResultEvent(JsonObject);
	}
	else
	{
		UE_LOG(LogNarshaMCPChatBackend, Verbose, TEXT("[NarshaMCP] Unhandled stream type: %s"), *Type);
	}

	return FString();
}

FString FCUClaudeCliBackendClient::HandleContentBlockDelta(const TSharedPtr<FJsonObject>& JsonObject)
{
	const TSharedPtr<FJsonObject>* DeltaObj = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("delta"), DeltaObj) && DeltaObj)
	{
		FString DeltaText;
		if ((*DeltaObj)->TryGetStringField(TEXT("text"), DeltaText))
		{
			// Mark that we've streamed deltas — the trailing `assistant` event
			// would otherwise duplicate this same text into the bubble.
			bReceivedContentDeltas = true;
			return DeltaText;
		}
	}
	return FString();
}

FString FCUClaudeCliBackendClient::HandleAssistantMessage(const TSharedPtr<FJsonObject>& JsonObject)
{
	// With --include-partial-messages, Claude CLI emits BOTH content_block_delta
	// (streaming tokens) AND a trailing `assistant` event with the full message
	// body. If deltas already streamed this content, returning it here causes
	// a second AppendText() on the bubble → visible "text shown twice" after
	// the close-and-reopen interleave change. Skip in that case.
	if (bReceivedContentDeltas)
	{
		return FString();
	}

	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (JsonObject->TryGetObjectField(TEXT("message"), MessageObj) && MessageObj)
	{
		const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
		if ((*MessageObj)->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray)
		{
			for (const auto& ContentItem : *ContentArray)
			{
				const TSharedPtr<FJsonObject>* ContentObj = nullptr;
				if (ContentItem->TryGetObject(ContentObj) && ContentObj)
				{
					FString ContentType;
					if ((*ContentObj)->TryGetStringField(TEXT("type"), ContentType) && ContentType == TEXT("text"))
					{
						FString TextValue;
						if ((*ContentObj)->TryGetStringField(TEXT("text"), TextValue))
						{
							return TextValue;
						}
					}
				}
			}
		}
	}
	return FString();
}

void FCUClaudeCliBackendClient::HandleResultEvent(const TSharedPtr<FJsonObject>& JsonObject)
{
	FString SessionId;
	if (JsonObject->TryGetStringField(TEXT("session_id"), SessionId) && !SessionId.IsEmpty())
	{
		LastExtractedSessionId = SessionId;
		UE_LOG(LogNarshaMCPChatBackend, Log, TEXT("[NarshaMCP] Session ID: %s"), *SessionId);
	}
}

// ============================================================================
// Issue #5656: Tool event parsing (tool_use + tool_result correlation)
// ============================================================================

bool FCUClaudeCliBackendClient::ExtractToolEventsFromStreamJson(const FString& JsonLine)
{
	// Claude stream-json tool event format:
	// tool_use (in assistant message content):
	//   {"type":"assistant","message":{"content":[{"type":"tool_use","id":"toolu_01X","name":"ue_check_health","input":{...}}]}}
	// tool_result (in user message content):
	//   {"type":"user","message":{"content":[{"type":"tool_result","tool_use_id":"toolu_01X","content":"..."}]}}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonLine);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	FString Type;
	if (!JsonObject->TryGetStringField(TEXT("type"), Type))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* MessageObj = nullptr;
	if (!JsonObject->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (!(*MessageObj)->TryGetArrayField(TEXT("content"), ContentArray) || !ContentArray)
	{
		return false;
	}

	bool bHandledToolEvent = false;

	for (const auto& ContentItem : *ContentArray)
	{
		const TSharedPtr<FJsonObject>* ContentObj = nullptr;
		if (!ContentItem->TryGetObject(ContentObj) || !ContentObj)
		{
			continue;
		}

		FString ContentType;
		if (!(*ContentObj)->TryGetStringField(TEXT("type"), ContentType))
		{
			continue;
		}

		// === tool_use: store in PendingToolUses for later correlation ===
		if (ContentType == TEXT("tool_use") && Type == TEXT("assistant"))
		{
			FString ToolUseId;
			FString ToolName;
			if ((*ContentObj)->TryGetStringField(TEXT("id"), ToolUseId) &&
				(*ContentObj)->TryGetStringField(TEXT("name"), ToolName))
			{
				// Issue #8835 hot-fix: Claude CLI prefixes MCP tool names with the
				// server identifier ("mcp__narshamcp__ue_*"). FCUChatToolWidgetFactory
				// registers creators with the bare tool name ("ue_*"). Strip the
				// prefix here so dispatch keys match — without this every MCP-tool
				// result falls back to the generic JSON tree widget instead of the
				// Phase 8 / Phase 9-11 specialized widgets.
				const FString MCPNarshaPrefix = TEXT("mcp__narshamcp__");
				if (ToolName.StartsWith(MCPNarshaPrefix))
				{
					ToolName = ToolName.Mid(MCPNarshaPrefix.Len());
				}

				// Extract operation from input.operation if present
				FString Operation;
				const TSharedPtr<FJsonObject>* InputObj = nullptr;
				if ((*ContentObj)->TryGetObjectField(TEXT("input"), InputObj) && InputObj)
				{
					(*InputObj)->TryGetStringField(TEXT("operation"), Operation);
				}

				PendingToolUses.Add(ToolUseId, TPair<FString, FString>(ToolName, Operation));
				UE_LOG(LogNarshaMCPChatBackend, Verbose, TEXT("[NarshaMCP] Tool use: %s (op: %s, id: %s)"), *ToolName, *Operation, *ToolUseId);
				bHandledToolEvent = true;
			}
		}
		// === tool_result: correlate with PendingToolUses and fire delegate ===
		else if (ContentType == TEXT("tool_result"))
		{
			FString ToolUseId;
			if (!(*ContentObj)->TryGetStringField(TEXT("tool_use_id"), ToolUseId))
			{
				continue;
			}

			FCUChatToolResult Result;
			Result.RequestId = ToolUseId;

			// Correlate with pending tool_use
			if (const TPair<FString, FString>* Found = PendingToolUses.Find(ToolUseId))
			{
				Result.ToolName = Found->Key;
				Result.Operation = Found->Value;
				PendingToolUses.Remove(ToolUseId);
			}
			else
			{
				UE_LOG(LogNarshaMCPChatBackend, Warning, TEXT("[NarshaMCP] Orphan tool_result (id: %s) — no matching tool_use found"), *ToolUseId);
			}

			// Check for error
			bool bIsErrorResult = false;
			if ((*ContentObj)->TryGetBoolField(TEXT("is_error"), bIsErrorResult))
			{
				Result.bIsError = bIsErrorResult;
			}

			// Parse content — may be string or JSON object
			FString ContentStr;
			if ((*ContentObj)->TryGetStringField(TEXT("content"), ContentStr))
			{
				// Try parsing content string as JSON
				TSharedPtr<FJsonObject> ParsedResult;
				TSharedRef<TJsonReader<>> ContentReader = TJsonReaderFactory<>::Create(ContentStr);
				if (FJsonSerializer::Deserialize(ContentReader, ParsedResult) && ParsedResult.IsValid())
				{
					Result.JsonResult = ParsedResult;
				}
				else
				{
					// Wrap plain text in a JSON object
					Result.JsonResult = MakeShared<FJsonObject>();
					Result.JsonResult->SetStringField(TEXT("text"), ContentStr);
				}

				if (Result.bIsError)
				{
					Result.ErrorMessage = ContentStr;
				}
			}
			else
			{
				// Content might be a nested array of content blocks
				const TArray<TSharedPtr<FJsonValue>>* ResultContentArray = nullptr;
				if ((*ContentObj)->TryGetArrayField(TEXT("content"), ResultContentArray) && ResultContentArray)
				{
					// Extract text from content blocks
					FString CombinedText;
					for (const auto& Block : *ResultContentArray)
					{
						const TSharedPtr<FJsonObject>* BlockObj = nullptr;
						if (Block->TryGetObject(BlockObj) && BlockObj)
						{
							FString BlockText;
							if ((*BlockObj)->TryGetStringField(TEXT("text"), BlockText))
							{
								CombinedText += BlockText;
							}
						}
					}
					if (!CombinedText.IsEmpty())
					{
						// Try parsing combined text as JSON
						TSharedPtr<FJsonObject> ParsedResult;
						TSharedRef<TJsonReader<>> ContentReader = TJsonReaderFactory<>::Create(CombinedText);
						if (FJsonSerializer::Deserialize(ContentReader, ParsedResult) && ParsedResult.IsValid())
						{
							Result.JsonResult = ParsedResult;
						}
						else
						{
							Result.JsonResult = MakeShared<FJsonObject>();
							Result.JsonResult->SetStringField(TEXT("text"), CombinedText);
						}
					}
				}
			}

			// Issue #9238: Record stream-json emit time for log-tail dedup.
			// Direct (un-wrapped) MCP calls produce both stream events AND daemon log
			// entries; without this record the log tail would render the same widget twice.
			StreamToolEmitTimes.Add(Result.ToolName, FPlatformTime::Seconds());

			// Dispatch to GameThread
			TWeakPtr<FCUChatBackendClient> WeakSelf = AsShared();
			AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]()
			{
				if (auto Pinned = WeakSelf.Pin())
				{
					Pinned->OnToolResult.ExecuteIfBound(Result);
				}
			});

			UE_LOG(LogNarshaMCPChatBackend, Verbose, TEXT("[NarshaMCP] Tool result: %s/%s (error: %d)"),
				*Result.ToolName, *Result.Operation, Result.bIsError);
			bHandledToolEvent = true;
		}
	}

	return bHandledToolEvent;
}

// ============================================================================
// Issue #9238: daemon tool-call log tail (nested-MCP widget visibility)
// ============================================================================

void FCUClaudeCliBackendClient::TailToolCallLog()
{
	// Issue #11458: Incremental byte-offset tailing.
	//
	// Predecessor implementation (Issue #9238) re-read and re-parsed the entire
	// JSONL file on every 100ms Core ticker. On a daemon-heavy dev PC the file
	// reaches multiple MB before the 10 MB rotation threshold, which stalled
	// the editor game thread. This version:
	//
	//   1. Reads only bytes appended past ToolCallLogReadOffset (O(delta), not O(N)).
	//   2. Stitches trailing partial bytes across ticks in BYTE space — UTF-8
	//      codepoints split across read boundaries (한글 3-byte, emoji 4-byte) are
	//      preserved intact; decode happens only at LF boundaries which are
	//      guaranteed codepoint-safe (LF = 0x0A is never a UTF-8 continuation byte).
	//   3. Anchors the first observation to EOF when idle so a multi-MB
	//      pre-existing log is not replayed into the chat UI.
	//   4. Caps initial streaming catch-up to STREAMING_CATCHUP_MAX_BYTES so a
	//      multi-MB stale log on a long-running daemon does not produce a
	//      single-tick stall when the user starts a new chat session.
	//
	// Daemon rotation contract (#9238): the daemon rotates the live file to
	// `.jsonl.1` when it exceeds 10 MB, leaving the new live file at 0 bytes.
	// We detect this via `CurrentSize < ToolCallLogReadOffset` and reset state.

	// 256 KB cap on initial streaming catch-up. Larger than a typical assistant
	// turn (~10-50 KB of tool calls) but small enough to bound parse cost on
	// the game thread (~1-3 ms even with 200+ JSONL entries).
	constexpr int64 STREAMING_CATCHUP_MAX_BYTES = 256 * 1024;

	if (bDestroyed)
	{
		return;
	}

	// Lazy-compute log path on first call (project dir stable within a process).
	// Tests can override via SetToolCallLogPathForTesting() before this runs.
	if (ToolCallLogPath.IsEmpty())
	{
		ToolCallLogPath = FPaths::Combine(
			FPaths::ProjectDir(),
			TEXT("Saved"), TEXT("NarshaMCP"),
			TEXT("tool-call-log.jsonl"));
	}

	IFileManager& FM = IFileManager::Get();
	const int64 CurrentSize = FM.FileSize(*ToolCallLogPath);
	if (CurrentSize < 0)
	{
		return; // Log not yet created by daemon — no-op
	}

	// First observation: anchor the read offset before we ever parse anything.
	if (!bToolCallLogTailInitialized)
	{
		bToolCallLogTailInitialized = true;
		ToolCallLogPartialBytes.Reset();
		if (!bStreaming)
		{
			// Idle: skip to EOF so the daemon's pre-existing log is ignored.
			ToolCallLogReadOffset = CurrentSize;
			return;
		}
		// Streaming: anchor near EOF, capped to STREAMING_CATCHUP_MAX_BYTES.
		if (CurrentSize > STREAMING_CATCHUP_MAX_BYTES)
		{
			ToolCallLogReadOffset = CurrentSize - STREAMING_CATCHUP_MAX_BYTES;
			UE_LOG(LogNarshaMCPChatBackend, Warning,
				TEXT("[NarshaMCP] Tool-call log streaming catch-up capped at %lld bytes (skipped %lld of %lld)"),
				static_cast<int64>(STREAMING_CATCHUP_MAX_BYTES), ToolCallLogReadOffset, CurrentSize);
		}
		else
		{
			ToolCallLogReadOffset = 0;
		}
		// Falls through and reads the (possibly capped) tail below.
	}

	// Rotation / truncation: file shrank since last observation. Reset state.
	if (CurrentSize < ToolCallLogReadOffset)
	{
		ToolCallLogReadOffset = 0;
		ToolCallLogPartialBytes.Reset();
		LastProcessedToolCallSeq = 0;
		if (!bStreaming)
		{
			ToolCallLogReadOffset = CurrentSize;
			return; // Idle — re-anchor at new EOF
		}
	}

	if (CurrentSize == ToolCallLogReadOffset)
	{
		return; // No growth — the fast path on every quiet tick
	}

	const int64 Delta = CurrentSize - ToolCallLogReadOffset;

	// Defensive int32 guard. With the 10 MB daemon rotation + 256 KB streaming
	// cap above, Delta is always well under int32::Max in practice — but a
	// raw narrowing cast would be a silent bug if either invariant ever broke.
	if (Delta > static_cast<int64>(TNumericLimits<int32>::Max()))
	{
		UE_LOG(LogNarshaMCPChatBackend, Error,
			TEXT("[NarshaMCP] Tool-call log Delta=%lld exceeds int32 range; jumping to EOF"),
			Delta);
		ToolCallLogReadOffset = CurrentSize;
		ToolCallLogPartialBytes.Reset();
		return;
	}

	// Read only the appended bytes. FILEREAD_AllowWrite is required so the
	// daemon can keep appending concurrently on Windows — without it,
	// CreateFileReader frequently fails when daemon traffic is active.
	TUniquePtr<FArchive> Reader(FM.CreateFileReader(*ToolCallLogPath, FILEREAD_AllowWrite));
	if (!Reader.IsValid())
	{
		return; // Transient — retry next tick
	}
	Reader->Seek(ToolCallLogReadOffset);

	const int32 DeltaInt = static_cast<int32>(Delta);
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(DeltaInt);
	Reader->Serialize(Buffer.GetData(), DeltaInt);
	const bool bReadOk = !Reader->IsError();
	Reader->Close();
	if (!bReadOk)
	{
		return; // Don't advance the offset — try again next tick
	}

	ToolCallLogBytesReadTotal += DeltaInt;

	// Stitch raw bytes across ticks. We slice at the LAST LF — bytes before
	// it form complete UTF-8-safe lines (LF is never inside a multibyte
	// sequence), bytes after it are an in-flight line (possibly mid-codepoint)
	// to carry to the next tick.
	TArray<uint8> StitchedBuffer = MoveTemp(ToolCallLogPartialBytes);
	StitchedBuffer.Append(Buffer);

	int32 LastLFIdx = INDEX_NONE;
	for (int32 j = StitchedBuffer.Num() - 1; j >= 0; --j)
	{
		if (StitchedBuffer[j] == static_cast<uint8>('\n'))
		{
			LastLFIdx = j;
			break;
		}
	}

	if (LastLFIdx == INDEX_NONE)
	{
		// No complete line in this chunk — carry everything to the next tick.
		ToolCallLogPartialBytes = MoveTemp(StitchedBuffer);
		ToolCallLogReadOffset = CurrentSize;
		return;
	}

	// Decode only the [0, LastLFIdx+1) range — UTF-8 boundary guaranteed.
	const int32 CompleteByteLen = LastLFIdx + 1;
	const FUTF8ToTCHAR Converter(
		reinterpret_cast<const ANSICHAR*>(StitchedBuffer.GetData()),
		CompleteByteLen);
	FString CompletePortion(Converter.Length(), Converter.Get());

	// Carry the post-LF bytes (next-line in-flight prefix, possibly mid-codepoint).
	ToolCallLogPartialBytes.Reset();
	const int32 RemainingBytes = StitchedBuffer.Num() - CompleteByteLen;
	if (RemainingBytes > 0)
	{
		ToolCallLogPartialBytes.Append(
			StitchedBuffer.GetData() + CompleteByteLen,
			RemainingBytes);
	}

	// Split decoded portion on LF. The trailing element after the final LF is
	// empty and skipped via Line.IsEmpty() below; no special-case needed.
	TArray<FString> Lines;
	CompletePortion.ParseIntoArray(Lines, TEXT("\n"), /*InCullEmpty=*/false);

	for (FString& Line : Lines)
	{
		// CRLF tolerated: strip trailing CR if daemon writes Windows line endings.
		if (Line.EndsWith(TEXT("\r"), ESearchCase::CaseSensitive))
		{
			Line.LeftChopInline(1);
		}
		if (Line.IsEmpty())
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(Line);
		if (!FJsonSerializer::Deserialize(JsonReader, Entry) || !Entry.IsValid())
		{
			continue; // Malformed line — skip silently (offset still advances)
		}

		double SeqDouble = 0.0;
		if (!Entry->TryGetNumberField(TEXT("seq"), SeqDouble))
		{
			continue; // Missing seq field — skip (offset still advances)
		}
		const uint64 Seq = static_cast<uint64>(SeqDouble);
		if (Seq <= LastProcessedToolCallSeq && LastProcessedToolCallSeq > 0)
		{
			continue; // Already processed in a prior poll
		}

		HandleToolCallLogEntry(Entry);
		LastProcessedToolCallSeq = Seq;
	}

	ToolCallLogReadOffset = CurrentSize;
}

void FCUClaudeCliBackendClient::HandleToolCallLogEntry(const TSharedPtr<FJsonObject>& Entry)
{
	if (!Entry.IsValid())
	{
		return;
	}

	FString ToolName;
	Entry->TryGetStringField(TEXT("tool"), ToolName);
	if (ToolName.IsEmpty())
	{
		return;
	}

	// Dedup: if stream-json already emitted same tool_name within last 5s,
	// assume it was a direct (un-wrapped) call — skip to avoid duplicate widget.
	const double* LastStreamEmit = StreamToolEmitTimes.Find(ToolName);
	if (LastStreamEmit != nullptr)
	{
		const double Now = FPlatformTime::Seconds();
		if ((Now - *LastStreamEmit) < 5.0)
		{
			UE_LOG(LogNarshaMCPChatBackend, Verbose,
				TEXT("[NarshaMCP] Log tail: skipping %s (stream-json emitted %.2fs ago)"),
				*ToolName, Now - *LastStreamEmit);
			return;
		}
	}

	FString Operation;
	Entry->TryGetStringField(TEXT("operation"), Operation);
	bool bIsError = false;
	Entry->TryGetBoolField(TEXT("is_error"), bIsError);

	// Build FCUChatToolResult to match the stream-json pipeline's shape.
	FCUChatToolResult Result;
	Result.ToolName = ToolName;
	Result.Operation = Operation;
	Result.bIsError = bIsError;

	// `result` field is the raw tool response JSON. Most NarshaMCP tools return
	// {"content":[{"type":"text","text":"<stringified json>"}],"isError":...}.
	// Unwrap the text payload (best-effort) to match stream-json's JsonResult shape.
	const TSharedPtr<FJsonValue> ResultField = Entry->TryGetField(TEXT("result"));
	if (ResultField.IsValid() && ResultField->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& ResultObj = ResultField->AsObject();

		// Try to extract text content out of mcp content array
		FString InnerText;
		const TArray<TSharedPtr<FJsonValue>>* ContentArr = nullptr;
		if (ResultObj->TryGetArrayField(TEXT("content"), ContentArr) && ContentArr)
		{
			for (const TSharedPtr<FJsonValue>& Item : *ContentArr)
			{
				const TSharedPtr<FJsonObject>* ItemObj = nullptr;
				if (Item->TryGetObject(ItemObj) && ItemObj)
				{
					FString ItemType;
					(*ItemObj)->TryGetStringField(TEXT("type"), ItemType);
					if (ItemType == TEXT("text"))
					{
						FString Text;
						if ((*ItemObj)->TryGetStringField(TEXT("text"), Text))
						{
							InnerText += Text;
						}
					}
				}
			}
		}

		if (!InnerText.IsEmpty())
		{
			// Parse inner text as JSON (most tools stringify their output)
			TSharedPtr<FJsonObject> ParsedInner;
			TSharedRef<TJsonReader<>> InnerReader = TJsonReaderFactory<>::Create(InnerText);
			if (FJsonSerializer::Deserialize(InnerReader, ParsedInner) && ParsedInner.IsValid())
			{
				Result.JsonResult = ParsedInner;
			}
			else
			{
				Result.JsonResult = MakeShared<FJsonObject>();
				Result.JsonResult->SetStringField(TEXT("text"), InnerText);
			}
		}
		else
		{
			// Fall back to the whole result object
			Result.JsonResult = ResultObj;
		}
	}
	else
	{
		Result.JsonResult = MakeShared<FJsonObject>();
	}

	if (bIsError)
	{
		// Try to populate ErrorMessage from result.error
		FString ErrMsg;
		if (Result.JsonResult.IsValid() && Result.JsonResult->TryGetStringField(TEXT("error"), ErrMsg))
		{
			Result.ErrorMessage = ErrMsg;
		}
	}

	// Dispatch to GameThread (same pattern as stream-json path)
	TWeakPtr<FCUChatBackendClient> WeakSelf = AsShared();
	AsyncTask(ENamedThreads::GameThread, [WeakSelf, Result]()
	{
		if (auto Pinned = WeakSelf.Pin())
		{
			Pinned->OnToolResult.ExecuteIfBound(Result);
		}
	});

	UE_LOG(LogNarshaMCPChatBackend, Verbose,
		TEXT("[NarshaMCP] Tool result (log tail): %s/%s (error: %d)"),
		*Result.ToolName, *Result.Operation, Result.bIsError);
}
