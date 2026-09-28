#include "Core/CodexTacticsGameMode.h"
#include "Core/CodexTacticsGameState.h"
#include "Core/CodexTacticsPlayerController.h"

ACodexTacticsGameMode::ACodexTacticsGameMode()
{
	GameStateClass = ACodexTacticsGameState::StaticClass();
	PlayerControllerClass = ACodexTacticsPlayerController::StaticClass();
}
