// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5656: AI Chat Tool Widget Factory implementation

#include "Slate/CUChatToolWidgetFactory.h"
#include "Slate/SCUChatBarChart.h"
#include "Slate/SCUChatDiagnosticCard.h"
#include "Slate/SCUChatActorList.h"
#include "Slate/SCUChatSpatialGrid.h"
#include "Slate/SCUChatGraphDiff.h"
#include "Slate/SCUChatTimelineBar.h"
#include "Slate/SCUChatReplayWidget.h"
#include "Slate/SCUChatMarkerCatalog.h"
#include "Slate/SCUChatWorkflowWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Dom/JsonValue.h"
// Issue #8839: deep-link click dispatch via subsystem
#include "CUChatPanelSubsystem.h"
#include "Editor.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPChatWidget, Log, All);

// Configuration constants
namespace FactoryConstants
{
	static constexpr int32 MaxDisplayItems = 100;
	static constexpr int32 MaxRecursionDepth = 5;
}

// ============================================================================
// Issue #8839 (Phase 9-4) — deep-link click dispatch
// ============================================================================
//
// Helpers for routing tool-result row clicks into UCUChatPanelSubsystem.
// Static functions in this file have no instance state, so we go through
// GEditor->GetEditorSubsystem<UCUChatPanelSubsystem>().
//
// Scope (this PR): symbols → ue-source://, assets → ue-asset://.
// Deferred (follow-up): Blueprint node focus, error auto-fix, health diag
// request — each requires additional infra (BP editor focus API access,
// BackendClient capture, diagnostic API) outside the click-handler scope.
namespace ChatToolFactoryClick
{
	static void DispatchSourceLink(const FString& Url)
	{
		if (Url.IsEmpty()) return;
		if (!GEditor) return;
		if (UCUChatPanelSubsystem* Subsystem = GEditor->GetEditorSubsystem<UCUChatPanelSubsystem>())
		{
			Subsystem->HandleSourceLink(Url);
		}
	}

	static void DispatchAssetLink(const FString& Url)
	{
		if (Url.IsEmpty()) return;
		if (!GEditor) return;
		if (UCUChatPanelSubsystem* Subsystem = GEditor->GetEditorSubsystem<UCUChatPanelSubsystem>())
		{
			Subsystem->HandleAssetLink(Url);
		}
	}

	/** Build a ue-source:// URL from file path + optional 1-based line. */
	static FString BuildSourceUrl(const FString& File, int32 Line)
	{
		if (File.IsEmpty()) return FString();
		return Line > 0
			? FString::Printf(TEXT("ue-source://%s:%d"), *File, Line)
			: FString::Printf(TEXT("ue-source://%s"), *File);
	}

	/** Build a ue-asset:// URL from object path. */
	static FString BuildAssetUrl(const FString& Path)
	{
		if (Path.IsEmpty()) return FString();
		// Path may already contain ue-asset:// prefix from upstream — keep as-is
		if (Path.StartsWith(TEXT("ue-asset://"))) return Path;
		return FString::Printf(TEXT("ue-asset://%s"), *Path);
	}
}

// ============================================================================
// Singleton
// ============================================================================

FCUChatToolWidgetFactory& FCUChatToolWidgetFactory::Get()
{
	static FCUChatToolWidgetFactory Instance;
	return Instance;
}

FCUChatToolWidgetFactory::FCUChatToolWidgetFactory()
{
	RegisterBuiltInCreators();
}

// ============================================================================
// Registration
// ============================================================================

void FCUChatToolWidgetFactory::RegisterCreator(const FString& Key, FToolWidgetCreator Creator)
{
	Creators.Add(Key, MoveTemp(Creator));
}

bool FCUChatToolWidgetFactory::HasCreator(const FString& Key) const
{
	return Creators.Contains(Key);
}

void FCUChatToolWidgetFactory::RegisterBuiltInCreators()
{
	// 1. Symbol analysis
	RegisterCreator(TEXT("ue_analyze_symbols"), &CreateSymbolsWidget);

	// 2. Blueprint structure
	RegisterCreator(TEXT("ue_manage_blueprint"), &CreateBlueprintWidget);

	// 3. Error list
	RegisterCreator(TEXT("ue_fix_errors"), &CreateErrorListWidget);

	// 4. Asset search
	RegisterCreator(TEXT("ue_search_assets"), &CreateAssetSearchWidget);

	// 5. Code generation
	RegisterCreator(TEXT("ue_generate_code"), &CreateCodeGenWidget);

	// 6. Material
	RegisterCreator(TEXT("ue_manage_material"), &CreateMaterialWidget);

	// 7. Health check
	RegisterCreator(TEXT("ue_check_health"), &CreateHealthDashWidget);

	// 8. Config
	RegisterCreator(TEXT("ue_analyze_config"), &CreateConfigWidget);

	// 9. Gameplay/tags
	RegisterCreator(TEXT("ue_manage_gameplay"), &CreateGameplayWidget);

	// 10. Execution flow
	RegisterCreator(TEXT("ue_trace_execution"), &CreateExecutionFlowWidget);

	// === Phase 9-1 (Issue #8836) — level analysis widgets ===
	// Operation-specific keys take dispatch priority over tool-level keys (see CreateContentWidget).
	RegisterCreator(TEXT("ue_manage_project_ops:level_get_summary"), &CreateLevelSummaryBarChartWidget);
	RegisterCreator(TEXT("ue_manage_project_ops:level_diagnose"), &CreateLevelDiagnosticCardWidget);
	RegisterCreator(TEXT("ue_manage_project_ops:level_find_actors"), &CreateLevelActorListWidget);

	// === Phase 9-2 (Issue #8837) — spatial density heatmap ===
	RegisterCreator(TEXT("ue_manage_project_ops:level_spatial_analysis"), &CreateLevelSpatialGridWidget);

	// === Phase 9-3 (Issue #8838) — graph snapshot marker card ===
	RegisterCreator(TEXT("ue_manage_project_ops:level_mark_transaction"), &CreateLevelMarkTransactionWidget);

	// === Phase 10-2 (Issue #8841) — workflow log timeline bar ===
	RegisterCreator(TEXT("ue_manage_project_ops:level_get_workflow_log"), &CreateLevelWorkflowLogTimelineWidget);

	// === Phase 11-1 (Issue #8843) — replay script preview ===
	RegisterCreator(TEXT("ue_manage_project_ops:level_generate_replay_script"), &CreateLevelReplayScriptWidget);

	// === Phase 11-2 (Issue #8844) — marker catalog ===
	RegisterCreator(TEXT("ue_manage_project_ops:level_snapshot_status"), &CreateLevelSnapshotStatusWidget);

	// === Phase 11-3 (Issue #8845) — replay execution / dry-run summary ===
	RegisterCreator(TEXT("ue_manage_project_ops:level_replay_transactions"), &CreateLevelReplayTransactionsWidget);
}

