// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5653: Markdown rendering widget implementation
// Issue #5655: Deep links — clickable asset/source/actor paths

#include "Slate/SCUChatMarkdownBlock.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SMultiLineEditableText.h"  // Issue #9174: selectable/copyable assistant text
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"  // FTextBlockStyle
#include "HAL/PlatformApplicationMisc.h"
#include "Internationalization/Regex.h"
#include "CUChatPanelSubsystem.h"
#include "Editor.h"

// ── Style Constants ───────────────────────────────────────────
namespace MarkdownStyle
{
	// Code block colors
	constexpr FLinearColor CodeHeaderBg(0.16f, 0.16f, 0.16f, 1.0f);    // #292929
	constexpr FLinearColor CodeBodyBg(0.08f, 0.08f, 0.08f, 1.0f);      // #141414
	constexpr FLinearColor CodeLabelColor(0.6f, 0.6f, 0.6f, 1.0f);     // #999999
	constexpr FLinearColor CodeTextColor(0.85f, 0.85f, 0.85f, 1.0f);   // #D9D9D9

	// Inline code colors
	constexpr FLinearColor InlineCodeBg(0.15f, 0.15f, 0.15f, 1.0f);
	constexpr FLinearColor InlineCodeText(0.9f, 0.6f, 0.4f, 1.0f);     // Orange-ish

	// List & blockquote colors
	constexpr FLinearColor BulletColor(0.7f, 0.7f, 0.7f, 1.0f);
	constexpr FLinearColor QuoteBorderColor(0.3f, 0.3f, 0.3f, 1.0f);
	constexpr FLinearColor QuoteTextColor(0.7f, 0.7f, 0.7f, 1.0f);

	// Font sizes
	constexpr int32 HeadingH1Size = 18;
	constexpr int32 HeadingH2Size = 15;
	constexpr int32 HeadingH3Size = 13;
	constexpr int32 BodyFontSize = 11;
	constexpr int32 InlineCodeFontSize = 10;
	constexpr int32 CodeLabelFontSize = 9;

	// Spacing
	constexpr float EmptyLineHeight = 6.0f;
	constexpr float HeadingTopPad = 6.0f;
	constexpr float HeadingBottomPad = 3.0f;
	constexpr float CodeBlockVerticalPad = 4.0f;
	constexpr float CodeHeaderHPad = 10.0f;
	constexpr float CodeHeaderVPad = 4.0f;
	constexpr float CodeBodyHPad = 12.0f;
	constexpr float CodeBodyVPad = 8.0f;
	constexpr float ParagraphVerticalPad = 1.0f;
	constexpr float ListBulletRightPad = 6.0f;
	constexpr float QuoteBorderRightPad = 8.0f;
	constexpr float QuoteBorderWidth = 2.0f;
	constexpr float QuoteVerticalPad = 2.0f;
	constexpr float InlineCodeHPad = 4.0f;
	constexpr float InlineCodeVPad = 1.0f;
	constexpr float CopyButtonHPad = 6.0f;
	constexpr float CopyButtonVPad = 2.0f;

	// Default labels
	constexpr TCHAR DefaultLanguageLabel[] = TEXT("code");

	// Deep link colors (Issue #5655)
	constexpr FLinearColor AssetLinkColor(0.055f, 0.525f, 1.0f, 1.0f);    // Blue #0E86FF
	constexpr FLinearColor SourceLinkColor(0.306f, 0.788f, 0.690f, 1.0f); // Teal #4EC9B0
	constexpr FLinearColor ActorLinkColor(1.0f, 0.843f, 0.0f, 1.0f);      // Gold #FFD700
	constexpr FLinearColor AnnotateLinkColor(0.698f, 0.565f, 0.957f, 1.0f); // Purple #B290F4 (Issue #5821)
}

