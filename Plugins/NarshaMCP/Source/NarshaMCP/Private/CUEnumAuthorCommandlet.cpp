// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #20524

#include "CUEnumAuthorCommandlet.h"
#include "CUEnumAuthor.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UCUEnumAuthorCommandlet::UCUEnumAuthorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCUEnumAuthorCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("=== EnumAuthor Commandlet (Issue #20524) ==="));

	FString Operation;
	if (!FParse::Value(*Params, TEXT("Operation="), Operation) || Operation.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Missing -Operation=create_enum|add_enumerator|remove_enumerator|get_enum"));
		return 1;
	}

	FString EnumPath;
	if (!FParse::Value(*Params, TEXT("EnumPath="), EnumPath) || EnumPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Missing -EnumPath=/Game/Path/E_Name"));
		return 1;
	}

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

	FString OutputDir;
	if (!FParse::Value(*Params, TEXT("OutputDir="), OutputDir) || OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
	}
	IFileManager::Get().MakeDirectory(*OutputDir, true);

	FCUEnumAuthorParams AuthorParams;
	AuthorParams.Operation = Operation;
	AuthorParams.EnumPackagePath = EnumPath;
	AuthorParams.SpecJson = SpecJson;

	FCUEnumAuthorResult Result;
	const bool bOk = FCUEnumAuthor::Execute(AuthorParams, Result);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Result.bSuccess);
	Root->SetStringField(TEXT("operation"), Operation);
	Root->SetStringField(TEXT("enum_path"), EnumPath);
	Root->SetNumberField(TEXT("enumerator_count"), Result.EnumeratorCount);
	Root->SetStringField(TEXT("cpp_type"), Result.CppType);
	Root->SetStringField(TEXT("saved_file_path"), Result.SavedFilePath);
	Root->SetStringField(TEXT("error"), Result.Error);

	TArray<TSharedPtr<FJsonValue>> EnumsJson;
	for (const FCUEnumEnumeratorInfo& E : Result.Enumerators)
	{
		const TSharedRef<FJsonObject> EO = MakeShared<FJsonObject>();
		EO->SetStringField(TEXT("name"), E.Name);
		EO->SetNumberField(TEXT("value"), static_cast<double>(E.Value));
		EnumsJson.Add(MakeShared<FJsonValueObject>(EO));
	}
	Root->SetArrayField(TEXT("enumerators"), EnumsJson);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ReportPath = OutputDir / TEXT("enum_author.json");
	FFileHelper::SaveStringToFile(JsonText, *ReportPath);
	FCUCommandletHelpers::EmitResultMarker(Root);

	if (bOk)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("OK %s on %s (%d enum(s))"), *Operation, *EnumPath, Result.EnumeratorCount);
		return 0;
	}
	UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("EnumAuthor failed: %s"), *Result.Error);
	return 1;
}
