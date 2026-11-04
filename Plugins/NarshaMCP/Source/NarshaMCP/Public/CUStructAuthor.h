// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #18415: Domain C++ authoring for reflection-blocked editor-only UserDefinedStruct edits.

#pragma once

#include "CoreMinimal.h"

/**
 * Inputs for FCUStructAuthor::Execute.
 *
 * Plain C++ (non-USTRUCT) on purpose: an intra-module helper contract shared by the commandlet
 * (headless) and a future Remote Control UFUNCTION (live editor), neither of which needs
 * reflection on the parameter bag itself. Mirrors FCUCameraRigAuthor / FCUBehaviorTreeAuthor.
 */
struct FCUStructAuthorParams
{
	/** create_struct / add_member / modify_member / remove_member / rename_member / reorder_members / set_member_metadata / rename_struct / duplicate_struct / delete_struct / get_structure. */
	FString Operation;

	/** Long package name of the UserDefinedStruct, e.g. "/Game/NarshaMCP/S_DamageInfo". */
	FString StructPackagePath;

	/**
	 * Raw JSON spec, shape depends on Operation (the commandlet loads it from -SpecFile):
	 *   create_struct       : {"members":[{"name","type","default"?}, ...]}  (members optional → empty struct + default bool)
	 *   add_member          : {"member":{"name","type","default"?}}
	 *   modify_member       : {"member_name", "type"?, "default"?}           (at least one of type/default)
	 *   remove_member       : {"member_name"}
	 *   rename_member       : {"member_name", "new_name"}                    (#19625; new_name must be unique)
	 *   reorder_members     : {"member_order":["Name", ...]}                 (#19625; full permutation of member display names)
	 *   set_member_metadata : {"member_name", "metadata":{"Key":"Value", ...}} (#19625; UPROPERTY meta e.g. Category/ToolTip/ClampMin)
	 *   rename_struct       : {"destination_path"}                          (#19714; rename/move the UDS ASSET; referencers fixed up)
	 *   duplicate_struct    : {"destination_path"}                          (#19714; copy the UDS asset; destination must not exist)
	 *   delete_struct       : {}                                            (#19714; delete the UDS asset; refused while referenced)
	 *   get_structure       : {}                                            (read-back, no write)
	 *
	 * `type` grammar: bool | int | float | double | string | name | text | FVector | FRotator |
	 * FTransform | object:<ClassObjectPath> | struct:<UserDefinedStructObjectPath>.
	 */
	FString SpecJson;
};

/** One authored field, echoed back for machine verification / get_structure. */
struct FCUStructFieldInfo
{
	FString Name;         // friendly (display) name
	FString Type;         // normalized type token (matches the input grammar)
	FString DefaultValue; // serialized default (may be empty)
};

/** Result of an authoring run; serialized to JSON by the commandlet. */
struct FCUStructAuthorResult
{
	bool bSuccess = false;
	int32 FieldCount = 0;
	TArray<FCUStructFieldInfo> Fields;
	FString SavedFilePath; // empty for get_structure and on failure
	FString NewPath;       // #19714: destination long package name (rename_struct/duplicate_struct), else empty
	TArray<FString> Referencers; // #19714: referencing package names when delete_struct refuses, else empty
	FString Error;         // empty on success
};

/**
 * Authors / edits a UUserDefinedStruct (UDS) from C++.
 *
 * Why this exists (Issue #18415): `FStructureEditorUtils` (UnrealEd, editor-only) is NOT exposed to
 * Python / Remote Control / set_editor_property reflection, so no scripting path can create a UDS or
 * add/modify/remove its members. Epic's official Unreal MCP stops at the shell; this bypasses the wall
 * from domain C++. Same pattern as FCUCameraRigAuthor (#16577) / FCUBehaviorTreeAuthor (#16915).
 *
 * The reflection-bypassing logic lives here (not in the commandlet) so the same call can later back a
 * live-editor UFUNCTION without duplication.
 */
class NARSHAMCP_API FCUStructAuthor
{
public:
	/**
	 * Apply one operation (create / add / modify / remove / rename / reorder / metadata /
	 * asset rename-duplicate-delete / read) to a UDS and, for write ops, save it.
	 *
	 * @param In   Operation + package path + JSON spec.
	 * @param Out  Populated with the resulting field list, saved path, or error.
	 * @return true on success.
	 */
	static bool Execute(const FCUStructAuthorParams& In, FCUStructAuthorResult& Out);
};
