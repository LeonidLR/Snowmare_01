#pragma once

#include "CoreMinimal.h"

/**
 * Formation look sectors - shared types (Godot reference: none (UE-only, design docs/design/FORMATION_SECTORS.md, data contract
 * docs/design/FORMATION_CONTRACT.md)). Pure data, no UObjects: the rules in FormationShapeRules / FormationSectorRules /
 * FormationTerrainRules / FormationWallRules work on it, the TypeScript simulator (Tools/TrainingGround) ports the same rules and
 * golden vectors prove equality (CodexTactics.Formation.Parity.*).
 *
 * Units: cm, degrees, seconds. Positions are 2D world (X, Y). Yaw is UE yaw: atan2(dy, dx) in degrees, X forward, Y right, growing
 * from +X towards +Y (clockwise seen from above). A clock sector c relative to a heading H is the yaw H + 30 * c: 3 o'clock =
 * H + 90 = RIGHT, 9 o'clock = H - 90 = LEFT (coordinator decision 2026-10-25). Config angles are relative to the leader's heading
 * (wall.*: to the wall's outward normal), positive = right.
 */

/** A squad member's place in the formation (design table "Roles and sectors"). */
enum class EFormationRole : uint8
{
	/** K - the leader (the selected operative), slot 0, 0. */
	Commander,
	/** P - right flanker. */
	Right,
	/** L - left flanker. */
	Left,
	/** Z - rear man (4th member; the partner of a pair). */
	Rear
};

/** Formation shapes (contract names in brackets). */
enum class EFormationKind : uint8
{
	/** 4 men (rear centred) [diamond]; with 3 it is the wedge, with 2 the pair (the partner Z behind the right shoulder). */
	Diamond,
	/** 3 men triangle [wedge]; same slots as the diamond (the size picks the members). */
	Wedge,
	/** Herringbone column for narrow passages [column]. */
	Column,
	/** All abreast, slight fan forward [line]. */
	Line,
	/** Obstacle on the right, guns to the open left flank [echelon_left]. */
	EchelonLeft,
	/** Obstacle on the left, guns to the open right flank [echelon_right]. */
	EchelonRight,
	/** Along a wall, backs to it, facing the open side [wall]. */
	Wall,
	/** Ring round a point (heat source), backs to the centre, facing out [hedgehog]. */
	Hedgehog,
	/** Tight diamond in a blizzard [blizzard]. */
	Blizzard
};

/** What the squad is doing (contract: walk | run | halt | wall). */
enum class EFormationMode : uint8
{
	/** Walk = patrol: scans with dwell, the squad at the patrol pace. */
	Walk,
	/** Run (double click): everybody faces 12. */
	Run,
	/** Short halt: everybody turns to the centre of his sector. */
	Halt,
	/** At a wall (group cover in formation mode): the wall layout's yaws. */
	Wall,
	/** v1.2: the menu's "All forward" (10:30) - walking, everybody faces 12 whatever the formation [forward]. */
	Forward
};

/** Terrain around the leader (contract: open | wall_left | wall_right | corridor | at_wall). */
enum class EFormationTerrainClass : uint8
{
	Open,
	WallLeft,
	WallRight,
	Corridor,
	AtWall
};

/** A side relative to a heading. */
enum class EFormationSide : uint8
{
	Left,
	Right
};

/** One member's place: role, position and (Slots: the halt look yaw; wall layout: the look yaw). */
struct FFormationSlot
{
	EFormationRole Role = EFormationRole::Commander;
	FVector2D Position = FVector2D::ZeroVector;
	float Yaw = 0.f;
};

/** A straight wall segment: its centre line A -> B and its thickness (0 = a thin line), cm (layout JSON walls[]). */
struct FFormationWall
{
	FVector2D A = FVector2D::ZeroVector;
	FVector2D B = FVector2D::ZeroVector;
	float Thickness = 0.f;
};

