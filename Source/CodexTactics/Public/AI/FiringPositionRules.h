#pragma once

#include "CoreMinimal.h"
#include "Characters/OperativeMovementRules.h"
#include "Tactics/CoverTypes.h"

class FJsonObject;

/**
 * FIRING POSITIONS (smart enemies T-05, docs/design/gdd/smart-enemies.md section 5 + formula F2, roadmap T-05; UE-only, no Godot
 * reference): a stand point + stance + facing + the area it covers, valid only when a line of fire from the muzzle at that stance reaches
 * the covered area. This file is the PURE half (no UWorld): the data, F2 scoring, the crossfire test, the covered share and the greedy
 * pickers. UFiringPositionSubsystem gathers the candidates and traces the lines. Numbers: the "firing_positions" block of
 * Content/Data/AI/pmc_commander.json (frozen, not trainer genes; the defaults here equal the file). Tested in CodexTactics.AI.FiringPosition.*.
 */

/** What a firing position is (GDD section 5 table). Corner / LowCover = cover slots; Window / RoofEdge = the tactical map's stands. */
enum class EFiringPositionKind : uint8
{
	/** A high wall's exposed corner: he leans out of it (CoverRules::CornerLeanPoint). */
	Corner,
	/** A low wall / crate: he fires over the top. */
	LowCover,
	/** Inside a window, beside the frame (tactical map windows[].stand). */
	Window,
	/** A roof spot at the parapet; the stance (and a step toward the rim) is whatever the traces prove. */
	RoofEdge,
	/** Above a flight of stairs. NOT built yet: the tactical file has no stair-head geometry (T-05 report). */
	StairHead,
	/** A mantle target (container, crate stack, wall top). NOT built yet: the mantle edge data is on the unmerged building-generator branch. */
	ObstacleTop
};

/** One firing position. bValid = a traced line of fire from FireOrigin reaches CoveredShare of the area's samples (>= the configured minimum). */
struct CODEXTACTICS_API FFiringPosition
{
	EFiringPositionKind Kind = EFiringPositionKind::Corner;
	/** Where his feet stand (a roof spot after the rim step: the stepped point). */
	FVector StandPoint = FVector::ZeroVector;
	/** The stance that fires (a roof edge fires standing: the parapet is above a kneeling muzzle). */
	EOperativeStance Stance = EOperativeStance::Standing;
	/** UE yaw, degrees, toward the middle of the covered samples. */
	float FacingYaw = 0.f;
	/** 0..1: the share of the area's samples (inside the weapon band) with a clear traced line. */
	float CoveredShare = 0.f;
	/** The object he hides behind (CoverObjectKey; spacing, "one man per object"); empty for a roof / window. */
	FString CoverKey;
	/** The source: a tactical roof building / window id / cover wall. For logs and the cache key. */
	FString SourceId;
	/** The muzzle point the lines were traced from (the corner lean point, the stepped muzzle). */
	FVector FireOrigin = FVector::ZeroVector;
	/** Centimetres the stand point was stepped toward the rim (RoofEdge). */
	float RimStepCm = 0.f;
	/** Corner / LowCover: the cover slot the position was found at (the man takes it with the TakeCover order: bInCover, the corner lean). */
	FCoverSlot Slot;
	bool bHasSlot = false;
	bool bValid = false;
};

/** A position with its F2 score (one query's candidate list). */
struct CODEXTACTICS_API FFiringCandidate
{
	FFiringPosition Position;
	float Score = 0.f;
};

