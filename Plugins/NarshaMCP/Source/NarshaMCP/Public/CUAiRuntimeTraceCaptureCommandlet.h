// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26815 (AI-01): Headless Editor-saved .vlog fixture capture.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUAiRuntimeTraceCaptureCommandlet.generated.h"

/**
 * Writes a real Visual Logger .vlog via SetIsRecordingToFile (not VISLOG record)
 * plus a JSON sidecar the Rust parser must match.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUAiRuntimeTraceCapture
 *     [-OutputDir=<dir>] -NullRHI -unattended -nosplash
 *
 * Bootstrap fixture only: logs known UE_VLOG lines. Does NOT exercise live
 * BehaviorTree decorator abort (that remains a PIE spike).
 */
UCLASS()
class UCUAiRuntimeTraceCaptureCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUAiRuntimeTraceCaptureCommandlet();
	virtual int32 Main(const FString& Params) override;
};
