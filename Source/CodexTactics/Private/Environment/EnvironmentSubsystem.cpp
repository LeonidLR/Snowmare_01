#include "Environment/EnvironmentSubsystem.h"

#include "AI/WorldAIPauseSubsystem.h"
#include "CodexTactics.h"
#include "Core/CodexTacticsGameMode.h"
#include "Data/EnvironmentTuning.h"
#include "Data/WaveConfigTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentVisualRules.h"
#include "Environment/WeatherPresenterActor.h"
#include "GameFlow/GameFlowSubsystem.h"
#include "Math/RandomStream.h"
#include "Subsystems/CodexEventBus.h"
#include "Templates/TypeHash.h"

namespace
{
	float WrapHour(float Hour)
	{
		Hour = FMath::Fmod(Hour, 24.f);
		return Hour < 0.f ? Hour + 24.f : Hour;
	}

	/** A non-zero seed (0 means "random" to EnvironmentRules::MakeInitialState). */
	int32 MakeRandomSeed()
	{
		int32 Seed = 0;
		while (Seed == 0)
		{
			Seed = FMath::Rand() ^ (FMath::Rand() << 15);
		}
		return Seed;
	}

	/** The start weather is drawn from a stream DERIVED from Seed, so it never shares its first draw with the NextWeather roll. */
	EWeatherState DrawStartWeather(const FEnvironmentConfig& Config, int32 Seed)
	{
		FRandomStream StartStream(static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x7F4A7C15u)));
		return EnvironmentRules::PickStartWeather(Config, StartStream);
	}

	FEnvironmentState MakeFreshState(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level, int32 RandomSeedIfNeeded, bool bLocked)
	{
		int32 Seed = Level.Seed != 0 ? Level.Seed : RandomSeedIfNeeded;
		if (Seed == 0)
		{
			Seed = MakeRandomSeed();
		}
		const float Hour = WrapHour(Level.bHasStartHour ? Level.StartHour : Config.DefaultStartHour);
		const EWeatherState Weather = Level.bHasWeather ? Level.Weather : DrawStartWeather(Config, Seed);
		FEnvironmentState State = EnvironmentRules::MakeInitialState(Config, Hour, Weather, Seed);
		State.bLocked = bLocked;
		State.SnowAmount = EnvironmentRules::GetInitialSnowCover(Config, Level);
		return State;
	}

	/**
	 * Command-line start conditions of an UNLOCKED level: -EnvWeather=<name> / -EnvHour=<h> / -EnvSeed=<n> set them; a smoke
	 * (-ExecCmds=CodexTactics.*) or a bot run (-CodexBot) without them starts NEUTRAL (Clear at 15:00 = Afternoon: cold x1, the roster
	 * as written), so the wave / cold smokes stay deterministic; -EnvRandom keeps the procedural start there.
	 */
	void ApplyCommandLineStart(FEnvironmentLevelSettings& Level)
	{
		const TCHAR* CommandLine = FCommandLine::Get();
		FString WeatherName;
		if (FParse::Value(CommandLine, TEXT("-EnvWeather="), WeatherName))
		{
			EWeatherState Weather;
			if (EnvironmentRules::WeatherFromString(WeatherName, Weather))
			{
				Level.bHasWeather = true;
				Level.Weather = Weather;
			}
		}
		float Hour = 0.f;
		if (FParse::Value(CommandLine, TEXT("-EnvHour="), Hour))
		{
			Level.bHasStartHour = true;
			Level.StartHour = Hour;
		}
		FParse::Value(CommandLine, TEXT("-EnvSeed="), Level.Seed);
		float Snow = 0.f;
		if (FParse::Value(CommandLine, TEXT("-EnvSnow="), Snow))
		{
			Level.bHasSnowCover = true;
			Level.SnowCover = FMath::Clamp(Snow, 0.f, 1.f);
		}
		const bool bAutomated = FString(CommandLine).Contains(TEXT("-ExecCmds=CodexTactics.")) || FParse::Param(CommandLine, TEXT("CodexBot"));
		if (bAutomated && !FParse::Param(CommandLine, TEXT("EnvRandom")))
		{
			if (!Level.bHasWeather)
			{
				Level.bHasWeather = true;
				Level.Weather = EWeatherState::Clear;
			}
			if (!Level.bHasStartHour)
			{
				Level.bHasStartHour = true;
				Level.StartHour = 15.f;
			}
			if (Level.Seed == 0)
			{
				Level.Seed = 1;
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------------
// UEnvironmentCarrySubsystem

UEnvironmentCarrySubsystem* UEnvironmentCarrySubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UEnvironmentCarrySubsystem>() : nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------
// UEnvironmentSubsystem

bool UEnvironmentSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

UEnvironmentSubsystem* UEnvironmentSubsystem::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UEnvironmentSubsystem>() : nullptr;
}

void UEnvironmentSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// The tuning file once per process (every later world keeps the values; Set / the dump command may change them meanwhile).
	static bool bTuningApplied = false;
	if (!bTuningApplied)
	{
		bTuningApplied = true;
		const FString Path = EnvironmentTuning::GetDefaultPath();
		UE_LOG(LogCodexTactics, Log, TEXT("[Env] tuning %s: %s"), *Path, EnvironmentTuning::ApplyFile(Path) ? TEXT("applied") : TEXT("missing / unreadable, defaults kept"));
	}
	InitialiseFromWorld(InWorld);
}

void UEnvironmentSubsystem::Deinitialize()
{
	HandOverToNextLevel();
	Super::Deinitialize();
}

TStatId UEnvironmentSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UEnvironmentSubsystem, STATGROUP_Tickables);
}

