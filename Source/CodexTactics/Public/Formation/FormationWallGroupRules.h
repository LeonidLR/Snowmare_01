#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * The wall group - who looks where when the squad stands at a wall and the leader's look is decided elsewhere (Godot reference: none
 * (UE-only, user playtest 2026-10-25: "in group cover at a wall corner the leader switched to another wall's edge and held his new
 * corner, the two others looked the same way as he - a wide gap opened"; docs/design/FORMATION_SECTORS.md v2.2 "A look decided elsewhere
 * counts as cover", contract docs/design/FORMATION_CONTRACT.md 11.3)). Pure. Tests: CodexTactics.Formation.WallGroup.*.
 *
 * Allocate: FormationCoverRules::AllocateStatic round the standing group with the fixed looks (the leader's corner aim / his look along
 * the wall), then each free member's pose:
 * - a member holding a real cover slot (bCoverPose) can only look ALONG his wall (the cover clips: WallNormalYaw +- 90). An allocated look
 *   within FacingOutMaxDeg of the outward normal ("away from the wall") is the back-to-wall pose instead (FacingOut: out of the cover,
 *   crouched, facing that yaw - the formation wall layout's baseline pose); any other look takes the cover side nearer to it (AlongWall)
 *   unless that direction is blocked by a wall next to him (an inner corner): then the back-to-wall pose with the allocated yaw;
 * - any other member (already in the back-to-wall pose, prone behind the cover) looks exactly where the allocator says (Free).
 * A snapped cover look is not where the allocator wanted it: it becomes a fixed look and the others are allocated again (one member at a
 * time, at most once per member), so the gaps it leaves are filled. Nobody gets a blocked look (AllocateStatic's open degrees).
 *
 * LargestOpenGap / CoveredFraction / FacesWall: the Training Ground bot's checks 10, 11 and 1 (contract 4 / 11.4 / 11.5) for smokes and
 * tests.
 */

/** How a wall-group member holds the look Allocate gives him. */
enum class EWallGroupLook : uint8
{
	/** A look decided elsewhere (the leader's corner aim) - kept as given. */
	Fixed,
	/** In his cover slot, the cover side whose along-the-wall look this is. */
	AlongWall,
	/** Out of the cover into the back-to-wall pose (crouched, facing out) at this yaw. */
	FacingOut,
	/** Not in a cover slot (the back-to-wall pose already, prone behind the cover): exactly this yaw. */
	Free
};

/** One member of the wall group. */
struct FWallGroupMember
{
	/** A distinct place label (K = the leader first, then P / L / Z) - AllocateStatic keys looks by role. */
	EFormationRole Role = EFormationRole::Commander;
	/** Where he stands (his cover slot, the slot / spot he is on his way to). */
	FVector2D Pos = FVector2D::ZeroVector;
	/** A look decided elsewhere (the leader's cover facing / corner aim): kept, its cone counts as covered. */
	TOptional<float> FixedYaw;
	/** He holds (or is on his way to) a real cover slot: the cover pose looks only along the wall. */
	bool bCoverPose = false;
	/** His wall's outward normal (from the wall towards him), UE yaw - for bCoverPose. */
	float WallNormalYaw = 0.f;
};

/** Allocate's answer for one member. */
struct FWallGroupLookResult
{
	EFormationRole Role = EFormationRole::Commander;
	/** The world yaw he looks at (AlongWall: exactly along his wall). */
	float Yaw = 0.f;
	EWallGroupLook Kind = EWallGroupLook::Free;
};

namespace FormationWallGroup
{
	/** deg: an allocated look within this of a cover member's outward normal points "away from the wall" - the back-to-wall pose. */
	inline constexpr float FacingOutMaxDeg = 45.f;
	/** deg: check 10's wall block - directions within this of a near wall's closest point are not open (contract 11.4). */
	inline constexpr float CheckBlockHalfDeg = 45.f;
	/** cm / deg: check 1 (wall stare) - facing within StareAngleDeg of a wall surface closer than StareDistCm (contract 4). */
	inline constexpr float StareDistCm = 200.f;
	inline constexpr float StareAngleDeg = 45.f;

	/** The looks and poses (see the header comment), fixed members first, then in decision order. */
	CODEXTACTICS_API TArray<FWallGroupLookResult> Allocate(const TArray<FFormationWall>& Walls, const TArray<FWallGroupMember>& Members,
		const FFormationConfig& Config = FFormationConfig());

	/**
	 * Check 10: the largest arc of directions (world yaws, sampled every StepDeg) that no member's cone (LookYaws[i] +- FovDeg[i] / 2)
	 * covers and no wall within WallDistCm of a member blocks (+- BlockHalfDeg round the direction to its closest point), deg. 0 = all
	 * covered; 360 = nothing at all.
	 */
	CODEXTACTICS_API float LargestOpenGap(const TArray<FFormationWall>& Walls, const TArray<FVector2D>& Positions, const TArray<float>& LookYaws,
		const TArray<float>& FovDeg, float WallDistCm, float BlockHalfDeg = CheckBlockHalfDeg, float StepDeg = 2.f);

	/** Check 11: the fraction (0..1) of the directions CentreYaw +- HalfWidthDeg (sampled every StepDeg) inside some member's cone. */
	CODEXTACTICS_API float CoveredFraction(const TArray<float>& LookYaws, const TArray<float>& FovDeg, float CentreYaw, float HalfWidthDeg,
		float StepDeg = 2.f);

	/** Check 1: a wall surface within DistCm of Pos whose closest point lies within AngleDeg of Yaw (he stares into it). */
	CODEXTACTICS_API bool FacesWall(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float DistCm = StareDistCm,
		float AngleDeg = StareAngleDeg);
}
