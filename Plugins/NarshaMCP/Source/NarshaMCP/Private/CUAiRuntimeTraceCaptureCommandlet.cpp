// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26815 (AI-01)

#include "CUAiRuntimeTraceCaptureCommandlet.h"
#include "CUAiRuntimeTraceLibrary.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"

#include "BehaviorTree/BTNode.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/ArchiveSaveCompressedProxy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "ProfilingDebugging/TraceAuxiliary.h"
#include "VisualLogger/VisualLogger.h"
#include "VisualLogger/VisualLoggerTypes.h"

UCUAiRuntimeTraceCaptureCommandlet::UCUAiRuntimeTraceCaptureCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCUAiRuntimeTraceCaptureCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("=== AiRuntimeTraceCapture (Issue #26815) ==="));
	const bool bIssue27940Fixture = FParse::Param(*Params, TEXT("Issue27940Fixture"));
	bool bCompressionConfigured = false;
	float FrameCacheLength = 0.0f;
	if (GConfig != nullptr)
	{
		GConfig->GetBool(TEXT("VisualLogger"), TEXT("UseCompression"), bCompressionConfigured, GEngineIni);
		GConfig->GetFloat(TEXT("VisualLogger"), TEXT("FrameCacheLenght"), FrameCacheLength, GEngineIni);
	}

	const FString OutDir = FCUCommandletHelpers::ParseOutputDirectory(Params);
	IFileManager::Get().MakeDirectory(*OutDir, /*Tree=*/true);
	if (!IFileManager::Get().DirectoryExists(*OutDir))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Cannot create OutputDir %s"), *OutDir);
		return 1;
	}

#if !ENABLE_VISUAL_LOG
	UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("ENABLE_VISUAL_LOG is off; cannot write .vlog"));
	return 1;
