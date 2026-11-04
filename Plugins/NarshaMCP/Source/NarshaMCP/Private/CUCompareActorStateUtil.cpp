// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #10128: see CUCompareActorStateUtil.h for the rationale.
//
// Cross-language default-value note:
//   - C++ default Tolerance is 0.0  (CUWebSocketEditorSubsystem.cpp HandleCompareActorState
//     entry point — falls through the `Tolerance <= 0.0` exact-equality branch below).
//   - Rust bridge default is 0.001  (editor_control_pie.rs NarshaMcpServer::resolve_tolerance,
//     `unwrap_or(0.001)`).
//   - The divergence is intentional and documented per Issue #10128. Bridge consumers
//     (every AI-agent caller via MCP) experience 0.001 unless they explicitly pass
//     `tolerance: 0.0` — direct C++ unit-test callers see 0.0. If alignment is ever
//     chosen, both resolve_tolerance in editor_control_pie.rs and CUWebSocketEditorSubsystem.cpp:13055
//     must be updated together with this file's behavior pinned tests.

#include "CUCompareActorStateUtil.h"
#include "CUJsonUtils.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtr.h"

namespace CompareActorState
{
	bool DiffersWithinTolerance(const TSet<FString>& Values, double Tolerance)
	{
		if (Values.Num() <= 1)
		{
			return false;
		}

		// Tolerance == 0.0 short-circuit: any difference is a diff. Mirrors the
		// `if (Tolerance > 0.0) { ... } else { bDiffers = true; }` else-branch of
		// the original inline block (CUWebSocketEditorSubsystem.cpp pre-#10128).
		if (Tolerance <= 0.0)
		{
			return true;
		}

		TArray<double> Numerics;
		Numerics.Reserve(Values.Num());
		for (const FString& V : Values)
		{
			if (!V.IsNumeric())
			{
				// Mixed numeric+non-numeric forces diff. The original block tracked
				// this via `bAllNumeric=false; break;` followed by the
				// `bAllNumeric && Numerics.Num() >= 2` guard which evaluated false →
				// fell into the `else { bDiffers = true; }`. Early-return here is
				// equivalent: both paths arrive at "diff" for any mixed input.
				return true;
			}
			Numerics.Add(FCString::Atod(*V));
		}

		// Defensive fall-through: 0 or 1 parsed numerics cannot have a spread.
		// The original block used `bAllNumeric && Numerics.Num() >= 2` as the guard;
		// reaching here means bAllNumeric==true (no early-return above). When
		// Values.Num() > 1 every entry was numeric, so Numerics.Num() == Values.Num()
		// and this branch is unreachable in practice — kept to mirror the original
		// guard so any future code that loosens the parse step doesn't regress.
		if (Numerics.Num() < 2)
		{
			return false;
		}

		double MinV = Numerics[0];
		double MaxV = Numerics[0];
		for (double D : Numerics)
		{
			MinV = FMath::Min(MinV, D);
			MaxV = FMath::Max(MaxV, D);
		}
		// Strict `>` boundary: exactly-at-Tolerance counts as a match (NOT a diff).
		// Mirrors line `bDiffers = (MaxV - MinV) > Tolerance;` of the original block.
		return (MaxV - MinV) > Tolerance;
	}

	namespace
	{
		bool ParsePathPart(const FString& Raw, FString& OutName, TOptional<int32>& OutIndex)
		{
			OutIndex.Reset();
			const int32 Bracket = Raw.Find(TEXT("["));
			if (Bracket == INDEX_NONE)
			{
				if (Raw.IsNumeric())
				{
					OutName.Reset();
					OutIndex = FCString::Atoi(*Raw);
					return true;
				}
				OutName = Raw;
				return true;
			}
			if (!Raw.EndsWith(TEXT("]")) || Bracket == 0)
			{
				return false;
			}
			OutName = Raw.Left(Bracket);
			const FString IndexStr = Raw.Mid(Bracket + 1, Raw.Len() - Bracket - 2);
			if (IndexStr.IsEmpty() || !IndexStr.IsNumeric())
			{
				return false;
			}
			OutIndex = FCString::Atoi(*IndexStr);
			return true;
		}

