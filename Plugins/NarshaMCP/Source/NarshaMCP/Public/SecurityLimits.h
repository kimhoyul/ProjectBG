// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #1402: Centralized security limits and constants

#pragma once

#include "CoreMinimal.h"

/**
 * Security Limits for NarshaMCP (Issue #1402)
 *
 * Centralized constants for all security-related limits.
 * Single source of truth for tuning security parameters.
 *
 * Design Rationale:
 * - MaxGZIPCompressedSize: 10MB prevents memory spike from large compressed files
 * - MaxGZIPDecompressedSize: 100MB prevents memory exhaustion from GZIP bombs
 * - MaxGZIPCompressionRatio: 100:1 catches crafted GZIP bombs (normal JSON is ~10:1)
 * - MaxWebSocketMessageSize: 1MB is sufficient for all JSON-RPC messages
 * - MaxWebSocketRequestsPerSecond: 30 allows responsive UI while preventing DoS
 * - MaxStringLength*: Prevents integer overflow in string operations
 */
namespace SecurityLimits
{
	// ============================================
	// GZIP Decompression Limits (CommandletHelpers)
	// ============================================

	/** Maximum compressed file size before decompression (Issue #1402 C-3) */
	constexpr int32 MaxGZIPCompressedSize = 10 * 1024 * 1024;  // 10MB

	/** Maximum decompressed data size (Issue #1402 GZIP bomb protection) */
	constexpr int32 MaxGZIPDecompressedSize = 100 * 1024 * 1024;  // 100MB

	/** Maximum compression ratio (decompressed/compressed) to detect GZIP bombs */
	constexpr float MaxGZIPCompressionRatio = 100.0f;  // 100:1 max ratio

	/** Maximum concurrent decompression operations (Issue #1402 L-2) */
	constexpr int32 MaxConcurrentDecompressions = 2;

	// ============================================
	// WebSocket Security Limits
	// ============================================

	/** Maximum WebSocket message size (Issue #1402) */
	constexpr int32 MaxWebSocketMessageSize = 1024 * 1024;  // 1MB

	/** Maximum requests per second per client (Issue #1402 rate limiting) */
	constexpr int32 MaxWebSocketRequestsPerSecond = 30;

	/** Rate limit violation threshold before disconnect (Issue #1402 M-1) */
	constexpr int32 MaxRateLimitViolationsBeforeDisconnect = 3;

	/** Token bucket capacity for burst protection (Issue #1402 C-2) */
	constexpr double TokenBucketCapacity = 30.0;

	/** Token bucket refill rate (tokens per second) */
	constexpr double TokenBucketRefillRate = 30.0;

	// ============================================
	// String Length Limits (UAssetBinaryModifier)
	// ============================================

	/** Maximum string length for scanning FStrings in binary data */
	constexpr int32 MaxStringLengthScan = 2048;

	/** Maximum string length for reading FStrings */
	constexpr int32 MaxStringLengthRead = 4096;

	/** Maximum UTF-16 string length (prevents multiplication overflow) */
	constexpr int32 MaxStringLengthUTF16 = 65536;

	/** Maximum UTF-8 string length */
	constexpr int32 MaxStringLengthUTF8 = 65536;

	// ============================================
	// Path Security Limits (Issue #1403)
	// ============================================

	/**
	 * Check for path traversal sequences in raw path string
	 *
	 * Detects ".." sequences that could escape intended directory boundaries.
	 * This check should be performed BEFORE path normalization as the first
	 * line of defense (defense-in-depth strategy).
	 *
	 * Attack Vectors Blocked:
	 * - "/Game/../Config/DefaultEngine.ini" → Access config files
	 * - "/Game/../../etc/passwd" → Access system files (Linux)
	 * - "/Game/../../../Windows/System32" → Access system files (Windows)
	 *
	 * @param Path - Raw input path to check (before normalization)
	 * @return true if path traversal detected, false otherwise
	 *
	 * Example:
	 *   ContainsPathTraversal("/Game/../Config") → true (BLOCKED)
	 *   ContainsPathTraversal("/Game/Blueprints/BP_Test") → false (ALLOWED)
	 *
	 * @see ContainsEncodedPathTraversal for URL-encoded variants
	 *
	 * @note noexcept rationale (Issue #2443): FString::Contains() is guaranteed
	 *       noexcept in Unreal Engine - it performs in-place string search without
	 *       memory allocation or exception-throwing operations.
	 */
	inline bool ContainsPathTraversal(const FString& Path) noexcept
	{
		// Check raw path BEFORE normalization for defense-in-depth
		return Path.Contains(TEXT(".."));
	}

	/**
	 * Check for URL-encoded path traversal patterns (Issue #1402 M-2, #1403)
	 *
	 * Detects URL-encoded variants of path traversal sequences that bypass
	 * plain string checks. CRITICAL: Must be called BEFORE path normalization,
	 * as normalization does NOT decode URL encoding.
	 *
	 * Encoded Patterns Detected:
	 * - %2e = '.' (lowercase)
	 * - %2E = '.' (uppercase)
	 * - %2f = '/' (forward slash, lowercase)
	 * - %2F = '/' (forward slash, uppercase)
	 * - %5c = '\' (backslash, lowercase)
	 * - %5C = '\' (backslash, uppercase)
	 *
	 * Attack Vectors Blocked:
	 * - "/Game/%2e%2e/Config" → Encoded "../" (lowercase)
	 * - "/Game/%2E%2E/Config" → Encoded "../" (uppercase)
	 * - "/Game/path%2f%2e%2e" → Encoded "/.." in path segment
	 * - "path%5c%2e%2e" → Windows backslash variant
	 *
	 * @param Path - Raw input path to check (before normalization)
	 * @return true if URL-encoded traversal detected, false otherwise
	 *
	 * Example:
	 *   ContainsEncodedPathTraversal("/Game/%2e%2e/Config") → true (BLOCKED)
	 *   ContainsEncodedPathTraversal("/Game/Blueprints/BP_Test") → false (ALLOWED)
	 *   ContainsEncodedPathTraversal("path?param=value") → false (URL params OK)
	 *
	 * @see ContainsPathTraversal for plain-text variant detection
	 *
	 * @note noexcept rationale (Issue #2443): FString::Contains() is guaranteed
	 *       noexcept in Unreal Engine - it performs in-place string search without
	 *       memory allocation or exception-throwing operations.
	 */
	inline bool ContainsEncodedPathTraversal(const FString& Path) noexcept
	{
		// Check for URL-encoded variants: %2e = '.', %2f = '/', %5c = '\'
		return Path.Contains(TEXT("%2e")) ||
		       Path.Contains(TEXT("%2E")) ||
		       Path.Contains(TEXT("%2f")) ||
		       Path.Contains(TEXT("%2F")) ||
		       Path.Contains(TEXT("%5c")) ||
		       Path.Contains(TEXT("%5C"));
	}
}
