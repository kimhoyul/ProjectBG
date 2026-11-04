// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Renderers implementation
// Issue #5816: M2 — annotation grouping per asset
// Issue #5817: M3 — Material editor parameter annotation renderer
// Issue #5818: M4 — Sequencer track/keyframe annotation renderer
// Issue #5820: M6 — lazy widget creation with pending annotations

#include "CUAnnotationRenderers.h"

// Engine includes (most fundamental)
#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Materials/MaterialInterface.h"

// Editor includes (depends on Engine)
#include "Async/Async.h"
#include "Editor.h"
#include "Misc/App.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "BlueprintEditor.h"
#include "CUBlueprintManipulator.h"

// UI Framework includes
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"

// Project includes (last, depends on all above)
#include "CUAnnotationRenderers.h"
#include "CUAnnotationData.h"
#include "Slate/SCUAnnotationOverlay.h"
#include "Slate/SCUAnnotationGroup.h"
#include "CUAnnotationChatBridge.h"  // Issue #5821: bridge delegate for Discuss action

// Suppress MSVC C4946 for static_cast between related UE editor types without RTTI (/GR-)
#if PLATFORM_WINDOWS
#pragma warning(push)
#pragma warning(disable: 4946)  // cast between related types without RTTI
#endif

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPAnnotationRenderer, Log, All);

namespace
{
	/**
	 * Issue #23127: headless/nullrhi still initializes Slate, so FSlateApplication::IsInitialized()
	 * is true, but asset-editor OpenEditorForAsset schedules a deferred layout save that later
	 * calls GetRestoredDimensions and fatals on platforms that cannot render.
	 * Annotation overlays are visual-only — skip opening asset editors when rendering is off.
	 */
	static bool CanOpenAssetEditorsForAnnotation()
	{
		return FApp::CanEverRender() && FSlateApplication::IsInitialized();
	}
}

namespace AIAnnotationRendererConstants
{
	// Window sizing constants for annotation group windows (Issue #5816)
	/** Default width for annotation group windows - Optimized for readability without excessive screen real estate usage */
	static constexpr float GroupWindowWidth = 330.0f;
	/** Default height for annotation group windows - Sufficient for 3-4 annotations before requiring scroll */
	static constexpr float GroupWindowHeight = 200.0f;

	// Performance constants
	/** Expected maximum annotations per group before performance degradation */
	static constexpr int32 MaxAnnotationsPerGroup = 50;
}

// ===================================================================
// FCUAnnotationRendererBase — shared overlay + group management
// ===================================================================

void FCUAnnotationRendererBase::HideAnnotation(const FString& AnnotationId)
{
	RemoveOverlayWidget(AnnotationId);
}

void FCUAnnotationRendererBase::HideAll()
{
	ActiveOverlays.Empty();
	PendingAnnotations.Empty();

	for (auto& Pair : GroupWindows)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->RequestDestroyWindow();
		}
	}
	GroupWindows.Empty();
	ActiveGroups.Empty();
}

void FCUAnnotationRendererBase::RemoveOverlayWidget(const FString& AnnotationId)
{
	// Check if it's a pending annotation first (Issue #5820)
	if (PendingAnnotations.Remove(AnnotationId) > 0)
	{
		FString AssetPath;
		FString Target;
		if (FCUAnnotationData::ParseId(AnnotationId, AssetPath, Target) && !AssetPath.IsEmpty())
		{
			TSharedPtr<SCUAnnotationGroup>* GroupPtr = ActiveGroups.Find(AssetPath);
			if (GroupPtr && GroupPtr->IsValid())
			{
				(*GroupPtr)->RemoveAnnotation(AnnotationId);
				RemoveGroupIfEmpty(AssetPath);
			}
		}
		return;
	}

	TSharedPtr<SCUAnnotationOverlay> Overlay;
	if (!ActiveOverlays.RemoveAndCopyValue(AnnotationId, Overlay))
	{
		return;
	}

	// Parse the asset path from the annotation ID ("AssetPath::Target")
	FString AssetPath;
	FString Target;
	if (FCUAnnotationData::ParseId(AnnotationId, AssetPath, Target) && !AssetPath.IsEmpty())
	{
		TSharedPtr<SCUAnnotationGroup>* GroupPtr = ActiveGroups.Find(AssetPath);
		if (GroupPtr && GroupPtr->IsValid())
		{
			(*GroupPtr)->RemoveAnnotation(AnnotationId);
			RemoveGroupIfEmpty(AssetPath);
		}
	}
}