// ============================================================================
// CreateContentWidget — dispatch logic
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateContentWidget(const FCUChatToolResult& Result)
{
	// Priority 1: operation-specific key ("tool_name:operation")
	if (!Result.Operation.IsEmpty())
	{
		FString OpKey = FString::Printf(TEXT("%s:%s"), *Result.ToolName, *Result.Operation);
		if (const FToolWidgetCreator* Creator = Creators.Find(OpKey))
		{
			return (*Creator)(Result);
		}
	}

	// Priority 2: tool-level key ("tool_name")
	if (const FToolWidgetCreator* Creator = Creators.Find(Result.ToolName))
	{
		return (*Creator)(Result);
	}

	// Priority 3: generic JSON tree fallback
	return CreateGenericJsonWidget(Result);
}

// ============================================================================
// Helper: Extract JSON array from result (common pattern)
// ============================================================================

static const TArray<TSharedPtr<FJsonValue>>* GetResultArray(const FCUChatToolResult& Result, const FString& FieldName)
{
	if (!Result.JsonResult.IsValid())
	{
		return nullptr;
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	Result.JsonResult->TryGetArrayField(FieldName, Arr);
	return Arr;
}

static FString GetStringField(const TSharedPtr<FJsonObject>& Obj, const FString& Field, const FString& Default = FString())
{
	if (!Obj.IsValid()) return Default;
	FString Val;
	if (Obj->TryGetStringField(Field, Val)) return Val;
	return Default;
}

// ============================================================================
// 1. ue_analyze_symbols — SVerticalBox list of symbols
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateSymbolsWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Symbols = GetResultArray(Result, TEXT("symbols"));
	if (!Symbols)
	{
		Symbols = GetResultArray(Result, TEXT("results"));
	}

	if (Symbols)
	{
		int32 Count = 0;
		for (const auto& Item : *Symbols)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break; // Truncate large results

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj) continue;

			FString Name = GetStringField(*Obj, TEXT("name"));
			FString Kind = GetStringField(*Obj, TEXT("kind"), TEXT("symbol"));
			FString File = GetStringField(*Obj, TEXT("file"));
			int32 Line = 0;
			{
				double LineTmp = 0.0;
				if ((*Obj)->TryGetNumberField(TEXT("line"), LineTmp))
				{
					Line = (int32)LineTmp;
				}
			}
			// Issue #8839: build ue-source:// URL once per row for click dispatch
			const FString SourceUrl = ChatToolFactoryClick::BuildSourceUrl(File, Line);

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(0.0f))
				.ToolTipText(SourceUrl.IsEmpty()
					? FText::GetEmpty()
					: FText::FromString(FString::Printf(TEXT("Open %s"), *SourceUrl)))
				.OnClicked_Lambda([SourceUrl]() -> FReply {
					ChatToolFactoryClick::DispatchSourceLink(SourceUrl);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(FMargin(3.0f, 1.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(Kind))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.8f, 0.5f)))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Name))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.6f)))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText::FromString(File))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					]
				]
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 2. ue_manage_blueprint — SVerticalBox list of nodes
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateBlueprintWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Nodes = GetResultArray(Result, TEXT("nodes"));
	if (!Nodes)
	{
		Nodes = GetResultArray(Result, TEXT("graphs"));
	}

	if (Nodes)
	{
		int32 Count = 0;
		for (const auto& Item : *Nodes)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break;

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj) continue;

			FString Name = GetStringField(*Obj, TEXT("name"));
			FString NodeType = GetStringField(*Obj, TEXT("type"), TEXT("node"));

			// Color-code by type
			FLinearColor TypeColor(0.5f, 0.7f, 1.0f); // default blue
			if (NodeType.Contains(TEXT("Function"))) TypeColor = FLinearColor(0.3f, 0.8f, 0.3f); // green
			else if (NodeType.Contains(TEXT("Event"))) TypeColor = FLinearColor(1.0f, 0.3f, 0.3f); // red
			else if (NodeType.Contains(TEXT("Variable"))) TypeColor = FLinearColor(0.8f, 0.6f, 1.0f); // purple

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(FMargin(3.0f, 1.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(NodeType))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
						.ColorAndOpacity(FSlateColor(TypeColor))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Name))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
				]
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 3. ue_fix_errors — error list with severity icons
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateErrorListWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Errors = GetResultArray(Result, TEXT("errors"));
	if (!Errors)
	{
		Errors = GetResultArray(Result, TEXT("diagnostics"));
	}
	if (!Errors)
	{
		Errors = GetResultArray(Result, TEXT("results"));
	}

	if (Errors)
	{
		int32 Count = 0;
		for (const auto& Item : *Errors)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break;

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj) continue;

			FString Message = GetStringField(*Obj, TEXT("message"));
			FString Severity = GetStringField(*Obj, TEXT("severity"), TEXT("error"));
			FString File = GetStringField(*Obj, TEXT("file"));
			double Line = 0;
			(*Obj)->TryGetNumberField(TEXT("line"), Line);

			// Severity icon and color
			FString Icon;
			FLinearColor SevColor;
			if (Severity.Contains(TEXT("error"), ESearchCase::IgnoreCase))
			{
				Icon = TEXT("\x2716"); // ✖
				SevColor = FLinearColor(1.0f, 0.3f, 0.3f);
			}
			else if (Severity.Contains(TEXT("warn"), ESearchCase::IgnoreCase))
			{
				Icon = TEXT("\x26A0"); // ⚠
				SevColor = FLinearColor(1.0f, 0.8f, 0.2f);
			}
			else
			{
				Icon = TEXT("\x2139"); // ℹ
				SevColor = FLinearColor(0.4f, 0.7f, 1.0f);
			}

			FString Location;
			if (!File.IsEmpty())
			{
				Location = Line > 0
					? FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(File), (int32)Line)
					: FPaths::GetCleanFilename(File);
			}

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				.VAlign(VAlign_Top)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Icon))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(SevColor))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Message))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
						.AutoWrapText(true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(Location))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						.Visibility(Location.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
				]
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 4. ue_search_assets — asset list
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateAssetSearchWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Assets = GetResultArray(Result, TEXT("assets"));
	if (!Assets)
	{
		Assets = GetResultArray(Result, TEXT("results"));
	}

	if (Assets)
	{
		int32 Count = 0;
		for (const auto& Item : *Assets)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break;

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj) continue;

			FString Name = GetStringField(*Obj, TEXT("name"));
			FString AssetType = GetStringField(*Obj, TEXT("type"), TEXT("asset"));
			FString Path = GetStringField(*Obj, TEXT("path"));
			// Issue #8839: build ue-asset:// URL once per row for click dispatch
			const FString AssetUrl = ChatToolFactoryClick::BuildAssetUrl(Path);

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "NoBorder")
				.ContentPadding(FMargin(0.0f))
				.ToolTipText(AssetUrl.IsEmpty()
					? FText::GetEmpty()
					: FText::FromString(FString::Printf(TEXT("Open %s"), *AssetUrl)))
				.OnClicked_Lambda([AssetUrl]() -> FReply {
					ChatToolFactoryClick::DispatchAssetLink(AssetUrl);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(SBorder)
						.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
						.Padding(FMargin(3.0f, 1.0f))
						[
							SNew(STextBlock)
							.Text(FText::FromString(AssetType))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.5f, 1.0f)))
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Name))
							.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(Path))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
							.Visibility(Path.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
						]
					]
				]
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 5. ue_generate_code — code block with copy button
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateCodeGenWidget(const FCUChatToolResult& Result)
{
	FString Code;
	if (Result.JsonResult.IsValid())
	{
		Result.JsonResult->TryGetStringField(TEXT("code"), Code);
		if (Code.IsEmpty())
		{
			Result.JsonResult->TryGetStringField(TEXT("header"), Code);
		}
		if (Code.IsEmpty())
		{
			Result.JsonResult->TryGetStringField(TEXT("source"), Code);
		}
		if (Code.IsEmpty())
		{
			Result.JsonResult->TryGetStringField(TEXT("text"), Code);
		}
	}

	if (Code.IsEmpty())
	{
		return CreateGenericJsonWidget(Result);
	}

	TSharedPtr<FString> CodePtr = MakeShared<FString>(Code);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "NoBorder")
			.OnClicked_Lambda([CodePtr]() -> FReply {
				FPlatformApplicationMisc::ClipboardCopy(**CodePtr);
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(NSLOCTEXT("NarshaMCP", "CopyCode", "Copy to Clipboard"))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.7f, 1.0f)))
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("ToolPanel.DarkGroupBorder"))
			.Padding(FMargin(8.0f))
			[
				SNew(STextBlock)
				.Text(FText::FromString(Code))
				.Font(FCoreStyle::GetDefaultFontStyle("Mono", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.9f, 0.8f)))
				.AutoWrapText(true)
			]
		];
}

