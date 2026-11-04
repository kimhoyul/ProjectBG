// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #20524: UserDefinedEnum authoring via FEnumEditorUtils (UnrealEd, reflection-blocked).

#include "CUEnumAuthor.h"
#include "CULog.h"

#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	bool LoadEnumAuthorSpec(const FString& SpecJson, TSharedPtr<FJsonObject>& OutSpec, FString& OutError)
	{
		OutSpec.Reset();
		if (SpecJson.IsEmpty())
		{
			OutSpec = MakeShared<FJsonObject>();
			return true;
		}
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
		if (!FJsonSerializer::Deserialize(Reader, OutSpec) || !OutSpec.IsValid())
		{
			OutError = TEXT("SpecJson is not a JSON object");
			return false;
		}
		return true;
	}

	void FillEnumerators(UUserDefinedEnum* Enum, FCUEnumAuthorResult& Out)
	{
		Out.Enumerators.Reset();
		if (!Enum) { return; }
		Out.CppType = Enum->CppType;
		const int32 Num = Enum->NumEnums();
		// Last entry is often _MAX sentinel; include all named non-hidden.
		for (int32 i = 0; i < Num; ++i)
		{
			const FString Name = Enum->GetNameStringByIndex(i);
			if (Name.EndsWith(TEXT("_MAX"))) { continue; }
			FCUEnumEnumeratorInfo Info;
			Info.Name = Name;
			Info.Value = Enum->GetValueByIndex(i);
			Out.Enumerators.Add(Info);
		}
		Out.EnumeratorCount = Out.Enumerators.Num();
	}

	bool SaveEnumPackage(UUserDefinedEnum* Enum, FCUEnumAuthorResult& Out)
	{
		if (!Enum) { Out.Error = TEXT("null enum"); return false; }
		UPackage* Package = Enum->GetOutermost();
		Package->MarkPackageDirty();
		FString PackageFilename;
		if (!FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			Out.Error = TEXT("package path conversion failed");
			return false;
		}
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const bool bSaved = UPackage::SavePackage(Package, Enum, *PackageFilename, SaveArgs);
		if (!bSaved)
		{
			Out.Error = FString::Printf(TEXT("SavePackage failed for %s"), *PackageFilename);
			return false;
		}
		Out.SavedFilePath = PackageFilename;
		return true;
	}

	UUserDefinedEnum* LoadOrFindEnum(const FString& PackagePath, FString& OutError)
	{
		UUserDefinedEnum* Enum = LoadObject<UUserDefinedEnum>(nullptr, *PackagePath);
		if (!Enum)
		{
			OutError = FString::Printf(TEXT("Failed to load UserDefinedEnum %s"), *PackagePath);
		}
		return Enum;
	}
} // namespace

bool FCUEnumAuthor::Execute(const FCUEnumAuthorParams& In, FCUEnumAuthorResult& Out)
{
	Out = FCUEnumAuthorResult{};
	const FString Op = In.Operation.TrimStartAndEnd().ToLower();
	const FString PackagePath = In.EnumPackagePath.TrimStartAndEnd();
	if (PackagePath.IsEmpty())
	{
		Out.Error = TEXT("EnumPackagePath is empty");
		return false;
	}

	TSharedPtr<FJsonObject> Spec;
	if (!LoadEnumAuthorSpec(In.SpecJson, Spec, Out.Error))
	{
		return false;
	}

	if (Op == TEXT("create_enum"))
	{
		FString PackageName = PackagePath;
		FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
		if (AssetName.IsEmpty())
		{
			Out.Error = TEXT("could not derive asset name from EnumPackagePath");
			return false;
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			Out.Error = FString::Printf(TEXT("CreatePackage failed for %s"), *PackageName);
			return false;
		}
		Package->FullyLoad();
		// Issue #22834: CreateUserDefinedEnum returns UEnum* ??cast to UUserDefinedEnum.
		UEnum* CreatedEnum = FEnumEditorUtils::CreateUserDefinedEnum(
			Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(CreatedEnum);
		if (!Enum)
		{
			Out.Error = TEXT("CreateUserDefinedEnum returned null or non-UUserDefinedEnum");
			return false;
		}
		// Optional bulk enumerators
		const TArray<TSharedPtr<FJsonValue>>* Names = nullptr;
		if (Spec->TryGetArrayField(TEXT("enumerators"), Names) && Names)
		{
			for (const TSharedPtr<FJsonValue>& V : *Names)
			{
				FString Name;
				if (V->Type == EJson::String) { Name = V->AsString(); }
				else if (V->Type == EJson::Object)
				{
					const TSharedPtr<FJsonObject> O = V->AsObject();
					Name = O.IsValid() ? O->GetStringField(TEXT("name")) : FString();
				}
				Name = Name.TrimStartAndEnd();
				if (Name.IsEmpty()) { continue; }
				// UE 5.7+: plural ForEnums APIs removed in favor of UserDefinedEnum-specific helpers.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
				FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
#else
				FEnumEditorUtils::AddNewEnumeratorForEnums({ Enum });
#endif
				// Rename last non-_MAX enumerator to requested name via display name API.
				const int32 Last = Enum->NumEnums() - 2; // before _MAX
				if (Last >= 0)
				{
					FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Last, FText::FromString(Name));
				}
			}
		}
		FAssetRegistryModule::AssetCreated(Enum);
		FillEnumerators(Enum, Out);
		if (!SaveEnumPackage(Enum, Out)) { return false; }
		Out.bSuccess = true;
		return true;
	}

	UUserDefinedEnum* Enum = LoadOrFindEnum(PackagePath, Out.Error);
	if (!Enum) { return false; }

	if (Op == TEXT("get_enum"))
	{
		FillEnumerators(Enum, Out);
		Out.bSuccess = true;
		return true;
	}

	if (Op == TEXT("add_enumerator"))
	{
		FString Name = Spec->GetStringField(TEXT("name"));
		Name = Name.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			Out.Error = TEXT("add_enumerator requires name");
			return false;
		}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);
#else
		FEnumEditorUtils::AddNewEnumeratorForEnums({ Enum });
#endif
		const int32 Last = Enum->NumEnums() - 2;
		if (Last >= 0)
		{
			FEnumEditorUtils::SetEnumeratorDisplayName(Enum, Last, FText::FromString(Name));
		}
		FillEnumerators(Enum, Out);
		if (!SaveEnumPackage(Enum, Out)) { return false; }
		Out.bSuccess = true;
		return true;
	}

	if (Op == TEXT("remove_enumerator"))
	{
		FString Name = Spec->GetStringField(TEXT("name"));
		Name = Name.TrimStartAndEnd();
		if (Name.IsEmpty())
		{
			Out.Error = TEXT("remove_enumerator requires name");
			return false;
		}
		int32 Index = INDEX_NONE;
		for (int32 i = 0; i < Enum->NumEnums(); ++i)
		{
			if (Enum->GetNameStringByIndex(i) == Name)
			{
				Index = i;
				break;
			}
		}
		if (Index == INDEX_NONE)
		{
			Out.Error = FString::Printf(TEXT("enumerator not found: %s"), *Name);
			return false;
		}
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
		FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Index);
#else
		FEnumEditorUtils::RemoveEnumeratorFromEnums({ Enum }, Index);
#endif
		FillEnumerators(Enum, Out);
		if (!SaveEnumPackage(Enum, Out)) { return false; }
		Out.bSuccess = true;
		return true;
	}

	Out.Error = FString::Printf(TEXT("unknown operation: %s"), *In.Operation);
	return false;
}
