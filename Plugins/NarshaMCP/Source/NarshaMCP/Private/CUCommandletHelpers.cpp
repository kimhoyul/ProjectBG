// Copyright 2026 Next Stage Inc. All rights reserved.

#include "CUCommandletHelpers.h"
#include "SecurityLimits.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Dom/JsonObject.h"
#include "Misc/Compression.h"
#include "Commandlets/Commandlet.h"

// Issue #1402 L-1: Windows API for symbolic link detection
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

#include "CULog.h"

// Issue #1402 L-2: Decompression throttling
static FCriticalSection GDecompressionLock;
static int32 GActiveDecompressions = 0;

void FCUCommandletHelpers::EmitResultMarker(const TSharedRef<FJsonObject>& Report)
{
	FString Compact;
	const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Compact);
	FJsonSerializer::Serialize(Report, Writer);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("NARSHAMCP_RESULT: %s"), *Compact);
}

FString FCUCommandletHelpers::ParseOutputDirectory(const FString& Params)
{
	// Default output directory (v5.1.0: metadata subdirectory)
	FString OutputDir = FPaths::ProjectDir() / TEXT("Intermediate/NarshaMCP/metadata");

	// Parse command line parameters
	TArray<FString> Tokens;
	TArray<FString> Switches;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches);

	// Search Tokens and Switches. `-OutputDir=` lands in Switches
	// (UCommandlet::ParseCommandLine), so Token-only lookup ignored the flag
	// and always defaulted to Intermediate/NarshaMCP/metadata (Issue #26815).
	auto Consume = [&OutputDir](const FString& Item)
	{
		FString Clean = Item;
		if (Clean.StartsWith(TEXT("-")))
		{
			Clean = Clean.RightChop(1);
		}
		if (Clean.StartsWith(TEXT("OutputDir=")))
		{
			OutputDir = Clean.RightChop(10);
			return true;
		}
		return false;
	};
	bool bFound = false;
	for (const FString& Token : Tokens)
	{
		if (Consume(Token))
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		for (const FString& Switch : Switches)
		{
			if (Consume(Switch))
			{
				break;
			}
		}
	}

	return OutputDir;
}

bool FCUCommandletHelpers::EnsureOutputDirectory(const FString& OutputDir)
{
	// ============================================
	// Security: Path Traversal Prevention (Issue #1402)
	// Defense-in-Depth Strategy (Issue #1404 bugfix):
	//   Layer 1: Check for URL-encoded traversal attempts BEFORE normalization
	//   Layer 2: Normalize path to resolve relative components
	//   Layer 3: Check normalized path for traversal (allows legitimate cross-drive paths)
	//   Layer 4: Verify normalized path is within project boundary
	//   Layer 5: Check for symbolic links (platform-specific)
	// ============================================

	// Layer 1: Check for URL-encoded traversal attempts (Issue #1402 M-2)
	// Attackers may use %2e%2e%2f to bypass string checks
	// NOTE: Check this BEFORE normalization since normalization won't decode URL encoding
	if (SecurityLimits::ContainsEncodedPathTraversal(OutputDir))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] URL-encoded path traversal detected: %s"), *OutputDir);
		return false;
	}

	// Layer 2: Normalize path to resolve relative components
	// NOTE: This converts relative paths (which may contain ..) to absolute paths
	// Issue #1404 bugfix: Cross-drive relative paths legitimately contain .. and should be allowed
	FString NormalizedPath = FPaths::ConvertRelativePathToFull(OutputDir);
	FPaths::NormalizeDirectoryName(NormalizedPath);

	// Layer 3: Check normalized path for traversal (defense-in-depth)
	// After normalization, no legitimate path should contain ".."
	if (NormalizedPath.Contains(TEXT("..")))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Path traversal detected after normalization: %s"), *NormalizedPath);
		return false;
	}

	// Layer 4: Verify path is within project boundary
	FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	FPaths::NormalizeDirectoryName(ProjectDir);

	if (!NormalizedPath.StartsWith(ProjectDir))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Output directory outside project: %s"), *NormalizedPath);
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Must be within: %s"), *ProjectDir);
		return false;
	}

	// Layer 5: Check for symbolic links (Issue #1402 L-1, Windows-specific)
#if PLATFORM_WINDOWS
	WIN32_FILE_ATTRIBUTE_DATA FileAttrData;
	if (GetFileAttributesExW(*NormalizedPath, GetFileExInfoStandard, &FileAttrData))
	{
		if (FileAttrData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Symbolic link/junction detected: %s"), *NormalizedPath);
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Symlinks are not allowed for security reasons"));
			return false;
		}
	}
