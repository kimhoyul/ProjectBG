// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #26812 (PR3): the blackboard-key persistence guard, shared by the two paths that write
// FBlackboardKeySelector / FValueOrBBKey_* values:
//   * the RUNTIME Remote Control path  — CURemoteLibrary.cpp (its original home, Issue #16913);
//   * the EDITOR-GRAPH authoring path  — CUBehaviorTreeAuthor.cpp (added by #26812 PR3).
//
// The body below was MOVED VERBATIM out of CURemoteLibrary.cpp: same signatures, same branch
// order, same message strings. Nothing here may be "improved" in place — the runtime path's
// #16913 contract is pinned on these exact outcomes, and the authoring path is being wired to
// them precisely so both surfaces answer the question identically.
//
// Not a Public API — do not include from outside Source/NarshaMCP/Private.

#pragma once

#include "CoreMinimal.h"

#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"

// ---------------------------------------------------------------------------
// Issue #16913: blackboard-key struct persistence guard.
//
// FBlackboardKeySelector and FValueOrBBKey_* persist ONLY their key NAME; on
// save UBTNode::PreSave re-validates that name against the BT's blackboard and
// silently resets it (FBlackboardKeySelector -> NAME_None or InitSelection
// rewrite; FValueOrBBKey -> NAME_None) when the name is missing or its type is
// not allowed. The #4411 FStructProperty path set the name via JsonObjectToUStruct
// without that validation, so an unknown/wrong-type key was silently wiped on
// save with no error surfaced. These helpers replicate the engine's keep/wipe
// gate so the caller can fail loudly (and non-destructively) instead.
// All editor-only: the PreSave normalization itself is `#if WITH_EDITOR`.
// ---------------------------------------------------------------------------
#if WITH_EDITOR
namespace NarshaBlackboardKeyGuard
{
	/** All key names reachable from this blackboard (incl. parent chain), for error messages. */
	static FString DescribeAvailableKeys(const UBlackboardData* BB)
	{
		TArray<FString> Names;
		for (const UBlackboardData* It = BB; It; It = It->Parent)
		{
			for (const FBlackboardEntry& Entry : It->GetKeys())
			{
				if (!Entry.EntryName.IsNone())
				{
					Names.AddUnique(Entry.EntryName.ToString());
				}
			}
		}
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : TEXT("(none)");
	}

	/**
	 * Predicts whether the engine's PreSave gate will KEEP the struct's key as-authored.
	 * Mirrors FBlackboardKeySelector::PreSave / FValueOrBlackboardKeyBase::PreSave exactly:
	 * the name must resolve in the blackboard AND the resolved key's type must be allowed.
	 * Returns false + fills OutWhy (incl. available keys) when the key would be wiped/rewritten.
	 */
	static bool WillKeyPersist(bool bIsSelector, void* ValuePtr, const UBlackboardData& BB, FString& OutWhy)
	{
		if (bIsSelector)
		{
			FBlackboardKeySelector* Selector = static_cast<FBlackboardKeySelector*>(ValuePtr);
			if (Selector->SelectedKeyName.IsNone())
			{
				return true; // explicit None survives PreSave unchanged
			}
			// Populate transient SelectedKeyID/Type so the in-memory node is correct this session.
			Selector->ResolveSelectedKey(BB);

			const FBlackboard::FKey KeyId = BB.GetKeyID(Selector->SelectedKeyName);
			if (KeyId != FBlackboard::InvalidKey)
			{
				if (const FBlackboardEntry* Entry = BB.GetKey(KeyId))
				{
					// Key EXISTS. Mirror FBlackboardKeySelector::PreSave's branch order:
					// an empty AllowedTypes keeps the key unconditionally (the engine returns
					// before any KeyType deref); only with a filter does the resolved key's
					// type have to pass. The KeyType null-check guards the filter deref only
					// (a fresh, un-typed BB entry must not flip a would-be KEEP into REJECT).
					if (Selector->AllowedTypes.Num() == 0)
					{
						return true;
					}
					if (Entry->KeyType)
					{
						for (const TObjectPtr<UBlackboardKeyType>& AllowedType : Selector->AllowedTypes)
						{
							if (AllowedType && Entry->KeyType->IsAllowedByFilter(AllowedType))
							{
								return true;
							}
						}
					}
					OutWhy = FString::Printf(
						TEXT("key '%s' exists but its type is not allowed for this node (engine would reset it on save); available keys: %s"),
						*Selector->SelectedKeyName.ToString(), *DescribeAvailableKeys(&BB));
					return false;
				}
			}
			OutWhy = FString::Printf(
				TEXT("key '%s' does not exist in blackboard '%s' (engine would reset it on save); available keys: %s"),
				*Selector->SelectedKeyName.ToString(), *BB.GetName(), *DescribeAvailableKeys(&BB));
			return false;
		}
		else
		{
			FValueOrBlackboardKeyBase* ValueOrKey = static_cast<FValueOrBlackboardKeyBase*>(ValuePtr);
			const FName Key = ValueOrKey->GetKey();
			if (Key.IsNone())
			{
				return true; // literal value, not a blackboard binding
			}
			const FBlackboard::FKey KeyId = BB.GetKeyID(Key);
			if (KeyId != FBlackboard::InvalidKey)
			{
				if (const FBlackboardEntry* Entry = BB.GetKey(KeyId))
				{
					if (ValueOrKey->IsCompatibleType(Entry->KeyType))
					{
						return true;
					}
				}
			}
			OutWhy = FString::Printf(
				TEXT("key '%s' is missing or the wrong type for this value (engine would reset it on save); available keys: %s"),
				*Key.ToString(), *DescribeAvailableKeys(&BB));
			return false;
		}
	}
}
#endif // WITH_EDITOR
