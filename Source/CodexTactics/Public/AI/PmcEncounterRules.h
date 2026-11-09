#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"

class FJsonObject;
class FJsonValue;

/**
 * What a PMC squad of a level encounter does before its first contact (user request 2026-11-03 "play against the PMC";
 * docs/systems/pmc-squad-ai.md 7). On contact the utility commander takes over in every posture.
 */
enum class EPmcPosture : uint8
{
	/** Holds an ambush round its spawn spot (the commander's Ambush order) until contact. */
	Defend,
	/** Advances on the player's last known position (the player start first, then radio intel near the squad), bounding overwatch. */
	Hunt,
	/** Walks slowly between seeded points round its spawn spot. */
	Patrol,
};

/** One "pmc_squads" entry of a level JSON (or the CodexTactics.PmcSpawn console command). UE-only, no Godot reference. */
struct CODEXTACTICS_API FPmcEncounterSpec
{
	/** Fighters in the squad ("count", 1..6). */
	int32 Count = 3;
	/** Commander weights file ("weights": absolute, project-relative or a name in Content/Data/AI). */
	FString Weights = TEXT("pmc_commander.json");
	/** "spawn": "random" (a seeded navmesh spot MinCm..MaxCm from the squad) or [x, y] / [x, y, z] (cm, a fixed spot). */
	bool bRandomSpawn = true;
	FVector FixedSpot = FVector::ZeroVector;
	/** Planar distance from the player's squad ("min_m" / "max_m", metres in the JSON). */
	float MinCm = 4000.f;
	float MaxCm = 9000.f;
	/** Seconds after the level start ("delay_s"). */
	float DelaySeconds = 20.f;
	EPmcPosture Posture = EPmcPosture::Hunt;
	/** Spawn / looks seed ("seed"); 0 = derived from the entry's index. */
	int32 Seed = 0;
};

namespace PmcEncounterRules
{
	/** "defend" / "hunt" / "patrol". */
	CODEXTACTICS_API const TCHAR* PostureName(EPmcPosture Posture);
	/** Case-insensitive; false (Out untouched) for an unknown word. */
	CODEXTACTICS_API bool ParsePosture(const FString& Text, EPmcPosture& Out);

	/** One entry over the defaults; false + OutError for a bad value (a wrong posture / spawn, count < 1). Values are clamped sensibly. */
	CODEXTACTICS_API bool ParseSquad(const FJsonObject& Json, FPmcEncounterSpec& Out, FString* OutError = nullptr);
	/** Every valid entry of the "pmc_squads" array (bad ones are skipped and listed in OutWarnings). */
	CODEXTACTICS_API TArray<FPmcEncounterSpec> ParseSquads(const TArray<TSharedPtr<FJsonValue>>& Array, TArray<FString>* OutWarnings = nullptr);

	/**
	 * The spawn spot of an encounter squad: every candidate MinCm..MaxCm (planar) from Anchor gets a FRandomStream(Seed) key (+0.15 when
	 * it is above the ground: roofs / walkways); tried best first, the first IsAcceptable(index) accepts (out of the squad's view, a full
	 * navmesh path to the squad) wins, at most MaxChecks tries. INDEX_NONE when none fits. Same input -> the same spot.
	 */
	CODEXTACTICS_API int32 PickEncounterSpot(const TArray<FPmcSpawnCandidate>& Candidates, const FVector& Anchor, float MinCm, float MaxCm, int32 Seed,
		TFunctionRef<bool(int32 Index)> IsAcceptable, int32 MaxChecks = 24);

	/**
	 * Where a hunting squad searches on its Leg-th leg (0 first): the team's last known player position when it has one; else the
	 * player start on leg 0, then the player's squad centre off by a seeded JitterCm (the "radio intel" sweep: never on the squad itself).
	 */
	CODEXTACTICS_API FVector HuntSearchPoint(bool bHasBelief, const FVector& Belief, int32 Leg, const FVector& PlayerStart, const FVector& SquadCentre,
		float JitterCm, int32 Seed);

	/**
	 * fix/pmc-hunt-honest-sight (2026-11-09, PmcEncounterHuntSmoke timed out on main): a hunting squad that met the player's men and lost them
	 * - nobody of it has SEEN one for ResumeSeconds (honest sight: heard shots do not count) - goes back to hunting (searching towards the last
	 * known spot, then the radio intel) instead of holding in cover at a remembered spot for good. Not while one of them sees an enemy now.
	 */
	CODEXTACTICS_API bool ShouldResumeHunt(bool bHuntTeam, bool bEverContact, float SecondsSinceSeen, bool bAnySeenNow, float ResumeSeconds);

	/** A patrol's Leg-th roam point: seeded, RadiusCm * 0.4..1 round Anchor (planar, Anchor's Z). */
	CODEXTACTICS_API FVector PatrolPoint(const FVector& Anchor, float RadiusCm, int32 Seed, int32 Leg);
}
