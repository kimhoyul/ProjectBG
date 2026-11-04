// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8836 (Phase 9-1): SCUChatActorList implementation

#include "Slate/SCUChatActorList.h"
#include "Slate/SCUChatBarChart.h" // For GetCategoryColor

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxActorRows = 100;
}

void SCUChatActorList::Construct(const FArguments& InArgs)
{
	Actors = InArgs._Actors;
	TotalMatches = InArgs._TotalMatches;

	if (Actors.Num() > MaxActorRows)
	{
		Actors.SetNum(MaxActorRows);
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Header line: count summary
	const FString HeaderText = (TotalMatches > Actors.Num())
		? FString::Printf(TEXT("Showing %d of %d actors"), Actors.Num(), TotalMatches)
		: FString::Printf(TEXT("%d actors"), Actors.Num());

	Box->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(HeaderText))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 1.0f)))
	];

	// Actor rows
	for (const FCUChatActorEntry& Entry : Actors)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			BuildActorRow(Entry)
		];
	}

	ChildSlot
	[
		Box
	];
}

TSharedRef<SWidget> SCUChatActorList::BuildActorRow(const FCUChatActorEntry& Entry) const
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// Category badge (color-coded)
	Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(4.0f, 1.0f))
		[
			SNew(STextBlock)
			.Text(FText::FromString(Entry.Category))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
			.ColorAndOpacity(FSlateColor(SCUChatBarChart::GetCategoryColor(Entry.Category)))
		]
	];

	// BP indicator
	if (Entry.bIsBlueprint)
	{
		Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 4.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("BP")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 7))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 0.4f)))
		];
	}

	// Actor class name + level + path/size
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Entry.ActorClass))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.6f)))
	];

	FString MetaText = Entry.LevelName;
	if (!Entry.BlueprintClassPath.IsEmpty())
	{
		MetaText = FString::Printf(TEXT("%s  ·  %s"), *Entry.LevelName, *Entry.BlueprintClassPath);
	}
	if (Entry.FileSize > 0)
	{
		MetaText += FString::Printf(TEXT("  ·  %.1f KB"), Entry.FileSize / 1024.0);
	}

	Body->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(MetaText))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		.Visibility(MetaText.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
	];

	Row->AddSlot().FillWidth(1.0f)
	[
		Body
	];

	return Row;
}