#endif

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Directory already exists
	if (PlatformFile.DirectoryExists(*NormalizedPath))
	{
		return true;
	}

	// Create directory tree
	if (PlatformFile.CreateDirectoryTree(*NormalizedPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] Created output directory: %s"), *NormalizedPath);
		return true;
	}
	else
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to create output directory: %s"), *NormalizedPath);
		return false;
	}
}

IAssetRegistry& FCUCommandletHelpers::InitializeAssetRegistry(bool bWaitForCompletion)
{
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] Initializing AssetRegistry..."));

	// Load AssetRegistry module
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// Trigger full asset scan
	FDateTime StartTime = FDateTime::UtcNow();
	AssetRegistry.SearchAllAssets(true);

	// Wait for scan to complete (required for commandlet mode)
	if (bWaitForCompletion)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] Waiting for AssetRegistry scan to complete..."));

		const float TimeoutSeconds = 180.0f; // 3 minutes timeout (Issue #320)
		float LastLogTime = 0.0f;
		bool bTimedOut = false; // Track if timeout actually occurred (Issue #320 Code Review)

		while (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.Tick(1.0f);

			float ElapsedSeconds = (FDateTime::UtcNow() - StartTime).GetTotalSeconds();

			// Timeout check (Issue #320: Prevent infinite hang)
			if (ElapsedSeconds > TimeoutSeconds)
			{
				bTimedOut = true; // Mark as timed out (Issue #320 Code Review)
				UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] AssetRegistry initialization timeout (%.0fs)"), TimeoutSeconds);
				UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] AssetRegistry is still loading after timeout"));
				UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Proceeding with partial data - results may be incomplete"));
				break;
			}

			// Log progress every 5 seconds (Issue #320: More frequent logging)
			if (ElapsedSeconds - LastLogTime >= 5.0f)
			{
				UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] AssetRegistry scanning... (%.1fs / %.0fs timeout)"),
					ElapsedSeconds, TimeoutSeconds);
				LastLogTime = ElapsedSeconds;
			}

			// Yield CPU to prevent 100% usage
			FPlatformProcess::Sleep(0.1f);
		}

		// Issue #320 Code Review: Use bTimedOut flag instead of time comparison
		// This prevents false positive "timed out" warning when scan completes just after 180s
		float TotalSeconds = (FDateTime::UtcNow() - StartTime).GetTotalSeconds();
		if (bTimedOut)
		{
			UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[CommandletHelpers] AssetRegistry scan timed out (%.1fs)"), TotalSeconds);
		}
		else
		{
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] AssetRegistry scan complete! (%.1fs)"), TotalSeconds);
		}
	}

	return AssetRegistry;
}

bool FCUCommandletHelpers::SaveJsonToFile(const FString& OutputPath, TSharedPtr<FJsonObject> JsonObject)
{
	if (!JsonObject.IsValid())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Invalid JSON object"));
		return false;
	}

	// Ensure parent directory exists
	FString ParentDir = FPaths::GetPath(OutputPath);
	if (!EnsureOutputDirectory(ParentDir))
	{
		return false;
	}

	// Serialize JSON to string
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to serialize JSON"));
		return false;
	}

	// Write to file using an atomic temp -> rename (Issue #14142 / #108 multi-terminal safety).
	// Prevents a torn file when 2+ commandlets write the same path concurrently. The final
	// on-disk bytes are identical to a direct write, so callers' output format is unchanged.
	FString TempPath = OutputPath + TEXT(".tmp");
	if (!FFileHelper::SaveStringToFile(JsonString, *TempPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to write temp file: %s"), *TempPath);
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.Move(*OutputPath, *TempPath, /*Replace=*/true, /*EvenIfReadOnly=*/true))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Atomic rename failed, deleting temp file: %s"), *TempPath);
		FileManager.Delete(*TempPath);
		return false;
	}

	int64 FileSize = FileManager.FileSize(*OutputPath);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] JSON saved (atomic write): %s (%.2f KB)"), *OutputPath, FileSize / 1024.0f);
	return true;
}

