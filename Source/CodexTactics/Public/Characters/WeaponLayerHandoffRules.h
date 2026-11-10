#pragma once

#include "CoreMinimal.h"

/**
 * When the operative's native weapon animation layer (UOperativeAnimInstance::UpdateShotgunLayer, OperativeAnimInstanceShotgun.cpp) hands the
 * body back to ABP_Operative's own locomotion - the game designer's shotgun play-test 2026-11-09: crouched (a set with "crouch_body": "abp" in
 * Content/Data/Weapons/weapon_anim_sets.json) and the whole cover mechanics (pressed to the wall, the last metres of the walk / run to the
 * cover slot, an open shot off the wall) use the same ABP locomotion as an M16 operative; the weapon in hands is the only difference. The
 * layer's weights blend out / in over its blend_seconds (no pop); upper-body montages (the D-1's fire clip) and the native per-shell reload
 * still play over the ABP pose. UE-only (Godot had no weapon layer). Tested in CodexTactics.Animation.WeaponLayer.Handoff*.
 */
struct CODEXTACTICS_API FWeaponLayerHandoffFacts
{
	/** The weapon set's crouch is the ABP's ("crouch_body": "abp"). */
	bool bCrouchAbpSet = false;
	bool bCrouching = false;
	/** The weapon set's cover mechanics are the ABP's ("cover_mode": "abp": the approach's last metres and the open shot too; in cover always). */
	bool bCoverAbpSet = false;
	bool bInCover = false;
	/** On his way to a cover slot (a cover order / run-and-peek / squad peek walk not entered yet). */
	bool bPendingCover = false;
	/** Horizontal distance to that slot, cm. */
	float PendingCoverDistanceCm = 0.f;
	/** Stepped off the wall for an open shot, coming back to the slot. */
	bool bCoverOpenShot = false;
};

/**
 * The direction a FULL-BODY weapon set's directional legs play (WeaponLayerHandoffRules::StepLayerDirection; FormationJitterSmoke 2026-11-09):
 * Current is the direction they play, From the one they cross-fade from while Fade < 1 (Fade = Current's share).
 */
struct CODEXTACTICS_API FWeaponLayerDirectionState
{
	float Current = 0.f;
	float From = 0.f;
	float Fade = 1.f;
	bool bValid = false;
};

namespace WeaponLayerHandoffRules
{
	/** The last this-many cm of the walk to a cover slot are the ABP's (the run-and-peek's pre-turn starts at up to 400 cm + the run-in glide). */
	inline constexpr float DefaultCoverApproachCm = 700.f;

	/**
	 * The layer gives the body to the ABP now: crouched with a "crouch_body": "abp" set; in cover (every set); with a "cover_mode": "abp" set also the last
	 * CoverApproachCm of the walk to a cover slot and an open shot off the wall.
	 */
	CODEXTACTICS_API bool DefersToAbp(const FWeaponLayerHandoffFacts& Facts, float CoverApproachCm = DefaultCoverApproachCm);

	/**
	 * The movement direction a FULL-BODY set's directional legs follow (FormationJitterSmoke 2026-11-09: a formation follower's direction swings
	 * ~15-25 deg/s back and forth as he corrects his slot; the pack's directional clips hold the hips 30-60 deg apart, so the same swings drove
	 * his hips / shoulders back and forth 3-4x as much as the ABP's rifle legs). Current eases towards Target over SmoothSeconds (exponential)
	 * and a change within DeadZoneDeg is ignored (the legs stay on their clip); angles in deg (-180..180), the shortest way round. SmoothSeconds
	 * <= 0: Target at once (the old behaviour).
	 */
	CODEXTACTICS_API float SmoothLayerDirection(float Current, float Target, float DeltaSeconds, float SmoothSeconds, float DeadZoneDeg);

	/**
	 * One frame of the full-body legs' direction: a change of more than SnapDeg (a reversal - the follower turning round from walking backwards
	 * to his slot) does not sweep through the side clips (the pack's strafes hold the hips ~90 deg off the backward / forward ones: the sweep
	 * spun his hips 90 deg in 0.2 s, FormationJitterSmoke): the legs cross-fade from the old direction's clips to the new one's over FadeSeconds;
	 * a smaller change follows SmoothLayerDirection. The first call seeds it at Target. SnapDeg <= 0 or FadeSeconds <= 0: no cross-fade.
	 */
	CODEXTACTICS_API void StepLayerDirection(FWeaponLayerDirectionState& State, float Target, float DeltaSeconds, float SmoothSeconds, float DeadZoneDeg,
		float SnapDeg, float FadeSeconds);
}