namespace
{
	// Issue #9174: Factory for selectable/copyable read-only text.
	// Uses SMultiLineEditableText in read-only mode so mouse drag selection + Ctrl+C work
	// on finalized assistant messages (STextBlock has no user-selection support).
	// TextStyle is copied by value inside SMultiLineEditableText::Construct (verified in UE 5.7
	// SMultiLineEditableText.cpp:73 `FTextBlockStyle TextStyle = *InArgs._TextStyle;`), so
	// passing a pointer to a stack-local style is safe.
	TSharedRef<SMultiLineEditableText> MakeSelectableText(
		const FString& Text,
		const FSlateFontInfo& Font,
		const FLinearColor& Color,
		bool bAutoWrap)
	{
		static const FTextBlockStyle BaseStyle =
			FCoreStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText");
		const FTextBlockStyle ColorStyle = FTextBlockStyle(BaseStyle)
			.SetColorAndOpacity(FSlateColor(Color));

		return SNew(SMultiLineEditableText)
			.Text(FText::FromString(Text))
			.TextStyle(&ColorStyle)
			.Font(Font)
			.IsReadOnly(true)
			.AllowMultiLine(true)
			.AutoWrapText(bAutoWrap)
			.SelectAllTextWhenFocused(false)
			.ClearTextSelectionOnFocusLoss(false)
			.AllowContextMenu(true);
	}
}

void SCUChatMarkdownBlock::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SAssignNew(ContentBox, SVerticalBox)
	];

	if (!InArgs._MarkdownText.IsEmpty())
	{
		RebuildContent(InArgs._MarkdownText);
	}
}

void SCUChatMarkdownBlock::SetMarkdownText(const FString& NewText)
{
	if (ContentBox.IsValid())
	{
		ContentBox->ClearChildren();
		BlockMeta.Empty();
		RebuildContent(NewText);
	}
}

int32 SCUChatMarkdownBlock::GetBlockCount() const
{
	if (ContentBox.IsValid())
	{
		return ContentBox->NumSlots();
	}
	return 0;
}

void SCUChatMarkdownBlock::RebuildContent(const FString& Text)
{
	if (!ContentBox.IsValid() || Text.IsEmpty())
	{
		return;
	}

	BlockMeta.Empty();

	// Line-by-line state machine
	enum class EParseState { Normal, InCodeBlock };
	EParseState State = EParseState::Normal;

	FString CodeBuffer;
	FString CodeLanguage;

	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines);

	for (const FString& RawLine : Lines)
	{
		FString Line = RawLine;

		if (Line.StartsWith(TEXT("```")))
		{
			if (State == EParseState::InCodeBlock)
			{
				// Close code block
				AddCodeBlock(CodeBuffer, CodeLanguage);
				CodeBuffer.Empty();
				CodeLanguage.Empty();
				State = EParseState::Normal;
			}
			else
			{
				// Open code block — extract language from ```lang
				CodeLanguage = Line.Mid(3).TrimStartAndEnd();
				State = EParseState::InCodeBlock;
			}
			continue;
		}

		if (State == EParseState::InCodeBlock)
		{
			if (!CodeBuffer.IsEmpty())
			{
				CodeBuffer += TEXT("\n");
			}
			CodeBuffer += Line;
			continue;
		}

		// Normal state — parse markdown elements
		if (Line.StartsWith(TEXT("### ")))
		{
			AddHeading(Line.Mid(4), 3);
		}
		else if (Line.StartsWith(TEXT("## ")))
		{
			AddHeading(Line.Mid(3), 2);
		}
		else if (Line.StartsWith(TEXT("# ")))
		{
			AddHeading(Line.Mid(2), 1);
		}
		else if (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* ")))
		{
			AddListItem(Line.Mid(2));
		}
		else if (Line.StartsWith(TEXT("> ")))
		{
			AddBlockquote(Line.Mid(2));
		}
		else if (Line.TrimStartAndEnd().IsEmpty())
		{
			// Empty line — add vertical spacing
			ContentBox->AddSlot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(MarkdownStyle::EmptyLineHeight)
			];
			BlockMeta.Add({EMarkdownBlockType::Spacing, {}, {}, 0});
		}
		else
		{
			AddParagraph(Line);
		}
	}

	// Handle unclosed code block at end of text
	if (State == EParseState::InCodeBlock && !CodeBuffer.IsEmpty())
	{
		AddCodeBlock(CodeBuffer, CodeLanguage);
	}
}

