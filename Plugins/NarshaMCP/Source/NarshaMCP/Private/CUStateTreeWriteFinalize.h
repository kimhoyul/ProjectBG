// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27780 (ST-06b): compile / save / fresh-load receipt for incremental StateTree writes.
// Private helper — do not include from outside Source/NarshaMCP/Private.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UStateTree;

namespace CUStateTreeWriteFinalize
{
	/**
	 * Validate → mark dirty (UE 5.8) → compile → save → fresh-load verify.
	 * Mutates ResultObject with compiled / compile_errors / saved / reload_verified /
	 * needs_recompile / ready_to_run (plus AC-0 before-compile snapshots).
	 *
	 * @return empty string on success (caller serializes ResultObject via CreateSuccessJson);
	 *         a fully-formed error JSON (success=false) on compile/save/reload failure.
	 *         Compile failure does not save (CreateStateTree / #26805 contract).
	 */
	FString CompileSaveAndVerify(UStateTree* StateTree, const TSharedRef<FJsonObject>& ResultObject);
}
