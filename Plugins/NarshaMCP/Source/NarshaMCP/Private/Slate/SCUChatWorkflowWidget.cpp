// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8845 (Phase 11-3): SCUChatWorkflowWidget implementation

#include "Slate/SCUChatWorkflowWidget.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	constexpr int32 MaxStepRows = 50;
}

void SCUChatWorkflowWidget::Construct(const FArguments& InArgs)
{
	Mode = InArgs._Mode;
	Hint = InArgs._Hint;
	DependencyWarning = InArgs._DependencyWarning;
	PythonActions = InArgs._PythonActions;
	MCPActions = InArgs._MCPActions;
	SuccessCount = InArgs._SuccessCount;
	FailCount = InArgs._FailCount;
	TotalCount = InArgs._TotalCount;
	Steps = InArgs._Steps;

	if (Steps.Num() > MaxStepRows)
	{
		Steps.SetNum(MaxStepRows);
	}

	const bool bIsExecuteMode = Mode.StartsWith(TEXT("execute"))
		|| Mode.StartsWith(TEXT("batch_execute"));

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// === Header strip: mode badge + counts ===
	{
		TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);

		// Mode badge
		Header->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f).VAlign(VAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(5.0f, 1.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Mode.IsEmpty() ? TEXT("?") : Mode.ToUpper()))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
				.ColorAndOpacity(FSlateColor(GetModeColor(Mode)))
			]
		];

		// Counts: differ by mode
		if (bIsExecuteMode)
		{
			Header->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Total: %d"), TotalCount)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
			];
			Header->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("OK: %d"), SuccessCount)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 0.4f)))
			];
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("FAIL: %d"), FailCount)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FailCount > 0
					? FLinearColor(1.0f, 0.35f, 0.35f)
					: FLinearColor(0.5f, 0.5f, 0.5f)))
			];
		}
		else
		{
			// Preview mode: show planned action counts
			Header->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Python: %d"), PythonActions)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.85f, 1.0f)))
			];
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("MCP: %d"), MCPActions)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 0.4f)))
			];
		}

		Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			[
				Header
			]
		];
	}

	// === Hint (preview only — explains it's a dry-run) ===
	if (!Hint.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Hint))
			.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.85f)))
			.AutoWrapText(true)
		];
	}

	// === Dependency warning (preview only) ===
	if (!DependencyWarning.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 4.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("\x26A0  %s"), *DependencyWarning))) // ⚠
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.25f)))
				.AutoWrapText(true)
			]
		];
	}

	// === Steps timeline (execute mode) ===
	if (Steps.Num() > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("Steps (%d)"), Steps.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 1.0f)))
		];
		for (const FCUChatWorkflowStep& Step : Steps)
		{
			const FLinearColor StepColor = Step.bSuccess
				? FLinearColor(0.4f, 0.85f, 0.4f)
				: FLinearColor(1.0f, 0.35f, 0.35f);
			const FString StatusIcon = Step.bSuccess ? TEXT("\x2713") : TEXT("\x2716"); // ✓ vs ✖

			TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(StatusIcon))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.ColorAndOpacity(FSlateColor(StepColor))
			];
			Row->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Step.StepName))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
			];
			Row->AddSlot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Step.Error.IsEmpty()
					? FString::Printf(TEXT("%.1f ms"), Step.TimeMs)
					: FString::Printf(TEXT("%s  ·  %.1f ms"), *Step.Error, Step.TimeMs)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(Step.Error.IsEmpty()
					? FLinearColor(0.5f, 0.5f, 0.5f)
					: FLinearColor(1.0f, 0.45f, 0.35f)))
			];
			Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				Row
			];
		}
	}

	ChildSlot
	[
		Body
	];
}

FLinearColor SCUChatWorkflowWidget::GetModeColor(const FString& Mode)
{
	if (Mode.Equals(TEXT("preview"), ESearchCase::IgnoreCase) ||
	    Mode.Equals(TEXT("batch_dry_run"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.55f, 0.7f, 1.0f); // blue — preview
	}
	if (Mode.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
	    Mode.Equals(TEXT("batch_execute"), ESearchCase::IgnoreCase))
	{
		return FLinearColor(0.4f, 0.85f, 0.4f); // green — execute
	}
	return FLinearColor(0.7f, 0.7f, 0.7f); // gray fallback
}
