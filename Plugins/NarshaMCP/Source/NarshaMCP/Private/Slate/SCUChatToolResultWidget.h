// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5656: AI Chat Tool Result Widget — collapsible container for MCP tool results

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Containers/Ticker.h"  // FTSTicker::FDelegateHandle (Issue #9174 copy-confirm flash)
#include "CUChatToolResult.h"

class SBorder;
class SBox;
class SButton;
class SHorizontalBox;
class SScrollBox;
class STextBlock;
class SVerticalBox;

/**
 * Collapsible container widget for MCP tool results (Issue #5656).
 *
 * Layout:
 *   [Header] Tool icon + name + operation badge + collapse toggle
 *   [Content] Populated by FCUChatToolWidgetFactory::CreateContentWidget()
 *             MaxDesiredHeight(300) with inner SScrollBox (collapsed scrollbar)
 */
class SCUChatToolResultWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatToolResultWidget)
		: _ToolResult(FCUChatToolResult())
	{}
		SLATE_ARGUMENT(FCUChatToolResult, ToolResult)
		SLATE_ARGUMENT(TSharedPtr<SWidget>, ContentWidget)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Issue #9174: cancel the outstanding "✓ Copied" FTSTicker delegate if any.
	 *  Without this, destroying the widget while the 1.5s confirmation is still
	 *  flashing leaves a dangling delegate in FTSTicker that fires the weak-ptr
	 *  lambda on a dead object every frame until the ticker itself is GC'd. */
	virtual ~SCUChatToolResultWidget() override;

	/** Get the tool result data */
	const FCUChatToolResult& GetToolResult() const { return ToolResult; }

	/** Whether the content section is currently expanded */
	bool IsExpanded() const { return bIsExpanded; }

	/** Issue #9174: Serialize a tool result to plain text for clipboard copy.
	 * Exposed as a public static so unit tests can verify the serialization
	 * format without instantiating the full Slate widget tree. */
	static FString BuildPlainTextForClipboard(const FCUChatToolResult& Result);

private:
	FCUChatToolResult ToolResult;
	TSharedPtr<SBox> ContentBox;
	bool bIsExpanded = true;

	/** Issue #9174: transient "✓ Copied" confirmation state for the Copy button. */
	bool bCopyConfirmVisible = false;

	/** Issue #9174: timer handle that clears bCopyConfirmVisible after a short window. */
	FTSTicker::FDelegateHandle CopyFeedbackTimerHandle;

	/** Toggle content visibility */
	FReply OnToggleCollapse();

	/** Issue #9174: serialize ToolResult to clipboard as plain text. */
	FReply OnCopyClicked();

	/** Get display name for the tool (friendly format) */
	FText GetToolDisplayName() const;

	/** Get color for the tool header based on error state */
	FSlateColor GetHeaderColor() const;
};
