// Copyright 2026 Next Stage Inc. All rights reserved.
// Issue #27012: PIE measurement-mode helpers (hostile AI disable + optional pawn health).

#pragma once

#include "CoreMinimal.h"

class APawn;

/** True when a class name looks like a Lyra/bot AI spawner (not PlayerStart / item spawners). */
bool CUPieMeasurement_IsHostileAiSpawnerClassName(const FString& ClassName);

/**
 * JSON fragment to splice into pie_get_player_state's player object.
 * Returns empty string when no numeric Health/CurrentHealth field is found,
 * otherwise `,\"health\":<n>` (no Lyra module dependency — reflection only).
 */
FString CUPieMeasurement_PawnHealthFields(APawn* Pawn);
