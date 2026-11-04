// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

// Forward declarations
class IAssetRegistry;
class FJsonObject;

/**
 * Utility functions for NarshaMCP commandlets
 *
 * Provides common functionality used across multiple commandlets:
 * - Parameter parsing
 * - Directory management
 * - AssetRegistry initialization
 * - JSON serialization
 *
 * Purpose: Eliminate code duplication and enable batch commandlet execution
 *
 * @since v4.8.0 (Batch Commandlet Implementation)
 */
class NARSHAMCP_API FCUCommandletHelpers
{
public:
	/**
	 * Parse output directory from commandlet parameters
	 *
	 * Searches for "OutputDir=" token in parameters and returns the path.
	 * If not found, returns default path: {ProjectDir}/Intermediate/NarshaMCP
	 *
	 * @param Params - Commandlet parameters string (e.g., "OutputDir=D:/Custom/Path -NoShaderCompile")
	 * @return Output directory path
	 *
	 * Example:
	 *   FString OutDir = FCUCommandletHelpers::ParseOutputDirectory("OutputDir=D:/Temp");
	 *   // OutDir = "D:/Temp"
	 */
	static FString ParseOutputDirectory(const FString& Params);

	/**
	 * Ensure output directory exists, create if necessary
	 *
	 * Creates directory tree recursively if it doesn't exist.
	 * Safe to call multiple times (idempotent).
	 *
	 * @param OutputDir - Directory path to ensure exists
	 * @return True if directory exists or was created successfully, false on error
	 *
	 * Example:
	 *   FCUCommandletHelpers::EnsureOutputDirectory("D:/NarshaMCP/Output");
	 */
	static bool EnsureOutputDirectory(const FString& OutputDir);

	/**
	 * Initialize AssetRegistry and wait for completion
	 *
	 * Loads AssetRegistry module, triggers full asset scan, and waits until complete.
	 * This is the most time-consuming operation in commandlets (10-50 seconds).
	 *
	 * IMPORTANT: In batch commandlets, call this ONCE and reuse the reference.
	 *
	 * @param bWaitForCompletion - If true, blocks until AssetRegistry.IsLoadingAssets() returns false
	 * @return Reference to initialized AssetRegistry
	 *
	 * Example:
	 *   IAssetRegistry& AssetRegistry = FCUCommandletHelpers::InitializeAssetRegistry(true);
	 *   // Now ready to use for asset queries
	 */
	static IAssetRegistry& InitializeAssetRegistry(bool bWaitForCompletion = true);

	/**
	 * Save JSON object to file
	 *
	 * Serializes JSON object to string and writes to file.
	 * Creates parent directories if needed.
	 *
	 * @param OutputPath - Full file path (e.g., "D:/Output/data.json")
	 * @param JsonObject - JSON object to serialize
	 * @return True if saved successfully, false on error
	 *
	 * Example:
	 *   TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
	 *   Json->SetStringField("version", "1.0");
	 *   FCUCommandletHelpers::SaveJsonToFile("D:/output.json", Json);
	 */
	static bool SaveJsonToFile(const FString& OutputPath, TSharedPtr<FJsonObject> JsonObject);

	/**
	 * Emit a single-line machine-readable result marker for a commandlet run.
	 *
	 * Serializes `Report` to compact JSON and logs `NARSHAMCP_RESULT: {...}` at Display so the Rust
	 * build_pipeline runner can treat the commandlet's self-reported `success` as authoritative over the
	 * editor PROCESS exit code (which can be 0 on a logical failure, or non-zero on a logical success
	 * after RHI teardown). `Report` MUST carry a top-level "success" bool.
	 *
	 * Single shared definition (Issue #16915 origin; #17410 root fix): the author commandlets previously
	 * each declared an identical anonymous-namespace `EmitResultMarker`, which is a unity-build ODR
	 * collision (C2084) waiting to be triggered by file-set changes. One definition here removes both the
	 * collision and the duplication.
	 *
	 * Example:
	 *   TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	 *   R->SetBoolField(TEXT("success"), true);
	 *   FCUCommandletHelpers::EmitResultMarker(R);
	 */
	static void EmitResultMarker(const TSharedRef<FJsonObject>& Report);

	/**
	 * Save JSON object to file with GZIP compression
	 *
	 * Serializes JSON and compresses with GZIP, saves as {OutputPath}.gz
	 * Typically reduces file size by 70-90% for large metadata files.
	 *
	 * @param OutputPath - Base file path (e.g., "D:/Output/data.json")
	 *                     Actual output: "D:/Output/data.json.gz"
	 * @param JsonObject - JSON object to serialize and compress
	 * @return True if saved successfully, false on error
	 *
	 * Example:
	 *   TSharedPtr<FJsonObject> Json = MakeShareable(new FJsonObject);
	 *   // ... populate JSON with large data ...
	 *   FCUCommandletHelpers::SaveJsonWithCompression("D:/output.json", Json);
	 *   // Creates: D:/output.json.gz (compressed)
	 */
	static bool SaveJsonWithCompression(const FString& OutputPath, TSharedPtr<FJsonObject> JsonObject);

