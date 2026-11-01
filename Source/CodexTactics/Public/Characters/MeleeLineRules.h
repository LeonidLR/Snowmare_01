#pragma once

#include "CoreMinimal.h"

/**
 * What a melee line / pounce sweep met (game designer bug 06, 2026-11-01: the Cutter hit and leapt through walls). The world trace
 * classifies every hit in order; MeleeLineRules decides.
 */
enum class EMeleeLineHit : uint8
{
	/** The target itself: the line reaches it. */
	Target,
	/** Another character: a blow / leap is not stopped by a body (it passes beside it). */
	Pawn,
	/** Something that does not block bodies (a click proxy, a trigger): ignored. */
	NonSolid,
	/** A wall, a building, a tower, a vehicle hull - anything that blocks a body: the line is cut. */
	Solid
};

/** Why a Cutter pounce is started or not (AEnemyCharacter::TickBehavior). */
enum class ECutterLeapCheck : uint8
{
	Leap,
	/** Closer than JumpMinDistance or further than JumpMaxDistance. */
	OutOfRange,
	/** More than MaxLeapHeightCm of height difference (Godot 2.5 m). */
	TooHigh,
	/** A wall between it and the target (the melee line, MeleeLineRules::IsLineClear). */
	WallInLine,
	/** The flight arc (its capsule swept along LeapArc) runs into something solid. */
	PathBlocked
};

/**
 * Melee blows and the Cutter's pounce never pass a wall (game designer bug 06, 2026-11-01; UE-only rule - Godot's can_melee_attack is a
 * distance check only). A blow lands only with a clear line between the attacker and the target at LineHeightCm over each one's feet
 * (over low cover - CoverTraceRules::LowCoverMaxCm 130 cm, a barricade - and under a full-height wall, HighCoverMinCm 180 cm); a pounce
 * starts only with that line clear and its whole flight arc free of solid things; its landing hurts only those it has a clear line to.
 * Every enemy blow (Hound, Frostbitten, Brute, Cutter, zombies: AEnemyCharacter::AttackTarget) goes through the same rule.
 */
namespace MeleeLineRules
{
	/** Height of the line over each end's feet, cm. */
	constexpr float LineHeightCm = 150.f;
	/** Godot enemy_cutter.gd: at most 2.5 m of height difference for a pounce. */
	constexpr float MaxLeapHeightCm = 250.f;
	/** Segments of the swept flight arc. */
	constexpr int32 LeapArcSegments = 8;
	/** The pounce sweep's capsule is lifted / shortened this much at the bottom (it starts and lands on the floor), cm. */
	constexpr float LeapSweepFloorClearanceCm = 30.f;

	/** The line point over Feet. */
	CODEXTACTICS_API FVector LinePoint(const FVector& Feet);

	/** One hit, as the trace met it (bBlocksBodies: the component blocks the Pawn channel). */
	CODEXTACTICS_API EMeleeLineHit ClassifyHit(bool bIsTarget, bool bIsPawn, bool bBlocksBodies);

	/** The hits in the order the line met them: clear unless a Solid one comes before the target (an empty list is clear). */
	CODEXTACTICS_API bool IsLineClear(TConstArrayView<EMeleeLineHit> HitsInOrder);

	/** A blow lands: within reach (EnemyAIRules::CanMelee) and a clear line. */
	CODEXTACTICS_API bool MayStrike(bool bInReach, bool bLineClear);

	/** The pounce decision: range, height, then the line, then the arc (the caller traces only once the cheap checks pass). */
	CODEXTACTICS_API ECutterLeapCheck EvaluateLeap(float DistanceCm, float HeightDifferenceCm, float MinDistanceCm, float MaxDistanceCm,
		bool bLineClear, bool bArcClear);

	CODEXTACTICS_API const TCHAR* LeapCheckName(ECutterLeapCheck Check);

	/**
	 * Godot _launch_jump_flight: the launch velocity of a ballistic flight from Start landing at Landing after FlightSeconds under Gravity
	 * (cm/s^2, positive), its horizontal part clamped to MaxHorizontalCm.
	 */
	CODEXTACTICS_API FVector LeapVelocity(const FVector& Start, const FVector& Landing, float FlightSeconds, float Gravity, float MaxHorizontalCm);

	/** Where the flight is Seconds after Start. */
	CODEXTACTICS_API FVector LeapArcPoint(const FVector& Start, const FVector& Velocity, float Gravity, float Seconds);

	/** Segments + 1 points of the flight from Start (its first) to its end (its last) - the path the sweep checks. */
	CODEXTACTICS_API TArray<FVector> LeapArc(const FVector& Start, const FVector& Landing, float FlightSeconds, float Gravity, float MaxHorizontalCm,
		int32 Segments = LeapArcSegments);

	/** The landing hurts an operative: within the radius, at most 1.5 m of height difference (Godot), and a clear line to him. */
	CODEXTACTICS_API bool PounceReaches(float DistanceCm, float HeightDifferenceCm, float RadiusCm, bool bLineClear);
}
