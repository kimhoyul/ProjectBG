// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUBehaviorTreeWriteFinalize.h"
#include "CULog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "DiffUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "PackageTools.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace CUBehaviorTreeWriteFinalize
{
namespace
{
	struct FFileSnapshot
	{
		FString Filename;
		bool bExisted = false;
		TArray<uint8> Bytes;
	};

	struct FCanonicalSignature
	{
		FString EditorGraph;
		FString RuntimeTree;
		FString Blackboard;
		FString BlackboardReference;
	};

#if WITH_DEV_AUTOMATION_TESTS
	ETestFailureMode GTestFailureMode = ETestFailureMode::None;
#endif

	bool ShouldFailCompile()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::Compile;
#else
		return false;
#endif
	}

	bool ShouldFailBlackboardSave()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::BlackboardSave;
#else
		return false;
#endif
	}

	bool ShouldFailBehaviorTreeSave()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::BehaviorTreeSave;
#else
		return false;
#endif
	}

	bool ShouldFailReload()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::Reload;
#else
		return false;
#endif
	}

	bool ShouldPerturbEditorSignature()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::EditorSignature;
#else
		return false;
#endif
	}

	bool ShouldPerturbRuntimeSignature()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::RuntimeSignature;
#else
		return false;
#endif
	}

	bool ShouldPerturbBlackboardSignature()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::BlackboardSignature;
#else
		return false;
