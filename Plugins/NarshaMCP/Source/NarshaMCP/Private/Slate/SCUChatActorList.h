// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8836 (Phase 9-1): SCUChatActorList — level_find_actors result list

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

/** Single actor entry from level_find_actors response. */
struct FCUChatActorEntry
{
	FString ActorClass;
	FString Category;
	FString LevelName;
	FString BlueprintClassPath;
	bool bIsBlueprint = false;
	int64 FileSize = 0;
};

/**
 * Actor list display for `ue_manage_project_ops:level_find_actors` (Issue #8836).
 *
 * Per-row layout:
 *   [Category badge] [BP icon if bIsBlueprint] [ActorClass]  (LevelName · path/size)
 */
class SCUChatActorList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SCUChatActorList)
		: _TotalMatches(0)
	{}
		SLATE_ARGUMENT(TArray<FCUChatActorEntry>, Actors)
		SLATE_ARGUMENT(int32, TotalMatches)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

#if WITH_AUTOMATION_WORKER
	int32 TestGetActorCount() const { return Actors.Num(); }
	int32 TestGetTotalMatches() const { return TotalMatches; }
#endif

private:
	TArray<FCUChatActorEntry> Actors;
	int32 TotalMatches = 0;

	TSharedRef<class SWidget> BuildActorRow(const FCUChatActorEntry& Entry) const;
};