// ============================================================================
// 6. ue_manage_material — material parameters list
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateMaterialWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Params = GetResultArray(Result, TEXT("parameters"));
	if (!Params)
	{
		Params = GetResultArray(Result, TEXT("nodes"));
	}
	if (!Params)
	{
		Params = GetResultArray(Result, TEXT("results"));
	}

	if (Params)
	{
		int32 Count = 0;
		for (const auto& Item : *Params)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break;

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj) continue;

			FString Name = GetStringField(*Obj, TEXT("name"));
			FString ParamType = GetStringField(*Obj, TEXT("type"), TEXT("param"));
			FString Value = GetStringField(*Obj, TEXT("value"));

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(SBorder)
					.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
					.Padding(FMargin(3.0f, 1.0f))
					[
						SNew(STextBlock)
						.Text(FText::FromString(ParamType))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 7))
						.ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.6f, 0.3f)))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Name))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.5f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Value))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.8f, 0.6f)))
					.Visibility(Value.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 7. ue_check_health — status dashboard
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateHealthDashWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	// Iterate all fields and show as status rows
	for (const auto& Pair : Result.JsonResult->Values)
	{
		FString Value;
		FLinearColor StatusColor(0.6f, 0.6f, 0.6f);

		if (Pair.Value->Type == EJson::String)
		{
			Value = Pair.Value->AsString();
		}
		else if (Pair.Value->Type == EJson::Boolean)
		{
			bool bVal = Pair.Value->AsBool();
			Value = bVal ? TEXT("OK") : TEXT("FAIL");
			StatusColor = bVal ? FLinearColor(0.3f, 0.9f, 0.3f) : FLinearColor(1.0f, 0.3f, 0.3f);
		}
		else if (Pair.Value->Type == EJson::Number)
		{
			Value = FString::Printf(TEXT("%.0f"), Pair.Value->AsNumber());
		}
		else
		{
			continue; // Skip nested objects/arrays for dashboard view
		}

		// Determine status color from value content
		if (Value.Contains(TEXT("ok"), ESearchCase::IgnoreCase) || Value.Contains(TEXT("pass"), ESearchCase::IgnoreCase) || Value.Contains(TEXT("healthy"), ESearchCase::IgnoreCase))
		{
			StatusColor = FLinearColor(0.3f, 0.9f, 0.3f);
		}
		else if (Value.Contains(TEXT("warn"), ESearchCase::IgnoreCase) || Value.Contains(TEXT("partial"), ESearchCase::IgnoreCase))
		{
			StatusColor = FLinearColor(1.0f, 0.8f, 0.2f);
		}
		else if (Value.Contains(TEXT("fail"), ESearchCase::IgnoreCase) || Value.Contains(TEXT("error"), ESearchCase::IgnoreCase))
		{
			StatusColor = FLinearColor(1.0f, 0.3f, 0.3f);
		}

		Container->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(0.4f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString(Pair.Key)))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(0.6f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Value))
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(FSlateColor(StatusColor))
			]
		];
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 8. ue_analyze_config — key-value table
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateConfigWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Configs = GetResultArray(Result, TEXT("configs"));
	if (!Configs)
	{
		Configs = GetResultArray(Result, TEXT("results"));
	}

	if (Configs)
	{
		int32 Count = 0;
		for (const auto& Item : *Configs)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break;

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj) continue;

			FString Key = GetStringField(*Obj, TEXT("key"));
			FString Value = GetStringField(*Obj, TEXT("value"));
			FString Source = GetStringField(*Obj, TEXT("source"));

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Key))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.4f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Value))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.6f, 0.9f, 0.6f)))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(0.2f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Source))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
					.Visibility(Source.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
				]
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 9. ue_manage_gameplay — gameplay tag tree
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateGameplayWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Tags = GetResultArray(Result, TEXT("tags"));
	if (!Tags)
	{
		Tags = GetResultArray(Result, TEXT("abilities"));
	}
	if (!Tags)
	{
		Tags = GetResultArray(Result, TEXT("results"));
	}

	if (Tags)
	{
		int32 Count = 0;
		for (const auto& Item : *Tags)
		{
			if (Count >= FactoryConstants::MaxDisplayItems) break;

			FString TagStr;
			if (Item->Type == EJson::String)
			{
				TagStr = Item->AsString();
			}
			else
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Item->TryGetObject(Obj) && Obj)
				{
					TagStr = GetStringField(*Obj, TEXT("name"));
					if (TagStr.IsEmpty())
					{
						TagStr = GetStringField(*Obj, TEXT("tag"));
					}
				}
			}

			if (TagStr.IsEmpty()) continue;

			// Calculate indent from tag depth (dots)
			int32 Depth = 0;
			for (const TCHAR& C : TagStr)
			{
				if (C == TEXT('.')) ++Depth;
			}

			Container->AddSlot()
			.AutoHeight()
			.Padding(Depth * 12.0f, 1.0f, 0.0f, 1.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TagStr))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.7f, 0.3f)))
			];
			++Count;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// 10. ue_trace_execution — sequential flow steps
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateExecutionFlowWidget(const FCUChatToolResult& Result)
{
	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	const TArray<TSharedPtr<FJsonValue>>* Steps = GetResultArray(Result, TEXT("steps"));
	if (!Steps)
	{
		Steps = GetResultArray(Result, TEXT("flow"));
	}
	if (!Steps)
	{
		Steps = GetResultArray(Result, TEXT("nodes"));
	}

	if (Steps)
	{
		int32 Index = 0;
		for (const auto& Item : *Steps)
		{
			if (Index >= 100) break;

			FString StepName;
			FString StepType;

			if (Item->Type == EJson::String)
			{
				StepName = Item->AsString();
			}
			else
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Item->TryGetObject(Obj) && Obj)
				{
					StepName = GetStringField(*Obj, TEXT("name"));
					StepType = GetStringField(*Obj, TEXT("type"));
				}
			}

			if (StepName.IsEmpty()) continue;

			// Arrow between steps (not before first)
			if (Index > 0)
			{
				Container->AddSlot()
				.AutoHeight()
				.Padding(16.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\x2193"))) // ↓
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.4f, 0.4f)))
				];
			}

			Container->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
				.Padding(FMargin(6.0f, 3.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.0f, 0.0f, 6.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%d."), Index + 1)))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.7f, 1.0f)))
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(StepName))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.85f, 0.85f, 0.85f)))
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(STextBlock)
						.Text(FText::FromString(StepType))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
						.Visibility(StepType.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
					]
				]
			];
			++Index;
		}
	}

	if (Container->NumSlots() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}
	return Container;
}

