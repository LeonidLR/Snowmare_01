#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"

class FJsonObject;

/**
 * The PMC squad commander's utility scoring (user request 2026-11-03: "very smart, human-like PMC fighters - a real fight between
 * humanoid squads"; UE-only, no Godot reference). Pure rules, no world access: UPmcSquadSubsystem builds the views every
 * DecisionInterval and executes the winning order through the shared SquadOrderCatalog. Every consideration is a 0..1 factor,
 * every order's score = weight x the product of its considerations; the weights file (Content/Data/AI/pmc_commander.json, one
 * file = one "personality") is what the overnight trainer will tune later. Tested in CodexTactics.AI.PmcCommander.*.
 */

/** The orders the commander can give one fighter (the SquadOrderCatalog entries it maps onto). */
enum class EPmcOrder : uint8
{
	/** Keep the position and the current cover; the operative's own fire does the rest. */
	Hold,
	/** Walk to the best cover slot against the nearest threat. */
	TakeCover,
	/** Bounding overwatch: close in towards the nearest known threat while a mate stays put. */
	Advance,
	/** Walk to a point 90 deg off the threat -> squad line, on the threat's outer side. */
	Flank,
	/** Suppressive fire on a seen threat while a mate flanks / advances. */
	Suppress,
	/** Break contact: a cover behind, away from the threats. */
	FallBack,
	/** Walk to a wounded mate and spend a medkit. */
	Heal,
	/** Spotted at this spot for too long: another cover slot. */
	Relocate,
	/** No contact yet: crouch in cover, wait for the enemy. */
	Ambush,
	/** Reload behind cover / out of contact. */
	Reload,
	/** The sidearm at point-blank range with an empty rifle. */
	Sidearm,
	/** Crouch where he stands (in contact, not moving, not in cover). */
	Crouch,
	Count
};

/** One weight per order plus the thresholds the considerations use (pmc_commander.json). */
struct CODEXTACTICS_API FPmcWeights
{
	FString Name = TEXT("default");
	float Hold = 0.35f;
	float TakeCover = 1.0f;
	float Advance = 0.7f;
	float Flank = 0.9f;
	float Suppress = 0.8f;
	float FallBack = 0.9f;
	float Heal = 1.1f;
	float Relocate = 0.6f;
	float Ambush = 1.0f;
	float Reload = 0.8f;
	float Sidearm = 1.2f;
	float Crouch = 0.5f;
	/** A mate below this health fraction is a patient. */
	float HealBelow = 0.45f;
	/** Below this health fraction the fighter breaks contact. */
	float FallBackBelow = 0.35f;
	/** The ammo fraction a suppressor needs. */
	float SuppressAmmoMin = 0.4f;
	/** Advance stops at this fraction of the weapon range from the threat. */
	float AdvanceStopRangeFrac = 0.6f;
	/** The flank point's distance from the threat, cm, and its angle off the threat -> squad line, degrees. */
	float FlankDistanceCm = 1000.f;
	float FlankAngleDeg = 90.f;
	/** Seen by the enemy at one spot this long -> relocate. */
	float RelocateAfterSeconds = 6.f;
	/** A threat not seen for this long is "lost" (no contact). */
	float ContactMemorySeconds = 5.f;
	/** The commander decides this often, s. */
	float DecisionInterval = 0.5f;
	/** An order keeps running while its score is within this of the best (hysteresis). */
	float SwitchMargin = 0.1f;
	/** A running WALK (advance / flank / fall back / cover) is only broken by a score this much above it (the man commits to his move). */
	float MoveSwitchMargin = 0.45f;
	/** Any order holds at least this long before another replaces it, s. */
	float CommitSeconds = 2.f;
	/**
	 * High ground (user request 2026-11-03, the Depot arena): a cover / relocate / ambush position's score grows by this per height
	 * level above the ground (3 m steps, SightRules::HeightLevel), up to HighGroundMaxLevels levels. 0 = height ignored.
	 */
	float HighGround = 0.2f;
	/** The level count the high-ground bonus stops growing at. */
	int32 HighGroundMaxLevels = 3;
};

