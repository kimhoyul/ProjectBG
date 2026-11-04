// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5656: AI Chat Tool Widget Factory — creates native UE widgets for MCP tool results

#pragma once

#include "CoreMinimal.h"
#include "CUChatToolResult.h"

/**
 * Factory for creating native UE Slate widgets from MCP tool results (Issue #5656).
 *
 * Singleton with TMap<FString, FToolWidgetCreator> registry.
 * Dispatch priority: operation-specific ("tool:op") > tool-level ("tool") > generic JSON tree.
 *
 * 10 registered widget creators + generic fallback:
 *   1. ue_analyze_symbols → STreeView (symbol hierarchy)
 *   2. ue_manage_blueprint → STreeView (graph nodes)
 *   3. ue_fix_errors → SListView (error list with severity)
 *   4. ue_search_assets → SListView (asset list)
 *   5. ue_generate_code → SMultiLineEditableText (code block)
 *   6. ue_manage_material → SListView (material params)
 *   7. ue_check_health → Status dashboard (grid)
 *   8. ue_analyze_config → Key-value table
 *   9. ue_manage_gameplay → STreeView (tag tree)
 *  10. ue_trace_execution → SVerticalBox (execution flow)
 *  --  Generic fallback → STreeView (recursive JSON key-value)
 */
class FCUChatToolWidgetFactory
{
public:
	using FToolWidgetCreator = TFunction<TSharedRef<SWidget>(const FCUChatToolResult&)>;

	/** Get singleton instance */
	static FCUChatToolWidgetFactory& Get();

	/** Create the full widget (container + content) for a tool result */
	TSharedRef<SWidget> CreateContentWidget(const FCUChatToolResult& Result);

	/** Register a custom widget creator for a tool (or tool:operation) */
	void RegisterCreator(const FString& Key, FToolWidgetCreator Creator);

	/** Check if a creator exists for the given key */
	bool HasCreator(const FString& Key) const;

private:
	FCUChatToolWidgetFactory();

	/** Creator registry: "tool_name" or "tool_name:operation" → creator function */
	TMap<FString, FToolWidgetCreator> Creators;

	/** Register all built-in widget creators */
	void RegisterBuiltInCreators();

	// === Built-in widget creators ===
	static TSharedRef<SWidget> CreateSymbolsWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateBlueprintWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateErrorListWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateAssetSearchWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateCodeGenWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateMaterialWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateHealthDashWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateConfigWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateGameplayWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateExecutionFlowWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateGenericJsonWidget(const FCUChatToolResult& Result);

	// === Phase 9-1 (Issue #8836) — level analysis widgets ===
	static TSharedRef<SWidget> CreateLevelSummaryBarChartWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateLevelDiagnosticCardWidget(const FCUChatToolResult& Result);
	static TSharedRef<SWidget> CreateLevelActorListWidget(const FCUChatToolResult& Result);

	// === Phase 9-2 (Issue #8837) — spatial heatmap widget ===
	static TSharedRef<SWidget> CreateLevelSpatialGridWidget(const FCUChatToolResult& Result);

	// === Phase 9-3 (Issue #8838) — graph diff status card ===
	static TSharedRef<SWidget> CreateLevelMarkTransactionWidget(const FCUChatToolResult& Result);

	// === Phase 10-2 (Issue #8841) — timeline bar (subscribes to OnEventBroadcast live) ===
	static TSharedRef<SWidget> CreateLevelWorkflowLogTimelineWidget(const FCUChatToolResult& Result);

	// === Phase 11-1 (Issue #8843) — replay script preview ===
	static TSharedRef<SWidget> CreateLevelReplayScriptWidget(const FCUChatToolResult& Result);

	// === Phase 11-2 (Issue #8844) — marker catalog ===
	static TSharedRef<SWidget> CreateLevelSnapshotStatusWidget(const FCUChatToolResult& Result);

	// === Phase 11-3 (Issue #8845) — replay execution / dry-run summary ===
	static TSharedRef<SWidget> CreateLevelReplayTransactionsWidget(const FCUChatToolResult& Result);

	// === JSON tree helpers ===
	/** Recursively build a widget tree from a JSON value */
	static TSharedRef<SWidget> BuildJsonValueWidget(const FString& Key, const TSharedPtr<FJsonValue>& Value, int32 Depth);

	/** Build a key-value row for the JSON tree */
	static TSharedRef<SWidget> BuildJsonRow(const FString& Key, const FString& Value, const FLinearColor& ValueColor);
};
