#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CodexTacticsGameMode.generated.h"

class AOperativeCharacter;

/** One squad member spawned at mission start. */
USTRUCT(BlueprintType)
struct CODEXTACTICS_API FSquadMemberSpawn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	FText DisplayName;

	/** Placeholder body tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	FLinearColor Color = FLinearColor::White;

	/** Spawn offset from the player start (X forward, Y right), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	FVector Offset = FVector::ZeroVector;

	/** Cold resistance (Godot player.gd fortitude: commander 15, engineer 25, medic-sapper 20). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	float Fortitude = 15.f;
};

/**
 * Root game mode: camera pawn for the player, squad spawned at the player start
 * unless the level already contains operatives.
 * Godot reference: Scenes/movements/main.gd (scene bootstrap, squad nodes Player/Follower1/Follower2).
 */
UCLASS()
class CODEXTACTICS_API ACodexTacticsGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACodexTacticsGameMode();

	virtual void StartPlay() override;

	/** Operative class to spawn. */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad")
	TSubclassOf<AOperativeCharacter> OperativeClass;

	/** Squad roster in selection order (keys 1..N). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad")
	TArray<FSquadMemberSpawn> SquadRoster;

private:
	void SpawnSquad();
};
