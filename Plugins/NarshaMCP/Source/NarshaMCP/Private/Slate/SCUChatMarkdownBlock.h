// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5653: Markdown rendering widget — converts markdown text to Slate widget tree
// Issue #5655: Deep links — clickable asset/source/actor paths

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SVerticalBox;
class STextBlock;

/**
 * Markdown-to-Slate renderer widget (Issue #5653).
 * Parses a subset of markdown and builds a vertical box of styled Slate widgets.
 *
 * Supported syntax:
 *   # / ## / ### Headings
 *   **bold**, *italic*, `inline code`
 *   ```lang ... ``` fenced code blocks (with language label + Copy button)
 *   Plain text paragraphs with auto-wrap
 *   - Unordered list items
 *   > Blockquotes
 *   /Game/... and Content/... asset paths (clickable deep links, Issue #5655)
 *   Source/... and Plugins/... source paths (clickable deep links)
 *   [text](actor://Name) actor links (clickable deep links)
 *
 * Style constants defined in MarkdownStyle namespace for consistent theming.
 * Used by SCUChatMessageBubble for assistant messages after streaming completes.
 */
class SCUChatMarkdownBlock : public SCompoundWidget
{
public:
	/** Block type enum for testing (Issue #5653) */
	enum class EMarkdownBlockType : uint8
	{
		Paragraph,
		Heading,
		CodeBlock,
		ListItem,
		Blockquote,
		Spacing
	};

	/** Deep link type for inline path detection (Issue #5655) */
	enum class ELinkType : uint8
	{
		None,
		Asset,    // /Game/... or Content/...
		Source,   // Source/... or Plugins/... (.cpp, .h, etc.)
		Actor,    // [text](actor://Name) explicit syntax
		Annotate  // ue-annotate://... annotation cross-link (Issue #5821)
	};

	SLATE_BEGIN_ARGS(SCUChatMarkdownBlock)
		: _MarkdownText(FString())
	{}
		SLATE_ARGUMENT(FString, MarkdownText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Re-parse and rebuild the widget tree with new markdown */
	void SetMarkdownText(const FString& NewText);

	/** Get the number of content blocks (for testing) */
	int32 GetBlockCount() const;

	/** Get block type at index (for testing) */
	EMarkdownBlockType GetBlockType(int32 Index) const;

	/** Get code block content at index; returns false if not a CodeBlock */
	bool GetCodeBlockInfo(int32 Index, FString& OutCode, FString& OutLanguage) const;

	/** Get heading level (1-3) at index; returns 0 if not a Heading */
	int32 GetHeadingLevel(int32 Index) const;

private:
	struct FBlockMeta
	{
		EMarkdownBlockType Type = EMarkdownBlockType::Paragraph;
		FString CodeContent;
		FString Language;
		int32 HeadingLevel = 0;
		TArray<FString> LinkTargets;
		TArray<ELinkType> LinkTypes;
	};

	TSharedPtr<SVerticalBox> ContentBox;
	TArray<FBlockMeta> BlockMeta;

	/** Parse markdown text and populate ContentBox */
	void RebuildContent(const FString& Text);

	/** Add a heading line (# / ## / ###) */
	void AddHeading(const FString& Text, int32 Level);

	/** Add a fenced code block with language label + Copy button */
	void AddCodeBlock(const FString& Code, const FString& Language);

	/** Add a paragraph with inline formatting (**bold**, *italic*, `code`) */
	void AddParagraph(const FString& Line);

	/** Add a list item (- text) */
	void AddListItem(const FString& Text);

	/** Add a blockquote (> text) */
	void AddBlockquote(const FString& Text);

	/** Parse inline formatting and create an SHorizontalBox with styled segments */
	TSharedRef<SWidget> CreateFormattedText(const FString& Text, const FSlateFontInfo& BaseFont,
		TArray<FString>* OutLinkTargets = nullptr, TArray<ELinkType>* OutLinkTypes = nullptr);

	/** Copy text to system clipboard */
	static void CopyToClipboard(const FString& Text);

#if WITH_AUTOMATION_WORKER || WITH_EDITOR
public:
	/** Get number of deep links in a block (for testing, Issue #5655) */
	int32 GetLinkCount(int32 BlockIndex) const;

	/** Get link target string at index within a block (for testing, Issue #5655) */
	FString GetLinkTarget(int32 BlockIndex, int32 LinkIndex) const;

	/** Get link type at index within a block (for testing, Issue #5655) */
	ELinkType GetLinkType(int32 BlockIndex, int32 LinkIndex) const;

	/** Returns true if the block at Index contains any SMultiLineEditableText widget (Issue #9174) */
	bool BlockContainsSelectableText(int32 BlockIndex) const;

	/** Returns the type name of the root widget of the block at Index (Issue #9174) */
	FName GetBlockRootWidgetType(int32 BlockIndex) const;

	/** Returns true if the block at Index contains a widget with the given type name
	 *  (Issue #9174 — used to verify the code-block Copy button is preserved). */
	bool BlockContainsWidgetOfType(int32 BlockIndex, FName TypeName) const;
#endif
};