/**
 * Every tunable of the formation rules. The defaults mirror Content/Data/FormationLab/formation_config.json (owned by the
 * Training Ground simulator session; keys in brackets, read by FormationTuning). Angles: degrees relative to the leader's heading
 * (wall.*: to the wall's outward normal), positive = right.
 */
struct CODEXTACTICS_API FFormationConfig
{
	/** [slot.side_offset] Flankers P / L sideways from the leader, cm. */
	float SlotSideOffset = 260.f;
	/** [slot.back_offset] Flankers P / L behind the leader, cm. */
	float SlotBackOffset = 280.f;
	/** [diamond.rear_back] Rear man Z straight behind the leader, cm. */
	float DiamondRearBack = 560.f;
	/** [triad.rear_back] v2.2: the triad's rear man Z straight behind the leader (diamond / wedge of three), cm. */
	float TriadRearBack = 420.f;
	/** [slot.min_spacing] Two members closer than this count as a spacing failure (checks only), cm. */
	float SlotMinSpacing = 80.f;
	/** [pair.side_offset] Pair: the partner to the right of the leader, cm. */
	float PairSideOffset = 180.f;
	/** [pair.back_offset] Pair: the partner behind the leader, cm. */
	float PairBackOffset = 220.f;
	/** [pair.scan_period] Pair: the partner's sweep period, s. */
	float PairScanPeriod = 5.f;
	/** [pair.scan_from] Pair: the partner's sweep from (90 = 3 o'clock), deg. */
	float PairScanFrom = 90.f;
	/** [pair.scan_to] Pair: the partner's sweep to (270 = 9 o'clock; the arc passes through 6), deg. */
	float PairScanTo = 270.f;
	/** [column.base_back] Column: the first follower's distance is BaseBack + Spacing (FSquadFormationConfig::ColumnBaseBack), cm. */
	float ColumnBaseBack = 180.f;
	/** [column.spacing] Column: spacing between followers, cm. */
	float ColumnSpacing = 150.f;
	/** [column.stagger] Column herringbone: the 2nd man this far right, the 3rd this far left, cm. */
	float ColumnStagger = 60.f;
	/** [column.stagger_clear] v2.3: the trail column's stagger shrinks until the slot keeps this from every wall surface, cm. */
	float ColumnStaggerClear = 45.f;
	/** [line.spacing] Line: spacing between neighbours, cm. */
	float LineSpacing = 260.f;
	/** [line.fan_inner] Line: the inner men look this far out from 12, deg. */
	float LineFanInner = 5.f;
	/** [line.fan_outer] Line: the outermost men look this far out from 12, deg. */
	float LineFanOuter = 25.f;
	/** [echelon.step] Echelon: each man this far behind and to the open side of the one before, cm. */
	float EchelonStep = 180.f;
	/** [echelon.sector_lead] Echelon: the leader's sector towards the open side, deg. */
	float EchelonSectorLead = 10.f;
	/** [echelon.sector_2] Echelon: the 2nd man's sector, deg. */
	float EchelonSector2 = 45.f;
	/** [echelon.sector_3] Echelon: the 3rd man's sector, deg. */
	float EchelonSector3 = 80.f;
	/** [echelon.sector_4] Echelon: the 4th man's sector (rear on the open side; v1.1, user: 150 keeps 5-7 o'clock watched), deg. */
	float EchelonSector4 = 150.f;
	/** [blizzard.side_offset] Blizzard tight diamond: flankers sideways, cm. */
	float BlizzardSideOffset = 110.f;
	/** [blizzard.back_offset] Blizzard tight diamond: flankers back (the rear man twice this), cm. */
	float BlizzardBackOffset = 120.f;
	/** [hedgehog.radius] Hedgehog: ring radius round the centre, cm. */
	float HedgehogRadius = 130.f;
	/** [wall.spacing] Wall layout: neighbours along the wall, cm. */
	float WallSpacing = 100.f;
	/** [wall.face_offset] Wall layout: body centre off the wall line, cm. */
	float WallFaceOffset = 45.f;
	/** [wall.flank_deg] Wall layout: the flankers' look from the outward normal (L 8, P 4 o'clock), deg. */
	float WallFlankDeg = 60.f;
	/** [wall.neighbour_deg] Wall layout with 4: the 4th man's neighbour turns to the centre (4 -> 5 / 8 -> 7), deg. */
	float WallNeighbourDeg = 30.f;
	/** [wall.end_deg] Wall layout with 4: the 4th man at the end takes the flank along the wall (3-4 / 8-9), deg. */
	float WallEndDeg = 75.f;
	/** [pace.patrol] Patrol (walk) pace as a fraction of the walk speed. */
	float PacePatrol = 0.8f;
	/** [pace.walk_speed] Walk speed (OperativeMovementRules WalkSpeed), cm/s. */
	float PaceWalkSpeed = 220.f;
	/** [pace.run_speed] Run speed of the squad (sprint), cm/s. */
	float PaceRunSpeed = 480.f;
	/** [move.backward_mult] A member walking backwards moves at most this fraction of the walk speed. */
	float MoveBackwardMult = 0.9f;
	/** [move.catchup_mult] A member far behind his slot may move this much faster than the squad pace. */
	float MoveCatchupMult = 1.25f;
	/** [facing.turn_speed] Body turn rate (FacingRules::StepYaw turn speed), 1/s. */
	float FacingTurnSpeed = 10.f;
	/** [scan.period] Patrol scan period, s. */
	float ScanPeriod = 3.f;
	/** [scan.amp_side] Flankers' scan amplitude round their sector centre, deg. */
	float ScanAmpSide = 45.f;
	/** [scan.center_side] Flankers' walk sector centre (P +45 = 1:30, L -45 = 10:30), deg. */
	float ScanCenterSide = 45.f;
	/** [scan.amp_lead] The leader's search amplitude round 12 (v1.2: 30 = 11 - 1 o'clock, as the design says), deg. */
	float ScanAmpLead = 30.f;
	/** [scan.dwell_exp] Scan shape sign(sin) * |sin|^exp: below 1 it dwells at the sector's ends. */
	float ScanDwellExp = 0.45f;
	/** [halt.side_deg] Short halt: the flankers' sector centres (P 2, L 10 o'clock), deg. */
	float HaltSideDeg = 60.f;
	/** [column.side_deg] Wide column (herringbone) sectors P +70 / L -70, deg. */
	float ColumnSideDeg = 70.f;
	/** [column.narrow_width] v1.1 (user): a column in a passage narrower than this uses the narrow sectors, cm. */
	float ColumnNarrowWidth = 600.f;
	/** [column.narrow_fwd_deg] Narrow column: P looks this far right of the heading (forward over the leader's shoulder), deg. */
	float ColumnNarrowFwdDeg = 15.f;
	/** [column.narrow_back_deg] Narrow column: L looks back at a slant, this far LEFT of the heading (-value), deg. */
	float ColumnNarrowBackDeg = 165.f;
	/** [classify.probe_side] Terrain probe: a wall within this lateral distance to a side marks that side as blocked, cm. */
	float ClassifyProbeSide = 400.f;
	/** [classify.probe_ahead] Forward look-ahead range (simulator only; the C++ classifier's forward rays reach ClassifyAtWallDist), cm. */
	float ClassifyProbeAhead = 600.f;
	/**
	 * [classify.hold_distance] v2 terrain hysteresis: a LOOSER class takes over only after the leader walked this far with it (tighter
	 * ones at once); >= the column length so its tail leaves a passage first, cm.
	 */
	float ClassifyHoldDistance = 500.f;
	/** [classify.at_wall_dist] Terrain: on ARRIVAL (ClassifyArriveDist) a wall ahead within this distance = at_wall, cm. */
	float ClassifyAtWallDist = 150.f;
	/** [classify.arrive_dist] v1.2: the leader is arriving within this distance of his move's destination (at_wall only then), cm. */
	float ClassifyArriveDist = 50.f;
	/** [classify.side_ignore_perp_deg] v1.1: the side probes ignore wall segments within this angle of perpendicular to the heading, deg. */
	float ClassifySideIgnorePerpDeg = 30.f;
	/** [sight.fov] Vision cone width standing / crouched (simulator checks only; the game owns the real value), deg. */
	float SightFov = 140.f;
	/** [look.wall_dist] v2.1.1 (user): a wall surface closer than this blocks looks towards it (FormationSectorRules::ClearLook), cm. */
	float LookWallDist = 260.f;
	/** [look.wall_angle] v2.1.1: half-width of the blocked span round the direction to the wall's closest point, deg. */
	float LookWallAngle = 55.f;
	/** [look.min_free_deg] v2.3: a free arc narrower than this does not count for ClearLook (a sliver between two wall spans), deg. */
	float LookMinFreeDeg = 30.f;
	/** [look.hold_deg] v2.3: ClearLook's held look competes this much better than its distance to the wanted yaw (0 = memoryless), deg. */
	float LookHoldDeg = 60.f;
	/** [look.hold_margin_deg] v2.3: the held look stays while no wall direction is within LookWallAngle - this of it, deg. */
	float LookHoldMarginDeg = 5.f;
	/** [look.narrow_penalty] v2.3: a free but narrow direction still competes this much worse (0 = the hard cut), deg. */
	float LookNarrowPenalty = 180.f;
	/** [guard.clearance] v2.1: a slot is cut off when the leader -> slot / chain segment passes closer than this to a wall body, cm. */
	float GuardClearance = 10.f;
	/** [guard.lookahead] v2.1: the guard also checks the formation at the leader's pose this far ahead on his path (0 = only here), cm. */
	float GuardLookahead = 500.f;
	/** [turn.fold_deg] v2.1.1 (user): a heading change sharper than this within TurnLookahead folds an echelon / line into the trail column, deg. */
	float TurnFoldDeg = 25.f;
	/** [turn.lookahead] v2.1.1: how far ahead on the leader's path a sharp turn is looked for (FormationGuardRules::TurnAhead), cm. */
	float TurnLookahead = 800.f;
	/** [bypass.max_detour] v2.1 (user): the shielded side of a wall is taken when at most this much longer than the shortest (ratio). */
	float BypassMaxDetour = 0.3f;
	/** [bypass.threat_memory_s] v2.1: a threat seen / heard counts as known this long after its last contact, s. */
	float BypassThreatMemoryS = 20.f;
	/** [corner.lead_dist] v2.1 slicing the pie: the corner man starts when the leader is this far before the wall end, cm. */
	float CornerLeadDist = 300.f;
	/** [corner.hold_dist] v2.1: ... and keeps looking until the leader is this far past it, cm. */
	float CornerHoldDist = 200.f;
	/** [corner.peek_dist] v2.1: once past the end, he looks at the point this far behind the wall's far face, cm. */
	float CornerPeekDist = 300.f;
	/** [rear.share_half] v2.2: the rear guard's search half-width round his centre on the move, deg. */
	float RearShareHalf = 30.f;
	/** [rear.wall_shift_deg] v2.2: the rear guard's centre shifts this far to the open side with walls on one side of his slot, deg. */
	float RearWallShiftDeg = 30.f;
	/** [coverage.max_gap_deg] v2.2: an open gap no wider than this may stay open (stretching a sweep for it opens more), deg. */
	float CoverageMaxGapDeg = 60.f;
	/** [coverage.max_sweep] v2.2: a member's sweep amplitude cap round his share's middle, deg. */
	float CoverageMaxSweep = 90.f;
	/** [slide.max_off_wall] v2.2: a destination within this of the wall surface (same wall, squad's side) is a wall slide, cm. */
	float SlideMaxOffWall = 200.f;
	/** [slide.lead_out_deg] v2.2: the slide's leading / trailing ends look along the wall turned this far outward, deg. */
	float SlideLeadOutDeg = 20.f;
};

