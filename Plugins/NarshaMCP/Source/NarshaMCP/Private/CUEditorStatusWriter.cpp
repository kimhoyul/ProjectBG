// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUEditorStatusWriter.h"
#include "CULog.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Timespan.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

namespace
{
	constexpr const TCHAR* EditorStatusLockName = TEXT("NarshaMCPEditorStatus");
	constexpr const TCHAR* WriterPidField = TEXT("writer_pid");
	constexpr const TCHAR* WriterStartTokenField = TEXT("writer_start_token");

	FString GetStatusFilePath(const FString& ProjectDir)
	{
		return FPaths::Combine(ProjectDir, TEXT(".narshamcp"), TEXT("editor_status.json"));
	}

	bool LoadStatusObject(const FString& StatusFilePath, TSharedPtr<FJsonObject>& OutObject)
	{
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *StatusFilePath))
		{
			return false;
		}

		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		return FJsonSerializer::Deserialize(Reader, OutObject) && OutObject.IsValid();
	}

	bool ReplaceStatusFile(const FString& StatusFilePath, const FString& TempFilePath)
	{
#if PLATFORM_WINDOWS
		// FFileManagerGeneric::Move(Replace=true) deletes the destination before
		// moving on Windows. ReplaceFileW keeps the previous valid marker intact
		// if replacement fails and swaps same-volume files without an absent gap.
		if (IFileManager::Get().FileExists(*StatusFilePath))
		{
			return ::ReplaceFileW(
				*StatusFilePath, *TempFilePath, nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr) != 0;
		}
		return ::MoveFileExW(*TempFilePath, *StatusFilePath, MOVEFILE_WRITE_THROUGH) != 0;
#else
		// Unix platform files implement MoveFile with same-volume rename semantics.
		return FPlatformFileManager::Get().GetPlatformFile().MoveFile(
			*StatusFilePath, *TempFilePath);
#endif
	}
}

bool FCUEditorStatusWriter::ShouldManageEditorStatus(bool bIsEditor, bool bIsCommandlet)
{
	return bIsEditor && !bIsCommandlet;
}

void FCUEditorStatusWriter::WriteEditorStatus(const FString& ProjectDir)
{
	// Create .narshamcp directory
	const FString NarshaMCPDir = FPaths::Combine(ProjectDir, TEXT(".narshamcp"));
	IFileManager& FileManager = IFileManager::Get();

	if (!FileManager.DirectoryExists(*NarshaMCPDir))
	{
		if (!FileManager.MakeDirectory(*NarshaMCPDir, /*Tree=*/true))
		{
			UE_LOG(LogNarshaMCP, Error, TEXT("[EditorStatus] Failed to create directory: %s"), *NarshaMCPDir);
			return;
		}
	}

	// Issue #27966: serialize every cross-process status transaction. The same lock
	// protects DeleteEditorStatus's read/compare/delete sequence from a writer race.
	FSystemWideCriticalSection StatusLock(EditorStatusLockName, FTimespan::FromSeconds(5));
	if (!StatusLock.IsValid())
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("[EditorStatus] Failed to acquire ownership lock; status left unchanged"));
		return;
	}

	const uint32 CurrentPid = GetCurrentProcessId();
	const FString& CurrentStartToken = GetCurrentProcessStartToken();

	// Build JSON
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetBoolField(TEXT("plugin_loaded"), true);
	JsonObject->SetNumberField(TEXT("editor_pid"), CurrentPid);
	JsonObject->SetNumberField(WriterPidField, CurrentPid);
	JsonObject->SetStringField(WriterStartTokenField, CurrentStartToken);
	JsonObject->SetStringField(TEXT("project_path"), ProjectDir);
	JsonObject->SetStringField(TEXT("timestamp"), GetISO8601Timestamp());
	JsonObject->SetBoolField(TEXT("communication_ready"), true);

	// Serialize
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("[EditorStatus] Failed to serialize JSON"));
		return;
	}

	// Write to a process-unique temp file, then replace the marker while holding the
	// cross-process lock so readers never observe a partial JSON document.
	const FString StatusFilePath = GetStatusFilePath(ProjectDir);
	const FString TempFilePath = FString::Printf(
		TEXT("%s.%u.%s.tmp"), *StatusFilePath, CurrentPid, *CurrentStartToken);
	if (!FFileHelper::SaveStringToFile(
		JsonString, *TempFilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("[EditorStatus] Failed to write temp file: %s"), *TempFilePath);
		return;
	}

	if (!ReplaceStatusFile(StatusFilePath, TempFilePath))
	{
		FileManager.Delete(*TempFilePath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
		UE_LOG(LogNarshaMCP, Error, TEXT("[EditorStatus] Failed to replace: %s"), *StatusFilePath);
		return;
	}

	UE_LOG(LogNarshaMCP, Log, TEXT("[EditorStatus] Written: %s (owner=%u/%s)"),
		*StatusFilePath, CurrentPid, *CurrentStartToken);
}

void FCUEditorStatusWriter::DeleteEditorStatus(const FString& ProjectDir)
{
	const FString StatusFilePath = GetStatusFilePath(ProjectDir);
	IFileManager& FileManager = IFileManager::Get();

	if (!FileManager.FileExists(*StatusFilePath))
	{
		return;
	}

	FSystemWideCriticalSection StatusLock(EditorStatusLockName, FTimespan::FromSeconds(5));
	if (!StatusLock.IsValid())
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("[EditorStatus] Failed to acquire ownership lock; delete skipped"));
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	uint32 WriterPid = 0;
	FString WriterStartToken;
	if (!LoadStatusObject(StatusFilePath, JsonObject)
		|| !JsonObject->TryGetNumberField(WriterPidField, WriterPid)
		|| !JsonObject->TryGetStringField(WriterStartTokenField, WriterStartToken)
		|| WriterStartToken.IsEmpty())
	{
		UE_LOG(LogNarshaMCP, Warning,
			TEXT("[EditorStatus] Delete skipped: marker has no valid writer ownership: %s"), *StatusFilePath);
		return;
	}

	const uint32 CurrentPid = GetCurrentProcessId();
	const FString& CurrentStartToken = GetCurrentProcessStartToken();
	if (WriterPid != CurrentPid || WriterStartToken != CurrentStartToken)
	{
		UE_LOG(LogNarshaMCP, Log,
			TEXT("[EditorStatus] Delete skipped: foreign owner=%u/%s current=%u/%s"),
			WriterPid, *WriterStartToken, CurrentPid, *CurrentStartToken);
		return;
	}

	if (FileManager.Delete(*StatusFilePath, /*RequireExists=*/true, /*EvenReadOnly=*/true, /*Quiet=*/true))
	{
		UE_LOG(LogNarshaMCP, Log, TEXT("[EditorStatus] Deleted owned marker: %s"), *StatusFilePath);
	}
	else
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("[EditorStatus] Failed to delete owned marker: %s"), *StatusFilePath);
	}
}

uint32 FCUEditorStatusWriter::GetCurrentProcessId()
{
	return FPlatformProcess::GetCurrentProcessId();
}

const FString& FCUEditorStatusWriter::GetCurrentProcessStartToken()
{
	// A process-lifetime GUID is equivalent to a process-start token for marker
	// ownership: PID reuse or a separate Unreal process cannot reproduce the pair.
	static const FString ProcessStartToken = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	return ProcessStartToken;
}

FString FCUEditorStatusWriter::GetISO8601Timestamp()
{
	return FDateTime::UtcNow().ToIso8601();
}