void FCUAnnotationRendererBase::DismissGroupOverlays(const FString& AssetPath)
{
	// Remove all overlay entries whose IDs begin with "AssetPath::"
	const FString Prefix = AssetPath + FCUAnnotationData::IdSeparator;
	TArray<FString> IdsToRemove;
	for (const auto& Pair : ActiveOverlays)
	{
		if (Pair.Key.StartsWith(Prefix))
		{
			IdsToRemove.Add(Pair.Key);
		}
	}
	for (const FString& Id : IdsToRemove)
	{
		ActiveOverlays.Remove(Id);
	}

	// Also remove pending annotations for this asset (Issue #5820)
	TArray<FString> PendingToRemove;
	for (const auto& Pair : PendingAnnotations)
	{
		if (Pair.Key.StartsWith(Prefix))
		{
			PendingToRemove.Add(Pair.Key);
		}
	}
	for (const FString& Id : PendingToRemove)
	{
		PendingAnnotations.Remove(Id);
	}

	// Destroy the hosting window
	TSharedPtr<SWindow>* WindowPtr = GroupWindows.Find(AssetPath);
	if (WindowPtr && WindowPtr->IsValid())
	{
		(*WindowPtr)->RequestDestroyWindow();
	}
	GroupWindows.Remove(AssetPath);
	ActiveGroups.Remove(AssetPath);
}

void FCUAnnotationRendererBase::RemoveGroupIfEmpty(const FString& AssetPath)
{
	TSharedPtr<SCUAnnotationGroup>* GroupPtr = ActiveGroups.Find(AssetPath);
	if (!GroupPtr || !GroupPtr->IsValid())
	{
		return;
	}

	if ((*GroupPtr)->GetAnnotationCount() > 0)
	{
		return;
	}

	// Group is now empty — destroy window and remove entries
	TSharedPtr<SWindow>* WindowPtr = GroupWindows.Find(AssetPath);
	if (WindowPtr && WindowPtr->IsValid())
	{
		(*WindowPtr)->RequestDestroyWindow();
	}
	GroupWindows.Remove(AssetPath);
	ActiveGroups.Remove(AssetPath);
}

void FCUAnnotationRendererBase::AddGroupWindow(
	const FString& AssetPath,
	const FText& WindowTitle,
	TSharedRef<SCUAnnotationGroup> Group)
{
	// Issue #10756: AddWindow requires a live FSlateApplication. The annotation
	// renderer is editor-only by design, but defensive — skip silently when Slate
	// is unavailable (cook commandlet, headless automation) instead of crashing.
	if (!FSlateApplication::IsInitialized()) return;

	TSharedRef<SWindow> GroupWindow = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(FVector2D(AIAnnotationRendererConstants::GroupWindowWidth, AIAnnotationRendererConstants::GroupWindowHeight))
		.SupportsMaximize(false)
		.SupportsMinimize(false)
		.IsTopmostWindow(true)
		.SizingRule(ESizingRule::Autosized)
		[
			Group
		];

	FSlateApplication::Get().AddWindow(GroupWindow);
	GroupWindows.Add(AssetPath, GroupWindow);
}

