// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5652: AI Chat Message Bubble implementation
// Issue #5653: Two-phase rendering (plain text -> markdown)
// Issue #5654: 2-stage streaming (SMultiLineEditableText → Markdown)

#include "Slate/SCUChatMessageBubble.h"
#include "Slate/SCUChatMarkdownBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Async/Async.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPChatBubble, Log, All);

// ── Bubble Style Constants ────────────────────────────────────
namespace BubbleStyle
{
	// Bubble background colors (match SpawnAITab theme L348-378)
	constexpr FLinearColor UserBg(0.0f, 0.44f, 0.88f, 1.0f);           // #0070E0 BluePrimary
	constexpr FLinearColor AssistantBg(0.118f, 0.118f, 0.118f, 1.0f);   // #1E1E1E InputBgHover

	// Layout
	constexpr float UserLeftPad = 40.0f;
	constexpr float MessageSidePad = 8.0f;
	constexpr float MessageVerticalPad = 4.0f;
	constexpr float BubbleHPad = 12.0f;
	constexpr float BubbleVPad = 8.0f;

	// SWidgetSwitcher indices
	constexpr int32 PlainTextIndex = 0;
	constexpr int32 MarkdownIndex = 1;

	// Thinking animation (Issue #5589)
	constexpr double ThinkingAnimationSpeed = 2.0; // 0.5s cycle
	constexpr int32 ThinkingMaxDots = 3;

	// Issue #5654: Streaming text style (white text for dark background)
	static const FTextBlockStyle& GetStreamingTextStyle()
	{
		static FTextBlockStyle Style = []()
		{
			FTextBlockStyle S = FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
			S.SetColorAndOpacity(FSlateColor(FLinearColor::White));
			return S;
		}();
		return Style;
	}
}

void SCUChatMessageBubble::Construct(const FArguments& InArgs)
{
	using namespace BubbleStyle;

	bIsUser = InArgs._bIsUser;
	bIsStreaming = InArgs._bIsStreaming;
	CurrentText = InArgs._MessageText;
	// Initialize StreamingBuffer with CurrentText for consistent state
	// (both history load and streaming start from same initial content)
	StreamingBuffer = CurrentText;

	ChildSlot
	[
		SNew(SBox)
		.HAlign(bIsUser ? HAlign_Right : HAlign_Left)
		.Padding(FMargin(bIsUser ? UserLeftPad : MessageSidePad, MessageVerticalPad, bIsUser ? MessageSidePad : UserLeftPad, MessageVerticalPad))
		[
			SNew(SBorder)
			.BorderBackgroundColor(bIsUser ? UserBg : AssistantBg)
			.Padding(FMargin(BubbleHPad, BubbleVPad))
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			[
				// Issue #5653/#5654: SWidgetSwitcher for two-phase rendering
				// Index 0 = SMultiLineEditableText read-only (streaming + user messages)
				// Index 1 = SCUChatMarkdownBlock (finalized assistant messages)
				SAssignNew(ContentSwitcher, SWidgetSwitcher)
				.WidgetIndex(PlainTextIndex)

				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(StreamingTextWidget, SMultiLineEditableText)
					.Text(FText::FromString(CurrentText))
					.IsReadOnly(true)
					.AutoWrapText(true)
					.TextStyle(&GetStreamingTextStyle())
				]

				+ SWidgetSwitcher::Slot()
				[
					SAssignNew(MarkdownBlock, SCUChatMarkdownBlock)
					.MarkdownText(FString())
				]
			]
		]
	];

	// If constructed with non-empty text and not streaming (e.g., history load),
	// immediately show markdown for assistant messages
	if (!bIsStreaming && !bIsUser && !CurrentText.IsEmpty())
	{
		if (MarkdownBlock.IsValid())
		{
			MarkdownBlock->SetMarkdownText(CurrentText);
		}
		if (ContentSwitcher.IsValid())
		{
			ContentSwitcher->SetActiveWidgetIndex(MarkdownIndex);
		}
	}

	// Issue #5589: Show "Thinking..." for empty assistant bubbles (waiting for first token)
	if (!bIsUser && CurrentText.IsEmpty())
	{
		bIsThinking = true;
		ThinkingStartTime = FPlatformTime::Seconds();
		if (StreamingTextWidget.IsValid())
		{
			StreamingTextWidget->SetText(NSLOCTEXT("NarshaMCP", "ThinkingText", "Thinking..."));
		}
	}
}

