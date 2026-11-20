#pragma once

#include "CoreMinimal.h"
#include "AI/PmcCommanderRules.h"

class FJsonObject;
struct FTacticalMapFile;
struct FTacticalRuntimeConfig;

/**
 * The arena round's BEHAVIOUR metrics per side (docs/design/TACTICAL_MAP.md P3 / section 4, designer 2026-11-17: "the data must feed the
 * trainer"): what a squad did on the map - time in contact, flanks completed, position changes, suppress-then-move, ambush hits from a
 * chokepoint's hiding spot, overwatch kills, time inside a chokepoint (bad), window positions used, quiet spells, order churn, stalemate.
 * The JSON follows Grok's G002 schema "codex.pmc_arena_behaviour/1" (the rich form: metrics.<key> = { name, unit, range, direction, value };
 * Scripts/Trainer/nighttrainer/adapter.py BEHAVIOUR_METRICS reads it from rounds[].behaviour = side A, or sides.<X>.behaviour). Pure rules
 * used by CodexTactics.PmcArena (Debug/PmcArenaCommand.cpp). UE-only, no Godot reference. Tested in CodexTactics.AI.PmcArena.Behaviour*.
 */

/** One metric of the catalog (G002 table: key, name, unit, range = the sensor's passport, direction higher / lower / context). */
struct CODEXTACTICS_API FPmcBehaviourMetricSpec
{
	const TCHAR* Key = TEXT("");
	const TCHAR* Name = TEXT("");
	const TCHAR* Unit = TEXT("");
	float RangeMin = 0.f;
	float RangeMax = 0.f;
	const TCHAR* Direction = TEXT("context");
};

/** One side's behaviour in a round. */
struct CODEXTACTICS_API FPmcBehaviour
{
	/** Seconds the sides were in fire contact (each contact moment holds it ContactWindowS; overlaps counted once). */
	float TimeInContactS = 0.f;
	/** Flanks that got there (FPmcTeamStats::FlanksSucceeded), not attempts. */
	int32 FlanksCompleted = 0;
	/** Walk orders after which the man really left his spot (>= MoveMinCm before his next order / the round's end). */
	int32 Moves = 0;
	/** Suppressions under which a mate moved (a walk order while the suppressor still ran it, within SuppressWindowS). */
	int32 SuppressThenMove = 0;
	/** Enemies first hit from a hiding spot of a chokepoint (the TACTICAL map's ambush spots; the target near the spot's threatened point). */
	int32 AmbushSuccess = 0;
	/** Kills by a hit from an overwatch roof spot, or by a man standing on his Overwatch post. */
	int32 OverwatchKills = 0;
	/** Mean seconds per man of the side spent inside a chokepoint (its outline, his floor) - bad. */
	float TimeInChokepointS = 0.f;
	/** Different window stand points the side's men stood at (within ~1 m). */
	int32 WindowPositionsUsed = 0;
	/** Gaps of QuietSpellS or more without a contact moment after the first one (the quiet tail too). */
	int32 QuietSpells = 0;
	/** Order changes of the side's men (an order different from the man's previous one). */
	int32 OrderChurn = 0;
	/** 1 when the round ended without contact or as a time-limit draw, else 0. */
	float StalemateShare = 0.f;
	/**
	 * G008: mean pairwise planar distance of the living defenders, metres, averaged over the round's 1 Hz samples.
	 * 0 for the attacking side, and when fewer than two defenders are alive.
	 */
	float DefenderSpreadM = 0.f;
	/**
	 * G008: fraction of samples where a living defender stands within guard_dist of the roof access. 0 for the attacking
	 * side. Meaningful only when bGuardShareApplicable; otherwise the metric is n/a (JSON null, not a numeric 0).
	 */
	float GuardNearAccessShare = 0.f;
	/** False when the round found no roof access: guard_near_access_share is absent (n/a), not a real zero. */
	bool bGuardShareApplicable = false;

	/** The value of a catalog key (0 for an unknown one, and for a guard share that is n/a). */
	float Value(const FString& Key) const;
};

/** A decision of the commander as the metrics see it: when (round seconds), who (a stable fighter index) and what. */
struct CODEXTACTICS_API FPmcOrderEvent
{
	float Time = 0.f;
	int32 Fighter = INDEX_NONE;
	EPmcOrder Order = EPmcOrder::Hold;
};

