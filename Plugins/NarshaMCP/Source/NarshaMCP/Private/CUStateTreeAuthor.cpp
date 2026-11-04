// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26808 ST-06: spec → StateTree author.

#include "CUStateTreeAuthor.h"

#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "CURemoteLibrary.h"
#include "CULog.h"
#include "ScopedTransaction.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "DiffUtils.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ObjectTools.h"
#include "StateTreeEditorTypes.h"
#include "StateTreeState.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	void SplitSTPath(const FString& InPath, FString& OutPackage, FString& OutName, FString& OutLoadPath)
	{
		FString Path = InPath;
		Path.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Path.EndsWith(TEXT("/")))
		{
			Path.LeftChopInline(1);
		}
		int32 SlashIdx = INDEX_NONE;
		int32 DotIdx = INDEX_NONE;
		Path.FindLastChar(TEXT('/'), SlashIdx);
		Path.FindLastChar(TEXT('.'), DotIdx);
		if (DotIdx != INDEX_NONE && DotIdx > SlashIdx)
		{
			Path.LeftInline(DotIdx);
			SlashIdx = INDEX_NONE;
			Path.FindLastChar(TEXT('/'), SlashIdx);
		}
		OutName = (SlashIdx == INDEX_NONE) ? Path : Path.Mid(SlashIdx + 1);
		OutPackage = (SlashIdx == INDEX_NONE) ? FString(TEXT("/Game")) : Path.Left(SlashIdx);
		OutLoadPath = OutPackage / OutName + TEXT(".") + OutName;
	}

	TSharedPtr<FJsonObject> ParseObject(const FString& Json)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
		{
			return nullptr;
		}
		return Obj;
	}

	int32 ArrayLen(const TSharedPtr<FJsonObject>& Spec, const TCHAR* Key)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Spec.IsValid() && Spec->TryGetArrayField(Key, Arr) && Arr)
		{
			return Arr->Num();
		}
		return 0;
	}

	FString JsonObjToString(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid())
		{
			return TEXT("{}");
		}
		FString Out;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	bool RcSucceeded(const FString& Json, FString& OutDetail, TSharedPtr<FJsonObject>* OutObj = nullptr)
	{
		TSharedPtr<FJsonObject> Obj = ParseObject(Json);
		bool bOk = false;
		if (Obj.IsValid() && Obj->TryGetBoolField(TEXT("success"), bOk) && bOk)
		{
			if (OutObj)
			{
				*OutObj = Obj;
			}
			return true;
		}
		OutDetail = Json.Left(240);
		return false;
	}

	FString InstanceJson(const TSharedPtr<FJsonObject>& Row)
	{
		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (Row.IsValid() && Row->TryGetObjectField(TEXT("parameters"), ParamsObj) && ParamsObj)
		{
			return JsonObjToString(*ParamsObj);
		}
		return TEXT("{}");
	}

	void RememberAlias(TMap<FString, FString>& Aliases, const FString& Key, const FString& Id)
	{
		if (!Key.IsEmpty() && !Id.IsEmpty())
		{
			Aliases.Add(Key, Id);
		}
	}

	void RemapAliasFields(const TSharedPtr<FJsonObject>& Row, const TMap<FString, FString>& Aliases)
	{
		if (!Row.IsValid() || Aliases.Num() == 0)
		{
			return;
		}
		auto Remap = [&](const TCHAR* Field)
		{
			FString Value;
			if (!Row->TryGetStringField(Field, Value) || Value.IsEmpty())
			{
				return;
			}
			if (const FString* Mapped = Aliases.Find(Value))
			{
				Row->SetStringField(Field, *Mapped);
			}
		};
		Remap(TEXT("source_task_id"));
		Remap(TEXT("target_struct_id"));
		Remap(TEXT("target"));
		Remap(TEXT("target_id"));
		Remap(TEXT("target_task_id"));
		Remap(TEXT("transition"));

		FString TargetStructId;
		Row->TryGetStringField(TEXT("target_struct_id"), TargetStructId);
		if (TargetStructId.IsEmpty())
		{
			static const TCHAR* TargetFields[] = { TEXT("target"), TEXT("target_id"), TEXT("target_task_id") };
			for (const TCHAR* Field : TargetFields)
			{
				FString Value;
				if (Row->TryGetStringField(Field, Value) && !Value.IsEmpty())
				{
					Row->SetStringField(TEXT("target_struct_id"), Value);
					TargetStructId = Value;
					break;
				}
			}
		}

		FString TargetPath;
		if (!Row->TryGetStringField(TEXT("target_path"), TargetPath) || TargetPath.IsEmpty())
		{
			return;
		}
		int32 Colon = INDEX_NONE;
		if (TargetPath.FindChar(TEXT(':'), Colon) && Colon > 0)
		{
			const FString Prefix = TargetPath.Left(Colon);
			if (const FString* Mapped = Aliases.Find(Prefix))
			{
				Row->SetStringField(TEXT("target_path"), *Mapped + TargetPath.Mid(Colon));
			}
			return;
		}
		int32 Dot = INDEX_NONE;
		if (TargetPath.FindChar(TEXT('.'), Dot) && Dot > 0)
		{
			const FString Prefix = TargetPath.Left(Dot);
			if (const FString* Mapped = Aliases.Find(Prefix))
			{
				Row->SetStringField(TEXT("target_path"), *Mapped + TEXT(":") + TargetPath.Mid(Dot + 1));
			}
		}
	}

	bool IsContextDependentDiagnostic(const FCUStateTreeSpecDiagnostic& Diagnostic)
	{
		// Mirrors author_state_tree.rs: a saved create-or-open target may satisfy only reference
		// and ownership findings. Shape, required-field, duplicate-alias, and binding-shape
		// diagnostics are intrinsic to the request and must reject before LoadObject can create
		// an otherwise empty transient package (Issue #28408).
		return Diagnostic.Code == TEXT("unresolved_reference")
			|| Diagnostic.Code == TEXT("owner_mismatch");
	}

	struct FAuthorDiskGuard
	{
		FString FileName;
		FString PackageName;
		FString ObjectPath;
		TArray<uint8> Bytes;
		TWeakObjectPtr<UStateTree> CreatedAsset;
		bool bHadFile = false;
		bool bCreated = false;

		bool Capture(UStateTree* Asset, FString& OutError)
		{
			OutError.Reset();
			if (!Asset)
			{
				OutError = TEXT("Disk snapshot: asset is null");
				return false;
			}
			UPackage* Package = Asset->GetOutermost();
			if (!Package)
			{
				OutError = TEXT("Disk snapshot: package is null");
				return false;
			}
			PackageName = Package->GetName();
			ObjectPath = Asset->GetPathName();
			if (bCreated)
			{
				CreatedAsset = Asset;
			}
			FileName = FPackageName::LongPackageNameToFilename(
				PackageName, FPackageName::GetAssetPackageExtension());
			bHadFile = FPaths::FileExists(FileName);
			Bytes.Reset();
			if (bHadFile && !bCreated)
			{
				if (!FFileHelper::LoadFileToArray(Bytes, *FileName) || Bytes.Num() == 0)
				{
					OutError = FString::Printf(TEXT("Failed to snapshot existing package: %s"), *FileName);
					return false;
				}
			}
			return true;
		}

		bool Revert()
		{
			if (FileName.IsEmpty())
			{
				return true;
			}
			if (bCreated)
			{
				// Issue #28408: deleting only the .uasset leaves the newly-created package, registry
				// record, and UStateTree identity alive. A same-name retry then resolves the rejected
				// object and can report an unrelated duplicate-state error. Notify the registry while
				// the object still owns its original path, then discard it and verify caller-visible
				// absence across all four surfaces.
				IAssetRegistry& Registry =
					FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
				UStateTree* Asset = CreatedAsset.Get();
				if (!IsValid(Asset) && !ObjectPath.IsEmpty())
				{
					Asset = FindObject<UStateTree>(nullptr, *ObjectPath);
				}
				UPackage* Package = PackageName.IsEmpty()
					? nullptr
					: FindPackage(nullptr, *PackageName);

				const bool bFileDeleted = !FPaths::FileExists(FileName)
					|| IFileManager::Get().Delete(
						*FileName,
						/*RequireExists=*/false,
						/*EvenReadOnly=*/true,
						/*Quiet=*/true);
				if (IsValid(Asset))
				{
					Registry.AssetDeleted(Asset);
					ObjectTools::DeleteObjectsUnchecked({Asset});
				}
				if (Package)
				{
					Package->SetDirtyFlag(false);
					Registry.PackageDeleted(Package);
				}
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);
				Registry.ScanModifiedAssetFiles({FileName});

				const bool bRegistryClear = ObjectPath.IsEmpty()
					|| !Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath)).IsValid();
				const bool bObjectClear = ObjectPath.IsEmpty()
					|| FindObject<UStateTree>(nullptr, *ObjectPath) == nullptr;
				const bool bPackageClear = PackageName.IsEmpty()
					|| FindPackage(nullptr, *PackageName) == nullptr;
				return bFileDeleted
					&& !FPaths::FileExists(FileName)
					&& bRegistryClear
					&& bObjectClear
					&& bPackageClear;
			}
			if (!bHadFile)
			{
				if (!FPaths::FileExists(FileName))
				{
					return true;
				}
				return IFileManager::Get().Delete(*FileName, false, true);
			}
			return FFileHelper::SaveArrayToFile(Bytes, *FileName);
		}
	};

	void Record(FCUStateTreeAuthorResult& Out, const bool bOk, const FString& Label, const FString& Detail)
	{
		if (bOk)
		{
			++Out.AppliedOk;
			Out.Added.Add(Label);
		}
		else
		{
			++Out.AppliedFail;
			Out.Failed.Add(Label + TEXT(": ") + Detail);
		}
	}

	bool CompileAndSave(UStateTree* Asset, const FString& LoadPath, FCUStateTreeAuthorResult& Out)
	{
		if (!Asset)
		{
			Out.Error = TEXT("CompileAndSave: asset is null");
			return false;
		}
		FStateTreeCompilerLog CompilerLog;
		if (!UStateTreeEditingSubsystem::CompileStateTree(Asset, CompilerLog))
		{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
			CompilerLog.DumpToLog(Asset, LogNarshaMCPRemote);
#else
			CompilerLog.DumpToLog(LogNarshaMCPRemote);
#endif
			Out.bCompiled = false;
			Out.Error = TEXT("StateTree compile failed (ST-03). See editor log.");
			return false;
		}
		Out.bCompiled = true;

		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs))
		{
			Out.Error = FString::Printf(TEXT("Compile succeeded but save failed: %s"), *LoadPath);
			return false;
		}
		Out.SavedFilePath = PackageFileName;
		return true;
	}

	FString DefaultValueFromRow(const TSharedPtr<FJsonObject>& Row)
	{
		FString DefaultVal;
		Row->TryGetStringField(TEXT("default"), DefaultVal);
		if (!DefaultVal.IsEmpty())
		{
			return DefaultVal;
		}
		const TSharedPtr<FJsonValue> Def = Row->TryGetField(TEXT("default"));
		if (!Def.IsValid() || Def->IsNull())
		{
			return DefaultVal;
		}
		switch (Def->Type)
		{
		case EJson::String:
			return Def->AsString();
		case EJson::Number:
			return FString::SanitizeFloat(Def->AsNumber());
		case EJson::Boolean:
			return Def->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Object:
			return JsonObjToString(Def->AsObject());
		case EJson::Array:
		{
			FString Serialized;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
			FJsonSerializer::Serialize(Def->AsArray(), Writer);
			return Serialized;
		}
		default:
			return DefaultVal;
		}
	}
}

