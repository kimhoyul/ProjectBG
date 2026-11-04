// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27939: side-effect-free StateTree spec preflight (dry-run == apply).

#include "CUStateTreeAuthor.h"

#if WITH_NARSHAMCP_STATETREE && WITH_NARSHAMCP_PROPERTYBINDINGUTILS
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorTypes.h"
#include "StateTreeState.h"

namespace
{
	const TCHAR* CodeFieldNotArray = TEXT("field_not_array");
	const TCHAR* CodeRowNotObject = TEXT("row_not_object");
	const TCHAR* CodeMissingRequired = TEXT("missing_required_field");
	const TCHAR* CodeDuplicateAlias = TEXT("duplicate_alias");
	const TCHAR* CodeUnresolved = TEXT("unresolved_reference");
	const TCHAR* CodeOwnerMismatch = TEXT("owner_mismatch");
	const TCHAR* CodeBindingUnprefixed = TEXT("binding_target_unprefixed");

	struct FPlannedTransition
	{
		FString OwnerKey;
		FString TargetName;
		FString TargetKey;
		FString GuidSelector;
	};

	/**
	 * Read-only mirror of the state/transition surface that the runtime resolvers see after the
	 * spec's states and transitions have been appended. It exists for two compatibility reasons:
	 * create-or-open specs may refer to existing objects, and condition.transition accepts a GUID,
	 * a global digits-only index, or a unique target-state name (not just author aliases).
	 */
	struct FExistingValidationContext
	{
		static const FString RootKey;

		TMap<FString, FString> StateIdToKey;
		TMap<FString, TArray<FString>> StateNameToKeys;
		TMap<FString, FString> StateDisplayByKey;
		TMap<FString, TArray<FString>> ChildrenByKey;
		TArray<FString> RootChildren;
		TMap<FString, TArray<FPlannedTransition>> TransitionsByOwner;
		TSet<FString> KnownBindableTargets;

		FExistingValidationContext()
		{
			const FString DefaultRootKey(TEXT("@default-root"));
			StateNameToKeys.FindOrAdd(TEXT("Root")).Add(DefaultRootKey);
			StateDisplayByKey.Add(DefaultRootKey, TEXT("Root"));
			RootChildren.Add(DefaultRootKey);
		}

		FString ResolveState(const FString& Selector) const
		{
			if (const FString* Found = StateIdToKey.Find(Selector))
			{
				return *Found;
			}
			if (const TArray<FString>* Found = StateNameToKeys.Find(Selector))
			{
				return Found->Num() == 1 ? (*Found)[0] : FString();
			}
			return FString();
		}

		bool HasStateName(const FString& Name) const
		{
			const TArray<FString>* Found = StateNameToKeys.Find(Name);
			return Found && Found->Num() > 0;
		}

		void AddKnownNode(const FGuid& Id)
		{
			if (Id.IsValid())
			{
				KnownBindableTargets.Add(Id.ToString());
			}
		}