const FEnvironmentConfig& UEnvironmentSubsystem::GetConfig() const
{
	return EnvironmentTuning::Get();
}

FEnvironmentState UEnvironmentSubsystem::ResolveInitialState(const FEnvironmentConfig& Config, const FEnvironmentLevelSettings& Level,
	const TOptional<FEnvironmentState>& Carried, int32 RandomSeedIfNeeded)
{
	if (Level.bLocked)
	{
		// The tutorial: its own fixed hour / weather, nothing moves. (It still hands its end state to the next level.)
		return MakeFreshState(Config, Level, RandomSeedIfNeeded, true);
	}
	if (Carried.IsSet())
	{
		// User decision 17: continue the previous level exactly - its end hour, weather, blend, dwell and RNG - but unlocked.
		FEnvironmentState State = Carried.GetValue();
		State.Hour = WrapHour(State.Hour);
		State.bLocked = false;
		return State;
	}
	return MakeFreshState(Config, Level, RandomSeedIfNeeded, false);
}

void UEnvironmentSubsystem::InitialiseFromWorld(UWorld& World)
{
	// Only the project's game mode has level data (and a menu map must neither run nor hand over a clock).
	ACodexTacticsGameMode* GameMode = World.GetAuthGameMode<ACodexTacticsGameMode>();
	if (!GameMode)
	{
		UE_LOG(LogCodexTactics, Verbose, TEXT("[Env] %s: no CodexTactics game mode, environment inactive"), *World.GetMapName());
		return;
	}
	const ULevelConfigAsset* Level = GameMode->PeekLevelConfig();
	LevelSettings = Level ? Level->Config.Environment : FEnvironmentLevelSettings();
	Resolve();
	bInitialised = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] %s start: %s"), *World.GetMapName(), *Describe());
	OnStateSet.Broadcast(State);
	// Phase 3: the visual presenter (never placed in a map; visual only, never read by gameplay).
	Presenter = AWeatherPresenterActor::SpawnForWorld(World, LevelSettings.bVisuals);
}

