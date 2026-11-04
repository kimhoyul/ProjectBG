// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #9122: JSONL helper functions extracted for regression testing.
//
// These are implementation helpers used exclusively by CUModule's transaction
// capture pipeline. Kept in Private/ so they do not leak to the public plugin API.

#pragma once

#include "CoreMinimal.h"

namespace NarshaMCP
{
	// Must match rust_indexer transaction_log::MAX_TRANSACTION_LINE_BYTES.
	inline constexpr int64 MaxTransactionEntryBytes = 1024LL * 1024;

	/** JSON-safe string escaping — all JSON control chars (U+0000..U+001F).
	 *  Extended beyond the original \n\r\t-only escape (Issue #9122 finding #2). */
	FString JsonEscape(const FString& Input);

	/** Append a JSONL entry to Path with size-based rotation at MaxBytes.
	 *  When the file reaches MaxBytes, it is moved to Path + ".1" (overwriting any
	 *  previous .1) before the new append. Default 100 MB keeps logs bounded for
	 *  long-lived editor sessions. Returns true on successful append. */
	bool AppendJsonlEntry(
		const FString& Path,
		const FString& Entry,
		int64 MaxBytes = 100LL * 1024 * 1024,
		int64 MaxEntryBytes = MaxTransactionEntryBytes);
}
