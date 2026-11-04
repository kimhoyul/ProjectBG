// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8843 (Phase 11-1): SCUChatReplayWidget implementation

#include "Slate/SCUChatReplayWidget.h"

#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"

namespace
{
	constexpr int32 MaxMCPCallRows = 20;
	constexpr int32 MaxPythonPreviewChars = 2000;
}

void SCUChatReplayWidget::Construct(const FArguments& InArgs)
{
	PythonScript = InArgs._PythonScript;
	MCPCalls = InArgs._MCPCalls;
	TotalEntries = InArgs._TotalEntries;
	PythonActions = InArgs._PythonActions;
	MCPActions = InArgs._MCPActions;
	Skipped = InArgs._Skipped;
	Warning = InArgs._Warning;
	LargeScriptWarning = InArgs._LargeScriptWarning;

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);

	// === Header: stats summary ===
	Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 4.0f)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(6.0f, 4.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Total: %d"), TotalEntries)))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Python: %d"), PythonActions)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.85f, 1.0f)))
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("MCP: %d"), MCPActions)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 0.4f)))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("Skipped: %d"), Skipped)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.5f)))
			]
		]
	];

	// === Warnings ===
	auto AddWarningPanel = [&Body](const FString& Text, const FLinearColor& Color)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 2.0f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 3.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("\x26A0  %s"), *Text)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 9))
				.ColorAndOpacity(FSlateColor(Color))
				.AutoWrapText(true)
			]
		];
	};
	if (!Warning.IsEmpty())
	{
		AddWarningPanel(Warning, FLinearColor(1.0f, 0.85f, 0.25f));
	}
	if (!LargeScriptWarning.IsEmpty())
	{
		AddWarningPanel(LargeScriptWarning, FLinearColor(1.0f, 0.6f, 0.4f));
	}

	// === Python script preview (with copy button) ===
	if (!PythonScript.IsEmpty())
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("Python Replay Script")))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.85f, 1.0f)))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ToolTipText(NSLOCTEXT("NarshaMCP", "CopyPythonScript", "Copy full Python script to clipboard"))
				.OnClicked(this, &SCUChatReplayWidget::OnCopyPythonClicked)
				[
					SNew(STextBlock)
					.Text(NSLOCTEXT("NarshaMCP", "CopyButton", "[ Copy ]"))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
				]
			]
		];

		// Truncate preview to keep widget responsive
		FString PreviewText = PythonScript;
		if (PreviewText.Len() > MaxPythonPreviewChars)
		{
			PreviewText = PreviewText.Left(MaxPythonPreviewChars) +
				FString::Printf(TEXT("\n... [truncated %d chars — use Copy for full script]"),
					PythonScript.Len() - MaxPythonPreviewChars);
		}
		Body->AddSlot().AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(PreviewText))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.9f, 0.8f)))
				.AutoWrapText(true)
			]
		];
	}

	// === MCP calls list ===
	if (MCPCalls.Num() > 0)
	{
		Body->AddSlot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 2.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(FString::Printf(TEXT("MCP Calls (%d)"), MCPCalls.Num())))
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.85f, 0.4f)))
		];

		const int32 NumToShow = FMath::Min(MCPCalls.Num(), MaxMCPCallRows);
		for (int32 i = 0; i < NumToShow; ++i)
		{
			const FCUChatReplayMCPCall& Call = MCPCalls[i];

			// Single-row: [domain] operation  (params preview)
			Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(FMargin(3.0f, 1.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(Call.DomainRouting.IsEmpty() ? TEXT("?") : Call.DomainRouting))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.85f, 0.5f)))
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Call.Operation))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.6f)))
				]
				+ SHorizontalBox::Slot().FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Call.ParamsPreview))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.55f, 0.55f)))
				]
			];
		}

		if (MCPCalls.Num() > NumToShow)
		{
			Body->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("... + %d more MCP calls (use Copy or run via level_replay_transactions)"),
					MCPCalls.Num() - NumToShow)))
				.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
			];
		}
	}

	// === Execution hint ===
	Body->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(TEXT(
			"Tip: paste the Python script into Editor's Output Log → Cmd → Python, "
			"or call level_replay_transactions(label=...) to run via the MCP path.")))
		.Font(FCoreStyle::GetDefaultFontStyle("Italic", 8))
		.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
		.AutoWrapText(true)
	];

	ChildSlot
	[
		Body
	];
}

FReply SCUChatReplayWidget::OnCopyPythonClicked()
{
	if (!PythonScript.IsEmpty())
	{
		FPlatformApplicationMisc::ClipboardCopy(*PythonScript);
	}
	return FReply::Handled();
}
