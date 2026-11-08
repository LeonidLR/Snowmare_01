#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"

/**
 * Pure rules of the operative's native weapon animation layer (the D-1 shotgun set from the user's ShotgunLocomotionPack, designer
 * decision 21, 2026-11-03; UOperativeAnimInstance::UpdateShotgunLayer, FOperativeAnimInstanceProxy). UE-only. No AnimGraph node: the
 * layer is blended over the ABP's pose after the graph (like the ladder rung IK), so the user's ABP stays untouched.
 * Tested in CodexTactics.Animation.WeaponLayer.*.
 */

/** The eight movement directions of a directional clip set, clockwise from forward (0 deg). */
enum class EWeaponLayerDirection : uint8
{
	Forward, ForwardRight, Right, BackwardRight, Backward, BackwardLeft, Left, ForwardLeft, Count
};

/** Which bones the layer drives this frame. */
enum class EWeaponLayerMask : uint8
{
	/** The ABP alone (a full-body montage: knockdown, death, cover, interaction, stance transition; or not eligible). */
	Off,
	/** Every bone below the root (locomotion + the shotgun hold). */
	Full,
	/** Pelvis and legs only: an upper-body montage plays (fire, reload, grenade, hit) and owns the torso. */
	LowerBody
};

/** The two directional clips to blend and the share of the second. */
struct CODEXTACTICS_API FWeaponLayerDirectionBlend
{
	EWeaponLayerDirection A = EWeaponLayerDirection::Forward;
	EWeaponLayerDirection B = EWeaponLayerDirection::Forward;
	/** 0 = all A, 1 = all B. */
	float Alpha = 0.f;
};

namespace WeaponAnimLayerRules
{
	/** The direction's angle, degrees clockwise from forward (Right = 90, Backward = 180, Left = -90). */
	CODEXTACTICS_API float DirectionAngle(EWeaponLayerDirection Direction);

	/**
	 * The two available directional clips around AngleDeg (the movement relative to the body, clockwise from forward) and the blend
	 * between them. Available marks which of the eight clips exist (a 4-way set blends Forward / Right / Backward / Left). With none
	 * available: Forward, alpha 0.
	 */
	CODEXTACTICS_API FWeaponLayerDirectionBlend BlendDirections(float AngleDeg, const TStaticArray<bool, 8>& Available);

	/** The mask this frame (gates: eligible = shotgun in hands, alive, up, not climbing / vaulting / in cover / prone / carrying). */
	CODEXTACTICS_API EWeaponLayerMask ResolveMask(bool bEligible, bool bFullBodyMontage, bool bUpperBodyMontage);

	/**
	 * Share of the moving set against the idle clip by speed (cm/s): 0 below StartSpeed, 1 from FullSpeed on, linear between (the
	 * walk clips' own play rate follows the speed, so there is no foot slide above it).
	 */
	CODEXTACTICS_API float MoveWeight(float Speed, float StartSpeed, float FullSpeed);

	/** Share of the jog set against the walk set by speed: 0 at WalkSpeed or slower, 1 at JogSpeed or faster. */
	CODEXTACTICS_API float JogWeight(float Speed, float WalkSpeed, float JogSpeed);

	/** Play rate of a loop authored at NominalSpeed for the actual Speed, clamped into [MinRate, MaxRate]. */
	CODEXTACTICS_API float LoopPlayRate(float Speed, float NominalSpeed, float MinRate = 0.5f, float MaxRate = 1.6f);

	/**
	 * Section of the reload clip one shell plays, seconds [OutStart, OutEnd) and its play rate to fit StepSeconds. With no authored
	 * section (ShellStart >= ShellEnd) the whole clip plays.
	 */
	CODEXTACTICS_API float ShellReloadRate(float ClipLength, float ShellStart, float ShellEnd, float StepSeconds, float& OutStart, float& OutEnd);

	/**
	 * The retargeted copy of a pack clip: "<Folder>/<Name>.<Name>" for the pack object path "/Game/.../<Name>.<Name>" (or "/Game/.../<Name>");
	 * empty when Folder or the path is empty. weapon_anim_sets.json "retargeted" maps a skeleton name to its folder.
	 */
	CODEXTACTICS_API FString RetargetedClipPath(const FString& PackPath, const FString& Folder);
}