/** Numbers of the "firing_positions" block (the defaults = the shipped JSON). Distances cm, angles degrees. */
struct CODEXTACTICS_API FFiringPositionConfig
{
	/** "crossfire_min_deg" (45): two hammers' bearings from the kill zone differ by at least this (best 60-120, GDD F2). */
	float CrossfireMinDeg = 45.f;
	/** "line_clear_cm" (100): a mate this close to the segment shooter -> kill zone is in his line of fire. */
	float LineClearCm = 100.f;
	/** "area_samples" (9): a 3x3 grid over the area. Only the squares 4 and 9 are meaningful (1 = the centre, 9 = the grid). */
	int32 AreaSamples = 9;
	/** "sample_height_cm" (80): the aim point above the sample's floor - AOperativeCharacter::EvaluateShotLine aims at feet + 80, so a clear line here is a clear real shot. */
	float SampleHeightCm = 80.f;
	/** "min_covered_share" (0.34): a position covering less of the area is dropped (not a position). */
	float MinCoveredShare = 0.34f;
	/** "rim_step_cm" (10) / "rim_step_max_cm" (80): a roof spot steps toward the rim in these steps while the trace stays blocked. */
	float RimStepCm = 10.f;
	float RimStepMaxCm = 80.f;
	/**
	 * "rim_search_max_cm" (6000): how far from the file's roof spot toward the area the walk surface is followed to find the rim. T-05's diagnosis: the
	 * overwatch spots are NOT at the rim (B_hall_n: the walk surface runs on 15+ m past the spot and the line to the street hits the roof slab 1.4-6 m
	 * ahead), so the rim step is counted back from the rim: rim - 0 .. rim - rim_step_max_cm.
	 */
	float RimSearchMaxCm = 6000.f;
	/** "muzzle_height_cm": the muzzle above the feet per stance - a mirror of AOperativeCharacter::GetMuzzleLocation (checked by FiringPositionSmoke). */
	float MuzzleStandingCm = 140.f;
	float MuzzleCrouchingCm = 85.f;
	float MuzzleProneCm = 25.f;
	/** "w_danger" (0.6): F2's (1 - w_danger * danger). */
	float DangerWeight = 0.6f;
	/** "roof_protect" (0.7) + "high_ground" (0.2 per storey): the protect term of a roof edge (the parapet + the height). */
	float RoofProtect = 0.7f;
	float HighGroundPerLevel = 0.2f;
	/** "window_protect" (0.8): the wall beside the frame. */
	float WindowProtect = 0.8f;
	/** "escape_*": F2's escape term - a second way out 1, one way 0.6, a ladder-only roof 0.3. */
	float EscapeSecondWay = 1.f;
	float EscapeOneWay = 0.6f;
	float EscapeLadderOnly = 0.3f;
	/** "min_spacing_m" (5) / "spacing_penalty" (0.25): the G008 numbers (a position within this of a chosen one scores x penalty; the same cover object 0). */
	float MinSpacingCm = 500.f;
	float SpacingPenalty = 0.25f;
	/** "scan_step_cm" (600), "scan_rays" (8), "scan_reach_cm" (350), "max_scan_points" (160): the cover-slot scan of an area ring (a trace budget, one query per round). */
	float ScanStepCm = 600.f;
	int32 ScanRays = 8;
	float ScanReachCm = 350.f;
	int32 MaxScanPoints = 160;
	/** "max_cover_slots" (120): wall hits turned into cover slots per area (FindCoverSlotAt is ~25 traces each). */
	int32 MaxCoverSlots = 120;
	/** "defence_use" (true): objective defenders take firing positions before contact (phase b; PmcSquadSubsystem). */
	bool bDefenceUse = true;

	/** The muzzle height above the feet of a stance, cm. */
	float MuzzleHeight(EOperativeStance Stance) const;
};

/** The inputs of F2. Every term 0..1 except Danger (0..1 as DangerAt). */
struct CODEXTACTICS_API FFiringScoreInputs
{
	float Fire = 0.f;
	float Protect = 1.f;
	float Escape = 1.f;
	float Danger = 0.f;
	float Spacing = 1.f;
	float RoleFit = 1.f;
};

/** Stances a query may use, a bit per EOperativeStance. A sniper fires only kneeling (SniperRules::FiringStance): his query is Crouching only. */
namespace FiringStances
{
	constexpr uint8 Standing = 1 << static_cast<uint8>(EOperativeStance::Standing);
	constexpr uint8 Crouching = 1 << static_cast<uint8>(EOperativeStance::Crouching);
	/** The default: a rifleman stands or crouches (prone is not a position stance). */
	constexpr uint8 Default = Standing | Crouching;
	inline bool Allows(uint8 Mask, EOperativeStance Stance) { return (Mask & (1 << static_cast<uint8>(Stance))) != 0; }
}

namespace FiringPositionRules
{
	CODEXTACTICS_API const TCHAR* KindName(EFiringPositionKind Kind);

	/** The stance name for logs ("standing" / "crouching" / "prone"). */
	CODEXTACTICS_API const TCHAR* StanceName(EOperativeStance Stance);

	/** Reads the "firing_positions" object of pmc_commander.json over Base (missing keys keep Base; values are clamped to sane ranges). */
	CODEXTACTICS_API FFiringPositionConfig ConfigFromJson(const FJsonObject& Block, const FFiringPositionConfig& Base = FFiringPositionConfig());

	/** The "firing_positions" block of Content/Data/AI/pmc_commander.json; the defaults when the file or the block is missing. */
	CODEXTACTICS_API FFiringPositionConfig LoadConfig();

