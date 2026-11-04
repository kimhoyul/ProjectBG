// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUInsightsMemoryCSVCommandlet.h"
#include "CUCommandletHelpers.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"

// TraceServices module includes
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/AllocationsProvider.h"
#include "TraceServices/Model/Memory.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPInsights, Log, All);

// Constants
namespace
{
	// Memory conversion constants
	constexpr double BytesToMB = 1024.0 * 1024.0;

}

UCUInsightsMemoryCSVCommandlet::UCUInsightsMemoryCSVCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UCUInsightsMemoryCSVCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] Starting memory CSV export..."));

	// Parse parameters from command line
	FString TraceFile;
	double TimeStart = 0.0, TimeEnd = 0.0;
	FString PoolFilter;
	int32 MaxAllocations = 0;

	// Get command line params
	FString CmdLine = FCommandLine::Get();
	ParseParameters(CmdLine, TraceFile, TimeStart, TimeEnd, PoolFilter, MaxAllocations);

	if (TraceFile.IsEmpty())
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Missing required parameter: -TraceFile"));
		return 1;
	}

	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV]   Trace: %s"), *TraceFile);
	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV]   Output: %s"), *OutputDir);

	return RunExtractionStatic(OutputDir, TraceFile, TimeStart, TimeEnd, PoolFilter, MaxAllocations);
}

int32 UCUInsightsMemoryCSVCommandlet::RunExtractionStatic(
	const FString& OutputDir,
	const FString& TraceFile,
	double TimeRangeStart,
	double TimeRangeEnd,
	const FString& PoolFilter,
	int32 MaxAllocations)
{
	// Ensure output directory exists
	if (!FCUCommandletHelpers::EnsureOutputDirectory(OutputDir))
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to create output directory"));
		return 1;
	}

	// Load trace session via TraceServices
	TSharedPtr<const TraceServices::IAnalysisSession> Session = LoadTraceSession(TraceFile);
	if (!Session.IsValid())
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to load trace file"));
		return 1;
	}

	int32 ErrorCount = 0;

	// Export MemoryAllocs.csv
	FString AllocsPath = FPaths::Combine(OutputDir, TEXT("MemoryAllocs.csv"));
	int32 AllocCount = ExportAllocationsCSV(Session, AllocsPath, TimeRangeStart, TimeRangeEnd, PoolFilter, MaxAllocations);
	if (AllocCount < 0)
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to export allocations"));
		ErrorCount++;
	}
	else
	{
		UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] ✓ Exported %d allocations"), AllocCount);
	}

	// Export MemoryPools.csv
	FString PoolsPath = FPaths::Combine(OutputDir, TEXT("MemoryPools.csv"));
	int32 PoolCount = ExportPoolsCSV(Session, PoolsPath, PoolFilter);
	if (PoolCount < 0)
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to export pools"));
		ErrorCount++;
	}
	else
	{
		UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] ✓ Exported %d pools"), PoolCount);
	}

	// Export LLMCounters.csv
	FString CountersPath = FPaths::Combine(OutputDir, TEXT("LLMCounters.csv"));
	int32 SampleCount = ExportLLMCountersCSV(Session, CountersPath);
	if (SampleCount < 0)
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to export LLM counters"));
		ErrorCount++;
	}
	else
	{
		UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] ✓ Exported %d samples"), SampleCount);
	}

	if (ErrorCount > 0)
	{
		UE_LOG(LogNarshaMCPInsights, Warning, TEXT("[InsightsMemoryCSV] Completed with %d errors"), ErrorCount);
		return ErrorCount;
	}

	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] ✓ Memory CSV export completed successfully"));
	return 0;
}

