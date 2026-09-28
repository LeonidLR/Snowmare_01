#pragma once

#include "CoreMinimal.h"

/** Outcome of trying to light a barrel. */
enum class EBarrelIgniteResult : uint8
{
	Ignited,
	AlreadyBurning,
	BurntOut,
	NoMatches
};

/**
 * Burn state of a fuel barrel: lit once with a match, burns for a fixed time, then stays charred.
 * Godot reference: Scenes/movements/interactable.gd `_interact_barrel`, `_process`, `extinguish_barrel`.
 */
struct CODEXTACTICS_API FBarrelBurnState
{
	bool bBurning = false;
	/** Godot has_been_burned: a barrel burns only once. */
	bool bBurnt = false;
	float TimeLeft = 0.f;

	/** Lights the barrel with one of Matches (decremented on success). */
	EBarrelIgniteResult TryIgnite(int32& Matches, float BurnDuration);

	/** Advances the fire; returns true when the barrel just went out. */
	bool Tick(float DeltaSeconds);

	/** Light strength 0..1 without flicker: fades over the last FadeSeconds (Godot: 7 s). */
	float GetFireStrength(float FadeSeconds) const;
};