		FString SerializeLeaf(FProperty* Prop, void* Container, FString& OutReason)
		{
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
			if (FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
			{
				if (NumProp->IsFloatingPoint())
				{
					const double FV = NumProp->GetFloatingPointPropertyValue(ValuePtr);
					if (!FMath::IsFinite(FV))
					{
						OutReason = FMath::IsNaN(FV) ? TEXT("non_finite:nan") : TEXT("non_finite:inf");
						return TEXT("null");
					}
					return FString::Printf(TEXT("%.17g"), FV);
				}
				return FString::Printf(TEXT("%lld"), NumProp->GetSignedIntPropertyValue(ValuePtr));
			}
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			{
				return BoolProp->GetPropertyValue(ValuePtr) ? TEXT("true") : TEXT("false");
			}
			if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			{
				return FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(StrProp->GetPropertyValue(ValuePtr)));
			}
			if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
			{
				return FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(NameProp->GetPropertyValue(ValuePtr).ToString()));
			}
			if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
			{
				UObject* Obj = ObjProp->GetObjectPropertyValue(ValuePtr);
				if (!IsValid(Obj))
				{
					OutReason = TEXT("null_object_leaf");
					return TEXT("null");
				}
				return FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Obj->GetPathName()));
			}
			if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				FString Exported;
				StructProp->ExportTextItem_Direct(Exported, ValuePtr, nullptr, nullptr, PPF_None);
				return FString::Printf(TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(Exported));
			}

			OutReason = FString::Printf(TEXT("unsupported_type:%s"), *Prop->GetClass()->GetName());
			return TEXT("null");
		}
	}

	FString ResolveDotPath(UObject* RootObject, const FString& DotPath, FString& OutReason)
	{
		OutReason.Reset();
		if (!IsValid(RootObject))
		{
			OutReason = TEXT("null_object");
			return TEXT("null");
		}

		TArray<FString> Parts;
		DotPath.ParseIntoArray(Parts, TEXT("."), true);
		if (Parts.Num() == 0)
		{
			OutReason = TEXT("empty_path");
			return TEXT("null");
		}

		UStruct* CurrentStruct = RootObject->GetClass();
		void* CurrentContainer = RootObject;
		FProperty* Prop = nullptr;
		UObject* CurrentObject = RootObject;

		auto HopObject = [&](UObject* SubObj, const FString& Label) -> bool
		{
			if (!IsValid(SubObj))
			{
				OutReason = FString::Printf(TEXT("null_object_intermediate:%s"), *Label);
				return false;
			}
			CurrentObject = SubObj;
			CurrentContainer = SubObj;
			CurrentStruct = SubObj->GetClass();
			Prop = nullptr;
			return true;
		};

		auto HopArrayIndex = [&](FArrayProperty* ArrayProp, void* ArrayOwner, int32 Index) -> bool
		{
			void* ArrayPtr = ArrayProp->ContainerPtrToValuePtr<void>(ArrayOwner);
			FScriptArrayHelper Helper(ArrayProp, ArrayPtr);
			if (Index < 0 || Index >= Helper.Num())
			{
				OutReason = FString::Printf(TEXT("array_index_oob:%d"), Index);
				return false;
			}
			uint8* Elem = Helper.GetRawPtr(Index);
			if (FObjectProperty* InnerObj = CastField<FObjectProperty>(ArrayProp->Inner))
			{
				return HopObject(InnerObj->GetObjectPropertyValue(Elem),
					FString::Printf(TEXT("%s[%d]"), *ArrayProp->GetName(), Index));
			}
			if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
			{
				CurrentContainer = Elem;
				CurrentStruct = InnerStruct->Struct;
				Prop = InnerStruct;
				return true;
			}
			OutReason = FString::Printf(TEXT("unsupported_array_inner:%s"),
				*ArrayProp->Inner->GetClass()->GetName());
			return false;
		};

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			FString Name;
			TOptional<int32> Index;
			if (!ParsePathPart(Parts[i], Name, Index))
			{
				OutReason = FString::Printf(TEXT("bad_path_segment:%s"), *Parts[i]);
				return TEXT("null");
			}

			if (!Name.IsEmpty())
			{
				if (!CurrentStruct || !CurrentContainer)
				{
					OutReason = TEXT("path_nav_failed");
					return TEXT("null");
				}
				Prop = CurrentStruct->FindPropertyByName(FName(*Name));
				if (!Prop)
				{
					// Blueprint-added components are often not UPROPERTY fields on the
					// generated class (CharacterMovement is; LyraEquipmentManager is not).
					// Fall back to actor component name so actor→component→owned UObject
					// paths work for Issue #27039.
					AActor* AsActor = Cast<AActor>(CurrentObject);
					UActorComponent* FoundComp = nullptr;
					if (AsActor)
					{
						for (UActorComponent* Comp : AsActor->GetComponents())
						{
							if (!FoundComp && IsValid(Comp)
								&& Comp->GetName() == Name)
							{
								FoundComp = Comp;
							}
						}
					}
					if (!FoundComp)
					{
						OutReason = FString::Printf(TEXT("property_not_found:%s"), *Name);
						return TEXT("null");
					}
					if (!HopObject(FoundComp, Name))
					{
						return TEXT("null");
					}
					continue;
				}

				const bool bLastBare = (i == Parts.Num() - 1) && !Index.IsSet();
				if (bLastBare)
				{
					break;
				}

				void* OwnerContainer = CurrentContainer;
				if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(OwnerContainer);
					CurrentStruct = StructProp->Struct;
				}
				else if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
				{
					UObject* SubObj = ObjProp->GetObjectPropertyValue(
						ObjProp->ContainerPtrToValuePtr<void>(OwnerContainer));
					if (!HopObject(SubObj, Name))
					{
						return TEXT("null");
					}
				}
				else if (FWeakObjectProperty* WeakProp = CastField<FWeakObjectProperty>(Prop))
				{
					FWeakObjectPtr WeakPtr = WeakProp->GetPropertyValue(
						WeakProp->ContainerPtrToValuePtr<void>(OwnerContainer));
					if (!HopObject(WeakPtr.Get(), Name))
					{
						return TEXT("null");
					}
				}
				else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
				{
					if (!Index.IsSet())
					{
						continue;
					}
					if (!HopArrayIndex(ArrayProp, OwnerContainer, Index.GetValue()))
					{
						return TEXT("null");
					}
					Index.Reset();
				}
				else
				{
					OutReason = FString::Printf(TEXT("unsupported_intermediate_type:%s"),
						*Prop->GetClass()->GetName());
					return TEXT("null");
				}
			}

			if (Index.IsSet())
			{
				FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop);
				if (!ArrayProp)
				{
					OutReason = TEXT("index_without_array");
					return TEXT("null");
				}
				if (!HopArrayIndex(ArrayProp, CurrentContainer, Index.GetValue()))
				{
					return TEXT("null");
				}
			}
		}

		if (Prop && CurrentContainer)
		{
			if (CastField<FArrayProperty>(Prop))
			{
				OutReason = TEXT("array_needs_index");
				return TEXT("null");
			}
			return SerializeLeaf(Prop, CurrentContainer, OutReason);
		}

		if (IsValid(CurrentObject))
		{
			return FString::Printf(TEXT("\"%s\""),
				*CUJsonUtils::EscapeJsonString(CurrentObject->GetPathName()));
		}

		OutReason = TEXT("path_nav_failed");
		return TEXT("null");
	}
}