TSharedPtr<const TraceServices::IAnalysisSession> UCUInsightsMemoryCSVCommandlet::LoadTraceSession(const FString& TraceFile)
{
	// Load TraceServices module
	ITraceServicesModule& TraceServicesModule = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");

	// Get analysis service
	TSharedPtr<TraceServices::IAnalysisService> AnalysisService = TraceServicesModule.GetAnalysisService();
	if (!AnalysisService.IsValid())
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to get analysis service"));
		return nullptr;
	}

	// Analyze trace file (blocks until complete)
	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] Analyzing trace file: %s"), *TraceFile);
	TSharedPtr<const TraceServices::IAnalysisSession> Session = AnalysisService->Analyze(*TraceFile);

	if (!Session.IsValid())
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to analyze trace file"));
		return nullptr;
	}

	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] Trace analysis completed"));
	return Session;
}

int32 UCUInsightsMemoryCSVCommandlet::ExportAllocationsCSV(
	const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
	const FString& OutputPath,
	double TimeStart,
	double TimeEnd,
	const FString& PoolFilter,
	int32 MaxAllocations)
{
	// Get IAllocationsProvider
	const TraceServices::IAllocationsProvider* AllocProvider = Session->ReadProvider<TraceServices::IAllocationsProvider>("AllocationsProvider");
	if (!AllocProvider)
	{
		UE_LOG(LogNarshaMCPInsights, Warning,
			TEXT("[InsightsMemoryCSV] IAllocationsProvider not available. "
				 "The trace does not contain memory allocation data. "
				 "To capture memory data: "
				 "(1) Enable Memory Insights in Editor: Edit > Preferences > Memory Insights, "
				 "(2) Launch with '-trace=memory' flag, "
				 "(3) Re-capture the trace and re-run this commandlet."));
		return -1;
	}

	// Open output file
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenWrite(*OutputPath));
	if (!FileHandle)
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to create output file: %s"), *OutputPath);
		return -1;
	}

	// Write CSV header
	TArray<FString> Headers = {
		TEXT("Timestamp"), TEXT("ThreadId"), TEXT("Address"), TEXT("Size"),
		TEXT("CallstackId"), TEXT("EventType"), TEXT("PoolTag")
	};
	if (!WriteCSVHeader(FileHandle.Get(), Headers))
	{
		return -1;
	}

	// Pre-parse pool filter once (avoid re-parsing per allocation — M-2 perf fix)
	TArray<FString> ParsedPoolFilter;
	if (!PoolFilter.IsEmpty())
	{
		PoolFilter.ParseIntoArray(ParsedPoolFilter, TEXT(","), true);
		for (FString& Entry : ParsedPoolFilter)
		{
			Entry.TrimStartAndEndInline();
		}
	}
	const bool bHasPoolFilter = (ParsedPoolFilter.Num() > 0);

	// Query allocations using UE 5.7 API
	int32 ExportedCount = 0;
	int32 MaxCount = (MaxAllocations > 0) ? MaxAllocations : INT_MAX;

	AllocProvider->BeginRead();

	// Setup query parameters for alloc events in time range
	TraceServices::IAllocationsProvider::FQueryParams QueryParams;
	QueryParams.Rule = TraceServices::IAllocationsProvider::EQueryRule::AaB;  // Alloc events between A and B
	QueryParams.TimeA = (TimeStart > 0.0) ? TimeStart : 0.0;
	QueryParams.TimeB = (TimeEnd > 0.0) ? TimeEnd : TNumericLimits<double>::Max();
	QueryParams.TimeC = 0.0;
	QueryParams.TimeD = 0.0;

	// Start query
	TraceServices::IAllocationsProvider::FQueryHandle QueryHandle = AllocProvider->StartQuery(QueryParams);

	// Poll for results
	bool bQueryComplete = false;
	while (!bQueryComplete && ExportedCount < MaxCount)
	{
		TraceServices::IAllocationsProvider::FQueryStatus Status = AllocProvider->PollQuery(QueryHandle);

		if (Status.Status == TraceServices::IAllocationsProvider::EQueryStatus::Done)
		{
			bQueryComplete = true;
		}

		// Process available results
		TraceServices::IAllocationsProvider::FQueryResult Result = Status.NextResult();
		if (Result.IsValid())
		{
			uint32 NumAllocs = Result->Num();
			for (uint32 i = 0; i < NumAllocs && ExportedCount < MaxCount; i++)
			{
				const TraceServices::IAllocationsProvider::FAllocation* Alloc = Result->Get(i);
				if (!Alloc) continue;

				// Get tag name (may be null)
				const TCHAR* TagNamePtr = AllocProvider->GetTagName(Alloc->GetTag());
				FString TagStr = TagNamePtr ? FString(TagNamePtr) : TEXT("Unknown");

				// Apply pool filter if specified (using pre-parsed array)
				if (bHasPoolFilter)
				{
					bool bMatched = false;
					for (const FString& FilterEntry : ParsedPoolFilter)
					{
						if (TagStr.Equals(FilterEntry, ESearchCase::IgnoreCase))
						{
							bMatched = true;
							break;
						}
					}
					if (!bMatched)
					{
						continue;
					}
				}

				// Build CSV row for allocation event
				TArray<FString> Fields;
				Fields.Add(FString::Printf(TEXT("%.6f"), Alloc->GetStartTime()));
				Fields.Add(LexToString(Alloc->GetAllocThreadId()));
				Fields.Add(LexToString(Alloc->GetAddress()));  // DECIMAL (not hex) for Rust parser
				Fields.Add(LexToString(Alloc->GetSize()));
				Fields.Add(LexToString(Alloc->GetAllocCallstackId()));
				Fields.Add(TEXT("Alloc"));
				Fields.Add(EscapeCSVField(TagStr));

				if (!WriteCSVRow(FileHandle.Get(), Fields))
				{
					AllocProvider->EndRead();
					return -1;  // Write error
				}

				ExportedCount++;
			}
		}

		if (Status.Status == TraceServices::IAllocationsProvider::EQueryStatus::Working)
		{
			// Query still in progress, continue polling
			continue;
		}
	}

	AllocProvider->CancelQuery(QueryHandle);
	AllocProvider->EndRead();

	FileHandle->Flush();
	FileHandle.Reset();

	return ExportedCount;
}

