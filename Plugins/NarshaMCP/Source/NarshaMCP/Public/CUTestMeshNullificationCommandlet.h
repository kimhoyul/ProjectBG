// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUTestMeshNullificationCommandlet.generated.h"

/**
 * Test commandlet for validating in-memory mesh nullification
 *
 * Purpose:
 *   Isolated test to prove that mesh nullification works correctly
 *   before integrating into production BlueprintMetadataCommandlet.
 *
 * Usage:
 *   UnrealEditor-Cmd.exe YourProject.uproject -run=TestMeshNullification -Blueprint=/Game/BP_ParseTest
 *
 * Features:
 *   1. Loads single Blueprint specified by -Blueprint parameter
 *   2. Modifies uasset binary in memory to nullify mesh references
 *   3. Saves to temporary file
 *   4. Loads Blueprint from temporary file
 *   5. Validates Blueprint loaded successfully
 *   6. Cleans up temporary file
 *
 * Expected Output:
 *   [TestMeshNullification] Loading: /Game/BP_ParseTest
 *   [TestMeshNullification] Nullified 2 mesh reference(s)
 *   [TestMeshNullification] Blueprint loaded successfully
 *   [TestMeshNullification] Class: BP_ParseTest_C
 *   [TestMeshNullification] Parent: Blueprint
 *
 * Related:
 *   - FCUAssetBinaryModifier - Binary modification utility
 *   - BlueprintMetadataCommandlet - Production commandlet (integration target)
 */
UCLASS()
class NARSHAMCP_API UCUTestMeshNullificationCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUTestMeshNullificationCommandlet();

	/**
	 * Main commandlet entry point
	 *
	 * @param Params - Command line parameters (expects -Blueprint=/Game/AssetPath)
	 * @return 0 if success, 1 if failure
	 */
	virtual int32 Main(const FString& Params) override;

private:
	/**
	 * Parse Blueprint path from command line parameters
	 *
	 * @param Params - Command line string
	 * @param OutBlueprintPath - Parsed Blueprint package path (e.g., "/Game/BP_ParseTest")
	 * @return true if parsed successfully, false if missing or invalid
	 */
	bool ParseBlueprintPath(const FString& Params, FString& OutBlueprintPath);

	/**
	 * Load Blueprint with mesh nullification
	 *
	 * @param BlueprintPath - Package path (e.g., "/Game/BP_ParseTest")
	 * @param OutNullifiedCount - Number of mesh references nullified
	 * @return Loaded Blueprint, or nullptr if failed
	 */
	UBlueprint* LoadBlueprintWithMeshNullification(const FString& BlueprintPath, int32& OutNullifiedCount);

	/**
	 * Validate and log Blueprint information
	 *
	 * @param Blueprint - Loaded Blueprint to validate
	 * @param NullifiedCount - Number of mesh references that were nullified
	 */
	void ValidateBlueprint(UBlueprint* Blueprint, int32 NullifiedCount);
};
