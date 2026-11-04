// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5652: AI Chat Message List implementation
// Issue #5656: Extended with tool result widget support

#include "Slate/SCUChatMessageList.h"
#include "Slate/SCUChatMessageBubble.h"
#include "Slate/SCUChatToolResultWidget.h"
#include "Slate/CUChatToolWidgetFactory.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"

void SCUChatMessageList::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(ScrollBox, SScrollBox)
		.ScrollBarAlwaysVisible(false)
		+ SScrollBox::Slot()
		[
			SAssignNew(MessageContainer, SVerticalBox)
		]
	];
}

void SCUChatMessageList::AddUserMessage(const FString& Text)
{
	if (!MessageContainer.IsValid())
	{
		return;
	}

	MessageContainer->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f)
	[
		SNew(SCUChatMessageBubble)
		.MessageText(Text)
		.bIsUser(true)
		.bIsStreaming(false)
	];

	ScrollToBottom();
}

void SCUChatMessageList::AddSystemMessage(const FString& Text)
{
	if (!MessageContainer.IsValid())
	{
		return;
	}

	MessageContainer->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f)
	[
		SNew(SCUChatMessageBubble)
		.MessageText(Text)
		.bIsUser(false)
		.bIsStreaming(false)
	];

	ScrollToBottom();
}

TSharedRef<SCUChatMessageBubble> SCUChatMessageList::AddAssistantMessage()
{
	TSharedRef<SCUChatMessageBubble> Bubble = SNew(SCUChatMessageBubble)
		.MessageText(FString())
		.bIsUser(false)
		.bIsStreaming(true);

	if (MessageContainer.IsValid())
	{
		MessageContainer->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 2.0f)
		[
			Bubble
		];

		ScrollToBottom();
	}

	return Bubble;
}

void SCUChatMessageList::AddToolResult(const FCUChatToolResult& Result)
{
	if (!MessageContainer.IsValid())
	{
		return;
	}

	// Create content widget via factory
	TSharedRef<SWidget> ContentWidget = FCUChatToolWidgetFactory::Get().CreateContentWidget(Result);

	MessageContainer->AddSlot()
	.AutoHeight()
	.Padding(0.0f, 2.0f)
	[
		SNew(SCUChatToolResultWidget)
		.ToolResult(Result)
		.ContentWidget(ContentWidget)
	];

	ScrollToBottom();
}

void SCUChatMessageList::ClearMessages()
{
	if (MessageContainer.IsValid())
	{
		MessageContainer->ClearChildren();
	}
}

void SCUChatMessageList::ScrollToBottom()
{
	if (ScrollBox.IsValid())
	{
		ScrollBox->ScrollToEnd();
	}
}

int32 SCUChatMessageList::GetMessageCount() const
{
	if (MessageContainer.IsValid())
	{
		return MessageContainer->NumSlots();
	}
	return 0;
}