int32 UCUInsightsMemoryCSVCommandlet::ExportPoolsCSV(
	const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
	const FString& OutputPath,
	const FString& PoolFilter)
{
	// Get IAllocationsProvider
	const TraceServices::IAllocationsProvider* AllocProvider = Session->ReadProvider<TraceServices::IAllocationsProvider>("AllocationsProvider");
	if (!AllocProvider)
	{
		UE_LOG(LogNarshaMCPInsights, Warning,
			TEXT("[InsightsMemoryCSV] IAllocationsProvider not available. "
				 "The trace does not contain memory allocation data. "
				 "To capture memory data: "
				 "(1) Enable Memory Insights in Editor: Edit > Preferences > Memory Insights, "
				 "(2) Launch with '-trace=memory' flag, "
				 "(3) Re-capture the trace and re-run this commandlet."));
		return -1;
	}

	// Open output file
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenWrite(*OutputPath));
	if (!FileHandle)
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to create output file: %s"), *OutputPath);
		return -1;
	}

	// Write CSV header
	TArray<FString> Headers = {
		TEXT("PoolTag"), TEXT("AllocationCount"), TEXT("OutstandingAllocations"),
		TEXT("PeakMemoryMB"), TEXT("OutstandingMemoryMB")
	};
	if (!WriteCSVHeader(FileHandle.Get(), Headers))
	{
		return -1;
	}

	// Aggregate by pool tag using Query API
	struct FPoolStats
	{
		uint64 AllocationCount = 0;
		uint64 OutstandingCount = 0;
		uint64 PeakMemory = 0;
		uint64 OutstandingMemory = 0;
		FString TagName;  // Store tag name during BeginRead() to avoid accessing provider after EndRead()
	};

	TMap<TraceServices::TagIdType, FPoolStats> PoolStatsMap;

	AllocProvider->BeginRead();

	// Query all allocations to aggregate by tag
	TraceServices::IAllocationsProvider::FQueryParams QueryParams;
	QueryParams.Rule = TraceServices::IAllocationsProvider::EQueryRule::AaB;  // All alloc events
	QueryParams.TimeA = 0.0;
	QueryParams.TimeB = TNumericLimits<double>::Max();
	QueryParams.TimeC = 0.0;
	QueryParams.TimeD = 0.0;

	TraceServices::IAllocationsProvider::FQueryHandle QueryHandle = AllocProvider->StartQuery(QueryParams);

	bool bQueryComplete = false;
	while (!bQueryComplete)
	{
		TraceServices::IAllocationsProvider::FQueryStatus Status = AllocProvider->PollQuery(QueryHandle);

		if (Status.Status == TraceServices::IAllocationsProvider::EQueryStatus::Done)
		{
			bQueryComplete = true;
		}

		TraceServices::IAllocationsProvider::FQueryResult Result = Status.NextResult();
		if (Result.IsValid())
		{
			uint32 NumAllocs = Result->Num();
			for (uint32 i = 0; i < NumAllocs; i++)
			{
				const TraceServices::IAllocationsProvider::FAllocation* Alloc = Result->Get(i);
				if (!Alloc) continue;

				TraceServices::TagIdType Tag = Alloc->GetTag();
				uint64 Size = Alloc->GetSize();

				FPoolStats& Stats = PoolStatsMap.FindOrAdd(Tag);
				Stats.AllocationCount++;

				// Store tag name during BeginRead() (thread-safe)
				if (Stats.TagName.IsEmpty())
				{
					const TCHAR* TagNamePtr = AllocProvider->GetTagName(Tag);
					Stats.TagName = TagNamePtr ? FString(TagNamePtr) : FString::Printf(TEXT("Tag_%u"), Tag);
				}

				// Live allocs use EndTime=infinity (TraceServices AllocationsProvider);
				// freed allocs get a finite free time. Issue #21705: the old
				// (EndTime==0 || EndTime>StartTime) predicate matched everything.
				if (!FMath::IsFinite(Alloc->GetEndTime()))
				{
					Stats.OutstandingCount++;
					Stats.OutstandingMemory += Size;
				}

				// Track peak memory (max of outstanding at any point)
				if (Stats.OutstandingMemory > Stats.PeakMemory)
				{
					Stats.PeakMemory = Stats.OutstandingMemory;
				}
			}
		}

		if (Status.Status == TraceServices::IAllocationsProvider::EQueryStatus::Working)
		{
			continue;
		}
	}

	AllocProvider->CancelQuery(QueryHandle);
	AllocProvider->EndRead();

	// Pre-parse pool filter once (avoid re-parsing per pool — P-1 perf fix)
	TArray<FString> ParsedPoolFilter;
	if (!PoolFilter.IsEmpty())
	{
		PoolFilter.ParseIntoArray(ParsedPoolFilter, TEXT(","), true);
		for (FString& Entry : ParsedPoolFilter)
		{
			Entry.TrimStartAndEndInline();
		}
	}

	// Write aggregated pool statistics to CSV
	int32 PoolCount = 0;
	for (const auto& Pair : PoolStatsMap)
	{
		const FPoolStats& Stats = Pair.Value;

		// Apply pool filter if specified (uses pre-parsed entries)
		if (!MatchesPoolFilter(Stats.TagName, ParsedPoolFilter))
		{
			continue;
		}

		// Use pre-stored tag name (no provider access needed)
		TArray<FString> Fields;
		Fields.Add(EscapeCSVField(Stats.TagName));
		Fields.Add(LexToString(Stats.AllocationCount));
		Fields.Add(LexToString(Stats.OutstandingCount));
		Fields.Add(FString::Printf(TEXT("%.2f"), Stats.PeakMemory / BytesToMB));
		Fields.Add(FString::Printf(TEXT("%.2f"), Stats.OutstandingMemory / BytesToMB));

		if (!WriteCSVRow(FileHandle.Get(), Fields))
		{
			FileHandle->Flush();
			FileHandle.Reset();
			return -1;
		}

		PoolCount++;
	}

	FileHandle->Flush();
	FileHandle.Reset();

	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] Exported %d pool statistics"), PoolCount);

	return PoolCount;
}