// ============================================================================
// Generic JSON tree fallback
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateGenericJsonWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		FString DisplayText = Result.bIsError ? Result.ErrorMessage : TEXT("(empty result)");
		return SNew(STextBlock)
			.Text(FText::FromString(DisplayText))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(
				Result.bIsError ? 1.0f : 0.6f,
				Result.bIsError ? 0.3f : 0.6f,
				Result.bIsError ? 0.3f : 0.6f)))
			.AutoWrapText(true);
	}

	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	for (const auto& Pair : Result.JsonResult->Values)
	{
		Container->AddSlot()
		.AutoHeight()
		[
			BuildJsonValueWidget(FString(Pair.Key), Pair.Value, 0)
		];
	}

	return Container;
}

TSharedRef<SWidget> FCUChatToolWidgetFactory::BuildJsonValueWidget(const FString& Key, const TSharedPtr<FJsonValue>& Value, int32 Depth)
{
	if (Depth > FactoryConstants::MaxRecursionDepth) // Prevent infinite recursion
	{
		return BuildJsonRow(Key, TEXT("..."), FLinearColor(0.5f, 0.5f, 0.5f));
	}

	if (!Value.IsValid())
	{
		return BuildJsonRow(Key, TEXT("null"), FLinearColor(0.5f, 0.5f, 0.5f));
	}

	switch (Value->Type)
	{
	case EJson::String:
		return BuildJsonRow(Key, Value->AsString(), FLinearColor(0.6f, 0.9f, 0.6f));

	case EJson::Number:
		return BuildJsonRow(Key, FString::Printf(TEXT("%g"), Value->AsNumber()), FLinearColor(0.6f, 0.7f, 1.0f));

	case EJson::Boolean:
		return BuildJsonRow(Key, Value->AsBool() ? TEXT("true") : TEXT("false"),
			Value->AsBool() ? FLinearColor(0.3f, 0.9f, 0.3f) : FLinearColor(1.0f, 0.3f, 0.3f));

	case EJson::Array:
	{
		TSharedRef<SVerticalBox> ArrayBox = SNew(SVerticalBox);
		ArrayBox->AddSlot()
		.AutoHeight()
		[
			BuildJsonRow(Key, FString::Printf(TEXT("[%d items]"), Value->AsArray().Num()), FLinearColor(0.7f, 0.7f, 0.7f))
		];

		int32 Count = 0;
		for (const auto& Item : Value->AsArray())
		{
			if (Count >= 20) // Truncate large arrays
			{
				ArrayBox->AddSlot()
				.AutoHeight()
				.Padding((Depth + 1) * 12.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("... (%d more)"), Value->AsArray().Num() - 20)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.5f, 0.5f)))
				];
				break;
			}

			ArrayBox->AddSlot()
			.AutoHeight()
			.Padding((Depth + 1) * 12.0f, 0.0f, 0.0f, 0.0f)
			[
				BuildJsonValueWidget(FString::Printf(TEXT("[%d]"), Count), Item, Depth + 1)
			];
			++Count;
		}
		return ArrayBox;
	}

	case EJson::Object:
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Value->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid())
		{
			return BuildJsonRow(Key, TEXT("{}"), FLinearColor(0.5f, 0.5f, 0.5f));
		}

		TSharedRef<SVerticalBox> ObjBox = SNew(SVerticalBox);
		if (!Key.IsEmpty())
		{
			ObjBox->AddSlot()
			.AutoHeight()
			[
				BuildJsonRow(Key, TEXT("{...}"), FLinearColor(0.7f, 0.7f, 0.7f))
			];
		}

		for (const auto& Pair : (*ObjPtr)->Values)
		{
			ObjBox->AddSlot()
			.AutoHeight()
			.Padding((Depth + 1) * 12.0f, 0.0f, 0.0f, 0.0f)
			[
				BuildJsonValueWidget(FString(Pair.Key), Pair.Value, Depth + 1)
			];
		}
		return ObjBox;
	}

	default:
		return BuildJsonRow(Key, TEXT("null"), FLinearColor(0.5f, 0.5f, 0.5f));
	}
}

