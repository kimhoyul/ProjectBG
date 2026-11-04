// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8836 (Phase 9-1): SCUChatBarChart — SLeafWidget bar chart for level analysis
//
// Renders horizontal bars from level_get_summary's category_breakdown / top_actor_classes.
// Uses FSlateDrawElement::MakeBox + MakeText (no SListView/SVerticalBox per-row overhead).
// Pattern reference: UE engine SGeometryCollectionHistogram, SCurveTimelineView.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateColor.h"
#include "Fonts/SlateFontInfo.h"

/** Single bar item in SCUChatBarChart (Issue #8836). */
struct FCUChatBarItem
{
	FString Label;
	double Value = 0.0;
	FLinearColor Color = FLinearColor::Gray;
};

/**
 * SLeafWidget horizontal bar chart for MCP level analysis results (Issue #8836).
 *
 * Rendering layout (left → right):
 *   [Label, fixed width] [Bar, normalized to max Value] [Value text]
 *
 * Used by FCUChatToolWidgetFactory for `ue_manage_project_ops:level_get_summary`.
 * Color mapping for ActorCategory (FCUChatBarChart::GetCategoryColor):
 *   Light=yellow, Static=blue, Blueprint=green, Volume=purple, Navigation=orange, default=gray.
 */
class SCUChatBarChart : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatBarChart)
		: _BarHeight(18.0f)
		, _BarSpacing(4.0f)
		, _LabelWidth(110.0f)
		, _ValueTextWidth(48.0f)
		, _TopPadding(2.0f)
	{}
		SLATE_ARGUMENT(TArray<FCUChatBarItem>, Items)
		SLATE_ARGUMENT(float, BarHeight)
		SLATE_ARGUMENT(float, BarSpacing)
		SLATE_ARGUMENT(float, LabelWidth)
		SLATE_ARGUMENT(float, ValueTextWidth)
		SLATE_ARGUMENT(float, TopPadding)
		SLATE_ARGUMENT(FString, Title)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	//~ Begin SWidget Interface
	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	virtual FVector2D ComputeDesiredSize(float LayoutScaleMultiplier) const override;
	//~ End SWidget Interface

	/** Map ActorCategory string → bar color (deterministic palette). */
	static FLinearColor GetCategoryColor(const FString& Category);

#if WITH_AUTOMATION_WORKER
	// Test-only accessors
	const TArray<FCUChatBarItem>& TestGetItems() const { return Items; }
	float TestGetBarHeight() const { return BarHeight; }
	float TestGetTotalDesiredHeight() const { return ComputeDesiredSize(1.0f).Y; }
#endif

private:
	TArray<FCUChatBarItem> Items;
	float BarHeight = 18.0f;
	float BarSpacing = 4.0f;
	float LabelWidth = 110.0f;
	float ValueTextWidth = 48.0f;
	float TopPadding = 2.0f;
	FString Title;

	/** Cached max value for normalization (recomputed on Construct). */
	double MaxValue = 1.0;

	/** Cached font info (avoid recreating in OnPaint). */
	FSlateFontInfo LabelFont;
	FSlateFontInfo ValueFont;
	FSlateFontInfo TitleFont;
};
