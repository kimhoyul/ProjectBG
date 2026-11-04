// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26815 (AI-01)

#include "CUAiRuntimeTraceLibrary.h"
#include "CULog.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "VisualLogger/VisualLogger.h"

namespace
{
	FString FindNewestVlogUnder(const FString& Dir)
	{
		TArray<FString> Found;
		IFileManager::Get().FindFiles(Found, *(Dir / TEXT("*.vlog")), true, false);
		TArray<FString> Bvlog;
		IFileManager::Get().FindFiles(Bvlog, *(Dir / TEXT("*.bvlog")), true, false);
		Found.Append(Bvlog);
		TArray<FString> Temp;
		IFileManager::Get().FindFiles(Temp, *(Dir / TEXT("VTEMP_*.*")), true, false);
		Found.Append(Temp);

		FString Best;
		FDateTime BestTime = FDateTime::MinValue();
		for (const FString& Name : Found)
		{
			const FString Full = Dir / Name;
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Full);
			if (Stamp > BestTime)
			{
				BestTime = Stamp;
				Best = Full;
			}
		}
		return Best;
	}
}

bool UCUAiRuntimeTraceLibrary::StartBtFileRecording()
{
#if ENABLE_VISUAL_LOG
	FVisualLogger::Get().SetIsRecording(true);
	FVisualLogger::Get().SetIsRecordingToFile(true);
	const bool bOn = FVisualLogger::Get().IsRecordingToFile();
	UE_LOG(LogNarshaMCP, Display, TEXT("StartBtFileRecording recording_to_file=%s"),
		bOn ? TEXT("true") : TEXT("false"));
	return bOn;
#else
	UE_LOG(LogNarshaMCP, Error, TEXT("StartBtFileRecording: ENABLE_VISUAL_LOG is off"));
	return false;
#endif
}

bool UCUAiRuntimeTraceLibrary::StopBtFileRecording()
{
#if ENABLE_VISUAL_LOG
	if (!FVisualLogger::Get().IsRecordingToFile())
	{
		UE_LOG(LogNarshaMCP, Warning, TEXT("StopBtFileRecording: not recording to file"));
		return false;
	}
	FVisualLogger::Get().SetIsRecordingToFile(false);
	FVisualLogger::Get().SetIsRecording(false);
	return true;
#else
	return false;
#endif
}

bool UCUAiRuntimeTraceLibrary::DiscardBtFileRecording()
{
#if ENABLE_VISUAL_LOG
	FVisualLogger::Get().DiscardRecordingToFile();
	FVisualLogger::Get().SetIsRecording(false);
	return true;
#else
	return false;
#endif
}

bool UCUAiRuntimeTraceLibrary::IsBtFileRecording()
{
#if ENABLE_VISUAL_LOG
	return FVisualLogger::Get().IsRecordingToFile();
#else
	return false;
#endif
}

FString UCUAiRuntimeTraceLibrary::GetLatestVlogPath()
{
	return FindNewestVlogUnder(FPaths::ProjectLogDir());
}
