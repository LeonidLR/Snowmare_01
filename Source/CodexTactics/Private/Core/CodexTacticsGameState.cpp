#include "Core/CodexTacticsGameState.h"
#include "Engine/World.h"
#include "GameFlow/GameFlowSubsystem.h"

ECodexGamePhase ACodexTacticsGameState::GetGamePhase() const
{
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow ? Flow->GetPhase() : ECodexGamePhase::Exploration;
}

ECodexCombatMode ACodexTacticsGameState::GetCombatMode() const
{
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return Flow ? Flow->GetCombatMode() : ECodexCombatMode::None;
}
