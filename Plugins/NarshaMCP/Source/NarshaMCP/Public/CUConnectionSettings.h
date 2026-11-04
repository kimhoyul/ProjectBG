// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #25250: WS connection settings surface (EditorPerProjectUserSettings)

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "CUConnectionSettings.generated.h"

/**
 * Per-project local connection settings for NarshaMCP (Issue #25250).
 *
 * Stored in EditorPerProjectUserSettings so multi-worktree / multi-session
 * WS ports are NOT checked into shared Default*.ini (SVN-friendly).
 *
 * Section: [/Script/NarshaMCP.CUConnectionSettings]
 * Access: Edit > Project Settings > Plugins > NarshaMCP - Connection
 *
 * Resolve order in ResolveWsBasePort:
 *   1. -NarshaMCPWSPort=N (cmdline)
 *   2. UECODEGEN_RC_WS_PORT (env)
 *   3. WebSocketPort from this settings object (when > 0)
 *   4. kDefaultWsPort scan base
 */
UCLASS(Config = EditorPerProjectUserSettings, meta = (DisplayName = "Connection"))
class NARSHAMCP_API UCUConnectionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/**
	 * Explicit WebSocket port for this Editor project (0 = unset / free-port scan).
	 * When set (1..65535), StartAuto binds exactly this port (fail loud if taken; #25249).
	 */
	UPROPERTY(Config, EditAnywhere, Category = "WebSocket",
		meta = (ClampMin = "0", ClampMax = "65535", UIMin = "0", UIMax = "65535"))
	int32 WebSocketPort = 0;

	virtual FName GetContainerName() const override { return TEXT("Project"); }
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
	virtual FName GetSectionName() const override { return TEXT("NarshaMCP - Connection"); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override
	{
		return NSLOCTEXT("CUConnectionSettings", "SectionText", "Connection");
	}

	virtual FText GetSectionDescription() const override
	{
		return NSLOCTEXT("CUConnectionSettings", "SectionDescription",
			"Per-project WebSocket port for NarshaMCP (local EditorPerProjectUserSettings). "
			"Priority: cmdline -NarshaMCPWSPort > env UECODEGEN_RC_WS_PORT > this setting > free-port scan.");
	}
#endif

	UCUConnectionSettings();
};
