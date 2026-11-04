// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #5647: AI Annotation Data — structs and cache for editor AI annotations
// Issue #5816: M2 additions — ECUAnnotationSeverity enum, SeverityFromCategory()
// Issue #5820: M6 — LRU cache eviction, LastAccessedAt, ToJson/FromJson persistence
// Issue #5822: M8 — ECUAnnotationSeverity extracted to CUAnnotationTypes.h (public UENUM)

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "CUAnnotationTypes.h"

/**
 * Data for a single AI annotation overlay on an editor element (Issue #5647).
 * Memory layout optimized: strings first, primitives last, cache-line friendly.
 *
 * AnnotationId format: "{AssetPath}::{TargetIdentifier}"
 * e.g. "/Game/BP_Player::NodeGuid_ABC123"
 *
 * Sequencer TargetIdentifier conventions (Issue #5818):
 *   Track-level:    "Track::{TrackDisplayName}"
 *   Keyframe-level: "Keyframe::{TrackName}::{FrameNumber}"
 */
struct alignas(8) FCUAnnotationData
{
	// ---------------------------------------------------------------
	// Annotation ID format constants (single source of truth)
	// ---------------------------------------------------------------

	/** Separator between AssetPath and TargetIdentifier in annotation IDs (array form for compile-time length) */
	static constexpr TCHAR IdSeparator[] = TEXT("::");

	/** Prefix for Sequencer track-level annotation targets (Issue #5818) */
	static constexpr const TCHAR* TrackTargetPrefix = TEXT("Track::");

	/** Prefix for Sequencer keyframe-level annotation targets (Issue #5818) */
	static constexpr const TCHAR* KeyframeTargetPrefix = TEXT("Keyframe::");

	/** UE content root prefix used to identify Blueprint assets */
	static constexpr const TCHAR* GamePathPrefix = TEXT("/Game/");

	/** Placeholder text shown while the AI analysis is in progress */
	static constexpr const TCHAR* AnalyzingPlaceholderText = TEXT("Analyzing node...");

	/** Placeholder text for Material analysis */
	static constexpr const TCHAR* AnalyzingMaterialText = TEXT("Analyzing material...");

	/** Placeholder text prefix for track analysis */
	static constexpr const TCHAR* AnalyzingTrackPrefix = TEXT("Analyzing track: ");

	/** Default category applied when none is specified */
	static constexpr const TCHAR* DefaultCategory = TEXT("analysis");

	/** URL scheme prefix for chat → annotation cross-linking (Issue #5821) */
	static constexpr const TCHAR* AnnotateLinkPrefix = TEXT("ue-annotate://");

	// ---------------------------------------------------------------
	// Fields
	// ---------------------------------------------------------------

	/** Unique ID: "asset_path::target_identifier" */
	FString AnnotationId;

	/** Source asset path (e.g. /Game/BP_Player) */
	FString SourceAssetPath;

	/** Target within the asset: node GUID, parameter name, track name, etc. */
	FString TargetIdentifier;

	/** AI-generated annotation text */
	FString AnnotationText;

	/** Category: "analysis" | "suggestion" | "warning" | "error" */
	FString Category;

	/** Typed severity for color/icon dispatch (Issue #5816) */
	ECUAnnotationSeverity Severity = ECUAnnotationSeverity::Info;

	/** When this annotation was created (for TTL expiration) */
	FDateTime CreatedAt;

	/** When this annotation was last accessed (for LRU eviction, Issue #5820) */
	FDateTime LastAccessedAt;

	/** Optional metadata from the AI response */
	TSharedPtr<FJsonObject> Metadata;

	/** Chat message ID that originated this annotation (empty if not from chat, Issue #5821) */
	FString OriginatingChatMessageId;

	FCUAnnotationData()
		: CreatedAt(FDateTime::Now())
		, LastAccessedAt(FDateTime::Now())
	{
	}

