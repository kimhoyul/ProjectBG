// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5656: AI Chat Tool Result Widget implementation
// Issue #9174: Copy-to-clipboard button in header for tool results

#include "Slate/SCUChatToolResultWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/PrettyJsonPrintPolicy.h"

namespace ToolResultWidgetColors
{
	static const FLinearColor NoContent(0.5f, 0.5f, 0.5f);
	static const FLinearColor CollapseIndicator(0.6f, 0.6f, 0.6f);
	static const FLinearColor ToolName(0.85f, 0.85f, 0.85f);
	static const FLinearColor OperationBadge(0.5f, 0.7f, 1.0f);
	static const FLinearColor HeaderError(1.0f, 0.3f, 0.3f);
	static const FLinearColor HeaderNormal(0.4f, 0.7f, 1.0f);
	static const FLinearColor CopyButtonIdle(0.6f, 0.6f, 0.6f);
	static const FLinearColor CopyButtonActive(0.3f, 0.9f, 0.5f);
}

namespace ToolResultWidgetLayout
{
	static constexpr float DefaultMaxContentHeight = 300.0f;
	static constexpr float CopyFeedbackSeconds = 1.5f;
}

// Note: the free-function form that previously lived in an anonymous namespace
// has been promoted to SCUChatToolResultWidget::BuildPlainTextForClipboard
// (declared in the header) so unit tests can cover the serialization format
// directly. See the static member definition at the bottom of this file.

void SCUChatToolResultWidget::Construct(const FArguments& InArgs)
{
	ToolResult = InArgs._ToolResult;
	TSharedPtr<SWidget> ContentWidget = InArgs._ContentWidget;

	// Fallback if no content widget provided
	if (!ContentWidget.IsValid())
	{
		ContentWidget = SNew(STextBlock)
			.Text(NSLOCTEXT("NarshaMCP", "ToolNoContent", "(no content)"))
			.ColorAndOpacity(FSlateColor(ToolResultWidgetColors::NoContent));
	}

	ChildSlot
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
		.Padding(0.0f)
		[
			SNew(SVerticalBox)

			// Header bar — split into collapse-toggle button (FillWidth)
			// and a separate Copy button (AutoWidth, right-aligned) so
			// clicking Copy does not also toggle the collapse state.
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)

				// Collapse-toggle button — occupies most of the header
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.OnClicked(this, &SCUChatToolResultWidget::OnToggleCollapse)
					.ContentPadding(FMargin(8.0f, 4.0f))
					[
						SNew(SHorizontalBox)

						// Collapse indicator
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text_Lambda([this]() -> FText {
								return bIsExpanded
									? NSLOCTEXT("NarshaMCP", "CollapseExpanded", "\x25BC")  // ▼
									: NSLOCTEXT("NarshaMCP", "CollapseCollapsed", "\x25B6"); // ▶
							})
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(ToolResultWidgetColors::CollapseIndicator))
						]

						// Tool icon indicator
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(ToolResult.bIsError
								? NSLOCTEXT("NarshaMCP", "ToolIconError", "\x26A0")     // ⚠
								: NSLOCTEXT("NarshaMCP", "ToolIconNormal", "\x2699"))   // ⚙
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
							.ColorAndOpacity(GetHeaderColor())
						]

						// Tool name
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 4.0f, 0.0f)
						[
							SNew(STextBlock)
							.Text(GetToolDisplayName())
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(ToolResultWidgetColors::ToolName))
						]

						// Operation badge
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(SBorder)
							.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
							.Padding(FMargin(4.0f, 1.0f))
							.Visibility_Lambda([this]() -> EVisibility {
								return ToolResult.Operation.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
							})
							[
								SNew(STextBlock)
								.Text(FText::FromString(ToolResult.Operation))
								.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
								.ColorAndOpacity(FSlateColor(ToolResultWidgetColors::OperationBadge))
							]
						]

						// Spacer so Operation badge does not stretch
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
					]
				]

				// Copy-to-clipboard button (Issue #9174)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(6.0f, 2.0f))
					.ToolTipText(NSLOCTEXT("NarshaMCP", "CopyToolResultTooltip",
						"Copy tool result to clipboard (Issue #9174)"))
					.OnClicked(this, &SCUChatToolResultWidget::OnCopyClicked)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() -> FText {
							// ✓ (U+2713) is in BMP so safe for wide-char TCHAR on Windows.
							// 📋 (U+1F4CB) would need a surrogate pair and is intentionally
							// avoided — plain "Copy" text is also clearer per UX review.
							return bCopyConfirmVisible
								? NSLOCTEXT("NarshaMCP", "CopyToolResultDone", "\x2713 Copied")
								: NSLOCTEXT("NarshaMCP", "CopyToolResult",     "Copy");
						})
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity_Lambda([this]() -> FSlateColor {
							return FSlateColor(bCopyConfirmVisible
								? ToolResultWidgetColors::CopyButtonActive
								: ToolResultWidgetColors::CopyButtonIdle);
						})
					]
				]
			]

			// Content area (collapsible)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(ContentBox, SBox)
				.MaxDesiredHeight(ToolResultWidgetLayout::DefaultMaxContentHeight)
				.Padding(FMargin(8.0f, 0.0f, 8.0f, 8.0f))
				[
					SNew(SScrollBox)
					.ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()
					[
						ContentWidget.ToSharedRef()
					]
				]
			]
		]
	];
}