int32 UCUInsightsMemoryCSVCommandlet::ExportLLMCountersCSV(
	const TSharedPtr<const TraceServices::IAnalysisSession>& Session,
	const FString& OutputPath)
{
	// Get IMemoryProvider
	const TraceServices::IMemoryProvider* MemoryProvider = Session->ReadProvider<TraceServices::IMemoryProvider>("MemoryProvider");
	if (!MemoryProvider)
	{
		UE_LOG(LogNarshaMCPInsights, Warning,
			TEXT("[InsightsMemoryCSV] IMemoryProvider not available. "
				 "The trace does not contain LLM memory tag data. "
				 "To capture LLM data: "
				 "(1) Enable Memory Insights with LLM tracking in Editor, "
				 "(2) Launch with '-trace=memory' flag, "
				 "(3) Re-capture the trace and re-run this commandlet."));
		return -1;
	}

	// Open output file
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> FileHandle(PlatformFile.OpenWrite(*OutputPath));
	if (!FileHandle)
	{
		UE_LOG(LogNarshaMCPInsights, Error, TEXT("[InsightsMemoryCSV] Failed to create output file: %s"), *OutputPath);
		return -1;
	}

	// Write CSV header
	TArray<FString> Headers = {
		TEXT("Timestamp"), TEXT("CounterName"), TEXT("ValueMB")
	};
	if (!WriteCSVHeader(FileHandle.Get(), Headers))
	{
		return -1;
	}

	// Enumerate LLM tags and their samples
	MemoryProvider->BeginRead();

	int32 SampleCount = 0;

	// Enumerate all trackers
	MemoryProvider->EnumerateTrackers([&](const TraceServices::FMemoryTrackerInfo& TrackerInfo)
	{
		// Enumerate all tags for this tracker
		MemoryProvider->EnumerateTags([&](const TraceServices::FMemoryTagInfo& TagInfo)
		{
			// Skip tags not used by this tracker
			if ((TagInfo.Trackers & (1ULL << TrackerInfo.Id)) == 0)
			{
				return;
			}

			// Get sample count for this tag
			uint64 TagSampleCount = MemoryProvider->GetTagSampleCount(TrackerInfo.Id, TagInfo.Id);
			if (TagSampleCount == 0)
			{
				return;
			}

			// Enumerate samples for this tag
			MemoryProvider->EnumerateTagSamples(
				TrackerInfo.Id,
				TagInfo.Id,
				0.0,
				TNumericLimits<double>::Max(),
				false,
				[&](double Time, double Duration, const TraceServices::FMemoryTagSample& Sample)
				{
					TArray<FString> Fields;
					Fields.Add(FString::Printf(TEXT("%.6f"), Time));
					Fields.Add(EscapeCSVField(TagInfo.Name));
					Fields.Add(FString::Printf(TEXT("%.2f"), Sample.Value / BytesToMB));

					WriteCSVRow(FileHandle.Get(), Fields);
					SampleCount++;
				}
			);
		});
	});

	MemoryProvider->EndRead();

	FileHandle->Flush();
	FileHandle.Reset();

	UE_LOG(LogNarshaMCPInsights, Display, TEXT("[InsightsMemoryCSV] Exported %d LLM counter samples"), SampleCount);

	return SampleCount;
}