	/**
	 * Load JSON from file (supports GZIP-compressed .json.gz files) - Issue #213
	 *
	 * Loads and deserializes JSON from file. Automatically detects GZIP compression
	 * by checking file extension (.json.gz) and decompresses if needed.
	 *
	 * @param FilePath - Path to JSON file (.json or .json.gz)
	 *                   Example: "D:/Output/blueprint_metadata.json.gz"
	 * @return Shared pointer to JSON object (null if failed to load or parse)
	 *
	 * Example:
	 *   TSharedPtr<FJsonObject> Json = FCUCommandletHelpers::LoadJsonFromFile("D:/output.json.gz");
	 *   if (Json.IsValid())
	 *   {
	 *       FString Version = Json->GetStringField("version");
	 *   }
	 */
	static TSharedPtr<FJsonObject> LoadJsonFromFile(const FString& FilePath);

	/**
	 * Parse task list from commandlet parameters
	 *
	 * Extracts comma-separated task names from "-Tasks=A,B,C" parameter.
	 * Used by batch commandlets to selectively run tasks.
	 *
	 * @param Params - Commandlet parameters string
	 * @param OutTasks - Output array of task names
	 *
	 * Example:
	 *   TArray<FString> Tasks;
	 *   FCUCommandletHelpers::ParseTasks("-Tasks=ValidatePatterns,BlueprintMetadata", Tasks);
	 *   // Tasks = ["ValidatePatterns", "BlueprintMetadata"]
	 */
	static void ParseTasks(const FString& Params, TArray<FString>& OutTasks);

	/**
	 * Log progress for batch operations
	 *
	 * Formats and logs progress message with percentage and task name.
	 *
	 * @param TaskName - Name of current task
	 * @param Current - Current task index (1-based)
	 * @param Total - Total number of tasks
	 *
	 * Example:
	 *   FCUCommandletHelpers::LogProgress("ValidatePatterns", 1, 5);
	 *   // Output: "[NarshaMCP] [1/5] (20%) Running: ValidatePatterns..."
	 */
	static void LogProgress(const FString& TaskName, int32 Current, int32 Total);

	// ========================================================================
	// PROFILING HELPERS (v4.9.0 - Performance Profiling Feature)
	// ========================================================================

	/**
	 * Begin profiling a named section
	 *
	 * Starts a timer for the specified section. Call EndProfileSection() to stop.
	 * Supports hierarchical sections (e.g., "CommandletName.AssetSearch").
	 *
	 * @param SectionName - Unique name for this profiling section
	 *
	 * Example:
	 *   FCUCommandletHelpers::BeginProfileSection("NarshaMCP.AssetSearch");
	 *   // ... perform asset search ...
	 *   double ElapsedSec = FCUCommandletHelpers::EndProfileSection("NarshaMCP.AssetSearch");
	 */
	static void BeginProfileSection(const FString& SectionName);

	/**
	 * End profiling a named section and return elapsed time
	 *
	 * Stops the timer started by BeginProfileSection() and records the result.
	 * Returns elapsed time in seconds.
	 *
	 * @param SectionName - Section name (must match BeginProfileSection call)
	 * @return Elapsed time in seconds, or 0.0 if section not found
	 *
	 * Example:
	 *   FCUCommandletHelpers::BeginProfileSection("AssetSearch");
	 *   // ... work ...
	 *   double Elapsed = FCUCommandletHelpers::EndProfileSection("AssetSearch");
	 *   UE_LOG(LogNarshaMCP, Display, TEXT("AssetSearch took %.1fs"), Elapsed);
	 */
	static double EndProfileSection(const FString& SectionName);

	/**
	 * Log profiling results to console
	 *
	 * Outputs all recorded profiling sections with their times and percentages.
	 * Automatically identifies bottlenecks (>30% of total time).
	 *
	 * Example output:
	 *   [Profile] NarshaMCP.AssetSearch:       2.1s (20%)
	 *   [Profile] NarshaMCP.BlueprintLoading:  6.2s (59%) ⚠️ BOTTLENECK
	 *   [Profile] NarshaMCP.TagExtraction:     1.5s (14%)
	 */
	static void LogProfileResults();

	/**
	 * Get profiling results as JSON object
	 *
	 * Returns all profiling data in structured JSON format for serialization.
	 * Includes timestamps, section times, and bottleneck identification.
	 *
	 * @return JSON object with profiling data
	 *
	 * Example JSON:
	 * {
	 *   "timestamp": "2025-10-23T17:19:24",
	 *   "sections": {
	 *     "NarshaMCP.AssetSearch": { "time_sec": 2.1, "percentage": 20.0 },
	 *     "NarshaMCP.BlueprintLoading": { "time_sec": 6.2, "percentage": 59.0, "is_bottleneck": true }
	 *   },
	 *   "total_time_sec": 10.5
	 * }
	 */
	static TSharedPtr<FJsonObject> GetProfileResultsAsJson();

	/**
	 * Clear all profiling data
	 *
	 * Resets profiling timers and results. Use at start of batch operations.
	 */
	static void ClearProfileData();

private:
	// Issue #1403: Thread-safe profiling data storage
	static FCriticalSection GProfileLock;
	static TMap<FString, FDateTime> ProfileTimers;
	static TMap<FString, double> ProfileResults;
};
