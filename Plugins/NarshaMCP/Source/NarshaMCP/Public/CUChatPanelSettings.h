// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5589: Editor Preferences toggle for AI Chat Panel

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CUChatPanelSettings.generated.h"

/**
 * Developer settings for AI Chat Panel (Issue #5589)
 *
 * Controls AI Chat Panel visibility in the UE Editor.
 * Accessible via Edit > Project Settings > Plugins > NarshaMCP - AI Chat Panel.
 *
 * Priority: config.ini [AIChatPanel] section > UDeveloperSettings defaults
 * (config.ini overrides when present, for backward compatibility)
 *
 * Usage:
 *   const UCUChatPanelSettings* Settings = GetDefault<UCUChatPanelSettings>();
 *   bool bShow = Settings->bShowInEditor;
 *
 * Config File: Config/DefaultAIChatPanel.ini
 * Section: [/Script/NarshaMCP.CUChatPanelSettings]
 */
UCLASS(Config = AIChatPanel, DefaultConfig, meta = (DisplayName = "AI Chat Panel"))
class NARSHAMCP_API UCUChatPanelSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Show AI Chat Panel in the Editor.
	 *
	 * When false: no dock tab, no Ctrl+Shift+P shortcut.
	 * Requires Editor restart to take effect.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadWrite, Category = "General")
	bool bShowInEditor = true;

	/** Get container name for Project Settings */
	virtual FName GetContainerName() const override { return TEXT("Project"); }

	/** Get category name for Project Settings */
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** Get section name for Project Settings */
	virtual FName GetSectionName() const override { return TEXT("NarshaMCP - AI Chat Panel"); }

#if WITH_EDITOR
	/** Get section text for Project Settings UI */
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("CUChatPanelSettings", "SectionText", "AI Chat Panel");
	}

	/** Get section description for Project Settings UI */
	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("CUChatPanelSettings", "SectionDescription",
			"Configure the AI Chat Panel visibility. Changes require Editor restart.");
	}
#endif

	UCUChatPanelSettings();
};