	/** Serialize to JSON object for cache persistence (Issue #5820) */
	TSharedPtr<FJsonObject> ToJson() const
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("annotation_id"), AnnotationId);
		Obj->SetStringField(TEXT("source_asset_path"), SourceAssetPath);
		Obj->SetStringField(TEXT("target_identifier"), TargetIdentifier);
		Obj->SetStringField(TEXT("annotation_text"), AnnotationText);
		Obj->SetStringField(TEXT("category"), Category);
		Obj->SetStringField(TEXT("created_at"), CreatedAt.ToIso8601());
		if (Metadata.IsValid())
		{
			Obj->SetObjectField(TEXT("metadata"), Metadata);
		}
		if (!OriginatingChatMessageId.IsEmpty())
		{
			Obj->SetStringField(TEXT("chat_message_id"), OriginatingChatMessageId);
		}
		return Obj;
	}

	/** Deserialize from JSON object (Issue #5820) */
	static FCUAnnotationData FromJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FCUAnnotationData Data;
		Data.AnnotationId = Obj->GetStringField(TEXT("annotation_id"));
		Data.SourceAssetPath = Obj->GetStringField(TEXT("source_asset_path"));
		Data.TargetIdentifier = Obj->GetStringField(TEXT("target_identifier"));
		Data.AnnotationText = Obj->GetStringField(TEXT("annotation_text"));
		Data.Category = Obj->GetStringField(TEXT("category"));
		Data.Severity = SeverityFromCategory(Data.Category);
		FDateTime::ParseIso8601(*Obj->GetStringField(TEXT("created_at")), Data.CreatedAt);
		Data.LastAccessedAt = FDateTime::Now();
		if (Obj->HasField(TEXT("metadata")))
		{
			Data.Metadata = Obj->GetObjectField(TEXT("metadata"));
		}
		if (Obj->HasField(TEXT("chat_message_id")))
		{
			Data.OriginatingChatMessageId = Obj->GetStringField(TEXT("chat_message_id"));
		}
		return Data;
	}

	/** Generate a consistent annotation ID from asset path and target */
	static FString MakeId(const FString& AssetPath, const FString& Target)
	{
		return AssetPath + IdSeparator + Target;
	}

	/** Length of IdSeparator for ParseId substring operations. Compile-time derived from IdSeparator array. */
	static constexpr int32 IdSeparatorLen = UE_ARRAY_COUNT(IdSeparator) - 1;

	/**
	 * Parse the asset path and target identifier from an annotation ID.
	 * Returns false if the ID does not contain the separator.
	 */
	static bool ParseId(const FString& AnnotationId, FString& OutAssetPath, FString& OutTarget)
	{
		const int32 SepIdx = AnnotationId.Find(IdSeparator);
		if (SepIdx == INDEX_NONE)
		{
			return false;
		}
		OutAssetPath = AnnotationId.Left(SepIdx);
		OutTarget    = AnnotationId.Mid(SepIdx + IdSeparatorLen);
		return true;
	}

	/**
	 * Convert category string to typed severity enum (Issue #5816).
	 * @param InCategory Category string to convert
	 * @return Mapped severity - case sensitive matching, empty/unknown returns Info
	 *         Examples: "error" → Error, "warning" → Warning, "suggestion" → Suggestion, "" → Info
	 * @note Case-sensitive comparison used to avoid FString::Equals() overhead in high-frequency calls
	 */
	static ECUAnnotationSeverity SeverityFromCategory(const FString& InCategory)
	{
		// Use compile-time string constants for optimal performance
		static constexpr const TCHAR* SuggestionCategory = TEXT("suggestion");
		static constexpr const TCHAR* WarningCategory = TEXT("warning");
		static constexpr const TCHAR* ErrorCategory = TEXT("error");

		if (InCategory == SuggestionCategory)
		{
			return ECUAnnotationSeverity::Suggestion;
		}
		if (InCategory == WarningCategory)
		{
			return ECUAnnotationSeverity::Warning;
		}
		if (InCategory == ErrorCategory)
		{
			return ECUAnnotationSeverity::Error;
		}
		// Default case: "analysis" category or unknown/empty strings → Info severity
		return ECUAnnotationSeverity::Info;
	}

	/**
	 * Check if target has Material annotation prefix (Issue #5817 M3).
	 * Performance: O(1) prefix check using FString::StartsWith(), no allocation.
	 * @param TargetIdentifier Target identifier to test (e.g. "MatParam_BaseColor", "MatExpr_15")
	 * @return true if starts with "MatParam_" or "MatExpr_", false otherwise
	 */
	static bool HasMaterialPrefix(const FString& TargetIdentifier)
	{
		return TargetIdentifier.StartsWith(TEXT("MatParam_")) || TargetIdentifier.StartsWith(TEXT("MatExpr_"));
	}

	// Note: ParseId implementation is located at line 166, grouped with MakeId for logical cohesion.
};

/**
 * Thread-safe LRU cache for AI annotations with TTL expiration (Issue #5647, #5820).
 *
 * Uses FCriticalSection for thread safety (matching AIChatPanelSubsystem pattern).
 * Default TTL: 300 seconds (5 minutes).
 * LRU eviction: When entries exceed MaxEntries, least-recently-accessed entries are evicted.
 * AccessOrder[0] = LRU, AccessOrder.Last() = MRU.
 */
class FCUAnnotationCache
{
public:
	/** Default max entries for LRU eviction (Issue #5820) */
	static constexpr int32 DefaultMaxEntries = 128;

