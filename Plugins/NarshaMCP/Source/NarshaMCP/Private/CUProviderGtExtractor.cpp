// Copyright 2026 Next Stage Inc. All rights reserved.

// Issue #25 + #68 — Provider API GT Extractor implementation.
//
// Uses UE 5.7 `IAnalysisService::Analyze()` to build a session via
// stock analyzers, then reads `ILoadTimeProfilerProvider` /
// `IBookmarkProvider` / `ILogProvider` to emit CSVs. UE 5.7 Provider
// API surface: IBookmarkProvider — Bookmarks.h:20-27, ILogProvider —
// Log.h:33-172, FLogCategoryInfo — Log.h:16-20. RAII guard
// FAnalysisSessionReadScope at AnalysisSession.h:147-162 (ctor body
// :152 calls BeginRead(), dtor body :157 calls EndRead()). See the
// header for the schema rationale. Full mapping in
// docs/utrace-parity/MARKERS_UE_5_7_GROUND_TRUTH.md.

#include "CUProviderGtExtractor.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "Containers/Set.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"

#include "TraceServices/AnalysisService.h"
#include "TraceServices/ITraceServicesModule.h"
#include "TraceServices/ModuleService.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "TraceServices/Model/Bookmarks.h"
#include "TraceServices/Model/LoadTimeProfiler.h"
#include "TraceServices/Model/Log.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPProviderGt, Log, All);