bool FCUCommandletHelpers::SaveJsonWithCompression(const FString& OutputPath, TSharedPtr<FJsonObject> JsonObject)
{
	if (!JsonObject.IsValid())
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Invalid JSON object"));
		return false;
	}

	// Ensure parent directory exists
	FString ParentDir = FPaths::GetPath(OutputPath);
	if (!EnsureOutputDirectory(ParentDir))
	{
		return false;
	}

	// Serialize JSON to string
	FString JsonString;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&JsonString);
	if (!FJsonSerializer::Serialize(JsonObject.ToSharedRef(), JsonWriter))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to serialize JSON"));
		return false;
	}

	// Convert to UTF8 bytes
	FTCHARToUTF8 UTF8String(*JsonString);
	TArray<uint8> UncompressedData;
	UncompressedData.Append((uint8*)UTF8String.Get(), UTF8String.Length());

	// Compress with GZIP
	TArray<uint8> CompressedData;
	int32 CompressedSizeBound = FCompression::CompressMemoryBound(NAME_Gzip, UncompressedData.Num());
	CompressedData.SetNumUninitialized(CompressedSizeBound);

	int32 ActualCompressedSize = CompressedSizeBound;
	if (!FCompression::CompressMemory(
		NAME_Gzip,
		CompressedData.GetData(),
		ActualCompressedSize,
		UncompressedData.GetData(),
		UncompressedData.Num()))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] GZIP compression failed"));
		return false;
	}

	// Resize to actual compressed size
	CompressedData.SetNum(ActualCompressedSize, EAllowShrinking::No);

	// Write compressed data to file using an atomic temp -> rename (Issue #14142 / #108).
	// Prevents a torn .gz when 2+ commandlets write the same path concurrently. The final
	// on-disk bytes are identical to a direct write, so callers' output format is unchanged.
	FString CompressedPath = OutputPath + TEXT(".gz");
	FString TempCompressedPath = CompressedPath + TEXT(".tmp");

	if (!FFileHelper::SaveArrayToFile(CompressedData, *TempCompressedPath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to write temp compressed file: %s"), *TempCompressedPath);
		return false;
	}

	IFileManager& FileManager = IFileManager::Get();
	if (!FileManager.Move(*CompressedPath, *TempCompressedPath, /*Replace=*/true, /*EvenIfReadOnly=*/true))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Atomic rename failed, deleting temp file: %s"), *TempCompressedPath);
		FileManager.Delete(*TempCompressedPath);
		return false;
	}

	int64 UncompressedSize = UncompressedData.Num();
	int64 CompressedSize = CompressedData.Num();
	float CompressionRatio = (1.0f - (float)CompressedSize / UncompressedSize) * 100.0f;

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] JSON saved with GZIP (atomic write): %s"), *CompressedPath);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] Compression: %.2f KB -> %.2f KB (%.1f%% reduction)"),
		UncompressedSize / 1024.0f,
		CompressedSize / 1024.0f,
		CompressionRatio);

	return true;
}

