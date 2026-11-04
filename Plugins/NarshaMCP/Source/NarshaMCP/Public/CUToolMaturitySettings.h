// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CUToolMaturitySettings.generated.h"

/**
 * Developer settings for MCP Tool Maturity - Per-Tool Control (Issue #2547)
 *
 * Controls which MCP tools are registered at runtime with individual on/off switches.
 *
 * Roster SSOT (Issue #15782): the `bool ue_*` field set below mirrors the curated
 * maturity registry — `docs/generated/features_registry.yaml` (tools.mcp_tools),
 * `MCP/core/tool_maturity.py` (_TOOL_MATURITY_MAP), and `Config/DefaultToolMaturity.ini`.
 * EDGE 5 of `scripts/quality/check_tool_maturity_fallback_sync.py` enforces that this
 * field set equals the registry name-set, with each default bool = enable-projection
 * (production/beta = true, experimental = false). Do NOT hand-edit this list out of sync.
 *
 * Tool Groups:
 *   - Production: Stable API (enabled by default)
 *   - Beta: Feature complete (enabled by default)
 *   - Experimental: In development (disabled by default)
 *
 * Priority: INI Per-Tool > Environment Variables > Default
 *
 * Usage:
 *   const UCUToolMaturitySettings* Settings = GetDefault<UCUToolMaturitySettings>();
 *   bool bEnabled = Settings->IsToolEnabled("ue_fix_errors");
 *
 * Config File: Config/DefaultToolMaturity.ini
 * Section: [/Script/NarshaMCP.CUToolMaturitySettings]
 *
 * Example:
 *   ue_analyze_symbols=true
 *   ue_fix_errors=true
 *   ue_manage_pcg=false
 *
 * Hot Reload:
 *   Use ue_editor_automation(operation="get_tool_maturity") to view all states
 *   Use ue_editor_automation(operation="set_tool_enabled", params={"tool_name": "...", "enabled": true})
 *   Use ue_editor_automation(operation="reload_tool_maturity") to reload from INI
 */
UCLASS(Config = ToolMaturity, DefaultConfig, meta = (DisplayName = "Tool Maturity"))
class NARSHAMCP_API UCUToolMaturitySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ============================================
	// Production Group (4 tools - enabled by default)
	// ============================================

	/** Symbol search, hierarchy, caller tracing */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Production")
	bool ue_analyze_symbols = true;

	/** Server health status check */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Production")
	bool ue_check_health = true;

	/** Unified AI management (StateTree + BehaviorTree) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Production")
	bool ue_manage_ai = true;

	/** MCP tool documentation search and lookup */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Production")
	bool ue_tool_docs = true;

	// ============================================
	// Beta Group (13 tools - enabled by default)
	// ============================================

	/** Auto error resolution with fix strategies */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_fix_errors = true;

	/** GameplayTag/GAS/Input management */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_manage_gameplay = true;

	/** Blueprint execution tracing */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_trace_execution = true;

	/** Blueprint analysis, creation, modification */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_manage_blueprint = true;

	/** C++ boilerplate code generation */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_generate_code = true;

	/** Config option search/analysis */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_analyze_config = true;

	/** Asset search in Content Browser */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_search_assets = true;

	/** Editor actor spawn/transform/selection */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_editor_actors = true;

	/** Read-only actor/property query (Issue #27644) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_query_actors = true;

	/** Editor asset create/import/save */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_editor_assets = true;

	/** Editor debug + message log inspection */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_editor_debug = true;

	/** Editor automation + Remote Control */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_editor_automation = true;

	/** Cache statistics and control */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_cache_control = true;

	/** Sequencer structure inspection */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Beta")
	bool ue_sequencer_structure = true;

	// ============================================
	// Experimental Group (5 tools - enable as needed)
	// ============================================

	/** Workflow automation */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Experimental")
	bool ue_run_workflow = false;

	/** Material analysis and modification */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Experimental")
	bool ue_manage_material = false;

	/** PCG (Procedural Content Generation) analysis and modification */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Experimental")
	bool ue_manage_pcg = false;

	/** Authentication / telemetry (developer-only) */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Experimental")
	bool ue_auth = false;

	/** Unreal Insights trace analysis */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Experimental")
	bool ue_analyze_insights = false;

	// ============================================
	// Helper Functions
	// ============================================

	/**
	 * Check if a specific tool is enabled
	 *
	 * @param ToolName - Tool name (e.g., "ue_fix_errors")
	 * @return True if tool is enabled (false for an unknown tool)
	 */
	bool IsToolEnabled(const FString& ToolName) const;

	/**
	 * Set tool enabled state at runtime (does NOT persist to INI)
	 *
	 * @param ToolName - Tool name to modify
	 * @param bEnabled - New enabled state
	 *
	 * Note: For persistent changes, use ue_editor_automation(operation="set_tool_enabled")
	 */
	void SetToolEnabled(const FString& ToolName, bool bEnabled);

	/**
	 * Get all tool states as a map (name -> enabled).
	 *
	 * Reflection-driven: every `ue_*` bool UPROPERTY on this class is included, so the
	 * map stays in lockstep with the field roster above without a hand-maintained list.
	 *
	 * @return TMap of tool name to enabled state
	 */
	TMap<FString, bool> GetAllToolStates() const;

	/** Get container name for Project Settings */
	virtual FName GetContainerName() const override { return TEXT("Project"); }

	/** Get category name for Project Settings */
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Get section name for Project Settings */
	virtual FName GetSectionName() const override { return TEXT("NarshaMCP - Tool Maturity"); }

#if WITH_EDITOR
	/** Get section text for Project Settings UI */
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("CUToolMaturitySettings", "SectionText", "Tool Maturity");
	}

	/** Get section description for Project Settings UI */
	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("CUToolMaturitySettings", "SectionDescription",
			"Configure which MCP tools are available. Tools are grouped by maturity level.");
	}
#endif

	UCUToolMaturitySettings();
};
