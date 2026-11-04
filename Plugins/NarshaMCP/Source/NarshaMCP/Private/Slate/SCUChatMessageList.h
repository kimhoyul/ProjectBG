// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5652: AI Chat Message List — scrollable message container
// Issue #5656: Extended with tool result widget support

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

struct FCUChatToolResult;
class SCUChatMessageBubble;
class SScrollBox;
class SVerticalBox;

/**
 * Scrollable message list widget (Issue #5652).
 * Contains user and assistant message bubbles in a vertical scroll box.
 */
class SCUChatMessageList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatMessageList) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Add a user message bubble */
	void AddUserMessage(const FString& Text);

	/** Add an empty assistant message bubble for streaming — returns shared ref for AppendText calls */
	TSharedRef<SCUChatMessageBubble> AddAssistantMessage();

	/** Add a system message bubble (non-interactive, pre-filled) */
	void AddSystemMessage(const FString& Text);

	/** Add a tool result widget (Issue #5656) */
	void AddToolResult(const FCUChatToolResult& Result);

	/** Clear all messages */
	void ClearMessages();

	/** Scroll to the bottom of the message list */
	void ScrollToBottom();

	/** Get count of message bubbles */
	int32 GetMessageCount() const;

private:
	TSharedPtr<SScrollBox> ScrollBox;
	TSharedPtr<SVerticalBox> MessageContainer;
};