/** Names, clock math and the shared helpers of the formation rules. */
namespace FormationNames
{
	/** Contract name of a formation ("diamond", "echelon_right", ...). */
	CODEXTACTICS_API const TCHAR* ToString(EFormationKind Kind);
	/** Contract name of a role ("K", "P", "L", "Z"). */
	CODEXTACTICS_API const TCHAR* ToString(EFormationRole Role);
	/** Contract name of a mode ("walk", "run", "halt", "wall"). */
	CODEXTACTICS_API const TCHAR* ToString(EFormationMode Mode);
	/** Contract name of a terrain class ("open", "wall_left", ...). */
	CODEXTACTICS_API const TCHAR* ToString(EFormationTerrainClass Class);

	/** Parses a contract name (case-insensitive); false when unknown. */
	CODEXTACTICS_API bool Parse(const FString& Name, EFormationKind& Out);
	CODEXTACTICS_API bool Parse(const FString& Name, EFormationRole& Out);
	CODEXTACTICS_API bool Parse(const FString& Name, EFormationMode& Out);
	CODEXTACTICS_API bool Parse(const FString& Name, EFormationTerrainClass& Out);
}

namespace FormationMath
{
	/** UE yaw of clock sector Clock relative to HeadingYaw: HeadingYaw + 30 * Clock, normalized to (-180, 180]. */
	CODEXTACTICS_API float ClockToYaw(float Clock, float HeadingYaw);
	/** Clock sector (0..12) of Yaw relative to HeadingYaw. */
	CODEXTACTICS_API float YawToClock(float Yaw, float HeadingYaw);
	/** Unit forward of a UE yaw: (cos, sin). */
	CODEXTACTICS_API FVector2D Forward(float Yaw);
	/** Unit right of a UE yaw (= Forward(Yaw + 90)): (-sin, cos). */
	CODEXTACTICS_API FVector2D Right(float Yaw);
	/** UE yaw of a direction, degrees. */
	CODEXTACTICS_API float YawOf(const FVector2D& Direction);
	/** Absolute angle between two yaws, 0..180 degrees. */
	CODEXTACTICS_API float AngleBetween(float YawA, float YawB);
	/** LeaderPos + Forward * ForwardCm + Right * RightCm. */
	CODEXTACTICS_API FVector2D Offset(const FVector2D& LeaderPos, float LeaderYaw, float ForwardCm, float RightCm);

