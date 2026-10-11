#include "Survival/TacticalHeatTimerComponent.h"

namespace
{
	TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>>& HeatTimerRegistry()
	{
		static TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>> Timers;
		return Timers;
	}
}

UTacticalHeatTimerComponent::UTacticalHeatTimerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UTacticalHeatTimerComponent::OnRegister()
{
	Super::OnRegister();
	HeatTimerRegistry().AddUnique(this);
}

void UTacticalHeatTimerComponent::OnUnregister()
{
	HeatTimerRegistry().Remove(this);
	Super::OnUnregister();
}

const TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>>& UTacticalHeatTimerComponent::GetAll()
{
	TArray<TWeakObjectPtr<UTacticalHeatTimerComponent>>& Timers = HeatTimerRegistry();
	Timers.RemoveAll([](const TWeakObjectPtr<UTacticalHeatTimerComponent>& Timer) { return !Timer.IsValid(); });
	return Timers;
}

void UTacticalHeatTimerComponent::Ignite(float Seconds, float InSecondsPerRound)
{
	SecondsPerRound = FMath::Max(0.f, InSecondsPerRound);
	State.Ignite(Seconds);
}

void UTacticalHeatTimerComponent::Stop()
{
	State.Stop();
}

void UTacticalHeatTimerComponent::Restore(bool bInBurning, bool bInBurntOut, float InTimeLeft, float InSecondsPerRound)
{
	State.bBurning = bInBurning && InTimeLeft > 0.f;
	State.bBurntOut = bInBurntOut && !State.bBurning;
	State.TimeLeft = FMath::Max(0.f, InTimeLeft);
	SecondsPerRound = FMath::Max(0.f, InSecondsPerRound);
}

void UTacticalHeatTimerComponent::AdvanceRealTime(float DeltaSeconds)
{
	if (!State.bBurning || DeltaSeconds <= 0.f)
	{
		return;
	}
	const bool bOut = State.Step(DeltaSeconds);
	OnAdvanced.Broadcast(DeltaSeconds);
	if (bOut)
	{
		OnBurntOut.Broadcast();
	}
}

void UTacticalHeatTimerComponent::AdvanceRound()
{
	if (State.bBurning && State.AdvanceRound(SecondsPerRound))
	{
		OnBurntOut.Broadcast();
	}
}
