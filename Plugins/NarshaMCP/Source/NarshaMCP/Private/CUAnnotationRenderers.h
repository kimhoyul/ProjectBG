// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Renderers — interface and Blueprint renderer
// Issue #5816: M2 — annotation grouping per asset
// Issue #5817: M3 — Material editor parameter annotation renderer
// Issue #5818: M4 — Sequencer track/keyframe annotation renderer
// Issue #5820: M6 — lazy widget creation with pending annotations

#pragma once

#include "CoreMinimal.h"
#include "CUAnnotationData.h"

class SCUAnnotationOverlay;
class SCUAnnotationGroup;
class FBlueprintEditor;        // Forward declaration for Blueprint renderer casting
class UAssetEditorSubsystem;   // Forward declaration for asset editor access
class UMaterialInterface;      // Forward declaration for Material renderer

/**
 * Interface for rendering AI annotations in editor contexts (Issue #5647).
 *
 * Each editor type (Blueprint, Material, Sequencer) has its own renderer
 * that knows how to position overlay widgets relative to editor elements.
 *
 * Dispatch order: Subsystem iterates renderers in registration order,
 * calls CanRender() to find first match (break on first hit).
 */
class ICUAnnotationRenderer
{
public:
	virtual ~ICUAnnotationRenderer() = default;

	/** Whether this renderer can handle the given annotation (e.g. BP renderer for BP assets) */
	virtual bool CanRender(const FCUAnnotationData& Data) const = 0;

	/** Show annotation overlay for the given data */
	virtual void ShowAnnotation(const FCUAnnotationData& Data) = 0;

	/** Hide a specific annotation by ID */
	virtual void HideAnnotation(const FString& AnnotationId) = 0;

	/** Hide all annotations from this renderer */
	virtual void HideAll() = 0;
};

/**
 * Base class providing shared overlay and group window management for annotation renderers.
 *
 * Both FCUBlueprintAnnotationRenderer and FCUSequencerAnnotationRenderer use the same
 * pattern of:
 *   - ActiveOverlays  — TMap keyed by annotation ID
 *   - ActiveGroups    — TMap keyed by asset path, each holding an SCUAnnotationGroup
 *   - GroupWindows    — TMap keyed by asset path, each hosting the group in an SWindow
 *
 * This base class centralises RemoveOverlayWidget, HideAll, and RemoveGroupIfEmpty so
 * the two concrete renderers only need to implement CanRender, ShowAnnotation, and the
 * GetOrCreateGroup factory (which differs in window title / group title formatting).
 *
 * Implementation note: GetOrCreateGroup is left pure-virtual so each renderer can set
 * the window/group title according to its domain (e.g. "AI: BP_Player" vs "Seq: LS_Intro").
 */
class FCUAnnotationRendererBase : public ICUAnnotationRenderer
{
public:
	//~ Begin ICUAnnotationRenderer Interface
	virtual void HideAnnotation(const FString& AnnotationId) override;
	virtual void HideAll() override;
	//~ End ICUAnnotationRenderer Interface

protected:
	/** Active overlay widgets, keyed by annotation ID */
	TMap<FString, TSharedPtr<SCUAnnotationOverlay>> ActiveOverlays;

	/** Active annotation groups, keyed by asset path */
	TMap<FString, TSharedPtr<SCUAnnotationGroup>> ActiveGroups;

	/** Windows hosting annotation groups, keyed by asset path */
	TMap<FString, TSharedPtr<SWindow>> GroupWindows;

	/** Pending annotations awaiting widget materialization (Issue #5820) */
	TMap<FString, FCUAnnotationData> PendingAnnotations;

	/**
	 * Remove an overlay widget from the active map, also updating its parent group.
	 * Also handles pending annotations (Issue #5820).
	 * Parses the AssetPath from the annotation ID using FCUAnnotationData::IdSeparator.
	 */
	void RemoveOverlayWidget(const FString& AnnotationId);

	/**
	 * Remove all overlays for AssetPath from the active map and dismiss the group window.
	 * Also clears pending annotations for this asset (Issue #5820).
	 * Called from the OnDismissAll lambda in GetOrCreateGroup implementations.
	 */
	void DismissGroupOverlays(const FString& AssetPath);

	/**
	 * Destroy the group window and remove it from ActiveGroups/GroupWindows if the group
	 * contains no more annotations.
	 */
	void RemoveGroupIfEmpty(const FString& AssetPath);

	/**
	 * Get or create a floating SWindow + SCUAnnotationGroup for the given asset path.
	 * Subclasses override this to set domain-specific window and group titles.
	 */
	virtual TSharedRef<SCUAnnotationGroup> GetOrCreateGroup(const FString& AssetPath) = 0;