void UEnvironmentSubsystem::Resolve()
{
	const UEnvironmentCarrySubsystem* Carry = UEnvironmentCarrySubsystem::Get(this);
	const TOptional<FEnvironmentState> Carried = Carry ? Carry->GetCarriedState() : TOptional<FEnvironmentState>();
	FEnvironmentLevelSettings StartSettings = LevelSettings;
	if (!StartSettings.bLocked)
	{
		ApplyCommandLineStart(StartSettings);
	}
	// RandomSeedIfNeeded 0: a random seed is drawn (FMath::Rand) ONLY when the start needs one, so a fixed start (smokes, the bot, a
	// level seed) leaves the global random sequence of -FixedSeed runs untouched.
	State = ResolveInitialState(GetConfig(), StartSettings, Carried, 0);
	if (Carried.IsSet() && !LevelSettings.bLocked)
	{
		UE_LOG(LogCodexTactics, Log, TEXT("[Env] continuing the previous level's environment (hour %.2f, %s)"), Carried->Hour,
			*EnvironmentRules::WeatherToString(Carried->Weather));
	}
}

void UEnvironmentSubsystem::ApplyLevelSettings(const FEnvironmentLevelSettings& Settings)
{
	LevelSettings = Settings;
	if (bClockRan)
	{
		UE_LOG(LogCodexTactics, Warning, TEXT("[Env] level settings arrived after the clock ran; kept %s"), *Describe());
		return;
	}
	const FEnvironmentState Old = State;
	Resolve();
	const bool bWasInitialised = bInitialised;
	bInitialised = true;
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] level settings applied: %s"), *Describe());
	if (bWasInitialised)
	{
		BroadcastDifferences(Old, true);
	}
	else
	{
		OnStateSet.Broadcast(State);
	}
}

ETimeOfDayPhase UEnvironmentSubsystem::GetPhase() const
{
	return EnvironmentRules::GetPhase(GetConfig(), State.Hour);
}

float UEnvironmentSubsystem::GetTransitionBlend() const
{
	return EnvironmentRules::GetTransitionBlend(GetConfig(), State);
}

bool UEnvironmentSubsystem::CanAdvance() const
{
	if (State.bLocked)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const UGameFlowSubsystem* Flow = World ? World->GetSubsystem<UGameFlowSubsystem>() : nullptr;
	return EnvironmentRules::ShouldAdvance(Flow ? Flow->GetCombatMode() : ECodexCombatMode::None, UWorldAIPauseSubsystem::IsPausedIn(World));
}

void UEnvironmentSubsystem::Tick(float DeltaTime)
{
	if (!bInitialised || DeltaTime <= 0.f || ClockScale <= 0.f || !CanAdvance())
	{
		return;
	}
	const float Delta = DeltaTime * ClockScale;
	const FEnvironmentConfig& Config = GetConfig();
	const ETimeOfDayPhase OldPhase = EnvironmentRules::GetPhase(Config, State.Hour);
	// The hour first: a dwell that ends in this step is rolled for the phase the clock is in now.
	State.Hour = EnvironmentRules::AdvanceClock(Config, State.Hour, Delta);
	const FWeatherStepResult Step = EnvironmentRules::StepWeather(Config, State, Delta);
	// Phase 4: the snow cover grows with the weather now in force (blended); it never recedes.
	EnvironmentRules::StepSnowCover(Config, State, Delta);
	bClockRan = true;
	AdvancedSeconds += Delta;

	const ETimeOfDayPhase NewPhase = EnvironmentRules::GetPhase(Config, State.Hour);
	if (NewPhase != OldPhase)
	{
		BroadcastPhaseChanged(OldPhase, NewPhase);
	}
	if (Step.bWeatherChanged)
	{
		BroadcastWeatherChanged(Step.OldWeather, State.Weather);
	}
	if (Step.bTransitionStarted && (!Step.bWeatherChanged || GetTransitionBlend() > 0.f))
	{
		BroadcastTransitionStarted(State.Weather, State.NextWeather);
	}
}

void UEnvironmentSubsystem::SetState(const FEnvironmentState& NewState)
{
	const FEnvironmentState Old = State;
	State = NewState;
	State.Hour = WrapHour(State.Hour);
	State.SnowAmount = FMath::Clamp(State.SnowAmount, 0.f, 1.f);
	bInitialised = true;
	UE_LOG(LogCodexTactics, Log, TEXT("[Env] state set: %s"), *Describe());
	BroadcastDifferences(Old, true);
}

