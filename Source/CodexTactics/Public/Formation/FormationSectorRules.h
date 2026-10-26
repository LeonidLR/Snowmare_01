#pragma once

#include "CoreMinimal.h"
#include "Formation/FormationTypes.h"

/**
 * Formation look sectors (Godot reference: none (UE-only, design docs/design/FORMATION_SECTORS.md "Roles and sectors", visual
 * reference docs/design/formation-sectors.html, contract docs/design/FORMATION_CONTRACT.md 2; reference implementation
 * Tools/TrainingGround/src/movement/rules.ts LookYaw)). Pure: where each member looks for a formation and a mode, and which facing
 * wins. Tests: CodexTactics.Formation.Sector.*, parity CodexTactics.Formation.Parity.LookYaw.
 *
 * Look offsets relative to the reference yaw (the leader's heading; mode / formation wall: the wall's OUTWARD normal), degrees,
 * positive = right (clock c = 30 * c). scan(c, a, T, ph) = c + a * dwell(sin(2 pi t / T + ph)), dwell(s) = sign(s) * |s|^ScanDwellExp
 * (holds at the sector's ends). n = the member count.
 * - Run and Forward (v1.2, the menu's "All forward"): everybody 0 (12 o'clock), every formation.
 * - Wall (mode or formation): n = 2: K +45 (7:30), Z -45 (4:30); else K 0 (6 of the heading into the wall), L +WallFlankDeg (8),
 *   P -WallFlankDeg (4), Z -WallEndDeg (the 4th at the right end; FormationWallRules::Layout has the full answer for either end).
 * - Diamond / wedge / blizzard: n = 2 partner Z: walk scan((from + to) / 2, (to - from) / 2, PairScanPeriod, 0) = sweeps 3 -> 6 -> 9,
 *   halt the centre (6). Else Z 180 (walks backwards looking at 6); K walk scan(0, ScanAmpLead, ScanPeriod, 1.3) (light search), halt 0;
 *   P walk scan(+ScanCenterSide, ScanAmpSide, ScanPeriod, 0) (12 -> 3), halt +HaltSideDeg (2); L walk scan(-ScanCenterSide,
 *   ScanAmpSide, ScanPeriod, pi) (9 -> 12, counter-phase: both out together), halt -HaltSideDeg (10).
 * - Column: K 0, Z 180; in a NARROW passage (PassageWidth set and < ColumnNarrowWidth; v1.1, user decision: the side men do not
 *   look into the walls) P +ColumnNarrowFwdDeg (forward over the shoulder), L -ColumnNarrowBackDeg (back at a slant); else the
 *   herringbone P +ColumnSideDeg, L -ColumnSideDeg.
 * - Line: n = 2: K -LineFanInner, partner +LineFanInner; n = 3: K 0, P +LineFanOuter, L -LineFanOuter; else K -LineFanInner,
 *   P +LineFanInner, L -LineFanOuter, Z +LineFanOuter (the line runs L, K, P, Z left to right).
 * - Echelon: the man's echelon index i (FormationShapeRules::EchelonIndex) looks EchelonSectorLead / 2 / 3 / 4 to the open side
 *   (echelon right +, left -).
 * - Hedgehog: outward - K 0; n = 2: Z 180; n = 3: P +120, L -120; else P +90, L -90, Z 180.
 * Walk and halt differ only for the diamond family (the other shapes hold their sectors).
 */

/** Which facing source decides the body yaw (design: combat target > explicit facing order > cover > formation sector > movement). */
enum class EFormationFacingSource : uint8
{
	CombatTarget,
	FacingOrder,
	Cover,
	FormationSector,
	Movement
};

/** The facing candidates of one member this frame (unset = that source does not apply). */
struct FFormationFacingInputs
{
	/** Yaw to the combat target he is engaging. */
	TOptional<float> CombatTargetYaw;
	/** Yaw of an explicit facing order from the player. */
	TOptional<float> FacingOrderYaw;
	/** Yaw the cover pose dictates. */
	TOptional<float> CoverYaw;
	/** Yaw of his formation sector (FormationSectorRules::LookYaw); unset with box-select orders (each faces his own path). */
	TOptional<float> SectorYaw;
	/** Yaw of his movement direction (the fallback). */
	float MovementYaw = 0.f;
};

namespace FormationSectorRules
{
	/** Phase offset of the leader's light search, rad (formation-sectors.html patrolFace). */
	inline constexpr float LeadScanPhase = 1.3f;
	/** The pair's look offsets at a wall from the outward normal (K 7:30 / partner 4:30), deg. */
	inline constexpr float WallPairLookDeg = 45.f;

