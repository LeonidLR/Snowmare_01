// Dev-only console commands for headless checks of the day cycle + weather (UEnvironmentSubsystem, UE-only, no Godot
// reference; docs/systems/environment-weather.md, phase 1):
//   Scripts/smoke.ps1 -Command CodexTactics.EnvironmentSmoke     -Map /Game/Maps/L_MovementTest
//   Scripts/smoke.ps1 -Command CodexTactics.EnvironmentLakeSmoke -Map /Game/Maps/L_Demo01_A_FrozenLake
// EnvironmentSmoke (unlocked level, fresh state):
//   1. the subsystem is initialised, not locked, the phase matches the hour;
//   2. the clock runs: at ClockScale 20 the hour advanced as EnvironmentRules::AdvanceClock says, the weather dwell grew;
//   3. the world AI pause (UWorldAIPauseSubsystem blocker) freezes the hour and the weather; releasing it resumes both;
//   4. SetHour across a phase boundary -> OnTimeOfDayPhaseChanged (none inside a phase, none over midnight in Night);
//      ForceWeather(neighbour, instant) -> OnWeatherChanged; ForceWeather(neighbour, blend) -> OnWeatherTransitionStarted,
//      the blend rises and the weather in force flips only at its end (OnWeatherChanged);
//   5. ladder: at a high ClockScale three weather changes in a row, every pair (and every blend) between neighbours.
// EnvironmentLakeSmoke (Demo 01 lake, "environment": locked, start_hour 23, Snowfall, snow_cover 0.6): hour 23 (Night) + Snowfall +
// snow cover 0.6, and none of them moves with ClockScale 20 and ~2 s of ticking.
// Phase 4 snow cover in EnvironmentSmoke: the neutral start (Clear) holds the cover while the clock runs (2), the AI pause freezes it (3).
// Both print "[EnvSmoke] PASS/FAIL ..." lines and the usual "Smoke RESULT: PASS|FAIL", then exit.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "AI/WorldAIPauseSubsystem.h"
#include "CodexTactics.h"
#include "Containers/Ticker.h"
#include "Debug/EnvironmentSmokeProbe.h"
#include "Engine/World.h"
#include "Environment/EnvironmentRules.h"
#include "Environment/EnvironmentSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Subsystems/CodexEventBus.h"
#include "UObject/StrongObjectPtr.h"

namespace EnvironmentSmoke
{
	constexpr float StepSeconds = 0.05f;
	/** Smoke time (game seconds) to let the level settle before the first check. */
	constexpr float WarmupSeconds = 1.f;
	/** Ticker seconds of the clock run (stage 2 and the lake smoke). */
	constexpr float ClockRunSeconds = 2.f;
	constexpr float DebugClockScale = 20.f;
	/** The blend of stage 4 / the ladder of stage 5: dwell 225 s, blend 45 s -> well under a second each. */
	constexpr float BlendClockScale = 60.f;
	constexpr float LadderClockScale = 200.f;
	constexpr int32 LadderChanges = 3;
	constexpr float StageTimeoutSeconds = 20.f;
	constexpr float HourTolerance = 0.02f;
	const FName PauseReason(TEXT("EnvironmentSmoke"));

	enum class EStage : uint8
	{
		Warmup,
		Clock,
		Frozen,
		Resumed,
		Blend,
		Ladder
	};

	struct FState
	{
		EStage Stage = EStage::Warmup;
		/** Game seconds (UWorld::GetTimeSeconds, the clock the subsystem ticks with - the core ticker's delta is not) in this stage / in all. */
		float StageTime = 0.f;
		float TotalTime = 0.f;
		float StageStart = 0.f;
		float RunStart = -1.f;
		int32 Failures = 0;
		bool bDone = false;
		TStrongObjectPtr<UEnvironmentSmokeProbe> Probe;

		// Values noted at the start of a stage.
		float StartHour = 0.f;
		float StartAdvanced = 0.f;
		float StartElapsed = 0.f;
		float StartSnow = 0.f;
		EWeatherState BlendFrom = EWeatherState::Clear;
		EWeatherState BlendTo = EWeatherState::Overcast;
		float MaxBlend = 0.f;
		bool bFlippedEarly = false;
	};

