#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GateActor.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 * Heavy sliding gate of the checkpoint. Opens when the quest powers the gate terminal: the doors slide apart
 * along the local Y axis (left -OpenDistance, right +OpenDistance) at OpenSpeed; once open, their collision
 * stops blocking the passage.
 * Godot reference: Scenes/movements/gate.gd (open_distance 3.5 m, open_speed 2 m/s).
 */
UCLASS()
class CODEXTACTICS_API AGateActor : public AActor
{
	GENERATED_BODY()

public:
	AGateActor();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Gate")
	void OpenGate();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	bool IsOpening() const { return bOpening; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Gate")
	bool IsOpen() const { return bOpen; }

	/** Save-game load: snaps both leaves open (no blocking, no nav) or closed (blocking) at once. */
	void RestoreOpen(bool bInOpen);

	/** Slide distance of each door, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float OpenDistance = 350.f;

	/** Slide speed, cm/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate", meta = (ClampMin = "0"))
	float OpenSpeed = 200.f;

	/** Size of one door leaf (X thickness, Y width, Z height), cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Gate")
	FVector DoorSize = FVector(40.f, 350.f, 400.f);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	TObjectPtr<UBoxComponent> LeftDoor;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Gate")
	TObjectPtr<UBoxComponent> RightDoor;

private:
	UFUNCTION()
	void HandleGateOpened();

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate")
	TObjectPtr<UStaticMeshComponent> LeftDoorMesh;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Gate")
	TObjectPtr<UStaticMeshComponent> RightDoorMesh;

	float LeftTargetY = 0.f;
	float RightTargetY = 0.f;
	bool bOpening = false;
	bool bOpen = false;
};
