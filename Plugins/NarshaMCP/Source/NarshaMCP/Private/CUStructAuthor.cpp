// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #18415: Domain C++ authoring for reflection-blocked editor-only UserDefinedStruct edits.

#include "CUStructAuthor.h"
#include "CULog.h"
#include "CUPinTypeUtils.h"  // Issue #19009: shared MakePinType (was file-local here)

// Editor-only UDS authoring API (module UnrealEd; UEdGraphSchema_K2 in BlueprintGraph — both in Build.cs).
#include "Kismet2/StructureEditorUtils.h"
#include "StructUtils/UserDefinedStruct.h"
// Full definition of FStructVariableDescription (StructureEditorUtils.h only forward-declares it);
// we read VarGuid/FriendlyName/DefaultValue + call ToPinType() on it.
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "EdGraphSchema_K2.h"
#include "EdGraph/EdGraphPin.h"

#include "AssetRegistry/AssetRegistryModule.h"
// Issue #19714: asset-lifecycle ops (rename/duplicate/delete the UDS asset itself).
// AssetTools is an engine module (Developer/AssetTools), new PrivateDependency in Build.cs;
// ObjectTools comes with the already-linked UnrealEd.
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
#include "Editor.h" // GEditor->ResetTransaction (undo refs pin husks; see delete_struct)
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h" // TObjectIterator (PurgeStaleStructReinstanceHusks)
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	// MakePinType moved to CUPinTypeUtils.h (Issue #19009) — shared with
	// CURemoteLibrary::CreateBlueprintVariable so the type grammar cannot drift.
	using NarshaPinTypeUtils::MakePinType;

	/** Reverse map a pin type → an input-grammar token (for get_structure / verification echo). */
	FString PinTypeToToken(const FEdGraphPinType& Pin)
	{
		const FName C = Pin.PinCategory;
		if (C == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
		if (C == UEdGraphSchema_K2::PC_Int) return TEXT("int");
		if (C == UEdGraphSchema_K2::PC_Int64) return TEXT("int64");
		if (C == UEdGraphSchema_K2::PC_Real) return TEXT("float");
		if (C == UEdGraphSchema_K2::PC_String) return TEXT("string");
		if (C == UEdGraphSchema_K2::PC_Name) return TEXT("name");
		if (C == UEdGraphSchema_K2::PC_Text) return TEXT("text");
		if (C == UEdGraphSchema_K2::PC_Struct)
		{
			if (const UScriptStruct* SS = Cast<UScriptStruct>(Pin.PinSubCategoryObject.Get()))
			{
				return FString::Printf(TEXT("struct:%s"), *SS->GetName());
			}
			return TEXT("struct:?");
		}
		if (C == UEdGraphSchema_K2::PC_Object)
		{
			if (const UClass* Cls = Cast<UClass>(Pin.PinSubCategoryObject.Get()))
			{
				return FString::Printf(TEXT("object:%s"), *Cls->GetName());
			}
			return TEXT("object:?");
		}
		return C.ToString();
	}

	/** Locate a member's GUID by its friendly (display) name; returns an invalid FGuid on miss. */
	FGuid FindGuidByFriendlyName(UUserDefinedStruct* Struct, const FString& FriendlyName)
	{
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(Struct))
		{
			if (V.FriendlyName == FriendlyName)
			{
				return V.VarGuid;
			}
		}
		return FGuid();
	}

	/** Add one member with a friendly name + optional default; returns false + OutError on failure. */
	bool AddOneMember(UUserDefinedStruct* Struct, const FString& Name, const FString& TypeStr,
		const FString& DefaultValue, FString& OutError)
	{
		if (Name.IsEmpty())
		{
			OutError = TEXT("member 'name' is required");
			return false;
		}
		FEdGraphPinType PinType;
		if (!MakePinType(TypeStr, PinType, OutError))
		{
			return false;
		}

		// AddVariable auto-generates a GUID-suffixed name; capture the pre-set GUIDs to find the new one.
		TSet<FGuid> Before;
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(Struct))
		{
			Before.Add(V.VarGuid);
		}
		if (!FStructureEditorUtils::AddVariable(Struct, PinType))
		{
			OutError = FString::Printf(TEXT("AddVariable failed for '%s' (type %s)"), *Name, *TypeStr);
			return false;
		}
		FGuid NewGuid;
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(Struct))
		{
			if (!Before.Contains(V.VarGuid))
			{
				NewGuid = V.VarGuid;
				break;
			}
		}
		if (!NewGuid.IsValid())
		{
			OutError = FString::Printf(TEXT("could not resolve new member GUID for '%s'"), *Name);
			return false;
		}
		FStructureEditorUtils::RenameVariable(Struct, NewGuid, Name);
		if (!DefaultValue.IsEmpty())
		{
			// Engine validates the default string against the pin type; a rejected value returns false
			// and leaves the old default. Surface it as an error instead of falsely reporting success.
			if (!FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewGuid, DefaultValue))
			{
				OutError = FString::Printf(
					TEXT("default value '%s' is invalid for member '%s'"), *DefaultValue, *Name);
				return false;
			}
		}
		return true;
	}

	/** Populate Out.Fields + FieldCount from the struct's current member descriptions. */
	void DumpFields(UUserDefinedStruct* Struct, FCUStructAuthorResult& Out)
	{
		Out.Fields.Reset();
		for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(Struct))
		{
			FCUStructFieldInfo Info;
			Info.Name = V.FriendlyName;
			Info.Type = PinTypeToToken(V.ToPinType());
			Info.DefaultValue = V.DefaultValue;
			Out.Fields.Add(MoveTemp(Info));
		}
		Out.FieldCount = Out.Fields.Num();
	}

	/** Save the UDS package to disk; sets Out.SavedFilePath or Out.Error. */
	bool SaveStructPackage(UUserDefinedStruct* Struct, const FString& PackageName, FCUStructAuthorResult& Out)
	{
		FStructureEditorUtils::CompileStructure(Struct);
		Struct->MarkPackageDirty();

		const FString FileName =
			FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension());
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FileName), /*Tree=*/true);

		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Struct->GetOutermost(), Struct, *FileName, SaveArgs))
		{
			Out.Error = FString::Printf(TEXT("SavePackage failed for %s"), *FileName);
			return false;
		}
		Out.SavedFilePath = FileName;
		return true;
	}

	/**
	 * Issue #19714: force the registry scan non-cook commandlets skip. UE only auto-scans at
	 * startup for the editor and cook (AssetRegistry.cpp ShouldSearchAllAssetsAtStart);
	 * NarshaMCP.CUStructAuthor is not in [AssetRegistry] CommandletsUsingAR, so headless the
	 * registry is silently EMPTY — GetReferencers would return 0 (delete gate false-negatives)
	 * and RenameAssets would "fix up" zero referencers before deleting the old package.
	 * Synchronous full scan: multi-second on large projects, but no-op-cheap when already
	 * scanned (live editor / repeat ops in one commandlet run).
	 */
	void EnsureAssetRegistrySearched()
	{
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
			.Get().SearchAllAssets(/*bSynchronousSearch=*/true);
	}

	/**
	 * Issue #19714: every UDS compile (FStructureEditorUtils edits) parks the superseded struct
	 * version in the transient package as a STRUCT_REINST_* husk (UDSS_Duplicate) that INHERITS
	 * the asset's RF_Standalone (StaticDuplicateObject copies flags, UserDefinedStructureCompilerUtils
	 * CreateDuplicate), so the editor's keep-standalone GC never reclaims it. Each husk still holds
	 * its member FStructProperty references — after deleting a referencer struct, its husks keep
	 * "referencing" the referee in memory and the engine's safe-delete backstop refuses (observed
	 * live: 9 STRUCT_REINST husks of a deleted struct blocking its referee's delete). Un-pin them
	 * (clear RF_Standalone) and purge; anything genuinely referenced survives GC regardless of flags.
	 */
	void PurgeStaleStructReinstanceHusks()
	{
		for (TObjectIterator<UUserDefinedStruct> It; It; ++It)
		{
			UUserDefinedStruct* Candidate = *It;
			if (Candidate->GetOutermost() == GetTransientPackage()
				&& Candidate->Status == EUserDefinedStructureStatus::UDSS_Duplicate)
			{
				Candidate->ClearFlags(RF_Standalone);
			}
		}
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);
	}

	/**
	 * Package-level referencer names, excluding the asset's own package. Replica of the
	 * CUCountAssetReferencers pattern (CURemoteLibrary.cpp — a static local there, not shareable).
	 */
	int32 CountStructReferencers(UObject* Asset, TArray<FString>& OutNames)
	{
		OutNames.Reset();
		if (!Asset)
		{
			return 0;
		}
		const UPackage* Package = Asset->GetOutermost();
		if (!Package)
		{
			return 0;
		}
		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(Package->GetFName(), Referencers);
		for (const FName& Ref : Referencers)
		{
			if (Ref == Package->GetFName())
			{
				continue;
			}
			OutNames.Add(Ref.ToString());
		}
		return OutNames.Num();
	}
} // namespace