TSharedRef<SCUAnnotationOverlay> FCUAnnotationRendererBase::CreateOverlayWidget(const FCUAnnotationData& Data)
{
	const FString AnnotationId = Data.AnnotationId;
	const FString AnnotationText = Data.AnnotationText;
	const FString AssetPath = Data.SourceAssetPath;
	return SNew(SCUAnnotationOverlay)
		.AnnotationId(Data.AnnotationId)
		.AnnotationText(Data.AnnotationText)
		.Category(Data.Category)
		.OnDismiss(FSimpleDelegate::CreateLambda([this, AnnotationId]()
		{
			RemoveOverlayWidget(AnnotationId);
		}))
		.OnDiscuss(FSimpleDelegate::CreateLambda([AnnotationId, AnnotationText, AssetPath]()
		{
			GetOnCUAnnotationDiscussRequested().ExecuteIfBound(AnnotationId, AnnotationText, AssetPath);
		}));
}

void FCUAnnotationRendererBase::MaterializeAllPending(const FString& AssetPath)
{
	const FString Prefix = AssetPath + FCUAnnotationData::IdSeparator;
	TArray<FString> IdsToMaterialize;
	for (const auto& Pair : PendingAnnotations)
	{
		if (Pair.Key.StartsWith(Prefix))
		{
			IdsToMaterialize.Add(Pair.Key);
		}
	}

	TSharedPtr<SCUAnnotationGroup>* GroupPtr = ActiveGroups.Find(AssetPath);
	if (!GroupPtr || !GroupPtr->IsValid())
	{
		return;
	}

	for (const FString& Id : IdsToMaterialize)
	{
		FCUAnnotationData Data;
		if (PendingAnnotations.RemoveAndCopyValue(Id, Data))
		{
			TSharedRef<SCUAnnotationOverlay> Overlay = CreateOverlayWidget(Data);
			ActiveOverlays.Add(Id, Overlay);
			(*GroupPtr)->MaterializePendingAnnotation(Id, Overlay);
		}
	}
}

#if WITH_AUTOMATION_WORKER
int32 FCUAnnotationRendererBase::TestGetGroupAnnotationCount(const FString& AssetPath) const
{
	const TSharedPtr<SCUAnnotationGroup>* GroupPtr = ActiveGroups.Find(AssetPath);
	if (GroupPtr && GroupPtr->IsValid())
	{
		return (*GroupPtr)->GetAnnotationCount();
	}
	return 0;
}
#endif

// ===================================================================
// FCUBlueprintAnnotationRenderer
// ===================================================================

namespace
{
	/** FName used by the Blueprint editor instance — avoids constructing FName on every call */
	const FName GBlueprintEditorName(TEXT("BlueprintEditor"));
}

bool FCUBlueprintAnnotationRenderer::CanRender(const FCUAnnotationData& Data) const
{
	// Handle Blueprint assets: path contains "/Game/" and target is a node GUID.
	// Blueprint assets typically have paths like /Game/Blueprints/BP_Player.
	return !Data.SourceAssetPath.IsEmpty()
		&& !Data.TargetIdentifier.IsEmpty()
		&& Data.SourceAssetPath.Contains(FCUAnnotationData::GamePathPrefix);
}

