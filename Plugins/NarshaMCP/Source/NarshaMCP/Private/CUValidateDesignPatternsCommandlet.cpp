// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUValidateDesignPatternsCommandlet.h"
#include "CUCommandletHelpers.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "Engine/Blueprint.h"

// For Blueprint asset path lookup
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPValidation, Log, All);

UCUValidateDesignPatternsCommandlet::UCUValidateDesignPatternsCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

int32 UCUValidateDesignPatternsCommandlet::RunExtraction(const FString& OutputDir, IAssetRegistry& Registry)
{
	// Delegate to static function (for batch commandlet support)
	return RunValidation(OutputDir, Registry);
}

int32 UCUValidateDesignPatternsCommandlet::RunValidation(const FString& OutputDir, IAssetRegistry& AssetRegistry)
{
	UE_LOG(LogNarshaMCPValidation, Display, TEXT("[ValidatePatterns] Running validation..."));

	// PROFILING: Design pattern validation
	FCUCommandletHelpers::BeginProfileSection(TEXT("ValidatePatterns.Validation"));

	// Define forbidden ancestors for CallInEditor pattern
	TArray<FString> ForbiddenAncestors;
	ForbiddenAncestors.Add(TEXT("UEditorUtilityObject"));
	ForbiddenAncestors.Add(TEXT("UAssetActionUtility"));
	ForbiddenAncestors.Add(TEXT("UEditorUtilityWidget"));

	// Collect all violations
	TArray<TSharedPtr<FJsonValue>> ViolationsArray;
	int32 ClassesChecked = 0;
	int32 ViolationsFound = 0;

	// Phase 2 Optimization: Use AssetRegistry to filter Blueprint assets only
	// This avoids iterating through all C++ classes in memory (TObjectIterator)
	// AssetRegistry is provided as parameter (shared in batch mode)

	// Filter for Blueprint assets only (project content)
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.PackagePaths.Add("/Game");  // Project content only (exclude engine)

	TArray<FAssetData> BlueprintAssets;
	AssetRegistry.GetAssets(Filter, BlueprintAssets);

	UE_LOG(LogNarshaMCPValidation, Display, TEXT("[NarshaMCP] Found %d Blueprint assets to check"), BlueprintAssets.Num());

	// Iterate Blueprint assets only (much faster than TObjectIterator)
	for (const FAssetData& AssetData : BlueprintAssets)
	{
		// Load Blueprint asset
		UObject* Asset = AssetData.GetAsset();
		if (!Asset)
		{
			continue;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (!Blueprint || !Blueprint->GeneratedClass)
		{
			continue;
		}

		UClass* Class = Blueprint->GeneratedClass;

		// Skip non-Blueprint classes (should not happen with AssetRegistry filter)
		if (!Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
		{
			continue;
		}

		ClassesChecked++;

		// Check if class inherits from forbidden ancestor
		FString ForbiddenAncestor;
		if (!InheritsFromForbiddenAncestor(Class, ForbiddenAncestors, ForbiddenAncestor))
		{
			continue; // No forbidden ancestor found
		}

		// Find all CallInEditor functions
		TArray<UFunction*> CallInEditorFunctions = FindCallInEditorFunctions(Class);

		// Create violation JSON for each CallInEditor function
		for (UFunction* Function : CallInEditorFunctions)
		{
			TSharedPtr<FJsonObject> ViolationJson = CreateViolationJson(Class, Function, ForbiddenAncestor);
			ViolationsArray.Add(MakeShareable(new FJsonValueObject(ViolationJson)));
			ViolationsFound++;
		}
	}

	UE_LOG(LogNarshaMCPValidation, Display, TEXT("[NarshaMCP] Checked %d Blueprint classes, found %d violations"), ClassesChecked, ViolationsFound);

	FCUCommandletHelpers::EndProfileSection(TEXT("ValidatePatterns.Validation"));

	// PROFILING: JSON save
	FCUCommandletHelpers::BeginProfileSection(TEXT("ValidatePatterns.JsonSave"));

	// Build final JSON output
	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject());
	RootObject->SetStringField(TEXT("version"), TEXT("4.7.0"));
	RootObject->SetStringField(TEXT("validation_method"), TEXT("commandlet_runtime"));
	RootObject->SetNumberField(TEXT("accuracy"), 0.99);
	RootObject->SetNumberField(TEXT("classes_checked"), ClassesChecked);
	RootObject->SetNumberField(TEXT("violations_found"), ViolationsFound);
	RootObject->SetArrayField(TEXT("violations"), ViolationsArray);

	// Write JSON to file
	FString JsonPath = OutputDir / TEXT("design_pattern_validation.json");
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *JsonPath))
	{
		FCUCommandletHelpers::EndProfileSection(TEXT("ValidatePatterns.JsonSave"));
		UE_LOG(LogNarshaMCPValidation, Display, TEXT("[NarshaMCP] Design pattern validation complete: %s"), *JsonPath);
		return 0; // Success
	}
	else
	{
		FCUCommandletHelpers::EndProfileSection(TEXT("ValidatePatterns.JsonSave"));
		UE_LOG(LogNarshaMCPValidation, Error, TEXT("[NarshaMCP] Failed to write output file: %s"), *JsonPath);
		return 1; // Error
	}
}