#endif
	}

	bool ShouldPerturbRebuiltSignature()
	{
#if WITH_DEV_AUTOMATION_TESTS
		return GTestFailureMode == ETestFailureMode::RebuiltSignature;
#else
		return false;
#endif
	}

	FString MakeSerializablePropertySignature(const UObject* Object, TSet<const UObject*>& VisitedObjects);

	FString MakeCanonicalPropertyValue(
		const FProperty* Property,
		const void* ValuePtr,
		const UObject* Owner,
		TSet<const UObject*>& VisitedObjects)
	{
		if (!Property || !ValuePtr)
		{
			return TEXT("<none>");
		}

		// FSoftObjectProperty derives from FObjectPropertyBase, but its object getter resolves the
		// pointer and therefore returns null for a valid unloaded soft path. Preserve the stored path
		// without loading it so distinct durable soft object/class references cannot collapse to the
		// same <null> signature.
		if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FSoftObjectPtr SoftObject = SoftObjectProperty->GetPropertyValue(ValuePtr);
			const FSoftObjectPath SoftPath = SoftObject.ToSoftObjectPath();
			return SoftPath.IsNull() ? TEXT("<null>") : SoftPath.ToString();
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const UObject* Referenced = ObjectProperty->GetObjectPropertyValue(ValuePtr);
			if (!Referenced)
			{
				return TEXT("<null>");
			}

			const UObject* Root = Owner ? Owner->GetOutermost() : nullptr;
			const FString StablePath = Root && Referenced->IsIn(Root)
				? Referenced->GetPathName(Root)
				: Referenced->GetPathName();
			FString Result = FString::Printf(TEXT("%s|%s"), *Referenced->GetClass()->GetPathName(), *StablePath);
			if (Property->HasAnyPropertyFlags(CPF_InstancedReference)
				&& Root
				&& Referenced->IsIn(Root))
			{
				// An instanced object's path identifies only the export. Its owned serialized fields
				// (for example UAIDataProvider_QueryParams::ParamName) are runtime semantics too.
				// Recurse into them while the visited stack breaks Outer/back-reference cycles.
				Result += TEXT("|{") + MakeSerializablePropertySignature(Referenced, VisitedObjects) + TEXT("}");
			}
			return Result;
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			FScriptArrayHelper Helper(ArrayProperty, ValuePtr);
			TArray<FString> Elements;
			Elements.Reserve(Helper.Num());
			for (int32 Index = 0; Index < Helper.Num(); ++Index)
			{
				Elements.Add(MakeCanonicalPropertyValue(ArrayProperty->Inner, Helper.GetRawPtr(Index), Owner, VisitedObjects));
			}
			return TEXT("[") + FString::Join(Elements, TEXT(",")) + TEXT("]");
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			FScriptSetHelper Helper(SetProperty, ValuePtr);
			TArray<FString> Elements;
			Elements.Reserve(Helper.Num());
			for (FScriptSetHelper::FIterator It = Helper.CreateIterator(); It; ++It)
			{
				Elements.Add(MakeCanonicalPropertyValue(SetProperty->ElementProp, Helper.GetElementPtr(It), Owner, VisitedObjects));
			}
			Elements.Sort();
			return TEXT("{") + FString::Join(Elements, TEXT(",")) + TEXT("}");
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			FScriptMapHelper Helper(MapProperty, ValuePtr);
			TArray<FString> Entries;
			Entries.Reserve(Helper.Num());
			for (FScriptMapHelper::FIterator It = Helper.CreateIterator(); It; ++It)
			{
				Entries.Add(FString::Printf(
					TEXT("%s=>%s"),
					*MakeCanonicalPropertyValue(MapProperty->KeyProp, Helper.GetKeyPtr(It), Owner, VisitedObjects),
					*MakeCanonicalPropertyValue(MapProperty->ValueProp, Helper.GetValuePtr(It), Owner, VisitedObjects)));
			}
			Entries.Sort();
			return TEXT("{") + FString::Join(Entries, TEXT(",")) + TEXT("}");
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			TArray<FString> Fields;
			for (TFieldIterator<FProperty> It(StructProperty->Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				const FProperty* Field = *It;
				if (Field->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_SkipSerialization | CPF_Deprecated))
				{
					continue;
				}

				TArray<FString> Values;
				Values.Reserve(Field->ArrayDim);
				for (int32 ArrayIndex = 0; ArrayIndex < Field->ArrayDim; ++ArrayIndex)
				{
					Values.Add(MakeCanonicalPropertyValue(
						Field,
						Field->ContainerPtrToValuePtr<void>(ValuePtr, ArrayIndex),
						Owner,
						VisitedObjects));
				}
				Fields.Add(FString::Printf(TEXT("%s=%s"), *Field->GetName(), *FString::Join(Values, TEXT("|"))));
			}
			Fields.Sort();
			return TEXT("(") + FString::Join(Fields, TEXT(";")) + TEXT(")");
		}

		FString Value;
		Property->ExportTextItem_Direct(
			Value,
			ValuePtr,
			nullptr,
			const_cast<UObject*>(Owner),
			PPF_Delimited | PPF_ExportsNotFullyQualified,
			Owner ? Owner->GetOutermost() : nullptr);
		Value.ReplaceInline(TEXT("\r"), TEXT("\\r"));
		Value.ReplaceInline(TEXT("\n"), TEXT("\\n"));
		return Value;
	}

	FString MakeSerializablePropertySignature(const UObject* Object, TSet<const UObject*>& VisitedObjects)
	{
		if (!Object)
		{
			return TEXT("<none>");
		}
		if (VisitedObjects.Contains(Object))
		{
			return FString::Printf(TEXT("<cycle:%s>"), *Object->GetPathName(Object->GetOutermost()));
		}
		VisitedObjects.Add(Object);
		ON_SCOPE_EXIT
		{
			VisitedObjects.Remove(Object);
		};

		TArray<FString> Properties;
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			// ApplyProperties accepts any reflected property that ImportText can mutate, including
			// runtime-affecting UPROPERTY() fields without CPF_Edit (for example
			// UBTService_DefaultFocus::FocusPriority). The receipt must cover that same domain.
			if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_NonPIEDuplicateTransient | CPF_SkipSerialization | CPF_Deprecated))
			{
				continue;
			}

			TArray<FString> Values;
			Values.Reserve(Property->ArrayDim);
			for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
			{
				Values.Add(MakeCanonicalPropertyValue(
					Property,
					Property->ContainerPtrToValuePtr<void>(Object, ArrayIndex),
					Object,
					VisitedObjects));
			}
			Properties.Add(FString::Printf(TEXT("%s=%s"), *Property->GetName(), *FString::Join(Values, TEXT("|"))));
		}
		Properties.Sort();
		return FString::Join(Properties, TEXT(";"));
	}

	FString MakeSerializablePropertySignature(const UObject* Object)
	{
		TSet<const UObject*> VisitedObjects;
		return MakeSerializablePropertySignature(Object, VisitedObjects);
	}

	FString MakeNodeKey(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return TEXT("<null>");
		}

		const FString StableId = Node->NodeGuid.IsValid()
			? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower)
			: FString::Printf(TEXT("%s@%d,%d"), *Node->GetName(), Node->NodePosX, Node->NodePosY);
		return FString::Printf(TEXT("%s|%s"), *Node->GetClass()->GetPathName(), *StableId);
	}

	FString MakeEditorGraphSignature(const UBehaviorTreeGraph* Graph)
	{
		if (!Graph)
		{
			return FString();
		}

		TArray<FString> Nodes;
		TArray<FString> Links;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node)
			{
				continue;
			}

			const FString NodeKey = MakeNodeKey(Node);
			const UBehaviorTreeGraphNode* BehaviorNode = Cast<UBehaviorTreeGraphNode>(Node);
			const UBTNode* NodeInstance = BehaviorNode ? Cast<UBTNode>(BehaviorNode->NodeInstance) : nullptr;
			Nodes.Add(FString::Printf(
				TEXT("N|%s|%d|%d|%s|%s"),
				*NodeKey,
				Node->NodePosX,
				Node->NodePosY,
				NodeInstance ? *NodeInstance->GetClass()->GetPathName() : TEXT("<none>"),
				*MakeSerializablePropertySignature(NodeInstance)));

			if (BehaviorNode)
			{
				for (int32 Index = 0; Index < BehaviorNode->Decorators.Num(); ++Index)
				{
					const UBehaviorTreeGraphNode* SubNode = BehaviorNode->Decorators[Index];
					const UBTNode* SubInstance = SubNode ? Cast<UBTNode>(SubNode->NodeInstance) : nullptr;
					Nodes.Add(FString::Printf(
						TEXT("D|%s|%d|%s|%s|%s"),
						*NodeKey,
						Index,
						SubNode ? *SubNode->GetClass()->GetPathName() : TEXT("<null>"),
						SubInstance ? *SubInstance->GetClass()->GetPathName() : TEXT("<none>"),
						*MakeSerializablePropertySignature(SubInstance)));
				}

				for (int32 Index = 0; Index < BehaviorNode->Services.Num(); ++Index)
				{
					const UBehaviorTreeGraphNode* SubNode = BehaviorNode->Services[Index];
					const UBTNode* SubInstance = SubNode ? Cast<UBTNode>(SubNode->NodeInstance) : nullptr;
					Nodes.Add(FString::Printf(
						TEXT("S|%s|%d|%s|%s|%s"),
						*NodeKey,
						Index,
						SubNode ? *SubNode->GetClass()->GetPathName() : TEXT("<null>"),
						SubInstance ? *SubInstance->GetClass()->GetPathName() : TEXT("<none>"),
						*MakeSerializablePropertySignature(SubInstance)));
				}
			}

			for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
			{
				const UEdGraphPin* Pin = Node->Pins[PinIndex];
				if (!Pin)
				{
					continue;
				}
				for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (!LinkedPin || !LinkedPin->GetOwningNode())
					{
						continue;
					}
					Links.Add(FString::Printf(
						TEXT("L|%s|%d|%s|%s"),
						*NodeKey,
						PinIndex,
						*Pin->PinName.ToString(),
						*MakeNodeKey(LinkedPin->GetOwningNode())));
				}
			}
		}

		Nodes.Sort();
		Links.Sort();
		return FString::Join(Nodes, TEXT("\n")) + TEXT("\n--links--\n") + FString::Join(Links, TEXT("\n"));
	}

	void AppendRuntimeNodeSignature(
		const UBTNode* Node,
		const FString& Path,
		TSet<const UBTNode*>& Visited,
		TArray<FString>& Entries)
	{
		if (!Node)
		{
			Entries.Add(Path + TEXT("|<null>"));
			return;
		}

		if (Visited.Contains(Node))
		{
			Entries.Add(Path + TEXT("|<cycle>"));
			return;
		}
		Visited.Add(Node);

		Entries.Add(FString::Printf(
			TEXT("%s|%s|%s"),
			*Path,
			*Node->GetClass()->GetPathName(),
			*MakeSerializablePropertySignature(Node)));

		if (const UBTTaskNode* Task = Cast<UBTTaskNode>(Node))
		{
			for (int32 ServiceIndex = 0; ServiceIndex < Task->Services.Num(); ++ServiceIndex)
			{
				const UBTService* Service = Task->Services[ServiceIndex];
				Entries.Add(FString::Printf(
					TEXT("%s/task-service/%d|%s|%s"),
					*Path,
					ServiceIndex,
					Service ? *Service->GetClass()->GetPathName() : TEXT("<null>"),
					*MakeSerializablePropertySignature(Service)));
			}
		}

		const UBTCompositeNode* Composite = Cast<UBTCompositeNode>(Node);
		if (Composite)
		{
			for (int32 ServiceIndex = 0; ServiceIndex < Composite->Services.Num(); ++ServiceIndex)
			{
				const UBTService* Service = Composite->Services[ServiceIndex];
				Entries.Add(FString::Printf(
					TEXT("%s/service/%d|%s|%s"),
					*Path,
					ServiceIndex,
					Service ? *Service->GetClass()->GetPathName() : TEXT("<null>"),
					*MakeSerializablePropertySignature(Service)));
			}
			for (int32 ChildIndex = 0; ChildIndex < Composite->GetChildrenNum(); ++ChildIndex)
			{
				const FBTCompositeChild& Child = Composite->Children[ChildIndex];
				for (int32 DecoratorIndex = 0; DecoratorIndex < Child.Decorators.Num(); ++DecoratorIndex)
				{
					const UBTDecorator* Decorator = Child.Decorators[DecoratorIndex];
					Entries.Add(FString::Printf(
						TEXT("%s/%d/decorator/%d|%s|%s"),
						*Path,
						ChildIndex,
						DecoratorIndex,
						Decorator ? *Decorator->GetClass()->GetPathName() : TEXT("<null>"),
						*MakeSerializablePropertySignature(Decorator)));
				}
				for (int32 OpIndex = 0; OpIndex < Child.DecoratorOps.Num(); ++OpIndex)
				{
					const FBTDecoratorLogic& Op = Child.DecoratorOps[OpIndex];
					Entries.Add(FString::Printf(
						TEXT("%s/%d/decorator-op/%d|%d|%d"),
						*Path,
						ChildIndex,
						OpIndex,
						static_cast<int32>(Op.Operation.GetValue()),
						static_cast<int32>(Op.Number)));
				}
				AppendRuntimeNodeSignature(
					Child.ChildComposite ? static_cast<const UBTNode*>(Child.ChildComposite.Get()) : Child.ChildTask.Get(),
					FString::Printf(TEXT("%s/%d"), *Path, ChildIndex),
					Visited,
					Entries);
			}
		}
	}

	FString MakeRuntimeTreeSignature(const UBehaviorTree* BehaviorTree)
	{
		TArray<FString> Entries;
		if (BehaviorTree)
		{
			for (int32 DecoratorIndex = 0; DecoratorIndex < BehaviorTree->RootDecorators.Num(); ++DecoratorIndex)
			{
				const UBTDecorator* Decorator = BehaviorTree->RootDecorators[DecoratorIndex];
				Entries.Add(FString::Printf(
					TEXT("root-decorator/%d|%s|%s"),
					DecoratorIndex,
					Decorator ? *Decorator->GetClass()->GetPathName() : TEXT("<null>"),
					*MakeSerializablePropertySignature(Decorator)));
			}
			for (int32 OpIndex = 0; OpIndex < BehaviorTree->RootDecoratorOps.Num(); ++OpIndex)
			{
				const FBTDecoratorLogic& Op = BehaviorTree->RootDecoratorOps[OpIndex];
				Entries.Add(FString::Printf(
					TEXT("root-decorator-op/%d|%d|%d"),
					OpIndex,
					static_cast<int32>(Op.Operation.GetValue()),
					static_cast<int32>(Op.Number)));
			}
		}
		TSet<const UBTNode*> Visited;
		AppendRuntimeNodeSignature(BehaviorTree ? BehaviorTree->RootNode : nullptr, TEXT("root"), Visited, Entries);
		return FString::Join(Entries, TEXT("\n"));
	}

	FString MakeBlackboardSignature(const UBlackboardData* Blackboard)
	{
		if (!Blackboard)
		{
			return TEXT("<none>");
		}

		TArray<FString> Entries;
		const TArray<FBlackboardEntry>& Keys = Blackboard->GetKeys();
		Entries.Reserve(Keys.Num());
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			const FBlackboardEntry& Entry = Keys[Index];
			Entries.Add(FString::Printf(
				TEXT("%d|%s|%s|%s"),
				Index,
				*Entry.EntryName.ToString(),
				Entry.KeyType ? *Entry.KeyType->GetClass()->GetPathName() : TEXT("<null>"),
				*MakeSerializablePropertySignature(Entry.KeyType)));
		}
		return FString::Join(Entries, TEXT("\n"));
	}

	FString MakeBlackboardReferenceSignature(const UBehaviorTree* BehaviorTree)
	{
		const UBlackboardData* Blackboard = BehaviorTree ? BehaviorTree->BlackboardAsset.Get() : nullptr;
		if (!Blackboard)
		{
			return TEXT("<none>");
		}

		const UPackage* Package = Blackboard->GetOutermost();
		if (!Package || Package == GetTransientPackage() || FPackageName::IsTempPackage(Package->GetName()))
		{
			// Transient Blackboard references cannot survive SavePackage. Treat them as the
			// same serialized state as the null reference observed by an isolated reload.
			return TEXT("<none>");
		}
		return Blackboard->GetPathName();
	}

	FCanonicalSignature CaptureSignature(const UBehaviorTree* BehaviorTree, const UBlackboardData* Blackboard)
	{
		FCanonicalSignature Result;
		Result.EditorGraph = MakeEditorGraphSignature(BehaviorTree ? Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph) : nullptr);
		Result.RuntimeTree = MakeRuntimeTreeSignature(BehaviorTree);
		Result.Blackboard = MakeBlackboardSignature(Blackboard);
		Result.BlackboardReference = MakeBlackboardReferenceSignature(BehaviorTree);
		return Result;
	}

	bool CaptureFileSnapshot(const FString& Filename, FFileSnapshot& OutSnapshot, FString& OutError)
	{
		OutSnapshot.Filename = Filename;
		OutSnapshot.bExisted = IFileManager::Get().FileExists(*Filename);
		if (OutSnapshot.bExisted && !FFileHelper::LoadFileToArray(OutSnapshot.Bytes, *Filename))
		{
			OutError = FString::Printf(TEXT("Failed to snapshot existing package: %s"), *Filename);
			return false;
		}
		return true;
	}

	bool RestoreSnapshots(
		const TArray<FFileSnapshot>& Snapshots,
		const FInputs& Inputs,
		FCUBehaviorTreeAuthorResult& OutResult)
	{
		OutResult.bRollbackAttempted = true;
		OutResult.RollbackErrors.Reset();
		// A fresh-load verification can leave a linker on a live dependency package (most
		// commonly the companion Blackboard referenced by the isolated BT). Detach only the
		// target packages' file loaders before replacing/deleting their files; keep the live
		// UObjects and their in-memory edits intact for the caller to inspect or retry.
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		TSet<UPackage*> PackagesToReset;
		if (Inputs.BehaviorTree && Inputs.BehaviorTree->GetOutermost())
		{
			PackagesToReset.Add(Inputs.BehaviorTree->GetOutermost());
		}
		if (Inputs.bSaveBlackboard && Inputs.Blackboard && Inputs.Blackboard->GetOutermost())
		{
			PackagesToReset.Add(Inputs.Blackboard->GetOutermost());
		}
		for (UPackage* Package : PackagesToReset)
		{
			ResetLoaders(Package);
		}
#endif
		for (const FFileSnapshot& Snapshot : Snapshots)
		{
			if (Snapshot.bExisted)
			{
				if (!FFileHelper::SaveArrayToFile(Snapshot.Bytes, *Snapshot.Filename))
				{
					OutResult.RollbackErrors.Add(FString::Printf(TEXT("Failed to restore %s"), *Snapshot.Filename));
					continue;
				}

				TArray<uint8> RestoredBytes;
				if (!FFileHelper::LoadFileToArray(RestoredBytes, *Snapshot.Filename) || RestoredBytes != Snapshot.Bytes)
				{
					OutResult.RollbackErrors.Add(FString::Printf(TEXT("Restored bytes did not verify for %s"), *Snapshot.Filename));
				}
			}
			else if (IFileManager::Get().FileExists(*Snapshot.Filename)
				&& !IFileManager::Get().Delete(*Snapshot.Filename, false, true, true))
			{
				OutResult.RollbackErrors.Add(FString::Printf(TEXT("Failed to remove newly created file %s"), *Snapshot.Filename));
			}
		}

		// Newly created assets have no durable package after rollback. Clear only their dirty package
		// flags so Save All cannot recreate files we intentionally deleted. Existing assets remain live
		// and dirty: this on-disk rollback makes no claim that their mutated UObjects were unwound.
		const FString BehaviorTreeFilename = FPackageName::LongPackageNameToFilename(
			Inputs.BehaviorTreePackagePath, FPackageName::GetAssetPackageExtension());
		const FString BlackboardFilename = Inputs.bSaveBlackboard
			? FPackageName::LongPackageNameToFilename(
				Inputs.BlackboardPackagePath, FPackageName::GetAssetPackageExtension())
			: FString();
		for (const FFileSnapshot& Snapshot : Snapshots)
		{
			if (Inputs.BehaviorTree
				&& Snapshot.Filename.Equals(BehaviorTreeFilename, ESearchCase::IgnoreCase))
			{
				Inputs.BehaviorTree->GetOutermost()->SetDirtyFlag(Snapshot.bExisted);
			}
			else if (Inputs.Blackboard
				&& Snapshot.Filename.Equals(BlackboardFilename, ESearchCase::IgnoreCase))
			{
				Inputs.Blackboard->GetOutermost()->SetDirtyFlag(Snapshot.bExisted);
			}
		}

		OutResult.bRollbackSucceeded = OutResult.RollbackErrors.IsEmpty();
		return OutResult.bRollbackSucceeded;
	}

	bool SaveAssetPackage(UObject* Asset, const FString& Filename, FString& OutError)
	{
		if (!Asset || !Asset->GetOutermost())
		{
			OutError = TEXT("Cannot save an asset without an outer package");
			return false;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		if (!UPackage::SavePackage(Asset->GetOutermost(), Asset, *Filename, SaveArgs))
		{
			OutError = FString::Printf(TEXT("Failed to save package: %s"), *Filename);
			return false;
		}
		return true;
	}

	UObject* LoadIsolatedObject(
		const FString& SourceFile,
		const FString& OriginalPackageName,
		const FString& AssetName,
		TArray<UPackage*>& OutPackages,
		TArray<FString>& OutTempDirectories,
		FString& OutError)
	{
		const FString TempDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("NarshaMCPReloadCheck"),
			FGuid::NewGuid().ToString(EGuidFormats::Digits));
		const FString TempFile = FPaths::Combine(TempDirectory, FPaths::GetCleanFilename(SourceFile));
		IFileManager::Get().MakeDirectory(*TempDirectory, true);
		OutTempDirectories.Add(TempDirectory);

		if (IFileManager::Get().Copy(*TempFile, *SourceFile, true, true) != COPY_OK)
		{
			OutError = FString::Printf(TEXT("Failed to stage isolated reload copy: %s"), *SourceFile);
			return nullptr;
		}

		UPackage* DiffPackage = DiffUtils::LoadPackageForDiff(
			FPackagePath::FromLocalPath(TempFile),
			FPackagePath::FromPackageNameChecked(*OriginalPackageName));
		if (!DiffPackage)
		{
			OutError = FString::Printf(TEXT("Isolated reload failed for %s"), *OriginalPackageName);
			return nullptr;
		}
		if (!FPackageName::IsTempPackage(DiffPackage->GetName()))
		{
			OutError = FString::Printf(TEXT("Isolated reload fell back to the live package for %s"), *OriginalPackageName);
			return nullptr;
		}
		OutPackages.Add(DiffPackage);

		UObject* LoadedObject = FindObject<UObject>(DiffPackage, *AssetName);
		if (!LoadedObject)
		{
			OutError = FString::Printf(TEXT("Reloaded package lacks asset %s"), *AssetName);
			return nullptr;
		}
		return LoadedObject;
	}

	bool VerifySavedState(
		const FInputs& Inputs,
		const FString& BehaviorTreeFilename,
		const FString& BlackboardFilename,
		const FCanonicalSignature& ExpectedSerialized,
		const FCanonicalSignature& ExpectedRebuilt,
		FCUBehaviorTreeAuthorResult& OutResult,
		FString& OutError)
	{
		TArray<UPackage*> LoadedPackages;
		TArray<FString> TempDirectories;
		auto CleanupIsolatedLoads = [&LoadedPackages, &TempDirectories, &OutError](bool bReportFailure) -> bool
		{
			bool bUnloaded = true;
			if (!LoadedPackages.IsEmpty())
			{
				UPackageTools::FUnloadPackageParams UnloadParams(LoadedPackages);
				UnloadParams.bUnloadDirtyPackages = true;
				// Diff packages are not user edits. Never clear the editor's undo history merely to
				// discard the fresh-load verification copies.
				UnloadParams.bResetTransBuffer = false;
				bUnloaded = UPackageTools::UnloadPackages(UnloadParams);
				if (!bUnloaded)
				{
					const FString Message = UnloadParams.OutErrorMessage.IsEmpty()
						? TEXT("Failed to unload isolated Behavior Tree verification packages")
						: UnloadParams.OutErrorMessage.ToString();
					if (bReportFailure)
					{
						OutError = Message;
					}
					else
					{
						UE_LOG(LogNarshaMCP, Warning, TEXT("[BehaviorTreeAuthor] %s"), *Message);
					}
				}
				// UnloadPackages performs GC, so do not retain possibly stale UObject pointers.
				LoadedPackages.Reset();
			}

			for (const FString& TempDirectory : TempDirectories)
			{
				IFileManager::Get().DeleteDirectory(*TempDirectory, false, true);
			}
			TempDirectories.Reset();
			return bUnloaded;
		};
		ON_SCOPE_EXIT
		{
			CleanupIsolatedLoads(false);
		};

		if (ShouldFailReload())
		{
			OutError = TEXT("Injected isolated reload failure");
			return false;
		}

		UBehaviorTree* ReloadedBehaviorTree = Cast<UBehaviorTree>(LoadIsolatedObject(
			BehaviorTreeFilename,
			Inputs.BehaviorTreePackagePath,
			Inputs.BehaviorTree->GetName(),
			LoadedPackages,
			TempDirectories,
			OutError));
		if (!ReloadedBehaviorTree || ReloadedBehaviorTree == Inputs.BehaviorTree)
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Behavior Tree reload was not isolated from the live UObject");
			}
			return false;
		}

		UBlackboardData* ReloadedBlackboard = nullptr;
		if (Inputs.bSaveBlackboard)
		{
			ReloadedBlackboard = Cast<UBlackboardData>(LoadIsolatedObject(
				BlackboardFilename,
				Inputs.BlackboardPackagePath,
				Inputs.Blackboard->GetName(),
				LoadedPackages,
				TempDirectories,
				OutError));
			if (!ReloadedBlackboard || ReloadedBlackboard == Inputs.Blackboard)
			{
				if (OutError.IsEmpty())
				{
					OutError = TEXT("Blackboard reload was not isolated from the live UObject");
				}
				return false;
			}
		}

		OutResult.bReloadVerified = true;
		UBehaviorTreeGraph* ReloadedGraph = Cast<UBehaviorTreeGraph>(ReloadedBehaviorTree->BTGraph);
		if (!ReloadedGraph)
		{
			OutError = TEXT("Reloaded Behavior Tree has no editor graph");
			return false;
		}
		FCanonicalSignature Serialized = CaptureSignature(ReloadedBehaviorTree, ReloadedBlackboard);
		if (ShouldPerturbEditorSignature())
		{
			Serialized.EditorGraph += TEXT("|injected-mismatch");
		}
		else if (ShouldPerturbRuntimeSignature())
		{
			Serialized.RuntimeTree += TEXT("|injected-mismatch");
		}
		else if (ShouldPerturbBlackboardSignature())
		{
			Serialized.Blackboard += TEXT("|injected-mismatch");
		}
		if (ExpectedSerialized.EditorGraph != Serialized.EditorGraph
			|| ExpectedSerialized.RuntimeTree != Serialized.RuntimeTree
			|| ExpectedSerialized.Blackboard != Serialized.Blackboard
			|| ExpectedSerialized.BlackboardReference != Serialized.BlackboardReference)
		{
			TArray<FString> Mismatches;
			if (ExpectedSerialized.EditorGraph != Serialized.EditorGraph) { Mismatches.Add(TEXT("editor")); }
			if (ExpectedSerialized.RuntimeTree != Serialized.RuntimeTree) { Mismatches.Add(TEXT("runtime")); }
			if (ExpectedSerialized.Blackboard != Serialized.Blackboard) { Mismatches.Add(TEXT("blackboard")); }
			if (ExpectedSerialized.BlackboardReference != Serialized.BlackboardReference) { Mismatches.Add(TEXT("blackboard-reference")); }
			OutError = FString::Printf(
				TEXT("Reloaded Behavior Tree state does not match the committed editor/runtime/Blackboard signature (%s)"),
				*FString::Join(Mismatches, TEXT(",")));
			return false;
		}

		// Check the serialized runtime before rebuilding it. Otherwise UpdateAsset() could repair a
		// missing/corrupt RootNode and turn a bad disk commit into a false verification success.
		ReloadedGraph->UpdateAsset();
		FCanonicalSignature Rebuilt = CaptureSignature(ReloadedBehaviorTree, ReloadedBlackboard);
		if (ShouldPerturbRebuiltSignature())
		{
			Rebuilt.RuntimeTree += TEXT("|injected-rebuilt-mismatch");
		}
		if (ExpectedRebuilt.EditorGraph != Rebuilt.EditorGraph
			|| ExpectedRebuilt.RuntimeTree != Rebuilt.RuntimeTree
			|| ExpectedRebuilt.Blackboard != Rebuilt.Blackboard
			|| ExpectedRebuilt.BlackboardReference != Rebuilt.BlackboardReference)
		{
			TArray<FString> Mismatches;
			if (ExpectedRebuilt.EditorGraph != Rebuilt.EditorGraph) { Mismatches.Add(TEXT("editor")); }
			if (ExpectedRebuilt.RuntimeTree != Rebuilt.RuntimeTree) { Mismatches.Add(TEXT("runtime")); }
			if (ExpectedRebuilt.Blackboard != Rebuilt.Blackboard) { Mismatches.Add(TEXT("blackboard")); }
			if (ExpectedRebuilt.BlackboardReference != Rebuilt.BlackboardReference) { Mismatches.Add(TEXT("blackboard-reference")); }
			OutError = FString::Printf(
				TEXT("Rebuilt Behavior Tree state does not match the committed editor/runtime/Blackboard signature (%s)"),
				*FString::Join(Mismatches, TEXT(",")));
			return false;
		}
		if (!CleanupIsolatedLoads(true))
		{
			return false;
		}

		OutResult.bRuntimeGraphVerified = true;
		return true;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
