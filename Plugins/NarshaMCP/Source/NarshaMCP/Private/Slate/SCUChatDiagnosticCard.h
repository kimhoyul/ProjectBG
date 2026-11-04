// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8836 (Phase 9-1): SCUChatDiagnosticCard — level diagnostic findings + suggestions UI

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Dom/JsonObject.h"

/** Diagnostic severity for SCUChatDiagnosticCard color/icon mapping. */
enum class ECUChatDiagnosticSeverity : uint8
{
	Error,
	Warning,
	Info,
	Suggestion,
};

/** Single finding/suggestion entry rendered as a row. */
struct FCUChatDiagnosticEntry
{
	ECUChatDiagnosticSeverity Severity = ECUChatDiagnosticSeverity::Info;
	FString Rule;
	FString Message;
	FString Entity;
	FString Action;       // for suggestions only
	double Value = 0.0;   // for findings (current value)
	double Threshold = 0.0;
};

/**
 * Diagnostic card for `ue_manage_project_ops:level_diagnose` (Issue #8836).
 *
 * Layout:
 *   [Header: total / errors X / warnings Y / infos Z / suggestions S]
 *   [Findings list with severity icons]
 *   [Suggestions list with action badges]
 */
class SCUChatDiagnosticCard : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatDiagnosticCard)
		: _ErrorCount(0)
		, _WarningCount(0)
		, _InfoCount(0)
	{}
		SLATE_ARGUMENT(int32, ErrorCount)
		SLATE_ARGUMENT(int32, WarningCount)
		SLATE_ARGUMENT(int32, InfoCount)
		SLATE_ARGUMENT(TArray<FCUChatDiagnosticEntry>, Findings)
		SLATE_ARGUMENT(TArray<FCUChatDiagnosticEntry>, Suggestions)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static FLinearColor GetSeverityColor(ECUChatDiagnosticSeverity Sev);
	static FString GetSeverityIcon(ECUChatDiagnosticSeverity Sev);
	static ECUChatDiagnosticSeverity ParseSeverityString(const FString& Str);

#if WITH_AUTOMATION_WORKER
	int32 TestGetErrorCount() const { return ErrorCount; }
	int32 TestGetWarningCount() const { return WarningCount; }
	int32 TestGetFindingsCount() const { return Findings.Num(); }
	int32 TestGetSuggestionsCount() const { return Suggestions.Num(); }
#endif

private:
	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	int32 InfoCount = 0;
	TArray<FCUChatDiagnosticEntry> Findings;
	TArray<FCUChatDiagnosticEntry> Suggestions;

	TSharedRef<class SWidget> BuildHeader() const;
	TSharedRef<class SWidget> BuildEntryRow(const FCUChatDiagnosticEntry& Entry, bool bIsSuggestion) const;
};
