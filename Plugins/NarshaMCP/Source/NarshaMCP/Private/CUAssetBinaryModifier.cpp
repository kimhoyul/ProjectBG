// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUAssetBinaryModifier.h"
#include "SecurityLimits.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformMemory.h"

DEFINE_LOG_CATEGORY_STATIC(LogNarshaMCPBinary, Log, All);

int32 FCUAssetBinaryModifier::NullifyMeshReferences(TArray<uint8>& AssetData)
{
	if (AssetData.Num() < 100)
	{
		// Too small to be a valid uasset
		return 0;
	}

	// Find all mesh path offsets
	TArray<int32> MeshOffsets = FindMeshPathOffsets(AssetData);

	if (MeshOffsets.Num() == 0)
	{
		// No mesh references found
		return 0;
	}

	// Nullify each mesh reference
	int32 NullifiedCount = 0;
	for (int32 Offset : MeshOffsets)
	{
		if (NullifyFString(AssetData, Offset))
		{
			NullifiedCount++;
		}
	}

	return NullifiedCount;
}

TArray<int32> FCUAssetBinaryModifier::FindMeshPathOffsets(const TArray<uint8>& AssetData)
{
	TArray<int32> Offsets;

	UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier] Scanning all FStrings for mesh references..."));
	UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier] File size: %d bytes"), AssetData.Num());

	int32 ScannedStrings = 0;
	int32 FoundMeshes = 0;

	// Scan through entire file looking for FString length prefixes
	for (int32 i = 0; i < AssetData.Num() - 4; i++)
	{
		// Read potential length prefix
		int32 Length;
		FMemory::Memcpy(&Length, &AssetData[i], sizeof(int32));

		// Skip invalid lengths
		if (Length == 0)
		{
			continue;
		}

		// Calculate string size
		int32 StringSize = 0;
		bool bIsUTF16 = false;
		if (Length < 0)
		{
			// ============================================
			// Security: Integer Overflow Protection (Issue #1402)
			// Defense-in-Depth for negation and multiplication:
			//   1. Check for INT32_MIN (negation overflow)
			//   2. Check multiplication won't overflow
			//   3. Check result is within reasonable bounds
			// ============================================

			// Layer 1: INT32_MIN check (negating INT32_MIN causes overflow)
			if (Length == INT32_MIN)
			{
				continue;  // Cannot safely negate
			}

			int32 CharCount = -Length;

			// Layer 2: Multiplication overflow check (CharCount * 2)
			// If CharCount > INT32_MAX / 2, then CharCount * 2 overflows
			if (CharCount > INT32_MAX / 2)
			{
				continue;  // Would overflow
			}

			// Layer 3: Reasonable bounds check (Issue #1402 M-5)
			// Use centralized constant for consistency
			if (CharCount > SecurityLimits::MaxStringLengthUTF16)
			{
				continue;  // Unreasonably large
			}

			StringSize = CharCount * 2;
			bIsUTF16 = true;
		}
		else if (Length > 0 && Length < SecurityLimits::MaxStringLengthScan) // Use centralized constant
		{
			// UTF-8 string
			StringSize = Length;
		}
		else
		{
			continue; // Not a valid string
		}

		// Check if we have enough data
		if (i + 4 + StringSize > AssetData.Num())
		{
			continue;
		}

		// Try to read the string
		FString PathString;
		if (ReadFString(AssetData, i, PathString))
		{
			ScannedStrings++;

			// Debug: Log first 20 strings to see what we're scanning
			if (ScannedStrings <= 20)
			{
				UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier]   Sample string %d at offset %d: %s"), ScannedStrings, i, *PathString);
			}

			// Check if this is a mesh reference path
			// Mesh paths contain: /Script/Engine.SkeletalMesh or /Script/Engine.StaticMesh
			if ((PathString.Contains(TEXT("/Script/Engine.SkeletalMesh")) ||
			     PathString.Contains(TEXT("/Script/Engine.StaticMesh"))) &&
			    PathString.Contains(TEXT("/Game/")))
			{
				FoundMeshes++;
				UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier]   Found mesh at offset %d: %s"), i, *PathString);
				Offsets.AddUnique(i);
			}
			// Also check for direct asset paths (just SK_ or SM_ with /Game/)
			else if (PathString.Contains(TEXT("/Game/")) &&
			         (PathString.Contains(TEXT("SK_")) || PathString.Contains(TEXT("SM_"))))
			{
				FoundMeshes++;
				UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier]   Found mesh (asset path) at offset %d: %s"), i, *PathString);
				Offsets.AddUnique(i);
			}
		}
	}

	UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier] Scanned %d FStrings, found %d mesh references"), ScannedStrings, FoundMeshes);
	return Offsets;
}