		void Seed(const UStateTree* ExistingAsset, TSet<FString>& TaskAliases)
		{
			if (!ExistingAsset)
			{
				return;
			}
			UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(
				const_cast<UStateTree*>(ExistingAsset)->EditorData);
			if (!EditorData)
			{
				return;
			}
			// Replace the synthetic fresh-asset Root with the exact saved hierarchy.
			StateIdToKey.Reset();
			StateNameToKeys.Reset();
			StateDisplayByKey.Reset();
			ChildrenByKey.Reset();
			RootChildren.Reset();
			TransitionsByOwner.Reset();

			AddKnownNode(EditorData->GetRootParametersGuid());
			for (const FStateTreeEditorNode& Node : EditorData->GlobalTasks)
			{
				AddKnownNode(Node.ID);
				if (Node.ID.IsValid())
				{
					TaskAliases.Add(Node.ID.ToString());
				}
			}
			for (const FStateTreeEditorNode& Node : EditorData->Evaluators)
			{
				AddKnownNode(Node.ID);
			}

			EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* Parent)
			{
				const FString StateKey = State.ID.IsValid()
					? State.ID.ToString()
					: FString::Printf(TEXT("existing:%p"), &State);
				const FString StateName = State.Name.ToString();
				StateNameToKeys.FindOrAdd(StateName).Add(StateKey);
				if (State.ID.IsValid())
				{
					StateIdToKey.Add(State.ID.ToString(), StateKey);
					KnownBindableTargets.Add(State.ID.ToString());
				}
				StateDisplayByKey.Add(StateKey, StateName);
				if (Parent)
				{
					const FString ParentKey = Parent->ID.IsValid()
						? Parent->ID.ToString()
						: FString::Printf(TEXT("existing:%p"), Parent);
					ChildrenByKey.FindOrAdd(ParentKey).Add(StateKey);
				}
				else
				{
					RootChildren.Add(StateKey);
				}

				for (const FStateTreeEditorNode& Node : State.Tasks)
				{
					AddKnownNode(Node.ID);
					if (Node.ID.IsValid())
					{
						TaskAliases.Add(Node.ID.ToString());
					}
				}
				AddKnownNode(State.SingleTask.ID);
				if (State.SingleTask.ID.IsValid())
				{
					TaskAliases.Add(State.SingleTask.ID.ToString());
				}
				for (const FStateTreeEditorNode& Node : State.EnterConditions) { AddKnownNode(Node.ID); }
				for (const FStateTreeEditorNode& Node : State.Considerations) { AddKnownNode(Node.ID); }

				for (const FStateTreeTransition& Transition : State.Transitions)
				{
					FPlannedTransition Planned;
					Planned.OwnerKey = StateKey;
					Planned.TargetName = Transition.State.Name.ToString();
					if (Transition.State.ID.IsValid())
					{
						Planned.TargetKey = Transition.State.ID.ToString();
					}
					if (Transition.ID.IsValid())
					{
						Planned.GuidSelector = Transition.ID.ToString();
					}
					for (const FStateTreeEditorNode& Node : Transition.Conditions) { AddKnownNode(Node.ID); }
					TransitionsByOwner.FindOrAdd(StateKey).Add(MoveTemp(Planned));
				}
				return EStateTreeVisitor::Continue;
			});

