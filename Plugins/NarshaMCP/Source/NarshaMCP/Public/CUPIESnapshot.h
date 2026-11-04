// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8783: PIE runtime snapshot data model + structural diff.

#pragma once

#include "CoreMinimal.h"
#include "Math/Rotator.h"
#include "Math/Vector.h"

class UWorld;

/** Snapshot scope tier. Controls per-actor field coverage and which actors are enumerated. */
enum class ECUSnapshotScope : uint8
{
	Minimal  = 0,  // players + actors with tag "snapshot.include" — path/class/location only
	Standard = 1,  // pawns + ACharacter + APawn + tagged + filter_class — adds rotation/tags
	Full     = 2,  // all actors — adds velocity/scale/component names/reflected scalars
};

/** Returns "minimal" / "standard" / "full". */
FString CUSnapshotScopeToString(ECUSnapshotScope Scope);

/**
 * Parse scope string. Valid: "minimal" / "standard" / "full" (case-insensitive).
 * @param bOutValid  true on known scope, false on unknown (returns Standard by default).
 */
ECUSnapshotScope CUParseSnapshotScope(const FString& ScopeString, bool& bOutValid);

/** Per-actor snapshot entry. Which `bHas*` flags are set depends on Scope. */
struct FCUPIESnapshotActor
{
	FString Path;                       // Actor full path (stable, used as diff key)
	FString ClassName;                  // Actor->GetClass()->GetName()
	FVector Location = FVector::ZeroVector;

	// Standard+ fields
	FRotator Rotation = FRotator::ZeroRotator;
	TArray<FString> Tags;
	bool bHasRotation = false;

	// Full-only fields
	FVector Scale = FVector::OneVector;
	FVector Velocity = FVector::ZeroVector;
	TArray<FString> ComponentNames;
	TMap<FString, double> ReflectedScalars;  // e.g. "Health" → 85.0
	bool bHasScale = false;
	bool bHasVelocity = false;
	bool bHasComponents = false;
	bool bHasReflection = false;
};

/** Per-player snapshot entry (PlayerController + Pawn). */
struct FCUPIESnapshotPlayer
{
	int32 Index = 0;                   // Player index in GameInstance
	FString ControllerPath;            // PlayerController path (empty if missing)
	FString PawnPath;                  // Possessed pawn path (empty if missing)
	FString PawnClassName;
	FVector PawnLocation = FVector::ZeroVector;
	FRotator PawnRotation = FRotator::ZeroRotator;
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	bool bHasPawn = false;
	bool bHasView = false;
};

/** Frame snapshot captured from a running PIE session. */
struct FCUPIESnapshot
{
	FString SnapshotId;                         // "snap_<timeSeconds_ms>_<seq>"
	ECUSnapshotScope Scope = ECUSnapshotScope::Standard;
	FString LevelName;                          // Persistent level name
	double TimeSeconds = 0.0;                   // World->GetTimeSeconds()
	int64 FrameNumber = 0;                      // GFrameCounter at capture
	TArray<FCUPIESnapshotActor> Actors;         // Sorted by Path (deterministic diff)
	TArray<FCUPIESnapshotPlayer> Players;       // Sorted by Index
	int32 TotalActorCount = 0;                  // Pre-cap count (for truncation detection)
	int32 MaxActorsCap = 2000;                  // Cap used during capture
	bool bActorsTruncated = false;              // true if TotalActorCount > Actors.Num()
	FString FilterClass;                        // Filter applied (empty = none)

	/** True if this snapshot holds no actors (e.g. capture before world tick). */
	bool IsEmpty() const { return Actors.Num() == 0 && Players.Num() == 0; }

	/** Serialize deterministically (sorted keys). Returns JSON object string. */
	FString ToJsonString() const;
};

/** Single field change between two snapshots. Values are pre-serialized JSON fragments. */
struct FCUPIEFieldChange
{
	FString Field;   // "location" | "rotation" | "velocity" | "tags" | ...
	FString From;    // JSON fragment (e.g. "[1,2,3]" for vector, "true" for bool)
	FString To;      // JSON fragment
};

/** Per-actor diff entry: same Path, non-empty ChangedFields. */
struct FCUPIESnapshotActorDiff
{
	FString Path;
	FString ClassName;
	TArray<FCUPIEFieldChange> ChangedFields;
};

/** Per-player diff entry. */
struct FCUPIESnapshotPlayerDiff
{
	int32 Index = 0;
	TArray<FCUPIEFieldChange> ChangedFields;
};

/** Full A→B structural diff. */
struct FCUPIESnapshotDiff
{
	TArray<FCUPIESnapshotActor> Added;            // Paths in B only
	TArray<FCUPIESnapshotActor> Removed;          // Paths in A only
	TArray<FCUPIESnapshotActorDiff> Modified;     // Path in both, fields differ
	TArray<FCUPIESnapshotPlayerDiff> PlayersDiff; // Players with any field change

	// game_diff
	bool bLevelChanged = false;
	double TimeDelta = 0.0;   // B.TimeSeconds - A.TimeSeconds
	int64 FrameDelta = 0;     // B.FrameNumber - A.FrameNumber

	// summary
	int32 UnchangedCount = 0;

	/** Serialize to {"added":[], "removed":[], "modified":[], "players_diff":[], "game_diff":{}, "summary":{}}. */
	FString ToJsonString() const;
};

/** Tolerance + field-filter options for diff computation. */
struct FCUPIESnapshotDiffOptions
{
	double VectorEpsilon = 1e-3;   // per-component float compare for FVector / FRotator
	double ScalarEpsilon = 1e-6;   // per-component float compare for reflected scalars
	TSet<FString> IgnoreFields;    // e.g. {"velocity"} to suppress physics jitter
};

/**
 * Capture a snapshot from a PIE world.
 * All methods are static and side-effect-free (no mutation of the passed world).
 */
struct FCUPIESnapshotCapture
{
	/**
	 * Walk the PIE world and build a snapshot.
	 *
	 * @param PlayWorld    GEditor->PlayWorld — caller MUST null-check before calling.
	 * @param Scope        Minimal / Standard / Full.
	 * @param FilterClass  Optional class-name filter (exact match against
	 *                     Actor->GetClass()->GetName()). Ignored if empty.
	 * @param MaxActors    Hard cap on actor count (default 2000, clamp <=10000).
	 *                     Exceeded → sets bActorsTruncated = true.
	 * @return Populated snapshot. Caller must assign SnapshotId after capture.
	 */
	static FCUPIESnapshot Capture(
		UWorld* PlayWorld,
		ECUSnapshotScope Scope,
		const FString& FilterClass = FString(),
		int32 MaxActors = 2000);
};

/**
 * Compute structural diff between two snapshots.
 * Independent of capture — can be called with externally reconstructed FCUPIESnapshot.
 */
struct FCUPIESnapshotCompare
{
	/**
	 * A → B diff. Both snapshots must have Actors sorted by Path (guaranteed by Capture).
	 */
	static FCUPIESnapshotDiff Diff(
		const FCUPIESnapshot& A,
		const FCUPIESnapshot& B,
		const FCUPIESnapshotDiffOptions& Options = FCUPIESnapshotDiffOptions());
};
