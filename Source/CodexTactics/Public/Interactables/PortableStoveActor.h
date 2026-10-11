#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "PortableStoveActor.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class UTacticalHeatTimerComponent;

/**
 * Portable gas stove (Sprint 15, TANDEM request #14 §2.3; EDeployableType::PortableStove): reusable squad equipment. «Deploy» =
 * F / the drawer like any deployable (2 AP in turn-based combat); it burns its canister's gas (StoveGasCapacitySeconds 180 in
 * total, the rest is kept with the packed item: AOperativeCharacter::StoveGasSeconds) and warms within StoveHeatRadius (2.2 m).
 * Windproof, silent, no smoke: no noise and no detection. «Pack up» (the menu, a crouched StovePackUpSeconds work) puts it back
 * into the packer's inventory with the gas left. An empty canister keeps it cold until packed. UE-only; no Godot reference.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API APortableStoveActor : public ADeployableActor
{
	GENERATED_BODY()

public:
	APortableStoveActor();

	virtual void BeginPlay() override;
	virtual bool CanReceiveTrap() const override { return false; }
	virtual void OnDeployedBy(AOperativeCharacter* Worker) override;

	/** Starts with Gas seconds in the canister (lit when > 0). */
	void StartWithGas(float Gas);

	/** Save-game load. */
	void RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Stove")
	bool IsBurning() const;

	/** Gas left in the canister, s. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Stove")
	float GetGasLeft() const;

	UTacticalHeatTimerComponent* GetTimer() const { return Timer; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Stove")
	TObjectPtr<UPointLightComponent> FlameLight;

	/** VFX slot: small blue gas flame (Gemini: /Game/VFX/Heat/NS_PortableStove). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Stove")
	TObjectPtr<UNiagaraComponent> FlameEffect;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Stove")
	TObjectPtr<UTacticalHeatTimerComponent> Timer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stove|Visuals")
	TSoftObjectPtr<UNiagaraSystem> FlameEffectAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stove|Visuals", meta = (ClampMin = "0"))
	float FlameLightIntensity = 25.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stove|Visuals")
	FLinearColor BodyColor = FLinearColor(0.32f, 0.36f, 0.38f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Stove|Visuals")
	FLinearColor LitColor = FLinearColor(0.2f, 0.45f, 1.f);

	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Stove", meta = (DisplayName = "On Burning Changed"))
	void ReceiveBurningChanged(bool bBurning);

protected:
	virtual void DescribeForMenu(const AOperativeCharacter* Leader, FText& OutTitle, FText& OutDescription, FText& OutConfirm,
		bool& bOutDisabled) const override;
	virtual void OnPickedUpBy(AOperativeCharacter& Recipient) override;

private:
	void ApplyVisuals();
	void HandleBurntOut();

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> BodyMaterial;

	bool bLastBurning = false;
};