bool FCUStructAuthor::Execute(const FCUStructAuthorParams& In, FCUStructAuthorResult& Out)
{
	Out = FCUStructAuthorResult{};

	if (In.StructPackagePath.IsEmpty() || !FPackageName::IsValidLongPackageName(In.StructPackagePath))
	{
		Out.Error = FString::Printf(TEXT("Invalid long package name: '%s'"), *In.StructPackagePath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[StructAuthor] %s"), *Out.Error);
		return false;
	}
	const FString PackageName = In.StructPackagePath;
	const FString AssetName = FPackageName::GetShortName(PackageName);
	const FString Op = In.Operation.ToLower();

	// Parse the JSON spec (empty object for get_structure).
	TSharedPtr<FJsonObject> Spec = MakeShared<FJsonObject>();
	if (!In.SpecJson.IsEmpty())
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In.SpecJson);
		if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
		{
			Out.Error = TEXT("SpecJson parse failed");
			UE_LOG(LogNarshaMCP, Error, TEXT("[StructAuthor] %s"), *Out.Error);
			return false;
		}
	}

	UUserDefinedStruct* Struct = nullptr;

	if (Op == TEXT("create_struct"))
	{
		if (FPackageName::DoesPackageExist(PackageName))
		{
			UE_LOG(LogNarshaMCP, Warning, TEXT("[StructAuthor] Overwriting existing asset at %s"), *PackageName);
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			Out.Error = FString::Printf(TEXT("CreatePackage failed for %s"), *PackageName);
			return false;
		}
		Package->FullyLoad();

		// CreateUserDefinedStruct sets up the mandatory UUserDefinedStructEditorData + a default bool member.
		Struct = FStructureEditorUtils::CreateUserDefinedStruct(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		if (!Struct)
		{
			Out.Error = TEXT("CreateUserDefinedStruct returned null (UserDefinedStructEnabled false?)");
			return false;
		}

		const TArray<TSharedPtr<FJsonValue>>* Members = nullptr;
		if (Spec->TryGetArrayField(TEXT("members"), Members) && Members && Members->Num() > 0)
		{
			// Capture the auto-created default member(s) so they can be dropped after real ones are added
			// (a UDS must never be empty — add first, then remove the defaults).
			TArray<FGuid> DefaultGuids;
			for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(Struct))
			{
				DefaultGuids.Add(V.VarGuid);
			}
			bool bMemberFailed = false;
			for (const TSharedPtr<FJsonValue>& MV : *Members)
			{
				const TSharedPtr<FJsonObject> M = MV->AsObject();
				if (!M.IsValid())
				{
					Out.Error = TEXT("members[] entry is not an object");
					bMemberFailed = true;
					break;
				}
				if (!AddOneMember(Struct, M->GetStringField(TEXT("name")), M->GetStringField(TEXT("type")),
						M->HasField(TEXT("default")) ? M->GetStringField(TEXT("default")) : FString(), Out.Error))
				{
					bMemberFailed = true;
					break;
				}
			}
			if (bMemberFailed)
			{
				// Roll back everything added this call so a same-process retry (CreatePackage is
				// find-or-create) resumes from a clean state: remove all non-default members, keeping the
				// auto-created default so the struct never goes empty. Nothing was saved to disk.
				TArray<FGuid> Added;
				for (const FStructVariableDescription& V : FStructureEditorUtils::GetVarDesc(Struct))
				{
					if (!DefaultGuids.Contains(V.VarGuid))
					{
						Added.Add(V.VarGuid);
					}
				}
				for (const FGuid& G : Added)
				{
					FStructureEditorUtils::RemoveVariable(Struct, G);
				}
				return false;
			}
			for (const FGuid& G : DefaultGuids)
			{
				FStructureEditorUtils::RemoveVariable(Struct, G);
			}
		}

		FAssetRegistryModule::AssetCreated(Struct);
		if (!SaveStructPackage(Struct, PackageName, Out))
		{
			return false;
		}
	}
	else
	{
		// Edit / read ops load the existing UDS.
		Struct = LoadObject<UUserDefinedStruct>(nullptr, *(PackageName + TEXT(".") + AssetName));
		if (!Struct)
		{
			// #19714: idempotent delete-of-absent — a retry after a crash-following-delete must not
			// surface a spurious error (mirrors the #19625 idempotency discipline). A package that IS
			// on disk but failed to load stays a real error (corrupt asset), not a no-op.
			if (Op == TEXT("delete_struct") && !FPackageName::DoesPackageExist(PackageName))
			{
				Out.bSuccess = true;
				UE_LOG(LogNarshaMCP, Display,
					TEXT("[StructAuthor] delete_struct: %s already absent (no-op)"), *PackageName);
				return true;
			}
			Out.Error = FString::Printf(TEXT("UserDefinedStruct not found at %s"), *PackageName);
			UE_LOG(LogNarshaMCP, Error, TEXT("[StructAuthor] %s"), *Out.Error);
			return false;
		}

		if (Op == TEXT("add_member"))
		{
			const TSharedPtr<FJsonObject>* M = nullptr;
			if (!Spec->TryGetObjectField(TEXT("member"), M) || !M || !M->IsValid())
			{
				Out.Error = TEXT("add_member requires {\"member\":{name,type,default?}}");
				return false;
			}
			if (!AddOneMember(Struct, (*M)->GetStringField(TEXT("name")), (*M)->GetStringField(TEXT("type")),
					(*M)->HasField(TEXT("default")) ? (*M)->GetStringField(TEXT("default")) : FString(), Out.Error))
			{
				return false;
			}
			if (!SaveStructPackage(Struct, PackageName, Out)) return false;
		}
		else if (Op == TEXT("modify_member"))
		{
			const FString Target = Spec->GetStringField(TEXT("member_name"));
			const FGuid Guid = FindGuidByFriendlyName(Struct, Target);
			if (!Guid.IsValid())
			{
				Out.Error = FString::Printf(TEXT("member '%s' not found"), *Target);
				return false;
			}
			if (Spec->HasField(TEXT("type")))
			{
				FEdGraphPinType NewType;
				if (!MakePinType(Spec->GetStringField(TEXT("type")), NewType, Out.Error)) return false;
				// ChangeVariableType returns false (no mutation) if the engine rejects the type change.
				if (!FStructureEditorUtils::ChangeVariableType(Struct, Guid, NewType))
				{
					Out.Error = FString::Printf(TEXT("type change rejected for member '%s'"), *Target);
					return false;
				}
			}
			if (Spec->HasField(TEXT("default")))
			{
				const FString NewDefault = Spec->GetStringField(TEXT("default"));
				if (!FStructureEditorUtils::ChangeVariableDefaultValue(Struct, Guid, NewDefault))
				{
					Out.Error = FString::Printf(
						TEXT("default value '%s' is invalid for member '%s'"), *NewDefault, *Target);
					return false;
				}
			}
			if (!SaveStructPackage(Struct, PackageName, Out)) return false;
		}
		else if (Op == TEXT("remove_member"))
		{
			const FString Target = Spec->GetStringField(TEXT("member_name"));
			const FGuid Guid = FindGuidByFriendlyName(Struct, Target);
			if (!Guid.IsValid())
			{
				Out.Error = FString::Printf(TEXT("member '%s' not found"), *Target);
				return false;
			}
			if (FStructureEditorUtils::GetVarDesc(Struct).Num() <= 1)
			{
				Out.Error = TEXT("cannot remove the last member (a UserDefinedStruct must have >= 1 member)");
				return false;
			}
			FStructureEditorUtils::RemoveVariable(Struct, Guid);
			if (!SaveStructPackage(Struct, PackageName, Out)) return false;
		}
		else if (Op == TEXT("rename_member")) // Issue #19625
		{
			const FString Target = Spec->GetStringField(TEXT("member_name"));
			const FString NewName = Spec->GetStringField(TEXT("new_name"));
			if (NewName.IsEmpty())
			{
				Out.Error = TEXT("rename_member requires a non-empty 'new_name'");
				return false;
			}
			const FGuid Guid = FindGuidByFriendlyName(Struct, Target);
			if (!Guid.IsValid())
			{
				Out.Error = FString::Printf(TEXT("member '%s' not found"), *Target);
				return false;
			}
			// RenameVariable returns false BOTH for a rejected rename (duplicate/invalid name) AND for
			// an idempotent no-op — renaming a member to its own current name fails the engine's
			// IsUniqueVariableFriendlyName check and returns false without mutating. Distinguish the two
			// by the resulting friendly name so an idempotent retry (Alpha already renamed to Health) is
			// not surfaced as a spurious error. Compare CASE-SENSITIVELY: FString::operator!= is
			// case-insensitive (Stricmp), so a genuinely-rejected case-only rename (Alpha -> alpha)
			// would otherwise read as "already achieved" and report a fake success. Mirrors the
			// ESearchCase::CaseSensitive check in set_member_metadata below. (Verified vs
			// StructureEditorUtils.cpp on 5.7 + 5.8.)
			if (!FStructureEditorUtils::RenameVariable(Struct, Guid, NewName)
				&& !FStructureEditorUtils::GetVariableFriendlyName(Struct, Guid).Equals(NewName, ESearchCase::CaseSensitive))
			{
				Out.Error = FString::Printf(
					TEXT("rename rejected for '%s' -> '%s' (duplicate or invalid name?)"), *Target, *NewName);
				return false;
			}
			if (!SaveStructPackage(Struct, PackageName, Out)) return false;
		}
		else if (Op == TEXT("reorder_members")) // Issue #19625
		{
			const TArray<TSharedPtr<FJsonValue>>* OrderArr = nullptr;
			if (!Spec->TryGetArrayField(TEXT("member_order"), OrderArr) || !OrderArr || OrderArr->Num() == 0)
			{
				Out.Error = TEXT("reorder_members requires a non-empty 'member_order' array");
				return false;
			}
			// Require a FULL permutation of the current members: same count, every name known, no dup.
			// Snapshot the count before any move (GetVarDesc returns a live reference the moves mutate).
			const int32 MemberCount = FStructureEditorUtils::GetVarDesc(Struct).Num();
			if (OrderArr->Num() != MemberCount)
			{
				Out.Error = FString::Printf(
					TEXT("member_order must list all %d members (got %d)"), MemberCount, OrderArr->Num());
				return false;
			}
			TArray<FGuid> DesiredGuids;
			TSet<FGuid> Unique;
			for (const TSharedPtr<FJsonValue>& NameVal : *OrderArr)
			{
				const FString Name = NameVal->AsString();
				const FGuid G = FindGuidByFriendlyName(Struct, Name);
				if (!G.IsValid())
				{
					Out.Error = FString::Printf(TEXT("member_order names unknown member '%s'"), *Name);
					return false;
				}
				bool bDup = false;
				Unique.Add(G, &bDup);
				if (bDup)
				{
					Out.Error = FString::Printf(TEXT("member_order lists '%s' more than once"), *Name);
					return false;
				}
				DesiredGuids.Add(G);
			}
			// Apply: move each member to sit immediately below its predecessor in the desired order.
			// MoveVariable is relative (above/below another var), so this selection sequence yields the
			// exact target order regardless of the starting arrangement. A no-op move returns false
			// harmlessly; correctness is confirmed by the final-order verify below.
			for (int32 i = 1; i < DesiredGuids.Num(); ++i)
			{
				FStructureEditorUtils::MoveVariable(
					Struct, DesiredGuids[i], DesiredGuids[i - 1], FStructureEditorUtils::PositionBelow);
			}
			// Verify the live order now matches the requested order (guards against any engine quirk).
			const TArray<FStructVariableDescription>& AfterVars = FStructureEditorUtils::GetVarDesc(Struct);
			bool bOrdered = (AfterVars.Num() == DesiredGuids.Num());
			for (int32 i = 0; bOrdered && i < AfterVars.Num(); ++i)
			{
				if (AfterVars[i].VarGuid != DesiredGuids[i])
				{
					bOrdered = false;
				}
			}
			if (!bOrdered)
			{
				Out.Error = TEXT("reorder failed to achieve the requested member order");
				return false;
			}
			if (!SaveStructPackage(Struct, PackageName, Out)) return false;
		}
		else if (Op == TEXT("set_member_metadata")) // Issue #19625
		{
			const FString Target = Spec->GetStringField(TEXT("member_name"));
			const FGuid Guid = FindGuidByFriendlyName(Struct, Target);
			if (!Guid.IsValid())
			{
				Out.Error = FString::Printf(TEXT("member '%s' not found"), *Target);
				return false;
			}
			const TSharedPtr<FJsonObject>* Meta = nullptr;
			if (!Spec->TryGetObjectField(TEXT("metadata"), Meta) || !Meta || !(*Meta).IsValid()
				|| (*Meta)->Values.Num() == 0)
			{
				Out.Error = TEXT("set_member_metadata requires a non-empty 'metadata' object");
				return false;
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Kvp : (*Meta)->Values)
			{
				// Values arrive as strings from the Rust layer; coerce any stray scalar to string.
				FString ValueStr;
				if (!Kvp.Value.IsValid() || !Kvp.Value->TryGetString(ValueStr))
				{
					ValueStr = Kvp.Value.IsValid() ? Kvp.Value->AsString() : FString();
				}
				// SetMetaData returns false BOTH on real failure AND on an idempotent no-op — setting a
				// key to a value it already holds (or unsetting an already-absent key) returns false
				// without mutating. Only treat it as an error when the stored value does NOT already
				// match the request, so an idempotent retry does not surface a spurious failure.
				// (Verified vs StructureEditorUtils.cpp on 5.7 + 5.8.)
				if (!FStructureEditorUtils::SetMetaData(Struct, Guid, FName(*Kvp.Key), ValueStr))
				{
					const FString* StoredValue =
						FStructureEditorUtils::GetMetaData(Struct, Guid, FName(*Kvp.Key));
					const bool bAlreadyDesired = ValueStr.IsEmpty()
						? (StoredValue == nullptr)
						: (StoredValue && StoredValue->Equals(ValueStr, ESearchCase::CaseSensitive));
					if (!bAlreadyDesired)
					{
						Out.Error = FString::Printf(
							TEXT("failed to set metadata '%s' on member '%s'"), *Kvp.Key, *Target);
						return false;
					}
				}
			}
			if (!SaveStructPackage(Struct, PackageName, Out)) return false;
		}
		else if (Op == TEXT("rename_struct")) // Issue #19714
		{
			// Belt-and-braces for a future live-editor UFUNCTION path: the commandlet already runs
			// -unattended, but engine asset ops consult GIsRunningUnattendedScript too (the idiom
			// opening every UEditorAssetSubsystem op).
			TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

			const FString Destination = Spec->GetStringField(TEXT("destination_path"));
			if (!FPackageName::IsValidLongPackageName(Destination))
			{
				Out.Error = FString::Printf(
					TEXT("rename_struct requires a valid 'destination_path' long package name (got '%s')"),
					*Destination);
				return false;
			}
			if (Destination.Equals(PackageName, ESearchCase::CaseSensitive))
			{
				// Idempotent no-op (#19625 discipline): renaming to the exact current path succeeds
				// without touching the engine — the tail dumps the unchanged struct.
				Out.NewPath = PackageName;
				Out.SavedFilePath = FPackageName::LongPackageNameToFilename(
					PackageName, FPackageName::GetAssetPackageExtension());
			}
			else
			{
				// A case-ONLY difference is a real rename: the engine has a dedicated case-change path
				// (AssetRenameManager temp-rename dance). Skip the collision pre-check for it — on
				// case-insensitive content filesystems DoesPackageExist(destination) finds the SOURCE
				// package and would falsely refuse. On a case-SENSITIVE filesystem, where the case-only
				// destination could be a genuinely distinct existing asset, skipping the pre-check is
				// still lossless: the engine's own rename path refuses an occupied destination
				// (ObjectTools RenameSingleObject -> "An object named '{0}' already exists",
				// ObjectTools.cpp:4495 on 5.8) — the failure just surfaces through the generic
				// RenameAssets error below instead of the specific message.
				const bool bCaseOnlyRename = Destination.Equals(PackageName, ESearchCase::IgnoreCase);
				if (!bCaseOnlyRename && FPackageName::DoesPackageExist(Destination))
				{
					Out.Error = FString::Printf(
						TEXT("rename destination '%s' already exists"), *Destination);
					return false;
				}

				// MANDATORY headless: without this the registry is empty in commandlet context, so
				// RenameAssets would fix up ZERO referencers and delete the old package -> dangling imports.
				EnsureAssetRegistrySearched();

				TArray<FAssetRenameData> Renames;
				Renames.Emplace(Struct, FPackageName::GetLongPackagePath(Destination),
					FPackageName::GetShortName(Destination));
				IAssetTools& AssetTools =
					FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
				if (!AssetTools.RenameAssets(Renames))
				{
					Out.Error = FString::Printf(
						TEXT("RenameAssets failed for '%s' -> '%s' (CDO references (hard or soft), ")
						TEXT("read-only packages, or an invalid destination? see LogAssetTools)"),
						*PackageName, *Destination);
					return false;
				}
				// No re-save: the rename manager saved the renamed package and every rewritten
				// referencer itself, and the member layout is unchanged (no CompileStructure needed).
				// Referencers are fixed up; a redirector remains at the old path only if some could
				// not be rewritten (unloaded maps, read-only, non-local SCC state).
				Out.NewPath = Destination;
				Out.SavedFilePath = FPackageName::LongPackageNameToFilename(
					Destination, FPackageName::GetAssetPackageExtension());
			}
			// Falls through: the tail dumps the same (now renamed) UObject.
		}
		else if (Op == TEXT("duplicate_struct")) // Issue #19714
		{
			TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

			const FString Destination = Spec->GetStringField(TEXT("destination_path"));
			if (!FPackageName::IsValidLongPackageName(Destination))
			{
				Out.Error = FString::Printf(
					TEXT("duplicate_struct requires a valid 'destination_path' long package name (got '%s')"),
					*Destination);
				return false;
			}
			// Deliberately NOT idempotent: an existing destination (including the source itself) is
			// refused — "success" on a pre-existing destination could bless a stale or entirely
			// different asset. Epic's own headless wrapper does this pre-check because the engine's
			// in-memory collision guard cannot see unloaded on-disk assets (it would silently
			// overwrite them at save).
			if (FPackageName::DoesPackageExist(Destination))
			{
				Out.Error = FString::Printf(
					TEXT("duplicate destination '%s' already exists"), *Destination);
				return false;
			}

			IAssetTools& AssetTools =
				FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
			UObject* NewObj = AssetTools.DuplicateAsset(FPackageName::GetShortName(Destination),
				FPackageName::GetLongPackagePath(Destination), Struct);
			UUserDefinedStruct* NewStruct = Cast<UUserDefinedStruct>(NewObj);
			if (!NewStruct)
			{
				Out.Error = FString::Printf(
					TEXT("DuplicateAsset returned null for '%s' -> '%s'"), *PackageName, *Destination);
				return false;
			}
			// No manual AssetCreated: ObjectTools::DuplicateSingleObject already notified the registry
			// unconditionally — a second call would double-broadcast AssetAdded.
			// The engine saves the copy only under source control; save it ourselves (this also
			// CompileStructure()s the copy).
			if (!SaveStructPackage(NewStruct, Destination, Out))
			{
				return false;
			}
			Out.NewPath = Destination;
			Struct = NewStruct; // the tail dumps the COPY
		}
		else if (Op == TEXT("delete_struct")) // Issue #19714
		{
			TGuardValue<bool> UnattendedScriptGuard(GIsRunningUnattendedScript, true);

			// Live-editor context: the undo/transaction buffer holds strong refs from earlier
			// FStructureEditorUtils edits (create/add_member/rename… all open FScopedTransactions).
			// Those refs keep struct husks alive through the engine's delete-time GC, which then
			// (a) leaves a findable husk at the old path and (b) refuses a follow-up delete of an
			// asset the husk references (in-memory referencer check). The editor's own delete flow
			// resets the transaction buffer in exactly this situation; do the same. No-op headless
			// (commandlets have no transaction buffer).
			if (GEditor && GEditor->Trans)
			{
				GEditor->ResetTransaction(NSLOCTEXT("NarshaMCP", "StructAuthorDeleteReset",
					"delete_struct: clearing undo history before asset delete"));
			}

			// Un-pin superseded STRUCT_REINST_* compiler husks so they can't masquerade as
			// in-memory referencers of this struct's referees (see helper doc).
			PurgeStaleStructReinstanceHusks();

			// MANDATORY headless (same trap as rename_struct): with an unscanned registry the
			// referencer gate would false-negative and delete a still-referenced asset.
			EnsureAssetRegistrySearched();

			if (CountStructReferencers(Struct, Out.Referencers) > 0)
			{
				Out.Error = FString::Printf(
					TEXT("cannot delete '%s': referenced by %d asset(s): %s"),
					*PackageName, Out.Referencers.Num(), *FString::Join(Out.Referencers, TEXT(", ")));
				return false;
			}

			TArray<UObject*> ToDelete;
			ToDelete.Add(Struct);
			const int32 Deleted = ObjectTools::DeleteObjects(ToDelete, /*bShowConfirmation=*/false);
			if (Deleted != 1)
			{
				// FAssetDeleteModel's own referenced-object backstop lands here (0 deleted, no
				// dialog) — but without naming a culprit. Gather the in-memory referencers so the
				// error is actionable (mirrors the on-disk referencer gate above).
				UObject* Target = Struct;
				FReferencerInformationList MemRefs;
				FString RefNames;
				if (IsReferenced(Target, GARBAGE_COLLECTION_KEEPFLAGS,
						EInternalObjectFlags_GarbageCollectionKeepFlags,
						/*bCheckSubObjects=*/true, &MemRefs))
				{
					for (const FReferencerInformation& Ref : MemRefs.ExternalReferences)
					{
						if (Ref.Referencer)
						{
							if (!RefNames.IsEmpty())
							{
								RefNames += TEXT(", ");
							}
							RefNames += Ref.Referencer->GetPathName();
							Out.Referencers.Add(Ref.Referencer->GetPathName());
						}
					}
				}
				Out.Error = FString::Printf(
					TEXT("delete failed for '%s' (%d of 1 deleted — in-memory referencers: %s)"),
					*PackageName, Deleted, RefNames.IsEmpty() ? TEXT("none found / read-only package?") : *RefNames);
				return false;
			}

			// Full-purge GC so the delete COMPLETES before we return (safe-delete only clears
			// RF_Standalone|RF_Public and relies on GC; the engine's internal cleanup GC already
			// ran, but make the caller-visible postcondition — gone from memory AND disk —
			// deterministic for same-process create→delete→recreate flows).
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS, /*bPerformFullPurge=*/true);

			// EARLY RETURN — the object is destroyed; DumpFields on it would read freed memory.
			Out.bSuccess = true;
			UE_LOG(LogNarshaMCP, Display, TEXT("[StructAuthor] delete_struct removed %s"), *PackageName);
			return true;
		}
		else if (Op == TEXT("get_structure"))
		{
			// read-only — no save
		}
		else
		{
			Out.Error = FString::Printf(TEXT("unknown operation '%s'"), *In.Operation);
			return false;
		}
	}

	DumpFields(Struct, Out);
	Out.bSuccess = true;
	UE_LOG(LogNarshaMCP, Display, TEXT("[StructAuthor] %s on %s -> %d field(s)"),
		*Op, *PackageName, Out.FieldCount);
	return true;
}
