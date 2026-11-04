// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26808 ST-06: headless StateTree spec author commandlet.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUStateTreeAuthorCommandlet.generated.h"

/**
 * Headless authoring commandlet for a StateTree from spec JSON.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUStateTreeAuthor
 *     -STPath=/Game/NarshaMCP/ST_Authored
 *     [-SpecFile=C:/tmp/spec.json]
 *     [-DryRun]
 *     [-OutputDir="D:/Custom/Report/Dir"]
 *     -NullRHI -unattended -nosplash
 */
UCLASS()
class UCUStateTreeAuthorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUStateTreeAuthorCommandlet();
	virtual int32 Main(const FString& Params) override;
};
