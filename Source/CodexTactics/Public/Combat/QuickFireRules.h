#pragma once

#include "CoreMinimal.h"

/** Tunables of the quick fire (game designer request 2026-10-19; Ctrl + click on a point, user decision 2026-10-19). */
struct CODEXTACTICS_API FQuickFireConfig
{
	/** A visible live enemy this close to the clicked point (planar, cm) gets the targeted shot instead of the point (~1.5 m). */
	float EnemyPickRadiusCm = 150.f;
	/** Rounds of a burst with a magazine weapon (M16); single-shot weapons (sniper, pistol, shotgun) fire one round. */
	int32 BurstRounds = 3;
	/** A weapon with at least this clip size counts as a burst weapon. */
	int32 BurstMinClipSize = 20;
	/** The order gives up when the first / next round could not leave within this many seconds (turn, reload, kneel). */
	float OrderTimeoutSeconds = 6.f;
	/** Area fire (no aimed target): a round that crosses an enemy hits it with the weapon's chance times this. */
	float AreaFireHitMultiplier = 0.5f;
	/** Area fire spread (half angle, degrees) by stance: standing / crouched / prone. */
	float SpreadStandingDeg = 1.5f;
	float SpreadCrouchingDeg = 1.0f;
	float SpreadProneDeg = 0.7f;
	/** A round flies this far past the clicked point (cm) so a surface under the cursor is reached by the trace. */
	float OvershootCm = 30.f;
	/** A round passing this close to an enemy's capsule (cm) counts as crossing it (a round into the snow at its feet). */
	float CapsuleToleranceCm = 20.f;
};

/** One candidate enemy for the quick fire target pick. */
struct CODEXTACTICS_API FQuickFireCandidate
{
	FVector Location = FVector::ZeroVector;
	/** The squad sees it now (squad sight); unseen enemies are never picked. */
	bool bVisible = true;
	bool bAlive = true;
};

/** A capsule a quick fire round may cross (an enemy's collision, centre = the actor location). */
struct CODEXTACTICS_API FQuickFireCapsule
{
	FVector Center = FVector::ZeroVector;
	float Radius = 40.f;
	float HalfHeight = 90.f;
};

/**
 * Pure rules of the quick fire (game designer request 2026-10-19, UE-only - no Godot reference; bound to Ctrl + click on a
 * point with nothing to aim at, user decision 2026-10-19): the selected operative(s) turn to the clicked point and fire at
 * it at once. Unit tests: CodexTactics.Combat.QuickFire.*.
 */
namespace QuickFireRules
{
	CODEXTACTICS_API const FQuickFireConfig& GetConfig();

	/**
	 * The enemy a click at Point means: the visible, live candidate nearest to Point (planar) within RadiusCm (the targeted shot).
	 * INDEX_NONE: fire at the point (area fire). Unseen enemies are never picked (squad sight); a round at the point may still
	 * hit one.
	 */
	CODEXTACTICS_API int32 PickTarget(const FVector& Point, const TArray<FQuickFireCandidate>& Candidates, float RadiusCm);

	/** Rounds of one quick fire: 0 for a weapon without ammo (the knife: no fire at a point), 1 for single-shot weapons, else BurstRounds. */
	CODEXTACTICS_API int32 GetBurstRounds(bool bUsesAmmo, bool bSniper, int32 MaxClipSize, const FQuickFireConfig& Config);

	/**
	 * Where a round towards Aim ends: Aim + OvershootCm along the line, but never further than RangeCm from Muzzle (beyond the
	 * weapon's range the round falls short - the order still fires towards the point).
	 */
	CODEXTACTICS_API FVector ComputeRoundEnd(const FVector& Muzzle, const FVector& Aim, float RangeCm, const FQuickFireConfig& Config);

	/**
	 * The first capsule (its radius + ToleranceCm) the segment Start -> End crosses (closest to Start), INDEX_NONE when none;
	 * OutAlongCm = the distance from Start of the round's closest approach to its axis (where the tracer ends).
	 */
	CODEXTACTICS_API int32 FindFirstCapsuleOnSegment(const FVector& Start, const FVector& End, const TArray<FQuickFireCapsule>& Capsules,
		float ToleranceCm, float& OutAlongCm);

	/** Area-fire hit chance on a crossed enemy: WeaponChance * AreaFireHitMultiplier, clamped 0..1. */
	CODEXTACTICS_API float GetAreaHitChance(float WeaponChance, const FQuickFireConfig& Config);

	/** Spread half angle of the stance index (0 standing, 1 crouching, 2 prone), degrees. */
	CODEXTACTICS_API float GetSpreadDeg(int32 StanceIndex, const FQuickFireConfig& Config);
}
