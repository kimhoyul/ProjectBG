// Copyright 2026 Next Stage Inc. All rights reserved.
// AI Chat Detail Panel implementation — SDetailsView for inspecting assets/actors

#include "Slate/SCUChatDetailPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Styling/AppStyle.h"
#include "IDetailsView.h"
#include "PropertyEditorModule.h"
#include "Modules/ModuleManager.h"

namespace DetailPanelStyle
{
	static constexpr FLinearColor HeaderTextColor(0.9f, 0.9f, 0.9f);
	static constexpr FLinearColor CloseButtonColor(0.7f, 0.7f, 0.7f);
	static constexpr float HeaderPadH = 8.0f;
	static constexpr float HeaderPadV = 4.0f;
	static constexpr float ClosePadH = 4.0f;
	static constexpr float ClosePadV = 2.0f;
	static constexpr int32 HeaderFontSize = 11;
	static constexpr float SeparatorThickness = 1.0f;
	static const FText NoSelectionText = NSLOCTEXT("NarshaMCP", "DetailNoSelection", "No Selection");
}

void SCUChatDetailPanel::Construct(const FArguments& InArgs)
{
	OnCloseRequested = InArgs._OnCloseRequested;

	// Create details view via PropertyEditor module
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bHideSelectionTip = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bShowObjectLabel = false;

	DetailsView = PropertyEditor.CreateDetailView(DetailsViewArgs);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Header: object name + close button
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("Brushes.Header"))
			.Padding(FMargin(DetailPanelStyle::HeaderPadH, DetailPanelStyle::HeaderPadV))
			[
				SNew(SHorizontalBox)

				// Object name
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SAssignNew(HeaderText, STextBlock)
					.Text(DetailPanelStyle::NoSelectionText)
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", DetailPanelStyle::HeaderFontSize))
					.ColorAndOpacity(FSlateColor(DetailPanelStyle::HeaderTextColor))
				]

				// Close button [X]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "NoBorder")
					.ContentPadding(FMargin(DetailPanelStyle::ClosePadH, DetailPanelStyle::ClosePadV))
					.OnClicked_Lambda([this]() -> FReply {
						OnCloseRequested.ExecuteIfBound();
						return FReply::Handled();
					})
					.ToolTipText(NSLOCTEXT("NarshaMCP", "DetailCloseTooltip", "Close Detail Panel"))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("X")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", DetailPanelStyle::HeaderFontSize))
						.ColorAndOpacity(FSlateColor(DetailPanelStyle::CloseButtonColor))
					]
				]
			]
		]

		// Separator
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSeparator)
			.Thickness(DetailPanelStyle::SeparatorThickness)
		]

		// Details view content
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			DetailsView.ToSharedRef()
		]
	];
}

void SCUChatDetailPanel::SetObject(UObject* InObject, const FString& DisplayName)
{
	if (!InObject || !IsValid(InObject))
	{
		ClearObject();
		return;
	}

	// Hold strong reference to prevent GC (critical for Blueprint CDOs)
	StrongObjectRef.Reset(InObject);

	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(InObject);
	}

	if (HeaderText.IsValid())
	{
		HeaderText->SetText(FText::FromString(DisplayName));
	}
}

void SCUChatDetailPanel::ClearObject()
{
	if (DetailsView.IsValid())
	{
		DetailsView->SetObject(nullptr);
	}

	StrongObjectRef.Reset();

	if (HeaderText.IsValid())
	{
		HeaderText->SetText(DetailPanelStyle::NoSelectionText);
	}
}

bool SCUChatDetailPanel::HasObject() const
{
	return StrongObjectRef.IsValid();
}

#if WITH_AUTOMATION_WORKER
FText SCUChatDetailPanel::TestGetHeaderText() const
{
	return HeaderText.IsValid() ? HeaderText->GetText() : FText::GetEmpty();
}
#endif
