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
	/**
	 * The round's objective point (task #53, the arena): an attacker walks onto it (he wins by holding it), a defender goes back to it
	 * when the attackers stand on it or he strayed out of its overwatch.
	 */
	Objective,
	Count
};

/** A side's role in an objective round (the arena, task #53): the attackers take the point, the defenders hold it. */
enum class EPmcRole : uint8
{
	None,
	Attacker,
	Defender
};

/** The round's objective as one fighter's commander sees it (inactive: Role None - every objective factor is 1, the Objective order 0). */
struct CODEXTACTICS_API FPmcObjectiveView
{
	EPmcRole Role = EPmcRole::None;
	FVector Location = FVector::ZeroVector;
	/** Planar distance fighter -> point, cm. */
	float DistanceCm = 0.f;
	/** The hold radius, cm (attackers within it and no live defender within it = the attackers hold the point). */
	float RadiusCm = 0.f;
	/** 0..1 of the round's time limit gone (the attackers' urgency). */
	float TimeFrac = 0.f;
	/** 0..1 of the attackers' capture clock (HoldSeconds held without a defender on the point). */
	float HoldFrac = 0.f;
	/** An enemy stands within the radius (the attackers for a defender, the defenders for an attacker). */
	bool bEnemyOnPoint = false;

	bool IsActive() const { return Role != EPmcRole::None && RadiusCm > 0.f; }
};

/**
 * The capture clock of one objective round (pure; the arena steps it): the attackers hold the point while at least one live attacker
 * and no live defender stands within the radius; HoldSeconds of hold, summed over stints, capture it (user decision 2026-11-07:
 * cumulative with a reset - a contested or empty point only pauses the clock, the defenders retaking it alone resets it).
 * Per side the seconds of sole control (own men on the point, no enemy) for the result.
 */
struct CODEXTACTICS_API FPmcObjectiveClock
{
	float HoldSeconds = 15.f;
	/** The attackers' cumulative hold since the defenders last retook the point, s. */
	float Held = 0.f;
	bool bAttackersHolding = false;
	bool bCaptured = false;
	/** Seconds of sole control per role: [0] attackers, [1] defenders. */
	float ControlSeconds[2] = { 0.f, 0.f };

	/** One step: the live attackers / defenders within the radius now, DeltaSeconds since the last step. True when this step captured it. */
	bool Step(int32 AttackersOnPoint, int32 DefendersOnPoint, float DeltaSeconds);
	float HoldFrac() const { return HoldSeconds > 0.f ? FMath::Clamp(Held / HoldSeconds, 0.f, 1.f) : 0.f; }
};

/** How an arena round ended (pure: PmcCommanderRules::ResolveRound). Winner "A" / "B" / "draw"; Reason elimination / objective / time_limit. */
struct CODEXTACTICS_API FPmcRoundOutcome
{
	FString Winner = TEXT("draw");
	FString Reason = TEXT("time_limit");
	/** captured (the attackers took it) / held (the defenders kept it to the time limit) / contested (attackers on it at the limit) / none. */
	FString ObjectiveResult = TEXT("none");
	bool bOver = false;
};

/** The arena's round scenarios (task #53): Scenario=random picks one per round from the round seed. */
enum class EPmcScenario : uint8
{
	/** The seeded random spawn pair (any levels), the objective near the defender. */
	Skirmish,
	/** The objective + the defender's spawn at a narrow passage (1-3 m between walls >= 3 m). */
	Chokepoint,
	/** The objective on open ground. */
	Street,
	/** The objective on a roof level. */
	Roofs,
	/** A skirmish in a blizzard (the sight halved). */
	Blizzard,
	Count
};

/**
 * A weapon's preferred engagement band (user request 2026-11-04: "a man with an assault rifle keeps as far from his target as his weapon allows,
 * fires from corners, prone, never fights at point-blank range; with a pistol he must come closer but still not point-blank; the shotgun is the
 * close-range weapon"): the distance to the threat he wants to fight from, cm. pmc_commander.json "engagement_bands" { "<weapon id>": { "min_m",
 * "max_m", "close_quarters" } }; PmcCommanderRules::ResolveBand caps it at the weapon's reach.
 */