	// --- v2.1 double-precision geometry (Training Ground geometry.ts: same maths, so the golden vectors agree) ---

	/** Yaw normalized to (-180, 180] (geometry.ts normYaw), double. */
	CODEXTACTICS_API double NormYaw(double Yaw);
	/** Unit vector of a yaw, double (geometry.ts dirOf). */
	CODEXTACTICS_API FVector2D DirOf(double Yaw);
	/** Yaw of a vector, double; 0 for a zero vector (geometry.ts yawOf). */
	CODEXTACTICS_API double YawOfD(const FVector2D& V);
	/** The point of segment A -> B nearest to P (parameter clamped to 0..1; OutT = the parameter). */
	CODEXTACTICS_API FVector2D ClosestOnSegment(const FVector2D& P, const FVector2D& A, const FVector2D& B, double* OutT = nullptr);
	/** Distance from P to the wall's surface: centre-line distance minus half the thickness, never below 0. */
	CODEXTACTICS_API double DistToWall(const FVector2D& P, const FFormationWall& Wall);
	/** Minimum distance between segments P -> Q and A -> B (0 when they cross properly). */
	CODEXTACTICS_API double SegSegDist(const FVector2D& P, const FVector2D& Q, const FVector2D& A, const FVector2D& B);
	/**
	 * True when the straight move P -> Q passes closer than Thickness / 2 + Radius to a wall's centre line (a wall body widened by
	 * Radius); a zero-length move: P inside such a body.
	 */
	CODEXTACTICS_API bool SegmentBlocked(const FVector2D& P, const FVector2D& Q, const TArray<FFormationWall>& Walls, double Radius);
}
