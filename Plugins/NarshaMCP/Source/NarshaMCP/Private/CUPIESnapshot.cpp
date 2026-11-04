// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #8783: PIE runtime snapshot data model + structural diff.

#include "CUPIESnapshot.h"

#include "CUJsonUtils.h"

#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Engine/EngineTypes.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "UObject/UnrealType.h"

//--------------------------------------------------------------------
// Scope parsing helpers
//--------------------------------------------------------------------

FString CUSnapshotScopeToString(ECUSnapshotScope Scope)
{
	switch (Scope)
	{
		case ECUSnapshotScope::Minimal:  return TEXT("minimal");
		case ECUSnapshotScope::Standard: return TEXT("standard");
		case ECUSnapshotScope::Full:     return TEXT("full");
		default:                         return TEXT("standard");
	}
}

ECUSnapshotScope CUParseSnapshotScope(const FString& ScopeString, bool& bOutValid)
{
	bOutValid = true;
	if (ScopeString.Equals(TEXT("minimal"),  ESearchCase::IgnoreCase)) return ECUSnapshotScope::Minimal;
	if (ScopeString.Equals(TEXT("standard"), ESearchCase::IgnoreCase)) return ECUSnapshotScope::Standard;
	if (ScopeString.Equals(TEXT("full"),     ESearchCase::IgnoreCase)) return ECUSnapshotScope::Full;

	bOutValid = false;
	return ECUSnapshotScope::Standard;
}

//--------------------------------------------------------------------
// JSON fragment helpers (FString-based; avoids FJsonValueString wrapping)
//--------------------------------------------------------------------

namespace
{
	// Max reflected scalar fields per actor (cap blowup on heavy BPs).
	constexpr int32 kReflectedScalarCapPerActor = 16;

	// Max component names serialized per actor.
	constexpr int32 kComponentNameCapPerActor = 32;

	FString NumToJson(double Value)
	{
		// Deterministic formatting: avoid locale, limit precision for stability.
		// %g drops trailing zeros; %.9g matches Python json for most floats.
		if (FMath::IsFinite(Value))
		{
			return FString::Printf(TEXT("%.9g"), Value);
		}
		// JSON has no NaN/Inf — clamp to null.
		return TEXT("null");
	}

	FString IntToJson(int64 Value)
	{
		return FString::Printf(TEXT("%lld"), Value);
	}

