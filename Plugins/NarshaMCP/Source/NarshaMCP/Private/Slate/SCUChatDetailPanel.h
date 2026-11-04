// Copyright 2026 Next Stage Inc. All rights reserved.
// AI Chat Detail Panel — SDetailsView side panel for inspecting clicked assets/actors

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"

class IDetailsView;
class STextBlock;

/**
 * Detail side panel widget for the AI Chat Panel.
 *
 * Displays UPROPERTY inspector (SDetailsView) for assets/actors clicked
 * in chat messages. Supports Blueprint CDOs, Actors, Materials, and
 * any UObject with UPROPERTY fields.
 *
 * Layout:
 *   [Header] Object name + [X] close button
 *   [Content] IDetailsView with property search
 *
 * Visibility is controlled by the parent SBox via SetVisibility(Collapsed/Visible).
 */
class SCUChatDetailPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatDetailPanel) {}
		SLATE_EVENT(FSimpleDelegate, OnCloseRequested)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set the inspected object and update header text */
	void SetObject(UObject* InObject, const FString& DisplayName);

	/** Clear the inspected object */
	void ClearObject();

	/** Whether any object is currently displayed */
	bool HasObject() const;

#if WITH_AUTOMATION_WORKER
	/** Test-only access to the rendered header value. */
	FText TestGetHeaderText() const;
#endif

private:
	TSharedPtr<IDetailsView> DetailsView;
	TSharedPtr<STextBlock> HeaderText;

	/** Strong reference to prevent GC on Blueprint CDOs while panel is open */
	TStrongObjectPtr<UObject> StrongObjectRef;

	/** Cached close delegate */
	FSimpleDelegate OnCloseRequested;
};
