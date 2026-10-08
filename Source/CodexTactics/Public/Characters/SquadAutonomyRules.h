#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Data/CombatTypes.h"

/**
 * Commander Mode (Sprint 07, TANDEM «SPRINT 07 DIRECTIVE: Autonomous Squad Combat»; UE-only, no Godot reference):
 * with autonomy on, a squad member fights on its own inside a leash around the point the player sent it to — cover,
 * stance, target choice, reload / sidearm, flank turns, field aid. Pure rules, tested in
 * CodexTactics.Characters.SquadAutonomy.*; USquadAutonomySubsystem applies them to the live squad. The tactical ROE
 * (13 parameters) is tuned in the Wave Editor (Content/Data/AI/squad_roe.json, SquadROE loader).
 */

/** How far the leash stretches for field aid. */
enum class ELeashStrictness : uint8
{
	/** Up to 10 m (or the anchor radius when larger) to reach a wounded mate. */
	Flexible,
	/** Never beyond the anchor radius. */
	Strict
};

enum class EOpenGroundStance : uint8
{
	Crouch,
	Prone,
	Standing
};

enum class ECoverStance : uint8
{
	Crouch,
	Standing
};

/** What an operative does when a marksman's aim is on him. */
enum class ESniperReaction : uint8
{
	/** Into the nearest barricade cover inside the leash (prone when there is none). */
	DiveToCover,
	/** Flat on the ground where he stands. */
	DropProne
};

enum class ETargetPriorityPolicy : uint8
{
	/** Marksmen / spitters first, then hounds and drones, then the rest; closer first within a tier. */
	ThreatLevel,
	ClosestFirst,
	LowestHP,
	/** The leader's target first (focus fire), else closest. */
	AssistLeader
};

/** The tactical ROE: the 13 Sprint 07 parameters (TANDEM 7-E) + 4 of the defense line (Sprint 10); snake_case keys in squad_roe.json. */
struct CODEXTACTICS_API FSquadROE
{
	float AnchorRadiusMeters = 7.f;
	ELeashStrictness LeashStrictness = ELeashStrictness::Flexible;
	bool bPreferHighGround = true;
	EOpenGroundStance OpenGroundStance = EOpenGroundStance::Crouch;
	ECoverStance CoverStance = ECoverStance::Crouch;
	ESniperReaction SniperReaction = ESniperReaction::DiveToCover;
	ETargetPriorityPolicy TargetPriorityPolicy = ETargetPriorityPolicy::ThreatLevel;
	float FlankDefenseAngleDeg = 75.f;
	float AidHealthThresholdPct = 25.f;
	bool bRequireSafeRouteForAid = true;
	bool bReservePersonalMedkit = true;
	float AutoReloadThresholdPct = 25.f;
	float EmergencySidearmDistMeters = 3.5f;
	// --- Sprint 10: defense line ("Not one step back") ---
	/** Enemies this close to the defended object / point are intruders, m. */
	float DefenseInterceptRadiusMeters = 12.f;
	/** Strict: the defender never leaves the 5 m defense leash; Flexible: up to 10 m for aid when no intruder is there. */
	ELeashStrictness DefenseLeashStrictness = ELeashStrictness::Strict;
	/** A point-blank enemy (body-blocking the defender) comes before the intruders; off: the intruders come first. */
	bool bDefenseBodyBlockPriority = true;
	/** The defender ignores wounded mates outside his defense leash. */
	bool bDefenseIgnoreDistantAid = true;
};

/**
 * Sprint 10 "Hold line" (Hold Objective at all costs): the operative holds a defended object (generator, terminal,
 * gate, barricade) or point. Intruders within the intercept radius of it come first, he never leaves the 5 m defense
 * leash, never retreats from a point-blank enemy (fires on the spot, draws the knife) and does not run off to aid
 * while intruders are there.
 */
struct CODEXTACTICS_API FDefenseDirective
{
	TWeakObjectPtr<AActor> DefendedActor;
	FVector DefendedLocation = FVector::ZeroVector;
	bool bHoldAtAllCosts = false;
	float InterceptRadiusCm = 1200.f;
	float MaxDefenseLeashCm = 500.f;

	bool IsActive() const { return bHoldAtAllCosts; }
};

