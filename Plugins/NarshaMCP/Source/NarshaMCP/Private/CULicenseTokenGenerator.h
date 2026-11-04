// Copyright 2026 Next Stage Inc. All rights reserved.
// CULicenseTokenGenerator.h — Fab license token generation (Issue #7948)
//
// Generates license tokens for narshamcp.exe verification (Issue #25867 Option A:
// unsigned tokens — Firebase is the entitlement boundary; no private key in source).
// Called during plugin StartupModule after PluginWarden entitlement check.
//
// Token file: {ProjectDir}/Intermediate/NarshaMCP/.license_token
// Token validity: 7 days (re-generated every Editor launch)
//
// Phase 1 (pre-ID): PluginWarden bypassed, token always generated
// Phase 2 (post-ID): Real PluginWarden verification with Epic-assigned IDs

#pragma once

#include "CoreMinimal.h"

/**
 * Generates license tokens for the MCP server binary (Issue #25867 Option A).
 *
 * The token bridges PluginWarden (Editor-only) verification to the
 * standalone narshamcp.exe. Token includes machine ID binding to
 * prevent cross-machine copying. Signature is written empty — the
 * server never verified it, and a private key must not ship in source.
 *
 * CU prefix compliance: Issue #5915
 */
class FCULicenseTokenGenerator
{
public:
	/**
	 * Generate a license token and write it to disk.
	 *
	 * @param ProjectDir The project root directory (for Intermediate/NarshaMCP/ path)
	 * @return true if token was successfully written
	 */
	static bool Generate(const FString& ProjectDir);

	/**
	 * Check if PluginWarden confirms Fab entitlement.
	 *
	 * Phase 1: Always returns true (placeholder IDs)
	 * Phase 2: Calls IPluginWardenModule with real ItemID/OfferID
	 *
	 * @return true if entitled (or Phase 1 bypass)
	 */
	static bool CheckEntitlement();

	/**
	 * Get a deterministic machine fingerprint.
	 *
	 * SHA256(cpu_brand + "\n" + hostname + "\n" + machine_guid)
	 * Must produce identical output to Rust license::machine::machine_id()
	 *
	 * @return Hex-encoded SHA256 hash (64 chars)
	 */
	static FString GetMachineId();

private:
	/**
	 * Write token JSON to disk with atomic rename.
	 *
	 * Writes to .license_token.tmp first, then renames to .license_token.
	 *
	 * @param TokenDir Directory to write to (Intermediate/NarshaMCP/)
	 * @param TokenJson Full JSON content (signature field is empty, Issue #25867)
	 * @return true if write succeeded
	 */
	static bool WriteTokenAtomic(const FString& TokenDir, const FString& TokenJson);

	// PluginWarden IDs (Phase 1: placeholder, Phase 2: real from Epic)
	// TODO(Issue #7948 Phase 2): Replace with real ItemID/OfferID from Epic
	static constexpr const TCHAR* FabItemId = TEXT("PLACEHOLDER_ITEM_ID");
	static constexpr const TCHAR* FabOfferId = TEXT("PLACEHOLDER_OFFER_ID");

	// Token validity: 7 days in seconds
	static constexpr int64 TokenValiditySecs = 7 * 24 * 3600; // 604800
};
