// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8836 (Phase 9-1): SCUChatBarChart implementation

#include "Slate/SCUChatBarChart.h"

#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
	constexpr int32 MaxBarItems = 50; // truncate huge category lists
}

void SCUChatBarChart::Construct(const FArguments& InArgs)
{
	Items = InArgs._Items;
	BarHeight = InArgs._BarHeight;
	BarSpacing = InArgs._BarSpacing;
	LabelWidth = InArgs._LabelWidth;
	ValueTextWidth = InArgs._ValueTextWidth;
	TopPadding = InArgs._TopPadding;
	Title = InArgs._Title;

	if (Items.Num() > MaxBarItems)
	{
		Items.SetNum(MaxBarItems);
	}

	// Compute max value once for OnPaint normalization
	MaxValue = 1.0;
	for (const FCUChatBarItem& Item : Items)
	{
		if (Item.Value > MaxValue)
		{
			MaxValue = Item.Value;
		}
	}

	LabelFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
	ValueFont = FCoreStyle::GetDefaultFontStyle("Bold", 9);
	TitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 10);
}

FVector2D SCUChatBarChart::ComputeDesiredSize(float /*LayoutScaleMultiplier*/) const
{
	const float TitleSpace = Title.IsEmpty() ? 0.0f : (BarHeight + BarSpacing);
	const float ItemsSpace = Items.Num() > 0
		? (Items.Num() * BarHeight + (Items.Num() - 1) * BarSpacing)
		: 0.0f;
	const float TotalHeight = TopPadding + TitleSpace + ItemsSpace + TopPadding;
	const float DesiredWidth = LabelWidth + 200.0f + ValueTextWidth + 16.0f;
	return FVector2D(DesiredWidth, FMath::Max(TotalHeight, BarHeight));
}

int32 SCUChatBarChart::OnPaint(
	const FPaintArgs& Args,
	const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements,
	int32 LayerId,
	const FWidgetStyle& InWidgetStyle,
	bool bParentEnabled) const
{
	const FVector2D LocalSize = AllottedGeometry.GetLocalSize();
	const float Width = LocalSize.X;
	const float BarAreaX = LabelWidth + 8.0f;
	const float BarAreaWidth = FMath::Max(20.0f, Width - LabelWidth - ValueTextWidth - 16.0f);

	const FSlateBrush* WhiteBrush = FAppStyle::GetBrush("WhiteBrush");
	if (!WhiteBrush)
	{
		WhiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	}

	float CursorY = TopPadding;

	// Title (optional)
	if (!Title.IsEmpty())
	{
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(Width, BarHeight), FSlateLayoutTransform(FVector2D(4.0f, CursorY))),
			FText::FromString(Title),
			TitleFont,
			ESlateDrawEffect::None,
			FLinearColor(0.95f, 0.95f, 0.95f, 1.0f)
		);
		CursorY += BarHeight + BarSpacing;
	}

	// Issue #10756: defensive — OnPaint should only fire when this widget is in a live
	// Slate tree, so FSlateApplication is normally up. Guard anyway so we never crash
	// on widgets that get painted via test harnesses without a real Slate runtime.
	if (!FSlateApplication::IsInitialized() || FSlateApplication::Get().GetRenderer() == nullptr)
	{
		return LayerId;
	}
	const TSharedRef<FSlateFontMeasure> FontMeasure = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();

	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		const FCUChatBarItem& Item = Items[Index];

		// Label (left)
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(LabelWidth, BarHeight), FSlateLayoutTransform(FVector2D(0.0f, CursorY + 2.0f))),
			FText::FromString(Item.Label),
			LabelFont,
			ESlateDrawEffect::None,
			FLinearColor(0.85f, 0.85f, 0.85f, 1.0f)
		);

		// Bar (background dim)
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(BarAreaWidth, BarHeight - 2.0f), FSlateLayoutTransform(FVector2D(BarAreaX, CursorY + 1.0f))),
			WhiteBrush,
			ESlateDrawEffect::None,
			FLinearColor(0.12f, 0.12f, 0.12f, 0.6f)
		);

		// Bar (filled)
		const float Ratio = MaxValue > 0.0 ? (float)(Item.Value / MaxValue) : 0.0f;
		const float FilledWidth = FMath::Clamp(BarAreaWidth * Ratio, 1.0f, BarAreaWidth);
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(FilledWidth, BarHeight - 2.0f), FSlateLayoutTransform(FVector2D(BarAreaX, CursorY + 1.0f))),
			WhiteBrush,
			ESlateDrawEffect::None,
			Item.Color
		);

		// Value text (right of bar)
		const FString ValueText = FString::Printf(TEXT("%g"), Item.Value);
		const FVector2D ValueSize = FontMeasure->Measure(ValueText, ValueFont);
		FSlateDrawElement::MakeText(
			OutDrawElements,
			LayerId + 2,
			AllottedGeometry.ToPaintGeometry(FVector2D(ValueTextWidth, BarHeight), FSlateLayoutTransform(FVector2D(BarAreaX + BarAreaWidth + 4.0f, CursorY + 2.0f))),
			FText::FromString(ValueText),
			ValueFont,
			ESlateDrawEffect::None,
			FLinearColor(0.7f, 0.9f, 0.7f, 1.0f)
		);

		CursorY += BarHeight + BarSpacing;
	}

	return LayerId + 3;
}

