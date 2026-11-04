// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26814: incremental editor-graph patch (load in place — not overwrite-recreate).
//
// Issue #26812 PR3 -- READ THIS BEFORE "FIXING" AN UNEXPECTED ABORT HERE.
// The four `Out.FailedProperties.Num() > 0` checks below (add-node / add-decorator / add-service /
// replace-class steps) abort UNCONDITIONALLY: unlike FCUBehaviorTreeAuthor::Execute, this path does
// not read bAllowPartialProperties. PR3 gave those checks a NEW trigger -- CUBehaviorTreeAuthorPrivate::
// ApplyProperties (CUBehaviorTreeAuthor.cpp, the FBlackboardKeySelector branch) now reports a
// blackboard key that is missing, or that exists with a type the node's AllowedTypes filter forbids,
// as a FAILED property rather than a warning, because the engine silently rewrites such a key in
// FBlackboardKeySelector::PreSave on save. So an ops patch that used to warn-and-continue on a bad
// key now stops.
//
// That opt-out gap is DELIBERATE, not an oversight: #26812 PR2 made `ops` + `allow_partial_properties`
// an illegal combination at the MCP layer (MCP/native/mcp_server/src/tools/author_behavior_tree.rs,
// `test_allow_partial_properties_is_refused_with_ops`). Wiring bAllowPartialProperties through this
// file would reverse a contract that shipped one PR earlier -- coordinate both layers if you change it.

#include "CUBehaviorTreeAuthor.h"
#include "CUBehaviorTreeAuthorInternals.h"
#include "CUBehaviorTreeWriteFinalize.h"
#include "CULog.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"

#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "EdGraph/EdGraphPin.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"

#if WITH_EDITORONLY_DATA

using namespace CUBehaviorTreeAuthorPrivate;

namespace
{
	constexpr int32 GMaxPatchOps = 256;
	constexpr int32 GPosClamp = 1000000;