	FString BoolToJson(bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	FString StringToJson(const FString& Value)
	{
		return FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Value));
	}

	FString VectorToJson(const FVector& V)
	{
		return FString::Printf(TEXT("[%s,%s,%s]"), *NumToJson(V.X), *NumToJson(V.Y), *NumToJson(V.Z));
	}

	FString RotatorToJson(const FRotator& R)
	{
		return FString::Printf(TEXT("[%s,%s,%s]"), *NumToJson(R.Pitch), *NumToJson(R.Yaw), *NumToJson(R.Roll));
	}

	FString StringArrayToJson(const TArray<FString>& Strings)
	{
		if (Strings.Num() == 0) return TEXT("[]");

		FString Out = TEXT("[");
		for (int32 i = 0; i < Strings.Num(); ++i)
		{
			if (i > 0) Out += TEXT(",");
			Out += StringToJson(Strings[i]);
		}
		Out += TEXT("]");
		return Out;
	}

	// Determinism: callers sort before serializing.
	FString ScalarMapToJson(const TMap<FString, double>& Map)
	{
		if (Map.Num() == 0) return TEXT("{}");

		TArray<FString> SortedKeys;
		Map.GenerateKeyArray(SortedKeys);
		SortedKeys.Sort();

		FString Out = TEXT("{");
		for (int32 i = 0; i < SortedKeys.Num(); ++i)
		{
			if (i > 0) Out += TEXT(",");
			Out += StringToJson(SortedKeys[i]);
			Out += TEXT(":");
			Out += NumToJson(Map[SortedKeys[i]]);
		}
		Out += TEXT("}");
		return Out;
	}

	bool VectorsApproxEqual(const FVector& A, const FVector& B, double Eps)
	{
		return FMath::Abs(A.X - B.X) <= Eps
			&& FMath::Abs(A.Y - B.Y) <= Eps
			&& FMath::Abs(A.Z - B.Z) <= Eps;
	}

	bool RotatorsApproxEqual(const FRotator& A, const FRotator& B, double Eps)
	{
		return FMath::Abs(A.Pitch - B.Pitch) <= Eps
			&& FMath::Abs(A.Yaw   - B.Yaw)   <= Eps
			&& FMath::Abs(A.Roll  - B.Roll)  <= Eps;
	}

	bool StringArraysEqual(const TArray<FString>& A, const TArray<FString>& B)
	{
		if (A.Num() != B.Num()) return false;
		TArray<FString> SortedA = A; SortedA.Sort();
		TArray<FString> SortedB = B; SortedB.Sort();
		for (int32 i = 0; i < SortedA.Num(); ++i)
		{
			if (!SortedA[i].Equals(SortedB[i])) return false;
		}
		return true;
	}

	FString SerializeActor(const FCUPIESnapshotActor& Actor, ECUSnapshotScope Scope)
	{
		// Field order is fixed for byte-stable diffs across processes.
		FString Out = TEXT("{");
		Out += TEXT("\"path\":");     Out += StringToJson(Actor.Path);
		Out += TEXT(",\"class\":");   Out += StringToJson(Actor.ClassName);
		Out += TEXT(",\"location\":"); Out += VectorToJson(Actor.Location);

		if (Scope != ECUSnapshotScope::Minimal && Actor.bHasRotation)
		{
			Out += TEXT(",\"rotation\":"); Out += RotatorToJson(Actor.Rotation);
		}
		if (Scope != ECUSnapshotScope::Minimal && Actor.Tags.Num() > 0)
		{
			Out += TEXT(",\"tags\":"); Out += StringArrayToJson(Actor.Tags);
		}
		if (Scope == ECUSnapshotScope::Full)
		{
			if (Actor.bHasScale)
			{
				Out += TEXT(",\"scale\":"); Out += VectorToJson(Actor.Scale);
			}
			if (Actor.bHasVelocity)
			{
				Out += TEXT(",\"velocity\":"); Out += VectorToJson(Actor.Velocity);
			}
			if (Actor.bHasComponents)
			{
				Out += TEXT(",\"components\":"); Out += StringArrayToJson(Actor.ComponentNames);
			}
			if (Actor.bHasReflection && Actor.ReflectedScalars.Num() > 0)
			{
				Out += TEXT(",\"scalars\":"); Out += ScalarMapToJson(Actor.ReflectedScalars);
			}
		}
		Out += TEXT("}");
		return Out;
	}

	FString SerializePlayer(const FCUPIESnapshotPlayer& Player)
	{
		FString Out = TEXT("{");
		Out += TEXT("\"index\":");             Out += IntToJson(Player.Index);
		Out += TEXT(",\"controller_path\":");  Out += StringToJson(Player.ControllerPath);
		Out += TEXT(",\"pawn_path\":");        Out += StringToJson(Player.PawnPath);
		Out += TEXT(",\"pawn_class\":");       Out += StringToJson(Player.PawnClassName);
		if (Player.bHasPawn)
		{
			Out += TEXT(",\"pawn_location\":"); Out += VectorToJson(Player.PawnLocation);
			Out += TEXT(",\"pawn_rotation\":"); Out += RotatorToJson(Player.PawnRotation);
		}
		if (Player.bHasView)
		{
			Out += TEXT(",\"view_location\":"); Out += VectorToJson(Player.ViewLocation);
			Out += TEXT(",\"view_rotation\":"); Out += RotatorToJson(Player.ViewRotation);
		}
		Out += TEXT("}");
		return Out;
	}

	FString SerializeFieldChange(const FCUPIEFieldChange& Change)
	{
		// Change.From / Change.To are already JSON fragments.
		FString Out = TEXT("{");
		Out += TEXT("\"field\":"); Out += StringToJson(Change.Field);
		Out += TEXT(",\"from\":");  Out += (Change.From.IsEmpty()  ? TEXT("null") : Change.From);
		Out += TEXT(",\"to\":");    Out += (Change.To.IsEmpty()    ? TEXT("null") : Change.To);
		Out += TEXT("}");
		return Out;
	}

	FString SerializeActorDiff(const FCUPIESnapshotActorDiff& Entry)
	{
		FString Out = TEXT("{");
		Out += TEXT("\"path\":");  Out += StringToJson(Entry.Path);
		Out += TEXT(",\"class\":"); Out += StringToJson(Entry.ClassName);
		Out += TEXT(",\"changed_fields\":[");
		for (int32 i = 0; i < Entry.ChangedFields.Num(); ++i)
		{
			if (i > 0) Out += TEXT(",");
			Out += SerializeFieldChange(Entry.ChangedFields[i]);
		}
		Out += TEXT("]}");
		return Out;
	}

	FString SerializePlayerDiff(const FCUPIESnapshotPlayerDiff& Entry)
	{
		FString Out = TEXT("{");
		Out += TEXT("\"index\":"); Out += IntToJson(Entry.Index);
		Out += TEXT(",\"changed_fields\":[");
		for (int32 i = 0; i < Entry.ChangedFields.Num(); ++i)
		{
			if (i > 0) Out += TEXT(",");
			Out += SerializeFieldChange(Entry.ChangedFields[i]);
		}
		Out += TEXT("]}");
		return Out;
	}

	//----------------------------------------------------------------
	// Actor capture helpers
	//----------------------------------------------------------------

	bool ActorMatchesScope(AActor* Actor, ECUSnapshotScope Scope, const FString& FilterClass)
	{
		if (!IsValid(Actor) || !Actor->GetClass()) return false;

		// Full scope: all actors.
		if (Scope == ECUSnapshotScope::Full) return true;

		const bool bIsPawn = Actor->IsA<APawn>();
		const bool bIsController = Actor->IsA<APlayerController>();
		const bool bHasIncludeTag = Actor->ActorHasTag(FName(TEXT("snapshot.include")));

		if (Scope == ECUSnapshotScope::Minimal)
		{
			// Minimal: players (pawns/controllers) + explicit tag.
			return bIsPawn || bIsController || bHasIncludeTag;
		}

		// Standard: pawns + tagged + filter_class match.
		if (bIsPawn || bIsController || bHasIncludeTag) return true;
		if (!FilterClass.IsEmpty())
		{
			return Actor->GetClass()->GetName().Equals(FilterClass, ESearchCase::IgnoreCase);
		}
		return false;
	}

	void ExtractReflectedScalars(AActor* Actor, TMap<FString, double>& OutScalars)
	{
		if (!IsValid(Actor)) return;
		UClass* Class = Actor->GetClass();
		if (!Class) return;

		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Class); It && Count < kReflectedScalarCapPerActor; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;

			// TFieldIterator already limits us to reflected UPROPERTY fields. We further
			// restrict to primitive numeric/bool types below for speed and JSON-stability;
			// strings/structs/arrays are intentionally skipped from the snapshot.
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Actor);
			if (!ValuePtr) continue;

			if (const FFloatProperty* FP = CastField<FFloatProperty>(Prop))
			{
				OutScalars.Add(Prop->GetName(), (double)FP->GetPropertyValue(ValuePtr));
				++Count;
			}
			else if (const FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
			{
				OutScalars.Add(Prop->GetName(), DP->GetPropertyValue(ValuePtr));
				++Count;
			}
			else if (const FIntProperty* IP = CastField<FIntProperty>(Prop))
			{
				OutScalars.Add(Prop->GetName(), (double)IP->GetPropertyValue(ValuePtr));
				++Count;
			}
			else if (const FBoolProperty* BP = CastField<FBoolProperty>(Prop))
			{
				OutScalars.Add(Prop->GetName(), BP->GetPropertyValue(ValuePtr) ? 1.0 : 0.0);
				++Count;
			}
		}
	}

	void ExtractComponentNames(AActor* Actor, TArray<FString>& OutNames)
	{
		if (!IsValid(Actor)) return;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		OutNames.Reserve(FMath::Min(Components.Num(), kComponentNameCapPerActor));
		for (UActorComponent* Comp : Components)
		{
			if (OutNames.Num() >= kComponentNameCapPerActor) break;
			if (!IsValid(Comp)) continue;
			OutNames.Add(Comp->GetName());
		}
		OutNames.Sort();
	}

	FCUPIESnapshotActor BuildActorEntry(AActor* Actor, ECUSnapshotScope Scope)
	{
		FCUPIESnapshotActor Entry;
		Entry.Path      = Actor->GetPathName();
		Entry.ClassName = Actor->GetClass()->GetName();
		Entry.Location  = Actor->GetActorLocation();

		if (Scope != ECUSnapshotScope::Minimal)
		{
			Entry.Rotation = Actor->GetActorRotation();
			Entry.bHasRotation = true;

			// Tags as FName array → FString array (sorted).
			const TArray<FName>& Tags = Actor->Tags;
			Entry.Tags.Reserve(Tags.Num());
			for (const FName& Tag : Tags)
			{
				Entry.Tags.Add(Tag.ToString());
			}
			Entry.Tags.Sort();
		}

		if (Scope == ECUSnapshotScope::Full)
		{
			Entry.Scale = Actor->GetActorScale3D();
			Entry.bHasScale = true;

			Entry.Velocity = Actor->GetVelocity();
			Entry.bHasVelocity = true;

			ExtractComponentNames(Actor, Entry.ComponentNames);
			Entry.bHasComponents = true;

			ExtractReflectedScalars(Actor, Entry.ReflectedScalars);
			Entry.bHasReflection = Entry.ReflectedScalars.Num() > 0;
		}

		return Entry;
	}

	FCUPIESnapshotPlayer BuildPlayerEntry(APlayerController* PC, int32 Index)
	{
		FCUPIESnapshotPlayer Entry;
		Entry.Index = Index;

		if (!IsValid(PC)) return Entry;
		Entry.ControllerPath = PC->GetPathName();

		APawn* Pawn = PC->GetPawn();
		if (IsValid(Pawn) && Pawn->GetClass())
		{
			Entry.PawnPath = Pawn->GetPathName();
			Entry.PawnClassName = Pawn->GetClass()->GetName();
			Entry.PawnLocation = Pawn->GetActorLocation();
			Entry.PawnRotation = Pawn->GetActorRotation();
			Entry.bHasPawn = true;
		}

		FVector ViewLoc; FRotator ViewRot;
		PC->GetPlayerViewPoint(ViewLoc, ViewRot);
		Entry.ViewLocation = ViewLoc;
		Entry.ViewRotation = ViewRot;
		Entry.bHasView = true;

		return Entry;
	}
}  // anonymous namespace

