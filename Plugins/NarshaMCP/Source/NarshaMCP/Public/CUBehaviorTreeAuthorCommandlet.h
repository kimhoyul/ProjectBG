// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16915 (Epic #16906 Tier 3): Domain C++ commandlet for the reflection-blocked
// BehaviorTree editor graph.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUBehaviorTreeAuthorCommandlet.generated.h"

/**
 * Headless authoring commandlet for a BehaviorTree's EDITOR GRAPH.
 *
 * Demonstrates the "domain C++ toolset" pattern for areas reflection cannot reach:
 * UBehaviorTree::BTGraph and the UBTGraphNode_* visual node classes live in the editor-only
 * BehaviorTreeEditor module and are not surfaced to set_editor_property / ObjectTools /
 * execute_python / Remote Control. This commandlet builds a positioned Root → Selector → Wait
 * editor graph from C++ (via FCUBehaviorTreeAuthor), the only full-automation path for durable
 * BT authoring. See docs/technical/spikes/16915-behaviortree-author-eval.md.
 *
 * Modeled on UCUCameraRigAuthorCommandlet (Issue #16577): derives directly from UCommandlet,
 * no AssetRegistry full-scan wait needed.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUBehaviorTreeAuthor
 *     -BTPath=/Game/NarshaMCP/Spike/BT_Authored
 *     [-CompositeX=0 -CompositeY=200 -TaskX=0 -TaskY=400]
 *     [-OutputDir="D:/Custom/Report/Dir"]
 *     -NullRHI -unattended -nosplash
 *
 * Writes a JSON report (behaviortree_author.json) to OutputDir (default:
 * <Project>/Intermediate/NarshaMCP) for machine verification. Returns 0 on success, 1 on error.
 * NOTE (#16577 finding): trust the JSON `success` field over the editor process exit code, which
 * can false-negative on RHI teardown; this op is RHI-independent, so prefer -NullRHI.
 */
UCLASS()
class UCUBehaviorTreeAuthorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUBehaviorTreeAuthorCommandlet();

	virtual int32 Main(const FString& Params) override;
};