void SCUChatMessageBubble::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Issue #5589: Animate "Thinking..." dots while waiting for first token
	if (bIsThinking && StreamingTextWidget.IsValid())
	{
		double Elapsed = InCurrentTime - ThinkingStartTime;
		int32 DotCount = (static_cast<int32>(Elapsed * BubbleStyle::ThinkingAnimationSpeed) % BubbleStyle::ThinkingMaxDots) + 1;
		FString Dots = FString::ChrN(DotCount, TEXT('.'));
		StreamingTextWidget->SetText(FText::FromString(FString::Printf(TEXT("Thinking%s"), *Dots)));
	}
}

void SCUChatMessageBubble::AppendText(const FString& Token)
{
	// Early return for empty tokens (performance micro-optimization)
	if (Token.IsEmpty())
	{
		return;
	}

	// Issue #5589: Clear thinking indicator on first real token
	if (bIsThinking)
	{
		bIsThinking = false;
		CurrentText.Empty();
		{
			FScopeLock Lock(&BufferLock);
			StreamingBuffer.Empty();
		}
	}

	{
		FScopeLock Lock(&BufferLock);
		StreamingBuffer += Token;
	}

	// Issue #5654: Atomic throttle — coalesce to one UI update per frame.
	// Multiple rapid AppendText calls accumulate in StreamingBuffer under lock;
	// only the first posts AsyncTask; when it runs it snapshots the full buffer.
	// AsyncTask(GameThread) defers execution to next frame when called from GameThread,
	// providing natural per-frame UI update coalescing.
	if (!bThrottlePending.Exchange(true))
	{
		TWeakPtr<SCUChatMessageBubble> WeakSelf(
			StaticCastSharedRef<SCUChatMessageBubble>(AsShared()));

		AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
		{
			TSharedPtr<SCUChatMessageBubble> Pinned = WeakSelf.Pin();
			if (!Pinned.IsValid())
			{
				return;
			}

			FString Snapshot;
			{
				FScopeLock Lock(&Pinned->BufferLock);
				Snapshot = Pinned->StreamingBuffer;
			}
			Pinned->CurrentText = Snapshot;
			if (Pinned->StreamingTextWidget.IsValid())
			{
				Pinned->StreamingTextWidget->SetText(FText::FromString(Snapshot));
			}
			Pinned->bThrottlePending = false;
		});
	}
}

void SCUChatMessageBubble::FinalizeMessage(const FString& FullText)
{
	// Issue #5589: Clear thinking state if still active (e.g., error before any tokens)
	bIsThinking = false;

	// Issue #6000: Allow re-finalization (error recovery, content correction).
	// Log warning instead of assert — re-finalize is a valid use case.
	UE_CLOG(!bIsStreaming && !CurrentText.IsEmpty(), LogNarshaMCPChatBubble, Warning,
		TEXT("FinalizeMessage called on non-streaming bubble with existing content -- re-finalizing"));

	// Issue #5654: Synchronous flush — set authoritative final text
	{
		FScopeLock Lock(&BufferLock);
		StreamingBuffer = FullText;
	}

	CurrentText = FullText;
	bIsStreaming = false;

	// Issue #5653: Switch to markdown rendering for assistant messages
	if (!bIsUser)
	{
		if (MarkdownBlock.IsValid())
		{
			MarkdownBlock->SetMarkdownText(CurrentText);
		}
		if (ContentSwitcher.IsValid())
		{
			ContentSwitcher->SetActiveWidgetIndex(BubbleStyle::MarkdownIndex);
		}
	}
	else
	{
		// User messages remain plain text
		if (StreamingTextWidget.IsValid())
		{
			StreamingTextWidget->SetText(FText::FromString(CurrentText));
		}
	}
}

bool SCUChatMessageBubble::IsMarkdownActive() const
{
	if (ContentSwitcher.IsValid())
	{
		return ContentSwitcher->GetActiveWidgetIndex() == BubbleStyle::MarkdownIndex;
	}
	return false;
}

#if WITH_AUTOMATION_WORKER || WITH_EDITOR
FString SCUChatMessageBubble::GetStreamingBuffer() const
{
	FScopeLock Lock(&BufferLock);
	return StreamingBuffer;
}
#endif
