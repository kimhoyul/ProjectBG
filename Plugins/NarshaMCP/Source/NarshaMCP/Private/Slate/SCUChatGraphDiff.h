// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8838 (Phase 9-3): SCUChatGraphDiff — graph snapshot marker / diff card

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/**
 * Graph snapshot status card for `ue_manage_project_ops:level_mark_transaction`
 * (Issue #8838).
 *
 * level_mark_transaction's response contains the marker metadata
 * (action / label / snapshots_captured / diffs_written / warning) but
 * NOT the actual diff payload (which is written to the transaction
 * log JSONL). This widget renders the marker status as a card, with
 * color-coded indicators for action type and a hint pointing to the
 * full diff via level_get_workflow_log.
 *
 * Layout:
 *   [Action badge] [Label]                   [diffs_written: N]
 *   [Snapshots captured: N]
 *   [Warning, if any — e.g. PIE state hint]
 *   [Hint: "Use level_get_workflow_log to inspect per-graph diffs"]
 */
class SCUChatGraphDiff : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatGraphDiff)
		: _SnapshotsCaptured(0)
		, _DiffsWritten(0)
	{}
		SLATE_ARGUMENT(FString, Action)             // "start" | "end"
		SLATE_ARGUMENT(FString, Label)
		SLATE_ARGUMENT(int32, SnapshotsCaptured)
		SLATE_ARGUMENT(int32, DiffsWritten)
		SLATE_ARGUMENT(FString, Warning)
		SLATE_ARGUMENT(FString, LogFile)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static FLinearColor GetActionColor(const FString& Action);

#if WITH_AUTOMATION_WORKER
	const FString& TestGetAction() const { return Action; }
	const FString& TestGetLabel() const { return Label; }
	int32 TestGetSnapshotsCaptured() const { return SnapshotsCaptured; }
	int32 TestGetDiffsWritten() const { return DiffsWritten; }
	bool TestHasWarning() const { return !Warning.IsEmpty(); }
#endif

private:
	FString Action;
	FString Label;
	int32 SnapshotsCaptured = 0;
	int32 DiffsWritten = 0;
	FString Warning;
	FString LogFile;
};