			// Name-only state links are legal in saved assets. Resolve them only after the
			// complete hierarchy has been indexed so duplicate display names stay ambiguous.
			for (TPair<FString, TArray<FPlannedTransition>>& Pair : TransitionsByOwner)
			{
				for (FPlannedTransition& Transition : Pair.Value)
				{
					if (Transition.TargetKey.IsEmpty())
					{
						Transition.TargetKey = ResolveState(Transition.TargetName);
					}
				}
			}
		}

		void AddSpecState(const FString& Name, const FString& ParentSelector)
		{
			if (Name.IsEmpty() || Name.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
			{
				return;
			}
			const FString StateKey = TEXT("spec:") + Name;
			const FString ParentKey = ParentSelector.IsEmpty() ? RootKey : ResolveState(ParentSelector);
			StateNameToKeys.FindOrAdd(Name).Add(StateKey);
			StateDisplayByKey.Add(StateKey, Name);
			if (ParentKey == RootKey)
			{
				RootChildren.Add(StateKey);
			}
			else
			{
				ChildrenByKey.FindOrAdd(ParentKey).Add(StateKey);
			}
		}

		void AddSpecTransition(const FString& From, const FString& To)
		{
			const FString OwnerKey = ResolveState(From);
			const FString TargetKey = ResolveState(To);
			if (OwnerKey.IsEmpty() || TargetKey.IsEmpty())
			{
				return;
			}
			FPlannedTransition Planned;
			Planned.OwnerKey = OwnerKey;
			Planned.TargetName = StateDisplayByKey.FindRef(TargetKey);
			Planned.TargetKey = TargetKey;
			TransitionsByOwner.FindOrAdd(OwnerKey).Add(MoveTemp(Planned));
		}

		void AddRuntimeTransitionSelectors(
			const TMap<FString, FString>& GenericAliases,
			const TSet<FString>& TypedStateAliases,
			const TSet<FString>& TypedTransitionAliases,
			TMap<FString, FString>& OutOwners) const
		{
			TArray<FPlannedTransition> Ordered;
			// UStateTreeEditorData::VisitHierarchy drains one top-level subtree breadth-first
			// before starting the next top-level subtree. A single global queue would instead
			// interleave roots and assign the wrong digits-only transition indices.
			for (const FString& RootState : RootChildren)
			{
				TArray<FString> Queue;
				Queue.Add(RootState);
				for (int32 Cursor = 0; Cursor < Queue.Num(); ++Cursor)
				{
					const FString& StateKey = Queue[Cursor];
					if (const TArray<FPlannedTransition>* StateTransitions = TransitionsByOwner.Find(StateKey))
					{
						Ordered.Append(*StateTransitions);
					}
					if (const TArray<FString>* Children = ChildrenByKey.Find(StateKey))
					{
						Queue.Append(*Children);
					}
				}
			}

			TMap<FString, int32> TargetCounts;
			TMap<FString, FString> UniqueTargetOwners;
			for (const FPlannedTransition& Transition : Ordered)
			{
				if (!Transition.TargetKey.IsEmpty())
				{
					++TargetCounts.FindOrAdd(Transition.TargetKey);
					UniqueTargetOwners.Add(Transition.TargetKey, Transition.OwnerKey);
				}
			}

			for (int32 Index = 0; Index < Ordered.Num(); ++Index)
			{
				const FPlannedTransition& Transition = Ordered[Index];
				auto AddRawSelector = [&](const FString& Selector)
				{
					if (Selector.IsEmpty()) { return; }
					// Author-time aliases are remapped before the runtime resolver. A state/task/parameter
					// alias with the same spelling therefore shadows a raw numeric/GUID/target selector.
					if (!GenericAliases.Contains(Selector) || TypedTransitionAliases.Contains(Selector))
					{
						OutOwners.FindOrAdd(Selector, Transition.OwnerKey);
					}
				};
				AddRawSelector(Transition.GuidSelector);
				AddRawSelector(FString::FromInt(Index));
			}
			auto AddTargetSelector = [&](const FString& Selector, const FString& TargetKey)
			{
				if (TargetCounts.FindRef(TargetKey) != 1 || Selector.IsEmpty())
				{
					return;
				}
				if (TypedStateAliases.Contains(Selector))
				{
					// Execute remaps a state alias to the target state's GUID before runtime
					// transition resolution, so it overrides a numeric/GUID raw selector.
					OutOwners.Add(Selector, UniqueTargetOwners.FindRef(TargetKey));
				}
				else if (!GenericAliases.Contains(Selector) || TypedTransitionAliases.Contains(Selector))
				{
					OutOwners.FindOrAdd(Selector, UniqueTargetOwners.FindRef(TargetKey));
				}
			};
			for (const TPair<FString, FString>& Pair : StateIdToKey)
			{
				AddTargetSelector(Pair.Key, Pair.Value);
			}
			for (const TPair<FString, TArray<FString>>& Pair : StateNameToKeys)
			{
				if (Pair.Value.Num() == 1)
				{
					AddTargetSelector(Pair.Key, Pair.Value[0]);
				}
			}
		}
	};

	const FString FExistingValidationContext::RootKey(TEXT("@root-container"));

	FString JsonTypeName(const TSharedPtr<FJsonValue>& V)
	{
		if (!V.IsValid() || V->IsNull())
		{
			return TEXT("null");
		}
		switch (V->Type)
		{
		case EJson::String: return TEXT("string");
		case EJson::Number: return TEXT("number");
		case EJson::Boolean: return TEXT("boolean");
		case EJson::Array: return TEXT("array");
		case EJson::Object: return TEXT("object");
		default: return TEXT("null");
		}
	}

	void AddDiag(
		TArray<FCUStateTreeSpecDiagnostic>& Out,
		const FString& Path,
		const TCHAR* Code,
		const FString& Value,
		const FString& Message)
	{
		FCUStateTreeSpecDiagnostic Diag;
		Diag.Path = Path;
		Diag.Code = Code;
		Diag.Value = Value;
		Diag.Message = Message;
		Out.Add(MoveTemp(Diag));
	}

	int32 DiagnosticPriority(const FString& Code)
	{
		// Issue #28408: destination context can add duplicate/unresolved diagnostics that a clean
		// target does not have. Intrinsic shape/required-field failures must stay primary so a stale
		// asset cannot mask the same malformed request with an unrelated existing-state error.
		return Code == CodeFieldNotArray || Code == CodeRowNotObject || Code == CodeMissingRequired
			? 0
			: 1;
	}

	const TArray<TSharedPtr<FJsonValue>>* GetArrayOrDiag(
		const TSharedPtr<FJsonObject>& Spec,
		const TCHAR* Key,
		TArray<FCUStateTreeSpecDiagnostic>& Out)
	{
		if (!Spec.IsValid())
		{
			return nullptr;
		}
		const TSharedPtr<FJsonValue> Field = Spec->TryGetField(Key);
		if (!Field.IsValid() || Field->IsNull())
		{
			return nullptr;
		}
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (Spec->TryGetArrayField(Key, Arr) && Arr)
		{
			return Arr;
		}
		AddDiag(Out, FString(Key), CodeFieldNotArray, JsonTypeName(Field),
			FString::Printf(TEXT("%s is not a JSON array"), Key));
		return nullptr;
	}

	bool RowObjectOrDiag(
		const TSharedPtr<FJsonValue>& V,
		const TCHAR* Kind,
		int32 Index,
		TSharedPtr<FJsonObject>& OutRow,
		TArray<FCUStateTreeSpecDiagnostic>& Out)
	{
		OutRow.Reset();
		if (V.IsValid() && V->Type == EJson::Object)
		{
			OutRow = V->AsObject();
			if (OutRow.IsValid())
			{
				return true;
			}
		}
		AddDiag(Out,
			FString::Printf(TEXT("%s[%d]"), Kind, Index),
			CodeRowNotObject,
			JsonTypeName(V),
			FString::Printf(TEXT("%s[%d] is not a JSON object"), Kind, Index));
		return false;
	}

	FString RowString(const TSharedPtr<FJsonObject>& Row, const TCHAR* Field)
	{
		FString Value;
		if (Row.IsValid())
		{
			Row->TryGetStringField(Field, Value);
		}
		return Value;
	}

	bool IsRootName(const FString& Name)
	{
		return Name.Equals(TEXT("Root"), ESearchCase::IgnoreCase);
	}

	bool IsReservedOverwriteKey(const FString& Key)
	{
		return Key.Equals(TEXT("$last")) || Key.Equals(TEXT("$last_transition"));
	}

	void TryRegisterUser(
		TMap<FString, FString>& Aliases,
		const FString& Key,
		const TCHAR* Kind,
		int32 Index,
		const TCHAR* Field,
		const TCHAR* Noun,
		TArray<FCUStateTreeSpecDiagnostic>& Out)
	{
		if (Key.IsEmpty() || IsReservedOverwriteKey(Key))
		{
			return;
		}
		if (Aliases.Contains(Key))
		{
			AddDiag(Out,
				FString::Printf(TEXT("%s[%d].%s"), Kind, Index, Field),
				CodeDuplicateAlias,
				Key,
				FString::Printf(TEXT("%s[%d] duplicate %s '%s'"), Kind, Index, Noun, *Key));
			return;
		}
		Aliases.Add(Key, Key);
	}

	void PutOverwrite(TMap<FString, FString>& Aliases, const FString& Key)
	{
		if (!Key.IsEmpty())
		{
			Aliases.Add(Key, Key);
		}
	}

	bool HasPathPrefix(const FString& Path, FString& OutPrefix)
	{
		int32 Colon = INDEX_NONE;
		if (Path.FindChar(TEXT(':'), Colon) && Colon > 0)
		{
			OutPrefix = Path.Left(Colon);
			return true;
		}
		return false;
	}

}

