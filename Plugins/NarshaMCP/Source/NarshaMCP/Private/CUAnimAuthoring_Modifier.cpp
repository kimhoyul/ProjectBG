// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #27875 — editor-only Animation Modifier apply/revert operations.
// Modifier execution rewrites the source AnimSequence package, so each response
// carries a before/after SHA-1 receipt for the saved .uasset. This is an
// in-editor path and does not weaken the offline raw/compressed animation-editing
// boundary recorded in Issues #11155/#11189.

#include "CUAnimAuthoring_Shared.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AnimationModifier.h"
#include "AnimationModifiersAssetUserData.h"
#include "HAL/PlatformFileManager.h"
#include "IAnimationModifiersModule.h"
#include "Interfaces/Interface_AssetUserData.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"

using namespace NarshaAnimAuthoring;

namespace
{
	constexpr const TCHAR* AnimationModifiersModuleName = TEXT("AnimationModifiers");

	struct FModifierContext
	{
		UAnimSequence* Sequence = nullptr;
		TArray<UAnimationModifier*> Modifiers;
		FString PackageFilename;
	};

	FString HashFileSha1(const FString& Filename)
	{
		TUniquePtr<IFileHandle> File(
			FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Filename));
		if (Filename.IsEmpty() || !File)
		{
			return FString();
		}

		FSHA1 Sha;
		constexpr int32 ChunkSize = 1024 * 1024;
		TArray<uint8> Buffer;
		Buffer.SetNumUninitialized(ChunkSize);
		int64 Remaining = File->Size();
		while (Remaining > 0)
		{
			const int64 ToRead = FMath::Min(Remaining, static_cast<int64>(ChunkSize));
			if (!File->Read(Buffer.GetData(), ToRead))
			{
				return FString();
			}
			Sha.Update(Buffer.GetData(), static_cast<uint32>(ToRead));
			Remaining -= ToRead;
		}
		Sha.Final();
		uint8 Digest[FSHA1::DigestSize];
		Sha.GetHash(Digest);

