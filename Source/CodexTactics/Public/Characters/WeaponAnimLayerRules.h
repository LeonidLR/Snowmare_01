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
	/**
	 * The set's whole share: an "upper" set (the shotgun, the pistol - designer 2026-11-09) drives spine_01 and everything under it (the pelvis,
	 * legs, feet and root stay the ABP's); a "full" set (the RPG-7 launcher pack, designer 2026-11-09 b) drives every bone but the root.
	 */
	Full,
	/** A "full" set under an upper-body montage (a hit reaction): the set keeps the pelvis and legs, the montage owns the torso. */
	LowerBody
};

/** The two directional clips to blend and the share of the second. */
/** The per-shell reload's phases on one reload clip: intro (raise / tilt the gun, the first shell) -> the shell-insertion loop while shells go
 * in -> finish the current loop -> outro (pump, back to the aim). */
enum class EShellReloadPhase : uint8
{
	None,
	Intro,
	Loop,
	Finishing,
	Outro
};

/** The reload clip's segments (seconds) and the loop's play rate. */
struct CODEXTACTICS_API FShellReloadClip
{
	float Length = 0.f;
	float LoopStart = 0.f;
	float LoopEnd = 0.f;
	/** Where the outro starts (its first pose = the loop's pose at LoopEnd); <= 0 or >= Length: no outro, the reload ends with the loop. */
	float OutroStart = 0.f;
	float LoopRate = 1.f;
};

/** Where the clip is: phase + clip time. */
struct CODEXTACTICS_API FShellReloadState
{
	EShellReloadPhase Phase = EShellReloadPhase::None;
	float Time = 0.f;
	/** Loops completed since the reload began (one per shell at the matching rate). */
	int32 Loops = 0;
};

struct CODEXTACTICS_API FWeaponLayerDirectionBlend
{
	EWeaponLayerDirection A = EWeaponLayerDirection::Forward;
	EWeaponLayerDirection B = EWeaponLayerDirection::Forward;
	/** 0 = all A, 1 = all B. */
	float Alpha = 0.f;
};

/**
 * The nine poses of a pose-based aim offset (the launcher pack's Rotation_* clips, 2026-11-09): pitch rows (up / level / down) x yaw columns
 * (left / centre / right). The centre is the set's aim idle; the others are applied as additive deltas against it.
 */
enum class EWeaponAimPose : uint8
{
	UpLeft, Up, UpRight, Left, Center, Right, DownLeft, Down, DownRight, Count
};

/** The launcher's action clip on the upper body this frame (the shot, the reload, or none). */
enum class ELauncherAction : uint8
{
	None,
	Shot,
	Reload
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

	/**
	 * The mask this frame (gates: eligible = shotgun in hands, alive, up, not climbing / vaulting / in cover / prone / carrying). Any montage
	 * (full body, or an upper-body one: fire / reload / grenade / hit) owns the body, so the layer is Off then; the legs are never the layer's.
	 */
	CODEXTACTICS_API EWeaponLayerMask ResolveMask(bool bEligible, bool bFullBodyMontage, bool bUpperBodyMontage, bool bFullBodySet = false);

	/**
	 * The authored ground speed of a directional blend: the two clips' own speeds (cm/s; <= 0 = Fallback) mixed by the blend's alpha - the play
	 * rate speed / this keeps a full-body set's feet planted in every direction (the launcher pack: walk forward 92, back / sides 112.5 cm/s).
	 */
	CODEXTACTICS_API float BlendedNominalSpeed(const FWeaponLayerDirectionBlend& Blend, const TStaticArray<float, 8>& Speeds, float Fallback);

	/**
	 * A loop's time on the shared phase, its own sync offset added (the share of the loop where its LEFT heel strikes - a set's loops then all
	 * plant the same foot at the same phase, so a direction blend does not mix one clip's planted foot with another's swinging one; the launcher
	 * pack's loops strike at 0 forward / right, ~0.5 back / left, measured). Wraps into [0, Length).
	 */
	CODEXTACTICS_API float SyncedClipTime(float Phase, float Offset, float Length);

	/**
	 * Advance of a turn-in-place clip that turns ClipTurnDeg over ClipLength seconds for a body that turned YawDeltaDeg this frame: the clip time
	 * follows the body's own turn (feet planted when the capsule turns at the clip's rate), in seconds.
	 */
	CODEXTACTICS_API float TurnClipAdvance(float YawDeltaDeg, float ClipTurnDeg, float ClipLength);

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
	 * The retargeted copy of a pack clip: "<Folder>/<Name>.<Name>" for the pack object path "/Game/.../<Name>.<Name>" (or "/Game/.../<Name>");
	 * empty when Folder or the path is empty. weapon_anim_sets.json "retargeted" maps a skeleton name to its folder.
	 */
	CODEXTACTICS_API FString RetargetedClipPath(const FString& PackPath, const FString& Folder);

