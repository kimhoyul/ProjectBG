// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #20524: Domain C++ authoring for DataTable.

#pragma once

#include "CoreMinimal.h"

struct FCUDataTableAuthorParams
{
	FString Operation; // create_datatable | add_row | remove_row | get_datatable
	FString TablePackagePath;
	FString SpecJson;
};

struct FCUDataTableRowInfo
{
	FString Name;
};

struct FCUDataTableAuthorResult
{
	bool bSuccess = false;
	int32 RowCount = 0;
	TArray<FCUDataTableRowInfo> Rows;
	FString RowStructPath;
	FString SavedFilePath;
	FString Error;
};

class NARSHAMCP_API FCUDataTableAuthor
{
public:
	static bool Execute(const FCUDataTableAuthorParams& In, FCUDataTableAuthorResult& Out);
};
