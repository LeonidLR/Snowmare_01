#pragma once

#include "CoreMinimal.h"
#include "Combat/AmbushSpotRules.h"

/**
 * The playtest bot takes COVER AT A WALL first in a fight (game designer decision 5, 2026-11-01 night, docs/design/COMBAT_LAB.md section 7.2;
 * UE-only, no Godot reference): on contact the bot picks the nearest suitable wall that stands between the squad and the threat and clicks
 * its near face (the player's own group cover order = the formation wall layout), fights from there and walks on when the fight is over
 * (or after HoldSeconds, firing on the move). Pure rules, tested in CodexTactics.Bot.Cover.*; the walls are the level's obstacle boxes
 * (UCombatPressureSubsystem::GetObstacleBoxes).
 */

/** Tuning (level JSON "bot_route"."fight_cover"). */
struct CODEXTACTICS_API FBotCoverParams
{
	/** Take cover at a wall on contact ("enabled"); off = the old halt in place (fight_hold_sec). */
	bool bEnabled = true;
	/** The wall face at most this far from the leader, cm ("search_cm"). */
	float SearchCm = 1500.f;
	/** At the wall at most this long in a fight, then the route goes on (firing on the move), s ("hold_sec"). */
	float HoldSeconds = 45.f;
	/** A wall at least this long (room for the squad's layout), cm ("min_wall_cm"). */
	float MinWallLengthCm = 200.f;
	/** ... and at least this tall (crouched cover; a box of unknown height counts), cm ("min_height_cm"). */
	float MinHeightCm = 120.f;
	/** The threat at least this far from the face (no cover in the enemy's lap), cm ("min_threat_cm"). */
	float MinThreatCm = 400.f;
	/** The wall faces the threat: cos(into the wall, face -> threat) at least this ("min_facing_cos"). */
	float MinFacingCos = 0.5f;
	/** The clicked face point keeps this far inside the wall's ends, cm. */
	float EndInsetCm = 60.f;
};

/** The chosen cover face. */
struct CODEXTACTICS_API FBotCoverChoice
{
	/** The obstacle box (index into the boxes). */
	int32 Box = INDEX_NONE;
	/** The point on the near face the bot clicks (the leader's projection onto it, inside the ends). */
	FVector2D FacePoint = FVector2D::ZeroVector;
	/** Unit normal of that face, out towards the squad's side (away from the threat). */
	FVector2D Normal = FVector2D::ZeroVector;
	/** The walk from the leader to the face point (round the wall's nearer end when he stands on its far side), cm. */
	float DistanceCm = 0.f;
	float FacingCos = 0.f;
	float Score = 0.f;
};

namespace BotCoverRules
{
	/**
	 * One face of Box as cover against Threat for a squad led from Leader: FaceSign +1 / -1 picks the face (the long axis rotated +90 deg
	 * times the sign). Unset when the box is too short / too low, the threat is on this face's side, the face does not face the threat
	 * (MinFacingCos), the threat is too close or the face too far.
	 */
	CODEXTACTICS_API TOptional<FBotCoverChoice> EvaluateFace(const FPressureBox& Box, int32 BoxIndex, int32 FaceSign, const FVector2D& Leader,
		const FVector2D& Threat, const FBotCoverParams& Params);

	/** The best face of all the boxes (highest score: facing the threat squarely, near the leader); unset when none qualifies. */
	CODEXTACTICS_API TOptional<FBotCoverChoice> PickWall(const TArray<FPressureBox>& Boxes, const FVector2D& Leader, const FVector2D& Threat,
		const FBotCoverParams& Params);

	/** The score of a face: its facing cosine minus 0.8 x its distance as a share of the search radius. */
	CODEXTACTICS_API float Score(float DistanceCm, float FacingCos, const FBotCoverParams& Params);
}
