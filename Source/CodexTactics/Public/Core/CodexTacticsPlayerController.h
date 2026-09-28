#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CodexTacticsPlayerController.generated.h"

/**
 * Player controller for squad control. Enhanced Input mapping arrives in Phase 3.
 * Godot reference: Scenes/movements/player.gd (input handling part).
 */
UCLASS()
class CODEXTACTICS_API ACodexTacticsPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACodexTacticsPlayerController();
};
