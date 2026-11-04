// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5657: AI Chat History Panel implementation

#include "Slate/SCUChatHistoryPanel.h"
#include "CUChatBackendClient.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Views/STableRow.h"

namespace SAIChatHistoryPanelConstants
{
	static constexpr int32 kSessionTitleFontSize = 10;
	static constexpr int32 kSessionMetaFontSize = 8;
	static constexpr int32 kDeleteButtonFontSize = 9;
}

void SCUChatHistoryPanel::Construct(const FArguments& InArgs)
{
	OnSessionSelectedDelegate = InArgs._OnSessionSelected;
	OnBackRequestedDelegate = InArgs._OnBackRequested;
	OnSessionDeletedDelegate = InArgs._OnSessionDeleted;

	ChildSlot
	[
		SNew(SVerticalBox)

		// Search box
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 8.0f, 8.0f, 4.0f)
		[
			SNew(SSearchBox)
			.HintText(FText::FromString(TEXT("Search sessions...")))
			.OnTextChanged(this, &SCUChatHistoryPanel::OnSearchTextChanged)
		]

		// Session list
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(8.0f, 4.0f)
		[
			SAssignNew(SessionListView, SListView<TSharedPtr<FCUChatSession>>)
			.ListItemsSource(&FilteredSessions)
			.OnGenerateRow(this, &SCUChatHistoryPanel::GenerateSessionRow)
			.SelectionMode(ESelectionMode::Single)
			.OnSelectionChanged_Lambda([this](TSharedPtr<FCUChatSession> Item, ESelectInfo::Type)
			{
				if (Item.IsValid())
				{
					OnSessionSelectedDelegate.ExecuteIfBound(Item->SessionId);
				}
			})
		]

		// Back to Chat button
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.0f, 4.0f, 8.0f, 8.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.OnClicked_Lambda([this]()
			{
				OnBackRequestedDelegate.ExecuteIfBound();
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Back to Chat")))
			]
		]
	];
}

void SCUChatHistoryPanel::RefreshSessionList(const TArray<FCUChatSession>& Sessions)
{
	AllSessions.Empty();
	AllSessions.Reserve(Sessions.Num());
	for (const FCUChatSession& Session : Sessions)
	{
		AllSessions.Emplace(MakeShared<FCUChatSession>(Session));
	}
	ApplyFilter();
}

int32 SCUChatHistoryPanel::GetVisibleSessionCount() const
{
	return FilteredSessions.Num();
}

TSharedRef<ITableRow> SCUChatHistoryPanel::GenerateSessionRow(
	TSharedPtr<FCUChatSession> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Title = Item->GetDisplayTitle();
	const FString DateStr = FormatSessionDate(Item->UpdatedAt);
	const int32 MsgCount = Item->GetMessageCount();
	const FString SessionId = Item->SessionId;

	return SNew(STableRow<TSharedPtr<FCUChatSession>>, OwnerTable)
	[
		SNew(SHorizontalBox)

		// Title + metadata
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(4.0f, 6.0f)
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Title))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", SAIChatHistoryPanelConstants::kSessionTitleFontSize))
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%s  |  %d messages"), *DateStr, MsgCount)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", SAIChatHistoryPanelConstants::kSessionMetaFontSize))
				.ColorAndOpacity(FSlateColor(FLinearColor::Gray))
			]
		]

		// Delete button
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(4.0f, 0.0f)
		[
			SNew(SButton)
			.ToolTipText(FText::FromString(TEXT("Delete session")))
			.OnClicked_Lambda([this, SessionId]()
			{
				OnSessionDeletedDelegate.ExecuteIfBound(SessionId);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("X")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", SAIChatHistoryPanelConstants::kDeleteButtonFontSize))
				.ColorAndOpacity(FSlateColor(FLinearColor::Red))
			]
		]
	];
}

void SCUChatHistoryPanel::OnSearchTextChanged(const FText& NewText)
{
	FilterText = NewText.ToString();
	ApplyFilter();
}

void SCUChatHistoryPanel::ApplyFilter()
{
	FilteredSessions.Empty();

	if (FilterText.IsEmpty())
	{
		FilteredSessions = AllSessions;
	}
	else
	{
		for (const TSharedPtr<FCUChatSession>& Session : AllSessions)
		{
			if (Session->GetDisplayTitle().Contains(FilterText, ESearchCase::IgnoreCase))
			{
				FilteredSessions.Add(Session);
			}
		}
	}

	if (SessionListView.IsValid())
	{
		SessionListView->RequestListRefresh();
	}
}

FString SCUChatHistoryPanel::FormatSessionDate(const FDateTime& DateTime)
{
	// Format: "Mar 3, 2026 14:30"
	static const TCHAR* MonthNames[] = {
		TEXT("Jan"), TEXT("Feb"), TEXT("Mar"), TEXT("Apr"),
		TEXT("May"), TEXT("Jun"), TEXT("Jul"), TEXT("Aug"),
		TEXT("Sep"), TEXT("Oct"), TEXT("Nov"), TEXT("Dec")
	};

	const int32 MonthIndex = FMath::Clamp(DateTime.GetMonth() - 1, 0, 11);
	return FString::Printf(TEXT("%s %d, %d %02d:%02d"),
		MonthNames[MonthIndex],
		DateTime.GetDay(),
		DateTime.GetYear(),
		DateTime.GetHour(),
		DateTime.GetMinute());
}
