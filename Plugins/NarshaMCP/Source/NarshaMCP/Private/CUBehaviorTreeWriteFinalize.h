// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "CUBehaviorTreeAuthor.h"

class UBehaviorTree;
class UBlackboardData;
class FProperty;

namespace CUBehaviorTreeWriteFinalize
{
	struct FInputs
	{
		UBehaviorTree* BehaviorTree = nullptr;
		UBlackboardData* Blackboard = nullptr;
		FString BehaviorTreePackagePath;
		FString BlackboardPackagePath;
		bool bSaveBlackboard = false;
		bool bRegisterBehaviorTree = false;
		bool bRegisterBlackboard = false;
	};

	/** Rebuild, save, isolated-reload, and verify one Behavior Tree write transaction. */
	bool CompileSaveAndVerify(const FInputs& Inputs, FCUBehaviorTreeAuthorResult& OutResult);

#if WITH_DEV_AUTOMATION_TESTS
	enum class ETestFailureMode : uint8
	{
		None,
		Compile,
		BlackboardSave,
		BehaviorTreeSave,
		Reload,
		EditorSignature,
		RuntimeSignature,
		BlackboardSignature,
		RebuiltSignature,
	};

	/** Scoped, test-only failure injection. This surface is not compiled into shipping builds. */
	class FScopedTestFailure
	{
	public:
		explicit FScopedTestFailure(ETestFailureMode InMode);
		~FScopedTestFailure();

	private:
		ETestFailureMode PreviousMode;
	};

	/** White-box coverage for unloaded soft-reference canonicalization. */
	FString CanonicalizePropertyForTest(const FProperty* Property, const void* ValuePtr, const UObject* Owner = nullptr);
#endif
}
