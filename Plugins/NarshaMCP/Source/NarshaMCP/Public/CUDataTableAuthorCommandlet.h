// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #20524

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUDataTableAuthorCommandlet.generated.h"

UCLASS()
class UCUDataTableAuthorCommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	UCUDataTableAuthorCommandlet();
	virtual int32 Main(const FString& Params) override;
};
