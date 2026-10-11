#pragma once

#include "CoreMinimal.h"

/**
 * Which class a squad member spawns from (user request 2026-10-11: a member may have its own Blueprint, e.g. the
 * Medic-Sapper's BP_Operative_FemaleSoldier with a backpack, a child of BP_Operative).
 * No Godot reference: the Godot squad had one scene per member (Scenes/movements/main.tscn Player / Follower1 / Follower2).
 */
namespace SquadSpawnRules
{
	/** The spawn class of one member and whether the roster's BodyMesh override applies to it. */
	struct FMemberClassChoice
	{
		/** The class to spawn (nullptr only when DefaultClass is nullptr too). */
		UClass* Class = nullptr;

		/** True = FSquadMemberSpawn::BodyMesh replaces the Blueprint's mesh; false = the member Blueprint owns the look. */
		bool bApplyBodyMesh = true;

		/** The member Blueprint was set but could not be loaded (the caller logs the fallback). */
		bool bMemberBlueprintMissing = false;
	};

	/**
	 * bMemberBlueprintSet: FSquadMemberSpawn::MemberBlueprint is not empty; MemberClass: that class loaded (nullptr =
	 * missing); DefaultClass: the game mode's operative Blueprint (else its C++ fallback class).
	 * Set and loaded -> MemberClass, no BodyMesh; otherwise -> DefaultClass with BodyMesh (the behaviour before 2026-10-11).
	 */
	CODEXTACTICS_API FMemberClassChoice ChooseMemberClass(bool bMemberBlueprintSet, UClass* MemberClass, UClass* DefaultClass);
}
