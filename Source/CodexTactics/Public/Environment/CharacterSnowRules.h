#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"

/**
 * Pure logic of the snow on characters (UE-only, no Godot reference; docs/systems/environment-weather.md section 15, user decisions
 * 2026-10-23). UCharacterSnowSubsystem owns the per-character pockets (FDirectionalSnow in the BODY mesh's LOCAL axes, so the snow stays
 * on the back when the character turns) and pushes them to the body mesh as Custom Primitive Data, read by the snow layer of the
 * character master materials (M_CharacterMaster*, Scripts/Editor/add_character_snow_layer.py).
 */
namespace CharacterSnowRules
{
	/**
	 * First Custom Primitive Data index of the five pockets on the body mesh: Top, +X, -X, +Y, -Y at FirstCustomDataIndex + 0..4 (8..12).
	 * Verified free on every body (no C++ / Blueprint / material of the project uses Custom Primitive Data on a character, 2026-10-23).
	 */
	constexpr int32 FirstCustomDataIndex = 8;

	/** Push interval of the subsystem (~4 Hz) and the smallest pocket change that is worth a push. */
	constexpr float PushIntervalSeconds = 0.25f;
	constexpr float PushThreshold = 0.01f;

	/** What one character experiences now (filled by the subsystem; pure input). */
	struct FCharacterSnowConditions
	{
		/** The blended snow cover rate (EnvironmentRules::GetBlendedSnowCoverRate; 0 when it does not snow). */
		float SnowCoverRate = 0.f;
		/** Horizontal unit wind TRAVEL direction in the body's local frame (ToLocalWind). */
		FVector2D LocalWind = FVector2D::ZeroVector;
		float WindStrength = 0.f;
		/** The objects' sun melt per second now (EnvironmentRules::GetObjectSnowMeltRate). */
		float ObjectMeltRate = 0.f;
		/** Near an active heat source, inside a Closed / Shelter zone or (an operative) hidden in a vehicle: no growth, the snow clears fast. */
		bool bSheltered = false;
	};

	/** The world wind travel direction in the body's local frame (BodyRotation = the body mesh's world rotation), horizontal, unit (or zero). */
	CODEXTACTICS_API FVector2D ToLocalWind(const FVector& WorldWindDirection, const FQuat& BodyRotation);

	/**
	 * The step inputs of a character: outdoors GrowRate = SnowCoverRate x CharacterSnowFillScale, sides by the local wind
	 * (ObjectSnowSideRateScale), melt = ObjectMeltRate x CharacterSnowMeltScale with floor 0; sheltered: no growth / melt, ClearRate =
	 * CharacterSnowClearRate.
	 */
	CODEXTACTICS_API FDirectionalSnowInputs MakeInputs(const FEnvironmentConfig& Config, const FCharacterSnowConditions& Conditions);

	/** Pockets of a frame (World, e.g. the objects' world-axis pockets) re-expressed in a body frame rotated by BodyRotation: the top is kept,
	 * each local side takes sum_j World_j x max(0, dot(local side in world, world side j))^2 (exact for 90-degree turns). */
	CODEXTACTICS_API FDirectionalSnow RotateToLocal(const FDirectionalSnow& World, const FQuat& BodyRotation);

	/**
	 * Cover of a character that appears now (an enemy spawned mid-storm): RotateToLocal(ObjectSnow) x CharacterSnowSpawnShare x SnowingAmount
	 * (0..1: the Snowfall + Blizzard weather amounts). Nothing when it does not snow.
	 */
	CODEXTACTICS_API FDirectionalSnow MakeSpawnCover(const FEnvironmentConfig& Config, const FDirectionalSnow& ObjectSnow, float SnowingAmount,
		const FQuat& BodyRotation);

	/** Largest per-pocket difference. */
	CODEXTACTICS_API float MaxDifference(const FDirectionalSnow& A, const FDirectionalSnow& B);

	/**
	 * The CPD push throttle: push when at least IntervalSeconds passed since the last push AND some pocket moved by more than Threshold
	 * from the pushed values; a pocket that reached exactly 0 from above is always pushed (identical-at-0 look) once the interval passed.
	 */
	CODEXTACTICS_API bool ShouldPush(const FDirectionalSnow& Pushed, const FDirectionalSnow& Current, float SecondsSincePush,
		float IntervalSeconds = PushIntervalSeconds, float Threshold = PushThreshold);
}