	void Check(FState& State, bool bOk, const FString& What)
	{
		UE_LOG(LogCodexTactics, Display, TEXT("[EnvSmoke] %s: %s"), bOk ? TEXT("PASS") : TEXT("FAIL"), *What);
		State.Failures += bOk ? 0 : 1;
	}

	void Finish(FState& State, UWorld* World)
	{
		if (State.Probe.IsValid())
		{
			if (UCodexEventBus* Bus = UCodexEventBus::Get(World))
			{
				State.Probe->StopListening(Bus);
			}
		}
		State.bDone = true;
		const bool bPass = State.Failures == 0;
		UE_LOG(LogCodexTactics, Display, TEXT("[EnvSmoke] RESULT %s (%d failed checks)"), bPass ? TEXT("PASS") : TEXT("FAIL"), State.Failures);
		UE_LOG(LogCodexTactics, Display, TEXT("Smoke RESULT: %s"), bPass ? TEXT("PASS") : TEXT("FAIL"));
		FPlatformMisc::RequestExit(false, TEXT("EnvironmentSmoke"));
	}

	/** Game time of the running smoke: sets StageTime / TotalTime from World's clock (the first call starts the run). */
	void UpdateTime(FState& State, const UWorld& World)
	{
		const float Now = World.GetTimeSeconds();
		if (State.RunStart < 0.f)
		{
			State.RunStart = Now;
			State.StageStart = Now;
		}
		State.StageTime = Now - State.StageStart;
		State.TotalTime = Now - State.RunStart;
	}

	void NextStage(FState& State, EStage Stage)
	{
		State.Stage = Stage;
		State.StageStart += State.StageTime;
		State.StageTime = 0.f;
	}

	/** Hours from From forward to To on the 24 h clock, in [0, 24). */
	float ForwardHours(float From, float To)
	{
		return FMath::Fmod(To - From + 48.f, 24.f);
	}

	/** True when A and B are the same clock time within Tolerance (wrap-safe). */
	bool SameHour(float A, float B, float Tolerance = HourTolerance)
	{
		const float Delta = ForwardHours(A, B);
		return Delta <= Tolerance || Delta >= 24.f - Tolerance;
	}

	FString Hours(float Hour)
	{
		return FString::Printf(TEXT("%.3f"), Hour);
	}

	FString Pair(EWeatherState A, EWeatherState B)
	{
		return FString::Printf(TEXT("%s -> %s"), *EnvironmentRules::WeatherToString(A), *EnvironmentRules::WeatherToString(B));
	}

	/** A ladder neighbour of Weather (one step up, or down at the top). */
	EWeatherState NeighbourOf(EWeatherState Weather)
	{
		return Weather == EWeatherState::Blizzard ? EWeatherState::Snowfall : static_cast<EWeatherState>(static_cast<uint8>(Weather) + 1);
	}

	/** The other ladder neighbour of Weather (one step down, or up at the bottom). */
	EWeatherState OtherNeighbourOf(EWeatherState Weather)
	{
		return Weather == EWeatherState::Clear ? EWeatherState::Overcast : static_cast<EWeatherState>(static_cast<uint8>(Weather) - 1);
	}

	/** Stage 1: the subsystem state on a fresh unlocked level. */
	void CheckInitialState(FState& State, UEnvironmentSubsystem& Env)
	{
		const FEnvironmentConfig& Config = Env.GetConfig();
		UE_LOG(LogCodexTactics, Display, TEXT("[EnvSmoke] start state: %s"), *Env.Describe());
		Check(State, Env.IsInitialised(), TEXT("1 subsystem initialised"));
		Check(State, !Env.IsLocked(), TEXT("1 unlocked on this level"));
		Check(State, Env.GetHour() >= 0.f && Env.GetHour() < 24.f, FString::Printf(TEXT("1 hour %s inside [0, 24)"), *Hours(Env.GetHour())));
		Check(State, Env.GetPhase() == EnvironmentRules::GetPhase(Config, Env.GetHour()),
			FString::Printf(TEXT("1 phase %s matches hour %s"), *EnvironmentRules::PhaseToString(Env.GetPhase()), *Hours(Env.GetHour())));
		Check(State, EnvironmentRules::AreNeighbours(Env.GetWeather(), Env.GetNextWeather()),
			FString::Printf(TEXT("1 next weather is a ladder neighbour (%s)"), *Pair(Env.GetWeather(), Env.GetNextWeather())));
		Check(State, Env.CanAdvance(), TEXT("1 the clock may advance (no combat, no AI pause)"));
		Check(State, FMath::IsNearlyEqual(Env.GetClockScale(), 1.f), TEXT("1 design clock speed x1 by default"));
	}

