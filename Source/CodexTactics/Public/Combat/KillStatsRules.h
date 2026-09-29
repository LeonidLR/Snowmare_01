#pragma once

#include "CoreMinimal.h"
#include "Data/CombatTypes.h"

/** Kills of one squad member (Godot main.gd squad_kill_stats entry; per type only hound / spitter / brute). */
struct CODEXTACTICS_API FMemberKillStats
{
	int32 Hound = 0;
	int32 Spitter = 0;
	int32 Brute = 0;
	int32 Total = 0;
	/** Commander only: kills by turrets. */
	int32 TurretKills = 0;
	/** Medic-sapper only: kills by mines. */
	int32 MineKills = 0;
};

/** Squad kill statistics for the victory panel (Godot squad_kill_stats + deployables_combat_stats kills). */
struct CODEXTACTICS_API FSquadKillStats
{
	FMemberKillStats Commander;
	FMemberKillStats Engineer;
	FMemberKillStats Medic;
	int32 TurretKills = 0;
	int32 MineKills = 0;
	int32 BarricadeKills = 0;

	int32 GetTotal() const { return Commander.Total + Engineer.Total + Medic.Total; }
};

/**
 * Pure kill statistics rules.
 * Godot reference: Scenes/movements/main.gd register_enemy_kill, _format_kill_stats_bbcode.
 */
namespace KillStatsRules
{
	/**
	 * Godot register_enemy_kill: «Турель» / «Командир» count for the commander (turret kills flagged), «Инженер» for the
	 * engineer, «Мина» / «Медик-сапёр» / «Медик» for the medic-sapper (mine kills flagged); any other source counts for
	 * the current leader — only when the leader is one of the three (the recruit has no entry). «Баррикада» also
	 * counts a barricade kill.
	 */
	CODEXTACTICS_API void RegisterKill(FSquadKillStats& Stats, EEnemyArchetype Type, const FString& Source, const FString& LeaderName);

	/** Victory panel text (Godot _format_kill_stats_bbcode without the BBCode tags). */
	CODEXTACTICS_API FString Format(const FSquadKillStats& Stats);
}