		FString Out;
		Out.Reserve(FSHA1::DigestSize * 2);
		for (int32 Index = 0; Index < FSHA1::DigestSize; ++Index)
		{
			Out += FString::Printf(TEXT("%02x"), Digest[Index]);
		}
		return Out;
	}

	void AppendConfiguredModifiers(
		IInterface_AssetUserData* Owner,
		TArray<UAnimationModifier*>& OutModifiers)
	{
		if (!Owner)
		{
			return;
		}
		const UAnimationModifiersAssetUserData* UserData =
			Cast<UAnimationModifiersAssetUserData>(Owner->GetAssetUserDataOfClass(
				UAnimationModifiersAssetUserData::StaticClass()));
		if (!UserData)
		{
			return;
		}
		for (UAnimationModifier* Modifier : UserData->GetAnimationModifierInstances())
		{
			if (Modifier)
			{
				OutModifiers.Add(Modifier);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> ModifierClassValues(const TArray<UAnimationModifier*>& Modifiers)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		Values.Reserve(Modifiers.Num());
		for (const UAnimationModifier* Modifier : Modifiers)
		{
			if (Modifier && Modifier->GetClass())
			{
				Values.Add(MakeShared<FJsonValueString>(Modifier->GetClass()->GetPathName()));
			}
		}
		return Values;
	}

	TSharedRef<FJsonObject> MakeModifierError(
		const FString& AssetPath,
		const FString& ErrorCode,
		const FString& Error,
		const FString& Hint = FString())
	{
		TSharedRef<FJsonObject> Obj = MakeErrorObject(AssetPath, Error, Hint);
		Obj->SetStringField(TEXT("error_code"), ErrorCode);
		return Obj;
	}

	IAnimationModifiersModule* LoadAnimationModifiersModule(
		const FString& AssetPath,
		TSharedPtr<FJsonObject>& OutError)
	{
		FModuleManager& ModuleManager = FModuleManager::Get();
		if (!ModuleManager.ModuleExists(AnimationModifiersModuleName))
		{
			OutError = MakeModifierError(
				AssetPath,
				TEXT("module_unavailable"),
				TEXT("AnimationModifiers editor module is not installed"),
				TEXT("Use an Unreal Editor build that includes Engine/Source/Editor/AnimationModifiers."));
			return nullptr;
		}

		IAnimationModifiersModule* Module =
			FModuleManager::LoadModulePtr<IAnimationModifiersModule>(AnimationModifiersModuleName);
		if (!Module)
		{
			OutError = MakeModifierError(
				AssetPath,
				TEXT("module_unavailable"),
				TEXT("AnimationModifiers editor module could not be loaded"));
		}
		return Module;
	}

	bool BuildModifierContext(
		const FString& AssetPath,
		FModifierContext& Out,
		TSharedPtr<FJsonObject>& OutError)
	{
		Out.Sequence = LoadObject<UAnimSequence>(nullptr, *AssetPath);
		if (!Out.Sequence)
		{
			OutError = MakeModifierError(
				AssetPath,
				TEXT("invalid_anim_sequence"),
				FString::Printf(TEXT("Failed to load AnimSequence: %s"), *AssetPath),
				TEXT("Animation Modifier execution targets UAnimSequence assets only."));
			return false;
		}
		if (!Out.Sequence->GetSkeleton())
		{
			OutError = MakeModifierError(
				AssetPath,
				TEXT("invalid_skeleton"),
				TEXT("AnimSequence has no Skeleton"),
				TEXT("Assign a valid Skeleton before executing Animation Modifiers."));
			return false;
		}

		// Match the engine module's destructive scope: Modifiers-on-Skeleton
		// execute before modifiers configured directly on the sequence.
		AppendConfiguredModifiers(Out.Sequence->GetSkeleton(), Out.Modifiers);
		AppendConfiguredModifiers(Out.Sequence, Out.Modifiers);
		if (Out.Modifiers.IsEmpty())
		{
			OutError = MakeModifierError(
				AssetPath,
				TEXT("no_animation_modifiers"),
				TEXT("AnimSequence has no configured Animation Modifier instances"),
				TEXT("Attach at least one modifier to the AnimSequence or its Skeleton before applying or reverting."));
			return false;
		}

		UPackage* Package = Out.Sequence->GetOutermost();
		if (!Package || !FPackageName::TryConvertLongPackageNameToFilename(
				Package->GetName(), Out.PackageFilename, FPackageName::GetAssetPackageExtension()))
		{
			OutError = MakeModifierError(
				AssetPath,
				TEXT("package_path_unresolved"),
				TEXT("Could not resolve the AnimSequence package to a .uasset filename"));
			return false;
		}

		return true;
	}

	bool SaveModifierAsset(UAnimSequence* Sequence, const FString& Filename, FString& OutError)
	{
		if (!Sequence || Filename.IsEmpty())
		{
			OutError = TEXT("AnimSequence or package filename is unavailable");
			return false;
		}

		Sequence->MarkPackageDirty();
		UPackage* Package = Sequence->GetOutermost();
		if (!Package)
		{
			OutError = TEXT("AnimSequence has no outer package");
			return false;
		}

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Standalone;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Package, Sequence, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("SavePackage returned false for '%s'"), *Filename);
			return false;
		}
		return true;
	}

	TSharedRef<FJsonObject> MakeHashReadError(
		const FString& AssetPath,
		const FString& Phase,
		const FString& Filename,
		bool bModified,
		bool bSaved,
		const FString& HashBefore = FString())
	{
		TSharedRef<FJsonObject> Error = MakeModifierError(
			AssetPath,
			TEXT("asset_hash_unavailable"),
			FString::Printf(TEXT("Could not read .uasset hash %s mutation: %s"), *Phase, *Filename),
			TEXT("Ensure the package has been saved and the file is readable."));
		Error->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
		Error->SetBoolField(TEXT("modified"), bModified);
		Error->SetBoolField(TEXT("saved"), bSaved);
		if (!HashBefore.IsEmpty())
		{
			Error->SetStringField(TEXT("asset_file_hash_before"), HashBefore);
		}
		return Error;
	}

	void AddReceiptFields(
		const TSharedRef<FJsonObject>& Result,
		const FModifierContext& Context,
		const FString& HashBefore,
		const FString& HashAfter,
		int32 ChangedCount)
	{
		Result->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
		Result->SetStringField(TEXT("asset_file_hash_before"), HashBefore);
		Result->SetStringField(TEXT("asset_file_hash_after"), HashAfter);
		Result->SetBoolField(TEXT("hash_changed"), HashBefore != HashAfter);
		Result->SetNumberField(TEXT("modifier_count"), Context.Modifiers.Num());
		Result->SetNumberField(TEXT("changed_modifier_count"), ChangedCount);
		Result->SetArrayField(TEXT("modifier_classes"), ModifierClassValues(Context.Modifiers));
		Result->SetBoolField(TEXT("modified"), true);
		Result->SetBoolField(TEXT("saved"), true);
	}

	void AddUnsavedMutationDisclosure(
		const TSharedRef<FJsonObject>& Error,
		const FString& HashBefore)
	{
		Error->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
		Error->SetStringField(TEXT("asset_file_hash_before"), HashBefore);
		Error->SetStringField(TEXT("asset_file_hash_after"), HashBefore);
		Error->SetBoolField(TEXT("hash_changed"), false);
		Error->SetBoolField(TEXT("modified"), true);
		Error->SetBoolField(TEXT("saved"), false);
		Error->SetBoolField(TEXT("in_memory_state_changed"), true);
		Error->SetBoolField(TEXT("requires_editor_reload"), true);
	}

	void AddFailedSaveMutationDisclosure(
		const TSharedRef<FJsonObject>& Error,
		const FString& Filename,
		const FString& HashBefore)
	{
		Error->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
		Error->SetStringField(TEXT("asset_file_hash_before"), HashBefore);
		Error->SetBoolField(TEXT("modified"), true);
		Error->SetBoolField(TEXT("saved"), false);
		Error->SetBoolField(TEXT("in_memory_state_changed"), true);
		Error->SetBoolField(TEXT("requires_editor_reload"), true);

		// SavePackage can fail after touching the destination. Re-read the file
		// instead of claiming that its pre-save receipt is still authoritative.
		const FString HashAfter = HashFileSha1(Filename);
		if (HashAfter.IsEmpty())
		{
			Error->SetBoolField(TEXT("asset_file_hash_after_available"), false);
			return;
		}

		Error->SetBoolField(TEXT("asset_file_hash_after_available"), true);
		Error->SetStringField(TEXT("asset_file_hash_after"), HashAfter);
		Error->SetBoolField(TEXT("hash_changed"), HashBefore != HashAfter);
	}
}

