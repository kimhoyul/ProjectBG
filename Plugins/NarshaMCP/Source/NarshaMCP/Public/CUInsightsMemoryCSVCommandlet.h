// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CommandletFramework/CUCommandletBase.h"
#include "CUInsightsMemoryCSVCommandlet.generated.h"

// Forward declarations
namespace TraceServices
{
	class IAnalysisSession;
	class IAllocationsProvider;
	class IMemoryProvider;
}

/**
 * Commandlet for exporting Memory Profiling data from .utrace files to CSV
 *
 * Issue #3907: Memory Profiling Support - Phase 2 (C++ Commandlet)
 *
 * Purpose:
 *   Export memory allocation data from UnrealInsights .utrace files using
 *   TraceServices Public APIs (IAllocationsProvider, IMemoryProvider).
 *   This bypasses CLI limitations by directly accessing the same APIs
 *   that power the UnrealInsights GUI export functionality.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=InsightsMemoryCSV \
 *       -TraceFile="D:/Traces/MySession.utrace" \
 *       -OutputDir="D:/Output" \
 *       [-TimeRangeStart=10.0] \
 *       [-TimeRangeEnd=60.0] \
 *       [-PoolFilter="Textures,Audio"] \
 *       [-MaxAllocations=1000000]
 *
 * Output Files:
 *   - MemoryAllocs.csv: Individual allocation/free events
 *   - MemoryPools.csv: Per-pool statistics (peak, outstanding)
 *   - LLMCounters.csv: Low-Level Memory tag hierarchy time-series
 *
 * CSV Format (MemoryAllocs.csv):
 *   Timestamp,ThreadId,Address,Size,CallstackId,EventType,PoolTag
 *   1.234567,1234,140701234567890,4096,5678,Alloc,Textures
 *
 * IMPORTANT: Address is DECIMAL (not hex) for Rust parser compatibility
 *
 * TraceServices API:
 *   - IAnalysisSession: Entry point for trace analysis
 *   - IAllocationsProvider: Per-allocation data with callstacks
 *   - IMemoryProvider: LLM tag-based memory tracking
 *
 * Integration:
 *   - Extends CommandletBase (automatic profiling, directory management)
 *   - Called by Python handlers (memory_handlers.py)
 *   - Output parsed by Rust (rust_indexer::insights_csv::memory_events.rs)
 *
 * @since Issue #3907 Phase 2 - C++ Commandlet Implementation
 */
UCLASS()
class NARSHAMCP_API UCUInsightsMemoryCSVCommandlet : public UCUCommandletBase
{
	GENERATED_BODY()

public:
	UCUInsightsMemoryCSVCommandlet(const FObjectInitializer& ObjectInitializer);

protected:
	/**
	 * Override CommandletBase - Skip AssetRegistry wait
	 * Memory export doesn't need AssetRegistry scan
	 */
	virtual bool ShouldWaitForAssetRegistry() const override { return false; }

	/**
	 * Run extraction logic - Overrides CommandletBase::RunExtraction()
	 *
	 * Performs memory CSV export work.
	 *
	 * @param OutputDir - Output directory for CSV files
	 * @param Registry - AssetRegistry (unused, for interface compatibility)
	 * @return 0 on success, non-zero on error
	 */
	virtual int32 RunExtraction(const FString& OutputDir, IAssetRegistry& Registry) override;

public:
	/**
	 * Static extraction method (for batch commandlet or direct invocation)
	 *
	 * @param OutputDir - Output directory for CSV files
	 * @param TraceFile - Path to .utrace file
	 * @param TimeRangeStart - Optional start time (seconds, 0.0 = from start)
	 * @param TimeRangeEnd - Optional end time (seconds, 0.0 = to end)
	 * @param PoolFilter - Optional pool filter (comma-separated, empty = all)
	 * @param MaxAllocations - Max allocations to export (0 = unlimited)
	 * @return 0 on success, non-zero on error
	 */
	UFUNCTION(BlueprintCallable, Category="NarshaMCP|Insights")
	static int32 RunExtractionStatic(
		const FString& OutputDir,
		const FString& TraceFile,
		double TimeRangeStart = 0.0,
		double TimeRangeEnd = 0.0,
		const FString& PoolFilter = TEXT(""),
		int32 MaxAllocations = 0
	);

private:
	// ========================================================================
	// TraceServices API Integration
	// ========================================================================

