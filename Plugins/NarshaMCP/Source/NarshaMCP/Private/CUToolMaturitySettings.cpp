// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUToolMaturitySettings.h"
#include "CULog.h"
#include "UObject/UnrealType.h"

UCUToolMaturitySettings::UCUToolMaturitySettings()
{
	// Default values are set on the header UPROPERTY initializers (enable-projection:
	// production/beta = true, experimental = false). Issue #15782: the roster is the
	// curated maturity registry; EDGE 5 of check_tool_maturity_fallback_sync.py guards it.
	//
	// Users can enable individual tools via:
	// 1. INI file (Config/DefaultToolMaturity.ini)
	// 2. Project Settings UI (Editor -> Project Settings -> Plugins -> Tool Maturity)
	// 3. ue_editor_automation(operation="set_tool_enabled")
}

bool UCUToolMaturitySettings::IsToolEnabled(const FString& ToolName) const
{
	// Reflection lookup: a `ue_*` bool UPROPERTY whose name matches ToolName (Issue #15782).
	// Roster-agnostic — adding/removing a tool in the header needs no change here.
	if (const FBoolProperty* Prop = FindFProperty<FBoolProperty>(GetClass(), FName(*ToolName)))
	{
		return Prop->GetPropertyValue_InContainer(this);
	}

	// Unknown tool: return false for safety
	return false;
}

void UCUToolMaturitySettings::SetToolEnabled(const FString& ToolName, bool bEnabled)
{
	// Note: This modifies the in-memory CDO, not the INI file.
	// For persistent changes, use ue_editor_automation(operation="set_tool_enabled")
	// which writes to INI via GConfig.
	if (FBoolProperty* Prop = FindFProperty<FBoolProperty>(GetClass(), FName(*ToolName)))
	{
		Prop->SetPropertyValue_InContainer(this, bEnabled);
		return;
	}

	// Unknown tool: log warning
	UE_LOG(LogNarshaMCP, Warning, TEXT("[ToolMaturitySettings] Unknown tool: %s"), *ToolName);
}

TMap<FString, bool> UCUToolMaturitySettings::GetAllToolStates() const
{
	TMap<FString, bool> States;

	// Reflection walk over every `ue_*` bool UPROPERTY on this class (incl. inherited,
	// but the `ue_` prefix filter keeps it to our roster only). Stays in lockstep with
	// the header field list — no hand-maintained per-tool branches (Issue #15782).
	for (TFieldIterator<FBoolProperty> It(GetClass()); It; ++It)
	{
		const FString Name = It->GetName();
		if (Name.StartsWith(TEXT("ue_")))
		{
			States.Add(Name, It->GetPropertyValue_InContainer(this));
		}
	}

	return States;
}
