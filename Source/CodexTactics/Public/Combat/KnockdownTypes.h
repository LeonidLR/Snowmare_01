#pragma once

#include "CoreMinimal.h"
#include "KnockdownTypes.generated.h"

/**
 * Knockdown & Recovery types (Sprint 14, TANDEM request #12; UE-only, no Godot reference — Gemini spec "Knockdown &
 * Recovery System", user-approved 2026-10-08).
 */

/** Phase of a knockdown: the fall clip, lying on the ground (recovery bar), the get-up clip. */
UENUM(BlueprintType)
enum class EKnockdownPhase : uint8
{
	None UMETA(DisplayName = "None"),
	Falling UMETA(DisplayName = "Falling"),
	Downed UMETA(DisplayName = "Downed"),
	GettingUp UMETA(DisplayName = "Getting Up")
};

/**
 * Which way the unit falls. A blow from the front throws him on his back (Knocked_Back -> Revive_Back / Death_Back),
 * a blow from behind on his face (Knocked_Front -> Revive_Front / Death_Front).
 */
UENUM(BlueprintType)
enum class EKnockdownDirection : uint8
{
	None UMETA(DisplayName = "None"),
	Back UMETA(DisplayName = "On The Back"),
	Front UMETA(DisplayName = "On The Face")
};

/** What knocked him down (decides poise and the feed line). */
UENUM(BlueprintType)
enum class EKnockdownCause : uint8
{
	None UMETA(DisplayName = "None"),
	/** Hound / Cutter leap attack landing on him. */
	Pounce UMETA(DisplayName = "Pounce"),
	/** Frost Brute heavy swing or charge slam. */
	HeavyMelee UMETA(DisplayName = "Heavy Melee"),
	/** F-1 grenade / fuel barrel blast close by. */
	Explosion UMETA(DisplayName = "Explosion"),
	/** A single heavy hit (>= the damage threshold), e.g. a point-blank shotgun blast. */
	HeavyHit UMETA(DisplayName = "Heavy Hit")
};
