// Copyright 2026 Next Stage Inc. All rights reserved.
// AI Chat Auto-Linkifier — converts bare actor names to clickable markdown links

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

/**
 * Static utility for auto-linkifying actor names in AI response text.
 *
 * Converts bare actor names (e.g., "BP_Player") to markdown link syntax
 * [BP_Player](actor://BP_Player) that SCUChatMarkdownBlock already renders
 * as gold-colored clickable links.
 *
 * False positive prevention:
 *   - Actor names must be >= 4 characters (filters "Sky", "Sun", etc.)
 *   - Word boundary check prevents partial matching
 *   - Code blocks and existing links are skipped
 *   - Longest name matched first (greedy)
 *
 * Actor name list is cached with 2-second TTL for performance.
 */
class FCUChatLinkifier
{
public:
	/**
	 * Linkify actor names in response text.
	 * @param ResponseText AI response text (raw, before markdown rendering)
	 * @param bActorLinksEnabled If false, returns text unchanged
	 * @return Linkified text with [Name](actor://Name) markdown links
	 */
	[[nodiscard]] static FString Linkify(const FString& ResponseText, bool bActorLinksEnabled);

	/** Collect actor FNames from current editor world (cached, 2s TTL). Game-thread only. */
	[[nodiscard]] static const TArray<FString>& CollectActorNames();

	/** Invalidate actor name cache (call on level change) */
	static void InvalidateCache();

private:
	/** Cached actor names, sorted longest-first for greedy matching */
	static TArray<FString> CachedActorNames;
	static double LastCacheTime;
	static constexpr double CacheTTL = 2.0;                      // Cache TTL in seconds - balanced between freshness and performance
	static constexpr int32 MinActorNameLength = 4;              // Minimum length to filter generic names like "Sky", "Sun"
	static constexpr int32 MaxActorNamesForLinkify = 200;       // Performance guard against excessive iteration
	static constexpr int32 MaxResponseLengthForLinkify = 50000; // Performance guard against pathological input size
	static constexpr int32 LinkExpansionFactor = 5;            // Pre-allocate Reserve() buffer assuming ~20% text expansion from actor linkification

	/** Check if character is a word boundary (not alphanumeric or underscore) */
	static bool IsWordBoundary(TCHAR Ch);

#if WITH_AUTOMATION_WORKER
public:
	/** Test-only: inject actor names into cache (bypasses TActorIterator for headless tests) */
	static void TestInjectActorNames(const TArray<FString>& Names)
	{
		CachedActorNames = Names;
		// Sort longest-first (same as CollectActorNames)
		CachedActorNames.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
		LastCacheTime = FPlatformTime::Seconds();
	}
#endif
};
