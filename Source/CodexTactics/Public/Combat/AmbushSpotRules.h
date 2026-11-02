#pragma once

#include "CoreMinimal.h"

/**
 * Where a combat-lab ambush hides (game designer answer 1, 2026-11-01, docs/design/COMBAT_LAB.md "Hiding spots"; UE-only, no Godot
 * reference): at wall ENDS and DOORWAYS / gaps found in the level's walls, plus hand-placed ambush points (level JSON
 * "pressure"."ambush"."points", actors tagged CodexAmbushPoint). UCombatPressureSubsystem keeps the former selector among these
 * candidates (the best concealed one ahead along the squad's heading, CombatPressureRules::ScoreSpot + KindBonus). Pure rules, tested in
 * CodexTactics.Combat.AmbushSpots.*.
 */

/** A wall / obstacle as a 2D oriented box (the level's blocking static meshes). */
struct CODEXTACTICS_API FPressureBox
{
	FVector2D Center = FVector2D::ZeroVector;
	/** Unit axis of the box's local X (its local Y is perpendicular, rotated +90 deg). */
	FVector2D AxisX = FVector2D(1.0, 0.0);
	/** Half sizes along the local X / Y, cm. */
	FVector2D HalfSize = FVector2D::ZeroVector;
	/** Its height, cm (0 = unknown; the bot's fight cover wants a wall at least crouch-high, BotCoverRules). */
	float HeightCm = 0.f;
};

/** What a hiding spot is. */
enum class EAmbushSpotKind : uint8
{
	None,
	/** Behind the end of a wall (either face, a little inside its end). */
	WallEnd,
	/** Beside a doorway / gap: behind a wall end that faces another wall's end across a passable gap. */
	Doorway,
	/** Placed by hand (level JSON "ambush"."points" or an actor tagged CodexAmbushPoint). */
	Authored
};

/** Tuning (level JSON "pressure"."ambush"."spots"). */
struct CODEXTACTICS_API FAmbushSpotParams
{
	/**
	 * A spot stands this far from the wall's face, cm: 80 = the ambusher hugs the wall (game designer decision 3, 2026-11-01 night; was 260).
	 * The hiding spots keep min(wall clearance, standoff - 10) from every wall (HideClearanceCm); the 2.5 m rule stays for PATROL spawns.
	 */
	float WallStandoffCm = 80.f;
	/** ... this far inside the wall's end along it, cm (the wall still covers him from the far side). */
	float EndInsetCm = 100.f;
	/** Shorter boxes (posts, ladder rails) are no walls to hide behind, cm. */
	float MinWallLengthCm = 150.f;
	/** An end this close to another box continues into it (a corner, touching pieces) - no real end, cm. */
	float JoinedGapCm = 30.f;
	/** Two facing, parallel wall ends this far apart are a doorway / gap, cm. */
	float DoorwayMinCm = 80.f;
	float DoorwayMaxCm = 600.f;
	/** Score bonus of an authored point / a doorway spot over a plain wall end (CombatPressureRules::ScoreSpot scale). */
	float AuthoredBonus = 0.5f;
	float DoorwayBonus = 0.25f;
	/** Spots this close are one (the higher kind wins: authored > doorway > wall end), cm. */
	float MergeCm = 100.f;
};

/** One hiding-spot candidate. */
struct CODEXTACTICS_API FAmbushSpot
{
	FVector2D Position = FVector2D::ZeroVector;
	EAmbushSpotKind Kind = EAmbushSpotKind::None;
	/** The box it hides behind (index into the boxes; INDEX_NONE for an authored point). */
	int32 Box = INDEX_NONE;
	/** Unit direction from the wall's face out to the spot (zero for an authored point). */
	FVector2D Normal = FVector2D::ZeroVector;
};

/** A free end of a wall box (not joined to another box). */
struct CODEXTACTICS_API FAmbushWallEnd
{
	int32 Box = INDEX_NONE;
	/** +1 / -1: which end along the box's long axis. */
	int32 Sign = 1;
	/** The centre of the end face. */
	FVector2D Point = FVector2D::ZeroVector;
	/** Unit direction out of the end along the wall. */
	FVector2D Outward = FVector2D(1.0, 0.0);
};

namespace AmbushSpotRules
{
	/** The wall clearance a hiding spot keeps: min(the level's wall clearance, the standoff - 10 cm), cm. */
	CODEXTACTICS_API float HideClearanceCm(float WallClearanceCm, const FAmbushSpotParams& Params);

	/** The box's long axis (unit), its half length and half thickness (a square box: its local X). */
	CODEXTACTICS_API void LongAxis(const FPressureBox& Box, FVector2D& OutAxis, float& OutHalfLength, float& OutHalfThickness);

	/** A box point: Along its long axis, Across it (the axis rotated +90 deg), from its centre. */
	CODEXTACTICS_API FVector2D BoxPoint(const FPressureBox& Box, float Along, float Across);

	/** Every free end of every wall (length >= MinWallLengthCm): an end whose point JoinedGapCm beyond it touches no other box. */
	CODEXTACTICS_API TArray<FAmbushWallEnd> FreeEnds(const TArray<FPressureBox>& Boxes, const FAmbushSpotParams& Params);

	/**
	 * Two free ends of different walls form a doorway / gap: the walls parallel (within 20 deg), each end facing the other (B lies ahead of
	 * A's outward direction and A ahead of B's), the gap between them DoorwayMinCm..DoorwayMaxCm. OutGapCm: the gap.
	 */
	CODEXTACTICS_API bool IsDoorwayPair(const FAmbushWallEnd& A, const FAmbushWallEnd& B, const FAmbushSpotParams& Params, float* OutGapCm = nullptr);

	/** The wall spots: per free end one spot on each face (EndInsetCm inside the end, WallStandoffCm off the face); Doorway beside a gap. */
	CODEXTACTICS_API TArray<FAmbushSpot> WallSpots(const TArray<FPressureBox>& Boxes, const FAmbushSpotParams& Params);

	/** Authored points + wall spots, merged (spots within MergeCm are one; the higher kind wins). */
	CODEXTACTICS_API TArray<FAmbushSpot> Candidates(const TArray<FPressureBox>& Boxes, const TArray<FVector2D>& Authored, const FAmbushSpotParams& Params);

	/** The score bonus of a kind (authored / doorway / wall end 0). */
	CODEXTACTICS_API float KindBonus(EAmbushSpotKind Kind, const FAmbushSpotParams& Params);

	/** "wall end" / "doorway" / "authored" / "none" (logs). */
	CODEXTACTICS_API const TCHAR* KindName(EAmbushSpotKind Kind);
}