void FCUBlueprintAnnotationRenderer::ShowAnnotation(const FCUAnnotationData& Data)
{
	if (!GEditor)
	{
		return;
	}

	// Remove existing overlay or pending annotation for this ID (if re-annotating)
	if (ActiveOverlays.Contains(Data.AnnotationId) || PendingAnnotations.Contains(Data.AnnotationId))
	{
		RemoveOverlayWidget(Data.AnnotationId);
	}

	// 1. Load the Blueprint asset
	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *Data.SourceAssetPath);
	if (!Blueprint)
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Warning, TEXT("Blueprint not found: %s"), *Data.SourceAssetPath);
		return;
	}

	// 2. Open the Blueprint editor if not already open (skipped headless — Issue #23127)
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	IAssetEditorInstance* EditorInstance = nullptr;
	if (CanOpenAssetEditorsForAnnotation())
	{
		AssetEditorSubsystem->OpenEditorForAsset(Blueprint);
		EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Blueprint, /*bFocusIfOpen=*/false);
	}
	else
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Verbose,
			TEXT("Skipping Blueprint OpenEditorForAsset (no render / headless): %s"),
			*Data.SourceAssetPath);
	}
	if (!EditorInstance)
	{
		// Headless: no editor toolkit — still allow overlay-side paths that do not need it.
		// Focus/navigation paths below require an open editor; bail with the existing contract.
		UE_LOG(LogNarshaMCPAnnotationRenderer, Warning, TEXT("Could not open Blueprint editor for: %s"), *Data.SourceAssetPath);
		return;
	}

	// 3. Find the target node by GUID
	FGuid NodeGuid;
	if (!FGuid::Parse(Data.TargetIdentifier, NodeGuid))
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Warning, TEXT("Invalid node GUID: %s"), *Data.TargetIdentifier);
		return;
	}

	UEdGraphNode* TargetNode = FCUBlueprintManipulator::FindNodeByGuid(Blueprint, NodeGuid);
	if (!TargetNode)
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Warning, TEXT("Node not found in %s: %s"),
			*Data.SourceAssetPath, *Data.TargetIdentifier);
		return;
	}

	// 4. Navigate to the node (makes it visible in the graph)
	// Safety: dynamic_cast unavailable (/GR-). EditorName == GBlueprintEditorName
	// guarantees the instance is FBlueprintEditor (UE registers each editor type
	// with a unique name via IAssetEditorInstance::GetEditorName()).
	FBlueprintEditor* BlueprintEditor = nullptr;
	if (EditorInstance->GetEditorName() == GBlueprintEditorName)
	{
		FAssetEditorToolkit* Toolkit = static_cast<FAssetEditorToolkit*>(EditorInstance);
		if (Toolkit && Toolkit->GetToolkitName().ToString() == GBlueprintEditorName.ToString())
		{
			BlueprintEditor = static_cast<FBlueprintEditor*>(Toolkit);
		}
	}
	if (BlueprintEditor)
	{
		BlueprintEditor->JumpToNode(TargetNode, /*bRequestRename=*/false);
	}

	// 5. Add to annotation group — immediate or lazy (Issue #5820)
	const bool bGroupExists = ActiveGroups.Contains(Data.SourceAssetPath);
	TSharedRef<SCUAnnotationGroup> Group = GetOrCreateGroup(Data.SourceAssetPath);

	if (!bGroupExists || Group->IsExpanded())
	{
		// New group or expanded group → materialize immediately
		TSharedRef<SCUAnnotationOverlay> Overlay = CreateOverlayWidget(Data);
		ActiveOverlays.Add(Data.AnnotationId, Overlay);
		Group->AddAnnotation(Data.AnnotationId, Overlay);
	}
	else
	{
		// Existing collapsed group → defer until expanded
		PendingAnnotations.Add(Data.AnnotationId, Data);
		Group->AddPendingAnnotation(Data.AnnotationId);
	}

	UE_LOG(LogNarshaMCPAnnotationRenderer, Log, TEXT("Showed annotation for node %s in %s (category: %s)"),
		*Data.TargetIdentifier, *Data.SourceAssetPath, *Data.Category);
}

TSharedRef<SCUAnnotationGroup> FCUBlueprintAnnotationRenderer::GetOrCreateGroup(const FString& AssetPath)
{
	TSharedPtr<SCUAnnotationGroup>* Existing = ActiveGroups.Find(AssetPath);
	if (Existing && Existing->IsValid())
	{
		return Existing->ToSharedRef();
	}

	// Extract short name for title (e.g. "/Game/BP_Player" → "BP_Player")
	FString GroupTitle = FPaths::GetBaseFilename(AssetPath);
	if (GroupTitle.IsEmpty())
	{
		GroupTitle = AssetPath;
	}

	TSharedRef<SCUAnnotationGroup> Group = SNew(SCUAnnotationGroup)
		.AssetPath(AssetPath)
		.GroupTitle(GroupTitle)
		.OnDismissAll(FSimpleDelegate::CreateLambda([this, AssetPath]()
		{
			DismissGroupOverlays(AssetPath);
		}));

	// Bind expand delegate for lazy widget materialization (Issue #5820)
	Group->OnGroupExpanded.BindLambda([this, AssetPath]() { MaterializeAllPending(AssetPath); });

	ActiveGroups.Add(AssetPath, Group);

	// Window title format: "AI: {AssetName}" (Issue #5816)
	const FText WindowTitle = FText::FromString(FString::Printf(TEXT("AI: %s"), *GroupTitle));
	AddGroupWindow(AssetPath, WindowTitle, Group);

	return Group;
}

