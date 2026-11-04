// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Overlay — floating widget pinned to editor nodes
// Issue #5816: M2 — severity enum colors/icons

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

#include "CUAnnotationData.h"

class SBorder;
class STextBlock;

/**
 * Floating annotation overlay widget pinned to an editor element (Issue #5647).
 *
 * Layout:
 *   [Category icon] [Annotation text] [X dismiss]
 *
 * Category colors:
 *   suggestion = blue, warning = yellow, error = red, analysis = gray
 *
 * MaxDesiredWidth(300) to fit alongside BP/Material/Sequencer nodes.
 * Style matches SCUChatToolResultWidget (DarkGroupBorder).
 */
class SCUAnnotationOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUAnnotationOverlay)
	{}
		SLATE_ARGUMENT(FString, AnnotationId)
		SLATE_ARGUMENT(FString, AnnotationText)
		SLATE_ARGUMENT(FString, Category)
		SLATE_EVENT(FSimpleDelegate, OnDismiss)
		SLATE_EVENT(FSimpleDelegate, OnDiscuss)  // Issue #5821: "Discuss in Chat" action
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Get the annotation ID this overlay represents */
	const FString& GetAnnotationId() const { return AnnotationId; }

	//~ Begin SWidget Interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SWidget Interface

private:
	FString AnnotationId;
	FSimpleDelegate OnDismiss;
	FSimpleDelegate OnDiscuss;

	// === Auto-fade after inactivity (Issue #5822: M8 UX) ===
	double CreatedTimestamp = 0.0;
	bool bIsFaded = false;
	static constexpr double FadeDelaySeconds = 15.0;
	static constexpr float FadedOpacity = 0.5f;
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;

	/** Get color for category (string-based, backward compat) */
	static FLinearColor GetCategoryColor(const FString& Category);

	/** Get icon text for category (string-based, backward compat) */
	static FText GetCategoryIcon(const FString& Category);

	/** Get color for severity enum (Issue #5816) */
	static FLinearColor GetSeverityColor(ECUAnnotationSeverity Severity);

	/** Get icon text for severity enum (Issue #5816) */
	static FText GetSeverityIcon(ECUAnnotationSeverity Severity);

	/** Dismiss button clicked */
	FReply OnDismissClicked();

	/** Discuss button clicked (Issue #5821) */
	FReply OnDiscussClicked();

#if WITH_AUTOMATION_WORKER
public:
	// Test-only accessors for category mapping validation
	static FLinearColor TestGetCategoryColor(const FString& Category) { return GetCategoryColor(Category); }
	static FText TestGetCategoryIcon(const FString& Category) { return GetCategoryIcon(Category); }
	// Test-only accessors for severity mapping validation (Issue #5816)
	static FLinearColor TestGetSeverityColor(ECUAnnotationSeverity Severity) { return GetSeverityColor(Severity); }
	static FText TestGetSeverityIcon(ECUAnnotationSeverity Severity) { return GetSeverityIcon(Severity); }
	// Test-only accessors for auto-fade (Issue #5985)
	static constexpr double TestGetFadeDelaySeconds() { return FadeDelaySeconds; }
	static constexpr float TestGetFadedOpacity() { return FadedOpacity; }
	bool TestGetIsFaded() const { return bIsFaded; }
	double TestGetCreatedTimestamp() const { return CreatedTimestamp; }
	void TestSetCreatedTimestamp(double T) { CreatedTimestamp = T; }
	void TestSetIsFaded(bool b) { bIsFaded = b; }
	void TestOnMouseEnter(const FGeometry& Geo, const FPointerEvent& Evt) { OnMouseEnter(Geo, Evt); }
	void TestOnMouseLeave(const FPointerEvent& Evt) { OnMouseLeave(Evt); }
#endif
};