bool UCUInsightsMemoryCSVCommandlet::MatchesPoolFilter(const FString& TagName, const FString& PoolFilter)
{
	// Empty filter = match all (backward compatible)
	if (PoolFilter.IsEmpty())
	{
		return true;
	}

	// Parse comma-separated filter entries and trim whitespace
	TArray<FString> FilterEntries;
	PoolFilter.ParseIntoArray(FilterEntries, TEXT(","), true);

	for (const FString& Entry : FilterEntries)
	{
		FString TrimmedEntry = Entry.TrimStartAndEnd();
		if (TagName.Equals(TrimmedEntry, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

bool UCUInsightsMemoryCSVCommandlet::MatchesPoolFilter(const FString& TagName, const TArray<FString>& ParsedFilterEntries)
{
	// Empty filter = match all (backward compatible)
	if (ParsedFilterEntries.Num() == 0)
	{
		return true;
	}

	for (const FString& Entry : ParsedFilterEntries)
	{
		if (TagName.Equals(Entry, ESearchCase::IgnoreCase))
		{
			return true;
		}
	}

	return false;
}

FString UCUInsightsMemoryCSVCommandlet::EscapeCSVField(const FString& Value)
{
	// Check if escaping needed
	bool bNeedsQuotes = Value.Contains(TEXT(",")) || Value.Contains(TEXT("\"")) || Value.Contains(TEXT("\n"));

	if (!bNeedsQuotes)
	{
		return Value;
	}

	// Escape internal quotes by doubling them
	FString Escaped = Value.Replace(TEXT("\""), TEXT("\"\""));

	// Wrap in quotes
	return FString::Printf(TEXT("\"%s\""), *Escaped);
}

bool UCUInsightsMemoryCSVCommandlet::WriteCSVHeader(IFileHandle* FileHandle, const TArray<FString>& Headers)
{
	FString HeaderLine = FString::Join(Headers, TEXT(",")) + TEXT("\n");
	return FileHandle->Write((const uint8*)TCHAR_TO_UTF8(*HeaderLine), FCStringAnsi::Strlen(TCHAR_TO_UTF8(*HeaderLine)));
}

bool UCUInsightsMemoryCSVCommandlet::WriteCSVRow(IFileHandle* FileHandle, const TArray<FString>& Fields)
{
	FString RowLine = FString::Join(Fields, TEXT(",")) + TEXT("\n");
	return FileHandle->Write((const uint8*)TCHAR_TO_UTF8(*RowLine), FCStringAnsi::Strlen(TCHAR_TO_UTF8(*RowLine)));
}

void UCUInsightsMemoryCSVCommandlet::ParseParameters(
	const FString& Params,
	FString& OutTraceFile,
	double& OutTimeStart,
	double& OutTimeEnd,
	FString& OutPoolFilter,
	int32& OutMaxAllocations)
{
	// Parse -TraceFile="path"
	FParse::Value(*Params, TEXT("-TraceFile="), OutTraceFile);

	// Parse -TimeRangeStart=10.0
	FString TimeStartStr;
	if (FParse::Value(*Params, TEXT("-TimeRangeStart="), TimeStartStr))
	{
		OutTimeStart = FCString::Atod(*TimeStartStr);
	}

	// Parse -TimeRangeEnd=60.0
	FString TimeEndStr;
	if (FParse::Value(*Params, TEXT("-TimeRangeEnd="), TimeEndStr))
	{
		OutTimeEnd = FCString::Atod(*TimeEndStr);
	}

	// Parse -PoolFilter="Textures,Audio"
	FParse::Value(*Params, TEXT("-PoolFilter="), OutPoolFilter);

	// Parse -MaxAllocations=1000000
	FString MaxAllocsStr;
	if (FParse::Value(*Params, TEXT("-MaxAllocations="), MaxAllocsStr))
	{
		OutMaxAllocations = FCString::Atoi(*MaxAllocsStr);
	}
}
