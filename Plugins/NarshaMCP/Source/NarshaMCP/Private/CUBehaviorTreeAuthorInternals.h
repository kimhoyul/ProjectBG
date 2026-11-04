// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26814: Private helpers shared by FCUBehaviorTreeAuthor::Execute and ::Patch.
// Not a Public API — do not include from outside Source/NarshaMCP/Private.

#pragma once

#include "CoreMinimal.h"
#include "CUBehaviorTreeAuthor.h"

#if WITH_EDITORONLY_DATA

class UBehaviorTreeGraph;
class UBehaviorTreeGraphNode;
class UBehaviorTreeGraphNode_Root;
class UBlackboardData;
class UEdGraph;
class UEdGraphPin;

namespace CUBehaviorTreeAuthorPrivate
{
	/** Outcome of applying one property map (Issue #26812). Shared by Execute and Patch. */
	struct FApplyPropertiesResult
	{
		TArray<FString> Modified;
		TArray<FString> Failed;
		TArray<FString> Warnings;
	};

	inline void AccumulateProps(FCUBehaviorTreeAuthorResult& Out, const FApplyPropertiesResult& R)
	{
		Out.ModifiedProperties.Append(R.Modified);
		Out.FailedProperties.Append(R.Failed);
		Out.Warnings.Append(R.Warnings);
	}

	UBehaviorTreeGraphNode_Root* FindRootEdNode(const UEdGraph* Graph);
	UClass* ResolveNodeClass(const FString& Name);
	bool CheckClassAgainstBase(
		UClass* Cls, const UClass* Required, const FString& NodeId, const FString& Kind, FString& Err);
	FApplyPropertiesResult ApplyProperties(UObject* Obj, const TMap<FString, FString>& Props, UBlackboardData* BB);
	UBehaviorTreeGraphNode* CreateGraphNode(
		UBehaviorTreeGraph* Graph, const FCUBTNodeSpec& N, UBlackboardData* BB, FString& Err,
		FCUBehaviorTreeAuthorResult& Out);
	bool AddSubNodeFromSpec(
		UBehaviorTreeGraph* Graph, UBehaviorTreeGraphNode* Parent,
		const FCUBTSubNodeSpec& S, bool bDecorator, UBlackboardData* BB, FString& Err,
		FCUBehaviorTreeAuthorResult& Out);
	int32 CountExistingGraphNodes(const FString& PackageName);
	bool LinkPins(UEdGraphPin* From, UEdGraphPin* To);
}

#endif // WITH_EDITORONLY_DATA