FString UCURemoteLibrary::AnimSeq_ApplyAnimationModifiers(const FString& AssetPath)
{
	FModifierContext Context;
	TSharedPtr<FJsonObject> ContextError;
	if (!BuildModifierContext(AssetPath, Context, ContextError))
	{
		return SerializeJsonResponse(ContextError.ToSharedRef());
	}

	TSharedPtr<FJsonObject> ModuleError;
	IAnimationModifiersModule* Module = LoadAnimationModifiersModule(AssetPath, ModuleError);
	if (!Module)
	{
		return SerializeJsonResponse(ModuleError.ToSharedRef());
	}

	const FString HashBefore = HashFileSha1(Context.PackageFilename);
	if (HashBefore.IsEmpty())
	{
		return SerializeJsonResponse(MakeHashReadError(
			AssetPath, TEXT("before"), Context.PackageFilename, false, false));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("NarshaMCP: apply animation modifiers on %s"), *AssetPath)));
	Context.Sequence->Modify();
#endif
	{
		UE::Anim::FApplyModifiersScope Scope(
			UE::Anim::FApplyModifiersScope::SuppressWarningAndError);
		TArray<UAnimSequence*> Sequences{Context.Sequence};
		Module->ApplyAnimationModifiers(Sequences, true);
	}

	int32 AppliedCount = 0;
	for (const UAnimationModifier* Modifier : Context.Modifiers)
	{
		if (Modifier && Modifier->IsLatestRevisionApplied(Context.Sequence))
		{
			++AppliedCount;
		}
	}
	if (AppliedCount != Context.Modifiers.Num())
	{
		TSharedRef<FJsonObject> Error = MakeModifierError(
			AssetPath,
			TEXT("apply_incomplete"),
			FString::Printf(TEXT("Applied %d of %d configured Animation Modifiers"),
				AppliedCount, Context.Modifiers.Num()),
			TEXT("Inspect the modifier implementation and Editor log; dialogs were suppressed."));
		Error->SetNumberField(TEXT("modifier_count"), Context.Modifiers.Num());
		Error->SetNumberField(TEXT("changed_modifier_count"), AppliedCount);
		Error->SetArrayField(TEXT("modifier_classes"), ModifierClassValues(Context.Modifiers));
		AddUnsavedMutationDisclosure(Error, HashBefore);
		return SerializeJsonResponse(Error);
	}

	FString SaveError;
	if (!SaveModifierAsset(Context.Sequence, Context.PackageFilename, SaveError))
	{
		TSharedRef<FJsonObject> Error = MakeModifierError(
			AssetPath, TEXT("save_failed"), SaveError);
		AddFailedSaveMutationDisclosure(
			Error, Context.PackageFilename, HashBefore);
		return SerializeJsonResponse(Error);
	}

	const FString HashAfter = HashFileSha1(Context.PackageFilename);
	if (HashAfter.IsEmpty())
	{
		return SerializeJsonResponse(MakeHashReadError(
			AssetPath, TEXT("after"), Context.PackageFilename, true, true, HashBefore));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	AddReceiptFields(Result, Context, HashBefore, HashAfter, AppliedCount);
	return SerializeJsonResponse(Result);
}