bool FCUStateTreeAuthor::Execute(const FCUStateTreeAuthorParams& In, FCUStateTreeAuthorResult& Out)
{
	Out = FCUStateTreeAuthorResult{};
	Out.bDryRun = In.bDryRun;

	if (In.STPackagePath.TrimStartAndEnd().IsEmpty())
	{
		Out.Error = TEXT("STPackagePath is required (e.g. /Game/NarshaMCP/ST_Authored)");
		return false;
	}

	TSharedPtr<FJsonObject> Spec;
	if (!In.SpecJson.IsEmpty())
	{
		Spec = ParseObject(In.SpecJson);
		if (!Spec.IsValid())
		{
			Out.Error = TEXT("SpecJson is not a valid JSON object");
			return false;
		}
	}
	Out.PlannedStates = ArrayLen(Spec, TEXT("states"));
	Out.PlannedTransitions = ArrayLen(Spec, TEXT("transitions"));
	Out.PlannedTasks = ArrayLen(Spec, TEXT("tasks"));
	Out.PlannedParameters = ArrayLen(Spec, TEXT("parameters"));
	Out.PlannedConditions = ArrayLen(Spec, TEXT("conditions"));
	Out.PlannedEvaluators = ArrayLen(Spec, TEXT("evaluators"));
	Out.PlannedConsiderations = ArrayLen(Spec, TEXT("considerations"));
	Out.PlannedBindings = ArrayLen(Spec, TEXT("bindings"));

	FString PackagePath, AssetName, LoadPath;
	SplitSTPath(In.STPackagePath, PackagePath, AssetName, LoadPath);
	if (AssetName.IsEmpty())
	{
		Out.Error = TEXT("Could not parse asset name from STPackagePath");
		return false;
	}

	// Issue #28408: validate intrinsic request failures before loading the destination. LoadObject on
	// a missing target can create an empty transient UPackage even though no asset exists; returning
	// after that point violates fail-closed cleanup. A saved create-or-open target may still satisfy
	// unresolved-reference/owner findings, so defer only those two context-dependent codes to the
	// existing-asset pass (the same boundary used by the Rust preflight).
	TArray<FCUStateTreeSpecDiagnostic> PreloadDiagnostics;
	if (!ValidateSpec(Spec, PreloadDiagnostics, nullptr))
	{
		const FString TargetPackageName = PackagePath / AssetName;
		const FString TargetFileName = FPackageName::LongPackageNameToFilename(
			TargetPackageName, FPackageName::GetAssetPackageExtension());
		if (FPaths::FileExists(TargetFileName))
		{
			PreloadDiagnostics.RemoveAll([](const FCUStateTreeSpecDiagnostic& Diagnostic)
			{
				return IsContextDependentDiagnostic(Diagnostic);
			});
		}
		if (PreloadDiagnostics.Num() > 0)
		{
			Out.Diagnostics = MoveTemp(PreloadDiagnostics);
			Out.Error = Out.Diagnostics[0].Message;
			return false;
		}
	}

	// Issue #27939: once request-intrinsic validation passes, load the destination read-only and
	// validate references against its StateTree context before any create/transaction/mutation/save.
	UStateTree* Asset = LoadObject<UStateTree>(nullptr, *LoadPath);
	if (!ValidateSpec(Spec, Out.Diagnostics, Asset))
	{
		Out.Error = Out.Diagnostics[0].Message;
		return false;
	}

	if (In.bDryRun)
	{
		Out.bSuccess = true;
		return true;
	}

	if (!Asset)
	{
		const FString Created = UCURemoteLibrary::CreateStateTree(AssetName, PackagePath);
		FString Detail;
		if (!RcSucceeded(Created, Detail))
		{
			Out.Error = FString::Printf(TEXT("CreateStateTree failed: %s"), *Detail);
			return false;
		}
		Out.bCreated = true;
		Out.Added.Add(TEXT("create:") + LoadPath);
		++Out.AppliedOk;
		Asset = LoadObject<UStateTree>(nullptr, *LoadPath);
		if (!Asset)
		{
			Out.Error = TEXT("CreateStateTree reported success but the asset did not load");
			return false;
		}
	}

	FAuthorDiskGuard Disk;
	Disk.bCreated = Out.bCreated;
	{
		FString SnapError;
		if (!Disk.Capture(Asset, SnapError))
		{
			Out.Error = SnapError;
			return false;
		}
	}

	{
#if WITH_EDITOR
	FScopedTransaction Transaction(NSLOCTEXT("NarshaMCP", "AuthorStateTree", "Author StateTree"));
	Asset->Modify();
	if (UStateTreeEditorData* ED = Cast<UStateTreeEditorData>(Asset->EditorData))
	{
		ED->Modify();
	}
#endif

	auto AbortApply = [&](const FString& Err) -> bool
	{
		Out.Error = Err;
#if WITH_EDITOR
		Transaction.Cancel();
#endif
		if (!Disk.Revert())
		{
			Out.Error += TEXT(" (also failed to restore the on-disk package)");
		}
		return false;
	};

	auto RequireObject = [&](const TSharedPtr<FJsonValue>& V, const TCHAR* Kind, int32 Index,
		TSharedPtr<FJsonObject>& OutRow) -> bool
	{
		OutRow = V.IsValid() ? V->AsObject() : nullptr;
		if (OutRow.IsValid())
		{
			return true;
		}
		AbortApply(FString::Printf(TEXT("%s[%d] is not a JSON object"), Kind, Index));
		return false;
	};

	TMap<FString, FString> Aliases;
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("states"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("states"), i, Row)) { return false; }
			FString Name;
			Row->TryGetStringField(TEXT("name"), Name);
			if (Name.Equals(TEXT("Root"), ESearchCase::IgnoreCase)) { continue; }
			FString Parent;
			Row->TryGetStringField(TEXT("parent"), Parent);
			FString Detail;
			TSharedPtr<FJsonObject> Applied;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeState(LoadPath, Name, Parent, TEXT("")), Detail, &Applied);
			Record(Out, bOk, TEXT("state:") + Name, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
			FString StateId;
			if (Applied.IsValid())
			{
				Applied->TryGetStringField(TEXT("state_id"), StateId);
			}
			RememberAlias(Aliases, Name, StateId);
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("parameters"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("parameters"), i, Row)) { return false; }
			FString Name;
			Row->TryGetStringField(TEXT("name"), Name);
			FString Type = TEXT("float");
			Row->TryGetStringField(TEXT("type"), Type);
			const FString DefaultVal = DefaultValueFromRow(Row);
			FString Detail;
			TSharedPtr<FJsonObject> Applied;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeParameter(LoadPath, Name, Type, DefaultVal), Detail, &Applied);
			Record(Out, bOk, TEXT("parameter:") + Name, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
			FString BagId;
			if (Applied.IsValid())
			{
				Applied->TryGetStringField(TEXT("root_parameters_guid"), BagId);
			}
			RememberAlias(Aliases, Name, BagId);
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("tasks"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("tasks"), i, Row)) { return false; }
			FString StateName;
			Row->TryGetStringField(TEXT("state"), StateName);
			FString ClassName;
			Row->TryGetStringField(TEXT("class"), ClassName);
			FString Detail;
			TSharedPtr<FJsonObject> Applied;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeTask(LoadPath, StateName, ClassName, InstanceJson(Row)), Detail, &Applied);
			Record(Out, bOk, TEXT("task:") + ClassName, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
			FString TaskId;
			if (Applied.IsValid())
			{
				Applied->TryGetStringField(TEXT("task_id"), TaskId);
			}
			if (!TaskId.IsEmpty())
			{
				FString Alias;
				Row->TryGetStringField(TEXT("id"), Alias);
				if (Alias.IsEmpty())
				{
					Row->TryGetStringField(TEXT("name"), Alias);
				}
				RememberAlias(Aliases, Alias, TaskId);
				RememberAlias(Aliases, TEXT("$last"), TaskId);
			}
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("transitions"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("transitions"), i, Row)) { return false; }
			FString From; Row->TryGetStringField(TEXT("from"), From);
			FString To; Row->TryGetStringField(TEXT("to"), To);
			TSharedPtr<FJsonObject> Trigger = MakeShared<FJsonObject>();
			FString TriggerName;
			if (Row->TryGetStringField(TEXT("trigger"), TriggerName) && !TriggerName.IsEmpty())
			{
				Trigger->SetStringField(TEXT("Trigger"), TriggerName);
			}
			FString Detail;
			TSharedPtr<FJsonObject> Applied;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeTransition(LoadPath, From, To, JsonObjToString(Trigger)), Detail, &Applied);
			Record(Out, bOk, TEXT("transition:") + From + TEXT("->") + To, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
			FString TransitionId;
			if (Applied.IsValid())
			{
				Applied->TryGetStringField(TEXT("transition_id"), TransitionId);
			}
			FString Alias;
			Row->TryGetStringField(TEXT("id"), Alias);
			RememberAlias(Aliases, Alias, TransitionId);
			RememberAlias(Aliases, From + TEXT("->") + To, TransitionId);
			RememberAlias(Aliases, TEXT("$last_transition"), TransitionId);
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("conditions"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("conditions"), i, Row)) { return false; }
			RemapAliasFields(Row, Aliases);
			FString StateName; Row->TryGetStringField(TEXT("state"), StateName);
			FString Transition; Row->TryGetStringField(TEXT("transition"), Transition);
			FString ClassName; Row->TryGetStringField(TEXT("class"), ClassName);
			FString Detail;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeCondition(LoadPath, StateName, Transition, ClassName, InstanceJson(Row)), Detail);
			Record(Out, bOk, TEXT("condition:") + ClassName, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("evaluators"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("evaluators"), i, Row)) { return false; }
			FString ClassName; Row->TryGetStringField(TEXT("class"), ClassName);
			FString Detail;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeEvaluator(LoadPath, ClassName, InstanceJson(Row)), Detail);
			Record(Out, bOk, TEXT("evaluator:") + ClassName, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("considerations"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("considerations"), i, Row)) { return false; }
			FString StateName; Row->TryGetStringField(TEXT("state"), StateName);
			FString ClassName; Row->TryGetStringField(TEXT("class"), ClassName);
			FString Detail;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeConsideration(LoadPath, StateName, ClassName, InstanceJson(Row)), Detail);
			Record(Out, bOk, TEXT("consideration:") + ClassName, Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
		}
	}
	if (Spec.IsValid() && Spec->TryGetArrayField(TEXT("bindings"), Arr) && Arr)
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RequireObject((*Arr)[i], TEXT("bindings"), i, Row)) { return false; }
			RemapAliasFields(Row, Aliases);
			FString Detail;
			const bool bOk = RcSucceeded(
				UCURemoteLibrary::AddStateTreeBinding(LoadPath, JsonObjToString(Row)), Detail);
			Record(Out, bOk, TEXT("binding"), Detail);
			if (!bOk)
			{
				return AbortApply(Detail);
			}
		}
	}

	if (Out.AppliedFail > 0)
	{
		return AbortApply(FString::Printf(TEXT("%d spec item(s) failed to apply"), Out.AppliedFail));
	}

	if (!CompileAndSave(Asset, LoadPath, Out))
	{
		return AbortApply(Out.Error);
	}
	}

	FCUStateTreeAuthorVerifyResult VerifyOut;
	Out.bVerified = Verify(In.STPackagePath, VerifyOut);
	if (!Out.bVerified)
	{
		Out.Error = VerifyOut.Error.IsEmpty()
			? TEXT("Fresh-load verify failed after save")
			: VerifyOut.Error;
		if (!Disk.Revert())
		{
			Out.Error += TEXT(" (also failed to restore the on-disk package)");
		}
		return false;
	}

	Out.bSuccess = true;
	return true;
}

