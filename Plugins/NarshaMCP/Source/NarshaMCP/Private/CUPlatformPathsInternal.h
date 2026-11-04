// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26457: Deterministic Claude CLI path resolution internals.

#pragma once

#include "CoreMinimal.h"

namespace NarshaMCP::Platform::Private
{
	/**
	 * Inputs captured from the host before resolving Claude CLI candidates.
	 * Keeping environment and PATH results explicit makes the precedence policy
	 * deterministic without mutating process-global environment variables in tests.
	 */
	struct FClaudeCliSearchContext
	{
		FString NativeOnPath;
		FString NpmOnPath;
		FString UserProfile;
		FString LocalAppData;
		FString AppData;
		FString Home;
	};

	/** Return the first existing Claude CLI candidate in platform priority order. */
	FString ResolveClaudeCliPath(
		const FClaudeCliSearchContext& Context,
		TFunctionRef<bool(const FString&)> FileExists);
}
