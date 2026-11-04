// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27012: pie_set_measurement_mode — disable hostile AI in the running PIE world.

#include "CUPieMeasurementMode.h"

#include "CUJsonUtils.h"
#include "CUWebSocketEditorSubsystem.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

bool EnsurePIERunningMultiPIE(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const FString& RequestId,
	const FString& ResponseType);

UWorld* ResolveClientWorldOrReject(
	UCUWebSocketEditorSubsystem* Self,
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request,
	const FString& RequestId,
	const TCHAR* ResponseType,
	int32& OutClientId);

namespace
{
	bool ValidateSocketAndRequest(
		INetworkingWebSocket* Socket,
		const TSharedPtr<FJsonObject>& Request,
		const TCHAR* FunctionName,
		FString& OutRequestId)
	{
		if (!Socket || !Request.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("NarshaMCP: %s: Invalid socket or request"), FunctionName);
			return false;
		}
		if (!Request->TryGetStringField(TEXT("request_id"), OutRequestId)
			|| OutRequestId.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("NarshaMCP: %s: Missing or invalid request_id"), FunctionName);
			return false;
		}
		return true;
	}

	bool IsPlayerPawn(const TSet<const APawn*>& PlayerPawns, const APawn* Pawn)
	{
		return Pawn != nullptr && PlayerPawns.Contains(Pawn);
	}

	void CollectPlayerPawns(UWorld* World, TSet<const APawn*>& OutPlayerPawns)
	{
		if (!World)
		{
			return;
		}
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (!IsValid(PC))
			{
				continue;
			}
			APawn* Pawn = PC->GetPawn();
			if (IsValid(Pawn))
			{
				OutPlayerPawns.Add(Pawn);
			}
		}
	}

	int32 DisableHostileAiControllers(UWorld* World, const TSet<const APawn*>& PlayerPawns)
	{
		int32 Disabled = 0;
		if (!World)
		{
			return Disabled;
		}
		for (TActorIterator<AAIController> It(World); It; ++It)
		{
			AAIController* AIC = *It;
			if (!IsValid(AIC))
			{
				continue;
			}
			APawn* Pawn = AIC->GetPawn();
			if (IsPlayerPawn(PlayerPawns, Pawn))
			{
				continue;
			}
			if (UBrainComponent* Brain = AIC->GetBrainComponent())
			{
				Brain->StopLogic(TEXT("measurement_mode"));
			}
			AIC->StopMovement();
			AIC->SetActorTickEnabled(false);
			if (IsValid(Pawn) && !IsPlayerPawn(PlayerPawns, Pawn))
			{
				Pawn->SetActorTickEnabled(false);
			}
			++Disabled;
		}
		return Disabled;
	}

	int32 SuppressAiSpawners(UWorld* World, const TSet<const APawn*>& PlayerPawns)
	{
		int32 Suppressed = 0;
		if (!World)
		{
			return Suppressed;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!IsValid(Actor) || !Actor->GetClass())
			{
				continue;
			}
			if (const APawn* AsPawn = Cast<APawn>(Actor))
			{
				if (IsPlayerPawn(PlayerPawns, AsPawn))
				{
					continue;
				}
			}
			if (CUPieMeasurement_IsHostileAiSpawnerClassName(Actor->GetClass()->GetName()))
			{
				Actor->SetActorTickEnabled(false);
				Actor->SetActorHiddenInGame(true);
				++Suppressed;
				continue;
			}
			// Lyra BotCreation / B_ShooterBotSpawner live on components, not the
			// owning GameMode/GameState actor class. Tick-off the component only.
			TInlineComponentArray<UActorComponent*> Components;
			Actor->GetComponents(Components);
			for (UActorComponent* Comp : Components)
			{
				if (!IsValid(Comp) || !Comp->GetClass())
				{
					continue;
				}
				if (!CUPieMeasurement_IsHostileAiSpawnerClassName(Comp->GetClass()->GetName()))
				{
					continue;
				}
				Comp->SetComponentTickEnabled(false);
				++Suppressed;
			}
		}
		return Suppressed;
	}
}

bool CUPieMeasurement_IsHostileAiSpawnerClassName(const FString& ClassName)
{
	if (ClassName.IsEmpty())
	{
		return false;
	}
	return ClassName.Contains(TEXT("BotCreation"), ESearchCase::IgnoreCase)
		|| ClassName.Contains(TEXT("AISpawner"), ESearchCase::IgnoreCase)
		|| ClassName.Contains(TEXT("ShooterAISpawner"), ESearchCase::IgnoreCase);
}

