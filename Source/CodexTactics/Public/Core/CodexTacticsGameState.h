#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "CodexTacticsGameState.generated.h"

/** Top-level play mode: free real-time exploration or turn-based tactical combat. */
UENUM(BlueprintType)
enum class ECodexPlayMode : uint8
{
	Exploration,
	Combat
};

/**
 * Holds the global Exploration <-> Combat mode.
 * Godot reference: Scenes/movements/game_state.gd.
 * Transition logic arrives in Phase 4 (combat); for now this only stores the mode.
 */
UCLASS()
class CODEXTACTICS_API ACodexTacticsGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/** Current top-level play mode. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|State")
	ECodexPlayMode GetPlayMode() const { return PlayMode; }

private:
	UPROPERTY(VisibleInstanceOnly, Category = "CodexTactics|State")
	ECodexPlayMode PlayMode = ECodexPlayMode::Exploration;
};