	/** Stage 2 result: the hour followed AdvanceClock for the game seconds the subsystem ran, and those match the world clock. */
	void CheckClockRan(FState& State, const UEnvironmentSubsystem& Env)
	{
		const FEnvironmentConfig& Config = Env.GetConfig();
		const float RanSeconds = Env.GetAdvancedSeconds() - State.StartAdvanced;
		const float ExpectedSeconds = State.StageTime * DebugClockScale;
		Check(State, Env.HasClockRun() && RanSeconds > 0.f, FString::Printf(TEXT("2 the clock ran (%.1f game s at x%.0f)"), RanSeconds, DebugClockScale));
		Check(State, FMath::Abs(RanSeconds - ExpectedSeconds) <= 0.3f * ExpectedSeconds,
			FString::Printf(TEXT("2 %.1f game s ran in %.2f game s x%.0f (expected ~%.1f, +-30 %%)"), RanSeconds, State.StageTime, DebugClockScale, ExpectedSeconds));
		const float ExpectedHour = EnvironmentRules::AdvanceClock(Config, State.StartHour, RanSeconds);
		Check(State, SameHour(Env.GetHour(), ExpectedHour),
			FString::Printf(TEXT("2 hour %s -> %s, AdvanceClock says %s"), *Hours(State.StartHour), *Hours(Env.GetHour()), *Hours(ExpectedHour)));
		Check(State, ForwardHours(State.StartHour, Env.GetHour()) > 0.05f && ForwardHours(State.StartHour, Env.GetHour()) < 12.f,
			FString::Printf(TEXT("2 the hour moved forward by %s h"), *Hours(ForwardHours(State.StartHour, Env.GetHour()))));
		Check(State, Env.GetStateRef().WeatherElapsedSeconds > State.StartElapsed || Env.GetStateRef().WeatherElapsedSeconds < State.StartElapsed - 1.f,
			FString::Printf(TEXT("2 weather dwell grew (%.1f -> %.1f s)"), State.StartElapsed, Env.GetStateRef().WeatherElapsedSeconds));
		const FEnvironmentState& Now = Env.GetStateRef();
		if (EnvironmentRules::GetSnowCoverRate(Config, Now.Weather) == 0.f && EnvironmentRules::GetSnowCoverRate(Config, Now.NextWeather) == 0.f)
		{
			Check(State, Now.SnowAmount == State.StartSnow, FString::Printf(TEXT("2 %s: the snow cover holds at %.3f (never recedes, no growth)"),
				*EnvironmentRules::WeatherToString(Now.Weather), Now.SnowAmount));
		}
	}

