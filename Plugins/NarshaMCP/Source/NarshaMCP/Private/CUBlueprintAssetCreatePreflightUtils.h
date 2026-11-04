// Copyright 2026 Next Stage Inc. All rights reserved.

#pragma once

#include "CoreMinimal.h"

namespace CUBlueprintAssetCreatePreflight
{
	enum class ESourceControlSafety
	{
		Disabled,
		ProviderUnavailable,
		StateUnavailable,
		Deleted,
		Safe,
	};

	inline ESourceControlSafety ClassifySourceControlSafety(
		bool bSourceControlEnabled,
		bool bProviderAvailable,
		bool bStateValid,
		bool bPackageDeleted)
	{
		if (!bSourceControlEnabled)
		{
			return ESourceControlSafety::Disabled;
		}
		if (!bProviderAvailable)
		{
			return ESourceControlSafety::ProviderUnavailable;
		}
		if (!bStateValid)
		{
			return ESourceControlSafety::StateUnavailable;
		}
		return bPackageDeleted
			? ESourceControlSafety::Deleted
			: ESourceControlSafety::Safe;
	}

	inline const TCHAR* GetSourceControlRejectionReason(ESourceControlSafety Safety)
	{
		switch (Safety)
		{
		case ESourceControlSafety::ProviderUnavailable:
			return TEXT("source_control_provider_unavailable");
		case ESourceControlSafety::StateUnavailable:
			return TEXT("source_control_state_unavailable");
		case ESourceControlSafety::Deleted:
			return TEXT("source_control_deleted_package");
		case ESourceControlSafety::Disabled:
		case ESourceControlSafety::Safe:
		default:
			return nullptr;
		}
	}
}
