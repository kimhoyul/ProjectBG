// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5816: AI Annotation Group — collapsible container for per-asset annotations
// Issue #5820: M6 — lazy widget creation, pending annotations, expand-to-materialize

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class SCUAnnotationOverlay;
class SVerticalBox;

/** Delegate fired when the group's expandable area is expanded (Issue #5820) */
DECLARE_DELEGATE(FOnGroupExpanded);

/**
 * Collapsible group container for annotations belonging to the same asset (Issue #5816).
 *
 * Layout:
 *   [▼ AssetName (N annotations)] [X dismiss all]
 *   ├─ SCUAnnotationOverlay #1
 *   ├─ SCUAnnotationOverlay #2
 *   └─ ...
 *
 * Uses SExpandableArea to collapse/expand.
 * One SCUAnnotationGroup per asset path, managed by FCUBlueprintAnnotationRenderer.
 *
 * Lazy widget creation (Issue #5820):
 *   Annotations can be added as "pending" (ID tracked but no widget created).
 *   When the group is expanded, the renderer materializes pending items.
 *   GetAnnotationCount() returns materialized + pending.
 */
class SCUAnnotationGroup : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUAnnotationGroup)
	{}
		SLATE_ARGUMENT(FString, AssetPath)
		SLATE_ARGUMENT(FString, GroupTitle)
		SLATE_EVENT(FSimpleDelegate, OnDismissAll)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Add or replace an annotation overlay in this group */
	void AddAnnotation(const FString& AnnotationId, TSharedRef<SCUAnnotationOverlay> Overlay);

	/** Remove an annotation overlay by ID (checks both materialized and pending). Returns true if found. */
	bool RemoveAnnotation(const FString& AnnotationId);

	/** Track a pending annotation ID without creating a widget (Issue #5820) */
	void AddPendingAnnotation(const FString& AnnotationId);

	/** Move an annotation from pending to materialized with a created widget (Issue #5820) */
	void MaterializePendingAnnotation(const FString& AnnotationId, TSharedRef<SCUAnnotationOverlay> Overlay);

	/** Get count of annotations in this group (materialized + pending) */
	[[nodiscard]] int32 GetAnnotationCount() const { return ChildOverlays.Num() + PendingIds.Num(); }

	/** Whether the expandable area is currently expanded (Issue #5820) */
	[[nodiscard]] bool IsExpanded() const { return bIsCurrentlyExpanded; }

	/** Get the asset path this group represents */
	[[nodiscard]] const FString& GetAssetPath() const noexcept { return AssetPath; }

	/** Delegate fired when the group is expanded — renderer binds to materialize pending (Issue #5820) */
	FOnGroupExpanded OnGroupExpanded;

private:
	FString AssetPath;
	FSimpleDelegate OnDismissAll;

	/** Child overlays keyed by annotation ID (materialized widgets) */
	TMap<FString, TSharedPtr<SCUAnnotationOverlay>> ChildOverlays;

	/** Pending annotation IDs awaiting materialization (Issue #5820) */
	TSet<FString> PendingIds;

	/** Vertical box containing the child overlays */
	TSharedPtr<SVerticalBox> OverlayContainer;

	/** Update the header text to reflect current count */
	void UpdateHeaderText();

	/** Expandable area header text */
	TSharedPtr<STextBlock> HeaderTextBlock;
	FString GroupTitle;

	/** Current expansion state (Issue #5820) */
	bool bIsCurrentlyExpanded = true;

	/** Callback for SExpandableArea expansion changes (Issue #5820) */
	void HandleAreaExpansionChanged(bool bIsExpanded);

	FReply OnDismissAllClicked();

#if WITH_AUTOMATION_WORKER
public:
	/** Test-only: trigger dismiss-all as if button was clicked */
	FReply TestTriggerDismissAll() { return OnDismissAllClicked(); }
	/** Test-only: get pending annotation count (Issue #5820) */
	int32 TestGetPendingCount() const { return PendingIds.Num(); }
	/** Test-only: get materialized overlay count (Issue #5820) */
	int32 TestGetMaterializedCount() const { return ChildOverlays.Num(); }
#endif
};
