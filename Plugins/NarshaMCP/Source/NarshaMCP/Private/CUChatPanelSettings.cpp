// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5589: Editor Preferences toggle for AI Chat Panel

#include "CUChatPanelSettings.h"

UCUChatPanelSettings::UCUChatPanelSettings()
{
	// Default values set in header:
	// - bShowInEditor = true (panel enabled by default)
	//
	// Users can configure via:
	// 1. Project Settings UI (Edit > Project Settings > Plugins > NarshaMCP - AI Chat Panel)
	// 2. INI file (Config/DefaultAIChatPanel.ini)
	// 3. config.ini [AIChatPanel] section (legacy override, takes precedence)
}