TSharedPtr<FJsonObject> FCUCommandletHelpers::LoadJsonFromFile(const FString& FilePath)
{
	// Check if file exists
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] File not found: %s"), *FilePath);
		return nullptr;
	}

	// Check if file is GZIP compressed (.gz extension)
	bool bIsCompressed = FilePath.EndsWith(TEXT(".gz"));

	TArray<uint8> FileData;

	if (bIsCompressed)
	{
		// Load compressed file
		if (!FFileHelper::LoadFileToArray(FileData, *FilePath))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to load compressed file: %s"), *FilePath);
			return nullptr;
		}

		// ============================================
		// Security: GZIP Bomb Protection (Issue #1402)
		// Multiple layers of defense:
		//   1. Compressed file size limit (prevents large file attacks)
		//   2. Decompressed size limit (prevents memory exhaustion)
		//   3. Compression ratio limit (detects crafted GZIP bombs)
		//   4. Concurrent decompression throttle (prevents DoS)
		// ============================================

		// Layer 1: Compressed file size limit (Issue #1402 C-3)
		// Prevents memory spike from loading large compressed files
		if (FileData.Num() > SecurityLimits::MaxGZIPCompressedSize)
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Compressed file too large: %d bytes (max: %d)"),
				FileData.Num(), SecurityLimits::MaxGZIPCompressedSize);
			return nullptr;
		}

		// Decompress GZIP data
		TArray<uint8> DecompressedData;

		// Calculate uncompressed size (GZIP stores this in last 4 bytes)
		if (FileData.Num() < 4)
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Invalid GZIP file (too small): %s"), *FilePath);
			return nullptr;
		}

		// Read uncompressed size from GZIP footer (last 4 bytes, little-endian)
		// IMPORTANT: GZIP footer stores size modulo 2^32 (Issue #1402 M-3)
		// - For files >4GB, the footer wraps around (5GB stored as ~1GB)
		// - Our MaxCompressedSize limit (10MB) ensures we never hit this case
		// - Even if attacker crafts footer to claim small size, ratio check catches it
		const uint8* FooterBytes = FileData.GetData() + FileData.Num() - 4;
		uint32 UncompressedSizeU32 = FooterBytes[0] | (FooterBytes[1] << 8) | (FooterBytes[2] << 16) | (FooterBytes[3] << 24);
		int32 EstimatedSize = static_cast<int32>(UncompressedSizeU32);

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] GZIP file: %s, size=%d bytes"), *FilePath, FileData.Num());
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] GZIP footer bytes: %02X %02X %02X %02X"),
			FooterBytes[0], FooterBytes[1], FooterBytes[2], FooterBytes[3]);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] GZIP footer uncompressed size: %u → int32: %d"),
			UncompressedSizeU32, EstimatedSize);

		// Layer 2: Decompressed size limit (Issue #1402)
		if (EstimatedSize <= 0 || EstimatedSize > SecurityLimits::MaxGZIPDecompressedSize)
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] GZIP decompression rejected: size %d exceeds limit %d bytes"),
				EstimatedSize, SecurityLimits::MaxGZIPDecompressedSize);
			return nullptr;
		}

		// Layer 3: Compression ratio validation (Issue #1402)
		// Normal JSON compresses ~10:1, GZIP bombs can be 1000:1+
		float CompressionRatio = (float)EstimatedSize / FileData.Num();
		if (CompressionRatio > SecurityLimits::MaxGZIPCompressionRatio)
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Suspicious compression ratio %.1f:1 (max: %.0f:1)"),
				CompressionRatio, SecurityLimits::MaxGZIPCompressionRatio);
			return nullptr;
		}

		// Layer 4: Decompression throttle (Issue #1402 L-2)
		// Prevents DoS via many concurrent decompression requests
		{
			FScopeLock Lock(&GDecompressionLock);
			if (GActiveDecompressions >= SecurityLimits::MaxConcurrentDecompressions)
			{
				UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[CommandletHelpers] Decompression throttled (max concurrent: %d)"),
					SecurityLimits::MaxConcurrentDecompressions);
				return nullptr;
			}
			GActiveDecompressions++;
		}

		// Allocate buffer exactly as GZIP footer specifies
		// FCompression::UncompressMemory requires EXACT size, not larger!
		DecompressedData.SetNumUninitialized(EstimatedSize);

		// UncompressedSize must match GZIP footer size exactly
		int32 UncompressedSize = EstimatedSize;
		if (!FCompression::UncompressMemory(
			NAME_Gzip,
			DecompressedData.GetData(),
			UncompressedSize,
			FileData.GetData(),
			FileData.Num()))
		{
			// Cleanup: Decrement decompression counter on failure
			{
				FScopeLock Lock(&GDecompressionLock);
				GActiveDecompressions--;
			}
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] GZIP decompression failed: %s"), *FilePath);

			// Recovery (Issue #14142): delete the corrupted .gz and force a full rebuild.
			// Upstreamed from CUPCGMetadataIncrementalHelpers so every incremental loader is
			// self-healing on a corrupted cache instead of permanently failing fast.
			UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[CommandletHelpers] Deleting corrupted GZIP file, forcing full rebuild: %s"), *FilePath);
			IFileManager::Get().Delete(*FilePath, false, true);
			return nullptr;
		}

		// Cleanup: Decrement decompression counter on success (before parsing)
		{
			FScopeLock Lock(&GDecompressionLock);
			GActiveDecompressions--;
		}

		// FCompression does NOT modify UncompressedSize, so it still equals EstimatedSize
		// We need to find the actual decompressed data size
		// For JSON, we can scan backwards to find the closing brace
		int32 ActualSize = UncompressedSize;
		for (int32 i = UncompressedSize - 1; i >= 0; i--)
		{
			if (DecompressedData[i] == '}' || DecompressedData[i] == ']')
			{
				ActualSize = i + 1;
				break;
			}
		}

		DecompressedData.SetNum(ActualSize, EAllowShrinking::No);

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] GZIP decompression: footer size=%d, actual JSON size=%d"),
			UncompressedSize, ActualSize);

		// Debug: Log first and last bytes
		FString FirstBytes, LastBytes;
		for (int32 i = 0; i < FMath::Min(50, ActualSize); i++)
		{
			FirstBytes += FString::Printf(TEXT("%02X "), DecompressedData[i]);
		}
		for (int32 i = FMath::Max(0, ActualSize - 50); i < ActualSize; i++)
		{
			LastBytes += FString::Printf(TEXT("%02X "), DecompressedData[i]);
		}
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] First 50 bytes: %s"), *FirstBytes);
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] Last 50 bytes: %s"), *LastBytes);

		// Convert to string. Issue #14142: use the length-bounded FString constructor — FUTF8ToTCHAR::Get()
		// returns a buffer that is NOT null-terminated, so `JsonString = Get()` strlen-overreads past the
		// decompressed buffer into adjacent heap, appending garbage after the closing brace and making the
		// JSON unparseable for some files (non-deterministic, heap-dependent). The bounded ctor copies exactly
		// Length() chars. (Latent canonical-loader bug; amplified once all commandlet loaders route here.)
		FString JsonString;
		FUTF8ToTCHAR UTF8Converter((const ANSICHAR*)DecompressedData.GetData(), DecompressedData.Num());
		JsonString = FString(UTF8Converter.Length(), UTF8Converter.Get());

		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] JSON string length: %d chars"), JsonString.Len());

		// Parse JSON
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			int64 CompressedSize = FileData.Num();
			int64 UncompressedSizeActual = DecompressedData.Num();

			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] JSON loaded from GZIP: %s (%.2f KB -> %.2f KB)"),
				*FilePath,
				CompressedSize / 1024.0f,
				UncompressedSizeActual / 1024.0f);

			return JsonObject;
		}
		else
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to parse JSON from compressed file: %s"), *FilePath);

			// Recovery (Issue #14142): delete the corrupted .gz and force a full rebuild.
			UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[CommandletHelpers] Deleting corrupted GZIP file, forcing full rebuild: %s"), *FilePath);
			IFileManager::Get().Delete(*FilePath, false, true);
			return nullptr;
		}
	}
	else
	{
		// Load uncompressed JSON file
		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to load file: %s"), *FilePath);
			return nullptr;
		}

		// Parse JSON
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JsonString);

		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			int64 FileSize = IFileManager::Get().FileSize(*FilePath);
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] JSON loaded: %s (%.2f KB)"), *FilePath, FileSize / 1024.0f);
			return JsonObject;
		}
		else
		{
			UE_LOG(LogNarshaMCPCommandlet, Error, TEXT("[CommandletHelpers] Failed to parse JSON: %s"), *FilePath);
			return nullptr;
		}
	}
}

