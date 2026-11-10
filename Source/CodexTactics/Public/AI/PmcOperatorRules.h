#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The PMC Operator's data (user request 2026-11-03, docs/systems/pmc-squad-ai.md): Content/Data/AI/pmc_operators.json - the names,
 * the look variants (a coat material instance + the role-colour tint the operative material already takes, a snow share), the weapon
 * pool drawn per fighter, the stats and the faction rules (monsters vs the PMC). Pure parse / pick rules; UPmcSquadSubsystem applies
 * them when it spawns a fighter. UE-only, no Godot reference. Tested in CodexTactics.AI.PmcOperators.*.
 */
struct CODEXTACTICS_API FPmcLook
{
	FString Id = TEXT("grey");
	/** The coat tint (BaseColorFactor of the Explorer_Coat slot; the operative's BodyColor path). */
	FLinearColor Tint = FLinearColor(0.6f, 0.62f, 0.64f, 1.f);
	/** A material instance for the coat slot ("" = the Blueprint's own material; a missing asset only logs). */
	FString CoatMaterial;
	/** The mesh material slot the coat material / tint go on ("" = the operative's RoleColorMaterialSlot). */
	FString CoatSlot;
	/** The vector parameter the tint sets on that slot ("" = the operative's RoleColorParameter). */
	FString TintParameter;
	/** 0..1: how snowed-over the jacket starts (the character snow system's initial cover share). */
	float SnowShare = 0.f;
};

struct CODEXTACTICS_API FPmcWeaponChoice
{
	FString WeaponId = TEXT("m16");
	int32 Weight = 1;
};

/**
 * The squad's RPG gunner (the game designer 2026-11-09: "PMC fighters must be able to use the RPG"; pmc_operators.json "rpg_gunner"): a squad
 * (one seed) gets gunners with the chance Share; they are max_per_squad consecutive fighter indices from a seeded start among the first
 * slot_span. A gunner carries the launcher IN ADDITION to his pool weapon (in hands); the shared launcher rule draws it (LauncherTacticsRules).
 */
struct CODEXTACTICS_API FPmcRpgGunnerConfig
{
	/** The launcher's weapon id ("" = no gunners). */
	FString WeaponId = TEXT("rpg7");
	/** 0..1: the chance a squad has its gunner(s). */
	float Share = 0.5f;
	/** At most this many gunners per squad. */
	int32 MaxPerSquad = 1;
	/** The gunners are among fighter indices 0 .. slot_span - 1 (a squad smaller than that may draw none). */
	int32 SlotSpan = 3;
};

struct CODEXTACTICS_API FPmcOperatorConfig
{
	/**
	 * The PMC fighter body (user decision 2026-11-03): a skeletal mesh on a skeleton compatible with the operative's, driven by ABP_Operative
	 * (Scripts/Editor/setup_pmc_operator_body.py); "" or a missing asset = the operative Blueprint's own body.
	 */
	FString BodyMesh;
	TArray<FString> Names;
	TArray<FPmcLook> Looks;
	TArray<FPmcWeaponChoice> WeaponPool;
	/** Marksmanship / luck / cold fortitude of a fighter (the operative stats). */
	float Accuracy = 85.f;
	float Luck = 25.f;
	float Fortitude = 20.f;
	int32 Medkits = 1;
	/** Grenades are off by default: the autonomous grenade still aims at monster clusters (follow-up: team-aware grenades). */
	int32 Grenades = 0;
	/** Monsters (zombies, hounds, ...) attack PMC fighters too. */
	bool bMonstersAttackPmc = true;
	/** PMC fighters shoot a monster only in self-defence: one that hunts a mate within this distance, cm. */
	float SelfDefenceCm = 1200.f;
	/** The cold affects the PMC like the player's men (default yes, pending the user's confirmation). */
	bool bColdApplies = true;
	/** The squad's RPG gunner (2026-11-09). */
	FPmcRpgGunnerConfig RpgGunner;
};

namespace PmcOperatorRules
{
	/** The parsed file (defaults when missing); loaded once per process, Reload() re-reads it. */
	CODEXTACTICS_API const FPmcOperatorConfig& Get();
	CODEXTACTICS_API bool Reload();
	CODEXTACTICS_API FString DefaultPath();

	/** Config over Base from Json (missing keys keep Base). */
	CODEXTACTICS_API FPmcOperatorConfig FromJson(const FJsonObject& Json, const FPmcOperatorConfig& Base = FPmcOperatorConfig());

	/** A deterministic pick for fighter Index of a squad seeded Seed (the same seed + index = the same look / name / weapon). */
	CODEXTACTICS_API const FPmcLook& PickLook(const FPmcOperatorConfig& Config, int32 Seed, int32 Index);
	CODEXTACTICS_API FString PickName(const FPmcOperatorConfig& Config, int32 Seed, int32 Index);
	/** Weighted pick from the pool ("m16" when the pool is empty). */
	CODEXTACTICS_API FString PickWeaponId(const FPmcOperatorConfig& Config, int32 Seed, int32 Index);

	/** Fighter Index of the squad seeded Seed carries the RPG gunner's launcher (FPmcRpgGunnerConfig; deterministic, at most max_per_squad). */
	CODEXTACTICS_API bool IsRpgGunner(const FPmcOperatorConfig& Config, int32 Seed, int32 Index);

	/** /Game/Data/Weapons/DA_Weapon_<id>.DA_Weapon_<id> */
	CODEXTACTICS_API FString WeaponAssetPath(const FString& WeaponId);
}