SCUChatToolResultWidget::~SCUChatToolResultWidget()
{
	// Issue #9174: release the one-shot "✓ Copied" ticker if it's still pending.
	// FTSTicker holds the delegate in a frame-iterable list; destroying the
	// widget without removing the handle leaves a weak-ptr lambda bound to a
	// dead SharedThis until the ticker is globally GC'd. The lambda itself is
	// weak-ptr guarded, so the dangling case is benign — but the handle leak
	// shows up in ticker-count telemetry and prevents clean shutdown timing.
	if (CopyFeedbackTimerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CopyFeedbackTimerHandle);
	}
}

FReply SCUChatToolResultWidget::OnToggleCollapse()
{
	bIsExpanded = !bIsExpanded;
	if (ContentBox.IsValid())
	{
		ContentBox->SetVisibility(bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed);
	}
	return FReply::Handled();
}

FReply SCUChatToolResultWidget::OnCopyClicked()
{
	// Issue #9174: serialize full tool result to clipboard — side-steps the
	// per-row SHorizontalBox selection boundary that made drag-copy tedious.
	const FString Payload = SCUChatToolResultWidget::BuildPlainTextForClipboard(ToolResult);
	FPlatformApplicationMisc::ClipboardCopy(*Payload);

	// Flash the button to "✓ Copied" for a short window so the user knows
	// the click registered (no dependency on SNotificationList / editor subsystem).
	bCopyConfirmVisible = true;
	if (CopyFeedbackTimerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(CopyFeedbackTimerHandle);
	}
	TWeakPtr<SCUChatToolResultWidget> WeakSelf = SharedThis(this);
	CopyFeedbackTimerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakSelf](float /*DeltaTime*/) -> bool
		{
			if (TSharedPtr<SCUChatToolResultWidget> Self = WeakSelf.Pin())
			{
				Self->bCopyConfirmVisible = false;
			}
			return false; // one-shot
		}),
		ToolResultWidgetLayout::CopyFeedbackSeconds);

	return FReply::Handled();
}

FText SCUChatToolResultWidget::GetToolDisplayName() const
{
	if (ToolResult.ToolName.IsEmpty())
	{
		return NSLOCTEXT("NarshaMCP", "ToolUnknown", "Tool Result");
	}

	// Convert "ue_analyze_symbols" → "Analyze Symbols"
	FString Display = ToolResult.ToolName;
	Display.RemoveFromStart(TEXT("ue_"));
	Display.ReplaceInline(TEXT("_"), TEXT(" "));

	// Capitalize first letter of each word
	bool bCapNext = true;
	for (int32 i = 0; i < Display.Len(); ++i)
	{
		if (bCapNext && FChar::IsAlpha(Display[i]))
		{
			Display[i] = FChar::ToUpper(Display[i]);
			bCapNext = false;
		}
		else if (Display[i] == TEXT(' '))
		{
			bCapNext = true;
		}
	}

	return FText::FromString(Display);
}

FSlateColor SCUChatToolResultWidget::GetHeaderColor() const
{
	if (ToolResult.bIsError)
	{
		return FSlateColor(ToolResultWidgetColors::HeaderError);
	}
	return FSlateColor(ToolResultWidgetColors::HeaderNormal);
}

FString SCUChatToolResultWidget::BuildPlainTextForClipboard(const FCUChatToolResult& R)
{
	FString Out;

	// Header: [ToolName / Operation]
	if (!R.ToolName.IsEmpty())
	{
		if (!R.Operation.IsEmpty())
		{
			Out += FString::Printf(TEXT("[%s / %s]\n"), *R.ToolName, *R.Operation);
		}
		else
		{
			Out += FString::Printf(TEXT("[%s]\n"), *R.ToolName);
		}
	}

	// Error message (populated only when bIsError)
	if (R.bIsError && !R.ErrorMessage.IsEmpty())
	{
		Out += FString::Printf(TEXT("Error: %s\n"), *R.ErrorMessage);
	}

	// JSON body — pretty-printed for readability when pasted elsewhere
	if (R.JsonResult.IsValid())
	{
		FString JsonString;
		TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&JsonString);
		if (FJsonSerializer::Serialize(R.JsonResult.ToSharedRef(), Writer))
		{
			Out += JsonString;
		}
	}

	if (Out.IsEmpty())
	{
		Out = TEXT("(no content)");
	}
	return Out;
}
