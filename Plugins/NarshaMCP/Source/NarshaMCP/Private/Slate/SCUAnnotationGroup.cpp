// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5816: AI Annotation Group implementation
// Issue #5820: M6 — lazy widget creation, expand-to-materialize

#include "Slate/SCUAnnotationGroup.h"
#include "Slate/SCUAnnotationOverlay.h"

#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"

void SCUAnnotationGroup::Construct(const FArguments& InArgs)
{
	AssetPath = InArgs._AssetPath;
	GroupTitle = InArgs._GroupTitle;
	OnDismissAll = InArgs._OnDismissAll;

	ChildSlot
	[
		SNew(SBox)
		.MaxDesiredWidth(320.0f)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.02f, 0.9f))
			.OnAreaExpansionChanged(FOnBooleanValueChanged::CreateLambda([this](bool bExpanded)
			{
				HandleAreaExpansionChanged(bExpanded);
			}))
			.HeaderContent()
			[
				SNew(SHorizontalBox)

				// Group title + count
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(HeaderTextBlock, STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%s (0)"), *GroupTitle)))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f)))
				]

				// Dismiss all button
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.OnClicked(this, &SCUAnnotationGroup::OnDismissAllClicked)
					.ContentPadding(FMargin(2.0f, 0.0f))
					.ToolTipText(NSLOCTEXT("NarshaMCP", "DismissAllAnnotations", "Dismiss all annotations in this group"))
					[
						SNew(STextBlock)
						.Text(NSLOCTEXT("NarshaMCP", "DismissAllButton", "\x2715"))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]
			]
			.BodyContent()
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(OverlayContainer, SVerticalBox)
				]
			]
		]
	];
}

void SCUAnnotationGroup::AddAnnotation(const FString& AnnotationId, TSharedRef<SCUAnnotationOverlay> Overlay)
{
	// Remove existing if duplicate
	if (ChildOverlays.Contains(AnnotationId))
	{
		RemoveAnnotation(AnnotationId);
	}

	ChildOverlays.Add(AnnotationId, Overlay);

	if (OverlayContainer.IsValid())
	{
		OverlayContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			Overlay
		];
	}

	UpdateHeaderText();
}

bool SCUAnnotationGroup::RemoveAnnotation(const FString& AnnotationId)
{
	// Check pending first (Issue #5820)
	if (PendingIds.Remove(AnnotationId) > 0)
	{
		UpdateHeaderText();
		return true;
	}

	TSharedPtr<SCUAnnotationOverlay> Overlay;
	if (!ChildOverlays.RemoveAndCopyValue(AnnotationId, Overlay))
	{
		return false;
	}

	if (OverlayContainer.IsValid() && Overlay.IsValid())
	{
		OverlayContainer->RemoveSlot(Overlay.ToSharedRef());
	}

	UpdateHeaderText();
	return true;
}

void SCUAnnotationGroup::AddPendingAnnotation(const FString& AnnotationId)
{
	PendingIds.Add(AnnotationId);
	UpdateHeaderText();
}

void SCUAnnotationGroup::MaterializePendingAnnotation(const FString& AnnotationId, TSharedRef<SCUAnnotationOverlay> Overlay)
{
	if (PendingIds.Remove(AnnotationId) == 0)
	{
		return;
	}

	ChildOverlays.Add(AnnotationId, Overlay);

	if (OverlayContainer.IsValid())
	{
		OverlayContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			Overlay
		];
	}

	UpdateHeaderText();
}

void SCUAnnotationGroup::UpdateHeaderText()
{
	if (HeaderTextBlock.IsValid())
	{
		HeaderTextBlock->SetText(FText::FromString(
			FString::Printf(TEXT("%s (%d)"), *GroupTitle, GetAnnotationCount())));
	}
}

void SCUAnnotationGroup::HandleAreaExpansionChanged(bool bIsExpanded)
{
	bIsCurrentlyExpanded = bIsExpanded;
	if (bIsExpanded)
	{
		OnGroupExpanded.ExecuteIfBound();
	}
}

FReply SCUAnnotationGroup::OnDismissAllClicked()
{
	OnDismissAll.ExecuteIfBound();
	return FReply::Handled();
}