struct CODEXTACTICS_API FPmcEngagementBand
{
	float MinCm = 0.f;
	float MaxCm = 0.f;
	/** A close-range weapon (the shotgun): no "too close" retreat, it closes in to its band instead. */
	bool bCloseQuarters = false;

	bool IsValid() const { return MaxCm > 0.f && MaxCm >= MinCm; }
};

/** One weight per order plus the thresholds the considerations use (pmc_commander.json). */
struct CODEXTACTICS_API FPmcWeights
{
	/** The defaults below plus the built-in engagement bands (PmcCommanderRules::DefaultBands). */
	FPmcWeights();

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
	/**
	 * Mortar nests (2026-11-09, SQUAD_SIX_FIRE_TEAMS.md 7.3): a manned nest of the team fires at the biggest cluster of enemies the team SEES now
	 * - score = mortar_fire x min(1, cluster / mortar_cluster_full) (0 out of the nest's range band); it fires at >= mortar_threshold.
	 */
	float MortarFire = 0.8f;
	float MortarThreshold = 0.3f;
	float MortarClusterRadiusCm = 800.f;
	float MortarClusterFull = 3.f;
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

	// --- Engagement bands (user request 2026-11-04: riflemen keep their distance; docs/systems/pmc-squad-ai.md 4) ---

	/** "use_engagement_bands" (a bool, not a trainer gene): false = the pre-2026-11-04 behaviour (no bands; A/B). */
	bool bUseBands = true;
	/** Per weapon id (weapons_tuning.json ids: m16, sniper_rifle, pistol, shotgun, ...); an unknown id gets DefaultBandFracs x its reach. */
	TMap<FString, FPmcEngagementBand> Bands;
	/** The band of a weapon with no row: [DefaultBandMinFrac, DefaultBandMaxFrac] x its reach. */
	float DefaultBandMinFrac = 0.5f;
	float DefaultBandMaxFrac = 0.95f;
	/** The band never reaches past this fraction of the weapon's reach (he must still hit from there). */
	float BandReachFrac = 0.95f;
	/** The "too close" retreat (a FallBack to the band, cover at band distance preferred): its weight. */
	float Standoff = 1.3f;
	/** Too close = the threat nearer than this fraction of the band's min (close-quarters weapons never retreat). */
	float StandoffTriggerFrac = 0.8f;
	/** How much a static order (Hold / Suppress / Crouch / TakeCover's slot) loses outside the band: x lerp(1, BandFactor, this). */
	float BandPosition = 0.7f;
	/** BandFactor's floor below the band's min (1 = no penalty, 0 = the full penalty at point-blank range). */
	float BandCloseFloor = 0.1f;
	/** A cover slot at a corner (a free edge to peek / fire round) scores x (1 + this). */
	float CornerBonus = 0.2f;
	/** A close-quarters weapon's Advance towards its band scores x this (the shotgunner closes in). */
	float CloseIn = 1.6f;
	/** Prone instead of crouched (the Crouch order in the open) for a weapon whose band starts at least this far, cm (rifles, not pistols / shotguns). */
	float ProneMinBandCm = 1500.f;

	// --- The round objective (task #53, the arena: attackers take the point, defenders hold it; trainer genes +-50 %) ---

	/** The Objective order's weight (an attacker onto the point, a defender back to it). */
	float Objective = 1.0f;
	/** The attacker's time pressure: the Objective order x (1 - this + this x the round's time fraction). */
	float ObjectiveUrgency = 0.65f;
	/** Attackers value positions on the way to / round the point: a cover slot one radius nearer x (1 + this); Hold on the point x (1 + this). */
	float ObjectiveApproach = 0.5f;
	/** Defenders value overwatch of the approaches: a slot / Hold within reach of the point x (1 + this). */
	float Overwatch = 0.5f;

	// --- Target attention (user 2026-11-07: strike when the enemy looks away; trainer genes +-50 %) ---