/** One spawn candidate of the arena's seeded spawn pairs: a navmesh point and its height level (0 = the ground). */
struct CODEXTACTICS_API FPmcSpawnCandidate
{
	FVector Location = FVector::ZeroVector;
	int32 Level = 0;
};

/** What the commander knows about the nearest threat of one fighter. */
struct CODEXTACTICS_API FPmcThreatView
{
	FVector Location = FVector::ZeroVector;
	float DistanceCm = 0.f;
	float SecondsSince = 0.f;
	bool bSeenNow = false;
};

/** One fighter as the commander sees him. */
struct CODEXTACTICS_API FPmcFighterView
{
	FVector Location = FVector::ZeroVector;
	float HealthFrac = 1.f;
	/** Rounds in the clip over the clip size. */
	float ClipFrac = 1.f;
	/** Clip + reserve over clip + default reserve. */
	float AmmoFrac = 1.f;
	bool bHasReserve = true;
	bool bInCover = false;
	/** 0..1: how well the current spot / cover shields him from the nearest threat (1 = the wall is between). */
	float CoverQuality = 0.f;
	/** A cover slot was found nearby (TakeCover / Relocate have somewhere to go). */
	bool bCoverAvailable = false;
	/** The enemy sees him now (or he demasked himself by firing). */
	bool bExposed = false;
	/** Seconds he has been at his spot while exposed. */
	float SecondsExposedHere = 0.f;
	EOperativeStance Stance = EOperativeStance::Standing;
	bool bMoving = false;
	bool bHasMedkit = false;
	bool bHasSidearm = false;
	bool bKnockedDown = false;
	float WeaponRangeCm = 1400.f;
	/** The nearest known threat (bHasThreat false: none known). */
	bool bHasThreat = false;
	FPmcThreatView Threat;
	/** The nearest wounded mate (-1: none) and his health fraction. */
	float WoundedMateDistanceCm = -1.f;
	float WoundedMateHealthFrac = 1.f;
};

/** The squad as a whole. */
struct CODEXTACTICS_API FPmcSquadView
{
	int32 Alive = 0;
	int32 EnemiesKnown = 0;
	int32 EnemiesSeenNow = 0;
	/** Any threat seen within ContactMemorySeconds. */
	bool bContact = false;
	/** The squad has met the enemy in this fight at all (ends the ambush hold). */
	bool bEverContact = false;
	int32 MatesSuppressing = 0;
	int32 MatesFlanking = 0;
	int32 MatesAdvancing = 0;
	int32 MatesHealing = 0;
};

struct CODEXTACTICS_API FPmcOrderScore
{
	EPmcOrder Order = EPmcOrder::Hold;
	float Score = 0.f;
};

namespace PmcCommanderRules
{
	/** The order's name for logs / JSON ("Hold", "TakeCover", ...). */
	CODEXTACTICS_API const TCHAR* OrderName(EPmcOrder Order);

	/** Alive - known enemies as a 0..1 factor (0.5 = even; 0 = outnumbered by 2+; 1 = 2+ more men). */
	CODEXTACTICS_API float NumbersFactor(int32 Alive, int32 EnemiesKnown);

	/** The weight x the product of the order's considerations for Fighter in Squad; 0 when the order makes no sense now. */
	CODEXTACTICS_API float ScoreOrder(EPmcOrder Order, const FPmcFighterView& Fighter, const FPmcSquadView& Squad, const FPmcWeights& Weights);

	/** Every order scored, best first. */
	CODEXTACTICS_API TArray<FPmcOrderScore> ScoreAll(const FPmcFighterView& Fighter, const FPmcSquadView& Squad, const FPmcWeights& Weights);