	explicit FCUAnnotationCache(int32 InMaxEntries = DefaultMaxEntries)
		: MaxEntries(FMath::Max(1, InMaxEntries))
	{
	}

	/** Add or update an annotation (promotes to MRU position) */
	void Add(const FCUAnnotationData& Data)
	{
		FScopeLock Lock(&CacheLock);
		AccessOrder.Remove(Data.AnnotationId);
		FCUAnnotationData Entry = Data;
		Entry.LastAccessedAt = FDateTime::Now();
		Entries.Add(Data.AnnotationId, MoveTemp(Entry));
		AccessOrder.Add(Data.AnnotationId);
		EvictIfNeeded();
	}

	/** Find annotation by ID, promotes to MRU (returns nullptr if not found) */
	const FCUAnnotationData* Find(const FString& AnnotationId) const
	{
		FScopeLock Lock(&CacheLock);
		const FCUAnnotationData* Result = Entries.Find(AnnotationId);
		if (Result)
		{
			const_cast<FCUAnnotationCache*>(this)->PromoteToMRU(AnnotationId);
		}
		return Result;
	}

	/** Find without promoting to MRU (for read-only/test access, Issue #5820) */
	const FCUAnnotationData* Peek(const FString& AnnotationId) const
	{
		FScopeLock Lock(&CacheLock);
		return Entries.Find(AnnotationId);
	}

	/** Remove a specific annotation */
	void Remove(const FString& AnnotationId)
	{
		FScopeLock Lock(&CacheLock);
		Entries.Remove(AnnotationId);
		AccessOrder.Remove(AnnotationId);
	}

	/** Remove all annotations older than TTLSeconds */
	void PurgeExpired(double TTLSeconds = 300.0)
	{
		FScopeLock Lock(&CacheLock);
		const FDateTime Now = FDateTime::Now();
		TArray<FString> Expired;
		for (const auto& Pair : Entries)
		{
			if ((Now - Pair.Value.CreatedAt).GetTotalSeconds() > TTLSeconds)
			{
				Expired.Add(Pair.Key);
			}
		}
		for (const FString& Key : Expired)
		{
			Entries.Remove(Key);
			AccessOrder.Remove(Key);
		}
	}

	/** Remove all annotations */
	void Clear()
	{
		FScopeLock Lock(&CacheLock);
		Entries.Empty();
		AccessOrder.Empty();
	}

	/** Number of cached annotations */
	int32 Num() const
	{
		FScopeLock Lock(&CacheLock);
		return Entries.Num();
	}

	/** Get all annotation IDs (for iteration) */
	TArray<FString> GetAllIds() const
	{
		FScopeLock Lock(&CacheLock);
		TArray<FString> Ids;
		Entries.GetKeys(Ids);
		return MoveTemp(Ids);
	}

	/** Get max entries limit (Issue #5820) */
	int32 GetMaxEntries() const { return MaxEntries; }

	/** Set max entries limit, triggers eviction if over capacity (Issue #5820) */
	void SetMaxEntries(int32 InMaxEntries)
	{
		FScopeLock Lock(&CacheLock);
		MaxEntries = FMath::Max(1, InMaxEntries);
		EvictIfNeeded();
	}

	// --- Persistence (Issue #5820) ---

	/** Serialize all non-placeholder entries to JSON array string */
	FString SerializeToJson() const;

	/** Deserialize from JSON array string, replacing current entries */
	bool DeserializeFromJson(const FString& JsonString);

	/** Save to file at given path */
	bool SaveToFile(const FString& FilePath) const;

	/** Load from file at given path */
	bool LoadFromFile(const FString& FilePath);

private:
	TMap<FString, FCUAnnotationData> Entries;
	TArray<FString> AccessOrder;   // Index 0 = LRU, last = MRU
	int32 MaxEntries;
	mutable FCriticalSection CacheLock;

	/** Promote an entry to MRU position (Issue #5820) */
	void PromoteToMRU(const FString& AnnotationId)
	{
		AccessOrder.Remove(AnnotationId);
		AccessOrder.Add(AnnotationId);
		if (FCUAnnotationData* Entry = Entries.Find(AnnotationId))
		{
			Entry->LastAccessedAt = FDateTime::Now();
		}
	}

	/** Evict LRU entries until at or below MaxEntries (Issue #5820) */
	void EvictIfNeeded()
	{
		while (Entries.Num() > MaxEntries && AccessOrder.Num() > 0)
		{
			const FString LruId = AccessOrder[0];
			AccessOrder.RemoveAt(0);
			Entries.Remove(LruId);
		}
	}
};
