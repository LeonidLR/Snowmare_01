#pragma once

#include "CoreMinimal.h"

class AActor;
class UWorld;

/**
 * Test cheats of the combat lab (game designer decisions 2026-11-01, docs/design/COMBAT_LAB.md "Options"; UE-only, no Godot reference):
 * IMMORTALITY of the squad (an operative loses no health: UHealthComponent ignores the damage) and INFINITE AMMO (a shot never empties the
 * clip). The level JSON "cheats" turns them on by default (L_FormationLabCombat: both); the console variables Codex.Cheat.Immortal /
 * Codex.Cheat.InfiniteAmmo override the level (-1 = the level decides, 0 = off, 1 = on). Development / editor builds only: in Shipping the
 * console variables do not exist and every cheat is off whatever the level says.
 */
namespace CombatCheats
{
	/** The legacy flag and console override apply to every team. The squad flag applies only to the player team. */
	CODEXTACTICS_API bool ResolveInfiniteAmmo(int32 ConsoleValue, bool bWorldFlag, bool bSquadFlag, bool bPlayerTeam, bool bShippingBuild);

	/** Pure: in Shipping always off; a console value >= 1 on, 0 off; < 0 (the default -1) = the level's flag. */
	CODEXTACTICS_API bool Resolve(int32 ConsoleValue, bool bLevelFlag, bool bShippingBuild);

	/** The squad's immortality in World (console override, else the level JSON "cheats"."immortal"). */
	CODEXTACTICS_API bool IsImmortal(const UWorld* World);

	/** Effective infinite ammo for the given team in World. The default reports the player squad. */
	CODEXTACTICS_API bool IsInfiniteAmmo(const UWorld* World, bool bPlayerTeam = true);

	/** Actor is an operative of the squad and IsImmortal holds in its world. */
	CODEXTACTICS_API bool IsImmortalActor(const AActor* Actor);
}