TSharedRef<SWidget> FCUChatToolWidgetFactory::BuildJsonRow(const FString& Key, const FString& Value, const FLinearColor& ValueColor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Key.IsEmpty() ? TEXT("") : Key + TEXT(":")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(FLinearColor(0.7f, 0.7f, 0.7f)))
			.Visibility(Key.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Value))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.ColorAndOpacity(FSlateColor(ValueColor))
			.AutoWrapText(true)
		];
}

// ============================================================================
// Phase 9-1 (Issue #8836) — level analysis widgets
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelSummaryBarChartWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	// summaries[] is an array — take the first match for the chart.
	const TArray<TSharedPtr<FJsonValue>>* SummariesArr = nullptr;
	if (!Result.JsonResult->TryGetArrayField(TEXT("summaries"), SummariesArr) || !SummariesArr || SummariesArr->Num() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	const TSharedPtr<FJsonObject>* FirstSummaryPtr = nullptr;
	if (!(*SummariesArr)[0]->TryGetObject(FirstSummaryPtr) || !FirstSummaryPtr || !(*FirstSummaryPtr).IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}
	const TSharedPtr<FJsonObject>& FirstSummary = *FirstSummaryPtr;

	FString LevelName;
	FirstSummary->TryGetStringField(TEXT("level_name"), LevelName);

	int32 ActorCount = 0;
	{
		double Tmp = 0.0;
		if (FirstSummary->TryGetNumberField(TEXT("actor_count"), Tmp))
		{
			ActorCount = (int32)Tmp;
		}
	}

	TArray<FCUChatBarItem> Items;

	// Extract category_breakdown (object with category-name → count)
	const TSharedPtr<FJsonObject>* CategoryObjPtr = nullptr;
	if (FirstSummary->TryGetObjectField(TEXT("category_breakdown"), CategoryObjPtr) && CategoryObjPtr && (*CategoryObjPtr).IsValid())
	{
		for (const auto& Pair : (*CategoryObjPtr)->Values)
		{
			if (!Pair.Value.IsValid()) continue;
			double Count = 0.0;
			if (Pair.Value->TryGetNumber(Count) && Count > 0.0)
			{
				FCUChatBarItem Item;
				Item.Label = FString(Pair.Key);
				Item.Value = Count;
				Item.Color = SCUChatBarChart::GetCategoryColor(Item.Label);
				Items.Add(MoveTemp(Item));
			}
		}
		// Sort descending by value for stable, readable output
		Items.Sort([](const FCUChatBarItem& A, const FCUChatBarItem& B) { return A.Value > B.Value; });
	}

	if (Items.Num() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	const FString TitleText = LevelName.IsEmpty()
		? FString::Printf(TEXT("Category Breakdown (%d actors)"), ActorCount)
		: FString::Printf(TEXT("%s — Category Breakdown (%d actors)"), *LevelName, ActorCount);

	return SNew(SCUChatBarChart)
		.Items(Items)
		.Title(TitleText);
}

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelDiagnosticCardWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	int32 ErrorCount = 0;
	int32 WarningCount = 0;
	int32 InfoCount = 0;

	// summary object
	const TSharedPtr<FJsonObject>* SummaryPtr = nullptr;
	if (Result.JsonResult->TryGetObjectField(TEXT("summary"), SummaryPtr) && SummaryPtr && (*SummaryPtr).IsValid())
	{
		double Tmp = 0.0;
		if ((*SummaryPtr)->TryGetNumberField(TEXT("errors"), Tmp))   ErrorCount = (int32)Tmp;
		if ((*SummaryPtr)->TryGetNumberField(TEXT("warnings"), Tmp)) WarningCount = (int32)Tmp;
		if ((*SummaryPtr)->TryGetNumberField(TEXT("infos"), Tmp))    InfoCount = (int32)Tmp;
	}

	auto ParseEntries = [](const TArray<TSharedPtr<FJsonValue>>* Arr, bool bIsSuggestion) -> TArray<FCUChatDiagnosticEntry>
	{
		TArray<FCUChatDiagnosticEntry> Out;
		if (!Arr) return Out;
		for (const auto& Item : *Arr)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;
			FCUChatDiagnosticEntry Entry;
			Entry.Rule    = GetStringField(*Obj, TEXT("rule"));
			Entry.Message = GetStringField(*Obj, TEXT("message"));
			Entry.Entity  = GetStringField(*Obj, TEXT("entity"));
			Entry.Action  = GetStringField(*Obj, TEXT("action"));
			Entry.Severity = bIsSuggestion
				? ECUChatDiagnosticSeverity::Suggestion
				: SCUChatDiagnosticCard::ParseSeverityString(GetStringField(*Obj, TEXT("severity"), TEXT("Info")));
			(*Obj)->TryGetNumberField(TEXT("value"), Entry.Value);
			(*Obj)->TryGetNumberField(TEXT("threshold"), Entry.Threshold);
			Out.Add(MoveTemp(Entry));
		}
		return Out;
	};

	const TArray<TSharedPtr<FJsonValue>>* FindingsArr = nullptr;
	Result.JsonResult->TryGetArrayField(TEXT("findings"), FindingsArr);
	const TArray<TSharedPtr<FJsonValue>>* SuggestionsArr = nullptr;
	Result.JsonResult->TryGetArrayField(TEXT("suggestions"), SuggestionsArr);

	TArray<FCUChatDiagnosticEntry> Findings = ParseEntries(FindingsArr, /*bIsSuggestion=*/false);
	TArray<FCUChatDiagnosticEntry> Suggestions = ParseEntries(SuggestionsArr, /*bIsSuggestion=*/true);

	if (ErrorCount + WarningCount + InfoCount == 0 && Findings.Num() == 0 && Suggestions.Num() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	return SNew(SCUChatDiagnosticCard)
		.ErrorCount(ErrorCount)
		.WarningCount(WarningCount)
		.InfoCount(InfoCount)
		.Findings(Findings)
		.Suggestions(Suggestions);
}

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelActorListWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	const TArray<TSharedPtr<FJsonValue>>* ActorsArr = nullptr;
	if (!Result.JsonResult->TryGetArrayField(TEXT("actors"), ActorsArr) || !ActorsArr)
	{
		return CreateGenericJsonWidget(Result);
	}

	int32 TotalMatches = 0;
	{
		double Tmp = 0.0;
		if (Result.JsonResult->TryGetNumberField(TEXT("total_matches"), Tmp))
		{
			TotalMatches = (int32)Tmp;
		}
	}

	TArray<FCUChatActorEntry> Entries;
	Entries.Reserve(ActorsArr->Num());
	for (const auto& Item : *ActorsArr)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Item->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;
		FCUChatActorEntry Entry;
		Entry.ActorClass         = GetStringField(*Obj, TEXT("actor_class"));
		Entry.Category           = GetStringField(*Obj, TEXT("category"));
		Entry.LevelName          = GetStringField(*Obj, TEXT("level_name"));
		Entry.BlueprintClassPath = GetStringField(*Obj, TEXT("blueprint_class_path"));
		(*Obj)->TryGetBoolField(TEXT("is_blueprint"), Entry.bIsBlueprint);
		double FileSizeTmp = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("file_size"), FileSizeTmp))
		{
			Entry.FileSize = (int64)FileSizeTmp;
		}
		Entries.Add(MoveTemp(Entry));
	}

	if (Entries.Num() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	if (TotalMatches < Entries.Num())
	{
		TotalMatches = Entries.Num();
	}

	return SNew(SCUChatActorList)
		.Actors(Entries)
		.TotalMatches(TotalMatches);
}

