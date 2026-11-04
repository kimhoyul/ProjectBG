// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #26808 ST-06: spec → StateTree author (create-or-open, one compile, save, verify).

#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UStateTree;

/** One spec-preflight diagnostic (Issue #27939). Dry-run and apply share this list. */
struct FCUStateTreeSpecDiagnostic
{
	FString Path;
	FString Code;
	FString Value;
	FString Message;
};

/** Inputs for FCUStateTreeAuthor::Execute. Plain C++ (non-USTRUCT), same pattern as FCUBehaviorTreeAuthorParams. */
struct FCUStateTreeAuthorParams
{
	/** Long package path, e.g. "/Game/NarshaMCP/ST_Authored". Existing assets are opened and extended. */
	FString STPackagePath;
	/** Spec JSON. Empty → create a compile-ready Root-only tree (CreateStateTree). */
	FString SpecJson;
	/** When true, parse + context-aware ValidateSpec + plan only. Existing assets may be loaded read-only. */
	bool bDryRun = false;
};

struct FCUStateTreeAuthorResult
{
	bool bSuccess = false;
	bool bDryRun = false;
	bool bCreated = false;
	bool bCompiled = false;
	bool bVerified = false;
	FString SavedFilePath;
	int32 PlannedStates = 0;
	int32 PlannedTransitions = 0;
	int32 PlannedTasks = 0;
	int32 PlannedParameters = 0;
	int32 PlannedConditions = 0;
	int32 PlannedEvaluators = 0;
	int32 PlannedConsiderations = 0;
	int32 PlannedBindings = 0;
	int32 AppliedOk = 0;
	int32 AppliedFail = 0;
	TArray<FString> Added;
	TArray<FString> Failed;
	FString Error;
	FString ErrorCode;
	FString Hint;
	/** Ordered structural diagnostics. Identical for dry-run and apply on the same spec. */
	TArray<FCUStateTreeSpecDiagnostic> Diagnostics;
};

struct FCUStateTreeAuthorVerifyResult
{
	bool bLoaded = false;
	bool bHasEditorData = false;
	int32 StateCount = 0;
	FString Error;
	FString ErrorCode;
	FString Hint;
};

/**
 * Authors a StateTree from a spec JSON: create-or-open, apply EditorData mutations,
 * compile (ST-03 fail-closed), save, then Verify via a DiffUtils disk copy (not the live UObject).
 *
 * Unlike BehaviorTree authoring, StateTree EditorData is already reachable via
 * UCURemoteLibrary write UFUNCTIONs. This helper batches those mutations, then
 * compiles once.
 */
class NARSHAMCP_API FCUStateTreeAuthor
{
public:
	static bool Execute(const FCUStateTreeAuthorParams& In, FCUStateTreeAuthorResult& Out);
	static bool Verify(const FString& STPackagePath, FCUStateTreeAuthorVerifyResult& Out);
	/**
	 * Side-effect-free spec preflight. ExistingAsset supplies read-only create-or-open context; no UObject is
	 * created or modified. True iff OutDiagnostics is empty. Null/empty Spec is valid (Root-only).
	 */
	static bool ValidateSpec(
		const TSharedPtr<FJsonObject>& Spec,
		TArray<FCUStateTreeSpecDiagnostic>& OutDiagnostics,
		const UStateTree* ExistingAsset = nullptr);
};