	/**
	 * Load trace file and create analysis session
	 *
	 * Uses TraceServices module to load .utrace file.
	 *
	 * @param TraceFile - Path to .utrace file
	 * @return Shared pointer to IAnalysisSession (null on failure)
	 */
	static TSharedPtr<const TraceServices::IAnalysisSession> LoadTraceSession(const FString& TraceFile);

	/**
	 * Export allocation events to MemoryAllocs.csv
	 *
	 * Queries IAllocationsProvider and writes CSV.
	 *
	 * @param Session - Trace analysis session
	 * @param OutputPath - Output CSV path
	 * @param TimeStart - Start time filter
	 * @param TimeEnd - End time filter
	 * @param MaxAllocations - Max allocations to export
	 * @return Number of allocations exported, -1 on error
	 */
	static int32 ExportAllocationsCSV(
		const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
		const FString& OutputPath,
		double TimeStart,
		double TimeEnd,
		const FString& PoolFilter,
		int32 MaxAllocations
	);

	/**
	 * Export pool statistics to MemoryPools.csv
	 *
	 * Aggregates allocation data by pool tag.
	 *
	 * @param Session - Trace analysis session
	 * @param OutputPath - Output CSV path
	 * @param PoolFilter - Optional pool filter
	 * @return Number of pools exported, -1 on error
	 */
	static int32 ExportPoolsCSV(
		const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
		const FString& OutputPath,
		const FString& PoolFilter
	);

	/**
	 * Export LLM counters to LLMCounters.csv
	 *
	 * Queries IMemoryProvider for LLM tag hierarchy time-series.
	 *
	 * @param Session - Trace analysis session
	 * @param OutputPath - Output CSV path
	 * @return Number of samples exported, -1 on error
	 */
	static int32 ExportLLMCountersCSV(
		const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
		const FString& OutputPath
	);

	// ========================================================================
	// CSV Generation Helpers
	// ========================================================================

	/**
	 * Escape CSV field value
	 *
	 * Wraps in quotes if contains comma, quote, or newline.
	 * Escapes internal quotes by doubling them.
	 *
	 * @param Value - Raw field value
	 * @return Escaped CSV field
	 */
	static FString EscapeCSVField(const FString& Value);

	/**
	 * Write CSV header row
	 *
	 * @param FileHandle - Output file handle
	 * @param Headers - Column names
	 * @return True on success
	 */
	static bool WriteCSVHeader(IFileHandle* FileHandle, const TArray<FString>& Headers);

	/**
	 * Write CSV data row
	 *
	 * @param FileHandle - Output file handle
	 * @param Fields - Field values
	 * @return True on success
	 */
	static bool WriteCSVRow(IFileHandle* FileHandle, const TArray<FString>& Fields);

	/**
	 * Check if a pool tag matches the filter
	 *
	 * @param TagName - Pool tag name to check
	 * @param PoolFilter - Comma-separated filter (empty = match all)
	 * @return True if tag matches filter or filter is empty
	 */
	static bool MatchesPoolFilter(const FString& TagName, const FString& PoolFilter);

	/**
	 * Check if a pool tag matches pre-parsed filter entries (avoids re-parsing per call)
	 *
	 * @param TagName - Pool tag name to check
	 * @param ParsedFilterEntries - Pre-parsed and trimmed filter entries (empty = match all)
	 * @return True if tag matches any entry or entries is empty
	 */
	static bool MatchesPoolFilter(const FString& TagName, const TArray<FString>& ParsedFilterEntries);

	// ========================================================================
	// Parameter Parsing
	// ========================================================================

	/**
	 * Parse commandlet parameters
	 *
	 * Extracts -TraceFile, -TimeRangeStart, etc. from command line.
	 *
	 * @param Params - Commandlet parameters string
	 * @param OutTraceFile - Output trace file path
	 * @param OutTimeStart - Output start time
	 * @param OutTimeEnd - Output end time
	 * @param OutPoolFilter - Output pool filter
	 * @param OutMaxAllocations - Output max allocations
	 */
	static void ParseParameters(
		const FString& Params,
		FString& OutTraceFile,
		double& OutTimeStart,
		double& OutTimeEnd,
		FString& OutPoolFilter,
		int32& OutMaxAllocations
	);
};