#else
	UWorld* EditorWorld = (GEditor != nullptr) ? GEditor->GetEditorWorldContext().World() : nullptr;
	UObject* Owner = EditorWorld ? static_cast<UObject*>(EditorWorld) : GetTransientPackage();
	if (!UCUAiRuntimeTraceLibrary::StartBtFileRecording())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("SetIsRecordingToFile failed"));
		return 1;
	}

	UE_VLOG(Owner, LogBehaviorTree, Log, TEXT("NarshaMCP #26815 bootstrap line"));
	UE_VLOG(Owner, LogBehaviorTree, Warning, TEXT("Abort task: BTTask_Wait (bootstrap marker, not PIE decorator abort)"));

	// File device only serializes on Tick/Flush. Commandlets do not pump the
	// core ticker unless we do it ourselves — otherwise Stop deletes an empty file.
	const int32 CaptureTicks = bIssue27940Fixture ? 12 : 8;
	const float CaptureDelta = bIssue27940Fixture ? 0.15f : 0.05f;
	for (int32 TickIdx = 0; TickIdx < CaptureTicks; ++TickIdx)
	{
		if (bIssue27940Fixture)
		{
			UE_VLOG(Owner, LogBehaviorTree, Log,
				TEXT("Blackboard key TargetActor changed by BTService_FindTarget (fixture frame %d)"), TickIdx);
			UE_VLOG(Owner, LogBehaviorTree, Warning,
				TEXT("Decorator BTDecorator_Blackboard aborted child BTTask_Wait because key TargetActor changed (fixture frame %d)"),
				TickIdx);
		}
		FTSTicker::GetCoreTicker().Tick(CaptureDelta);
		if (bIssue27940Fixture)
		{
			FPlatformProcess::Sleep(CaptureDelta);
		}
	}

	if (!UCUAiRuntimeTraceLibrary::StopBtFileRecording())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("StopBtFileRecording failed"));
		return 1;
	}

	const FString SrcVlog = UCUAiRuntimeTraceLibrary::GetLatestVlogPath();
	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetStringField(TEXT("issue"), TEXT("26815"));
	Report->SetStringField(TEXT("engine_api"), TEXT("FVisualLogger::SetIsRecordingToFile"));
	Report->SetStringField(TEXT("src_vlog"), SrcVlog);
	Report->SetBoolField(TEXT("exercises_pie_decorator_abort"), false);
	Report->SetBoolField(TEXT("issue_27940_fixture_mode"), bIssue27940Fixture);
	Report->SetBoolField(TEXT("compression_configured"), bCompressionConfigured);
	Report->SetNumberField(TEXT("frame_cache_length"), FrameCacheLength);

	bool bCopied = false;
	FString DestVlog;
	if (!SrcVlog.IsEmpty() && IFileManager::Get().FileExists(*SrcVlog))
	{
		DestVlog = OutDir / TEXT("bootstrap.vlog");
		bCopied = IFileManager::Get().Copy(*DestVlog, *SrcVlog) == COPY_OK;
		Report->SetStringField(TEXT("dest_vlog"), DestVlog);
		Report->SetNumberField(TEXT("bytes"), IFileManager::Get().FileSize(*SrcVlog));
	}
	else
	{
		Report->SetStringField(TEXT("error"), TEXT("no vlog file after stop"));
	}
	Report->SetBoolField(TEXT("copied"), bCopied);
	if (bIssue27940Fixture && bCopied)
	{
		TArray<uint8> SerializedFrames;
		TArray<uint8> CompressedFrames;
		TArray<int32> FrameOffsets;
		const bool bLoadedFrames = FFileHelper::LoadFileToArray(SerializedFrames, *SrcVlog);
		constexpr uint8 FrameMagic[] = {0xFB, 0xBB, 0xBF, 0xBF};
		if (bLoadedFrames)
		{
			for (int32 Offset = 0; Offset <= SerializedFrames.Num() - UE_ARRAY_COUNT(FrameMagic); ++Offset)
			{
				if (FMemory::Memcmp(SerializedFrames.GetData() + Offset,
					FrameMagic, UE_ARRAY_COUNT(FrameMagic)) == 0)
				{
					FrameOffsets.Add(Offset);
				}
			}
		}
		FrameOffsets.Add(SerializedFrames.Num());
		bool bCompressionSucceeded = FrameOffsets.Num() > 2 && FrameOffsets[0] == 0;
		for (int32 FrameIndex = 0; bCompressionSucceeded && FrameIndex + 1 < FrameOffsets.Num(); ++FrameIndex)
		{
			const int32 FrameSize = FrameOffsets[FrameIndex + 1] - FrameOffsets[FrameIndex];
			if (FrameSize <= 0)
			{
				bCompressionSucceeded = false;
				break;
			}
			TArray<uint8> Envelope;
			FArchiveSaveCompressedProxy Compressor(Envelope, NAME_Zlib);
			Compressor.Serialize(SerializedFrames.GetData() + FrameOffsets[FrameIndex], FrameSize);
			Compressor.Flush();
			bCompressionSucceeded = !Compressor.GetError() && !Envelope.IsEmpty();
			if (bCompressionSucceeded)
			{
				CompressedFrames.Append(Envelope);
			}
		}
		const FString CompressedVlog = OutDir / TEXT("bootstrap.compressed.vlog");
		const bool bCompressedFramesWritten = bCompressionSucceeded
			&& FFileHelper::SaveArrayToFile(CompressedFrames, *CompressedVlog);
		Report->SetStringField(TEXT("compressed_frame_cache_path"), CompressedVlog);
		Report->SetStringField(TEXT("compressed_frame_cache_source"),
			TEXT("VisualLogger device frame blocks, one FArchiveSaveCompressedProxy(NAME_Zlib) envelope per block"));
		Report->SetNumberField(TEXT("compressed_frame_cache_envelope_count"),
			FMath::Max(0, FrameOffsets.Num() - 1));
		Report->SetNumberField(TEXT("compressed_frame_cache_source_bytes"), SerializedFrames.Num());
		Report->SetNumberField(TEXT("compressed_frame_cache_bytes"), CompressedFrames.Num());
		Report->SetBoolField(TEXT("compressed_frame_cache_written"), bCompressedFramesWritten);
	}
	// ST fixture: commandlet cannot call IStateTreeModule::StartTraces
	// (returns false when IsRunningCommandlet). File-trace the channel
	// directly; empty events + channel_absent is the Wave A contract.
	const FString DestUtrace = OutDir / TEXT("bootstrap.utrace");
	bool bUtraceCopied = false;
	{
		FTraceAuxiliary::FOptions TraceOpts;
		TraceOpts.bTruncateFile = true;
		TraceOpts.bExcludeTail = true;
		const bool bTraceStarted = FTraceAuxiliary::Start(
			FTraceAuxiliary::EConnectionType::File,
			*DestUtrace,
			TEXT("statetreedebug"),
			&TraceOpts);
		for (int32 TickIdx = 0; TickIdx < 8; ++TickIdx)
		{
			FTSTicker::GetCoreTicker().Tick(0.05f);
		}
		FTraceAuxiliary::Stop();
		for (int32 FlushIdx = 0; FlushIdx < 50; ++FlushIdx)
		{
			if (IFileManager::Get().FileExists(*DestUtrace)
				&& IFileManager::Get().FileSize(*DestUtrace) > 0)
			{
				break;
			}
			FPlatformProcess::Sleep(0.05f);
		}
		bUtraceCopied = IFileManager::Get().FileExists(*DestUtrace)
			&& IFileManager::Get().FileSize(*DestUtrace) > 0;
		Report->SetBoolField(TEXT("utrace_started"), bTraceStarted);
		Report->SetStringField(TEXT("dest_utrace"), DestUtrace);
		Report->SetNumberField(TEXT("utrace_bytes"),
			bUtraceCopied ? IFileManager::Get().FileSize(*DestUtrace) : 0);
		Report->SetBoolField(TEXT("utrace_copied"), bUtraceCopied);
		Report->SetBoolField(TEXT("exercises_ticking_statetree"), false);
	}

	Report->SetBoolField(TEXT("success"), bCopied);

	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Report, Writer);
	const FString ReportPath = OutDir / TEXT("ai_runtime_trace_capture.json");
	FFileHelper::SaveStringToFile(Json, *ReportPath);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Report %s copied=%s src=%s"),
		*ReportPath, bCopied ? TEXT("true") : TEXT("false"), *SrcVlog);
	FCUCommandletHelpers::EmitResultMarker(Report);
	return bCopied ? 0 : 1;
#endif
}
