// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8843 (Phase 11-1): SCUChatReplayWidget — replay script preview + execution hint

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/** Single MCP call entry from level_generate_replay_script's mcp_calls[]. */
struct FCUChatReplayMCPCall
{
	FString Operation;
	FString DomainRouting;
	FString ParamsPreview; // condensed JSON
};

/**
 * Replay-script preview widget for `ue_manage_project_ops:level_generate_replay_script`
 * (Issue #8843).
 *
 * Layout:
 *   [Header: stats — total / python / mcp / skipped]
 *   [Warning panel, if any]
 *   [Large-script warning, if present]
 *   [Python script preview block — copy-to-clipboard button on top]
 *   [MCP calls list — first ~20 entries, "+ N more" footer if truncated]
 *
 * Direct execution paths (Run All / Step Next) are deferred to a follow-up
 * subissue: they require WebSocket Editor RC bridge access from a static
 * factory creator and need PIE-state guards. For this PR the widget renders
 * a hint pointing to the existing executors.
 */
class SCUChatReplayWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatReplayWidget)
		: _TotalEntries(0)
		, _PythonActions(0)
		, _MCPActions(0)
		, _Skipped(0)
	{}
		SLATE_ARGUMENT(FString, PythonScript)
		SLATE_ARGUMENT(TArray<FCUChatReplayMCPCall>, MCPCalls)
		SLATE_ARGUMENT(int32, TotalEntries)
		SLATE_ARGUMENT(int32, PythonActions)
		SLATE_ARGUMENT(int32, MCPActions)
		SLATE_ARGUMENT(int32, Skipped)
		SLATE_ARGUMENT(FString, Warning)
		SLATE_ARGUMENT(FString, LargeScriptWarning)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

#if WITH_AUTOMATION_WORKER
	int32 TestGetTotalEntries() const { return TotalEntries; }
	int32 TestGetPythonActions() const { return PythonActions; }
	int32 TestGetMCPActions() const { return MCPActions; }
	int32 TestGetSkipped() const { return Skipped; }
	int32 TestGetMCPCallCount() const { return MCPCalls.Num(); }
	const FString& TestGetPythonScript() const { return PythonScript; }
	bool TestHasWarning() const { return !Warning.IsEmpty(); }
	bool TestHasLargeScriptWarning() const { return !LargeScriptWarning.IsEmpty(); }
#endif

private:
	FString PythonScript;
	TArray<FCUChatReplayMCPCall> MCPCalls;
	int32 TotalEntries = 0;
	int32 PythonActions = 0;
	int32 MCPActions = 0;
	int32 Skipped = 0;
	FString Warning;
	FString LargeScriptWarning;

	/** Copy PythonScript to system clipboard. */
	FReply OnCopyPythonClicked();
};