namespace NarshaMCP::ProviderGt
{

namespace
{

// =================================================================
// Sanitize a string for CSV emission. Replaces `,` `\r` `\n` `|`
// with safe substitutes so neither CSV nor pipe-separated readers
// break. The Rust comparator side strips the same characters.
// =================================================================
static FString SanitizeForCsv(const FString& In)
{
	FString Out = In;
	Out.ReplaceCharInline(TEXT(','), TEXT(';'));
	Out.ReplaceCharInline(TEXT('|'), TEXT('/'));
	Out.ReplaceCharInline(TEXT('\n'), TEXT(' '));
	Out.ReplaceCharInline(TEXT('\r'), TEXT(' '));
	return Out;
}

// =================================================================
// LoadTime provider extraction
// =================================================================
static bool ExtractLoadtime(const TraceServices::IAnalysisSession& Session, const FString& OutDir)
{
	using namespace TraceServices;

	// UE 5.7 standard pattern — `FAnalysisSessionReadScope` RAII guard
	// (AnalysisSession.h:147-162; ctor body :152 calls BeginRead(),
	// dtor body :157 calls EndRead()). Provider read methods inside
	// the scope call Session.ReadAccessCheck() separately. Calling
	// BeginRead() directly without the RAII does not satisfy the check.
	FAnalysisSessionReadScope SessionReadScope(Session);

	const ILoadTimeProfilerProvider* LoadProvider = ReadLoadTimeProfilerProvider(Session);
	if (!LoadProvider)
	{
		UE_LOG(LogNarshaMCPProviderGt, Warning,
			TEXT("[provider-gt] LoadTimeProfilerProvider unavailable — emitting empty CSVs."));
	}

	// ----------- Packages: name set + summary fields -----------
	TSet<FString> PackageNames;
	TArray<TTuple<uint32, FString, uint32, uint32, uint32, int32>> PackageSummaries;
	TSet<FString> ClassNamesViaExports;
	int32 ExportRowCount = 0;

	if (LoadProvider)
	{
		// LoadTimeProfiler 모듈에는 EnumeratePackages 직접 API가 없고
		// `CreatePackageDetailsTable(intervalStart, intervalEnd)` 가 입력 시간 범위의
		// per-package row 를 생성한다. infinity 범위로 호출하면 모든 패키지를 받음.
		// `CreateExportDetailsTable` 도 동일 — Class.Name 을 export 마다 추적.
		const double InfStart = 0.0;
		const double InfEnd = Session.GetDurationSeconds() + 3600.0; // 1h 여유 — overkill but safe

		// PackageDetailsTable: iterates per FPackageInfo.
		TUniquePtr<ITable<FPackagesTableRow>> PackageTbl(
			LoadProvider->CreatePackageDetailsTable(InfStart, InfEnd));
		if (PackageTbl.IsValid())
		{
			const uint64 Cnt = PackageTbl->GetRowCount();
			for (uint64 i = 0; i < Cnt; ++i)
			{
				TUniquePtr<ITableReader<FPackagesTableRow>> Rd(PackageTbl->CreateReader());
				if (!Rd.IsValid()) break;
				Rd->SetRowIndex(static_cast<int32>(i));
				const FPackagesTableRow* Row = Rd->GetCurrentRow();
				if (!Row || !Row->PackageInfo) continue;
				const FPackageInfo& P = *Row->PackageInfo;
				const FString Name = P.Name ? FString(P.Name) : FString();
				if (!Name.IsEmpty()) PackageNames.Add(Name);
				PackageSummaries.Add(MakeTuple(
					P.Id,
					Name,
					P.Summary.TotalHeaderSize,
					P.Summary.ImportCount,
					P.Summary.ExportCount,
					P.Summary.Priority));
			}
		}

		// ExportDetailsTable: per export row → Class.Name 으로 class set.
		TUniquePtr<ITable<FExportsTableRow>> ExportTbl(
			LoadProvider->CreateExportDetailsTable(InfStart, InfEnd));
		if (ExportTbl.IsValid())
		{
			const uint64 Cnt = ExportTbl->GetRowCount();
			ExportRowCount = static_cast<int32>(Cnt);
			TUniquePtr<ITableReader<FExportsTableRow>> Rd(ExportTbl->CreateReader());
			if (Rd.IsValid())
			{
				for (uint64 i = 0; i < Cnt; ++i)
				{
					Rd->SetRowIndex(static_cast<int32>(i));
					const FExportsTableRow* R = Rd->GetCurrentRow();
					if (!R || !R->ExportInfo || !R->ExportInfo->Class) continue;
					const TCHAR* ClassName = R->ExportInfo->Class->Name;
					if (ClassName && *ClassName)
					{
						ClassNamesViaExports.Add(FString(ClassName));
					}
				}
			}
		}
	}

	// ----------- Load requests count -----------
	int32 RequestCount = 0;
	if (LoadProvider)
	{
		const ITable<FLoadRequest>& ReqTbl = LoadProvider->GetRequestsTable();
		RequestCount = static_cast<int32>(ReqTbl.GetRowCount());
	}

	// SessionReadScope auto-releases at function exit.

	// ----------- Emit CSVs -----------
	auto Write = [&](const FString& Filename, const FString& Body) -> void
	{
		const FString Path = FPaths::Combine(OutDir, Filename);
		FFileHelper::SaveStringToFile(Body, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	};

	// package_names — sorted name set
	{
		TArray<FString> Sorted = PackageNames.Array();
		Sorted.Sort();
		FString Body = TEXT("name\n");
		for (const FString& N : Sorted)
		{
			Body += SanitizeForCsv(N);
			Body += TEXT("\n");
		}
		Write(TEXT("provider_loadtime_package_names.csv"), Body);
	}

	// package_summaries — (id, name, header, import, export, priority) sorted by id
	{
		Algo::SortBy(PackageSummaries, [](const auto& T) { return T.template Get<0>(); });
		FString Body = TEXT("id,name,total_header_size,import_count,export_count,priority\n");
		for (const auto& T : PackageSummaries)
		{
			Body += FString::Printf(TEXT("%u,%s,%u,%u,%u,%d\n"),
				T.template Get<0>(),
				*SanitizeForCsv(T.template Get<1>()),
				T.template Get<2>(),
				T.template Get<3>(),
				T.template Get<4>(),
				T.template Get<5>());
		}
		Write(TEXT("provider_loadtime_package_summaries.csv"), Body);
	}

	// class_names — sorted unique class names via Exports
	{
		TArray<FString> Sorted = ClassNamesViaExports.Array();
		Sorted.Sort();
		FString Body = TEXT("name\n");
		for (const FString& N : Sorted)
		{
			Body += SanitizeForCsv(N);
			Body += TEXT("\n");
		}
		Write(TEXT("provider_loadtime_class_names.csv"), Body);
	}

	// counts: package_count, request_count, export_row_count, class_name_count
	{
		FString Body = TEXT("metric,value\n");
		Body += FString::Printf(TEXT("package_count,%d\n"), PackageNames.Num());
		Body += FString::Printf(TEXT("request_count,%d\n"), RequestCount);
		Body += FString::Printf(TEXT("export_row_count,%d\n"), ExportRowCount);
		Body += FString::Printf(TEXT("class_name_count,%d\n"), ClassNamesViaExports.Num());
		Write(TEXT("provider_loadtime_counts.csv"), Body);
	}

	UE_LOG(LogNarshaMCPProviderGt, Display,
		TEXT("[provider-gt] LoadTime: %d packages, %d class names, %d exports, %d requests."),
		PackageNames.Num(), ClassNamesViaExports.Num(), ExportRowCount, RequestCount);
	return true;
}

// =================================================================
// Markers (Bookmark + Log) provider extraction
// =================================================================
static bool ExtractMarkers(const TraceServices::IAnalysisSession& Session, const FString& OutDir)
{
	using namespace TraceServices;

	// UE 5.7 AnalysisSession.h:147-162 RAII guard.
	FAnalysisSessionReadScope SessionReadScope(Session);

	// ----------- Bookmarks: count + sample times -----------
	// UE 5.7 IBookmarkProvider — Bookmarks.h:20-27.
	const IBookmarkProvider& BookmarkProvider = ReadBookmarkProvider(Session);
	// UE 5.7 Bookmarks.h:25.
	const int64 BookmarkCount = static_cast<int64>(BookmarkProvider.GetBookmarkCount());

	// EnumerateBookmarks across the full session range — collect
	// (time, callstack_id, text). Provider exposes pre-formatted Text
	// (FormatArgs already substituted into FormatString), so for
	// bookmarks without dynamic args this equals the spec's
	// FormatString. We sort by time for deterministic output.
	struct FBookmarkRow
	{
		double Time;
		uint32 CallstackId;
		FString Text;
		bool operator<(const FBookmarkRow& O) const
		{
			if (Time != O.Time) return Time < O.Time;
			if (CallstackId != O.CallstackId) return CallstackId < O.CallstackId;
			return Text < O.Text;
		}
	};
	TArray<FBookmarkRow> BookmarkRows;
	BookmarkRows.Reserve(static_cast<int32>(BookmarkCount));
	{
		const double SessionEnd = Session.GetDurationSeconds() + 1.0;
		// UE 5.7 Bookmarks.h:26 — time-range enumeration.
		BookmarkProvider.EnumerateBookmarks(0.0, SessionEnd,
			[&BookmarkRows](const FBookmark& B)
			{
				FBookmarkRow R;
				R.Time = B.Time;
				R.CallstackId = B.CallstackId;
				R.Text = B.Text ? FString(B.Text) : FString();
				BookmarkRows.Add(MoveTemp(R));
			});
	}

	// ----------- Log: categories + message count -----------
	// UE 5.7 ILogProvider — Log.h:33-172.
	const ILogProvider& LogProvider = ReadLogProvider(Session);
	// UE 5.7 Log.h:44 — fired message count (Messages.Num() per Log.cpp:232).
	const int64 LogMessageCount = static_cast<int64>(LogProvider.GetMessageCount());

	struct FCategoryRow
	{
		FString Name;
		uint8 DefaultVerbosity;
		bool operator<(const FCategoryRow& O) const
		{
			if (Name != O.Name) return Name < O.Name;
			return DefaultVerbosity < O.DefaultVerbosity;
		}
		bool operator==(const FCategoryRow& O) const
		{
			return Name == O.Name && DefaultVerbosity == O.DefaultVerbosity;
		}
	};
	TArray<FCategoryRow> CategoryRows;
	// UE 5.7 Log.h:134.
	const int64 CategoryCount = static_cast<int64>(LogProvider.GetCategoryCount());
	CategoryRows.Reserve(static_cast<int32>(CategoryCount));
	// UE 5.7 Log.h:143; FLogCategoryInfo at Log.h:16-20.
	LogProvider.EnumerateCategories(
		[&CategoryRows](const FLogCategoryInfo& C)
		{
			FCategoryRow R;
			R.Name = C.Name ? FString(C.Name) : FString();
			R.DefaultVerbosity = static_cast<uint8>(C.DefaultVerbosity);
			CategoryRows.Add(MoveTemp(R));
		});

	// ----------- Log: distinct (category, file, line, verbosity) spec set -----------
	// Provider does not expose LogMessageSpec directly. Although
	// `FLogProvider::MessageSpecs` is a TPagedArray<FLogMessageSpec>
	// (UE 5.7 LogPrivate.h:94), no public ILogProvider method iterates
	// it — `GetMessageSpecCount()` defaults to 0 (Log.h:127), no
	// `EnumerateMessageSpecs`. Specs are reachable only through the per-
	// message FLogMessageInfo returned by EnumerateMessagesByIndex
	// (Log.h:66 + Log.cpp:228-246). This builds the spec set from that
	// fired-only universe — the only Provider surface that exists.
	// The Tier 2.5 invariant (wire_fired ≡ provider_normalized) is
	// grounded on this fact.
	struct FLogSpecRow
	{
		FString CategoryName;
		FString File;
		int32 Line;
		uint8 Verbosity;
		bool operator<(const FLogSpecRow& O) const
		{
			if (CategoryName != O.CategoryName) return CategoryName < O.CategoryName;
			if (File != O.File) return File < O.File;
			if (Line != O.Line) return Line < O.Line;
			return Verbosity < O.Verbosity;
		}
		bool operator==(const FLogSpecRow& O) const
		{
			return CategoryName == O.CategoryName && File == O.File
				&& Line == O.Line && Verbosity == O.Verbosity;
		}
	};
	TArray<FLogSpecRow> LogSpecRows;
	if (LogMessageCount > 0)
	{
		// UE 5.7 Log.h:66 — half-open [Start, End) per Log.cpp:228-246; FIRED-ONLY universe (Messages.Num() at Log.cpp:232).
		LogProvider.EnumerateMessagesByIndex(0, static_cast<uint64>(LogMessageCount),
			[&LogSpecRows](const FLogMessageInfo& M)
			{
				FLogSpecRow R;
				R.CategoryName = (M.Category && M.Category->Name) ? FString(M.Category->Name) : FString();
				R.File = M.File ? FString(M.File) : FString();
				R.Line = M.Line;
				R.Verbosity = static_cast<uint8>(M.Verbosity);
				LogSpecRows.Add(MoveTemp(R));
			});
		// Dedup to spec set.
		Algo::Sort(LogSpecRows);
		LogSpecRows.SetNum(Algo::Unique(LogSpecRows));
	}

	// SessionReadScope auto-releases at function exit.

	// ----------- Emit CSVs -----------
	auto Write = [&](const FString& Filename, const FString& Body) -> void
	{
		const FString Path = FPaths::Combine(OutDir, Filename);
		FFileHelper::SaveStringToFile(Body, *Path,
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	};

	// bookmark_count + per-row times
	{
		Algo::Sort(BookmarkRows);
		FString Body = TEXT("time_seconds|callstack_id|text\n");
		for (const FBookmarkRow& B : BookmarkRows)
		{
			Body += FString::Printf(TEXT("%.9g|%u|%s\n"),
				B.Time, B.CallstackId, *SanitizeForCsv(B.Text));
		}
		Write(TEXT("provider_markers_bookmarks.csv"), Body);
	}

	// log_categories
	{
		Algo::Sort(CategoryRows);
		CategoryRows.SetNum(Algo::Unique(CategoryRows));
		FString Body = TEXT("name|default_verbosity\n");
		for (const FCategoryRow& C : CategoryRows)
		{
			Body += FString::Printf(TEXT("%s|%u\n"),
				*SanitizeForCsv(C.Name), C.DefaultVerbosity);
		}
		Write(TEXT("provider_markers_log_categories.csv"), Body);
	}

	// log_spec set
	{
		FString Body = TEXT("category|file|line|verbosity\n");
		for (const FLogSpecRow& S : LogSpecRows)
		{
			Body += FString::Printf(TEXT("%s|%s|%d|%u\n"),
				*SanitizeForCsv(S.CategoryName),
				*SanitizeForCsv(S.File),
				S.Line,
				S.Verbosity);
		}
		Write(TEXT("provider_markers_log_spec_set.csv"), Body);
	}

	// counts
	{
		FString Body = TEXT("metric,value\n");
		Body += FString::Printf(TEXT("bookmark_count,%lld\n"), BookmarkCount);
		Body += FString::Printf(TEXT("log_category_count,%lld\n"), CategoryCount);
		Body += FString::Printf(TEXT("log_message_count,%lld\n"), LogMessageCount);
		Body += FString::Printf(TEXT("log_spec_unique_count,%d\n"), LogSpecRows.Num());
		Write(TEXT("provider_markers_counts.csv"), Body);
	}

	UE_LOG(LogNarshaMCPProviderGt, Display,
		TEXT("[provider-gt] Markers: %lld bookmarks, %lld log categories, %lld log messages, %d unique log specs."),
		BookmarkCount, CategoryCount, LogMessageCount, LogSpecRows.Num());
	return true;
}

} // anonymous namespace

bool ExtractProviderGt(const FString& UTracePath, const FString& OutDir, bool bLoadtime, bool bMarkers)
{
	using namespace TraceServices;

	UE_LOG(LogNarshaMCPProviderGt, Display,
		TEXT("[provider-gt] Loading trace via IAnalysisService::Analyze() — %s"),
		*UTracePath);

	// Ensure output dir exists.
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);

	// Load TraceServices module and request the AnalysisService.
	// `GetAnalysisService()` returns the singleton already created by
	// the Editor at startup; `CreateAnalysisService()` would assert
	// (TraceServicesModule.cpp:78 `!AnalysisService.IsValid()`).
	ITraceServicesModule& Mod = FModuleManager::LoadModuleChecked<ITraceServicesModule>("TraceServices");
	TSharedPtr<IAnalysisService> Service = Mod.GetAnalysisService();
	if (!Service.IsValid())
	{
		UE_LOG(LogNarshaMCPProviderGt, Error,
			TEXT("[provider-gt] GetAnalysisService failed."));
		return false;
	}

	// Force-enable LoadTime/Memory/etc. modules — the Editor's default
	// `IAnalysisService` may have them disabled (Insights GUI only
	// enables them when the corresponding tab is opened). Without
	// activation, `ReadLoadTimeProfilerProvider()` returns nullptr.
	TSharedPtr<IModuleService> ModService = Mod.GetModuleService();
	if (ModService.IsValid())
	{
		TArray<FModuleInfoEx> Modules;
		ModService->GetAvailableModulesEx(Modules);
		UE_LOG(LogNarshaMCPProviderGt, Display,
			TEXT("[provider-gt] %d TraceServices modules available:"), Modules.Num());
		for (const FModuleInfoEx& M : Modules)
		{
			UE_LOG(LogNarshaMCPProviderGt, Display,
				TEXT("  - %s  (enabled=%d)"), *M.Info.Name.ToString(), M.bIsEnabled ? 1 : 0);
		}
		// Enable everything — the GT extractor wants ALL provider state.
		for (const FModuleInfoEx& M : Modules)
		{
			if (!M.bIsEnabled)
			{
				ModService->SetModuleEnabled(M.Info.Name, true);
				UE_LOG(LogNarshaMCPProviderGt, Display,
					TEXT("[provider-gt] enabled module: %s"), *M.Info.Name.ToString());
			}
		}
	}
	else
	{
		UE_LOG(LogNarshaMCPProviderGt, Warning,
			TEXT("[provider-gt] GetModuleService returned null — modules cannot be enabled."));
	}

	UE_LOG(LogNarshaMCPProviderGt, Display, TEXT("[provider-gt] step 1: Service->Analyze() begin"));
	TSharedPtr<const IAnalysisSession> Session = Service->Analyze(*UTracePath);
	UE_LOG(LogNarshaMCPProviderGt, Display, TEXT("[provider-gt] step 2: Service->Analyze() returned, Session valid=%d"), Session.IsValid() ? 1 : 0);
	if (!Session.IsValid())
	{
		UE_LOG(LogNarshaMCPProviderGt, Error, TEXT("[provider-gt] Analyze() returned null session."));
		return false;
	}
	// `Analyze()` is documented blocking; do NOT call `Wait()` here —
	// `Wait()` is const and triggers `ReadAccessCheck` internally,
	// asserting if no `FAnalysisSessionReadScope` is held by this
	// thread. Move the per-channel ReadScope inside Extract*().

	bool bAllOk = true;
	if (bLoadtime)
	{
		bAllOk &= ExtractLoadtime(*Session, OutDir);
	}
	if (bMarkers)
	{
		bAllOk &= ExtractMarkers(*Session, OutDir);
	}

	return bAllOk;
}

} // namespace NarshaMCP::ProviderGt


// =================================================================
// Console command — NarshaMCP.ExportProviderGt
// =================================================================
//
// Usage:
//   NarshaMCP.ExportProviderGt <trace.utrace> <output_dir>
//       [--channels=loadtime,markers,all] [--auto-quit]

#include "HAL/IConsoleManager.h"

namespace
{

static TSet<FString> ParseChannels(const FString& Csv)
{
	TSet<FString> Out;
	TArray<FString> Parts;
	Csv.ParseIntoArray(Parts, TEXT(","), /*CullEmpty=*/true);
	for (const FString& P : Parts)
	{
		const FString T = P.TrimStartAndEnd().ToLower();
		if (!T.IsEmpty()) Out.Add(T);
	}
	return Out;
}

static void ExportProviderGtConsoleHandler(const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogNarshaMCPProviderGt, Warning,
			TEXT("Usage: NarshaMCP.ExportProviderGt <trace.utrace> <output_dir> "
			     "[--channels=loadtime,markers,all] [--auto-quit]"));
		return;
	}