// ===================================================================
// FCUSequencerAnnotationRenderer (Issue #5818)
// ===================================================================

bool FCUSequencerAnnotationRenderer::CanRender(const FCUAnnotationData& Data) const
{
	// Handle Sequencer annotations identified by TargetIdentifier prefix
	return !Data.SourceAssetPath.IsEmpty()
		&& !Data.TargetIdentifier.IsEmpty()
		&& (Data.TargetIdentifier.StartsWith(FCUAnnotationData::TrackTargetPrefix)
		 || Data.TargetIdentifier.StartsWith(FCUAnnotationData::KeyframeTargetPrefix));
}

void FCUSequencerAnnotationRenderer::ShowAnnotation(const FCUAnnotationData& Data)
{
	if (!GEditor)
	{
		return;
	}

	// Remove existing overlay or pending annotation for this ID (if re-annotating)
	if (ActiveOverlays.Contains(Data.AnnotationId) || PendingAnnotations.Contains(Data.AnnotationId))
	{
		RemoveOverlayWidget(Data.AnnotationId);
	}

	// 1. Load the LevelSequence asset
	UObject* Asset = LoadObject<UObject>(nullptr, *Data.SourceAssetPath);
	if (!Asset)
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Warning, TEXT("Sequence asset not found: %s"), *Data.SourceAssetPath);
		return;
	}

	// 2. Open the Sequencer editor if not already open
	//    Note: OpenEditorForAsset is deferred via AsyncTask to avoid blocking
	//    the game thread when ShowAnnotation is called from WS handlers (Issue #5818)
	//    Headless/nullrhi must not open asset editors (Issue #23127).
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (AssetEditorSubsystem && CanOpenAssetEditorsForAnnotation())
	{
		TWeakObjectPtr<UAssetEditorSubsystem> WeakAssetEditor(AssetEditorSubsystem);
		TWeakObjectPtr<UObject> WeakAsset(Asset);
		AsyncTask(ENamedThreads::GameThread, [WeakAssetEditor, WeakAsset]()
		{
			// Re-check on the game thread: render capability is process-wide but
			// subsystem/asset can go invalid while the async is queued.
			if (WeakAssetEditor.IsValid() && WeakAsset.IsValid() && CanOpenAssetEditorsForAnnotation())
			{
				WeakAssetEditor->OpenEditorForAsset(WeakAsset.Get());
			}
		});
	}
	else if (AssetEditorSubsystem)
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Verbose,
			TEXT("Skipping Sequencer OpenEditorForAsset (no render / headless): %s"),
			*Data.SourceAssetPath);
	}

	// 3. Add to annotation group — immediate or lazy (Issue #5820)
	const bool bGroupExists = ActiveGroups.Contains(Data.SourceAssetPath);
	TSharedRef<SCUAnnotationGroup> Group = GetOrCreateGroup(Data.SourceAssetPath);

	if (!bGroupExists || Group->IsExpanded())
	{
		// New group or expanded group → materialize immediately
		TSharedRef<SCUAnnotationOverlay> Overlay = CreateOverlayWidget(Data);
		ActiveOverlays.Add(Data.AnnotationId, Overlay);
		Group->AddAnnotation(Data.AnnotationId, Overlay);
	}
	else
	{
		// Existing collapsed group → defer until expanded
		PendingAnnotations.Add(Data.AnnotationId, Data);
		Group->AddPendingAnnotation(Data.AnnotationId);
	}

	// Extract human-readable target name for the log (strip the leading prefix)
	FString DisplayTarget = Data.TargetIdentifier;
	if (DisplayTarget.StartsWith(FCUAnnotationData::TrackTargetPrefix))
	{
		DisplayTarget = DisplayTarget.Mid(FCString::Strlen(FCUAnnotationData::TrackTargetPrefix));
	}
	else if (DisplayTarget.StartsWith(FCUAnnotationData::KeyframeTargetPrefix))
	{
		DisplayTarget = DisplayTarget.Mid(FCString::Strlen(FCUAnnotationData::KeyframeTargetPrefix));
	}

	UE_LOG(LogNarshaMCPAnnotationRenderer, Log, TEXT("Showed Sequencer annotation for %s in %s (category: %s)"),
		*DisplayTarget, *Data.SourceAssetPath, *Data.Category);
}