// ============================================================================
// Phase 9-2 (Issue #8837) — spatial density heatmap
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelSpatialGridWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	const TArray<TSharedPtr<FJsonValue>>* CellsArr = nullptr;
	if (!Result.JsonResult->TryGetArrayField(TEXT("cells"), CellsArr) || !CellsArr || CellsArr->Num() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	TArray<FCUSpatialCell> Cells;
	Cells.Reserve(CellsArr->Num());
	for (const auto& Item : *CellsArr)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Item->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;

		FCUSpatialCell Cell;

		// "cell": [x, y]
		const TArray<TSharedPtr<FJsonValue>>* CellPair = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("cell"), CellPair) && CellPair && CellPair->Num() >= 2)
		{
			double TmpX = 0.0, TmpY = 0.0;
			(*CellPair)[0]->TryGetNumber(TmpX);
			(*CellPair)[1]->TryGetNumber(TmpY);
			Cell.CellX = (int32)TmpX;
			Cell.CellY = (int32)TmpY;
		}

		double TmpCount = 0.0;
		if ((*Obj)->TryGetNumberField(TEXT("actor_count"), TmpCount))
		{
			Cell.ActorCount = (int32)TmpCount;
		}
		(*Obj)->TryGetNumberField(TEXT("density"), Cell.Density);

		Cells.Add(MoveTemp(Cell));
	}

	if (Cells.Num() == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	// Stats from "stats" object
	double MaxDensity = 1.0;
	double DensityP95 = 1.0;
	int32 ActorsWithPos = 0;
	int32 ActorsWithoutPos = 0;
	const TSharedPtr<FJsonObject>* StatsPtr = nullptr;
	if (Result.JsonResult->TryGetObjectField(TEXT("stats"), StatsPtr) && StatsPtr && (*StatsPtr).IsValid())
	{
		(*StatsPtr)->TryGetNumberField(TEXT("max_density"), MaxDensity);
		(*StatsPtr)->TryGetNumberField(TEXT("density_p95"), DensityP95);
		double TmpAW = 0.0, TmpAWO = 0.0;
		if ((*StatsPtr)->TryGetNumberField(TEXT("actors_with_position"), TmpAW))
		{
			ActorsWithPos = (int32)TmpAW;
		}
		if ((*StatsPtr)->TryGetNumberField(TEXT("actors_without_position"), TmpAWO))
		{
			ActorsWithoutPos = (int32)TmpAWO;
		}
	}

	return SNew(SCUChatSpatialGrid)
		.Cells(Cells)
		.MaxDensity(MaxDensity)
		.DensityP95(DensityP95)
		.ActorsWithPosition(ActorsWithPos)
		.ActorsWithoutPosition(ActorsWithoutPos);
}

// ============================================================================
// Phase 9-3 (Issue #8838) — graph snapshot marker card
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelMarkTransactionWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	FString Action = GetStringField(Result.JsonResult, TEXT("action"));
	FString Label = GetStringField(Result.JsonResult, TEXT("label"));
	FString Warning = GetStringField(Result.JsonResult, TEXT("warning"));
	FString LogFile = GetStringField(Result.JsonResult, TEXT("log_file"));

	int32 SnapshotsCaptured = 0;
	int32 DiffsWritten = 0;
	{
		double Tmp = 0.0;
		if (Result.JsonResult->TryGetNumberField(TEXT("snapshots_captured"), Tmp))
		{
			SnapshotsCaptured = (int32)Tmp;
		}
		if (Result.JsonResult->TryGetNumberField(TEXT("diffs_written"), Tmp))
		{
			DiffsWritten = (int32)Tmp;
		}
	}

	// If we have absolutely no marker info, fall back to generic to avoid an empty card.
	if (Action.IsEmpty() && Label.IsEmpty() && SnapshotsCaptured == 0 && DiffsWritten == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	return SNew(SCUChatGraphDiff)
		.Action(Action)
		.Label(Label)
		.SnapshotsCaptured(SnapshotsCaptured)
		.DiffsWritten(DiffsWritten)
		.Warning(Warning)
		.LogFile(LogFile);
}