bool FCUAssetBinaryModifier::NullifyFString(TArray<uint8>& Data, int32 Offset)
{
	if (Offset + 4 > Data.Num())
	{
		return false;
	}

	// Read current length
	int32 Length;
	FMemory::Memcpy(&Length, &Data[Offset], sizeof(int32));

	if (Length == 0)
	{
		// Already empty
		return false;
	}

	// Calculate string size in bytes
	int32 StringSize = 0;
	bool bIsUTF16 = false;
	if (Length < 0)
	{
		// ============================================
		// Security: Integer Overflow Protection (Issue #1402)
		// Same defense-in-depth as FindMeshPathOffsets
		// ============================================

		// Layer 1: INT32_MIN check
		if (Length == INT32_MIN)
		{
			return false;  // Cannot safely negate
		}

		int32 CharCount = -Length;

		// Layer 2: Multiplication overflow check
		if (CharCount > INT32_MAX / 2)
		{
			return false;  // Would overflow
		}

		// Layer 3: Reasonable bounds check (Issue #1402 M-5)
		if (CharCount > SecurityLimits::MaxStringLengthUTF16)
		{
			return false;  // Unreasonably large
		}

		StringSize = CharCount * 2;
		bIsUTF16 = true;
	}
	else
	{
		// UTF-8 string (positive length)
		// Layer 3: Reasonable bounds check (Issue #1402 M-5)
		if (Length > SecurityLimits::MaxStringLengthUTF8)
		{
			return false;  // Unreasonably large
		}
		StringSize = Length;
	}

	// Validate we have enough space
	if (Offset + 4 + StringSize > Data.Num())
	{
		return false;
	}

	// STRATEGY: Replace the entire path with padding characters to maintain size
	// This preserves PropertySize and all offsets, preventing file corruption
	// Fill with spaces + null terminator to indicate "empty/invalid" reference

	if (bIsUTF16)
	{
		// UTF-16: Fill with spaces (0x0020) and end with null (0x0000)
		int32 CharCount = -Length;
		for (int32 i = 0; i < CharCount - 1; i++)
		{
			uint16_t SpaceChar = 0x0020;
			FMemory::Memcpy(&Data[Offset + 4 + i * 2], &SpaceChar, sizeof(uint16_t));
		}
		// Null terminator at end
		uint16_t NullChar = 0x0000;
		FMemory::Memcpy(&Data[Offset + 4 + (CharCount - 1) * 2], &NullChar, sizeof(uint16_t));
	}
	else
	{
		// UTF-8: Fill with spaces (0x20) and end with null (0x00)
		for (int32 i = 0; i < Length - 1; i++)
		{
			Data[Offset + 4 + i] = 0x20; // Space character
		}
		// Null terminator at end
		Data[Offset + 4 + Length - 1] = 0x00;
	}

	UE_LOG(LogNarshaMCPBinary, Display, TEXT("[UAssetBinaryModifier] Nullified FString at offset %d (size preserved: %d bytes)"), Offset, StringSize);
	return true;
}

