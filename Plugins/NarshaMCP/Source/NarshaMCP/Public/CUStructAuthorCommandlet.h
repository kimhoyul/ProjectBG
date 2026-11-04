// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #18415: Domain C++ commandlet for reflection-blocked editor-only UserDefinedStruct edits.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "CUStructAuthorCommandlet.generated.h"

/**
 * Headless authoring commandlet for a UserDefinedStruct (UDS).
 *
 * The "domain C++ toolset" pattern (#18415, siblings #16577 / #16915): `FStructureEditorUtils`
 * (UnrealEd, editor-only) is invisible to Python / set_editor_property / Remote Control, so no
 * scripting path can create a UDS or edit its members. This commandlet delegates to FCUStructAuthor,
 * the only full-automation path. Epic's official Unreal MCP creates the shell only; this does
 * shell + members + defaults.
 *
 * Usage (Rule #10 — the `-run` name MUST carry the `NarshaMCP.` module prefix):
 *   UnrealEditor-Cmd.exe <Project>.uproject -run=NarshaMCP.CUStructAuthor
 *     -Operation=create_struct -StructPath=/Game/NarshaMCP/S_DamageInfo
 *     [-SpecFile="D:/tmp/members.json"] [-OutputDir="D:/Custom/Report/Dir"]
 *     -unattended -nosplash
 *
 * Operations: create_struct (with optional bulk members) / add_member / modify_member /
 * remove_member / rename_member / reorder_members / set_member_metadata (#19625) /
 * rename_struct / duplicate_struct / delete_struct (#19714 asset lifecycle) / get_structure.
 * Writes a JSON report (struct_author.json) to OutputDir
 * (default <Project>/Intermediate/NarshaMCP) + a `NARSHAMCP_RESULT:` marker. Returns 0 / 1.
 */
UCLASS()
class UCUStructAuthorCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UCUStructAuthorCommandlet();

	virtual int32 Main(const FString& Params) override;
};
