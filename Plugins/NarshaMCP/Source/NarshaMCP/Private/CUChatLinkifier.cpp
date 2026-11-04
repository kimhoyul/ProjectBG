// Copyright 2026 Next Stage Inc. All rights reserved.
// AI Chat Auto-Linkifier implementation

#include "CUChatLinkifier.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

// Static member initialization
TArray<FString> FCUChatLinkifier::CachedActorNames;
double FCUChatLinkifier::LastCacheTime = 0.0;

bool FCUChatLinkifier::IsWordBoundary(TCHAR Ch)
{
	return !FChar::IsAlnum(Ch) && Ch != TEXT('_');
}

void FCUChatLinkifier::InvalidateCache()
{
	check(IsInGameThread());
	CachedActorNames.Empty();
	LastCacheTime = 0.0;
}

const TArray<FString>& FCUChatLinkifier::CollectActorNames()
{
	check(IsInGameThread());
	const double Now = FPlatformTime::Seconds();
	if (Now - LastCacheTime < CacheTTL && CachedActorNames.Num() > 0)
	{
		return CachedActorNames;
	}

	CachedActorNames.Empty();

	if (!GEditor || GEditor->GetWorldContexts().Num() == 0)
	{
		return CachedActorNames;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return CachedActorNames;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}

		const FString Name = Actor->GetName();

		if (Name.Len() < MinActorNameLength)
		{
			continue;
		}

		// Prefer names with underscore or mixed case (BP_*, SM_*, A_*)
		// Filters all-lowercase names without underscores (rare in UE naming)
		// Check without allocating a temporary FString via ToLower()
		bool bPassesFilter = false;
		for (const TCHAR Ch : Name)
		{
			if (Ch == TEXT('_') || FChar::IsUpper(Ch)) { bPassesFilter = true; break; }
		}
		if (!bPassesFilter)
		{
			continue;
		}

		CachedActorNames.Add(Name);

		if (CachedActorNames.Num() >= MaxActorNamesForLinkify)
		{
			break;
		}
	}

	// Sort longest-first for greedy matching (BP_PlayerController before BP_Player)
	CachedActorNames.Sort([](const FString& A, const FString& B) {
		return A.Len() > B.Len();
	});

	LastCacheTime = Now;
	return CachedActorNames;
}

/** Check if position is inside inline code (`...`) or existing markdown link [...](...)  */
static bool IsInsideCodeOrLink(const FString& Line, int32 FoundIdx)
{
	// Inline code check: odd number of backticks before position means inside `...`
	int32 BacktickCount = 0;
	for (int32 i = 0; i < FoundIdx; ++i)
	{
		if (Line[i] == TEXT('`'))
		{
			BacktickCount++;
		}
	}
	if (BacktickCount % 2 != 0)
	{
		return true;
	}

	// Markdown link text check: look backward for '[' without closing ']'
	for (int32 i = FoundIdx - 1; i >= 0; --i)
	{
		if (Line[i] == TEXT('['))
		{
			return true;
		}
		if (Line[i] == TEXT(']') || Line[i] == TEXT(')'))
		{
			break;
		}
	}

	// Markdown link URL check: look backward for ](
	if (FoundIdx >= 2)
	{
		for (int32 i = FoundIdx - 1; i >= 1; --i)
		{
			if (Line[i] == TEXT('(') && Line[i - 1] == TEXT(']'))
			{
				return true;
			}
			if (Line[i] == TEXT(')'))
			{
				break;
			}
		}
	}

	return false;
}

FString FCUChatLinkifier::Linkify(const FString& ResponseText, bool bActorLinksEnabled)
{
	if (!bActorLinksEnabled || ResponseText.IsEmpty())
	{
		return ResponseText;
	}

	const TArray<FString>& ActorNames = CollectActorNames();
	if (ActorNames.Num() == 0)
	{
		return ResponseText;
	}

	// Guard against pathological inputs: O(N*M*L) complexity where N=actors, M=response_length, L=avg_actor_name_length
	if (ActorNames.Num() > MaxActorNamesForLinkify || ResponseText.Len() > MaxResponseLengthForLinkify)
	{
		return ResponseText;
	}

	// Process line by line to handle code block boundaries
	TArray<FString> Lines;
	ResponseText.ParseIntoArray(Lines, TEXT("\n"), false);

	bool bInCodeBlock = false;
	FString Result;
	Result.Reserve(ResponseText.Len() + ResponseText.Len() / LinkExpansionFactor);

	for (int32 LineIdx = 0; LineIdx < Lines.Num(); ++LineIdx)
	{
		if (LineIdx > 0)
		{
			Result += TEXT("\n");
		}

		const FString& Line = Lines[LineIdx];

		// Toggle code block state on ``` boundaries
		if (Line.TrimStart().StartsWith(TEXT("```")))
		{
			bInCodeBlock = !bInCodeBlock;
			Result += Line;
			continue;
		}

		// Skip lines inside code blocks
		if (bInCodeBlock)
		{
			Result += Line;
			continue;
		}

		// Process this line for actor name linkification
		FString ProcessedLine = Line;

		for (const FString& ActorName : ActorNames)
		{
			int32 SearchStart = 0;

			while (SearchStart < ProcessedLine.Len())
			{
				int32 FoundIdx = ProcessedLine.Find(ActorName, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
				if (FoundIdx == INDEX_NONE)
				{
					break;
				}

				// Word boundary check: character before match
				if (FoundIdx > 0 && !IsWordBoundary(ProcessedLine[FoundIdx - 1]))
				{
					SearchStart = FoundIdx + 1;
					continue;
				}

				// Word boundary check: character after match
				const int32 EndIdx = FoundIdx + ActorName.Len();
				if (EndIdx < ProcessedLine.Len() && !IsWordBoundary(ProcessedLine[EndIdx]))
				{
					SearchStart = FoundIdx + 1;
					continue;
				}

				// Skip matches inside inline code (`...`) or existing markdown links
				if (IsInsideCodeOrLink(ProcessedLine, FoundIdx))
				{
					SearchStart = FoundIdx + ActorName.Len();
					continue;
				}

				// Replace with markdown link
				const FString Replacement = FString::Printf(TEXT("[%s](actor://%s)"), *ActorName, *ActorName);
				ProcessedLine = ProcessedLine.Left(FoundIdx) + Replacement + ProcessedLine.Mid(EndIdx);

				// Skip past the replacement to avoid re-processing
				SearchStart = FoundIdx + Replacement.Len();
			}
		}

		Result += ProcessedLine;
	}

	return Result;
}