void UEnvironmentSubsystem::ForceWeather(EWeatherState Weather, bool bInstant)
{
	const FEnvironmentConfig& Config = GetConfig();
	const FEnvironmentState Old = State;
	// A locked state never steps (EnvironmentRules::StepWeather), so a blend could never end there: force it at once.
	if (bInstant || State.bLocked)
	{
		FRandomStream Stream(State.RandomSeed);
		State.Weather = Weather;
		State.NextWeather = EnvironmentRules::PickNextWeather(Config, Weather, Stream);
		State.WeatherElapsedSeconds = 0.f;
		State.WeatherDwellSeconds = EnvironmentRules::GetWeatherDwellSeconds(Config, GetPhase());
		State.RandomSeed = Stream.GetCurrentSeed();
		UE_LOG(LogCodexTactics, Display, TEXT("[Env] weather forced to %s: %s"), *EnvironmentRules::WeatherToString(Weather), *Describe());
		BroadcastDifferences(Old, true);
		return;
	}
	if (Weather == State.Weather)
	{
		// Already in force: a fresh dwell of it (cancels a running blend toward NextWeather).
		State.WeatherElapsedSeconds = 0.f;
		OnStateSet.Broadcast(State);
		return;
	}
	State.NextWeather = Weather;
	State.WeatherElapsedSeconds = FMath::Max(0.f, State.WeatherDwellSeconds - EnvironmentRules::GetTransitionSeconds(Config, State.WeatherDwellSeconds));
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] weather blending to %s: %s"), *EnvironmentRules::WeatherToString(Weather), *Describe());
	OnStateSet.Broadcast(State);
	BroadcastTransitionStarted(State.Weather, State.NextWeather);
}

void UEnvironmentSubsystem::SetHour(float Hour)
{
	const FEnvironmentState Old = State;
	State.Hour = WrapHour(Hour);
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] hour set: %s"), *Describe());
	BroadcastDifferences(Old, true);
}

void UEnvironmentSubsystem::SetClockScale(float Scale)
{
	ClockScale = FMath::Max(0.f, Scale);
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] clock scale x%.2f"), ClockScale);
}

void UEnvironmentSubsystem::RestartWeatherSequence(int32 Seed)
{
	if (Seed == 0)
	{
		Seed = MakeRandomSeed();
	}
	const FEnvironmentConfig& Config = GetConfig();
	const FEnvironmentState Old = State;
	State = EnvironmentRules::MakeInitialState(Config, State.Hour, DrawStartWeather(Config, Seed), Seed);
	State.bLocked = Old.bLocked;
	State.SnowAmount = Old.SnowAmount; // a new weather sequence never clears the snow already lying
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] weather sequence restarted with seed %d: %s"), Seed, *Describe());
	BroadcastDifferences(Old, true);
}

float UEnvironmentSubsystem::GetWindStrength() const
{
	if (!bInitialised)
	{
		return 0.f;
	}
	if (const AWeatherPresenterActor* Visuals = Presenter.Get())
	{
		return EnvironmentVisualRules::GetBlendedWindStrength(Visuals->GetPreset(State.Weather), Visuals->GetPreset(State.NextWeather), GetTransitionBlend());
	}
	return EnvironmentVisualRules::GetBlendedWindStrength(EnvironmentVisualRules::GetDefaultPreset(State.Weather),
		EnvironmentVisualRules::GetDefaultPreset(State.NextWeather), GetTransitionBlend());
}

float UEnvironmentSubsystem::GetWeatherAmount(EWeatherState Weather) const
{
	return bInitialised ? EnvironmentRules::GetWeatherAmount(GetConfig(), State, Weather) : 0.f;
}