/** The point a move order pinned the operative to (TANDEM 7-B). */
struct CODEXTACTICS_API FTacticalAnchor
{
	FVector Location = FVector::ZeroVector;
	/** cm (the ROE anchor radius when the anchor is set). */
	float Radius = 700.f;
	/** Facing when the order was given (towards the destination): the side the operative guards. */
	FRotator GuardFacing = FRotator::ZeroRotator;
	bool bIsActive = false;
	/** Sprint 10: a defense line on this anchor (a new move order clears it: MakeAnchor builds a plain anchor). */
	FDefenseDirective Defense;
};

/** What the autonomy knows about one enemy when it picks a target. */
struct CODEXTACTICS_API FAutonomyTargetCandidate
{
	EEnemyArchetype Archetype = EEnemyArchetype::Base;
	float DistanceCm = 0.f;
	float HealthFraction = 1.f;
	/** The leader shoots at it. */
	bool bLeaderTarget = false;
	/** This operative's current target (a little stickiness: no flip-flopping). */
	bool bCurrent = false;
	/** A marksman whose aim is on the squad. */
	bool bAimingAtSquad = false;
	/** In range and in the line of fire. */
	bool bCanHit = true;
	/** Sprint 10: distance to the defended object / point (huge without a defense line), cm. */
	float DistanceToDefendedCm = TNumericLimits<float>::Max();
	/** Sprint 10: it attacks the defended object. */
	bool bAttackingDefended = false;
};

namespace SquadAutonomyRules
{
	/** Field aid may stretch the flexible leash this far (cm). */
	inline constexpr float FlexibleAidLeashCm = 1000.f;
	/** Safe Aid: no enemy this close to the patient (cm). */
	inline constexpr float SafeAidEnemyClearanceCm = 600.f;
	/** Below this own health share the rescuer keeps his last medkit (reserve_personal_medkit). */
	inline constexpr float ReserveMedkitHealthFraction = 0.5f;
	/** Within this many metres an enemy counts as close enough to the line to matter (reload under fire, etc.). */
	inline constexpr float UnderThreatDistanceCm = 800.f;

	/** Anchor radius in cm; for aid the flexible leash stretches to FlexibleAidLeashCm. */
	CODEXTACTICS_API float LeashRadius(const FSquadROE& ROE, bool bForAid);

	/** A new anchor at Destination, guarding the direction From -> Destination (the operative's facing when From == Destination). */
	CODEXTACTICS_API FTacticalAnchor MakeAnchor(const FSquadROE& ROE, const FVector& From, const FVector& Destination, const FRotator& CurrentFacing);

	/** Planar distance check against the anchor (inactive anchors never leash). */
	CODEXTACTICS_API bool IsInsideLeash(const FTacticalAnchor& Anchor, const FVector& Point, float RadiusCm);

	/** Point pulled back onto the leash circle when it lies outside (planar; keeps Z). */
	CODEXTACTICS_API FVector ClampToLeash(const FTacticalAnchor& Anchor, const FVector& Point, float RadiusCm);

	/**
	 * The stance the operative should hold while not moving: sniper aim on him -> prone (DropProne, or DiveToCover with
	 * no reachable cover; in cover the cover stance at most crouched); behind a barricade -> cover stance; in the open ->
	 * open-ground stance.
	 */
	CODEXTACTICS_API EOperativeStance DesiredStance(const FSquadROE& ROE, bool bInCover, bool bSniperAiming, bool bCoverReachable);

	/** An enemy more than AngleDeg off the facing (planar). */
	CODEXTACTICS_API bool IsFlankThreat(const FVector& Forward, const FVector& Position, const FVector& Enemy, float AngleDeg);

	/** Reload: a ranged weapon below the threshold share of the clip, reserve left, not already reloading, and in cover or no enemy close. */
	CODEXTACTICS_API bool ShouldReload(const FSquadROE& ROE, int32 Clip, int32 MaxClip, int32 Reserve, bool bReloading, bool bInCover,
		float NearestEnemyCm);

	/** Sidearm: an enemy inside the emergency distance while the primary clip is empty (or reloading) and the sidearm has rounds. */
	CODEXTACTICS_API bool ShouldSwitchToSidearm(const FSquadROE& ROE, float NearestEnemyCm, int32 PrimaryClip, bool bReloading, int32 SidearmRounds);

	/** Back to the primary once nobody is within twice the emergency distance and it has rounds. */
	CODEXTACTICS_API bool ShouldSwitchBackToPrimary(const FSquadROE& ROE, float NearestEnemyCm, int32 PrimaryRounds);