void FCUCommandletHelpers::ParseTasks(const FString& Params, TArray<FString>& OutTasks)
{
	OutTasks.Empty();

	// Parse command line parameters
	TArray<FString> Tokens;
	TArray<FString> Switches;
	UCommandlet::ParseCommandLine(*Params, Tokens, Switches);

	// Search for "-Tasks=" token
	for (const FString& Token : Tokens)
	{
		if (Token.StartsWith(TEXT("Tasks=")))
		{
			FString TasksString = Token.RightChop(6); // Remove "Tasks=" prefix

			// Split by comma
			TasksString.ParseIntoArray(OutTasks, TEXT(","), true);

			// Trim whitespace
			for (FString& Task : OutTasks)
			{
				Task.TrimStartAndEndInline();
			}

			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[CommandletHelpers] Parsed %d tasks: %s"), OutTasks.Num(), *TasksString);
			break;
		}
	}

	// If no tasks specified, OutTasks will be empty (caller should use default task list)
}

void FCUCommandletHelpers::LogProgress(const FString& TaskName, int32 Current, int32 Total)
{
	if (Total <= 0)
	{
		return;
	}

	float ProgressPercent = (float)Current / Total * 100.0f;

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  [NarshaMCP Batch] [%d/%d] (%.0f%%) Running: %s..."), Current, Total, ProgressPercent, *TaskName);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
}

// ========================================================================
// PROFILING HELPERS IMPLEMENTATION (v4.9.0)
// ========================================================================

// Issue #1403: Static profiling data storage with thread-safe access
FCriticalSection FCUCommandletHelpers::GProfileLock;
TMap<FString, FDateTime> FCUCommandletHelpers::ProfileTimers;
TMap<FString, double> FCUCommandletHelpers::ProfileResults;

void FCUCommandletHelpers::BeginProfileSection(const FString& SectionName)
{
	FScopeLock Lock(&GProfileLock);
	ProfileTimers.Add(SectionName, FDateTime::UtcNow());
}

