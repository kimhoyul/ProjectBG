// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8837 (Phase 9-2): SCUChatSpatialGrid implementation

#include "Slate/SCUChatSpatialGrid.h"

#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Hard limit so a pathological cells[] array doesn't try to render a 10,000-cell grid.
	constexpr int32 MaxGridDimension = 64;
	constexpr float HeaderHeight = 18.0f;
	constexpr float HeaderPadding = 4.0f;
}

void SCUChatSpatialGrid::Construct(const FArguments& InArgs)
{
	Cells = InArgs._Cells;
	MaxDensity = InArgs._MaxDensity > 0.0 ? InArgs._MaxDensity : 1.0;
	DensityP95 = InArgs._DensityP95 > 0.0 ? InArgs._DensityP95 : MaxDensity;
	ActorsWithPosition = InArgs._ActorsWithPosition;
	ActorsWithoutPosition = InArgs._ActorsWithoutPosition;
	CellPixelSize = InArgs._CellPixelSize;
	MinDimension = InArgs._MinDimension;
	MaxDimension = InArgs._MaxDimension;

	HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);

	if (Cells.Num() == 0)
	{
		GridMinX = GridMaxX = 0;
		GridMinY = GridMaxY = 0;
		return;
	}

	// Compute bounds
	GridMinX = Cells[0].CellX;
	GridMaxX = Cells[0].CellX;
	GridMinY = Cells[0].CellY;
	GridMaxY = Cells[0].CellY;
	for (const FCUSpatialCell& Cell : Cells)
	{
		GridMinX = FMath::Min(GridMinX, Cell.CellX);
		GridMaxX = FMath::Max(GridMaxX, Cell.CellX);
		GridMinY = FMath::Min(GridMinY, Cell.CellY);
		GridMaxY = FMath::Max(GridMaxY, Cell.CellY);
	}

	// Clamp to MaxGridDimension to avoid extreme rendering loops
	const int32 W = GridMaxX - GridMinX + 1;
	const int32 H = GridMaxY - GridMinY + 1;
	if (W > MaxGridDimension)
	{
		GridMaxX = GridMinX + MaxGridDimension - 1;
	}
	if (H > MaxGridDimension)
	{
		GridMaxY = GridMinY + MaxGridDimension - 1;
	}
}

FVector2D SCUChatSpatialGrid::ComputeDesiredSize(float /*LayoutScaleMultiplier*/) const
{
	if (Cells.Num() == 0)
	{
		return FVector2D(MinDimension, HeaderHeight + HeaderPadding * 2.0f);
	}
	const int32 W = GridMaxX - GridMinX + 1;
	const int32 H = GridMaxY - GridMinY + 1;
	const float GridWidthPx = FMath::Clamp((float)W * CellPixelSize, MinDimension, MaxDimension);
	const float GridHeightPx = FMath::Clamp((float)H * CellPixelSize, MinDimension, MaxDimension);
	return FVector2D(GridWidthPx, HeaderHeight + HeaderPadding + GridHeightPx + HeaderPadding);
}

int32 SCUChatSpatialGrid::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	if (!WhiteBrush)
	{
		WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	}

	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();

	// Header
	const FString HeaderText = FString::Printf(
		TEXT("Cells: %d  ·  Max Density: %.2f  ·  P95: %.2f  ·  Positioned: %d / Skipped: %d"),
		Cells.Num(), (float)MaxDensity, (float)DensityP95, ActorsWithPosition, ActorsWithoutPosition);
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2D(LocalSize.X, HeaderHeight), FSlateLayoutTransform(FVector2D(2.0f, 0.0f))),
		FText::FromString(HeaderText),
		HeaderFont,
		ESlateDrawEffect::None,
		FLinearColor(0.6f, 0.85f, 1.0f, 1.0f)
	);

	if (Cells.Num() == 0)
	{
		return LayerId + 1;
	}

	const int32 W = GridMaxX - GridMinX + 1;
	const int32 H = GridMaxY - GridMinY + 1;

	const float GridTop = HeaderHeight + HeaderPadding;
	const float GridAvailW = LocalSize.X;
	const float GridAvailH = FMath::Max(20.0f, LocalSize.Y - GridTop - HeaderPadding);

	const float CellW = FMath::Max(2.0f, GridAvailW / W);
	const float CellH = FMath::Max(2.0f, GridAvailH / H);

	// Background grid (dim base) — single MakeBox call
	FSlateDrawElement::MakeBox(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2D(CellW * W, CellH * H), FSlateLayoutTransform(FVector2D(0.0f, GridTop))),
		WhiteBrush,
		ESlateDrawEffect::None,
		FLinearColor(0.06f, 0.06f, 0.08f, 1.0f)
	);

	// Cell heat colors
	const double NormalizingDenominator = FMath::Max(DensityP95, KINDA_SMALL_NUMBER);
	for (const FCUSpatialCell& Cell : Cells)
	{
		const int32 RelX = Cell.CellX - GridMinX;
		const int32 RelY = Cell.CellY - GridMinY;
		if (RelX < 0 || RelX >= W || RelY < 0 || RelY >= H) continue;

		const double Norm = FMath::Clamp(Cell.Density / NormalizingDenominator, 0.0, 1.0);
		const FLinearColor HeatColor = DensityToColor(Norm);

		const float PosX = (float)RelX * CellW;
		const float PosY = GridTop + (float)RelY * CellH;

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(CellW - 0.5f, CellH - 0.5f), FSlateLayoutTransform(FVector2D(PosX, PosY))),
			WhiteBrush,
			ESlateDrawEffect::None,
			HeatColor
		);
	}

	return LayerId + 2;
}

FLinearColor SCUChatSpatialGrid::DensityToColor(double NormalizedDensity)
{
	const float t = (float)FMath::Clamp(NormalizedDensity, 0.0, 1.0);

	// Cool→hot interpolation: dim gray (0) → blue (0.25) → green (0.5) → yellow (0.75) → red (1.0)
	if (t < 0.25f)
	{
		const float u = t / 0.25f;
		return FMath::Lerp(FLinearColor(0.18f, 0.18f, 0.22f, 1.0f), FLinearColor(0.20f, 0.45f, 0.95f, 1.0f), u);
	}
	if (t < 0.5f)
	{
		const float u = (t - 0.25f) / 0.25f;
		return FMath::Lerp(FLinearColor(0.20f, 0.45f, 0.95f, 1.0f), FLinearColor(0.30f, 0.85f, 0.45f, 1.0f), u);
	}
	if (t < 0.75f)
	{
		const float u = (t - 0.5f) / 0.25f;
		return FMath::Lerp(FLinearColor(0.30f, 0.85f, 0.45f, 1.0f), FLinearColor(0.95f, 0.85f, 0.25f, 1.0f), u);
	}
	const float u = (t - 0.75f) / 0.25f;
	return FMath::Lerp(FLinearColor(0.95f, 0.85f, 0.25f, 1.0f), FLinearColor(0.95f, 0.30f, 0.25f, 1.0f), u);
}