void SCUChatMarkdownBlock::AddHeading(const FString& Text, int32 Level)
{
	using namespace MarkdownStyle;
	// Map heading levels to font sizes for better readability
	int32 FontSize;
	switch (Level)
	{
		case 1: FontSize = HeadingH1Size; break;
		case 2: FontSize = HeadingH2Size; break;
		default: FontSize = HeadingH3Size; break;
	}

	// Issue #9174: use SMultiLineEditableText so assistant headings are selectable/copyable
	ContentBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, HeadingTopPad, 0.0f, HeadingBottomPad)
	[
		MakeSelectableText(
			Text,
			FCoreStyle::GetDefaultFontStyle("Bold", FontSize),
			FLinearColor::White,
			/*bAutoWrap=*/true)
	];
	BlockMeta.Add({EMarkdownBlockType::Heading, {}, {}, Level});
}

void SCUChatMarkdownBlock::AddCodeBlock(const FString& Code, const FString& Language)
{
	using namespace MarkdownStyle;

	const FString DisplayLang = Language.IsEmpty() ? DefaultLanguageLabel : Language;

	ContentBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, CodeBlockVerticalPad)
	[
		SNew(SVerticalBox)

		// Header bar: language label + Copy button
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderBackgroundColor(CodeHeaderBg)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.Padding(FMargin(CodeHeaderHPad, CodeHeaderVPad))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(DisplayLang))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", CodeLabelFontSize))
					.ColorAndOpacity(CodeLabelColor)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.OnClicked_Lambda([CodeCopy = Code]() -> FReply
					{
						CopyToClipboard(CodeCopy);
						return FReply::Handled();
					})
					.ContentPadding(FMargin(CopyButtonHPad, CopyButtonVPad))
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("Copy")))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", CodeLabelFontSize))
						.ColorAndOpacity(CodeLabelColor)
					]
				]
			]
		]

		// Code content area
		// Issue #9174: use SMultiLineEditableText so code block body is selectable/copyable.
		// Header label (line above) stays as STextBlock — it's a single word and not user content.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderBackgroundColor(CodeBodyBg)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.Padding(FMargin(CodeBodyHPad, CodeBodyVPad))
			[
				MakeSelectableText(
					Code,
					FCoreStyle::GetDefaultFontStyle("Mono", BodyFontSize),
					CodeTextColor,
					/*bAutoWrap=*/false)
			]
		]
	];
	BlockMeta.Add({EMarkdownBlockType::CodeBlock, Code, Language, 0});
}

void SCUChatMarkdownBlock::AddParagraph(const FString& Line)
{
	using namespace MarkdownStyle;
	const FSlateFontInfo BaseFont = FCoreStyle::GetDefaultFontStyle("Regular", BodyFontSize);

	TArray<FString> LinkTargets;
	TArray<ELinkType> LinkTypes;
	ContentBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, ParagraphVerticalPad)
	[
		CreateFormattedText(Line, BaseFont, &LinkTargets, &LinkTypes)
	];
	FBlockMeta Meta;
	Meta.Type = EMarkdownBlockType::Paragraph;
	Meta.LinkTargets = MoveTemp(LinkTargets);
	Meta.LinkTypes = MoveTemp(LinkTypes);
	BlockMeta.Add(MoveTemp(Meta));
}