bool FCUAssetBinaryModifier::ReadFString(const TArray<uint8>& Data, int32 Offset, FString& OutString)
{
	OutString.Empty();

	if (Offset + 4 > Data.Num())
	{
		return false;
	}

	// Read length
	int32 Length;
	FMemory::Memcpy(&Length, &Data[Offset], sizeof(int32));

	if (Length == 0)
	{
		// Empty string
		OutString = TEXT("");
		return true;
	}

	// Note: String length limits now in SecurityLimits.h for consistency (Issue #1402 M-5)

	if (Length < 0)
	{
		// ============================================
		// Security: Integer Overflow Protection (Issue #1402 C-4)
		// Defense-in-Depth with SEPARATE overflow checks:
		//   1. INT32_MIN negation check
		//   2. Reasonable bounds check
		//   3. Multiplication overflow check
		//   4a. Addition overflow check (Offset + 4)
		//   4b. Addition overflow check (result + ByteCount)
		//   5. Bounds check against actual data
		// ============================================

		// Layer 1: INT32_MIN check (negating causes overflow)
		if (Length == INT32_MIN)
		{
			return false;  // Cannot safely negate
		}

		int32 CharCount = -Length;

		// Layer 2: Reasonable bounds check (Issue #1402 M-5)
		if (CharCount > SecurityLimits::MaxStringLengthRead || CharCount <= 0)
		{
			return false;  // Unreasonably large or invalid
		}

		// Layer 3: Multiplication overflow check
		if (CharCount > INT32_MAX / 2)
		{
			return false;  // CharCount * 2 would overflow
		}

		int32 ByteCount = CharCount * 2;

		// Layer 4a: Addition overflow check - Offset + 4 (Issue #1402 C-4)
		// Split into separate checks to prevent intermediate overflow
		if (Offset > INT32_MAX - 4)
		{
			return false;  // Offset + 4 would overflow
		}

		int32 OffsetPlusFour = Offset + 4;  // Now safe to compute

		// Layer 4b: Addition overflow check - (Offset + 4) + ByteCount
		if (OffsetPlusFour > INT32_MAX - ByteCount)
		{
			return false;  // Offset + 4 + ByteCount would overflow
		}

		// Layer 5: Bounds check against actual data
		if (OffsetPlusFour + ByteCount > Data.Num())
		{
			return false;  // Out of bounds
		}

		// Convert UTF-16 to FString
		TArray<TCHAR> Buffer;
		Buffer.SetNumUninitialized(CharCount);
		FMemory::Memcpy(Buffer.GetData(), &Data[OffsetPlusFour], ByteCount);

		// Additional safety: check for null terminator
		if (CharCount > 0 && Buffer[CharCount - 1] == 0)
		{
			OutString = FString(CharCount - 1, Buffer.GetData()); // -1 to exclude null terminator
			return true;
		}
		else
		{
			// Invalid string (no null terminator)
			return false;
		}
	}
	else
	{
		// UTF-8 string

		// Layer 2: Reasonable bounds check (Issue #1402 M-5)
		if (Length > SecurityLimits::MaxStringLengthRead || Length <= 0)
		{
			return false;  // Unreasonably large or invalid
		}

		// Layer 4a: Addition overflow check - Offset + 4 (Issue #1402 C-4)
		if (Offset > INT32_MAX - 4)
		{
			return false;  // Offset + 4 would overflow
		}

		int32 OffsetPlusFour = Offset + 4;

		// Layer 4b: Addition overflow check - (Offset + 4) + Length
		if (OffsetPlusFour > INT32_MAX - Length)
		{
			return false;  // Offset + 4 + Length would overflow
		}

		// Layer 5: Bounds check against actual data
		if (OffsetPlusFour + Length > Data.Num())
		{
			return false;  // Out of bounds
		}

		// Convert UTF-8 to FString
		TArray<ANSICHAR> Buffer;
		Buffer.SetNumUninitialized(Length);
		FMemory::Memcpy(Buffer.GetData(), &Data[OffsetPlusFour], Length);

		// Additional safety: check for null terminator
		if (Length > 0 && Buffer[Length - 1] == 0)
		{
			OutString = FString(UTF8_TO_TCHAR(Buffer.GetData()));
			return true;
		}
		else
		{
			// Invalid string (no null terminator)
			return false;
		}
	}
}

bool FCUAssetBinaryModifier::IsMeshPath(const FString& Path)
{
	// Check for mesh asset patterns
	return Path.Contains(TEXT("/Game/")) &&
	       (Path.Contains(TEXT("SK_")) || Path.Contains(TEXT("SM_")));
}

TArray<int32> FCUAssetBinaryModifier::FindStringWithLengthPrefix(const TArray<uint8>& Data, const FString& SearchString)
{
	TArray<int32> Offsets;

	if (SearchString.IsEmpty())
	{
		return Offsets;
	}

	// Convert search string to UTF-8 (most common in uasset files)
	FTCHARToUTF8 UTF8String(*SearchString);
	int32 SearchLength = UTF8String.Length();

	if (SearchLength == 0)
	{
		return Offsets;
	}

	// Search through the data
	for (int32 i = 0; i < Data.Num() - SearchLength; i++)
	{
		// Check if content matches
		if (FMemory::Memcmp(&Data[i], UTF8String.Get(), SearchLength) == 0)
		{
			// Found string content, now check for length prefix before it
			if (i >= 4)
			{
				int32 Length;
				FMemory::Memcpy(&Length, &Data[i - 4], sizeof(int32));

				// Expected length = string length + 1 (null terminator)
				int32 ExpectedLength = SearchLength + 1;

				if (Length == ExpectedLength)
				{
					// Valid FString with length prefix!
					Offsets.Add(i - 4); // Return offset of length prefix
				}
			}
		}
	}

	return Offsets;
}
