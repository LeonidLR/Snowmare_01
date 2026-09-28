#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Interactables/DeployableRules.h"
class UDialogueSequenceAsset;
class UWeaponDataAsset;
#include "CodexTacticsGameMode.generated.h"

class AOperativeCharacter;
class ABarricadeActor;
class AProximityMineActor;
class ATurretActor;

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

	/** Squad role (defusal skill, deployable routing). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	EOperativeRole Role = EOperativeRole::Commander;

	/** Luck, % (Godot: commander 25, engineer 30, medic-sapper 35). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	float Luck = 25.f;

	/** Marksmanship, % (Godot player.gd accuracy: commander 90, engineer 75, medic-sapper 85). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Squad")
	float Accuracy = 90.f;
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

	/** Fallback operative class, used when OperativeBlueprint is missing. */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad")
	TSubclassOf<AOperativeCharacter> OperativeClass;

	/** Operative Blueprint (look, collision, animation are set up there). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad")
	TSoftClassPtr<AOperativeCharacter> OperativeBlueprint;

	/** Classes spawned when operatives set up items from their supply (Blueprints may replace them). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Deployables")
	TSubclassOf<ABarricadeActor> BarricadeClass;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Deployables")
	TSubclassOf<AProximityMineActor> MineClass;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Deployables")
	TSubclassOf<ATurretActor> TurretClass;

	/** Weapon every spawned operative starts with (imported from Godot rifle_m16.tres by Scripts/Editor/import_weapons.py). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad")
	TSoftObjectPtr<UWeaponDataAsset> StartingWeapon;

	/** Starting reserve of that weapon (Godot player.gd _init_weapons: m16 reserve 60). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad", meta = (ClampMin = "0"))
	int32 StartingReserveAmmo = 60;

	/** Story dialogues (imported from Godot by Scripts/Editor/import_dialogues.py; Godot main.gd dialogue_* exports). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Dialogue")
	TSoftObjectPtr<UDialogueSequenceAsset> DialogueMissionStart;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Dialogue")
	TSoftObjectPtr<UDialogueSequenceAsset> DialoguePreparationStarted;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Dialogue")
	TSoftObjectPtr<UDialogueSequenceAsset> DialogueWaveRest;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Dialogue")
	TSoftObjectPtr<UDialogueSequenceAsset> DialogueVictory;

	/** Squad roster in selection order (keys 1..N). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Squad")
	TArray<FSquadMemberSpawn> SquadRoster;

private:
	void SpawnSquad();
};
