// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8844 (Phase 11-2): SCUChatMarkerCatalog — active markers + available labels list

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/** Single active marker entry from level_snapshot_status's active_markers[]. */
struct FCUChatActiveMarker
{
	FString Label;
	int64 StartedAt = 0; // Unix timestamp
	int32 BlueprintCount = 0;
	int32 SnapshotCount = 0;
	bool bUnclosed = false;
	FString GraphName;
	FString Warning;
};

/**
 * Marker catalog for `ue_manage_project_ops:level_snapshot_status` (Issue #8844).
 *
 * Layout:
 *   [Header: log status / active count / total entries]
 *   [Active markers — each row with label + counts + unclosed warning]
 *   [Available labels — chip strip + hint to call level_generate_replay_script]
 *
 * Click-to-replay (per issue body) is deferred to a follow-up: that needs a
 * static-creator-side path to invoke an arbitrary MCP call from a chat widget,
 * which currently routes through the chat backend (Claude tool_use). This MVP
 * surfaces the catalog and provides a copy-friendly hint.
 */
class SCUChatMarkerCatalog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatMarkerCatalog)
		: _LogAvailable(false)
		, _LogEntryCount(0)
	{}
		SLATE_ARGUMENT(TArray<FCUChatActiveMarker>, ActiveMarkers)
		SLATE_ARGUMENT(TArray<FString>, AvailableLabels)
		SLATE_ARGUMENT(bool, LogAvailable)
		SLATE_ARGUMENT(int32, LogEntryCount)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

#if WITH_AUTOMATION_WORKER
	int32 TestGetActiveMarkerCount() const { return ActiveMarkers.Num(); }
	int32 TestGetAvailableLabelCount() const { return AvailableLabels.Num(); }
	bool TestGetLogAvailable() const { return bLogAvailable; }
	int32 TestGetLogEntryCount() const { return LogEntryCount; }
	int32 TestGetUnclosedCount() const
	{
		int32 N = 0;
		for (const FCUChatActiveMarker& M : ActiveMarkers) { if (M.bUnclosed) ++N; }
		return N;
	}
#endif

private:
	TArray<FCUChatActiveMarker> ActiveMarkers;
	TArray<FString> AvailableLabels;
	bool bLogAvailable = false;
	int32 LogEntryCount = 0;

	TSharedRef<class SWidget> BuildHeader() const;
	TSharedRef<class SWidget> BuildMarkerRow(const FCUChatActiveMarker& Marker) const;
	TSharedRef<class SWidget> BuildLabelChips() const;
};
