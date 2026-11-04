// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Binary modification utility for .uasset files
 *
 * Purpose:
 *   Remove mesh references (SkeletalMesh, StaticMesh) from Blueprint assets
 *   BEFORE loading them, to prevent unnecessary heavy asset loading.
 *
 * Problem:
 *   - BlueprintMetadataCommandlet loads heavy mesh assets unnecessarily
 *   - Causes slow processing (91.4s for 1,513 Blueprints)
 *   - High memory usage (~100-500MB per Blueprint with mesh references)
 *
 * Solution:
 *   - Nullify FSoftObjectPath strings in .uasset binary data
 *   - Set FString length=0 (empty string) for mesh asset paths
 *   - Prevents mesh loading while preserving Blueprint metadata
 *
 * Expected Performance:
 *   - 5-10x faster Blueprint loading
 *   - 50-70% reduction in total commandlet time (91.4s → ~20-40s)
 *   - Memory reduction: ~100-500MB per Blueprint
 *
 * Technical Details:
 *   - FString format: [int32 Length][UTF-8/UTF-16 bytes]
 *   - Empty string: Length=0 (NO data bytes)
 *   - FSoftObjectPath: AssetPath (FString) + SubPathString (FString)
 *
 * Usage:
 *   TArray<uint8> AssetData;
 *   FFileHelper::LoadFileToArray(AssetData, *AssetPath);
 *   FCUAssetBinaryModifier::NullifyMeshReferences(AssetData);
 *   // Now load the modified data...
 */
class NARSHAMCP_API FCUAssetBinaryModifier
{
public:
	/**
	 * Nullify all mesh references in uasset binary data
	 *
	 * @param AssetData - Binary data of .uasset file (modified in-place)
	 * @return Number of mesh references nullified (0 if none found)
	 */
	static int32 NullifyMeshReferences(TArray<uint8>& AssetData);

	/**
	 * Search for FSoftObjectPath strings containing mesh paths
	 *
	 * Searches for common mesh path patterns:
	 *   - /Game/.../SK_*  (SkeletalMesh)
	 *   - /Game/.../SM_*  (StaticMesh)
	 *   - /Script/Engine.SkeletalMesh'...'
	 *   - /Script/Engine.StaticMesh'...'
	 *
	 * @param AssetData - Binary data to search
	 * @return Array of byte offsets where mesh paths were found (points to length prefix)
	 */
	static TArray<int32> FindMeshPathOffsets(const TArray<uint8>& AssetData);

private:
	/**
	 * Nullify FString at given offset
	 *
	 * Sets the FString length to 0 (empty string) and zeroes out the old data.
	 *
	 * @param Data - Binary data (modified in-place)
	 * @param Offset - Offset to FString length prefix
	 * @return true if successful, false if offset invalid
	 */
	static bool NullifyFString(TArray<uint8>& Data, int32 Offset);

	/**
	 * Read FString at given offset (for validation/debugging)
	 *
	 * @param Data - Binary data to read from
	 * @param Offset - Offset to FString length prefix
	 * @param OutString - Output string (empty if failed)
	 * @return true if successful, false if offset invalid
	 */
	static bool ReadFString(const TArray<uint8>& Data, int32 Offset, FString& OutString);

	/**
	 * Check if a string looks like a mesh asset path
	 *
	 * @param Path - Asset path to check
	 * @return true if path contains mesh references
	 */
	static bool IsMeshPath(const FString& Path);

	/**
	 * Find FString pattern in binary data
	 *
	 * Searches for length-prefixed strings matching the search string.
	 *
	 * @param Data - Binary data to search
	 * @param SearchString - String to find
	 * @return Array of offsets to length prefixes (not string data)
	 */
	static TArray<int32> FindStringWithLengthPrefix(const TArray<uint8>& Data, const FString& SearchString);
};
