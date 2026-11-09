#pragma once

#include "CoreMinimal.h"

/**
 * The formation look a member's body turns to (USquadSubsystem::ApplyFormationFrame -> AOperativeCharacter::SetFormationLookYaw), filtered
 * against one-frame flips (the game designer's video 09.11.2026_ Trembling_Bug_01, FormationJitterSmoke 2026-11-09): along walls the
 * "nobody looks into a wall" rule (FormationSectorRules::ClearLook) can pick a free span edge on one side for a frame and the other side the
 * next ("-76 -> ClearLook 57", next frame "-75 -> ClearLook -59": the facing / the velocity-anticipated position change its sweep scores), and
 * the body - turned by lerp_angle - spun 130 deg towards each pick and back: every member trembled, the M16 men too. The filter passes a look
 * that stays within JumpDeg of the applied one at once (the sweeping corner looks, a wall look easing round); a jump further away is taken only
 * once the new direction has held (within JumpDeg of itself) for HoldSeconds; only for a member walking in the formation (not in cover,
 * on his way to a cover slot or in the squad's wall pose: the cover's looks turn him at once). UE-only (Godot had no wall-clear look).
 * Tested in CodexTactics.Formation.LookFilter.
 */
struct CODEXTACTICS_API FFormationLookFilter
{
	/** The look the body is turned to (unset: none). */
	TOptional<float> Applied;
	/** A far jump waiting to prove it holds, and how long it has. */
	TOptional<float> Pending;
	float PendingSeconds = 0.f;
};

namespace FormationLookFilterRules
{
	/** A look change up to this passes at once, deg. */
	inline constexpr float DefaultJumpDeg = 35.f;
	/** A further jump is taken after holding this long, s (0.3 kept a man facing a wall 0.3 s: FormationLabSmoke's wall-facing gate). */
	inline constexpr float DefaultHoldSeconds = 0.15f;

	/**
	 * One frame: the new raw look (unset = the formation gives no look: cleared at once) -> the look applied. Angles in deg, compared the short
	 * way round.
	 */
	CODEXTACTICS_API TOptional<float> Step(FFormationLookFilter& Filter, const TOptional<float>& Raw, float DeltaSeconds, float JumpDeg = DefaultJumpDeg,
		float HoldSeconds = DefaultHoldSeconds);
}
