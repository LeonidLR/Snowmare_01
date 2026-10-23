#pragma once

// `stat SnowInteraction` (interactive snow, docs/systems/environment-weather.md §14). Shared by SnowInteractionSubsystem.cpp and
// SnowInteractionWalkers.cpp.

#include "CoreMinimal.h"
#include "Stats/Stats.h"

DECLARE_STATS_GROUP(TEXT("SnowInteraction"), STATGROUP_SnowInteraction, STATCAT_Advanced);

DECLARE_CYCLE_STAT_EXTERN(TEXT("Tick (total)"), STAT_SnowInteraction_Tick, STATGROUP_SnowInteraction, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Poll walkers"), STAT_SnowInteraction_Poll, STATGROUP_SnowInteraction, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Draw stamps"), STAT_SnowInteraction_Draw, STATGROUP_SnowInteraction, );
DECLARE_CYCLE_STAT_EXTERN(TEXT("Fade / shift / clamp passes"), STAT_SnowInteraction_Passes, STATGROUP_SnowInteraction, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Stamps drawn"), STAT_SnowInteraction_Stamps, STATGROUP_SnowInteraction, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Stamps dropped (cap)"), STAT_SnowInteraction_Dropped, STATGROUP_SnowInteraction, );
DECLARE_DWORD_COUNTER_STAT_EXTERN(TEXT("Walkers polled"), STAT_SnowInteraction_Walkers, STATGROUP_SnowInteraction, );