FString UCURemoteLibrary::AnimSeq_RevertAnimationModifiers(const FString& AssetPath)
{
	FModifierContext Context;
	TSharedPtr<FJsonObject> ContextError;
	if (!BuildModifierContext(AssetPath, Context, ContextError))
	{
		return SerializeJsonResponse(ContextError.ToSharedRef());
	}

	TSharedPtr<FJsonObject> ModuleError;
	if (!LoadAnimationModifiersModule(AssetPath, ModuleError))
	{
		return SerializeJsonResponse(ModuleError.ToSharedRef());
	}

	TArray<TSharedPtr<FJsonValue>> NonRevertible;
	for (const UAnimationModifier* Modifier : Context.Modifiers)
	{
		if (!Modifier || !Modifier->CanRevert(Context.Sequence))
		{
			const FString ClassPath = Modifier && Modifier->GetClass()
				? Modifier->GetClass()->GetPathName()
				: TEXT("<null>");
			NonRevertible.Add(MakeShared<FJsonValueString>(ClassPath));
		}
	}
	if (!NonRevertible.IsEmpty())
	{
		TSharedRef<FJsonObject> Error = MakeModifierError(
			AssetPath,
			TEXT("modifier_not_revertible"),
			TEXT("At least one configured Animation Modifier cannot be reverted"),
			TEXT("Apply every configured modifier successfully before requesting revert."));
		Error->SetArrayField(TEXT("non_revertible_modifiers"), NonRevertible);
		Error->SetNumberField(TEXT("modifier_count"), Context.Modifiers.Num());
		return SerializeJsonResponse(Error);
	}

	const FString HashBefore = HashFileSha1(Context.PackageFilename);
	if (HashBefore.IsEmpty())
	{
		return SerializeJsonResponse(MakeHashReadError(
			AssetPath, TEXT("before"), Context.PackageFilename, false, false));
	}

#if WITH_EDITOR
	const FScopedTransaction Transaction(FText::FromString(
		FString::Printf(TEXT("NarshaMCP: revert animation modifiers on %s"), *AssetPath)));
	Context.Sequence->Modify();
#endif
	{
		UE::Anim::FApplyModifiersScope Scope(
			UE::Anim::FApplyModifiersScope::SuppressWarningAndError);
		for (int32 Index = Context.Modifiers.Num() - 1; Index >= 0; --Index)
		{
			Context.Modifiers[Index]->RevertFromAnimationSequence(Context.Sequence);
		}
	}

	int32 RevertedCount = 0;
	for (const UAnimationModifier* Modifier : Context.Modifiers)
	{
		if (Modifier && !Modifier->CanRevert(Context.Sequence))
		{
			++RevertedCount;
		}
	}
	if (RevertedCount != Context.Modifiers.Num())
	{
		TSharedRef<FJsonObject> Error = MakeModifierError(
			AssetPath,
			TEXT("revert_incomplete"),
			FString::Printf(TEXT("Reverted %d of %d configured Animation Modifiers"),
				RevertedCount, Context.Modifiers.Num()),
			TEXT("Inspect the modifier implementation and Editor log; dialogs were suppressed."));
		Error->SetNumberField(TEXT("modifier_count"), Context.Modifiers.Num());
		Error->SetNumberField(TEXT("changed_modifier_count"), RevertedCount);
		Error->SetArrayField(TEXT("modifier_classes"), ModifierClassValues(Context.Modifiers));
		AddUnsavedMutationDisclosure(Error, HashBefore);
		return SerializeJsonResponse(Error);
	}

	FString SaveError;
	if (!SaveModifierAsset(Context.Sequence, Context.PackageFilename, SaveError))
	{
		TSharedRef<FJsonObject> Error = MakeModifierError(
			AssetPath, TEXT("save_failed"), SaveError);
		AddFailedSaveMutationDisclosure(
			Error, Context.PackageFilename, HashBefore);
		return SerializeJsonResponse(Error);
	}

	const FString HashAfter = HashFileSha1(Context.PackageFilename);
	if (HashAfter.IsEmpty())
	{
		return SerializeJsonResponse(MakeHashReadError(
			AssetPath, TEXT("after"), Context.PackageFilename, true, true, HashBefore));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), AssetPath);
	AddReceiptFields(Result, Context, HashBefore, HashAfter, RevertedCount);
	return SerializeJsonResponse(Result);
}