	/** Threat tier for the ThreatLevel policy: marksman / spitter 3, hound / cryo drone 2, cutter / brute 1, others 0. */
	CODEXTACTICS_API int32 ThreatTier(EEnemyArchetype Archetype);

	/**
	 * Target score under the policy (higher is better): any enemy inside the emergency distance comes first (self-defense),
	 * then the policy key, distance breaking ties; the current target gets 3 m of stickiness.
	 */
	CODEXTACTICS_API float TargetScore(const FSquadROE& ROE, const FAutonomyTargetCandidate& Candidate);

	/** Best candidate it can hit; INDEX_NONE when none. */
	CODEXTACTICS_API int32 ChooseTarget(const FSquadROE& ROE, const TArray<FAutonomyTargetCandidate>& Candidates);

	// --- Sprint 10: defense line ---

	/** A directive on Location (actor optional) with the ROE intercept radius and the 5 m leash. */
	CODEXTACTICS_API FDefenseDirective MakeDefense(const FSquadROE& ROE, AActor* DefendedActor, const FVector& Location);

	/** The candidate is an intruder: within the intercept radius of the defended spot or attacking the object. */
	CODEXTACTICS_API bool IsIntruder(const FDefenseDirective& Defense, const FAutonomyTargetCandidate& Candidate);

	/**
	 * Target score with a defense line, in tiers: a point-blank enemy first (defense_body_block_priority), then the
	 * intruders — the one closest to the defended object first (500 per metre, beats any threat tier) — then the ROE
	 * policy. Without an active directive it is TargetScore.
	 */
	CODEXTACTICS_API float DefenseTargetScore(const FSquadROE& ROE, const FDefenseDirective& Defense, const FAutonomyTargetCandidate& Candidate);

	/** ChooseTarget with a defense line (Sprint 10-1; the directive's «PickTarget»). */
	CODEXTACTICS_API int32 PickTarget(const FSquadROE& ROE, const FDefenseDirective& Defense, const TArray<FAutonomyTargetCandidate>& Candidates);

	/** Defense leash, cm: Strict 5 m; Flexible 5 m, for aid up to the flexible aid leash (10 m). */
	CODEXTACTICS_API float DefenseLeashRadius(const FSquadROE& ROE, const FDefenseDirective& Defense, bool bForAid);

	/**
	 * Safe Aid with a defense line: the Safe Aid Check (IsSafeAidRoute) and — while the line is held — no intruder
	 * inside the intercept radius, and (defense_ignore_distant_aid) the patient within the defense leash of the spot.
	 */
	CODEXTACTICS_API bool CanGiveSafeAid(const FSquadROE& ROE, const FDefenseDirective& Defense, bool bSniperAimingOnRoute,
		float NearestEnemyToPatientCm, bool bIntruderPresent, float PatientDistanceToDefendedCm);

	/** "Not one step back": a defender never withdraws (no retreat / kiting step away from an enemy). */
	CODEXTACTICS_API bool AllowsRetreat(const FDefenseDirective& Defense);

	/** He holds the spot (no walking at all) while an enemy is inside the emergency (point-blank) distance. */
	CODEXTACTICS_API bool HoldsGround(const FSquadROE& ROE, const FDefenseDirective& Defense, float NearestEnemyCm);

	/** Body-blocking: the knife at half the emergency distance (defense_body_block_priority). */
	CODEXTACTICS_API bool ShouldDrawMelee(const FSquadROE& ROE, const FDefenseDirective& Defense, float NearestEnemyCm);

	/** A mate needs aid: downed, or health share below the ROE threshold. */
	CODEXTACTICS_API bool NeedsAid(const FSquadROE& ROE, float PatientHealthFraction, bool bDowned);

	/** The rescuer can spare a medkit (keeps the last one below 50 % health when reserve_personal_medkit). */
	CODEXTACTICS_API bool CanGiveAid(const FSquadROE& ROE, float RescuerHealthFraction, int32 Medkits);

	/** Safe Aid Check: no marksman aim on the rescuer or patient and no enemy within 6 m of the patient (skipped when not required). */
	CODEXTACTICS_API bool IsSafeAidRoute(const FSquadROE& ROE, bool bSniperAimingOnRoute, float NearestEnemyToPatientCm);

	CODEXTACTICS_API FString PolicyName(ETargetPriorityPolicy Policy);
}