	/** Flank / Advance (not a close-quarters weapon's) / an attacker's Objective walk x (1 + this x (AttentionOff - 0.5)): moving while they look away is worth more. */
	float Attention = 0.6f;
	/** An objective defender in ambush holds his fire while the seen target's AttentionOff is below this (he still looks our way). 0 = off. */
	float AmbushPatience = 0.5f;
	/** ... unless the target is deep in the kill zone: nearer than this x his band's max (the weapon's reach without a band), or on the point. */
	float KillZoneFrac = 0.6f;
	/** ... and at most this long after the team's first contact, s (then it fires whatever he looks at). */
	float AmbushMaxHoldSeconds = 20.f;
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
	/**
	 * No enemy known: the team's assault point (the arena's objective / the defender's spawn) stands in for a threat so the men advance
	 * on it (task #53: no "too close" standoff from it and no band penalty near it - an attacker must be able to stand ON the point).
	 */
	bool bStandIn = false;
	/**
	 * Target attention (user 2026-11-07: "strike when they look away, through the blind zones"): how far the fighter stands outside the
	 * look sectors of the enemies his team SEES now (PmcCommanderRules::AttentionOff; their facing as seen): 0 = one looks right at him,
	 * 1 = all look away; 0.5 = unknown (nobody seen now).
	 */
	float AttentionOff = 0.5f;
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
	/** His weapon's engagement band (ResolveBand; invalid = no bands: the old rules). */
	FPmcEngagementBand Band;
	/** The band factor (BandFactor) of the best cover slot nearby (bCoverAvailable): 1 = at band distance from the threat. */
	float CoverBandFactor = 1.f;
	/**
	 * The order he is still running (Count: none). FallBack: the standoff keeps scoring until he reaches the band's middle (not only below the
	 * trigger); Flank: no standoff - the flank walk round the threat passes it closer than the band on purpose (PmcCommanderSmoke).
	 */
	EPmcOrder RunningOrder = EPmcOrder::Count;
	/** The nearest known threat (bHasThreat false: none known). */
	bool bHasThreat = false;
	FPmcThreatView Threat;
	/** The nearest wounded mate (-1: none) and his health fraction. */
	float WoundedMateDistanceCm = -1.f;
	float WoundedMateHealthFrac = 1.f;
	/** The round's objective (the arena, task #53; inactive elsewhere). */
	FPmcObjectiveView Objective;
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
	/** A shot was fired between this team and an enemy in this fight (an ambush holding its fire is blown). */
	bool bShotsExchanged = false;
	/** Seconds since the team's first contact of the fight (0 before it). */
	float ContactSeconds = 0.f;
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

	/** A team's mortar nest: the fire score at a seen cluster of ClusterCount enemies (0 unmanned / out of the band). */
	CODEXTACTICS_API float ScoreMortarFire(int32 ClusterCount, bool bManned, bool bInRangeBand, const FPmcWeights& Weights);

	/**
	 * The flank point: DistanceCm (>= 0; else FlankDistanceCm - the commander passes FlankDistanceFor his band) from Threat, FlankAngleDeg off the Threat -> SquadCentre line, on the threat's side away from
	 * Fighter (so the two men come at the threat from different directions); planar, Z of the threat.
	 */
	CODEXTACTICS_API FVector FlankPoint(const FVector& Threat, const FVector& SquadCentre, const FVector& Fighter, const FPmcWeights& Weights,
		float DistanceCm = -1.f);

	/**
	 * The advance goal: on the Fighter -> Threat line, AdvanceStopCm short of the threat (AdvanceStopRangeFrac x WeaponRangeCm, clamped into
	 * Band when it is valid); the fighter's spot when already that close.
	 */
	CODEXTACTICS_API FVector AdvancePoint(const FVector& Fighter, const FVector& Threat, float WeaponRangeCm, const FPmcWeights& Weights,
		const FPmcEngagementBand& Band = FPmcEngagementBand());

	/** The fall-back goal: DistanceCm straight away from the threat (planar). */
	CODEXTACTICS_API FVector FallBackPoint(const FVector& Fighter, const FVector& Threat, float DistanceCm = 800.f);

