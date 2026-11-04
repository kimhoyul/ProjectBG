// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27039: tiny UObject graph for CompareActorState::ResolveDotPath unit tests.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CUDotPathProbe.generated.h"

UCLASS()
class UCUDotPathProbe : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	float Leaf = 42.0f;

	UPROPERTY()
	TObjectPtr<UCUDotPathProbe> Child;

	UPROPERTY()
	TArray<TObjectPtr<UCUDotPathProbe>> Children;
};