FString CanonicalizePropertyForTest(const FProperty* Property, const void* ValuePtr, const UObject* Owner)
{
	TSet<const UObject*> VisitedObjects;
	return MakeCanonicalPropertyValue(Property, ValuePtr, Owner, VisitedObjects);
}
#endif

bool CompileSaveAndVerify(const FInputs& Inputs, FCUBehaviorTreeAuthorResult& OutResult)
{
	OutResult.bSuccess = false;
	OutResult.bCompiled = false;
	OutResult.CompileErrors.Reset();
	OutResult.bSaved = false;
	OutResult.bReloadVerified = false;
	OutResult.bRuntimeGraphVerified = false;
	OutResult.bPartialCommit = false;
	OutResult.bRollbackAttempted = false;
	OutResult.bRollbackSucceeded = false;
	OutResult.CommittedFiles.Reset();
	OutResult.RollbackErrors.Reset();

	if (!Inputs.BehaviorTree)
	{
		OutResult.CompileErrors.Add(TEXT("Behavior Tree is null"));
		OutResult.Error = OutResult.CompileErrors[0];
		return false;
	}

	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(Inputs.BehaviorTree->BTGraph);
	if (!Graph)
	{
		OutResult.CompileErrors.Add(TEXT("Behavior Tree has no UBehaviorTreeGraph"));
		OutResult.Error = OutResult.CompileErrors[0];
		return false;
	}

	if (ShouldFailCompile())
	{
		OutResult.CompileErrors.Add(TEXT("Injected Behavior Tree compile failure"));
		OutResult.Error = OutResult.CompileErrors[0];
		return false;
	}

	Graph->UpdateAsset();
	OutResult.NumGraphNodes = Graph->Nodes.Num();
	OutResult.bHasEditorGraph = true;
	OutResult.RuntimeRootClass = Inputs.BehaviorTree->RootNode
		? Inputs.BehaviorTree->RootNode->GetClass()->GetName()
		: FString();
	if (!Inputs.BehaviorTree->RootNode)
	{
		OutResult.CompileErrors.Add(TEXT("Behavior Tree graph rebuild produced no runtime root node"));
		OutResult.Error = OutResult.CompileErrors[0];
		return false;
	}
	OutResult.bCompiled = true;

	const UBlackboardData* BlackboardForSignature = Inputs.bSaveBlackboard ? Inputs.Blackboard : nullptr;
	FCanonicalSignature Expected = CaptureSignature(Inputs.BehaviorTree, BlackboardForSignature);
	if (Expected.EditorGraph.IsEmpty() || Expected.RuntimeTree.IsEmpty())
	{
		OutResult.bCompiled = false;
		OutResult.CompileErrors.Add(TEXT("Behavior Tree rebuild produced an empty editor or runtime signature"));
		OutResult.Error = OutResult.CompileErrors[0];
		return false;
	}
	const FString BehaviorTreeFilename = FPackageName::LongPackageNameToFilename(
		Inputs.BehaviorTreePackagePath,
		FPackageName::GetAssetPackageExtension());
	const FString BlackboardFilename = Inputs.bSaveBlackboard
		? FPackageName::LongPackageNameToFilename(Inputs.BlackboardPackagePath, FPackageName::GetAssetPackageExtension())
		: FString();

	TArray<FFileSnapshot> Snapshots;
	FString OperationError;
	auto AddSnapshot = [&Snapshots, &OperationError](const FString& Filename) -> bool
	{
		if (Snapshots.ContainsByPredicate([&Filename](const FFileSnapshot& Existing)
			{ return Existing.Filename.Equals(Filename, ESearchCase::IgnoreCase); }))
		{
			return true;
		}
		FFileSnapshot& Snapshot = Snapshots.AddDefaulted_GetRef();
		return CaptureFileSnapshot(Filename, Snapshot, OperationError);
	};

	if (!AddSnapshot(BehaviorTreeFilename)
		|| (Inputs.bSaveBlackboard && !AddSnapshot(BlackboardFilename)))
	{
		OutResult.Error = OperationError;
		return false;
	}

	Inputs.BehaviorTree->MarkPackageDirty();

	if (Inputs.bSaveBlackboard)
	{
		if (!Inputs.Blackboard)
		{
			OutResult.Error = TEXT("Blackboard save was requested without a Blackboard asset");
			return false;
		}
		Inputs.Blackboard->MarkPackageDirty();
		const bool bBlackboardSaved = !ShouldFailBlackboardSave()
			&& SaveAssetPackage(Inputs.Blackboard, BlackboardFilename, OperationError);
		if (!bBlackboardSaved)
		{
			if (OperationError.IsEmpty())
			{
				OperationError = TEXT("Injected Blackboard save failure");
			}
			OutResult.Error = OperationError;
			RestoreSnapshots(Snapshots, Inputs, OutResult);
			return false;
		}
		OutResult.CommittedFiles.Add(BlackboardFilename);
	}

	const bool bBehaviorTreeSaved = !ShouldFailBehaviorTreeSave()
		&& SaveAssetPackage(Inputs.BehaviorTree, BehaviorTreeFilename, OperationError);
	if (!bBehaviorTreeSaved)
	{
		if (OperationError.IsEmpty())
		{
			OperationError = TEXT("Injected Behavior Tree save failure");
		}
		OutResult.bPartialCommit = !OutResult.CommittedFiles.IsEmpty();
		OutResult.Error = OperationError;
		RestoreSnapshots(Snapshots, Inputs, OutResult);
		return false;
	}
	OutResult.CommittedFiles.Add(BehaviorTreeFilename);
	OutResult.bSaved = true;
	OutResult.SavedFilePath = BehaviorTreeFilename;
	// SavePackage runs the engine's PreSave normalization for key selectors and
	// FValueOrBBKey values. That normalized live state is the authoritative semantic
	// state which the isolated disk copy must preserve.
	Expected = CaptureSignature(Inputs.BehaviorTree, BlackboardForSignature);
	Graph->UpdateAsset();
	const FCanonicalSignature ExpectedRebuilt = CaptureSignature(Inputs.BehaviorTree, BlackboardForSignature);

	if (!VerifySavedState(
		Inputs,
		BehaviorTreeFilename,
		BlackboardFilename,
		Expected,
		ExpectedRebuilt,
		OutResult,
		OperationError))
	{
		OutResult.Error = OperationError;
		RestoreSnapshots(Snapshots, Inputs, OutResult);
		return false;
	}

	if (Inputs.bRegisterBlackboard && Inputs.Blackboard)
	{
		FAssetRegistryModule::AssetCreated(Inputs.Blackboard);
	}
	if (Inputs.bRegisterBehaviorTree)
	{
		FAssetRegistryModule::AssetCreated(Inputs.BehaviorTree);
	}
	OutResult.bSuccess = true;
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
FScopedTestFailure::FScopedTestFailure(ETestFailureMode InMode)
	: PreviousMode(GTestFailureMode)
{
	GTestFailureMode = InMode;
}

FScopedTestFailure::~FScopedTestFailure()
{
	GTestFailureMode = PreviousMode;
}
#endif
}
