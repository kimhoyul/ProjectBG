// Copyright 2026 Next Stage Inc. All rights reserved.

// Issue #25 + #68 — Provider API GT Extractor.
//
// Independent third-party oracle for `/utrace-parity` byte-exact
// verification. Loads a `.utrace` via UE's standard `IAnalysisService`,
// reads UE-built `ILoadTimeProfilerProvider` / `IBookmarkProvider` /
// `ILogProvider` (NOT our port), and emits CSVs in a schema designed
// to be **set-equivalent comparable** with our Rust dump.
//
// # Why this oracle is meaningful
//
// `CULoadtimeChannelDumper` / `CUMarkersChannelDumper` build CSVs by
// running our own 1:1-ported analyzers. The Rust port consumes the
// same wire bytes. A Rust ↔ C++ byte-exact match proves *self-
// consistency* of the two reads.
//
// This extractor instead uses **UE 5.7 stock** `FAsyncLoadingTraceAnalyzer`
// / `FBookmarksAnalyzer` / `FLogTraceAnalyzer` — invoked transparently
// inside `IAnalysisService::Analyze()` — to populate the
// `IAnalysisSession`'s providers. We then read provider state via the
// public `EnumerateXxx` / `ReadXxx` APIs. The wire-level interpretation
// is therefore Epic-authoritative, not our port.
//
// # Schema diff vs `CULoadtimeChannelDumper` / `CUMarkersChannelDumper`
//
// Provider APIs intentionally hide wire-level identifiers
// (`bookmark_point` ptr, `log_point` ptr, raw `FormatArgs` bytes) —
// they are *processed presentation*, not *raw wire payload*.
// Therefore Provider-GT and our wire-GT cannot be byte-exact diff'd
// directly. The meaningful comparison is **set/multiset/count
// equality** on the projections both surfaces preserve:
//
//   - LoadTime  class name set         (Provider: walk exports → Class.Name)
//   - LoadTime  package name set       (Provider: EnumeratePackages)
//   - LoadTime  package summary fields (Provider: per-package Summary)
//   - LoadTime  load request count     (Provider: GetRequestsTable)
//   - Markers   bookmark count         (Provider: GetBookmarkCount)
//   - Markers   log category names     (Provider: EnumerateCategories)
//   - Markers   log message count      (Provider: GetMessageCount)
//   - Markers   log message file set   (Provider: per-message File set)
//
// Outputs are emitted to `<out_dir>/provider_<channel>_<artifact>.csv`
// so they coexist with `ue_<channel>_<artifact>.csv` from the
// wire-level dumpers.
//
// # Console command
//
//   NarshaMCP.ExportProviderGt <trace.utrace> <output_dir>
//       [--channels=loadtime,markers,all] [--auto-quit]
//
// Plan ref: out-of-PR follow-up — third-party GT oracle for
// `/utrace-parity` verification (`stage-2-functional-canyon.md`).

#pragma once

#include "CoreMinimal.h"

namespace NarshaMCP::ProviderGt
{

/**
 * Extract Provider-level GT for LoadTime + Markers channels from a
 * `.utrace` and write CSVs into `OutDir`. Returns `true` if all
 * requested channels were extracted; `false` if any channel failed
 * (the others are still emitted — partial output is preferable for
 * diagnostics).
 *
 * @param UTracePath   Absolute path to the `.utrace` file.
 * @param OutDir       Directory for the `provider_*.csv` output.
 * @param bLoadtime    Emit `provider_loadtime_*.csv`.
 * @param bMarkers     Emit `provider_markers_*.csv`.
 */
bool ExtractProviderGt(
	const FString& UTracePath,
	const FString& OutDir,
	bool bLoadtime,
	bool bMarkers);

} // namespace NarshaMCP::ProviderGt