// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8836 (Phase 9-1): SCUChatDiagnosticCard implementation

#include "Slate/SCUChatDiagnosticCard.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxRowsPerSection = 50;
}

void SCUChatDiagnosticCard::Construct(const FArguments& InArgs)
{
	ErrorCount = InArgs._ErrorCount;
	WarningCount = InArgs._WarningCount;
	InfoCount = InArgs._InfoCount;
	Findings = InArgs._Findings;
	Suggestions = InArgs._Suggestions;

	if (Findings.Num() > MaxRowsPerSection)
	{
		Findings.SetNum(MaxRowsPerSection);
	}
	if (Suggestions.Num() > MaxRowsPerSection)
	{
		Suggestions.SetNum(MaxRowsPerSection);
	}

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	// Header
	Box->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f, 0.0f, 4.0f)
	[
		BuildHeader()
	];

	// Findings section
	if (Findings.Num() > 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Findings (%d)"), Findings.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.6f)))
		];
		for (const FCUChatDiagnosticEntry& Entry : Findings)
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				BuildEntryRow(Entry, /*bIsSuggestion=*/false)
			];
		}
	}

	// Suggestions section
	if (Suggestions.Num() > 0)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Suggestions (%d)"), Suggestions.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 1.0f)))
		];
		for (const FCUChatDiagnosticEntry& Entry : Suggestions)
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				BuildEntryRow(Entry, /*bIsSuggestion=*/true)
			];
		}
	}

	ChildSlot
	[
		Box
	];
}

TSharedRef<SWidget> SCUChatDiagnosticCard::BuildHeader() const
{
	return SNew(SBorder)
	.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
	.Padding(FMargin(6.0f, 4.0f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Errors: %d"), ErrorCount)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(GetSeverityColor(ECUChatDiagnosticSeverity::Error)))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Warnings: %d"), WarningCount)))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(GetSeverityColor(ECUChatDiagnosticSeverity::Warning)))
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Infos: %d"), InfoCount)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(GetSeverityColor(ECUChatDiagnosticSeverity::Info)))
		]
	];
}

TSharedRef<SWidget> SCUChatDiagnosticCard::BuildEntryRow(const FCUChatDiagnosticEntry& Entry, bool bIsSuggestion) const
{
	const FLinearColor SevColor = GetSeverityColor(Entry.Severity);
	const FString Icon = bIsSuggestion ? TEXT("\x2192") /* → */ : GetSeverityIcon(Entry.Severity);

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);

	// Icon
	Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f).VAlign(VAlign_Top)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Icon))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
		.ColorAndOpacity(FSlateColor(SevColor))
	];

	// Body (rule + message + entity / action)
	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// Top line: rule [+ entity]
	{
		FString TopText = Entry.Rule;
		if (!Entry.Entity.IsEmpty())
		{
			TopText = FString::Printf(TEXT("%s  [%s]"), *Entry.Rule, *Entry.Entity);
		}
		Body->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Text(FText::FromString(TopText))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
		];
	}

	// Message
	Body->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Entry.Message))
		.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f)))
		.AutoWrapText(true)
	];

	// Bottom: value/threshold (findings) or action (suggestions)
	if (bIsSuggestion && !Entry.Action.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Action: %s"), *Entry.Action)))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.85f, 0.55f)))
		];
	}
	else if (!bIsSuggestion && (Entry.Value != 0.0 || Entry.Threshold != 0.0))
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Value: %g  /  Threshold: %g"), Entry.Value, Entry.Threshold)))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
		];
	}

	Row->AddSlot().FillWidth(1.0f)
	[
		Body
	];

	return Row;
}

FLinearColor SCUChatDiagnosticCard::GetSeverityColor(ECUChatDiagnosticSeverity Sev)
{
	switch (Sev)
	{
		case ECUChatDiagnosticSeverity::Error:      return FLinearColor(1.0f, 0.35f, 0.35f);
		case ECUChatDiagnosticSeverity::Warning:    return FLinearColor(1.0f, 0.85f, 0.25f);
		case ECUChatDiagnosticSeverity::Info:       return FLinearColor(0.45f, 0.7f, 1.0f);
		case ECUChatDiagnosticSeverity::Suggestion: return FLinearColor(0.55f, 0.85f, 0.55f);
		default:                                    return FLinearColor::Gray;
	}
}

FString SCUChatDiagnosticCard::GetSeverityIcon(ECUChatDiagnosticSeverity Sev)
{
	switch (Sev)
	{
		case ECUChatDiagnosticSeverity::Error:      return TEXT("\x2716"); // ✖
		case ECUChatDiagnosticSeverity::Warning:    return TEXT("\x26A0"); // ⚠
		case ECUChatDiagnosticSeverity::Info:       return TEXT("\x2139"); // ℹ
		case ECUChatDiagnosticSeverity::Suggestion: return TEXT("\x2192"); // →
		default:                                    return TEXT("?");
	}
}

ECUChatDiagnosticSeverity SCUChatDiagnosticCard::ParseSeverityString(const FString& Str)
{
	if (Str.Equals(TEXT("Error"), ESearchCase::IgnoreCase))      return ECUChatDiagnosticSeverity::Error;
	if (Str.Equals(TEXT("Warning"), ESearchCase::IgnoreCase) ||
	    Str.Equals(TEXT("Warn"), ESearchCase::IgnoreCase))       return ECUChatDiagnosticSeverity::Warning;
	if (Str.Equals(TEXT("Info"), ESearchCase::IgnoreCase))       return ECUChatDiagnosticSeverity::Info;
	if (Str.Equals(TEXT("Suggestion"), ESearchCase::IgnoreCase)) return ECUChatDiagnosticSeverity::Suggestion;
	return ECUChatDiagnosticSeverity::Info;
}