	/** Stage 4, instant part: events fired by SetHour and ForceWeather(instant). Leaves a blend toward BlendTo running. */
	void RunEventChecks(FState& State, UEnvironmentSubsystem& Env)
	{
		UEnvironmentSmokeProbe& Probe = *State.Probe;
		const FEnvironmentConfig& Config = Env.GetConfig();
		Env.SetClockScale(0.f); // nothing but the calls below changes the state now

		// --- Phase events: across the Midday -> Afternoon boundary, none inside a phase, none over midnight in Night. ---
		const float Boundary = EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::Afternoon);
		Env.SetHour(Boundary - 0.5f);
		Probe.Reset();
		Env.SetHour(Boundary + 0.1f);
		Check(State, Env.GetPhase() == ETimeOfDayPhase::Afternoon, FString::Printf(TEXT("4 SetHour(%s) is Afternoon"), *Hours(Boundary + 0.1f)));
		Check(State, Probe.Phases.Num() == 1 && Probe.Phases[0].Old == ETimeOfDayPhase::Midday && Probe.Phases[0].New == ETimeOfDayPhase::Afternoon,
			FString::Printf(TEXT("4 OnTimeOfDayPhaseChanged Midday -> Afternoon fired once (events: %d)"), Probe.Phases.Num()));
		Probe.Reset();
		Env.SetHour(Boundary + 0.5f);
		Check(State, Probe.Phases.Num() == 0, TEXT("4 no phase event for SetHour inside the same phase"));
		Env.SetHour(EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::Night) + 0.2f);
		Probe.Reset();
		Env.SetHour(23.9f);
		Env.SetHour(0.1f);
		Check(State, Env.GetPhase() == ETimeOfDayPhase::Night && Probe.Phases.Num() == 0,
			TEXT("4 23.9 -> 0.1 stays Night: no phase event over midnight"));
		Env.SetHour(EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::EarlyMorning) - 0.1f);
		Probe.Reset();
		Env.SetHour(EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::EarlyMorning) + 0.1f);
		Check(State, Probe.Phases.Num() == 1 && Probe.Phases[0].Old == ETimeOfDayPhase::Night && Probe.Phases[0].New == ETimeOfDayPhase::EarlyMorning,
			TEXT("4 OnTimeOfDayPhaseChanged Night -> EarlyMorning fired once"));
		// A hour in the middle of the next phase keeps the later blend / ladder inside one phase at the start.
		Env.SetHour(EnvironmentRules::GetPhaseStartHour(Config, ETimeOfDayPhase::Midday) + 0.5f);

		// --- ForceWeather instant: OnWeatherChanged, no transition. ---
		const EWeatherState W0 = Env.GetWeather();
		const EWeatherState Instant = NeighbourOf(W0);
		Probe.Reset();
		Env.ForceWeather(Instant, true);
		Check(State, Env.GetWeather() == Instant, FString::Printf(TEXT("4 ForceWeather(%s, instant): in force at once"), *EnvironmentRules::WeatherToString(Instant)));
		Check(State, Probe.Changes.Num() == 1 && Probe.Changes[0].Old == W0 && Probe.Changes[0].New == Instant,
			FString::Printf(TEXT("4 OnWeatherChanged %s fired once (events: %d)"), *Pair(W0, Instant), Probe.Changes.Num()));
		Check(State, Probe.Transitions.Num() == 0, TEXT("4 an instant force starts no blend"));
		Check(State, EnvironmentRules::AreNeighbours(Env.GetWeather(), Env.GetNextWeather()), TEXT("4 a fresh neighbour was rolled as next weather"));

		// --- ForceWeather blend: OnWeatherTransitionStarted now, OnWeatherChanged only at its end. ---
		State.BlendFrom = Instant;
		State.BlendTo = OtherNeighbourOf(Instant);
		Probe.Reset();
		Env.ForceWeather(State.BlendTo, false);
		Check(State, Probe.Transitions.Num() == 1 && Probe.Transitions[0].Old == State.BlendFrom && Probe.Transitions[0].New == State.BlendTo,
			FString::Printf(TEXT("4 OnWeatherTransitionStarted %s fired once (events: %d)"), *Pair(State.BlendFrom, State.BlendTo), Probe.Transitions.Num()));
		Check(State, Probe.Changes.Num() == 0 && Env.GetWeather() == State.BlendFrom && Env.GetNextWeather() == State.BlendTo,
			TEXT("4 the blend started: the old weather is still in force, next weather is the forced one"));
		State.MaxBlend = 0.f;
		State.bFlippedEarly = false;
		Env.SetClockScale(BlendClockScale);
	}

	/** Stage 4, blend part: the clock runs; true when the blend ended (or timed out). */
	bool StepBlend(FState& State, UEnvironmentSubsystem& Env)
	{
		UEnvironmentSmokeProbe& Probe = *State.Probe;
		State.MaxBlend = FMath::Max(State.MaxBlend, Env.GetTransitionBlend());
		if (Probe.Changes.Num() == 0)
		{
			State.bFlippedEarly |= Env.GetWeather() != State.BlendFrom;
			if (State.StageTime < StageTimeoutSeconds)
			{
				return false;
			}
		}
		Check(State, Probe.Changes.Num() == 1 && Probe.Changes[0].Old == State.BlendFrom && Probe.Changes[0].New == State.BlendTo,
			FString::Printf(TEXT("4 the blend ended with OnWeatherChanged %s (events: %d, after %.2f s)"), *Pair(State.BlendFrom, State.BlendTo), Probe.Changes.Num(), State.StageTime));
		Check(State, Env.GetWeather() == State.BlendTo && !State.bFlippedEarly, TEXT("4 the new weather came into force only at the end of the blend"));
		Check(State, State.MaxBlend > 0.3f, FString::Printf(TEXT("4 the blend rose while it ran (max %.2f)"), State.MaxBlend));
		Check(State, Env.GetTransitionBlend() < 0.3f && EnvironmentRules::AreNeighbours(Env.GetWeather(), Env.GetNextWeather()),
			FString::Printf(TEXT("4 a new dwell began (blend %.2f, next %s)"), Env.GetTransitionBlend(), *EnvironmentRules::WeatherToString(Env.GetNextWeather())));
		return true;
	}

	/** Stage 5: true when the ladder run ended. */
	bool StepLadder(FState& State, UEnvironmentSubsystem& Env)
	{
		const UEnvironmentSmokeProbe& Probe = *State.Probe;
		if (Probe.Changes.Num() < LadderChanges && State.StageTime < StageTimeoutSeconds)
		{
			return false;
		}
		Check(State, Probe.Changes.Num() >= LadderChanges, FString::Printf(TEXT("5 %d weather changes in %.2f game s at x%.0f (wanted %d)"),
			Probe.Changes.Num(), State.StageTime, LadderClockScale, LadderChanges));
		bool bAllNeighbours = true;
		bool bChained = true;
		FString Chain;
		for (int32 Index = 0; Index < Probe.Changes.Num(); ++Index)
		{
			const TEnvEventPair<EWeatherState>& Change = Probe.Changes[Index];
			bAllNeighbours &= EnvironmentRules::AreNeighbours(Change.Old, Change.New);
			bChained &= Index == 0 ? Change.Old == State.BlendTo : Change.Old == Probe.Changes[Index - 1].New;
			Chain += (Chain.IsEmpty() ? FString() : FString(TEXT(" | "))) + Pair(Change.Old, Change.New);
		}
		UE_LOG(LogCodexTactics, Display, TEXT("[EnvSmoke] ladder: %s"), *Chain);
		Check(State, bAllNeighbours, TEXT("5 every OldWeather/NewWeather pair are ladder neighbours"));
		Check(State, bChained, TEXT("5 every change starts from the weather the previous one ended in"));
		bool bTransitionsMatch = Probe.Transitions.Num() >= Probe.Changes.Num();
		for (int32 Index = 0; bTransitionsMatch && Index < Probe.Changes.Num(); ++Index)
		{
			bTransitionsMatch &= Probe.Transitions[Index].Old == Probe.Changes[Index].Old && Probe.Transitions[Index].New == Probe.Changes[Index].New;
		}
		Check(State, bTransitionsMatch, FString::Printf(TEXT("5 every change was announced by its OnWeatherTransitionStarted (%d transitions)"), Probe.Transitions.Num()));
		Check(State, Env.GetHour() >= 0.f && Env.GetHour() < 24.f && Env.GetPhase() == EnvironmentRules::GetPhase(Env.GetConfig(), Env.GetHour()),
			FString::Printf(TEXT("5 the clock stayed valid (hour %s, %s)"), *Hours(Env.GetHour()), *EnvironmentRules::PhaseToString(Env.GetPhase())));
		return true;
	}

	/** One step of EnvironmentSmoke; false ends the ticker. */
	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FState& State)
	{
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		if (State.StageTime < WarmupSeconds && State.Stage == EStage::Warmup)
		{
			return true;
		}
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		UCodexEventBus* Bus = UCodexEventBus::Get(World);
		UWorldAIPauseSubsystem* AIPause = World->GetSubsystem<UWorldAIPauseSubsystem>();
		if (!Env || !Bus || !AIPause || State.TotalTime > 6.f * StageTimeoutSeconds)
		{
			Check(State, false, FString::Printf(TEXT("environment subsystem / event bus / AI pause present (env %d, bus %d, pause %d, t %.1f s)"),
				Env ? 1 : 0, Bus ? 1 : 0, AIPause ? 1 : 0, State.TotalTime));
			Finish(State, World);
			return false;
		}

		switch (State.Stage)
		{
		case EStage::Warmup:
			CheckInitialState(State, *Env);
			if (!State.Probe.IsValid())
			{
				State.Probe.Reset(NewObject<UEnvironmentSmokeProbe>());
				State.Probe->Listen(Bus);
			}
			State.StartHour = Env->GetHour();
			State.StartAdvanced = Env->GetAdvancedSeconds();
			State.StartElapsed = Env->GetStateRef().WeatherElapsedSeconds;
			State.StartSnow = Env->GetSnowAmount();
			Env->SetClockScale(DebugClockScale);
			NextStage(State, EStage::Clock);
			break;

		case EStage::Clock:
			if (State.StageTime >= ClockRunSeconds)
			{
				CheckClockRan(State, *Env);
				// 3. the world AI pause freezes the clock.
				AIPause->AddPauseBlocker(PauseReason);
				// A snowing weather so a frozen cover means the pause, not the weather.
				Env->ForceWeather(EWeatherState::Blizzard, true);
				State.StartSnow = Env->GetSnowAmount();
				State.StartHour = Env->GetHour();
				State.StartAdvanced = Env->GetAdvancedSeconds();
				State.StartElapsed = Env->GetStateRef().WeatherElapsedSeconds;
				NextStage(State, EStage::Frozen);
			}
			break;

		case EStage::Frozen:
			if (State.StageTime >= 1.f)
			{
				Check(State, AIPause->IsWorldAIPaused() && !Env->CanAdvance(), TEXT("3 world AI paused: the environment may not advance"));
				Check(State, FMath::IsNearlyEqual(Env->GetHour(), State.StartHour, 1.e-4f) && Env->GetAdvancedSeconds() == State.StartAdvanced,
					FString::Printf(TEXT("3 the hour is frozen at %s over %.2f s of pause (x%.0f)"), *Hours(Env->GetHour()), State.StageTime, Env->GetClockScale()));
				Check(State, Env->GetStateRef().WeatherElapsedSeconds == State.StartElapsed, TEXT("3 the weather dwell is frozen"));
				Check(State, Env->GetSnowAmount() == State.StartSnow, FString::Printf(TEXT("3 the snow cover is frozen (%.3f)"), Env->GetSnowAmount()));
				AIPause->RemovePauseBlocker(PauseReason);
				Check(State, !AIPause->IsWorldAIPaused() && Env->CanAdvance(), TEXT("3 pause released: the environment may advance again"));
				NextStage(State, EStage::Resumed);
			}
			break;

		case EStage::Resumed:
			if (State.StageTime >= 1.f)
			{
				const float Moved = ForwardHours(State.StartHour, Env->GetHour());
				const float RanSeconds = Env->GetAdvancedSeconds() - State.StartAdvanced;
				Check(State, Moved > 0.05f && Moved < 12.f && RanSeconds > 0.f,
					FString::Printf(TEXT("3 the hour resumed after the pause (+%s h, %.1f game s)"), *Hours(Moved), RanSeconds));
				Check(State, SameHour(Env->GetHour(), EnvironmentRules::AdvanceClock(Env->GetConfig(), State.StartHour, RanSeconds)),
					TEXT("3 the resumed hour follows AdvanceClock"));
				Check(State, Env->GetStateRef().WeatherElapsedSeconds != State.StartElapsed, TEXT("3 the weather dwell resumed"));
				Check(State, Env->GetSnowAmount() > State.StartSnow || Env->GetSnowAmount() >= 1.f,
					FString::Printf(TEXT("3 Blizzard after the pause: the snow cover grows again (%.3f -> %.3f)"), State.StartSnow, Env->GetSnowAmount()));
				RunEventChecks(State, *Env);
				NextStage(State, EStage::Blend);
			}
			break;

		case EStage::Blend:
			if (StepBlend(State, *Env))
			{
				// 5. ladder
				State.Probe->Reset();
				Env->SetClockScale(LadderClockScale);
				NextStage(State, EStage::Ladder);
			}
			break;

		case EStage::Ladder:
			if (StepLadder(State, *Env))
			{
				Env->SetClockScale(1.f);
				Finish(State, World);
				return false;
			}
			break;
		}
		return true;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FState> State = MakeShared<FState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, State](float)
		{
			return Step(WeakWorld, *State);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnvironmentSmoke"),
		TEXT("Dev check: day clock, AI-pause freeze, phase / weather events on the bus and the weather ladder (unlocked level); logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

namespace EnvironmentLakeSmoke
{
	using namespace EnvironmentSmoke;

	struct FLakeState
	{
		FState Base;
		bool bClockStarted = false;
		float StartHour = 0.f;
	};

	bool Step(TWeakObjectPtr<UWorld> WeakWorld, FLakeState& Lake)
	{
		FState& State = Lake.Base;
		UWorld* World = WeakWorld.Get();
		if (!World)
		{
			return false;
		}
		UpdateTime(State, *World);
		if (State.StageTime < (Lake.bClockStarted ? ClockRunSeconds : WarmupSeconds))
		{
			return true;
		}
		UEnvironmentSubsystem* Env = UEnvironmentSubsystem::Get(World);
		if (!Env)
		{
			Check(State, false, TEXT("lake: environment subsystem present"));
			Finish(State, World);
			return false;
		}
		const FEnvironmentConfig& Config = Env->GetConfig();
		if (!Lake.bClockStarted)
		{
			UE_LOG(LogCodexTactics, Display, TEXT("[EnvSmoke] lake start state: %s"), *Env->Describe());
			Check(State, Env->IsInitialised(), TEXT("L1 subsystem initialised"));
			Check(State, Env->IsLocked(), TEXT("L1 the level is locked"));
			Check(State, SameHour(Env->GetHour(), 23.f, 1.e-3f), FString::Printf(TEXT("L1 hour %s is 23"), *Hours(Env->GetHour())));
			Check(State, Env->GetPhase() == ETimeOfDayPhase::Night && EnvironmentRules::GetPhase(Config, Env->GetHour()) == ETimeOfDayPhase::Night,
				FString::Printf(TEXT("L1 phase %s is Night"), *EnvironmentRules::PhaseToString(Env->GetPhase())));
			Check(State, Env->GetWeather() == EWeatherState::Snowfall, FString::Printf(TEXT("L1 weather %s is Snowfall"), *EnvironmentRules::WeatherToString(Env->GetWeather())));
			Check(State, FMath::IsNearlyEqual(Env->GetSnowAmount(), 0.6f, 1.e-4f), FString::Printf(TEXT("L1 snow cover %.3f is 0.6 (level JSON snow_cover)"), Env->GetSnowAmount()));
			Check(State, !Env->CanAdvance(), TEXT("L1 a locked level may not advance"));
			Lake.StartHour = Env->GetHour();
			State.StartAdvanced = Env->GetAdvancedSeconds();
			State.StartElapsed = Env->GetStateRef().WeatherElapsedSeconds;
			Env->SetClockScale(DebugClockScale);
			Lake.bClockStarted = true;
			NextStage(State, EStage::Clock);
			return true;
		}
		Check(State, Env->GetHour() == Lake.StartHour && Env->GetAdvancedSeconds() == State.StartAdvanced && !Env->HasClockRun(),
			FString::Printf(TEXT("L2 hour still %s after %.2f game s at x%.0f (clock never ran)"), *Hours(Env->GetHour()), State.StageTime, Env->GetClockScale()));
		Check(State, Env->GetWeather() == EWeatherState::Snowfall && Env->GetStateRef().WeatherElapsedSeconds == State.StartElapsed,
			TEXT("L2 weather still Snowfall, its dwell did not grow"));
		Check(State, Env->GetPhase() == ETimeOfDayPhase::Night && Env->IsLocked(), TEXT("L2 still Night and locked"));
		Check(State, FMath::IsNearlyEqual(Env->GetSnowAmount(), 0.6f, 1.e-4f), FString::Printf(TEXT("L2 snow cover still 0.6 (%.3f): a locked level never grows it"), Env->GetSnowAmount()));
		Env->SetClockScale(1.f);
		Finish(State, World);
		return false;
	}

	void Run(const TArray<FString>& Args, UWorld* World)
	{
		if (!World)
		{
			return;
		}
		TWeakObjectPtr<UWorld> WeakWorld(World);
		TSharedRef<FLakeState> Lake = MakeShared<FLakeState>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld, Lake](float)
		{
			return Step(WeakWorld, *Lake);
		}), StepSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs Command(
		TEXT("CodexTactics.EnvironmentLakeSmoke"),
		TEXT("Dev check: the Demo 01 lake is locked at hour 23 (Night) + Snowfall and neither moves at ClockScale 20; logs PASS/FAIL, then exits."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Run));
}

#endif // !UE_BUILD_SHIPPING
