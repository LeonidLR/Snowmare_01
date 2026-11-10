#pragma once

#include "CoreMinimal.h"
#include "LevelGen/LevelPropsTypes.h"

/**
 * Pure reader / writer of Content/Data/LevelGen/props/<map>.props.json (codex.levelgen_props/1, docs/design/levelgen/PROPS_LAYERS.md 1.2,
 * task C1 of section 5). No world, no actors. Godot reference: none (UE-only). Tests: CodexTactics.LevelGen.Props.*.
 *
 * Number rules (shared with the TS side): cm / degrees / revision / seed are integers. A fractional cm / degree value in a hand-edited
 * file is rounded half up (FMath::RoundToInt == JS Math.round, also for negatives: -2.5 -> -2). A seed must be an integer in [0, 2^32).
 * A JSON null on an optional field reads as absent; a known field of the wrong type is an error.
 * Limit: JSON keys that differ only by case are not supported (UE's FJsonObject keeps one of them); the schema's keys are lower case.
 */
namespace LevelPropsFile
{
	/** Schema family and the one supported major version. */
	constexpr const TCHAR* SchemaFamily = TEXT("codex.levelgen_props");
	constexpr const TCHAR* SchemaId = TEXT("codex.levelgen_props/1");

	/** True for barrels / crates / turrets / mines / loot. */
	CODEXTACTICS_API bool IsValidKind(const FString& Kind);

	/**
	 * Parses a props file. False + Errors on invalid JSON, a foreign schema, an unknown major version (refused, logged, nothing read),
	 * or a bad layer / object / override (missing id, bad kind, missing seed, bad pos, wrong field type, duplicate id). Never crashes;
	 * Out holds every part that was valid (a layer with a bad object keeps its good objects). Unknown keys are kept. Every error is
	 * logged (LogCodexTactics, Warning).
	 */
	CODEXTACTICS_API bool Parse(const FString& Json, FLevelPropsFile& Out, TArray<FString>& Errors);

	/**
	 * Canonical JSON: fixed key order for known keys, keys sorted ordinally (case-sensitive, UTF-16 code units, like JS sort) for opaque
	 * blocks (rules, report, set fields) and unknown keys, integer cm, always schema /1, tab indent, trailing newline.
	 * Write(Parse(Write(x))) == Write(x).
	 */
	CODEXTACTICS_API FString Write(const FLevelPropsFile& File);

	/**
	 * The resolved object list: layer objects (enabled layers only) with the overrides applied in file order.
	 *  - move: pos / walk_layer / z_offset / yaw; the object is pinned.
	 *  - delete: the object is hidden; a generated id also leaves a keep-out disk (position = the override pos, else the object's;
	 *    radius = its layer's rules.min_spacing_cm), also when the id is no longer in the layer after a re-roll.
	 *  - add: appended with origin "hand", pinned; its layer comes from the id ("hand_<layer>_<n>").
	 *  - set: fields changed (a null value clears an optional field; "id" is immutable), pinned.
	 * Overrides that target a disabled layer are skipped quietly; any other problem goes to OutWarnings (never throws).
	 */
	CODEXTACTICS_API FLevelPropsResolved ApplyOverrides(const FLevelPropsFile& File, TArray<FString>* OutWarnings = nullptr);
}
