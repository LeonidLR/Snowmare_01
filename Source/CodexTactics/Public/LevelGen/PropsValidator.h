#pragma once

#include "CoreMinimal.h"
#include "LevelGen/LevelPropsTypes.h"
#include "LevelGen/PropsLayoutView.h"

class FJsonObject;

/**
 * Static validation issue for a props object (docs/design/levelgen/PROPS_LAYERS.md sections 2.4, 2.5, task C5).
 * Records failure reason and whether the object was dropped or flagged as LOCKED/HAND INVALID.
 */
struct CODEXTACTICS_API FPropsValidationIssue
{
	/** Prop object ID */
	FString ObjectId;

	/** Layer ID */
	FString LayerId;

	/** True if object was locked */
	bool bLocked = false;

	/** True if object was added or edited by hand (origin == "hand", pinned, or bHand) */
	bool bHand = false;

	/** Description of the validation failure */
	FString Reason;

	/** True if the object was dropped (not safe to spawn) */
	bool bDropped = true;
};

/**
 * Result of static props validation (PROPS_LAYERS.md 2.4, 2.5, task C5).
 */
struct CODEXTACTICS_API FPropsValidationResult
{
	/** Total objects evaluated */
	int32 TotalObjects = 0;

	/** Valid resolved objects that passed all static checks and are safe to spawn */
	TArray<FLevelPropsResolvedObject> ValidObjects;

	/** Objects dropped due to validation failures */
	TArray<FLevelPropsResolvedObject> DroppedObjects;

	/** All validation issues recorded */
	TArray<FPropsValidationIssue> Issues;

	/** True if any validation issue was found */
	bool HasErrors() const { return Issues.Num() > 0; }

	/** True if any locked or hand objects failed validation (flagged as LOCKED/HAND INVALID) */
	bool HasLockedOrHandInvalid() const
	{
		for (const FPropsValidationIssue& Issue : Issues)
		{
			if (Issue.bLocked || Issue.bHand)
			{
				return true;
			}
		}
		return false;
	}
};

/**
 * Pure C++ static validator for levelgen props (docs/design/levelgen/PROPS_LAYERS.md 2.4, 2.5, task C5).
 * Validates props against layout geometry, doorways, ladders/stairs, keep-outs, and attach dependencies.
 * Drops bad objects with id logs; flags locked/hand ones as LOCKED/HAND INVALID.
 * No world, no actors. Godot reference: none (UE-only level authoring).
 */
class CODEXTACTICS_API FPropsValidator
{
public:
	/**
	 * Validates all props of a file after overrides are applied.
	 * Calls LevelPropsFile::ApplyOverrides, validates against LayoutView, drops bad objects,
	 * logs each drop, and flags locked/hand failures as LOCKED/HAND INVALID.
	 *
	 * @param LayoutView Parsed layout view
	 * @param File Parsed props file
	 * @param OutResult Holds ValidObjects (safe to spawn), DroppedObjects, and Issues
	 * @param Balance Optional props_balance.json for variant footprint dimensions
	 * @return true if all objects passed validation (0 dropped), false if any were dropped
	 */
	static bool Validate(
		const FPropsLayoutView& LayoutView,
		const FLevelPropsFile& File,
		FPropsValidationResult& OutResult,
		const FJsonObject* Balance = nullptr);

	/**
	 * Validates pre-resolved props (FLevelPropsResolved).
	 */
	static bool ValidateResolved(
		const FPropsLayoutView& LayoutView,
		const FLevelPropsResolved& Resolved,
		FPropsValidationResult& OutResult,
		const FJsonObject* Balance = nullptr);

	/**
	 * Validates a list of resolved props objects directly with optional keep-outs.
	 */
	static bool ValidateObjects(
		const FPropsLayoutView& LayoutView,
		const TArray<FLevelPropsResolvedObject>& Objects,
		const TArray<FLevelPropsKeepOut>& KeepOuts,
		FPropsValidationResult& OutResult,
		const FJsonObject* Balance = nullptr);

	/**
	 * Checks if an object intrudes into any doorway clear corridor (90 cm wide, 150 cm deep on both sides),
	 * except the doorway's own mine (Object.Door == Doorway.Id).
	 *
	 * @param LayoutView Layout view
	 * @param Object Prop object
	 * @param HalfSize Half footprint size of the prop in cm
	 * @param OutBlockingDoorwayId Optional output for ID of the blocking doorway
	 * @return true if intruding into a doorway clear corridor
	 */
	static bool IsInDoorwayCorridor(
		const FPropsLayoutView& LayoutView,
		const FLevelPropsObject& Object,
		int32 HalfSize,
		FString* OutBlockingDoorwayId = nullptr);

	/**
	 * Checks if an object intrudes into any ladder foot/top (100 cm disk) or stair end (100 cm disk).
	 *
	 * @param LayoutView Layout view
	 * @param Object Prop object
	 * @param HalfSize Half footprint size of the prop in cm
	 * @param OutBlockingId Optional output for ID of the blocking ladder or stair
	 * @return true if intruding into a ladder/stair keep-out disk
	 */
	static bool IsInLadderOrStairDisk(
		const FPropsLayoutView& LayoutView,
		const FLevelPropsObject& Object,
		int32 HalfSize,
		FString* OutBlockingId = nullptr);

	/**
	 * Resolves half footprint size in cm for a variant from props_balance.json kinds or built-in defaults.
	 */
	static int32 GetVariantHalfSize(const FString& Variant, const FJsonObject* Balance = nullptr);

	/**
	 * The power-network checks (TURRET_POWER.md phase 1; the Training Ground's validatePowerLinks, same reasons and order): a generator's
	 * start state, its link count (max = the largest presets.power_default.links.turrets_per_generator key), every link a turret of the
	 * map within the cable reach (links.cable_cm[1]), no turret on two generators, every turret's "generator" listing it back, an unlinked
	 * turret on a map with generators ("no power line"), and with SpawnPos a running hostile network away from the spawn
	 * (start_state.no_hostile_within_spawn_cm). Warnings only (bDropped false): a broken link leaves the turret dark.
	 * KindOfLayer = layer id -> layer kind.
	 */
	static TArray<FPropsValidationIssue> ValidatePowerLinks(const TArray<FLevelPropsResolvedObject>& Objects, const TMap<FString, FString>& KindOfLayer,
		const FJsonObject* Balance = nullptr, const FIntPoint* SpawnPos = nullptr);
};
