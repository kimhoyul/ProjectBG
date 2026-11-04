// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8841 (Phase 10-2): SCUChatTimelineBar implementation

#include "Slate/SCUChatTimelineBar.h"

#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Editor.h"
#include "CUWebSocketEditorSubsystem.h"
#include "Misc/DateTime.h"

namespace
{
	constexpr int32 NumBuckets = 24;
}

void SCUChatTimelineBar::Construct(const FArguments& InArgs)
{
	BarHeight = InArgs._BarHeight;
	BarSpacing = InArgs._BarSpacing;
	BarWidth = InArgs._BarWidth;

	HeaderFont = FCoreStyle::GetDefaultFontStyle("Bold", 8);

	// Initialize 24 hourly buckets (zero-filled if no seed)
	Buckets.SetNum(NumBuckets);
	for (int32 i = 0; i < NumBuckets; ++i)
	{
		Buckets[i] = (i < InArgs._HourlyDistribution.Num()) ? InArgs._HourlyDistribution[i] : 0;
	}

	if (InArgs._SubscribeToLiveEvents)
	{
		SubscribeToWebSocketSubsystem();
	}
}

SCUChatTimelineBar::~SCUChatTimelineBar()
{
	UnsubscribeFromWebSocketSubsystem();
}

void SCUChatTimelineBar::SubscribeToWebSocketSubsystem()
{
	if (!GEditor) return;
	UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
	if (!WS) return;
	DelegateHandle = WS->OnEventBroadcast.AddSP(
		SharedThis(this),
		&SCUChatTimelineBar::HandleEditorEvent);
}

void SCUChatTimelineBar::UnsubscribeFromWebSocketSubsystem()
{
	if (!DelegateHandle.IsValid()) return;
	if (!GEditor) return;
	UCUWebSocketEditorSubsystem* WS = GEditor->GetEditorSubsystem<UCUWebSocketEditorSubsystem>();
	if (!WS) return;
	WS->OnEventBroadcast.Remove(DelegateHandle);
	DelegateHandle.Reset();
}

void SCUChatTimelineBar::HandleEditorEvent(const FString& EventType, const FString& EventData)
{
	const FDateTime Now = FDateTime::Now();
	const int32 HourBucket = FMath::Clamp(Now.GetHour(), 0, NumBuckets - 1);
	if (Buckets.IsValidIndex(HourBucket))
	{
		++Buckets[HourBucket];
		++TotalEventsReceived;
		Invalidate(EInvalidateWidget::Paint);
	}
}

FVector2D SCUChatTimelineBar::ComputeDesiredSize(float /*LayoutScaleMultiplier*/) const
{
	const float TotalWidth = NumBuckets * BarWidth + (NumBuckets - 1) * BarSpacing + 8.0f;
	return FVector2D(TotalWidth, BarHeight + 16.0f); // header line + bars
}

int32 SCUChatTimelineBar::OnPaint(
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

	// Compute max for normalization
	int32 MaxCount = 1;
	for (int32 c : Buckets) { if (c > MaxCount) MaxCount = c; }

	// Header
	const FString HeaderText = FString::Printf(
		TEXT("Editor Timeline (24h)  ·  Live events: %d  ·  Peak hour: %d"),
		TotalEventsReceived, MaxCount);
	FSlateDrawElement::MakeText(
		OutDrawElements,
		LayerId,
		AllottedGeometry.ToPaintGeometry(FVector2D(LocalSize.X, 14.0f), FSlateLayoutTransform(FVector2D(2.0f, 0.0f))),
		FText::FromString(HeaderText),
		HeaderFont,
		ESlateDrawEffect::None,
		FLinearColor(0.6f, 0.85f, 1.0f, 1.0f)
	);

	const float BarsTop = 14.0f;
	const float BarsAvailH = FMath::Max(8.0f, LocalSize.Y - BarsTop);

	// Current hour highlight
	const int32 CurrentHour = FMath::Clamp(FDateTime::Now().GetHour(), 0, NumBuckets - 1);

	for (int32 i = 0; i < NumBuckets; ++i)
	{
		const float NormHeight = (float)Buckets[i] / (float)MaxCount;
		const float ThisHeight = FMath::Max(2.0f, BarsAvailH * NormHeight);
		const float X = i * (BarWidth + BarSpacing);
		const float Y = BarsTop + (BarsAvailH - ThisHeight);

		// Background tick (always)
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			AllottedGeometry.ToPaintGeometry(FVector2D(BarWidth, BarsAvailH), FSlateLayoutTransform(FVector2D(X, BarsTop))),
			WhiteBrush,
			ESlateDrawEffect::None,
			FLinearColor(0.10f, 0.10f, 0.12f, 0.5f)
		);

		// Filled bar
		const FLinearColor Fill = (i == CurrentHour)
			? FLinearColor(0.95f, 0.7f, 0.2f, 1.0f)        // amber for current hour
			: FLinearColor(0.35f, 0.65f, 1.0f, 1.0f);      // blue otherwise
		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId + 1,
			AllottedGeometry.ToPaintGeometry(FVector2D(BarWidth, ThisHeight), FSlateLayoutTransform(FVector2D(X, Y))),
			WhiteBrush,
			ESlateDrawEffect::None,
			Fill
		);
	}

	return LayerId + 2;
}
