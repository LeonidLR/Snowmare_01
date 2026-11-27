#pragma once

#include "CoreMinimal.h"
#include "Tactics/CoverTypes.h"

/**
 * CoverIndoor aim offset at a corner peek (G025). A plain peek stays the current pose. A Shift-click look after that
 * pose is up picks the hand from the look versus the wall normal and drives the aim-offset yaw and pitch. The clamp
 * is the blend space only: the peek sight cone keeps the unclamped look (PeekSightRules::PeekConeYawDeg).
 * Tests: CodexTactics.Tactics.Cover.LookAo.
 */
namespace CoverLookAoRules
{
	/** Signed yaw of LookDirection relative to WallNormal, degrees. Positive = to the right of the normal (ECoverFacing::Right). */
	CODEXTACTICS_API float SignedYawFromNormal(const FVector& WallNormal, const FVector& LookDirection);

	/**
	 * The hand for this look. The current side stays until the look crosses the wall normal by more than HysteresisDeg,
	 * so a look that sits on the normal does not flicker.
	 */
	CODEXTACTICS_API ECoverFacing SideFromLook(float SignedYawDeg, ECoverFacing Current, float HysteresisDeg);

	/** Clamp the aim-offset axes into the blend space. This value is not the sight direction. */
	CODEXTACTICS_API void ClampAxes(float& YawDeg, float& PitchDeg, float YawAbsDeg, float PitchAbsDeg);

	/**
	 * Half-range of one blend-space axis: the wider of |Min| and |Max|. An unset axis (both ends 0) keeps Fallback
	 * (the rifle aim-offset clamp) so a missing CoverIndoor blend space does not open the twist to 0.
	 */
	CODEXTACTICS_API float AxisAbs(float Min, float Max, float Fallback);

	/** The CoverIndoor pose is on only while he is in the corner peek and a look is held. */
	CODEXTACTICS_API bool WantsPose(bool bInCornerPeek, bool bHasLook);

	/**
	 * Unsigned yaw delta (degrees) between LookDirection and CornerAimDirection(Slot, LookFacing, OutwardDeg).
	 * Returns true when that delta <= MaxAoOffDeg (in cover AO can reach the look; otherwise step out / turn body).
	 */
	CODEXTACTICS_API bool IsWithinCornerAoReach(const FCoverSlot& Slot, ECoverFacing LookFacing, const FVector& LookDirection,
		float OutwardDeg, float MaxAoOffDeg, float* OutOffDeg = nullptr);
}
