#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CombatFeedbackActor.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;
class UPointLightComponent;
class UStaticMeshComponent;

/**
 * Short-lived glow spawned by UCombatFeedbackSubsystem: a tracer beam, a muzzle / aim flash light, a planned-order
 * marker disc or a target flash overlay. Fades its glow and light to zero, then destroys itself (markers stay until
 * cleared). Runs on world time, so it slows down with the tactical pause like Godot tweens under Engine.time_scale.
 * Godot reference: player.gd / turret.gd `_spawn_muzzle_tracer`, main.gd `_spawn_waypoint_marker`,
 * `_highlight_target_feedback`.
 */
UCLASS(NotBlueprintable)
class CODEXTACTICS_API ACombatFeedbackActor : public AActor
{
	GENERATED_BODY()

public:
	ACombatFeedbackActor();

	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Beam from Start to End (thickness cm), glowing Color * Intensity, fading in FadeTime s. */
	void SetupBeam(const FVector& Start, const FVector& End, float Thickness, const FLinearColor& Color, float Intensity, float FadeTime);

	/** Flat disc on the ground (planned order marker); FadeTime <= 0 keeps it until destroyed. */
	void SetupDisc(const FVector& Location, float Radius, float Height, const FLinearColor& Color, float Intensity, float FadeTime);

	/** Point light at the actor location fading from Intensity to 0 in FadeTime s. */
	void SetupLight(const FLinearColor& Color, float Intensity, float Radius, float FadeTime);

	/** Glowing overlay on every mesh of Target, restored after FadeTime s. */
	void SetupOverlayFlash(AActor* Target, const FLinearColor& Color, float Intensity, float FadeTime);

	/** Glow material (M_CombatFeedback: Color, Intensity). */
	UPROPERTY(EditDefaultsOnly, Category = "CodexTactics|Feedback")
	TObjectPtr<UMaterialInterface> GlowMaterial;

private:
	UMaterialInstanceDynamic* MakeGlow(const FLinearColor& Color, float Intensity);
	void RestoreOverlays();

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Feedback")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "CodexTactics|Feedback")
	TObjectPtr<UPointLightComponent> Light;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Glow;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OverlayGlow;

	TArray<TWeakObjectPtr<UMeshComponent>> OverlaidMeshes;
	TArray<TObjectPtr<UMaterialInterface>> PreviousOverlays;

	float GlowIntensity = 0.f;
	float GlowFade = 0.f;
	float OverlayIntensity = 0.f;
	float OverlayFade = 0.f;
	float LightIntensity = 0.f;
	float LightFade = 0.f;
	float Age = 0.f;
	bool bPersistent = false;
};
