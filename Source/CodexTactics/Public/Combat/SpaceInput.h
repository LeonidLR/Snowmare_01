#pragma once

#include "CoreMinimal.h"

/** What a Space key press resolved to. */
enum class ESpaceInputAction : uint8
{
	None,
	/** Released before the hold limit: toggle tactical pause (or the command bar). */
	Tap,
	/** Held for the hold limit: enter / leave turn-based combat. Fires once per press. */
	Hold
};

/**
 * Space key tap / hold detection on real time.
 * Godot reference: main.gd KEY_SPACE handling (`is_space_pressed_down`, `space_hold_time`,
 * `get_hold_space_duration()` = balance.tres tactical_hold_space_duration): any release before the hold limit
 * is a tap; reaching the limit while held fires the hold action once and the later release does nothing.
 */
class CODEXTACTICS_API FSpaceInputTracker
{
public:
	/** Key went down. */
	void Press();

	/** Key went up; returns Tap if the hold limit was not reached. */
	ESpaceInputAction Release();

	/** Advances real time while held; returns Hold once when the limit is reached. */
	ESpaceInputAction Tick(float RealDeltaSeconds, float HoldDuration);

	bool IsPressed() const { return bPressed; }

	/** Seconds the key has been held in the current press. */
	float GetHeldTime() const { return HeldTime; }

private:
	bool bPressed = false;
	bool bHoldFired = false;
	float HeldTime = 0.f;
};
