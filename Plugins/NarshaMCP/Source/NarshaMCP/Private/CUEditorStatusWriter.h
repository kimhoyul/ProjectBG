// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Editor Status Writer (Issue #118 Phase 1)
 *
 * Writes .narshamcp/editor_status.json to signal that:
 * 1. Editor is running
 * 2. NarshaMCP plugin is loaded
 * 3. Editor is ready for IPC communication
 */
class FCUEditorStatusWriter
{
public:
	/** True only for an interactive Editor process that may own the status marker. */
	static bool ShouldManageEditorStatus(bool bIsEditor, bool bIsCommandlet);

	/** Write editor status file (Issue #118) */
	static void WriteEditorStatus(const FString& ProjectDir);

	/** Delete editor status file on shutdown when the on-disk owner is this process. */
	static void DeleteEditorStatus(const FString& ProjectDir);

private:
	static uint32 GetCurrentProcessId();
	static const FString& GetCurrentProcessStartToken();
	static FString GetISO8601Timestamp();
};
