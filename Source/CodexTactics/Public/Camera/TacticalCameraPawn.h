#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TacticalCameraPawn.generated.h"

class AOperativeCharacter;
class UCameraComponent;
class USpringArmComponent;

/**
 * The player's view: an angled top-down camera that smoothly follows the squad leader.
 * Uses real (undilated) time so it keeps moving during a tactical pause.
 * Godot reference: Scenes/movements/camera.gd (follow part; zones, zoom and shake come later).
 */
UCLASS()
class CODEXTACTICS_API ATacticalCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ATacticalCameraPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Actor the camera follows; null keeps the camera in place. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Camera")
	void SetFollowTarget(AActor* NewTarget);

	/** Distance from the followed point, cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera", meta = (ClampMin = "100"))
	float ArmLength = 2800.f;

	/** Camera pitch, degrees (negative looks down). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	float Pitch = -55.f;

	/** Camera yaw, degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera")
	float Yaw = -45.f;

	/** Follow interpolation speed, 1/s. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Camera", meta = (ClampMin = "0"))
	float FollowSpeed = 6.f;

private:
	UFUNCTION()
	void HandleLeaderChanged(AOperativeCharacter* NewLeader);

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Camera")
	TObjectPtr<UCameraComponent> Camera;

	TWeakObjectPtr<AActor> FollowTarget;
};