	/**
	 * Create a floating group window hosting the given SCUAnnotationGroup.
	 * Called from GetOrCreateGroup implementations after the group widget is built.
	 * @param AssetPath  Key for GroupWindows map entry
	 * @param WindowTitle Window title text (e.g. "AI: BP_Player")
	 * @param Group      The group widget to host
	 */
	void AddGroupWindow(const FString& AssetPath, const FText& WindowTitle, TSharedRef<SCUAnnotationGroup> Group);

	/**
	 * Create an SCUAnnotationOverlay widget from annotation data (Issue #5820).
	 * Factored out from ShowAnnotation for reuse in immediate and deferred paths.
	 */
	TSharedRef<SCUAnnotationOverlay> CreateOverlayWidget(const FCUAnnotationData& Data);

	/**
	 * Materialize all pending annotations for a given asset path (Issue #5820).
	 * Called when the group's SExpandableArea is expanded.
	 */
	void MaterializeAllPending(const FString& AssetPath);

#if WITH_AUTOMATION_WORKER
public:
	/** Test accessor: number of active overlays */
	int32 TestGetOverlayCount() const { return ActiveOverlays.Num(); }
	/** Test accessor: number of active groups */
	int32 TestGetGroupCount() const { return ActiveGroups.Num(); }
	/** Test accessor: check if group exists for asset path */
	bool TestHasGroup(const FString& AssetPath) const { return ActiveGroups.Contains(AssetPath); }
	/** Test accessor: get annotation count in group */
	int32 TestGetGroupAnnotationCount(const FString& AssetPath) const;
	/** Test accessor: number of pending annotations (Issue #5820) */
	int32 TestGetPendingCount() const { return PendingAnnotations.Num(); }
#endif
};

/**
 * Blueprint editor annotation renderer (Issue #5647 M1).
 *
 * Reuses the FBlueprintEditor access pattern from WebSocketEditorSubsystem:
 *   UAssetEditorSubsystem::FindEditorForAsset() → FBlueprintEditor* → node position
 *
 * CanRender: asset path contains FCUAnnotationData::GamePathPrefix ("/Game/").
 * Group window title: "AI: {AssetBaseName}"
 */
class FCUBlueprintAnnotationRenderer : public FCUAnnotationRendererBase
{
public:
	//~ Begin ICUAnnotationRenderer Interface
	virtual bool CanRender(const FCUAnnotationData& Data) const override;
	virtual void ShowAnnotation(const FCUAnnotationData& Data) override;
	//~ End ICUAnnotationRenderer Interface

protected:
	virtual TSharedRef<SCUAnnotationGroup> GetOrCreateGroup(const FString& AssetPath) override;
};

/**
 * Material editor annotation renderer (Issue #5817 M3).
 *
 * Routes annotations with TargetIdentifier prefixes:
 *   "MatParam_{Name}" — Material parameter annotations
 *   "MatExpr_{Index}" — Expression node annotations
 *
 * Uses FCUAnnotationRendererBase for shared overlay/group management.
 * Registered BEFORE BP renderer so prefix-based CanRender() gets first-match priority.
 * Group window title: "AI Mat: {AssetBaseName}"
 */
class FCUMaterialAnnotationRenderer : public FCUAnnotationRendererBase
{
public:
	//~ Begin ICUAnnotationRenderer Interface
	virtual bool CanRender(const FCUAnnotationData& Data) const override;
	virtual void ShowAnnotation(const FCUAnnotationData& Data) override;
	//~ End ICUAnnotationRenderer Interface

protected:
	virtual TSharedRef<SCUAnnotationGroup> GetOrCreateGroup(const FString& AssetPath) override;
};

/**
 * Sequencer editor annotation renderer (Issue #5818 M4).
 *
 * Handles annotations for Sequencer tracks and keyframes.
 * TargetIdentifier convention (FCUAnnotationData constants):
 *   FCUAnnotationData::TrackTargetPrefix    + TrackDisplayName  (track-level)
 *   FCUAnnotationData::KeyframeTargetPrefix + TrackName + "::" + FrameNumber  (keyframe-level)
 *
 * Registered BEFORE BP renderer so prefix-based CanRender() gets first-match priority.
 * Group window title: "Seq: {AssetBaseName}"
 */
class FCUSequencerAnnotationRenderer : public FCUAnnotationRendererBase
{
public:
	//~ Begin ICUAnnotationRenderer Interface
	virtual bool CanRender(const FCUAnnotationData& Data) const override;
	virtual void ShowAnnotation(const FCUAnnotationData& Data) override;
	//~ End ICUAnnotationRenderer Interface

protected:
	virtual TSharedRef<SCUAnnotationGroup> GetOrCreateGroup(const FString& AssetPath) override;
};
