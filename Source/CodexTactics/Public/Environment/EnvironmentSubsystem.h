#pragma once

#include "CoreMinimal.h"
#include "Environment/EnvironmentTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "EnvironmentSubsystem.generated.h"

class ACodexTacticsGameMode;
class AWeatherPresenterActor;

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

	/**
	 * Remembers the carried state the current level was ENTERED with (unset = it started fresh). Written by UEnvironmentSubsystem when
	 * it resolves the start state, before the level's own clock changes anything.
	 */
	void RecordEntryState(const TOptional<FEnvironmentState>& Entry) { EntryState = Entry; bEntryRecorded = true; }

	/**
	 * Restart of the current level: puts the entry state back as the carried state (Clear when the level was entered without one) so the
	 * failed attempt's end state does not accumulate (snow cover, time of day). No-op before any level recorded an entry.
	 * @return true when the carry was reset
	 */
	bool RestoreEntryState()
	{
		if (!bEntryRecorded)
		{
			return false;
		}
		Carried = EntryState;
		return true;
	}

	/** Forgets the previous level's end state: the next level starts fresh (NEW GAME). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void Clear() { Carried.Reset(); EntryState.Reset(); bEntryRecorded = false; }

private:
	TOptional<FEnvironmentState> Carried;
	TOptional<FEnvironmentState> EntryState;
	bool bEntryRecorded = false;
};

/**
 * The authoritative day cycle + procedural weather of a level (UE-only, docs/systems/environment-weather.md, phase 1).
 * Owns one FEnvironmentState and advances it with EnvironmentRules while EnvironmentRules::ShouldAdvance allows it: the clock
 * and the weather FREEZE in the tactical pause, for the whole turn-based combat (user decision 8: 0 minutes per round) and while
 * the world AI is held (dialogues, cutscenes). Start state (ResolveInitialState): a locked level (Demo 01 tutorial) -> its fixed
 * hour / weather; else the previous level's end state (UEnvironmentCarrySubsystem); else fresh (level start_hour / weather /
 * seed, DefaultStartHour, PickStartWeather). Tuning: Content/Data/Environment/environment.json (EnvironmentTuning).
 * Broadcasts: the native delegates below and UCodexEventBus OnTimeOfDayPhaseChanged / OnWeatherTransitionStarted /
 * OnWeatherChanged. Phase 4: the snow cover (FEnvironmentState::SnowAmount) grows after the weather step (EnvironmentRules::StepSnowCover).
 * Save / load: the whole state under "game_state.environment" (USaveGameSubsystem restore step "Environment" -> SetState;
 * docs/systems/save-load.md).
 * Console (CodexTacticsDev): Codex.Env.Dump / SetWeather / SetHour / ClockScale / Seed / SnowCover.
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
	 * Section 17: the state takes Level.Climate; a Carried state of ANOTHER climate is not continued but re-initialised for this level's
	 * profile (fresh weather / snow / wetness from the level's keys or the defaults, seeded by the carried RNG) at the carried hour.
	 */
	static FEnvironmentState ResolveInitialState(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level,
		const TOptional<FEnvironmentState>& Carried, int32 RandomSeedIfNeeded);

	/** The level's "environment" block arrived (normally read at OnWorldBeginPlay): re-resolves the start state unless the clock already ran. */
	void ApplyLevelSettings(const FEnvironmentLevelSettings& Settings);

	/** The level's "environment" block in force (FEnvironmentLevelSettings::bVisuals gates the weather presenter). */
	const FEnvironmentLevelSettings& GetLevelSettings() const { return LevelSettings; }

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

	/**
	 * Moves every time the whole state JUMPS (the level start resolve, SetState = a save load / debug): the visuals snap to it - no
	 * smoothing, the eye adaptation settles at once, the falling snow is pre-warmed (EnvironmentStartRules::ShouldSnap; section 16).
	 * Weather / hour changes in play (the clock, ForceWeather, SetHour) do not move it.
	 */
	int32 GetSnapSerial() const { return SnapSerial; }

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

	/**
	 * Wind 0..1 in force (e.g. the operatives' blizzard walk layer): the WindStrength of the weather presets blended Weather -> NextWeather
	 * by GetTransitionBlend (EnvironmentVisualRules::GetBlendedWindStrength). Presets: the presenter's (DA_Weather_* assets) when this
	 * world has one, else the C++ defaults (EnvironmentVisualRules::GetDefaultPreset) - also with the visuals off. 0 when not initialised.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetWindStrength() const;

	/**
	 * How much of Weather is in force now, 0..1 (EnvironmentRules::GetWeatherAmount): 1 for the current weather outside a transition,
	 * 1 - blend for it and the blend for NextWeather while blending, else 0. 0 when not initialised.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetWeatherAmount(EWeatherState Weather) const;

	/** Phase 4: persistent snow cover 0..1 (FEnvironmentState::SnowAmount; grows while it snows, never recedes). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetSnowAmount() const { return State.SnowAmount; }

	/** Section 17: the climate profile in force (the level's "climate"; Arctic everywhere but the narrative temperate levels). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	EClimateProfile GetClimate() const { return State.Climate; }

	/** Section 17: the weather's name on this climate for the UI / logs ("Snowfall" on arctic, "Rain" on temperate, ...). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	FString GetWeatherDisplayName() const;

	/** Section 17: rain 0..1 in force (0 SunnyDay / Overcast, ~0.6 Rain, 1 Storm, blended; always 0 on arctic levels). For the rain FX. */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetRainIntensity() const;

	/** Section 17: how wet the ground is, 0..1 (temperate; 0 on arctic levels). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetWetness() const { return State.Wetness; }

	/** Section 17: standing water 0..1 (lags the wetness; temperate only). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetPuddleAmount() const { return State.PuddleAmount; }

	/**
	 * Debug (console Codex.Env.Climate): switches the climate profile at runtime and re-initialises the weather for it like a level of
	 * that climate would (ResolveInitialState with this state as the carry: the time of day continues); snaps the visuals (SetState).
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetClimate(EClimateProfile Climate);

	/**
	 * Debug (console Codex.Env.SnowCover): sets the snow cover (clamped 0..1; may lower it, unlike the weather) and the objects' TOP pocket
	 * to the same value (their sides are kept); broadcasts OnStateSet.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetSnowAmount(float Amount);

	/**
	 * Section 18, debug / captures (console Codex.Env.Wetness): sets the ground wetness and the puddles (clamped 0..1; a temperate level then
	 * keeps stepping them with the weather); broadcasts OnStateSet (no snap). Visual tuning only - an arctic level keeps them at 0 anyway.
	 */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetWetness(float Wetness, float PuddleAmount);

	/** Section 15: the directional snow on static objects (world axes; FEnvironmentState::ObjectSnow). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	FDirectionalSnow GetObjectSnow() const { return State.ObjectSnow; }

	/** Debug (console Codex.Env.ObjectSnow): sets the objects' pockets (clamped 0..1); broadcasts OnStateSet. */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetObjectSnow(const FDirectionalSnow& Pockets);

	/**
	 * Section 15: the horizontal unit wind TRAVEL direction in force (where the snow is blown to; MPC WindDirection.xyz): the debug
	 * override when set (SetWindDirectionOverride, console Codex.Env.WindFrom), else the presenter's slow drift
	 * (AWeatherPresenterActor::ComputeWindDirection with its WindBaseYaw / WindDriftDegrees / WindDriftPeriodSeconds, the C++ defaults
	 * without a presenter) at the world time. Visual (and the directional snow); never read by gameplay.
	 */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	FVector GetWindDirection() const;

	/** Debug: fixes the wind travel direction (z dropped, normalised; zero clears the override). */
	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void SetWindDirectionOverride(const FVector& Direction);

	UFUNCTION(BlueprintCallable, Category = "CodexTactics|Environment")
	void ClearWindDirectionOverride() { WindOverride.Reset(); }

	bool HasWindDirectionOverride() const { return WindOverride.IsSet(); }

	/** Section 15: sun elevation above the horizon in degrees at the current hour (the presenter's DayCurves, else the C++ defaults). */
	UFUNCTION(BlueprintPure, Category = "CodexTactics|Environment")
	float GetSunElevation() const;

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
	int32 SnapSerial = 0;
	/** The presenter spawned for this world (GetWindStrength reads its presets; weak: it may be destroyed first). */
	TWeakObjectPtr<AWeatherPresenterActor> Presenter;
	/** Debug wind travel direction (GetWindDirection). */
	TOptional<FVector> WindOverride;
};
