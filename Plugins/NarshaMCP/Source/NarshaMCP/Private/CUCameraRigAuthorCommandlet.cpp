// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16577: Domain C++ commandlet for reflection-blocked UPROPERTY areas (spike).

#include "CUCameraRigAuthorCommandlet.h"
#include "CUCameraRigAuthor.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UCUCameraRigAuthorCommandlet::UCUCameraRigAuthorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCUCameraRigAuthorCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("=== CameraRigAuthor Commandlet (Issue #16577) ==="));

	// --- Required: -RigPath ---
	FString RigPath;
	if (!FParse::Value(*Params, TEXT("RigPath="), RigPath) || RigPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error,
			TEXT("Missing required parameter: -RigPath=/Game/Path/To/Rig"));
		return 1;
	}

	// --- Optional: -OffsetX/Y/Z (default 0) ---
	float OffsetX = 0.f, OffsetY = 0.f, OffsetZ = 0.f;
	FParse::Value(*Params, TEXT("OffsetX="), OffsetX);
	FParse::Value(*Params, TEXT("OffsetY="), OffsetY);
	FParse::Value(*Params, TEXT("OffsetZ="), OffsetZ);

	// --- Optional: -OutputDir (default <Project>/Intermediate/NarshaMCP) ---
	FString OutputDir;
	if (!FParse::Value(*Params, TEXT("OutputDir="), OutputDir) || OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
	}
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	// --- Author the rig ---
	FCUCameraRigAuthorParams AuthorParams;
	AuthorParams.RigPackagePath = RigPath;
	AuthorParams.TranslationOffset = FVector(OffsetX, OffsetY, OffsetZ);

	FCUCameraRigAuthorResult Result;
	const bool bOk = FCUCameraRigAuthor::Execute(AuthorParams, Result);

	// --- Emit JSON report for machine verification ---
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Result.bSuccess);
	Root->SetStringField(TEXT("rig_path"), RigPath);
	Root->SetStringField(TEXT("root_node_class"), Result.RootNodeClass);
	Root->SetStringField(TEXT("build_status"), Result.BuildStatus);
	Root->SetStringField(TEXT("saved_file_path"), Result.SavedFilePath);
	Root->SetStringField(TEXT("error"), Result.Error);
	if (!Result.ErrorCode.IsEmpty())
	{
		Root->SetStringField(TEXT("error_code"), Result.ErrorCode);
		Root->SetStringField(TEXT("hint"), Result.Hint);
	}

	const TSharedRef<FJsonObject> Offset = MakeShared<FJsonObject>();
	Offset->SetNumberField(TEXT("x"), OffsetX);
	Offset->SetNumberField(TEXT("y"), OffsetY);
	Offset->SetNumberField(TEXT("z"), OffsetZ);
	Root->SetObjectField(TEXT("offset"), Offset);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ReportPath = OutputDir / TEXT("gameplaycamera_rig_author.json");
	if (FFileHelper::SaveStringToFile(JsonText, *ReportPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Report written: %s"), *ReportPath);
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("Failed to write report: %s"), *ReportPath);
	}

	// #16915: authoritative result marker for build_pipeline (shared FCUCommandletHelpers::EmitResultMarker).
	FCUCommandletHelpers::EmitResultMarker(Root);

	if (bOk)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display,
			TEXT("✓ Authored rig %s (root=%s, build=%s)"),
			*RigPath, *Result.RootNodeClass, *Result.BuildStatus);
		return 0;
	}

	UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("✗ Authoring failed: %s"), *Result.Error);
	return 1;
}