bool FCUStateTreeAuthor::ValidateSpec(
	const TSharedPtr<FJsonObject>& Spec,
	TArray<FCUStateTreeSpecDiagnostic>& OutDiagnostics,
	const UStateTree* ExistingAsset)
{
	OutDiagnostics.Reset();
	if (!Spec.IsValid())
	{
		return true;
	}

	TMap<FString, FString> Aliases;
	TSet<FString> TaskAliases;
	TSet<FString> BindableAliases;
	FExistingValidationContext Existing;
	// Typed transition aliases are kept separate from the generic remap table. Execute remaps
	// `condition.transition` through the generic table, so accepting a task/state alias here would
	// turn it into a non-transition GUID and fail only after CreateStateTree. The owner value mirrors
	// ResolveAddSlot's state/transition ownership check in CURemoteLibrary.cpp.
	TMap<FString, FString> TransitionOwners;
	TSet<FString> TypedStateAliases;
	TSet<FString> TypedTransitionAliases;
	Existing.Seed(ExistingAsset, TaskAliases);

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("states"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("states"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString Name = RowString(Row, TEXT("name"));
			if (IsRootName(Name))
			{
				continue;
			}
			if (Name.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("states[%d].name"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("states[%d] missing required field 'name'"), i));
				continue;
			}
			const FString Parent = RowString(Row, TEXT("parent"));
			if (!Parent.IsEmpty() && Existing.ResolveState(Parent).IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("states[%d].parent"), i),
					CodeUnresolved,
					Parent,
					FString::Printf(TEXT("states[%d] unresolved parent '%s'"), i, *Parent));
			}
			const bool bAliasCollision = Aliases.Contains(Name);
			const bool bExistingNameCollision = ExistingAsset && Existing.HasStateName(Name);
			if (bExistingNameCollision)
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("states[%d].name"), i),
					CodeDuplicateAlias,
					Name,
					FString::Printf(TEXT("states[%d] duplicate existing state name '%s'"), i, *Name));
			}
			TryRegisterUser(Aliases, Name, TEXT("states"), i, TEXT("name"), TEXT("name"), OutDiagnostics);
			if (!bAliasCollision && !bExistingNameCollision)
			{
				Existing.AddSpecState(Name, Parent);
				TypedStateAliases.Add(Name);
				BindableAliases.Add(Name);
			}
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("parameters"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("parameters"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString Name = RowString(Row, TEXT("name"));
			if (Name.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("parameters[%d].name"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("parameters[%d] missing required field 'name'"), i));
				continue;
			}
			TryRegisterUser(Aliases, Name, TEXT("parameters"), i, TEXT("name"), TEXT("name"), OutDiagnostics);
			BindableAliases.Add(Name);
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("tasks"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("tasks"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString ClassName = RowString(Row, TEXT("class"));
			if (ClassName.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("tasks[%d].class"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("tasks[%d] missing required field 'class'"), i));
			}
			const FString StateName = RowString(Row, TEXT("state"));
			if (!StateName.IsEmpty() && Existing.ResolveState(StateName).IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("tasks[%d].state"), i),
					CodeUnresolved,
					StateName,
					FString::Printf(TEXT("tasks[%d] unresolved state '%s'"), i, *StateName));
			}
			// Apply registers exactly ONE alias per task -- `id` when present, else `name`
			// (CUStateTreeAuthor.cpp Execute). Registering both here would let the preflight
			// resolve a reference apply cannot, so a spec accepted by dry-run would still fail
			// AFTER CreateStateTree -- the ordering this issue exists to remove.
			const FString Id = RowString(Row, TEXT("id"));
			const FString Name = RowString(Row, TEXT("name"));
			const FString Alias = Id.IsEmpty() ? Name : Id;
			const TCHAR* AliasField = Id.IsEmpty() ? TEXT("name") : TEXT("id");
			if (!Alias.IsEmpty())
			{
				TryRegisterUser(Aliases, Alias, TEXT("tasks"), i, AliasField, AliasField, OutDiagnostics);
				TaskAliases.Add(Alias);
				BindableAliases.Add(Alias);
			}
			PutOverwrite(Aliases, TEXT("$last"));
			TaskAliases.Add(TEXT("$last"));
			BindableAliases.Add(TEXT("$last"));
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("transitions"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("transitions"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString From = RowString(Row, TEXT("from"));
			const FString To = RowString(Row, TEXT("to"));
			if (From.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("transitions[%d].from"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("transitions[%d] missing required field 'from'"), i));
			}
			else if (Existing.ResolveState(From).IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("transitions[%d].from"), i),
					CodeUnresolved,
					From,
					FString::Printf(TEXT("transitions[%d] unresolved from '%s'"), i, *From));
			}
			if (To.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("transitions[%d].to"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("transitions[%d] missing required field 'to'"), i));
			}
			else if (Existing.ResolveState(To).IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("transitions[%d].to"), i),
					CodeUnresolved,
					To,
					FString::Printf(TEXT("transitions[%d] unresolved to '%s'"), i, *To));
			}
			const FString Id = RowString(Row, TEXT("id"));
			if (!Id.IsEmpty())
			{
				TryRegisterUser(Aliases, Id, TEXT("transitions"), i, TEXT("id"), TEXT("id"), OutDiagnostics);
				TransitionOwners.Add(Id, Existing.ResolveState(From));
				TypedTransitionAliases.Add(Id);
			}
			const FString Edge = From + TEXT("->") + To;
			if (!From.IsEmpty())
			{
				PutOverwrite(Aliases, Edge);
				TransitionOwners.Add(Edge, Existing.ResolveState(From));
				TypedTransitionAliases.Add(Edge);
			}
			PutOverwrite(Aliases, TEXT("$last_transition"));
			TransitionOwners.Add(TEXT("$last_transition"), Existing.ResolveState(From));
			TypedTransitionAliases.Add(TEXT("$last_transition"));
			Existing.AddSpecTransition(From, To);
		}
	}
	Existing.AddRuntimeTransitionSelectors(Aliases, TypedStateAliases, TypedTransitionAliases, TransitionOwners);

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("conditions"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("conditions"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString ClassName = RowString(Row, TEXT("class"));
			if (ClassName.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("conditions[%d].class"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("conditions[%d] missing required field 'class'"), i));
			}
			const FString StateName = RowString(Row, TEXT("state"));
			const FString Transition = RowString(Row, TEXT("transition"));
			if (StateName.IsEmpty() && Transition.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("conditions[%d]"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("conditions[%d] requires state or transition"), i));
			}
			const FString StateKey = StateName.IsEmpty() ? FString() : Existing.ResolveState(StateName);
			const bool bStateKnown = StateName.IsEmpty() || !StateKey.IsEmpty();
			if (!bStateKnown)
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("conditions[%d].state"), i),
					CodeUnresolved,
					StateName,
					FString::Printf(TEXT("conditions[%d] unresolved state '%s'"), i, *StateName));
			}
			const FString* TransitionOwner = Transition.IsEmpty() ? nullptr : TransitionOwners.Find(Transition);
			if (!Transition.IsEmpty() && !TransitionOwner)
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("conditions[%d].transition"), i),
					CodeUnresolved,
					Transition,
					FString::Printf(TEXT("conditions[%d] unresolved transition '%s'"), i, *Transition));
			}
			else if (!StateName.IsEmpty() && bStateKnown && TransitionOwner
				&& StateKey != *TransitionOwner)
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("conditions[%d].state"), i),
					CodeOwnerMismatch,
					StateName,
					FString::Printf(TEXT("conditions[%d] state '%s' does not own transition '%s'"),
						i, *StateName, *Transition));
			}
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("evaluators"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("evaluators"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString ClassName = RowString(Row, TEXT("class"));
			if (ClassName.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("evaluators[%d].class"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("evaluators[%d] missing required field 'class'"), i));
			}
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("considerations"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("considerations"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString ClassName = RowString(Row, TEXT("class"));
			if (ClassName.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("considerations[%d].class"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("considerations[%d] missing required field 'class'"), i));
			}
			const FString StateName = RowString(Row, TEXT("state"));
			if (StateName.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("considerations[%d].state"), i),
					CodeMissingRequired,
					FString(),
					FString::Printf(TEXT("considerations[%d] missing required field 'state'"), i));
			}
			else if (Existing.ResolveState(StateName).IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("considerations[%d].state"), i),
					CodeUnresolved,
					StateName,
					FString::Printf(TEXT("considerations[%d] unresolved state '%s'"), i, *StateName));
			}
		}
	}

	if (const TArray<TSharedPtr<FJsonValue>>* Arr = GetArrayOrDiag(Spec, TEXT("bindings"), OutDiagnostics))
	{
		for (int32 i = 0; i < Arr->Num(); ++i)
		{
			TSharedPtr<FJsonObject> Row;
			if (!RowObjectOrDiag((*Arr)[i], TEXT("bindings"), i, Row, OutDiagnostics))
			{
				continue;
			}
			const FString SourceTaskId = RowString(Row, TEXT("source_task_id"));
			const FString TargetPath = RowString(Row, TEXT("target_path"));
			if (SourceTaskId.IsEmpty() || TargetPath.IsEmpty())
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("bindings[%d]"), i),
					CodeMissingRequired,
					SourceTaskId.IsEmpty() ? SourceTaskId : TargetPath,
					FString::Printf(TEXT("bindings[%d] requires source_task_id and target_path"), i));
				continue;
			}
			if (!TaskAliases.Contains(SourceTaskId))
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("bindings[%d].source_task_id"), i),
					CodeUnresolved,
					SourceTaskId,
					FString::Printf(TEXT("bindings[%d] unresolved source_task_id '%s'"), i, *SourceTaskId));
			}
			FString Prefix;
			const bool bHasPrefix = HasPathPrefix(TargetPath, Prefix);
			const FString TargetStruct = RowString(Row, TEXT("target_struct_id"));
			if (TargetStruct.IsEmpty() && !bHasPrefix)
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("bindings[%d].target_path"), i),
					CodeBindingUnprefixed,
					TargetPath,
					FString::Printf(
						TEXT("bindings[%d] target_path needs 'alias:Property' or target_struct_id (got '%s')"),
						i, *TargetPath));
			}
			if (bHasPrefix && !BindableAliases.Contains(Prefix)
				&& !Existing.KnownBindableTargets.Contains(Prefix))
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("bindings[%d].target_path"), i),
					CodeUnresolved,
					Prefix,
					FString::Printf(TEXT("bindings[%d] unresolved target '%s'"), i, *Prefix));
			}
			if (!TargetStruct.IsEmpty() && !BindableAliases.Contains(TargetStruct)
				&& !Existing.KnownBindableTargets.Contains(TargetStruct))
			{
				AddDiag(OutDiagnostics,
					FString::Printf(TEXT("bindings[%d].target_struct_id"), i),
					CodeUnresolved,
					TargetStruct,
					FString::Printf(TEXT("bindings[%d] unresolved target '%s'"), i, *TargetStruct));
			}
		}
	}

	OutDiagnostics.StableSort([](
		const FCUStateTreeSpecDiagnostic& A,
		const FCUStateTreeSpecDiagnostic& B)
	{
		return DiagnosticPriority(A.Code) < DiagnosticPriority(B.Code);
	});
	return OutDiagnostics.Num() == 0;
}
#else
bool FCUStateTreeAuthor::ValidateSpec(
	const TSharedPtr<FJsonObject>& Spec,
	TArray<FCUStateTreeSpecDiagnostic>& OutDiagnostics,
	const UStateTree* ExistingAsset)
{
	OutDiagnostics.Reset();
	FCUStateTreeSpecDiagnostic Diagnostic;
	Diagnostic.Path = TEXT("$");
	Diagnostic.Code = TEXT("state_tree_plugin_unavailable");
	Diagnostic.Message = TEXT("StateTree spec validation requires the optional StateTree and PropertyBindingUtils plugins. Enable StateTree and PropertyBindingUtils in your .uproject, then rebuild.");
	OutDiagnostics.Add(MoveTemp(Diagnostic));
	return false;
}
#endif
