#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

/**
 * The squad on a narrow ramp / stair: single file (the game designer's bug report 2026-11-02 on L_Gen_Depot_1, item 7: "the squad stands
 * together at the foot of a ramp, one operative ordered up onto the roof - they all just get STUCK at the bottom; moved aside and ordered
 * again, 2 of 3 went up and 1 stayed on the ground"). Godot reference: none (UE-only; the generated maps' 1.5 m external stairs / stairwells).
 *
 * Root causes (LevelGenRampSmoke traces): the ramp's navmesh corridor is 80 cm wide (150 cm ramp - 2 x the 35 cm agent radius), one body
 * of 80 cm. (1) Two men walking up it side by side - a box-selected group, a follower catching up - pressed against each other in the
 * Detour crowd and crawled at 5-15 cm/s until their walks ended "blocked" at the foot; (2) a follower's trail slot behind a leader who
 * stopped just past the ramp's top lay ON the ramp: he walked back down and parked there, and the next man up pushed into him for good;
 * (3) in Solo the holding team-mates bunched in the ramp's mouth stood on the selected man's way - nobody stepped aside, his walk ended
 * blocked at the foot. The fixes: the ramp file below (the man behind waits until the man ahead is FileGapCm up / down the ramp - the
 * ladder queue's idea without a lease), no formation slot on a ramp while the leader stands on a floor (ShouldDropRampSlot), and a holding
 * team-mate on the selected man's path steps aside (FormationRuntime::YieldFromLeaderPath).
 *
 * Pure. Tests: CodexTactics.Formation.RampFile.*. Runtime: USquadSubsystem (SquadSubsystemRampFile.cpp), CVar Codex.Squad.RampFile (1;
 * 0 = the old behaviour, A/B).
 */
namespace RampFileRules
{
	struct FRampFileConfig
	{
		/** A floor sloped at least this much (a static mesh, never terrain) is a ramp / stair, deg. The generated maps' ramps: 23-35 deg. */
		float MinSlopeDeg = 12.f;
		/** The man behind waits while the man ahead (same way) is closer than this along the ramp, cm (~2 body lengths). */
		float FileGapCm = 170.f;
		/** Closer than this along the ramp the two count as side by side: the one with the later priority waits, cm. */
		float SideBySideCm = 5.f;
		/** A waiting man walks on only once the man ahead is FileGapCm + this along (no stop-and-go at the gap's edge), cm. */
		float ReleaseMarginCm = 40.f;
		/** A man off the ramp this close (2D) to a man on it, and about to change level, is in its mouth (in the file), cm. */
		float MouthCm = 260.f;
		/** ... and no more than this above / below him, cm. */
		float MouthHeightCm = 200.f;
		/** ... and no more than this off the ramp's line beside him (a man walking past beside the ramp is not in its mouth), cm. */
		float MouthLateralCm = 100.f;
		/** A man counts as making progress for this long after he last walked at ProgressSpeed (a crawl in the crowd never flips the file), s. */
		float ProgressMemorySeconds = 0.6f;
		/** A walk whose goal lies this far above / below his feet changes level (up / down the ramp), cm. Half a 3 m level: a formation
		 *  slot's height is the leader's capsule centre (~90 cm over his feet), never mistaken for a level change. */
		float LevelChangeCm = 150.f;
		/** Walking at least this fast he makes progress (the man behind may wait for him), cm/s. */
		float ProgressSpeed = 30.f;
		/** A holder steps aside to a spot at least this far off the selected man's path (two 40 cm bodies pass, with room), cm. */
		float YieldSpotClearCm = 130.f;
		/** No wait lasts longer than this (a stuck man ahead never holds the file for good), s. */
		float MaxHoldSeconds = 6.f;
		/** ... after which he is not held again for this long, s. */
		float RetryAfterSeconds = 2.f;
	};

	/** The floor under him is a ramp / stair: a static mesh (not terrain) sloped at least MinSlopeDeg. */
	CODEXTACTICS_API bool IsRampSurface(const FVector& FloorNormal, bool bTerrain, float MinSlopeDeg);

	/** The ramp's uphill direction (2D, unit) from its surface normal; zero on a flat floor. */
	CODEXTACTICS_API FVector2D UphillOf(const FVector& FloorNormal);

	/**
	 * +1 his walk's goal is a level up, -1 a level down, 0 no level change (or no walk). On a ramp a goal just past its end (less than
	 * LevelChangeCm above / below) still counts: his way along the ramp (UphillSpeed = his velocity . the uphill axis, cm/s) at ProgressSpeed+.
	 */
	CODEXTACTICS_API int32 TravelDirection(float FeetZ, float DestinationZ, bool bHasWalk, float LevelChangeCm, bool bOnRamp = false,
		float UphillSpeed = 0.f, float ProgressSpeed = 30.f);

	/** One squad member on a ramp or in its mouth. */
	struct FRampWalker
	{
		/** Where he is along the ramp's uphill axis, cm. */
		float Along = 0.f;
		/** +1 up the ramp, -1 down it, 0 not changing level (no file for him). */
		int32 Direction = 0;
		/** Walking on (at least ProgressSpeed) or waiting in the file himself - the man behind may wait for him. */
		bool bProgressing = false;
		/** Lower = earlier: the leader first, then the roster order (side by side the later one waits). */
		int32 Priority = 0;
	};

	/**
	 * The walker Me must wait behind now (INDEX_NONE = he walks on): the nearest walker going the same way that is ahead of him along the
	 * ramp (in their walking direction; level within SideBySideCm: the earlier priority is ahead) closer than FileGapCm (+ ReleaseMarginCm
	 * while Me already waits) and makes progress. The order is strict (never two waiting for each other): the front man always walks.
	 */
	CODEXTACTICS_API int32 FindWaitBehind(int32 Me, TArrayView<const FRampWalker> Walkers, const FRampFileConfig& Config, bool bMeWaiting = false);

	/**
	 * A man off the ramp (OffsetFromOnRamp = his 2D position minus a man's on it, Uphill = the ramp's axis) is in that man's file - its mouth -
	 * when he is within MouthCm of him, MouthLateralCm of the ramp's line, and BEHIND him in their walking Direction (a man beside the ramp or
	 * already past its far end is not).
	 */
	CODEXTACTICS_API bool IsInMouth(const FVector2D& OffsetFromOnRamp, float HeightDifference, const FVector2D& Uphill, int32 Direction, const FRampFileConfig& Config);

	/**
	 * Where a holding team-mate standing on the selected man's path (Path[0] = the selected man, then the corners of his path) may step
	 * to: points on rings of 130 / 190 / 250 / 310 cm round Here at least CorridorCm off the path (FormationRuntime::YieldFromLeaderPath finds
	 * them in nobody's way), his own side of the path first (crossing it walks into the selected man), then the nearest. The caller keeps
	 * the first on the navmesh, on his level, off a ramp and clear of the others.
	 */
	CODEXTACTICS_API TArray<FVector2D> YieldSpots(TArrayView<const FVector2D> Path, const FVector2D& Here, float CorridorCm, float AheadCm);

	/** A follower's formation slot on a ramp is dropped (a stand-in on the leader's floor) unless the leader himself is on a ramp. */
	CODEXTACTICS_API bool ShouldDropRampSlot(bool bLeaderOnRamp, bool bSlotOnRamp);

	/** He waited HeldSeconds: the wait ends (MaxHoldSeconds) - the man ahead is stuck, not walking. */
	CODEXTACTICS_API bool HoldExpired(float HeldSeconds, const FRampFileConfig& Config);
}
