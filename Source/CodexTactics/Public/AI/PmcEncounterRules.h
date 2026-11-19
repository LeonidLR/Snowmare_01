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

/**
 * A generator-placed post of a "pmc_squads" entry (smart-enemies T-11, PMC_DEFENCE D2; the Training Ground's pmcPosts.ts, written
 * by Scripts/Editor/build_levelgen_map.py): the squad stands ON it instead of a random ring spot. UE-only, no Godot reference.
 */
struct CODEXTACTICS_API FPmcEncounterPost
{
	/** "id" of the generator's post (log / merge key); may be empty. */
	FString Id;
	/** "kind": ambush_exit / overwatch_roof / window / hiding_spot (informational; the placement is the same for all). */
	FString Kind;
	/** "pos": [x, y, z] world cm. Z 0 means "the ground, height unknown" (as the spawn spot's Z 0). */
	FVector Pos = FVector::ZeroVector;
	/** "yaw": world yaw in degrees the men face (the tactical map's look yaw). */
	float YawDeg = 0.f;
};

/**
 * How the men of one post stand round it (Content/Data/AI/pmc_operators.json "post_spawn"; missing keys keep these values, which mirror
 * the file).
 */
struct CODEXTACTICS_API FPmcPostSpawnConfig
{
	/** Between neighbours of the firing line across the post's facing (the first man is ON the post, the rest alternate right / left). */
	float LineSpacingCm = 150.f;
	/** A man whose line slot is off the navmesh / taken tries this far behind it, up to RankCount ranks. */
	float RankSpacingCm = 150.f;
	int32 RankCount = 2;
	/** Last resort: eight points on a ring this far round the post. */
	float RingCm = 140.f;
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
	/** "post": the squad spawns ON this generator post (posture defend unless the entry says otherwise, no random ring); false = unchanged. */
	bool bHasPost = false;
	FPmcEncounterPost Post;
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
	 * Smart-enemies T-11: the candidate places of the Man-th (0-based) man on a post, best first and deterministic. Man 0: the post itself;
	 * the others a firing line across the facing (alternating right / left, LineSpacingCm apart), then up to RankCount ranks behind that
	 * slot, then the ring round the post, then the post. Same Z as the post; no randomness (the generator's post is the exact place).
	 */
	CODEXTACTICS_API TArray<FVector> PostSlots(const FPmcEncounterPost& Post, int32 Man, const FPmcPostSpawnConfig& Config);
	/** The facing of a man on the post: yaw = the post's yaw (normalised to -180..180), no pitch / roll. */
	CODEXTACTICS_API FRotator PostFacing(const FPmcEncounterPost& Post);
	/** The "post_spawn" block of a JSON object (a missing block / key keeps the defaults). */
	CODEXTACTICS_API FPmcPostSpawnConfig PostSpawnConfigFromJson(const FJsonObject& Json);
	/** pmc_operators.json's "post_spawn" (Path empty = Content/Data/AI/pmc_operators.json); defaults when the file / block is missing. */
	CODEXTACTICS_API FPmcPostSpawnConfig LoadPostSpawnConfig(const FString& Path = FString());

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
