// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5656: AI Chat Tool Result — data model for MCP tool results in chat

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Parsed MCP tool result from Claude CLI stream-json output (Issue #5656).
 *
 * Represents a single tool invocation result extracted from the stream-json protocol.
 * tool_use (assistant) → PendingToolUses correlation → tool_result (user) → this struct.
 */
struct FCUChatToolResult
{
	/** MCP tool name (e.g., "ue_analyze_symbols", "ue_fix_errors") */
	FString ToolName;

	/** Operation within the tool (e.g., "search_symbols", "scan_build_log") */
	FString Operation;

	/** Unique request ID for tool_use/tool_result correlation */
	FString RequestId;

	/** Parsed JSON result object (may be null on error) */
	TSharedPtr<FJsonObject> JsonResult;

	/** Whether this result represents an error */
	bool bIsError = false;

	/** Error message (populated when bIsError is true) */
	FString ErrorMessage;

	// Explicit constructors with noexcept specifications
	FCUChatToolResult() noexcept = default;
	FCUChatToolResult(const FCUChatToolResult&) noexcept = default;
	FCUChatToolResult(FCUChatToolResult&&) noexcept = default;
	FCUChatToolResult& operator=(const FCUChatToolResult&) noexcept = default;
	FCUChatToolResult& operator=(FCUChatToolResult&&) noexcept = default;
	~FCUChatToolResult() noexcept = default;
};

DECLARE_DELEGATE_OneParam(FOnCUChatToolResult, const FCUChatToolResult& /* ToolResult */);