	/**
	 * How well a cover slot shields from Threat: the slot's wall is BEHIND the man facing along WallNormal, so it covers threats on
	 * the -WallNormal side: clamp(dot(-WallNormal, dir(slot -> threat)), 0, 1).
	 */
	CODEXTACTICS_API float CoverQualityAgainst(const FVector& SlotLocation, const FVector& WallNormal, const FVector& Threat);

	/** The angle (deg, 0..180) between the Threat -> A and Threat -> B directions (planar): a flank is >= 60 off the frontal line. */
	CODEXTACTICS_API float ApproachAngleDeg(const FVector& Threat, const FVector& A, const FVector& B);

	/**
	 * The band of WeaponId (Weights.Bands, else DefaultBandFracs x WeaponRangeCm), capped at the weapon's reach: max = min(band max,
	 * BandReachFrac x WeaponRangeCm), min = min(band min, 0.8 x that max). Invalid (MaxCm 0) when !Weights.bUseBands or no reach.
	 */
	/**
	 * The built-in bands (NUMBERS PENDING the user's confirmation; weapons_tuning.json ids): m16 / plasma_carbine (rifles) 25-60 m, sniper_rifle
	 * 45-110 m, pistol 10-25 m, shotgun 4-14 m (close quarters), lmg 30-70 m (no such weapon yet).
	 */
	CODEXTACTICS_API TMap<FString, FPmcEngagementBand> DefaultBands();

	CODEXTACTICS_API FPmcEngagementBand ResolveBand(const FString& WeaponId, float WeaponRangeCm, const FPmcWeights& Weights);

	/**
	 * How good a fighting distance is for Band, 0..1: 1 inside [MinCm, MaxCm]; below MinCm down to BandCloseFloor at 0 (quadratic: a strong
	 * penalty close in); past MaxCm down to 0.5 at 2 x MaxCm (out of reach, not dangerous). 1 for an invalid band.
	 */
	CODEXTACTICS_API float BandFactor(float DistanceCm, const FPmcEngagementBand& Band, const FPmcWeights& Weights);

	/** The threat is "too close" for Band: nearer than StandoffTriggerFrac x MinCm, and the weapon is not a close-quarters one. */
	CODEXTACTICS_API bool IsTooClose(float DistanceCm, const FPmcEngagementBand& Band, const FPmcWeights& Weights);

	/** Where an Advance stops short of the threat: clamp(AdvanceStopRangeFrac x reach, band min, band max) (the old fraction without a band). */
	CODEXTACTICS_API float AdvanceStopCm(float WeaponRangeCm, const FPmcEngagementBand& Band, const FPmcWeights& Weights);

	/** The flank point's distance from the threat: FlankDistanceCm clamped into the band (FlankDistanceCm without one). */
	CODEXTACTICS_API float FlankDistanceFor(const FPmcEngagementBand& Band, const FPmcWeights& Weights);

	/** The standoff goal: straight away from the threat to the middle of the band (planar; the fighter's spot when already that far). */
	CODEXTACTICS_API FVector StandoffPoint(const FVector& Fighter, const FVector& Threat, const FPmcEngagementBand& Band);

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

	// --- The round objective (task #53) ---

	/**
	 * How far from the point a defender still overwatches it, cm: his band's max (the weapon's reach x 0.8 without a band), at least
	 * 2 x the radius.
	 */
	CODEXTACTICS_API float OverwatchRangeCm(const FPmcObjectiveView& Objective, const FPmcEngagementBand& Band, float WeaponRangeCm);

	/**
	 * The objective consideration of a position (a cover slot) for a fighter at FighterLocation: an attacker x max(0.25, 1 +
	 * ObjectiveApproach x gain), gain = (his distance to the point - the slot's) / the radius in -1..1 (1 inside the radius); a defender
	 * x (1 + Overwatch x reach), reach = 1 within OverwatchRangeCm of the point, falling to 0 at twice that. 1 when inactive.
	 */
	CODEXTACTICS_API float ObjectivePositionFactor(const FVector& Position, const FVector& FighterLocation, const FPmcObjectiveView& Objective,
		const FPmcEngagementBand& Band, float WeaponRangeCm, const FPmcWeights& Weights);