	/** dwell(S) = sign(S) * |S|^Exp, -1..1. */
	CODEXTACTICS_API float Dwell(float S, float Exp);

	/** Center + Amp * Dwell(sin(2 pi Time / Period + Phase), Exp), deg. */
	CODEXTACTICS_API float ScanValue(float Center, float Amp, float Period, float Phase, float Time, float Exp);

	/** The member's look relative to the reference yaw, deg (positive = right; see the header comment). */
	CODEXTACTICS_API float LookOffset(EFormationRole Role, EFormationKind Formation, EFormationMode Mode, int32 MemberCount, float TimeInPatrol,
		const TOptional<float>& PassageWidth = TOptional<float>(), const FFormationConfig& Config = FFormationConfig());

	/**
	 * The member's world look yaw (UE yaw, normalized): LeaderYaw + LookOffset. LeaderYaw = the leader's heading; in mode / formation wall
	 * the wall's outward normal. PassageWidth = the free width of the passage the leader is in (FormationTerrainRules::PassageWidth;
	 * unset = not between two walls).
	 */
	CODEXTACTICS_API float LookYaw(EFormationRole Role, EFormationKind Formation, EFormationMode Mode, int32 MemberCount, float TimeInPatrol,
		float LeaderYaw, const TOptional<float>& PassageWidth = TOptional<float>(), const FFormationConfig& Config = FFormationConfig());

	/**
	 * v2.1.1 (user decision: nobody looks into a wall, in ANY formation; v2.3 contract 13.1 / 13.4): a wall whose surface is within
	 * LookWallDist of Pos blocks the directions within LookWallAngle of the direction to its closest point.
	 * - Yaw is kept when it is not blocked and lies in a free arc at least LookMinFreeDeg wide (a sliver between two wall spans does
	 *   not count).
	 * - Else the candidates compete by score, the lowest wins (ties: the first): Held (his previous result; unset when ClearLook did not
	 *   apply last frame) = |Held - Yaw| - LookHoldDeg while no wall direction is within LookWallAngle - LookHoldMarginDeg of it; a free
	 *   but narrow Yaw = LookNarrowPenalty + 360 x walls swept; each free span edge (walls in order, the minus edge first) = |edge - Yaw| +
	 *   360 x the wall directions a turn from CurrentYaw to it sweeps across + LookNarrowPenalty when its arc is narrow (LookNarrowPenalty
	 *   0: narrow edges are out).
	 * - Enclosed (no candidate): the longest clear ray, every 5 deg from Yaw both ways (+ before -), capped at 2 x LookWallDist; ties: the
	 *   nearest Yaw.
	 * The integration applies it to patrol / halt / all-forward looks - not to the wall layout, running, the corner watcher or the leader
	 * looking within 35 deg of his travel - and keeps the last result per member as Held (reset whenever it does not apply).
	 */
	CODEXTACTICS_API float ClearLook(const TArray<FFormationWall>& Walls, const FVector2D& Pos, float Yaw, float CurrentYaw,
		const FFormationConfig& Config = FFormationConfig(), const TOptional<float>& Held = TOptional<float>());

	/**
	 * v2.3 (coordinator 2026-10-26, user decision 1A clarified): true when this role's look in this formation is a user decision the
	 * coverage allocator must keep - the integration passes LookYaw's look to FormationCoverRules::Allocate as a FIXED look. In a NARROW
	 * column (PassageWidth set and < ColumnNarrowWidth) with 3+ members: K, P and L; Z is not (he is the allocator's rear guard). Every
	 * other case: false. PassageWidth = the classifier's held corridor width (FFormationClassifyState::PassageWidth), for the trail
	 * column too.
	 */
	CODEXTACTICS_API bool Decided(EFormationRole Role, EFormationKind Formation, int32 MemberCount, const TOptional<float>& PassageWidth,
		const FFormationConfig& Config = FFormationConfig());

	/** The squad's pace for a mode, cm/s: walk = PaceWalkSpeed * PacePatrol, run = PaceRunSpeed, halt / wall = 0. */
	CODEXTACTICS_API float SquadPace(EFormationMode Mode, const FFormationConfig& Config = FFormationConfig());

	/** Patrol pace for a given walk speed, cm/s (WalkSpeed * PacePatrol: the rear man keeps up walking backwards). */
	CODEXTACTICS_API float PatrolSpeed(float WalkSpeed, const FFormationConfig& Config = FFormationConfig());

	/** The facing that wins (combat target > facing order > cover > formation sector > movement direction); OutYaw = its yaw. */
	CODEXTACTICS_API EFormationFacingSource ResolveFacing(const FFormationFacingInputs& Inputs, float& OutYaw);
}
