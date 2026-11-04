// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8845 (Phase 11-3): SCUChatWorkflowWidget — replay execution / dry-run summary

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/** Single step result from level_replay_transactions (execute mode). */
struct FCUChatWorkflowStep
{
	FString StepName;     // "python", "mcp_call", or domain-specific tag
	bool bSuccess = true;
	double TimeMs = 0.0;
	FString Error;        // populated on failure
};

/**
 * Replay execution / dry-run workflow widget for
 * `ue_manage_project_ops:level_replay_transactions` (Issue #8845).
 *
 * Three response modes are surfaced via the `mode` field:
 *   - "preview"      → dry-run: stats + dependency_warning + hint
 *   - "execute"      → real run: per-step success/fail timeline + summary
 *   - "batch_execute" / "batch_dry_run" → batch summary across multiple targets
 *
 * Sub-replay tree visualization (per issue body's Claude tool_use streaming
 * scenario) is deferred — that path requires hooking into ChatBackendClient's
 * tool_use stream, which is orthogonal to this MCP-result widget. The
 * widget's `Steps` array is shaped to support tree display in a follow-up.
 */
class SCUChatWorkflowWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatWorkflowWidget)
		: _PythonActions(0)
		, _MCPActions(0)
		, _SuccessCount(0)
		, _FailCount(0)
		, _TotalCount(0)
	{}
		SLATE_ARGUMENT(FString, Mode)             // preview | execute | batch_dry_run | batch_execute
		SLATE_ARGUMENT(FString, Hint)
		SLATE_ARGUMENT(FString, DependencyWarning)
		SLATE_ARGUMENT(int32, PythonActions)
		SLATE_ARGUMENT(int32, MCPActions)
		SLATE_ARGUMENT(int32, SuccessCount)
		SLATE_ARGUMENT(int32, FailCount)
		SLATE_ARGUMENT(int32, TotalCount)
		SLATE_ARGUMENT(TArray<FCUChatWorkflowStep>, Steps)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Map mode string → display color. */
	static FLinearColor GetModeColor(const FString& Mode);

#if WITH_AUTOMATION_WORKER
	const FString& TestGetMode() const { return Mode; }
	int32 TestGetStepCount() const { return Steps.Num(); }
	int32 TestGetSuccessCount() const { return SuccessCount; }
	int32 TestGetFailCount() const { return FailCount; }
	bool TestHasDependencyWarning() const { return !DependencyWarning.IsEmpty(); }
#endif

private:
	FString Mode;
	FString Hint;
	FString DependencyWarning;
	int32 PythonActions = 0;
	int32 MCPActions = 0;
	int32 SuccessCount = 0;
	int32 FailCount = 0;
	int32 TotalCount = 0;
	TArray<FCUChatWorkflowStep> Steps;
};
