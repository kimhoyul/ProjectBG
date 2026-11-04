// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5822: AI Annotation M8 — Editor Preferences for annotation settings

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CUAnnotationTypes.h"
#include "CUAnnotationSettings.generated.h"

/**
 * Developer settings for AI Annotation system (Issue #5822).
 *
 * Controls annotation cache timing, auto-analysis behavior, and display filters.
 * Accessible via Edit > Project Settings > Plugins > NarshaMCP - AI Annotation.
 *
 * Config File: Config/DefaultAIAnnotation.ini
 * Section: [/Script/NarshaMCP.CUAnnotationSettings]
 *
 * Usage:
 *   const UCUAnnotationSettings* Settings = GetDefault<UCUAnnotationSettings>();
 *   double TTL = Settings->CacheTTLSeconds;
 */
UCLASS(Config = AIAnnotation, DefaultConfig, meta = (DisplayName = "AI Annotation"))
class NARSHAMCP_API UCUAnnotationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ── Cache ──

	/** Cache TTL in seconds. Annotations expire after this duration. Default: 300 (5 minutes). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Cache",
		meta = (ClampMin = "30.0", ClampMax = "3600.0", UIMin = "30.0", UIMax = "3600.0"))
	double CacheTTLSeconds = 300.0;

	/** Cache purge check interval in seconds. Default: 60 (1 minute). */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Cache",
		meta = (ClampMin = "10.0", ClampMax = "600.0"))
	float PurgeIntervalSeconds = 60.0f;

	// ── Auto-Analysis ──

	/** Enable auto-analysis on Blueprint node selection change. Generates AI API calls on selection. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Auto-Analysis")
	bool bAutoAnalysisEnabled = false;

	/** Selection polling interval for auto-analysis (seconds). Default: 0.5. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Auto-Analysis",
		meta = (ClampMin = "0.1", ClampMax = "5.0", EditCondition = "bAutoAnalysisEnabled"))
	float SelectionPollIntervalSeconds = 0.5f;

	/** Debounce for auto-analysis after selection change (seconds). Default: 1.0. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Auto-Analysis",
		meta = (ClampMin = "0.1", ClampMax = "10.0", EditCondition = "bAutoAnalysisEnabled"))
	double AutoAnalysisDebounceSeconds = 1.0;

	/** Debounce for explicit annotation requests (seconds). Default: 0.3. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Auto-Analysis",
		meta = (ClampMin = "0.0", ClampMax = "5.0"))
	double ExplicitRequestDebounceSeconds = 0.3;

	// ── Display ──

	/** Minimum severity level to display. Annotations below this severity are hidden. */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "Display")
	ECUAnnotationSeverity MinimumSeverity = ECUAnnotationSeverity::Info;

	// ── Project Settings UI ──

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("NarshaMCP - AI Annotation"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("CUAnnotationSettings", "SectionText", "AI Annotation");
	}

	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("CUAnnotationSettings", "SectionDescription",
			"Configure AI annotation overlays: cache timing, auto-analysis behavior, and display filters. Timing changes require Editor restart.");
	}
#endif

	UCUAnnotationSettings();
};