	/**
	 * The shell loop's play rate: one loop per gameplay shell step (LoopEnd - LoopStart) / StepSeconds, kept near natural (MinRate..MaxRate)
	 * - a loop restarted at x4.8 per shell was the designer's "shaking" (2026-11-08). 1 when the segment is invalid.
	 */
	CODEXTACTICS_API float ShellLoopRate(float LoopStart, float LoopEnd, float StepSeconds, float MinRate = 0.7f, float MaxRate = 1.4f);

	/** A segment table that always works: an invalid loop = the whole clip as the loop (no intro / outro). */
	CODEXTACTICS_API FShellReloadClip SanitizeShellReloadClip(const FShellReloadClip& Clip);

	/**
	 * Advances the per-shell reload by DeltaSeconds: None -> Intro when bReloading; Intro (rate 1) -> Loop at LoopStart; Loop wraps within
	 * [LoopStart, LoopEnd) at LoopRate (Loops + 1 per wrap) while bReloading; then Finishing plays the current loop out to LoopEnd and
	 * jumps to OutroStart (the same pose), Outro (rate 1) -> None at the clip's end. A new reload during Finishing goes back to Loop; during
	 * the outro it starts over at the intro. The clip time never jumps except at those matching poses.
	 */
	CODEXTACTICS_API void AdvanceShellReload(FShellReloadState& State, const FShellReloadClip& Clip, float DeltaSeconds, bool bReloading);

	// --- The launcher set (the RPG-7 on the game designer's launcher pack, 2026-11-09) -----------------------------------------------------

	/**
	 * Weights of the nine aim poses (EWeaponAimPose order) for YawDeg (+ = to the body's right) / PitchDeg (+ = up): bilinear over the 3 x 3 grid
	 * whose outer poses sit at +-YawRangeDeg / +-PitchRangeDeg (clamped there). The poses are additive deltas against the centre, so a missing
	 * CORNER is made of its two edge neighbours (its weight goes to both - their deltas add up to the diagonal); a missing edge pose drops its
	 * share. The centre's weight is reported but carries no delta.
	 */
	CODEXTACTICS_API TStaticArray<float, 9> AimPoseWeights(float YawDeg, float PitchDeg, float YawRangeDeg, float PitchRangeDeg,
		const TStaticArray<bool, 9>& Available);

	/**
	 * Is the loaded rocket on the tube? bRoundInTube = a round in the clip (or a shot whose rocket has not left yet): yes. Empty and reloading:
	 * from the moment the reload clip pushes it in (ReloadProgress01 >= InsertAt01 of the whole reload). Empty, not reloading: no.
	 */
	CODEXTACTICS_API bool LauncherRoundVisible(bool bRoundInTube, bool bReloading, float ReloadProgress01, float InsertAt01);

	/**
	 * The reload clip's time for a reload ElapsedSeconds into TotalSeconds: it waits LeadInSeconds (the shot clip's follow-through after the
	 * launch), then plays its whole ClipLength over the rest (the clip's rate matched to the weapon's reload_time). < 0 = not started yet.
	 */
	CODEXTACTICS_API float LauncherReloadClipTime(float ElapsedSeconds, float TotalSeconds, float ClipLength, float LeadInSeconds);

	/** The share of the whole reload (0..1) at which the clip reaches InsertSeconds (the rocket goes onto the tube), same timing as above. */
	CODEXTACTICS_API float LauncherInsertFraction(float InsertSeconds, float ClipLength, float TotalSeconds, float LeadInSeconds);

	/**
	 * Which action clip the launcher shows: the shot while it plays (ShotSeconds in [0, ShotLength)), then the reload clip once it has started;
	 * otherwise none (the locomotion / aim set).
	 */
	CODEXTACTICS_API ELauncherAction ResolveLauncherAction(float ShotSeconds, float ShotLength, float ReloadClipTime);

	/**
	 * Share of the turn-in-place clip (0..1) for a body turning at YawRateDegPerSec while (nearly) standing: 0 while moving (Speed >= MoveSpeed) or
	 * turning slower than MinRate, 1 from FullRate on.
	 */
	CODEXTACTICS_API float TurnInPlaceShare(float YawRateDegPerSec, float Speed, float MoveSpeed, float MinRate = 30.f, float FullRate = 90.f);
}
