// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26815 (AI-01): Editor-callable Visual Logger file recording.
// Do NOT use the `VISLOG record` console command from the Editor — that path
// calls SetIsRecording(true) only and never writes a .vlog (GIsEditor branch).

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CUAiRuntimeTraceLibrary.generated.h"

/**
 * Remote-Control / Python-callable Visual Logger file recording.
 *
 * Engine GT: FVisualLogger::SetIsRecordingToFile (VisualLogger.h).
 * Output lands under FPaths::ProjectLogDir() via FVisualLoggerBinaryFileDevice.
 */
UCLASS()
class NARSHAMCP_API UCUAiRuntimeTraceLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|AIRuntime")
	static bool StartBtFileRecording();

	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|AIRuntime")
	static bool StopBtFileRecording();

	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|AIRuntime")
	static bool DiscardBtFileRecording();

	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|AIRuntime")
	static bool IsBtFileRecording();

	/** Newest .vlog (or VTEMP_*) under ProjectLogDir, or empty. */
	UFUNCTION(BlueprintCallable, Category = "NarshaMCP|AIRuntime")
	static FString GetLatestVlogPath();
};
