#pragma once

#include "CoreMinimal.h"
#include "Data/WeaponStats.h"

class FJsonObject;

/**
 * The mortar nests' pure rules (2026-11-09; docs/design/SQUAD_SIX_FIRE_TEAMS.md 7.3 - the designer's decision: a fixed nest, both sides, a crew of
 * three, high-angle plunging fire, a minimum and a maximum range, a long flight, simplified bracketing, the spotter rule; docs/systems/weapons.md
 * "Mortar nests"; new - no Godot reference). The nest's data (weapons_tuning.json "mortar_nest": the crew, the range band, the rate, the ammo,
 * the bracketing and the round in the weapon schema), the fire checks in order, the bracketing walk, the PMC's target cluster, and the level
 * JSON "mortar_nests" placement. The world part is AMortarNestActor / UMortarSubsystem. UE-only. Tested in CodexTactics.Weapons.Mortar.*.
 */

/** Simplified bracketing: the first round lands FirstErrorCm off the aim point, each next one at the same point x Shrink, never under MinErrorCm. */
struct CODEXTACTICS_API FMortarBracketing
{
	float FirstErrorCm = 1500.f;
	float Shrink = 0.55f;
	float MinErrorCm = 150.f;
	/** A new aim point farther than this from the last one starts the walk again, cm. */
	float SameTargetCm = 1000.f;
};

/** One nest's data (weapons_tuning.json "mortar_nest"). */
struct CODEXTACTICS_API FMortarNestConfig
{
	/** Men needed at the nest; fewer = no fire (the designer: three). */
	int32 CrewNeeded = 3;
	/** The crew's places round the tube, cm from its centre. */
	float CrewRadiusCm = 160.f;
	/** A crewman farther than this from his place has left the nest, cm. */
	float CrewLeaveCm = 250.f;
	float MinRangeCm = 3000.f;
	float MaxRangeCm = 25000.f;
	/** Between rounds, s. */
	float IntervalSeconds = 3.5f;
	/** Rounds per fire order (then it waits for the next order). */
	int32 RoundsPerOrder = 6;
	/** Rounds a nest holds at the start. */
	int32 Ammo = 24;
	/** The spotter's sight point is this far above the aim point (a man sees the spot, not the snow under it), cm. */
	float SpotHeightCm = 60.f;
	FMortarBracketing Bracketing;
	/** The round: kind projectile (trajectory high_angle), explosion block, base_damage = the blast's base (the weapon schema). */
	FWeaponStats Round;
};

/** Why a nest does not fire (in the order it is checked). */
enum class EMortarRefusal : uint8
{
	None,
	/** Fewer men at the nest than CrewNeeded. */
	Crew,
	NoAmmo,
	/** Inside the minimum range (the round cannot come down that close). */
	TooClose,
	TooFar,
	/** Nobody on the nest's side sees the point now (the spotter rule). */
	NotSpotted
};

/** One "mortar_nests" entry of a level JSON. */
struct CODEXTACTICS_API FMortarNestPlacement
{
	FString Id;
	FVector Location = FVector::ZeroVector;
	/** Whether the JSON gave a z (else the nest is dropped onto the ground). */
	bool bHasZ = false;
	float YawDeg = 0.f;
	/** 0 = the player's side; 1.. = a PMC team (a free id when Crew > 0 and no team is given: -1). */
	int32 TeamId = 0;
	/** PMC fighters spawned at the nest as its crew (0 = it waits for a crew). */
	int32 Crew = 0;
	/** Overrides the data's ammo (< 0 = the data's). */
	int32 Ammo = -1;
};

namespace MortarRules
{
	/** The fire checks, in order: crew, ammo, the range band (horizontal distance), the spotter. */
	CODEXTACTICS_API EMortarRefusal Check(int32 CrewAtNest, int32 Ammo, float HorizontalCm, bool bSpotted, const FMortarNestConfig& Config);

	CODEXTACTICS_API FString RefusalText(EMortarRefusal Refusal);

	/** The bracketing error of the RoundOnTarget-th round (0 = the first) at the same aim point, cm. */
	CODEXTACTICS_API float BracketErrorCm(int32 RoundOnTarget, const FMortarBracketing& Bracketing);

	/** Where a round aimed at Target lands before its own flight: ErrorCm off in the direction AngleRoll x 360 degrees (ground plane). */
	CODEXTACTICS_API FVector BracketPoint(const FVector& Target, float ErrorCm, float AngleRoll01);

	/** The aim point is the one being walked in (within SameTargetCm of the last). */
	CODEXTACTICS_API bool IsSameTarget(const FVector& Last, const FVector& New, const FMortarBracketing& Bracketing);

	/** The crew's place Index (0..CrewNeeded-1) round a nest at Centre facing YawDeg. */
	CODEXTACTICS_API FVector CrewPlace(const FVector& Centre, float YawDeg, int32 Index, const FMortarNestConfig& Config);

	/**
	 * The PMC's target: the seen point with the most others within RadiusCm (ties: the nearer to the nest), its count, and the centroid of that
	 * cluster. False for no points.
	 */
	CODEXTACTICS_API bool BestCluster(const TArray<FVector>& Points, float RadiusCm, const FVector& Nest, FVector& OutCentre, int32& OutCount);

	/** Reads "mortar_nest" over the defaults; the round goes through the weapon schema (WeaponStatsRules). Bad values: kept + OutErrors. */
	CODEXTACTICS_API FMortarNestConfig Parse(const FJsonObject& Block, TArray<FString>* OutErrors = nullptr);
	CODEXTACTICS_API FMortarNestConfig Defaults();
	CODEXTACTICS_API void Set(const FMortarNestConfig& Config);
	CODEXTACTICS_API const FMortarNestConfig& Get();

	/** A level JSON root's "mortar_nests" array (missing = none). */
	CODEXTACTICS_API TArray<FMortarNestPlacement> ParsePlacements(const FJsonObject& LevelRoot, TArray<FString>* OutErrors = nullptr);
}
