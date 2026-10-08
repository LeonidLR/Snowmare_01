#include "Interactables/UseOrderRules.h"

EUseOrderDispatch UseOrderRules::GetDispatch(ECodexGamePhase Phase, ECodexCombatMode Mode)
{
	if (Phase != ECodexGamePhase::WaveCombat)
	{
		return EUseOrderDispatch::Immediate;
	}
	switch (Mode)
	{
	case ECodexCombatMode::TacticalPause: return EUseOrderDispatch::Queue;
	case ECodexCombatMode::RealTime: return EUseOrderDispatch::Execute;
	default: return EUseOrderDispatch::Immediate;
	}
}

EUseOrderStep UseOrderRules::GetStep(float DistanceToObject, float InteractionDistance, float GoalDrift, float ElapsedSeconds)
{
	if (DistanceToObject <= InteractionDistance)
	{
		return EUseOrderStep::Use;
	}
	if (GoalDrift > OtherOrderTolerance)
	{
		return EUseOrderStep::Cancelled;
	}
	return ElapsedSeconds > TimeoutSeconds ? EUseOrderStep::TimedOut : EUseOrderStep::Walk;
}