bool UCUValidateDesignPatternsCommandlet::InheritsFromForbiddenAncestor(
	UClass* Class,
	const TArray<FString>& ForbiddenAncestors,
	FString& OutForbiddenAncestor
)
{
	if (!Class)
	{
		return false;
	}

	// Traverse class hierarchy using GetSuperClass()
	UClass* CurrentClass = Class->GetSuperClass();

	while (CurrentClass)
	{
		FString ClassName = CurrentClass->GetName();

		// Check if this is a forbidden ancestor
		for (const FString& Forbidden : ForbiddenAncestors)
		{
			if (ClassName == Forbidden)
			{
				OutForbiddenAncestor = Forbidden;
				return true;
			}
		}

		// Move up the hierarchy
		CurrentClass = CurrentClass->GetSuperClass();
	}

	return false; // No forbidden ancestor found
}

TArray<UFunction*> UCUValidateDesignPatternsCommandlet::FindCallInEditorFunctions(UClass* Class)
{
	TArray<UFunction*> Result;

	if (!Class)
	{
		return Result;
	}

	// Iterate all functions in the class
	for (TFieldIterator<UFunction> FuncIt(Class); FuncIt; ++FuncIt)
	{
		UFunction* Function = *FuncIt;

		// Check if function has CallInEditor metadata
		if (Function->HasMetaData(TEXT("CallInEditor")))
		{
			Result.Add(Function);
		}
	}

	return Result;
}

FString UCUValidateDesignPatternsCommandlet::GetSourceFilePath(UClass* Class)
{
	if (!Class)
	{
		return TEXT("Unknown");
	}

	// For Blueprint classes, get the asset path
	if (Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		// Try to find the Blueprint asset in AssetRegistry
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		// Blueprint classes have "_C" suffix in their name (e.g., BP_MyBlueprint_C)
		FString ClassNameStr = Class->GetName();
		if (ClassNameStr.EndsWith(TEXT("_C")))
		{
			ClassNameStr.LeftChopInline(2); // Remove "_C" suffix
		}

		// Search for Blueprint asset
		FARFilter Filter;
		Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;

		TArray<FAssetData> AssetDataList;
		AssetRegistry.GetAssets(Filter, AssetDataList);

		for (const FAssetData& AssetData : AssetDataList)
		{
			if (AssetData.AssetName.ToString() == ClassNameStr)
			{
				return AssetData.GetObjectPathString(); // Return Blueprint asset path
			}
		}

		// Fallback: Return package name
		return Class->GetPackage()->GetName();
	}

	// For C++ classes, return package name (not used in this Commandlet, but for completeness)
	return Class->GetPackage()->GetName();
}

TSharedPtr<FJsonObject> UCUValidateDesignPatternsCommandlet::CreateViolationJson(
	UClass* Class,
	UFunction* Function,
	const FString& ForbiddenAncestor
)
{
	TSharedPtr<FJsonObject> Violation = MakeShareable(new FJsonObject());

	// Basic violation info
	Violation->SetStringField(TEXT("type"), TEXT("design_pattern_violation"));
	Violation->SetStringField(TEXT("pattern_id"), TEXT("callineditor_editorutility_conflict"));
	Violation->SetStringField(TEXT("severity"), TEXT("error"));
	Violation->SetNumberField(TEXT("confidence"), 0.99);

	// Class info
	Violation->SetStringField(TEXT("class_name"), Class->GetName());
	Violation->SetStringField(TEXT("forbidden_ancestor"), ForbiddenAncestor);
	Violation->SetStringField(TEXT("file"), GetSourceFilePath(Class));

	// Function info
	Violation->SetStringField(TEXT("function_name"), Function->GetName());

	// Message
	FString Message = FString::Printf(
		TEXT("CallInEditor on %s descendant: %s::%s() has CallInEditor but class inherits from %s. CallInEditor buttons don't appear in Details panel for %s-based classes."),
		*ForbiddenAncestor,
		*Class->GetName(),
		*Function->GetName(),
		*ForbiddenAncestor,
		*ForbiddenAncestor
	);
	Violation->SetStringField(TEXT("message"), Message);

	// Fix suggestion
	Violation->SetStringField(TEXT("fix"), TEXT("Use UActorActionUtility with SupportedClasses meta specifier instead"));

	// Additional context
	TSharedPtr<FJsonObject> Context = MakeShareable(new FJsonObject());
	Context->SetStringField(TEXT("validation_method"), TEXT("runtime_metadata"));
	Context->SetStringField(TEXT("class_flags"), FString::Printf(TEXT("0x%08X"), Class->GetClassFlags()));
	Context->SetBoolField(TEXT("is_blueprint"), Class->HasAnyClassFlags(CLASS_CompiledFromBlueprint));
	Violation->SetObjectField(TEXT("context"), Context);

	return Violation;
}