	FString JsonValueToImportString(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid())
		{
			return FString();
		}
		switch (V->Type)
		{
		case EJson::String:
			return V->AsString();
		case EJson::Boolean:
			return V->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:
		{
			const double D = V->AsNumber();
			if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)))
			{
				return FString::Printf(TEXT("%lld"), static_cast<int64>(D));
			}
			return FString::SanitizeFloat(D);
		}
		default:
			return FString();
		}
	}

	void ParsePropsField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TMap<FString, FString>& Out)
	{
		const TSharedPtr<FJsonObject>* PropsObj;
		if (Obj->TryGetObjectField(Field, PropsObj))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*PropsObj)->Values)
			{
				Out.Add(KV.Key, JsonValueToImportString(KV.Value));
			}
		}
	}

	bool ParseNodeSpec(const TSharedPtr<FJsonObject>& Obj, FCUBTNodeSpec& Out, FString& Err)
	{
		if (!Obj.IsValid())
		{
			Err = TEXT("add_node missing node object");
			return false;
		}
		Obj->TryGetStringField(TEXT("id"), Out.Id);
		if (Out.Id.IsEmpty() || Out.Id.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
		{
			Err = TEXT("add_node node.id is required and must not be Root");
			return false;
		}
		Obj->TryGetStringField(TEXT("kind"), Out.Kind);
		if (Out.Kind.IsEmpty())
		{
			Out.Kind = TEXT("task");
		}
		Out.Kind = Out.Kind.ToLower();
		if (Out.Kind != TEXT("composite") && Out.Kind != TEXT("task"))
		{
			Err = FString::Printf(TEXT("unknown kind '%s'"), *Out.Kind);
			return false;
		}
		Obj->TryGetStringField(TEXT("class"), Out.ClassName);
		if (Out.ClassName.IsEmpty())
		{
			Err = TEXT("add_node node.class is required");
			return false;
		}
		double X = 0, Y = 0;
		if (Obj->TryGetNumberField(TEXT("x"), X))
		{
			Out.X = FMath::Clamp(static_cast<int32>(X), -GPosClamp, GPosClamp);
		}
		if (Obj->TryGetNumberField(TEXT("y"), Y))
		{
			Out.Y = FMath::Clamp(static_cast<int32>(Y), -GPosClamp, GPosClamp);
		}
		ParsePropsField(Obj, TEXT("properties"), Out.Properties);
		return true;
	}

	UBehaviorTreeGraphNode* ResolveGraphNode(UBehaviorTreeGraph* Graph, const FString& Id, FString& Err)
	{
		if (!Graph || Id.IsEmpty())
		{
			Err = TEXT("resolve: empty graph or id");
			return nullptr;
		}
		if (Id.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
		{
			if (UBehaviorTreeGraphNode_Root* Root = FindRootEdNode(Graph))
			{
				return Root;
			}
			Err = TEXT("resolve: Root graph node missing");
			return nullptr;
		}
		TArray<UBehaviorTreeGraphNode*> Hits;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UBehaviorTreeGraphNode* GN = Cast<UBehaviorTreeGraphNode>(Node);
			if (!GN)
			{
				continue;
			}
			if (const UBTNode* Inst = Cast<UBTNode>(GN->NodeInstance))
			{
				if (Inst->NodeName == Id || Inst->GetNodeName() == Id)
				{
					Hits.AddUnique(GN);
					continue;
				}
			}
			if (GN->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).Equals(Id, ESearchCase::IgnoreCase)
				|| GN->GetName() == Id)
			{
				Hits.AddUnique(GN);
			}
		}
		if (Hits.Num() == 1)
		{
			return Hits[0];
		}
		if (Hits.Num() == 0)
		{
			Err = FString::Printf(TEXT("node '%s' not found"), *Id);
			return nullptr;
		}
		Err = FString::Printf(TEXT("ambiguous node id '%s' (%d matches)"), *Id, Hits.Num());
		return nullptr;
	}

	TArray<UBehaviorTreeGraphNode*> ChildNodes(UBehaviorTreeGraphNode* Parent)
	{
		TArray<UBehaviorTreeGraphNode*> Out;
		if (!Parent)
		{
			return Out;
		}
		UEdGraphPin* Pin = Parent->GetOutputPin();
		if (!Pin)
		{
			return Out;
		}
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked)
			{
				if (UBehaviorTreeGraphNode* Child = Cast<UBehaviorTreeGraphNode>(Linked->GetOwningNode()))
				{
					Out.Add(Child);
				}
			}
		}
		return Out;
	}

	UBehaviorTreeGraphNode* ParentOf(UBehaviorTreeGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		UEdGraphPin* In = Node->GetInputPin();
		if (!In || In->LinkedTo.Num() == 0 || !In->LinkedTo[0])
		{
			return nullptr;
		}
		return Cast<UBehaviorTreeGraphNode>(In->LinkedTo[0]->GetOwningNode());
	}

	bool RelinkChildren(UBehaviorTreeGraphNode* Parent, const TArray<UBehaviorTreeGraphNode*>& Children, FString& Err)
	{
		if (!Parent)
		{
			Err = TEXT("relink: parent is null");
			return false;
		}
		UEdGraphPin* Out = Parent->GetOutputPin();
		if (!Out)
		{
			Err = TEXT("relink: parent has no output pin");
			return false;
		}
		Out->BreakAllPinLinks();
		for (UBehaviorTreeGraphNode* Child : Children)
		{
			if (!Child || !LinkPins(Out, Child->GetInputPin()))
			{
				Err = TEXT("relink: failed to MakeLinkTo child");
				return false;
			}
		}
		return true;
	}

	bool WouldCreateCycle(UBehaviorTreeGraphNode* Node, UBehaviorTreeGraphNode* NewParent)
	{
		if (!Node || !NewParent)
		{
			return false;
		}
		TArray<UBehaviorTreeGraphNode*> Stack;
		Stack.Add(Node);
		TSet<const UBehaviorTreeGraphNode*> Seen;
		while (Stack.Num() > 0)
		{
			UBehaviorTreeGraphNode* Cur = Stack.Pop();
			if (Cur == NewParent)
			{
				return true;
			}
			if (Seen.Contains(Cur))
			{
				continue;
			}
			Seen.Add(Cur);
			for (UBehaviorTreeGraphNode* Child : ChildNodes(Cur))
			{
				Stack.Add(Child);
			}
		}
		return false;
	}

	FString SubNodeStableId(const UBehaviorTreeGraphNode* Sub)
	{
		if (!Sub)
		{
			return TEXT("<null>");
		}
		return Sub->NodeGuid.IsValid()
			? Sub->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens)
			: Sub->GetName();
	}

	FString DescribeSubNode(const UBehaviorTreeGraphNode* Sub)
	{
		const FString ClassName = Sub && Sub->NodeInstance
			? Sub->NodeInstance->GetClass()->GetName()
			: TEXT("<none>");
		const UBTNode* Inst = Sub ? Cast<UBTNode>(Sub->NodeInstance) : nullptr;
		const FString DisplayName = Inst ? Inst->GetNodeName() : TEXT("<none>");
		return FString::Printf(
			TEXT("{id='%s', object='%s', class='%s', name='%s'}"),
			*SubNodeStableId(Sub), Sub ? *Sub->GetName() : TEXT("<null>"), *ClassName, *DisplayName);
	}

	FString DescribeSubNodeCandidates(const TArray<UBehaviorTreeGraphNode*>& Subs)
	{
		TArray<FString> Descriptions;
		for (const UBehaviorTreeGraphNode* Sub : Subs)
		{
			if (Sub)
			{
				Descriptions.Add(DescribeSubNode(Sub));
			}
		}
		return FString::Join(Descriptions, TEXT(", "));
	}

	UBehaviorTreeGraphNode* ResolveSubNode(
		const TArray<UBehaviorTreeGraphNode*>& Subs,
		const FString& ExplicitId,
		const FString& LegacySelector,
		const FString& ParentId,
		const FString& Op,
		FString& Err)
	{
		TArray<UBehaviorTreeGraphNode*> Hits;
		for (UBehaviorTreeGraphNode* Sub : Subs)
		{
			if (!Sub)
			{
				continue;
			}

			bool bMatches = false;
			if (!ExplicitId.IsEmpty())
			{
				bMatches = Sub->GetName().Equals(ExplicitId, ESearchCase::CaseSensitive);
				if (Sub->NodeGuid.IsValid())
				{
					bMatches = bMatches
						|| Sub->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens).Equals(ExplicitId, ESearchCase::IgnoreCase)
						|| Sub->NodeGuid.ToString(EGuidFormats::Digits).Equals(ExplicitId, ESearchCase::IgnoreCase);
				}
			}
			else
			{
				bMatches = Sub->GetName() == LegacySelector;
				if (Sub->NodeInstance)
				{
					bMatches = bMatches || Sub->NodeInstance->GetClass()->GetName() == LegacySelector;
				}
				if (const UBTNode* Inst = Cast<UBTNode>(Sub->NodeInstance))
				{
					bMatches = bMatches || Inst->GetNodeName() == LegacySelector || Inst->NodeName == LegacySelector;
				}
			}

			if (bMatches)
			{
				Hits.AddUnique(Sub);
			}
		}

		if (Hits.Num() == 1)
		{
			return Hits[0];
		}

		const FString Candidates = DescribeSubNodeCandidates(Subs);
		if (!ExplicitId.IsEmpty())
		{
			Err = Hits.Num() == 0
				? FString::Printf(
					TEXT("%s: no matching sub-node id='%s' under parent='%s'; candidates=[%s]"),
					*Op, *ExplicitId, *ParentId, *Candidates)
				: FString::Printf(
					TEXT("%s: ambiguous explicit sub-node id='%s' under parent='%s' (%d matches); candidates=[%s]"),
					*Op, *ExplicitId, *ParentId, Hits.Num(), *Candidates);
			return nullptr;
		}

		if (LegacySelector.IsEmpty())
		{
			Err = FString::Printf(
				TEXT("%s requires id, name, or class under parent='%s'; candidates=[%s]"),
				*Op, *ParentId, *Candidates);
			return nullptr;
		}
		Err = Hits.Num() == 0
			? FString::Printf(
				TEXT("%s: no matching sub-node selector '%s' under parent='%s'; candidates=[%s]"),
				*Op, *LegacySelector, *ParentId, *Candidates)
			: FString::Printf(
				TEXT("%s: ambiguous sub-node selector '%s' under parent='%s' (%d matches); retry with one candidate id; candidates=[%s]"),
				*Op, *LegacySelector, *ParentId, Hits.Num(), *Candidates);
		return nullptr;
	}

	void WalkBlackboardSelectors(UObject* Obj, const FName From, const FName To, UBlackboardData* BB, int32& Renamed)
	{
		if (!Obj)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop)
			{
				continue;
			}
			if (const FStructProperty* SP = CastField<FStructProperty>(Prop))
			{
				if (SP->Struct == FBlackboardKeySelector::StaticStruct())
				{
					FBlackboardKeySelector* Sel = SP->ContainerPtrToValuePtr<FBlackboardKeySelector>(Obj);
					if (Sel && Sel->SelectedKeyName == From)
					{
						Sel->SelectedKeyName = To;
						if (BB)
						{
							Sel->ResolveSelectedKey(*BB);
						}
						++Renamed;
					}
				}
			}
		}
	}

	int32 ClampIndex(int32 Index, int32 Num)
	{
		if (Index < 0)
		{
			return Num;
		}
		return FMath::Clamp(Index, 0, Num);
	}

	void RemoveSubNodesFromGraph(UBehaviorTreeGraph* Graph, UBehaviorTreeGraphNode* Node)
	{
		if (!Graph || !Node)
		{
			return;
		}
		TArray<UBehaviorTreeGraphNode*> Subs;
		Subs.Append(Node->Decorators);
		Subs.Append(Node->Services);
		for (UBehaviorTreeGraphNode* Sub : Subs)
		{
			if (!Sub)
			{
				continue;
			}
			Node->RemoveSubNode(Sub);
			Graph->RemoveNode(Sub);
		}
	}



	struct FParsedOp
	{
		FString Op;
		FString Id;
		FString Parent;
		FString NewParent;
		FString Target;
		FString ClassName;
		FString Name;
		FString From;
		FString To;
		int32 Index = -1;
		int32 X = 0;
		int32 Y = 0;
		bool bHasX = false;
		bool bHasY = false;
		FCUBTNodeSpec Node;
		TMap<FString, FString> Properties;
		TArray<FString> Children;
	};

	bool ParseOps(const FString& Json, TArray<FParsedOp>& Out, FString& Err)
	{
		TSharedPtr<FJsonValue> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Err = TEXT("PatchJson is not valid JSON");
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Root->Type == EJson::Array)
		{
			Arr = &Root->AsArray();
		}
		else if (Root->Type == EJson::Object)
		{
			const TSharedPtr<FJsonObject> Obj = Root->AsObject();
			if (!Obj->TryGetArrayField(TEXT("ops"), Arr))
			{
				Err = TEXT("PatchJson object must contain ops[]");
				return false;
			}
		}
		else
		{
			Err = TEXT("PatchJson must be an array or {ops:[]}");
			return false;
		}
		if (!Arr)
		{
			Err = TEXT("ops array missing");
			return false;
		}
		if (Arr->Num() > GMaxPatchOps)
		{
			Err = FString::Printf(TEXT("ops exceeds max %d"), GMaxPatchOps);
			return false;
		}
		for (const TSharedPtr<FJsonValue>& Item : *Arr)
		{
			const TSharedPtr<FJsonObject>* ObjPtr;
			if (!Item.IsValid() || !Item->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid())
			{
				Err = TEXT("each op must be a JSON object");
				return false;
			}
			const TSharedPtr<FJsonObject>& Obj = *ObjPtr;
			FParsedOp P;
			Obj->TryGetStringField(TEXT("op"), P.Op);
			P.Op = P.Op.ToLower();
			Obj->TryGetStringField(TEXT("id"), P.Id);
			Obj->TryGetStringField(TEXT("parent"), P.Parent);
			Obj->TryGetStringField(TEXT("new_parent"), P.NewParent);
			Obj->TryGetStringField(TEXT("target"), P.Target);
			Obj->TryGetStringField(TEXT("class"), P.ClassName);
			Obj->TryGetStringField(TEXT("name"), P.Name);
			Obj->TryGetStringField(TEXT("from"), P.From);
			Obj->TryGetStringField(TEXT("to"), P.To);
			double Idx = -1, X = 0, Y = 0;
			if (Obj->TryGetNumberField(TEXT("index"), Idx))
			{
				P.Index = FMath::Clamp(static_cast<int32>(Idx), -1, GMaxPatchOps);
			}
			if (Obj->TryGetNumberField(TEXT("x"), X))
			{
				P.X = FMath::Clamp(static_cast<int32>(X), -GPosClamp, GPosClamp);
				P.bHasX = true;
			}
			if (Obj->TryGetNumberField(TEXT("y"), Y))
			{
				P.Y = FMath::Clamp(static_cast<int32>(Y), -GPosClamp, GPosClamp);
				P.bHasY = true;
			}
			ParsePropsField(Obj, TEXT("properties"), P.Properties);
			const TArray<TSharedPtr<FJsonValue>>* Kids;
			if (Obj->TryGetArrayField(TEXT("children"), Kids) && Kids)
			{
				for (const TSharedPtr<FJsonValue>& K : *Kids)
				{
					if (K.IsValid() && K->Type == EJson::String)
					{
						P.Children.Add(K->AsString());
					}
				}
			}
			const TSharedPtr<FJsonObject>* NodeObj;
			if (Obj->TryGetObjectField(TEXT("node"), NodeObj) && NodeObj)
			{
				if (!ParseNodeSpec(*NodeObj, P.Node, Err))
				{
					return false;
				}
			}
			if (P.Op.IsEmpty())
			{
				Err = TEXT("op field is required");
				return false;
			}
			Out.Add(MoveTemp(P));
		}
		return true;
	}

	/**
	 * Validate every legacy decorator/service removal before the apply loop can mutate the live
	 * graph. This is deliberately conservative when an earlier add op could change the candidate
	 * set: callers must split the request, observe the new stable id, then remove by id. Full
	 * arbitrary-op rollback/Undo remains the transaction concern tracked by Issue #27937.
	 */
	bool PreflightLegacySubNodeRemovals(
		UBehaviorTreeGraph* Graph, const TArray<FParsedOp>& Ops, FString& Err)
	{
		for (int32 OpIndex = 0; OpIndex < Ops.Num(); ++OpIndex)
		{
			const FParsedOp& P = Ops[OpIndex];
			const bool bDecorator = P.Op == TEXT("remove_decorator");
			const bool bService = P.Op == TEXT("remove_service");
			if ((!bDecorator && !bService) || !P.Id.IsEmpty())
			{
				continue;
			}

			const FString LegacySelector = P.Name.IsEmpty() ? P.ClassName : P.Name;
			FString TargetErr;
			UBehaviorTreeGraphNode* Target = ResolveGraphNode(Graph, P.Target, TargetErr);
			if (!Target)
			{
				Err = FString::Printf(TEXT("%s preflight: %s"), *P.Op, *TargetErr);
				return false;
			}

			FString ResolveErr;
			if (!ResolveSubNode(
				bDecorator ? Target->Decorators : Target->Services,
				FString(),
				LegacySelector,
				P.Target,
				P.Op,
				ResolveErr))
			{
				Err = ResolveErr;
				return false;
			}

			for (int32 PriorIndex = 0; PriorIndex < OpIndex; ++PriorIndex)
			{
				const FParsedOp& Prior = Ops[PriorIndex];
				const bool bSameKindAdd = bDecorator
					? Prior.Op == TEXT("add_decorator")
					: Prior.Op == TEXT("add_service");
				const bool bSameKindRemove = bDecorator
					? Prior.Op == TEXT("remove_decorator")
					: Prior.Op == TEXT("remove_service");
				if (!bSameKindAdd && !bSameKindRemove)
				{
					continue;
				}

				// Compare the resolved parent identity, not the caller's spelling. ResolveGraphNode
				// accepts semantic NodeName, NodeGuid, and graph object name aliases for one node.
				FString PriorTargetErr;
				UBehaviorTreeGraphNode* PriorTarget = ResolveGraphNode(Graph, Prior.Target, PriorTargetErr);
				if (PriorTarget == Target)
				{
					Err = FString::Printf(
						TEXT("%s preflight: legacy selector '%s' under parent='%s' depends on earlier %s on the same resolved parent; split the patch and retry with the returned sub-node id"),
						*P.Op, *LegacySelector, *P.Target, *Prior.Op);
					return false;
				}
			}
		}
		return true;
	}
}

