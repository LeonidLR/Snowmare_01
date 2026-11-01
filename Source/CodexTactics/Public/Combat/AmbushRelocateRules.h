#pragma once

#include "CoreMinimal.h"

/**
 * A bypassed ambush relocates (game designer answer 3, 2026-11-01, docs/design/COMBAT_LAB.md "Relocation"; UE-only, no Godot reference):
 * an ambush the squad did not pass - it walked past it while watched, or went another way - moves, concealed and out of the squad's sight,
 * to a new hiding spot ahead of the squad instead of waiting to be recycled. Pure rules, tested in CodexTactics.Combat.AmbushRelocate.*;
 * UCombatPressureSubsystem moves the group only while no operative sees any of its members and the new spot is concealed.
 */

/** Tuning (level JSON "pressure"."ambush"."relocate"). */
struct CODEXTACTICS_API FAmbushRelocateParams
{
	/** Relocation on ("enabled"); off = a bypassed ambush waits until recycled (the 2026-11-01 first version). */
	bool bEnabled = true;
	/** Bypassed this long before it moves, s ("after_sec"). */
	float AfterSeconds = 15.f;
	/** Behind the squad: the bearing from the leader is this far off the squad's heading, degrees ("behind_angle_deg"). */
	float BehindAngleDeg = 100.f;
	/** ... or simply this far from every operative, cm ("far_cm"). */
	float FarCm = 3500.f;
	/** The new spot must lie ahead: cos(heading, leader -> spot) at least this ("min_ahead_cos"). */
	float MinAheadCos = 0.2f;
};

/** What a hidden ambush does about relocating this step. */
enum class EAmbushRelocateDecision : uint8
{
	/** Not bypassed (long enough): stay. */
	Stay,
	/** Due, but an operative sees one of its members: wait until it is unseen. */
	WaitUnseen,
	/** Move to a new hiding spot ahead of the squad now. */
	Relocate
};

namespace AmbushRelocateRules
{
	/**
	 * Bypassed now: the squad is beyond StrikeRangeCm (the ambush cannot strike) and the ambush is behind the squad's way (AngleOffHeadingDeg
	 * beyond BehindAngleDeg) or farther than FarCm from every operative.
	 */
	CODEXTACTICS_API bool IsBypassed(float NearestSquadCm, float AngleOffHeadingDeg, float StrikeRangeCm, const FAmbushRelocateParams& Params);

	/** The bypass clock: grows by DeltaSeconds while bypassed, back to 0 otherwise. */
	CODEXTACTICS_API float StepBypassedSeconds(bool bBypassed, float Seconds, float DeltaSeconds);

	/** Relocate once bypassed for AfterSeconds and unseen; WaitUnseen while seen; Stay otherwise (and always when disabled). */
	CODEXTACTICS_API EAmbushRelocateDecision Decide(float BypassedSeconds, bool bSeenNow, const FAmbushRelocateParams& Params);

	/** A new spot qualifies: ahead of the squad (AheadCos >= MinAheadCos) and concealed (no operative's line of sight). */
	CODEXTACTICS_API bool IsGoodDestination(float AheadCos, bool bConcealed, const FAmbushRelocateParams& Params);
}
