// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #10128: extracted from CUWebSocketEditorSubsystem.cpp HandleCompareActorState
// (~L13108-L13158) so the tolerance-arithmetic edge cases can be unit-tested without
// requiring a multi-PIE Editor world. The handler dispatches per-property diff
// detection through this free function; the inline block was removed in favor of a
// single call. See Tests/CUCompareActorStateTests.cpp for the 5 edge-case unit tests.

#pragma once

#include "CoreMinimal.h"

namespace CompareActorState
{
	// Returns true if the set of stringified property values across multi-PIE clients
	// differs beyond `Tolerance`.
	//
	// Semantics (must match the original inline block byte-for-byte):
	//   - Values.Num() <= 1               → false (no divergence to compare)
	//   - Tolerance <= 0.0                → true if Values.Num() > 1 (exact-equality mode)
	//   - any value where !V.IsNumeric()  → true (mixed types ⇒ diff)
	//   - parsed Numerics.Num() < 2       → false (defensive fall-through; cannot happen
	//                                              once mixed-type early-return fires,
	//                                              but kept to mirror the original guard)
	//   - else                            → (max - min) > Tolerance  (strict > boundary;
	//                                              exactly-at-Tolerance counts as a match)
	//
	// IMPORTANT: any change to the polarity of these comparisons must be reflected in
	// CUCompareActorStateTests.cpp and the 12 test_compare_actor_state_* mock E2E
	// fixtures. See Issue #10128 plan for the truth-table equivalence proof.
	bool DiffersWithinTolerance(const TSet<FString>& Values, double Tolerance);

	// Issue #27039: traverse a dot-path (e.g. "RootComponent.RelativeLocation.X",
	// "Children[0].Leaf", "Children.0.Leaf") from RootObject. Returns a JSON token
	// (number / true|false / quoted string / null). Sets OutReason on failure.
	// Intermediate hops: FStructProperty, FObjectProperty, FWeakObjectProperty,
	// FArrayProperty (object or struct inner). Map hops are not supported.
	FString ResolveDotPath(UObject* RootObject, const FString& DotPath, FString& OutReason);
}
