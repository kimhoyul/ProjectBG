// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8844 (Phase 11-2): SCUChatMarkerCatalog implementation

#include "Slate/SCUChatMarkerCatalog.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Misc/DateTime.h"

namespace
{
	constexpr int32 MaxActiveMarkerRows = 50;
	constexpr int32 MaxLabelChips = 30;
}

void SCUChatMarkerCatalog::Construct(const FArguments& InArgs)
{
	ActiveMarkers = InArgs._ActiveMarkers;
	AvailableLabels = InArgs._AvailableLabels;
	bLogAvailable = InArgs._LogAvailable;
	LogEntryCount = InArgs._LogEntryCount;

	if (ActiveMarkers.Num() > MaxActiveMarkerRows)
	{
		ActiveMarkers.SetNum(MaxActiveMarkerRows);
	}
	if (AvailableLabels.Num() > MaxLabelChips)
	{
		AvailableLabels.SetNum(MaxLabelChips);
	}

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)
	[
		BuildHeader()
	];

	if (ActiveMarkers.Num() > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Active Markers (%d)"), ActiveMarkers.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 1.0f)))
		];
		for (const FCUChatActiveMarker& M : ActiveMarkers)
		{
			Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				BuildMarkerRow(M)
			];
		}
	}
	else
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("(No active markers — call level_mark_transaction(action=\"start\") to record.)")))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		];
	}

	if (AvailableLabels.Num() > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Available Labels (%d)"), AvailableLabels.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 0.6f)))
		];
		Body->AddSlot().AutoHeight()
		[
			BuildLabelChips()
		];
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT(
				"Tip: call level_generate_replay_script(label=\"<label>\") to inspect a marker's transactions.")))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			.AutoWrapText(true)
		];
	}

	ChildSlot
	[
		Body
	];
}

TSharedRef<SWidget> SCUChatMarkerCatalog::BuildHeader() const
{
	const FLinearColor LogColor = bLogAvailable
		? FLinearColor(0.4f, 0.85f, 0.4f)
		: FLinearColor(1.0f, 0.55f, 0.4f);

	return SNew(SBorder)
	.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
	.Padding(FMargin(6.0f, 4.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(bLogAvailable ? TEXT("Log: AVAILABLE") : TEXT("Log: MISSING")))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(LogColor))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Entries: %d"), LogEntryCount)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f)
	];
}

TSharedRef<SWidget> SCUChatMarkerCatalog::BuildMarkerRow(const FCUChatActiveMarker& Marker) const
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// Status icon
	const FString StatusIcon = Marker.bUnclosed ? TEXT("\x26A0") : TEXT("\x2713"); // ⚠ vs ✓
	const FLinearColor StatusColor = Marker.bUnclosed
		? FLinearColor(1.0f, 0.85f, 0.25f)
		: FLinearColor(0.4f, 0.85f, 0.4f);
	Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f).VAlign(VAlign_Top)
	[
		SNew(STextBlock)
		.Text(FText::FromString(StatusIcon))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
		.ColorAndOpacity(FSlateColor(StatusColor))
	];

	// Body — label + meta
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	Body->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Marker.Label.IsEmpty() ? TEXT("(unnamed)") : Marker.Label))
		.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.6f)))
	];

	// Meta line: time + counts + graph
	FString MetaText;
	if (Marker.StartedAt > 0)
	{
		MetaText = FString::Printf(TEXT("started %s"), *FDateTime::FromUnixTimestamp(Marker.StartedAt).ToString());
	}
	if (Marker.BlueprintCount > 0)
	{
		if (!MetaText.IsEmpty()) MetaText += TEXT("  ·  ");
		MetaText += FString::Printf(TEXT("%d BP%s"), Marker.BlueprintCount,
			Marker.BlueprintCount == 1 ? TEXT("") : TEXT("s"));
	}
	if (Marker.SnapshotCount > 0)
	{
		if (!MetaText.IsEmpty()) MetaText += TEXT("  ·  ");
		MetaText += FString::Printf(TEXT("%d snapshot%s"), Marker.SnapshotCount,
			Marker.SnapshotCount == 1 ? TEXT("") : TEXT("s"));
	}
	if (!Marker.GraphName.IsEmpty())
	{
		if (!MetaText.IsEmpty()) MetaText += TEXT("  ·  ");
		MetaText += FString::Printf(TEXT("graph: %s"), *Marker.GraphName);
	}
	if (!MetaText.IsEmpty())
	{
		Body->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(MetaText))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		];
	}
	if (!Marker.Warning.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Marker.Warning))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f)))
			.AutoWrapText(true)
		];
	}

	Row->AddSlot().FillWidth(1.0f)
	[
		Body
	];

	return Row;
}

TSharedRef<SWidget> SCUChatMarkerCatalog::BuildLabelChips() const
{
	TSharedRef<SWrapBox> Wrap = SNew(SWrapBox).UseAllottedSize(true);
	for (const FString& Label : AvailableLabels)
	{
		Wrap->AddSlot().Padding(2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(5.0f, 1.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.6f)))
			]
		];
	}
	return Wrap;
}
