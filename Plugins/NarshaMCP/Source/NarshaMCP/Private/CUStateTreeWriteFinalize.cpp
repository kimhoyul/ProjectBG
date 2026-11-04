// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27780 (ST-06b): shared post-process for the 9 incremental StateTree write UFUNCTIONs.

#include "CUStateTreeWriteFinalize.h"

#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "CUJsonUtils.h"
#include "CURemoteLibrary.h"
#include "DiffUtils.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace CUStateTreeWriteFinalize
{
	static FString MakeDetailedError(
		const FString& ErrorCode,
		const FString& ErrorMessage,
		const FString& Context,
		const FString& Suggestion,
		const TSharedRef<FJsonObject>* Receipt = nullptr)
	{
		TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
		ErrorObject->SetBoolField(TEXT("success"), false);
		ErrorObject->SetStringField(TEXT("error_code"), ErrorCode);
		ErrorObject->SetStringField(TEXT("error"), ErrorMessage);
		if (!Context.IsEmpty())
		{
			ErrorObject->SetStringField(TEXT("context"), Context);
		}
		if (!Suggestion.IsEmpty())
		{
			ErrorObject->SetStringField(TEXT("suggestion"), Suggestion);
		}
		if (Receipt)
		{
			static const TCHAR* BoolKeys[] = {
				TEXT("compiled"), TEXT("saved"), TEXT("reload_verified"),
				TEXT("needs_recompile"), TEXT("ready_to_run"),
				TEXT("needs_recompile_before_compile"), TEXT("ready_to_run_before_compile"),
			};
			for (const TCHAR* Key : BoolKeys)
			{
				bool bValue = false;
				if ((*Receipt)->TryGetBoolField(Key, bValue))
				{
					ErrorObject->SetBoolField(Key, bValue);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
			if ((*Receipt)->TryGetArrayField(TEXT("compile_errors"), Errors) && Errors)
			{
				ErrorObject->SetArrayField(TEXT("compile_errors"), *Errors);
			}
		}
		return CUJsonUtils::SerializeToString(ErrorObject);
	}

	static void CollectCompilerMessages(const FStateTreeCompilerLog& CompilerLog, TArray<FString>& OutMessages)
	{
		for (const TSharedRef<FTokenizedMessage>& Message : CompilerLog.ToTokenizedMessages())
		{
			OutMessages.Add(Message->ToText().ToString());
		}
	}

	static void AttachReceiptArrays(const TSharedRef<FJsonObject>& ResultObject, const TArray<FString>& CompileErrors)
	{
		TArray<TSharedPtr<FJsonValue>> ErrArray;
		for (const FString& Msg : CompileErrors)
		{
			ErrArray.Add(MakeShared<FJsonValueString>(Msg));
		}
		ResultObject->SetArrayField(TEXT("compile_errors"), ErrArray);
	}

	static bool FreshLoadReadyToRun(UStateTree* LiveST, FString& OutError)
	{
		if (!LiveST)
		{
			OutError = TEXT("null StateTree");
			return false;
		}
		UPackage* Package = LiveST->GetOutermost();
		if (!Package)
		{
			OutError = TEXT("StateTree has no outermost package");
			return false;
		}
		const FString OrigFile = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		const FString AssetName = LiveST->GetName();
		const FString ReloadCheckDir = FPaths::ProjectSavedDir() / TEXT("__NarshaMCPReloadCheck")
			/ FGuid::NewGuid().ToString(EGuidFormats::Digits);
		const FString TempFile = ReloadCheckDir / (AssetName + TEXT(".uasset"));
		IFileManager::Get().MakeDirectory(*ReloadCheckDir, true);
		if (IFileManager::Get().Copy(*TempFile, *OrigFile) != COPY_OK)
		{
			OutError = TEXT("failed to copy saved .uasset for fresh-load verify");
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
			IFileManager::Get().DeleteDirectory(*ReloadCheckDir, /*RequireExists*/ false, /*Tree*/ true);
		};

		Copy = DiffUtils::LoadPackageForDiff(
			FPackagePath::FromLocalPath(TempFile),
			FPackagePath::FromPackageNameChecked(*Package->GetName()));
		if (!Copy)
		{
			OutError = TEXT("fresh-load package is null");
			return false;
		}
		if (!FPackageName::IsTempPackage(Copy->GetName()))
		{
			OutError = TEXT("diff-load fell back to the live package");
			return false;
		}
		UStateTree* CopyST = FindObject<UStateTree>(Copy, *AssetName);
		if (!CopyST)
		{
			OutError = TEXT("fresh-load StateTree object missing");
			return false;
		}
		if (CopyST == LiveST)
		{
			OutError = TEXT("fresh-load resolved to the live StateTree object");
			return false;
		}
		if (!CopyST->IsReadyToRun())
		{
			OutError = TEXT("fresh-load copy IsReadyToRun() is false");
			return false;
		}
		return true;
	}

	FString CompileSaveAndVerify(UStateTree* StateTree, const TSharedRef<FJsonObject>& ResultObject)
	{
		if (!StateTree)
		{
			return MakeDetailedError(
				TEXT("AI_ERR_603"),
				TEXT("StateTree compile/save helper received a null asset"),
				TEXT("Incremental write reached finalize without a loaded UStateTree"),
				TEXT("Ensure the asset loaded before mutation"));
		}

		UStateTreeEditingSubsystem::ValidateStateTree(StateTree);

#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		const bool bNeedsBefore = UStateTreeEditingSubsystem::NeedsRecompile(StateTree);
		const bool bReadyBefore = StateTree->IsReadyToRun();
		UStateTreeEditingSubsystem::MarkAsPubliclyModified(StateTree);
#else
		const bool bNeedsBefore = true;
		const bool bReadyBefore = StateTree->IsReadyToRun();
#endif
		ResultObject->SetBoolField(TEXT("needs_recompile_before_compile"), bNeedsBefore);
		ResultObject->SetBoolField(TEXT("ready_to_run_before_compile"), bReadyBefore);

		FStateTreeCompilerLog CompilerLog;
		const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog);
		TArray<FString> CompileErrors;
		CollectCompilerMessages(CompilerLog, CompileErrors);
		ResultObject->SetBoolField(TEXT("compiled"), bCompiled);
		AttachReceiptArrays(ResultObject, CompileErrors);

		if (!bCompiled)
		{
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
			CompilerLog.DumpToLog(StateTree, LogNarshaMCPRemote);
#else
			CompilerLog.DumpToLog(LogNarshaMCPRemote);
#endif

			ResultObject->SetBoolField(TEXT("saved"), false);
			ResultObject->SetBoolField(TEXT("reload_verified"), false);
			ResultObject->SetBoolField(TEXT("needs_recompile"), true);
			ResultObject->SetBoolField(TEXT("ready_to_run"), false);
			FString Context = FString::Join(CompileErrors, TEXT("\n"));
			if (Context.IsEmpty())
			{
				Context = TEXT("Compiler reported no messages");
			}
			if (Context.Len() > 2000)
			{
				Context.LeftInline(2000);
				Context.Append(TEXT(" ...(truncated)"));
			}
			return MakeDetailedError(
				TEXT("AI_ERR_603"),
				TEXT("StateTree incremental write failed to compile"),
				Context,
				TEXT("Fix the compile errors; the package was not saved"),
				&ResultObject);
		}

		UPackage* Package = StateTree->GetOutermost();
		if (!Package)
		{
			ResultObject->SetBoolField(TEXT("saved"), false);
			ResultObject->SetBoolField(TEXT("reload_verified"), false);
			return MakeDetailedError(
				TEXT("BP_ERR_501"),
				TEXT("Failed to save StateTree"),
				TEXT("Compiled StateTree has no outermost package"),
				TEXT("Reload the asset and retry"),
				&ResultObject);
		}
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(), FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.SaveFlags = SAVE_NoError;
		if (!UPackage::SavePackage(Package, StateTree, *PackageFileName, SaveArgs))
		{
			ResultObject->SetBoolField(TEXT("saved"), false);
			ResultObject->SetBoolField(TEXT("reload_verified"), false);
			ResultObject->SetBoolField(TEXT("ready_to_run"), StateTree->IsReadyToRun());
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
			ResultObject->SetBoolField(TEXT("needs_recompile"), UStateTreeEditingSubsystem::NeedsRecompile(StateTree));
#else
			ResultObject->SetBoolField(TEXT("needs_recompile"), false);
#endif
			return MakeDetailedError(
				TEXT("BP_ERR_501"),
				TEXT("Failed to save StateTree"),
				FString::Printf(TEXT("Compiled but could not save package '%s'"), *Package->GetName()),
				TEXT("Ensure the asset is not read-only"),
				&ResultObject);
		}
		ResultObject->SetBoolField(TEXT("saved"), true);

		const bool bReadyAfter = StateTree->IsReadyToRun();
#if UE_VERSION_NEWER_THAN_OR_EQUAL(5, 8, 0)
		const bool bNeedsAfter = UStateTreeEditingSubsystem::NeedsRecompile(StateTree);
#else
		const bool bNeedsAfter = false;
#endif
		ResultObject->SetBoolField(TEXT("ready_to_run"), bReadyAfter);
		ResultObject->SetBoolField(TEXT("needs_recompile"), bNeedsAfter);

		FString ReloadErr;
		const bool bReloadVerified = FreshLoadReadyToRun(StateTree, ReloadErr);
		ResultObject->SetBoolField(TEXT("reload_verified"), bReloadVerified);
		if (!bReloadVerified)
		{
			return MakeDetailedError(
				TEXT("AI_ERR_603"),
				TEXT("StateTree saved but fresh-load verify failed"),
				ReloadErr,
				TEXT("Inspect the saved asset; compile/save succeeded but reload_verified is false"),
				&ResultObject);
		}
		return FString();
	}
}
#else
namespace CUStateTreeWriteFinalize
{
	FString CompileSaveAndVerify(UStateTree* StateTree, const TSharedRef<FJsonObject>& ResultObject)
	{
		ResultObject->SetBoolField(TEXT("compiled"), false);
		ResultObject->SetBoolField(TEXT("saved"), false);
		ResultObject->SetBoolField(TEXT("reload_verified"), false);
		ResultObject->SetStringField(TEXT("error_code"), TEXT("state_tree_plugin_unavailable"));
		ResultObject->SetStringField(TEXT("hint"),
			TEXT("Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild."));
		return TEXT("{\"success\":false,\"error\":\"StateTree incremental writes require the optional StateTree and PropertyBindingUtils plugins\",\"error_code\":\"state_tree_plugin_unavailable\",\"hint\":\"Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild.\"}");
	}
}
#endif