void SCUChatMarkdownBlock::AddListItem(const FString& Text)
{
	using namespace MarkdownStyle;
	const FSlateFontInfo BaseFont = FCoreStyle::GetDefaultFontStyle("Regular", BodyFontSize);

	TArray<FString> LinkTargets;
	TArray<ELinkType> LinkTypes;
	ContentBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, ParagraphVerticalPad)
	[
		SNew(SHorizontalBox)

		// Bullet
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, ListBulletRightPad, 0.0f)
		.VAlign(VAlign_Top)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("\u2022")))
			.Font(BaseFont)
			.ColorAndOpacity(BulletColor)
		]

		// Text content
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			CreateFormattedText(Text, BaseFont, &LinkTargets, &LinkTypes)
		]
	];
	FBlockMeta Meta;
	Meta.Type = EMarkdownBlockType::ListItem;
	Meta.LinkTargets = MoveTemp(LinkTargets);
	Meta.LinkTypes = MoveTemp(LinkTypes);
	BlockMeta.Add(MoveTemp(Meta));
}

void SCUChatMarkdownBlock::AddBlockquote(const FString& Text)
{
	using namespace MarkdownStyle;
	const FSlateFontInfo BaseFont = FCoreStyle::GetDefaultFontStyle("Italic", BodyFontSize);

	ContentBox->AddSlot()
	.AutoHeight()
	.Padding(0.0f, QuoteVerticalPad)
	[
		SNew(SHorizontalBox)

		// Left border bar
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, QuoteBorderRightPad, 0.0f)
		[
			SNew(SBorder)
			.BorderBackgroundColor(QuoteBorderColor)
			.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
			.Padding(FMargin(QuoteBorderWidth, 0.0f))
			[
				SNew(SBox)
				.WidthOverride(0.0f)
			]
		]

		// Quote text
		// Issue #9174: use SMultiLineEditableText so blockquotes are selectable/copyable
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			MakeSelectableText(Text, BaseFont, QuoteTextColor, /*bAutoWrap=*/true)
		]
	];
	BlockMeta.Add({EMarkdownBlockType::Blockquote, {}, {}, 0});
}

