#pragma once

#include "CoreMinimal.h"

/**
 * Tripwire mine «Растяжка» МУВ-3 + 2 Ф-1 (Sprint 09, TANDEM «SPRINT 09 DIRECTIVE: Military Tripwire Mine»; UE-only, no
 * Godot reference). Pure rules, tested in CodexTactics.Interactables.Tripwire.*; ATripwireActor and the placement in
 * URelocationSubsystem use them.
 */
namespace TripwireRules
{
	/** Grenades the squad spends on one tripwire. */
	inline constexpr int32 GrenadeCost = 2;
	/** Wire span between the anchors, cm. */
	inline constexpr float MinSpanCm = 100.f;
	inline constexpr float MaxSpanCm = 500.f;
	/** The wire runs this high above the ground, cm (a prone body, 25 cm, passes under it). */
	inline constexpr float WireHeightCm = 30.f;
	/** Pin pulled («ЩЁЛК!») -> the grenades go off after this, s. */
	inline constexpr float FuseDelaySeconds = 0.25f;
	/** The paired Ф-1 blast at the wire: damage at the centre, radius, the squad takes 75 % (falloff by ApplyBlast). */
	inline constexpr float BlastDamage = 140.f;
	inline constexpr float BlastRadiusCm = 450.f;
	inline constexpr float SquadDamageShare = 0.75f;
	/** Full armour penetration (100 % shred) and a stagger knockdown, s. */
	inline constexpr float ArmorPenetration = 1.f;
	inline constexpr float StaggerSeconds = 1.5f;
	/** Rigging the wire, s; disarming it (medic-sapper only), s. */
	inline constexpr float RigSeconds = 2.f;
	inline constexpr float DisarmSeconds = 3.f;
	/** A disarm fumbles (one grenade lost) this often. */
	inline constexpr float DisarmFumbleChance = 0.1f;
	/** After rigging the wire arms this late (the rigger steps away), s. */
	inline constexpr float ArmingSeconds = 1.5f;

	/** Span between 1 and 5 m. */
	CODEXTACTICS_API bool IsSpanValid(float SpanCm);

	/** A body whose top (profile height above the feet) is above the wire trips it: prone (25 cm) crawls under. */
	CODEXTACTICS_API bool TripsWire(float BodyHeightCm);

	/** Planar distance from a point to the wire segment A-B (beyond the ends: to the nearest end). */
	CODEXTACTICS_API float DistanceToWire(const FVector2D& Point, const FVector2D& A, const FVector2D& B);

	/** A body of Radius at Point crosses the wire (touches the segment, not just its end region beyond the anchors). */
	CODEXTACTICS_API bool TouchesWire(const FVector2D& Point, float RadiusCm, const FVector2D& A, const FVector2D& B);

	/** Grenades a disarm returns: 2, or 1 on a fumble. */
	CODEXTACTICS_API int32 GrenadesReturned(bool bFumble);
}
