// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #16915 (Epic #16906 Tier 3): Domain C++ authoring for the reflection-blocked
// BehaviorTree editor graph. Phase B: arbitrary-topology spec (composites/tasks/decorators/
// services, nesting, positions, per-node properties) — turns the PoC into a real feature.

#include "CUBehaviorTreeAuthor.h"
#include "CUBehaviorTreeAuthorInternals.h"
#include "CUBehaviorTreeWriteFinalize.h"
#include "CUBlackboardKeyGuard.h" // Issue #26812 PR3: blackboard key existence + type validation
#include "CULog.h"
#include "Misc/StringOutputDevice.h" // Issue #26812: capture engine ImportText diagnostics
#include "Misc/DefaultValueHelper.h" // Issue #26812: strict float literal validation
#include "BehaviorTree/BTNode.h"

// Runtime BT asset + concrete node classes (AIModule).
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h" // FBlackboardKeySelector
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Tasks/BTTask_Wait.h"
// Blackboard key types (Phase B.2).
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"

// Editor graph (BehaviorTreeEditor / AIGraph — reflection-blocked area being bypassed).
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_CompositeDecorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "AIGraphTypes.h" // FGraphNodeClassData

#include "EdGraph/EdGraph.h" // FGraphNodeCreator
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h" // FBlueprintEditorUtils::CreateNewGraph

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h" // FProperty::ImportText_Direct
#include "UObject/UObjectGlobals.h"