TSharedRef<SWidget> SCUChatMarkdownBlock::CreateFormattedText(const FString& Text,
	const FSlateFontInfo& BaseFont, TArray<FString>* OutLinkTargets, TArray<ELinkType>* OutLinkTypes)
{
	// Issue #5655: Extended segment with deep link support
	struct FTextSegment
	{
		FString Content;
		bool bBold = false;
		bool bItalic = false;
		bool bCode = false;
		bool bIsLink = false;
		ELinkType LinkType = ELinkType::None;
		FString LinkTarget;
	};

	// ── Step 1: Pre-split by deep link patterns (Issue #5655) ──
	// Extract link segments BEFORE inline formatting to avoid conflicts
	struct FPreSegment
	{
		FString Content;
		bool bIsLink = false;
		ELinkType LinkType = ELinkType::None;
		FString LinkTarget;
	};
	TArray<FPreSegment> PreSegments;

	// Combined regex: asset paths + source paths + explicit actor links + annotate links
	// Group 1: /Game/... or Content/...
	// Group 2: Source/... or Plugins/... with file extension + optional :lineNumber
	// Group 3+4: [text](actor://Name) explicit actor link syntax
	// Group 5: ue-annotate://... annotation cross-link (Issue #5821)
	const FRegexPattern LinkPattern(TEXT(
		"((?:/Game|Content)/[\\w/\\.\\-]+)"
		"|((?:Source|Plugins)/[\\w/\\.\\-]+\\.(?:cpp|h|cs|hpp|inl)(?::\\d+)?)"
		"|\\[([^\\]]+)\\]\\(actor://([^)]+)\\)"
		"|ue-annotate://([^\\s)]+)"
	));

	FRegexMatcher Matcher(LinkPattern, Text);
	int32 LastEnd = 0;

	while (Matcher.FindNext())
	{
		const int32 MatchBegin = Matcher.GetMatchBeginning();
		const int32 MatchEnd = Matcher.GetMatchEnding();

		// Plain text before this match
		if (MatchBegin > LastEnd)
		{
			FPreSegment Plain;
			Plain.Content = Text.Mid(LastEnd, MatchBegin - LastEnd);
			PreSegments.Add(MoveTemp(Plain));
		}

		// Determine which group matched
		FPreSegment Link;
		Link.bIsLink = true;

		FString Group1 = Matcher.GetCaptureGroup(1);
		FString Group2 = Matcher.GetCaptureGroup(2);
		FString Group3 = Matcher.GetCaptureGroup(3); // actor display text
		FString Group4 = Matcher.GetCaptureGroup(4); // actor name
		FString Group5 = Matcher.GetCaptureGroup(5); // annotate URL body (Issue #5821)

		if (!Group1.IsEmpty())
		{
			Link.Content = Group1;
			Link.LinkType = ELinkType::Asset;
			Link.LinkTarget = Group1;
		}
		else if (!Group2.IsEmpty())
		{
			Link.Content = Group2;
			Link.LinkType = ELinkType::Source;
			Link.LinkTarget = Group2;
		}
		else if (!Group3.IsEmpty() && !Group4.IsEmpty())
		{
			Link.Content = Group3;
			Link.LinkType = ELinkType::Actor;
			Link.LinkTarget = Group4;
		}
		else if (!Group5.IsEmpty())
		{
			// Display: strip query params for readable text
			FString Display = Group5;
			int32 QueryIdx;
			if (Display.FindChar(TEXT('?'), QueryIdx))
			{
				Display = Display.Left(QueryIdx);
			}
			Link.Content = FString::Printf(TEXT("[Annotate: %s]"), *Display);
			Link.LinkType = ELinkType::Annotate;
			Link.LinkTarget = Group5;
		}

		if (Link.bIsLink && !Link.LinkTarget.IsEmpty() && OutLinkTargets)
		{
			OutLinkTargets->Add(Link.LinkTarget);
			if (OutLinkTypes)
			{
				OutLinkTypes->Add(Link.LinkType);
			}
		}
		PreSegments.Add(MoveTemp(Link));
		LastEnd = MatchEnd;
	}

	// Remaining text after last match
	if (LastEnd < Text.Len())
	{
		FPreSegment Tail;
		Tail.Content = Text.Mid(LastEnd);
		PreSegments.Add(MoveTemp(Tail));
	}

	// If no links found, fall back to single pre-segment
	if (PreSegments.Num() == 0)
	{
		FPreSegment All;
		All.Content = Text;
		PreSegments.Add(MoveTemp(All));
	}

	// ── Step 2: Parse inline formatting for non-link segments ──
	TArray<FTextSegment> Segments;

	for (const FPreSegment& Pre : PreSegments)
	{
		if (Pre.bIsLink)
		{
			// Link segments bypass inline formatting
			FTextSegment LinkSeg;
			LinkSeg.Content = Pre.Content;
			LinkSeg.bIsLink = true;
			LinkSeg.LinkType = Pre.LinkType;
			LinkSeg.LinkTarget = Pre.LinkTarget;
			Segments.Add(MoveTemp(LinkSeg));
			continue;
		}

		// Existing inline formatting parser for non-link text
		FString Remaining = Pre.Content;

		// Declare enum outside loop for better performance
		enum class EMarker { None, Code, Bold, Italic };

		while (!Remaining.IsEmpty())
		{
			int32 BacktickIdx = Remaining.Find(TEXT("`"));
			int32 DoubleStar = Remaining.Find(TEXT("**"));
			int32 SingleStar = Remaining.Find(TEXT("*"));

			int32 EarliestIdx = MAX_int32;
			EMarker MarkerType = EMarker::None;

			if (BacktickIdx != INDEX_NONE && BacktickIdx < EarliestIdx)
			{
				EarliestIdx = BacktickIdx;
				MarkerType = EMarker::Code;
			}
			if (DoubleStar != INDEX_NONE && DoubleStar < EarliestIdx)
			{
				EarliestIdx = DoubleStar;
				MarkerType = EMarker::Bold;
			}
			if (SingleStar != INDEX_NONE && SingleStar < EarliestIdx
				&& (DoubleStar == INDEX_NONE || SingleStar != DoubleStar))
			{
				EarliestIdx = SingleStar;
				MarkerType = EMarker::Italic;
			}

			if (MarkerType == EMarker::None)
			{
				if (!Remaining.IsEmpty())
				{
					FTextSegment Seg;
					Seg.Content = Remaining;
					Segments.Add(MoveTemp(Seg));
				}
				break;
			}

			if (EarliestIdx > 0)
			{
				FTextSegment PlainSeg;
				PlainSeg.Content = Remaining.Left(EarliestIdx);
				Segments.Add(MoveTemp(PlainSeg));
			}

			if (MarkerType == EMarker::Code)
			{
				int32 CloseIdx = Remaining.Find(TEXT("`"), ESearchCase::CaseSensitive,
					ESearchDir::FromStart, EarliestIdx + 1);
				if (CloseIdx != INDEX_NONE)
				{
					FTextSegment CodeSeg;
					CodeSeg.Content = Remaining.Mid(EarliestIdx + 1, CloseIdx - EarliestIdx - 1);
					CodeSeg.bCode = true;
					Segments.Add(MoveTemp(CodeSeg));
					Remaining = Remaining.Mid(CloseIdx + 1);
				}
				else
				{
					FTextSegment PlainSeg;
					PlainSeg.Content = Remaining.Mid(EarliestIdx);
					Segments.Add(MoveTemp(PlainSeg));
					break;
				}
			}
			else if (MarkerType == EMarker::Bold)
			{
				int32 CloseIdx = Remaining.Find(TEXT("**"), ESearchCase::CaseSensitive,
					ESearchDir::FromStart, EarliestIdx + 2);
				if (CloseIdx != INDEX_NONE)
				{
					FTextSegment BoldSeg;
					BoldSeg.Content = Remaining.Mid(EarliestIdx + 2, CloseIdx - EarliestIdx - 2);
					BoldSeg.bBold = true;
					Segments.Add(MoveTemp(BoldSeg));
					Remaining = Remaining.Mid(CloseIdx + 2);
				}
				else
				{
					FTextSegment PlainSeg;
					PlainSeg.Content = Remaining.Mid(EarliestIdx);
					Segments.Add(MoveTemp(PlainSeg));
					break;
				}
			}
			else // Italic
			{
				int32 SearchStart = EarliestIdx + 1;
				int32 CloseIdx = INDEX_NONE;
				while (SearchStart < Remaining.Len())
				{
					int32 Found = Remaining.Find(TEXT("*"), ESearchCase::CaseSensitive,
						ESearchDir::FromStart, SearchStart);
					if (Found == INDEX_NONE)
					{
						break;
					}
					if (Found + 1 < Remaining.Len() && Remaining[Found + 1] == TEXT('*'))
					{
						SearchStart = Found + 2;
						continue;
					}
					CloseIdx = Found;
					break;
				}

				if (CloseIdx != INDEX_NONE)
				{
					FTextSegment ItalicSeg;
					ItalicSeg.Content = Remaining.Mid(EarliestIdx + 1, CloseIdx - EarliestIdx - 1);
					ItalicSeg.bItalic = true;
					Segments.Add(MoveTemp(ItalicSeg));
					Remaining = Remaining.Mid(CloseIdx + 1);
				}
				else
				{
					FTextSegment PlainSeg;
					PlainSeg.Content = Remaining.Mid(EarliestIdx);
					Segments.Add(MoveTemp(PlainSeg));
					break;
				}
			}
		}
	}

	using namespace MarkdownStyle;

	// ── Step 3: Compose widgets ──

	// Check for single plain text segment (optimization)
	// Issue #9174: single-segment plain paragraphs use SMultiLineEditableText so assistant
	// body text is selectable/copyable. Multi-segment formatted paragraphs (below) keep
	// STextBlock — native selection cannot cross SHorizontalBox segment boundaries anyway.
	if (Segments.Num() == 1 && !Segments[0].bBold && !Segments[0].bItalic
		&& !Segments[0].bCode && !Segments[0].bIsLink)
	{
		return MakeSelectableText(
			Segments[0].Content,
			BaseFont,
			FLinearColor::White,
			/*bAutoWrap=*/true);
	}

	TSharedRef<SHorizontalBox> HBox = SNew(SHorizontalBox);

	for (const FTextSegment& Seg : Segments)
	{
		if (Seg.Content.IsEmpty())
		{
			continue;
		}

		if (Seg.bIsLink)
		{
			// Deep link: clickable button with colored text (Issue #5655)
			FLinearColor LinkColor = AssetLinkColor;
			if (Seg.LinkType == ELinkType::Source)
			{
				LinkColor = SourceLinkColor;
			}
			else if (Seg.LinkType == ELinkType::Actor)
			{
				LinkColor = ActorLinkColor;
			}
			else if (Seg.LinkType == ELinkType::Annotate)
			{
				LinkColor = AnnotateLinkColor;
			}

			const FString CapturedTarget = Seg.LinkTarget;
			const ELinkType CapturedType = Seg.LinkType;

			HBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
				.Cursor(EMouseCursor::Hand)
				.ContentPadding(FMargin(0.0f))
				.OnClicked_Lambda([CapturedTarget, CapturedType]() -> FReply
				{
					if (GEditor)
					{
						UCUChatPanelSubsystem* Subsystem =
							GEditor->GetEditorSubsystem<UCUChatPanelSubsystem>();
						if (Subsystem)
						{
							switch (CapturedType)
							{
							case ELinkType::Asset:
								Subsystem->HandleAssetLink(
									FString::Printf(TEXT("ue-asset://%s"), *CapturedTarget));
								break;
							case ELinkType::Source:
								Subsystem->HandleSourceLink(
									FString::Printf(TEXT("ue-source://%s"), *CapturedTarget));
								break;
							case ELinkType::Actor:
								Subsystem->HandleActorLink(CapturedTarget);
								break;
							case ELinkType::Annotate:
								Subsystem->HandleAnnotateLink(
									FString::Printf(TEXT("ue-annotate://%s"), *CapturedTarget));
								break;
							default:
								break;
							}
						}
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(Seg.Content))
					.Font(BaseFont)
					.ColorAndOpacity(LinkColor)
				]
			];
		}
		else if (Seg.bCode)
		{
			// Inline code: monospace font with dark background
			HBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SBorder)
				.BorderBackgroundColor(InlineCodeBg)
				.BorderImage(FCoreStyle::Get().GetBrush("GenericWhiteBox"))
				.Padding(FMargin(InlineCodeHPad, InlineCodeVPad))
				[
					SNew(STextBlock)
					.Text(FText::FromString(Seg.Content))
					.Font(FCoreStyle::GetDefaultFontStyle("Mono", InlineCodeFontSize))
					.ColorAndOpacity(InlineCodeText)
				]
			];
		}
		else
		{
			FSlateFontInfo SegFont = BaseFont;
			if (Seg.bBold)
			{
				SegFont = FCoreStyle::GetDefaultFontStyle("Bold", BaseFont.Size);
			}
			else if (Seg.bItalic)
			{
				SegFont = FCoreStyle::GetDefaultFontStyle("Italic", BaseFont.Size);
			}

			HBox->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Seg.Content))
				.Font(SegFont)
				.ColorAndOpacity(FLinearColor::White)
			];
		}
	}

	return HBox;
}