	/**
	 * The order to run now: the best scored one, unless Current (still running) scores within Weights.SwitchMargin of it - then
	 * Current stays (hysteresis). Hold when nothing scores above 0.
	 */
	CODEXTACTICS_API EPmcOrder Pick(const TArray<FPmcOrderScore>& Scored, EPmcOrder Current, bool bCurrentRunning, const FPmcWeights& Weights);
	/** Pick with an explicit hysteresis margin (a committed walk uses Weights.MoveSwitchMargin). */
	CODEXTACTICS_API EPmcOrder PickWithMargin(const TArray<FPmcOrderScore>& Scored, EPmcOrder Current, bool bCurrentRunning, float Margin);

	/** The order is a walk the man commits to (advance / flank / fall back / relocate / take cover). */
	CODEXTACTICS_API bool IsMoveOrder(EPmcOrder Order);

	/**
	 * The flank point: FlankDistanceCm from Threat, FlankAngleDeg off the Threat -> SquadCentre line, on the threat's side away from
	 * Fighter (so the two men come at the threat from different directions); planar, Z of the threat.
	 */
	CODEXTACTICS_API FVector FlankPoint(const FVector& Threat, const FVector& SquadCentre, const FVector& Fighter, const FPmcWeights& Weights);

	/** The advance goal: on the Fighter -> Threat line, AdvanceStopRangeFrac x WeaponRangeCm short of the threat (the fighter's spot when already that close). */
	CODEXTACTICS_API FVector AdvancePoint(const FVector& Fighter, const FVector& Threat, float WeaponRangeCm, const FPmcWeights& Weights);

	/** The fall-back goal: DistanceCm straight away from the threat (planar). */
	CODEXTACTICS_API FVector FallBackPoint(const FVector& Fighter, const FVector& Threat, float DistanceCm = 800.f);

	/**
	 * How well a cover slot shields from Threat: the slot's wall is BEHIND the man facing along WallNormal, so it covers threats on
	 * the -WallNormal side: clamp(dot(-WallNormal, dir(slot -> threat)), 0, 1).
	 */
	CODEXTACTICS_API float CoverQualityAgainst(const FVector& SlotLocation, const FVector& WallNormal, const FVector& Threat);

	/** The angle (deg, 0..180) between the Threat -> A and Threat -> B directions (planar): a flank is >= 60 off the frontal line. */
	CODEXTACTICS_API float ApproachAngleDeg(const FVector& Threat, const FVector& A, const FVector& B);

	/** The high-ground factor of a position Level height levels above the ground: 1 + HighGround x clamp(Level, 0, HighGroundMaxLevels). */
	CODEXTACTICS_API float HighGroundFactor(int32 Level, const FPmcWeights& Weights);

	/**
	 * The arena's seeded spawn pair (Spawns=Random): every candidate pair MinDistCm..MaxDistCm apart (planar) gets a key from a
	 * FRandomStream(Seed) draw plus a variety bias (+0.35 when the two lie on different levels, +0.15 when either is above the ground);
	 * the pairs are tried by key, best first, and the first one IsReachable(A, B) accepts (at most MaxReachChecks tries) wins; the
	 * seed also decides which of the two is side A. Same candidates + seed + reachability -> the same pair. False when none fits.
	 */
	CODEXTACTICS_API bool PickSpawnPair(const TArray<FPmcSpawnCandidate>& Candidates, float MinDistCm, float MaxDistCm, int32 Seed,
		TFunctionRef<bool(int32 A, int32 B)> IsReachable, int32& OutA, int32& OutB, int32 MaxReachChecks = 48);

	/** Parameters of Json over Base (unknown keys ignored, missing ones keep Base). Keys = the field names in snake_case ("take_cover", "heal_below"). */
	CODEXTACTICS_API FPmcWeights FromJson(const FJsonObject& Json, const FPmcWeights& Base = FPmcWeights());

	/** Loads a weights file (absolute, or relative to Content/Data/AI); false (defaults) when missing / unreadable. */
	CODEXTACTICS_API bool LoadWeightsFile(const FString& PathOrName, FPmcWeights& Out);

	/** Content/Data/AI/pmc_commander.json. */
	CODEXTACTICS_API FString DefaultWeightsPath();
}
