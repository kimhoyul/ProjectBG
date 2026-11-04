// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5652: AI Chat Message Bubble — Slate widget for user/assistant messages
// Issue #5653: Markdown rendering + Code Block Actions (two-phase rendering)
// Issue #5654: 2-stage streaming (SMultiLineEditableText → Markdown)

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Styling/SlateTypes.h"

class SMultiLineEditableText;
class SBorder;
class SWidgetSwitcher;
class SCUChatMarkdownBlock;

/**
 * Chat message bubble widget (Issue #5652, #5653, #5654).
 * Displays user or assistant messages with distinct styling.
 * Supports streaming via AppendText() with atomic throttle.
 *
 * Two-phase rendering (Issue #5653, #5654):
 *   Phase 1 (streaming): SMultiLineEditableText (read-only) for fast token accumulation
 *   Phase 2 (finalized): SCUChatMarkdownBlock for formatted assistant messages
 *   User messages always remain plain text.
 *
 * Thread safety (Issue #5654):
 *   StreamingBuffer protected by FCriticalSection.
 *   bThrottlePending (TAtomic<bool>) coalesces UI updates to one per frame.
 */
class SCUChatMessageBubble : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatMessageBubble)
		: _MessageText(FString())
		, _bIsUser(false)
		, _bIsStreaming(false)
	{}
		SLATE_ARGUMENT(FString, MessageText)
		SLATE_ARGUMENT(bool, bIsUser)
		SLATE_ARGUMENT(bool, bIsStreaming)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Append streaming text token (atomic throttle — one UI update per frame) */
	void AppendText(const FString& Token);

	/** Finalize message — flush buffer, switch to markdown for assistant messages */
	void FinalizeMessage(const FString& FullText);

	/** Get current displayed text */
	FString GetCurrentText() const { return CurrentText; }

	/** Whether this is a user message */
	bool IsUserMessage() const { return bIsUser; }

	/** Whether markdown rendering is active (for testing) */
	bool IsMarkdownActive() const;

private:
	TSharedPtr<SMultiLineEditableText> StreamingTextWidget;
	TSharedPtr<SWidgetSwitcher> ContentSwitcher;
	TSharedPtr<SCUChatMarkdownBlock> MarkdownBlock;

	/** Thread-safe streaming buffer (Issue #5654) */
	mutable FCriticalSection BufferLock;
	FString StreamingBuffer;
	TAtomic<bool> bThrottlePending{false};

	FString CurrentText;
	bool bIsUser = false;
	bool bIsStreaming = false;

	/** Thinking indicator state (Issue #5589: UX feedback) */
	bool bIsThinking = false;
	double ThinkingStartTime = 0.0;

#if WITH_AUTOMATION_WORKER || WITH_EDITOR
public:
	/** Whether currently in streaming mode (for testing/debugging) */
	bool IsStreamingMode() const { return bIsStreaming; }
	/** Get streaming buffer contents under lock (for testing/debugging) */
	FString GetStreamingBuffer() const;
#endif
};
