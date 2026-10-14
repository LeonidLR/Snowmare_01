#pragma once

#include "CoreMinimal.h"
#include "Interactables/DeployableActor.h"
#include "CampfireActor.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UPointLightComponent;
class UTacticalHeatTimerComponent;

/**
 * Field campfire (Sprint 15, TANDEM request #14 §2.2): crafted on the spot by an operative from 1 match + 1 firewood (or 10 units
 * of flamethrower fuel) — F / the drawer's «Campfire» line, the placement ghost, the walk, then it is kindled. It warms the whole
 * squad within CampfireHeatRadius (3.5 m) for CampfireBurnSeconds (75 s; 4 turn-based rounds; -20 % on high ground in the wind),
 * then stays as ashes (BurntOut) that can be rekindled with new materials. Risk: the crackle and the smoke column make every
 * patrol within CampfireNoiseRadius (15 m) start a search at the fire (again every CampfireNoiseIntervalSeconds while it burns);
 * indoors (inside a Closed / Shelter ACameraZoneVolume) only within x CampfireIndoorNoiseMultiplier (0.4: 6 m; rev 5d).
 * Not packable, not relocatable, cannot be trapped. UE-only; no Godot reference.
 */
UCLASS(Blueprintable)
class CODEXTACTICS_API ACampfireActor : public ADeployableActor
{
	GENERATED_BODY()

public:
	ACampfireActor();

	virtual void BeginPlay() override;
	virtual FActionMenuRequest BuildActionMenu(const AOperativeCharacter* Leader) const override;
	virtual void ExecuteAction(AOperativeCharacter* User) override;
	virtual bool CanReceiveTrap() const override { return false; }
	virtual void OnDeployedBy(AOperativeCharacter* Worker) override;

	/** Lights the fire for its burn time (wind on high ground shortens it) and alerts the patrols in earshot. */
	void Kindle(AOperativeCharacter* Worker);

	/** Save-game load: timer state, visuals follow; no lines, no noise. */
	void RestoreHeatState(bool bBurning, bool bBurntOut, float TimeLeft, float SecondsPerRound);

	/** Patrols within the noise radius start a search here; returns how many were alerted. */
	int32 AlertPatrols();

	/** Rev 5d: inside a Closed / Shelter camera zone (walls and a roof): the noise radius shrinks (GetCampfireNoiseRadius). */
	bool IsIndoors() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Campfire")
	bool IsBurning() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Campfire")
	bool IsBurntOut() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Campfire")
	float GetTimeLeft() const;

	/** The wind on high ground shortened this fire. */
	bool IsWindExposed() const { return bWindExposed; }

	UTacticalHeatTimerComponent* GetTimer() const { return Timer; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Campfire")
	TObjectPtr<UPointLightComponent> FireLight;

	/** VFX slot: fire + smoke column (Gemini: /Game/VFX/Heat/NS_Campfire). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Campfire")
	TObjectPtr<UNiagaraComponent> FireEffect;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CodexTactics|Campfire")
	TObjectPtr<UTacticalHeatTimerComponent> Timer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Campfire|Visuals")
	TSoftObjectPtr<UNiagaraSystem> FireEffectAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Campfire|Visuals", meta = (ClampMin = "0"))
	float FireLightIntensity = 90.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Campfire|Visuals")
	FLinearColor LogColor = FLinearColor(0.25f, 0.12f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Campfire|Visuals")
	FLinearColor EmberColor = FLinearColor(0.9f, 0.3f, 0.05f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CodexTactics|Campfire|Visuals")
	FLinearColor AshColor = FLinearColor(0.08f, 0.07f, 0.07f);

	/** Blueprint hook for fire VFX / crackle sound. */
	UFUNCTION(BlueprintImplementableEvent, Category = "CodexTactics|Campfire", meta = (DisplayName = "On Burning Changed"))
	void ReceiveBurningChanged(bool bBurning);

private:
	void ApplyVisuals();
	void HandleAdvanced(float DeltaSeconds);
	void HandleBurntOut();

	UPROPERTY(Transient)
	TObjectPtr<class UMaterialInstanceDynamic> BodyMaterial;

	float NoiseClock = 0.f;
	bool bWindExposed = false;
	bool bLastBurning = false;
};