	const FString TraceFile = Args[0];
	const FString OutputDir = Args[1];

	TSet<FString> Requested;
	Requested.Add(TEXT("all"));
	bool bAutoQuit = false;

	for (int32 i = 2; i < Args.Num(); ++i)
	{
		const FString& A = Args[i];
		if (A.Equals(TEXT("--auto-quit"), ESearchCase::IgnoreCase))
		{
			bAutoQuit = true;
			continue;
		}
		FString Lhs, Rhs;
		if (A.Split(TEXT("="), &Lhs, &Rhs)
		    && Lhs.Equals(TEXT("--channels"), ESearchCase::IgnoreCase))
		{
			TSet<FString> Parsed = ParseChannels(Rhs);
			if (Parsed.Num() > 0) Requested = MoveTemp(Parsed);
		}
	}

	bool bLoadtime = Requested.Contains(TEXT("all")) || Requested.Contains(TEXT("loadtime"));
	bool bMarkers  = Requested.Contains(TEXT("all")) || Requested.Contains(TEXT("markers"));

	const bool bOk = NarshaMCP::ProviderGt::ExtractProviderGt(
		TraceFile, OutputDir, bLoadtime, bMarkers);

	UE_LOG(LogNarshaMCPProviderGt, Display,
		TEXT("[provider-gt] Done (%s)."), bOk ? TEXT("OK") : TEXT("partial"));

	if (bAutoQuit)
	{
		UE_LOG(LogNarshaMCPProviderGt, Display,
			TEXT("[provider-gt] --auto-quit set; requesting engine exit."));
		RequestEngineExit(TEXT("NarshaMCP.ExportProviderGt --auto-quit"));
	}
}

static FAutoConsoleCommand GCmdExportProviderGt(
	TEXT("NarshaMCP.ExportProviderGt"),
	TEXT("Export UE Provider API GT CSVs (third-party oracle for /utrace-parity verification). "
	     "Usage: NarshaMCP.ExportProviderGt <trace.utrace> <output_dir> [--channels=loadtime,markers,all] [--auto-quit]"),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ExportProviderGtConsoleHandler));

} // anonymous namespace