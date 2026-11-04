// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27040: resolve actor class filters and match exact / substring / is_a.

#pragma once

#include "CoreMinimal.h"

class UClass;

struct FCUActorClassFilter
{
	/** Resolve a short name, A/U-prefixed name, or /Script path to an AActor UClass (abstract allowed). */
	static UClass* ResolveActorFilterClass(const FString& ClassRef);

	/**
	 * Parse match_mode. Unknown values return false and set OutError.
	 * Valid: "is_a", "exact", "substring".
	 */
	static bool ParseMatchMode(const FString& MatchMode, FString& OutNormalized, FString& OutError);

	/**
	 * Shared actor-class filter predicate (Issue #27865). NormalizedMode must come
	 * from ParseMatchMode. ResolvedFilter is consulted only in is_a mode — callers
	 * resolve a filter UClass only under is_a, so exact/substring ignore it.
	 */
	static bool MatchesClass(const UClass* ActorClass, const FString& ClassName,
		const FString& NormalizedMode, const UClass* ResolvedFilter);
};
