#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CodexTacticsGameMode.generated.h"

/**
 * Root game mode. Wires the project's GameState and PlayerController.
 * Godot reference: Scenes/movements/main.gd (scene bootstrap).
 */
UCLASS()
class CODEXTACTICS_API ACodexTacticsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACodexTacticsGameMode();
};