TSharedRef<SCUAnnotationGroup> FCUSequencerAnnotationRenderer::GetOrCreateGroup(const FString& AssetPath)
{
	TSharedPtr<SCUAnnotationGroup>* Existing = ActiveGroups.Find(AssetPath);
	if (Existing && Existing->IsValid())
	{
		return Existing->ToSharedRef();
	}

	// Extract short name and prefix with "Seq:" to distinguish from BP groups
	FString BaseName = FPaths::GetBaseFilename(AssetPath);
	if (BaseName.IsEmpty())
	{
		BaseName = AssetPath;
	}
	const FString GroupTitle = FString::Printf(TEXT("Seq: %s"), *BaseName);

	TSharedRef<SCUAnnotationGroup> Group = SNew(SCUAnnotationGroup)
		.AssetPath(AssetPath)
		.GroupTitle(GroupTitle)
		.OnDismissAll(FSimpleDelegate::CreateLambda([this, AssetPath]()
		{
			DismissGroupOverlays(AssetPath);
		}));

	// Bind expand delegate for lazy widget materialization (Issue #5820)
	Group->OnGroupExpanded.BindLambda([this, AssetPath]() { MaterializeAllPending(AssetPath); });

	ActiveGroups.Add(AssetPath, Group);

	// Window title format: "AI Seq: {AssetName}" (Issue #5818)
	const FText WindowTitle = FText::FromString(FString::Printf(TEXT("AI Seq: %s"), *BaseName));
	AddGroupWindow(AssetPath, WindowTitle, Group);

	return Group;
}

// ===================================================================
// FCUMaterialAnnotationRenderer (Issue #5817)
// ===================================================================

bool FCUMaterialAnnotationRenderer::CanRender(const FCUAnnotationData& Data) const
{
	// Handle Material assets: path contains /Game/ and target has Material prefix
	return !Data.SourceAssetPath.IsEmpty()
		&& !Data.TargetIdentifier.IsEmpty()
		&& Data.SourceAssetPath.Contains(TEXT("/Game/"))
		&& FCUAnnotationData::HasMaterialPrefix(Data.TargetIdentifier);
}