SCUChatMarkdownBlock::EMarkdownBlockType SCUChatMarkdownBlock::GetBlockType(int32 Index) const
{
	if (!ensureMsgf(BlockMeta.IsValidIndex(Index),
		TEXT("GetBlockType: Index %d out of range [0, %d)"), Index, BlockMeta.Num()))
	{
		return EMarkdownBlockType::Paragraph;
	}
	return BlockMeta[Index].Type;
}

bool SCUChatMarkdownBlock::GetCodeBlockInfo(int32 Index, FString& OutCode, FString& OutLanguage) const
{
	if (!BlockMeta.IsValidIndex(Index) || BlockMeta[Index].Type != EMarkdownBlockType::CodeBlock)
	{
		return false;
	}
	OutCode = BlockMeta[Index].CodeContent;
	OutLanguage = BlockMeta[Index].Language;
	return true;
}

int32 SCUChatMarkdownBlock::GetHeadingLevel(int32 Index) const
{
	if (!BlockMeta.IsValidIndex(Index) || BlockMeta[Index].Type != EMarkdownBlockType::Heading)
	{
		return 0;
	}
	return BlockMeta[Index].HeadingLevel;
}

void SCUChatMarkdownBlock::CopyToClipboard(const FString& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

#if WITH_AUTOMATION_WORKER || WITH_EDITOR
int32 SCUChatMarkdownBlock::GetLinkCount(int32 BlockIndex) const
{
	if (!BlockMeta.IsValidIndex(BlockIndex))
	{
		return 0;
	}
	return BlockMeta[BlockIndex].LinkTargets.Num();
}

FString SCUChatMarkdownBlock::GetLinkTarget(int32 BlockIndex, int32 LinkIndex) const
{
	if (!BlockMeta.IsValidIndex(BlockIndex))
	{
		return FString();
	}
	const TArray<FString>& Targets = BlockMeta[BlockIndex].LinkTargets;
	if (!Targets.IsValidIndex(LinkIndex))
	{
		return FString();
	}
	return Targets[LinkIndex];
}

SCUChatMarkdownBlock::ELinkType SCUChatMarkdownBlock::GetLinkType(int32 BlockIndex, int32 LinkIndex) const
{
	if (!BlockMeta.IsValidIndex(BlockIndex))
	{
		return ELinkType::None;
	}
	const TArray<ELinkType>& Types = BlockMeta[BlockIndex].LinkTypes;
	if (!Types.IsValidIndex(LinkIndex))
	{
		return ELinkType::None;
	}
	return Types[LinkIndex];
}

// Issue #9174: test helpers for selectable-text widget assertions.
namespace
{
	// Recursively searches the widget tree for any widget whose type name matches `TypeName`.
	bool WidgetTreeContainsType(const TSharedRef<SWidget>& Root, const FName& TypeName)
	{
		if (Root->GetType() == TypeName)
		{
			return true;
		}
		FChildren* Children = Root->GetChildren();
		if (!Children)
		{
			return false;
		}
		for (int32 Index = 0; Index < Children->Num(); ++Index)
		{
			if (WidgetTreeContainsType(Children->GetChildAt(Index), TypeName))
			{
				return true;
			}
		}
		return false;
	}
}

bool SCUChatMarkdownBlock::BlockContainsSelectableText(int32 BlockIndex) const
{
	if (!ContentBox.IsValid() || BlockIndex < 0 || BlockIndex >= ContentBox->NumSlots())
	{
		return false;
	}
	const TSharedRef<SWidget> BlockRoot = ContentBox->GetSlot(BlockIndex).GetWidget();
	return WidgetTreeContainsType(BlockRoot, TEXT("SMultiLineEditableText"));
}

FName SCUChatMarkdownBlock::GetBlockRootWidgetType(int32 BlockIndex) const
{
	if (!ContentBox.IsValid() || BlockIndex < 0 || BlockIndex >= ContentBox->NumSlots())
	{
		return NAME_None;
	}
	return ContentBox->GetSlot(BlockIndex).GetWidget()->GetType();
}

bool SCUChatMarkdownBlock::BlockContainsWidgetOfType(int32 BlockIndex, FName TypeName) const
{
	if (!ContentBox.IsValid() || BlockIndex < 0 || BlockIndex >= ContentBox->NumSlots())
	{
		return false;
	}
	const TSharedRef<SWidget> BlockRoot = ContentBox->GetSlot(BlockIndex).GetWidget();
	return WidgetTreeContainsType(BlockRoot, TypeName);
}
#endif
