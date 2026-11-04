// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Overlay implementation
// Issue #5816: M2 — severity enum colors/icons

#include "Slate/SCUAnnotationOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "HAL/PlatformTime.h" // Issue #5822: FPlatformTime for auto-fade timer

namespace AnnotationOverlayColors
{
	static const FLinearColor Suggestion(0.3f, 0.5f, 1.0f);
	static const FLinearColor Warning(1.0f, 0.8f, 0.2f);
	static const FLinearColor Error(1.0f, 0.3f, 0.3f);
	static const FLinearColor Analysis(0.65f, 0.65f, 0.65f);
	/** Info severity: soft green — distinct from Analysis gray (Issue #5816) */
	static const FLinearColor Info(0.5f, 0.7f, 0.5f);
	static const FLinearColor DismissButton(0.5f, 0.5f, 0.5f);
	static const FLinearColor DiscussButton(0.4f, 0.6f, 0.9f); // Issue #5821: blue-ish chat button
	static const FLinearColor AnnotationText(0.9f, 0.9f, 0.9f);
}

namespace AnnotationOverlayLayout
{
	static constexpr float MaxWidth = 300.0f;
}

void SCUAnnotationOverlay::Construct(const FArguments& InArgs)
{
	AnnotationId = InArgs._AnnotationId;
	OnDismiss = InArgs._OnDismiss;
	OnDiscuss = InArgs._OnDiscuss;

	// Issue #5822: Initialize auto-fade timer
	CreatedTimestamp = FPlatformTime::Seconds();
	bIsFaded = false;
	SetCanTick(true);

	const FString Category = InArgs._Category;
	const FString AnnotationText = InArgs._AnnotationText;
	const FLinearColor CategoryColor = GetCategoryColor(Category);

	ChildSlot
	[
		SNew(SBox)
		.MaxDesiredWidth(AnnotationOverlayLayout::MaxWidth)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(6.0f, 4.0f))
			.BorderBackgroundColor(FSlateColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f)))
			[
				SNew(SVerticalBox)

				// Header: icon + category label + discuss + dismiss
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 2.0f)
				[
					SNew(SHorizontalBox)

					// Category icon
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(GetCategoryIcon(Category))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(FSlateColor(CategoryColor))
					]

					// Category label
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Category.ToUpper()))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(FSlateColor(CategoryColor))
					]

					// Discuss in Chat button (Issue #5821)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.OnClicked(this, &SCUAnnotationOverlay::OnDiscussClicked)
						.ContentPadding(FMargin(2.0f, 0.0f))
						.ToolTipText(NSLOCTEXT("NarshaMCP", "AnnotationDiscussTooltip", "Discuss in Chat"))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "AnnotationDiscuss", "\x2709")) // envelope icon
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FSlateColor(AnnotationOverlayColors::DiscussButton))
						]
					]

					// Dismiss button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SButton)
						.ButtonStyle(FAppStyle::Get(), "NoBorder")
						.OnClicked(this, &SCUAnnotationOverlay::OnDismissClicked)
						.ContentPadding(FMargin(2.0f, 0.0f))
						[
							SNew(STextBlock)
							.Text(NSLOCTEXT("NarshaMCP", "AnnotationDismiss", "\x2715")) // ✕
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
							.ColorAndOpacity(FSlateColor(AnnotationOverlayColors::DismissButton))
						]
					]
				]

				// Annotation text body
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(FText::FromString(AnnotationText))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(AnnotationOverlayColors::AnnotationText))
					.AutoWrapText(true)
				]
			]
		]
	];
}

FLinearColor SCUAnnotationOverlay::GetCategoryColor(const FString& Category)
{
	if (Category == TEXT("suggestion"))
	{
		return AnnotationOverlayColors::Suggestion;
	}
	if (Category == TEXT("warning"))
	{
		return AnnotationOverlayColors::Warning;
	}
	if (Category == TEXT("error"))
	{
		return AnnotationOverlayColors::Error;
	}
	// "analysis" or any unknown category
	return AnnotationOverlayColors::Analysis;
}

FText SCUAnnotationOverlay::GetCategoryIcon(const FString& Category)
{
	if (Category == TEXT("suggestion"))
	{
		return FText::FromString(TEXT("\u2726")); // ✦ suggestion
	}
	if (Category == TEXT("warning"))
	{
		return FText::FromString(TEXT("\u26A0")); // ⚠ warning
	}
	if (Category == TEXT("error"))
	{
		return FText::FromString(TEXT("\u2716")); // ✖ error
	}
	// "analysis"
	return FText::FromString(TEXT("\u25C9")); // ◉ analysis
}

// ===================================================================
// Severity enum-based color/icon dispatch (Issue #5816)
// ===================================================================

FLinearColor SCUAnnotationOverlay::GetSeverityColor(ECUAnnotationSeverity Severity)
{
	switch (Severity)
	{
	case ECUAnnotationSeverity::Suggestion:
		return AnnotationOverlayColors::Suggestion;
	case ECUAnnotationSeverity::Warning:
		return AnnotationOverlayColors::Warning;
	case ECUAnnotationSeverity::Error:
		return AnnotationOverlayColors::Error;
	case ECUAnnotationSeverity::Info:
	default:
		return AnnotationOverlayColors::Info;
	}
}

FText SCUAnnotationOverlay::GetSeverityIcon(ECUAnnotationSeverity Severity)
{
	switch (Severity)
	{
	case ECUAnnotationSeverity::Suggestion:
		return FText::FromString(TEXT("\u2726")); // ✦
	case ECUAnnotationSeverity::Warning:
		return FText::FromString(TEXT("\u26A0")); // ⚠
	case ECUAnnotationSeverity::Error:
		return FText::FromString(TEXT("\u2716")); // ✖
	case ECUAnnotationSeverity::Info:
	default:
		return FText::FromString(TEXT("\u2139")); // ℹ
	}
}

// === Auto-fade state machine (Issue #5822: M8 UX) ===
// State: !bIsFaded → Tick checks elapsed > 15s → bIsFaded, opacity=0.5
// Hover: opacity=1.0, timer resets (bIsFaded preserved)
// Leave: if bIsFaded, restore opacity=0.5; else no-op

void SCUAnnotationOverlay::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	if (!bIsFaded && (FPlatformTime::Seconds() - CreatedTimestamp) > FadeDelaySeconds)
	{
		bIsFaded = true;
		SetRenderOpacity(FadedOpacity);
	}
}

void SCUAnnotationOverlay::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseEnter(MyGeometry, MouseEvent);
	SetRenderOpacity(1.0f);
	CreatedTimestamp = FPlatformTime::Seconds();
}

void SCUAnnotationOverlay::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	SCompoundWidget::OnMouseLeave(MouseEvent);
	if (bIsFaded)
	{
		SetRenderOpacity(FadedOpacity);
	}
}

FReply SCUAnnotationOverlay::OnDismissClicked()
{
	OnDismiss.ExecuteIfBound();
	return FReply::Handled();
}

FReply SCUAnnotationOverlay::OnDiscussClicked()
{
	OnDiscuss.ExecuteIfBound();
	return FReply::Handled();
}
