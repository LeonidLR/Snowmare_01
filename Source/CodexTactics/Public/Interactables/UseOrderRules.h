#pragma once

#include "CoreMinimal.h"
#include "GameFlow/GameFlowTypes.h"

/** How a «use the object» order (e.g. «Разжечь» a barrel) given in a fight is dispatched. */
enum class EUseOrderDispatch : uint8
{
	/** Outside the real-time / paused fight: the action menu runs it at once (the leader already stands there). */
	Immediate,
	/** Real-time fight: the operative walks up and uses it. */
	Execute,
	/** Tactical pause: planned with a marker, run on the release. */
	Queue
};

/** One step of a running use order. */
enum class EUseOrderStep : uint8
{
	/** Within reach: use the object now. */
	Use,
	/** Still walking up. */
	Walk,
	/** The operative got another move order: the use order is dropped. */
	Cancelled,
	/** Never got there: given up with a feed line. */
	TimedOut
};

/**
 * Orders to use an object in a fight (user decision 2026-10-08: igniting a barrel with matches in the real-time fight and
 * in the tactical pause). No Godot reference: Godot only allowed the barrel menu outside the wave.
 */
namespace UseOrderRules
{
	/** A use order that has not reached its object after this long is given up, s. */
	constexpr float TimeoutSeconds = 30.f;
	/** The operative's move goal moved further than this from the use order's approach = another order, cm. */
	constexpr float OtherOrderTolerance = 100.f;

	CODEXTACTICS_API EUseOrderDispatch GetDispatch(ECodexGamePhase Phase, ECodexCombatMode Mode);

	CODEXTACTICS_API EUseOrderStep GetStep(float DistanceToObject, float InteractionDistance, float GoalDrift, float ElapsedSeconds);
}
