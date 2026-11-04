// Copyright 2026 Next Stage Inc. All rights reserved.
//
// Issue #19009 — shared type-token → FEdGraphPinType mapper.
// Extracted verbatim from CUStructAuthor.cpp (#18415) so ue_author_struct and
// CreateBlueprintVariable (ue_manage_blueprint add_variable) share ONE
// implementation of the struct:/object: grammar and cannot drift.
//
// `static inline` in a namespace: internal linkage + included by multiple TUs
// (same pattern as CUAnimAuthoring_Shared.h) — no ODR violation, no linker conflict.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphNode.h"        // FEdGraphTerminalType::FromPinType (map value type)
#include "EdGraphSchema_K2.h"
#include "UObject/Class.h"
#include "UObject/UObjectGlobals.h"

namespace NarshaPinTypeUtils
{
	/**
	 * Map an input type token → FEdGraphPinType. Returns false + fills OutError for an unsupported
	 * type (explicit failure, never a silent wrong type — mirrors add_variable's UNSUPPORTED_VARIABLE_TYPE).
	 * Minimal set (#18415 acceptance): primitives + FVector/FRotator/FTransform + object:<class> + struct:<uds>.
	 */
	static inline bool MakePinType(const FString& TypeStr, FEdGraphPinType& Out, FString& OutError)
	{
		const FString T = TypeStr.TrimStartAndEnd();
		Out = FEdGraphPinType();
		Out.ContainerType = EPinContainerType::None;

		auto Is = [&T](const TCHAR* S) { return T.Equals(S, ESearchCase::IgnoreCase); };

		// Container prefixes (Issue #19277): array:/set:/map: wrap an element type. Depth-1 only —
		// the wrapped element must itself resolve to a NON-container type (no array:array:...).
		{
			auto StartsCI = [&T](const TCHAR* S) { return T.StartsWith(S, ESearchCase::IgnoreCase); };
			auto IsContainerToken = [](const FString& S) -> bool
			{
				const FString E = S.TrimStartAndEnd();
				return E.StartsWith(TEXT("array:"), ESearchCase::IgnoreCase)
					|| E.StartsWith(TEXT("set:"), ESearchCase::IgnoreCase)
					|| E.StartsWith(TEXT("map:"), ESearchCase::IgnoreCase);
			};
			auto ResolveElement = [&IsContainerToken](const FString& ElemStr, FEdGraphPinType& ElemOut, FString& Err) -> bool
			{
				// Depth-1: reject a nested container BEFORE recursing. This bounds MakePinType recursion
				// to a single extra frame regardless of input length — a crafted 'array:'*N+'int' string
				// can no longer drive N stack frames (Editor stack-overflow DoS, Issue #19277 review).
				if (IsContainerToken(ElemStr))
				{
					Err = FString::Printf(TEXT("nested containers are not supported: '%s'"), *ElemStr.TrimStartAndEnd());
					return false;
				}
				return MakePinType(ElemStr, ElemOut, Err);
			};

			if (StartsCI(TEXT("array:")) || StartsCI(TEXT("set:")))
			{
				const bool bArray = StartsCI(TEXT("array:"));
				const FString ElemStr = T.Mid(bArray ? 6 : 4).TrimStartAndEnd();
				FEdGraphPinType Elem;
				if (!ResolveElement(ElemStr, Elem, OutError))
				{
					return false;
				}
				Out = Elem;
				Out.ContainerType = bArray ? EPinContainerType::Array : EPinContainerType::Set;
				return true;
			}
			if (StartsCI(TEXT("map:")))
			{
				// map:<key>:<value>. A key or value may be a prefixed object:/struct: token whose PATH
				// itself can contain ':' (UE subobject syntax 'Pkg.Asset:Sub'), so a fixed "first ':'
				// after the prefix" split is ambiguous. Resolve GREEDILY instead: scan ':' boundaries
				// left-to-right and take the FIRST split where BOTH the key AND the value resolve. This
				// removes the "paths carry no ':'" assumption entirely — a subobject-path key (never a
				// valid class/struct map key) simply yields no valid split and fail-closes with an error
				// rather than mis-typing (Issue #19277 review — full fix, not just characterization).
				const FString Rest = T.Mid(4).TrimStartAndEnd();
				if (IsContainerToken(Rest))   // a container-typed key is never valid
				{
					OutError = FString::Printf(TEXT("map key cannot be a container type: '%s'"), *TypeStr);
					return false;
				}
				FEdGraphPinType KeyPin, ValPin;
				bool bSplit = false;
				// Most-actionable diagnostic for the fail path: the value-side error from the FIRST
				// split whose KEY resolved. That split's key is almost always the intended key, so its
				// value error is the real reason (e.g. a typo'd 'struct:'/'object:' value path) — the
				// most common failure mode. Without this, greedy candidate-probing would swallow every
				// per-candidate error and only the generic "map requires..." would survive, regressing
				// MakePinType's "explicit, actionable failure — never generic" contract (Issue #19277 review).
				FString BestErr;
				for (int32 SearchFrom = 0; ; )
				{
					const int32 Colon = Rest.Find(TEXT(":"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
					if (Colon == INDEX_NONE)
					{
						break;
					}
					const FString KeyStr = Rest.Left(Colon).TrimStartAndEnd();
					const FString ValStr = Rest.Mid(Colon + 1).TrimStartAndEnd();
					FString KeyErr, ValErr;
					FEdGraphPinType K, V;
					const bool bKeyOk = ResolveElement(KeyStr, K, KeyErr);
					if (bKeyOk && ResolveElement(ValStr, V, ValErr))
					{
						KeyPin = K;
						ValPin = V;
						bSplit = true;
						break;
					}
					if (bKeyOk && BestErr.IsEmpty() && !ValErr.IsEmpty())
					{
						BestErr = ValErr;   // key resolved, value didn't — capture the specific value error once
					}
					SearchFrom = Colon + 1;   // this boundary didn't yield a valid key+value; try the next ':'
				}
				if (!bSplit)
				{
					OutError = BestErr.IsEmpty()
						? FString::Printf(
							TEXT("map type requires 'map:<key>:<value>' with a resolvable key and value (got '%s')"), *TypeStr)
						: FString::Printf(
							TEXT("map value type unresolved (got '%s'): %s"), *TypeStr, *BestErr);
					return false;
				}
				Out = KeyPin;                                       // key type occupies the main pin
				Out.ContainerType = EPinContainerType::Map;
				Out.PinValueType = FEdGraphTerminalType::FromPinType(ValPin);   // value type (all fields, incl. const/weak/wrapper)
				return true;
			}
		}

		if (Is(TEXT("bool")) || Is(TEXT("boolean")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Boolean;
			return true;
		}
		if (Is(TEXT("int")) || Is(TEXT("int32")) || Is(TEXT("integer")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Int;
			return true;
		}
		if (Is(TEXT("int64")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Int64;
			return true;
		}
		if (Is(TEXT("float")) || Is(TEXT("double")) || Is(TEXT("real")))
		{
			// UE5 unified real: PC_Real + subcategory PC_Double.
			Out.PinCategory = UEdGraphSchema_K2::PC_Real;
			Out.PinSubCategory = UEdGraphSchema_K2::PC_Double;
			return true;
		}
		if (Is(TEXT("string")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_String;
			return true;
		}
		if (Is(TEXT("name")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Name;
			return true;
		}
		if (Is(TEXT("text")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Text;
			return true;
		}
		if (Is(TEXT("FVector")) || Is(TEXT("vector")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Out.PinSubCategoryObject = TBaseStructure<FVector>::Get();
			return true;
		}
		if (Is(TEXT("FRotator")) || Is(TEXT("rotator")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Out.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
			return true;
		}
		if (Is(TEXT("FTransform")) || Is(TEXT("transform")))
		{
			Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Out.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
			return true;
		}
		if (T.StartsWith(TEXT("object:"), ESearchCase::IgnoreCase))
		{
			const FString ClassPath = T.Mid(7).TrimStartAndEnd();
			UClass* Cls = LoadObject<UClass>(nullptr, *ClassPath);
			if (!Cls)
			{
				OutError = FString::Printf(TEXT("object type: could not load UClass '%s'"), *ClassPath);
				return false;
			}
			Out.PinCategory = UEdGraphSchema_K2::PC_Object;
			Out.PinSubCategoryObject = Cls;
			return true;
		}
		if (T.StartsWith(TEXT("struct:"), ESearchCase::IgnoreCase))
		{
			const FString StructPath = T.Mid(7).TrimStartAndEnd();
			UScriptStruct* SS = LoadObject<UScriptStruct>(nullptr, *StructPath);
			if (!SS)
			{
				OutError = FString::Printf(TEXT("struct type: could not load UScriptStruct '%s'"), *StructPath);
				return false;
			}
			Out.PinCategory = UEdGraphSchema_K2::PC_Struct;
			Out.PinSubCategoryObject = SS;
			return true;
		}

		OutError = FString::Printf(
			TEXT("Unsupported type '%s'. Supported: bool, int, int64, float/double, string, name, text, ")
			TEXT("FVector, FRotator, FTransform, object:<ClassPath>, struct:<UserDefinedStructPath>, ")
			TEXT("array:<elem>, set:<elem>, map:<key>:<value> (elem/key/value = any of the above; depth-1)"),
			*TypeStr);
		return false;
	}
}
