// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #7106: Centralized log categories for NarshaMCP plugin

#pragma once

#include "CoreMinimal.h"

/**
 * Global log category for NarshaMCP plugin (Issue #7106)
 *
 * Usage:
 *   UE_LOG(LogNarshaMCP, Log, TEXT("Plugin initialized"));
 *   UE_LOG(LogNarshaMCP, Warning, TEXT("Config not found: %s"), *Path);
 *
 * Shared subsystem categories (used by multiple files):
 *   LogNarshaMCPMetadata   - Blueprint/AI/Input/UMG metadata extraction
 *   LogNarshaMCPCommandlet - Commandlet framework and batch operations
 *
 * For single-file subsystem logging, use DEFINE_LOG_CATEGORY_STATIC in .cpp files:
 *   DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPWebSocket, Log, All);
 *
 * Naming convention: LogNarshaMCP<Subsystem>
 */
NARSHAMCP_API DECLARE_LOG_CATEGORY_EXTERN(LogNarshaMCP, Log, All);
NARSHAMCP_API DECLARE_LOG_CATEGORY_EXTERN(LogNarshaMCPMetadata, Log, All);
NARSHAMCP_API DECLARE_LOG_CATEGORY_EXTERN(LogNarshaMCPCommandlet, Log, All);
NARSHAMCP_API DECLARE_LOG_CATEGORY_EXTERN(LogNarshaMCPBlueprint, Log, All);
NARSHAMCP_API DECLARE_LOG_CATEGORY_EXTERN(LogNarshaMCPPCG, Log, All);
