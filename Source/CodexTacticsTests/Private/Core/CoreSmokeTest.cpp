#include "Misc/AutomationTest.h"
#include "Core/CodexTacticsGameMode.h"
#include "Core/CodexTacticsGameState.h"
#include "Core/CodexTacticsPlayerController.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoreSmokeGameModeWiringTest,
	"CodexTactics.Core.Smoke.GameModeWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCoreSmokeGameModeWiringTest::RunTest(const FString& Parameters)
{
	const ACodexTacticsGameMode* GameMode = GetDefault<ACodexTacticsGameMode>();
	TestNotNull(TEXT("GameMode CDO exists"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	TestEqual(TEXT("GameState class"), GameMode->GameStateClass.Get(), ACodexTacticsGameState::StaticClass());
	TestEqual(TEXT("PlayerController class"), GameMode->PlayerControllerClass.Get(), ACodexTacticsPlayerController::StaticClass());

	const ACodexTacticsGameState* GameState = GetDefault<ACodexTacticsGameState>();
	TestEqual(TEXT("Game starts in Exploration"), GameState->GetPlayMode(), ECodexPlayMode::Exploration);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
