// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8837 (Phase 9-2): SCUChatSpatialGrid — density heatmap for level_spatial_analysis

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Fonts/SlateFontInfo.h"

/** Single cell in the spatial heat-map. */
struct FCUSpatialCell
{
	int32 CellX = 0;
	int32 CellY = 0;
	int32 ActorCount = 0;
	double Density = 0.0;
};

/**
 * Density heat-map for `ue_manage_project_ops:level_spatial_analysis` (Issue #8837).
 *
 * Renders a uniform grid where each cell is colored by density:
 *   density=0 → dim gray, density≥P95 → bright red, linear lerp between.
 *
 * Grid bounds (min/max X/Y) are derived from the supplied cells; missing
 * cells in between are drawn as the empty-state color.
 *
 * SLeafWidget + OnPaint pattern (FSlateDrawElement::MakeBox).
 */
class SCUChatSpatialGrid : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatSpatialGrid)
		: _MaxDensity(1.0)
		, _DensityP95(1.0)
		, _ActorsWithPosition(0)
		, _ActorsWithoutPosition(0)
		, _CellPixelSize(14.0f)
		, _MinDimension(180.0f)
		, _MaxDimension(380.0f)
	{}
		SLATE_ARGUMENT(TArray<FCUSpatialCell>, Cells)
		SLATE_ARGUMENT(double, MaxDensity)
		SLATE_ARGUMENT(double, DensityP95)
		SLATE_ARGUMENT(int32, ActorsWithPosition)
		SLATE_ARGUMENT(int32, ActorsWithoutPosition)
		SLATE_ARGUMENT(float, CellPixelSize)
		SLATE_ARGUMENT(float, MinDimension)
		SLATE_ARGUMENT(float, MaxDimension)
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

	/** Convert a normalized density [0..1] to heat color. */
	static FLinearColor DensityToColor(double NormalizedDensity);

#if WITH_AUTOMATION_WORKER
	int32 TestGetCellCount() const { return Cells.Num(); }
	int32 TestGetGridWidth() const { return GridMaxX - GridMinX + 1; }
	int32 TestGetGridHeight() const { return GridMaxY - GridMinY + 1; }
	double TestGetMaxDensity() const { return MaxDensity; }
	double TestGetDensityP95() const { return DensityP95; }
#endif

private:
	TArray<FCUSpatialCell> Cells;
	double MaxDensity = 1.0;
	double DensityP95 = 1.0;
	int32 ActorsWithPosition = 0;
	int32 ActorsWithoutPosition = 0;
	float CellPixelSize = 14.0f;
	float MinDimension = 180.0f;
	float MaxDimension = 380.0f;

	// Computed grid bounds (cached in Construct).
	int32 GridMinX = 0;
	int32 GridMaxX = 0;
	int32 GridMinY = 0;
	int32 GridMaxY = 0;

	FSlateFontInfo HeaderFont;
};
