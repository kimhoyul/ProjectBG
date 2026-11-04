// Copyright 2026 Next Stage Inc. All rights reserved. Issue #20524
#include "CUDataTableAuthorCommandlet.h"
#include "CUDataTableAuthor.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UCUDataTableAuthorCommandlet::UCUDataTableAuthorCommandlet(){ IsClient=false; IsEditor=true; IsServer=false; LogToConsole=true; }

int32 UCUDataTableAuthorCommandlet::Main(const FString& Params)
{
	FString Operation, TablePath, SpecJson, SpecFile, OutputDir;
	if (!FParse::Value(*Params, TEXT("Operation="), Operation) || Operation.IsEmpty()) { UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Missing -Operation")); return 1; }
	if (!FParse::Value(*Params, TEXT("TablePath="), TablePath) || TablePath.IsEmpty()) { UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Missing -TablePath")); return 1; }
	if (FParse::Value(*Params, TEXT("SpecFile="), SpecFile) && !SpecFile.IsEmpty()) { if (!FFileHelper::LoadFileToString(SpecJson, *SpecFile)) return 1; }
	if (!FParse::Value(*Params, TEXT("OutputDir="), OutputDir) || OutputDir.IsEmpty()) { OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP"); }
	IFileManager::Get().MakeDirectory(*OutputDir, true);
	FCUDataTableAuthorParams P; P.Operation=Operation; P.TablePackagePath=TablePath; P.SpecJson=SpecJson;
	FCUDataTableAuthorResult R; const bool bOk = FCUDataTableAuthor::Execute(P, R);
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), R.bSuccess);
	Root->SetStringField(TEXT("operation"), Operation);
	Root->SetStringField(TEXT("table_path"), TablePath);
	Root->SetNumberField(TEXT("row_count"), R.RowCount);
	Root->SetStringField(TEXT("row_struct_path"), R.RowStructPath);
	Root->SetStringField(TEXT("saved_file_path"), R.SavedFilePath);
	Root->SetStringField(TEXT("error"), R.Error);
	TArray<TSharedPtr<FJsonValue>> RowsJson;
	for (const FCUDataTableRowInfo& Row : R.Rows) { const TSharedRef<FJsonObject> RO=MakeShared<FJsonObject>(); RO->SetStringField(TEXT("name"), Row.Name); RowsJson.Add(MakeShared<FJsonValueObject>(RO)); }
	Root->SetArrayField(TEXT("rows"), RowsJson);
	FString JsonText; const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText); FJsonSerializer::Serialize(Root, Writer);
	FFileHelper::SaveStringToFile(JsonText, *(OutputDir / TEXT("datatable_author.json")));
	FCUCommandletHelpers::EmitResultMarker(Root);
	return bOk ? 0 : 1;
}