	/**
	 * GDD F2 exactly: S = fire * protect * escape * (1 - w_danger * danger) * spacing * role_fit. fire <= 0 (cannot fire) scores 0,
	 * whatever the other terms; every term is clamped to 0..1 first.
	 */
	CODEXTACTICS_API float ScoreF2(const FFiringScoreInputs& Inputs, float DangerWeight);

	/** hits / samples (0 with no samples), clamped 0..1. */
	CODEXTACTICS_API float CoveredShare(int32 Hits, int32 Samples);

	/** The planar angle (deg, 0..180) between the kill zone's bearings to A and to B. 0 when either stands on the zone. */
	CODEXTACTICS_API float CrossfireAngleDeg(const FVector& A, const FVector& B, const FVector& KillZone);

	/**
	 * Mate stands in Shooter's line of fire to KillZone: his planar projection on the segment shooter -> zone lies strictly inside it and
	 * he is within HalfWidthCm of the line (a hammer firing past his own mate's shoulder).
	 */
	CODEXTACTICS_API bool InLineOfFire(const FVector& Shooter, const FVector& Mate, const FVector& KillZone, float HalfWidthCm);

	/** GDD F2 crossfire: the bearings differ by >= MinDeg and neither hammer stands in the other's line of fire. */
	CODEXTACTICS_API bool CrossfireOk(const FVector& H1, const FVector& H2, const FVector& KillZone, float MinDeg, float LineClearCm);

	/**
	 * F2's spacing term of Pos against the positions already chosen / claimed: 0 on the same cover object (non-empty key), SpacingPenalty
	 * within MinSpacingCm of one, else 1 (PmcDefenceRules::SpacingFactor, the G008 rule).
	 */
	CODEXTACTICS_API float SpacingOf(const FFiringPosition& Pos, TConstArrayView<FFiringPosition> Chosen, TConstArrayView<FString> ClaimedKeys,
		const FFiringPositionConfig& Config);

	/**
	 * The best N valid candidates by Score x spacing, greedy: the best first, then the best of the rest re-scored against what is
	 * already chosen. Invalid or score <= 0 candidates are never picked; ties go to the lower input index. Returns indices into Candidates.
	 */
	CODEXTACTICS_API TArray<int32> PickN(TConstArrayView<FFiringCandidate> Candidates, int32 N, TConstArrayView<FString> ExcludeKeys,
		const FFiringPositionConfig& Config);

	/**
	 * Two hammers on KillZone: the best candidate that has a partner passing CrossfireOk, then its best partner (Score x spacing).
	 * False when no pair exists (OutFirst / OutSecond untouched). Both are indices into Candidates.
	 */
	CODEXTACTICS_API bool PickCrossfirePair(TConstArrayView<FFiringCandidate> Candidates, const FVector& KillZone, TConstArrayView<FString> ExcludeKeys,
		const FFiringPositionConfig& Config, int32& OutFirst, int32& OutSecond);

	/**
	 * The 3x3 (AreaSamples 9) or centre-only (1) sample points of a circular area on its floor: centre +- (2/3 R) on each axis.
	 * Heights are the floor's (the caller adds SampleHeightCm).
	 */
	CODEXTACTICS_API TArray<FVector> AreaSamplePoints(const FVector& Centre, float RadiusCm, int32 AreaSamples);

	/**
	 * Which samples count toward the share: those whose distance from the muzzle lies inside [BandMinCm, BandMaxCm] (a band with
	 * BandMaxCm <= 0 has no upper limit; BandMinCm <= 0 no lower one) and within MaxRangeCm (<= 0: unlimited).
	 */
	CODEXTACTICS_API bool SampleInBand(float DistanceCm, float BandMinCm, float BandMaxCm, float MaxRangeCm);

	/** The escape term of a roof by its access tokens: a ladder-only roof EscapeLadderOnly, >= 2 ways EscapeSecondWay, else EscapeOneWay. */
	CODEXTACTICS_API float RoofEscape(int32 AccessCount, bool bLadderOnly, const FFiringPositionConfig& Config);

	/** The protect term of a roof edge: RoofProtect + HighGroundPerLevel x levels (the storeys the roof stands over), at most 1. */
	CODEXTACTICS_API float RoofProtectOf(int32 Levels, const FFiringPositionConfig& Config);

	/**
	 * The offsets back from the rim tried in turn: 0, RimStepCm, 2 x RimStepCm ... up to RimStepMaxCm (inclusive). Always at least {0}.
	 */
	CODEXTACTICS_API TArray<float> RimSteps(const FFiringPositionConfig& Config);

	/** One line for the log: "[FiringPos] <kind> <source> stand (x, y, z) <stance> yaw covered 0.xx". */
	CODEXTACTICS_API FString LogLine(const FFiringPosition& Pos);
}
