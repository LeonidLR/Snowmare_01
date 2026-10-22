#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "OperativePortraitCapture.generated.h"

class AOperativeCharacter;
class UPointLightComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Live portrait of one operative for the stat-points screen after a level exit (game designer 2026-10-22: "a menu with the
 * character's image and the points"): a scene capture in front of his chest that renders only him (show-only list) into a render
 * target, with its own key / fill lights (the world is behind the black exit screen, so the lights never show in the scene).
 * Spawned by UPointsReviewWidget; only where the game renders (FApp::CanEverRender) - headless checks keep the letter portrait.
 * UE-only (the Godot profile had an emoji portrait).
 */
UCLASS(NotPlaceable, Transient)
class CODEXTACTICS_API AOperativePortraitCapture : public AActor
{
	GENERATED_BODY()

public:
	AOperativePortraitCapture();

	/** Frames Operative (nullptr: stops capturing). */
	void SetTarget(AOperativeCharacter* Operative);

	AOperativeCharacter* GetTarget() const { return Target.Get(); }

	/** The image the widget shows (created on first use). */
	UTextureRenderTarget2D* GetRenderTarget();

	/** Portrait camera distance in front of the chest, cm. */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Portrait")
	float DistanceCm = 230.f;

	/** Height of the framed point above the capsule centre, cm (chest / head). */
	UPROPERTY(EditAnywhere, Category = "CodexTactics|Portrait")
	float FocusHeightCm = 40.f;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Portrait")
	float FieldOfView = 30.f;

	UPROPERTY(EditAnywhere, Category = "CodexTactics|Portrait")
	FIntPoint Resolution = FIntPoint(360, 450);

	virtual void Tick(float DeltaSeconds) override;

private:
	void Place();

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Portrait")
	TObjectPtr<USceneCaptureComponent2D> Capture;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Portrait")
	TObjectPtr<UPointLightComponent> KeyLight;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Portrait")
	TObjectPtr<UPointLightComponent> FillLight;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	TWeakObjectPtr<AOperativeCharacter> Target;
};
