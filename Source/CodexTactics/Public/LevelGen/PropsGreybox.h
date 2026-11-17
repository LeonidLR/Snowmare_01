#pragma once

#include "CoreMinimal.h"

class FJsonObject;

/**
 * The runtime greybox look of a levelgen prop (G003, docs/design/levelgen/PROPS_LAYERS.md 3.2 + the designer's answers 6.1 #1 / #4):
 * the props of a Training Ground autogen file had no mesh at all (the class_map Blueprints do not exist yet), so the live-map guard
 * destroyed every crate / barrel / turret and the mines / loot piles were invisible. Pure: shape, size and colour of a variant from
 * Content/Data/Balance/props_balance.json ("kinds" sizes, "crate_colors", "greybox"). No world, no UObjects.
 * Godot reference: none (UE-only level authoring). Tests: CodexTactics.LevelGen.Props.Greybox.*.
 */
enum class EPropGreyboxShape : uint8
{
	Box,
	Cylinder
};

/** What ACodexPropActor::ApplyGreybox builds: one body standing on the actor origin (+ an optional muzzle / tripwire). */
struct FPropGreyboxSpec
{
	EPropGreyboxShape Shape = EPropGreyboxShape::Box;
	/** Full size of the body in cm (x, y along the actor's yaw; z up from the ground). */
	FVector SizeCm = FVector(100.0, 100.0, 100.0);
	/** Linear RGB. */
	FLinearColor Color = FLinearColor(0.5f, 0.5f, 0.5f);
	/** The body blocks pawns / shots (nav-affecting kinds); a mine marker / loot pile does not. */
	bool bBlocks = true;
	/** Turret: a muzzle bar along +x on top of the body (full size in cm; zero = none). */
	FVector MuzzleCm = FVector::ZeroVector;
	/** Tripwire mine: a wire across the doorway along the actor's y (length = the mine's width_cm; zero = none). */
	float WireLengthCm = 0.f;
	/** Height of the wire above the ground (cm). */
	float WireHeightCm = 0.f;
	/** Thickness of the wire (cm). */
	float WireThicknessCm = 0.f;
};

namespace PropsGreybox
{
	/**
	 * The look of one prop. Variant = the object's variant (crate_s, barrel_explosive, turret_mg, mine_tripwire, loot_pile, ...),
	 * Kind = its kind from the balance file (crate_s -> crate_s; mine_* -> "mine"), bDangerous = a mined crate (red, crate_colors.dangerous),
	 * Trigger / WidthCm = a mine's trigger and doorway width. Missing data falls back to a grey 100 cm box (never fails).
	 */
	CODEXTACTICS_API FPropGreyboxSpec Resolve(const FJsonObject* Balance, const FString& Variant, bool bDangerous = false,
		const FString& Trigger = FString(), float WidthCm = 0.f);

	/** The balance "kinds" key of a variant: the variant itself when listed, else "mine" for mine_*, else the variant. */
	CODEXTACTICS_API FString KindKeyFor(const FJsonObject* Balance, const FString& Variant);
}
