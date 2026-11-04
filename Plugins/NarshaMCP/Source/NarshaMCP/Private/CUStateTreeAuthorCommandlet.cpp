// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26808 ST-06: headless StateTree spec author commandlet.

#include "CUStateTreeAuthorCommandlet.h"
#include "CUStateTreeAuthor.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

UCUStateTreeAuthorCommandlet::UCUStateTreeAuthorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCUStateTreeAuthorCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("=== StateTreeAuthor Commandlet (Issue #26808) ==="));

	FString VerifyPath;
	if (FParse::Value(*Params, TEXT("VerifyPath="), VerifyPath) && !VerifyPath.IsEmpty())
	{
		FString OutDirV;
		if (!FParse::Value(*Params, TEXT("OutputDir="), OutDirV) || OutDirV.IsEmpty())
		{
			OutDirV = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
		}
		IFileManager::Get().MakeDirectory(*OutDirV, /*Tree=*/true);
		FCUStateTreeAuthorVerifyResult V;
		const bool bOk = FCUStateTreeAuthor::Verify(VerifyPath, V);
		const TSharedRef<FJsonObject> VR = MakeShared<FJsonObject>();
		VR->SetBoolField(TEXT("success"), bOk);
		VR->SetStringField(TEXT("verify_path"), VerifyPath);
		VR->SetBoolField(TEXT("loaded"), V.bLoaded);
		VR->SetBoolField(TEXT("has_editor_data"), V.bHasEditorData);
		VR->SetNumberField(TEXT("state_count"), V.StateCount);
		VR->SetStringField(TEXT("error"), V.Error);
		VR->SetStringField(TEXT("error_code"), V.ErrorCode);
		VR->SetStringField(TEXT("hint"), V.Hint);
		FString VJson;
		const TSharedRef<TJsonWriter<>> VWriter = TJsonWriterFactory<>::Create(&VJson);
		FJsonSerializer::Serialize(VR, VWriter);
		const FString VReportPath = OutDirV / TEXT("statetree_verify.json");
		FFileHelper::SaveStringToFile(VJson, *VReportPath);
		FCUCommandletHelpers::EmitResultMarker(VR);
		return bOk ? 0 : 1;
	}

	FString STPath;
	if (!FParse::Value(*Params, TEXT("STPath="), STPath) || STPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Missing required parameter: -STPath=/Game/Path/To/ST"));
		return 1;
	}

	FString OutputDir;
	if (!FParse::Value(*Params, TEXT("OutputDir="), OutputDir) || OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
	}
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	FCUStateTreeAuthorParams AuthorParams;
	AuthorParams.STPackagePath = STPath;
	AuthorParams.bDryRun = FParse::Param(*Params, TEXT("DryRun"));
	FString SpecFile;
	if (FParse::Value(*Params, TEXT("SpecFile="), SpecFile) && !SpecFile.IsEmpty())
	{
		if (!FFileHelper::LoadFileToString(AuthorParams.SpecJson, *SpecFile))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to read -SpecFile=%s"), *SpecFile);
			return 1;
		}
	}

	FCUStateTreeAuthorResult Result;
	const bool bOk = FCUStateTreeAuthor::Execute(AuthorParams, Result);

	const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("success"), bOk && Result.bSuccess);
	Report->SetBoolField(TEXT("dry_run"), Result.bDryRun);
	Report->SetBoolField(TEXT("created"), Result.bCreated);
	Report->SetBoolField(TEXT("compiled"), Result.bCompiled);
	Report->SetBoolField(TEXT("verified"), Result.bVerified);
	Report->SetStringField(TEXT("st_path"), STPath);
	Report->SetStringField(TEXT("saved_file"), Result.SavedFilePath);
	Report->SetStringField(TEXT("error_code"), Result.ErrorCode);
	Report->SetStringField(TEXT("hint"), Result.Hint);
	Report->SetNumberField(TEXT("planned_states"), Result.PlannedStates);
	Report->SetNumberField(TEXT("planned_transitions"), Result.PlannedTransitions);
	Report->SetNumberField(TEXT("planned_tasks"), Result.PlannedTasks);
	Report->SetNumberField(TEXT("planned_parameters"), Result.PlannedParameters);
	Report->SetNumberField(TEXT("planned_conditions"), Result.PlannedConditions);
	Report->SetNumberField(TEXT("planned_evaluators"), Result.PlannedEvaluators);
	Report->SetNumberField(TEXT("planned_considerations"), Result.PlannedConsiderations);
	Report->SetNumberField(TEXT("planned_bindings"), Result.PlannedBindings);
	Report->SetNumberField(TEXT("applied_ok"), Result.AppliedOk);
	Report->SetNumberField(TEXT("applied_fail"), Result.AppliedFail);
	Report->SetStringField(TEXT("error"), Result.Error);
	TArray<TSharedPtr<FJsonValue>> DiagArr;
	for (const FCUStateTreeSpecDiagnostic& D : Result.Diagnostics)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("path"), D.Path);
		Obj->SetStringField(TEXT("code"), D.Code);
		Obj->SetStringField(TEXT("value"), D.Value);
		Obj->SetStringField(TEXT("message"), D.Message);
		DiagArr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	Report->SetArrayField(TEXT("diagnostics"), DiagArr);
	TArray<TSharedPtr<FJsonValue>> AddedArr;
	for (const FString& S : Result.Added) { AddedArr.Add(MakeShared<FJsonValueString>(S)); }
	Report->SetArrayField(TEXT("added"), AddedArr);
	TArray<TSharedPtr<FJsonValue>> FailedArr;
	for (const FString& S : Result.Failed) { FailedArr.Add(MakeShared<FJsonValueString>(S)); }
	Report->SetArrayField(TEXT("failed"), FailedArr);

	FString ReportJson;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ReportJson);
	FJsonSerializer::Serialize(Report, Writer);
	const FString ReportPath = OutputDir / TEXT("statetree_author.json");
	FFileHelper::SaveStringToFile(ReportJson, *ReportPath);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Author report written: %s"), *ReportPath);
	FCUCommandletHelpers::EmitResultMarker(Report);
	return (bOk && Result.bSuccess) ? 0 : 1;
}
