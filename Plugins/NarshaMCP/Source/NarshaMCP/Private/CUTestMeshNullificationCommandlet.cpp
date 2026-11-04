// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUTestMeshNullificationCommandlet.h"
#include "CUAssetBinaryModifier.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
// Issue #11472 Gap 1: UE 5.8 deprecated bool overload of ForEachObjectWithPackage in favor of EGetObjectsFlags.
#include "Misc/EngineVersionComparison.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPTestUtil, Log, All);

UCUTestMeshNullificationCommandlet::UCUTestMeshNullificationCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowProgress = true;
	ShowErrorCount = true;
}

int32 UCUTestMeshNullificationCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("========================================"));
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Starting test"));
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("========================================"));

	// Step 1: Parse Blueprint path from command line
	FString BlueprintPath;
	if (!ParseBlueprintPath(Params, BlueprintPath))
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Failed to parse Blueprint path from parameters: %s"), *Params);
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Usage: -run=TestMeshNullification -Blueprint=/Game/BP_ParseTest"));
		return 1; // Failure
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Target Blueprint: %s"), *BlueprintPath);

	// Step 2: Load Blueprint with mesh nullification
	int32 NullifiedCount = 0;
	UBlueprint* Blueprint = LoadBlueprintWithMeshNullification(BlueprintPath, NullifiedCount);

	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Failed to load Blueprint: %s"), *BlueprintPath);
		return 1; // Failure
	}

	// Step 3: Validate Blueprint
	ValidateBlueprint(Blueprint, NullifiedCount);

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("========================================"));
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Test PASSED"));
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("========================================"));

	return 0; // Success
}

bool UCUTestMeshNullificationCommandlet::ParseBlueprintPath(const FString& Params, FString& OutBlueprintPath)
{
	// Look for -Blueprint=/Game/AssetPath parameter
	FString BlueprintParam;
	if (!FParse::Value(*Params, TEXT("-Blueprint="), BlueprintParam))
	{
		return false;
	}

	// Remove quotes if present
	BlueprintParam = BlueprintParam.TrimQuotes();

	// Validate it's a package path
	if (BlueprintParam.IsEmpty() || !BlueprintParam.StartsWith(TEXT("/")))
	{
		return false;
	}

	OutBlueprintPath = BlueprintParam;
	return true;
}

UBlueprint* UCUTestMeshNullificationCommandlet::LoadBlueprintWithMeshNullification(const FString& BlueprintPath, int32& OutNullifiedCount)
{
	OutNullifiedCount = 0;

	// Step 1: Convert package path to file path
	FString FilePath = FPackageName::LongPackageNameToFilename(BlueprintPath, TEXT(".uasset"));

	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] File not found: %s"), *FilePath);
		return nullptr;
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Loading file: %s"), *FilePath);

	// Step 2: Load raw binary data into memory
	TArray<uint8> AssetData;
	if (!FFileHelper::LoadFileToArray(AssetData, *FilePath))
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Failed to load file into memory: %s"), *FilePath);
		return nullptr;
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Loaded %d bytes into memory"), AssetData.Num());

	// Step 3: Nullify mesh references in memory
	OutNullifiedCount = FCUAssetBinaryModifier::NullifyMeshReferences(AssetData);

	if (OutNullifiedCount > 0)
	{
		UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Nullified %d mesh reference(s)"), OutNullifiedCount);
	}
	else
	{
		UE_LOG(LogNarshaMCPTestUtil, Warning, TEXT("[TestMeshNullification] No mesh references found"));
	}

	// Step 4: Save modified data to temporary file
	FString TempDir = FPaths::ProjectSavedDir() / TEXT("Temp");
	FString TempFileName = FString::Printf(TEXT("TestMeshNull_%s.uasset"), *FGuid::NewGuid().ToString());
	FString TempPath = TempDir / TempFileName;

	// Ensure temp directory exists
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*TempDir))
	{
		if (!PlatformFile.CreateDirectory(*TempDir))
		{
			UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Failed to create temp directory: %s"), *TempDir);
			return nullptr;
		}
	}

	if (!FFileHelper::SaveArrayToFile(AssetData, *TempPath))
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Failed to save temp file: %s"), *TempPath);
		return nullptr;
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Saved modified data to temp file: %s"), *TempPath);

	// Step 5: Load Blueprint from temporary file
	FString TempPackageName = FPackageName::FilenameToLongPackageName(TempPath);
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Loading package: %s"), *TempPackageName);

	UPackage* Package = LoadPackage(nullptr, *TempPackageName, LOAD_None);
	if (!Package)
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] Failed to load package from temp file"));

		// Cleanup temp file
		PlatformFile.DeleteFile(*TempPath);
		return nullptr;
	}

	// Find Blueprint in package
	UBlueprint* Blueprint = nullptr;
	// Issue #11472 Gap 1: UE 5.8 deprecated bool overload in favor of EGetObjectsFlags.
	auto VisitObject = [&Blueprint](UObject* Object) -> bool
	{
		if (UBlueprint* BP = Cast<UBlueprint>(Object))
		{
			Blueprint = BP;
			return false; // Stop iteration
		}
		return true; // Continue
	};
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
	ForEachObjectWithPackage(Package, VisitObject, EGetObjectsFlags::None);
#else
	ForEachObjectWithPackage(Package, VisitObject, false);
#endif

	// Step 6: Cleanup temp file
	PlatformFile.DeleteFile(*TempPath);
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Temp file deleted"));

	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPTestUtil, Error, TEXT("[TestMeshNullification] No Blueprint found in package"));
		return nullptr;
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Blueprint loaded successfully"));
	return Blueprint;
}

void UCUTestMeshNullificationCommandlet::ValidateBlueprint(UBlueprint* Blueprint, int32 NullifiedCount)
{
	if (!Blueprint)
	{
		return;
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT(""));
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification] Validation Results:"));
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification]   Blueprint Name: %s"), *Blueprint->GetName());

	if (Blueprint->GeneratedClass)
	{
		UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification]   Generated Class: %s"), *Blueprint->GeneratedClass->GetName());

		if (Blueprint->ParentClass)
		{
			UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification]   Parent Class: %s"), *Blueprint->ParentClass->GetName());
		}
	}

	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification]   Mesh References Nullified: %d"), NullifiedCount);
	UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification]   Blueprint Type: %s"),
		Blueprint->BlueprintType == BPTYPE_Normal ? TEXT("Normal") :
		Blueprint->BlueprintType == BPTYPE_MacroLibrary ? TEXT("Macro Library") :
		Blueprint->BlueprintType == BPTYPE_Interface ? TEXT("Interface") :
		Blueprint->BlueprintType == BPTYPE_LevelScript ? TEXT("Level Script") :
		Blueprint->BlueprintType == BPTYPE_FunctionLibrary ? TEXT("Function Library") :
		TEXT("Unknown"));

	// Check if Blueprint is valid for metadata extraction
	if (Blueprint->GeneratedClass && Blueprint->ParentClass)
	{
		UE_LOG(LogNarshaMCPTestUtil, Display, TEXT("[TestMeshNullification]   Status: Ready for metadata extraction"));
	}
	else
	{
		UE_LOG(LogNarshaMCPTestUtil, Warning, TEXT("[TestMeshNullification]   Status: Missing class information"));
	}
}