//--------------------------------------------------------------------
// FCUPIESnapshot::ToJsonString
//--------------------------------------------------------------------

FString FCUPIESnapshot::ToJsonString() const
{
	FString Out = TEXT("{");
	Out += TEXT("\"snapshot_id\":");       Out += StringToJson(SnapshotId);
	Out += TEXT(",\"scope\":");             Out += StringToJson(CUSnapshotScopeToString(Scope));
	Out += TEXT(",\"level\":");             Out += StringToJson(LevelName);
	Out += TEXT(",\"time_seconds\":");      Out += NumToJson(TimeSeconds);
	Out += TEXT(",\"frame_number\":");      Out += IntToJson(FrameNumber);
	Out += TEXT(",\"actor_count\":");       Out += IntToJson(Actors.Num());
	Out += TEXT(",\"total_actor_count\":"); Out += IntToJson(TotalActorCount);
	Out += TEXT(",\"actors_truncated\":");  Out += BoolToJson(bActorsTruncated);
	Out += TEXT(",\"max_actors_cap\":");    Out += IntToJson(MaxActorsCap);
	Out += TEXT(",\"filter_class\":");      Out += StringToJson(FilterClass);

	Out += TEXT(",\"players\":[");
	for (int32 i = 0; i < Players.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		Out += SerializePlayer(Players[i]);
	}
	Out += TEXT("]");

	Out += TEXT(",\"actors\":[");
	for (int32 i = 0; i < Actors.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		Out += SerializeActor(Actors[i], Scope);
	}
	Out += TEXT("]}");
	return Out;
}

//--------------------------------------------------------------------
// FCUPIESnapshotDiff::ToJsonString
//--------------------------------------------------------------------

FString FCUPIESnapshotDiff::ToJsonString() const
{
	FString Out = TEXT("{");
	Out += TEXT("\"added\":[");
	for (int32 i = 0; i < Added.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		// Added entries carry the actor's full captured view (use Full scope serialization).
		Out += SerializeActor(Added[i], ECUSnapshotScope::Full);
	}
	Out += TEXT("]");

	Out += TEXT(",\"removed\":[");
	for (int32 i = 0; i < Removed.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		Out += SerializeActor(Removed[i], ECUSnapshotScope::Full);
	}
	Out += TEXT("]");

	Out += TEXT(",\"modified\":[");
	for (int32 i = 0; i < Modified.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		Out += SerializeActorDiff(Modified[i]);
	}
	Out += TEXT("]");

	Out += TEXT(",\"players_diff\":[");
	for (int32 i = 0; i < PlayersDiff.Num(); ++i)
	{
		if (i > 0) Out += TEXT(",");
		Out += SerializePlayerDiff(PlayersDiff[i]);
	}
	Out += TEXT("]");

	Out += TEXT(",\"game_diff\":{");
	Out += TEXT("\"level_changed\":"); Out += BoolToJson(bLevelChanged);
	Out += TEXT(",\"time_delta\":");    Out += NumToJson(TimeDelta);
	Out += TEXT(",\"frame_delta\":");   Out += IntToJson(FrameDelta);
	Out += TEXT("}");

	Out += TEXT(",\"summary\":{");
	Out += TEXT("\"added\":");     Out += IntToJson(Added.Num());
	Out += TEXT(",\"removed\":");   Out += IntToJson(Removed.Num());
	Out += TEXT(",\"modified\":");  Out += IntToJson(Modified.Num());
	Out += TEXT(",\"unchanged\":"); Out += IntToJson(UnchangedCount);
	Out += TEXT("}}");
	return Out;
}

//--------------------------------------------------------------------
// FCUPIESnapshotCapture::Capture
//--------------------------------------------------------------------

FCUPIESnapshot FCUPIESnapshotCapture::Capture(
	UWorld* PlayWorld,
	ECUSnapshotScope Scope,
	const FString& FilterClass,
	int32 MaxActors)
{
	FCUPIESnapshot Snapshot;
	Snapshot.Scope = Scope;
	Snapshot.FilterClass = FilterClass;
	Snapshot.MaxActorsCap = FMath::Clamp(MaxActors, 1, 10000);

	if (!PlayWorld) return Snapshot;

	Snapshot.LevelName = PlayWorld->GetMapName();
	Snapshot.TimeSeconds = PlayWorld->GetTimeSeconds();
	Snapshot.FrameNumber = (int64)GFrameCounter;

	// Players: iterate PlayerController list.
	int32 PlayerIndex = 0;
	for (FConstPlayerControllerIterator It = PlayWorld->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		Snapshot.Players.Add(BuildPlayerEntry(PC, PlayerIndex++));
	}
	Snapshot.Players.Sort([](const FCUPIESnapshotPlayer& A, const FCUPIESnapshotPlayer& B)
	{
		return A.Index < B.Index;
	});

	// Actors: walk world iterator with scope filter + cap.
	int32 MatchedCount = 0;
	for (TActorIterator<AActor> It(PlayWorld); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || !Actor->GetClass()) continue;

		if (!ActorMatchesScope(Actor, Scope, FilterClass)) continue;

		++MatchedCount;
		if (Snapshot.Actors.Num() >= Snapshot.MaxActorsCap)
		{
			Snapshot.bActorsTruncated = true;
			continue;
		}

		Snapshot.Actors.Add(BuildActorEntry(Actor, Scope));
	}
	Snapshot.TotalActorCount = MatchedCount;

	// Deterministic: sort by Path.
	Snapshot.Actors.Sort([](const FCUPIESnapshotActor& A, const FCUPIESnapshotActor& B)
	{
		return A.Path < B.Path;
	});

	return Snapshot;
}

//--------------------------------------------------------------------
// FCUPIESnapshotCompare::Diff
//--------------------------------------------------------------------

namespace
{
	void ComputeActorFieldChanges(
		const FCUPIESnapshotActor& A,
		const FCUPIESnapshotActor& B,
		const FCUPIESnapshotDiffOptions& Options,
		TArray<FCUPIEFieldChange>& OutChanges)
	{
		// Always-compared: class, location.
		if (!A.ClassName.Equals(B.ClassName))
		{
			OutChanges.Add({TEXT("class"), StringToJson(A.ClassName), StringToJson(B.ClassName)});
		}
		if (!Options.IgnoreFields.Contains(TEXT("location"))
			&& !VectorsApproxEqual(A.Location, B.Location, Options.VectorEpsilon))
		{
			OutChanges.Add({TEXT("location"), VectorToJson(A.Location), VectorToJson(B.Location)});
		}

		// Rotation / tags (present at Standard+).
		const bool bBothHaveRotation = A.bHasRotation && B.bHasRotation;
		if (bBothHaveRotation
			&& !Options.IgnoreFields.Contains(TEXT("rotation"))
			&& !RotatorsApproxEqual(A.Rotation, B.Rotation, Options.VectorEpsilon))
		{
			OutChanges.Add({TEXT("rotation"), RotatorToJson(A.Rotation), RotatorToJson(B.Rotation)});
		}
		if (!Options.IgnoreFields.Contains(TEXT("tags"))
			&& !StringArraysEqual(A.Tags, B.Tags))
		{
			OutChanges.Add({TEXT("tags"), StringArrayToJson(A.Tags), StringArrayToJson(B.Tags)});
		}

		// Full-only fields.
		if (A.bHasScale && B.bHasScale
			&& !Options.IgnoreFields.Contains(TEXT("scale"))
			&& !VectorsApproxEqual(A.Scale, B.Scale, Options.VectorEpsilon))
		{
			OutChanges.Add({TEXT("scale"), VectorToJson(A.Scale), VectorToJson(B.Scale)});
		}
		if (A.bHasVelocity && B.bHasVelocity
			&& !Options.IgnoreFields.Contains(TEXT("velocity"))
			&& !VectorsApproxEqual(A.Velocity, B.Velocity, Options.VectorEpsilon))
		{
			OutChanges.Add({TEXT("velocity"), VectorToJson(A.Velocity), VectorToJson(B.Velocity)});
		}
		if (A.bHasComponents && B.bHasComponents
			&& !Options.IgnoreFields.Contains(TEXT("components"))
			&& !StringArraysEqual(A.ComponentNames, B.ComponentNames))
		{
			OutChanges.Add({TEXT("components"),
				StringArrayToJson(A.ComponentNames), StringArrayToJson(B.ComponentNames)});
		}
		if (A.bHasReflection && B.bHasReflection
			&& !Options.IgnoreFields.Contains(TEXT("scalars")))
		{
			// Union of keys; emit only differing ones as a single `scalars` diff field.
			TSet<FString> AllKeys;
			for (const TPair<FString, double>& P : A.ReflectedScalars) AllKeys.Add(P.Key);
			for (const TPair<FString, double>& P : B.ReflectedScalars) AllKeys.Add(P.Key);

			bool bAnyDiff = false;
			for (const FString& Key : AllKeys)
			{
				const double* AV = A.ReflectedScalars.Find(Key);
				const double* BV = B.ReflectedScalars.Find(Key);
				if (!AV || !BV) { bAnyDiff = true; break; }
				if (FMath::Abs(*AV - *BV) > Options.ScalarEpsilon) { bAnyDiff = true; break; }
			}
			if (bAnyDiff)
			{
				OutChanges.Add({TEXT("scalars"),
					ScalarMapToJson(A.ReflectedScalars), ScalarMapToJson(B.ReflectedScalars)});
			}
		}
	}

