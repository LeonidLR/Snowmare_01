#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Coverage allocator - who looks where so the squad has no blind gap (Godot reference: none (UE-only, user decisions 2026-10-25 "the
 * last man is responsible for the rear; recompute all variants" in docs/design/FORMATION_SECTORS.md, contract
 * docs/design/FORMATION_CONTRACT.md 11; reference implementation Tools/TrainingGround/src/movement/rules.ts FormationCoverRules)). Pure.
 * Tests: CodexTactics.Formation.Cover.*, parity CodexTactics.Formation.Parity.Cover / .CoverStatic.
 *
 * Allocate (walk / halt with 3+ members; replaces the per-formation sector tables on the move - LookYaw stays for the pair, run, all
 * forward, the wall layout and the standing hedgehog). Angles relative to the heading, sampled per whole degree (-179..180):
 * 1. K keeps the front: walk = his search +-ScanAmpLead, halt = 12; he guarantees +-(fov / 2 - ScanAmpLead) on the move (the whole cone
 *    standing).
 * 2. The REAR GUARD = the place with the largest Back (ties: the later) whose look is not fixed (a corner watcher hands the rear to the
 *    next man back; all fixed: the farthest back anyway): centre 6 o'clock, shifted RearWallShiftDeg to the open side when walls are
 *    within LookWallDist on ONE side of his slot; on the move he searches +-RearShareHalf round it, cut so that 5-7 o'clock (RearSector)
 *    stays inside his cone; he guarantees his cone minus the search; standing he holds the centre.
 * 3. Open = not blocked (within LookWallAngle of the closest point of a wall within LookWallDist of any slot). The open degrees nobody
 *    guarantees (K, the rear guard, the fixed looks' full cones) form gaps; the others take them greedily - the widest gap first, to the
 *    free member whose bearing from the leader is nearest its centre (ties: the earlier gap from -179, the earlier member). Gaps left
 *    over join the nearest share unless no wider than CoverageMaxGapDeg, through the rear, or across a blocked direction. A member with
 *    no gap doubles K's or the rear guard's share, whichever is nearer his bearing.
 * 4. Walk: each other member sweeps his share (centre the middle; amplitude min(half its width, CoverageMaxSweep, and for a share one
 *    cone can hold (fov + max gap - width) / 2); period ScanPeriod x max(1, amplitude / ScanAmpSide); phases alternate 0 / pi in
 *    assignment order). A share across the front: K's search runs against it. Halt: K 12, the rear guard his centre, the others the
 *    centre of the first gap they took.
 * Fixed looks (a corner watcher, a combat target, an explicit facing order) are kept as given; their cones count as covered and the
 * duty they leave (K's front, the rear) becomes a gap for the others.
 *
 * AllocateStatic (a STANDING group with looks decided elsewhere, e.g. the leader aiming round a new corner in cover): absolute yaws
 * sampled per whole degree; 1. with 3+ members one free member covers the rear (opposite the fixed looks' mean; the first fixed look if
 * they cancel; none without fixed looks) - the one whose bearing from the group centre is nearest it; blocked -> the nearest open
 * degree; 2. the others greedily: the widest uncovered open gap first, to the free member whose bearing is nearest its middle; he looks
 * at the middle; 3. no gap left -> outward along his bearing (the nearest open degree). Every assigned cone counts as covered.
 */

/** One place for the allocator. */
struct FFormationCoverMember
{
	/** The place's role (K first in the list). */
	EFormationRole Role = EFormationRole::Commander;
	/** The place's slot (not the man's position: the allocation must not jitter with his steps). */
	FVector2D Pos = FVector2D::ZeroVector;
	/** How far back the place is along the leader's path / heading, cm (the largest = the rear guard). */
	float Back = 0.f;
};

/** A role and a world yaw (a look, or a fixed look given to the allocator). */
struct FFormationRoleYaw
{
	EFormationRole Role = EFormationRole::Commander;
	float Yaw = 0.f;
};

/** A member's share of the open arc, degrees relative to the heading (From <= To; To may run past 180 when it wraps). */
struct FFormationCoverShare
{
	EFormationRole Role = EFormationRole::Commander;
	float From = 0.f;
	float To = 0.f;
	/** The centre of the first gap he took (his halt look, relative). */
	float Primary = 0.f;
};

/** Allocate's answer. */
struct FFormationCoverResult
{
	TOptional<EFormationRole> RearGuard;
	/** World look yaws: K, the rear guard, the other fixed looks, then the others in assignment order. */
	TArray<FFormationRoleYaw> Looks;
	TArray<FFormationCoverShare> Shares;
};

namespace FormationCoverRules
{
	/** deg: the rear guard always keeps 6 o'clock +- this inside his cone (the rear-cover check's 5-7 o'clock). */
	inline constexpr double RearSector = 30.0;

	/**
	 * The coverage on the move / at a halt (see the header comment). Members = the places, K first; Mode Halt = standing, anything else =
	 * walk; Time = the patrol time; Fixed = looks a higher rule sets. Fewer than 3 members: unset (use LookYaw).
	 */
	CODEXTACTICS_API TOptional<FFormationCoverResult> Allocate(const TArray<FFormationWall>& Walls, const FVector2D& LeaderPos, float Heading,
		const TArray<FFormationCoverMember>& Members, EFormationMode Mode, float Time, const FFormationConfig& Config = FFormationConfig(),
		const TArray<FFormationRoleYaw>& Fixed = TArray<FFormationRoleYaw>());

	/**
	 * The coverage round a standing group (see the header comment): Members = everyone's {role, position} (Yaw ignored), Fixed = the
	 * looks decided elsewhere, Free = the members to place (priority order). Returns the free members' world yaws in Free's order.
	 */
	CODEXTACTICS_API TArray<FFormationRoleYaw> AllocateStatic(const TArray<FFormationWall>& Walls, const TArray<FFormationSlot>& Members,
		const TArray<FFormationRoleYaw>& Fixed, const TArray<EFormationRole>& Free, const FFormationConfig& Config = FFormationConfig());
}