/** T-04 position sample. Alive, cover/post occupancy and locomotion come from the arena, not from orders. */
struct CODEXTACTICS_API FPmcArenaPositionSample
{
	FVector Feet = FVector::ZeroVector;
	float WeaponRangeCm = 0.f;
	bool bAlive = false;
	bool bMoving = false;
	bool bWalking = false;
	bool bOnPosition = false;
	bool bNearCoverSlot = false;
};

struct CODEXTACTICS_API FPmcArenaPositionTime
{
	float OpenIdleS = 0.f;
	float OpenIdleBeforeContactS = 0.f;
	double ExposedRunCm = 0.0;
	double MaxExposedRunCm = 0.0;

	/** Integrate one observed interval. The previous sample holds until the new sample. */
	void AddInterval(const FPmcArenaPositionSample& Previous, const FPmcArenaPositionSample& Current,
		float DeltaS, bool bBeforeContact);
};

/** Only the first shot by each defender counts. Simultaneous openers have a zero gap. */
struct CODEXTACTICS_API FPmcArenaOpeningShots
{
	void Record(int32 Fighter, double TimeS);
	TOptional<float> GapS() const { return Gap; }

private:
	int32 FirstFighter = INDEX_NONE;
	double FirstTimeS = 0.0;
	TOptional<float> Gap;
};

/**
 * One hit that may become an overwatch kill. Filled at the shot: the killer's side, his tactical post, and whether he stood on
 * that post or on an overwatch roof spot. The death time is compared with HitTimeS.
 */
struct CODEXTACTICS_API FPmcOverwatchKillNote
{
	int32 Side = INDEX_NONE;
	EPmcPostKind PostKind = EPmcPostKind::None;
	bool bAtPost = false;
	bool bAtOverwatchSpot = false;
	float HitTimeS = 0.f;
};

namespace PmcArenaBehaviourRules
{
	/**
	 * The squad's kill-zone reach: the MEDIAN of the living armed defenders' WeaponRangeCm (the effective reach of the equipped weapon).
	 * The median, not the longest gun: a squad with one marksman (90 m) and two rifles (28 m) opens its kill zone at 28 m, and a
	 * 78-88 m spawn separation is then no "contact" (T-04 run 2: the longest reach made the first sample the contact). 0 = nobody armed.
	 */
	CODEXTACTICS_API float SquadReachCm(TConstArrayView<FPmcArenaPositionSample> Defenders);

	/** A living attacker is within SquadReachCm of a living armed defender (3D, independent of visibility and fire). */
	CODEXTACTICS_API bool AttackersInRange(TConstArrayView<FPmcArenaPositionSample> Defenders, TConstArrayView<FPmcArenaPositionSample> Attackers);

	/** Share of the living defenders on cover/post at the first in-range sample. */
	CODEXTACTICS_API TOptional<float> OnPositionShare(TConstArrayView<FPmcArenaPositionSample> Defenders);

	/** Largest 3D angle between the living defenders' lines to the live attackers' centroid. */
	CODEXTACTICS_API TOptional<float> CrossfireDeg(TConstArrayView<FVector> Defenders, TConstArrayView<FVector> Attackers);

	/** "codex.pmc_arena_behaviour/1". */
	extern CODEXTACTICS_API const TCHAR* const Schema;

	/** A contact moment keeps the sides "in contact" this long, s (the commander's contact memory). */
	inline constexpr float ContactWindowS = 5.f;
	/** A quiet spell = a gap of at least this without a contact moment, s. */
	inline constexpr float QuietSpellS = 15.f;
	/** A suppression covers a mate's move this long at most (or until the suppressor's next order), s. */
	inline constexpr float SuppressWindowS = 8.f;
	/** A walk counts as a position change when the man ended at least this far from where he took it, cm. */
	inline constexpr float MoveMinCm = 300.f;
	/** A shooter stands at an ambush / overwatch spot within this, cm (planar, his floor). */
	inline constexpr float SpotRadiusCm = 300.f;
	/** A kill counts from a hit no older than this (the damage attribution window). Age equal to it still counts. */
	inline constexpr float OverwatchKillWindowS = 3.f;
	/** A man stands at a window position within this of its stand point, cm (planar, his floor). */
	inline constexpr float WindowRadiusCm = 120.f;
	/** The ambush hit: the target within this of the hiding spot's threatened point, cm. */
	inline constexpr float AmbushThreatRadiusCm = 1000.f;

