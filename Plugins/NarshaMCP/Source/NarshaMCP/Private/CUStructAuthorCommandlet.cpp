// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #18415: Domain C++ commandlet for reflection-blocked editor-only UserDefinedStruct edits.

#include "CUStructAuthorCommandlet.h"
#include "CUStructAuthor.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UCUStructAuthorCommandlet::UCUStructAuthorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCUStructAuthorCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("=== StructAuthor Commandlet (Issue #18415) ==="));

	// --- Required: -Operation ---
	FString Operation;
	if (!FParse::Value(*Params, TEXT("Operation="), Operation) || Operation.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error,
			TEXT("Missing required parameter: -Operation=create_struct|add_member|modify_member|remove_member|rename_member|reorder_members|set_member_metadata|rename_struct|duplicate_struct|delete_struct|get_structure"));
		return 1;
	}

	// --- Required: -StructPath ---
	FString StructPath;
	if (!FParse::Value(*Params, TEXT("StructPath="), StructPath) || StructPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Missing required parameter: -StructPath=/Game/Path/To/S_Name"));
		return 1;
	}

	// --- Optional: -SpecFile (JSON spec; absent → empty, e.g. get_structure) ---
	FString SpecJson;
	FString SpecFile;
	if (FParse::Value(*Params, TEXT("SpecFile="), SpecFile) && !SpecFile.IsEmpty())
	{
		if (!FFileHelper::LoadFileToString(SpecJson, *SpecFile))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to read -SpecFile: %s"), *SpecFile);
			return 1;
		}
	}

	// --- Optional: -OutputDir (default <Project>/Intermediate/NarshaMCP) ---
	FString OutputDir;
	if (!FParse::Value(*Params, TEXT("OutputDir="), OutputDir) || OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
	}
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	// --- Author ---
	FCUStructAuthorParams AuthorParams;
	AuthorParams.Operation = Operation;
	AuthorParams.StructPackagePath = StructPath;
	AuthorParams.SpecJson = SpecJson;

	FCUStructAuthorResult Result;
	const bool bOk = FCUStructAuthor::Execute(AuthorParams, Result);

	// --- Emit JSON report for machine verification ---
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Result.bSuccess);
	Root->SetStringField(TEXT("operation"), Operation);
	Root->SetStringField(TEXT("struct_path"), StructPath);
	Root->SetNumberField(TEXT("field_count"), Result.FieldCount);
	Root->SetStringField(TEXT("saved_file_path"), Result.SavedFilePath);
	// #19714: asset-lifecycle fields (empty/[] outside rename/duplicate/delete flows).
	Root->SetStringField(TEXT("new_path"), Result.NewPath);
	TArray<TSharedPtr<FJsonValue>> ReferencersJson;
	for (const FString& Referencer : Result.Referencers)
	{
		ReferencersJson.Add(MakeShared<FJsonValueString>(Referencer));
	}
	Root->SetArrayField(TEXT("referencers"), ReferencersJson);
	Root->SetStringField(TEXT("error"), Result.Error);

	TArray<TSharedPtr<FJsonValue>> FieldsJson;
	for (const FCUStructFieldInfo& F : Result.Fields)
	{
		const TSharedRef<FJsonObject> FO = MakeShared<FJsonObject>();
		FO->SetStringField(TEXT("name"), F.Name);
		FO->SetStringField(TEXT("type"), F.Type);
		FO->SetStringField(TEXT("default"), F.DefaultValue);
		FieldsJson.Add(MakeShared<FJsonValueObject>(FO));
	}
	Root->SetArrayField(TEXT("fields"), FieldsJson);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ReportPath = OutputDir / TEXT("struct_author.json");
	if (FFileHelper::SaveStringToFile(JsonText, *ReportPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Report written: %s"), *ReportPath);
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("Failed to write report: %s"), *ReportPath);
	}

	// Authoritative result marker for build_pipeline (shared FCUCommandletHelpers::EmitResultMarker).
	FCUCommandletHelpers::EmitResultMarker(Root);

	if (bOk)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("✓ %s on %s (%d field(s))"),
			*Operation, *StructPath, Result.FieldCount);
		return 0;
	}

	UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("✗ StructAuthor failed: %s"), *Result.Error);
	return 1;
}
