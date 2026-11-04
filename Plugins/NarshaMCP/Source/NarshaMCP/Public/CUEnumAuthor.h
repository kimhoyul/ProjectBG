// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #20524: Domain C++ authoring for UserDefinedEnum.

#pragma once

#include "CoreMinimal.h"

struct FCUEnumAuthorParams
{
	FString Operation;
	FString EnumPackagePath;
	FString SpecJson;
};

struct FCUEnumEnumeratorInfo
{
	FString Name;
	int64 Value = 0;
};

struct FCUEnumAuthorResult
{
	bool bSuccess = false;
	int32 EnumeratorCount = 0;
	TArray<FCUEnumEnumeratorInfo> Enumerators;
	FString SavedFilePath;
	FString CppType;
	FString Error;
};

class NARSHAMCP_API FCUEnumAuthor
{
public:
	static bool Execute(const FCUEnumAuthorParams& In, FCUEnumAuthorResult& Out);
};
