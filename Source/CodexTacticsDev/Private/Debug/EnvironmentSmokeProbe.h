#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"
#include "Subsystems/CodexEventBus.h"
#include "UObject/Object.h"
#include "EnvironmentSmokeProbe.generated.h"

/** One phase / weather event: Old -> New (From -> To for the transition event). */
template <typename T>
struct TEnvEventPair
{
	T Old;
	T New;
};

/** Dev-only listener for EnvironmentSmoke: records every environment event the UCodexEventBus broadcasts, in order. */
UCLASS(Transient)
class UEnvironmentSmokeProbe : public UObject
{
	GENERATED_BODY()

public:
	TArray<TEnvEventPair<ETimeOfDayPhase>> Phases;
	TArray<TEnvEventPair<EWeatherState>> Transitions;
	TArray<TEnvEventPair<EWeatherState>> Changes;

	void Listen(UCodexEventBus* Bus)
	{
		Bus->OnTimeOfDayPhaseChanged.AddDynamic(this, &UEnvironmentSmokeProbe::OnPhase);
		Bus->OnWeatherTransitionStarted.AddDynamic(this, &UEnvironmentSmokeProbe::OnTransition);
		Bus->OnWeatherChanged.AddDynamic(this, &UEnvironmentSmokeProbe::OnChanged);
	}

	void StopListening(UCodexEventBus* Bus)
	{
		Bus->OnTimeOfDayPhaseChanged.RemoveAll(this);
		Bus->OnWeatherTransitionStarted.RemoveAll(this);
		Bus->OnWeatherChanged.RemoveAll(this);
	}

	void Reset()
	{
		Phases.Reset();
		Transitions.Reset();
		Changes.Reset();
	}

	UFUNCTION() void OnPhase(ETimeOfDayPhase OldPhase, ETimeOfDayPhase NewPhase) { Phases.Add({OldPhase, NewPhase}); }
	UFUNCTION() void OnTransition(EWeatherState From, EWeatherState To) { Transitions.Add({From, To}); }
	UFUNCTION() void OnChanged(EWeatherState From, EWeatherState To) { Changes.Add({From, To}); }
};