bool FCUBehaviorTreeAuthor::Patch(const FCUBehaviorTreePatchParams& In, FCUBehaviorTreeAuthorResult& Out)
{
	Out = FCUBehaviorTreeAuthorResult{};
	Out.bDryRun = In.bDryRun;

	if (In.BTPackagePath.IsEmpty() || !FPackageName::IsValidLongPackageName(In.BTPackagePath))
	{
		Out.Error = FString::Printf(TEXT("Invalid BT package path: %s"), *In.BTPackagePath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}
	if (In.PatchJson.IsEmpty())
	{
		Out.Error = TEXT("PatchJson is empty");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}

	TArray<FParsedOp> Ops;
	if (!ParseOps(In.PatchJson, Ops, Out.Error))
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}
	if (Ops.Num() == 0)
	{
		Out.Error = TEXT("ops array is empty");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}

	const FString PackageName = In.BTPackagePath;
	const FString AssetName = FPackageName::GetShortName(PackageName);
	if (!FPackageName::DoesPackageExist(PackageName))
	{
		Out.Error = FString::Printf(TEXT("BehaviorTree does not exist: %s"), *PackageName);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}

	Out.ExistingGraphNodeCount = CountExistingGraphNodes(PackageName);

	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *ObjectPath);
	if (!BT)
	{
		Out.Error = FString::Printf(TEXT("Failed to load BehaviorTree %s"), *ObjectPath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph)
	{
		Out.Error = TEXT("BTGraph is null — cannot patch (asset has no editor graph)");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}
	UBlackboardData* BB = BT->BlackboardAsset;
	if (!PreflightLegacySubNodeRemovals(Graph, Ops, Out.Error))
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
		return false;
	}

	for (const FParsedOp& P : Ops)
	{
		FString StepErr;
		if (P.Op == TEXT("add_node"))
		{
			UBehaviorTreeGraphNode* Parent = ResolveGraphNode(Graph, P.Parent, StepErr);
			if (!Parent)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			{
				FString DupErr;
				if (UBehaviorTreeGraphNode* Existing = ResolveGraphNode(Graph, P.Node.Id, DupErr))
				{
					(void)Existing;
					Out.Error = FString::Printf(TEXT("add_node duplicate id '%s'"), *P.Node.Id);
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
				if (!DupErr.Contains(TEXT("not found")))
				{
					Out.Error = DupErr.IsEmpty()
						? FString::Printf(TEXT("add_node duplicate id '%s'"), *P.Node.Id)
						: DupErr;
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
			}
			UBehaviorTreeGraphNode* Created = CreateGraphNode(Graph, P.Node, BB, StepErr, Out);
			if (!Created)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (Out.FailedProperties.Num() > 0)
			{
				Out.Error = FString::Printf(TEXT("%d property/properties could not be applied: %s"),
					Out.FailedProperties.Num(), *FString::Join(Out.FailedProperties, TEXT("; ")));
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			TArray<UBehaviorTreeGraphNode*> Kids = ChildNodes(Parent);
			const int32 At = ClampIndex(P.Index, Kids.Num());
			Kids.Insert(Created, At);
			if (!RelinkChildren(Parent, Kids, StepErr))
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			++Out.NodesAdded;
			Out.AppliedOps.Add(TEXT("add_node"));
		}
		else if (P.Op == TEXT("remove_node"))
		{
			UBehaviorTreeGraphNode* Node = ResolveGraphNode(Graph, P.Id, StepErr);
			if (!Node)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (Cast<UBehaviorTreeGraphNode_Root>(Node))
			{
				Out.Error = TEXT("remove_node cannot remove Root");
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (ChildNodes(Node).Num() > 0)
			{
				Out.Error = FString::Printf(TEXT("remove_node '%s' still has children"), *P.Id);
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			UBehaviorTreeGraphNode* Parent = ParentOf(Node);
			if (Parent && Cast<UBehaviorTreeGraphNode_Root>(Parent) && ChildNodes(Parent).Num() <= 1)
			{
				Out.Error = TEXT("remove_node refuses to delete the sole root_child");
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (UEdGraphPin* InputPin = Node->GetInputPin())
			{
				InputPin->BreakAllPinLinks();
			}
			if (UEdGraphPin* OutPin = Node->GetOutputPin())
			{
				OutPin->BreakAllPinLinks();
			}
			RemoveSubNodesFromGraph(Graph, Node);
			Graph->RemoveNode(Node);
			++Out.NodesRemoved;
			Out.AppliedOps.Add(TEXT("remove_node"));
		}
		else if (P.Op == TEXT("move_node"))
		{
			UBehaviorTreeGraphNode* Node = ResolveGraphNode(Graph, P.Id, StepErr);
			UBehaviorTreeGraphNode* NewParent = ResolveGraphNode(Graph, P.NewParent, StepErr);
			if (!Node || !NewParent)
			{
				Out.Error = StepErr.IsEmpty() ? TEXT("move_node requires id and new_parent") : StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (WouldCreateCycle(Node, NewParent))
			{
				Out.Error = FString::Printf(TEXT("move_node '%s' → '%s' would create a cycle"), *P.Id, *P.NewParent);
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (UBehaviorTreeGraphNode* OldParent = ParentOf(Node))
			{
				TArray<UBehaviorTreeGraphNode*> OldKids = ChildNodes(OldParent);
				OldKids.Remove(Node);
				if (!RelinkChildren(OldParent, OldKids, StepErr))
				{
					Out.Error = StepErr;
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
			}
			TArray<UBehaviorTreeGraphNode*> NewKids = ChildNodes(NewParent);
			NewKids.Remove(Node);
			const int32 At = ClampIndex(P.Index, NewKids.Num());
			NewKids.Insert(Node, At);
			if (!RelinkChildren(NewParent, NewKids, StepErr))
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			++Out.NodesMoved;
			Out.AppliedOps.Add(TEXT("move_node"));
		}
		else if (P.Op == TEXT("reorder_children"))
		{
			UBehaviorTreeGraphNode* Parent = ResolveGraphNode(Graph, P.Id, StepErr);
			if (!Parent)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			TArray<UBehaviorTreeGraphNode*> Ordered;
			for (const FString& ChildId : P.Children)
			{
				UBehaviorTreeGraphNode* Child = ResolveGraphNode(Graph, ChildId, StepErr);
				if (!Child)
				{
					Out.Error = StepErr;
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
				if (ParentOf(Child) != Parent)
				{
					Out.Error = FString::Printf(
						TEXT("reorder_children: '%s' is not a child of '%s'"), *ChildId, *P.Id);
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
				Ordered.Add(Child);
			}
			if (!RelinkChildren(Parent, Ordered, StepErr))
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			Out.AppliedOps.Add(TEXT("reorder_children"));
		}
		else if (P.Op == TEXT("replace_class"))
		{
			UBehaviorTreeGraphNode* Node = ResolveGraphNode(Graph, P.Id, StepErr);
			if (!Node)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (Cast<UBehaviorTreeGraphNode_Root>(Node))
			{
				Out.Error = TEXT("replace_class cannot retarget Root");
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			const bool bWasComposite = Node->NodeInstance && Node->NodeInstance->IsA(UBTCompositeNode::StaticClass());
			FCUBTNodeSpec Spec;
			Spec.Id = P.Id;
			Spec.ClassName = P.ClassName;
			Spec.Kind = bWasComposite ? TEXT("composite") : TEXT("task");
			Spec.X = Node->NodePosX;
			Spec.Y = Node->NodePosY;
			UClass* Cls = ResolveNodeClass(P.ClassName);
			if (!Cls)
			{
				Out.Error = FString::Printf(TEXT("Unknown node class '%s' (node '%s')"), *P.ClassName, *P.Id);
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			const bool bNewComposite = Cls->IsChildOf(UBTCompositeNode::StaticClass());
			Spec.Kind = bNewComposite ? TEXT("composite") : TEXT("task");
			const UClass* Required = bNewComposite ? UBTCompositeNode::StaticClass() : UBTTaskNode::StaticClass();
			if (!CheckClassAgainstBase(Cls, Required, P.Id, Spec.Kind, StepErr))
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			UBehaviorTreeGraphNode* OldParent = ParentOf(Node);
			TArray<UBehaviorTreeGraphNode*> Kids = ChildNodes(Node);
			if (Kids.Num() > 0 && !bNewComposite)
			{
				Out.Error = FString::Printf(
					TEXT("replace_class '%s' to task would orphan %d children"), *P.Id, Kids.Num());
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			TArray<UBehaviorTreeGraphNode*> KeepDecorators = Node->Decorators;
			TArray<UBehaviorTreeGraphNode*> KeepServices = Node->Services;
			UBehaviorTreeGraphNode* Created = CreateGraphNode(Graph, Spec, BB, StepErr, Out);
			if (!Created)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (Out.FailedProperties.Num() > 0)
			{
				Out.Error = FString::Printf(TEXT("%d property/properties could not be applied: %s"),
					Out.FailedProperties.Num(), *FString::Join(Out.FailedProperties, TEXT("; ")));
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (OldParent)
			{
				TArray<UBehaviorTreeGraphNode*> Siblings = ChildNodes(OldParent);
				const int32 At = Siblings.IndexOfByKey(Node);
				Siblings.Remove(Node);
				Siblings.Insert(Created, At < 0 ? Siblings.Num() : At);
				if (!RelinkChildren(OldParent, Siblings, StepErr))
				{
					Out.Error = StepErr;
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
			}
			if (bNewComposite && Kids.Num() > 0)
			{
				if (!RelinkChildren(Created, Kids, StepErr))
				{
					Out.Error = StepErr;
					UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
					return false;
				}
			}
			for (UBehaviorTreeGraphNode* D : KeepDecorators)
			{
				if (!D)
				{
					continue;
				}
				Node->RemoveSubNode(D);
				Created->AddSubNode(D, Graph);
			}
			for (UBehaviorTreeGraphNode* S : KeepServices)
			{
				if (!S)
				{
					continue;
				}
				Node->RemoveSubNode(S);
				Created->AddSubNode(S, Graph);
			}
			if (UEdGraphPin* InputPin = Node->GetInputPin())
			{
				InputPin->BreakAllPinLinks();
			}
			if (UEdGraphPin* OutPin = Node->GetOutputPin())
			{
				OutPin->BreakAllPinLinks();
			}
			Graph->RemoveNode(Node);
			++Out.ClassChanges;
			Out.AppliedOps.Add(TEXT("replace_class"));
		}
		else if (P.Op == TEXT("add_decorator") || P.Op == TEXT("add_service"))
		{
			const bool bDec = P.Op == TEXT("add_decorator");
			UBehaviorTreeGraphNode* Target = ResolveGraphNode(Graph, P.Target, StepErr);
			if (!Target)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			FCUBTSubNodeSpec Spec;
			Spec.ClassName = P.ClassName;
			Spec.Properties = P.Properties;
			if (Spec.ClassName.IsEmpty())
			{
				Out.Error = FString::Printf(TEXT("%s requires class"), *P.Op);
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (!AddSubNodeFromSpec(Graph, Target, Spec, bDec, BB, StepErr, Out))
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (Out.FailedProperties.Num() > 0)
			{
				Out.Error = FString::Printf(TEXT("%d property/properties could not be applied: %s"),
					Out.FailedProperties.Num(), *FString::Join(Out.FailedProperties, TEXT("; ")));
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (bDec)
			{
				++Out.DecoratorsAdded;
			}
			else
			{
				++Out.ServicesAdded;
			}
			Out.AppliedOps.Add(P.Op);
		}
		else if (P.Op == TEXT("remove_decorator") || P.Op == TEXT("remove_service"))
		{
			const bool bDec = P.Op == TEXT("remove_decorator");
			UBehaviorTreeGraphNode* Target = ResolveGraphNode(Graph, P.Target, StepErr);
			if (!Target)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			const FString LegacySelector = P.Name.IsEmpty() ? P.ClassName : P.Name;
			UBehaviorTreeGraphNode* Sub = ResolveSubNode(
				bDec ? Target->Decorators : Target->Services,
				P.Id,
				LegacySelector,
				P.Target,
				P.Op,
				StepErr);
			if (!Sub)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			Target->RemoveSubNode(Sub);
			Graph->RemoveNode(Sub);
			if (bDec)
			{
				++Out.DecoratorsRemoved;
			}
			else
			{
				++Out.ServicesRemoved;
			}
			Out.AppliedOps.Add(P.Op);
		}
		else if (P.Op == TEXT("rename_bb_key"))
		{
			if (P.From.IsEmpty() || P.To.IsEmpty())
			{
				Out.Error = TEXT("rename_bb_key requires from and to");
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (!BB)
			{
				Out.Error = TEXT("rename_bb_key: BT has no BlackboardAsset");
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			bool bFound = false;
			const FName FromName(*P.From);
			const FName ToName(*P.To);
			for (FBlackboardEntry& Entry : BB->Keys)
			{
				if (Entry.EntryName == FromName)
				{
					Entry.EntryName = ToName;
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Out.Error = FString::Printf(TEXT("rename_bb_key: key '%s' not found"), *P.From);
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			int32 SelectorHits = 0;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UBehaviorTreeGraphNode* GN = Cast<UBehaviorTreeGraphNode>(Node))
				{
					WalkBlackboardSelectors(GN->NodeInstance, FromName, ToName, BB, SelectorHits);
					for (UBehaviorTreeGraphNode* D : GN->Decorators)
					{
						if (D)
						{
							WalkBlackboardSelectors(D->NodeInstance, FromName, ToName, BB, SelectorHits);
						}
					}
					for (UBehaviorTreeGraphNode* S : GN->Services)
					{
						if (S)
						{
							WalkBlackboardSelectors(S->NodeInstance, FromName, ToName, BB, SelectorHits);
						}
					}
				}
			}
			BB->MarkPackageDirty();
			++Out.BbKeysRenamed;
			Out.AppliedOps.Add(TEXT("rename_bb_key"));
			(void)SelectorHits;
		}
		else if (P.Op == TEXT("set_properties"))
		{
			UBehaviorTreeGraphNode* Node = ResolveGraphNode(Graph, P.Id, StepErr);
			if (!Node || !Node->NodeInstance)
			{
				Out.Error = StepErr.IsEmpty() ? TEXT("set_properties: node has no NodeInstance") : StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			AccumulateProps(Out, ApplyProperties(Node->NodeInstance, P.Properties, BB));
			if (Out.FailedProperties.Num() > 0)
			{
				Out.Error = FString::Printf(TEXT("%d property/properties could not be applied: %s"),
					Out.FailedProperties.Num(), *FString::Join(Out.FailedProperties, TEXT("; ")));
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			Out.AppliedOps.Add(TEXT("set_properties"));
		}
		else if (P.Op == TEXT("set_position"))
		{
			UBehaviorTreeGraphNode* Node = ResolveGraphNode(Graph, P.Id, StepErr);
			if (!Node)
			{
				Out.Error = StepErr;
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			if (!P.bHasX || !P.bHasY)
			{
				Out.Error = TEXT("set_position requires both x and y");
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
				return false;
			}
			Node->NodePosX = P.X;
			Node->NodePosY = P.Y;
			Out.AppliedOps.Add(TEXT("set_position"));
		}
		else
		{
			Out.Error = FString::Printf(TEXT("unknown patch op '%s'"), *P.Op);
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
			return false;
		}
	}

	if (In.bDryRun)
	{
		Graph->UpdateAsset();
		Out.NumGraphNodes = Graph->Nodes.Num();
		Out.bHasEditorGraph = (BT->BTGraph != nullptr);
		Out.RuntimeRootClass = BT->RootNode ? BT->RootNode->GetClass()->GetName() : TEXT("None");
		if (BT->RootNode == nullptr)
		{
			Out.Error = TEXT("RootNode null after UpdateAsset (graph→tree sync produced no runtime root)");
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
			return false;
		}
	}
	else
	{
		const bool bSaveBlackboard = BB
			&& BB->GetOutermost() != BT->GetOutermost()
			&& BB->GetOutermost()->IsDirty();
		CUBehaviorTreeWriteFinalize::FInputs FinalizeInputs;
		FinalizeInputs.BehaviorTree = BT;
		FinalizeInputs.Blackboard = BB;
		FinalizeInputs.BehaviorTreePackagePath = PackageName;
		FinalizeInputs.BlackboardPackagePath = bSaveBlackboard ? BB->GetOutermost()->GetName() : FString();
		FinalizeInputs.bSaveBlackboard = bSaveBlackboard;
		if (!CUBehaviorTreeWriteFinalize::CompileSaveAndVerify(FinalizeInputs, Out))
		{
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
			return false;
		}
	}

	if (BB)
	{
		Out.BlackboardKeyCount = BB->GetNumKeys();
	}
	Out.bPatched = true;
	if (In.bDryRun)
	{
		Out.bSuccess = true;
	}
	Out.bOverwrote = false;
	UE_LOG(LogNarshaMCP, Display,
		TEXT("[BehaviorTreeAuthor/Patch] %s ops=%d graph_nodes=%d dry_run=%s -> %s"),
		*PackageName, Out.AppliedOps.Num(), Out.NumGraphNodes,
		In.bDryRun ? TEXT("true") : TEXT("false"), *Out.SavedFilePath);
	return true;
}

#else // !WITH_EDITORONLY_DATA

bool FCUBehaviorTreeAuthor::Patch(const FCUBehaviorTreePatchParams& In, FCUBehaviorTreeAuthorResult& Out)
{
	(void)In;
	Out = FCUBehaviorTreeAuthorResult{};
	Out.Error = TEXT("BehaviorTree editor-graph patch requires WITH_EDITORONLY_DATA (editor build)");
	UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/Patch] %s"), *Out.Error);
	return false;
}

#endif // WITH_EDITORONLY_DATA
