// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5821: AI Annotation M7 — Bridge delegates for Annotation ↔ Chat Panel cross-linking
//
// Lightweight delegate header included by both UCUAnnotationSubsystem and UCUChatPanelSubsystem.
// Neither subsystem includes the other — loose coupling via these global delegates.

#pragma once

#include "CoreMinimal.h"

struct FCUAnnotationData;

/**
 * Delegate: Annotation overlay "Discuss" button clicked → open Chat Panel with context.
 * Fired by annotation renderers, handled by UCUChatPanelSubsystem.
 *
 * @param AnnotationId   e.g., "/Game/BP_Player::NodeGuid_123"
 * @param AnnotationText AI-generated text to pre-fill as context
 * @param AssetPath      Source asset path for display
 */
DECLARE_DELEGATE_ThreeParams(FOnCUAnnotationDiscussRequested,
	const FString& /* AnnotationId */,
	const FString& /* AnnotationText */,
	const FString& /* AssetPath */);

/**
 * Delegate: Chat Panel AI response contains ue-annotate:// link → create annotation.
 * Fired by UCUChatPanelSubsystem, handled by UCUAnnotationSubsystem.
 *
 * @param AnnotationId   Target: "AssetPath::TargetIdentifier"
 * @param AnnotationText Text from AI response
 * @param Category       "analysis" | "suggestion" | "warning" | "error"
 * @param ChatMessageId  Originating chat message ID for cross-reference
 */
DECLARE_DELEGATE_FourParams(FOnCUChatAnnotationRequested,
	const FString& /* AnnotationId */,
	const FString& /* AnnotationText */,
	const FString& /* Category */,
	const FString& /* ChatMessageId */);

/**
 * Delegate: Chat Panel queries annotation history from cache.
 * Bound by UCUAnnotationSubsystem, called by UCUChatPanelSubsystem toolbar.
 *
 * @return Array of recent annotations (up to MaxHistoryCount, LRU order)
 */
DECLARE_DELEGATE_RetVal(TArray<FCUAnnotationData>, FOnCUAnnotationHistoryQuery);

/** Global accessor: Annotation → Chat discuss request delegate */
NARSHAMCP_API FOnCUAnnotationDiscussRequested& GetOnCUAnnotationDiscussRequested();

/** Global accessor: Chat → Annotation creation request delegate */
NARSHAMCP_API FOnCUChatAnnotationRequested& GetOnCUChatAnnotationRequested();

/** Global accessor: Annotation history query delegate */
NARSHAMCP_API FOnCUAnnotationHistoryQuery& GetOnCUAnnotationHistoryQuery();

/** Maximum number of annotations shown in Chat Panel history popup (Issue #5821) */
constexpr int32 CUAnnotationHistoryMaxCount = 10;