// ============================================================================
// Phase 10-2 (Issue #8841) — workflow log timeline bar
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelWorkflowLogTimelineWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	// Extract patterns.hourly_distribution → 24-element seed array
	TArray<int32> HourlySeed;
	HourlySeed.SetNumZeroed(24);

	const TSharedPtr<FJsonObject>* PatternsPtr = nullptr;
	if (Result.JsonResult->TryGetObjectField(TEXT("patterns"), PatternsPtr) && PatternsPtr && (*PatternsPtr).IsValid())
	{
		const TSharedPtr<FJsonObject>* HourlyPtr = nullptr;
		if ((*PatternsPtr)->TryGetObjectField(TEXT("hourly_distribution"), HourlyPtr) && HourlyPtr && (*HourlyPtr).IsValid())
		{
			// Issue #9560: FJsonObject keys on UE 5.8+ are FSharedString, which does
			// not expose operator*() returning TCHAR*. Constructing an FString from
			// the key first yields a TCHAR* that FCString::Atoi can consume. Matches
			// the #7368 invariant enforced by test_fsharedstring_all_jsonobject_values_wrapped.
			for (const auto& Pair : (*HourlyPtr)->Values)
			{
				if (!Pair.Value.IsValid()) continue;
				int32 Hour = FCString::Atoi(*FString(Pair.Key));
				if (Hour < 0 || Hour >= 24) continue;
				double Count = 0.0;
				if (Pair.Value->TryGetNumber(Count))
				{
					HourlySeed[Hour] = (int32)Count;
				}
			}
		}
	}

	return SNew(SCUChatTimelineBar)
		.HourlyDistribution(HourlySeed)
		.SubscribeToLiveEvents(true);
}

// ============================================================================
// Phase 11-1 (Issue #8843) — replay script preview
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelReplayScriptWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	FString PythonScript = GetStringField(Result.JsonResult, TEXT("python_script"));
	FString Warning = GetStringField(Result.JsonResult, TEXT("warning"));
	FString LargeWarning = GetStringField(Result.JsonResult, TEXT("large_script_warning"));

	int32 TotalEntries = 0, PythonActions = 0, MCPActions = 0, Skipped = 0;
	const TSharedPtr<FJsonObject>* StatsPtr = nullptr;
	if (Result.JsonResult->TryGetObjectField(TEXT("stats"), StatsPtr) && StatsPtr && (*StatsPtr).IsValid())
	{
		double Tmp = 0.0;
		if ((*StatsPtr)->TryGetNumberField(TEXT("total_entries"), Tmp))   TotalEntries = (int32)Tmp;
		if ((*StatsPtr)->TryGetNumberField(TEXT("python_actions"), Tmp))  PythonActions = (int32)Tmp;
		if ((*StatsPtr)->TryGetNumberField(TEXT("mcp_actions"), Tmp))     MCPActions = (int32)Tmp;
		if ((*StatsPtr)->TryGetNumberField(TEXT("skipped"), Tmp))         Skipped = (int32)Tmp;
	}

	// MCP calls
	TArray<FCUChatReplayMCPCall> MCPCalls;
	const TArray<TSharedPtr<FJsonValue>>* CallsArr = nullptr;
	if (Result.JsonResult->TryGetArrayField(TEXT("mcp_calls"), CallsArr) && CallsArr)
	{
		MCPCalls.Reserve(CallsArr->Num());
		for (const auto& Item : *CallsArr)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;
			FCUChatReplayMCPCall Call;
			Call.Operation = GetStringField(*Obj, TEXT("operation"));
			Call.DomainRouting = GetStringField(*Obj, TEXT("domain_routing"));
			// Params summary: serialize the params object compactly (best-effort)
			const TSharedPtr<FJsonObject>* ParamsPtr = nullptr;
			if ((*Obj)->TryGetObjectField(TEXT("params"), ParamsPtr) && ParamsPtr && (*ParamsPtr).IsValid())
			{
				TArray<FString> KeyPreviews;
				for (const auto& P : (*ParamsPtr)->Values)
				{
					if (KeyPreviews.Num() >= 3) { KeyPreviews.Add(TEXT("...")); break; }
					KeyPreviews.Add(FString(P.Key));
				}
				Call.ParamsPreview = FString::Printf(TEXT("{%s}"), *FString::Join(KeyPreviews, TEXT(", ")));
			}
			MCPCalls.Add(MoveTemp(Call));
		}
	}

	// If we have neither script nor stats, fall back
	if (PythonScript.IsEmpty() && MCPCalls.Num() == 0 && TotalEntries == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	return SNew(SCUChatReplayWidget)
		.PythonScript(PythonScript)
		.MCPCalls(MCPCalls)
		.TotalEntries(TotalEntries)
		.PythonActions(PythonActions)
		.MCPActions(MCPActions)
		.Skipped(Skipped)
		.Warning(Warning)
		.LargeScriptWarning(LargeWarning);
}

