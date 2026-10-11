#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalHeatActor.generated.h"

class UHeatSourceComponent;
class UMaterialInstanceDynamic;
class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class UStaticMeshComponent;
class UTacticalHeatTimerComponent;

/**
 * Base of the loose Sprint 15 heat sources that are not squad equipment (signal flare, residual fire puddle): a placeholder mesh,
 * a warm zone (UHeatSourceComponent), a point light, a Niagara slot (EffectAsset, loaded only when the asset exists — Gemini
 * plugs NS_* assets in /Game/VFX/Heat without a code change) and a burn timer driven by UTacticalHeatSubsystem. Saved / loaded
 * by the subsystem (class, place, timer). Blueprint subclasses may swap the mesh / effect / light.
 * UE-only (TANDEM request #14); no Godot reference.
 */
UCLASS(Abstract, Blueprintable)
class CODEXTACTICS_API ATacticalHeatActor : public AActor
{
	GENERATED_BODY()

public:
	ATacticalHeatActor();

	virtual void BeginPlay() override;

	/** Starts burning: heat, light and effect on. */
	void Ignite(float Seconds, float SecondsPerRound);

	/** Save-game load: timer state, visuals follow; no lines. */
	virtual void RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	bool IsBurning() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	bool IsBurntOut() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	float GetTimeLeft() const;

	UTacticalHeatTimerComponent* GetTimer() const { return Timer; }

	/** The save keeps this source (a flare still in the air is saved as landed). */
	virtual bool ShouldSave() const { return true; }
	virtual FVector GetSaveLocation() const { return GetActorLocation(); }
	virtual float GetSaveTimeLeft() const { return GetTimeLeft(); }
	virtual bool GetSaveBurning() const { return IsBurning(); }
	virtual float GetSaveRoundSeconds() const;

	/** Turns a Niagara component on / off with Asset (loaded on demand when its package exists; nothing when it does not). */
	static void SetEffectActive(UNiagaraComponent* Effect, const TSoftObjectPtr<UNiagaraSystem>& Asset, bool bActive);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Heat")
	TObjectPtr<USceneComponent> Root;

	/** Placeholder body (engine shape); a Blueprint may set a real mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Heat")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Heat")
	TObjectPtr<UHeatSourceComponent> HeatSource;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Heat")
	TObjectPtr<UPointLightComponent> Light;

	/** VFX slot (fire / sparks / smoke); runs EffectAsset while burning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Heat")
	TObjectPtr<UNiagaraComponent> Effect;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Heat")
	TObjectPtr<UTacticalHeatTimerComponent> Timer;

	/** Niagara system of the burning state (Gemini: /Game/VFX/Heat/NS_*). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat|Visuals")
	TSoftObjectPtr<UNiagaraSystem> EffectAsset;

	/** Light at full strength (candelas). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat|Visuals", meta = (ClampMin = "0"))
	float LightIntensity = 60.f;

	/** The light fades over the last seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat|Visuals", meta = (ClampMin = "0"))
	float FadeSeconds = 4.f;

	/** Placeholder colours (BasicShapeMaterial "Color"): burning / spent. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat|Visuals")
	FLinearColor BurningColor = FLinearColor(1.f, 0.35f, 0.08f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat|Visuals")
	FLinearColor SpentColor = FLinearColor(0.06f, 0.05f, 0.05f);

	/** A spent source disappears after this long (0 = stays). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Heat", meta = (ClampMin = "0"))
	float SpentLifetime = 3.f;

	/** Blueprint hook for VFX / sound when the fire starts / goes out. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Heat", meta = (DisplayName = "On Burning Changed"))
	void ReceiveBurningChanged(bool bBurning);

protected:
	/** Heat on / off, light, effect, colour (also called after a load). */
	virtual void ApplyBurningVisuals();

	/** Real-time step of the timer (light fade; subclasses add damage pulses). */
	virtual void HandleAdvanced(float DeltaSeconds);

	/** The time ran out. */
	virtual void HandleBurntOut();

	/** Sets the placeholder mesh / scale / colour (constructor helper). */
	void SetPlaceholderShape(const TCHAR* MeshPath, const FVector& Scale, const FVector& Offset);

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;

	bool bLastBurning = false;
};
