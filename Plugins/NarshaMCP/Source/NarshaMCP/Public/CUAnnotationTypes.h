// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5822: AI Annotation shared types — extracted for public visibility

#pragma once

#include "CoreMinimal.h"
#include "CUAnnotationTypes.generated.h"

/**
 * Severity level for AI annotations (Issue #5816, extracted Issue #5822).
 * Maps from Category strings to typed enum for color/icon dispatch.
 * Range: 0-3 (Info lowest, Error highest). Used by FCUAnnotationData::SeverityFromCategory().
 */
UENUM(BlueprintType)
enum class ECUAnnotationSeverity : uint8
{
	Info       = 0  UMETA(DisplayName = "Info"),
	Suggestion = 1  UMETA(DisplayName = "Suggestion"),
	Warning    = 2  UMETA(DisplayName = "Warning"),
	Error      = 3  UMETA(DisplayName = "Error")
};