namespace CUBehaviorTreeAuthorPrivate
{
	/** Find the single UBehaviorTreeGraphNode_Root spawned by CreateDefaultNodesForGraph. */
	UBehaviorTreeGraphNode_Root* FindRootEdNode(const UEdGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UBehaviorTreeGraphNode_Root* Root = Cast<UBehaviorTreeGraphNode_Root>(Node))
			{
				return Root;
			}
		}
		return nullptr;
	}

	/** Resolve a node class from a native short/full name OR a class object path (BP nodes). */
	UClass* ResolveNodeClass(const FString& Name)
	{
		if (Name.IsEmpty())
		{
			return nullptr;
		}
		// Object path form (e.g. "/Game/AI/BTT_Foo.BTT_Foo_C" or "/Script/AIModule.BTTask_Wait").
		if (Name.Contains(TEXT("/")) || Name.Contains(TEXT(".")))
		{
			if (UClass* C = LoadObject<UClass>(nullptr, *Name))
			{
				return C;
			}
			if (UClass* C = LoadClass<UObject>(nullptr, *Name))
			{
				return C;
			}
		}
		// Native class by short or full name (e.g. "BTComposite_Selector", "BTTask_Wait").
		return UClass::TryFindTypeSlow<UClass>(Name);
	}

	FString ClassCppName(const UClass* Cls)
	{
		if (!Cls)
		{
			return TEXT("(null)");
		}
		return FString::Printf(TEXT("%s%s"), Cls->GetPrefixCPP(), *Cls->GetName());
	}

	/** Issue #26811: reject abstract/deprecated classes and kind/base mismatches. */
	bool CheckClassAgainstBase(UClass* Cls, const UClass* Required, const FString& NodeId, const FString& Kind, FString& Err)
	{
		if (!Cls || !Required)
		{
			return false;
		}
		if (Cls->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			Err = FString::Printf(
				TEXT("class '%s' is abstract/deprecated (node '%s')"),
				*ClassCppName(Cls), *NodeId);
			return false;
		}
		if (!Cls->IsChildOf(Required))
		{
			Err = FString::Printf(
				TEXT("class '%s' is not a '%s' (node '%s', kind '%s')"),
				*ClassCppName(Cls), *ClassCppName(Required), *NodeId, *Kind);
			return false;
		}
		return true;
	}

	bool ValidateSpecNodeClasses(const FCUBehaviorTreeSpec& Spec, FString& Err)
	{
		for (const FCUBTNodeSpec& N : Spec.Nodes)
		{
			UClass* Cls = ResolveNodeClass(N.ClassName);
			if (!Cls)
			{
				Err = FString::Printf(TEXT("Unknown node class '%s' (node '%s')"), *N.ClassName, *N.Id);
				return false;
			}
			const UClass* Required = (N.Kind == TEXT("composite"))
				? UBTCompositeNode::StaticClass()
				: UBTTaskNode::StaticClass();
			if (!CheckClassAgainstBase(Cls, Required, N.Id, N.Kind, Err))
			{
				return false;
			}
			for (const FCUBTSubNodeSpec& D : N.Decorators)
			{
				UClass* DCls = ResolveNodeClass(D.ClassName);
				if (!DCls)
				{
					Err = FString::Printf(TEXT("Unknown decorator class '%s'"), *D.ClassName);
					return false;
				}
				if (!CheckClassAgainstBase(DCls, UBTDecorator::StaticClass(), N.Id, TEXT("decorator"), Err))
				{
					return false;
				}
			}
			for (const FCUBTSubNodeSpec& S : N.Services)
			{
				UClass* SCls = ResolveNodeClass(S.ClassName);
				if (!SCls)
				{
					Err = FString::Printf(TEXT("Unknown service class '%s'"), *S.ClassName);
					return false;
				}
				if (!CheckClassAgainstBase(SCls, UBTService::StaticClass(), N.Id, TEXT("service"), Err))
				{
					return false;
				}
			}
		}
		return true;
	}

	/** Convert a JSON value to a string ImportText can parse. */
	FString JsonValueToImportString(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid())
		{
			return FString();
		}
		switch (V->Type)
		{
		case EJson::String:
			return V->AsString();
		case EJson::Boolean:
			return V->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:
		{
			const double D = V->AsNumber();
			if (FMath::IsNearlyEqual(D, FMath::RoundToDouble(D)))
			{
				return FString::Printf(TEXT("%lld"), static_cast<int64>(D));
			}
			return FString::SanitizeFloat(D);
		}
		default:
			return FString();
		}
	}

	/**
	 * Captures what the engine writes to ImportText's ErrorText channel, KEEPING the verbosity.
	 *
	 * Issue #26812. FStringOutputDevice discards the verbosity argument (StringOutputDevice.h:29),
	 * but the engine writes both real errors and mere warnings here, and a warning can accompany a
	 * SUCCESSFUL import -- e.g. a fixed-size array that ignored surplus elements logs
	 * ELogVerbosity::Warning and then returns a non-null buffer (PropertyArray.cpp:1222). Treating
	 * every write as fatal would therefore reject working input.
	 *
	 * It also matters that some of these writes are wrapped in UE_SUPPRESS(LogExec, ...)
	 * (Property.cpp:1660/1666), so whether they happen at all depends on the runtime LogExec
	 * verbosity. A default-fatal gate must not be configuration-dependent, which is a second reason
	 * to key the fatal decision on severity rather than on "anything was written".
	 */
	class FCUImportDiagnostics : public FStringOutputDevice
	{
	public:
		virtual void Serialize(const TCHAR* InData, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			const ELogVerbosity::Type Masked =
				static_cast<ELogVerbosity::Type>(Verbosity & ELogVerbosity::VerbosityMask);
			// Lower value == more severe (Fatal=1, Error=2, Warning=3); NoLogging=0 means "nothing yet".
			if (Worst == ELogVerbosity::NoLogging || Masked < Worst)
			{
				Worst = Masked;
			}
			FStringOutputDevice::Serialize(InData, Verbosity, Category);
		}

		/** True only for Fatal/Error. A Warning is reported, but does not fail the property. */
		bool HasError() const
		{
			return Worst != ELogVerbosity::NoLogging && Worst <= ELogVerbosity::Error;
		}

		ELogVerbosity::Type Worst = ELogVerbosity::NoLogging;
	};

	/**
	 * Would `Text` parse as an integer?
	 *
	 * Issue #26812, the integer sibling of IsParsableFloatText. The integer branch of
	 * FNumericProperty::ImportText_Internal DOES reject text it consumed nothing from
	 * (PropertyNumeric.cpp:132), but a PARTIAL consume still succeeds: "3x" advances past the '3',
	 * returns non-null, and then sets the value from the WHOLE string via Atoi -- storing 3 and
	 * reporting success. IsStringValidInteger applies the strict grammar (decimal/hex/octal,
	 * nothing left over).
	 *
	 * Enum-backed integers are EXCLUDED: alpha text is legitimate there and is resolved by
	 * UEnum::ParseEnum (PropertyNumeric.cpp:88-104), which the strict numeric grammar would reject.
	 */
	bool IsParsableIntegerText(const FString& Text)
	{
		return FDefaultValueHelper::IsStringValidInteger(Text.TrimStartAndEnd());
	}

	/**
	 * Would `Text` parse as a floating-point value?
	 *
	 * Issue #26812. FNumericProperty::ImportText_Internal guards the INTEGER branch with
	 * `if (Start == Buffer) return NULL;` but the floating-point branch has no such guard: it
	 * consumes only [+-.0-9] plus a trailing f/F, then sets the value from the WHOLE string via
	 * Atof and returns whatever it consumed. So "abc" and "--1" both yield 0.0 with a non-null
	 * return and nothing written to the error device -- invisible to the other two rules.
	 *
	 * The check is a strict grammar test plus a numeric parse (see the body). Leading/trailing
	 * whitespace is trimmed for the CHECK only -- the original text is still what gets handed to
	 * ImportText, so string/name properties keep meaningful spaces.
	 */
	bool IsParsableFloatText(const FString& Text)
	{
		FString Trimmed = Text.TrimStartAndEnd();
		// The engine's float branch consumes one optional trailing f/F (PropertyNumeric.cpp:150), so
		// "1.5f" is a legitimate value; the strict grammar below does not know about it.
		if (Trimmed.Len() > 1 && Trimmed.EndsWith(TEXT("f"), ESearchCase::IgnoreCase))
		{
			Trimmed.LeftChopInline(1);
		}
		double Parsed = 0.0;
		// IsStringValidFloat is a STRICT grammar test -- sign, digits, '.', digits, optional e/E with
		// sign and digits, and nothing left over (DefaultValueHelper.cpp:268-308). LexTryParseString
		// alone is NOT enough: its zero-branch only requires that some '0' was seen, so "0abc" returns
		// true (UnrealString.h.inl:2219-2238), and "nan"/"inf" are non-zero so they skip that branch
		// entirely. Either would be reported as applied while the stored value is garbage -- exactly
		// the class of silence this issue exists to remove. IsFinite is belt-and-braces for the same.
		return FDefaultValueHelper::IsStringValidFloat(Trimmed)
			&& LexTryParseString(Parsed, *Trimmed)
			&& FMath::IsFinite(Parsed);
	}

	/** Apply a property map to a spawned NodeInstance via reflection. Struct values
	 *  (FVector "(X=..)", etc.) flow through ImportText; FBlackboardKeySelector is special-cased to
	 *  bind by key name against the BT's blackboard (ImportText cannot resolve the key id).
	 *
	 *  Issue #26812 -- a property is reported FAILED when any of:
	 *    1. the name does not exist on the class;
	 *    2. ImportText_Direct returns nullptr;
	 *    3. the property is numeric and the text does not parse strictly as that number type;
	 *    4. the engine wrote a diagnostic to the import error device.
	 *  Rule 4 needs an explicit device: the ErrorText parameter defaults to GWarn, so today every
	 *  engine import diagnostic is emitted somewhere unreachable to the caller. Passing a device
	 *  REDIRECTS those messages, so they are re-logged below or they would simply vanish. */
	FApplyPropertiesResult ApplyProperties(UObject* Obj, const TMap<FString, FString>& Props, UBlackboardData* BB)
	{
		FApplyPropertiesResult Out;
		if (!Obj)
		{
			return Out;
		}
		for (const TPair<FString, FString>& KV : Props)
		{
			FProperty* Prop = Obj->GetClass()->FindPropertyByName(*KV.Key);
			if (!Prop)
			{
				UE_LOG(LogNarshaMCP, Warning,
					TEXT("[BehaviorTreeAuthor] Unknown property '%s' on %s"),
					*KV.Key, *Obj->GetClass()->GetName());
				Out.Failed.Add(FString::Printf(TEXT("%s (no such property on %s)"),
					*KV.Key, *Obj->GetClass()->GetName()));
				continue;
			}
			void* Addr = Prop->ContainerPtrToValuePtr<void>(Obj);
			if (const FStructProperty* SP = CastField<FStructProperty>(Prop))
			{
				// IsChildOf, not ==: UBTNode::PreSave selects the structs it normalizes with
				// IsChildOf (BTNode.cpp:245), so an `==` test here would let a DERIVED selector
				// struct through unvalidated and the engine would still rewrite it on save. No
				// derived struct exists in the engine today (Engine/Source grep: 0 hits), so this
				// is a tripwire rather than a live fix -- but the predicate that validates must be
				// the same one that wipes.
				if (SP->Struct && SP->Struct->IsChildOf(FBlackboardKeySelector::StaticStruct()))
				{
					FBlackboardKeySelector* Sel = static_cast<FBlackboardKeySelector*>(Addr);
					Sel->SelectedKeyName = FName(*KV.Value);
					if (BB)
					{
#if WITH_EDITOR
						// Issue #26812 PR3 -- key existence AND type are validated here, through the
						// SAME predicate the runtime Remote Control path uses (#16913). The gate this
						// replaced, `!Sel->NeedsResolving()`, only asked whether the NAME resolved:
						// ResolveSelectedKey never consults AllowedTypes (BehaviorTreeTypes.cpp:545-564),
						// so a key that exists with a FORBIDDEN type resolved fine and was reported as
						// APPLIED -- and then FBlackboardKeySelector::PreSave rewrote it on save
						// (BehaviorTreeTypes.cpp:681-724, reached via UBTNode::PreSave). That was the
						// last silent partial success on this surface.
						//
						// WillKeyPersist calls ResolveSelectedKey itself, so the transient
						// SelectedKeyID/Type are still populated for this session exactly as before.
						//
						// This is a BEHAVIOUR CHANGE: a bad key now lands in Failed, which Execute's
						// abort gate acts on unless allow_partial_properties=true (Issue #26812 PR2).
						FString Why;
						if (NarshaBlackboardKeyGuard::WillKeyPersist(/*bIsSelector=*/true, Sel, *BB, Why))
						{
							Out.Modified.Add(KV.Key);
						}
						else
						{
							UE_LOG(LogNarshaMCP, Warning,
								TEXT("[BehaviorTreeAuthor] Blackboard key '%s' for property '%s' would not persist: %s"),
								*KV.Value, *KV.Key, *Why);
							Out.Failed.Add(FString::Printf(TEXT("%s='%s' (%s)"), *KV.Key, *KV.Value, *Why));
						}
#else
						// Without WITH_EDITOR the engine's PreSave normalization does not exist, so
						// nothing gets wiped and there is no divergence to report. Keep the
						// pre-PR3 name-only resolve (same reasoning as CURemoteLibrary's non-editor
						// branch). NeedsResolving() is the engine's own "id invalid AND a real name
						// was asked for" predicate (BehaviorTreeTypes.h:685).
						Sel->ResolveSelectedKey(*BB);
						if (!Sel->NeedsResolving())
						{
							Out.Modified.Add(KV.Key);
						}
						else
						{
							Out.Warnings.Add(FString::Printf(
								TEXT("%s='%s' (blackboard key did not resolve; selector left unset)"),
								*KV.Key, *KV.Value));
						}
#endif // WITH_EDITOR
					}
					else
					{
						// Still a WARNING after PR3's key validation, on purpose. UBTNode::PreSave
						// early-returns when GetBlackboardAsset() is null (BTNode.cpp:231), so with
						// no blackboard the engine normalizes NOTHING -- what was asked for is what
						// gets saved, and there is no divergence between the response and the asset.
						// That is the defect this issue is defined against, so this case is not one.
						// (CURemoteLibrary's runtime path declines to validate here for the same
						// reason.) The key still will not bind at runtime, which the warning says.
						UE_LOG(LogNarshaMCP, Warning,
							TEXT("[BehaviorTreeAuthor] Property '%s' is a blackboard key but no blackboard was authored"),
							*KV.Key);
						Out.Warnings.Add(FString::Printf(TEXT("%s (blackboard key with no authored blackboard)"), *KV.Key));
					}
					continue;
				}
			}

			const TCHAR* const Start = *KV.Value;
			FCUImportDiagnostics ImportErrors;
			const TCHAR* const Ret = Prop->ImportText_Direct(Start, Addr, Obj, PPF_None, &ImportErrors);

			const FNumericProperty* NP = CastField<FNumericProperty>(Prop);
			// The engine's integer branch tries UEnum::ParseEnum whenever the FIRST CHARACTER is alpha,
			// and it searches the GLOBAL enum registry -- it does not require the property to be
			// enum-backed (PropertyNumeric.cpp:88-104). So "EBTNodeResult::Succeeded" imports fine even
			// into a plain int32. Keying the exclusion on the property alone would reject that working
			// input; key it on the same thing the engine keys on. Both outcomes of that path are
			// already covered without the grammar rule: ParseEnum failure returns nullptr (rule 1).
			const bool bEngineEnumNamePath = FChar::IsAlpha(**KV.Value);
			const bool bNumericUnparsable =
				NP
				&& (NP->IsFloatingPoint()
						? !IsParsableFloatText(KV.Value)
						: (NP->GetIntPropertyEnum() == nullptr
							&& !bEngineEnumNamePath
							&& !IsParsableIntegerText(KV.Value)));

			// Multiple diagnostics are joined with LINE_TERMINATOR (CRLF on Windows), so flatten both.
			const FString Diagnostic = ImportErrors.TrimStartAndEnd()
				.Replace(TEXT("\r"), TEXT(" "))
				.Replace(TEXT("\n"), TEXT(" "));
			if (ImportErrors.Len() > 0)
			{
				// Passing a device REDIRECTS these away from GWarn -- re-log or they are lost entirely.
				UE_LOG(LogNarshaMCP, Warning, TEXT("[BehaviorTreeAuthor] property '%s': %s"),
					*KV.Key, *Diagnostic);
			}

			if (Ret == nullptr || bNumericUnparsable || ImportErrors.HasError())
			{
				FString Why;
				if (Ret == nullptr)        { Why = TEXT("ImportText rejected the value"); }
				else if (bNumericUnparsable) { Why = TEXT("not a parsable number"); }
				else                       { Why = Diagnostic; }
				Out.Failed.Add(FString::Printf(TEXT("%s='%s' (%s)"), *KV.Key, *KV.Value, *Why));
				continue;
			}
			if (ImportErrors.Len() > 0)
			{
				// Imported, but the engine had something to say (e.g. surplus fixed-array elements were
				// dropped). Surface it without failing the author.
				Out.Warnings.Add(FString::Printf(TEXT("%s='%s' (%s)"), *KV.Key, *KV.Value, *Diagnostic));
			}
			Out.Modified.Add(KV.Key);
		}
		return Out;
	}

	/** Map a blackboard key type name to its UBlackboardKeyType_* class. */
	UClass* ResolveBlackboardKeyTypeClass(const FString& Type)
	{
		if (Type.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Bool::StaticClass();
		if (Type.Equals(TEXT("Int"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Int::StaticClass();
		if (Type.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Float::StaticClass();
		if (Type.Equals(TEXT("Vector"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Vector::StaticClass();
		if (Type.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Rotator::StaticClass();
		if (Type.Equals(TEXT("Object"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Object::StaticClass();
		if (Type.Equals(TEXT("Class"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Class::StaticClass();
		if (Type.Equals(TEXT("Name"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_Name::StaticClass();
		if (Type.Equals(TEXT("String"), ESearchCase::IgnoreCase)) return UBlackboardKeyType_String::StaticClass();
		return nullptr;
	}

	int32 CountExistingGraphNodes(const FString& PackageName)
	{
		const FString AssetName = FPackageName::GetShortName(PackageName);
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *AssetName);
		const UBehaviorTree* Existing = LoadObject<UBehaviorTree>(nullptr, *ObjectPath);
		if (Existing && Existing->BTGraph)
		{
			return Existing->BTGraph->Nodes.Num();
		}
		return 0;
	}

	/** Reject a same-name object of another class before StaticAllocateObject reaches its fatal path. */
	bool ValidateNamedAssetClassForOverwrite(
		UPackage* Package,
		const FString& AssetName,
		const UClass* RequiredClass,
		const TCHAR* AssetKind,
		FString& Err)
	{
		if (!Package || !RequiredClass)
		{
			Err = FString::Printf(TEXT("Cannot validate %s asset class"), AssetKind);
			return false;
		}

		if (UObject* Existing = FindObject<UObject>(Package, *AssetName))
		{
			// Named NewObject only supports in-place reconstruction for the same Class/Outer/Name.
			// A different exact class reaches a fatal replacement path inside StaticAllocateObject.
			if (Existing->GetClass() != RequiredClass)
			{
				Err = FString::Printf(
					TEXT("%s asset name collision at %s.%s: existing object class '%s' differs from required class '%s'"),
					AssetKind,
					*Package->GetName(),
					*AssetName,
					*ClassCppName(Existing->GetClass()),
					*ClassCppName(RequiredClass));
				return false;
			}
		}
		return true;
	}

	/**
	 * BUILD a UBlackboardData asset with the given keys, IN MEMORY ONLY (Phase B.2).
	 *
	 * Issue #26812: this used to save the package too, which happened long before the first node
	 * property was applied -- so a property failure left an orphaned blackboard on disk. The save is
	 * now owned by CUBehaviorTreeWriteFinalize, called from Execute's single commit point once
	 * everything that can fail has succeeded. That makes an aborted author leave nothing behind.
	 *
	 * The #26809 existence gate stays HERE, before anything is created: a refusal must happen before
	 * any state is built, not be unwound afterwards.
	 */
	UBlackboardData* BuildBlackboard(const FString& PackagePath, const TArray<FCUBTBlackboardKeySpec>& Keys, bool bOverwrite, FString& Err)
	{
		if (!FPackageName::IsValidLongPackageName(PackagePath))
		{
			Err = FString::Printf(TEXT("Invalid blackboard package path: %s"), *PackagePath);
			return nullptr;
		}
		if (FPackageName::DoesPackageExist(PackagePath))
		{
			if (!bOverwrite)
			{
				Err = FString::Printf(
					TEXT("Blackboard already exists: %s"),
					*PackagePath);
				return nullptr;
			}
			UE_LOG(LogNarshaMCP, Warning, TEXT("[BehaviorTreeAuthor] Overwriting existing blackboard at %s"), *PackagePath);
		}
		const FString BBName = FPackageName::GetShortName(PackagePath);
		UPackage* BBPackage = CreatePackage(*PackagePath);
		if (!BBPackage)
		{
			Err = FString::Printf(TEXT("CreatePackage failed for blackboard %s"), *PackagePath);
			return nullptr;
		}
		BBPackage->FullyLoad();
		if (!ValidateNamedAssetClassForOverwrite(
			BBPackage, BBName, UBlackboardData::StaticClass(), TEXT("Blackboard"), Err))
		{
			return nullptr;
		}
		// StaticAllocateObject reconstructs an object in place when Class, Outer and Name match.
		// Keep the export loadable: InvalidateExport explicitly makes later loads fail, which races
		// ValidateOnSave reloads in projects such as Lyra when a fixed-name asset is overwritten.
		UBlackboardData* BB =
			NewObject<UBlackboardData>(BBPackage, FName(*BBName), RF_Public | RF_Standalone | RF_Transactional);
		for (const FCUBTBlackboardKeySpec& K : Keys)
		{
			UClass* KeyTypeClass = ResolveBlackboardKeyTypeClass(K.Type);
			if (!KeyTypeClass)
			{
				Err = FString::Printf(TEXT("Unknown blackboard key type '%s' (key '%s')"), *K.Type, *K.Name);
				return nullptr;
			}
			FBlackboardEntry Entry;
			Entry.EntryName = FName(*K.Name);
			Entry.KeyType = NewObject<UBlackboardKeyType>(BB, KeyTypeClass);
			BB->Keys.Add(Entry);
		}
		BB->MarkPackageDirty();
		return BB;
	}

	/** Parse a `properties` JSON object into a string map. */
	void ParsePropsField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TMap<FString, FString>& Out)
	{
		const TSharedPtr<FJsonObject>* PropsObj;
		if (Obj->TryGetObjectField(Field, PropsObj))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& KV : (*PropsObj)->Values)
			{
				Out.Add(KV.Key, JsonValueToImportString(KV.Value));
			}
		}
	}

	/** Reject cycles, multi-parent nodes, orphans, self-refs, and duplicate child links (Issue #26810). */
	bool ValidateSpecTopology(FCUBehaviorTreeSpec& Spec)
	{
		const int32 NodeCount = Spec.Nodes.Num();
		if (NodeCount <= 0)
		{
			return true;
		}

		TMap<FString, const FCUBTNodeSpec*> ById;
		ById.Reserve(NodeCount);
		TMap<FString, int32> Incoming;
		Incoming.Reserve(NodeCount);
		for (const FCUBTNodeSpec& N : Spec.Nodes)
		{
			ById.Add(N.Id, &N);
			Incoming.FindOrAdd(N.Id) = 0;
		}

		for (const FCUBTNodeSpec& N : Spec.Nodes)
		{
			if (N.Children.Num() > NodeCount)
			{
				Spec.ParseError = FString::Printf(TEXT("node '%s' has too many children"), *N.Id);
				return false;
			}
			TSet<FString> SeenOnParent;
			SeenOnParent.Reserve(N.Children.Num());
			for (const FString& Child : N.Children)
			{
				if (Child == N.Id)
				{
					Spec.ParseError = FString::Printf(TEXT("self-reference on node '%s'"), *N.Id);
					return false;
				}
				bool bDupChild = false;
				SeenOnParent.Add(Child, &bDupChild);
				if (bDupChild)
				{
					Spec.ParseError = FString::Printf(
						TEXT("duplicate child '%s' on node '%s'"), *Child, *N.Id);
					return false;
				}
				Incoming.FindOrAdd(Child)++;
			}
		}

		for (const TPair<FString, int32>& Pair : Incoming)
		{
			if (Pair.Value > 1)
			{
				Spec.ParseError = FString::Printf(TEXT("multiple parents for node '%s'"), *Pair.Key);
				return false;
			}
		}

		TArray<TPair<FString, int32>> Frame;
		TSet<FString> Gray;
		TSet<FString> Black;
		if (!Spec.RootChildId.IsEmpty())
		{
			Frame.Add(TPair<FString, int32>(Spec.RootChildId, 0));
			Gray.Add(Spec.RootChildId);
		}
		while (Frame.Num() > 0)
		{
			if (Frame.Num() > NodeCount)
			{
				Spec.ParseError = TEXT("cycle detected in spec tree");
				return false;
			}
			TPair<FString, int32>& Top = Frame.Last();
			const FCUBTNodeSpec* const* Found = ById.Find(Top.Key);
			const TArray<FString>* Kids = (Found && *Found) ? &(*Found)->Children : nullptr;
			const int32 KidCount = Kids ? FMath::Min(Kids->Num(), NodeCount) : 0;
			if (Top.Value < KidCount)
			{
				const FString& Next = (*Kids)[Top.Value++];
				if (Gray.Contains(Next))
				{
					Spec.ParseError = FString::Printf(TEXT("cycle through node '%s'"), *Next);
					return false;
				}
				if (!Black.Contains(Next))
				{
					Gray.Add(Next);
					Frame.Add(TPair<FString, int32>(Next, 0));
				}
			}
			else
			{
				Gray.Remove(Top.Key);
				Black.Add(Top.Key);
				Frame.Pop();
			}
		}

		for (const FCUBTNodeSpec& N : Spec.Nodes)
		{
			if (!Black.Contains(N.Id))
			{
				Spec.ParseError = FString::Printf(
					TEXT("orphan node '%s' is not reachable from root_child"), *N.Id);
				return false;
			}
		}
		return true;
	}

	/** Parse a `decorators`/`services` JSON array into sub-node specs. */
	void ParseSubNodes(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<FCUBTSubNodeSpec>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (!Obj->TryGetArrayField(Field, Arr))
		{
			return;
		}
		for (const TSharedPtr<FJsonValue>& V : *Arr)
		{
			const TSharedPtr<FJsonObject>* SObj;
			if (!V->TryGetObject(SObj))
			{
				continue;
			}
			FCUBTSubNodeSpec S;
			(*SObj)->TryGetStringField(TEXT("class"), S.ClassName);
			ParsePropsField(*SObj, TEXT("properties"), S.Properties);
			if (!S.ClassName.IsEmpty())
			{
				Out.Add(MoveTemp(S));
			}
		}
	}

	/** Parse the tool's `tree` JSON into a FCUBehaviorTreeSpec (with validation). */
	FCUBehaviorTreeSpec ParseSpecJson(const FString& Json)
	{
		FCUBehaviorTreeSpec Spec;
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			Spec.ParseError = TEXT("tree JSON is not a valid object");
			return Spec;
		}
		Root->TryGetStringField(TEXT("root_child"), Spec.RootChildId);

		// Blackboard (Phase B.2): a string path (load existing) OR an object {path, keys:[{name,type}]}.
		if (Root->HasField(TEXT("blackboard")))
		{
			const TSharedPtr<FJsonValue> BBVal = Root->TryGetField(TEXT("blackboard"));
			FString BBStr;
			const TSharedPtr<FJsonObject>* BBObj;
			if (BBVal.IsValid() && BBVal->TryGetString(BBStr) && !BBStr.IsEmpty())
			{
				Spec.BlackboardPath = BBStr;
				Spec.bHasBlackboard = true;
			}
			else if (BBVal.IsValid() && BBVal->TryGetObject(BBObj))
			{
				(*BBObj)->TryGetStringField(TEXT("path"), Spec.BlackboardPath);
				const TArray<TSharedPtr<FJsonValue>>* KeysArr;
				if ((*BBObj)->TryGetArrayField(TEXT("keys"), KeysArr))
				{
					for (const TSharedPtr<FJsonValue>& KV : *KeysArr)
					{
						const TSharedPtr<FJsonObject>* KObj;
						if (!KV->TryGetObject(KObj)) { continue; }
						FCUBTBlackboardKeySpec K;
						(*KObj)->TryGetStringField(TEXT("name"), K.Name);
						(*KObj)->TryGetStringField(TEXT("type"), K.Type);
						if (!K.Name.IsEmpty() && !K.Type.IsEmpty()) { Spec.BlackboardKeys.Add(MoveTemp(K)); }
					}
				}
				if (Spec.BlackboardPath.IsEmpty())
				{
					Spec.ParseError = TEXT("blackboard object needs a 'path'");
					return Spec;
				}
				Spec.bHasBlackboard = true;
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* NodesArr;
		if (!Root->TryGetArrayField(TEXT("nodes"), NodesArr))
		{
			Spec.ParseError = TEXT("tree JSON missing 'nodes' array");
			return Spec;
		}
		for (const TSharedPtr<FJsonValue>& V : *NodesArr)
		{
			const TSharedPtr<FJsonObject>* NObj;
			if (!V->TryGetObject(NObj))
			{
				continue;
			}
			FCUBTNodeSpec N;
			(*NObj)->TryGetStringField(TEXT("id"), N.Id);
			(*NObj)->TryGetStringField(TEXT("kind"), N.Kind);
			(*NObj)->TryGetStringField(TEXT("class"), N.ClassName);
			double X = 0.0, Y = 0.0;
			(*NObj)->TryGetNumberField(TEXT("x"), X);
			(*NObj)->TryGetNumberField(TEXT("y"), Y);
			N.X = static_cast<int32>(X);
			N.Y = static_cast<int32>(Y);
			if (N.Kind.IsEmpty())
			{
				N.Kind = TEXT("task");
			}
			const TArray<TSharedPtr<FJsonValue>>* ChArr;
			if ((*NObj)->TryGetArrayField(TEXT("children"), ChArr))
			{
				for (const TSharedPtr<FJsonValue>& C : *ChArr)
				{
					FString ChildId;
					if (C->TryGetString(ChildId))
					{
						N.Children.Add(ChildId);
					}
				}
			}
			ParsePropsField(*NObj, TEXT("properties"), N.Properties);
			ParseSubNodes(*NObj, TEXT("decorators"), N.Decorators);
			ParseSubNodes(*NObj, TEXT("services"), N.Services);

			if (N.Id.IsEmpty() || N.ClassName.IsEmpty())
			{
				Spec.ParseError = TEXT("every node needs a non-empty 'id' and 'class'");
				return Spec;
			}
			if (N.Kind != TEXT("composite") && N.Kind != TEXT("task"))
			{
				Spec.ParseError = FString::Printf(TEXT("unknown kind '%s' on node '%s'"), *N.Kind, *N.Id);
				return Spec;
			}
			Spec.Nodes.Add(MoveTemp(N));
		}

		// Validation: unique ids, children/root_child reference existing ids, tasks have no children.
		TSet<FString> Ids;
		for (const FCUBTNodeSpec& N : Spec.Nodes)
		{
			bool bDup = false;
			Ids.Add(N.Id, &bDup);
			if (bDup)
			{
				Spec.ParseError = FString::Printf(TEXT("duplicate node id '%s'"), *N.Id);
				return Spec;
			}
			if (N.Kind != TEXT("composite") && N.Children.Num() > 0)
			{
				Spec.ParseError = FString::Printf(TEXT("task node '%s' cannot have children"), *N.Id);
				return Spec;
			}
		}
		if (!Spec.RootChildId.IsEmpty() && !Ids.Contains(Spec.RootChildId))
		{
			Spec.ParseError = FString::Printf(TEXT("root_child '%s' is not a declared node"), *Spec.RootChildId);
			return Spec;
		}
		for (const FCUBTNodeSpec& N : Spec.Nodes)
		{
			for (const FString& Child : N.Children)
			{
				if (!Ids.Contains(Child))
				{
					Spec.ParseError = FString::Printf(TEXT("node '%s' references unknown child '%s'"), *N.Id, *Child);
					return Spec;
				}
			}
		}
		// Default root_child to the first node when omitted (convenience).
		if (Spec.RootChildId.IsEmpty() && Spec.Nodes.Num() > 0)
		{
			Spec.RootChildId = Spec.Nodes[0].Id;
		}
		if (!ValidateSpecTopology(Spec))
		{
			return Spec;
		}
		return Spec;
	}

	/** Back-compat default: a Selector→Wait tree using the legacy position params. */
	FCUBehaviorTreeSpec BuildDefaultSpec(const FCUBehaviorTreeAuthorParams& In)
	{
		FCUBehaviorTreeSpec Spec;
		FCUBTNodeSpec Comp;
		Comp.Id = TEXT("root_selector");
		Comp.Kind = TEXT("composite");
		Comp.ClassName = TEXT("BTComposite_Selector");
		Comp.X = In.CompositePosX;
		Comp.Y = In.CompositePosY;
		Comp.Children.Add(TEXT("wait_task"));
		FCUBTNodeSpec Task;
		Task.Id = TEXT("wait_task");
		Task.Kind = TEXT("task");
		Task.ClassName = TEXT("BTTask_Wait");
		Task.X = In.TaskPosX;
		Task.Y = In.TaskPosY;
		Spec.Nodes.Add(MoveTemp(Comp));
		Spec.Nodes.Add(MoveTemp(Task));
		Spec.RootChildId = TEXT("root_selector");
		return Spec;
	}

	/** Create a composite/task graph node from a spec (NodeInstance + position + properties). */
	UBehaviorTreeGraphNode* CreateGraphNode(UBehaviorTreeGraph* Graph, const FCUBTNodeSpec& N, UBlackboardData* BB,
		FString& Err, FCUBehaviorTreeAuthorResult& Out)
	{
		UClass* Cls = ResolveNodeClass(N.ClassName);
		if (!Cls)
		{
			Err = FString::Printf(TEXT("Unknown node class '%s' (node '%s')"), *N.ClassName, *N.Id);
			return nullptr;
		}
		const UClass* Required = (N.Kind == TEXT("composite"))
			? UBTCompositeNode::StaticClass()
			: UBTTaskNode::StaticClass();
		if (!CheckClassAgainstBase(Cls, Required, N.Id, N.Kind, Err))
		{
			return nullptr;
		}
		UBehaviorTreeGraphNode* GN = nullptr;
		if (N.Kind == TEXT("composite"))
		{
			FGraphNodeCreator<UBehaviorTreeGraphNode_Composite> Creator(*Graph);
			UBehaviorTreeGraphNode_Composite* Node = Creator.CreateNode(false);
			Node->ClassData = FGraphNodeClassData(Cls, FString());
			Node->NodePosX = N.X;
			Node->NodePosY = N.Y;
			Creator.Finalize();
			GN = Node;
		}
		else
		{
			FGraphNodeCreator<UBehaviorTreeGraphNode_Task> Creator(*Graph);
			UBehaviorTreeGraphNode_Task* Node = Creator.CreateNode(false);
			Node->ClassData = FGraphNodeClassData(Cls, FString());
			Node->NodePosX = N.X;
			Node->NodePosY = N.Y;
			Creator.Finalize();
			GN = Node;
		}
		if (!GN->NodeInstance)
		{
			Err = FString::Printf(TEXT("NodeInstance not created for node '%s' (class %s)"), *N.Id, *N.ClassName);
			return nullptr;
		}
		// Issue #26814: persist spec id on the runtime node so Patch can target it later.
		if (UBTNode* Inst = Cast<UBTNode>(GN->NodeInstance))
		{
			if (!N.Id.IsEmpty())
			{
				Inst->NodeName = FName(*N.Id).ToString();
			}
		}
		AccumulateProps(Out, ApplyProperties(GN->NodeInstance, N.Properties, BB));
		return GN;
	}

	/** Attach a decorator/service sub-node to a parent graph node via the editor's AddSubNode path. */
	bool AddSubNodeFromSpec(UBehaviorTreeGraph* Graph, UBehaviorTreeGraphNode* Parent,
		const FCUBTSubNodeSpec& S, bool bDecorator, UBlackboardData* BB, FString& Err,
		FCUBehaviorTreeAuthorResult& Out)
	{
		UClass* Cls = ResolveNodeClass(S.ClassName);
		if (!Cls)
		{
			Err = FString::Printf(TEXT("Unknown %s class '%s'"), bDecorator ? TEXT("decorator") : TEXT("service"), *S.ClassName);
			return false;
		}
		const UClass* Required = bDecorator
			? UBTDecorator::StaticClass()
			: UBTService::StaticClass();
		if (!CheckClassAgainstBase(
			Cls, Required, Parent ? Parent->GetName() : FString(),
			bDecorator ? FString(TEXT("decorator")) : FString(TEXT("service")), Err))
		{
			return false;
		}
		UBehaviorTreeGraphNode* Sub = bDecorator
			? static_cast<UBehaviorTreeGraphNode*>(NewObject<UBehaviorTreeGraphNode_Decorator>(Graph))
			: static_cast<UBehaviorTreeGraphNode*>(NewObject<UBehaviorTreeGraphNode_Service>(Graph));
		Sub->ClassData = FGraphNodeClassData(Cls, FString());
		// AddSubNode: Rename to graph outer, PostPlacedNewNode (creates NodeInstance from ClassData),
		// AllocateDefaultPins, then OnSubNodeAdded routes Sub into Parent->Decorators/Services.
		Parent->AddSubNode(Sub, Graph);
		if (Sub->NodeInstance)
		{
			AccumulateProps(Out, ApplyProperties(Sub->NodeInstance, S.Properties, BB));
		}
		return true;
	}

	bool LinkPins(UEdGraphPin* From, UEdGraphPin* To)
	{
		if (From && To)
		{
			From->MakeLinkTo(To);
			return true;
		}
		return false;
	}
}

using namespace CUBehaviorTreeAuthorPrivate;

bool FCUBehaviorTreeAuthor::Execute(const FCUBehaviorTreeAuthorParams& In, FCUBehaviorTreeAuthorResult& Out)
{
	Out = FCUBehaviorTreeAuthorResult{};

#if WITH_EDITORONLY_DATA
	if (In.BTPackagePath.IsEmpty())
	{
		Out.Error = TEXT("BTPackagePath is empty");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}
	if (!FPackageName::IsValidLongPackageName(In.BTPackagePath))
	{
		Out.Error = FString::Printf(TEXT("Invalid long package name: %s"), *In.BTPackagePath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}

	// Resolve the spec FIRST so a bad spec fails before we create a package.
	FCUBehaviorTreeSpec Spec = In.SpecJson.IsEmpty() ? BuildDefaultSpec(In) : ParseSpecJson(In.SpecJson);
	if (!Spec.IsValid())
	{
		Out.Error = FString::Printf(TEXT("Invalid tree spec: %s"),
			Spec.ParseError.IsEmpty() ? TEXT("empty nodes or root_child") : *Spec.ParseError);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}

	// Issue #26811: kind/class hierarchy + abstract/deprecated, before CreatePackage.
	{
		FString ClassErr;
		if (!ValidateSpecNodeClasses(Spec, ClassErr))
		{
			Out.Error = ClassErr;
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
	}

	const FString PackageName = In.BTPackagePath;
	const FString AssetName = FPackageName::GetShortName(PackageName);
	if (FPackageName::DoesPackageExist(PackageName))
	{
		Out.ExistingGraphNodeCount = CountExistingGraphNodes(PackageName);
		if (!In.bOverwrite)
		{
			Out.Error = FString::Printf(
				TEXT("BehaviorTree already exists: %s"),
				*PackageName);
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
		Out.bOverwrote = true;
		UE_LOG(LogNarshaMCP, Warning,
			TEXT("[BehaviorTreeAuthor] Overwriting existing asset at %s (ExistingGraphNodeCount=%d)"),
			*PackageName, Out.ExistingGraphNodeCount);
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		Out.Error = FString::Printf(TEXT("CreatePackage failed for %s"), *PackageName);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}
	Package->FullyLoad();
	if (!ValidateNamedAssetClassForOverwrite(
		Package, AssetName, UBehaviorTree::StaticClass(), TEXT("BehaviorTree"), Out.Error))
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}

	// Same Class/Outer/Name is the engine-supported in-place replacement path. Do not invalidate
	// the export: asynchronous validation may reload the just-saved package before this call returns.
	UBehaviorTree* BTAsset =
		NewObject<UBehaviorTree>(Package, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!BTAsset)
	{
		Out.Error = TEXT("NewObject<UBehaviorTree> returned null");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}

	// ── Build the EDITOR graph (the reflection-blocked area) ──
	BTAsset->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
		BTAsset, TEXT("Behavior Tree"),
		UBehaviorTreeGraph::StaticClass(),
		UEdGraphSchema_BehaviorTree::StaticClass());
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BTAsset->BTGraph);
	if (!Graph)
	{
		Out.Error = TEXT("CreateNewGraph did not return a UBehaviorTreeGraph");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}
	Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph); // spawns UBehaviorTreeGraphNode_Root
	Graph->OnCreated();

	UBehaviorTreeGraphNode_Root* RootEdNode = FindRootEdNode(Graph);
	if (!RootEdNode)
	{
		Out.Error = TEXT("No UBehaviorTreeGraphNode_Root after CreateDefaultNodesForGraph");
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}

	// ── Blackboard (Phase B.2): create-with-keys or load-existing, then bind to the BT so
	// FBlackboardKeySelector node properties resolve and the editor shows valid keys. ──
	UBlackboardData* BB = nullptr;
	// Issue #26812: only a blackboard WE built is ours to persist; the load-existing branch must not
	// be re-saved.
	bool bBuiltBlackboard = false;
	if (Spec.bHasBlackboard)
	{
		if (Spec.BlackboardKeys.Num() > 0)
		{
			BB = BuildBlackboard(Spec.BlackboardPath, Spec.BlackboardKeys, In.bOverwrite, Out.Error);
			bBuiltBlackboard = (BB != nullptr);
		}
		else
		{
			const FString BBName = FPackageName::GetShortName(Spec.BlackboardPath);
			BB = LoadObject<UBlackboardData>(nullptr, *FString::Printf(TEXT("%s.%s"), *Spec.BlackboardPath, *BBName));
			if (!BB)
			{
				Out.Error = FString::Printf(TEXT("Failed to load blackboard %s"), *Spec.BlackboardPath);
			}
		}
		if (!BB)
		{
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
	}
	// NewObject reconstructs a same-name asset in place during overwrite. Explicitly assign the
	// optional reference even when the new full-author spec has no Blackboard, otherwise the old
	// asset's reference survives in memory while SavePackage writes the new no-Blackboard state.
	BTAsset->BlackboardAsset = BB;
	Out.BlackboardKeyCount = BB ? BB->GetNumKeys() : 0;

	// ── Materialise every spec node, keyed by id ──
	TMap<FString, UBehaviorTreeGraphNode*> NodesById;
	for (const FCUBTNodeSpec& N : Spec.Nodes)
	{
		UBehaviorTreeGraphNode* GN = CreateGraphNode(Graph, N, BB, Out.Error, Out);
		if (!GN)
		{
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
		NodesById.Add(N.Id, GN);
	}

	// ── Wire Root → root_child, then each composite → its ordered children ──
	UBehaviorTreeGraphNode* RootChild = NodesById.FindRef(Spec.RootChildId);
	if (!RootChild)
	{
		Out.Error = FString::Printf(TEXT("Failed to link Root to root_child '%s'"), *Spec.RootChildId);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}
	// Issue #27138: engine BTGraphHelpers::CreateChildren AVs (read 0x68) when
	// Root's only child is a task. A task-rooted spec is valid (OverwriteGate
	// MinimalSpec); wrap it in an implicit Selector so UpdateAsset can sync.
	const FCUBTNodeSpec* RootChildSpec = Spec.Nodes.FindByPredicate(
		[&Spec](const FCUBTNodeSpec& Node) { return Node.Id == Spec.RootChildId; });
	UBehaviorTreeGraphNode* RootLinkTarget = RootChild;
	if (RootChildSpec && RootChildSpec->Kind != TEXT("composite"))
	{
		FCUBTNodeSpec ImplicitRoot;
		ImplicitRoot.Id = TEXT("__narsha_implicit_root_selector");
		ImplicitRoot.Kind = TEXT("composite");
		ImplicitRoot.ClassName = TEXT("BTComposite_Selector");
		UBehaviorTreeGraphNode* WrapNode = CreateGraphNode(Graph, ImplicitRoot, BB, Out.Error, Out);
		if (!WrapNode)
		{
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
		if (!LinkPins(WrapNode->GetOutputPin(), RootChild->GetInputPin()))
		{
			Out.Error = FString::Printf(
				TEXT("Failed to wrap task-rooted root_child '%s' under implicit Selector"),
				*Spec.RootChildId);
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
		RootLinkTarget = WrapNode;
		UE_LOG(LogNarshaMCP, Display,
			TEXT("[BehaviorTreeAuthor] Implicit Selector wrap for task-rooted spec (root_child='%s')"),
			*Spec.RootChildId);
	}
	if (!LinkPins(RootEdNode->GetOutputPin(), RootLinkTarget->GetInputPin()))
	{
		Out.Error = FString::Printf(TEXT("Failed to link Root to root_child '%s'"), *Spec.RootChildId);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}
	for (const FCUBTNodeSpec& N : Spec.Nodes)
	{
		// FindRef (not operator[]) so a map miss returns null instead of inserting a default —
		// matches the RootChild lookup above and keeps a guaranteed-present id from ever crashing.
		UBehaviorTreeGraphNode* Parent = NodesById.FindRef(N.Id);
		if (!Parent)
		{
			Out.Error = FString::Printf(TEXT("Internal: node '%s' not materialised"), *N.Id);
			UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
			return false;
		}
		for (const FString& ChildId : N.Children)
		{
			UBehaviorTreeGraphNode* Child = NodesById.FindRef(ChildId);
			if (!Child || !LinkPins(Parent->GetOutputPin(), Child->GetInputPin()))
			{
				Out.Error = FString::Printf(TEXT("Failed to link '%s' → '%s' (missing node or pins)"), *N.Id, *ChildId);
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
				return false;
			}
		}
		// Decorators + services (sub-nodes) on this node.
		for (const FCUBTSubNodeSpec& D : N.Decorators)
		{
			if (!AddSubNodeFromSpec(Graph, Parent, D, /*bDecorator=*/true, BB, Out.Error, Out))
			{
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
				return false;
			}
		}
		for (const FCUBTSubNodeSpec& S : N.Services)
		{
			if (!AddSubNodeFromSpec(Graph, Parent, S, /*bDecorator=*/false, BB, Out.Error, Out))
			{
				UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
				return false;
			}
		}
	}

	// ── Issue #26812: (by default) STOP before writing anything. The property arrays are already
	// populated -- AccumulateProps writes straight into Out -- so every earlier failure return above
	// also reports what had been evaluated so far. ──
	if (Out.FailedProperties.Num() > 0 && !In.bAllowPartialProperties)
	{
		Out.Error = FString::Printf(TEXT("%d property/properties could not be applied: %s"),
			Out.FailedProperties.Num(), *FString::Join(Out.FailedProperties, TEXT("; ")));
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		// Nothing has been written to DISK at this point -- the blackboard save was deferred to the
		// commit point below -- so no rollback is needed. NOTE: in-memory state is NOT unwound: the
		// packages exist, and named NewObject may already have reconstructed a top-level asset in place
		// and marked its package dirty.
		// In a live editor an aborted author therefore leaves mutated in-memory state and a dirty
		// package a later Save All would write. Unwinding that is a separate concern from this issue.
		return false;
	}

	CUBehaviorTreeWriteFinalize::FInputs FinalizeInputs;
	FinalizeInputs.BehaviorTree = BTAsset;
	FinalizeInputs.Blackboard = BB;
	FinalizeInputs.BehaviorTreePackagePath = PackageName;
	FinalizeInputs.BlackboardPackagePath = Spec.BlackboardPath;
	FinalizeInputs.bSaveBlackboard = bBuiltBlackboard;
	FinalizeInputs.bRegisterBehaviorTree = true;
	FinalizeInputs.bRegisterBlackboard = bBuiltBlackboard;
	if (!CUBehaviorTreeWriteFinalize::CompileSaveAndVerify(FinalizeInputs, Out))
	{
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
		return false;
	}

	UE_LOG(LogNarshaMCP, Display,
		TEXT("[BehaviorTreeAuthor] Authored %s (spec_nodes=%d, graph_nodes=%d, runtime_root=%s, bb_keys=%d) -> %s"),
		*PackageName, Spec.Nodes.Num(), Out.NumGraphNodes, *Out.RuntimeRootClass, Out.BlackboardKeyCount, *Out.SavedFilePath);
	return true;
#else
	Out.Error = TEXT("BehaviorTree editor-graph authoring requires WITH_EDITORONLY_DATA (editor build)");
	UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor] %s"), *Out.Error);
	return false;
#endif // WITH_EDITORONLY_DATA
}

bool FCUBehaviorTreeAuthor::Verify(const FString& BTPackagePath, FCUBehaviorTreeAuthorVerifyResult& Out)
{
	Out = FCUBehaviorTreeAuthorVerifyResult{};

#if WITH_EDITORONLY_DATA
	if (BTPackagePath.IsEmpty() || !FPackageName::IsValidLongPackageName(BTPackagePath))
	{
		Out.Error = FString::Printf(TEXT("Invalid long package name: %s"), *BTPackagePath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeVerify] %s"), *Out.Error);
		return false;
	}

	const FString AssetName = FPackageName::GetShortName(BTPackagePath);
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *BTPackagePath, *AssetName);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *ObjectPath);
	if (!BT)
	{
		Out.Error = FString::Printf(TEXT("LoadObject<UBehaviorTree> failed for %s"), *ObjectPath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeVerify] %s"), *Out.Error);
		return false;
	}
	Out.bLoaded = true;
	Out.bHasEditorGraph = (BT->BTGraph != nullptr);
	Out.BlackboardKeyCount = BT->BlackboardAsset ? BT->BlackboardAsset->GetNumKeys() : -1;

	if (BT->BTGraph)
	{
		Out.NumGraphNodes = BT->BTGraph->Nodes.Num();
		for (UEdGraphNode* Node : BT->BTGraph->Nodes)
		{
			int32 LinkedPins = 0;
			for (UEdGraphPin* Pin : Node->Pins)
			{
				LinkedPins += Pin->LinkedTo.Num();
			}
			int32 SubNodes = 0;
			if (UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(Node))
			{
				SubNodes = BTNode->Decorators.Num() + BTNode->Services.Num();
			}
			Out.GraphNodeSummaries.Add(FString::Printf(
				TEXT("%s @ (%d,%d) pins=%d linked=%d subnodes=%d"),
				*Node->GetClass()->GetName(), Node->NodePosX, Node->NodePosY,
				Node->Pins.Num(), LinkedPins, SubNodes));
		}
	}

	const auto DescribeRoot = [](UBehaviorTree* InBT, FString& OutClass, int32& OutChildCount, FString& OutFirstChild)
	{
		UBTCompositeNode* Root = InBT->RootNode;
		OutClass = Root ? Root->GetClass()->GetName() : TEXT("None");
		OutChildCount = Root ? Root->GetChildrenNum() : 0;
		OutFirstChild = TEXT("None");
		if (Root && Root->GetChildrenNum() > 0)
		{
			if (UBTNode* Child = Root->GetChildNode(0))
			{
				OutFirstChild = Child->GetClass()->GetName();
			}
		}
	};
	DescribeRoot(BT, Out.RootNodeClass, Out.RootChildCount, Out.FirstChildClass);

	FString DummyChild;
	if (UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph))
	{
		Graph->UpdateAsset();
	}
	DescribeRoot(BT, Out.RootNodeClassAfterRebuild, Out.RootChildCountAfterRebuild, DummyChild);

	Out.bSurvivesRecompile =
		(Out.RootNodeClassAfterRebuild == Out.RootNodeClass) &&
		(Out.RootChildCountAfterRebuild == Out.RootChildCount) &&
		(Out.RootNodeClass != TEXT("None"));

	UE_LOG(LogNarshaMCP, Display,
		TEXT("[BehaviorTreeVerify] %s: graph_nodes=%d root=%s children=%d -> after-rebuild root=%s children=%d survives=%s"),
		*BTPackagePath, Out.NumGraphNodes, *Out.RootNodeClass, Out.RootChildCount,
		*Out.RootNodeClassAfterRebuild, Out.RootChildCountAfterRebuild,
		Out.bSurvivesRecompile ? TEXT("true") : TEXT("false"));
	return Out.bSurvivesRecompile;
#else
	Out.Error = TEXT("Verify requires WITH_EDITORONLY_DATA (editor build)");
	return false;
#endif // WITH_EDITORONLY_DATA
}

bool FCUBehaviorTreeAuthor::AuthorDecoratorOpsFixture(const FString& BTPackagePath, FCUBehaviorTreeAuthorResult& Out)
{
	Out = FCUBehaviorTreeAuthorResult{};

#if WITH_EDITORONLY_DATA
	// Issue #21226: real-bytes DecoratorOps GT fixture. Plain decorator sub-nodes get
	// DecoratorOperations.Reset() at CollectDecorators (BehaviorTreeGraph.cpp ~411) — only a
	// UBehaviorTreeGraphNode_CompositeDecorator produces a non-empty FBTCompositeChild.DecoratorOps
	// stream. Build Root→Selector→Wait, then attach Composite Decorator with
	// `(KeyA AND KeyB) OR NOT KeyC` via SpawnMissingNodes (the reverse of the editor's own
	// graph→ops compile path).
	if (BTPackagePath.IsEmpty() || !FPackageName::IsValidLongPackageName(BTPackagePath))
	{
		Out.Error = FString::Printf(TEXT("Invalid BT package path: %s"), *BTPackagePath);
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	// Companion blackboard lives next to the BT: .../BT_Foo → .../BB_Foo.
	const FString AssetName = FPackageName::GetShortName(BTPackagePath);
	FString BBName = AssetName;
	if (BBName.StartsWith(TEXT("BT_")))
	{
		BBName = FString(TEXT("BB_")) + BBName.Mid(3);
	}
	else
	{
		BBName = FString(TEXT("BB_")) + BBName;
	}
	const FString BBPackagePath = FPackageName::GetLongPackagePath(BTPackagePath) / BBName;

	// Issue #26812: the Wait node carried `"properties": {"WaitTime": "0.1"}`. WaitTime is
	// FValueOrBBKey_Float, so a bare scalar is not valid struct text -- UScriptStruct::ImportText
	// rejects it ("Missing opening parenthesis"), returns nullptr, and leaves the value at the CDO
	// default 5.0f (BTTask_Wait.cpp:10), which delta serialisation then omits entirely. The key
	// therefore contributed nothing to the authored asset, and once property failures abort by
	// default it would break this fixture. Setting it for real needs struct-text support for
	// FValueOrBBKey_* -- deferred to a follow-up issue.
	// 1) Author base topology + blackboard via the existing arbitrary-spec path.
	const FString SpecJson = FString::Printf(
		TEXT(R"({
			"blackboard": {
				"path": "%s",
				"keys": [
					{"name": "KeyA", "type": "Bool"},
					{"name": "KeyB", "type": "Bool"},
					{"name": "KeyC", "type": "Bool"}
				]
			},
			"root_child": "sel",
			"nodes": [
				{
					"id": "sel",
					"kind": "composite",
					"class": "BTComposite_Selector",
					"x": 0, "y": 200,
					"children": ["wait"]
				},
				{
					"id": "wait",
					"kind": "task",
					"class": "BTTask_Wait",
					"x": 0, "y": 400
				}
			]
		})"),
		*BBPackagePath);

	FCUBehaviorTreeAuthorParams BaseParams;
	BaseParams.BTPackagePath = BTPackagePath;
	BaseParams.SpecJson = SpecJson;
	BaseParams.bOverwrite = true; // fixture re-runs replace the same on-disk asset (Issue #26809)
	if (!Execute(BaseParams, Out) || !Out.bSuccess)
	{
		// Out.Error already set by Execute.
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] base author failed: %s"), *Out.Error);
		return false;
	}

	// 2) Reload the just-saved BT so we attach Composite Decorator against durable package state.
	const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *BTPackagePath, *AssetName);
	UBehaviorTree* BT = LoadObject<UBehaviorTree>(nullptr, *ObjectPath);
	if (!BT)
	{
		Out.Error = FString::Printf(TEXT("Reload failed after base author: %s"), *ObjectPath);
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}
	UBehaviorTreeGraph* Graph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!Graph)
	{
		Out.Error = TEXT("BTGraph null after base author — cannot attach Composite Decorator");
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	// Find the Wait task graph node (child of Selector) to hang the Composite Decorator on.
	UBehaviorTreeGraphNode* WaitGraphNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UBehaviorTreeGraphNode* BTNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (!BTNode || !BTNode->NodeInstance)
		{
			continue;
		}
		if (BTNode->NodeInstance->IsA(UBTTask_Wait::StaticClass()))
		{
			WaitGraphNode = BTNode;
			break;
		}
	}
	if (!WaitGraphNode)
	{
		Out.Error = TEXT("Could not find BTTask_Wait graph node to attach Composite Decorator");
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	// 3) Add Composite Decorator sub-node (PostPlacedNewNode → CreateBoundGraph with Sink root).
	UBehaviorTreeGraphNode_CompositeDecorator* CompDec =
		NewObject<UBehaviorTreeGraphNode_CompositeDecorator>(Graph);
	CompDec->CompositeName = TEXT("DecoratorOpsTest");
	CompDec->bShowOperations = true;
	// AddSubNode routes into Parent->Decorators and may call UpdateAsset early (empty ops OK).
	WaitGraphNode->AddSubNode(CompDec, Graph);
	if (!CompDec->GetBoundGraph())
	{
		Out.Error = TEXT("Composite Decorator BoundGraph was not created by PostPlacedNewNode");
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	// 4) Three Blackboard decorator instances (KeyA/KeyB/KeyC IsSet) — outer = BT so CollectDecorators
	//    does not need to Rename them off a transient outer. BlackboardKey is protected on
	//    UBTDecorator_BlackboardBase → bind via reflection (same path as ApplyProperties).
	auto MakeBBDecorator = [BT, &Out](const TCHAR* KeyName) -> UBTDecorator_Blackboard*
	{
		UBTDecorator_Blackboard* Dec = NewObject<UBTDecorator_Blackboard>(BT);
		if (!Dec)
		{
			return nullptr;
		}
		// UBTDecorator_Blackboard redeclares InitializeFromAsset as protected; call via UBTNode*
		// (public base declaration) so the virtual override still runs.
		static_cast<UBTNode*>(Dec)->InitializeFromAsset(*BT);
		TMap<FString, FString> Props;
		Props.Add(TEXT("BlackboardKey"), FString(KeyName));
		AccumulateProps(Out, ApplyProperties(Dec, Props, BT->BlackboardAsset));
		// These properties are applied AFTER Execute returned, so Execute's abort gate has already run.
		// Re-check, or this path could report success:true with a non-empty failed_properties and
		// contradict the contract.
		// Issue #26812 PR3 made this REACHABLE: a key selector whose key is missing or type-forbidden
		// now lands in Failed. It does not fire for THIS fixture (KeyA/KeyB/KeyC all exist and
		// UBTDecorator_BlackboardBase leaves AllowedTypes empty -- "allow everything",
		// BTDecorator_BlackboardBase.cpp:16 -- so WillKeyPersist keeps them), but it is no longer
		// dead code, and unlike Execute this path has NO allow_partial_properties opt-out.
		if (Out.FailedProperties.Num() > 0)
		{
			UE_LOG(LogNarshaMCP, Error,
				TEXT("[BehaviorTreeAuthor/DecoratorOps] decorator property failed: %s"),
				*FString::Join(Out.FailedProperties, TEXT("; ")));
			return nullptr;
		}
		// BasicOperation IsSet (0) is the default for Bool keys — leave defaults.
		return Dec;
	};

	TArray<UBTDecorator*> Instances;
	UBTDecorator_Blackboard* DecA = MakeBBDecorator(TEXT("KeyA"));
	UBTDecorator_Blackboard* DecB = MakeBBDecorator(TEXT("KeyB"));
	UBTDecorator_Blackboard* DecC = MakeBBDecorator(TEXT("KeyC"));
	if (!DecA || !DecB || !DecC)
	{
		Out.Error = TEXT("Failed to NewObject UBTDecorator_Blackboard instances");
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}
	Instances.Add(DecA);
	Instances.Add(DecB);
	Instances.Add(DecC);

	// 5) Ops stream for `(A AND B) OR NOT C` — pre-order, matching CollectDecoratorDataWorker:
	//    Or(2), And(2), Test(0), Test(1), Not(1), Test(2)
	TArray<FBTDecoratorLogic> Ops;
	Ops.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::Or), 2));
	Ops.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::And), 2));
	Ops.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::Test), 0));
	Ops.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::Test), 1));
	Ops.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::Not), 1));
	Ops.Add(FBTDecoratorLogic(static_cast<uint8>(EBTDecoratorLogic::Test), 2));

	const int32 NextIdx = CompDec->SpawnMissingNodes(Instances, Ops, 0);
	if (NextIdx < Ops.Num())
	{
		Out.Error = FString::Printf(
			TEXT("SpawnMissingNodes under-consumed ops (next=%d, ops=%d) — BoundGraph may lack Sink"),
			NextIdx, Ops.Num());
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	// 6) Recompile graph → runtime so Children[slot].DecoratorOps is populated, then re-save.
	Graph->UpdateAsset();

	// Prove non-empty DecoratorOps on at least one child before claiming success.
	bool bHasOps = false;
	int32 OpsCount = 0;
	if (BT->RootNode)
	{
		for (int32 Ci = 0; Ci < BT->RootNode->Children.Num(); ++Ci)
		{
			const FBTCompositeChild& Child = BT->RootNode->Children[Ci];
			if (Child.DecoratorOps.Num() > 0)
			{
				bHasOps = true;
				OpsCount = Child.DecoratorOps.Num();
				break;
			}
		}
	}
	if (!bHasOps)
	{
		Out.Error = TEXT("Composite Decorator attached but runtime DecoratorOps is still empty after UpdateAsset");
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	BT->MarkPackageDirty();
	UPackage* Package = BT->GetOutermost();
	const FString FileName =
		FPackageName::LongPackageNameToFilename(BTPackagePath, FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	if (!UPackage::SavePackage(Package, BT, *FileName, SaveArgs))
	{
		Out.Error = FString::Printf(TEXT("SavePackage failed for DecoratorOps fixture: %s"), *FileName);
		Out.bSuccess = false;
		UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
		return false;
	}

	Out.SavedFilePath = FileName;
	Out.NumGraphNodes = Graph->Nodes.Num();
	Out.bHasEditorGraph = true;
	Out.RuntimeRootClass = BT->RootNode ? BT->RootNode->GetClass()->GetName() : TEXT("None");
	Out.BlackboardKeyCount = BT->BlackboardAsset ? BT->BlackboardAsset->GetNumKeys() : -1;
	Out.bSuccess = true;
	Out.Error.Empty();
	UE_LOG(LogNarshaMCP, Display,
		TEXT("[BehaviorTreeAuthor/DecoratorOps] Authored %s (decorator_ops=%d on child slot, graph_nodes=%d) -> %s"),
		*BTPackagePath, OpsCount, Out.NumGraphNodes, *FileName);
	return true;
#else
	Out.Error = TEXT("DecoratorOps fixture authoring requires WITH_EDITORONLY_DATA (editor build)");
	UE_LOG(LogNarshaMCP, Error, TEXT("[BehaviorTreeAuthor/DecoratorOps] %s"), *Out.Error);
	return false;
#endif // WITH_EDITORONLY_DATA
}
