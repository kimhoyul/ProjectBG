// Copyright 2026 Next Stage Inc. All rights reserved.
#pragma once

#include "CoreMinimal.h"
#include "NiagaraScriptVariable.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "UObject/UnrealType.h"

namespace NarshaMCP::NiagaraUserScriptVariableLookup
{
	enum class EStatus : uint8
	{
		Found,
		Absent,
		Unavailable,
		Ambiguous,
	};

	struct FResult
	{
		EStatus Status = EStatus::Unavailable;
		const UNiagaraScriptVariable* Variable = nullptr;
	};

	inline FResult Find(
		const UNiagaraSystemEditorData* EditorData,
		FNiagaraVariable UserParameter,
		const UNiagaraSystem& System)
	{
		if (!EditorData ||
			!System.GetExposedParameters().RedirectUserVariable(UserParameter))
		{
			return {};
		}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8)
		const UNiagaraScriptVariable* Match =
			EditorData->FindUserScriptVariable(UserParameter, System);
		return Match
			? FResult{EStatus::Found, Match}
			: FResult{EStatus::Absent, nullptr};
#else
		// UE 5.7 exposes only GUID-based read-only lookup. Its public
		// ParameterGuidMapping is a rapid-iteration rename map and is not populated
		// by AddParameter, so inspect the serialized metadata array itself. Property
		// or type drift is Unavailable, never equivalent to an absent value.
		const FArrayProperty* MetadataArray = FindFProperty<FArrayProperty>(
			EditorData->GetClass(), TEXT("UserParameterMetaData"));
		const FObjectPropertyBase* InnerObject = MetadataArray
			? CastField<FObjectPropertyBase>(MetadataArray->Inner)
			: nullptr;
		if (!InnerObject || !InnerObject->PropertyClass ||
			!InnerObject->PropertyClass->IsChildOf(UNiagaraScriptVariable::StaticClass()))
		{
			return {};
		}

		FScriptArrayHelper_InContainer Values(MetadataArray, EditorData);
		const UNiagaraScriptVariable* Match = nullptr;
		for (int32 Index = 0; Index < Values.Num(); ++Index)
		{
			const UNiagaraScriptVariable* Candidate = Cast<UNiagaraScriptVariable>(
				InnerObject->GetObjectPropertyValue(Values.GetRawPtr(Index)));
			if (Candidate && Candidate->Variable == UserParameter)
			{
				if (Match)
				{
					return {EStatus::Ambiguous, nullptr};
				}
				Match = Candidate;
			}
		}

		return Match
			? FResult{EStatus::Found, Match}
			: FResult{EStatus::Absent, nullptr};
#endif
	}
}