// ============================================================================
// Phase 11-2 (Issue #8844) — marker catalog
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelSnapshotStatusWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	bool bLogAvailable = false;
	int32 LogEntryCount = 0;
	Result.JsonResult->TryGetBoolField(TEXT("log_available"), bLogAvailable);
	{
		double Tmp = 0.0;
		if (Result.JsonResult->TryGetNumberField(TEXT("log_entry_count"), Tmp))
		{
			LogEntryCount = (int32)Tmp;
		}
	}

	// Active markers
	TArray<FCUChatActiveMarker> Markers;
	const TArray<TSharedPtr<FJsonValue>>* MarkersArr = nullptr;
	if (Result.JsonResult->TryGetArrayField(TEXT("active_markers"), MarkersArr) && MarkersArr)
	{
		Markers.Reserve(MarkersArr->Num());
		for (const auto& Item : *MarkersArr)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;
			FCUChatActiveMarker M;
			M.Label = GetStringField(*Obj, TEXT("label"));
			M.GraphName = GetStringField(*Obj, TEXT("graph_name"));
			M.Warning = GetStringField(*Obj, TEXT("warning"));
			(*Obj)->TryGetBoolField(TEXT("unclosed"), M.bUnclosed);
			double TmpStarted = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("started_at"), TmpStarted))
			{
				M.StartedAt = (int64)TmpStarted;
			}
			double TmpBp = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("blueprint_count"), TmpBp))
			{
				M.BlueprintCount = (int32)TmpBp;
			}
			double TmpSnap = 0.0;
			if ((*Obj)->TryGetNumberField(TEXT("snapshot_count"), TmpSnap))
			{
				M.SnapshotCount = (int32)TmpSnap;
			}
			Markers.Add(MoveTemp(M));
		}
	}

	// Available labels
	TArray<FString> Labels;
	const TArray<TSharedPtr<FJsonValue>>* LabelsArr = nullptr;
	if (Result.JsonResult->TryGetArrayField(TEXT("available_labels"), LabelsArr) && LabelsArr)
	{
		Labels.Reserve(LabelsArr->Num());
		for (const auto& Item : *LabelsArr)
		{
			FString S;
			if (Item->TryGetString(S))
			{
				Labels.Add(MoveTemp(S));
			}
		}
	}

	// Fall back when there's literally nothing to show
	if (!bLogAvailable && Markers.Num() == 0 && Labels.Num() == 0 && LogEntryCount == 0)
	{
		return CreateGenericJsonWidget(Result);
	}

	return SNew(SCUChatMarkerCatalog)
		.ActiveMarkers(Markers)
		.AvailableLabels(Labels)
		.LogAvailable(bLogAvailable)
		.LogEntryCount(LogEntryCount);
}

// ============================================================================
// Phase 11-3 (Issue #8845) — replay execution / dry-run summary
// ============================================================================

TSharedRef<SWidget> FCUChatToolWidgetFactory::CreateLevelReplayTransactionsWidget(const FCUChatToolResult& Result)
{
	if (!Result.JsonResult.IsValid())
	{
		return CreateGenericJsonWidget(Result);
	}

	const FString Mode = GetStringField(Result.JsonResult, TEXT("mode"));
	const FString Hint = GetStringField(Result.JsonResult, TEXT("hint"));
	const FString DepWarn = GetStringField(Result.JsonResult, TEXT("dependency_warning"));

	// Preview mode counts come from "stats"
	int32 PythonActions = 0, MCPActions = 0;
	const TSharedPtr<FJsonObject>* StatsPtr = nullptr;
	if (Result.JsonResult->TryGetObjectField(TEXT("stats"), StatsPtr) && StatsPtr && (*StatsPtr).IsValid())
	{
		double Tmp = 0.0;
		if ((*StatsPtr)->TryGetNumberField(TEXT("python_actions"), Tmp))  PythonActions = (int32)Tmp;
		if ((*StatsPtr)->TryGetNumberField(TEXT("mcp_actions"), Tmp))     MCPActions = (int32)Tmp;
	}

	// Execute mode counts come from "summary" (single-target) or "batch_summary"
	int32 SuccessCount = 0, FailCount = 0, TotalCount = 0;
	const TSharedPtr<FJsonObject>* SummaryPtr = nullptr;
	if (Result.JsonResult->TryGetObjectField(TEXT("summary"), SummaryPtr) && SummaryPtr && (*SummaryPtr).IsValid())
	{
		double Tmp = 0.0;
		if ((*SummaryPtr)->TryGetNumberField(TEXT("success"), Tmp))  SuccessCount = (int32)Tmp;
		if ((*SummaryPtr)->TryGetNumberField(TEXT("failed"), Tmp))   FailCount = (int32)Tmp;
		if ((*SummaryPtr)->TryGetNumberField(TEXT("total"), Tmp))    TotalCount = (int32)Tmp;
	}
	else
	{
		const TSharedPtr<FJsonObject>* BatchPtr = nullptr;
		if (Result.JsonResult->TryGetObjectField(TEXT("batch_summary"), BatchPtr) && BatchPtr && (*BatchPtr).IsValid())
		{
			double Tmp = 0.0;
			if ((*BatchPtr)->TryGetNumberField(TEXT("success_targets"), Tmp))  SuccessCount = (int32)Tmp;
			if ((*BatchPtr)->TryGetNumberField(TEXT("fail_targets"), Tmp))     FailCount = (int32)Tmp;
			if ((*BatchPtr)->TryGetNumberField(TEXT("total_targets"), Tmp))    TotalCount = (int32)Tmp;
		}
	}

	// Per-step results
	TArray<FCUChatWorkflowStep> Steps;
	const TArray<TSharedPtr<FJsonValue>>* ResultsArr = nullptr;
	if (Result.JsonResult->TryGetArrayField(TEXT("results"), ResultsArr) && ResultsArr)
	{
		Steps.Reserve(ResultsArr->Num());
		for (const auto& Item : *ResultsArr)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Item->TryGetObject(Obj) || !Obj || !(*Obj).IsValid()) continue;
			FCUChatWorkflowStep Step;
			Step.StepName = GetStringField(*Obj, TEXT("step"));
			Step.Error = GetStringField(*Obj, TEXT("error"));
			(*Obj)->TryGetBoolField(TEXT("success"), Step.bSuccess);
			(*Obj)->TryGetNumberField(TEXT("time_ms"), Step.TimeMs);
			Steps.Add(MoveTemp(Step));
		}
	}

	// Empty fallback: no mode AND no useful counts
	if (Mode.IsEmpty() && PythonActions == 0 && MCPActions == 0
		&& TotalCount == 0 && Steps.Num() == 0
		&& Hint.IsEmpty() && DepWarn.IsEmpty())
	{
		return CreateGenericJsonWidget(Result);
	}

	return SNew(SCUChatWorkflowWidget)
		.Mode(Mode)
		.Hint(Hint)
		.DependencyWarning(DepWarn)
		.PythonActions(PythonActions)
		.MCPActions(MCPActions)
		.SuccessCount(SuccessCount)
		.FailCount(FailCount)
		.TotalCount(TotalCount)
		.Steps(Steps);
}
