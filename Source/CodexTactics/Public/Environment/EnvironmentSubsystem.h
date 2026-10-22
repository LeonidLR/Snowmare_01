#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnvironmentSubsystem.generated.h"

class ACodexTacticsGameMode;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEnvironmentPhaseChangedNative, ETimeOfDayPhase /*OldPhase*/, ETimeOfDayPhase /*NewPhase*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnEnvironmentWeatherNative, EWeatherState /*From*/, EWeatherState /*To*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnEnvironmentStateSetNative, const FEnvironmentState& /*State*/);

/**
 * Carries the environment state from one level to the next (UE-only, docs/systems/environment-weather.md, user decision 17:
 * the next level CONTINUES the time and weather where the previous one ended, e.g. after the Demo 01 tutorial). Written by
 * UEnvironmentSubsystem when a game world ends, read by the next one; a locked level hands its END state over too (the next
 * level runs it unlocked). Cleared by NEW GAME (UCodexFrontendSubsystem::StartNewGame) so a fresh run starts fresh.
 */
UCLASS()
class CODEXTACTICS_API UEnvironmentCarrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** The carry of Context's game instance (null without one, e.g. in unit tests). */
	static UEnvironmentCarrySubsystem* Get(const UObject* Context);

	/** The state the previous level ended with (unset after Clear / before the first level). */
	const TOptional<FEnvironmentState>& GetCarriedState() const { return Carried; }

	void SetCarriedState(const FEnvironmentState& State) { Carried = State; }

	/** Forgets the previous level's end state: the next level starts fresh (NEW GAME). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void Clear() { Carried.Reset(); }

private:
	TOptional<FEnvironmentState> Carried;
};

/**
 * The authoritative day cycle + procedural weather of a level (UE-only, docs/systems/environment-weather.md, phase 1).
 * Owns one FEnvironmentState and advances it with EnvironmentRules while EnvironmentRules::ShouldAdvance allows it: the clock
 * and the weather FREEZE in the tactical pause, for the whole turn-based combat (user decision 8: 0 minutes per round) and while
 * the world AI is held (dialogues, cutscenes). Start state (ResolveInitialState): a locked level (Demo 01 tutorial) -> its fixed
 * hour / weather; else the previous level's end state (UEnvironmentCarrySubsystem); else fresh (level start_hour / weather /
 * seed, DefaultStartHour, PickStartWeather). Tuning: Content/Data/Environment/environment.json (EnvironmentTuning).
 * Broadcasts: the native delegates below and UCodexEventBus OnTimeOfDayPhaseChanged / OnWeatherTransitionStarted /
 * OnWeatherChanged. Save / load: SetState (the save keys are phase 5, docs/systems/save-load.md).
 * Console (CodexTacticsDev): Codex.Env.Dump / SetWeather / SetHour / ClockScale / Seed.
 */
UCLASS()
class CODEXTACTICS_API UEnvironmentSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableWhenPaused() const override { return false; }

	/** The environment subsystem of Context's world (null without one). */
	static UEnvironmentSubsystem* Get(const UObject* Context);

	/**
	 * The start state of a level (pure, unit-tested): Level.bLocked -> Level's hour (else Config.DefaultStartHour) and weather
	 * (else PickStartWeather), locked; else Carried when set (continued exactly, UNLOCKED even after a locked tutorial); else fresh:
	 * Level's hour / weather / seed, Config.DefaultStartHour, PickStartWeather, RandomSeedIfNeeded when the level fixes no seed.
	 */
	static FEnvironmentState ResolveInitialState(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level,
		const TOptional<FEnvironmentState>& Carried, int32 RandomSeedIfNeeded);

	/** The level's "environment" block arrived (normally read at OnWorldBeginPlay): re-resolves the start state unless the clock already ran. */
	void ApplyLevelSettings(const FEnvironmentLevelSettings& Settings);

	/** The tuning in force (EnvironmentTuning::Get). */
	const FEnvironmentConfig& GetConfig() const;

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	FEnvironmentState GetState() const { return State; }

	/** The state without a copy (C++). */
	const FEnvironmentState& GetStateRef() const { return State; }

	/** Game clock, hours in [0, 24). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetHour() const { return State.Hour; }

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	ETimeOfDayPhase GetPhase() const;

	/** The weather in force. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	EWeatherState GetWeather() const { return State.Weather; }

	/** The weather the current dwell blends into at its end. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	EWeatherState GetNextWeather() const { return State.NextWeather; }

	/** 0 = fully GetWeather, 1 = fully GetNextWeather (EnvironmentRules::GetTransitionBlend). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetTransitionBlend() const;

	/** Locked level (Demo 01 tutorial): neither the clock nor the weather moves. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	bool IsLocked() const { return State.bLocked; }

	/** The state has been resolved (after OnWorldBeginPlay of a game world with the project's game mode). */
	bool IsInitialised() const { return bInitialised; }

	/** The clock has advanced at least once on this level (level settings arriving later no longer re-init). */
	bool HasClockRun() const { return bClockRan; }

	/** Real seconds the clock ran on this level (smokes / telemetry). */
	float GetAdvancedSeconds() const { return AdvancedSeconds; }

	/** The clock and the weather may advance now (EnvironmentRules::ShouldAdvance for this world; false while locked). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	bool CanAdvance() const;

	/** Replaces the whole state (load / tests); broadcasts the phase and weather differences so presenters re-sync. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetState(const FEnvironmentState& NewState);

	/**
	 * Forces the weather: bInstant -> Weather is in force at once (new dwell and NextWeather rolled for the current phase);
	 * else the blend toward Weather starts now (it ends after WeatherTransitionSeconds). Works on a locked level too (debug).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void ForceWeather(EWeatherState Weather, bool bInstant);

	/** Sets the game clock (wrapped into [0, 24)); the current weather dwell keeps running. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetHour(float Hour);

	/** Debug speed-up of the clock and the weather (1 = design speed; clamped to >= 0). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetClockScale(float Scale);

	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetClockScale() const { return ClockScale; }

	/** Restarts the weather sequence from Seed at the current hour (0 = random): new start weather, NextWeather and dwell. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void RestartWeatherSequence(int32 Seed);

	/** One-line summary for logs / the console (hour, phase, weather -> next, blend, dwell, seed, locked, scale). */
	FString Describe() const;

	FOnEnvironmentPhaseChangedNative OnPhaseChanged;
	FOnEnvironmentWeatherNative OnWeatherTransitionStarted;
	FOnEnvironmentWeatherNative OnWeatherChanged;
	/** After SetState / ForceWeather / SetHour / RestartWeatherSequence / the initial resolve (the whole state may have jumped). */
	FOnEnvironmentStateSetNative OnStateSet;

private:
	/** Reads the level's "environment" block from the game mode (PeekLevelConfig) and resolves the start state. */
	void InitialiseFromWorld(UWorld& World);
	void Resolve();
	/** Broadcasts the phase / weather differences between Old and State plus OnStateSet. */
	void BroadcastDifferences(const FEnvironmentState& Old, bool bStateSet);
	void BroadcastPhaseChanged(ETimeOfDayPhase OldPhase, ETimeOfDayPhase NewPhase);
	void BroadcastTransitionStarted(EWeatherState From, EWeatherState To);
	void BroadcastWeatherChanged(EWeatherState OldWeather, EWeatherState NewWeather);
	void HandOverToNextLevel();

	FEnvironmentState State;
	FEnvironmentLevelSettings LevelSettings;
	float ClockScale = 1.f;
	float AdvancedSeconds = 0.f;
	bool bInitialised = false;
	bool bClockRan = false;
	bool bHandedOver = false;
};
