// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16915 (Epic #16906 Tier 3): Domain C++ commandlet for the reflection-blocked
// BehaviorTree editor graph.

#include "CUBehaviorTreeAuthorCommandlet.h"
#include "CUBehaviorTreeAuthor.h"
#include "CUCommandletHelpers.h"
#include "CULog.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

UCUBehaviorTreeAuthorCommandlet::UCUBehaviorTreeAuthorCommandlet()
{
	IsClient = false;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UCUBehaviorTreeAuthorCommandlet::Main(const FString& Params)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("=== BehaviorTreeAuthor Commandlet (Issue #16915) ==="));

	// --- Optional: -VerifyPath=<existing BT> — load-back + recompile-survival check (no authoring) ---
	FString VerifyPath;
	if (FParse::Value(*Params, TEXT("VerifyPath="), VerifyPath) && !VerifyPath.IsEmpty())
	{
		FString OutDirV;
		if (!FParse::Value(*Params, TEXT("OutputDir="), OutDirV) || OutDirV.IsEmpty())
		{
			OutDirV = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
		}
		IFileManager::Get().MakeDirectory(*OutDirV, /*Tree=*/true);

		FCUBehaviorTreeAuthorVerifyResult V;
		const bool bVerifyOk = FCUBehaviorTreeAuthor::Verify(VerifyPath, V);

		const TSharedRef<FJsonObject> VR = MakeShared<FJsonObject>();
		VR->SetBoolField(TEXT("success"), bVerifyOk); // top-level success for the NARSHAMCP_RESULT marker
		VR->SetStringField(TEXT("verify_path"), VerifyPath);
		VR->SetBoolField(TEXT("loaded"), V.bLoaded);
		VR->SetBoolField(TEXT("has_editor_graph"), V.bHasEditorGraph);
		VR->SetNumberField(TEXT("num_graph_nodes"), V.NumGraphNodes);
		VR->SetStringField(TEXT("root_node_class"), V.RootNodeClass);
		VR->SetNumberField(TEXT("root_child_count"), V.RootChildCount);
		VR->SetStringField(TEXT("first_child_class"), V.FirstChildClass);
		VR->SetStringField(TEXT("root_node_class_after_rebuild"), V.RootNodeClassAfterRebuild);
		VR->SetNumberField(TEXT("root_child_count_after_rebuild"), V.RootChildCountAfterRebuild);
		VR->SetBoolField(TEXT("survives_recompile"), V.bSurvivesRecompile);
		VR->SetNumberField(TEXT("blackboard_key_count"), V.BlackboardKeyCount);
		VR->SetStringField(TEXT("error"), V.Error);
		TArray<TSharedPtr<FJsonValue>> NodeArr;
		for (const FString& S : V.GraphNodeSummaries)
		{
			NodeArr.Add(MakeShared<FJsonValueString>(S));
		}
		VR->SetArrayField(TEXT("graph_nodes"), NodeArr);

		FString VJson;
		const TSharedRef<TJsonWriter<>> VWriter = TJsonWriterFactory<>::Create(&VJson);
		FJsonSerializer::Serialize(VR, VWriter);
		const FString VReportPath = OutDirV / TEXT("behaviortree_verify.json");
		FFileHelper::SaveStringToFile(VJson, *VReportPath);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Verify report written: %s"), *VReportPath);
		FCUCommandletHelpers::EmitResultMarker(VR);

		if (bVerifyOk)
		{
			UE_LOG(LogNarshaMCPCommandlet, Display,
				TEXT("✓ Verified %s (graph_nodes=%d, root=%s, survives_recompile=true)"),
				*VerifyPath, V.NumGraphNodes, *V.RootNodeClass);
			return 0;
		}
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("✗ Verify failed: %s"), *V.Error);
		return 1;
	}

	// --- Required: -BTPath ---
	FString BTPath;
	if (!FParse::Value(*Params, TEXT("BTPath="), BTPath) || BTPath.IsEmpty())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error,
			TEXT("Missing required parameter: -BTPath=/Game/Path/To/BT"));
		return 1;
	}

	// --- Optional: -OutputDir (default <Project>/Intermediate/NarshaMCP) ---
	FString OutputDir;
	if (!FParse::Value(*Params, TEXT("OutputDir="), OutputDir) || OutputDir.IsEmpty())
	{
		OutputDir = FPaths::ProjectIntermediateDir() / TEXT("NarshaMCP");
	}
	IFileManager::Get().MakeDirectory(*OutputDir, /*Tree=*/true);

	// --- Issue #21226: -DecoratorOpsFixture authors Composite Decorator + non-empty DecoratorOps ---
	const bool bDecoratorOpsFixture = FParse::Param(*Params, TEXT("DecoratorOpsFixture"));
	FCUBehaviorTreeAuthorResult Result;
	bool bOk = false;
	if (bDecoratorOpsFixture)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display,
			TEXT("DecoratorOpsFixture mode (Issue #21226): Composite Decorator (A AND B) OR NOT C"));
		bOk = FCUBehaviorTreeAuthor::AuthorDecoratorOpsFixture(BTPath, Result);
	}
	else if (FString PatchFile; FParse::Value(*Params, TEXT("PatchFile="), PatchFile) && !PatchFile.IsEmpty())
	{
		if (FParse::Param(*Params, TEXT("Overwrite")))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error,
				TEXT("Patch cannot be combined with -Overwrite (overwrite evicts the graph; Issue #26814)"));
			return 1;
		}
		FString SpecFileCombo;
		if (FParse::Value(*Params, TEXT("SpecFile="), SpecFileCombo) && !SpecFileCombo.IsEmpty())
		{
			UE_LOG(LogNarshaMCPCommandlet, Error,
				TEXT("Patch cannot be combined with -SpecFile (full-spec create; Issue #26814)"));
			return 1;
		}
		FCUBehaviorTreePatchParams PatchParams;
		PatchParams.BTPackagePath = BTPath;
		PatchParams.bDryRun = FParse::Param(*Params, TEXT("DryRun"));
		if (!FFileHelper::LoadFileToString(PatchParams.PatchJson, *PatchFile))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to read -PatchFile=%s"), *PatchFile);
			return 1;
		}
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Patch mode (Issue #26814): %s (%d chars, dry_run=%s)"),
			*PatchFile, PatchParams.PatchJson.Len(), PatchParams.bDryRun ? TEXT("true") : TEXT("false"));
		bOk = FCUBehaviorTreeAuthor::Patch(PatchParams, Result);
	}
	else
	{
		// --- Optional: node positions (defaults match FCUBehaviorTreeAuthorParams) ---
		FCUBehaviorTreeAuthorParams AuthorParams;
		AuthorParams.BTPackagePath = BTPath;
		FParse::Value(*Params, TEXT("CompositeX="), AuthorParams.CompositePosX);
		FParse::Value(*Params, TEXT("CompositeY="), AuthorParams.CompositePosY);
		FParse::Value(*Params, TEXT("TaskX="), AuthorParams.TaskPosX);
		FParse::Value(*Params, TEXT("TaskY="), AuthorParams.TaskPosY);
		// Issue #26809: default refuse an existing package; `-Overwrite` opts into replace.
		AuthorParams.bOverwrite = FParse::Param(*Params, TEXT("Overwrite"));
		// Issue #26812: opt out of the default abort-on-property-failure and author anyway; the
		// failures are still reported in failed_properties below.
		AuthorParams.bAllowPartialProperties = FParse::Param(*Params, TEXT("AllowPartialProperties"));

		// --- Optional: -SpecFile=<path to tree JSON> (Phase B arbitrary topology) ---
		// When present, its contents drive the whole tree; absent → legacy Selector→Wait default.
		FString SpecFile;
		if (FParse::Value(*Params, TEXT("SpecFile="), SpecFile) && !SpecFile.IsEmpty())
		{
			if (!FFileHelper::LoadFileToString(AuthorParams.SpecJson, *SpecFile))
			{
				UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("Failed to read -SpecFile=%s"), *SpecFile);
				return 1;
			}
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Loaded tree spec (%d chars) from %s"),
				AuthorParams.SpecJson.Len(), *SpecFile);
		}

		// --- Author the behavior tree ---
		bOk = FCUBehaviorTreeAuthor::Execute(AuthorParams, Result);
	}

	// --- Emit JSON report for machine verification ---
	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("success"), Result.bSuccess);
	Root->SetStringField(TEXT("bt_path"), BTPath);
	Root->SetNumberField(TEXT("num_graph_nodes"), Result.NumGraphNodes);
	Root->SetBoolField(TEXT("has_editor_graph"), Result.bHasEditorGraph);
	Root->SetStringField(TEXT("runtime_root_class"), Result.RuntimeRootClass);
	Root->SetNumberField(TEXT("blackboard_key_count"), Result.BlackboardKeyCount);
	Root->SetNumberField(TEXT("existing_graph_node_count"), Result.ExistingGraphNodeCount);
	Root->SetBoolField(TEXT("overwrote"), Result.bOverwrote);
	// Issue #26812: property outcomes must reach the MCP response, not just the log.
	auto ToJsonArray = [](const TArray<FString>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& S : In) { Arr.Add(MakeShared<FJsonValueString>(S)); }
		return Arr;
	};
	Root->SetArrayField(TEXT("modified_properties"), ToJsonArray(Result.ModifiedProperties));
	Root->SetArrayField(TEXT("failed_properties"), ToJsonArray(Result.FailedProperties));
	Root->SetArrayField(TEXT("warnings"), ToJsonArray(Result.Warnings));
	Root->SetBoolField(TEXT("patched"), Result.bPatched);
	Root->SetBoolField(TEXT("dry_run"), Result.bDryRun);
	Root->SetNumberField(TEXT("nodes_added"), Result.NodesAdded);
	Root->SetNumberField(TEXT("nodes_removed"), Result.NodesRemoved);
	Root->SetNumberField(TEXT("nodes_moved"), Result.NodesMoved);
	Root->SetNumberField(TEXT("class_changes"), Result.ClassChanges);
	Root->SetNumberField(TEXT("decorators_added"), Result.DecoratorsAdded);
	Root->SetNumberField(TEXT("decorators_removed"), Result.DecoratorsRemoved);
	Root->SetNumberField(TEXT("services_added"), Result.ServicesAdded);
	Root->SetNumberField(TEXT("services_removed"), Result.ServicesRemoved);
	Root->SetNumberField(TEXT("bb_keys_renamed"), Result.BbKeysRenamed);
	Root->SetBoolField(TEXT("compiled"), Result.bCompiled);
	Root->SetArrayField(TEXT("compile_errors"), ToJsonArray(Result.CompileErrors));
	Root->SetBoolField(TEXT("saved"), Result.bSaved);
	Root->SetBoolField(TEXT("reload_verified"), Result.bReloadVerified);
	Root->SetBoolField(TEXT("runtime_graph_verified"), Result.bRuntimeGraphVerified);
	Root->SetBoolField(TEXT("partial_commit"), Result.bPartialCommit);
	Root->SetBoolField(TEXT("rollback_attempted"), Result.bRollbackAttempted);
	Root->SetBoolField(TEXT("rollback_succeeded"), Result.bRollbackSucceeded);
	Root->SetArrayField(TEXT("committed_files"), ToJsonArray(Result.CommittedFiles));
	Root->SetArrayField(TEXT("rollback_errors"), ToJsonArray(Result.RollbackErrors));
	TArray<TSharedPtr<FJsonValue>> Applied;
	for (const FString& Op : Result.AppliedOps)
	{
		Applied.Add(MakeShared<FJsonValueString>(Op));
	}
	Root->SetArrayField(TEXT("applied"), Applied);
	Root->SetStringField(TEXT("saved_file_path"), Result.SavedFilePath);
	Root->SetStringField(TEXT("error"), Result.Error);

	FString JsonText;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	FJsonSerializer::Serialize(Root, Writer);

	const FString ReportPath = OutputDir / TEXT("behaviortree_author.json");
	if (FFileHelper::SaveStringToFile(JsonText, *ReportPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("Report written: %s"), *ReportPath);
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("Failed to write report: %s"), *ReportPath);
	}

	// #16915: authoritative result marker for build_pipeline (shared FCUCommandletHelpers::EmitResultMarker).
	FCUCommandletHelpers::EmitResultMarker(Root);

	if (bOk)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display,
			TEXT("✓ Authored BT %s (graph_nodes=%d, runtime_root=%s)"),
			*BTPath, Result.NumGraphNodes, *Result.RuntimeRootClass);
		return 0;
	}

	UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("✗ Authoring failed: %s"), *Result.Error);
	return 1;
}