void FCUMaterialAnnotationRenderer::ShowAnnotation(const FCUAnnotationData& Data)
{
	if (!GEditor)
	{
		return;
	}

	// Remove existing overlay or pending annotation for this ID (if re-annotating)
	if (ActiveOverlays.Contains(Data.AnnotationId) || PendingAnnotations.Contains(Data.AnnotationId))
	{
		RemoveOverlayWidget(Data.AnnotationId);
	}

	// 1. Load the Material asset
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *Data.SourceAssetPath);
	if (!Material)
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Warning, TEXT("Material not found: %s"), *Data.SourceAssetPath);
		return;
	}

	// 2. Open the Material editor if not already open (skipped headless — Issue #23127)
	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	IAssetEditorInstance* EditorInstance = nullptr;
	if (CanOpenAssetEditorsForAnnotation())
	{
		AssetEditorSubsystem->OpenEditorForAsset(Material);
		EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Material, /*bFocusIfOpen=*/false);
	}
	else
	{
		UE_LOG(LogNarshaMCPAnnotationRenderer, Verbose,
			TEXT("Skipping Material OpenEditorForAsset (no render / headless): %s"),
			*Data.SourceAssetPath);
	}

	// 3. Navigate to target parameter/expression (Issue #5817 M3)
	if (EditorInstance)
	{
		// Material Editor navigation - highlight parameter or expression
		// Note: Extract parameter name or expression index from TargetIdentifier
		if (Data.TargetIdentifier.StartsWith(TEXT("MatParam_")))
		{
			const FString ParameterName = Data.TargetIdentifier.RightChop(9); // Remove "MatParam_" prefix
			UE_LOG(LogNarshaMCPAnnotationRenderer, VeryVerbose, TEXT("Material parameter target: %s"), *ParameterName);
			// NOTE(#5817-M4): Parameter highlighting deferred — Material Editor API limitation
			//                 IMaterialEditor interface is private in current UE versions (5.1-5.4)
			//                 Consider public API availability in future UE releases or Engine modification approach
		}
		else if (Data.TargetIdentifier.StartsWith(TEXT("MatExpr_")))
		{
			const FString ExpressionIndexStr = Data.TargetIdentifier.RightChop(8); // Remove "MatExpr_" prefix
			const int32 ExpressionIndex = FCString::Atoi(*ExpressionIndexStr);
			UE_LOG(LogNarshaMCPAnnotationRenderer, VeryVerbose, TEXT("Material expression target: %d"), ExpressionIndex);
			// NOTE(#5817-M4): Expression node highlighting deferred — same Material Editor API limitation
			//                 IMaterialEditor interface is private in current UE versions (5.1-5.4)
			//                 Alternative: Engine modification or reflection-based access (higher complexity)
		}
	}

	// 4. Add to annotation group — immediate or lazy (Issue #5820)
	const bool bGroupExists = ActiveGroups.Contains(Data.SourceAssetPath);
	TSharedRef<SCUAnnotationGroup> Group = GetOrCreateGroup(Data.SourceAssetPath);

	if (!bGroupExists || Group->IsExpanded())
	{
		// New group or expanded group → materialize immediately
		TSharedRef<SCUAnnotationOverlay> Overlay = CreateOverlayWidget(Data);
		ActiveOverlays.Add(Data.AnnotationId, Overlay);
		Group->AddAnnotation(Data.AnnotationId, Overlay);
	}
	else
	{
		// Existing collapsed group → defer until expanded
		PendingAnnotations.Add(Data.AnnotationId, Data);
		Group->AddPendingAnnotation(Data.AnnotationId);
	}

	UE_LOG(LogNarshaMCPAnnotationRenderer, Log, TEXT("Showed Material annotation for %s in %s (category: %s)"),
		*Data.TargetIdentifier, *Data.SourceAssetPath, *Data.Category);
}

TSharedRef<SCUAnnotationGroup> FCUMaterialAnnotationRenderer::GetOrCreateGroup(const FString& AssetPath)
{
	TSharedPtr<SCUAnnotationGroup>* Existing = ActiveGroups.Find(AssetPath);
	if (Existing && Existing->IsValid())
	{
		return Existing->ToSharedRef();
	}

	// Extract short name for title (e.g. "/Game/Materials/M_Rock" → "M_Rock")
	FString GroupTitle = FPaths::GetBaseFilename(AssetPath);
	if (GroupTitle.IsEmpty())
	{
		GroupTitle = AssetPath;
	}

	TSharedRef<SCUAnnotationGroup> Group = SNew(SCUAnnotationGroup)
		.AssetPath(AssetPath)
		.GroupTitle(GroupTitle)
		.OnDismissAll(FSimpleDelegate::CreateLambda([this, AssetPath]()
		{
			DismissGroupOverlays(AssetPath);
		}));

	// Bind expand delegate for lazy widget materialization (Issue #5820)
	Group->OnGroupExpanded.BindLambda([this, AssetPath]() { MaterializeAllPending(AssetPath); });

	ActiveGroups.Add(AssetPath, Group);

	// Window title format: "AI Material: {AssetName}" (Issue #5816)
	const FText WindowTitle = FText::FromString(FString::Printf(TEXT("AI Material: %s"), *GroupTitle));
	AddGroupWindow(AssetPath, WindowTitle, Group);

	return Group;
}

#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif
