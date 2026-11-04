// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27040

#include "CUActorClassFilter.h"

#include "GameFramework/Actor.h"
#include "Misc/Char.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

UClass* FCUActorClassFilter::ResolveActorFilterClass(const FString& ClassRef)
{
	if (ClassRef.IsEmpty())
	{
		return nullptr;
	}

	// Always keep ClassRef. An A/U prefix is the C++ type name (APawn), not a
	// fully-qualified UClass FName (Pawn). Skip-on-prefix dropped APawn and
	// also skipped fallbacks for "Actor" (Issue #27040).
	TArray<FString, TInlineAllocator<8>> Candidates;
	Candidates.Add(ClassRef);
	if (!ClassRef.StartsWith(TEXT("/")))
	{
		FString EngineName = ClassRef;
		if (ClassRef.Len() >= 2)
		{
			const TCHAR Prefix = ClassRef[0];
			const TCHAR Next = ClassRef[1];
			if ((Prefix == TEXT('A') || Prefix == TEXT('U')) && FChar::IsUpper(Next))
			{
				EngineName = ClassRef.Mid(1);
				Candidates.AddUnique(EngineName);
			}
		}
		Candidates.AddUnique(FString::Printf(TEXT("A%s"), *EngineName));
		Candidates.AddUnique(FString::Printf(TEXT("U%s"), *EngineName));
		Candidates.AddUnique(FString::Printf(TEXT("/Script/Engine.%s"), *EngineName));
		Candidates.AddUnique(FString::Printf(TEXT("/Script/Engine.A%s"), *EngineName));
	}

	for (const FString& Cand : Candidates)
	{
		UClass* Found = FindFirstObject<UClass>(*Cand, EFindFirstObjectOptions::NativeFirst);
		if (!Found)
		{
			Found = FindObject<UClass>(nullptr, *Cand);
		}
		if (!Found && Cand.StartsWith(TEXT("/")))
		{
			Found = StaticLoadClass(AActor::StaticClass(), nullptr, *Cand);
		}
		if (Found && (Found == AActor::StaticClass() || Found->IsChildOf(AActor::StaticClass())))
		{
			return Found;
		}
	}
	return nullptr;
}

bool FCUActorClassFilter::ParseMatchMode(const FString& MatchMode, FString& OutNormalized, FString& OutError)
{
	const FString Lower = MatchMode.ToLower();
	if (Lower == TEXT("is_a") || Lower == TEXT("exact") || Lower == TEXT("substring"))
	{
		OutNormalized = Lower;
		OutError.Reset();
		return true;
	}
	OutError = FString::Printf(
		TEXT("Unknown match_mode '%s' (expected is_a, exact, or substring)"),
		*MatchMode);
	return false;
}

bool FCUActorClassFilter::MatchesClass(const UClass* ActorClass, const FString& ClassName,
	const FString& NormalizedMode, const UClass* ResolvedFilter)
{
	if (ActorClass == nullptr)
	{
		return false;
	}
	if (NormalizedMode == TEXT("is_a"))
	{
		// A null ResolvedFilter here means the caller skipped resolution, which
		// must never silently match. exact/substring never resolve one.
		return ResolvedFilter != nullptr && ActorClass->IsChildOf(ResolvedFilter);
	}
	const FString ActorClassName = ActorClass->GetName();
	return (NormalizedMode == TEXT("exact"))
		? ActorClassName.Equals(ClassName, ESearchCase::IgnoreCase)
		: ActorClassName.Contains(ClassName, ESearchCase::IgnoreCase);
}