	/** The G002 catalog in its table order (13 metrics: the original 11, then the two G008 defender metrics). */
	CODEXTACTICS_API const TArray<FPmcBehaviourMetricSpec>& Catalog();

	/** The seconds of the round covered by [t, t + WindowS] of the contact moments (clipped to [0, DurationS]; overlaps once). */
	CODEXTACTICS_API float TimeInContact(TArray<float> ContactTimes, float DurationS, float WindowS = ContactWindowS);

	/** The gaps of MinGapS or more after the first contact moment (between moments, and the tail to DurationS); 0 without contact. */
	CODEXTACTICS_API int32 QuietSpells(TArray<float> ContactTimes, float DurationS, float MinGapS = QuietSpellS);

	/**
	 * Suppress-then-move: each Suppress order (fighter X at t) counts once when another fighter of the side takes a walk order
	 * (PmcCommanderRules::IsMoveOrder) in (t, min(t + WindowS, X's next order)].
	 */
	CODEXTACTICS_API int32 SuppressThenMove(TArray<FPmcOrderEvent> Events, float WindowS = SuppressWindowS);

	/** Order churn: the orders that differ from the same fighter's previous order (his first order is no change). */
	CODEXTACTICS_API int32 OrderChurn(TArray<FPmcOrderEvent> Events);

	/** A walk order that took the man at least MoveMinCm (planar) from where he took it. */
	CODEXTACTICS_API bool IsCompletedMove(EPmcOrder Order, const FVector& From, const FVector& To);

	/** 1 for a round without contact or a time-limit draw ("draw" + "time_limit"), else 0. */
	CODEXTACTICS_API float StalemateShare(bool bContact, const FString& Winner, const FString& EndReason);

	/** Feet inside a chokepoint of the tactical map (its outline, his floor: TacticalMapRules::GetZonesNear(feet, 0) non-empty). */
	CODEXTACTICS_API bool IsInChokepoint(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Feet);

	/** The window whose stand point Feet is at (within WindowRadiusCm, his floor); INDEX_NONE for none. */
	CODEXTACTICS_API int32 WindowAt(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& Feet);

	/**
	 * An ambush hit: the shooter stands at a hiding spot that belongs to a chokepoint (its place; SpotRadiusCm, his floor) and the target
	 * is within AmbushThreatRadiusCm of that spot's threatened point.
	 */
	CODEXTACTICS_API bool IsAmbushHit(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& ShooterFeet, const FVector& TargetFeet);

	/** The shooter stands at an overwatch roof spot (SpotRadiusCm, its floor). */
	CODEXTACTICS_API bool IsAtOverwatchSpot(const FTacticalMapFile& File, const FTacticalRuntimeConfig& Config, const FVector& ShooterFeet);

	/**
	 * An overwatch kill: side 0 or 1, the hit is not older than WindowS (DeathTimeS - HitTimeS <= WindowS; a negative delta still
	 * counts), and the shooter stood on an overwatch roof spot OR on his Overwatch post. Window, ambush, ladder and none do not.
	 * An Overwatch post he has left, while he is not on a roof spot, does not.
	 */
	CODEXTACTICS_API bool CountsAsOverwatchKill(const FPmcOverwatchKillNote& Note, float DeathTimeS, float WindowS = OverwatchKillWindowS);

	/** 1 when a walk onto a post was taken, else 0. A hold and a refused walk stay 0. */
	CODEXTACTICS_API int32 PostMoveIncrement(bool bWalkTaken);

	/**
	 * The G002 rich object: { "schema", "side", "tactical_map", "metrics": { <key>: { name, unit, range [min, max], direction, value } } }
	 * - every catalog key present. guard_near_access_share is JSON null when the round has no access (n/a, not 0);
	 *   every other value is a number (0 when the metric did not happen).
	 */
	CODEXTACTICS_API TSharedRef<FJsonObject> ToJson(const FPmcBehaviour& Behaviour, const FString& Side, bool bTacticalMap);
}
