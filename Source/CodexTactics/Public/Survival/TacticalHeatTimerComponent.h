#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Survival/TacticalHeatRules.h"
#include "TacticalHeatTimerComponent.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnHeatTimerBurntOut);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnHeatTimerAdvanced, float /*DeltaSeconds*/);

/**
 * Burn timer (or gas supply) of a Sprint 15 heat source: campfire, portable stove, signal flare, residual fire puddle. It does not
 * tick itself: UTacticalHeatSubsystem advances every registered timer in real time only (TacticalHeatRules::ShouldAdvanceRealTime:
 * frozen in the tactical pause, the free horde pause / flyover, dialogues and cutscenes) and by SecondsPerRound per turn-based
 * squad round. The owner listens to OnBurntOut / OnAdvanced and switches its heat / light / effects.
 * UE-only (TANDEM request #14); no Godot reference.
 */
UCLASS(ClassGroup = (CodexTactics), meta = (BlueprintSpawnableComponent))
class CODEXTACTICS_API UTacticalHeatTimerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTacticalHeatTimerComponent();

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Starts burning for Seconds; one turn-based round takes InSecondsPerRound off. */
	void Ignite(float Seconds, float InSecondsPerRound);

	/** Stops without burning out (the rest stays: a stove packed up). */
	void Stop();

	/** Save-game load. */
	void Restore(bool bInBurning, bool bInBurntOut, float InTimeLeft, float InSecondsPerRound);

	/** Real-time step (called by UTacticalHeatSubsystem only when time may advance). */
	void AdvanceRealTime(float DeltaSeconds);

	/** One turn-based squad round. */
	void AdvanceRound();

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	bool IsBurning() const { return State.bBurning; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	bool IsBurntOut() const { return State.bBurntOut; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Heat")
	float GetTimeLeft() const { return State.TimeLeft; }

	float GetSecondsPerRound() const { return SecondsPerRound; }

	/** Every registered timer (callers filter by world). */
	static const TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>>& GetAll();

	/** Fired once when the time runs out (not on Stop). */
	FOnHeatTimerBurntOut OnBurntOut;

	/** Fired after every real-time step that burnt time (campfire noise, puddle damage pulses, light fade). */
	FOnHeatTimerAdvanced OnAdvanced;

private:
	FHeatBurnState State;
	float SecondsPerRound = 0.f;
};
