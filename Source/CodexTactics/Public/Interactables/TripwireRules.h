#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableRules.h"

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

	// --- PMC (hostile, level-placed) tripwires (Demo 01 G3, user decisions 2026-10-11): hidden until spotted with the
	// mine spotting rule, disarmed by ANY operative with the mine defusal formula (role / cold / failures; crouched, as a mine),
	// failure = the mine failure rule (warning, slip, detonation risk), triggered only by operatives. These are the
	// defaults of the editable APMCTripwireActor / ATripwireActor properties (per instance / Blueprint in Unreal). ---

	/** Spotting range of an operative (the mine rule: 4.5 m, Medic-Sapper +1.5 m), cm. */
	inline constexpr float SpotRangeCm = 450.f;
	inline constexpr float SapperSpotBonusCm = 150.f;
	/** Disarm duration of a PMC wire by role, s (the squad's own wire: DisarmSeconds, Medic-Sapper only). */
	inline constexpr float HostileDisarmSecondsSapper = 3.f;
	inline constexpr float HostileDisarmSecondsEngineer = 4.f;
	inline constexpr float HostileDisarmSecondsOther = 5.f;
	/** Turn-based: one disarm attempt costs this many AP (as setting a mine, TurnBasedCombatSubsystem::GetDeployActionPoints). */
	inline constexpr int32 TurnBasedDisarmAP = 2;
	/** Reach of a turn-based disarm: the active operative stands within this of the wire, cm (about one diagonal cell). */
	inline constexpr float TurnBasedDisarmReachCm = 220.f;

	/** A wire reacts to this body: the squad's own wire to everyone (friendly fire), a PMC wire only to operatives. */
	CODEXTACTICS_API bool TriggersOn(bool bHostileWire, bool bIsOperative);

	/** Who may disarm: the squad's wire only the Medic-Sapper, a PMC wire anyone (role chance). */
	CODEXTACTICS_API bool CanDisarm(bool bHostileWire, EOperativeRole Role);

	/** Disarm duration, s: squad wire DisarmSeconds; PMC wire Medic-Sapper 3 / Engineer 4 / others 5. */
	CODEXTACTICS_API float GetDisarmSeconds(bool bHostileWire, EOperativeRole Role);

	/** An operative at DistanceCm from the wire spots a hidden PMC wire (DeployableRules::GetSpotRangeCm, the mine rule). */
	CODEXTACTICS_API bool CanSpot(float DistanceCm, EOperativeRole Role, float BaseRangeCm = SpotRangeCm, float SapperBonusCm = SapperSpotBonusCm);

	/**
	 * Odds of a PMC wire disarm: the mine formula (DeployableRules::CalculateDefusal) in the mine's work stance - crouched
	 * (+10) - with the role base, luck, cold penalty (-15 / -30 / -50 above 25 / 50 / 75 %) and the failed attempts.
	 * Default luck: Medic-Sapper 60 + 10 + 35 / 2 = 87.5 %, Engineer 70 %, Commander 57.5 % (warm hands).
	 */
	CODEXTACTICS_API FDefusalChance GetHostileDisarmChance(EOperativeRole Role, float Luck, float ColdLevel, int32 FailedAttempts, bool bWarned);
}