FLinearColor SCUChatBarChart::GetCategoryColor(const FString& Category)
{
	// Match Rust ActorCategory enum (rust_indexer::level_content::ActorCategory).
	if (Category.Equals(TEXT("Light"), ESearchCase::IgnoreCase))         return FLinearColor(1.0f, 0.85f, 0.2f);  // yellow
	if (Category.Equals(TEXT("Static"), ESearchCase::IgnoreCase) ||
	    Category.Equals(TEXT("StaticMesh"), ESearchCase::IgnoreCase))    return FLinearColor(0.35f, 0.65f, 1.0f); // blue
	if (Category.Equals(TEXT("Blueprint"), ESearchCase::IgnoreCase))     return FLinearColor(0.4f, 0.85f, 0.4f);  // green
	if (Category.Equals(TEXT("Volume"), ESearchCase::IgnoreCase))        return FLinearColor(0.75f, 0.5f, 1.0f);  // purple
	if (Category.Equals(TEXT("Navigation"), ESearchCase::IgnoreCase) ||
	    Category.Equals(TEXT("Nav"), ESearchCase::IgnoreCase))           return FLinearColor(1.0f, 0.6f, 0.25f);  // orange
	if (Category.Equals(TEXT("Audio"), ESearchCase::IgnoreCase) ||
	    Category.Equals(TEXT("Sound"), ESearchCase::IgnoreCase))         return FLinearColor(1.0f, 0.4f, 0.7f);   // pink
	if (Category.Equals(TEXT("FX"), ESearchCase::IgnoreCase) ||
	    Category.Equals(TEXT("Effect"), ESearchCase::IgnoreCase))        return FLinearColor(0.95f, 0.55f, 0.2f); // amber
	if (Category.Equals(TEXT("Camera"), ESearchCase::IgnoreCase))        return FLinearColor(0.6f, 0.85f, 0.95f); // sky
	if (Category.Equals(TEXT("Trigger"), ESearchCase::IgnoreCase))       return FLinearColor(0.95f, 0.45f, 0.45f); // red
	if (Category.Equals(TEXT("Decal"), ESearchCase::IgnoreCase))         return FLinearColor(0.8f, 0.7f, 0.5f);   // tan
	if (Category.Equals(TEXT("Gameplay"), ESearchCase::IgnoreCase) ||
	    Category.Equals(TEXT("Spawner"), ESearchCase::IgnoreCase))       return FLinearColor(0.5f, 0.95f, 0.7f);  // teal
	return FLinearColor(0.6f, 0.6f, 0.6f, 1.0f); // default gray
}