FString CUPieMeasurement_PawnHealthFields(APawn* Pawn)
{
	if (!IsValid(Pawn))
	{
		return FString();
	}

	TInlineComponentArray<UActorComponent*> Components;
	Pawn->GetComponents(Components);
	for (UActorComponent* Comp : Components)
	{
		if (!IsValid(Comp) || !Comp->GetClass())
		{
			continue;
		}
		const FString ClassName = Comp->GetClass()->GetName();
		if (!ClassName.Contains(TEXT("Health")))
		{
			continue;
		}
		for (TFieldIterator<FNumericProperty> PropIt(Comp->GetClass()); PropIt; ++PropIt)
		{
			FNumericProperty* Num = *PropIt;
			if (!Num)
			{
				continue;
			}
			const FString PropName = Num->GetName();
			const bool bHealthName = PropName.Equals(TEXT("Health"), ESearchCase::IgnoreCase)
				|| PropName.Equals(TEXT("CurrentHealth"), ESearchCase::IgnoreCase)
				|| PropName.Equals(TEXT("HealthCurrent"), ESearchCase::IgnoreCase);
			if (!bHealthName)
			{
				continue;
			}
			const void* ValuePtr = Num->ContainerPtrToValuePtr<void>(Comp);
			if (!ValuePtr)
			{
				continue;
			}
			const double Health = Num->GetFloatingPointPropertyValue(ValuePtr);
			if (!FMath::IsFinite(Health))
			{
				continue;
			}
			return FString::Printf(TEXT(",\"health\":%.9g"), Health);
		}
		// LyraHealthComponent exposes GetHealth() as a UFUNCTION, not a Health UPROPERTY.
		if (UFunction* Fn = Comp->FindFunction(FName(TEXT("GetHealth"))))
		{
			FNumericProperty* RetNum = CastField<FNumericProperty>(Fn->GetReturnProperty());
			const int32 ParmSize = FMath::Clamp(static_cast<int32>(Fn->ParmsSize), 0, 256);
			if (RetNum && ParmSize > 0
				&& static_cast<int32>(Fn->ReturnValueOffset) + static_cast<int32>(RetNum->GetSize()) <= ParmSize)
			{
				TArray<uint8> Params;
				Params.SetNumZeroed(ParmSize);
				Comp->ProcessEvent(Fn, Params.GetData());
				const double Health = RetNum->GetFloatingPointPropertyValue(
					Params.GetData() + Fn->ReturnValueOffset);
				if (FMath::IsFinite(Health))
				{
					return FString::Printf(TEXT(",\"health\":%.9g"), Health);
				}
			}
		}
	}
	return FString();
}

void UCUWebSocketEditorSubsystem::HandlePIESetMeasurementMode(
	INetworkingWebSocket* Socket,
	const TSharedPtr<FJsonObject>& Request)
{
	FString RequestId;
	if (!ValidateSocketAndRequest(Socket, Request, TEXT("HandlePIESetMeasurementMode"), RequestId))
	{
		return;
	}

	bool bEnabled = false;
	if (!Request->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		const FString Response = FString::Printf(TEXT(
			"{\"type\":\"pie_set_measurement_mode_result\",\"request_id\":\"%s\","
			"\"success\":false,\"error_code\":\"MISSING_PARAM\","
			"\"error\":\"pie_set_measurement_mode requires 'enabled' (boolean)\"}"),
			*RequestId);
		SendResponse(Socket, Response);
		return;
	}

	if (!EnsurePIERunningMultiPIE(this, Socket, RequestId, TEXT("pie_set_measurement_mode")))
	{
		return;
	}

	double ClientIdD = 0.0;
	if (Request->TryGetNumberField(TEXT("client_id"), ClientIdD))
	{
		if (ClientIdD < 0.0 || ClientIdD > 64.0)
		{
			const int32 Reported = static_cast<int32>(FMath::Clamp(ClientIdD, -1.0, 65.0));
			const FString Response = FString::Printf(TEXT(
				"{\"type\":\"pie_set_measurement_mode_result\",\"request_id\":\"%s\","
				"\"success\":false,\"error_code\":\"INVALID_CLIENT_ID\","
				"\"client_id\":%d,"
				"\"error\":\"client_id out of range (0-64)\"}"),
				*RequestId, Reported);
			SendResponse(Socket, Response);
			return;
		}
	}

	int32 ClientId = 0;
	UWorld* TargetWorld = ResolveClientWorldOrReject(
		this, Socket, Request, RequestId, TEXT("pie_set_measurement_mode"), ClientId);
	if (!TargetWorld)
	{
		return;
	}

	int32 DisabledAi = 0;
	int32 SuppressedSpawners = 0;
	TArray<FString> PlayerPawnPaths;
	if (bEnabled)
	{
		TSet<const APawn*> PlayerPawns;
		CollectPlayerPawns(TargetWorld, PlayerPawns);
		for (const APawn* Pawn : PlayerPawns)
		{
			if (IsValid(Pawn))
			{
				PlayerPawnPaths.Add(Pawn->GetPathName());
			}
		}
		DisabledAi = DisableHostileAiControllers(TargetWorld, PlayerPawns);
		SuppressedSpawners = SuppressAiSpawners(TargetWorld, PlayerPawns);
	}

	FString PathsJson = TEXT("[");
	for (int32 i = 0; i < PlayerPawnPaths.Num(); ++i)
	{
		if (i > 0)
		{
			PathsJson += TEXT(",");
		}
		PathsJson += FString::Printf(
			TEXT("\"%s\""), *CUJsonUtils::EscapeJsonString(PlayerPawnPaths[i]));
	}
	PathsJson += TEXT("]");

	const FString Hint = bEnabled
		? TEXT("Hostile AI StopLogic + spawner tick-off applied. Restore default play with stop_play then play_in_editor.")
		: TEXT("enabled=false is a no-op. Restore default play with stop_play then play_in_editor.");

	const FString Response = FString::Printf(TEXT(
		"{\"type\":\"pie_set_measurement_mode_result\",\"request_id\":\"%s\","
		"\"success\":true,\"enabled\":%s,\"disabled_ai_controllers\":%d,"
		"\"suppressed_spawners\":%d,\"player_pawn_paths\":%s,"
		"\"hint\":\"%s\"}"),
		*RequestId,
		bEnabled ? TEXT("true") : TEXT("false"),
		DisabledAi,
		SuppressedSpawners,
		*PathsJson,
		*CUJsonUtils::EscapeJsonString(Hint));
	SendResponse(Socket, Response);
}
