#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GameFlow/GameFlowTypes.h"
#include "CodexTacticsGameState.generated.h"

/**
 * Exposes the mission game flow to UI and Blueprints. The flow itself lives in UGameFlowSubsystem.
 * Godot reference: Scenes/movements/game_state.gd.
 */
UCLASS()
class CODEXTACTICS_API ACodexTacticsGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** Current mission phase; Exploration when no game flow subsystem exists (e.g. editor worlds). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|State")
	ECodexGamePhase GetGamePhase() const;

	/** Current combat sub-mode; None outside of a wave. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|State")
	ECodexCombatMode GetCombatMode() const;
};