double FCUCommandletHelpers::EndProfileSection(const FString& SectionName)
{
	FScopeLock Lock(&GProfileLock);

	FDateTime* StartTime = ProfileTimers.Find(SectionName);
	if (!StartTime)
	{
		UE_LOG(LogNarshaMCPCommandlet, Warning, TEXT("[CommandletHelpers] EndProfileSection called for unknown section: %s"), *SectionName);
		return 0.0;
	}

	// Calculate elapsed time in seconds
	double ElapsedSeconds = (FDateTime::UtcNow() - *StartTime).GetTotalSeconds();

	// Store result (Issue #199: Accumulate instead of replace for repeated sections)
	double* ExistingTime = ProfileResults.Find(SectionName);
	if (ExistingTime)
	{
		*ExistingTime += ElapsedSeconds;  // Accumulate time for repeated sections
	}
	else
	{
		ProfileResults.Add(SectionName, ElapsedSeconds);  // First time
	}

	// Remove from active timers
	ProfileTimers.Remove(SectionName);

	return ElapsedSeconds;
}

void FCUCommandletHelpers::LogProfileResults()
{
	FScopeLock Lock(&GProfileLock);

	if (ProfileResults.Num() == 0)
	{
		UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("[Profile] No profiling data available"));
		return;
	}

	// Calculate total time
	double TotalTime = 0.0;
	for (const auto& Pair : ProfileResults)
	{
		TotalTime += Pair.Value;
	}

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  📊 Performance Profile"));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  Total Time: %.1fs"), TotalTime);
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));

	// Sort by time (descending)
	TArray<TPair<FString, double>> SortedResults;
	for (const auto& Pair : ProfileResults)
	{
		SortedResults.Add(Pair);
	}
	SortedResults.Sort([](const TPair<FString, double>& A, const TPair<FString, double>& B)
	{
		return A.Value > B.Value; // Descending order
	});

	// Log each section
	for (const auto& Pair : SortedResults)
	{
		double TimeSeconds = Pair.Value;
		double Percentage = (TimeSeconds / TotalTime) * 100.0;
		bool bIsBottleneck = Percentage > 30.0; // Bottleneck if >30% of total time

		if (bIsBottleneck)
		{
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  %-40s: %6.1fs (%5.1f%%) ⚠️ BOTTLENECK"),
				*Pair.Key, TimeSeconds, Percentage);
		}
		else
		{
			UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("  %-40s: %6.1fs (%5.1f%%)"),
				*Pair.Key, TimeSeconds, Percentage);
		}
	}

	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT("============================================================="));
	UE_LOG(LogNarshaMCPCommandlet, Display, TEXT(""));
}

TSharedPtr<FJsonObject> FCUCommandletHelpers::GetProfileResultsAsJson()
{
	FScopeLock Lock(&GProfileLock);

	TSharedPtr<FJsonObject> RootObject = MakeShareable(new FJsonObject);

	// Timestamp
	RootObject->SetStringField(TEXT("timestamp"), FDateTime::Now().ToString());

	// Calculate total time
	double TotalTime = 0.0;
	for (const auto& Pair : ProfileResults)
	{
		TotalTime += Pair.Value;
	}
	RootObject->SetNumberField(TEXT("total_time_sec"), TotalTime);

	// Sections
	TSharedPtr<FJsonObject> SectionsObject = MakeShareable(new FJsonObject);

	for (const auto& Pair : ProfileResults)
	{
		TSharedPtr<FJsonObject> SectionObject = MakeShareable(new FJsonObject);

		double TimeSeconds = Pair.Value;
		double Percentage = (TotalTime > 0.0) ? (TimeSeconds / TotalTime) * 100.0 : 0.0;
		bool bIsBottleneck = Percentage > 30.0;

		SectionObject->SetNumberField(TEXT("time_sec"), TimeSeconds);
		SectionObject->SetNumberField(TEXT("percentage"), Percentage);
		SectionObject->SetBoolField(TEXT("is_bottleneck"), bIsBottleneck);

		SectionsObject->SetObjectField(Pair.Key, SectionObject);
	}

	RootObject->SetObjectField(TEXT("sections"), SectionsObject);

	return RootObject;
}

void FCUCommandletHelpers::ClearProfileData()
{
	FScopeLock Lock(&GProfileLock);
	ProfileTimers.Empty();
	ProfileResults.Empty();
}