	/**
	 * The round's end (the arena): a side down = elimination (both down: a draw); the attackers' capture = objective; the time limit =
	 * the defenders win when the point is not held by the attackers at that moment (else a contested draw). Without an objective the
	 * time limit is a draw. bAttackerIsA: side A attacks. bOver false while the round goes on.
	 */
	CODEXTACTICS_API FPmcRoundOutcome ResolveRound(int32 AliveA, int32 AliveB, bool bTimeUp, bool bObjective, const FPmcObjectiveClock& Clock, bool bAttackerIsA);

	/**
	 * Target attention at Point (user 2026-11-07): for every watcher (an enemy the team sees now: his position and facing as seen) within
	 * RangeCm, how far Point lies outside his look sector: clamp((angle off his look - HalfAngleDeg) / 45, 0, 1); the most attentive
	 * watcher decides (the minimum). 1 with no watcher in range (nobody looks).
	 */
	CODEXTACTICS_API float AttentionOff(const TArray<FVector2D>& WatcherPositions, const TArray<float>& WatcherYawsDeg, const FVector2D& Point,
		float HalfAngleDeg = 45.f, float RangeCm = 5000.f);

	/** The attention consideration of a move: 1 + Attention x (Threat.AttentionOff - 0.5) while a threat is seen now (>= 0), else 1. */
	CODEXTACTICS_API float AttentionFactor(const FPmcFighterView& Fighter, const FPmcWeights& Weights);

	/**
	 * An objective defender in ambush holds his fire (the Passive posture) while no shot was exchanged in the fight yet, the enemy does not
	 * see him and his first contact is younger than AmbushMaxHoldSeconds - lying in wait with nobody seen now, or with a seen threat that
	 * is not deep in the kill zone (KillZoneFrac x his reach, or on the point) and still looks his way (AttentionOff < AmbushPatience).
	 */
	CODEXTACTICS_API bool ShouldHoldFire(const FPmcFighterView& Fighter, const FPmcSquadView& Squad, const FPmcWeights& Weights);

	/** The scenario's id in args / JSON ("skirmish", "chokepoint", "street", "roofs", "blizzard"). */
	CODEXTACTICS_API const TCHAR* ScenarioName(EPmcScenario Scenario);
	/** Parses a scenario id; false for "random" or an unknown id. */
	CODEXTACTICS_API bool ParseScenario(const FString& Name, EPmcScenario& Out);
	/** Scenario=random: the round's scenario from its seed (uniform over the five, deterministic). */
	CODEXTACTICS_API EPmcScenario PickScenario(int32 RoundSeed);

	/**
	 * A narrow passage at a probe point (the chokepoint scenario): Hits = the wall distances of 8 horizontal rays (45 deg apart, ray 0 =
	 * +X; <= 0 = no wall within the probe reach) and Tall = the same ray at 3 m above the floor hit too. A passage = an opposite pair
	 * (i, i + 4) both hit and tall with MinWidthCm <= the sum <= MaxWidthCm, and the perpendicular pair free (no wall) - OutAxisIndex = the
	 * free ray (the passage runs along it). The narrowest pair wins.
	 */
	CODEXTACTICS_API bool IsPassage(const float Hits[8], const bool Tall[8], float MinWidthCm, float MaxWidthCm, int32& OutAxisIndex, float& OutWidthCm);

	/** Parameters of Json over Base (unknown keys ignored, missing ones keep Base). Keys = the field names in snake_case ("take_cover", "heal_below"). */
	CODEXTACTICS_API FPmcWeights FromJson(const FJsonObject& Json, const FPmcWeights& Base = FPmcWeights());

	/** Loads a weights file (absolute, or relative to Content/Data/AI); false (defaults) when missing / unreadable. */
	CODEXTACTICS_API bool LoadWeightsFile(const FString& PathOrName, FPmcWeights& Out);

	/** Content/Data/AI/pmc_commander.json. */
	CODEXTACTICS_API FString DefaultWeightsPath();
}
