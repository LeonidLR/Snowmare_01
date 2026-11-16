// UE-only (no Godot reference): the arrival on a cover slot when a squadmate stands on it (075, CoverSmoke 2026-11-16).
#pragma once

#include "CoreMinimal.h"

/**
 * The cover arrival puts the capsule on the slot with a swept move (EnterCover; the mesh glides the rest). A squadmate standing there
 * stopped that sweep short and the short spot stayed for good: CoverSmoke 2026-11-16, the formation's corner-watch spot of the Engineer
 * (-380, -6261) lay 11 cm from the leader's corner stand-off slot (-391, -6259) - the sweep stopped at 46 % / 17 %, 49 / 54 / 72 cm from the
 * wall end instead of 28 (the corner snap then sees the slot "reached" and never walks on); the same for a wall entry, 74 cm off its slot.
 * Rule: a sweep blocked by a squadmate (same team) who is not in cover himself, short of the slot by more than PassSquadmateMinGapCm, is
 * repeated with that squadmate ignored - the slot is his; the walking squadmate is pushed off by the movement (his next step resolves the
 * overlap) and the formation re-plans its spots once the leader stands at the corner. Static geometry, enemies and a squadmate in cover
 * still stop the sweep (no change). Tunables: Content/Data/Tactics/cover_arrival.json (CoverArrivalRules::GetConfig).
 */
namespace CoverArrivalRules
{
	struct CODEXTACTICS_API FCoverArrivalConfig
	{
		/** A blocked sweep this far or less short of the slot is fine as it is, cm. */
		float PassSquadmateMinGapCm = 2.f;
	};

	/** The config read once from cover_arrival.json (the defaults above when the file is missing). */
	CODEXTACTICS_API const FCoverArrivalConfig& GetConfig();

	/** Parses a cover_arrival.json text ({ "params": { "pass_squadmate_min_gap_cm": N } }); false when it is no JSON object. */
	CODEXTACTICS_API bool LoadConfigFromJson(const FString& JsonString, FCoverArrivalConfig& OutConfig);

	/**
	 * Repeat the blocked arrival sweep with the blocker ignored: it was blocked (bSweepBlocked) by a squadmate (bBlockerIsSquadmate) who is
	 * not in cover (bBlockerInCover) and it stopped more than PassSquadmateMinGapCm short of the slot (GapCm).
	 */
	CODEXTACTICS_API bool ShouldPassSquadmate(bool bSweepBlocked, bool bBlockerIsSquadmate, bool bBlockerInCover, float GapCm, const FCoverArrivalConfig& Config);
}
