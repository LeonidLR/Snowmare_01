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

/** The 13 tactical ROE parameters (TANDEM 7-E; snake_case keys in squad_roe.json). */
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

	/** A mate needs aid: downed, or health share below the ROE threshold. */
	CODEXTACTICS_API bool NeedsAid(const FSquadROE& ROE, float PatientHealthFraction, bool bDowned);

	/** The rescuer can spare a medkit (keeps the last one below 50 % health when reserve_personal_medkit). */
	CODEXTACTICS_API bool CanGiveAid(const FSquadROE& ROE, float RescuerHealthFraction, int32 Medkits);

	/** Safe Aid Check: no marksman aim on the rescuer or patient and no enemy within 6 m of the patient (skipped when not required). */
	CODEXTACTICS_API bool IsSafeAidRoute(const FSquadROE& ROE, bool bSniperAimingOnRoute, float NearestEnemyToPatientCm);

	CODEXTACTICS_API FString PolicyName(ETargetPriorityPolicy Policy);
}
