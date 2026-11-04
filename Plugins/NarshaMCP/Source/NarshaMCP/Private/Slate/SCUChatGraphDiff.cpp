// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8838 (Phase 9-3): SCUChatGraphDiff implementation

#include "Slate/SCUChatGraphDiff.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Misc/Paths.h"

void SCUChatGraphDiff::Construct(const FArguments& InArgs)
{
	Action = InArgs._Action;
	Label = InArgs._Label;
	SnapshotsCaptured = InArgs._SnapshotsCaptured;
	DiffsWritten = InArgs._DiffsWritten;
	Warning = InArgs._Warning;
	LogFile = InArgs._LogFile;

	const FLinearColor ActionColor = GetActionColor(Action);

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// Header: action badge + label + diffs count
	{
		TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);

		// Action badge
		Header->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(5.0f, 1.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Action.IsEmpty() ? TEXT("?") : Action.ToUpper()))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(ActionColor))
			]
		];

		// Label
		Header->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label.IsEmpty() ? TEXT("(no label)") : Label))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.6f)))
		];

		// Diffs written count
		if (DiffsWritten > 0)
		{
			Header->AddSlot().AutoWidth().Padding(8.0f, 0.0f, 0.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("%d diff%s"), DiffsWritten, DiffsWritten == 1 ? TEXT("") : TEXT("s"))))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 0.4f)))
			];
		}

		Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			Header
		];
	}

	// Snapshot count line
	if (SnapshotsCaptured > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Snapshots captured: %d"), SnapshotsCaptured)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.85f)))
		];
	}

	// Warning (e.g. PIE state)
	if (!Warning.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("\x26A0  %s"), *Warning))) // ⚠
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f)))
				.AutoWrapText(true)
			]
		];
	}

	// Hint: how to view the actual diffs
	if (DiffsWritten > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(
				TEXT("Tip: call level_get_workflow_log(filter_type=\"graph_diff\") to inspect the %d recorded diff%s."),
				DiffsWritten, DiffsWritten == 1 ? TEXT("") : TEXT("s"))))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			.AutoWrapText(true)
		];
	}

	// Log file footer (compact)
	if (!LogFile.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Log: %s"), *FPaths::GetCleanFilename(LogFile))))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
		];
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(8.0f, 6.0f))
		[
			Body
		]
	];
}

FLinearColor SCUChatGraphDiff::GetActionColor(const FString& Action)
{
	if (Action.Equals(TEXT("start"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.45f, 0.7f, 1.0f); // blue — recording started
	}
	if (Action.Equals(TEXT("end"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.4f, 0.85f, 0.4f); // green — recording ended (diff produced)
	}
	return FLinearColor(0.7f, 0.7f, 0.7f); // gray fallback
}
