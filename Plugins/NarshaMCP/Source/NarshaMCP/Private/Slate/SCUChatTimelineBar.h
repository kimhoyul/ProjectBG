// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8841 (Phase 10-2): SCUChatTimelineBar — live editor action timeline

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Delegates/IDelegateInstance.h"
#include "Fonts/SlateFontInfo.h"

/**
 * Live editor action timeline (Issue #8841).
 *
 * Renders 24 hourly buckets along the X axis. Each bucket's height is
 * proportional to the number of editor events that occurred during
 * that hour. Two data sources are merged in real time:
 *
 *  1. Historical (initial seed) — passed via SLATE_ARGUMENT(HourlyDistribution)
 *     from `level_get_workflow_log` patterns.hourly_distribution.
 *  2. Live (incremental) — subscribes to UCUWebSocketEditorSubsystem's
 *     OnEventBroadcast multicast (added in #8840). Each event increments
 *     the bucket for the current local hour and triggers a paint
 *     invalidation.
 *
 * Subscription lifetime is bound to SharedThis(this) via AddSP, so widget
 * destruction cleanly removes the binding.
 */
class SCUChatTimelineBar : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatTimelineBar)
		: _BarHeight(40.0f)
		, _BarSpacing(2.0f)
		, _BarWidth(8.0f)
		, _SubscribeToLiveEvents(true)
	{}
		/** Optional initial seed: 24-element array, indexed by hour-of-day [0..23]. */
		SLATE_ARGUMENT(TArray<int32>, HourlyDistribution)
		SLATE_ARGUMENT(float, BarHeight)
		SLATE_ARGUMENT(float, BarSpacing)
		SLATE_ARGUMENT(float, BarWidth)
		SLATE_ARGUMENT(bool, SubscribeToLiveEvents)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual ~SCUChatTimelineBar();

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

	/** Test-only: programmatically push an event (bypasses delegate). */
	void HandleEditorEventForTest(const FString& EventType, const FString& EventData)
	{
		HandleEditorEvent(EventType, EventData);
	}

	/** Test accessors. */
	const TArray<int32>& TestGetBuckets() const { return Buckets; }
	int32 TestGetTotalEventsReceived() const { return TotalEventsReceived; }
	bool TestIsSubscribed() const { return DelegateHandle.IsValid(); }

private:
	/** 24 hourly buckets indexed by local hour-of-day [0..23]. */
	TArray<int32> Buckets;

	/** Aggregate count of live events received since Construct. */
	int32 TotalEventsReceived = 0;

	/** Render configuration. */
	float BarHeight = 40.0f;
	float BarSpacing = 2.0f;
	float BarWidth = 8.0f;

	/** Subscription handle to UCUWebSocketEditorSubsystem::OnEventBroadcast. */
	FDelegateHandle DelegateHandle;

	FSlateFontInfo HeaderFont;

	/** Delegate target — increments the current-hour bucket and invalidates paint. */
	void HandleEditorEvent(const FString& EventType, const FString& EventData);

	/** Subscribe to the WS subsystem (best-effort — no-op if subsystem absent). */
	void SubscribeToWebSocketSubsystem();

	/** Unsubscribe (best-effort, called from destructor). */
	void UnsubscribeFromWebSocketSubsystem();
};
