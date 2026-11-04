// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #20524: Headless UserDefinedEnum authoring commandlet.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUEnumAuthorCommandlet.generated.h"

UCLASS()
class UCUEnumAuthorCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UCUEnumAuthorCommandlet();
	virtual int32 Main(const FString& Params) override;
};