bool FCUStateTreeAuthor::Verify(const FString& STPackagePath, FCUStateTreeAuthorVerifyResult& Out)
{
	Out = FCUStateTreeAuthorVerifyResult{};
	FString PackagePath, AssetName, LoadPath;
	SplitSTPath(STPackagePath, PackagePath, AssetName, LoadPath);
	const FString LongPackage = PackagePath / AssetName;
	const FString OrigFile = FPackageName::LongPackageNameToFilename(
		LongPackage, FPackageName::GetAssetPackageExtension());
	if (!FPaths::FileExists(OrigFile))
	{
		Out.Error = FString::Printf(TEXT("Verify: on-disk package missing: %s"), *OrigFile);
		return false;
	}

	UStateTree* Live = FindObject<UStateTree>(nullptr, *LoadPath);
	if (!Live)
	{
		Live = LoadObject<UStateTree>(nullptr, *LoadPath);
	}

	const FString ReloadDir = FPaths::ProjectSavedDir() / TEXT("__NarshaMCPAuthorVerify")
		/ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	IFileManager::Get().MakeDirectory(*ReloadDir, true);
	const FString TempFile = ReloadDir / (AssetName + TEXT(".uasset"));
	if (IFileManager::Get().Copy(*TempFile, *OrigFile) != COPY_OK)
	{
		Out.Error = TEXT("Verify: failed to copy saved .uasset for fresh-load");
		return false;
	}

	UPackage* Copy = nullptr;
	ON_SCOPE_EXIT
	{
		if (Copy)
		{
			ResetLoaders(Copy);
		}
		IFileManager::Get().Delete(*TempFile);
		IFileManager::Get().DeleteDirectory(*ReloadDir, /*RequireExists*/ false, /*Tree*/ true);
	};

	Copy = DiffUtils::LoadPackageForDiff(
		FPackagePath::FromLocalPath(TempFile),
		FPackagePath::FromPackageNameChecked(*LongPackage));
	if (!Copy)
	{
		Out.Error = TEXT("Verify: LoadPackageForDiff returned null");
		return false;
	}
	if (!FPackageName::IsTempPackage(Copy->GetName()))
	{
		Out.Error = TEXT("Verify: diff-load fell back to the live package (not a fresh disk load)");
		return false;
	}

	UStateTree* CopyST = FindObject<UStateTree>(Copy, *AssetName);
	if (!CopyST)
	{
		Out.Error = TEXT("Verify: copy package has no StateTree");
		return false;
	}
	if (Live && CopyST == Live)
	{
		Out.Error = TEXT("Verify: copy resolved to the live StateTree object");
		return false;
	}

	Out.bLoaded = true;
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(CopyST->EditorData);
	Out.bHasEditorData = EditorData != nullptr;
	if (EditorData)
	{
		EditorData->VisitHierarchy([&](UStateTreeState& /*State*/, UStateTreeState* /*Parent*/)
		{
			++Out.StateCount;
			return EStateTreeVisitor::Continue;
		});
	}
	if (!Out.bHasEditorData)
	{
		Out.Error = TEXT("Fresh-loaded StateTree has no EditorData");
		return false;
	}
	return true;
}
#else
bool FCUStateTreeAuthor::Execute(const FCUStateTreeAuthorParams& In, FCUStateTreeAuthorResult& Out)
{
	Out = FCUStateTreeAuthorResult{};
	Out.bDryRun = In.bDryRun;
	Out.ErrorCode = TEXT("state_tree_plugin_unavailable");
	Out.Hint = TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild.");
	Out.Error = TEXT("StateTree authoring requires the optional StateTree and PropertyBindingUtils plugins.");
	return false;
}

bool FCUStateTreeAuthor::Verify(const FString& STPackagePath, FCUStateTreeAuthorVerifyResult& Out)
{
	Out = FCUStateTreeAuthorVerifyResult{};
	Out.ErrorCode = TEXT("state_tree_plugin_unavailable");
	Out.Hint = TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild.");
	Out.Error = TEXT("StateTree verification requires the optional StateTree and PropertyBindingUtils plugins.");
	return false;
}
#endif