void UEnvironmentSubsystem::SetSnowAmount(float Amount)
{
	State.SnowAmount = FMath::Clamp(Amount, 0.f, 1.f);
	UE_LOG(LogCodexTactics, Log, TEXT("[Env] snow cover set to %.3f"), State.SnowAmount);
	OnStateSet.Broadcast(State);
}

FString UEnvironmentSubsystem::Describe() const
{
	return FString::Printf(TEXT("hour %.2f (%s), weather %s -> %s, blend %.2f, dwell %.0f/%.0f s, seed %d%s, snow %.3f, clock x%.2f%s"),
		State.Hour, *EnvironmentRules::PhaseToString(GetPhase()), *EnvironmentRules::WeatherToString(State.Weather),
		*EnvironmentRules::WeatherToString(State.NextWeather), GetTransitionBlend(), State.WeatherElapsedSeconds, State.WeatherDwellSeconds,
		State.RandomSeed, State.bLocked ? TEXT(", LOCKED") : TEXT(""), State.SnowAmount, ClockScale, bInitialised ? TEXT("") : TEXT(" (not initialised)"));
}

void UEnvironmentSubsystem::BroadcastDifferences(const FEnvironmentState& Old, bool bStateSet)
{
	const FEnvironmentConfig& Config = GetConfig();
	const ETimeOfDayPhase OldPhase = EnvironmentRules::GetPhase(Config, Old.Hour);
	const ETimeOfDayPhase NewPhase = EnvironmentRules::GetPhase(Config, State.Hour);
	if (bStateSet)
	{
		OnStateSet.Broadcast(State);
	}
	if (OldPhase != NewPhase)
	{
		BroadcastPhaseChanged(OldPhase, NewPhase);
	}
	if (Old.Weather != State.Weather)
	{
		BroadcastWeatherChanged(Old.Weather, State.Weather);
	}
}

void UEnvironmentSubsystem::BroadcastPhaseChanged(ETimeOfDayPhase OldPhase, ETimeOfDayPhase NewPhase)
{
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] phase %s -> %s at hour %.2f"), *EnvironmentRules::PhaseToString(OldPhase),
		*EnvironmentRules::PhaseToString(NewPhase), State.Hour);
	OnPhaseChanged.Broadcast(OldPhase, NewPhase);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnTimeOfDayPhaseChanged.Broadcast(OldPhase, NewPhase);
	}
}

void UEnvironmentSubsystem::BroadcastTransitionStarted(EWeatherState From, EWeatherState To)
{
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] weather blend %s -> %s starts at hour %.2f"), *EnvironmentRules::WeatherToString(From),
		*EnvironmentRules::WeatherToString(To), State.Hour);
	OnWeatherTransitionStarted.Broadcast(From, To);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnWeatherTransitionStarted.Broadcast(From, To);
	}
}

void UEnvironmentSubsystem::BroadcastWeatherChanged(EWeatherState OldWeather, EWeatherState NewWeather)
{
	UE_LOG(LogCodexTactics, Display, TEXT("[Env] weather %s -> %s at hour %.2f (next %s, dwell %.0f s)"), *EnvironmentRules::WeatherToString(OldWeather),
		*EnvironmentRules::WeatherToString(NewWeather), State.Hour, *EnvironmentRules::WeatherToString(State.NextWeather), State.WeatherDwellSeconds);
	OnWeatherChanged.Broadcast(OldWeather, NewWeather);
	if (UCodexEventBus* Bus = UCodexEventBus::Get(this))
	{
		Bus->OnWeatherChanged.Broadcast(OldWeather, NewWeather);
	}
}

void UEnvironmentSubsystem::HandOverToNextLevel()
{
	// Only a level that resolved a state (a game world with the project's game mode) hands it on; a locked level too.
	if (!bInitialised || bHandedOver)
	{
		return;
	}
	bHandedOver = true;
	if (UEnvironmentCarrySubsystem* Carry = UEnvironmentCarrySubsystem::Get(this))
	{
		Carry->SetCarriedState(State);
		UE_LOG(LogCodexTactics, Log, TEXT("[Env] end state handed to the next level: %s"), *Describe());
	}
}
