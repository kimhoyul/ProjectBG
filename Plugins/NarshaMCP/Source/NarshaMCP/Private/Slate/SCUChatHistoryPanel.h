// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5657: AI Chat History Panel — session list with search, selection, and deletion

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Views/SListView.h"

struct FCUChatSession;

DECLARE_DELEGATE_OneParam(FOnSessionSelected, const FString& /* SessionId */);
DECLARE_DELEGATE(FOnBackRequested);
DECLARE_DELEGATE_OneParam(FOnSessionDeleted, const FString& /* SessionId */);

/**
 * Session history panel widget (Issue #5657).
 * Displays a searchable list of saved chat sessions with selection and deletion.
 *
 * Layout:
 *   SVerticalBox
 *     +-- SSearchBox (filter by title, AutoHeight)
 *     +-- SListView<TSharedPtr<FCUChatSession>> (FillHeight)
 *     |     Per row: Title + Date + MessageCount + Delete button
 *     +-- SButton "Back to Chat" (AutoHeight)
 */
class SCUChatHistoryPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatHistoryPanel) {}
		SLATE_EVENT(FOnSessionSelected, OnSessionSelected)
		SLATE_EVENT(FOnBackRequested, OnBackRequested)
		SLATE_EVENT(FOnSessionDeleted, OnSessionDeleted)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Refresh the session list from external data */
	void RefreshSessionList(const TArray<FCUChatSession>& Sessions);

	/** Get number of visible (filtered) sessions */
	int32 GetVisibleSessionCount() const;

	/** Format a FDateTime for display (e.g., "Mar 3, 2026 14:30").
	 * Public static for testability. Uses localized date formatting.
	 * @param DateTime The date/time to format
	 * @return Formatted date string suitable for UI display
	 */
	static FString FormatSessionDate(const FDateTime& DateTime);

private:
	/** Generate a row widget for a single session entry */
	TSharedRef<ITableRow> GenerateSessionRow(TSharedPtr<FCUChatSession> Item, const TSharedRef<STableViewBase>& OwnerTable);

	/** Handle search text change — filter session list */
	void OnSearchTextChanged(const FText& NewText);

	/** Apply current filter to session list */
	void ApplyFilter();

	FOnSessionSelected OnSessionSelectedDelegate;
	FOnBackRequested OnBackRequestedDelegate;
	FOnSessionDeleted OnSessionDeletedDelegate;

	/** All sessions (unfiltered) */
	TArray<TSharedPtr<FCUChatSession>> AllSessions;

	/** Filtered sessions (displayed in list view) */
	TArray<TSharedPtr<FCUChatSession>> FilteredSessions;

	/** Current search/filter text */
	FString FilterText;

	TSharedPtr<SListView<TSharedPtr<FCUChatSession>>> SessionListView;
};