	void ComputePlayerFieldChanges(
		const FCUPIESnapshotPlayer& A,
		const FCUPIESnapshotPlayer& B,
		const FCUPIESnapshotDiffOptions& Options,
		TArray<FCUPIEFieldChange>& OutChanges)
	{
		if (!A.PawnPath.Equals(B.PawnPath))
		{
			OutChanges.Add({TEXT("pawn_path"), StringToJson(A.PawnPath), StringToJson(B.PawnPath)});
		}
		if (!A.ControllerPath.Equals(B.ControllerPath))
		{
			OutChanges.Add({TEXT("controller_path"),
				StringToJson(A.ControllerPath), StringToJson(B.ControllerPath)});
		}
		if (A.bHasPawn && B.bHasPawn)
		{
			if (!Options.IgnoreFields.Contains(TEXT("pawn_location"))
				&& !VectorsApproxEqual(A.PawnLocation, B.PawnLocation, Options.VectorEpsilon))
			{
				OutChanges.Add({TEXT("pawn_location"),
					VectorToJson(A.PawnLocation), VectorToJson(B.PawnLocation)});
			}
			if (!Options.IgnoreFields.Contains(TEXT("pawn_rotation"))
				&& !RotatorsApproxEqual(A.PawnRotation, B.PawnRotation, Options.VectorEpsilon))
			{
				OutChanges.Add({TEXT("pawn_rotation"),
					RotatorToJson(A.PawnRotation), RotatorToJson(B.PawnRotation)});
			}
		}
	}
}  // anonymous namespace

FCUPIESnapshotDiff FCUPIESnapshotCompare::Diff(
	const FCUPIESnapshot& A,
	const FCUPIESnapshot& B,
	const FCUPIESnapshotDiffOptions& Options)
{
	FCUPIESnapshotDiff Out;
	Out.bLevelChanged = !A.LevelName.Equals(B.LevelName);
	Out.TimeDelta = B.TimeSeconds - A.TimeSeconds;
	Out.FrameDelta = B.FrameNumber - A.FrameNumber;

	// Index B by path for O(N) lookup; walk A; then walk remaining B for Added.
	TMap<FString, int32> BByPath;
	BByPath.Reserve(B.Actors.Num());
	for (int32 i = 0; i < B.Actors.Num(); ++i)
	{
		BByPath.Add(B.Actors[i].Path, i);
	}

	TArray<bool> BSeen;
	BSeen.Init(false, B.Actors.Num());

	for (const FCUPIESnapshotActor& AActor : A.Actors)
	{
		const int32* BIdxPtr = BByPath.Find(AActor.Path);
		if (!BIdxPtr)
		{
			Out.Removed.Add(AActor);
			continue;
		}
		const FCUPIESnapshotActor& BActor = B.Actors[*BIdxPtr];
		BSeen[*BIdxPtr] = true;

		FCUPIESnapshotActorDiff Entry;
		Entry.Path = AActor.Path;
		Entry.ClassName = AActor.ClassName;
		ComputeActorFieldChanges(AActor, BActor, Options, Entry.ChangedFields);
		if (Entry.ChangedFields.Num() > 0)
		{
			Out.Modified.Add(MoveTemp(Entry));
		}
		else
		{
			++Out.UnchangedCount;
		}
	}
	for (int32 i = 0; i < B.Actors.Num(); ++i)
	{
		if (!BSeen[i])
		{
			Out.Added.Add(B.Actors[i]);
		}
	}

	// Player diffs: match by Index.
	TMap<int32, int32> APlayers;
	for (int32 i = 0; i < A.Players.Num(); ++i) APlayers.Add(A.Players[i].Index, i);
	for (int32 j = 0; j < B.Players.Num(); ++j)
	{
		const FCUPIESnapshotPlayer& BP = B.Players[j];
		const int32* AIdxPtr = APlayers.Find(BP.Index);
		if (!AIdxPtr) continue;  // Player join/leave not modeled as diff here.

		FCUPIESnapshotPlayerDiff PDiff;
		PDiff.Index = BP.Index;
		ComputePlayerFieldChanges(A.Players[*AIdxPtr], BP, Options, PDiff.ChangedFields);
		if (PDiff.ChangedFields.Num() > 0)
		{
			Out.PlayersDiff.Add(MoveTemp(PDiff));
		}
	}

	return Out;
}
